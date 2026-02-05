// ======================================================================
// \title  ArtemisPing.hpp
// \author Dennis Sarsozo
// \brief  hpp file for ArtemisPing component implementation class
// ======================================================================

#ifndef Components_ArtemisPing_HPP
#define Components_ArtemisPing_HPP

#include "Components/ArtemisPing/ArtemisPingComponentAc.hpp"

namespace Components {

class ArtemisPing final : public ArtemisPingComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct ArtemisPing object
    ArtemisPing(const char* const compName  //!< The component name
    );

    //! Destroy ArtemisPing object
    ~ArtemisPing();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for ports
    // ----------------------------------------------------------------------

    //! Handler implementation for pingIn
    void pingIn_handler(FwIndexType portNum, U32 key) override;
};

}  // namespace Components

#endif
