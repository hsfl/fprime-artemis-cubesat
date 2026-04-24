#ifndef Components_GpsService_HPP
#define Components_GpsService_HPP

#include "Components/GpsService/GpsServiceComponentAc.hpp"

namespace Components {

class GpsService final : public GpsServiceComponentBase {
  public:
    GpsService(const char* const compName);
    ~GpsService();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void adapterStatusIn_handler(FwIndexType portNum, U32 key) override;
    void REQUEST_GPS_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    U32 m_state;
    U32 m_serviceHeartbeat;
};

}  // namespace Components

#endif
