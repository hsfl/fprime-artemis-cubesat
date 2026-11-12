import asyncio
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import RadioPacket, RadioParser, crc16
from radio_ground import GroundLink

ROOT = Path(__file__).resolve().parents[3]


class RadioRecordTests(unittest.TestCase):
    def test_crc_known_vector(self):
        self.assertEqual(crc16(b"123456789"), 0x29B1)

    def test_incremental_crc_and_resynchronization(self):
        packet = RadioPacket(bytes(range(49)))
        encoded = packet.encode()
        parser = RadioParser()
        recovered = []
        corrupted = bytearray(encoded)
        corrupted[-1] ^= 1
        for byte in b"junkR" + bytes(corrupted) + encoded:
            recovered += parser.feed(bytes([byte]))
        self.assertEqual(recovered, [packet])
        self.assertEqual(parser.crc_drops, 1)
        self.assertLessEqual(len(parser.buffer), 1)

    def test_lengths_and_header_fields_are_bounded(self):
        for data in (b"", bytes(50)):
            with self.assertRaises(ValueError):
                RadioPacket(data).encode()
        with self.assertRaises(ValueError):
            RadioPacket(b"a", to=256).encode()
        parser = RadioParser()
        self.assertEqual(parser.feed(b"RF\xff" + b"x" * 10000), [])
        self.assertLessEqual(len(parser.buffer), 1)


class GroundLinkTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.sent, self.delivered = [], []
        async def send(packet):
            self.sent.append(packet)
        async def deliver(channel, body):
            self.delivered.append((channel, body))
        self.link = GroundLink(ROOT, send, deliver)

    def incoming(self, body):
        return RadioPacket(body, self.link.ground, self.link.satellite, self.link.network, self.link.version)

    async def test_two_channels_reassemble_independently_and_drop_duplicates(self):
        a = bytes(range(100))
        b = b"N2science"
        await self.link.receive(self.incoming(bytes((0xA5, 2, 0, 3, 44)) + a[:44]))
        science = self.incoming(bytes((0xA6, 2, 0, 1, len(b))) + b)
        await self.link.receive(science)
        await self.link.receive(science)
        await self.link.receive(self.incoming(bytes((0xA5, 2, 1, 3, 44)) + a[44:88]))
        await self.link.receive(self.incoming(bytes((0xA5, 2, 2, 3, 12)) + a[88:]))
        self.assertEqual(self.delivered, [(1, b), (0, a)])
        self.assertEqual(self.link.counters["duplicates"], 1)

    async def test_wrong_network_and_gap_do_not_deliver(self):
        valid = self.incoming(bytes((0xA5, 0, 0, 1, 1)) + b"x")
        await self.link.receive(RadioPacket(valid.data, valid.to, valid.source, valid.network ^ 1, valid.version))
        await self.link.receive(self.incoming(bytes((0xA5, 0, 1, 2, 1)) + b"x"))
        self.assertEqual(self.delivered, [])
        self.assertEqual(self.link.counters["drops"], 2)

    async def test_uplink_ack_success_and_bounded_exhaustion(self):
        async def acknowledge(packet):
            self.sent.append(packet)
            body = packet.data
            await self.link.receive(self.incoming(bytes((body[0], body[1], 255, body[2], 0))))
        self.link.send_packet = acknowledge
        self.assertTrue(await self.link.transmit(0, bytes(128)))
        self.assertEqual(len(self.sent), 3)
        self.sent.clear()
        async def ignore(packet):
            self.sent.append(packet)
        self.link.send_packet = ignore
        self.link.rf["ack_timeout_ms"] = 1
        self.assertFalse(await self.link.transmit(0, b"lost"))
        self.assertEqual(len(self.sent), self.link.rf["ack_retries"] + 1)

    async def test_channel_one_uses_no_link_acks_and_independent_message_counter(self):
        self.assertTrue(await self.link.transmit(1, bytes(90)))
        self.assertEqual([packet.data[0] for packet in self.sent], [0xA6] * 3)
        self.assertEqual(self.link.next_message, [0, 1])


if __name__ == "__main__":
    unittest.main()
