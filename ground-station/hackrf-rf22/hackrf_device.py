#!/usr/bin/env python3
"""Small ctypes binding for one half-duplex HackRF device.

The binding intentionally covers only the libhackrf calls needed by the C3M
ground bridge.  Importing this module neither loads libhackrf nor opens USB;
both happen in :meth:`HackRFDevice.open`.

The callback signatures and ``hackrf_transfer`` layout match the installed
``libhackrf/hackrf.h`` API.  libhackrf callbacks run on its asynchronous USB
thread, so callbacks only copy bytes and signal Python events.  All libhackrf
start/stop calls remain on the caller's thread.
"""

from __future__ import annotations

import ctypes
import ctypes.util
import queue
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterator

from rf_safety import BASELINE_RX_LNA_GAIN_DB, BASELINE_RX_VGA_GAIN_DB


HACKRF_SUCCESS = 0
HACKRF_TRUE = 1
HACKRF_ERROR_STREAMING_THREAD_ERR = -1002
HACKRF_ERROR_STREAMING_STOPPED = -1003
HACKRF_ERROR_STREAMING_EXIT_CALLED = -1004


class HackRFError(RuntimeError):
    """Base class for wrapper failures."""


class HackRFLibraryError(HackRFError):
    """libhackrf could not be loaded."""


class HackRFCallError(HackRFError):
    """A libhackrf function returned an error code."""

    def __init__(self, operation: str, code: int, detail: str) -> None:
        self.operation = operation
        self.code = code
        self.detail = detail
        super().__init__(f"{operation} failed: {detail} ({code})")


class HackRFStateError(HackRFError):
    """An operation was requested in the wrong device state."""


class HackRFTimeoutError(HackRFError):
    """A bounded streaming operation exceeded its timeout."""


class HackRFCallbackError(HackRFError):
    """A Python RX/TX callback failed on libhackrf's USB thread."""


class HackRFCleanupError(HackRFError):
    """An operation failed and one or more cleanup steps also failed."""

    def __init__(
        self,
        context: str,
        cleanup_errors: list[BaseException],
        *,
        primary_error: BaseException | None = None,
    ) -> None:
        if not cleanup_errors:
            raise ValueError("HackRFCleanupError requires at least one cleanup error")
        self.context = context
        self.primary_error = primary_error
        self.cleanup_errors = tuple(cleanup_errors)
        # Preserve the original public aggregate for callers that only need to
        # enumerate every failure in occurrence order.
        self.errors = (
            ((primary_error,) if primary_error is not None else ())
            + self.cleanup_errors
        )
        cleanup_details = "; ".join(str(error) for error in cleanup_errors)
        if primary_error is None:
            details = cleanup_details
        else:
            details = f"primary={primary_error}; cleanup={cleanup_details}"
        super().__init__(f"{context}: {details}")


@dataclass(frozen=True)
class HackRFConfig:
    """C3M bench defaults, with the RF amplifier and bias tee off."""

    center_freq_hz: int = 433_000_000
    sample_rate_hz: int = 8_000_000
    bandwidth_hz: int = 1_750_000
    rx_lna_gain_db: int = BASELINE_RX_LNA_GAIN_DB
    rx_vga_gain_db: int = BASELINE_RX_VGA_GAIN_DB
    tx_vga_gain_db: int = 0
    amp_enabled: bool = False
    antenna_power_enabled: bool = False
    rx_queue_blocks: int = 8
    max_tx_bytes: int = 64 * 1024 * 1024

    def validate(self) -> None:
        if self.center_freq_hz <= 0:
            raise ValueError("center_freq_hz must be positive")
        if not 2_000_000 <= self.sample_rate_hz <= 20_000_000:
            raise ValueError("sample_rate_hz must be in libhackrf's 2..20 MHz range")
        if self.bandwidth_hz <= 0:
            raise ValueError("bandwidth_hz must be positive")
        if self.rx_lna_gain_db not in range(0, 41, 8):
            raise ValueError("rx_lna_gain_db must be 0..40 dB in 8 dB steps")
        if self.rx_vga_gain_db not in range(0, 63, 2):
            raise ValueError("rx_vga_gain_db must be 0..62 dB in 2 dB steps")
        if not 0 <= self.tx_vga_gain_db <= 47:
            raise ValueError("tx_vga_gain_db must be 0..47 dB")
        if self.amp_enabled:
            raise ValueError(
                "RF amplifier enable is intentionally unsupported by this ground adapter"
            )
        if self.antenna_power_enabled:
            raise ValueError(
                "antenna-port DC power is intentionally unsupported by this ground adapter"
            )
        if self.rx_queue_blocks < 1:
            raise ValueError("rx_queue_blocks must be at least one")
        if self.max_tx_bytes < 2:
            raise ValueError("max_tx_bytes must allow at least one I/Q sample")


@dataclass(frozen=True)
class TxResult:
    """Result of a completed bounded TX operation."""

    bytes_sent: int
    elapsed_s: float
    rx_was_resumed: bool


class _HackRFTransfer(ctypes.Structure):
    _fields_ = [
        ("device", ctypes.c_void_p),
        ("buffer", ctypes.POINTER(ctypes.c_uint8)),
        ("buffer_length", ctypes.c_int),
        ("valid_length", ctypes.c_int),
        ("rx_ctx", ctypes.c_void_p),
        ("tx_ctx", ctypes.c_void_p),
    ]


_SampleBlockCallback = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.POINTER(_HackRFTransfer)
)
_FlushCallback = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int)


def _set_signature(function: Any, argtypes: list[Any], restype: Any) -> None:
    """Set ctypes metadata while allowing simple Python fakes in unit tests."""

    try:
        function.argtypes = argtypes
        function.restype = restype
    except AttributeError:
        pass


def _bind_library(library: Any) -> Any:
    device = ctypes.c_void_p
    transfer_callback = _SampleBlockCallback
    flush_callback = _FlushCallback

    signatures = {
        "hackrf_init": ([], ctypes.c_int),
        "hackrf_exit": ([], ctypes.c_int),
        "hackrf_open_by_serial": (
            [ctypes.c_char_p, ctypes.POINTER(device)],
            ctypes.c_int,
        ),
        "hackrf_close": ([device], ctypes.c_int),
        "hackrf_start_rx": (
            [device, transfer_callback, ctypes.c_void_p],
            ctypes.c_int,
        ),
        "hackrf_stop_rx": ([device], ctypes.c_int),
        "hackrf_start_tx": (
            [device, transfer_callback, ctypes.c_void_p],
            ctypes.c_int,
        ),
        "hackrf_enable_tx_flush": (
            [device, flush_callback, ctypes.c_void_p],
            ctypes.c_int,
        ),
        "hackrf_stop_tx": ([device], ctypes.c_int),
        "hackrf_is_streaming": ([device], ctypes.c_int),
        "hackrf_set_freq": ([device, ctypes.c_uint64], ctypes.c_int),
        "hackrf_set_sample_rate": ([device, ctypes.c_double], ctypes.c_int),
        "hackrf_set_baseband_filter_bandwidth": (
            [device, ctypes.c_uint32],
            ctypes.c_int,
        ),
        "hackrf_set_amp_enable": ([device, ctypes.c_uint8], ctypes.c_int),
        "hackrf_set_antenna_enable": ([device, ctypes.c_uint8], ctypes.c_int),
        "hackrf_set_lna_gain": ([device, ctypes.c_uint32], ctypes.c_int),
        "hackrf_set_vga_gain": ([device, ctypes.c_uint32], ctypes.c_int),
        "hackrf_set_txvga_gain": ([device, ctypes.c_uint32], ctypes.c_int),
        "hackrf_error_name": ([ctypes.c_int], ctypes.c_char_p),
    }
    for name, (argtypes, restype) in signatures.items():
        try:
            function = getattr(library, name)
        except AttributeError as error:
            raise HackRFLibraryError(f"libhackrf is missing required symbol {name}") from error
        _set_signature(function, argtypes, restype)
    return library


def _load_library(path: str | Path | None) -> Any:
    candidate = str(path) if path is not None else ctypes.util.find_library("hackrf")
    if not candidate:
        raise HackRFLibraryError(
            "libhackrf was not found; install HackRF host tools or pass library_path"
        )
    try:
        return _bind_library(ctypes.CDLL(candidate))
    except OSError as error:
        raise HackRFLibraryError(f"could not load libhackrf from {candidate}: {error}") from error


class HackRFDevice:
    """One explicitly selected HackRF with bounded RX/TX/RX switching.

    ``start_rx`` starts continuous libhackrf reception.  Call ``rx_blocks`` to
    iterate copied interleaved signed-I/Q (cs8) blocks, or ``read_rx_block``
    when the caller needs an explicit no-data timeout.

    ``transmit_cs8`` stops RX if active, transmits exactly the supplied bytes,
    waits for libhackrf's TX flush callback, stops TX, and restores RX.  HackRF
    is physically half-duplex: samples sent by the peer before RX has restarted
    cannot be recovered, so this API alone cannot guarantee reception of an
    immediate RF acknowledgement.
    """

    def __init__(
        self,
        serial: str,
        *,
        config: HackRFConfig | None = None,
        library_path: str | Path | None = None,
        library: Any | None = None,
    ) -> None:
        if not serial or "\x00" in serial:
            raise ValueError("serial must be a non-empty string without NUL bytes")
        self.serial = serial
        self.config = config or HackRFConfig()
        self.config.validate()
        self._library_path = library_path
        self._library = _bind_library(library) if library is not None else None
        self._initialized = False
        self._device = ctypes.c_void_p()
        self._mode = "closed"
        self._mode_lock = threading.RLock()

        self._rx_queue: queue.Queue[bytes] = queue.Queue(
            maxsize=self.config.rx_queue_blocks
        )
        self._rx_callback_error: BaseException | None = None
        self._rx_dropped_blocks = 0

        self._tx_data = b""
        self._tx_offset = 0
        self._tx_callback_error: BaseException | None = None
        self._tx_flush_success: bool | None = None
        self._tx_flushed = threading.Event()

        # libhackrf retains these function pointers for the whole stream.
        self._rx_callback_ref = _SampleBlockCallback(self._rx_callback)
        self._tx_callback_ref = _SampleBlockCallback(self._tx_callback)
        self._flush_callback_ref = _FlushCallback(self._flush_callback)

    @property
    def mode(self) -> str:
        with self._mode_lock:
            return self._mode

    @property
    def is_open(self) -> bool:
        return bool(self._device.value)

    @property
    def rx_dropped_blocks(self) -> int:
        return self._rx_dropped_blocks

    def _error_name(self, code: int) -> str:
        if self._library is None:
            return "libhackrf unavailable"
        try:
            value = self._library.hackrf_error_name(code)
            if isinstance(value, bytes):
                return value.decode("utf-8", errors="replace")
            if value:
                return str(value)
        except Exception:
            pass
        return "unknown libhackrf error"

    def _call(self, operation: str, *arguments: Any) -> None:
        assert self._library is not None
        code = int(getattr(self._library, operation)(*arguments))
        if code != HACKRF_SUCCESS:
            raise HackRFCallError(operation, code, self._error_name(code))

    def _require_open(self) -> None:
        if not self.is_open:
            raise HackRFStateError("HackRF device is not open")

    def open(self) -> HackRFDevice:
        """Initialize libhackrf, open the selected serial, and configure it."""

        with self._mode_lock:
            if self.is_open:
                return self
            if self._library is None:
                self._library = _load_library(self._library_path)
            self._call("hackrf_init")
            self._initialized = True
            device = ctypes.c_void_p()
            try:
                self._call(
                    "hackrf_open_by_serial",
                    self.serial.encode("ascii"),
                    ctypes.byref(device),
                )
                self._device = device
                self._configure_locked()
            except BaseException as primary_error:
                cleanup_errors: list[BaseException] = []
                if device.value:
                    try:
                        self._call("hackrf_close", device)
                    except BaseException as cleanup_error:
                        cleanup_errors.append(cleanup_error)
                self._device = ctypes.c_void_p()
                try:
                    self._call("hackrf_exit")
                except BaseException as cleanup_error:
                    cleanup_errors.append(cleanup_error)
                self._initialized = False
                self._mode = "closed"
                if cleanup_errors:
                    raise HackRFCleanupError(
                        "HackRF open failed and rollback was incomplete",
                        cleanup_errors,
                        primary_error=primary_error,
                    ) from primary_error
                raise
            self._mode = "idle"
            return self

    def _configure_locked(self) -> None:
        config = self.config
        device = self._device
        self._call("hackrf_set_freq", device, config.center_freq_hz)
        self._call("hackrf_set_sample_rate", device, float(config.sample_rate_hz))
        # libhackrf resets the filter when the sample rate changes, so bandwidth
        # must be applied after sample rate.
        self._call(
            "hackrf_set_baseband_filter_bandwidth", device, config.bandwidth_hz
        )
        self._call("hackrf_set_amp_enable", device, int(config.amp_enabled))
        self._call(
            "hackrf_set_antenna_enable", device, int(config.antenna_power_enabled)
        )
        self._call("hackrf_set_lna_gain", device, config.rx_lna_gain_db)
        self._call("hackrf_set_vga_gain", device, config.rx_vga_gain_db)
        self._call("hackrf_set_txvga_gain", device, config.tx_vga_gain_db)

    def __enter__(self) -> HackRFDevice:
        return self.open()

    def __exit__(self, exc_type: Any, exc: Any, traceback: Any) -> None:
        if exc_type is None:
            self.close()
            return
        try:
            self.close()
        except HackRFCleanupError as cleanup_error:
            # A body failure remains the structured primary cause, while every
            # failed close step is still visible to callers and tracebacks.
            raise HackRFCleanupError(
                "HackRF context body failed and close was incomplete",
                list(cleanup_error.errors),
                primary_error=exc,
            ) from exc

    def close(self, *, timeout_s: float = 1.0) -> None:
        """Stop streaming, close USB, and release libhackrf global state."""

        with self._mode_lock:
            errors: list[BaseException] = []
            if self.is_open:
                if self._mode == "rx":
                    try:
                        self._stop_rx_locked(timeout_s)
                    except BaseException as error:
                        errors.append(error)
                elif self._mode == "tx":
                    try:
                        self._stop_tx_locked(timeout_s)
                    except BaseException as error:
                        errors.append(error)
                try:
                    self._call("hackrf_close", self._device)
                except BaseException as error:
                    errors.append(error)
                self._device = ctypes.c_void_p()
            if self._initialized:
                try:
                    self._call("hackrf_exit")
                except BaseException as error:
                    errors.append(error)
                self._initialized = False
            self._mode = "closed"
            if errors:
                raise HackRFCleanupError("HackRF close failed", errors)

    def start_rx(self) -> None:
        """Start continuous cs8 reception."""

        with self._mode_lock:
            self._start_rx_locked()

    def _start_rx_locked(self) -> None:
        self._require_open()
        if self._mode == "rx":
            return
        if self._mode != "idle":
            raise HackRFStateError(f"cannot start RX while device mode is {self._mode}")
        self._rx_callback_error = None
        self._call(
            "hackrf_start_rx", self._device, self._rx_callback_ref, ctypes.c_void_p()
        )
        self._mode = "rx"

    def stop_rx(self, *, timeout_s: float = 1.0) -> None:
        """Stop continuous RX and return to idle."""

        with self._mode_lock:
            self._require_open()
            if self._mode == "idle":
                return
            if self._mode != "rx":
                raise HackRFStateError(f"cannot stop RX while device mode is {self._mode}")
            self._stop_rx_locked(timeout_s)

    def set_frequency(self, frequency_hz: int) -> None:
        """Retune an idle device; callers must stop RX/TX first."""

        if frequency_hz <= 0:
            raise ValueError("frequency_hz must be positive")
        with self._mode_lock:
            self._require_open()
            if self._mode != "idle":
                raise HackRFStateError(
                    f"cannot retune while device mode is {self._mode}"
                )
            self._call("hackrf_set_freq", self._device, frequency_hz)

    def discard_rx_blocks(self) -> int:
        """Discard queued pre-switch RX blocks and return their count."""

        discarded = 0
        while True:
            try:
                self._rx_queue.get_nowait()
            except queue.Empty:
                return discarded
            discarded += 1

    def _stop_rx_locked(self, timeout_s: float) -> None:
        self._call("hackrf_stop_rx", self._device)
        self._wait_until_stopped("RX", timeout_s)
        self._mode = "idle"
        if self._rx_callback_error is not None:
            error = self._rx_callback_error
            self._rx_callback_error = None
            raise HackRFCallbackError(f"RX callback failed: {error}") from error

    def _wait_until_stopped(self, operation: str, timeout_s: float) -> None:
        if timeout_s <= 0:
            raise ValueError("timeout_s must be positive")
        assert self._library is not None
        deadline = time.monotonic() + timeout_s
        while True:
            status = int(self._library.hackrf_is_streaming(self._device))
            if status in (
                HACKRF_SUCCESS,
                HACKRF_ERROR_STREAMING_STOPPED,
                HACKRF_ERROR_STREAMING_EXIT_CALLED,
            ):
                return
            if status != HACKRF_TRUE:
                raise HackRFCallError(
                    "hackrf_is_streaming", status, self._error_name(status)
                )
            if time.monotonic() >= deadline:
                raise HackRFTimeoutError(
                    f"timed out waiting {timeout_s:.3f}s for {operation} to stop"
                )
            time.sleep(0.005)

    def _rx_callback(self, transfer_pointer: ctypes.POINTER(_HackRFTransfer)) -> int:
        try:
            transfer = transfer_pointer.contents
            length = int(transfer.valid_length)
            if length < 0 or length > int(transfer.buffer_length):
                raise ValueError(
                    f"invalid RX transfer length {length}/{transfer.buffer_length}"
                )
            if length == 0:
                return 0
            block = ctypes.string_at(transfer.buffer, length)
            try:
                self._rx_queue.put_nowait(block)
            except queue.Full:
                try:
                    self._rx_queue.get_nowait()
                except queue.Empty:
                    pass
                self._rx_dropped_blocks += 1
                self._rx_queue.put_nowait(block)
            return 0
        except BaseException as error:
            self._rx_callback_error = error
            return 1

    def read_rx_block(self, *, timeout_s: float = 1.0) -> bytes:
        """Return one copied cs8 block or raise an explicit no-data timeout."""

        if timeout_s <= 0:
            raise ValueError("timeout_s must be positive")
        if self._rx_callback_error is not None:
            raise HackRFCallbackError(
                f"RX callback failed: {self._rx_callback_error}"
            ) from self._rx_callback_error
        if self.mode != "rx" and self._rx_queue.empty():
            raise HackRFStateError("RX is not running")
        try:
            return self._rx_queue.get(timeout=timeout_s)
        except queue.Empty as error:
            if self._rx_callback_error is not None:
                raise HackRFCallbackError(
                    f"RX callback failed: {self._rx_callback_error}"
                ) from self._rx_callback_error
            raise HackRFTimeoutError(
                f"no RX block arrived within {timeout_s:.3f}s"
            ) from error

    def rx_blocks(self, *, poll_timeout_s: float = 0.25) -> Iterator[bytes]:
        """Yield cs8 blocks until RX is stopped, polling so shutdown is bounded."""

        if poll_timeout_s <= 0:
            raise ValueError("poll_timeout_s must be positive")
        while self.mode == "rx" or not self._rx_queue.empty():
            try:
                yield self.read_rx_block(timeout_s=poll_timeout_s)
            except HackRFTimeoutError:
                if self.mode != "rx":
                    return

    def transmit_cs8(
        self,
        samples: bytes | bytearray | memoryview,
        *,
        timeout_s: float = 2.0,
        stop_timeout_s: float = 1.0,
        resume_rx: bool = True,
    ) -> TxResult:
        """Transmit bounded cs8 bytes, restoring RX if it was active.

        ``timeout_s`` bounds the wait for libhackrf's TX flush notification.
        The synchronous C ``hackrf_stop_rx``/``hackrf_stop_tx`` calls cannot be
        interrupted by Python; ``stop_timeout_s`` bounds the subsequent
        streaming-state poll.
        """

        sample_view = memoryview(samples)
        if sample_view.nbytes == 0:
            raise ValueError("TX samples must not be empty")
        if sample_view.nbytes % 2:
            raise ValueError("cs8 TX requires an even number of interleaved I/Q bytes")
        if sample_view.nbytes > self.config.max_tx_bytes:
            raise ValueError(
                f"TX has {sample_view.nbytes} bytes, limit is {self.config.max_tx_bytes}"
            )
        if timeout_s <= 0 or stop_timeout_s <= 0:
            raise ValueError("timeouts must be positive")
        payload = sample_view.tobytes()

        with self._mode_lock:
            self._require_open()
            if self._mode not in ("idle", "rx"):
                raise HackRFStateError(
                    f"cannot start bounded TX while device mode is {self._mode}"
                )
            rx_was_active = self._mode == "rx"
            started_at = time.monotonic()
            errors: list[BaseException] = []
            tx_started = False
            rx_resumed = False

            try:
                if rx_was_active:
                    self._stop_rx_locked(stop_timeout_s)
                self._tx_data = payload
                self._tx_offset = 0
                self._tx_callback_error = None
                self._tx_flush_success = None
                self._tx_flushed.clear()
                self._call(
                    "hackrf_enable_tx_flush",
                    self._device,
                    self._flush_callback_ref,
                    ctypes.c_void_p(),
                )
                self._call(
                    "hackrf_start_tx",
                    self._device,
                    self._tx_callback_ref,
                    ctypes.c_void_p(),
                )
                tx_started = True
                self._mode = "tx"
                if not self._tx_flushed.wait(timeout_s):
                    raise HackRFTimeoutError(
                        f"TX flush did not arrive within {timeout_s:.3f}s"
                    )
                if self._tx_callback_error is not None:
                    raise HackRFCallbackError(
                        f"TX callback failed: {self._tx_callback_error}"
                    ) from self._tx_callback_error
                if self._tx_flush_success is not True:
                    raise HackRFCallbackError("libhackrf reported an unsuccessful TX flush")
                self._stop_tx_locked(stop_timeout_s)
                tx_started = False
            except BaseException as error:
                errors.append(error)
                if tx_started or self._mode == "tx":
                    try:
                        self._stop_tx_locked(stop_timeout_s)
                    except BaseException as cleanup_error:
                        errors.append(cleanup_error)
            finally:
                self._tx_data = b""
                self._tx_offset = 0
                if rx_was_active and resume_rx and self._mode == "idle":
                    try:
                        self._start_rx_locked()
                        rx_resumed = True
                    except BaseException as cleanup_error:
                        errors.append(cleanup_error)

            if errors:
                if len(errors) == 1:
                    raise errors[0]
                raise HackRFCleanupError(
                    "bounded HackRF TX failed",
                    errors[1:],
                    primary_error=errors[0],
                ) from errors[0]
            return TxResult(
                bytes_sent=len(payload),
                elapsed_s=time.monotonic() - started_at,
                rx_was_resumed=rx_resumed,
            )

    def _stop_tx_locked(self, timeout_s: float) -> None:
        self._call("hackrf_stop_tx", self._device)
        self._wait_until_stopped("TX", timeout_s)
        self._mode = "idle"

    def _tx_callback(self, transfer_pointer: ctypes.POINTER(_HackRFTransfer)) -> int:
        try:
            transfer = transfer_pointer.contents
            capacity = int(transfer.buffer_length)
            if capacity <= 0 or capacity % 2:
                raise ValueError(f"invalid TX transfer buffer length {capacity}")
            remaining = len(self._tx_data) - self._tx_offset
            # Match hackrf_transfer: submit the final non-empty buffer with a
            # zero return, then stop on the following callback. Returning
            # nonzero on the same callback can flush before a short final
            # buffer reaches the RF hardware.
            if remaining <= 0:
                transfer.valid_length = 0
                return 1
            length = min(capacity, remaining)
            chunk = self._tx_data[self._tx_offset : self._tx_offset + length]
            ctypes.memmove(transfer.buffer, chunk, length)
            self._tx_offset += length
            transfer.valid_length = length
            return 0
        except BaseException as error:
            self._tx_callback_error = error
            try:
                transfer_pointer.contents.valid_length = 0
            except BaseException:
                pass
            return 1

    def _flush_callback(self, _context: ctypes.c_void_p, success: int) -> None:
        self._tx_flush_success = bool(success)
        self._tx_flushed.set()


__all__ = [
    "HackRFCallError",
    "HackRFCallbackError",
    "HackRFConfig",
    "HackRFCleanupError",
    "HackRFDevice",
    "HackRFError",
    "HackRFLibraryError",
    "HackRFStateError",
    "HackRFTimeoutError",
    "TxResult",
]
