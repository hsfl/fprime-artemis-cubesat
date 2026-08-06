#!/usr/bin/env python3

from __future__ import annotations

import unittest

from rf_safety import TxSafetyError, validate_tx_request


class TxSafetyTests(unittest.TestCase):
    def test_receive_only_is_allowed_with_unqualified_label(self) -> None:
        self.assertEqual(
            validate_tx_request(
                enable_tx=False,
                tx_gain=47,
                tx_safety_confirmed=False,
                allow_elevated_tx_gain=False,
                rf_path_label="",
            ),
            "unqualified",
        )

    def test_tx_requires_path_confirmation_and_label(self) -> None:
        with self.assertRaises(TxSafetyError):
            validate_tx_request(
                enable_tx=True,
                tx_gain=0,
                tx_safety_confirmed=False,
                allow_elevated_tx_gain=False,
                rf_path_label="monopole-no-attenuator",
            )
        with self.assertRaises(TxSafetyError):
            validate_tx_request(
                enable_tx=True,
                tx_gain=0,
                tx_safety_confirmed=True,
                allow_elevated_tx_gain=False,
                rf_path_label="unqualified",
            )

    def test_gain_zero_is_the_only_initial_tx_setting(self) -> None:
        self.assertEqual(
            validate_tx_request(
                enable_tx=True,
                tx_gain=0,
                tx_safety_confirmed=True,
                allow_elevated_tx_gain=False,
                rf_path_label="monopole-no-attenuator",
            ),
            "monopole-no-attenuator",
        )
        with self.assertRaises(TxSafetyError):
            validate_tx_request(
                enable_tx=True,
                tx_gain=1,
                tx_safety_confirmed=True,
                allow_elevated_tx_gain=False,
                rf_path_label="monopole-no-attenuator",
            )

    def test_elevated_gain_requires_second_confirmation(self) -> None:
        self.assertEqual(
            validate_tx_request(
                enable_tx=True,
                tx_gain=12,
                tx_safety_confirmed=True,
                allow_elevated_tx_gain=True,
                rf_path_label="measured-step-12",
            ),
            "measured-step-12",
        )


if __name__ == "__main__":
    unittest.main()
