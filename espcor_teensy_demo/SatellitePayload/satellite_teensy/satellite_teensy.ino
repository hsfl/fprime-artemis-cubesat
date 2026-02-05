/**
 * Satellite Payload Transmitter for EPSCOR C3M Project
 *
 * This Arduino sketch runs on a Teensy microcontroller in the satellite payload
 * to capture thermal images from a Raspberry Pi via UART and transmit them
 * to the ground station via radio communication.
 *
 * Key Features:
 * - Triggers RPI thermal capture via UART command ("TRIGGER\n")
 * - Receives thermal image data from RPI via UART at 115200 baud
 * - Transmits image data via RFM23BP radio module in packetized format
 * - Handles reliable packet transmission with retry logic
 * - Provides transmission statistics and quality assessment
 * - Supports both capture and transmission operations
 *
 * Hardware Requirements:
 * - Teensy 4.1 microcontroller
 * - RF23BP radio module (433MHz)
 * - Raspberry Pi with thermal camera
 * - UART connection between Teensy and RPI
 *
 * Communication Protocol:
 * - UART Trigger: Sends "TRIGGER\n" command to initiate RPI capture
 * - UART Data: Receives thermal data with header/end markers
 * - Radio: Transmits packetized data with header/data/end packets
 *
 * @author EPSCOR C3M Team
 * @date 2025-12-08
 * @version 2.0.0
 */

// Simplified Transmitter with UART (Essential Functions Only)
// UART TRANSMITTER CODE - satellite teensy (8/25/2025)
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GPS.h>
#include <Adafruit_LIS3MDL.h>
#include <Adafruit_LSM6DSOX.h>
#include <Adafruit_Sensor.h>
/**
 * https://www.airspayce.com/mikem/arduino/RadioHead/
 * RH_RF22 Works with Hope-RF RF22B and RF23B based transceivers, and compatible chips and modules,
 * including the RFM22B transceiver module such as hthis bare module: https://www.sparkfun.com/products/10153 and
 * this shield: https://www.sparkfun.com/products/11018 and this board: https://www.anarduino.com/miniwireless and RF23BP modules
 * such as: https://www.anarduino.com/details.jsp?pid=130 Supports GFSK, FSK and OOK. Access to other chip features such as
 * on-chip temperature measurement, analog-digital converter, transmitter power control etc is also provided.
 */
#include <RH_RF22.h>
#include <RHHardwareSPI1.h>
#include <Wire.h>
#include <Adafruit_INA219.h>

/**  Build configuration flags
 *  Uncomment ONE of these for your build target:
 * */
// #define DEBUG    // Verbose logging for development
#define FLIGHT // Flight mode - minimal logging

// Packed struct attribute for ensuring no padding bytes
#define PACKED __attribute__((packed))

// Pin definitions for hardware control
const uint8_t RPI_ENABLE = 36; // Power control pin for Raspberry Pi
const uint8_t LED_PIN = 13;

// UART pins: Serial2 uses pins 7 (RX) and 8 (TX) automatically
// UART trigger command to signal RPI for capture
const char UART_TRIGGER_CMD[] = "TRIGGER\n";

// Radio configuration pins and object
const int RADIO_CS = 38;  // Chip select pin for RF22 module
const int RADIO_INT = 40; // Interrupt pin for RF22 module
/** Pin 30 from Arduino to RFM23BP RX_ON PIN */
const uint8_t RADIO_RX_ON_PIN = 30;
/** Pin 31 from Arduino to RFM23BP RX_ON PIN */
const uint8_t RADIO_TX_ON_PIN = 31;
// note that hardware_spi1 uses the RHHardwareSPI1.h library (same as using SPI1 bus but more explicit for the RH_RF22 library driver)
RH_RF22 rf23(RADIO_CS, RADIO_INT, hardware_spi1);
const int RADIO_WAIT_PACKET_SENT_MS = 500;

// === UART / Framing constants ===
#define UART_BAUD 115200 // <-- set this to match the Pi; 921600 is fine on Teensy 4.1

const uint8_t UART_MAGIC[4] = {0xDE, 0xAD, 0xBE, 0xEF};
const uint8_t UART_END[2] = {0xFF, 0xFF};

const uint32_t UART_HEADER_TIMEOUT_MS = 15000;  // 15s to see header (Pi capture + prep time)
const uint32_t UART_PAYLOAD_TIMEOUT_MS = 30000; // 30s to receive payload
const uint32_t UART_END_TIMEOUT_MS = 1000;      // 1s to see end markers

// Simple max for STATUS messages
const uint16_t MAX_STATUS_LEN = 256;

uint8_t piStatusBuf[MAX_STATUS_LEN];
bool piCaptureInProgress = false;
bool RPI_IDLE_READY = false;

// Image data storage and tracking
uint16_t capturedImageLength = 0; // Length of captured thermal image

// Buffer for thermal image storage
const uint32_t MAX_IMG = 40000; // Maximum image buffer size (40KB)
uint8_t imgBuf[MAX_IMG];        // Buffer to store thermal image data

// Serial output redirection
String serialBuffer = "";            // Buffer to accumulate serial output
bool radioReady = false;             // Flag to indicate if radio is ready for transmission
bool serialBufferAtLineStart = true; // Tracks when to prepend the SAT> prefix

// Radio transmission parameters
const uint8_t RADIO_PACKET_MAX_SIZE = 49; // RF22 payload limit for reliable recv/tx
const int MAX_SEND_PACKET_RETRIES = 3;

const uint8_t THERMAL_PACKET_OVERHEAD = 2 /*packet index*/ + 2 /*CRC16*/;
const uint8_t PACKET_DATA_SIZE = RADIO_PACKET_MAX_SIZE - THERMAL_PACKET_OVERHEAD; // Max bytes of image data per radio packet

// Serial message radio transmission parameters
const uint8_t SERIAL_MSG_TYPE = 0xAA;                         // Message type identifier for serial output
const uint8_t MAX_SERIAL_MSG_LEN = RADIO_PACKET_MAX_SIZE - 2; // Maximum serial message length per packet payload
const uint8_t SERIAL_CONTINUATION_FLAG = 0x80;                // High bit indicates additional chunks follow

// Packet retry protocol
const uint8_t RETRY_REQUEST_TYPE = 0xBB; // Message type for requesting missing packets

// Livestream protocol constants
const uint8_t STREAM_FRAME_TYPE = 0xCC;                                 // Message type for livestream frame packets
const uint8_t STREAM_MAGIC[4] = {0xCA, 0xFE, 0xBA, 0xBE};               // Magic header for livestream frames from Pi
const uint16_t STREAM_FRAME_SIZE = 4800;                                // 80x60 8-bit = 4800 bytes per frame
const uint8_t STREAM_PACKETS_PER_FRAME = (STREAM_FRAME_SIZE + PACKET_DATA_SIZE - 1) / PACKET_DATA_SIZE;  // ~107 packets
const char STREAM_START_CMD[] = "STREAM_START\n";                       // Command to RPi to start streaming
const char STREAM_STOP_CMD[] = "STREAM_STOP\n";                         // Command to RPi to stop streaming
const char FRAME_REQUEST_CMD[] = "FRAME\n";                             // Command to RPi to send one frame

// Stream mode state
bool streamModeActive = false;
uint8_t streamFrameBuffer[STREAM_FRAME_SIZE];  // Buffer for one stream frame

// === Packet Structure Definitions ===

/**
 * Header packet structure for thermal image transmission
 * Sent first to inform ground station of total image size and packet count
 * Total size: 10 bytes
 */
struct PACKED ThermalHeaderPacket
{
  uint8_t marker1;       // 0xFF
  uint8_t marker2;       // 0xFF
  uint16_t imageLength;  // Total image size in bytes
  uint16_t totalPackets; // Number of data packets to follow
  uint16_t magic[2];     // {0xDEAD, 0xBEEF}
};

/**
 * Data packet structure for thermal image transmission
 * Contains a chunk of image data with CRC for validation
 * Size: 4 + PACKET_DATA_SIZE bytes (49 bytes max)
 * Note: For variable-length packets, CRC must be accessed via buffer offset, not struct member
 */
struct PACKED ThermalDataPacket
{
  uint16_t packetIndex;           // Packet sequence number
  uint8_t data[PACKET_DATA_SIZE]; // Image data chunk (45 bytes max)
  uint16_t crc16;                 // CRC16 of the data field only
};

/**
 * End packet structure for thermal image transmission
 * Signals completion and provides overall image CRC
 * Total size: 6 bytes
 */
struct PACKED ThermalEndPacket
{
  uint8_t marker1;      // 0xEE
  uint8_t marker2;      // 0xEE
  uint16_t packetCount; // Total packets sent
  uint16_t imageCrc;    // CRC16 of entire image
};

/**
 * Serial message packet structure for console output forwarding
 * Allows satellite to send debug/status messages to ground station
 * Size: 2 + message length
 */
struct PACKED SerialMessagePacket
{
  uint8_t messageType;              // SERIAL_MSG_TYPE (0xAA)
  uint8_t length;                   // Length with optional continuation flag (bit 7)
  uint8_t data[MAX_SERIAL_MSG_LEN]; // Message text
};

/**
 * Retry request packet structure (received from ground station)
 * Contains bitmap of missing packet indices that need retransmission
 * Offset allows handling images with >360 packets
 */
struct PACKED RetryRequestPacket
{
  uint8_t type;         // RETRY_REQUEST_TYPE (0xBB)
  uint16_t packetCount; // Number of missing packets in this request
  uint16_t offset;      // Starting packet index this bitmap covers
  uint8_t bitmap[45];   // 360 bits = 45 bytes, bit=1 means packet is missing
};

/**
 * Stream frame header packet structure for livestream transmission
 * Sent before each frame to inform ground station of frame data
 * Total size: 6 bytes
 */
struct PACKED StreamFrameHeaderPacket
{
  uint8_t type;         // STREAM_FRAME_TYPE (0xCC)
  uint8_t frameSeq;     // Frame sequence number (0-255, wrapping)
  uint16_t frameSize;   // Frame size in bytes (4800)
  uint16_t totalPackets; // Number of data packets for this frame
};

/**
 * Stream frame data packet structure
 * Contains chunk of stream frame data (no CRC for speed - best-effort delivery)
 * Size: 3 + data length bytes
 */
struct PACKED StreamDataPacket
{
  uint8_t type;           // STREAM_FRAME_TYPE (0xCC)
  uint8_t frameSeq;       // Frame sequence number to associate with header
  uint8_t packetIndex;    // Packet index within frame (0-106)
  uint8_t data[PACKET_DATA_SIZE]; // Frame data chunk
};

// Shared radio buffers to avoid stack allocations inside hot paths
uint8_t radioRxBuffer[RADIO_PACKET_MAX_SIZE + RADIO_PACKET_MAX_SIZE];
uint8_t radioTxBuffer[RADIO_PACKET_MAX_SIZE + RADIO_PACKET_MAX_SIZE];
uint8_t radioSerialTxBuffer[MAX_SERIAL_MSG_LEN + RADIO_PACKET_MAX_SIZE];

/**
 * Dumps a raw snapshot of the RF23 RX FIFO via radio debug output.
 * This bypasses RH_RF22::available() and reads the hardware FIFO directly.
 */
void dumpRf23PendingPackets();
void dumpRf23PacketHexLines(const uint8_t *data, uint8_t length);

/**
 * Retry handlers for missing packet requests from ground station
 */
void handleRetryRequest(uint8_t *buf, uint8_t len);
void sendSpecificPacket(uint16_t packetIndex);

// Define the analog input pins for the temperature sensors and their labels (pulled from Artemis Manual AIN# should be 7 pins)
const int temperatureSensorPins[] = {14, 15, 41, 20, 21, 22, 23};
const char *tempereatureSensorLabels[] = {"OBC", "PDU", "Battery Board", "Solar Panel 1", "Solar Panel 2", "Solar Panel 3", "Solar Panel 4"};
const int numTemperatureSensors = 7;

// TMP36 temperature sensor constants
const float MV_PER_DEGREE_F = 1.0;             // 1 mV/°F
const float OFFSET_F = 58.0;                   // 58 mV (58°F) offset in the output voltage
const float MV_PER_ADC_UNIT = 3300.0 / 1024.0; // Assuming 3.3V reference voltage and 10-bit ADC resolution

// Define the I2C address and labels for each current sensor
#define INA219_ADDR_SOLAR1 0x40  // Solar Panel 1
#define INA219_ADDR_SOLAR2 0x41  // Solar Panel 2
#define INA219_ADDR_SOLAR3 0x42  // Solar Panel 3
#define INA219_ADDR_SOLAR4 0x43  // Solar Panel 4
#define INA219_ADDR_BATTERY 0x44 // Battery Board

// Create an array of current sensors and their labels
Adafruit_INA219 currentSensors[] = {
    Adafruit_INA219(INA219_ADDR_SOLAR1),
    Adafruit_INA219(INA219_ADDR_SOLAR2),
    Adafruit_INA219(INA219_ADDR_SOLAR3),
    Adafruit_INA219(INA219_ADDR_SOLAR4),
    Adafruit_INA219(INA219_ADDR_BATTERY)};
const char *currentSensorsLabels[] = {"Solar Panel 1", "Solar Panel 2", "Solar Panel 3", "Solar Panel 4", "Battery Board"};
const int numCurrentSensors = 5;

// GPS/IMU hardware
#define GPSSerial Serial7
Adafruit_GPS GPS(&GPSSerial);
const bool GPSECHO = false;
bool gpsConnected = false;

Adafruit_LIS3MDL lis = Adafruit_LIS3MDL();
Adafruit_LSM6DSOX lsm6dsox = Adafruit_LSM6DSOX();
bool imuConnected = false;

// Forward declarations for GPS/IMU helpers
void initGPS(bool announce = true);
void initIMU(bool announce = true);
void serviceGPS();
void printGPSData();
void printIMUData();
void handleSensorCommand(uint8_t *buf, uint8_t len);

// Forward declarations for stream mode
void startStreamMode();
void stopStreamMode();
void handleStreamMode();
void requestFrameFromPi();
bool waitForStreamMagic(uint32_t timeout_ms);
bool recvStreamFrameFromPi(uint8_t &frameSeq);
void sendStreamFrameViaRadio(uint8_t frameSeq);

/**
 * Sends a serial message via radio to ground station
 *
 * @param message The message string to send
 */
void radioPrint(const String &message)
{
  if (!radioReady)
    return;

#ifdef DEBUG
  Serial.print(message);
#endif

  unsigned int msgLen = message.length();
  for (unsigned int i = 0; i < msgLen; i++)
  {
    char c = message.charAt(i);
    if (serialBufferAtLineStart)
    {
      serialBuffer += "SAT> ";
      serialBufferAtLineStart = false;
    }

    serialBuffer += c;

    if (c == '\n')
    {
      serialBufferAtLineStart = true;
    }
  }

  // Send buffer when it gets long enough or contains newlines
  if (serialBuffer.length() >= MAX_SERIAL_MSG_LEN || serialBuffer.indexOf('\n') != -1)
  {
    sendSerialBuffer();
  }
}

/**
 * Sends a serial message with newline via radio to ground station
 *
 * @param message The message string to send (optional)
 */
void radioPrintln(const String &message = "")
{
  radioPrint(message + "\n");
  sendSerialBuffer(); // Force send after newline
}

/**
 * Sends accumulated serial buffer via radio
 */
void sendSerialBuffer()
{
  if (!radioReady || serialBuffer.length() == 0)
    return;

  // Split long messages into chunks
  while (serialBuffer.length() > 0)
  {
    unsigned int remaining = serialBuffer.length();
    uint8_t chunkSize = (uint8_t)min(remaining, (unsigned int)MAX_SERIAL_MSG_LEN);
    int newlineIndex = serialBuffer.indexOf('\n');
    if (newlineIndex != -1 && newlineIndex + 1 <= chunkSize)
    {
      chunkSize = (uint8_t)(newlineIndex + 1);
    }

    if (chunkSize == 0)
    {
      break;
    }

    // Build serial message packet
    SerialMessagePacket *serialPacket = (SerialMessagePacket *)radioSerialTxBuffer;
    serialPacket->messageType = SERIAL_MSG_TYPE;

    bool hasMore = remaining > chunkSize;
    serialPacket->length = chunkSize | (hasMore ? SERIAL_CONTINUATION_FLAG : 0);

    // Copy message data
    const char *src = serialBuffer.c_str();
    memcpy(serialPacket->data, src, chunkSize);

    // Send packet (header + actual data length)
    uint8_t packetSize = sizeof(serialPacket->messageType) + sizeof(serialPacket->length) + chunkSize;
    if (sendPacketReliable(radioSerialTxBuffer, packetSize))
    {
      // Remove sent chunk from buffer
      serialBuffer.remove(0, chunkSize);
    }
    else
    {
      // Failed to send, keep buffer for retry
      break;
    }

    delay(10); // Small delay between chunks
  }
}

/**
 * Read the RF23 RX FIFO directly over SPI and emit a hex/ASCII snapshot.
 *
 * This helper is intentionally invasive: it places the radio in idle,
 * captures diagnostic registers (STATUS: chip state bits, EZMAC: MAC layer
 * event flags, INT1/INT2: pending interrupt reasons that clear on read),
 * pulls the entire 64-byte FIFO via `spiBurstRead`, prints the results
 * through the radio logging channel, then returns the part to RX mode. The
 * FIFO snapshot is valuable when packets stall mid-flight or when you need
 * to confirm whether stray bytes are sitting in hardware before the driver
 * consumes them.
 */
void dumpRf23PendingPackets()
{
  radioPrintln("=== SATELLITE RF23 RX FIFO RAW SNAPSHOT START ===");
  // TODO: FIFO recovery checklist
  if (!radioReady)
  {
    radioPrintln("RF23 debug: radio not initialised");
    return;
  }

  // Freeze the radio so we can read the FIFO safely
  rf23.setModeIdle();
  delay(10);

  // Grab status/interrupt diagnostics straight from the silicon
  uint8_t status = rf23.statusRead();
  uint8_t ezmac = rf23.ezmacStatusRead();
  uint8_t irq1 = rf23.spiRead(RH_RF22_REG_03_INTERRUPT_STATUS1); // read-to-clear
  uint8_t irq2 = rf23.spiRead(RH_RF22_REG_04_INTERRUPT_STATUS2); // read-to-clear

  // Diagnostic usage guide (cross-reference RH_RF22.h or the Si4432 datasheet):
  //   - STATUS (reg 0x02) exposes live chip state bits such as `RH_RF22_CHIP_READY`,
  //     `RH_RF22_FFEM` (FIFO empty), and `RH_RF22_RXAFULL` (RX almost full). Use this to
  //     confirm whether the part thinks data is queued or if it is still synchronised.
  //   - EZMAC status (reg 0x31) mirrors MAC-events like packet sent/received and CRC
  //     outcomes. Flags such as `RH_RF22_PKSENT` or `RH_RF22_PKVALID` help determine if
  //     the high-level packet engine progressed even when our code did not.
  //   - Interrupt Status1 (reg 0x03) shows per-event causes (`RH_RF22_IPKVALID`,
  //     `RH_RF22_ICRCERROR`, `RH_RF22_IPKSENT`, etc.). A set bit means the corresponding
  //     event fired prior to this dump; the read here also clears it.
  //   - Interrupt Status2 (reg 0x04) covers secondary events (`RH_RF22_IFFERR`,
  //     `RH_RF22_ISWDET`, `RH_RF22_IEXT` ...). Inspect these when diagnosing FIFO
  //     overflows, sync losses, or external interrupts.
  // Using the four values together lets you decide if bytes stalled in hardware, if a
  // CRC failure occurred, or if the silicon believes the transaction already finished.

  auto formatHex = [](uint8_t value)
  {
    String hex = String(value, HEX);
    hex.toUpperCase();
    if (hex.length() < 2)
      hex = "0" + hex;
    return hex;
  };

  radioPrint("STATUS: 0x");
  radioPrintln(formatHex(status));
  radioPrint("EZMAC: 0x");
  radioPrintln(formatHex(ezmac));
  radioPrint("INT1 : 0x");
  radioPrintln(formatHex(irq1));
  radioPrint("INT2 : 0x");
  radioPrintln(formatHex(irq2));

  // Read the raw FIFO contents directly (always 64 bytes)
  uint8_t fifoSnapshot[RH_RF22_FIFO_SIZE];
  memset(fifoSnapshot, 0, sizeof(fifoSnapshot));
  rf23.spiBurstRead(RH_RF22_REG_7F_FIFO_ACCESS, fifoSnapshot, RH_RF22_FIFO_SIZE);

  radioPrintln("FIFO contents (oldest first):");
  dumpRf23PacketHexLines(fifoSnapshot, RH_RF22_FIFO_SIZE);

  // Leave the radio ready to receive the next command
  rf23.setModeRx();
  radioPrintln("=== SATELLITE RF23 RX FIFO RAW SNAPSHOT END ===");
}

/**
 * Helper to print bytes in hex + ASCII columns for readability.
 *
 * Output format mirrors classic hexdump style: byte offsets, hex tuples,
 * and printable ASCII ('.' for non-printable). Intended for short dumps
 * such as the 64-byte RF23 FIFO snapshot above.
 */
void dumpRf23PacketHexLines(const uint8_t *data, uint8_t length)
{
  const uint8_t BYTES_PER_LINE = 16;
  char lineBuf[4 /*offset*/ + 2 /*colon+space*/ + (BYTES_PER_LINE * 3) /*hex+space*/ + 2 /*| */ + BYTES_PER_LINE /*ascii*/ + 1];

  for (uint8_t offset = 0; offset < length; offset += BYTES_PER_LINE)
  {
    uint8_t remaining = length - offset;
    uint8_t lineLen = remaining < BYTES_PER_LINE ? remaining : BYTES_PER_LINE;
    int pos = snprintf(lineBuf, sizeof(lineBuf), "%03u: ", offset);

    for (uint8_t i = 0; i < lineLen && pos < (int)sizeof(lineBuf); i++)
    {
      pos += snprintf(lineBuf + pos, sizeof(lineBuf) - pos, "%02X ", data[offset + i]);
    }

    for (uint8_t i = lineLen; i < BYTES_PER_LINE && pos < (int)sizeof(lineBuf); i++)
    {
      pos += snprintf(lineBuf + pos, sizeof(lineBuf) - pos, "   ");
    }

    pos += snprintf(lineBuf + pos, sizeof(lineBuf) - pos, "| ");

    for (uint8_t i = 0; i < lineLen && pos < (int)sizeof(lineBuf); i++)
    {
      char c = static_cast<char>(data[offset + i]);
      if (c < 32 || c > 126)
        c = '.';
      pos += snprintf(lineBuf + pos, sizeof(lineBuf) - pos, "%c", c);
    }

    radioPrintln(String(lineBuf));
  }
}

/**
 * Listens for commands from ground station via radio and handles them
 *
 */
void listenForCommands()
{
  uint8_t len = sizeof(radioRxBuffer);

  if (rf23.recv(radioRxBuffer, &len))
  {
    if (len == 0)
      return;

    // Check for retry request packet first (special packet type)
    if (radioRxBuffer[0] == RETRY_REQUEST_TYPE && len >= 3)
    {
      handleRetryRequest(radioRxBuffer, len);
      return;
    }

    char cmd = (char)radioRxBuffer[0];

    if (cmd == 'u' || cmd == 'U')
    {
      captureThermalImageUART();
    }
    else if (cmd == 'r' || cmd == 'R')
    {
      sendThermalDataViaRadio();
    }
    else if (cmd == 'd' || cmd == 'D')
    {
      dumpRf23PendingPackets();
    }
    else if (cmd == 'p' || cmd == 'P')
    {
      // Power control: ['p','1'] on, ['p','0'] off, ['p','s'] status
      if (len >= 2)
      {
        char sub = (char)radioRxBuffer[1];
        if (sub == '1')
        {
          digitalWrite(RPI_ENABLE, HIGH); // Turn Pi ON
          radioPrintln("RPI POWER: ON (Wait until 'RPI STATUS: IDLE' before thermal capture.)");
        }
        else if (sub == '0')
        {
          digitalWrite(RPI_ENABLE, LOW); // Turn Pi OFF
          RPI_IDLE_READY = false;        // Reset idle state
          radioPrintln("RPI POWER: OFF");
        }
        else if (sub == 's' || sub == 'S')
        {
          int state = digitalRead(RPI_ENABLE);
          radioPrint("RPI POWER: ");
          radioPrint(state ? "ON" : "OFF");
          radioPrint(" | MODE: ");
          radioPrintln(RPI_IDLE_READY ? "IDLE" : "NOT READY");
        }
        else
        {
          radioPrintln("RPI POWER: Unknown subcommand");
        }
      }
      else
      {
        radioPrintln("RPI POWER: missing arg");
      }
    }
    else if (cmd == 's' || cmd == 'S')
    {
      handleSensorCommand(radioRxBuffer, len);
    }
    else if (cmd == 'g' || cmd == 'G')
    {
      radioPrintln("pong from satellite"); // reply back to GS
    }
    else if (cmd == 'v' || cmd == 'V')
    {
      // Stream command: 'v' + '1' = start, 'v' + '0' = stop
      if (len >= 2)
      {
        char sub = (char)radioRxBuffer[1];
        if (sub == '1')
        {
          startStreamMode();
        }
        else if (sub == '0')
        {
          stopStreamMode();
        }
        else
        {
          radioPrintln("STREAM: Unknown subcommand (use v1 to start, v0 to stop)");
        }
      }
      else
      {
        radioPrintln("STREAM: missing arg (use v1 to start, v0 to stop)");
      }
    }
    else if (cmd == '~')
    {
      // Software reset for Teensy 4.1 (ARM Cortex-M7)
      SCB_AIRCR = 0x05FA0004;
    }
    else
      radioPrintln("Unknown command received via radio");
  }
}

/**
 * Setup function - initializes the satellite payload transmitter
 *
 * Configures serial communication, GPIO pins, radio module, and UART
 * interface. Powers up the Raspberry Pi and displays available commands.
 */
void setup()
{
  Serial.begin(115200);
  while (!Serial && millis() < 5000)
    ;

  // Initialize radio first so we can send setup messages
  initRadio();

  // Send startup message via radio instead of serial
  radioPrintln("=== Artemis Cubesat Teensy 4.1 Satellite Transmitter ===");

  Wire.begin();

  initTemperatureSensors();

  initCurrentSensors();

  initGPS();

  initIMU();

  // TODO: Implement initPDU

  // init the RPI for thermal images (default turned off)
  initRPI();

  // Perform initial temperature sensor check
  // checkTemperatureSensors();

  // Perform initial current sensor check
  // checkCurrentSensors();

  // TODO: Implement checkPingPDU to ensure a pong is receivied from PDU

  radioPrintln("Commands:");
  radioPrintln(" 'u' - UART thermal capture (fast!)");
  radioPrintln(" 'r' - Send captured image via radio");
  radioPrintln(" 'v1'/'v0' - Start/stop livestream mode");
  radioPrintln(" 't' - Check temperature sensors");
  radioPrintln(" 'c' - Check current sensors");
  radioPrintln(" 'd' - Dump RF23 RX FIFO contents");
  radioPrintln(" 'sg'/'si'/'sb' - GPS, IMU, or both sensor data");
  radioPrintln(" 'sG'/'sI' - Reinitialize GPS or IMU");
  radioPrintln("Ready!");
}

void initTemperatureSensors()
{
  radioPrintln("Initializing temperature sensors...");

  // Initialize all temperature sensor pins as INPUT
  for (int i = 0; i < numTemperatureSensors; i++)
  {
    pinMode(temperatureSensorPins[i], INPUT);
    radioPrint("  Pin ");
    radioPrint(String(temperatureSensorPins[i]));
    radioPrint(" configured for ");
    radioPrintln(tempereatureSensorLabels[i]);
  }

  radioPrintln("Temperature sensors initialized successfully");
}

/**
 * Reads temperature from a TMP36 sensor and converts to Celsius
 *
 * @param sensorPin The analog pin connected to the TMP36 sensor
 * @return Temperature in Celsius
 */
float readTemperatureCelsius(int sensorPin)
{
  int adcValue = analogRead(sensorPin);
  float voltage = adcValue * MV_PER_ADC_UNIT;

  // Convert voltage to temperature in Fahrenheit and then subtract the offset
  float temperatureF = (voltage - OFFSET_F) / MV_PER_DEGREE_F;

  // Convert temperature to Celsius
  float temperatureC = (temperatureF - 32) * 5 / 9;

  return temperatureC;
}

/**
 * Checks if temperature sensor values are within valid range
 *
 * @return true if all sensors are reading valid temperatures, false otherwise
 */
bool checkTemperatureSensors()
{
  radioPrintln("--- TEMPERATURE SENSOR CHECK ---");

  bool allValid = true;
  int validSensors = 0;

  for (int i = 0; i < numTemperatureSensors; i++)
  {
    float temperatureC = readTemperatureCelsius(temperatureSensorPins[i]);

    // Check if temperature is within reasonable range (-40°C to +125°C for TMP36)
    bool isValid = (temperatureC >= -40.0 && temperatureC <= 125.0);

    radioPrint(tempereatureSensorLabels[i]);
    radioPrint(" (Teensy Pin ");
    radioPrint(String(temperatureSensorPins[i]));
    radioPrint("): ");
    radioPrint(String(temperatureC, 2));
    radioPrint("°C ");

    if (isValid)
    {
      radioPrintln("Y");
      validSensors++;
    }
    else
    {
      radioPrintln("N");
      allValid = false;
    }
  }

  radioPrint("Valid sensors: ");
  radioPrint(String(validSensors));
  radioPrint("/");
  radioPrintln(String(numTemperatureSensors));

  if (allValid)
  {
    radioPrintln("✓ All temperature sensors operational");
  }
  else
  {
    radioPrintln("⚠️ Some temperature sensors showing invalid readings");
  }

  return allValid;
}

/**
 * Gets temperature data from all sensors for telemetry transmission
 *
 * @param temperatures Array to store temperature readings (must be at least numTemperatureSensors)
 * @return Number of valid temperature readings
 */
int getValidTemperatureData(float temperatures[])
{
  int validCount = 0;

  for (int i = 0; i < numTemperatureSensors; i++)
  {
    float tempC = readTemperatureCelsius(temperatureSensorPins[i]);
    temperatures[i] = tempC;

    // Count valid readings
    if (tempC >= -40.0 && tempC <= 125.0)
    {
      validCount++;
    }
  }

  return validCount;
}

/**
 * Initializes all INA219 current sensors and checks their connectivity
 *
 * Configures I2C communication and calibrates each sensor for 16V/400mA range.
 * Reports which sensors are connected and operational.
 */
void initCurrentSensors()
{
  radioPrintln("Initializing current sensors...");

  // Initialize I2C communication
  Wire2.begin();

  int connectedSensors = 0;

  // Initialize and configure each INA219 sensor
  for (int i = 0; i < numCurrentSensors; i++)
  {
    bool sensorConnected = currentSensors[i].begin(&Wire2);

    if (sensorConnected)
    {
      radioPrint("  ");
      radioPrint(currentSensorsLabels[i]);
      radioPrintln(" - Connected");
      connectedSensors++;

      // Set calibration for 16V and 400mA range
      currentSensors[i].setCalibration_16V_400mA();
    }
    else
    {
      radioPrint("  ");
      radioPrint(currentSensorsLabels[i]);
      radioPrintln(" - Not detected");
    }
  }

  radioPrint("Current sensors initialized: ");
  radioPrint(String(connectedSensors));
  radioPrint("/");
  radioPrintln(String(numCurrentSensors));

  if (connectedSensors == numCurrentSensors)
  {
    radioPrintln("✓ All current sensors operational");
  }
  else if (connectedSensors > 0)
  {
    radioPrintln("⚠️ Some current sensors not detected");
  }
  else
  {
    radioPrintln("❌ No current sensors detected");
  }
}

/**
 * Checks if current sensor values are within valid range
 *
 * @return true if all sensors are reading valid current/voltage, false otherwise
 */
bool checkCurrentSensors()
{
  radioPrintln("--- CURRENT SENSOR CHECK ---");

  bool allValid = true;
  int validSensors = 0;

  for (int i = 0; i < numCurrentSensors; i++)
  {
    // Read current and voltage from each sensor
    float current_mA = currentSensors[i].getCurrent_mA();
    float bus_voltage_V = currentSensors[i].getBusVoltage_V();

    // Check if readings are within reasonable ranges
    // Current: -400mA to +400mA (INA219 range)
    // Voltage: 0V to 16V (INA219 range)
    bool currentValid = (current_mA >= -400.0 && current_mA <= 400.0);
    bool voltageValid = (bus_voltage_V >= 0.0 && bus_voltage_V <= 16.0);
    bool isValid = currentValid && voltageValid;

    radioPrint(currentSensorsLabels[i]);
    radioPrint(": ");
    radioPrint(String(current_mA, 2));
    radioPrint(" mA, ");
    radioPrint(String(bus_voltage_V, 2));
    radioPrint(" V ");

    if (isValid)
    {
      radioPrintln("Y");
      validSensors++;
    }
    else
    {
      radioPrintln("N");
      allValid = false;

      if (!currentValid)
      {
        radioPrint("  Current out of range: ");
        radioPrintln(String(current_mA, 2));
      }
      if (!voltageValid)
      {
        radioPrint("  Voltage out of range: ");
        radioPrintln(String(bus_voltage_V, 2));
      }
    }
  }

  radioPrint("Valid sensors: ");
  radioPrint(String(validSensors));
  radioPrint("/");
  radioPrintln(String(numCurrentSensors));

  if (allValid)
  {
    radioPrintln("✓ All current sensors operational");
  }
  else
  {
    radioPrintln("⚠️ Some current sensors showing invalid readings");
  }

  return allValid;
}

/**
 * Gets current and voltage data from all sensors for telemetry transmission
 *
 * @param currents Array to store current readings (must be at least numCurrentSensors)
 * @param voltages Array to store voltage readings (must be at least numCurrentSensors)
 * @return Number of valid sensor readings
 */
int getValidCurrentData(float currents[], float voltages[])
{
  int validCount = 0;

  for (int i = 0; i < numCurrentSensors; i++)
  {
    float current_mA = currentSensors[i].getCurrent_mA();
    float bus_voltage_V = currentSensors[i].getBusVoltage_V();

    currents[i] = current_mA;
    voltages[i] = bus_voltage_V;

    // Count valid readings
    bool currentValid = (current_mA >= -400.0 && current_mA <= 400.0);
    bool voltageValid = (bus_voltage_V >= 0.0 && bus_voltage_V <= 16.0);

    if (currentValid && voltageValid)
    {
      validCount++;
    }
  }

  return validCount;
}

static String twoDigit(uint8_t value)
{
  if (value < 10)
  {
    return "0" + String(value);
  }
  return String(value);
}

void initGPS(bool announce)
{
  if (announce)
  {
    radioPrintln("Initializing GPS...");
  }

  gpsConnected = false;

  GPS.begin(9600);
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);
  delay(1000);

  if (GPS.available() == 0)
  {
    if (announce)
    {
      radioPrintln("GPS module not detected");
    }
    gpsConnected = false;
  }
  else
  {
    if (announce)
    {
      radioPrintln("GPS module connected");
    }
    gpsConnected = true;
  }

  while (GPS.available())
  {
    GPS.read();
  }

  if (announce)
  {
    if (gpsConnected)
    {
      radioPrintln("GPS initialization complete");
    }
    else
    {
      radioPrintln("GPS initialization failed");
    }
  }
}

void initIMU(bool announce)
{
  if (announce)
  {
    radioPrintln("Initializing IMU...");
  }

  bool lisReady = lis.begin_I2C();
  bool lsmReady = lsm6dsox.begin_I2C();

  imuConnected = lisReady && lsmReady;

  if (!imuConnected)
  {
    if (announce)
    {
      if (!lisReady)
      {
        radioPrintln("LIS3MDL magnetometer not detected");
      }
      if (!lsmReady)
      {
        radioPrintln("LSM6DSOX accelerometer/gyro not detected");
      }
    }
    return;
  }

  if (announce)
  {
    radioPrintln("IMU connected");
  }
}

void serviceGPS()
{
  char c = GPS.read();
  if (c)
  {
    gpsConnected = true;
  }

  if (GPS.newNMEAreceived())
  {
    GPS.parse(GPS.lastNMEA());
  }
}

void printGPSData()
{
  radioPrintln("--- GPS DATA ---");

  if (!gpsConnected)
  {
    radioPrintln("GPS not connected");
    return;
  }

  if (!GPS.fix)
  {
    radioPrintln("No GPS fix - searching for satellites...");
    radioPrint("Satellites visible: ");
    radioPrintln(String((int)GPS.satellites));
    return;
  }

  radioPrint("Time (UTC): ");
  radioPrintln(twoDigit(GPS.hour) + ":" + twoDigit(GPS.minute) + ":" + twoDigit(GPS.seconds));

  radioPrint("Date: ");
  radioPrintln(String(GPS.day) + "/" + String(GPS.month) + "/20" + String(GPS.year));

  radioPrint("Location (DDMM.MMMM format): ");
  radioPrint(String(GPS.latitude, 4));
  radioPrint(String(GPS.lat));
  radioPrint(", ");
  radioPrint(String(GPS.longitude, 4));
  radioPrintln(String(GPS.lon));

  float lat = GPS.latitude / 100.0;
  int lat_deg = (int)lat;
  float lat_min = (lat - lat_deg) * 100.0;
  float lat_decimal = lat_deg + (lat_min / 60.0);
  if (GPS.lat == 'S')
  {
    lat_decimal = -lat_decimal;
  }

  float lon = GPS.longitude / 100.0;
  int lon_deg = (int)lon;
  float lon_min = (lon - lon_deg) * 100.0;
  float lon_decimal = lon_deg + (lon_min / 60.0);
  if (GPS.lon == 'W')
  {
    lon_decimal = -lon_decimal;
  }

  radioPrint("Decimal: ");
  radioPrint(String(lat_decimal, 6));
  radioPrint(", ");
  radioPrintln(String(lon_decimal, 6));

  radioPrint("Altitude: ");
  radioPrint(String(GPS.altitude));
  radioPrintln(" m");

  radioPrint("Speed: ");
  radioPrint(String(GPS.speed));
  radioPrintln(" knots");

  radioPrint("Satellites: ");
  radioPrintln(String((int)GPS.satellites));

  radioPrint("Fix quality: ");
  radioPrintln(String((int)GPS.fixquality));
}

void printIMUData()
{
  radioPrintln("--- IMU DATA ---");

  if (!imuConnected)
  {
    radioPrintln("IMU not connected");
    return;
  }

  sensors_event_t accel, gyro, temp, mag;
  lsm6dsox.getEvent(&accel, &gyro, &temp);
  lis.getEvent(&mag);

  radioPrint("Acceleration (m/s^2): x = ");
  radioPrint(String(accel.acceleration.x, 2));
  radioPrint(", y = ");
  radioPrint(String(accel.acceleration.y, 2));
  radioPrint(", z = ");
  radioPrintln(String(accel.acceleration.z, 2));

  radioPrint("Gyro (dps): x = ");
  radioPrint(String(gyro.gyro.x, 2));
  radioPrint(", y = ");
  radioPrint(String(gyro.gyro.y, 2));
  radioPrint(", z = ");
  radioPrintln(String(gyro.gyro.z, 2));

  radioPrint("Temperature: ");
  radioPrint(String(temp.temperature, 1));
  radioPrintln(" C");

  radioPrint("Magnetic field (uT): x = ");
  radioPrint(String(mag.magnetic.x, 2));
  radioPrint(", y = ");
  radioPrint(String(mag.magnetic.y, 2));
  radioPrint(", z = ");
  radioPrintln(String(mag.magnetic.z, 2));
}

void handleSensorCommand(uint8_t *buf, uint8_t len)
{
  if (len < 2)
  {
    radioPrintln("Sensor commands: sg(si/sb) print data, sG reinit GPS, sI reinit IMU");
    return;
  }

  char sub = (char)buf[1];

  switch (sub)
  {
  case 'g':
    radioPrintln("========== GPS DATA ==========");
    printGPSData();
    radioPrintln("==============================");
    break;
  case 'i':
    radioPrintln("========== IMU DATA ==========");
    printIMUData();
    radioPrintln("==============================");
    break;
  case 'b':
  case 'B':
    radioPrintln("========== SENSOR DATA ==========");
    printGPSData();
    radioPrintln("");
    printIMUData();
    radioPrintln("=================================");
    break;
  case 'G':
    initGPS();
    break;
  case 'I':
    initIMU();
    break;
  default:
    radioPrintln("Unknown sensor subcommand. Use sg, si, sb, sG, or sI");
    break;
  }
}

void initRPI()
{
  // Initialize Raspberry Pi control pin and make sure its off.
  pinMode(RPI_ENABLE, OUTPUT);
  digitalWrite(RPI_ENABLE, LOW);

  // Initialize UART for RPi communication
  Serial2.begin(UART_BAUD); // High-speed UART for data transfer
  radioPrintln("RPI UART initialized at 115200 baud");

  digitalWrite(RPI_ENABLE, HIGH); // Turn Pi ON
  radioPrintln("RPI POWER: ON (Wait until 'RPI STATUS: IDLE' before thermal capture.)");
}
/**
 * Initializes the RF22 radio module for transmit mode
 *
 * Configures SPI1 interface, sets radio frequency to 433MHz, configures
 * modem settings for GFSK modulation, and sets the radio to idle mode
 * ready for transmission. Also configures RX/TX control pins.
 */
void initRadio()
{
  // radioPrintln("Initializing radio");

  // Configure RX/TX control pins
  pinMode(RADIO_RX_ON_PIN, OUTPUT); // RX_ON pin
  pinMode(RADIO_TX_ON_PIN, OUTPUT); // TX_ON pin
  /**
   * Transmit burst: TXON high, RXON low.
   * Receive window: RXON high, TXON low.
   * Idle/standby: both low (saves current, keeps switch centered).
   */
  // default to listening
  digitalWrite(RADIO_RX_ON_PIN, HIGH);
  digitalWrite(RADIO_TX_ON_PIN, LOW);
  delayMicroseconds(500);

  // Configure SPI1 interface for radio communication
  SPI1.setMISO(39); // Master In, Slave Out
  SPI1.setMOSI(26); // Master Out, Slave In
  SPI1.setSCK(27);  // Serial Clock
  SPI1.begin();
  delay(10);

  // LED for status of active use of radio (sending/receiving packets)
  pinMode(LED_PIN, OUTPUT);

  // Initialize RF22 radio module
  if (!rf23.init())
  {
#ifdef DEBUG
    Serial.println("Satellite Radio init failed!");
#endif
    // TODO: graceful fail here... wait like 5 seconds then boot through GS and allow user to self initialize via radio init command
    // radioPrintln("Radio init failed!");
    radioReady = false;
    return; // init failed dont setup the rest.
  }

  // Configure radio parameters
  rf23.setFrequency(433.0); // 433MHz (good for drone use)

  // GFSK Modem Configurations - Ordered from fastest to slowest
  // Uncomment ONE line to select your desired configuration

  rf23.setModemConfig(RH_RF22::GFSK_Rb125Fd125); // 125 kbps, 125 kHz deviation (fastest, needs strong signal)
  // rf23.setModemConfig(RH_RF22::GFSK_Rb57_6Fd28_8); // 57.6 kbps, 28.8 kHz deviation
  // rf23.setModemConfig(RH_RF22::GFSK_Rb38_4Fd19_6); // 38.4 kbps, 19.6 kHz deviation (recommended starting point)
  // rf23.setModemConfig(RH_RF22::GFSK_Rb19_2Fd9_6);   // 19.2 kbps, 9.6 kHz deviation (good balance)

  // rf23.setModemConfig(RH_RF22::GFSK_Rb9_6Fd45); // 9.6 kbps, 45 kHz deviation (confirmed reliable)

  // rf23.setModemConfig(RH_RF22::GFSK_Rb4_8Fd45);     // 4.8 kbps, 45 kHz deviation
  // rf23.setModemConfig(RH_RF22::GFSK_Rb2_4Fd36);     // 2.4 kbps, 36 kHz deviation
  // rf23.setModemConfig(RH_RF22::GFSK_Rb2Fd5);        // 2 kbps, 5 kHz deviation (slowest, maximum range)

  rf23.setTxPower(RH_RF22_RF23BP_TXPOW_30DBM); // 30dBm (1000mW) - max for RFM23BP
  rf23.setModeIdle();                          // Set radio to idle mode
  delay(10);
  radioReady = true;
  radioPrintln("Satellite Radio is ready.");
}

void setRadioAmpTransmit()
{
  /**
   * Transmit burst: TXON low, RXON high PER THE DATASHEET.
   * Receive window: RXON high, TXON low.
   * Idle/standby: both low (saves current, keeps switch centered).
   */
  digitalWrite(RADIO_RX_ON_PIN, HIGH);
  digitalWrite(RADIO_TX_ON_PIN, LOW);
  delayMicroseconds(300); // ensure the amp is settled before transmitting
}
void setRadioAmpReceive()
{
  /**
   * Transmit burst: TXON low, RXON high PER THE DATASHEET.
   * Receive window: RXON low, TXON high.
   * Idle/standby: both low (saves current, keeps switch centered).
   */
  digitalWrite(RADIO_RX_ON_PIN, LOW);
  digitalWrite(RADIO_TX_ON_PIN, HIGH);
  delayMicroseconds(300); // ensure the amp is settled before receiving
}
void setRadioAmpIdle()
{
  /**
   * NOTE Shouldn't need to use this for the drone test but will
   * be useful later on for satellite deployment to conserve battery.
   * Transmit burst: TXON high, RXON low.
   * Receive window: RXON high, TXON low.
   * Idle/standby: both low (saves current, keeps switch centered).
   */
  digitalWrite(RADIO_RX_ON_PIN, LOW);
  digitalWrite(RADIO_TX_ON_PIN, LOW);
}

/**
 * Main loop - handles user commands and system operations
 *
 * Continuously monitors serial interface for user commands and executes
 * the corresponding operations (capture or transmit).
 */
void loop()
{
  serviceGPS();

  // Handle stream mode if active
  if (streamModeActive)
  {
    handleStreamMode();
    return; // Skip normal processing during stream mode
  }

  pollPIUartStatus();

  // Handle commands from ground station via radio
  while (rf23.available())
  {
    listenForCommands();
  }

  // Also allow manual commands via serial for testing/debug
#ifdef DEBUG
  if (Serial.available())
  {
    char cmd = Serial.read();
    while (Serial.available())
      Serial.read(); // Clear input buffer

    radioPrint("Command: ");
    radioPrintln(String(cmd));

    switch (cmd)
    {
    case 'z':
    case 'Z':
      radioPrintln("command Z pong from satellite"); // reply back to GS
      break;
    case 'u':
    case 'U':
      captureThermalImageUART();
      break;
    case 'r':
    case 'R':
      sendThermalDataViaRadio();
      break;
    case 't':
    case 'T':
      checkTemperatureSensors();
      break;
    case 'c':
    case 'C':
      checkCurrentSensors();
      break;
    case 'd':
    case 'D':
      dumpRf23PendingPackets();
      break;
    case 'g':
      radioPrintln("========== GPS DATA ==========");
      printGPSData();
      radioPrintln("==============================");
      break;
    case 'i':
      radioPrintln("========== IMU DATA ==========");
      printIMUData();
      radioPrintln("==============================");
      break;
    case 'b':
    case 'B':
      radioPrintln("========== SENSOR DATA ==========");
      printGPSData();
      radioPrintln("");
      printIMUData();
      radioPrintln("=================================");
      break;
    case 'G':
      initGPS();
      break;
    case 'I':
      initIMU();
      break;
    case '~':
      SCB_AIRCR = 0x05FA0004;
      break;
    default:
      radioPrintln("Unknown command. Use 'z','u','r','t','c','d','g','i','b','G','I','~'");
    }
  }
#endif
}

// Read exactly 'len' bytes from 'port' with a deadline
bool readExact(HardwareSerial &port, uint8_t *buf, size_t len, uint32_t timeout_ms)
{
  uint32_t start = millis();
  size_t got = 0;
  while (got < len)
  {
    if (millis() - start > timeout_ms)
      return false;
    int avail = port.available();
    if (avail > 0)
    {
      size_t toRead = (size_t)avail;
      size_t needed = len - got;
      if (toRead > needed)
        toRead = needed;

      size_t r = port.readBytes(buf + got, toRead);
      if (r > 0)
      {
        got += r;
      }
      else
      {
        delay(1);
      }
    }
    else
    {
      delay(1);
    }
  }
  return true;
}

// Validate magic
bool magicOK(const uint8_t *h)
{
  return h[0] == UART_MAGIC[0] && h[1] == UART_MAGIC[1] && h[2] == UART_MAGIC[2] && h[3] == UART_MAGIC[3];
}

// Validate end markers
bool endOK(const uint8_t *e)
{
  return e[0] == UART_END[0] && e[1] == UART_END[1];
}

// Standard CRC-16/CCITT-FALSE for cross-checking packet integrity
uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFF;
  while (len--)
  {
    crc ^= (uint16_t)(*data++) << 8;
    for (uint8_t i = 0; i < 8; ++i)
    {
      if (crc & 0x8000)
      {
        crc = (crc << 1) ^ 0x1021;
      }
      else
      {
        crc <<= 1;
      }
    }
  }
  return crc;
}

// Identify STATUS vs IMAGE by looking for ASCII "STATUS:" prefix
bool payloadIsStatus(const uint8_t *payload, uint16_t len)
{
  const char *prefix = "STATUS:";
  const size_t L = 7; // includes colon
  if (len < L)
    return false;
  for (size_t i = 0; i < L; ++i)
  {
    if ((char)payload[i] != prefix[i])
      return false;
  }
  return true;
}

// Receive ONE framed message from the Pi into 'dest' (up to destMax)
// Returns: true on success; writes outLen and sets isStatus accordingly.
bool recvFramedFromPi(HardwareSerial &port,
                      uint8_t *dest, uint16_t destMax,
                      uint16_t &outLen, bool &isStatus)
{
  outLen = 0;
  isStatus = false;

  // 1) Header: 4 magic + 2 length
  uint8_t header[6];
  if (!readExact(port, header, 6, UART_HEADER_TIMEOUT_MS))
  {
    radioPrintln("ERROR: UART header timeout");
    return false;
  }
  if (!magicOK(header))
  {
    radioPrint("ERROR: Bad magic: ");
    for (int i = 0; i < 4; i++)
    {
      radioPrint("0x");
      radioPrint(String(header[i], HEX));
      radioPrint(" ");
    }
    radioPrintln();
    return false;
  }

  uint16_t len = (uint16_t)header[4] | ((uint16_t)header[5] << 8);
  if (len == 0)
  {
    radioPrintln("ERROR: Zero-length payload");
    return false;
  }

  if (len > destMax)
  {
    radioPrint("ERROR: Payload too large (");
    radioPrint(String(len));
    radioPrintln(" bytes) for buffer");
    // Drain and discard payload + end markers to resync
    uint8_t dump[64];
    uint32_t remaining = (uint32_t)len + 2;
    uint32_t start = millis();
    while (remaining > 0 && (millis() - start) < UART_PAYLOAD_TIMEOUT_MS)
    {
      size_t toRead = (remaining < sizeof(dump)) ? (size_t)remaining : sizeof(dump);
      size_t r = port.readBytes(dump, toRead);
      if (r > 0)
      {
        remaining -= (uint32_t)r;
      }
      else
      {
        delay(1);
      }
    }
    radioPrintln("ERROR: Discarded oversized payload; request retransmit.");
    return false;
  }

  // 2) Payload
  if (!readExact(port, dest, len, UART_PAYLOAD_TIMEOUT_MS))
  {
    radioPrintln("ERROR: UART payload timeout");
    return false;
  }

  // 3) End markers
  uint8_t ender[2];
  if (!readExact(port, ender, 2, UART_END_TIMEOUT_MS))
  {
    radioPrintln("ERROR: UART end-marker timeout");
    return false;
  }
  if (!endOK(ender))
  {
    radioPrint("ERROR: Bad end markers: 0x");
    radioPrint(String(ender[0], HEX));
    radioPrint(" 0x");
    radioPrintln(String(ender[1], HEX));
    return false;
  }

  outLen = len;
  isStatus = payloadIsStatus(dest, len);
  return true;
}

// Pretty-print a STATUS payload (strip "STATUS:")
void handleStatusPayload(const uint8_t *payload, uint16_t len)
{
  const size_t L = 7;
  String msg;
  if (len > L)
  {
    // copy ASCII into a String safely
    for (uint16_t i = L; i < len; ++i)
    {
      char c = (char)payload[i];
      if (c == '\r' || c == '\n')
        continue;
      msg += c;
    }
  }
  if (msg.length() == 0)
    msg = "(empty)";
  radioPrint("RPI STATUS: ");
  radioPrintln(msg);

  if (msg == "IDLE")
  {
    RPI_IDLE_READY = true;
  }
}

// Drain any unsolicited framed UART messages (typically STATUS packets) while idle
void pollPIUartStatus()
{
  if (piCaptureInProgress)
    return;

  while (Serial2.available() >= 6)
  {
    uint16_t rxLen = 0;
    bool isStatus = false;

    if (!recvFramedFromPi(Serial2, piStatusBuf, MAX_STATUS_LEN, rxLen, isStatus))
    {
      return; // recvFramedFromPi already reported the error
    }

    if (isStatus)
    {
      handleStatusPayload(piStatusBuf, rxLen);
    }
    else
    {
      radioPrint("RPI payload received while idle (");
      radioPrint(String(rxLen));
      radioPrintln(" bytes)");
    }
  }
}
/**
 * Captures thermal image data from Raspberry Pi via UART
 *
 * Triggers the RPI to capture thermal data, receives the data via UART,
 * validates the transmission, and stores the image in the buffer.
 * Provides real-time progress updates and data quality assessment.
 */
void captureThermalImageUART()
{
  if (!RPI_IDLE_READY)
  {
    radioPrintln("Raspberry Pi is not ready yet, retry once in RPI STATUS: IDLE");
    return;
  }

  radioPrintln("--- UART THERMAL CAPTURE ---");
  radioPrintln("Triggering RPI capture...");

  memset(imgBuf, 0, MAX_IMG); // Clear previous frame residue
  capturedImageLength = 0;

  piCaptureInProgress = true;

  // Clear any pending UART bytes before sending trigger
  while (Serial2.available())
    Serial2.read();

  // Send UART trigger command to Raspberry Pi
  Serial2.print(UART_TRIGGER_CMD);
  Serial2.flush(); // Ensure command is sent immediately

  radioPrintln("Waiting for framed message from RPI...");

  bool imageReceived = false;

  while (!imageReceived)
  {
    uint16_t rxLen = 0;
    bool isStatus = false;

    if (!recvFramedFromPi(Serial2, imgBuf, MAX_IMG, rxLen, isStatus))
    {
      radioPrintln("ERROR: Failed to receive framed message");
      piCaptureInProgress = false;
      return;
    }

    if (isStatus)
    {
      handleStatusPayload(imgBuf, rxLen);
      continue;
    }

    capturedImageLength = rxLen;
    imageReceived = true;
  }

  piCaptureInProgress = false;

  radioPrintln("--- UART IMAGE RECEPTION COMPLETE ---");
  radioPrint("Received ");
  radioPrint(String(capturedImageLength));
  radioPrintln(" image bytes");

  // Quick quality check (same logic you already had)
  if (capturedImageLength >= 38400)
  {
    int validPixels = 0;
    for (uint32_t i = 0; i + 1 < capturedImageLength; i += 2)
    {
      uint16_t pixel = imgBuf[i] | (uint16_t(imgBuf[i + 1]) << 8);
      float tempC = (pixel - 27315) / 100.0f;
      if (tempC >= 0 && tempC <= 60)
        validPixels++;
    }
    float validPct = (float)validPixels * 100.0f / (capturedImageLength / 2);
    radioPrint("Data quality: ");
    radioPrint(String(validPct, 1));
    radioPrintln("% valid temperature pixels");

    if (validPct > 95)
      radioPrintln("✓ Excellent data quality! Ready for radio transmission - press 'r'");
    else if (validPct > 50)
      radioPrintln("⚠️ Moderate data quality - may still be usable");
    else
      radioPrintln("❌ Poor data quality detected");
  }
  else
  {
    radioPrintln("⚠️ Received size smaller than expected for thermal image");
  }

  // Drain any post-capture status messages immediately
  pollPIUartStatus();

  /* Once we finished the thermal capture, immediately send the GPS and IMU data
  / to store with the ground station.
  */
  printGPSData();
  printIMUData();
}

/**
 * Sends a specific thermal data packet by index
 *
 * Constructs and sends a single thermal data packet at the given index.
 * Used for retransmitting missing packets upon ground station request.
 *
 * @param packetIndex The index of the packet to send (0-based)
 */
void sendSpecificPacket(uint16_t packetIndex)
{
  if (capturedImageLength == 0)
  {
    radioPrintln("ERROR: No image data available for retry");
    return;
  }

  const uint16_t totalPackets = (capturedImageLength + PACKET_DATA_SIZE - 1) / PACKET_DATA_SIZE;

  if (packetIndex >= totalPackets)
  {
    radioPrint("ERROR: Invalid packet index ");
    radioPrint(String(packetIndex));
    radioPrint(" (max ");
    radioPrint(String(totalPackets - 1));
    radioPrintln(")");
    return;
  }

  // Calculate packet data
  uint16_t byteOffset = (uint32_t)packetIndex * PACKET_DATA_SIZE;
  uint16_t remaining = capturedImageLength - byteOffset;
  uint16_t chunkSize = (remaining < (uint16_t)PACKET_DATA_SIZE) ? remaining : (uint16_t)PACKET_DATA_SIZE;

  // Build data packet
  ThermalDataPacket *dataPacket = (ThermalDataPacket *)radioTxBuffer;
  dataPacket->packetIndex = packetIndex;
  memcpy(dataPacket->data, &imgBuf[byteOffset], chunkSize);

  // Calculate CRC
  uint16_t packetCrc = crc16_ccitt(dataPacket->data, chunkSize);
  uint16_t crcOffset = 2 + chunkSize;
  memcpy(&radioTxBuffer[crcOffset], &packetCrc, sizeof(uint16_t));

  // Calculate actual packet size
  uint8_t frameLen = 2 + chunkSize + 2;

  if (!sendPacketReliable(radioTxBuffer, frameLen))
  {
    radioPrint("Failed to resend packet ");
    radioPrintln(String(packetIndex));
  }
  else
  {
#ifdef DEBUG
    radioPrint("Resent packet ");
    radioPrintln(String(packetIndex));
#endif
  }
}

/**
 * Handles retry request from ground station
 *
 * Parses the retry request packet containing bitmap of missing packets
 * and retransmits those specific packets, followed by an end packet.
 *
 * @param buf Buffer containing retry request packet
 * @param len Length of retry request packet
 */
void handleRetryRequest(uint8_t *buf, uint8_t len)
{
  // Size check, drop packet if too short
  if (len < sizeof(RetryRequestPacket))
  {
    radioPrintln("ERROR: Invalid retry request packet (too short)");
    return;
  }

  // Parse retry request
  RetryRequestPacket *retryRequest = (RetryRequestPacket *)buf;
  uint16_t expectedMissingCount = retryRequest->packetCount;
  uint16_t startIndex = retryRequest->offset;
  uint8_t *bitmap = retryRequest->bitmap;

  // No missing packets - just resend END packet to acknowledge request
  if (expectedMissingCount == 0)
  {
#ifdef DEBUG
    radioPrintln("Retry request with 0 missing packets - sending END packet");
#endif

    const uint16_t totalPackets = (capturedImageLength + PACKET_DATA_SIZE - 1) / PACKET_DATA_SIZE;
    const uint16_t imageCrc = crc16_ccitt(imgBuf, capturedImageLength);

    ThermalEndPacket endPacket;
    endPacket.marker1 = 0xEE;
    endPacket.marker2 = 0xEE;
    endPacket.packetCount = totalPackets;
    endPacket.imageCrc = imageCrc;

    sendPacketReliable((uint8_t *)&endPacket, sizeof(endPacket));
    return;
  }

  // Scan bitmap and resend missing packets
  uint16_t resentCount = 0;
  for (uint16_t bitIndex = 0; bitIndex < 360; bitIndex++)
  {
    uint16_t packetIndex = startIndex + bitIndex;

    // Stop if we exceed total packets
    const uint16_t totalPackets = (capturedImageLength + PACKET_DATA_SIZE - 1) / PACKET_DATA_SIZE;
    if (packetIndex >= totalPackets)
      break;

    // Check if this packet is marked as missing
    uint16_t byteIndex = bitIndex / 8;
    uint8_t bitPos = bitIndex % 8;
    if (bitmap[byteIndex] & (1 << bitPos))
    {
      // Resend the specific missing packet
      sendSpecificPacket(packetIndex);
      resentCount++;
    }
  }

  // Send END packet to signal retry completion
  const uint16_t totalPackets = resentCount;
  const uint16_t imageCrc = crc16_ccitt(imgBuf, capturedImageLength);

  ThermalEndPacket endPacket;
  endPacket.marker1 = 0xEE;
  endPacket.marker2 = 0xEE;
  endPacket.packetCount = totalPackets;
  endPacket.imageCrc = imageCrc;

  sendPacketReliable((uint8_t *)&endPacket, sizeof(endPacket));
}

/**
 * Sends a packet reliably with retry logic
 *
 * Attempts to send a packet up to MAX_SEND_PACKET_RETRIES times, with proper
 * radio state management and error handling. Provides visual feedback
 * via LED during transmission.
 *
 * @param data Pointer to packet data
 * @param len Length of packet data
 * @return true if packet was sent successfully, false otherwise
 */
bool sendPacketReliable(uint8_t *data, uint8_t len)
{
  if (len == 0 || len > rf23.maxMessageLength())
  {
#ifdef DEBUG
    Serial.println("Invalid message length: " + len);
#endif
    // TODO should probably send a message to gs or log somewhere the packet was too big to send?
    return false;
  }

  digitalWrite(LED_PIN, HIGH); // Turn on LED during transmission
  setRadioAmpTransmit();
  for (int retry = 0; retry < MAX_SEND_PACKET_RETRIES; retry++)
  {
    if (rf23.send(data, len))
    {
      if (rf23.waitPacketSent(RADIO_WAIT_PACKET_SENT_MS))
      {
        // Turn off LED
        digitalWrite(LED_PIN, LOW);
        setRadioAmpReceive();
        return true;
      }
    }
    if (retry < MAX_SEND_PACKET_RETRIES - 1)
    {
      delay(10); // Delay before retry
    }
  }
  setRadioAmpReceive();
  digitalWrite(LED_PIN, LOW); // Turn off LED
  return false;
}

/**
 * Transmits captured thermal image data via radio
 *
 * Sends the captured image data in packetized format with header,
 * data packets, and end packet. Provides transmission statistics
 * and quality assessment. Requires captured image data to be present.
 */
void sendThermalDataViaRadio()
{
  if (capturedImageLength == 0)
  {
    radioPrintln("No image captured yet!");
    return;
  }

  const uint16_t totalPackets = (capturedImageLength + PACKET_DATA_SIZE - 1) / PACKET_DATA_SIZE;
  if (totalPackets == 0)
  {
    radioPrintln("No packets to transmit");
    return;
  }

  const uint16_t imageCrc = crc16_ccitt(imgBuf, capturedImageLength);

  // radioPrintln("--- SENDING THERMAL IMAGE VIA RADIO ---");
  // radioPrintln("Image size: " + String(capturedImageLength) + " bytes");
  // radioPrintln("Total packets: " + String(totalPackets));

#ifdef DEBUG
  Serial.println(F("=== Radio thermal downlink ==="));
  Serial.print(F("Bytes: "));
  Serial.println(capturedImageLength);
  Serial.print(F("Packets: "));
  Serial.println(totalPackets);
  Serial.print(F("Image CRC16: 0x"));
  Serial.println(String(imageCrc, HEX));
#endif

  // Build and send header packet
  ThermalHeaderPacket headerPacket;
  headerPacket.marker1 = 0xFF;
  headerPacket.marker2 = 0xFF;
  headerPacket.imageLength = capturedImageLength;
  headerPacket.totalPackets = totalPackets;
  headerPacket.magic[0] = 0xDEAD;
  headerPacket.magic[1] = 0xBEEF;

  if (!sendPacketReliable((uint8_t *)&headerPacket, sizeof(headerPacket)))
  {
    radioPrintln("❌ Failed to send header!");
    return;
  }

  uint16_t bytesSent = 0;
  uint16_t packetNum = 0;
  uint16_t successCount = 0;
  uint16_t failCount = 0;
#ifdef DEBUG
  unsigned long startTime = millis();
#endif

  while (bytesSent < capturedImageLength)
  {
    uint16_t remaining = capturedImageLength - bytesSent;
    uint16_t chunkSize = (remaining < (uint16_t)PACKET_DATA_SIZE) ? remaining : (uint16_t)PACKET_DATA_SIZE;

    // Build data packet
    ThermalDataPacket *dataPacket = (ThermalDataPacket *)radioTxBuffer;
    dataPacket->packetIndex = packetNum;
    memcpy(dataPacket->data, &imgBuf[bytesSent], chunkSize);

    // Calculate CRC and place it at correct position for variable-length packet
    uint16_t packetCrc = crc16_ccitt(dataPacket->data, chunkSize);
    // Write CRC16 at correct position (after actual data, not at struct's fixed offset)
    // CRC offset = 2 bytes (packetIndex) + chunkSize bytes (data)
    uint16_t crcOffset = 2 + chunkSize;
    memcpy(&radioTxBuffer[crcOffset], &packetCrc, sizeof(uint16_t));

    // Calculate actual packet size (may be smaller for last packet)
    // Packet structure: [2 bytes packetIndex][variable data][2 bytes CRC]
    uint8_t frameLen = 2 + chunkSize + 2;

    // send packet
    if (sendPacketReliable(radioTxBuffer, frameLen))
    {
      successCount++;
    }
    else
    {
      failCount++;
    }

    bytesSent += chunkSize;
    packetNum++;

#ifdef DEBUG
    if (((packetNum & 0x3F) == 0) || bytesSent >= capturedImageLength)
    {
      Serial.print(F("TX bytes: "));
      Serial.print(bytesSent);
      Serial.print(F("/"));
      Serial.println(capturedImageLength);
    }
#endif
  }

  // Build and send end packet
  ThermalEndPacket endPacket;
  endPacket.marker1 = 0xEE;
  endPacket.marker2 = 0xEE;
  endPacket.packetCount = packetNum;
  endPacket.imageCrc = imageCrc;

  if (!sendPacketReliable((uint8_t *)&endPacket, sizeof(endPacket)))
  {
    radioPrintln("❌ Failed to send end data packet!");
    return;
  }

#ifdef DEBUG
  unsigned long elapsed = millis() - startTime;
  float duration = elapsed / 1000.0f;
  float successPct = totalPackets ? (float)successCount * 100.0f / totalPackets : 0.0f;
  float dataRate = duration > 0.001f ? capturedImageLength / duration : 0.0f;

  Serial.print(F("Packets ok: "));
  Serial.print(successCount);
  Serial.print(F("/"));
  Serial.println(totalPackets);
  if (failCount > 0)
  {
    Serial.print(F("Failed sends: "));
    Serial.println(failCount);
  }

  Serial.print(F("Elapsed (s): "));
  Serial.println(String(duration, 2));
  Serial.print(F("Data rate (B/s): "));
  Serial.println(String(dataRate, 1));

  radioPrintln("=== SAT TRANSMISSION COMPLETE ===");
  radioPrint("SAT Packets sent ok: ");
  radioPrint(String(successCount));
  radioPrint("/");
  radioPrint(String(totalPackets));
  radioPrint(" (");
  radioPrint(String(successPct, 1));
  radioPrintln("%)");
  radioPrint("Data rate: ");
  radioPrint(String(dataRate, 0));
  radioPrintln(" bytes/sec");
  radioPrint("Image CRC16: 0x");
  radioPrintln(String(imageCrc, HEX));

  if (successCount == totalPackets)
  {
    radioPrintln("✅ SAT Perfect transmission!");
  }
  else if (successCount > totalPackets * 0.95)
  {
    radioPrintln("✅ SAT Excellent transmission!");
  }
  else
  {
    radioPrintln("⚠️ SAT Some packets lost");
  }
#endif
}

// ============================================================================
// LIVESTREAM MODE FUNCTIONS
// ============================================================================

/**
 * Check if the buffer starts with stream magic header
 */
bool isStreamMagic(const uint8_t *buf)
{
  return buf[0] == STREAM_MAGIC[0] && buf[1] == STREAM_MAGIC[1] &&
         buf[2] == STREAM_MAGIC[2] && buf[3] == STREAM_MAGIC[3];
}

/**
 * Start livestream mode - send command to Pi and enter stream state
 */
void startStreamMode()
{
  if (!RPI_IDLE_READY)
  {
    radioPrintln("STREAM: RPi not ready, wait for RPI STATUS: IDLE");
    return;
  }

  radioPrintln("STREAM: Starting livestream mode...");

  // Clear any pending UART data
  while (Serial2.available())
    Serial2.read();

  // Send stream start command to Pi
  Serial2.print(STREAM_START_CMD);
  Serial2.flush();

  streamModeActive = true;
  radioPrintln("STREAM: Mode active - forwarding frames to GS");
}

/**
 * Stop livestream mode - send command to Pi and exit stream state
 */
void stopStreamMode()
{
  if (!streamModeActive)
  {
    radioPrintln("STREAM: Not in stream mode");
    return;
  }

  radioPrintln("STREAM: Stopping livestream mode...");

  // Send stream stop command to Pi
  Serial2.print(STREAM_STOP_CMD);
  Serial2.flush();

  streamModeActive = false;
  radioPrintln("STREAM: Mode stopped");
}

/**
 * Scan UART byte-by-byte looking for STREAM_MAGIC header
 * This allows recovery from partial data or misalignment
 *
 * @param timeout_ms Maximum time to wait for magic header
 * @return true if stream magic was found
 */
bool waitForStreamMagic(uint32_t timeout_ms)
{
  uint32_t start = millis();
  uint8_t matchIndex = 0;

  while (millis() - start < timeout_ms)
  {
    if (Serial2.available())
    {
      uint8_t b = Serial2.read();
      if (b == STREAM_MAGIC[matchIndex])
      {
        matchIndex++;
        if (matchIndex == 4)
        {
          return true; // Found complete magic header
        }
      }
      else
      {
        // Mismatch - check if this byte could be start of new magic
        matchIndex = (b == STREAM_MAGIC[0]) ? 1 : 0;
      }
    }
    else
    {
      delay(1);
    }
  }
  return false; // Timeout
}

/**
 * Receive one stream frame from Pi via UART
 * Stream frame format: [STREAM_MAGIC 4B][Frame Seq 1B][Size 2B][Data 4800B][End 2B]
 * Uses byte-by-byte scanning to find magic header (handles misalignment)
 *
 * @param frameSeq Output parameter for frame sequence number
 * @return true if frame received successfully
 */
bool recvStreamFrameFromPi(uint8_t &frameSeq)
{
  const uint32_t STREAM_HEADER_TIMEOUT_MS = 1000; // 1s timeout for streaming
  const uint32_t STREAM_DATA_TIMEOUT_MS = 2000;   // 2s for frame data (4800 bytes)

  // Don't even try if no data available
  if (!Serial2.available())
  {
    return false;
  }

  // Scan for stream magic byte-by-byte
  if (!waitForStreamMagic(STREAM_HEADER_TIMEOUT_MS))
  {
    return false; // Timeout or no magic found
  }

  // Magic found, now read rest of header: 1 seq + 2 size = 3 bytes
  uint8_t headerRest[3];
  if (!readExact(Serial2, headerRest, 3, STREAM_HEADER_TIMEOUT_MS))
  {
    radioPrintln("STREAM: Header rest timeout");
    return false;
  }

  frameSeq = headerRest[0];
  uint16_t frameSize = (uint16_t)headerRest[1] | ((uint16_t)headerRest[2] << 8);

#ifdef DEBUG
  radioPrint("STREAM: seq=");
  radioPrint(String(frameSeq));
  radioPrint(" size=");
  radioPrintln(String(frameSize));
#endif

  if (frameSize != STREAM_FRAME_SIZE)
  {
    radioPrint("STREAM: Bad frame size ");
    radioPrint(String(frameSize));
    radioPrint(" (expected ");
    radioPrint(String(STREAM_FRAME_SIZE));
    radioPrint(", got bytes 0x");
    radioPrint(String(headerRest[1], HEX));
    radioPrint(" 0x");
    radioPrint(String(headerRest[2], HEX));
    radioPrintln(")");
    // Drain remaining data to resync
    while (Serial2.available()) Serial2.read();
    return false;
  }

  // Read frame data
  if (!readExact(Serial2, streamFrameBuffer, STREAM_FRAME_SIZE, STREAM_DATA_TIMEOUT_MS))
  {
    radioPrintln("STREAM: Frame data timeout");
    // Drain buffer to resync
    while (Serial2.available()) Serial2.read();
    return false;
  }

  // Read end markers
  uint8_t ender[2];
  if (!readExact(Serial2, ender, 2, 500))
  {
    radioPrintln("STREAM: End marker timeout");
    // Drain buffer to resync
    while (Serial2.available()) Serial2.read();
    return false;
  }

  if (ender[0] != 0xFF || ender[1] != 0xFF)
  {
    radioPrint("STREAM: Bad end markers 0x");
    radioPrint(String(ender[0], HEX));
    radioPrint(" 0x");
    radioPrintln(String(ender[1], HEX));
    // Drain buffer to resync for next frame
    while (Serial2.available()) Serial2.read();
    return false;
  }

  return true;
}

/**
 * Send a stream frame via radio to ground station
 * Uses STREAM_FRAME_TYPE packets for identification
 *
 * @param frameSeq Frame sequence number to include in packets
 */
void sendStreamFrameViaRadio(uint8_t frameSeq)
{
  // Send header packet first
  StreamFrameHeaderPacket headerPkt;
  headerPkt.type = STREAM_FRAME_TYPE;
  headerPkt.frameSeq = frameSeq;
  headerPkt.frameSize = STREAM_FRAME_SIZE;
  headerPkt.totalPackets = STREAM_PACKETS_PER_FRAME;

  setRadioAmpTransmit();

  if (!rf23.send((uint8_t *)&headerPkt, sizeof(headerPkt)))
  {
    setRadioAmpReceive();
    return;
  }
  rf23.waitPacketSent(100); // Short timeout for streaming

  // Send data packets (best-effort, no retries for speed)
  uint16_t bytesSent = 0;
  uint8_t packetNum = 0;

  while (bytesSent < STREAM_FRAME_SIZE)
  {
    // Check for stop command every 20 packets (allows faster response to stop)
    if (packetNum > 0 && (packetNum % 20) == 0)
    {
      setRadioAmpReceive();
      delay(2); // Brief pause to check for incoming packets
      if (rf23.available())
      {
        uint8_t len = sizeof(radioRxBuffer);
        if (rf23.recv(radioRxBuffer, &len))
        {
          if (len >= 2 && (radioRxBuffer[0] == 'v' || radioRxBuffer[0] == 'V') && radioRxBuffer[1] == '0')
          {
            stopStreamMode();
            return; // Exit immediately
          }
        }
      }
      setRadioAmpTransmit();
    }

    uint16_t remaining = STREAM_FRAME_SIZE - bytesSent;
    uint8_t chunkSize = (remaining < PACKET_DATA_SIZE) ? remaining : PACKET_DATA_SIZE;

    // Build stream data packet
    StreamDataPacket *dataPkt = (StreamDataPacket *)radioTxBuffer;
    dataPkt->type = STREAM_FRAME_TYPE;
    dataPkt->frameSeq = frameSeq;
    dataPkt->packetIndex = packetNum;
    memcpy(dataPkt->data, &streamFrameBuffer[bytesSent], chunkSize);

    // Actual packet size: type(1) + frameSeq(1) + packetIndex(1) + data(chunkSize)
    uint8_t packetSize = 3 + chunkSize;

    rf23.send(radioTxBuffer, packetSize);
    rf23.waitPacketSent(50);

    bytesSent += chunkSize;
    packetNum++;
  }

  setRadioAmpReceive();
}

/**
 * Request a single frame from the Raspberry Pi
 * Sends FRAME command over UART and clears any stale buffer data
 */
void requestFrameFromPi()
{
  // Clear any stale UART data before requesting
  while (Serial2.available()) Serial2.read();

  // Send frame request command
  Serial2.print(FRAME_REQUEST_CMD);
  Serial2.flush();
}

/**
 * Handle stream mode - called from main loop when streaming is active
 * Uses request-response flow control: request frame → receive → transmit → repeat
 *
 * Flow:
 * 1. Request frame from Pi (FRAME command)
 * 2. Wait for and receive frame with STREAM_MAGIC header
 * 3. Transmit frame over radio to ground station
 * 4. Check for stop command from GS
 * 5. Request next frame
 */
void handleStreamMode()
{
  // Check for stop command from GS via radio first
  if (rf23.available())
  {
    uint8_t len = sizeof(radioRxBuffer);
    if (rf23.recv(radioRxBuffer, &len))
    {
      // Check for stream stop command
      if (len >= 2 && (radioRxBuffer[0] == 'v' || radioRxBuffer[0] == 'V') && radioRxBuffer[1] == '0')
      {
        stopStreamMode();
        return;
      }
    }
  }

  // Request a frame from Pi
  requestFrameFromPi();

  // Wait for and receive the frame
  uint8_t frameSeq;
  if (recvStreamFrameFromPi(frameSeq))
  {
    // Successfully received frame - transmit over radio to GS
    sendStreamFrameViaRadio(frameSeq);

    // Brief message every 10 frames (frameSeq wraps at 255)
    if ((frameSeq % 10) == 0)
    {
      radioPrint("STREAM: Frame ");
      radioPrintln(String(frameSeq));
    }
  }
  else
  {
    // Failed to receive frame - will retry on next loop iteration
    // The receive function already drains buffer on errors
  }
}
