// Exercise the actual CDR relay/router/proxy with in-memory serial streams.
#include <cassert>
#include <iostream>
#include "local_teensy_router.hpp"

using Bytes = std::vector<uint8_t>;
uint16_t crc(const Bytes& bytes) {
  uint16_t value = 0xffff;
  for (auto byte : bytes) {
    value ^= uint16_t(byte) << 8;
    for (int bit = 0; bit < 8; ++bit)
      value = (value & 0x8000) ? (value << 1) ^ 0x1021 : value << 1;
  }
  return value;
}
Bytes frame(uint8_t channel, const Bytes& payload) {
  Bytes result{0xd4, 0xc3, channel, uint8_t(payload.size()), uint8_t(payload.size() >> 8)};
  result.insert(result.end(), payload.begin(), payload.end());
  const auto checksum = crc(payload);
  result.push_back(uint8_t(checksum)); result.push_back(uint8_t(checksum >> 8));
  return result;
}
Bytes pduFrame(uint8_t type, uint8_t sequence) {
  Bytes result{0xa5, 2, type, 2, sequence, 0, 0}; // GET_SUMMARY_STATUS
  const auto checksum = crc(Bytes(result.begin() + 1, result.end()));
  result.push_back(uint8_t(checksum)); result.push_back(uint8_t(checksum >> 8));
  return result;
}
Bytes request(uint8_t sequence) {
  const auto inner = pduFrame(0, sequence);
  Bytes result{1, sequence, uint8_t(inner.size()), 0};
  result.insert(result.end(), inner.begin(), inner.end());
  return frame(2, result);
}
Bytes response(uint8_t sequence, const Bytes& inner) {
  Bytes result{1, sequence, 0, uint8_t(inner.size())};
  result.insert(result.end(), inner.begin(), inner.end());
  return frame(2, result);
}

int main() {
  assert(crc(Bytes{'1','2','3','4','5','6','7','8','9'}) == 0x29b1);
  HardwareSerial pdu;
  Stream pi;
  LinkCounters counters;
  PduProxy proxy(pdu);
  LocalTeensyRouter router(proxy, counters);
  RelayConfig config;
  config.enableCommandMode = false;
  RelayUartRf relay(pi, counters, config, nullptr, &router);
  proxy.begin(9600); relay.begin();
  assert(pdu.baud == 9600);

  // A fragmented outer frame must wait until complete, then forward exact bytes.
  auto first = request(7);
  pi.inject(Bytes(first.begin(), first.begin() + 6)); relay.poll();
  assert(pdu.tx.empty());
  pi.inject(Bytes(first.begin() + 6, first.end())); relay.poll();
  assert(pdu.tx == pduFrame(0, 7));
  auto reply = pduFrame(1, 7);
  pdu.inject(Bytes(reply.begin(), reply.begin() + 4)); relay.poll();
  assert(pi.tx.empty());
  pdu.inject(Bytes(reply.begin() + 4, reply.end())); relay.poll();
  assert(pi.tx == response(7, reply));

  pdu.tx.clear(); pi.tx.clear();
  auto corrupt = request(8); corrupt.back() ^= 0x80;
  pi.inject(corrupt); relay.poll();
  assert(pdu.tx.empty() && pi.tx.empty() && counters.crcDrops == 1);

  // No PDU response produces the existing correlated local timeout response.
  pi.inject(request(9)); relay.poll();
  assert(pdu.tx == pduFrame(0, 9));
  testMillis += 351; relay.poll();
  assert(pi.tx == frame(2, Bytes{1, 9, link_protocol::TEENSY_STATUS_TIMEOUT, 0}));

  // Recovery after timeout must accept and return the next request.
  pi.tx.clear(); pdu.tx.clear();
  pi.inject(request(10)); relay.poll();
  pdu.inject(pduFrame(1, 10)); relay.poll();
  assert(pi.tx == response(10, pduFrame(1, 10)));

  // A well-formed legacy RF-status operation reports unavailable, never OK.
  pi.tx.clear(); pdu.tx.clear();
  pi.inject(frame(2, Bytes{link_protocol::TEENSY_TARGET_RF_STATUS, 11, 1, 0,
                         link_protocol::TEENSY_RF_OP_LINK_STATS}));
  relay.poll();
  assert(pi.tx == frame(2, Bytes{link_protocol::TEENSY_TARGET_RF_STATUS, 11,
                                link_protocol::TEENSY_STATUS_TARGET_ERROR, 0}));
  assert(pdu.tx.empty());

  // Legacy radio channels cannot reach the PDU or produce RF output in CDR.
  pi.tx.clear();
  pi.inject(frame(0, Bytes{1,2,3})); pi.inject(frame(1, Bytes{4,5,6}));
  for (int i = 0; i < 1000; ++i) { ++testMillis; relay.poll(); }
  assert(pdu.tx.empty() && pi.tx.empty());
  std::cout << "PASS: fragmented channel-2 roundtrip, CRC rejection, PDU timeout/recovery, RF rejection, radio-channel isolation\n";
}
