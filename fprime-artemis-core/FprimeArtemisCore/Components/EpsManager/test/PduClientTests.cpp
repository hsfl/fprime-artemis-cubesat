#include "../PduClient.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace Artemis::Eps;

std::vector<std::uint8_t> response(const PduClient& client, const std::vector<std::uint8_t>& payload,
                                 std::uint8_t status = PDU_V2_STATUS_OK) {
    const auto* request = client.requestBytes();
    std::vector<std::uint8_t> bytes = {PDU_V2_SOF, PDU_V2_VERSION, PDU_V2_MSG_RESPONSE,
                                     request[3], request[4], status, static_cast<std::uint8_t>(payload.size())};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    const auto crc = PduClient::crc16(bytes.data() + 1, bytes.size() - 1);
    bytes.push_back(static_cast<std::uint8_t>(crc)); bytes.push_back(static_cast<std::uint8_t>(crc >> 8));
    return bytes;
}
void recalculate(std::vector<std::uint8_t>& bytes) {
    const auto crc = PduClient::crc16(bytes.data() + 1, bytes.size() - 3);
    bytes[bytes.size() - 2] = static_cast<std::uint8_t>(crc);
    bytes[bytes.size() - 1] = static_cast<std::uint8_t>(crc >> 8);
}
void tests() {
    const std::uint8_t known[] = {'1','2','3','4','5','6','7','8','9'};
    assert(PduClient::crc16(known, sizeof(known)) == 0x29B1);
    assert(sizeof(PduClient) < 512);
    assert(PduClient::normalRail(PDU_OUTPUT_3V3_1) && PduClient::normalRail(PDU_OUTPUT_12V));
    assert(!PduClient::normalRail(PDU_OUTPUT_5V_4_RESERVED));
    assert(!PduClient::normalRail(PDU_OUTPUT_BURN1) && !PduClient::normalRail(PDU_OUTPUT_HBRIDGE1));
    assert(!PduClient::normalRail(PDU_V2_OUTPUT_ALL));
    assert(PduClient::outputBit(PDU_OUTPUT_12V) == 5 && PduClient::outputBit(PDU_OUTPUT_BURN2) == 8);
    assert(PduClient::outputBit(PDU_OUTPUT_5V_4_RESERVED) == -1);
    PduClient client;
    Result result;
    assert(!client.request(PDU_V2_OP_PING, nullptr, 1, 0));
    assert(!client.request(PDU_V2_OP_PING, known, PDU_V2_MAX_PAYLOAD_LEN + 1, 0));
    assert(client.request(PDU_V2_OP_PING, nullptr, 0, 0));
    assert(client.requestLength() == PDU_V2_HEADER_LEN + PDU_V2_CRC_LEN);
    assert(client.requestBytes()[0] == PDU_V2_SOF && client.requestBytes()[2] == PDU_V2_MSG_REQUEST);
    assert(!client.request(PDU_V2_OP_GET_SUMMARY_STATUS, nullptr, 0, 0));
    auto good = response(client, {PDU_V2_VERSION});
    auto bad = good; bad.back() ^= 1;
    client.receive(bad.data(), bad.size(), 1);
    assert(client.pending() && client.crcErrors() == 1 && !client.takeResult(result));
    auto stale = good; stale[4]++; recalculate(stale);
    client.receive(stale.data(), stale.size(), 2);
    stale = good; stale[3] = PDU_V2_OP_GET_SUMMARY_STATUS; recalculate(stale);
    client.receive(stale.data(), stale.size(), 3);
    stale = good; stale[1]++; recalculate(stale);
    client.receive(stale.data(), stale.size(), 4);
    stale = good; stale[2] = PDU_V2_MSG_EVENT; recalculate(stale);
    client.receive(stale.data(), stale.size(), 5);
    assert(client.pending() && client.ignoredResponses() == 4);
    for (const auto byte : good) client.receive(&byte, 1, 6);
    assert(!client.pending());
    assert(!client.request(PDU_V2_OP_PING, nullptr, 0, 7)); // Unconsumed result cannot be overwritten.
    assert(client.takeResult(result) && result.completion == Completion::RESPONSE);
    assert(result.opcode == PDU_V2_OP_PING && result.status == PDU_V2_STATUS_OK);
    assert(result.length == PDU_V2_PING_LEN && result.payload[0] == PDU_V2_VERSION);
    assert(!client.takeResult(result));

    assert(client.request(PDU_V2_OP_GET_SUMMARY_STATUS, nullptr, 0, 100));
    good = response(client, std::vector<std::uint8_t>(PDU_V2_SUMMARY_STATUS_LEN, 0));
    client.receive(good.data(), 4, 100);
    client.tick(201);
    assert(client.framingErrors() == 1 && client.pending());
    const std::uint8_t noise[] = {0, 1, 2, 3, 4};
    client.receive(noise, sizeof(noise), 201);
    auto malformed = good; malformed[6] = PDU_V2_MAX_PAYLOAD_LEN + 1;
    client.receive(malformed.data(), PDU_V2_HEADER_LEN, 202);
    assert(client.framingErrors() == 2);
    client.receive(good.data(), good.size(), 203);
    assert(client.takeResult(result) && result.length == PDU_V2_SUMMARY_STATUS_LEN);
    assert(client.request(PDU_V2_OP_SET_OUTPUT_STATE, known, 2, 300));
    good = response(client, {}, PDU_V2_STATUS_BUSY);
    client.receive(good.data(), good.size(), 301);
    assert(client.takeResult(result) && result.status == PDU_V2_STATUS_BUSY);

    assert(client.request(PDU_V2_OP_PING, nullptr, 0, 0xFFFFFF00U));
    client.tick(0xFFFFFF00U + 999U);
    assert(client.pending());
    client.tick(0xFFFFFF00U + 1000U);
    assert(!client.pending() && client.timeouts() == 1);
    assert(client.takeResult(result) && result.completion == Completion::TIMEOUT);
    assert(client.request(PDU_V2_OP_PING, nullptr, 0, 1));
    client.transportResult(false);
    assert(client.takeResult(result) && result.completion == Completion::TRANSPORT_ERROR);

    std::vector<std::uint8_t> maximum(PDU_V2_MAX_PAYLOAD_LEN, 0xA5);
    for (unsigned iteration = 0; iteration < 260; ++iteration) {
        assert(client.request(PDU_V2_OP_GET_PROTOCOL_INFO, nullptr, 0, iteration));
        assert(client.requestBytes()[4] != 0);
        good = response(client, maximum);
        client.receive(good.data(), good.size(), iteration);
        assert(client.takeResult(result) && result.length == PDU_V2_MAX_PAYLOAD_LEN);
        assert(std::equal(maximum.begin(), maximum.end(), result.payload.begin()));
    }
}
std::vector<std::uint8_t> fromHex(const std::string& text) {
    assert(text.size() % 2 == 0);
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i < text.size(); i += 2)
        bytes.push_back(static_cast<std::uint8_t>(std::strtoul(text.substr(i, 2).c_str(), nullptr, 16)));
    return bytes;
}
void printHex(const std::uint8_t* bytes, std::size_t count) {
    const char* hex = "0123456789abcdef";
    for (std::size_t i = 0; i < count; ++i) std::cout << hex[bytes[i] >> 4] << hex[bytes[i] & 15];
    std::cout << '\n';
}
int main(int argc, char** argv) {
    if ((argc == 4 || argc == 5) && std::string(argv[1]) == "--wire") {
        PduClient client;
        const auto payload = fromHex(argv[3]);
        const auto opcode = static_cast<std::uint8_t>(std::strtoul(argv[2], nullptr, 0));
        if (!client.request(opcode, payload.data(), payload.size(), 0)) return 2;
        if (argc == 4) printHex(client.requestBytes(), client.requestLength());
        else {
            const auto bytes = fromHex(argv[4]); client.receive(bytes.data(), bytes.size(), 1);
            Result result;
            if (!client.takeResult(result) || result.completion != Completion::RESPONSE) return 3;
            std::cout << static_cast<unsigned>(result.status) << '\n';
            printHex(result.payload.data(), result.length);
        }
        return 0;
    }
    tests();
    std::cout << "PDU client: PASS (CRC, bounded framing, correlation, busy, wrap, timeout and rail policy)\n";
}
