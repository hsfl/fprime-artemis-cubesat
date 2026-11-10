# N2 local mission-ops quickstart

Run the current **software-only synthetic pulse demo** on one computer. This does not connect to the payload, spacecraft, SatNOGS, or RF hardware.

For first-time setup, use [Local setup](LOCAL_SETUP.md). For errors, see [Troubleshooting](TROUBLESHOOTING.md).

For the two-pass FlatSat rehearsal on the deployed station, follow the [FSR Yamcs walkthrough](walkthroughs/FSR_MISSION_OPS.md).

## Start a session

From the `neutron_2` repository root, start one stack and leave its terminal open:

```sh
./tools/yamcs/run_pulses.sh
```

Open [Yamcs](http://127.0.0.1:8090), then select instance `fprime-project` and processor `realtime`.

## Collect and verify

In Yamcs, open **Commanding → Send a command** and select `ArtemisRpiTeensyDeployment.scienceApp.VERIFIED_COLLECT_AND_DOWNLINK`. Enter:

| Argument | Value |
| --- | --- |
| `verificationToken` | An unused decimal integer from `1` through `4294967295`, never reused across sessions. Example: `26101401` if it is unused. Keep a note of tokens already used. |
| `durationSeconds` | `5` |

Send one collection at a time. In that command's history/details, confirm `PayloadAccepted`, `PayloadTerminal`, `StorageCommitted`, `SenderComplete`, `GroundReceipt`, and `CommandComplete` each report `OK`. Count delivery as successful only when Yamcs shows **SUCCESS** and `CommandComplete_Status=OK`; a released command or successful sender stage alone does not confirm ground receipt.

To inspect and save the verified raw product, open the [local ground science viewer](http://127.0.0.1:8063). Match the product and transfer IDs to the Yamcs receipt, then use **Download raw Yamcs object**. The pulse plots are simulated ADC data, not calibrated detector science. See the [pulse walkthrough](walkthroughs/PULSE_SCIENCE.md) for screenshots and plot details.

## Stop

Press **Ctrl+C** in the terminal running `run_pulses.sh`, or stop the recorded local stack from a second terminal:

```sh
./tools/yamcs/stop.sh
```

Do not start another stack until the first one has stopped. No daily rebuild or data-directory cleanup is required.

## Windows note

The current stack binds to `127.0.0.1` on the computer running it. A browser on another computer cannot reach it over the network with this configuration. Running the full F′ emulation under WSL is not yet validated; see [Windows/WSL status](../development/WINDOWS_WSL.md).
