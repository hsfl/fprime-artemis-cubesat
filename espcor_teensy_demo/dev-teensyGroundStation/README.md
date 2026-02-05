# Teensy Ground Station CLI

A Python-based command-line interface for communicating with Teensy 4.1 microcontrollers. This tool provides a simple serial communication interface to send commands and receive responses from your Teensy-based ground station system.

## Features

- **Automatic Port Detection**: Automatically finds and connects to Teensy/Arduino serial ports
- **Real-time Communication**: Bidirectional communication with your Teensy device
- **Clean Output Formatting**: Properly formatted display of Teensy responses
- **Graceful Exit**: Automatic detection of system resets with clean shutdown
- **Cross-platform**: Works on macOS, Linux, and Windows

## Requirements

- Python 3.6 or higher
- Teensy 4.1 or compatible Arduino device
- USB cable for serial communication

## Installation

1. **Clone or download this repository**

2. **Set up a virtual environment (recommended):**
   ```bash
   python3 -m venv teensy_env
   source teensy_env/bin/activate  # On Windows: teensy_env\Scripts\activate
   ```

3. **Install dependencies:**
   ```bash
   pip install -r requirements.txt
   ```

## Usage

1. **Connect your Teensy 4.1** to your computer via USB

2. **Run the CLI:**
   ```bash
   python teensySerial.py
   ```

3. **Select your serial port** when prompted (or press Enter for the default)

4. **Start sending commands** to your Teensy device

## Example Session

```
Available ports:
  /dev/cu.debug-console: n/a
  /dev/cu.Bluetooth-Incoming-Port: n/a
  /dev/cu.usbmodem115563101: USB Serial

Enter port (default: /dev/cu.usbmodem115563101): 
Connected to /dev/cu.usbmodem115563101 at 115200 baud
Type commands and press Enter to send. Type 'quit' to exit.

Teensy: ================================================
Teensy: Ground Station Command Interpreter
Teensy: ================================================
Teensy: Version: 1.0.0
Teensy: Build Date: 2025-08-05
Teensy: Build Info: Arduino Ground Station Command Interpreter
Teensy: ================================================
Teensy: Type 'help' for available commands
Teensy: Type 'version' for detailed version info
Teensy: Type 'clear' to clear the screen
Teensy: ================================================
Teensy: GS> 

Send: help
Teensy: Available Commands:
Teensy: ==================
Teensy:   help - Show available commands
Teensy:   version - Show version information
Teensy:   status - Show system status
Teensy:   ping - Test communication
Teensy:   echo - Echo back the arguments
Teensy:   led - Control LED (on/off/toggle)
Teensy:   analog - Read analog pin (analog <pin>)
Teensy:   digital - Read/write digital pin (digital <pin> [value])
Teensy:   time - Show uptime
Teensy:   reset - Reset the system
Teensy:   history - Show command history
Teensy:   clear - Clear screen
Teensy:   rpi - Control Raspberry Pi (rpi <on|off|toggle>)
Teensy: Usage: command [arguments]
Teensy: Example: led on, analog 0, digital 13 1
Teensy: GS> 

Send: quit
```

## Commands

### CLI Commands
- `quit`, `exit`, `q` - Exit the program
- `Ctrl+C` - Force exit

### Teensy Commands (examples)
- `help` - Show available commands
- `version` - Show version information
- `status` - Show system status
- `ping` - Test communication
- `led on/off/toggle` - Control LED
- `analog <pin>` - Read analog pin
- `digital <pin> [value]` - Read/write digital pin
- `reset` - Reset the system
- `clear` - Clear screen

## Configuration

### Baud Rate
The default baud rate is set to 115200. To change it, modify the `BAUD_RATE` constant in `teensySerial.py`:

```python
BAUD_RATE = 115200  # Change this value as needed
```

### Port Detection
The program automatically detects serial ports with these patterns in their description:
- "serial"
- "dual serial" 
- "usbmodem"
- "arduino"
- "teensy"

## Troubleshooting

### No Serial Ports Found
- Ensure your Teensy is connected via USB
- Check that the Teensy is powered on
- Try running the program with administrator/sudo privileges
- Verify the Teensy drivers are installed

### Connection Issues
- Make sure no other programs are using the serial port (only one device can listen to a serial port at time)
- Try a different USB cable or port
- Check that the baud rate matches your Teensy configuration
- Restart the Teensy device

### Permission Denied (Linux/macOS)
```bash
sudo chmod 666 /dev/ttyUSB0  # Replace with your port
# Or add your user to the dialout group:
sudo usermod -a -G dialout $USER
```

## Auto-Exit on Reset

The program automatically detects when the Teensy sends a "Resetting system..." message and will gracefully exit. This prevents hanging when the Teensy resets itself.

## File Structure

```
sampleTeensy4.1/
├── teensySerial.py          # Main CLI program
├── sampleTeensy4.1.ino      # Teensy Arduino code
├── requirements.txt         # Python dependencies
├── README.md               # This file
└── teensy_env/             # Python virtual environment
```

## Development

### Adding New Features
1. Modify `teensySerial.py` and `teensy_ground_station.ino` to add new functionality
2. Test with your specific hardware setup

### Contributing
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

This project is open source. Feel free to modify and distribute as needed.

## Support

For issues or questions:
1. Check the troubleshooting section above
2. Verify your Teensy code is working with the Arduino Serial Monitor
3. Test with a simple echo program first
4. Check the Teensy documentation for hardware-specific issues
