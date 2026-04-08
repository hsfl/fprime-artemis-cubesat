#ifndef Components_PayloadAdapter_N1Legacy_HPP
#define Components_PayloadAdapter_N1Legacy_HPP

#include "Components/PayloadAdapter_N1Legacy/PayloadAdapter_N1LegacyComponentAc.hpp"

namespace Components {

class PayloadAdapter_N1Legacy final : public PayloadAdapter_N1LegacyComponentBase {
  public:
    PayloadAdapter_N1Legacy(const char* const compName);
    ~PayloadAdapter_N1Legacy();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void requestIn_handler(FwIndexType portNum, U32 key) override;

    static constexpr U32 STATUS_OFFSET = 200;
    U32 m_lastRequestKey;
};

}  // namespace Components

#endif
