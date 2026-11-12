#ifndef Components_PayloadLinkService_HPP
#define Components_PayloadLinkService_HPP

#include "FprimeArtemisCore/Components/PayloadLinkService/PayloadLinkServiceComponentAc.hpp"
#include "ProductFile.hpp"
#include "ProductCatalog.hpp"
#include <array>
#include <string>

namespace Components {

class PayloadLinkService final : public PayloadLinkServiceComponentBase {
  public:
    explicit PayloadLinkService(const char* name);
    ~PayloadLinkService();

  private:
    enum Error : U8 { NONE, BUSY, NOT_READY, BAD_REQUEST, STALE, CAPTURE_FAILED,
                      FILE_IO, TIMEOUT, BAD_ACK, ABORTED, QUEUE_FULL };
    enum Stage { IDLE, SEND_BEGIN, WAIT_BEGIN, SEND_CHUNK, WAIT_CHUNK, SEND_COMMIT, WAIT_COMMIT };
    static constexpr unsigned QUEUE_DEPTH = 16;
    static constexpr U32 RETRY_TICKS = 200;
    static constexpr U32 STATE_MAX_AGE_TICKS = 300;
    static constexpr unsigned MAX_RETRIES = 4;
    void messageIn_handler(FwIndexType, const TwinScience::Frame&) override;
    void productIn_handler(FwIndexType, U32, U32, const Components::ScienceProductSource&,
                           const Fw::StringBase&, U32) override;
    void stateIn_handler(FwIndexType, const Components::PayloadState&) override;
    void cancelDoneIn_handler(FwIndexType) override;
    void run_handler(FwIndexType, U32) override;
    void process(const TwinScience::Frame&);
    TwinScience::Frame reply(TwinScience::Operation operation, U8 error = NONE) const;
    bool enqueue(const TwinScience::Frame&);
    void fail(U8 error);
    void stopStage();
    void startStage(const TwinScience::Frame&);
    void advanceStage();
    bool sameRequest(const TwinScience::Frame&) const;
    bool sameStage(const TwinScience::Frame&) const;

    std::array<TwinScience::Frame, QUEUE_DEPTH> m_incoming{};
    unsigned m_inHead = 0, m_inCount = 0;
    std::array<TwinScience::Frame, QUEUE_DEPTH> m_outgoing{};
    unsigned m_outHead = 0, m_outCount = 0;
    bool m_queueOverflow = false;
    Components::PayloadState m_state = Components::PayloadState::UNKNOWN;
    U32 m_stateAgeTicks = STATE_MAX_AGE_TICKS + 1;
    U32 m_session = 0, m_request = 0, m_generation = 0, m_duration = 0;
    bool m_haveRequest = false, m_capturing = false, m_cancelled = false;
    U8 m_captureError = NONE;
    bool m_capturePending = false, m_cancelPending = false, m_productPending = false, m_productValid = false;
    bool m_cancelDispatched = false, m_cancelDonePending = false;
    U32 m_product = 0, m_bytes = 0;
    U16 m_crc = 0;
    std::string m_path;
    std::string m_catalog;
    std::string m_productRoot;
    bool m_haveStageOutcome = false;
    TwinScience::Frame m_stageOutcome{};
    Stage m_stage = IDLE;
    ArtemisPayload::ProductFile m_file;
    TwinScience::Frame m_lastSent{};
    U32 m_offset = 0, m_expectedOffset = 0, m_waitTicks = 0;
    unsigned m_retries = 0;
    U32 m_requests = 0, m_committed = 0, m_errors = 0;
};
}
#endif
