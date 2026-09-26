// ======================================================================
// \title  PayloadComputerDeploymentTopologyDefs.hpp
// \brief required header file containing the required definitions for the topology autocoder
//
// ======================================================================
#ifndef PAYLOADCOMPUTERDEPLOYMENT_PAYLOADCOMPUTERDEPLOYMENTTOPOLOGYDEFS_HPP
#define PAYLOADCOMPUTERDEPLOYMENT_PAYLOADCOMPUTERDEPLOYMENTTOPOLOGYDEFS_HPP

// Subtopology PingEntries includes
#include "Svc/Subtopologies/CdhCore/PingEntries.hpp"
#include "Svc/Subtopologies/ComCcsds/PingEntries.hpp"
#include "FprimeArtemisCore/Subtopologies/ArtemisDataProducts/PingEntries.hpp"
#include "Svc/Subtopologies/FileHandling/PingEntries.hpp"

// SubtopologyTopologyDefs includes
#include "Svc/Subtopologies/CdhCore/SubtopologyTopologyDefs.hpp"
#include "Svc/Subtopologies/ComCcsds/SubtopologyTopologyDefs.hpp"

// FC<->PC link: framing chain support
#include <Fw/Types/MallocAllocator.hpp>
#include <Svc/BufferManager/BufferManager.hpp>
#include <Svc/FrameAccumulator/FrameDetector/FprimeFrameDetector.hpp>

//! Sizing for the GenericHub link to the FlightController over /dev/serial0.
//! Must stay consistent with PcLink:: on the flight controller side.
namespace FcLink {
static constexpr FwSizeType bufferSize = 2048;      //!< must be >= the PosixUartDriver receive size passed to open()
static constexpr FwSizeType bufferCount = 6;        //!< hub transport buffers in the pool
static constexpr FwSizeType accumulatorSize = 1024; //!< frame reassembly ring capacity
static constexpr FwEnumStoreType bufferManagerId = 400;
}  // namespace FcLink
#include "FprimeArtemisCore/Subtopologies/ArtemisDataProducts/SubtopologyTopologyDefs.hpp"
#include "Svc/Subtopologies/FileHandling/SubtopologyTopologyDefs.hpp"

//ComCcsds Enum Includes
#include "Svc/Subtopologies/ComCcsds/Ports_ComPacketQueueEnumAc.hpp"
#include "Svc/Subtopologies/ComCcsds/Ports_ComBufferQueueEnumAc.hpp"

// Include autocoded FPP constants
#include "FprimeArtemisCore/Deployments/PayloadComputerDeployment/Top/FppConstantsAc.hpp"

/**
 * \brief required ping constants
 *
 * The topology autocoder requires a WARN and FATAL constant definition for each component that supports the health-ping
 * interface. These are expressed as enum constants placed in a namespace named for the component instance. These
 * are all placed in the PingEntries namespace.
 *
 * Each constant specifies how many missed pings are allowed before a WARNING_HI/FATAL event is triggered. In the
 * following example, the health component will emit a WARNING_HI event if the component instance cmdDisp does not
 * respond for 3 pings and will FATAL if responses are not received after a total of 5 pings.
 *
 * ```c++
 * namespace PingEntries {
 * namespace cmdDisp {
 *     enum { WARN = 3, FATAL = 5 };
 * }
 * }
 * ```
 */
namespace PingEntries {
    namespace PayloadComputerDeployment_rateGroup_1Hz {enum { WARN = 3, FATAL = 5 };}
    namespace PayloadComputerDeployment_rateGroup_0_5Hz {enum { WARN = 3, FATAL = 5 };}
    namespace PayloadComputerDeployment_rateGroup_0_25Hz {enum { WARN = 3, FATAL = 5 };}
    namespace PayloadComputerDeployment_cmdSeq {enum { WARN = 3, FATAL = 5 };}
    namespace PayloadComputerDeployment_payloadManager {enum { WARN = 3, FATAL = 5 };}
    namespace PayloadComputerDeployment_payloadDriverLepton {enum { WARN = 3, FATAL = 5 };}
}  // namespace PingEntries

// Definitions are placed within the same namespace as the FPP module that contains the topology.
namespace PayloadComputerDeployment {

/**
 * \brief required type definition to carry state
 *
 * The topology autocoder requires an object that carries state with the name `PayloadComputerDeployment::TopologyState`. Only the type
 * definition is required by the autocoder and the contents of this object are otherwise opaque to the autocoder. The
 * contents are entirely up to the definition of the project. This deployment uses subtopologies.
 */
struct TopologyState {
    const char* uartDevice; //!< UART device path for communication
    U32 baudRate;          //!< Baud rate for UART communication
    CdhCore::SubtopologyState cdhCore;           //!< Subtopology state for CdhCore
    ComCcsds::SubtopologyState comCcsds;         //!< Subtopology state for ComCcsds 
    ArtemisDataProducts::SubtopologyState dataProducts; //!< Subtopology state for ArtemisDataProducts
    FileHandling::SubtopologyState fileHandling; //!< Subtopology state for FileHandling
};

//! Allocator backing the link buffer manager and frame accumulator
extern Fw::MallocAllocator fcLinkAllocator;

namespace PingEntries = ::PingEntries;
}  // namespace PayloadComputerDeployment

// Deployment-supplied telemetry packet list accessor.
//
// The shared CdhCore tlmSend config calls this from configComponents(). Each
// deployment defines it in its own Topology.cpp, returning its own packet
// list, so the shared config stays deployment-agnostic.
#include "Svc/TlmPacketizer/TlmPacketizerTypes.hpp"
namespace FprimeArtemisConfig {
const Svc::TlmPacketizerPacketList& tlmPacketList();
}

#endif
