#ifndef Components_PayloadDriverSelector_HPP
#define Components_PayloadDriverSelector_HPP

#include "Components/PayloadDriverSelector/PayloadDriverSelectorComponentAc.hpp"

namespace Components {

class PayloadDriverSelector final : public PayloadDriverSelectorComponentBase {
  public:
    PayloadDriverSelector(const char* const compName);
    ~PayloadDriverSelector();

  private:
    enum RejectReason : U32 {
        REJECT_BUSY = 1,
        REJECT_INVALID_DRIVER = 2,
        REJECT_DRIVER_NOT_CONNECTED = 3,
        REJECT_DUPLICATE_REQUEST = 4,
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(FwIndexType portNum, U32 durationSeconds) override;
    void driverStatusIn_handler(FwIndexType portNum,
                                U32 productId,
                                U32 productBytes,
                                const Components::ScienceProductSource& sourceKind,
                                const Fw::StringBase& sourcePath,
                                U32 sourceCrc) override;
    void SELECT_PAYLOAD_DRIVER_cmdHandler(
        FwOpcodeType opCode,
        U32 cmdSeq,
        Components::PayloadDriverKind driver
    ) override;

    void publishFailure();
    void writeTelemetry();
    FwIndexType selectedIndex() const;

    Components::PayloadDriverKind m_selectedDriver;
    Components::PayloadDriverKind m_inFlightDriver;
    bool m_requestInFlight;
};

}  // namespace Components

#endif
