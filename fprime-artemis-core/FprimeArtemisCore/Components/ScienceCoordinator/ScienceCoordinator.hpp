#pragma once
#include "FprimeArtemisCore/Components/ScienceCoordinator/ScienceCoordinatorComponentAc.hpp"
#include "FprimeArtemisCore/Science/ProductCache.hpp"
#include <array>
namespace Components {
class ScienceCoordinator final : public ScienceCoordinatorComponentBase {
  public:
    explicit ScienceCoordinator(const char* name);
    void configure(U32 session);
  private:
    void run_handler(FwIndexType, U32) override;
    void admissionIn_handler(FwIndexType, bool) override;
    void payloadStateIn_handler(FwIndexType, const PayloadState&) override;
    void messageIn_handler(FwIndexType, const TwinScience::Frame&) override;
    Fw::CmdResponse scheduleIn_handler(FwIndexType, U32) override;
    Fw::CmdResponse cancelIn_handler(FwIndexType) override;
    Fw::CmdResponse downlinkIn_handler(FwIndexType) override;
    bool radioNext_handler(FwIndexType, ScienceBytes&, U8&) override;
    void radioSent_handler(FwIndexType, bool) override;
    void radioRetry_handler(FwIndexType, const ScienceBytes&, U8) override;
    void CONFIGURE_CAPTURE_DURATION_cmdHandler(FwOpcodeType, U32, U32) override;
    void REQUEST_STATUS_cmdHandler(FwOpcodeType, U32) override;
    void RESTAGE_PRODUCT_cmdHandler(FwOpcodeType, U32, U32) override;
    TwinScience::Frame make(TwinScience::Operation operation) const;
    void queue(const TwinScience::Frame& frame);
    void publish();
    void transition(ScienceJobState state);
    void fail(U32 reason);
    bool matching(const TwinScience::Frame& frame) const;
    Artemis::Science::ProductKey key(const TwinScience::Frame& frame) const;
    Artemis::Science::ProductCache cache;
    Artemis::Science::Downlink downlink;
    ScienceJobState state = ScienceJobState::IDLE;
    PayloadState payloadState = PayloadState::UNKNOWN;
    U32 session = 1, request = 0, generation = 0, product = 0, bytes = 0;
    U16 crc = 0;
    U32 ticks = 0, due = 0, deadline = 0, duration = 3, payloadAge = 301, sentPackets = 0;
    unsigned consecutiveTxFailures = 0;
    bool admitted = false, restaging = false;
    std::array<TwinScience::Frame, 4> outbox;
    unsigned head = 0, count = 0;
};
}
