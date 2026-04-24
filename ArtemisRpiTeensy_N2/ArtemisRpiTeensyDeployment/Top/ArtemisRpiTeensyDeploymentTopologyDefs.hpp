// ======================================================================
// \title  ArtemisRpiTeensyDeploymentTopologyDefs.hpp
// \brief required header file containing the required definitions for the topology autocoder
//
// ======================================================================
#ifndef ARTEMISRPITEENSYDEPLOYMENT_ARTEMISRPITEENSYDEPLOYMENTTOPOLOGYDEFS_HPP
#define ARTEMISRPITEENSYDEPLOYMENT_ARTEMISRPITEENSYDEPLOYMENTTOPOLOGYDEFS_HPP

// Subtopology PingEntries includes
#include "Svc/Subtopologies/CdhCore/PingEntries.hpp"
#include "Svc/Subtopologies/ComCcsds/PingEntries.hpp"
#include "Svc/Subtopologies/DataProducts/PingEntries.hpp"
#include "Svc/Subtopologies/FileHandling/PingEntries.hpp"

// SubtopologyTopologyDefs includes
#include "Svc/Subtopologies/CdhCore/SubtopologyTopologyDefs.hpp"
#include "Svc/Subtopologies/ComCcsds/SubtopologyTopologyDefs.hpp"
#include "Svc/Subtopologies/DataProducts/SubtopologyTopologyDefs.hpp"
#include "Svc/Subtopologies/FileHandling/SubtopologyTopologyDefs.hpp"

//ComCcsds Enum Includes
#include "Svc/Subtopologies/ComCcsds/Ports_ComPacketQueueEnumAc.hpp"
#include "Svc/Subtopologies/ComCcsds/Ports_ComBufferQueueEnumAc.hpp"

// Include autocoded FPP constants
#include "ArtemisRpiTeensyDeployment/Top/FppConstantsAc.hpp"

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
    namespace ArtemisRpiTeensyDeployment_rateGroup1 {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_rateGroup2 {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_rateGroup3 {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_cmdSeq {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_teensyTransportService {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_missionManager {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_scienceManager {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_sohManager {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_commsManager {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_epsService {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_payloadService {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_adcsService {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_gpsService {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_storageService {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_epsAdapterArtemis {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_payloadAdapterN1Legacy {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_adcsAdapterD2S2 {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_gpsAdapterArtemis {enum { WARN = 3, FATAL = 5 };}
    namespace ArtemisRpiTeensyDeployment_commsAdapterTeensyRfm23 {enum { WARN = 3, FATAL = 5 };}
}  // namespace PingEntries

// Definitions are placed within the same namespace as the FPP module that contains the topology.
namespace ArtemisRpiTeensyDeployment {

/**
 * \brief required type definition to carry state
 *
 * The topology autocoder requires an object that carries state with the name `ArtemisRpiTeensyDeployment::TopologyState`. Only the type
 * definition is required by the autocoder and the contents of this object are otherwise opaque to the autocoder. The
 * contents are entirely up to the definition of the project. This deployment uses subtopologies.
 */
struct TopologyState {
    const char* uartDevice; //!< Linux UART device path (e.g. /dev/serial0)
    CdhCore::SubtopologyState cdhCore;           //!< Subtopology state for CdhCore
    ComCcsds::SubtopologyState comCcsds;         //!< Subtopology state for ComCcsds 
    DataProducts::SubtopologyState dataProducts; //!< Subtopology state for DataProducts
    FileHandling::SubtopologyState fileHandling; //!< Subtopology state for FileHandling
};

namespace PingEntries = ::PingEntries;
}  // namespace ArtemisRpiTeensyDeployment

#endif
