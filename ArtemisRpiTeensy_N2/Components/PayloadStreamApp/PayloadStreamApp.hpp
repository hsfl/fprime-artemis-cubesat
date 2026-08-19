#ifndef Components_PayloadStreamApp_HPP
#define Components_PayloadStreamApp_HPP

#include "Components/PayloadStreamApp/PayloadStreamAppComponentAc.hpp"
#include "Components/LinkCfg/LinkCfg.hpp"
#include "Os/Mutex.hpp"

namespace Components {

class PayloadStreamApp final : public PayloadStreamAppComponentBase {
  public:
    PayloadStreamApp(const char* const compName);
    ~PayloadStreamApp();

  private:
    enum StreamState : U32 {
        STOPPED = 0U,
        WAITING_FOR_SOURCE = 1U,
        UPLOADING = 2U,
    };

    enum PreviewOperation : U8 {
        OP_BEGIN = 1U,
        OP_CHUNK = 2U,
        OP_COMMIT = 3U,
        OP_ABORT = 4U,
    };

    static constexpr U8 PREVIEW_TARGET = LinkCfg::TEENSY_TARGET_LEPTON_PREVIEW;
    static constexpr U8 RPC_STATUS_OK = 0U;
    static constexpr FwSizeType PREVIEW_BYTES = 80U * 60U;
    static constexpr FwSizeType MAX_CHUNK_BYTES = 200U;
    static constexpr U32 RESPONSE_TIMEOUT_TICKS = 5U;

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void previewIn_handler(FwIndexType portNum, Fw::Buffer& preview) override;
    void previewResponseIn_handler(FwIndexType portNum, Fw::Buffer& response) override;
    void responseAdvanceIn_handler(FwIndexType portNum) override;
    void START_STREAM_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void STOP_STREAM_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void GET_STREAM_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    void requestNextPreview();
    void processPreviewResponse(const U8* data);
    bool sendBegin();
    bool sendChunk(FwSizeType offset);
    bool sendCommit();
    void sendAbortBestEffort();
    bool sendRequest(FwSizeType size, PreviewOperation operation);
    void dropPreview(U32 reason, U32 detail);
    void finishPreview();
    void writeTelemetry();
    static void putU16(U8* data, FwSizeType offset, U16 value);
    static void putU32(U8* data, FwSizeType offset, U32 value);
    static U16 getU16(const U8* data, FwSizeType offset);
    static U32 getU32(const U8* data, FwSizeType offset);
    static U16 crc16Ccitt(const U8* data, FwSizeType size);

    StreamState m_state;
    bool m_streaming;
    bool m_waitingResponse;
    U32 m_responseDeadlineTicks;
    U8 m_requestId;
    U16 m_sessionId;
    U32 m_frameSequence;
    U32 m_framesUploaded;
    U32 m_framesDropped;
    U32 m_lastResponseStatus;
    PreviewOperation m_pendingOperation;
    FwSizeType m_expectedNextOffset;
    const U8* m_previewData;
    U16 m_previewCrc;
    U8 m_request[4U + 6U + MAX_CHUNK_BYTES];
    U8 m_responseMailbox[12U];
    bool m_haveResponseMailbox;
    Os::Mutex m_responseMailboxMutex;
};

}  // namespace Components

#endif
