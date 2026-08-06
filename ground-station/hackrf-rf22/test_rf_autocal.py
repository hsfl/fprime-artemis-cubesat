#!/usr/bin/env python3

from __future__ import annotations

import unittest

from rf_autocal import (
    RF_AMP_SEARCH_STATES,
    RX_CANDIDATES,
    TX_CANDIDATES,
    RxWindow,
    build_fprime_ping_command,
    select_rx_window,
)


class AutomaticGainPolicyTests(unittest.TestCase):
    def test_rf_amp_is_strictly_second_stage(self) -> None:
        self.assertEqual(RF_AMP_SEARCH_STATES, (False, True))
        rx_order = tuple(
            (amp, candidate)
            for amp in RF_AMP_SEARCH_STATES
            for candidate in RX_CANDIDATES
        )
        tx_order = tuple(
            (amp, candidate)
            for amp in RF_AMP_SEARCH_STATES
            for candidate in TX_CANDIDATES
        )
        self.assertEqual(rx_order[len(RX_CANDIDATES)], (True, (0, 0)))
        self.assertEqual(tx_order[len(TX_CANDIDATES)], (True, 0))

    def test_tx_search_is_increasing_and_covers_the_hardware_range(self) -> None:
        self.assertEqual(tuple(sorted(set(TX_CANDIDATES))), TX_CANDIDATES)
        self.assertEqual(TX_CANDIDATES[0], 0)
        self.assertEqual(TX_CANDIDATES[-1], 47)

    def test_rx_search_starts_at_minimum_and_stays_bounded(self) -> None:
        self.assertEqual(RX_CANDIDATES[0], (0, 0))
        self.assertEqual(max(lna for lna, _vga in RX_CANDIDATES), 40)
        self.assertEqual(max(vga for _lna, vga in RX_CANDIDATES), 62)

    def test_ping_builder_matches_current_dictionary_capture(self) -> None:
        self.assertEqual(
            build_fprime_ping_command(680701, 0),
            bytes.fromhex(
                "20 44 04 16 00 10 00 c0 00 00 09 00 00 10 00 60 "
                "01 00 0a 62 fd 92 0d"
            ),
        )

    def test_first_crc_valid_clip_free_drop_free_window_wins(self) -> None:
        windows = [
            RxWindow(0, 0, valid_frames=0, clipped_samples=0, dropped_blocks=0),
            RxWindow(0, 4, valid_frames=1, clipped_samples=2, dropped_blocks=0),
            RxWindow(0, 8, valid_frames=2, clipped_samples=0, dropped_blocks=0),
            RxWindow(8, 0, valid_frames=4, clipped_samples=0, dropped_blocks=0),
        ]
        self.assertEqual(select_rx_window(windows), windows[2])

    def test_no_valid_window_fails_closed(self) -> None:
        windows = [RxWindow(0, 0, 0, 0, 0), RxWindow(0, 4, 2, 0, 1)]
        self.assertIsNone(select_rx_window(windows))


if __name__ == "__main__":
    unittest.main()
