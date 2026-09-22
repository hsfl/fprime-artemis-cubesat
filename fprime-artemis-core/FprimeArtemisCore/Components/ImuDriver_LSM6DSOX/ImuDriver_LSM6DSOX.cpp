// ======================================================================
// \title  ImuDriver_LSM6DSOX.cpp
// \brief  cpp file for ImuDriver_LSM6DSOX component implementation class
// ======================================================================

#include "FprimeArtemisCore/Components/ImuDriver_LSM6DSOX/ImuDriver_LSM6DSOX.hpp"

namespace Components {

ImuDriver_LSM6DSOX::ImuDriver_LSM6DSOX(const char* const compName) : ImuDriver_LSM6DSOXComponentBase(compName) {}

ImuDriver_LSM6DSOX::~ImuDriver_LSM6DSOX() {}

void ImuDriver_LSM6DSOX::configure(U8 address) {
    this->m_address = address;
}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

Fw::Success ImuDriver_LSM6DSOX::powerRequestIn_handler(FwIndexType portNum, const Fw::On& state) {
    if (state == Fw::On::ON) {
        const Fw::Success status = this->powerOn();
        if (status != Fw::Success::SUCCESS) {
            // Best effort: a failure partway through may have left one sensor
            // sampling. The chip is reported off either way.
            (void)this->powerDown();
            this->setPowerState(Fw::On::OFF);
            return Fw::Success::FAILURE;
        }
        this->setPowerState(Fw::On::ON);
        return Fw::Success::SUCCESS;
    }

    // Reads stop even if the power-down write fails: a chip that cannot be
    // commanded is not one whose readings should be trusted.
    this->setPowerState(Fw::On::OFF);
    return this->powerDown();
}

Components::ImuReadStatus ImuDriver_LSM6DSOX::readingGet_handler(FwIndexType portNum,
                                                                  Components::ImuReading& reading) {
    // A powered-down chip still holds its last output, which is not "latest".
    if (this->m_powerState != Fw::On::ON) {
        return Components::ImuReadStatus::POWERED_OFF;
    }

    U8 data[OUTPUT_LENGTH] = {};
    if (this->readRegisters(REG_OUT_TEMP_L, data, OUTPUT_LENGTH) != Drv::I2cStatus::I2C_OK) {
        return Components::ImuReadStatus::BUS_ERROR;
    }

    // Burst layout from OUT_TEMP_L: temp, gyro X/Y/Z, accel X/Y/Z
    const F32 temperature = TEMP_OFFSET_DEGC + static_cast<F32>(toI16(&data[0])) / TEMP_LSB_PER_DEGC;
    const Components::Vector3 angularRate(static_cast<F32>(toI16(&data[2])) * GYRO_RADPS_PER_LSB,
                                          static_cast<F32>(toI16(&data[4])) * GYRO_RADPS_PER_LSB,
                                          static_cast<F32>(toI16(&data[6])) * GYRO_RADPS_PER_LSB);
    const Components::Vector3 acceleration(static_cast<F32>(toI16(&data[8])) * ACCEL_MPS2_PER_LSB,
                                           static_cast<F32>(toI16(&data[10])) * ACCEL_MPS2_PER_LSB,
                                           static_cast<F32>(toI16(&data[12])) * ACCEL_MPS2_PER_LSB);

    reading.set(acceleration, angularRate, temperature);
    return Components::ImuReadStatus::OK;
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

Fw::Success ImuDriver_LSM6DSOX::powerOn() {
    // Identify the chip before writing to it: a wrong address or a different
    // part must not be configured as an LSM6DSOX.
    U8 chipId = 0;
    if (this->readRegisters(REG_WHO_AM_I, &chipId, 1) != Drv::I2cStatus::I2C_OK) {
        return Fw::Success::FAILURE;
    }
    if (chipId != CHIP_ID) {
        this->log_WARNING_HI_ChipIdMismatch(CHIP_ID, chipId);
        return Fw::Success::FAILURE;
    }

    // Software reset puts every register in a known state, however the chip
    // was left. A Teensy reset does not reset the chip.
    if (this->writeRegister(REG_CTRL3_C, CTRL3_SW_RESET) != Drv::I2cStatus::I2C_OK) {
        return Fw::Success::FAILURE;
    }
    // Poll instead of sleeping: this runs on the caller's thread.
    bool resetDone = false;
    for (U32 i = 0; (i < RESET_POLL_LIMIT) && !resetDone; i++) {
        U8 ctrl3 = 0;
        if (this->readRegisters(REG_CTRL3_C, &ctrl3, 1) != Drv::I2cStatus::I2C_OK) {
            return Fw::Success::FAILURE;
        }
        resetDone = (ctrl3 & CTRL3_SW_RESET) == 0;
    }
    if (!resetDone) {
        this->log_WARNING_HI_ResetTimeout();
        return Fw::Success::FAILURE;
    }

    // Block data update keeps a reading's high and low bytes from the same
    // sample; auto-increment is needed for the 14-byte burst read.
    if (this->writeRegister(REG_CTRL3_C, CTRL3_BDU_IF_INC) != Drv::I2cStatus::I2C_OK) {
        return Fw::Success::FAILURE;
    }

    // Disable I3C, since the bus is plain I2C. CTRL9_XL's other bits have
    // non-zero defaults, so read-modify-write.
    U8 ctrl9 = 0;
    if (this->readRegisters(REG_CTRL9_XL, &ctrl9, 1) != Drv::I2cStatus::I2C_OK) {
        return Fw::Success::FAILURE;
    }
    if (this->writeRegister(REG_CTRL9_XL, static_cast<U8>(ctrl9 | CTRL9_I3C_DISABLE)) != Drv::I2cStatus::I2C_OK) {
        return Fw::Success::FAILURE;
    }

    // Setting a data rate is what starts sampling
    if (this->writeRegister(REG_CTRL1_XL, CTRL1_XL_104HZ_4G) != Drv::I2cStatus::I2C_OK) {
        return Fw::Success::FAILURE;
    }
    if (this->writeRegister(REG_CTRL2_G, CTRL2_G_104HZ_500DPS) != Drv::I2cStatus::I2C_OK) {
        return Fw::Success::FAILURE;
    }
    return Fw::Success::SUCCESS;
}

Fw::Success ImuDriver_LSM6DSOX::powerDown() {
    // Attempt both, so one failure does not leave the other sensor sampling
    const Drv::I2cStatus accelStatus = this->writeRegister(REG_CTRL1_XL, ODR_POWER_DOWN);
    const Drv::I2cStatus gyroStatus = this->writeRegister(REG_CTRL2_G, ODR_POWER_DOWN);
    return ((accelStatus == Drv::I2cStatus::I2C_OK) && (gyroStatus == Drv::I2cStatus::I2C_OK))
               ? Fw::Success::SUCCESS
               : Fw::Success::FAILURE;
}

Drv::I2cStatus ImuDriver_LSM6DSOX::writeRegister(U8 reg, U8 value) {
    U8 bytes[2] = {reg, value};
    Fw::Buffer writeBuffer(bytes, sizeof(bytes));
    const Drv::I2cStatus status = this->busWrite_out(0, this->m_address, writeBuffer);
    if (status != Drv::I2cStatus::I2C_OK) {
        this->log_WARNING_HI_I2cError(reg, status);
    }
    return status;
}

Drv::I2cStatus ImuDriver_LSM6DSOX::readRegisters(U8 reg, U8* data, FwSizeType length) {
    U8 regAddr = reg;
    Fw::Buffer writeBuffer(&regAddr, sizeof(regAddr));
    Fw::Buffer readBuffer(data, length);
    const Drv::I2cStatus status = this->busWriteRead_out(0, this->m_address, writeBuffer, readBuffer);
    if (status != Drv::I2cStatus::I2C_OK) {
        this->log_WARNING_HI_I2cError(reg, status);
    }
    return status;
}

void ImuDriver_LSM6DSOX::setPowerState(Fw::On state) {
    if (state != this->m_powerState) {
        this->m_powerState = state;
        this->log_ACTIVITY_HI_PowerStateChanged(state);
    }
    // Written every time so the channel is populated after the first request
    this->tlmWrite_PowerState(state);
}

I16 ImuDriver_LSM6DSOX::toI16(const U8* data) {
    return static_cast<I16>(static_cast<U16>(data[0]) | (static_cast<U16>(data[1]) << 8));
}

}  // namespace Components
