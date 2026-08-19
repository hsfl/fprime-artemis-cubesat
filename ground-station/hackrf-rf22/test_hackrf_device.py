#!/usr/bin/env python3
"""Hardware-free tests for the minimal libhackrf ctypes wrapper."""

from __future__ import annotations

import ctypes
import unittest

from hackrf_device import (
    HackRFCallError,
    HackRFCleanupError,
    HackRFConfig,
    HackRFDevice,
    HackRFStateError,
    HackRFTimeoutError,
    _HackRFTransfer,
)


class FakeHackRFLibrary:
    """Small callback-driving fake; it never discovers or opens USB."""

    def __init__(
        self,
        *,
        tx_buffer_length: int = 4,
        flush_success: int | None = 1,
        failures: dict[str, int] | None = None,
    ):
        self.calls: list[tuple[str, tuple[object, ...]]] = []
        self.streaming = False
        self.rx_callback = None
        self.tx_callback = None
        self.flush_callback = None
        self.tx_buffer_length = tx_buffer_length
        self.flush_success = flush_success
        self.transmitted = bytearray()
        self.tx_callback_records: list[tuple[int, bytes]] = []
        self.failures = failures or {}

    def _record(self, name: str, *arguments: object) -> int:
        self.calls.append((name, arguments))
        return self.failures.get(name, 0)

    def hackrf_init(self) -> int:
        return self._record("hackrf_init")

    def hackrf_exit(self) -> int:
        return self._record("hackrf_exit")

    def hackrf_open_by_serial(self, serial, output) -> int:
        self._record("hackrf_open_by_serial", serial)
        output._obj.value = 0x1234
        return 0

    def hackrf_close(self, device) -> int:
        return self._record("hackrf_close", device)

    def hackrf_set_freq(self, device, value) -> int:
        return self._record("hackrf_set_freq", device, value)

    def hackrf_set_sample_rate(self, device, value) -> int:
        return self._record("hackrf_set_sample_rate", device, value)

    def hackrf_set_baseband_filter_bandwidth(self, device, value) -> int:
        return self._record("hackrf_set_baseband_filter_bandwidth", device, value)

    def hackrf_set_amp_enable(self, device, value) -> int:
        return self._record("hackrf_set_amp_enable", device, value)

    def hackrf_set_antenna_enable(self, device, value) -> int:
        return self._record("hackrf_set_antenna_enable", device, value)

    def hackrf_set_lna_gain(self, device, value) -> int:
        return self._record("hackrf_set_lna_gain", device, value)

    def hackrf_set_vga_gain(self, device, value) -> int:
        return self._record("hackrf_set_vga_gain", device, value)

    def hackrf_set_txvga_gain(self, device, value) -> int:
        return self._record("hackrf_set_txvga_gain", device, value)

    def hackrf_start_rx(self, device, callback, context) -> int:
        self._record("hackrf_start_rx", device, context)
        self.rx_callback = callback
        self.streaming = True
        return 0

    def hackrf_stop_rx(self, device) -> int:
        self._record("hackrf_stop_rx", device)
        self.streaming = False
        return 0

    def hackrf_enable_tx_flush(self, device, callback, context) -> int:
        self._record("hackrf_enable_tx_flush", device, context)
        self.flush_callback = callback
        return 0

    def hackrf_start_tx(self, device, callback, context) -> int:
        self._record("hackrf_start_tx", device, context)
        self.tx_callback = callback
        self.streaming = True
        while True:
            buffer = (ctypes.c_uint8 * self.tx_buffer_length)()
            transfer = _HackRFTransfer(
                device,
                ctypes.cast(buffer, ctypes.POINTER(ctypes.c_uint8)),
                self.tx_buffer_length,
                0,
                None,
                None,
            )
            finished = callback(ctypes.pointer(transfer))
            submitted = bytes(buffer[: transfer.valid_length])
            self.tx_callback_records.append((finished, submitted))
            self.transmitted.extend(submitted)
            if finished:
                break
        if self.flush_success is not None:
            assert self.flush_callback is not None
            self.flush_callback(None, self.flush_success)
        return 0

    def hackrf_stop_tx(self, device) -> int:
        self._record("hackrf_stop_tx", device)
        self.streaming = False
        return 0

    def hackrf_is_streaming(self, device) -> int:
        self._record("hackrf_is_streaming", device)
        return 1 if self.streaming else -1004

    def hackrf_error_name(self, code) -> bytes:
        return f"fake error {code}".encode()

    def emit_rx(self, data: bytes) -> int:
        assert self.rx_callback is not None
        buffer = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
        transfer = _HackRFTransfer(
            ctypes.c_void_p(0x1234),
            ctypes.cast(buffer, ctypes.POINTER(ctypes.c_uint8)),
            len(data),
            len(data),
            None,
            None,
        )
        return self.rx_callback(ctypes.pointer(transfer))


def call_names(library: FakeHackRFLibrary) -> list[str]:
    return [name for name, _arguments in library.calls]


class HackRFDeviceTests(unittest.TestCase):
    def test_rf_amp_is_supported_but_antenna_power_is_hard_disabled(self) -> None:
        HackRFConfig(amp_enabled=True).validate()
        with self.assertRaises(ValueError):
            HackRFConfig(antenna_power_enabled=True).validate()

    def test_constructor_is_usb_idle_and_open_applies_c3m_defaults(self) -> None:
        library = FakeHackRFLibrary()
        device = HackRFDevice("0123456789abcdef", library=library)
        self.assertEqual(library.calls, [])

        device.open()

        self.assertEqual(device.mode, "idle")
        self.assertEqual(
            call_names(library)[:6],
            [
                "hackrf_init",
                "hackrf_open_by_serial",
                "hackrf_set_freq",
                "hackrf_set_sample_rate",
                "hackrf_set_baseband_filter_bandwidth",
                "hackrf_set_amp_enable",
            ],
        )
        values = {
            name: arguments[-1]
            for name, arguments in library.calls
            if name.startswith("hackrf_set_")
        }
        self.assertEqual(values["hackrf_set_freq"], 433_000_000)
        self.assertEqual(values["hackrf_set_sample_rate"], 8_000_000.0)
        self.assertEqual(values["hackrf_set_baseband_filter_bandwidth"], 1_750_000)
        self.assertEqual(values["hackrf_set_amp_enable"], 0)
        self.assertEqual(values["hackrf_set_antenna_enable"], 0)
        self.assertEqual(values["hackrf_set_lna_gain"], 0)
        self.assertEqual(values["hackrf_set_vga_gain"], 8)
        self.assertEqual(values["hackrf_set_txvga_gain"], 0)
        device.close()
        self.assertEqual(device.mode, "closed")

    def test_rx_callback_copies_cs8_block(self) -> None:
        library = FakeHackRFLibrary()
        device = HackRFDevice("serial", library=library).open()
        device.start_rx()
        expected = b"\x80\x7f\x00\xff\x01\xfe"

        self.assertEqual(library.emit_rx(expected), 0)
        self.assertEqual(device.read_rx_block(timeout_s=0.01), expected)
        device.close()

    def test_runtime_gain_changes_are_validated_and_applied_during_rx(self) -> None:
        library = FakeHackRFLibrary()
        device = HackRFDevice("serial", library=library).open()
        device.start_rx()

        device.set_rx_gains(8, 4)
        device.set_tx_gain(12)

        values = [
            (name, arguments[-1])
            for name, arguments in library.calls
            if name in {
                "hackrf_set_lna_gain",
                "hackrf_set_vga_gain",
                "hackrf_set_txvga_gain",
            }
        ]
        self.assertEqual(
            values[-3:],
            [
                ("hackrf_set_lna_gain", 8),
                ("hackrf_set_vga_gain", 4),
                ("hackrf_set_txvga_gain", 12),
            ],
        )
        with self.assertRaises(ValueError):
            device.set_rx_gains(7, 4)
        with self.assertRaises(ValueError):
            device.set_tx_gain(48)
        device.close()

    def test_runtime_rf_amp_change_requires_idle_and_is_applied(self) -> None:
        library = FakeHackRFLibrary()
        device = HackRFDevice("serial", library=library).open()
        device.set_amp_enabled(True)
        self.assertEqual(library.calls[-1][0], "hackrf_set_amp_enable")
        self.assertEqual(library.calls[-1][1][-1], 1)
        device.start_rx()
        with self.assertRaises(HackRFStateError):
            device.set_amp_enabled(False)
        device.close()

    def test_open_preserves_primary_configuration_error_when_rollback_succeeds(self) -> None:
        library = FakeHackRFLibrary(failures={"hackrf_set_freq": -10})
        device = HackRFDevice("serial", library=library)

        with self.assertRaises(HackRFCallError) as raised:
            device.open()

        self.assertEqual(raised.exception.operation, "hackrf_set_freq")
        self.assertEqual(
            call_names(library)[-2:], ["hackrf_close", "hackrf_exit"]
        )
        self.assertFalse(device.is_open)
        self.assertEqual(device.mode, "closed")

    def test_open_reports_primary_and_every_failed_rollback_step(self) -> None:
        library = FakeHackRFLibrary(
            failures={
                "hackrf_set_freq": -10,
                "hackrf_close": -20,
                "hackrf_exit": -30,
            }
        )
        device = HackRFDevice("serial", library=library)

        with self.assertRaises(HackRFCleanupError) as raised:
            device.open()

        error = raised.exception
        self.assertIsInstance(error.primary_error, HackRFCallError)
        self.assertEqual(error.primary_error.operation, "hackrf_set_freq")
        self.assertIs(error.__cause__, error.primary_error)
        self.assertEqual(
            [item.operation for item in error.cleanup_errors],
            ["hackrf_close", "hackrf_exit"],
        )
        self.assertEqual(error.errors[0], error.primary_error)
        self.assertEqual(
            call_names(library)[-2:], ["hackrf_close", "hackrf_exit"]
        )
        self.assertFalse(device.is_open)
        self.assertEqual(device.mode, "closed")

    def test_context_body_error_remains_primary_when_close_also_fails(self) -> None:
        library = FakeHackRFLibrary()
        device = HackRFDevice("serial", library=library)
        body_error = ValueError("body failed")

        with self.assertRaises(HackRFCleanupError) as raised:
            with device:
                library.failures.update(
                    {"hackrf_close": -20, "hackrf_exit": -30}
                )
                raise body_error

        error = raised.exception
        self.assertIs(error.primary_error, body_error)
        self.assertIs(error.__cause__, body_error)
        self.assertEqual(
            [item.operation for item in error.cleanup_errors],
            ["hackrf_close", "hackrf_exit"],
        )
        self.assertIn("body failed", str(error))
        self.assertFalse(device.is_open)
        self.assertEqual(device.mode, "closed")

    def test_context_body_error_is_unchanged_when_close_succeeds(self) -> None:
        library = FakeHackRFLibrary()
        device = HackRFDevice("serial", library=library)
        body_error = ValueError("body failed")

        try:
            with device:
                raise body_error
        except ValueError as observed:
            self.assertIs(observed, body_error)
        else:
            self.fail("context body error was unexpectedly suppressed")

    def test_rx_queue_is_bounded_and_keeps_newest_block(self) -> None:
        library = FakeHackRFLibrary()
        config = HackRFConfig(rx_queue_blocks=1)
        device = HackRFDevice("serial", library=library, config=config).open()
        device.start_rx()

        library.emit_rx(b"\x01\x02")
        library.emit_rx(b"\x03\x04")

        self.assertEqual(device.rx_dropped_blocks, 1)
        self.assertEqual(device.read_rx_block(timeout_s=0.01), b"\x03\x04")
        device.close()

    def test_bounded_tx_switches_rx_tx_rx_and_preserves_all_bytes(self) -> None:
        library = FakeHackRFLibrary(tx_buffer_length=4)
        device = HackRFDevice("serial", library=library).open()
        device.start_rx()
        payload = bytes(range(10))
        library.calls.clear()

        result = device.transmit_cs8(payload, timeout_s=0.05)

        self.assertEqual(result.bytes_sent, len(payload))
        self.assertTrue(result.rx_was_resumed)
        self.assertEqual(device.mode, "rx")
        self.assertEqual(bytes(library.transmitted), payload)
        # Match v2026.01.3 hackrf_transfer: submit the short final transfer
        # with zero, then signal completion from the following empty callback.
        self.assertEqual(
            library.tx_callback_records,
            [
                (0, payload[0:4]),
                (0, payload[4:8]),
                (0, payload[8:10]),
                (1, b""),
            ],
        )
        names = call_names(library)
        self.assertLess(names.index("hackrf_stop_rx"), names.index("hackrf_start_tx"))
        self.assertLess(names.index("hackrf_start_tx"), names.index("hackrf_stop_tx"))
        self.assertLess(names.index("hackrf_stop_tx"), names.index("hackrf_start_rx"))
        device.close()

    def test_tx_flush_timeout_forces_stop_and_restores_rx(self) -> None:
        library = FakeHackRFLibrary(flush_success=None)
        device = HackRFDevice("serial", library=library).open()
        device.start_rx()

        with self.assertRaises(HackRFTimeoutError):
            device.transmit_cs8(b"\x01\x02", timeout_s=0.01)

        self.assertEqual(device.mode, "rx")
        names = call_names(library)
        self.assertIn("hackrf_stop_tx", names)
        self.assertEqual(names[-1], "hackrf_start_rx")
        device.close()

    def test_invalid_tx_is_rejected_before_switching_modes(self) -> None:
        library = FakeHackRFLibrary()
        config = HackRFConfig(max_tx_bytes=4)
        device = HackRFDevice("serial", library=library, config=config).open()
        device.start_rx()
        library.calls.clear()

        with self.assertRaisesRegex(ValueError, "even number"):
            device.transmit_cs8(b"\x00")
        with self.assertRaisesRegex(ValueError, "limit"):
            device.transmit_cs8(b"\x00" * 6)

        self.assertEqual(library.calls, [])
        self.assertEqual(device.mode, "rx")
        device.close()


if __name__ == "__main__":
    unittest.main()
