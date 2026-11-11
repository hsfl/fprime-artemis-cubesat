#ifndef Components_StorageManagerTester_HPP
#define Components_StorageManagerTester_HPP

#include "Components/StorageManager/StorageManager.hpp"
#include "Components/StorageManager/StorageManagerGTestBase.hpp"

namespace Components {

class StorageManagerTester final : public StorageManagerGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 32;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    StorageManagerTester();
    ~StorageManagerTester();

    void testCommittedProductIsReported();
    void testBadChecksumIsReportedNotStored();
    void testBadDescriptorIsReportedNotStored();

  private:
    void connectPorts();
    void initComponents();

    StorageManager component;
};

}  // namespace Components

#endif
