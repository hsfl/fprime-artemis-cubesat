#ifndef Components_EpsAdapter_Artemis_HPP
#define Components_EpsAdapter_Artemis_HPP

#include "../../../external/artemis-pdu/src/pdu_protocol_v2.h"

#include "Components/EpsAdapter_Artemis/EpsAdapter_ArtemisComponentAc.hpp"
#include "Components/LinkCfg/LinkCfg.hpp"

namespace Components {

class EpsAdapter_Artemis final : public EpsAdapter_ArtemisComponentBase {
  public:
    EpsAdapter_Artemis(const char* const compName);
    ~EpsAdapter_Artemis();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(
        FwIndexType portNum,
        const Components::EpsRequest& request,
        U8 outputId,
        U8 state,
        U16 durationMs
    ) override;
    void teensyResponseIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    struct PduResponse {
        U8 opcode;
        U8 status;
        U8 payloadLen;
        U8 payload[96];
    };

    bool sendPduRequest(U8 opcode, const U8* payload, U8 payloadLen);
    bool parsePduResponse(const U8* frame, U32 frameLen, U8 expectedSeq, U8 expectedOpcode, PduResponse& response);
    void processResponse(const Components::EpsRequest& request, const PduResponse& response);
    void emitStatus(Components::HealthState health, U8 linkState, U8 pduStatus, U8 opcode);
    void writeTelemetry();

    static U16 crc16Ccitt(const U8* data, U32 length);
    static U32 readLe32(const U8* data);
    static U8 mapRequestToOpcode(const Components::EpsRequest& request);

    static constexpr U8 LINK_NOT_CONFIGURED = 0;
    static constexpr U8 LINK_REQUEST_QUEUED = 1;
    static constexpr U8 LINK_PROTOCOL_OK = 2;
    static constexpr U8 LINK_ERROR = 3;
    static constexpr U8 LINK_BUSY = 4;
    static constexpr U8 LOCAL_HEADER_LEN = 4;
    static constexpr U32 LOCAL_MAX_PACKET_LEN = LOCAL_HEADER_LEN + PDU_V2_MAX_FRAME_LEN;
    static constexpr U32 PDU_REQUEST_TIMEOUT_TICKS = 2;

    U8 m_sequence;
    U8 m_pendingRequestId;
    U8 m_pendingOpcode;
    U32 m_pendingRequestTicks;
    bool m_requestPending;
    Components::EpsRequest m_lastRequest;
    U8 m_protocolVersion;
    U16 m_outputBitmap;
    U8 m_resetCause;
    U8 m_faultBitmap;
    U32 m_uptimeSeconds;
    U8 m_capabilities;
    U8 m_lastPduStatus;
    U8 m_lastOpcode;
    U32 m_transportFailureCount;
    U8 m_localPacket[LOCAL_MAX_PACKET_LEN];

    void run_handler(FwIndexType portNum, U32 context) override;
    void timeoutPendingRequest();
};

}  // namespace Components

#endif
