// ======================================================================
// \title  GpsDriver_AdafruitMiniGps.hpp
// \brief  hpp file for GpsDriver_AdafruitMiniGps component implementation class
// ======================================================================

#ifndef Components_GpsDriver_AdafruitMiniGps_HPP
#define Components_GpsDriver_AdafruitMiniGps_HPP

#include "FprimeArtemisCore/Components/GpsDriver_AdafruitMiniGps/GpsDriver_AdafruitMiniGpsComponentAc.hpp"

namespace Components {

class GpsDriver_AdafruitMiniGps final : public GpsDriver_AdafruitMiniGpsComponentBase {
  public:
    //! Longest NMEA sentence accepted, including the leading '$'.
    //! The standard caps a sentence at 82 characters.
    static constexpr FwSizeType LINE_BUFFER_SIZE = 84;

    //! Most comma-separated fields read out of one sentence.
    //! GGA has 15; the rest are ignored.
    static constexpr U32 MAX_FIELDS = 16;

    //! Run ticks without an accepted sentence before reads return NO_DATA.
    //! The PA1010D emits GGA once a second, so at the 1Hz rate group this is
    //! three missed sentences.
    static constexpr U32 STALE_TICKS = 3;

    explicit GpsDriver_AdafruitMiniGps(const char* const compName);
    ~GpsDriver_AdafruitMiniGps();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! NMEA bytes from the UART driver
    void drvReceiveIn_handler(FwIndexType portNum,
                              Fw::Buffer& recvBuffer,
                              const Drv::ByteStreamStatus& recvStatus) override;

    //! The UART driver reports it is ready
    void drvConnected_handler(FwIndexType portNum) override;

    //! GpsManager asks for the latest fix
    Components::GpsReadStatus readingGet_handler(FwIndexType portNum, Components::GpsFix& fix) override;

    //! Age the last accepted sentence and publish counters
    void run_handler(FwIndexType portNum, U32 context) override;

    // ----------------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------------

    //! Feed one received byte into the line buffer, parsing a completed line
    void acceptByte(U8 byte);

    //! Validate and parse the sentence now in the line buffer
    void parseLine();

    //! Parse a GGA sentence's fields into m_fix
    //! \return true if the sentence yielded a usable position or a clean no-fix
    bool parseGga(const char* const fields[], U32 fieldCount);

    //! Convert an NMEA ddmm.mmmm / dddmm.mmmm value plus its hemisphere
    //! character into signed degrees
    //! \return false if the field is empty or malformed
    static bool parseDegrees(const char* text, char hemisphere, F64& degrees);

    //! Convert hhmmss.sss into seconds since UTC midnight
    static bool parseUtc(const char* text, U32& seconds);

    //! Convert a hex digit to its value
    //! \return false if the character is not a hex digit
    static bool hexValue(char c, U8& value);

    //! Latest fix, valid only while m_hasFix
    Components::GpsFix m_fix;

    //! Whether the last accepted GGA reported a satellite lock
    bool m_hasFix = false;

    //! Run ticks since the last accepted sentence. Starts stale: nothing has
    //! arrived yet at boot.
    U32 m_ticksSinceSentence = STALE_TICKS;

    //! Characters held in m_line
    FwSizeType m_lineLength = 0;

    //! Whether the current line overflowed and is being discarded up to the
    //! next line ending
    bool m_lineDropped = false;

    //! The sentence being assembled, NUL-terminated by parseLine
    char m_line[LINE_BUFFER_SIZE];

    //! Accepted GGA sentences since boot
    U32 m_sentencesParsed = 0;

    //! Sentences rejected since boot
    U32 m_sentenceErrors = 0;
};

}  // namespace Components

#endif
