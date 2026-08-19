#include "Components/PayloadDriverSelector/PayloadDriverSelector.hpp"

namespace Components {

PayloadDriverSelector::PayloadDriverSelector(const char* const compName)
    : PayloadDriverSelectorComponentBase(compName),
      m_selectedDriver(Components::PayloadDriverKind::LEPTON),
      m_inFlightDriver(Components::PayloadDriverKind::LEPTON),
      m_requestInFlight(false) {}

PayloadDriverSelector::~PayloadDriverSelector() {}

void PayloadDriverSelector::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void PayloadDriverSelector::requestIn_handler(FwIndexType portNum, U32 durationSeconds) {
    static_cast<void>(portNum);
    if (this->m_requestInFlight) {
        this->log_WARNING_LO_PayloadSelectionRejected(REJECT_DUPLICATE_REQUEST);
        return;
    }

    const FwIndexType index = this->selectedIndex();
    if (!this->isConnected_driverRequestOut_OutputPort(index)) {
        this->log_WARNING_LO_PayloadSelectionRejected(REJECT_DRIVER_NOT_CONNECTED);
        this->publishFailure();
        return;
    }

    this->m_inFlightDriver = this->m_selectedDriver;
    this->m_requestInFlight = true;
    this->writeTelemetry();
    this->log_ACTIVITY_LO_PayloadRequestRouted(this->m_inFlightDriver);
    this->driverRequestOut_out(index, durationSeconds);
}

void PayloadDriverSelector::driverStatusIn_handler(
    FwIndexType portNum,
    U32 productId,
    U32 productBytes,
    const Components::ScienceProductSource& sourceKind,
    const Fw::StringBase& sourcePath,
    U32 sourceCrc
) {
    const FwIndexType activeIndex =
        (this->m_inFlightDriver == Components::PayloadDriverKind::BOSON) ? 1U : 0U;
    if (!this->m_requestInFlight || (portNum != activeIndex)) {
        this->log_WARNING_LO_PayloadStatusIgnored(static_cast<U32>(portNum));
        return;
    }

    if (this->isConnected_statusOut_OutputPort(0)) {
        this->statusOut_out(0, productId, productBytes, sourceKind, sourcePath, sourceCrc);
    }

    this->m_requestInFlight = false;
    this->writeTelemetry();
}

void PayloadDriverSelector::SELECT_PAYLOAD_DRIVER_cmdHandler(
    FwOpcodeType opCode,
    U32 cmdSeq,
    Components::PayloadDriverKind driver
) {
    if (!driver.isValid()) {
        this->log_WARNING_LO_PayloadSelectionRejected(REJECT_INVALID_DRIVER);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (this->m_requestInFlight) {
        this->log_WARNING_LO_PayloadSelectionRejected(REJECT_BUSY);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
        return;
    }

    if (driver != this->m_selectedDriver) {
        const FwIndexType previousIndex = this->selectedIndex();
        if (this->isConnected_deactivateDriverOut_OutputPort(previousIndex)) {
            this->deactivateDriverOut_out(previousIndex);
        }
        this->m_selectedDriver = driver;
    }
    this->writeTelemetry();
    this->log_ACTIVITY_HI_PayloadDriverSelected(this->m_selectedDriver);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadDriverSelector::publishFailure() {
    if (this->isConnected_statusOut_OutputPort(0)) {
        const Fw::String emptyPath("");
        this->statusOut_out(
            0,
            0U,
            0U,
            Components::ScienceProductSource::UNKNOWN,
            emptyPath,
            0U
        );
    }
}

void PayloadDriverSelector::writeTelemetry() {
    this->tlmWrite_SelectedDriver(this->m_selectedDriver);
    this->tlmWrite_RequestInFlight(this->m_requestInFlight ? 1U : 0U);
}

FwIndexType PayloadDriverSelector::selectedIndex() const {
    return (this->m_selectedDriver == Components::PayloadDriverKind::BOSON) ? 1U : 0U;
}

}  // namespace Components
