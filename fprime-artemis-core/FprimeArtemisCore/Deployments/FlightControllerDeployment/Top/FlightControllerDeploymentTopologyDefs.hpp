// ======================================================================
// \title  FlightControllerDeploymentTopologyDefs.hpp
// \brief required header file containing the required definitions for the topology autocoder
//
// ======================================================================
#ifndef FLIGHTCONTROLLERDEPLOYMENT_FLIGHTCONTROLLERDEPLOYMENTTOPOLOGYDEFS_HPP
#define FLIGHTCONTROLLERDEPLOYMENT_FLIGHTCONTROLLERDEPLOYMENTTOPOLOGYDEFS_HPP

// Subtopology PingEntries includes
#include "Svc/Subtopologies/CdhCore/PingEntries.hpp"
#include "Svc/Subtopologies/ComCcsds/PingEntries.hpp"

// SubtopologyTopologyDefs includes
#include "Svc/Subtopologies/CdhCore/SubtopologyTopologyDefs.hpp"
#include "Svc/Subtopologies/ComCcsds/SubtopologyTopologyDefs.hpp"

// FC↔PC link: framing chain support
#include <Fw/Types/MallocAllocator.hpp>
#include <Svc/BufferManager/BufferManager.hpp>
#include <Svc/FrameAccumulator/FrameDetector/FprimeFrameDetector.hpp>

//! Sizing for the GenericHub link to the PayloadComputer over lpuart4.
//! Buffers must hold a whole pcLinkHub message plus its F Prime frame.
namespace PcLink {
static constexpr FwSizeType bufferSize = 512;       //!< >= ZephyrUartDriver SERIAL_BUFFER_SIZE (64); the Pi side needs 2048 for its driver
static constexpr FwSizeType bufferCount = 6;        //!< pcLinkHub transport buffers in the pool
static constexpr FwSizeType accumulatorSize = 1024; //!< frame reassembly ring capacity
static constexpr FwEnumStoreType bufferManagerId = 400;
}  // namespace PcLink

//ComCcsds Enum Includes
#include "Svc/Subtopologies/ComCcsds/Ports_ComPacketQueueEnumAc.hpp"
#include "Svc/Subtopologies/ComCcsds/Ports_ComBufferQueueEnumAc.hpp"

// Zephyr device handle types for the UART and GPIO drivers
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

// Include autocoded FPP constants
#include "FprimeArtemisCore/Deployments/FlightControllerDeployment/Top/FppConstantsAc.hpp"

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
    namespace FprimeArtemisCore_rateGroup_1Hz {enum { WARN = 3, FATAL = 5 };}
    namespace FprimeArtemisCore_rateGroup_0_5Hz {enum { WARN = 3, FATAL = 5 };}
    namespace FprimeArtemisCore_rateGroup_0_25Hz {enum { WARN = 3, FATAL = 5 };}
    namespace FprimeArtemisCore_cmdSeq {enum { WARN = 3, FATAL = 5 };}
}  // namespace PingEntries

// Definitions are placed within the same namespace as the FPP module that contains the topology.
namespace FprimeArtemisCore {

/**
 * \brief required type definition to carry state
 *
 * The topology autocoder requires an object that carries state with the name `FprimeArtemisCore::TopologyState`. Only the type
 * definition is required by the autocoder and the contents of this object are otherwise opaque to the autocoder. The
 * contents are entirely up to the definition of the project. This deployment uses subtopologies.
 */
struct TopologyState {
    const struct device* uartDevice; //!< Zephyr UART device handle for the ground link
    U32 baudRate;          //!< Baud rate for the ground link
    const struct device* pcLinkDevice; //!< Zephyr UART device handle for the PayloadComputer pcLinkHub link
    U32 pcLinkBaud;       //!< Baud rate for the pcLinkHub link
    CdhCore::SubtopologyState cdhCore;           //!< Subtopology state for CdhCore
    ComCcsds::SubtopologyState comCcsds;         //!< Subtopology state for ComCcsds 
};

//! Allocator backing the pcLinkHub buffer manager and frame accumulator
extern Fw::MallocAllocator pcLinkAllocator;

namespace PingEntries = ::PingEntries;
}  // namespace FprimeArtemisCore

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
