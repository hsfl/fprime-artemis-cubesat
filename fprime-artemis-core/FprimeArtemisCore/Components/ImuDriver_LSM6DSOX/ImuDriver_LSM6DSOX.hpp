// ======================================================================
// \title  ImuDriver_LSM6DSOX.hpp
// \brief  hpp file for ImuDriver_LSM6DSOX component implementation class
// ======================================================================

#ifndef Components_ImuDriver_LSM6DSOX_HPP
#define Components_ImuDriver_LSM6DSOX_HPP

#include "FprimeArtemisCore/Components/ImuDriver_LSM6DSOX/ImuDriver_LSM6DSOXComponentAc.hpp"

namespace Components {

class ImuDriver_LSM6DSOX final : public ImuDriver_LSM6DSOXComponentBase {
  public:
    //! I2C address with SDO/SA0 low: the Adafruit breakout default
    static constexpr U8 DEFAULT_ADDRESS = 0x6A;

    explicit ImuDriver_LSM6DSOX(const char* const compName);
    ~ImuDriver_LSM6DSOX();

    //! Set the chip's I2C address. Call before the first power request.
    void configure(U8 address);

  private:
    // ----------------------------------------------------------------------
    // LSM6DSOX register map (datasheet DS12814; matches Adafruit_LSM6DS)
    // ----------------------------------------------------------------------

    static constexpr U8 REG_WHO_AM_I = 0x0F;
    static constexpr U8 REG_CTRL1_XL = 0x10;  //!< accel ODR [7:4], full scale [3:2]
    static constexpr U8 REG_CTRL2_G = 0x11;   //!< gyro ODR [7:4], full scale [3:0]
    static constexpr U8 REG_CTRL3_C = 0x12;   //!< BDU [6], IF_INC [2], SW_RESET [0]
    static constexpr U8 REG_CTRL9_XL = 0x18;  //!< I3C_disable [1]
    static constexpr U8 REG_OUT_TEMP_L = 0x20;  //!< first of 14 output bytes: temp, gyro XYZ, accel XYZ

    static constexpr U8 CHIP_ID = 0x6C;

    static constexpr U8 CTRL3_SW_RESET = 0x01;
    static constexpr U8 CTRL3_BDU_IF_INC = 0x44;  //!< block data update + address auto-increment
    static constexpr U8 CTRL9_I3C_DISABLE = 0x02;
    static constexpr U8 CTRL1_XL_104HZ_4G = 0x48;  //!< ODR 0100 = 104 Hz, FS 10 = +/-4 g
    static constexpr U8 CTRL2_G_104HZ_500DPS = 0x44;  //!< ODR 0100 = 104 Hz, FS 01 = +/-500 dps
    static constexpr U8 ODR_POWER_DOWN = 0x00;

    //! CTRL3_C reads while waiting for SW_RESET to clear. Each read is ~0.3 ms
    //! at 100 kHz; the reset itself takes tens of microseconds.
    static constexpr U32 RESET_POLL_LIMIT = 10;

    //! Output burst length: temperature (2) + gyro (6) + accel (6)
    static constexpr FwSizeType OUTPUT_LENGTH = 14;

    //! Scale factors for the ranges configured above
    static constexpr F32 ACCEL_MPS2_PER_LSB = 0.122e-3F * 9.80665F;  //!< 0.122 mg/LSB at +/-4 g
    static constexpr F32 GYRO_RADPS_PER_LSB = 17.5e-3F * 0.0174532925F;  //!< 17.5 mdps/LSB at +/-500 dps
    static constexpr F32 TEMP_LSB_PER_DEGC = 256.0F;
    static constexpr F32 TEMP_OFFSET_DEGC = 25.0F;

    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! ON: identify, reset, and configure the chip. OFF: enter power-down.
    Fw::Success powerRequestIn_handler(FwIndexType portNum, const Fw::On& state) override;

    //! Read the latest sample, or POWERED_OFF without touching the bus
    Components::ImuReadStatus readingGet_handler(FwIndexType portNum, Components::ImuReading& reading) override;

    // ----------------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------------

    //! Identify, reset, and configure the chip
    Fw::Success powerOn();

    //! Write power-down to both sensors. Both writes are attempted.
    Fw::Success powerDown();

    //! Write one register. Emits I2cError on failure.
    Drv::I2cStatus writeRegister(U8 reg, U8 value);

    //! Read length consecutive registers starting at reg. Emits I2cError on failure.
    Drv::I2cStatus readRegisters(U8 reg, U8* data, FwSizeType length);

    //! Record the power state, emitting an event and telemetry if it changed
    void setPowerState(Fw::On state);

    //! Little-endian signed 16-bit value at data[0..1]
    static I16 toI16(const U8* data);

    U8 m_address = DEFAULT_ADDRESS;

    //! Whether the chip was last configured to sample. Off at construction.
    Fw::On m_powerState = Fw::On::OFF;
};

}  // namespace Components

#endif
