#pragma once
// Host-only serial/watchdog shim. No physical hardware is accessed.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <vector>

inline uint32_t testMillis = 0;
inline uint32_t millis() { return testMillis; }
inline void delay(uint32_t value) { testMillis += value; }
inline void yield() {}
inline volatile uint32_t SRC_SRSR = 0, WDOG1_WSR = 0, CCM_CCGR3 = 0, WDOG1_WCR = 0;
#define SRC_SRSR_WDOG_RST_B 1
#define CCM_CCGR_ON 1
#define CCM_CCGR3_WDOG1(x) (x)
#define WDOG_WCR_WDE 1
#define WDOG_WCR_WT(x) (x)
#define WDOG_WCR_SRS 2

class Stream {
 public:
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
  virtual ~Stream() = default;
  virtual int available() { return static_cast<int>(rx.size()); }
  virtual int read() {
    if (rx.empty()) return -1;
    auto value = rx.front(); rx.pop_front(); return value;
  }
  virtual size_t write(uint8_t value) { tx.push_back(value); return 1; }
  virtual size_t write(const uint8_t* bytes, size_t length) {
    tx.insert(tx.end(), bytes, bytes + length); return length;
  }
  virtual void flush() {}
  void inject(const std::vector<uint8_t>& bytes) { rx.insert(rx.end(), bytes.begin(), bytes.end()); }
};

class HardwareSerial : public Stream {
 public:
  uint32_t baud = 0;
  void begin(uint32_t value) { baud = value; }
};
