#ifndef Components_DpWrittenRouter_HPP
#define Components_DpWrittenRouter_HPP

#include "Components/DpWrittenRouter/DpWrittenRouterComponentAc.hpp"

namespace Components {

class DpWrittenRouter final : public DpWrittenRouterComponentBase {
  public:
    DpWrittenRouter(const char* const compName);
    ~DpWrittenRouter();

  private:
    void dpWrittenIn_handler(FwIndexType portNum,
                             const Fw::StringBase& fileName,
                             FwDpPriorityType priority,
                             FwSizeType size) override;
};

}  // namespace Components

#endif
