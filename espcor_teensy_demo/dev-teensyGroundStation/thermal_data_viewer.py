"""
Thermal Data Viewer
EPSCOR C3M Payload - Ground Station Component

Displays thermal image data from CSV files captured by the ground station
Usage: python thermal_data_viewer.py [filename]
If no filename is provided, defaults to thermal_data_001.csv

@author EPSCOR C3M Team
@date 2025-12-08
@version 2.0.0
"""

__version__ = "2.0.0"
__build_date__ = "2025-12-08"

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import sys
import os
import webbrowser
import argparse
from io import StringIO
from matplotlib.offsetbox import TextArea, AnnotationBbox
from matplotlib.widgets import Button

# Livestream constants
STREAM_FRAME_WIDTH = 80
STREAM_FRAME_HEIGHT = 60
STREAM_FRAME_SIZE = STREAM_FRAME_WIDTH * STREAM_FRAME_HEIGHT  # 4800 bytes

def _parse_ddmm_mmmm(coord):
    """Return DMS text and decimal degrees from DDMM.MMMM coordinate strings."""
    coord = coord.strip()
    if not coord:
        return None

    direction = ""
    if coord[-1] in "NSEW":
        direction = coord[-1]
        coord = coord[:-1]

    try:
        numeric = float(coord)
    except ValueError:
        return None

    degrees = int(numeric // 100)
    minutes_full = numeric - (degrees * 100)
    minutes = int(minutes_full)
    seconds = (minutes_full - minutes) * 60

    decimal = degrees + minutes_full / 60
    if direction in ("S", "W"):
        decimal *= -1

    dms_text = f"{degrees}°{minutes}'{seconds:.2f}\"{direction}"
    return dms_text, decimal


def parse_gps_location_ddmm(raw_value):
    """Return DMS string and list of decimal degree floats from DDMM.MMMM pair."""
    parts = [part.strip() for part in raw_value.split(",")]
    dms_parts = []
    decimal_values = []

    for part in parts:
        parsed = _parse_ddmm_mmmm(part)
        if parsed is None:
            return None, None
        dms_text, decimal = parsed
        dms_parts.append(dms_text)
        decimal_values.append(decimal)

    return ", ".join(dms_parts), decimal_values


def format_gps_location_ddmm_to_dms(raw_value):
    """Convert comma-separated DDMM.MMMM location string to DMS for display."""
    dms_text, _ = parse_gps_location_ddmm(raw_value)
    return dms_text


def parse_decimal_coordinate_pair(raw_value):
    """Return list of decimal degree floats parsed from comma-separated string."""
    if not raw_value:
        return None

    decimal_values = []
    for part in raw_value.split(","):
        stripped = part.strip()
        if not stripped:
            continue
        try:
            decimal_values.append(float(stripped))
        except ValueError:
            return None

    if len(decimal_values) < 2:
        return None
    return decimal_values


def load_thermal_file(filename):
    """
    Load thermal CSV data and associated metadata comments.

    Returns:
        tuple[np.ndarray, dict]: thermal grid, metadata dict with keys
        'captured_at', 'gps', and 'imu'.
    """
    metadata = {
        "captured_at": None,
        "gps": {},
        "imu": {},
    }

    data_lines = []
    current_section = None

    with open(filename, "r") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line:
                # Preserve blank line between metadata and data but do not append metadata blanks
                continue

            if line.startswith("#"):
                content = line.lstrip("#").strip()
                upper_content = content.upper()

                if upper_content.startswith("CAPTURED_AT,"):
                    _, value = content.split(",", 1)
                    metadata["captured_at"] = value.strip()
                elif upper_content.endswith("_DATA_START"):
                    current_section = content.split("_", 1)[0].lower()
                elif upper_content.endswith("_DATA_END"):
                    current_section = None
                elif "," in content and current_section in ("gps", "imu"):
                    key, value = content.split(",", 1)
                    # drop leading section prefix if present (e.g. GPS_latitude_deg)
                    key = key.strip()
                    prefix = f"{current_section.upper()}_"
                    if key.upper().startswith(prefix):
                        key = key[len(prefix):]
                    metadata[current_section][key] = value.strip()
                # Ignore other comment styles
                continue

            data_lines.append(raw_line)

    if not data_lines:
        raise ValueError("No thermal data rows found in file")

    data_buffer = StringIO("".join(data_lines))
    data = np.loadtxt(data_buffer, delimiter=",")
    return data, metadata

def run_livestream_viewer():
    """
    Run the livestream viewer mode.
    Connects to the stream_frame_queue from the CLI and displays frames in real-time.
    """
    # Import the queue from the CLI module
    try:
        from ground_station_serial_cli_teensy import stream_frame_queue
    except ImportError:
        print("Error: Cannot import stream_frame_queue from CLI module")
        print("Make sure ground_station_serial_cli_teensy.py is in the same directory")
        return

    print("Starting livestream viewer...")
    print("Waiting for stream frames from ground station...")
    print("Press Ctrl+C or close window to exit")

    # Set up the figure and axis
    fig, ax = plt.subplots(figsize=(10, 8))

    # Initialize with blank image (80x60)
    initial_data = np.zeros((STREAM_FRAME_HEIGHT, STREAM_FRAME_WIDTH), dtype=np.uint8)
    image = ax.imshow(initial_data, cmap='hot', vmin=0, vmax=255, aspect='equal',
                      interpolation='nearest')
    cbar = fig.colorbar(image, ax=ax, label='Intensity (0-255)')

    ax.set_title('Thermal Livestream - Waiting for frames...')
    ax.set_xlabel('Column')
    ax.set_ylabel('Row')

    # Stats text
    stats_text = ax.text(0.02, 0.98, '', transform=ax.transAxes,
                         verticalalignment='top', fontsize=9,
                         bbox=dict(boxstyle='round', facecolor='black', alpha=0.7),
                         color='white')

    frame_count = 0
    last_seq = -1

    def update_frame(frame_num):
        nonlocal frame_count, last_seq

        try:
            # Try to get a frame from the queue (non-blocking)
            frame_data = stream_frame_queue.get_nowait()

            frame_bytes = frame_data['data']
            seq = frame_data['seq']
            info = frame_data.get('info', '')

            # Convert bytes to numpy array and reshape
            frame_array = np.frombuffer(frame_bytes, dtype=np.uint8)
            frame_array = frame_array.reshape((STREAM_FRAME_HEIGHT, STREAM_FRAME_WIDTH))

            # Update the image
            image.set_array(frame_array)

            # Update stats
            frame_count += 1
            dropped = 0
            if last_seq >= 0:
                expected_seq = (last_seq + 1) & 0xFF
                if seq != expected_seq:
                    dropped = (seq - expected_seq) & 0xFF
            last_seq = seq

            min_val = frame_array.min()
            max_val = frame_array.max()
            mean_val = frame_array.mean()

            # Convert 8-bit intensity back to approximate temperature
            # 0=0°C, 255=100°C based on downsample mapping
            min_temp = min_val / 255.0 * 100.0
            max_temp = max_val / 255.0 * 100.0
            mean_temp = mean_val / 255.0 * 100.0

            stats_str = (f'Frame: {frame_count} | Seq: {seq} | {info}\n'
                         f'Temp: {min_temp:.1f}°C - {max_temp:.1f}°C (mean: {mean_temp:.1f}°C)')
            if dropped > 0:
                stats_str += f'\nDropped: {dropped} frames'

            stats_text.set_text(stats_str)
            ax.set_title(f'Thermal Livestream - Frame {frame_count}', fontsize=12)

        except:
            # No frame available, keep current display
            pass

        return [image, stats_text]

    # Create animation (update every 50ms = 20fps max)
    ani = animation.FuncAnimation(fig, update_frame, interval=50, blit=True, cache_frame_data=False)

    plt.tight_layout()
    plt.show()

    print(f"\nLivestream ended. Total frames displayed: {frame_count}")


def view_thermal_file(filename):
    """View a thermal data CSV file"""
    # Check if file exists
    if not os.path.exists(filename):
        print(f"Error: File '{filename}' not found!")
        print("Usage: python thermal_data_viewer.py [filename]")
        return

    try:
        # Load the thermal data
        print(f"Loading thermal data from: {filename}")
        data, metadata = load_thermal_file(filename)

        # Display as thermal image
        fig, ax = plt.subplots(figsize=(12, 8))
        image = ax.imshow(data, cmap='hot', aspect='auto')
        fig.colorbar(image, ax=ax, label='Temperature (°C)')

        # Add some statistics
        min_temp = np.nanmin(data)
        max_temp = np.nanmax(data)
        mean_temp = np.nanmean(data)

        # Build title and subtitle
        main_title = f'Thermal Image from Flat Sat - {filename}'

        subtitle_parts = [f'Min: {min_temp:.1f}°C, Max: {max_temp:.1f}°C, Mean: {mean_temp:.1f}°C']

        if metadata.get("captured_at"):
            subtitle_parts.append(f'Captured: {metadata["captured_at"]}')

        gps_location_display = None
        gps_location_decimal = None
        maps_link = None

        gps_decimal_raw = metadata["gps"].get("gs_sat_decimal")
        gps_location_decimal = parse_decimal_coordinate_pair(gps_decimal_raw)

        gps_location_raw = (
            metadata["gps"].get("gs_sat_location")
            or metadata["gps"].get("gs_sat_location_ddmm_mmmm_format")
        )

        if gps_location_raw:
            ddmm_display, ddmm_decimal = parse_gps_location_ddmm(gps_location_raw)
            if ddmm_display:
                gps_location_display = ddmm_display
            if not gps_location_decimal and ddmm_decimal:
                gps_location_decimal = ddmm_decimal

        if not gps_location_display and gps_location_decimal:
            gps_location_display = (
                f"{gps_location_decimal[0]:.6f}°, {gps_location_decimal[1]:.6f}°"
            )

        if gps_location_display:
            subtitle_parts.append(f"GPS: {gps_location_display}")

        if gps_location_decimal and len(gps_location_decimal) >= 2:
            maps_link = (
                f"https://maps.google.com/maps?q="
                f"{gps_location_decimal[0]:.6f},{gps_location_decimal[1]:.6f}"
            )

        subtitle = '\n'.join(subtitle_parts)

        ax.set_title(main_title + '\n' + subtitle, fontsize=10, pad=10)
        ax.set_xlabel('Column')
        ax.set_ylabel('Row')

        if maps_link:
            fig.subplots_adjust(top=0.80, bottom=0.1)
            button_ax = fig.add_axes([0.35, 0.92, 0.20, 0.05])
            maps_button = Button(button_ax, 'Open in Google Maps', color='lightblue', hovercolor='skyblue')

            def on_button_click(_event):
                webbrowser.open(maps_link)

            maps_button.on_clicked(on_button_click)
        else:
            fig.tight_layout()

        plt.show()

        print(f"Thermal image displayed successfully!")
        print(f"Temperature range: {min_temp:.1f}°C to {max_temp:.1f}°C")
        print(f"Mean temperature: {mean_temp:.1f}°C")
        if metadata.get("captured_at"):
            print(f"Captured at (UTC): {metadata['captured_at']}")
        if gps_location_display:
            print(f"GPS location: {gps_location_display}")
            if maps_link:
                print(f"Google Maps: {maps_link}")
        elif gps_location_raw:
            print(f"GPS location unparsed: {gps_location_raw}")

        print("GS> ")
    except Exception as e:
        print(f"Error loading or displaying thermal data: {e}")
        print("Make sure the file contains valid CSV data with temperature values.")


def main():
    parser = argparse.ArgumentParser(
        description='Thermal Data Viewer - View thermal images from CSV or livestream'
    )
    parser.add_argument('filename', nargs='?', default='thermal_data_001.csv',
                        help='CSV file to view (default: thermal_data_001.csv)')
    parser.add_argument('--stream', action='store_true',
                        help='Run in livestream mode to view real-time thermal data')

    args = parser.parse_args()

    if args.stream:
        run_livestream_viewer()
    else:
        print(f"Using filename: {args.filename}")
        view_thermal_file(args.filename)


if __name__ == "__main__":
    main()
