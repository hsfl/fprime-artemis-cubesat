#ifndef ARTEMIS_TEENSY_FIRMWARE_PROFILE_HPP
#define ARTEMIS_TEENSY_FIRMWARE_PROFILE_HPP

// The satellite bridge defaults to the CDR UART/PDU-only profile. The legacy
// RFM23BP bridge remains available only when the build explicitly selects it.
#if !defined(ARTEMIS_CDR_LEGACY_RF) && !defined(ARTEMIS_CDR_NO_RADIO)
#define ARTEMIS_CDR_NO_RADIO 1
#endif

#endif
