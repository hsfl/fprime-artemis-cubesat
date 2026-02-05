#!/usr/bin/env python3
"""
RPi Thermal Camera Controller with UART Data Transfer
EPSCOR C3M Payload - Satellite Raspberry Pi Component

This Python script runs on a Raspberry Pi in the satellite payload to capture
thermal images using a USB thermal camera and transmit the data to a Teensy
microcontroller via UART for radio transmission to the ground station.

Key Features:
- Captures thermal images using USB thermal camera (FLIR/Seek Thermal)
- Averages multiple frames for noise reduction
- Transmits data via UART with header/end markers
- Provides real-time progress updates and data validation
- Listens for UART trigger commands from Teensy microcontroller

Hardware Requirements:
- Raspberry Pi (3B+ or 4 recommended)
- USB thermal camera (FLIR ONE, Seek Thermal, etc.)
- UART connection to Teensy microcontroller

Communication Protocol:
- UART: 115200 baud, 8N1
- Trigger: "TRIGGER\n" command from Teensy
- Header: Magic bytes (0xDE 0xAD 0xBE 0xEF) + 16-bit length
- Data: Raw thermal image data (16-bit per pixel)
- End: Magic bytes (0xFF 0xFF)

@author EPSCOR C3M Team
@date 2025-12-08
@version 2.0.0
"""

__version__ = "2.0.0"
__build_date__ = "2025-12-08"

import numpy as np
import time
import struct
import serial
from uvctypes import *
import cv2
from queue import Queue, Empty, Full
import ctypes
import threading

# UART Configuration for Teensy communication
UART_PORT = '/dev/serial0'  # Primary UART (GPIO14/15, pins 8/10)
UART_BAUD = 115200         # High-speed UART to match Teensy baud rate
TRIGGER_COMMAND = b'TRIGGER\n'  # UART command from Teensy to initiate capture
STREAM_START_COMMAND = b'STREAM_START\n'  # Command to start livestream mode
STREAM_STOP_COMMAND = b'STREAM_STOP\n'    # Command to stop livestream mode
FRAME_REQUEST_COMMAND = b'FRAME\n'        # Command to request a single stream frame

# Livestream protocol constants
STREAM_MAGIC = bytes([0xCA, 0xFE, 0xBA, 0xBE])  # Magic header for livestream frames
STREAM_FRAME_WIDTH = 80   # Downsampled width (160/2)
STREAM_FRAME_HEIGHT = 60  # Downsampled height (120/2)
STREAM_FRAME_SIZE = STREAM_FRAME_WIDTH * STREAM_FRAME_HEIGHT  # 4,800 bytes per frame

# Camera Configuration
MAX_FRAMES = 10           # Number of frames to capture and average
THERMAL_QUEUE_SIZE = 20   # Size of frame queue for thermal data (2x the avg capture frame needs)
thermal_queue = Queue(THERMAL_QUEUE_SIZE)  # Queue for thermal frame data (used for averaging)

# Latest frame buffer for streaming (always holds the most recent frame)
# This avoids queue overhead and ensures we always send the freshest frame
_latest_frame = None
_latest_frame_lock = threading.Lock()
_latest_frame_timestamp = 0

FRAME_CALLBACK_TYPE = ctypes.CFUNCTYPE(None, ctypes.POINTER(uvc_frame), ctypes.c_void_p)

import sys


class ThermalCamera:
    def __init__(self):
        self.ctx = POINTER(uvc_context)()
        self.dev = POINTER(uvc_device)()
        self.devh = POINTER(uvc_device_handle)()
        self.ctrl = uvc_stream_ctrl()
        self.frame_callback = FRAME_CALLBACK_TYPE(self._frame_callback)
        self.streaming = False

    def _frame_callback(self, frame_ptr, userptr):
        global _latest_frame, _latest_frame_timestamp

        if not frame_ptr:
            return

        try:
            frame = frame_ptr.contents
            if not bool(frame.data):
                return

            size = frame.width * frame.height
            if size <= 0:
                return

            array_pointer = ctypes.cast(
                frame.data, ctypes.POINTER(ctypes.c_uint16 * size)
            )
            thermal = np.frombuffer(array_pointer.contents, dtype=np.uint16).reshape(
                frame.height, frame.width
            ).copy()

            # Update latest frame buffer (for streaming - always the freshest frame)
            with _latest_frame_lock:
                _latest_frame = thermal
                _latest_frame_timestamp = time.time()

            # Also put in queue (for frame averaging in single captures)
            try:
                thermal_queue.put_nowait(thermal)
            except Full:
                # Drop the oldest frame and retry to keep the queue flowing
                try:
                    thermal_queue.get_nowait()
                except Empty:
                    pass

                try:
                    thermal_queue.put_nowait(thermal)
                except Full:
                    pass

        except Exception:
            # Never propagate exceptions across the C callback boundary.
            return

    def initialize(self):
        # Context setup mirrors known-good original implementation
        res = libuvc.uvc_init(byref(self.ctx), 0)
        if res < 0 or not bool(self.ctx):
            raise RuntimeError(f"uvc_init failed: {res}")

        vid = globals().get("PT_USB_VID", 0)
        pid = globals().get("PT_USB_PID", 0)

        res = libuvc.uvc_find_device(self.ctx, byref(self.dev), vid, pid, 0)
        if res < 0 or not bool(self.dev):
            raise FileNotFoundError("No UVC thermal camera found (uvc_find_device).")

        res = libuvc.uvc_open(self.dev, byref(self.devh))
        if res < 0 or not bool(self.devh):
            raise RuntimeError(f"uvc_open failed: {res}")

        frame_formats = uvc_get_frame_formats_by_guid(self.devh, VS_FMT_GUID_Y16)
        if not frame_formats:
            raise RuntimeError("No Y16 frame formats available.")

        selected_format = frame_formats[0]
        if selected_format.wWidth == 0 or selected_format.wHeight == 0:
            raise RuntimeError("Invalid Y16 format: width/height is zero.")

        default_interval = selected_format.dwDefaultFrameInterval
        if not default_interval:
            default_interval = int(1e7 / 9)

        fps = int(1e7 / default_interval) if default_interval else 9
        if fps <= 0:
            fps = 9

        res = libuvc.uvc_get_stream_ctrl_format_size(
            self.devh, byref(self.ctrl), UVC_FRAME_FORMAT_Y16,
            selected_format.wWidth, selected_format.wHeight, fps
        )
        if res < 0:
            raise RuntimeError(f"uvc_get_stream_ctrl_format_size failed: {res}")

        print(f"Camera initialized: {selected_format.wWidth}x{selected_format.wHeight}")
        return selected_format.wWidth, selected_format.wHeight

    def start_streaming(self):
        res = libuvc.uvc_start_streaming(self.devh, byref(self.ctrl), self.frame_callback, None, 0)
        if res < 0:
            raise RuntimeError(f"uvc_start_streaming failed: {res}")
        self.streaming = True
        print("Camera streaming started")

    def cleanup(self):
        try:
            if self.streaming and bool(self.devh):
                libuvc.uvc_stop_streaming(self.devh)
        except Exception:
            pass
        finally:
            self.streaming = False

        try:
            if bool(self.dev):
                libuvc.uvc_unref_device(self.dev)
        except Exception:
            pass

        try:
            if bool(self.ctx):
                libuvc.uvc_exit(self.ctx)
        except Exception:
            pass

def is_valid_frame(frame):
    """Skip FFC frames"""
    zero_count = np.sum(frame < 1000)
    return zero_count < (frame.size * 0.8)


def get_latest_frame(max_age_s=1.0):
    """
    Get the most recent frame from the camera (for streaming).

    This is more efficient than using the queue for streaming because:
    1. No queue overhead (put/get operations)
    2. Always returns the freshest frame, not a stale queued one
    3. Simple lock-based access

    Args:
        max_age_s: Maximum age of frame in seconds (default 1.0s)

    Returns:
        numpy array of frame data, or None if no recent frame available
    """
    global _latest_frame, _latest_frame_timestamp

    with _latest_frame_lock:
        if _latest_frame is None:
            return None

        # Check if frame is too old
        age = time.time() - _latest_frame_timestamp
        if age > max_age_s:
            return None

        # Return a copy to avoid race conditions
        return _latest_frame.copy()


def downsample_frame(frame_16bit):
    """
    Downsample 160x120 16-bit thermal frame to 80x60 8-bit for streaming.

    Uses 2x2 block averaging and scales 16-bit values to 8-bit range.
    The 16-bit thermal values are in Kelvin*100 (e.g., 29315 = 20°C).
    We map a reasonable temperature range (0-100°C = 27315-37315) to 0-255.

    Args:
        frame_16bit: numpy array of shape (120, 160) with uint16 values

    Returns:
        numpy array of shape (60, 80) with uint8 values
    """
    if frame_16bit is None:
        return None

    # Ensure correct input shape
    if frame_16bit.shape != (120, 160):
        print(f"Warning: unexpected frame shape {frame_16bit.shape}, expected (120, 160)")
        return None

    # Reshape to (60, 2, 80, 2) to get 2x2 blocks, then average
    # This groups pixels into 2x2 blocks for averaging
    reshaped = frame_16bit.reshape(60, 2, 80, 2)
    averaged = reshaped.mean(axis=(1, 3)).astype(np.float32)

    # Map from Kelvin*100 to 0-255 range
    # Temperature range: 0°C (27315) to 100°C (37315)
    min_kelvin = 27315.0  # 0°C in Kelvin*100
    max_kelvin = 37315.0  # 100°C in Kelvin*100

    # Clip to valid range and scale to 0-255
    clipped = np.clip(averaged, min_kelvin, max_kelvin)
    scaled = ((clipped - min_kelvin) / (max_kelvin - min_kelvin) * 255.0)

    return scaled.astype(np.uint8)

def capture_and_average(timeout_s=2.0):
    """
    Capture and average multiple thermal frames for noise reduction
    
    Collects MAX_FRAMES thermal frames from the camera queue, averages them
    to reduce noise, and returns the averaged data as bytes. Provides
    real-time progress updates and temperature statistics.
    
    Returns:
        bytes: Averaged thermal image data as raw bytes
    """
    frames = []

    # Empty the queue first.
    while True:
        try:
            thermal_queue.get_nowait()
        except Empty:
            break

    print(f"Capturing up to {MAX_FRAMES} frames (timeout {timeout_s:.1f}s)...")
    deadline = time.time() + timeout_s

    while len(frames) < MAX_FRAMES:
        remaining = deadline - time.time()
        if remaining <= 0:
            break
        try:
            frame = thermal_queue.get(timeout=min(0.5, max(0.05, remaining)))
            if is_valid_frame(frame): #TODO check if this works
                frames.append(frame)
            #print(f"Captured {len(frames)}/{MAX_FRAMES} frames")
        except Empty:
            continue

    if not frames:
        print("No frames captured before timeout.")
        return None

    if len(frames) < MAX_FRAMES:
        print(f"Only captured {len(frames)} frame(s); averaging nonetheless.")

    print("Averaging frames...")
    stacked = np.stack(frames, axis=0).astype(np.float64)
    averaged = np.mean(stacked, axis=0).astype(np.uint16)
    
    # # Calculate and display temperature statistics
    # min_val = np.min(averaged)
    # max_val = np.max(averaged)
    # mean_val = np.mean(averaged)
    
    # print(f"Temperature stats:")
    # print(f"  Min: {(min_val - 27315) / 100.0:.1f}°C")  # Convert from Kelvin*100
    # print(f"  Max: {(max_val - 27315) / 100.0:.1f}°C")
    # print(f"  Mean: {(mean_val - 27315) / 100.0:.1f}°C")
    
    return averaged.tobytes()  # Return as raw bytes for UART transmission

def send_stream_frame_uart(frame_8bit, frame_seq, uart_port):
    """
    Send a downsampled stream frame via UART with livestream header.

    Uses STREAM_MAGIC header (0xCA 0xFE 0xBA 0xBE) to distinguish from
    regular thermal captures. The Teensy forwards these frames to the
    ground station for live viewing (not saving).

    Args:
        frame_8bit: numpy array of shape (60, 80) with uint8 values
        frame_seq: frame sequence number (0-255, wrapping)
        uart_port: Serial port object for UART communication

    Returns:
        bool: True if transmission successful, False otherwise
    """
    if frame_8bit is None:
        return False

    try:
        data = frame_8bit.tobytes()
        if len(data) != STREAM_FRAME_SIZE:
            print(f"Warning: stream frame size {len(data)}, expected {STREAM_FRAME_SIZE}")
            return False

        # Build stream frame packet:
        # [STREAM_MAGIC 4B][Frame Seq 1B][Frame Size 2B][Frame Data 4800B][End 2B]
        header = bytearray()
        header.extend(STREAM_MAGIC)
        header.append(frame_seq & 0xFF)  # 1-byte sequence number
        header.extend([STREAM_FRAME_SIZE & 0xFF, (STREAM_FRAME_SIZE >> 8) & 0xFF])

        # Send header
        uart_port.write(header)

        # Send frame data
        uart_port.write(data)

        # Send end markers
        uart_port.write(bytearray([0xFF, 0xFF]))
        uart_port.flush()

        return True

    except Exception as e:
        print(f"Stream frame UART error: {e}")
        return False


def send_status_uart(message: str, uart_port):
    """
    Send a human-readable STATUS payload using the same header/length/end scheme.
    """
    try:
        payload = f"STATUS:{message}".encode("ascii", "ignore")
        return send_data_uart(payload, uart_port)
    except Exception as e:
        print(f"UART status send error: {e}")
        return False


def send_data_uart(data, uart_port):
    """
    Send thermal image data via UART with header and end markers
    
    Transmits thermal data with proper protocol formatting including
    magic bytes, length information, and end markers. Provides
    real-time progress updates and transmission statistics.
    
    Args:
        data: Thermal image data as bytes
        uart_port: Serial port object for UART communication
        
    Returns:
        bool: True if transmission successful, False otherwise
    """
    if not data:
        print("UART: nothing to send (len=0).")
        return False
    
    # print(f"\nSending {len(data)} bytes via UART...")
    # print(f"UART port: {UART_PORT} at {UART_BAUD} baud")
    
    try:
        # Create header with magic bytes and length
        header = bytearray()
        header.extend([0xDE, 0xAD, 0xBE, 0xEF])  # Magic bytes for validation
        
        # Add data length (little-endian, 16-bit)
        length = len(data)
        header.extend([length & 0xFF, (length >> 8) & 0xFF])
        
        # print("Sending header...")
        uart_port.write(header)
        uart_port.flush()  # Ensure header is transmitted immediately
        
        # Small delay for header processing on receiver
        time.sleep(0.1)
        
        # print("Sending thermal data...")
        start_time = time.time()
        
        # Send data in chunks for better throughput and progress tracking
        chunk_size = 1024  # 1KB chunks for optimal transmission
        total_sent = 0
        
        while total_sent < length:
            chunk_end = min(total_sent + chunk_size, length)
            chunk = data[total_sent:chunk_end]
            
            uart_port.write(chunk)
            total_sent += len(chunk)
            
            # # Progress update every 4KB
            # if total_sent % 4096 == 0 or total_sent == length:
            #     elapsed = time.time() - start_time
            #     rate = total_sent / elapsed if elapsed > 0 else 0
            #     eta = (length - total_sent) / rate if rate > 0 else 0
                
            #     print(f"Progress: {total_sent}/{length} bytes "
            #           f"({total_sent/length*100:.1f}%) - "
            #           f"{rate:.0f} bytes/sec - ETA: {eta:.1f}s")
        
        # Send end markers to signal completion
        end_markers = bytearray([0xFF, 0xFF])
        uart_port.write(end_markers)
        uart_port.flush()
        
        # Display transmission summary
        elapsed = time.time() - start_time
        print(f"\nUART transmission complete!")
        print(f"Sent {length} bytes in {elapsed:.2f} seconds")
        print(f"Average rate: {length/elapsed:.0f} bytes/second")
        
        return True
        
    except Exception as e:
        print(f"UART transmission error: {e}")
        return False

def wait_for_uart_command(uart_port, timeout=None):
    """
    Wait for UART command from Teensy (TRIGGER or STREAM_START/STOP)

    Reads UART data and returns the command type when recognized.

    Args:
        uart_port: Serial port object for UART communication
        timeout: Optional timeout in seconds (None for blocking)

    Returns:
        str: 'trigger', 'stream_start', 'stream_stop', or None on timeout
    """
    start_time = time.time()
    buffer = b''

    while True:
        if timeout is not None:
            elapsed = time.time() - start_time
            if elapsed > timeout:
                return None

        if uart_port.in_waiting > 0:
            chunk = uart_port.read(uart_port.in_waiting)
            buffer += chunk

            # Check for commands
            if TRIGGER_COMMAND in buffer:
                return 'trigger'
            if STREAM_START_COMMAND in buffer:
                return 'stream_start'
            if STREAM_STOP_COMMAND in buffer:
                return 'stream_stop'

            # Keep buffer size manageable (only last 100 bytes)
            if len(buffer) > 100:
                buffer = buffer[-100:]
        else:
            time.sleep(0.01)  # Small delay to avoid busy-waiting


def wait_for_uart_trigger(uart_port, timeout=None):
    """
    Wait for UART trigger command from Teensy (legacy wrapper)

    Returns:
        bool: True if trigger received, False on timeout
    """
    result = wait_for_uart_command(uart_port, timeout)
    return result == 'trigger'


def run_stream_loop(uart_port):
    """
    Run request-response streaming loop: wait for FRAME request → send latest frame.

    Uses explicit flow control - the Teensy requests each frame when ready.
    This prevents serial buffer overflow since Pi only sends when Teensy asks.

    Optimized for performance:
    - Uses latest frame buffer instead of queue (no flushing needed)
    - Always sends the freshest available frame
    - Minimal latency between request and response

    Protocol:
    1. Teensy sends STREAM_START to enter stream mode
    2. Teensy sends FRAME to request each frame
    3. Pi grabs latest frame, downsamples, and sends it
    4. Teensy receives frame, transmits over radio, then requests next
    5. Teensy sends STREAM_STOP to exit

    Args:
        uart_port: Serial port object for UART communication

    Returns:
        str: Reason for exit ('stop_command', 'timeout', 'error')
    """
    print("Stream mode: waiting for frame requests from Teensy...")

    frame_seq = 0
    frames_sent = 0
    start_time = time.time()
    rx_buffer = b''  # Buffer for accumulating incoming UART data

    try:
        while True:
            # Read any available UART data into buffer
            if uart_port.in_waiting > 0:
                rx_buffer += uart_port.read(uart_port.in_waiting)

            # Check for stop command
            if STREAM_STOP_COMMAND in rx_buffer or b'STREAM_STOP' in rx_buffer:
                print("Stream stop command received")
                return 'stop_command'

            # Check for frame request
            if FRAME_REQUEST_COMMAND in rx_buffer or b'FRAME' in rx_buffer:
                # Remove the request from buffer
                if FRAME_REQUEST_COMMAND in rx_buffer:
                    rx_buffer = rx_buffer.replace(FRAME_REQUEST_COMMAND, b'', 1)
                elif b'FRAME\n' in rx_buffer:
                    rx_buffer = rx_buffer.replace(b'FRAME\n', b'', 1)
                else:
                    # Just 'FRAME' without newline
                    idx = rx_buffer.find(b'FRAME')
                    rx_buffer = rx_buffer[idx+5:]

                # Get the latest frame (much faster than queue - no flushing needed)
                frame = get_latest_frame(max_age_s=2.0)

                # If no recent frame, wait briefly for one
                if frame is None:
                    time.sleep(0.1)  # Brief wait for camera callback
                    frame = get_latest_frame(max_age_s=2.0)

                if frame is None:
                    print("No frame available from camera")
                    continue

                # Skip invalid frames (FFC calibration)
                if not is_valid_frame(frame):
                    # Wait for next frame
                    time.sleep(0.15)
                    frame = get_latest_frame(max_age_s=2.0)
                    if frame is None or not is_valid_frame(frame):
                        continue

                # Downsample to 80x60 8-bit
                frame_8bit = downsample_frame(frame)
                if frame_8bit is None:
                    print("Downsample failed")
                    continue

                # Send the frame
                if send_stream_frame_uart(frame_8bit, frame_seq, uart_port):
                    frames_sent += 1
                    frame_seq = (frame_seq + 1) & 0xFF

                    # Progress update every 10 frames
                    if frames_sent % 10 == 0:
                        elapsed = time.time() - start_time
                        fps = frames_sent / elapsed if elapsed > 0 else 0
                        print(f"Streaming: {frames_sent} frames, {fps:.1f} fps")
                else:
                    print("Failed to send stream frame")
            else:
                # No request yet, brief sleep to avoid busy-waiting
                time.sleep(0.01)

            # Keep buffer from growing too large (trim old data)
            if len(rx_buffer) > 256:
                rx_buffer = rx_buffer[-128:]

    except KeyboardInterrupt:
        print("Stream interrupted by user")
        return 'interrupted'

    except Exception as e:
        print(f"Stream error: {e}")
        import traceback
        traceback.print_exc()
        return 'error'

def main():
    print("RPi Thermal Camera - UART Output")
    print("="*40)

    # UART first (so we can notify Teensy if camera isn't present)
    try:
        uart = serial.Serial(UART_PORT, UART_BAUD, timeout=1)
        print(f"UART initialized: {UART_PORT} at {UART_BAUD} baud")
    except Exception as e:
        print(f"UART initialization failed: {e}")
        print("Make sure UART is enabled in raspi-config!")
        return

    send_status_uart("BOOT", uart)

    camera = ThermalCamera()

    try:
        # Try to bring up the camera; if it fails, notify and exit gracefully.
        try:
            camera.initialize()
            camera.start_streaming()
            time.sleep(2)  # allow to stabilize
            send_status_uart("CAM_READY", uart)
        except Exception as cam_err:
            print(f"Camera not ready: {cam_err}")
            # Send a STATUS packet to Teensy so it can display/log the reason
            send_status_uart("NO_CAMERA", uart)
            # Clean exit so systemd can retry later
            return

        print(f"\nSystem ready. Waiting for UART commands:")
        print(f"  - {TRIGGER_COMMAND.decode('ascii').strip()}: Single capture")
        print(f"  - {STREAM_START_COMMAND.decode('ascii').strip()}: Start livestream")
        print(f"  - {STREAM_STOP_COMMAND.decode('ascii').strip()}: Stop livestream")
        print(f"Data will be sent via UART: {UART_PORT} at {UART_BAUD} baud")
        send_status_uart("IDLE", uart)

        while True:
            # Wait for command from Teensy via UART
            command = wait_for_uart_command(uart)

            if command == 'trigger':
                timestamp = time.strftime("%H:%M:%S")
                print(f"\n[{timestamp}] Capture triggered via UART!")
                print("="*40)
                send_status_uart("CAPTURE_START", uart)

                thermal_data = capture_and_average()

                if thermal_data is None:
                    print("No thermal data available; notifying Teensy.")
                    send_status_uart("NO_FRAMES", uart)
                else:
                    print("\nWaiting 3 seconds for Teensy to prepare...")
                    time.sleep(3)
                    success = send_data_uart(thermal_data, uart)
                    if success:
                        print("✓ Data transmission successful!")
                        send_status_uart("CAPTURE_DONE", uart)
                    else:
                        print("✗ Data transmission failed!")
                        send_status_uart("TX_FAIL", uart)

                print("\nReady for next command...")
                print("="*40)
                send_status_uart("IDLE", uart)

            elif command == 'stream_start':
                timestamp = time.strftime("%H:%M:%S")
                print(f"\n[{timestamp}] Stream mode started via UART!")
                print("="*40)

                exit_reason = run_stream_loop(uart)
                print(f"Stream ended: {exit_reason}")

                print("\nReady for next command...")
                print("="*40)
                send_status_uart("IDLE", uart)

            elif command == 'stream_stop':
                # Stop command received outside of stream mode - just acknowledge
                print("Stream stop received (not in stream mode)")
                send_status_uart("IDLE", uart)

    except KeyboardInterrupt:
        print("\nShutting down...")
        send_status_uart("SHUTDOWN", uart)

    except Exception as exc:
        print(f"Unexpected error: {exc}")
        send_status_uart("ERROR", uart)

    finally:
        try:
            camera.cleanup()
        except Exception:
            pass
        try:
            uart.close()
        except Exception:
            pass

if __name__ == "__main__":
    main()
