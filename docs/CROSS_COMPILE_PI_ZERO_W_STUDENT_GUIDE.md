# Pi Zero W Cross-Compile Guide

This is the simple version of the Docker cross-build flow for the Raspberry Pi Zero W.

## What This Does

It builds the Raspberry Pi F' deployment on your computer, checks that the binary is really Pi Zero W compatible, copies it to the Pi, and runs a basic smoke test without using the real UART hardware.

## Before You Start

Make sure all of these are true:

- Docker is installed and running
- the Pi is reachable over SSH
- the repo submodules are initialized
- you are working in this repo

Before the first run, update the SSH target for your own setup. The scripts do
not need to use the same host alias as the original machine.

You can do that either by exporting variables:

```bash
export PI_ZERO_W_SSH_HOST=pi@192.168.1.44
export PI_ZERO_W_REMOTE_DIR=/home/pi/artemis/cross
```

or by passing flags each time:

```bash
./tools/docker_cross_compile_pi_zero_w.sh --host pi@192.168.1.44 --remote-dir /home/pi/artemis/cross
```

Your Pi SSH target should support:

- non-interactive SSH
- `rsync`
- `sudo rsync`

The target for this guide is specifically:

- Raspberry Pi Zero W
- 32-bit Raspberry Pi OS
- ARMv6 hardware

## Why This Is Special

Pi Zero W is older than many Raspberry Pi examples online.

A generic Linux ARM hard-float build is not automatically safe for Pi Zero W. Some cross-compilers quietly produce ARMv7-linked binaries, and those can crash on this hardware.

That is why this flow checks the final binary with `readelf` before using it.

## One-Command Flow

From the F' project directory:

```bash
cd ArtemisRpiTeensy_N2
./tools/docker_cross_compile_pi_zero_w.sh
```

That script will:

1. copy the needed sysroot files from the Pi
2. build the deployment in Docker
3. verify the binary is ARMv6-compatible
4. copy the binary to the Pi
5. run a smoke test with:

```bash
./ArtemisRpiTeensyDeployment -d /dev/null
```

## What Success Looks Like

The important signs of success are:

- the build finishes without CMake or linker errors
- `readelf -A` shows ARMv6, not ARMv7
- the binary copies to the Pi
- the smoke test starts up, prints normal startup events, and stays alive until timeout

The verification files are saved in:

- `ArtemisRpiTeensy_N2/cross/pi-zero-w/verify/`

The large generated sysroot and verification outputs are intentionally kept out
of Git. They are local build data, not source files.

## What To Check If Something Looks Wrong

If the binary is wrong, look at:

- `file.txt`
- `readelf-A.txt`
- `readelf-l.txt`

If you see ARMv7 in the attributes, do not trust the build.

## Why `/dev/null` Is Used

`/dev/null` is used for a quick smoke test so you can prove the process starts without depending on real UART wiring.

This does not test real serial communication.

It only answers:

- does the binary run on the Pi?
- does the deployment start correctly?

## Real UART Is A Separate Step

After the `/dev/null` smoke passes, you still need a real runtime test on:

```bash
./ArtemisRpiTeensyDeployment -d /dev/serial0
```

That is the real hardware path.

## Common Confusion

You may see a warning about task permissions and CPU affinity.

For this smoke test, that warning is usually okay. The process can still run.

It only becomes important later if you need strict thread scheduling behavior.

## Summary

Use this guide when you want the easiest safe path:

- build in Docker
- verify ARMv6 output
- smoke test on the Pi with `/dev/null`

Use the detailed handoff note if you need to understand why the toolchain was set up this way or how to debug a broken cross-build.
