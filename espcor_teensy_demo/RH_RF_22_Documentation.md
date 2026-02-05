# RH_RF22 — Driver Cheat‑Sheet (RadioHead)

**Use this when the agent doesn’t have the library files.** This summarizes the real `RH_RF22.h` surface area and the Si443x/RFM22B quirks.

For this project we USE the RFM23BP Radio.

---

## What it wraps
- Silicon Labs **Si4430/31/32** / HopeRF **RFM22/23** FSK/GFSK/OOK transceivers.
- SPI + **NIRQ** (interrupt). Optional **SDN**. GPIO0/GPIO1 may drive the antenna switch.

## Key compile‑time constants
- `RH_RF22_MAX_MESSAGE_LEN` (default overridden to **50** here; 1‑byte length allows up to 255)
- `RH_RF22_FIFO_SIZE = 64`
- FIFO thresholds: TX **AEM=4**, RX **FULL=55**

## Useful register bits (names match header)
- **INT status**: `REG_03/04` (read‑to‑clear): `IPKSENT`, `IPKVALID`, `ICRCERROR`, etc.
- **INT enable**: `REG_05/06`.
- **Mode**: `REG_07` (`TXON`, `RXON`, `PLLON`, `XTON`, `SWRES`).
- **Mode2**: `REG_08` (`FFCLRTX`, `FFCLRRX`, `AUTOTX`, `RXMPK`).
- **Pkt ctrl**: `REG_30` (CRC on/off, polynomial `CRC[1:0]`).
- **Header ctrl**: `REG_32/33` (broadcast/header check, fixed/var length).
- **Preamble/sync**: `REG_34–39`.
- **Power**: `REG_6D` (`TXPOW_*`, RF23B/BP variants).
- **Modem**: `REG_6E–72`, `REG_1C`, `REG_20–25`, `REG_69` (AFC/AGC, rate, deviation, IF BW).
- **Freq**: `REG_75–77` (+ optional hop `REG_79/7A`).

---

## Class surface (public)
**Construction**
```cpp
RH_RF22(uint8_t ss=SS, uint8_t nirq=2, RHGenericSPI& spi=hardware_spi);
```
Multiple instances supported (up to 3) with unique NIRQ lines.

**Bring‑up & status**
- `bool init();` (defaults: 434.0 MHz, `FSK_Rb2_4Fd36`, attaches ISR)
- `void reset();`
- `uint8_t statusRead();` (REG_02)
- `uint8_t ezmacStatusRead();`
- `uint8_t rssiRead();` (instantaneous, **not dBm**; for last‑packet dBm use base‑class `lastRssi()`)
- `uint32_t getLastPreambleTime();`

**RF setup**
- `bool setFrequency(float centreMHz, float afcPullIn=0.05);`
- `bool setFHStepSize(uint8_t fhs10kHz);` / `bool setFHChannel(uint8_t ch);`
- `void setTxPower(uint8_t level /* RH_RF22_TXPOW_* */);`
- `void setPreambleLength(uint8_t nibbles /* 4‑bit each */);`
- `void setSyncWords(const uint8_t* words, uint8_t len /*1..4*/);`
- `void setModemRegisters(const ModemConfig* m);`
- `bool setModemConfig(ModemConfigChoice idx);`
- `bool setCRCPolynomial(CRCPolynomial poly /* CCITT, 16‑IBM (default), IEC‑16, Biacheva */);`
- `void setGpioReversed(bool reversed=false);` (swap TX_ANT/RX_ANT wiring quirk)
- `void setOpMode(uint8_t modeBits);` / `void setIdleMode(uint8_t modeBits);`

**TX/RX**
- `bool send(const uint8_t* data, uint8_t len);`  (len>0)
- `bool available();`
- `bool recv(uint8_t* buf, uint8_t* len);`
- `void setModeIdle();` `void setModeRx();` `void setModeTx();`
- Base‑class waits: `waitPacketSent([timeout])`, `waitAvailableTimeout(timeout)`, etc.
- `virtual bool sleep();`

**Headers & addressing**
- Uses standard RadioHead headers (TO, FROM, ID, FLAGS). `setPromiscuous(true)` accepts any TO.

**Platform note**
- ESP8266 overrides `waitPacketSent()` and exposes `loopIsr()` (move SPI from ISR to loop context).

---

## Modem presets (`ModemConfigChoice`)
A curated subset—match both ends:
- **FSK**: `FSK_Rb2Fd5`, `FSK_Rb9_6Fd45`, `FSK_Rb38_4Fd19_6`, `FSK_Rb125Fd125`, `FSK_Rb_512Fd2_5` (POCSAG), …
- **GFSK**: `GFSK_Rb2Fd5`, `GFSK_Rb57_6Fd28_8`, `GFSK_Rb125Fd125`, …
- **OOK**: `OOK_Rb1_2Bw75`, `OOK_Rb9_6Bw335`, `OOK_Rb38_4Bw335`, `OOK_Rb40Bw335`.
- **UnmodulatedCarrier** for test tones; `FSK_PN9_Rb2Fd5` for PN9 test.

---

## Power levels (REG_6D)
- **RFM22B**: `TXPOW_1/2/5/8/11/14/17/20 dBm`
- **RFM23B**: `RF23B_TXPOW_*` (‑8…+13 dBm)
- **RFM23BP**: `RF23BP_TXPOW_28/29/30 dBm` (requires robust 5V rail; PA drive caveats)

---

## Packet format (actual)
```
[PREAMBLE 4B][SYNC 2B default 0x2D,0xD4]
[TO][FROM][ID][FLAGS][LENGTH][DATA…][CRC16(IBM)]
```
- `LENGTH` is **payload length** (0..255). Driver caps by `maxMessageLength()`.

---

## Typical flows
**Init**
```cpp
RH_RF22 rf22(SS, NIRQ);
if (!rf22.init()) fail();
rf22.setFrequency(915.0);
rf22.setModemConfig(RH_RF22::GFSK_Rb38_4Fd19_6);
rf22.setTxPower(RH_RF22_TXPOW_11DBM);
```
**TX → RX (ping/pong)**
```cpp
rf22.send(data, len);
rf22.waitPacketSent(100);
rf22.setModeRx();
if (rf22.waitAvailableTimeout(50)) { uint8_t n=sizeof(buf); if (rf22.recv(buf,&n)) { /*…*/ } }
```

---

## Hardware & wiring notes
- **Default SPI wiring**: MOSI/MISO/SCK are shared, but each RH_RF22 instance needs its own SS and interrupt line. RF22/RFM22/RFM23 modules expect GND, 3V3 (use a 5V rail for RFM23BP/ shields with regulators), optional SDN, and GPIO0/1 tied to TX_ANT/RX_ANT (or reversed boards discussed below). When moving SS away from the default (e.g., D10 on Uno/Due or D53 on Mega), force the unused default pin to `OUTPUT` so the hardware SPI master stays active as noted in the library warnings.
- **Supported boards**: the RadioHead docs list working pinouts for
  - Sparkfun RFM22 shield / Arduino Uno & Duemilanove (D10 SS, D2 NIRQ),
  - Mega 2560 (D50–D53 for SPI, D10 SS, D2 NIRQ; bend or jumper the shield if plugging it in directly),
  - ChipKit Uno32 (D10 SS, D38 NIRQ with JP4=RD4),
  - Teensy 3.1 (D10 SS, D2 NIRQ),
  - Due (SPI header pins for SCK/MOSI/MISO with D10 SS and D2 NIRQ),
  - STM32 F4 Discovery (PB0 SS, PB1 NIRQ, PB3/5/4 SPI),
  - ESP32 (GPIO13 SS, GPIO15 NIRQ),
  - ESP8266 ESP-12 (D5 SS, D4 NIRQ—watch for broken D4/D5 silks), and
  - AtTiny x16 series (PC0 SS, PA6 NIRQ, PA3/1/2 for SPI).
  These are just reference connections; you can override the default pins by passing them to the `RH_RF22` constructor.
- **RFM23BP high-power caution**: RF23BPs require a firm 5V rail and draw several hundred milliamps at 28‑30 dBm. Ensure GPIO0→RXON and GPIO1→TXON (or call `setGpioReversed(true)` for reversed modules) and keep the RF bias pins wired to the helper functions (`setRadioAmpTransmit/Receive/Idle`). Avoid starving the regulator or USB port—symptoms include hanging, mystery resets, incorrect power, or RF PA overheating.
- **Multiple radios & interrupts**: up to three radios can coexist (the driver tracks `_deviceForInterrupt[]`), but each still needs a unique SS and interrupt. Keep other SPI devices from sharing interrupts by wrapping their transfers with `cli()/sei()` or temporarily disabling the RF22 interrupt handler (see `RHSPIDriver::spiUsingInterrupt()`).
- **Power & antenna pins**: RF22 modules can pull >80 mA at full transmit power; the Arduino 3.3V line only gives ~50 mA. Use level shifters, a dedicated 3.3V regulator, or the Sparkfun shield. If you buy bare RFM22 boards note the 3.3V IO—they do not tolerate 5V logic unless you add level shifting. Some RFM23BP boards have their TX_ANT/RX_ANT wires flipped; `setGpioReversed(true)` fixes this once you identify the symptom (TX stuck or RX silent). Some RFM23BP units also have unreliable NIRQ outputs when powered above ~3.3V—voltage dividers or running at 3.3V can help.
- **Packet format compatibility**: RH_RF22 packets use the RadioHead header/CRC (TO/FROM/ID/FLAGS + CRC16 IBM). This format is **not** compatible with the older RF22 or VirtualWire libraries, so migrating code requires constructing both an `RH_RF22` driver and a manager (`RHReliableDatagram`, `RHMesh`, etc.) and switching from `RF22_MAX_MESSAGE_LEN` to `RH_RF22_MAX_MESSAGE_LEN`.

## Memory & platform limits
- Program size: most RH_RF22 examples compile to 9–14 kB, so flash is usually fine.
- RAM: the Duemilanova’s 2 kB RAM is nearly exhausted when using this driver; Diecimila usually lacks enough. The Mega (8 kB SRAM) gives substantially more headroom, and the RHRouter/RHMesh samples still require it. Symptoms of RAM pressure include mysterious restarts, hanging, missing `Serial` output, and changes in timing when inserting unrelated `Serial.print()` calls.
- ESP8266/ESP32: the ESP8266 port overrides `waitPacketSent()` and introduces `loopIsr()` so that SPI transfers happen in `loop()` rather than in the interrupt, avoiding long ISR execution.

## Compatibility with RF22
- The constructor changed (now `RH_RF22 driver; RHReliableDatagram manager(driver, CLIENT_ADDRESS);`) and you now need both a driver and a manager object for addressed/reliable packets.
- Modem configuration indexes have shifted; use the symbolic `ModemConfigChoice` names instead of literal numbers.
- RadioHead 1.6 changed how interrupt pins are specified on Arduino/Uno32 platforms—pass the **actual GPIO pin number**, not the legacy “interrupt number.” Example differences appear in the RadioHead docs.
- Replace `RF22_MAX_MESSAGE_LEN` references with `RH_RF22_MAX_MESSAGE_LEN` when sharing buffers or defining array sizes.

---

## FIFO & interrupt hygiene
**Read both** `INT_STATUS1/2` to clear. If things wedge:
```cpp
rf22.setModeIdle();
rf22.spiWriteRegister(RH_RF22_REG_08_OPERATING_MODE2,
                      RH_RF22_FFCLRTX | RH_RF22_FFCLRRX);
(void)rf22.spiReadRegister(RH_RF22_REG_03_INTERRUPT_STATUS1);
(void)rf22.spiReadRegister(RH_RF22_REG_04_INTERRUPT_STATUS2);
rf22.setModeRx();
```
When to do this: CRC storms, aborted TX, or `waitPacketSent()` timeouts.

---

## Antenna switch gotcha
Some boards wire **GPIO0↔RX_ANT** and **GPIO1↔TX_ANT** reversed. Call `setGpioReversed(true)` after `init()` if you see TX stuck / RX deaf.

---

## TX/RX amplifier gating
- The Teensy ground station firmware drives the RFM23BP `RX_ON` and `TX_ON` inputs from `RADIO_RX_ON_PIN` (Arduino pin 30) and `RADIO_TX_ON_PIN` (pin 31). `initRadio()` configures those pins as outputs, defaults to listenable mode (RX high / TX low), and helper routines like `setRadioAmpTransmit()`, `setRadioAmpReceive()`, and `setRadioAmpIdle()` toggle them per the datasheet so the amplifier bias and antenna switch stay in the expected state.
- Mirror that wiring in your setup and keep both signals low for idle to avoid inadvertently biasing the PA; the Teensy helpers show the settled logic levels (transmit = `RX_ON` high / `TX_ON` low, receive = `RX_ON` low / `TX_ON` high, idle = both low).

---

## Reliability tips
- Match **frequency, modem preset, preamble, sync, CRC** across nodes.
- Keep messages **≤ `maxMessageLength()`** (consider your build‑time override of 50).
- After TX expecting a reply, **force `setModeRx()`**.
- For ack/retry, pair with `RHReliableDatagram` or mirror its pattern (ID‑based acks with timeouts).

---

## Quick answers
- **Why `send()` false?** len==0 or (if CAD/LBT used upstream) channel busy; or state jam → do FIFO reset.
- **RSSI units?** `rssiRead()` is chip units; `lastRssi()` (base class) gives dBm for last packet.
- **Sleep?** `sleep()` lowers power; wake by entering Idle/Tx/Rx (adds wake latency).

*That’s the RH_RF22 essentials. Keep this short; defer fine register math to the header or datasheet when needed.*
