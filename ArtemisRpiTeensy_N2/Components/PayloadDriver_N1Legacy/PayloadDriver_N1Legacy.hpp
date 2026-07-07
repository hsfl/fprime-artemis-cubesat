#ifndef Components_PayloadDriver_N1Legacy_HPP
#define Components_PayloadDriver_N1Legacy_HPP

#include "Components/PayloadDriver_N1Legacy/PayloadDriver_N1LegacyComponentAc.hpp"

namespace Components {

class PayloadDriver_N1Legacy final : public PayloadDriver_N1LegacyComponentBase {
  public:
    PayloadDriver_N1Legacy(const char* const compName);
    ~PayloadDriver_N1Legacy();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(FwIndexType portNum, U32 key) override;

    static constexpr U32 STATUS_OFFSET = 200;
    U32 m_lastRequestKey;
};

}  // namespace Components

#endif
