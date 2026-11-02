#!/usr/bin/env python3
"""Supervise the N2 CDR binary using its periodic EPS status text event.

The event shows EPS polling/log progress only; it is not an end-to-end health
check. The monitored CDR profile must keep ACTIVITY_LO text logging enabled.
"""
import argparse
import os
import selectors
import signal
import socket
import stat
import subprocess
import sys
import time

HEARTBEAT = b'(epsManager) EpsStatusUpdated :'
stop_requested = False


def notify(message):
    address = os.environ.get('NOTIFY_SOCKET')
    if not address:
        return
    if address.startswith('@'):
        address = '\0' + address[1:]
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as sock:
            sock.sendto(message.encode(), address)
    except OSError as exc:
        print('systemd notification failed: {}'.format(exc), file=sys.stderr, flush=True)


def request_stop(signum, frame):
    global stop_requested
    stop_requested = True


def character_device(path):
    try:
        return stat.S_ISCHR(os.stat(path).st_mode)
    except OSError:
        return False


def owns_tcp_listener(pid, port):
    """Match the child's socket descriptors to Linux TCP LISTEN entries."""
    try:
        inodes = set()
        for fd in os.listdir('/proc/{}/fd'.format(pid)):
            try:
                target = os.readlink('/proc/{}/fd/{}'.format(pid, fd))
            except OSError:
                continue  # A descriptor can close while we inspect it.
            if target.startswith('socket:[') and target.endswith(']'):
                inodes.add(target[8:-1])
        for name in ('tcp', 'tcp6'):
            try:
                with open('/proc/{}/net/{}'.format(pid, name)) as table:
                    next(table, None)
                    for line in table:
                        fields = line.split()
                        if (len(fields) > 9 and fields[3] == '0A' and
                                int(fields[1].rsplit(':', 1)[1], 16) == port and
                                fields[9] in inodes):
                            return True
            except OSError:
                continue  # IPv6 may be unavailable; missing proc data fails closed.
    except OSError:
        pass
    return False


def terminate(child):
    if child.poll() is not None:
        return
    try:
        os.killpg(child.pid, signal.SIGTERM)
        child.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(child.pid, signal.SIGKILL)
        child.wait()
    except ProcessLookupError:
        child.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--uart', default='/dev/serial0')
    parser.add_argument('--tcp-port', type=int, default=50000)
    parser.add_argument('--startup-grace', type=float, default=90)
    parser.add_argument('--heartbeat-timeout', type=float, default=30)
    parser.add_argument('--poll-interval', type=float, default=1)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.command[:1] == ['--']:
        args.command.pop(0)
    if (not args.command or not 1 <= args.tcp_port <= 65535 or
            min(args.startup_grace, args.heartbeat_timeout, args.poll_interval) <= 0):
        parser.error('command and positive timeouts are required')
    signal.signal(signal.SIGTERM, request_stop)
    signal.signal(signal.SIGINT, request_stop)
    print('Waiting for UART character device {}'.format(args.uart), flush=True)
    notify('STATUS=Waiting for UART {}'.format(args.uart))
    while not stop_requested and not character_device(args.uart):
        time.sleep(args.poll_interval)
    if stop_requested:
        return 0
    print('Starting {}'.format(args.command[0]), flush=True)
    try:
        child = subprocess.Popen(args.command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 start_new_session=True, bufsize=0)
    except OSError as exc:
        print('Failed to start F Prime: {}'.format(exc), file=sys.stderr, flush=True)
        return 1
    notify('STATUS=Starting F Prime; waiting for EPS progress and TCP listener')
    selector = selectors.DefaultSelector()
    selector.register(child.stdout, selectors.EVENT_READ)
    start = time.monotonic()
    last_heartbeat = None
    last_notify = 0.0
    tail = b''
    try:
        while not stop_requested:
            for key, _ in selector.select(args.poll_interval):
                chunk = os.read(key.fd, 65536)
                if not chunk:
                    selector.unregister(key.fileobj)
                else:
                    os.write(sys.stdout.fileno(), chunk)
                    sample = tail + chunk
                    if HEARTBEAT in sample:
                        last_heartbeat = time.monotonic()
                    tail = sample[-(len(HEARTBEAT) - 1):]
            if child.poll() is not None:
                print('F Prime exited with status {}'.format(child.returncode), file=sys.stderr, flush=True)
                return 1
            now = time.monotonic()
            if last_notify == 0:
                fresh = last_heartbeat is not None and now - last_heartbeat < args.heartbeat_timeout
                if fresh and owns_tcp_listener(child.pid, args.tcp_port):
                    print('EPS progress and owned TCP listener observed; service ready', flush=True)
                    notify('READY=1\nSTATUS=EPS progress and TCP listener observed')
                    notify('WATCHDOG=1')
                    last_notify = now
                elif now - start >= args.startup_grace:
                    print('Startup grace expired waiting for fresh EPS progress and owned TCP listener',
                          file=sys.stderr, flush=True)
                    return 1
            elif last_heartbeat is None or now - last_heartbeat >= args.heartbeat_timeout:
                print('EPS progress event timed out', file=sys.stderr, flush=True)
                return 1
            elif now - last_notify >= 5:
                notify('WATCHDOG=1')
                last_notify = now
        notify('STOPPING=1\nSTATUS=Stopping F Prime')
        return 0
    finally:
        selector.close()
        terminate(child)
        child.stdout.close()


if __name__ == '__main__':
    sys.exit(main())
