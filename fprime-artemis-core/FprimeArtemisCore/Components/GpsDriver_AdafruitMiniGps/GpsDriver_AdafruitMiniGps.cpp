// ======================================================================
// \title  GpsDriver_AdafruitMiniGps.cpp
// \brief  cpp file for GpsDriver_AdafruitMiniGps component implementation class
// ======================================================================

#include "FprimeArtemisCore/Components/GpsDriver_AdafruitMiniGps/GpsDriver_AdafruitMiniGps.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Components {

namespace {
//! GGA field positions. Field 0 is the sentence name.
constexpr U32 GGA_UTC = 1;
constexpr U32 GGA_LATITUDE = 2;
constexpr U32 GGA_NS = 3;
constexpr U32 GGA_LONGITUDE = 4;
constexpr U32 GGA_EW = 5;
constexpr U32 GGA_FIX_QUALITY = 6;
constexpr U32 GGA_SATELLITES = 7;
constexpr U32 GGA_ALTITUDE = 9;
//! Fields needed before a GGA can be read at all
constexpr U32 GGA_MIN_FIELDS = GGA_ALTITUDE + 1;
}  // namespace

GpsDriver_AdafruitMiniGps::GpsDriver_AdafruitMiniGps(const char* const compName)
    : GpsDriver_AdafruitMiniGpsComponentBase(compName) {
    this->m_line[0] = '\0';
}

GpsDriver_AdafruitMiniGps::~GpsDriver_AdafruitMiniGps() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void GpsDriver_AdafruitMiniGps::drvReceiveIn_handler(FwIndexType portNum,
                                                     Fw::Buffer& recvBuffer,
                                                     const Drv::ByteStreamStatus& recvStatus) {
    if (recvStatus != Drv::ByteStreamStatus::OP_OK) {
        // RECV_NO_DATA is the normal idle case: the UART had nothing this tick.
        if (recvStatus != Drv::ByteStreamStatus::RECV_NO_DATA) {
            this->log_WARNING_LO_ReceiveError(recvStatus);
        }
    } else {
        const U8* const data = recvBuffer.getData();
        const FwSizeType size = recvBuffer.getSize();
        for (FwSizeType i = 0; i < size; i++) {
            this->acceptByte(data[i]);
        }
    }
    // Ownership goes back to the driver on every path, error included
    this->drvReceiveReturnOut_out(0, recvBuffer);
}

void GpsDriver_AdafruitMiniGps::drvConnected_handler(FwIndexType portNum) {
    // The UART is open. Nothing to configure: the module streams on its own.
}

Components::GpsReadStatus GpsDriver_AdafruitMiniGps::readingGet_handler(FwIndexType portNum,
                                                                       Components::GpsFix& fix) {
    // Nothing recent means nothing is driving the line: unpowered or unplugged.
    if (this->m_ticksSinceSentence >= STALE_TICKS) {
        return Components::GpsReadStatus::NO_DATA;
    }
    if (!this->m_hasFix) {
        return Components::GpsReadStatus::NO_FIX;
    }
    fix = this->m_fix;
    return Components::GpsReadStatus::OK;
}

void GpsDriver_AdafruitMiniGps::run_handler(FwIndexType portNum, U32 context) {
    if (this->m_ticksSinceSentence < STALE_TICKS) {
        this->m_ticksSinceSentence++;
        // A fix held past the staleness window is not a current position
        if (this->m_ticksSinceSentence >= STALE_TICKS) {
            this->m_hasFix = false;
        }
    }
    this->tlmWrite_SentencesParsed(this->m_sentencesParsed);
    this->tlmWrite_SentenceErrors(this->m_sentenceErrors);
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

void GpsDriver_AdafruitMiniGps::acceptByte(U8 byte) {
    const char c = static_cast<char>(byte);

    if ((c == '\r') || (c == '\n')) {
        if (this->m_lineDropped) {
            // The line ending is what re-syncs after an overflow
            this->m_lineDropped = false;
        } else if (this->m_lineLength > 0) {
            this->parseLine();
        }
        this->m_lineLength = 0;
        return;
    }

    if (this->m_lineDropped) {
        return;
    }

    // '$' starts a sentence: anything held before it was a partial line
    if (c == '$') {
        this->m_lineLength = 0;
    }

    // Leave room for the NUL parseLine writes
    if (this->m_lineLength >= (LINE_BUFFER_SIZE - 1)) {
        this->m_lineDropped = true;
        this->m_lineLength = 0;
        this->m_sentenceErrors++;
        this->log_WARNING_LO_SentenceOverflow();
        return;
    }

    this->m_line[this->m_lineLength] = c;
    this->m_lineLength++;
}

void GpsDriver_AdafruitMiniGps::parseLine() {
    this->m_line[this->m_lineLength] = '\0';

    // Shortest usable sentence is "$xxGGA*hh"
    if ((this->m_line[0] != '$') || (this->m_lineLength < 9)) {
        this->m_sentenceErrors++;
        return;
    }

    // The checksum is the XOR of everything between '$' and '*'
    const FwSizeType starIndex = this->m_lineLength - 3;
    if (this->m_line[starIndex] != '*') {
        this->m_sentenceErrors++;
        return;
    }
    U8 high = 0;
    U8 low = 0;
    if (!hexValue(this->m_line[starIndex + 1], high) || !hexValue(this->m_line[starIndex + 2], low)) {
        this->m_sentenceErrors++;
        return;
    }
    U8 checksum = 0;
    for (FwSizeType i = 1; i < starIndex; i++) {
        checksum ^= static_cast<U8>(this->m_line[i]);
    }
    if (checksum != static_cast<U8>((high << 4) | low)) {
        this->m_sentenceErrors++;
        return;
    }

    // Only GGA carries position, altitude, and satellite count. The talker ID
    // varies with the constellation used (GP, GN, GL), so match the last three
    // characters of the sentence name only.
    if (strncmp(&this->m_line[3], "GGA", 3) != 0) {
        return;  // A sentence this driver does not use is not an error
    }

    // Split in place: the checksum is not a field, so it ends the last one
    this->m_line[starIndex] = '\0';
    const char* fields[MAX_FIELDS] = {};
    U32 fieldCount = 0;
    fields[fieldCount] = &this->m_line[0];
    fieldCount++;
    for (FwSizeType i = 1; (i < starIndex) && (fieldCount < MAX_FIELDS); i++) {
        if (this->m_line[i] == ',') {
            this->m_line[i] = '\0';
            fields[fieldCount] = &this->m_line[i + 1];
            fieldCount++;
        }
    }

    if (!this->parseGga(fields, fieldCount)) {
        this->m_sentenceErrors++;
        return;
    }

    // The sentence was well formed, whether or not it carried a lock: the
    // module is alive and talking.
    this->m_sentencesParsed++;
    this->m_ticksSinceSentence = 0;
}

bool GpsDriver_AdafruitMiniGps::parseGga(const char* const fields[], U32 fieldCount) {
    if (fieldCount < GGA_MIN_FIELDS) {
        return false;
    }

    // The field count above guarantees these are populated; the tokenizer's
    // bound makes that an argument rather than something the code shows, so
    // check it here instead of indexing on faith.
    if ((fields[GGA_FIX_QUALITY] == nullptr) || (fields[GGA_NS] == nullptr) || (fields[GGA_EW] == nullptr)) {
        return false;
    }

    // Fix quality 0 means no lock. The module sends GGA with empty position
    // fields until it acquires one, so this is the normal startup case.
    const char* const quality = fields[GGA_FIX_QUALITY];
    if ((quality[0] == '\0') || (quality[0] == '0')) {
        this->m_hasFix = false;
        return true;
    }

    F64 latitude = 0.0;
    F64 longitude = 0.0;
    if (!parseDegrees(fields[GGA_LATITUDE], fields[GGA_NS][0], latitude) ||
        !parseDegrees(fields[GGA_LONGITUDE], fields[GGA_EW][0], longitude)) {
        return false;
    }

    U32 utcSeconds = 0;
    if (!parseUtc(fields[GGA_UTC], utcSeconds)) {
        return false;
    }

    // Altitude and satellite count are informational: a blank field costs the
    // value, not the fix.
    const F32 altitude = static_cast<F32>(strtod(fields[GGA_ALTITUDE], nullptr));
    const U8 satellites = static_cast<U8>(strtoul(fields[GGA_SATELLITES], nullptr, 10));

    this->m_fix.set(latitude, longitude, altitude, satellites, utcSeconds);
    this->m_hasFix = true;
    return true;
}

bool GpsDriver_AdafruitMiniGps::parseDegrees(const char* text, char hemisphere, F64& degrees) {
    if ((text == nullptr) || (text[0] == '\0')) {
        return false;
    }
    char* end = nullptr;
    // NMEA packs position as ddmm.mmmm (lat) or dddmm.mmmm (lon): the last two
    // digits before the point are whole minutes.
    const F64 packed = strtod(text, &end);
    if ((end == text) || (packed < 0.0)) {
        return false;
    }
    const F64 wholeDegrees = std::floor(packed / 100.0);
    const F64 minutes = packed - (wholeDegrees * 100.0);
    if (minutes >= 60.0) {
        return false;
    }
    degrees = wholeDegrees + (minutes / 60.0);
    if ((hemisphere == 'S') || (hemisphere == 'W')) {
        degrees = -degrees;
    } else if ((hemisphere != 'N') && (hemisphere != 'E')) {
        return false;
    }
    return true;
}

bool GpsDriver_AdafruitMiniGps::parseUtc(const char* text, U32& seconds) {
    if ((text == nullptr) || (text[0] == '\0')) {
        return false;
    }
    char* end = nullptr;
    // hhmmss.sss; fractional seconds are dropped
    const F64 packed = strtod(text, &end);
    if ((end == text) || (packed < 0.0)) {
        return false;
    }
    const U32 hhmmss = static_cast<U32>(packed);
    const U32 hours = hhmmss / 10000;
    const U32 minutes = (hhmmss / 100) % 100;
    const U32 wholeSeconds = hhmmss % 100;
    if ((hours > 23) || (minutes > 59) || (wholeSeconds > 59)) {
        return false;
    }
    seconds = (hours * 3600) + (minutes * 60) + wholeSeconds;
    return true;
}

bool GpsDriver_AdafruitMiniGps::hexValue(char c, U8& value) {
    if ((c >= '0') && (c <= '9')) {
        value = static_cast<U8>(c - '0');
    } else if ((c >= 'A') && (c <= 'F')) {
        value = static_cast<U8>((c - 'A') + 10);
    } else if ((c >= 'a') && (c <= 'f')) {
        value = static_cast<U8>((c - 'a') + 10);
    } else {
        return false;
    }
    return true;
}

}  // namespace Components
