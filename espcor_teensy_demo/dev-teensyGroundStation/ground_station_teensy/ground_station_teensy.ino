/*
 * Ground Station Command Interpreter
 * A comprehensive command interpreter system for Arduino with serial communication
 *
 * Features:
 * - Serial command parsing
 * - Command history
 * - Help system
 * - Error handling
 * - Extensible command structure
 * - Version management
 *
 * Version: 2.0.0
 * Build Date: 2025-12-08
 * Author: Artemis CubeSat Team
 */

#include <Arduino.h>
#include <SPI.h>
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

// Version information
#define VERSION_MAJOR 2
#define VERSION_MINOR 0
#define VERSION_PATCH 0
#define VERSION_BUILD "2025-12-08"
#define VERSION_STRING "2.0.0"
#define BUILD_INFO "Arduino Teensy 4.1 Ground Station Command Interpreter"

// Packed struct attribute for ensuring no padding bytes
#define PACKED __attribute__((packed))

// Command structure definition
struct Command
{
  const char *name;
  const char *description;
  void (*function)(const char *args);
};

// Radio configuration pins and object
/** Chip select pin for RF22 module */
const uint8_t RADIO_CS = 38;
/** Interrupt pin for RF22 module  */
const uint8_t RADIO_INT = 40;
/** Pin 30 from Arduino to RFM23BP RX_ON PIN */
const uint8_t RADIO_RX_ON_PIN = 30;
/** Pin 31 from Arduino to RFM23BP RX_ON PIN */
const uint8_t RADIO_TX_ON_PIN = 31;
// note that hardware_spi1 uses the RHHardwareSPI1.h library (same as using SPI1 bus but more explicit for the RH_RF22 library driver)
RH_RF22 rf23(RADIO_CS, RADIO_INT, hardware_spi1);
const int RADIO_WAIT_PACKET_SENT_MS = 500;

const uint8_t LED_PIN = 13;

// Image reception buffer and tracking variables
const uint32_t MAX_IMG = 40000;      // Maximum image buffer size (40KB)
uint8_t imgBuffer[MAX_IMG];          // Buffer to store received thermal image data
uint16_t expectedLength = 0;         // Expected total image size from header
uint16_t expectedPackets = 0;        // Total number of packets expected
uint16_t receivedPackets = 0;        // Number of packets successfully received
bool headerReceived = false;         // Flag indicating header packet was received
bool downloadingThermalData = false; // Flag indicating active thermal data packet transfer
bool imageComplete = false;          // Flag indicating all data was received
uint16_t expectedImageCrc = 0;       // CRC expected from satellite end packet
uint16_t lastComputedImageCrc = 0;   // CRC calculated locally after reception
uint32_t crcErrorCount = 0;          // Count of packets dropped due to CRC mismatch
bool crcVerified = false;            // True when computed CRC matches expected CRC

// Packet tracking for duplicate detection and missing packet identification
bool packetReceived[1200]; // Array to track which packets have been received
uint16_t maxPacketNum = 0; // Highest packet number received

// Communication statistics
unsigned long packetsReceived = 0;             // Total packets received (including duplicates)
unsigned long lastPacketTime = 0;              // Timestamp of last packet reception
int lastRSSI = 0;                              // Signal strength of last received packet
unsigned long thermalDataDownloadDuration = 0; // Track total time from request to completion
unsigned long thermalDataTransferDuration = 0; // Track pure data transfer time (first packet to last packet)

// Auto mode flag for continuous reception
bool autoMode = false;

// Packet size configuration
const uint8_t RADIO_PACKET_MAX_SIZE = 49; // RF22 payload limit of 50 for reliable recv/tx, 47 for reliability.
const uint8_t THERMAL_PACKET_OVERHEAD = 2 /*packet index*/ + 2 /*CRC16*/;
const uint8_t PACKET_DATA_SIZE = RADIO_PACKET_MAX_SIZE - THERMAL_PACKET_OVERHEAD; // Data payload size per packet (excludes index+CRC)
// Shared radio buffers to avoid per-call stack allocations
uint8_t radioRxBuffer[RADIO_PACKET_MAX_SIZE + RADIO_PACKET_MAX_SIZE];
uint8_t radioTxBuffer[RADIO_PACKET_MAX_SIZE + THERMAL_PACKET_OVERHEAD];

// Serial message radio reception parameters
const uint8_t SERIAL_MSG_TYPE = 0xAA; // Message type identifier for serial output
// const uint16_t SERIAL_MSG_TYPE = 0xAAAA;       // Message type identifier for serial output
const uint8_t SERIAL_CONTINUATION_FLAG = 0x80;                // High bit indicates additional chunks follow
const uint8_t MAX_SERIAL_MSG_LEN = RADIO_PACKET_MAX_SIZE - 2; // Maximum serial message length per packet payload

// Packet retry protocol
const uint8_t RETRY_REQUEST_TYPE = 0xBB;            // Message type for requesting missing packets
const uint8_t MAX_RETRY_ATTEMPTS = 2;               // Maximum number of retry rounds
const uint16_t RETRY_MISSING_PACKET_THRESHOLD = 10; // Only trigger retries when more than this many packets are missing

// Livestream protocol constants
const uint8_t STREAM_FRAME_TYPE = 0xCC;       // Message type for livestream frame packets
const uint16_t STREAM_FRAME_SIZE = 4800;      // 80x60 8-bit = 4800 bytes per frame
const uint8_t STREAM_PACKETS_PER_FRAME = 107; // ~107 packets per frame (4800/45)

// Retry timeout tracking
unsigned long lastRetryRequestTime = 0;           // When we last sent a retry request
const unsigned long RETRY_TIMEOUT_MS = 5000;      // Wait 20 seconds before re-requesting
const unsigned long RETRY_GRACE_PERIOD_MS = 2500; // Wait 5 seconds after end packet before requesting retries
uint8_t retryAttemptCount = 0;                    // How many times we've requested retries
bool waitingForRetry = false;                     // Flag: are we currently waiting for retry packets?
unsigned long endPacketReceivedTime;              // When we received the end packet

// === Packet Structure Definitions (Must match satellite!) ===

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
  uint8_t data[MAX_SERIAL_MSG_LEN]; // Message text (variable size)
};

/**
 * Retry request packet structure (sent to satellite)
 * Contains bitmap of missing packet indices that need retransmission
 * Offset allows handling images with >360 packets
 */
struct PACKED RetryRequestPacket
{
  uint8_t type;         // RETRY_REQUEST_TYPE (0xBB)
  uint16_t packetCount; // Number of missing packets in this request i.e. number of '1' bits in bitmap
  uint16_t offset;      // Starting packet index of the range this bitmap covers
  uint8_t bitmap[45];   // 360 bits = 45 bytes, bit=1 means packet is missing
};

/**
 * Stream frame header packet structure for livestream reception
 * Received from satellite to indicate start of a new frame
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
 * Contains chunk of stream frame data
 * Size: 3 + data length bytes
 */
struct PACKED StreamDataPacket
{
  uint8_t type;           // STREAM_FRAME_TYPE (0xCC)
  uint8_t frameSeq;       // Frame sequence number to associate with header
  uint8_t packetIndex;    // Packet index within frame (0-106)
  uint8_t data[PACKET_DATA_SIZE]; // Frame data chunk
};

// Global variables
String inputBuffer = "";
String commandHistory[10];
int historyIndex = 0;
bool commandComplete = false;
bool serialConnected = false;
bool interruptRequested = false;
const int RASPBERRY_PI_GPIO_PIN = 36;
const String THERMAL_CAPTURE_TIMESTAMP_FLAG = "--- UART THERMAL CAPTURE ---";
const String THERMAL_CSV_START = "=== START CSV ===";
const String THERMAL_CSV_END = "=== END CSV ===";

// Livestream state
bool streamModeActive = false;
uint32_t streamPacketsReceivedTotal = 0;  // Debug: total stream packets received
uint8_t currentStreamFrameSeq = 0;
uint8_t streamFrameBuffer[STREAM_FRAME_SIZE];  // Buffer for assembling current frame
uint8_t streamPacketsReceived[STREAM_PACKETS_PER_FRAME];  // Track which packets received
uint8_t streamPacketCount = 0;  // Number of packets received for current frame
// Binary stream protocol uses magic header 0xCAFEBABE and end marker 0xEDED

// Forward declarations
void parseCommand(const String &input);
void executeCommand(const char *cmd, const char *args);
void printPrompt();
void addToHistory(const String &command);
void showHistory();
void clearHistory();
void checkForInterrupt();
void resetInterrupt();
bool isInterruptRequested();
void helperTime(unsigned long durationMillis);

// Radio function declarations
void initRadio();
void clearReception();
void handlePacket();
void processPacket(uint8_t *buf, uint8_t len);
void handleThermalHeaderPacket(uint8_t *buf);
void handleThermalEndPacket(uint8_t *buf);
void handleThermalDataPacket(uint8_t *buf, uint8_t len);
bool handleSerialMessage(uint8_t *buf, uint8_t len, String *messageOut = nullptr);
void showReceptionSummary();
void exportThermalData();
void forwardToSatellite(char cmd);
void dumpRf23PendingPacketsToSerial();
void printRf23HexLines(const uint8_t *data, uint8_t length);
bool requestMissingPackets();

// Stream mode function declarations
void handleStreamFrameHeader(uint8_t *buf, uint8_t len);
void handleStreamFrameData(uint8_t *buf, uint8_t len);
void outputStreamFrame();
void resetStreamFrame();
void cmdStream(const char *args);

// Command function prototypes
void cmdHelp(const char *args);
void cmdVersion(const char *args);
void cmdStatus(const char *args);
void cmdPing(const char *args);
void cmdEcho(const char *args);
void cmdLed(const char *args);
void cmdAnalog(const char *args);
void cmdDigital(const char *args);
void cmdTime(const char *args);
void cmdGSReset(const char *args);
void cmdSatelliteReset(const char *args);
void cmdHistory(const char *args);
void cmdClear(const char *args);
void cmdRPIControl(const char *args);
void cmdTestInterrupt(const char *args);
void cmdTestLoopPing(const char *args);
void cmdRadio(const char *args);
void cmdExport(const char *args);
void cmdCapture(const char *args);
void cmdRequest(const char *args);
void cmdRadioStatus(const char *args);
void cmdSensor(const char *args);

// Command table - easily extensible
const Command commands[] = {
    {"help", "Show available commands", cmdHelp},
    {"version", "Show GS version information", cmdVersion},
    {"status", "Show system status", cmdStatus},
    {"ping", "Test communication between GS and Satellite", cmdPing},
    {"echo", "Echo back the arguments", cmdEcho},
    {"led", "Control LED (on/off/toggle)", cmdLed},
    {"analog", "Read analog pin (analog <pin>)", cmdAnalog},
    {"digital", "Read/write digital pin (digital <pin> [value])", cmdDigital},
    {"time", "Show uptime", cmdTime},
    {"gs_reset", "Reset the ground station system", cmdGSReset},
    {"sat_reset", "Reset the satellite system", cmdSatelliteReset},
    {"history", "Show command history", cmdHistory},
    {"clear", "Clear screen", cmdClear},
    {"rpi", "Control Raspberry Pi (rpi <on|off|status>)", cmdRPIControl},
    {"testint", "Test interrupt functionality (testint <seconds>)", cmdTestInterrupt},
    {"pingloop", "Continuously ping satellite and report RSSI (pingloop [interval_ms])", cmdTestLoopPing},
    {"radio", "Radio control (radio <init|status|tx|rx|dump>)", cmdRadio},
    {"export", "Export thermal data as CSV", cmdExport},
    {"capture", "Command satellite to capture thermal data", cmdCapture},
    {"request", "Request thermal data downlink from satellite", cmdRequest},
    {"rstatus", "Show radio reception status", cmdRadioStatus},
    {"sensor", "Request satellite sensor data (sensor <gps|imu|both|gps_init|imu_init>)", cmdSensor},
    {"stream", "Control livestream mode (stream <start|stop>)", cmdStream}};

const int numCommands = sizeof(commands) / sizeof(commands[0]);

// Helper: send 2-byte packet to satellite ('p', <sub>) and wait for send
bool sendBytesToSatellite(const uint8_t *data, uint8_t len, unsigned long timeout_ms = 500)
{
  digitalWrite(LED_PIN, HIGH);
  bool ok = false;
  setRadioAmpTransmit();
  if (!rf23.send((uint8_t *)data, len))
  {
    Serial.println("Failed to queue radio packet");
  }
  else
  {
    if (rf23.waitPacketSent(timeout_ms))
      ok = true;
    else
      Serial.println("Timeout waiting for packet send");
  }

  digitalWrite(LED_PIN, LOW);
  setRadioAmpReceive();
  return ok;
}

void parseCommand(const String &input)
{
  // Trim whitespace
  String trimmed = input;
  trimmed.trim();

  if (trimmed.length() == 0)
  {
    return;
  }

  // Find space to separate command and arguments
  int spaceIndex = trimmed.indexOf(' ');
  String command;
  String args;

  if (spaceIndex == -1)
  {
    command = trimmed;
    args = "";
  }
  else
  {
    command = trimmed.substring(0, spaceIndex);
    args = trimmed.substring(spaceIndex + 1);
  }

  // Convert to lowercase for case-insensitive matching
  command.toLowerCase();

  // Execute the command
  executeCommand(command.c_str(), args.c_str());
}

void executeCommand(const char *cmd, const char *args)
{
  // Search for command in command table
  for (int i = 0; i < numCommands; i++)
  {
    if (strcmp(cmd, commands[i].name) == 0)
    {
      commands[i].function(args);
      return;
    }
  }

  // Command not found
  Serial.print("Error: Unknown command '");
  Serial.print(cmd);
  Serial.println("'");
  Serial.println("Type 'help' for available commands");
}

void printPrompt()
{
  // Don't print prompt during streaming - clutters output
  if (streamModeActive) return;
  Serial.print("GS> ");
}

void addToHistory(const String &command)
{
  // Shift history array
  for (int i = 9; i > 0; i--)
  {
    commandHistory[i] = commandHistory[i - 1];
  }
  commandHistory[0] = command;

  if (historyIndex < 10)
  {
    historyIndex++;
  }
}

void clearHistory()
{
  for (int i = 0; i < 10; i++)
  {
    commandHistory[i] = "";
  }
  historyIndex = 0;
}

// void checkForInterrupt()
// {
//   // Check for interrupt character (Q) without blocking
//   if (Serial.available())
//   {
//     char c = Serial.peek(); // Look at next character without consuming it
//     if (c == 'Q' || c == 'q')
//     {
//       // Consume the interrupt character
//       Serial.read();
//       interruptRequested = true;
//       Serial.println("\n*** INTERRUPT REQUESTED ***");
//       Serial.println("Returning to command prompt...");
//       printPrompt();
//     }
//   }
// }

// Function to check for interrupt during command execution
bool isInterruptRequested()
{
  // Check for Q key during command execution
  if (Serial.available())
  {
    char c = Serial.peek();
    if (c == 'Q' || c == 'q')
    {
      Serial.read(); // Consume the Q
      interruptRequested = true;
      Serial.println("\n*** INTERRUPT REQUESTED ***");
      return true;
    }
  }
  return interruptRequested;
}

void resetInterrupt()
{
  interruptRequested = false;
}
void setRadioAmpTransmit()
{
  /**
   * Transmit burst: TXON low, RXON high PER THE DATASHEET.
   * Receive window: RXON high, TXON low.
   * Idle/standby: both low (saves current, keeps switch centered).
   */
  // Serial.println("Setting radio amp to TRANSMIT");
  digitalWrite(RADIO_RX_ON_PIN, HIGH);
  // Serial.print("RX_ON: HIGH");
  digitalWrite(RADIO_TX_ON_PIN, LOW);
  // Serial.println("   | TX_ON: LOW");
  delayMicroseconds(300); // ensure the amp is settled before transmitting
  // Serial.println("RADIO AMP SET TO TRANSMIT");
}
void setRadioAmpReceive()
{
  /**
   * Transmit burst: TXON low, RXON high PER THE DATASHEET.
   * Receive window: RXON low, TXON high.
   * Idle/standby: both low (saves current, keeps switch centered).
   */
  // Serial.println("Setting radio amp to RX");
  digitalWrite(RADIO_RX_ON_PIN, LOW);
  // Serial.print("RX_ON: LOW");
  digitalWrite(RADIO_TX_ON_PIN, HIGH);
  // Serial.println("   | TX_ON: HIGH");
  delayMicroseconds(300); // ensure the amp is settled before receiving
  // Serial.println("RADIO AMP SET TO RX");
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
// Radio function implementations
void initRadio()
{
  Serial.println("Initializing radio...");

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

  // Initialize RF22 radio module
  if (!rf23.init())
  {
    Serial.println("Radio init failed!");
    Serial.println("Delaying init for 5 seconds, continuing without radio. try 'radio init' again.");
    delay(5000);
    return; // Allow GS to continue so user can retry manually
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

  // rf23.setTxPower(RH_RF22_RF23BP_TXPOW_28DBM); // 28dBm lowest available power for RFM23BP
  rf23.setTxPower(RH_RF22_RF23BP_TXPOW_30DBM); // 30dBm (1000mW) - max for RFM23BP
  rf23.setModeIdle();                          // Set radio to idle mode
  Serial.println("GS Radio hardcoded config: 433MHz, GFSK_Rb38_4Fd19_6 38.4 kbps, 19.6 kHz deviation, 30dBm tx power.");
  delay(10);
  Serial.println("GS Radio ready");
}

void clearReception()
{
  headerReceived = false;
  imageComplete = false;
  expectedLength = 0;
  expectedPackets = 0;
  receivedPackets = 0;
  maxPacketNum = 0;
  expectedImageCrc = 0;
  lastComputedImageCrc = 0;
  crcErrorCount = 0;
  crcVerified = false;
  memset(imgBuffer, 0, MAX_IMG);
  memset(packetReceived, false, sizeof(packetReceived)); // Clear packet tracking array
  lastPacketTime = 0;

  // Reset retry state
  waitingForRetry = false;
  lastRetryRequestTime = 0;
  retryAttemptCount = 0;
}

void handlePacket()
{
  uint8_t len = sizeof(radioRxBuffer);
  if (rf23.recv(radioRxBuffer, &len))
  {
    digitalWrite(LED_PIN, HIGH); // Turn on LED to indicate packet reception
    lastPacketTime = millis();
    lastRSSI = rf23.lastRssi(); // Store signal strength
    packetsReceived++;
    processPacket(radioRxBuffer, len); // Process the received packet
    digitalWrite(LED_PIN, LOW);        // Turn off LED
  }
}

void processPacket(uint8_t *buf, uint8_t len)
{
  // Check for stream frame packets first (type 0xCC) - ONLY when streaming is active
  // This prevents 0xCC bytes in thermal data from being misinterpreted as stream packets
  if (streamModeActive && buf[0] == STREAM_FRAME_TYPE && len >= 3)
  {
    streamPacketsReceivedTotal++;

    // Debug: print every 100th stream packet received
    if (streamPacketsReceivedTotal % 100 == 1)
    {
      Serial.print("STREAM_DBG: pkt#");
      Serial.print(streamPacketsReceivedTotal);
      Serial.print(" len=");
      Serial.print(len);
      Serial.print(" cnt=");
      Serial.println(streamPacketCount);
    }

    // Stream header packet is 6 bytes, data packets are 3+ bytes
    if (len == sizeof(StreamFrameHeaderPacket))
    {
      handleStreamFrameHeader(buf, len);
    }
    else
    {
      handleStreamFrameData(buf, len);
    }
    return;
  }

  //  Check for serial message packet (MSG_TYPE + LENGTH + MESSAGE_DATA)
  if (buf[0] == SERIAL_MSG_TYPE && !downloadingThermalData && len >= 2)
  {
    handleSerialMessage(buf, len);
  }
  // Check for header packet (10 bytes with magic bytes)
  else if (len == 10 && buf[0] == 0xFF && buf[1] == 0xFF)
  {
    handleThermalHeaderPacket(buf);
  }
  // Check for end packet (6 bytes with magic bytes)
  else if (len == 6 && buf[0] == 0xEE && buf[1] == 0xEE)
  {
    handleThermalEndPacket(buf);
  }
  // Check for data packet (requires valid header and incomplete image)
  else if (headerReceived && !imageComplete && len >= 3)
  {
    handleThermalDataPacket(buf, len);
  }
  else
  {
    // should never hit this but leave for debugging radio packets.
    Serial.println("Header received: " + String(headerReceived));
    Serial.println("imageComplete: " + String(imageComplete));
    Serial.println("buf len: " + String(len));
    Serial.println("Unknown radio packet?! Dumping radio packets.");
    dumpRf23PendingPacketsToSerial();
  }
}

void handleThermalHeaderPacket(uint8_t *buf)
{
  // Parse header packet
  ThermalHeaderPacket *headerPacket = (ThermalHeaderPacket *)buf;

  expectedLength = headerPacket->imageLength;
  expectedPackets = headerPacket->totalPackets;

  Serial.println("\n📦 THERMAL IMAGE HEADER RECEIVED!");
  Serial.print("Expected size: ");
  Serial.print(expectedLength);
  Serial.println(" bytes");
  Serial.print("Expected packets: ");
  Serial.println(expectedPackets);

  if (expectedLength > MAX_IMG)
  {
    Serial.println("✗ Header length exceeds local buffer; aborting reception");
    clearReception();
    return;
  }

  // Validate magic bytes (0xDEAD 0xBEEF)
  if (headerPacket->magic[0] == 0xDEAD && headerPacket->magic[1] == 0xBEEF)
  {
    Serial.println("✓ Header valid - receiving thermal data...");
    headerReceived = true;
    imageComplete = false;
    downloadingThermalData = false;
    autoMode = true;
    receivedPackets = 0;
    maxPacketNum = 0;
    expectedImageCrc = 0;
    lastComputedImageCrc = 0;
    crcErrorCount = 0;
    crcVerified = false;
    memset(packetReceived, false, sizeof(packetReceived)); // Reset packet tracking
    memset(imgBuffer, 0, expectedLength);

    // Start timing the pure data transfer (from first packet arrival)
    thermalDataTransferDuration = millis();
  }
  else
  {
    Serial.println("✗ Invalid header!");
  }
}

void handleThermalEndPacket(uint8_t *buf)
{
  // Parse end packet
  ThermalEndPacket *endPacket = (ThermalEndPacket *)buf;

  uint16_t pktCount = endPacket->packetCount;
  expectedImageCrc = endPacket->imageCrc;

  Serial.println("\n🏁 END PACKET RECEIVED!");

  // If we're in retry mode, interpret packetCount as number of retry packets sent
  if (waitingForRetry)
  {
    Serial.print("Satellite resent ");
    Serial.print(pktCount);
    Serial.println(" retry packets");

    // Verify CRC of received image
    if (expectedLength > 0)
    {
      lastComputedImageCrc = crc16_ccitt(imgBuffer, expectedLength);
      crcVerified = (expectedImageCrc == lastComputedImageCrc);
      Serial.print("Image CRC16 expected 0x");
      Serial.print(expectedImageCrc, HEX);
      Serial.print(", computed 0x");
      Serial.print(lastComputedImageCrc, HEX);
      Serial.println(crcVerified ? " (match)" : " (MISMATCH)");
    }

    // Check if we now have all packets
    if (receivedPackets >= expectedPackets)
    {
      Serial.println("\n✅ All missing packets received!");
      waitingForRetry = false;
      downloadingThermalData = false;
      imageComplete = true;

      showReceptionSummary();
      autoMode = false;
    }
    else
    {
      // Still missing packets - will retry again after timeout
      Serial.print("⚠️ Still missing ");
      Serial.print(expectedPackets - receivedPackets);
      Serial.println(" packets - waiting for retry timeout");
    }
    return;
  }

  // Original transmission logic (first END packet received)
  Serial.print("Transmitter sent ");
  Serial.print(pktCount);
  Serial.println(" packets");

  // verify CRC of received image is what was expected
  if (expectedLength > 0)
  {
    lastComputedImageCrc = crc16_ccitt(imgBuffer, expectedLength);
    crcVerified = (expectedImageCrc == lastComputedImageCrc);
    Serial.print("Image CRC16 expected 0x");
    Serial.print(expectedImageCrc, HEX);
    Serial.print(", computed 0x");
    Serial.print(lastComputedImageCrc, HEX);
    Serial.println(crcVerified ? " (match)" : " (MISMATCH)");
  }

  // case where transmitter does not send all packets
  if (pktCount != expectedPackets)
  {
    Serial.print("⚠️ End packet reports ");
    Serial.print(pktCount);
    Serial.print(" packets but header expected ");
    Serial.println(expectedPackets);
  }

  // case where all packets were sent out, but not all were received
  if (receivedPackets < expectedPackets)
  {
    uint16_t missingPackets = expectedPackets - receivedPackets;
    Serial.print("⚠️ Only ");
    Serial.print(receivedPackets);
    Serial.print(" of ");
    Serial.print(expectedPackets);
    Serial.println(" packets were received");

    if (missingPackets <= RETRY_MISSING_PACKET_THRESHOLD)
    {
      Serial.print("Missing only ");
      Serial.print(missingPackets);
      Serial.print(" packets (<= ");
      Serial.print(RETRY_MISSING_PACKET_THRESHOLD);
      Serial.println(") - skipping automatic retry to allow manual inspection.");

      imageComplete = false;
      waitingForRetry = false;
      downloadingThermalData = false;
      autoMode = false;
      showReceptionSummary();
      printPrompt();
      return;
    }

    imageComplete = false;
    waitingForRetry = true;
    endPacketReceivedTime = millis();
    lastRetryRequestTime = millis();
    retryAttemptCount = 0; // Will be incremented when first retry is sent
    Serial.print("Waiting ");
    Serial.print(RETRY_GRACE_PERIOD_MS / 1000);
    Serial.println(" seconds for any in-flight packets before requesting retries...");
    return;
  }

  // case where all packets were received successfully
  downloadingThermalData = false; // exit downloading state
  imageComplete = true;
  showReceptionSummary();
  autoMode = false; // Exit auto mode only if complete
}

void handleThermalDataPacket(uint8_t *buf, uint8_t len)
{
  // Error checks
  if (len <= THERMAL_PACKET_OVERHEAD)
  {
    Serial.println("ERROR: data packet len less than thermal packet overhead?!");
    return; // Minimum packet size check
  }

  // Parse data packet using struct for packet index and data
  ThermalDataPacket *dataPacket = (ThermalDataPacket *)buf;
  uint8_t dataLen = len - THERMAL_PACKET_OVERHEAD;
  uint16_t packetNum = dataPacket->packetIndex;

  // Verify CRC
  uint16_t crcOffset = 2 + dataLen;
  uint16_t packetCrc;
  memcpy(&packetCrc, &buf[crcOffset], sizeof(uint16_t));

  uint16_t computedCrc = crc16_ccitt(dataPacket->data, dataLen);

  if (computedCrc != packetCrc)
  {
    // TODO: investigate why CRC mismatch is occurring
    Serial.println("CRC error detected, dropping packet: ");
    // print whole packet
    for (uint8_t i = 0; i < len; i++)
    {
      Serial.print(buf[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    crcErrorCount++;
    if (crcErrorCount <= 10)
    {
      Serial.print("CRC mismatch on packet ");
      Serial.print(packetNum);
      Serial.print(" (expected 0x");
      Serial.print(packetCrc, HEX);
      Serial.print(", computed 0x");
      Serial.print(computedCrc, HEX);
      Serial.println(")");
      if (crcErrorCount == 10)
      {
        Serial.println("Further CRC mismatch logs suppressed");
      }
    }
    return; // Discard corrupt packet
  }

  // packet index is within bounds
  if (packetNum >= 1200)
    return;

  // passed all checks, mark as downloading
  downloadingThermalData = true;
  // Process packet only if not already received (duplicate detection)
  if (!packetReceived[packetNum])
  {
    uint32_t bufferPos = (uint32_t)packetNum * PACKET_DATA_SIZE; // Calculate buffer position
    uint32_t bound = expectedLength > 0 ? expectedLength : MAX_IMG;
    // Buffer overflow protection
    if (bufferPos + dataLen <= bound)
    {
      memcpy(&imgBuffer[bufferPos], dataPacket->data, dataLen); // Copy data to buffer
      packetReceived[packetNum] = true;                         // Mark packet as received
      receivedPackets++;

      if (packetNum > maxPacketNum)
      {
        maxPacketNum = packetNum; // Track highest packet number
      }

      // Reset retry timeout when we receive new packets
      if (waitingForRetry)
      {
        lastRetryRequestTime = millis();
      }

      // Progress indication in auto mode - update every 25%
      if (autoMode)
      {
        static uint8_t lastReportedQuarter = 0;

        // Reset progress tracking at start of new reception
        if (receivedPackets == 1)
        {
          lastReportedQuarter = 0;
        }

        float progress = (float)receivedPackets / expectedPackets * 100;
        uint8_t currentQuarter = progress / 25;

        if (currentQuarter > lastReportedQuarter && currentQuarter <= 4)
        {
          lastReportedQuarter = currentQuarter;
          Serial.print(currentQuarter * 25);
          Serial.println("%");
        }
      }

      // Check if all packets now received during retry phase
      if (waitingForRetry && receivedPackets >= expectedPackets)
      {
        Serial.println("\n✅ All missing packets received!");
        waitingForRetry = false;
        downloadingThermalData = false;
        imageComplete = true;

        // Verify CRC
        lastComputedImageCrc = crc16_ccitt(imgBuffer, expectedLength);
        crcVerified = (expectedImageCrc == lastComputedImageCrc);

        showReceptionSummary();
        autoMode = false;
      }
    }
    else
      return;
  }
}

/**
 * Snapshot the RF23 RX FIFO directly over SPI and dump the contents to USB serial.
 *
 * Mirrors the satellite-side helper: forces the part idle, captures key status
 * registers (STATUS: current chip state, EZMAC: MAC event flags, INT1/INT2:
 * pending interrupt causes that clear on read), reads all 64 FIFO bytes via
 * `spiBurstRead`, prints a hex+ASCII table, then restores RX mode so normal
 * reception can resume. The FIFO dump lets you inspect raw bytes lingering in
 * hardware before the driver drains them, which is invaluable when debugging
 * wedged receptions or malformed packets.
 */
void dumpRf23PendingPacketsToSerial()
{
  Serial.println("\n=== RF23 RX FIFO RAW SNAPSHOT START ===");

  rf23.setModeIdle();
  delay(2);

  uint8_t status = rf23.statusRead();
  uint8_t ezmac = rf23.ezmacStatusRead();
  uint8_t irq1 = rf23.spiRead(RH_RF22_REG_03_INTERRUPT_STATUS1);
  uint8_t irq2 = rf23.spiRead(RH_RF22_REG_04_INTERRUPT_STATUS2);

  // Diagnostic quick reference (see RH_RF22.h / Si4432 datasheet for bit masks):
  //   STATUS (0x02) -> real-time chip flags (`RH_RF22_CHIP_READY`, `RH_RF22_FFEM`,
  //     `RH_RF22_RXAFULL`, ...). Handy for confirming FIFO level and lock state.
  //   EZMAC (0x31) -> MAC engine events like `RH_RF22_PKSENT`, `RH_RF22_PKVALID`, and
  //     CRC indications that tell you whether the packet engine completed.
  //   INT1 (0x03) -> primary interrupt causes (`RH_RF22_IPKVALID`, `RH_RF22_IPKSENT`,
  //     `RH_RF22_ICRCERROR`, etc.). A set bit means the event fired; reading clears it.
  //   INT2 (0x04) -> secondary causes (`RH_RF22_IFFERR`, `RH_RF22_ISWDET`, `RH_RF22_IEXT`).
  // Combining these values lets you pinpoint whether hardware saw a packet, rejected it,
  // or experienced FIFO/sync faults before our firmware reacted.

  auto formatHex = [](uint8_t value)
  {
    String hex = String(value, HEX);
    hex.toUpperCase();
    if (hex.length() < 2)
      hex = "0" + hex;
    return hex;
  };

  Serial.print("STATUS: 0x");
  Serial.println(formatHex(status));
  Serial.print("EZMAC : 0x");
  Serial.println(formatHex(ezmac));
  Serial.print("INT1  : 0x");
  Serial.println(formatHex(irq1));
  Serial.print("INT2  : 0x");
  Serial.println(formatHex(irq2));

  uint8_t fifoSnapshot[RH_RF22_FIFO_SIZE];
  memset(fifoSnapshot, 0, sizeof(fifoSnapshot));
  rf23.spiBurstRead(RH_RF22_REG_7F_FIFO_ACCESS, fifoSnapshot, RH_RF22_FIFO_SIZE);

  Serial.println("FIFO contents (64 bytes, oldest first):");
  printRf23HexLines(fifoSnapshot, RH_RF22_FIFO_SIZE);

  rf23.setModeRx();
  Serial.println("=== RF23 RX FIFO RAW SNAPSHOT END ===\n");
}

/**
 * Render raw bytes in a hex+ASCII table (`000: AA BB ... | ..`) to aid analysis.
 * Designed for short buffers such as the RF23 FIFO snapshot.
 */
void printRf23HexLines(const uint8_t *data, uint8_t length)
{
  const uint8_t BYTES_PER_LINE = 16;
  char lineBuf[4 + 2 + (BYTES_PER_LINE * 3) + 2 + BYTES_PER_LINE + 1];

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

    Serial.println(lineBuf);
  }
}

bool handleSerialMessage(uint8_t *buf, uint8_t len, String *messageOut)
{
  // Serial.println("Inside satellite serial message");
  // newline to go pass the >GS prompt
  if (len < 2)
    return false; // Minimum packet size check

  // Parse serial message
  SerialMessagePacket *serialPacket = (SerialMessagePacket *)buf;

  bool hasMore = (serialPacket->length & SERIAL_CONTINUATION_FLAG) != 0;
  uint8_t msgLen = serialPacket->length & 0x7F; // Lower 7 bits carry the actual length

  if (msgLen == 0 || len < msgLen + 2)
    return false; // Invalid message length or packet too short

  // if (msgLen == 0 || len < msgLen + 3)    return false; // Invalid message length or packet too short (3-byte overhead)

  // Extract message data and print to serial
  String message = "";
  message.reserve(msgLen);
  for (uint8_t i = 0; i < msgLen; i++)
  {
    message += (char)serialPacket->data[i];
  }

  if (messageOut != nullptr)
  {
    *messageOut = message;
  }

  Serial.print(message);

  if (!hasMore)
  {
    if (message.length() == 0)
    {
      Serial.println();
    }
    else
    {
      char lastChar = message.charAt(message.length() - 1);
      if (lastChar != '\n' && lastChar != '\r')
      {
        Serial.println();
      }
    }
  }

  return true;
}

// Standard CRC-16/CCITT-FALSE helper used for thermal payload validation
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

void showReceptionSummary()
{
  Serial.println("\n=== RECEPTION SUMMARY ===");
  Serial.print("Received ");
  Serial.print(receivedPackets);
  Serial.print(" of ");
  Serial.print(expectedPackets);
  Serial.print(" packets (");
  float pct = (expectedPackets > 0) ? ((float)receivedPackets / expectedPackets * 100.0f) : 0.0f;
  Serial.print(pct, 1);
  Serial.println("%)");

  if (crcErrorCount > 0)
  {
    Serial.print("Packets discarded (CRC): ");
    Serial.println(crcErrorCount);
  }

  if (expectedImageCrc != 0 || lastComputedImageCrc != 0)
  {
    Serial.print("Image CRC16 expected 0x");
    Serial.print(expectedImageCrc, HEX);
    Serial.print(", computed 0x");
    Serial.print(lastComputedImageCrc, HEX);
    Serial.println(crcVerified ? " (match)" : " (MISMATCH)");
  }

  if (expectedPackets > 0 && receivedPackets < expectedPackets)
  {
    Serial.print("Missing packets: ");
    Serial.println(expectedPackets - receivedPackets);

    for (int index = 0; index < expectedPackets; index++)
    {
      if (!packetReceived[index])
      {
        Serial.println(index);
      }
    }
  }

  // Quality assessment based on reception rate
  if (receivedPackets == expectedPackets)
  {
    Serial.println("\n✅ PERFECT RECEPTION!");
  }
  else if (receivedPackets > expectedPackets * 0.95)
  {
    Serial.println("\n✅ Excellent reception!");
  }
  else if (receivedPackets > expectedPackets * 0.80)
  {
    Serial.println("\n⚠️ Good reception");
  }
  else
  {
    Serial.println("\n❌ Poor reception");
  }

  // Indicate if thermal image is ready for export
  if (expectedLength == 38400)
  {
    Serial.println("\nThermal image ready! Type 'export' to export as CSV");
  }

  // Calculate both timing metrics
  thermalDataTransferDuration = millis() - thermalDataTransferDuration;
  thermalDataDownloadDuration = millis() - thermalDataDownloadDuration;
  unsigned long satelliteResponseTime = thermalDataDownloadDuration - thermalDataTransferDuration;

  Serial.println("\n**** Thermal Data Download Statistics ****");
  Serial.print("Total Duration (request → completion): ");
  helperTime(thermalDataDownloadDuration);
  Serial.print("Data Transfer Time (first → last packet): ");
  helperTime(thermalDataTransferDuration);
  Serial.print("Satellite Response Time (request → first packet): ");
  helperTime(satelliteResponseTime);

  // Serial.print("Payload Expected Size (bytes): ");
  // Serial.println(expectedLength);
  Serial.print("Last RSSI: ");
  Serial.println(lastRSSI);

  // Reset timing variables
  thermalDataDownloadDuration = 0;
  thermalDataTransferDuration = 0;
}

void exportThermalData()
{
  if (!headerReceived || expectedLength != 38400)
  {
    Serial.println("No complete thermal image to export");
    return;
  }

  if (!crcVerified)
  {
    Serial.println("⚠️ Warning: image CRC mismatch – export may contain corrupt data");
  }

  Serial.println("\n--- EXPORTING THERMAL DATA ---");
  Serial.println("Copying data below to a 'thermal_image.csv'");
  Serial.println(THERMAL_CSV_START);

  // Export thermal data as CSV (120x160 pixel grid)
  for (int row = 0; row < 120; row++)
  {
    for (int col = 0; col < 160; col++)
    {
      uint32_t idx = (row * 160 + col) * 2; // 2 bytes per pixel
      if (idx < MAX_IMG - 1)
      {
        // Convert raw 16-bit value to temperature in Celsius
        uint16_t pixel = imgBuffer[idx] | (imgBuffer[idx + 1] << 8);
        if (pixel >= 27315 && pixel <= 37315)
        {                                        // Valid temperature range (0-100°C)
          float tempC = (pixel - 27315) / 100.0; // Convert from Kelvin*100 to Celsius
          Serial.print(tempC, 2);
        }
        else
        {
          Serial.print("NaN"); // Invalid temperature value
        }
      }
      else
      {
        Serial.print("NaN"); // Buffer overflow protection
      }
      if (col < 159)
        Serial.print(","); // CSV separator
    }
    Serial.println(); // New line for each row
  }

  Serial.println(THERMAL_CSV_END);
  // Serial.println("\nVisualize with Python:");
  // Serial.println(" import numpy as np");
  // Serial.println(" import matplotlib.pyplot as plt");
  // Serial.println(" data = np.loadtxt('thermal_image.csv', delimiter=',')");
  // Serial.println(" plt.imshow(data, cmap='hot')");
  // Serial.println(" plt.colorbar(label='Temperature (°C)')");
  // Serial.println(" plt.show()");
}

void forwardToSatellite(char cmd)
{
  if (cmd == 'u')
  {
    Serial.println(THERMAL_CAPTURE_TIMESTAMP_FLAG);
    thermalDataDownloadDuration = millis();
    thermalDataTransferDuration = 0; // Reset transfer timer
  }
  else if (cmd == 'r')
  {
    Serial.println("\n--- REQUESTING THERMAL DATA DOWNLINK ---");
    thermalDataDownloadDuration = millis(); // Start measuring user latency from request
    thermalDataTransferDuration = 0;        // Reset transfer timer (will be set when header arrives)
  }
  else
  {
    return; // Ignore unknown commands
  }

  Serial.println("Forwarding command to satellite...");
  digitalWrite(LED_PIN, HIGH); // Turn on LED during transmission
  setRadioAmpTransmit();
  // Send the command
  if (!rf23.send((uint8_t *)&cmd, 1))
  { // a command should be a single byte
    Serial.print("Failed to queue command for transmission: ");
    Serial.println(cmd);
  }
  else
  {
    if (!rf23.waitPacketSent(RADIO_WAIT_PACKET_SENT_MS))
    {
      Serial.print("Failed to send command: ");
      Serial.println(cmd);
    }
    else
    {
      Serial.print("Command sent successfully: ");
      Serial.println(cmd);
    }
  }

  digitalWrite(LED_PIN, LOW); // Turn off LED
  setRadioAmpReceive();
  return;
}

// Command implementations
void cmdHelp(const char *args)
{
  Serial.println("\nAvailable Commands:");
  Serial.println("==================");

  for (int i = 0; i < numCommands; i++)
  {
    Serial.print("  ");
    Serial.print(commands[i].name);
    Serial.print(" - ");
    Serial.println(commands[i].description);
  }

  Serial.println("\nUsage: command [arguments]");
  Serial.println("Example: led on, analog 0, digital 13 1");
  Serial.println("\nInterrupt: Press 'Q' during command execution to interrupt and return to prompt");
}

void cmdVersion(const char *args)
{
  Serial.println("\n=== GS Version Information ===");
  Serial.print("Software: ");
  Serial.println(BUILD_INFO);
  Serial.print("GS Version: ");
  Serial.print(VERSION_MAJOR);
  Serial.print(".");
  Serial.print(VERSION_MINOR);
  Serial.print(".");
  Serial.println(VERSION_PATCH);
  Serial.print("Build Date: ");
  Serial.println(VERSION_BUILD);
  Serial.print("GS Arduino Version: ");
  Serial.println(ARDUINO);
  Serial.print("Board: Teensy 4.1");
  Serial.println(F_CPU);
  Serial.println("========================");
}

void cmdStatus(const char *args)
{
  Serial.println("\n=== GS System Status ===");
  cmdTime(args);

  Serial.print("GS LED Status: ");
  Serial.println(digitalRead(LED_PIN) ? "ON" : "OFF");

  Serial.print("Commands in history: ");
  Serial.println(historyIndex);
}

void cmdPing(const char *args)
{
  Serial.println("\nPinging satellite...");

  uint8_t ping = 'g';
  if (!sendBytesToSatellite(&ping, 1, 500))
  {
    Serial.println("Failed to send ping to satellite");
    return;
  }

  // Return to RX so the main loop can process the response via handleCommand.
  rf23.setModeRx();

  Serial.println("Ping sent. Waiting for satellite response...");
}

void cmdEcho(const char *args)
{
  if (strlen(args) > 0)
  {
    Serial.print("Echo: ");
    Serial.println(args);
  }
  else
  {
    Serial.println("Usage: echo <message>");
  }
}

void cmdLed(const char *args)
{
  String argStr = String(args);
  argStr.toLowerCase();

  if (argStr == "on")
  {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("LED turned ON");
  }
  else if (argStr == "off")
  {
    digitalWrite(LED_PIN, LOW);
    Serial.println("LED turned OFF");
  }
  else if (argStr == "toggle")
  {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    Serial.print("LED toggled to ");
    Serial.println(digitalRead(LED_PIN) ? "ON" : "OFF");
  }
  else
  {
    Serial.println("Usage: led [on|off|toggle]");
  }
}

void cmdAnalog(const char *args)
{
  if (strlen(args) == 0)
  {
    Serial.println("Usage: analog <pin>");
    return;
  }

  int pin = atoi(args);
  if (pin < 0 || pin > 15)
  { // Adjust based on your board
    Serial.println("Error: Invalid pin number (0-15)");
    return;
  }

  int value = analogRead(pin);
  float voltage = (value * 5.0) / 1024.0; // Assuming 5V reference

  Serial.print("Analog pin ");
  Serial.print(pin);
  Serial.print(": ");
  Serial.print(value);
  Serial.print(" (");
  Serial.print(voltage, 2);
  Serial.println("V)");
}

void cmdDigital(const char *args)
{
  String argStr = String(args);
  int spaceIndex = argStr.indexOf(' ');

  if (spaceIndex == -1)
  {
    Serial.println("Usage: digital <pin> [value]");
    return;
  }

  int pin = argStr.substring(0, spaceIndex).toInt();
  String valueStr = argStr.substring(spaceIndex + 1);

  if (pin < 0 || pin > 13)
  { // Adjust based on your board
    Serial.println("Error: Invalid pin number (0-13)");
    return;
  }

  if (valueStr.length() == 0)
  {
    // Read mode
    pinMode(pin, INPUT);
    int value = digitalRead(pin);
    Serial.print("Digital pin ");
    Serial.print(pin);
    Serial.print(": ");
    Serial.println(value ? "HIGH" : "LOW");
  }
  else
  {
    // Write mode
    pinMode(pin, OUTPUT);
    int value = valueStr.toInt();
    digitalWrite(pin, value);
    Serial.print("Digital pin ");
    Serial.print(pin);
    Serial.print(" set to ");
    Serial.println(value ? "HIGH" : "LOW");
  }
}

void helperTime(unsigned long durationMillis)
{
  unsigned long seconds = durationMillis / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;

  seconds = seconds % 60;
  minutes = minutes % 60;

  if (hours > 0)
  {
    Serial.print(hours);
    Serial.print("h ");
  }
  if (minutes > 0)
  {
    Serial.print(minutes);
    Serial.print("m ");
  }

  Serial.print(seconds);
  Serial.println("s");
}

void cmdTime(const char *args)
{
  Serial.print("GS Uptime: ");
  helperTime(millis());
}

void cmdGSReset(const char *args)
{
  Serial.println("Resetting GS system... manually reconnect to serial after 20 seconds.");
  Serial.println("Press 'Q' to cancel reset within 1 second...");

  // Check for interrupt during the 1-second delay
  unsigned long startTime = millis();
  while (millis() - startTime < 1000)
  {
    if (isInterruptRequested())
    {
      Serial.println("\nReset cancelled by user!");
      return;
    }
    delay(50);
  }

  // Software reset for Teensy 4.1 (ARM Cortex-M7)
  SCB_AIRCR = 0x05FA0004;
}

void cmdSatelliteReset(const char *args)
{
  Serial.println("Resetting Satellite system... reboot should take 20 seconds.");
  Serial.println("Press 'Q' to cancel reset within 1 second...");

  // Check for interrupt during the 1-second delay
  unsigned long startTime = millis();
  while (millis() - startTime < 1000)
  {
    if (isInterruptRequested())
    {
      Serial.println("\nReset cancelled by user!");
      return;
    }
    delay(50);
  }

  forwardToSatellite('~');
}

void cmdHistory(const char *args)
{
  Serial.println("\nGS Command History:");
  Serial.println("================");

  for (int i = 0; i < historyIndex && i < 10; i++)
  {
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.println(commandHistory[i]);
  }

  if (historyIndex == 0)
  {
    Serial.println("No commands in history");
  }
}

void cmdClear(const char *args)
{
  // Clear screen by printing newlines
  for (int i = 0; i < 50; i++)
  {
    Serial.println();
  }

  // Re-print welcome message with version
  Serial.println("================================================");
  Serial.println("    Artemis Ground Station Command Interpreter");
  Serial.println("================================================");
  Serial.print("GS Version: ");
  Serial.println(VERSION_STRING);
  Serial.print("Build Date: ");
  Serial.println(VERSION_BUILD);
  Serial.println("================================================\n");
}

void cmdRPIControl(const char *args)
{
  String argStr = String(args);
  argStr.trim();
  argStr.toLowerCase();

  if (argStr == "on" || argStr == "off" || argStr == "status")
  {
    memset(radioTxBuffer, 0, sizeof(radioTxBuffer));
    radioTxBuffer[0] = 'p';
    if (argStr == "on")
      radioTxBuffer[1] = '1';
    else if (argStr == "off")
      radioTxBuffer[1] = '0';
    else // status
      radioTxBuffer[1] = 's';

    Serial.print("Forwarding RPI command to satellite: rpi ");
    Serial.println(argStr);

    if (sendBytesToSatellite(radioTxBuffer, 2))
      Serial.println("Command forwarded");
    else
      Serial.println("Failed to forward command");
    return;
  }

  // fallback usage
  Serial.println("Usage: rpi [on|off|status]");
}

void cmdSensor(const char *args)
{
  String argStr = String(args);
  argStr.trim();
  argStr.toLowerCase();

  if (argStr.length() == 0)
  {
    Serial.println("Usage: sensor <gps|imu|both|gps_init|imu_init>");
    return;
  }

  char subcommand = 0;
  const char *action = nullptr;

  if (argStr == "gps" || argStr == "g")
  {
    subcommand = 'g';
    action = "GPS data request";
  }
  else if (argStr == "imu" || argStr == "i")
  {
    subcommand = 'i';
    action = "IMU data request";
  }
  else if (argStr == "both" || argStr == "b")
  {
    subcommand = 'b';
    action = "combined GPS/IMU data request";
  }
  else if (argStr == "gps_init" || argStr == "init_gps" || argStr == "reinit_gps" || argStr == "ginit")
  {
    subcommand = 'G';
    action = "GPS reinitialization";
  }
  else if (argStr == "imu_init" || argStr == "init_imu" || argStr == "reinit_imu" || argStr == "iinit")
  {
    subcommand = 'I';
    action = "IMU reinitialization";
  }
  else
  {
    Serial.println("Usage: sensor <gps|imu|both|gps_init|imu_init>");
    return;
  }

  radioTxBuffer[0] = 's';
  radioTxBuffer[1] = (uint8_t)subcommand;

  Serial.print("Forwarding sensor command: ");
  Serial.println(action);

  if (sendBytesToSatellite(radioTxBuffer, 2))
  {
    Serial.println("Sensor command forwarded");
    rf23.setModeRx();
  }
  else
  {
    Serial.println("Failed to forward sensor command");
  }
}

void cmdTestInterrupt(const char *args)
{
  if (strlen(args) == 0)
  {
    Serial.println("Usage: testint <seconds>");
    Serial.println("Example: testint 10 (runs for 10 seconds, press Q to interrupt)");
    return;
  }

  int duration = atoi(args);
  if (duration <= 0 || duration > 60)
  {
    Serial.println("Error: Duration must be between 1 and 60 seconds");
    return;
  }

  Serial.print("Starting test for ");
  Serial.print(duration);
  Serial.println(" seconds...");
  Serial.println("Press 'Q' at any time to interrupt and return to prompt");

  unsigned long startTime = millis();
  unsigned long endTime = startTime + (duration * 1000);

  while (millis() < endTime)
  {
    // Check for interrupt every 100ms
    if (isInterruptRequested())
    {
      Serial.println("\nTest interrupted by user!");
      return;
    }

    // Show progress every second
    unsigned long elapsed = (millis() - startTime) / 1000;
    static unsigned long lastPrint = 0;
    if (elapsed != lastPrint)
    {
      Serial.print("Test running... ");
      Serial.print(elapsed);
      Serial.print("/");
      Serial.print(duration);
      Serial.println(" seconds");
      lastPrint = elapsed;
    }

    delay(100); // Small delay to allow interrupt checking
  }

  Serial.println("Test completed successfully!");
}

void cmdTestLoopPing(const char *args)
{
  unsigned long intervalMs = 2000; // Default loop delay between pings
  const unsigned long minInterval = 250;
  const unsigned long responseWaitMs = 1000;

  if (strlen(args) > 0)
  {
    long parsed = atol(args);
    if (parsed >= (long)minInterval)
    {
      intervalMs = (unsigned long)parsed;
    }
    else
    {
      Serial.print("Ignoring interval below ");
      Serial.print(minInterval);
      Serial.println(" ms; using default 2000 ms.");
    }
  }

  Serial.println("\nStarting ping test loop. Press 'Q' to interrupt.");
  Serial.print("Loop interval: ");
  Serial.print(intervalMs);
  Serial.println(" ms");

  resetInterrupt();

  unsigned long iteration = 1;
  while (true)
  {
    if (isInterruptRequested())
    {
      Serial.println("\nPing loop interrupted by user.");
      resetInterrupt();
      return;
    }

    Serial.print("\n[Ping ");
    Serial.print(iteration);
    Serial.println("] Sending ping to satellite...");

    uint8_t ping = 'g';
    if (!sendBytesToSatellite(&ping, 1, 500))
    {
      Serial.println("Failed to send ping to satellite");
    }
    else
    {
      rf23.setModeRx();
    }

    unsigned long responseStart = millis();
    unsigned long initialPackets = packetsReceived;
    bool sawNewPacket = false;

    while (millis() - responseStart < responseWaitMs)
    {
      if (rf23.available())
      {
        handlePacket();
        if (packetsReceived > initialPackets)
        {
          sawNewPacket = true;
        }
      }

      if (isInterruptRequested())
      {
        Serial.println("\nPing loop interrupted by user.");
        resetInterrupt();
        return;
      }

      delay(10);
    }

    Serial.print("Last RSSI: ");
    Serial.print(lastRSSI);
    Serial.println(" dBm");
    if (!sawNewPacket)
    {
      Serial.println("(No new response detected during this interval; reporting most recent RSSI)");
    }

    unsigned long intervalStart = millis();
    while (millis() - intervalStart < intervalMs)
    {
      if (rf23.available())
      {
        handlePacket();
      }

      if (isInterruptRequested())
      {
        Serial.println("\nPing loop interrupted by user.");
        resetInterrupt();
        return;
      }

      delay(10);
    }

    iteration++;
  }
}

/**
 * Converts the raw ADC value from RFM23BP/RF22 to Celsius.
 * Based on Section 8.4 of the RFM23BP Datasheet.
 * Assumes default TSRANGE: -64C to +64C.
 *
 * @param adc_raw The 0-255 value returned by radio.adcRead()
 * @param cal_offset (Optional) User calibration correction (default 0.0)
 * @return Temperature in Degrees Celsius
 */
float convertRadioTemp(uint8_t adc_raw, float cal_offset = 0.0)
{
  // 1. Define the slope based on the range (-64 to +64 = 128 degree span)
  // Span (128) / ADC_Resolution (256) = 0.5 degrees per bit
  const float slope = 0.5;

  // 2. Define the base offset (Value at ADC = 0)
  const float range_floor = -64.0;

  // 3. Calculate
  float temp_c = ((float)adc_raw * slope) + range_floor;

  // 4. Apply user calibration (if any)
  return temp_c + cal_offset;
}

// Radio command implementations
void cmdRadio(const char *args)
{
  String argStr = String(args);
  argStr.toLowerCase();

  if (argStr == "init")
  {
    initRadio();
  }
  else if (argStr == "status")
  {
    Serial.println("\n=== RADIO STATUS ===");

    // Basic configuration
    Serial.println("Frequency: 433.3MHz");

    // Signal strength
    Serial.println("Last RSSI: " + String(rf23.lastRssi()) + " dBm");

    if (rf23.lastRssi() > -70)
    {
      Serial.println(" (EXCELLENT)");
    }
    else if (rf23.lastRssi() > -85)
    {
      Serial.println(" (GOOD)");
    }
    else if (rf23.lastRssi() > -95)
    {
      Serial.println(" (FAIR)");
    }
    else
    {
      Serial.println(" (POOR)");
    }

    // Radio mode
    Serial.print("Radio mode: ");
    uint8_t mode = rf23.mode();
    switch (mode)
    {
    case RHGenericDriver::RHModeInitialising:
      Serial.println("Initializing");
      break;
    case RHGenericDriver::RHModeSleep:
      Serial.println("Sleep");
      break;
    case RHGenericDriver::RHModeIdle:
      Serial.println("Idle");
      break;
    case RHGenericDriver::RHModeTx:
      Serial.println("Transmitting");
      break;
    case RHGenericDriver::RHModeRx:
      Serial.println("Receiving");
      break;
    default:
      Serial.println("Unknown");
      break;
    }

    // TX Power
    Serial.println("TX Power: 30dBM");

    // Modem configuration details
    Serial.print("Modem config: ");
    // You'll need to track which config you set, or just print what you know
    Serial.println("GFSK_Rb38_4Fd19_6 38kbps");

    // Temperature (if supported by RF22/23)
    Serial.print("Radio temp: ");
    Serial.print(convertRadioTemp(rf23.temperatureRead()));
    Serial.println(" °C");

    // Header settings
    Serial.print("Header TO: 0x");
    Serial.println(rf23.headerTo(), HEX);
    Serial.print("Header FROM: 0x");
    Serial.println(rf23.headerFrom(), HEX);
    Serial.print("Header ID: 0x");
    Serial.println(rf23.headerId(), HEX);
    Serial.print("Header FLAGS: 0x");
    Serial.println(rf23.headerFlags(), HEX);

    // Statistics
    Serial.print("RX Good: ");
    Serial.println(rf23.rxGood());
    Serial.print("RX Bad: ");
    Serial.println(rf23.rxBad());
    Serial.print("TX Good: ");
    Serial.println(rf23.txGood());

    // Your custom stats
    Serial.print("Packets received: ");
    Serial.println(packetsReceived);
    Serial.print("Auto mode: ");
    Serial.println(autoMode ? "ON" : "OFF");
    Serial.print("Header received: ");
    Serial.println(headerReceived ? "YES" : "NO");
    Serial.print("Image complete: ");
    Serial.println(imageComplete ? "YES" : "NO");

    if (headerReceived)
    {
      Serial.print("Expected thermal packets: ");
      Serial.println(expectedPackets);
      Serial.print("Received thermal packets: ");
      Serial.println(receivedPackets);
    }
  }
  else if (argStr == "tx")
  {
    rf23.setModeTx();
    Serial.println("Radio set to transmit mode");
  }
  else if (argStr == "rx")
  {
    rf23.setModeRx();
    Serial.println("Radio set to receive mode");
  }
  else if (argStr == "dump" || argStr == "debug")
  {
    dumpRf23PendingPacketsToSerial();
  }
  // else if (argStr == "reset")
  // {
  // Never run rf23.reset on the artemis kit - it seems to break the drivers? functional testing breaks when u call this, leaving this in for future devs.
  //   rf23.reset();
  //   Serial.println("Radio softeware reset. Try to run 'radio init' after 3 seconds to re-initialize.");
  // }
  else
  {
    Serial.println("Usage: radio [init|status|tx|rx|dump]");
  }
}

void cmdExport(const char *args)
{
  exportThermalData();
}

void cmdCapture(const char *args)
{
  forwardToSatellite('u');
}

void cmdRequest(const char *args)
{
  forwardToSatellite('r');
}

void cmdRadioStatus(const char *args)
{
  showReceptionSummary();
}

void setup()
{

  // Initialize serial communication
  Serial.begin(115200);

  // Wait for serial port to connect (optional for some boards)
  while (!Serial)
  {
    delay(10);
  }

  // Mark serial as connected and clear any existing buffer
  serialConnected = true;
  inputBuffer = "";

  // Initialize built-in LED to off.
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Configure the Raspberry Pi control pin
  pinMode(RASPBERRY_PI_GPIO_PIN, OUTPUT);
  // Set Raspberry Pi to off state initially
  digitalWrite(RASPBERRY_PI_GPIO_PIN, LOW);

  // Initialize radio module
  initRadio();

  // Initialize reception state
  clearReception();

  // Clear command history
  clearHistory();

  // Welcome message with version info
  Serial.println("\n================================================");
  Serial.println("    Artemis Ground Station Command Interpreter");
  Serial.println("================================================");
  Serial.print("GS Version: ");
  Serial.println(VERSION_STRING);
  Serial.print("Build Date: ");
  Serial.println(VERSION_BUILD);
  Serial.print("Build Info: ");
  Serial.println(BUILD_INFO);
  Serial.println("================================================");
  Serial.println("Type 'help' for available commands");
  Serial.println("Type 'version' for detailed version info");
  Serial.println("Type 'clear' to clear the screen");
  Serial.println("Type 'auto' to start thermal image reception");
  Serial.println("Type 'Q' during command execution to interrupt");
  Serial.println("================================================\n");

  printPrompt();
}

/**
 * Requests retransmission of missing packets from satellite using a bitmap
 */
bool requestMissingPackets()
{
  if (expectedPackets == 0 || receivedPackets >= expectedPackets)
  {
    Serial.println("No missing packets to request.");
    return false;
  }

  uint16_t missingPacketCount = expectedPackets - receivedPackets;
  Serial.print("Requesting retransmission of ");
  Serial.print(missingPacketCount);
  Serial.println(" missing packets...");

  bool anyQueued = false;

  // Find the index of the first missing packet
  int firstMissingIndex = -1;
  for (int i = 0; i < expectedPackets; i++)
  {
    if (!packetReceived[i])
    {
      firstMissingIndex = i;
      break;
    }
  }
  // in theory, would never reach here since we check for missing packets before calling this function but just in case
  if (firstMissingIndex == -1)
    return false; // No missing packets

  /* Send up to 3 requests at a time, each covering a chunk of 360 packets */
  for (int retryPacketNum = 0; retryPacketNum < 3; retryPacketNum++)
  {
    uint16_t offset = firstMissingIndex + (retryPacketNum * 360);
    if (offset >= expectedPackets)
      break;

    /* Build bitmap for this chunk */
    uint8_t bitmap[45]; // 45 bytes * 8 bits = 360 max packets per request
    memset(bitmap, 0, sizeof(bitmap));
    uint16_t packetCount = 0;

    for (int i = 0; i < 360; i++)
    {
      uint16_t packetIndex = offset + i;
      if (packetIndex >= expectedPackets)
        break; // No more packets to process

      // check PacketReceived to build bitmap
      if (!packetReceived[packetIndex])
      {
        uint16_t byteIndex = i / 8;
        uint8_t bitIndex = i % 8;
        bitmap[byteIndex] |= (1 << bitIndex);
        packetCount++;
      }
    }
    // if bitmap has no missing packets, skip sending
    if (packetCount == 0)
      continue;

    /* Construct retry request packet */
    RetryRequestPacket retryPacket;
    retryPacket.type = RETRY_REQUEST_TYPE;
    retryPacket.offset = offset;
    retryPacket.packetCount = packetCount;
    memcpy(retryPacket.bitmap, bitmap, sizeof(bitmap));
    setRadioAmpTransmit();
    /* Send retry request packet */
    if (!rf23.send((uint8_t *)&retryPacket, sizeof(retryPacket)))
    {
      Serial.println("Failed to queue retry packet for transmission");
    }
    else
    {
      if (!rf23.waitPacketSent(RADIO_WAIT_PACKET_SENT_MS))
      {
        Serial.println("Failed to send retry packet");
      }
      else
      {
        Serial.println("Retry packet sent successfully");
        anyQueued = true;
      }
    }
    setRadioAmpReceive();
  }
  if (!anyQueued)
  {
    Serial.println("No retry packets were sent successfully.");
  }
  return anyQueued;
}

void loop()
{
  // Check for serial connection state changes
  bool currentlyConnected = Serial;

  // Handle serial reconnection
  if (!serialConnected && currentlyConnected)
  {
    Serial.println("\nSerial connection restored.");
    printPrompt();
  }

  serialConnected = currentlyConnected;

  // Check for interrupt character (Q) at any time
  // checkForInterrupt();

  // Check for incoming radio packets (always listen for serial messages)
  if (rf23.available())
  {
    handlePacket();
    if (!autoMode)
    {
      // dont spam the serial line when reading data packets.
      printPrompt();
    }
  }

  // Check if we need to retry missing packet requests
  if (waitingForRetry && !imageComplete)
  {
    unsigned long currentTime = millis();

    // For first retry attempt, wait grace period after end packet
    // For subsequent retries, use the standard retry timeout
    unsigned long waitTime = (retryAttemptCount == 0) ? RETRY_GRACE_PERIOD_MS : RETRY_TIMEOUT_MS;
    unsigned long timeReference = (retryAttemptCount == 0) ? endPacketReceivedTime : lastRetryRequestTime;

    // If timeout expired and still missing packets
    if (currentTime - timeReference >= waitTime)
    {
      if (receivedPackets < expectedPackets && retryAttemptCount < MAX_RETRY_ATTEMPTS)
      {
        Serial.print("\n⏱️ Retry timeout - attempting retry ");
        Serial.print(retryAttemptCount + 1);
        Serial.print(" of ");
        Serial.println(MAX_RETRY_ATTEMPTS);

        bool retryQueued = requestMissingPackets();
        lastRetryRequestTime = currentTime;
        retryAttemptCount++;

        if (!retryQueued)
        {
          Serial.println("Retry request failed to send - exiting retry loop so commands remain responsive.");
          waitingForRetry = false;
          downloadingThermalData = false;
          autoMode = false;
          showReceptionSummary();
          printPrompt();
        }
      }
      else if (retryAttemptCount >= MAX_RETRY_ATTEMPTS)
      {
        Serial.println("\n❌ Max retry attempts reached - giving up");
        waitingForRetry = false;
        downloadingThermalData = false;
        autoMode = false;
        showReceptionSummary();
        printPrompt();
      }
    }
  }

  // Read entire line when available
  if (Serial.available())
  {
    String input = Serial.readStringUntil('\n');
    input.trim(); // Remove whitespace and newlines

    if (input.length() > 0)
    {
      // Reset interrupt flag before processing new command
      // resetInterrupt();
      parseCommand(input);
      // addToHistory(input);
      printPrompt();
    }
  }
}

// ============================================================================
// LIVESTREAM MODE FUNCTIONS
// ============================================================================

/**
 * Reset stream frame buffer for a new frame
 */
void resetStreamFrame()
{
  memset(streamFrameBuffer, 0, STREAM_FRAME_SIZE);
  memset(streamPacketsReceived, 0, STREAM_PACKETS_PER_FRAME);
  streamPacketCount = 0;
}

/**
 * Handle stream frame header packet from satellite
 * Indicates a new frame is starting
 */
void handleStreamFrameHeader(uint8_t *buf, uint8_t len)
{
  StreamFrameHeaderPacket *header = (StreamFrameHeaderPacket *)buf;

  // If we have a partial frame from previous seq, output it anyway
  if (streamPacketCount > 0 && header->frameSeq != currentStreamFrameSeq)
  {
    outputStreamFrame();
  }

  // Start new frame
  currentStreamFrameSeq = header->frameSeq;
  resetStreamFrame();
}

/**
 * Handle stream frame data packet from satellite
 * Assembles frame data from multiple packets
 */
void handleStreamFrameData(uint8_t *buf, uint8_t len)
{
  StreamDataPacket *pkt = (StreamDataPacket *)buf;

  // Verify this packet belongs to current frame
  if (pkt->frameSeq != currentStreamFrameSeq)
  {
    // Different frame - might have missed header
    // Output any existing frame data first
    if (streamPacketCount > 0)
    {
      outputStreamFrame();
    }
    currentStreamFrameSeq = pkt->frameSeq;
    resetStreamFrame();
  }

  // Validate packet index
  if (pkt->packetIndex >= STREAM_PACKETS_PER_FRAME)
  {
    return; // Invalid index
  }

  // Check if already received
  if (streamPacketsReceived[pkt->packetIndex])
  {
    return; // Duplicate
  }

  // Calculate data size (packet len - 3 byte header)
  uint8_t dataLen = len - 3;
  if (dataLen > PACKET_DATA_SIZE)
  {
    dataLen = PACKET_DATA_SIZE;
  }

  // Copy data to buffer
  uint16_t bufferOffset = (uint16_t)pkt->packetIndex * PACKET_DATA_SIZE;
  if (bufferOffset + dataLen <= STREAM_FRAME_SIZE)
  {
    memcpy(&streamFrameBuffer[bufferOffset], pkt->data, dataLen);
    streamPacketsReceived[pkt->packetIndex] = 1;
    streamPacketCount++;
  }

  // Check if frame is complete (or mostly complete)
  // Output when we have received enough packets (best-effort)
  if (streamPacketCount >= STREAM_PACKETS_PER_FRAME - 5)
  {
    outputStreamFrame();
  }
}

/**
 * Output assembled stream frame to serial for Python CLI to capture
 * Format: Binary with magic header and checksum for validation
 *
 * Protocol:
 *   [0x57 0x52 0x4D 0x21] - 4 byte magic "WRM!"
 *   [SEQ]                 - 1 byte frame sequence
 *   [PKTS]                - 1 byte packet count received
 *   [CHECKSUM_LO]         - 1 byte checksum low byte
 *   [CHECKSUM_HI]         - 1 byte checksum high byte
 *   [4800 bytes]          - raw frame data
 *
 * Total: 4 + 1 + 1 + 2 + 4800 = 4808 bytes per frame
 */
void outputStreamFrame()
{
  // Only output frames with enough packets to be useful (at least 50%)
  if (streamPacketCount < 50)
  {
    resetStreamFrame();
    return;
  }

  // Compute simple checksum of frame data
  uint16_t checksum = 0;
  for (uint16_t i = 0; i < STREAM_FRAME_SIZE; i++)
  {
    checksum += streamFrameBuffer[i];
  }

  // Debug marker (text, so it shows in terminal)
  Serial.print("FRAME_OUT: seq=");
  Serial.print(currentStreamFrameSeq);
  Serial.print(" pkts=");
  Serial.print(streamPacketCount);
  Serial.print(" chk=");
  Serial.println(checksum);

  // Binary magic header "WRM!" - less likely to appear in thermal data
  const uint8_t magic[4] = {0x57, 0x52, 0x4D, 0x21};
  Serial.write(magic, 4);

  // Frame metadata
  Serial.write(currentStreamFrameSeq);
  Serial.write(streamPacketCount);

  // Checksum (little-endian)
  Serial.write((uint8_t)(checksum & 0xFF));
  Serial.write((uint8_t)(checksum >> 8));

  // Raw frame data - 4800 bytes
  Serial.write(streamFrameBuffer, STREAM_FRAME_SIZE);

  // Flush to ensure data is sent
  Serial.flush();

  // Reset for next frame
  resetStreamFrame();
}

/**
 * Command handler for stream control
 * Usage: stream start | stream stop
 */
void cmdStream(const char *args)
{
  String argStr = String(args);
  argStr.trim();
  argStr.toLowerCase();

  if (argStr == "start" || argStr == "1")
  {
    Serial.println("Starting livestream mode...");
    Serial.println("Sending stream start command to satellite...");

    memset(radioTxBuffer, 0, sizeof(radioTxBuffer));
    radioTxBuffer[0] = 'v';
    radioTxBuffer[1] = '1';

    if (sendBytesToSatellite(radioTxBuffer, 2))
    {
      Serial.println("Stream start command sent");
      streamModeActive = true;
      resetStreamFrame();
      rf23.setModeRx();
    }
    else
    {
      Serial.println("Failed to send stream start command");
    }
  }
  else if (argStr == "stop" || argStr == "0")
  {
    Serial.println("Stopping livestream mode...");
    Serial.println("Sending stream stop command to satellite...");

    memset(radioTxBuffer, 0, sizeof(radioTxBuffer));
    radioTxBuffer[0] = 'v';
    radioTxBuffer[1] = '0';

    // Send stop command multiple times to ensure delivery
    // (satellite may be in transmit mode and miss single command)
    int sendCount = 0;
    for (int i = 0; i < 5; i++)
    {
      if (sendBytesToSatellite(radioTxBuffer, 2))
      {
        sendCount++;
      }
      delay(50); // Small delay between sends
    }

    if (sendCount > 0)
    {
      Serial.print("Stream stop command sent (");
      Serial.print(sendCount);
      Serial.println("x)");
      streamModeActive = false;
      rf23.setModeRx();
    }
    else
    {
      Serial.println("Failed to send stream stop command");
    }
  }
  else
  {
    Serial.println("Usage: stream <start|stop>");
    Serial.println("  start - Begin livestream from satellite thermal camera");
    Serial.println("  stop  - End livestream mode");
  }
}
