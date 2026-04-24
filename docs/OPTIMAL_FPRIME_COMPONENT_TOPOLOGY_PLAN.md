# Optimal F' Component, Topology, and Deployment Plan

## Purpose

This document defines the recommended F' software structure for the Neutron 2 FlatSat demo.

The design goal is:

- support the current Artemis-based demo
- keep components reusable for the final Neutron 2 satellite
- separate mission logic from hardware-specific implementation

## Design Rules

1. Keep one main flight/demo deployment for now.
2. Put spacecraft behavior in reusable mission and service components.
3. Hide Artemis-vs-Neutron hardware differences behind adapter or HAL layers.
4. Reuse standard F' services for commands, telemetry, files, and data products.
5. Do not build a demo-only monolith that mixes mission logic and hardware details.

## Recommended Software Layers

### 1. Standard F' service layer

Keep these as the deployment backbone:

- `CdhCore`
- `ComCcsds`
- `DataProducts`
- `FileHandling`
- `ChronoTime`
- `LinuxTimer`
- `LinuxUartDriver`
- rate groups

### 2. Mission layer

These components express spacecraft behavior and should remain reusable later.

- `MissionManager`
  - owns spacecraft mode logic
  - initial modes:
    - `BaseMode`
    - `DataCollectionPending`
    - `Collecting`
    - `ScienceTx`
- `ScienceManager`
  - coordinates payload collection, result packaging, and science downlink requests
- `SoHManager`
  - aggregates judge-facing and operator-facing status-of-health
- `CommsManager`
  - tracks comm state and link-level operational status at the mission layer

### 3. Reusable subsystem service layer

These components expose stable spacecraft-facing interfaces, not raw hardware details.

- `EpsService`
- `PayloadService`
- `AdcsService`
- `GpsService`
- `StorageService`
- `TeensyTransportService`

These services should expose commands, events, telemetry, and ports in subsystem terms such as:

- start payload collection
- get EPS health
- get ADCS state
- get GPS fix
- request science product storage/downlink
- report link counters and link health

### 4. Hardware adapter / HAL layer

These are the replaceable hardware-specific implementations.

- `EpsAdapter_Artemis`
- `PayloadAdapter_N1Legacy`
- `AdcsAdapter_D2S2`
- `GpsAdapter_Artemis`
- `CommsAdapter_TeensyRfm23`

Later replacements may include:

- `CommsAdapter_Satnogs`
- `PayloadAdapter_N2`
- `EpsAdapter_N2`
- `AdcsAdapter_Flight`

## Recommended Custom Components

### Mission layer

- `MissionManager`
- `ScienceManager`
- `SoHManager`
- `CommsManager`

### Service layer

- `TeensyTransportService`
- `EpsService`
- `PayloadService`
- `AdcsService`
- `GpsService`
- `StorageService`

### Adapter / HAL layer

- `EpsAdapter_Artemis`
- `PayloadAdapter_N1Legacy`
- `AdcsAdapter_D2S2`
- `GpsAdapter_Artemis`
- `CommsAdapter_TeensyRfm23`

## Topology Recommendation

Use one main deployment and one main topology for the current demo:

- deployment:
  - `ArtemisRpiTeensyDeployment`
- topology:
  - the main FlatSat demo topology inside that deployment

This topology should wire:

1. the standard F' backbone
2. the mission components
3. the reusable subsystem services
4. the hardware adapters behind those services

## Recommended Logical Wiring

```text
Ground <-> RF/Teensy/UART <-> LinuxUartDriver <-> ComCcsds <-> CdhCore
                                                        |
                                                        +-> MissionManager
                                                        +-> ScienceManager
                                                        +-> SoHManager
                                                        +-> CommsManager
                                                        +-> TeensyTransportService
                                                        +-> EpsService
                                                        +-> PayloadService
                                                        +-> AdcsService
                                                        +-> GpsService
                                                        +-> StorageService
                                                        +-> DataProducts / FileHandling
```

## Responsibility Split

### `MissionManager`

- owns spacecraft mode transitions
- accepts operator commands
- decides when the system is in `BaseMode`, collecting, or downlinking science

### `ScienceManager`

- starts payload collection
- tracks collection completion
- hands science results to storage/downlink path

### `SoHManager`

- gathers health status from EPS, comms, payload, ADCS, and GPS services
- publishes simplified health telemetry for the demo

### `CommsManager`

- provides mission-level comms state
- monitors transport health and link readiness

### `TeensyTransportService`

- owns UART bridge observability and transport counters
- sits above the concrete UART/radio hardware details

### `EpsService`, `PayloadService`, `AdcsService`, `GpsService`

- expose stable subsystem behavior to the rest of the flight software
- hide the current Artemis/demo hardware choices from mission logic

### `StorageService`

- uses `DataProducts` and `FileHandling`
- should not reimplement file-transfer behavior already provided by F'

## Demo Flow This Topology Must Support

1. Boot into `BaseMode`.
2. Downlink `SOH` telemetry over the active comms path.
3. Accept a command to schedule data collection after a short delay.
4. Trigger payload collection.
5. Store or package the science result.
6. Transition into science-data downlink.
7. Present the result on the ground side.

## What To Avoid

Do not:

- make one giant `DemoManager` that directly talks to every hardware detail
- make Artemis-specific hardware assumptions part of mission-layer components
- create a custom `Files` mission component when F' `DataProducts` and `FileHandling` already exist
- create a separate `S&M` software component for the MVP unless there is real software behavior to implement
- split the deployment into multiple competing demo topologies right now

## Current Repo To Target Direction

The current repo already has a good service backbone:

- `CdhCore`
- `ComCcsds`
- `DataProducts`
- `FileHandling`
- UART comm driver

The previous app-layer gap was that the deployment only had:

- `TeensyLink`
- `PingResponder`

That transition is now underway. The current deployment includes `TeensyTransportService` and additional mission/service components; keep using this direction:

- keep `TeensyTransportService` as the active transport-service component
- replace `PingResponder` with real mission-layer and subsystem-service components
- add service and adapter structure before deeper hardware expansion

## Recommended Near-Term Build Order

1. `MissionManager`
2. `TeensyTransportService`
3. `PayloadService`
4. `EpsService`
5. `AdcsService`
6. `GpsService`
7. `ScienceManager`
8. `SoHManager`

## Summary

The optimal F' design for this project is:

- one main deployment
- reusable mission components
- reusable subsystem service components
- hardware-specific adapters beneath them
- standard F' services reused wherever possible

This gives the team a demo-ready architecture now without locking the final Neutron 2 flight software to Artemis-specific hardware decisions.
