#ifndef ARTEMIS_TEENSY_USB_TX_PROGRESS_HPP
#define ARTEMIS_TEENSY_USB_TX_PROGRESS_HPP

#include <stddef.h>
#include <stdint.h>

namespace usb_tx {

static constexpr uint8_t CHANNEL_CCSDS = 0;
static constexpr uint8_t CHANNEL_PAYLOAD = 1;
static constexpr uint8_t CHANNEL_COUNT = 2;

enum class WriteResult : uint8_t {
  BACKPRESSURE = 0,
  ZERO_WRITE = 1,
  PARTIAL = 2,
  COMPLETE = 3,
};

struct WriteAttempt {
  WriteResult result;
  uint16_t written;
};

struct ChannelCounters {
  uint32_t zeroWrites = 0;
  uint32_t partialWrites = 0;
  uint32_t backpressureEvents = 0;
  uint32_t recoveries = 0;
  uint32_t queueHighWater = 0;
  uint32_t explicitDiscards = 0;

  void reset() {
    zeroWrites = 0;
    partialWrites = 0;
    backpressureEvents = 0;
    recoveries = 0;
    queueHighWater = 0;
    explicitDiscards = 0;
  }
};

class QueueAccounting {
 public:
  QueueAccounting() { reset(); }

  void reset() {
    for (uint8_t channel = 0; channel < CHANNEL_COUNT; channel++) {
      m_channels[channel].reset();
    }
  }

  ChannelCounters* forChannel(uint8_t channel) {
    return channel < CHANNEL_COUNT ? &m_channels[channel] : nullptr;
  }

  const ChannelCounters* forChannel(uint8_t channel) const {
    return channel < CHANNEL_COUNT ? &m_channels[channel] : nullptr;
  }

  bool admit(uint8_t channel, uint8_t currentCount, uint8_t depth) {
    ChannelCounters* counters = forChannel(channel);
    if (counters == nullptr) {
      return false;
    }
    if (currentCount >= depth) {
      counters->explicitDiscards += 1;
      return false;
    }
    const uint32_t nextCount = static_cast<uint32_t>(currentCount) + 1U;
    if (nextCount > counters->queueHighWater) {
      counters->queueHighWater = nextCount;
    }
    return true;
  }

  void discard(uint8_t channel) {
    ChannelCounters* counters = forChannel(channel);
    if (counters != nullptr) {
      counters->explicitDiscards += 1;
    }
  }

 private:
  ChannelCounters m_channels[CHANNEL_COUNT];
};

template <typename Writer>
WriteAttempt writeAvailable(Writer& writer,
                            const uint8_t* data,
                            uint16_t length,
                            uint16_t& offset) {
  if (data == nullptr || offset >= length) {
    return {WriteResult::COMPLETE, 0};
  }

  const int available = writer.availableForWrite();
  if (available <= 0) {
    return {WriteResult::BACKPRESSURE, 0};
  }

  const uint16_t remaining = static_cast<uint16_t>(length - offset);
  const size_t requested = static_cast<size_t>(available) < remaining
                               ? static_cast<size_t>(available)
                               : static_cast<size_t>(remaining);
  size_t written = writer.write(data + offset, requested);
  if (written > requested) {
    written = requested;
  }
  if (written == 0) {
    return {WriteResult::ZERO_WRITE, 0};
  }

  offset = static_cast<uint16_t>(offset + written);
  return {offset == length ? WriteResult::COMPLETE : WriteResult::PARTIAL,
          static_cast<uint16_t>(written)};
}

inline void observeWrite(ChannelCounters& counters,
                         const WriteAttempt& attempt,
                         bool& deliveryImpeded) {
  switch (attempt.result) {
    case WriteResult::BACKPRESSURE:
      counters.backpressureEvents += 1;
      deliveryImpeded = true;
      break;
    case WriteResult::ZERO_WRITE:
      counters.zeroWrites += 1;
      deliveryImpeded = true;
      break;
    case WriteResult::PARTIAL:
      counters.partialWrites += 1;
      deliveryImpeded = true;
      break;
    case WriteResult::COMPLETE:
      if (deliveryImpeded) {
        counters.recoveries += 1;
      }
      deliveryImpeded = false;
      break;
  }
}

}  // namespace usb_tx

#endif
