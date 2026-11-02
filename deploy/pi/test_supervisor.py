#!/usr/bin/env python3
"""Hardware-free lifecycle tests for the CDR service supervisor."""
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

SUPERVISOR = Path(__file__).with_name('artemis-supervisor.py')


class SupervisorTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.sock.bind(str(self.root / 'notify'))
        self.sock.settimeout(3)
        self.env = dict(os.environ, NOTIFY_SOCKET=str(self.root / 'notify'))
        self.process = None
        probe = socket.socket()
        probe.bind(('127.0.0.1', 0))
        self.port = probe.getsockname()[1]
        probe.close()

    def tearDown(self):
        if self.process and self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=3)
        if self.process and self.process.stdout:
            self.process.stdout.close()
        self.sock.close()
        self.tmp.cleanup()

    def start(self, uart, code, grace='1', timeout='0.5', listener=True):
        if listener:
            code = ('import socket; s=socket.socket(); '
                    's.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); '
                    's.bind(("127.0.0.1",{})); s.listen(1); '.format(self.port)) + code
        self.process = subprocess.Popen(
            [sys.executable, str(SUPERVISOR), '--uart', uart, '--tcp-port', str(self.port), '--startup-grace', grace,
             '--heartbeat-timeout', timeout, '--poll-interval', '0.05', '--',
             sys.executable, '-u', '-c', code],
            env=self.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def message(self, fragment):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            data = self.sock.recv(4096).decode()
            if fragment in data:
                return
        self.fail('notification not received: ' + fragment)

    def test_missing_uart_waits_and_can_stop_without_starting_child(self):
        marker = self.root / 'started'
        self.start(str(self.root / 'missing'), 'open({!r}, "w").close()'.format(str(marker)))
        self.message('Waiting for UART')
        time.sleep(0.2)
        self.assertIsNone(self.process.poll())
        self.assertFalse(marker.exists())
        self.process.send_signal(signal.SIGTERM)
        self.assertEqual(self.process.wait(timeout=3), 0)

    def test_child_exit_is_failure_even_with_success_status(self):
        self.start('/dev/null', 'raise SystemExit(0)')
        self.assertEqual(self.process.wait(timeout=3), 1)

    def test_uart_appearing_starts_child(self):
        uart = self.root / 'uart'
        self.start(str(uart), 'print("EVENT: (epsManager) EpsStatusUpdated : test", flush=True); import time; time.sleep(30)', timeout='10')
        self.message('Waiting for UART')
        uart.symlink_to('/dev/null')
        self.message('READY=1')
        self.process.send_signal(signal.SIGTERM)
        self.assertEqual(self.process.wait(timeout=3), 0)

    def test_progress_ready_then_hang_is_failure_and_child_is_reaped(self):
        pidfile = self.root / 'pid'
        code = ('import os,time; open({!r}, "w").write(str(os.getpid())); '
                'print("EVENT: (epsManager) EpsStatusUpdated : test", flush=True); time.sleep(30)').format(str(pidfile))
        self.start('/dev/null', code)
        self.message('READY=1')
        self.message('WATCHDOG=1')
        self.assertEqual(self.process.wait(timeout=3), 1)
        with self.assertRaises(ProcessLookupError):
            os.kill(int(pidfile.read_text()), 0)

    def test_stop_cleans_up_child(self):
        pidfile = self.root / 'pid'
        code = ('import os,time; open({!r}, "w").write(str(os.getpid())); '
                'print("EVENT: (epsManager) EpsStatusUpdated : test", flush=True); time.sleep(30)').format(str(pidfile))
        self.start('/dev/null', code, timeout='10')
        self.message('READY=1')
        self.process.send_signal(signal.SIGTERM)
        self.assertEqual(self.process.wait(timeout=3), 0)
        with self.assertRaises(ProcessLookupError):
            os.kill(int(pidfile.read_text()), 0)

    def test_eps_alone_and_other_process_listener_do_not_mark_ready(self):
        other = socket.socket()
        other.bind(('127.0.0.1', self.port))
        other.listen(1)
        try:
            self.start('/dev/null', 'print("EVENT: (epsManager) EpsStatusUpdated : test", flush=True); import time; time.sleep(30)', grace='0.4', timeout='10', listener=False)
            self.assertEqual(self.process.wait(timeout=3), 1)
            output = self.process.stdout.read().decode()
            self.assertIn('Startup grace expired', output)
            self.sock.settimeout(0.1)
            messages = []
            try:
                while True:
                    messages.append(self.sock.recv(4096).decode())
            except socket.timeout:
                pass
            self.assertFalse(any('READY=1' in message for message in messages))
        finally:
            other.close()

    def test_startup_without_progress_is_failure(self):
        self.start('/dev/null', 'import time; time.sleep(30)', grace='0.3')
        self.assertEqual(self.process.wait(timeout=3), 1)


if __name__ == '__main__':
    unittest.main()
