// \copyright
// Copyright 2009-2015, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.

#ifndef SVC_TLMPACKETIZER_TLMPACKETIZERCOMPONENTIMPLCFG_HPP_
#define SVC_TLMPACKETIZER_TLMPACKETIZERCOMPONENTIMPLCFG_HPP_

#include <Fw/FPrimeBasicTypes.hpp>
namespace Svc {
//! Maximum number of packets
// Overridden for FlightControllerDeployment: its packet set defines 21 packets
// across 93 non-omitted channel entries, which overflows the project-wide
// values of 5/40. Sized with a small margin rather than copying
// PayloadComputerDeployment's 32/128, because this target is the Teensy 4.1.
static const FwChanIdType MAX_PACKETIZER_PACKETS = 32;

//! Maximum number of channels that the packetizer can handle. Must be >= number of non-omitted channels
static const FwChanIdType MAX_PACKETIZER_CHANNELS = 96;

//! Maximum number of missing channels to track and report
static const FwChanIdType TLMPACKETIZER_MAX_MISSING_TLM_CHECK = 1;
}  // namespace Svc

#endif /* SVC_TLMPACKETIZER_TLMPACKETIZERCOMPONENTIMPLCFG_HPP_ */
