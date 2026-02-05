# Thermal Data CSV Export and Visualization
_Version 2.0.0 (updated 2025-12-08) - includes livestreaming capture and viewer integration enhancements._

This document describes the enhanced functionality for automatically capturing and visualizing thermal data from the ground station.

## Overview

The system now automatically detects when the Teensy ground station exports thermal data as CSV and:
1. Captures the CSV data from the serial stream
2. Saves it to incrementing filenames (thermal_data_001.csv, thermal_data_002.csv, etc.)
3. Automatically opens the thermal data viewer to display the image

## How It Works

### 1. CSV Detection
The `ground_station_serial_cli_teensy.py` script monitors the serial output for:
- **Start marker**: `=== START CSV ===`
- **End marker**: `=== END CSV ===`

### 2. Data Capture
When the start marker is detected:
- The script enters "CSV capture mode"
- All subsequent lines are collected until the end marker
- The "Teensy: " prefix is automatically removed from captured data

### 3. File Saving
- Files are saved with incrementing names: `thermal_data_001.csv`, `thermal_data_002.csv`, etc.
- The script automatically finds the next available number
- Files are saved in the same directory as the Python script

### 4. Automatic Visualization
- After saving, the thermal data viewer automatically opens
- The viewer displays the thermal image with temperature statistics
- Shows min, max, and mean temperatures

## Usage

### Running the Ground Station CLI
```bash
cd dev-teensyGroundStation
python ground_station_serial_cli_teensy.py
```

### Triggering CSV Export
In the ground station CLI, type:
```
export
```

This will trigger the Teensy to output the thermal data with the CSV markers.

### Manual Thermal Data Viewer
You can also run the thermal viewer manually:
```bash
python thermal_data_viewer.py thermal_data_001.csv
```

## File Structure

```
dev-teensyGroundStation/
├── ground_station_serial_cli_teensy.py  # Enhanced CLI with CSV detection
├── thermal_data_viewer.py               # Updated viewer with command-line support
├── thermal_data_001.csv                 # First captured thermal dataset
├── thermal_data_002.csv                 # Second captured thermal dataset
└── ...                                  # Additional datasets
```

## Features

### Enhanced Serial CLI
- **Automatic CSV detection**: Monitors for export markers
- **Incrementing filenames**: Prevents overwriting previous data
- **Automatic viewer launch**: Opens thermal visualization immediately
- **Progress indicators**: Shows capture status with emojis

### Enhanced Thermal Viewer
- **Command-line support**: Accepts filename as argument
- **Temperature statistics**: Shows min, max, and mean temperatures
- **Better visualization**: Improved layout and color mapping
- **Error handling**: Graceful handling of missing or invalid files

## Dependencies

The thermal viewer requires:
```bash
pip install numpy matplotlib
```

## Example Workflow

1. Start the ground station CLI:
   ```bash
   python ground_station_serial_cli_teensy.py
   ```

2. Connect to your Teensy and wait for the prompt:
   ```
   GS> 
   ```

3. Trigger thermal data export:
   ```
   GS> export
   ```

4. The system will automatically:
   - Detect the CSV export markers
   - Capture all thermal data
   - Save to `thermal_data_001.csv`
   - Open the thermal viewer
   - Display the thermal image

## Troubleshooting

### CSV Not Detected
- Ensure the Teensy is outputting the exact markers: `=== START CSV ===` and `=== END CSV ===`
- Check that the serial connection is stable

### Viewer Won't Open
- Verify matplotlib and numpy are installed
- Check that the CSV file was created successfully
- Ensure the CSV contains valid temperature data

### File Naming Issues
- The script automatically increments filenames
- If you have gaps in numbering, it will use the next available number
- Files are saved in the same directory as the Python script

## Technical Details

### CSV Format
The captured CSV data should contain:
- 120 rows (thermal image height)
- 160 columns (thermal image width)
- Temperature values in Celsius
- Comma-separated values

### File Naming Convention
- Format: `thermal_data_XXX.csv`
- XXX is a zero-padded 3-digit number
- Starts from 001 and increments automatically
