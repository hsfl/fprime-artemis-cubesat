// ======================================================================
// \title  ArtemisPing.cpp
// \author Dennis Sarsozo
// \brief  cpp file for ArtemisPing component implementation class
// ======================================================================

#include "Components/ArtemisPing/ArtemisPing.hpp"

namespace Components {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

ArtemisPing ::ArtemisPing(const char* const compName) : ArtemisPingComponentBase(compName) {}

ArtemisPing ::~ArtemisPing() {}

// ----------------------------------------------------------------------
// Handler implementations for ports
// ----------------------------------------------------------------------

void ArtemisPing ::pingIn_handler(FwIndexType portNum, U32 key) {
    this->pingOut_out(0, key);
}

}  // namespace Components
