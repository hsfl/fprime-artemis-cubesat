#ifndef Components_CommsDriver_TeensyRfm23Tester_HPP
#define Components_CommsDriver_TeensyRfm23Tester_HPP

#include "Components/CommsDriver_TeensyRfm23/CommsDriver_TeensyRfm23.hpp"
#include "Components/CommsDriver_TeensyRfm23/CommsDriver_TeensyRfm23GTestBase.hpp"

#include <vector>

namespace Components {

class CommsDriver_TeensyRfm23Tester final : public CommsDriver_TeensyRfm23GTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 32;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    CommsDriver_TeensyRfm23Tester();
    ~CommsDriver_TeensyRfm23Tester();

    void testParsesCorrelatedStatus();
    void testTargetErrorPreservesFactualOffState();
    void testRejectsStaleResponseAndTimesOutPendingRequest();

  private:
    void connectPorts();
    void initComponents();
    void from_teensyRequestOut_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    static void writeLe16(U8* out, U16 value);
    static void writeLe32(U8* out, U32 value);

    CommsDriver_TeensyRfm23 component;
    std::vector<std::vector<U8> > m_requests;
};

}  // namespace Components

#endif
