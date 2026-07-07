#ifndef Components_GpsManager_HPP
#define Components_GpsManager_HPP

#include "Components/GpsManager/GpsManagerComponentAc.hpp"

namespace Components {

class GpsManager final : public GpsManagerComponentBase {
  public:
    GpsManager(const char* const compName);
    ~GpsManager();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void driverStatusIn_handler(FwIndexType portNum, U32 key) override;
    void REQUEST_GPS_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    U32 m_state;
    U32 m_managerHeartbeat;
};

}  // namespace Components

#endif
