"""Ground Teensy raw USB contract checks using local PTYs, never physical devices."""
import asyncio
import contextlib
import os
from pathlib import Path
import queue
import struct
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from serial_ground import SerialGround
from common import crc16
try:
    import serial
    import yaml
    from ground import Ground, PayloadReceiver, RetryWriter, verify_artifacts
    HAVE_DEPS = True
except ImportError:
    HAVE_DEPS = False


async def until(predicate, timeout=3):
    deadline = asyncio.get_running_loop().time() + timeout
    while asyncio.get_running_loop().time() < deadline:
        result = predicate()
        if result:
            return result
        await asyncio.sleep(0.01)
    raise AssertionError("Timed out waiting for local PTY observation")


async def read_master(fd, length):
    result = bytearray()
    async def read():
        deadline = asyncio.get_running_loop().time() + 3
        while len(result) < length and asyncio.get_running_loop().time() < deadline:
            try:
                result.extend(os.read(fd, length - len(result)))
            except BlockingIOError:
                pass
            await asyncio.sleep(0.005)
        if len(result) != length:
            raise AssertionError(f"PTY short read: {result!r}")
    await read()
    return bytes(result)


def packets(blob, product=17, transfer=3):
    chunks = [blob[n:n + 35] for n in range(0, len(blob), 35)]
    header = b'N2' + bytes((1, transfer)) + struct.pack('<IIHBH', product, len(blob), len(chunks), 35, crc16(blob))
    data = []
    for index, chunk in enumerate(chunks):
        packet = b'N2' + bytes((2, transfer)) + struct.pack('<H', index) + bytes((len(chunk),)) + chunk
        data.append(packet + struct.pack('<H', crc16(packet)))
    end = b'N2' + bytes((3, transfer)) + struct.pack('<HH', len(chunks), crc16(blob))
    return header, data, end


@unittest.skipUnless(HAVE_DEPS and os.name == 'posix', 'requires pyserial/PyYAML and POSIX PTYs')
class SerialGroundTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.ports = [os.openpty(), os.openpty()]
        for master, slave in self.ports:
            os.set_blocking(master, False)
        self.paths = [os.ttyname(slave) for master, slave in self.ports]
        self.received = [bytearray(), bytearray()]
        self.failures = []
        async def receive(channel, data): self.received[channel].extend(data)
        async def disconnected(channel, detail): self.failures.append(channel)
        self.transport = SerialGround(*self.paths, receive, disconnected, lambda *a, **kw: None)
        self.transport.open()
        self.task = asyncio.create_task(self.transport.run())

    async def asyncTearDown(self):
        self.transport.close()
        self.task.cancel()
        await asyncio.gather(self.task, return_exceptions=True)
        for master, slave in self.ports:
            for fd in (master, slave):
                with contextlib.suppress(OSError): os.close(fd)
        self.tmp.cleanup()

    async def test_raw_bidirectional_channels_without_rf_envelope(self):
        os.write(self.ports[0][0], b'TM\x00\x01')
        os.write(self.ports[1][0], b'N2\x01science')
        await until(lambda: len(self.received[0]) == 4 and len(self.received[1]) == 10)
        self.assertEqual(bytes(self.received[0]), b'TM\x00\x01')
        self.assertEqual(bytes(self.received[1]), b'N2\x01science')
        self.assertTrue(await self.transport.transmit(0, b'TC\x00'))
        self.assertTrue(await self.transport.transmit(1, b'N2\x04retry'))
        self.assertEqual(await read_master(self.ports[0][0], 3), b'TC\x00')
        self.assertEqual(await read_master(self.ports[1][0], 8), b'N2\x04retry')
        self.assertFalse(await self.transport.transmit(2, b'invalid'))
        self.assertFalse(await self.transport.transmit(0, b'x' * 4097))

    async def test_same_alias_and_exclusive_ownership_refused(self):
        with self.assertRaisesRegex(ValueError, 'distinct'):
            SerialGround(self.paths[0], self.paths[0], None, None, None)
        alias = Path(self.tmp.name) / 'alias'
        alias.symlink_to(self.paths[0])
        with self.assertRaisesRegex(ValueError, 'distinct|same'):
            SerialGround(self.paths[0], str(alias), None, None, None)
        contender = SerialGround(*self.paths, None, None, lambda *a, **kw: None)
        try:
            with self.assertRaises(OSError): contender.open()
        finally:
            contender.close()

    async def test_disconnect_reconnect_stable_explicit_alias(self):
        self.transport.close()
        self.task.cancel()
        await asyncio.gather(self.task, return_exceptions=True)
        link = Path(self.tmp.name) / 'science'
        link.symlink_to(self.paths[1])
        async def receive(channel, data): self.received[channel].extend(data)
        async def disconnected(channel, detail): self.failures.append(channel)
        self.transport = SerialGround(self.paths[0], str(link), receive, disconnected, lambda *a, **kw: None)
        self.transport.open()
        self.task = asyncio.create_task(self.transport.run())
        os.close(self.ports[1][0])
        await until(lambda: 1 in self.failures)
        new = os.openpty(); os.set_blocking(new[0], False)
        self.ports.append(new)
        link.unlink(); link.symlink_to(os.ttyname(new[1]))
        await until(lambda: self.transport.endpoints[1].port is not None)
        os.write(new[0], b'fresh')
        await until(lambda: self.received[1] == b'fresh')
        self.assertTrue(await self.transport.transmit(1, b'new retry'))
        self.assertEqual(await read_master(new[0], 9), b'new retry')


@unittest.skipUnless(HAVE_DEPS and os.name == 'posix', 'requires pyserial/PyYAML and POSIX PTYs')
class GroundStreamTests(unittest.IsolatedAsyncioTestCase):
    async def test_tcp_yamcs_side_and_both_raw_usb_directions(self):
        ports = [os.openpty(), os.openpty()]
        for master, slave in ports: os.set_blocking(master, False)
        g = object.__new__(Ground)
        g.queue = asyncio.Queue(maxsize=128);g.science_queue = queue.Queue(maxsize=256)
        g.yamcs_writer = None;g.stop = asyncio.Event()
        with tempfile.TemporaryDirectory() as temporary:
            g.data = Path(temporary)
            g.radio = SerialGround(*(os.ttyname(slave) for master, slave in ports),
                                   g.frame, g.serial_disconnected, lambda *a, **kw: None)
            g.radio.open()
            server = await asyncio.start_server(g.yamcs_tcp, '127.0.0.1', 0)
            tasks = [asyncio.create_task(g.radio.run()), asyncio.create_task(g.transmission())]
            reader, writer = await asyncio.open_connection('127.0.0.1', server.sockets[0].getsockname()[1])
            try:
                writer.write(b'raw CCSDS command');await writer.drain()
                self.assertEqual(await read_master(ports[0][0], 17), b'raw CCSDS command')
                os.write(ports[0][0], b'raw TM downlink')
                self.assertEqual(await asyncio.wait_for(reader.readexactly(15), 3), b'raw TM downlink')
                os.write(ports[1][0], b'N2science')
                await until(lambda: not g.science_queue.empty())
                self.assertEqual(g.science_queue.get_nowait(), b'N2science')
                await g.queue.put((1, b'N2retry'))
                self.assertEqual(await read_master(ports[1][0], 7), b'N2retry')
            finally:
                writer.close();await writer.wait_closed()
                g.radio.close()
                for task in tasks:task.cancel()
                await asyncio.gather(*tasks, return_exceptions=True)
                server.close();await server.wait_closed()
                for master, slave in ports:
                    os.close(master);os.close(slave)

    def test_hil_ground_rejects_native_twin_artifact_pair(self):
        shared = dict(framework_sha='same', working_source_sha256='same',
                      dependency_states={'fprime': 'same'}, pdu_dependency={'sha': 'same'})
        with patch('ground.verify', side_effect=[shared, shared]):
            with self.assertRaisesRegex(RuntimeError, 'physical fc'):
                verify_artifacts(Path('/fc'), Path('/pi'), physical=True)
        fc = dict(shared, deployment_profile='yamcs-rfm23-hil', target='teensy41')
        pi = dict(shared, deployment_profile='yamcs-rfm23-hil', target='pi-zero-w-armv6hf', uvc_enabled=True)
        with patch('ground.verify', side_effect=[fc, pi]):
            self.assertEqual(verify_artifacts(Path('/fc'), Path('/pi'), physical=True), (fc, pi))
        for uvc_enabled in (False, None, 'true'):
            disabled = dict(pi, uvc_enabled=uvc_enabled)
            with patch('ground.verify', side_effect=[fc, disabled]):
                with self.assertRaisesRegex(RuntimeError, 'UVC payload capture'):
                    verify_artifacts(Path('/fc'), Path('/pi'), physical=True)

    async def test_actual_receiver_fragmented_coalesced_corruption_and_repair(self):
        with tempfile.TemporaryDirectory() as temporary:
            g = object.__new__(Ground)
            g.data = Path(temporary)
            g.queue = asyncio.Queue(maxsize=128)
            g.retry = RetryWriter(g.queue); g.retry.loop = asyncio.get_running_loop()
            g.science_queue = queue.Queue(maxsize=256)
            g.science_stop = threading.Event()
            g.yamcs_writer = None
            admitted = []
            class Publish:
                def submit(self, blob, manifest): admitted.append((blob, manifest)); return True
            g.publisher = Publish()
            g.receiver = PayloadReceiver('PTY contract', 115200, g.data/'received.fdp', 30,
                                         output_dir=g.data/'received', ext='.fdp', on_event=g.science_event,
                                         transfer_timeout_s=25, absolute_transfer_timeout_s=120,
                                         save_partial_on_timeout=True)
            worker = threading.Thread(target=g.science_worker)
            worker.start()
            try:
                blob = struct.pack('>HI', 5, 0x10027000) + b'full actual FDP payload' * 7
                header, data, end = packets(blob)
                # Noise and chunk boundaries are USB byte-stream artifacts, not RF envelopes.
                stream = b'noise' + header + b''.join(data) + end
                for start in range(0, len(stream), 13): await g.frame(1, stream[start:start+13])
                await until(lambda: len(admitted) == 1)
                self.assertEqual(admitted[0][0], blob)
                # A missing DATA packet produces a real RETRY on the science uplink.
                header, data, end = packets(blob, 18, 4)
                await g.frame(1, header + b''.join(data[1:]) + end)
                channel, retry = await asyncio.wait_for(g.queue.get(), 3);g.queue.task_done()
                self.assertEqual(channel, 1);self.assertEqual(retry[:4], b'N2\x04\x04')
                await g.frame(1, data[0])
                await until(lambda: len(admitted) == 2)
                self.assertEqual(admitted[-1][0], blob)
                self.assertGreaterEqual(admitted[-1][1]['retry_rounds'], 1)
                # Corrupt whole-file CRC never publishes; subsequent fresh file recovers.
                bad = bytearray(packets(blob, 19, 5)[0]);bad[-1] ^= 1
                _, data, end = packets(blob, 19, 5)
                await g.frame(1, bytes(bad) + b''.join(data) + end)
                header, data, end = packets(blob, 20, 6)
                await g.frame(1, header + b''.join(data) + end)
                await until(lambda: len(admitted) == 3)
                self.assertEqual([v[1]['product_id'] for v in admitted], [17, 18, 20])
            finally:
                g.science_stop.set()
                await asyncio.to_thread(worker.join, 3)
                self.assertFalse(worker.is_alive())

    async def test_disconnect_discards_queued_uplinks_and_resets_science(self):
        g = object.__new__(Ground)
        g.queue = asyncio.Queue(maxsize=3);g.science_queue = queue.Queue(maxsize=2)
        await g.queue.put((0, b'old command'));await g.queue.put((1, b'old retry'))
        g.yamcs_writer = None
        await g.serial_disconnected(1, 'unplug')
        self.assertTrue(g.queue.empty());self.assertEqual(g.queue._unfinished_tasks, 0)
        self.assertEqual(g.science_queue.get_nowait()[0], 'disconnect')

    async def test_disconnect_cancels_blocked_command_producer(self):
        g = object.__new__(Ground)
        g.queue = asyncio.Queue(maxsize=1);g.science_queue = queue.Queue(maxsize=2)
        g.yamcs_writer = None;g.yamcs_connection_task = None
        await g.queue.put((0, b'old queued command'))
        server = await asyncio.start_server(g.yamcs_tcp, '127.0.0.1', 0)
        reader, writer = await asyncio.open_connection('127.0.0.1', server.sockets[0].getsockname()[1])
        try:
            writer.write(b'old blocked command');await writer.drain()
            await until(lambda: g.queue._putters)
            await g.serial_disconnected(0, 'unplug')
            self.assertEqual(await asyncio.wait_for(reader.read(), 3), b'')
            await until(lambda: g.yamcs_connection_task is None)
            self.assertTrue(g.queue.empty())
            self.assertEqual(g.queue._unfinished_tasks, 0)
        finally:
            writer.close();await writer.wait_closed()
            server.close();await server.wait_closed()


if __name__ == '__main__':
    unittest.main()
