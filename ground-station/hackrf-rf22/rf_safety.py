"""Fail-closed RF transmit safety policy for the HackRF ground adapter."""

from __future__ import annotations


DEFAULT_RF_PATH_LABEL = "unqualified"

# Known C3M antenna/profile label plus conservative device bootstrap values.
# Automatic calibration replaces the bootstrap gains before GDS starts. The
# names remain for legacy fixed-run proof compatibility.
BASELINE_RF_PATH_LABEL = "pobady-433-3dbi-rg174-3m-magnetic-base"
BASELINE_TX_MODE = "ack"
BASELINE_TX_GAIN_DB = 16
BASELINE_RX_LNA_GAIN_DB = 0
BASELINE_RX_VGA_GAIN_DB = 8
BASELINE_TX_LEADING_MS = 100.0


class TxSafetyError(ValueError):
    """Raised when transmit was requested without the required interlocks."""


def validate_tx_request(
    *,
    enable_tx: bool,
    tx_gain: int,
    tx_safety_confirmed: bool,
    allow_elevated_tx_gain: bool,
    rf_path_label: str,
) -> str:
    """Validate an intentional TX request and return its normalized path label.

    Receive-only operation is always allowed and callers should force the
    configured TX gain to zero.  Transmit operation requires an attached
    antenna/rated load acknowledgement and a non-default RF-path label.  Any
    IF gain above zero requires a second explicit acknowledgement so a stale
    gain from a prior antenna/attenuator geometry cannot be reused silently.
    """

    if not 0 <= tx_gain <= 47:
        raise TxSafetyError("TX gain must be between 0 and 47 dB")

    label = rf_path_label.strip()
    if not enable_tx:
        return label or DEFAULT_RF_PATH_LABEL

    if not tx_safety_confirmed:
        raise TxSafetyError(
            "TX is fail-closed: add --tx-safety-confirmed only after an "
            "antenna or rated 50-ohm load is attached and the RF path is safe"
        )
    if not label or label.casefold() == DEFAULT_RF_PATH_LABEL:
        raise TxSafetyError(
            "TX requires a descriptive --rf-path-label for this physical setup"
        )
    if tx_gain > 0 and not allow_elevated_tx_gain:
        raise TxSafetyError(
            "TX gain above 0 dB requires --allow-elevated-tx-gain; begin a "
            "changed RF-path qualification at gain 0"
        )
    return label


__all__ = [
    "BASELINE_RF_PATH_LABEL",
    "BASELINE_RX_LNA_GAIN_DB",
    "BASELINE_RX_VGA_GAIN_DB",
    "BASELINE_TX_GAIN_DB",
    "BASELINE_TX_LEADING_MS",
    "BASELINE_TX_MODE",
    "DEFAULT_RF_PATH_LABEL",
    "TxSafetyError",
    "validate_tx_request",
]
