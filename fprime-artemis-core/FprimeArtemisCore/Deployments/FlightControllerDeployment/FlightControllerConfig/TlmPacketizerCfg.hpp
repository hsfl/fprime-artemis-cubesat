// \copyright
// Copyright 2009-2015, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.

#ifndef SVC_TLMPACKETIZER_TLMPACKETIZERCOMPONENTIMPLCFG_HPP_
#define SVC_TLMPACKETIZER_TLMPACKETIZERCOMPONENTIMPLCFG_HPP_

#include <Fw/FPrimeBasicTypes.hpp>
namespace Svc {
//! Maximum number of packets
// FC packets are split to fit the 104-byte com buffer and 128-byte TM frames.
// Allow room for the radio channels and the separately packetized version data.
static const FwChanIdType MAX_PACKETIZER_PACKETS = 40;

//! Maximum number of channels that the packetizer can handle. Must be >= number of non-omitted channels
static const FwChanIdType MAX_PACKETIZER_CHANNELS = 128;

//! Maximum number of missing channels to track and report
static const FwChanIdType TLMPACKETIZER_MAX_MISSING_TLM_CHECK = 1;
}  // namespace Svc

#endif /* SVC_TLMPACKETIZER_TLMPACKETIZERCOMPONENTIMPLCFG_HPP_ */
