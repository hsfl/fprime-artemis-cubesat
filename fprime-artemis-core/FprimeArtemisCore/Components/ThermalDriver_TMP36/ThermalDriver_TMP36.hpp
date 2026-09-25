// ======================================================================
// \title  ThermalDriver_TMP36.hpp
// \brief  hpp file for ThermalDriver_TMP36 component implementation class
// ======================================================================

#ifndef Components_ThermalDriver_TMP36_HPP
#define Components_ThermalDriver_TMP36_HPP

#include "FprimeArtemisCore/Components/ThermalDriver_TMP36/ThermalDriver_TMP36ComponentAc.hpp"
#include "FprimeArtemisCore/Types/FppConstantsAc.hpp"

namespace Components {

class ThermalDriver_TMP36 final : public ThermalDriver_TMP36ComponentBase {
  public:
    explicit ThermalDriver_TMP36(const char* const compName);
    ~ThermalDriver_TMP36();

    //! TMP36 transfer function: 500 mV at 0 C, 10 mV per degree C
    static constexpr F32 OFFSET_MV = 500.0f;
    static constexpr F32 MV_PER_DEGREE_C = 10.0f;

    //! Each TMP36 output reaches the ADC through a 45.3k/10k divider
    //! (TMP36 datasheet Rev. H, Figure 26): pin mV = TMP36 mV * 10 / 55.3.
    static constexpr F32 DIVIDER_R_TOP_KOHM = 45.3f;
    static constexpr F32 DIVIDER_R_BOTTOM_KOHM = 10.0f;
    static constexpr F32 DIVIDER_GAIN = (DIVIDER_R_TOP_KOHM + DIVIDER_R_BOTTOM_KOHM) / DIVIDER_R_BOTTOM_KOHM;

    //! Rated range at the TMP36 output, -40 C to +125 C. Anything outside it
    //! is not a TMP36 reading: 0 mV is a failed conversion or open input.
    static constexpr F32 MIN_VALID_MV = 100.0f;
    static constexpr F32 MAX_VALID_MV = 1750.0f;

  private:
    static constexpr FwSizeType SENSOR_COUNT = THERMAL_SENSOR_COUNT;

    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Convert every sensor and report which ones read inside the rated range
    ThermalReadStatus readingGet_handler(FwIndexType portNum, ThermalReading& reading) override;

    //! Millivolts back from ADC driver portNum, inside readingGet
    void adcMvIn_handler(FwIndexType portNum, U32 value) override;

    //! Last millivolts from each ADC
    U32 m_millivolts[SENSOR_COUNT] = {};

    //! Whether each ADC answered the current read
    bool m_received[SENSOR_COUNT] = {};
};

}  // namespace Components

#endif
