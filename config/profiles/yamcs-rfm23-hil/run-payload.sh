#!/bin/sh
set -eu
: "${ARTEMIS_PAYLOAD_ROOT:?Set ARTEMIS_PAYLOAD_ROOT in payload.env}"
: "${ARTEMIS_FC_UART:?Set ARTEMIS_FC_UART in payload.env}"
cd "$ARTEMIS_PAYLOAD_ROOT"
# Presence of ARTEMIS_TWIN, including "0", selects the recorded twin guard.
unset ARTEMIS_TWIN C3M_LEPTON_SAMPLE_CSV
export LEPTON_CAMERA_BACKEND=uvc
exec "$ARTEMIS_PAYLOAD_ROOT/bin/payload-computer" -d "$ARTEMIS_FC_UART" -b 115200
