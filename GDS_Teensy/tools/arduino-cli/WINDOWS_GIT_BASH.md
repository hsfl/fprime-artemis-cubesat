# Windows Git Bash Ground Teensy Build

Use this path when WSL2 is not usable for the Arduino/Teensy toolchain and the
normal Git Bash build fails with:

```text
fatal error: bits/c++config.h: No such file or directory
```

The Windows-specific script stores Arduino CLI packages under
`%LOCALAPPDATA%/Arduino15-n2` instead of the repo-local `.arduino15` directory.
This keeps the Teensy GCC toolchain path short enough for its C++ header lookup
while keeping downloaded tools in a normal per-user Windows application-data
location.

The script writes the generated Arduino CLI config under
`build/arduino-cli-windows-config/arduino-cli.windows.generated.yaml`.

You can override that cache location for one shell session:

```bash
export ARDUINO_WINDOWS_DATA_DIR="$(cygpath -m "$LOCALAPPDATA")/Arduino15-n2"
```

From Git Bash:

```bash
cd /c/path/to/fprime-artemis-cubesat/GDS_Teensy
./tools/arduino-cli/build_windows_git_bash.sh
```

List connected Teensy upload IDs:

```bash
arduino-cli board list
```

Upload by the physical `usb:` ID, not by `COMx`:

```bash
./tools/arduino-cli/upload_windows_git_bash.sh usb:0/140000/0/2
```

Use the `usb:` ID shown on your machine. The current HIL bench ground Teensy is
usually `usb:100000`, but Windows host enumeration can differ.

The normal macOS/Linux/WSL scripts remain:

```bash
./tools/arduino-cli/build.sh
./tools/arduino-cli/upload.sh usb:100000
```
