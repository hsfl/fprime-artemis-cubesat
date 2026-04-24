#include "Components/GpsAdapter_Artemis/GpsAdapter_Artemis.hpp"

namespace Components {

GpsAdapter_Artemis::GpsAdapter_Artemis(const char* const compName)
    : GpsAdapter_ArtemisComponentBase(compName),
      m_lastRequestKey(0),
      m_requestCount(0),
      m_fixState(FixState::NO_FIX),
      m_satellitesTracked(0),
      m_fixQualityScore(0) {}

GpsAdapter_Artemis::~GpsAdapter_Artemis() {}

void GpsAdapter_Artemis::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void GpsAdapter_Artemis::requestIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_lastRequestKey = key;
    this->m_requestCount += 1;

    const FixState previousState = this->m_fixState;
    this->updateFixModel();
    const U32 status = this->toStatusKey();

    this->tlmWrite_LastRequestKey(this->m_lastRequestKey);
    this->tlmWrite_RequestCount(this->m_requestCount);
    this->tlmWrite_FixState(status);
    this->tlmWrite_SatellitesTracked(this->m_satellitesTracked);
    this->tlmWrite_FixQualityScore(this->m_fixQualityScore);

    if (previousState != this->m_fixState) {
        this->log_ACTIVITY_HI_FixStateChanged(static_cast<U32>(previousState), status);
    }

    this->log_ACTIVITY_LO_RequestHandled(this->m_lastRequestKey, status);

    if (this->isConnected_statusOut_OutputPort(0)) {
        this->statusOut_out(0, status);
    }
}

void GpsAdapter_Artemis::updateFixModel() {
    if (this->m_requestCount < 3U) {
        this->m_fixState = FixState::ACQUIRING;
        this->m_satellitesTracked = 2U + (this->m_requestCount % 2U);
        this->m_fixQualityScore = 20U + (this->m_requestCount * 8U);
        return;
    }

    if ((this->m_requestCount % 47U) == 0U) {
        this->m_fixState = FixState::NO_FIX;
        this->m_satellitesTracked = 0U;
        this->m_fixQualityScore = 0U;
        return;
    }

    if ((this->m_requestCount % 19U) == 0U) {
        this->m_fixState = FixState::FIX_2D;
        this->m_satellitesTracked = 4U + (this->m_requestCount % 3U);
        this->m_fixQualityScore = 58U + (this->m_requestCount % 8U);
        return;
    }

    this->m_fixState = FixState::FIX_3D;
    this->m_satellitesTracked = 8U + (this->m_requestCount % 5U);
    this->m_fixQualityScore = 80U + (this->m_requestCount % 12U);
}

U32 GpsAdapter_Artemis::toStatusKey() const {
    return static_cast<U32>(this->m_fixState);
}

}  // namespace Components
