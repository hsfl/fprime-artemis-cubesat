# Components::MissionApp

`MissionApp` is the application tier of the flight controller and the mission
operator's interface. **An operator executes functions only through
`MissionApp`.** It owns the mission mode and drives managers through their port
contracts; it never touches hardware directly.

Managers also expose engineering commands (for example
`RpiPowerManager.SET_RPI_POWER`). Those exist for hardware bring-up and anomaly
response, not routine operations.

## Modes

| Mode | Meaning |
|---|---|
| `STANDBY` | Flight controller up, payload computer off. The mode at boot |
| `ENTERING_BASE` | Powering the payload computer and waiting for it to report `READY` |
| `BASE` | Payload computer powered and reporting in |

### Transitions

| From | Trigger | To | Action |
|---|---|---|---|
| any | boot (first `run` tick) | `STANDBY` | request power-off |
| `STANDBY` | `ENTER_BASE_MODE` | `ENTERING_BASE` | request power-on |
| `STANDBY` | `ENTER_BASE_MODE`, payload computer already `READY` | `BASE` | request power-on (no-op) |
| `STANDBY` | `ENTER_BASE_MODE`, power request fails | `STANDBY` | `BaseModeFailed(POWER_REQUEST_FAILED)` |
| `ENTERING_BASE` | `RpiState` → `READY` | `BASE` | |
| `ENTERING_BASE` | `RpiState` → `OFF` | `STANDBY` | `BaseModeFailed(POWERED_OFF)` |
| `ENTERING_BASE` | 120 s without `READY` | `STANDBY` | `BaseModeFailed(TIMEOUT)`, request power-off |
| `ENTERING_BASE` | `ENTER_BASE_MODE` | `ENTERING_BASE` | rejected, `BUSY` |
| `BASE` | `RpiState` → `OFF` | `STANDBY` | |
| `BASE` | `RpiState` → `BOOT` | `BASE` | none — systemd restarts the payload deployment |
| `BASE` | `ENTER_BASE_MODE` | `BASE` | none |
| any | `ENTER_STANDBY_MODE` | `STANDBY` | request power-off |

### Rules behind the table

**`STANDBY` means the payload computer is off, and that is enforced, not
assumed.** `ZephyrGpioDriver::open` configures the power pin as an output without
setting an initial level, so the pin's state at boot is whatever it reset to.
`MissionApp` requests power-off on its first `run` tick, after the GPIO driver
has been opened. `ENTER_STANDBY_MODE` requests power-off even from `STANDBY`,
because an engineering command may have powered the payload computer on.

**A flight controller reset may power-cycle the payload computer, and that is
not caused by `MissionApp`.** A cold reset (a reflash, `fatalHandler.RESTART`, or
any FATAL, since `FatalHandler` calls `sys_reboot(SYS_REBOOT_COLD)`) resets the
i.MX RT GPIO peripheral: the pin becomes an input, so the enable line floats
until F Prime starts. Then `ZephyrGpioDriver::open` sets the pin to output
without setting a level, and Zephyr's `gpio_mcux_igpio` driver writes only the
direction register for a plain `GPIO_OUTPUT`, so the pin drives the data
register's reset value, which is low. Both happen before `MissionApp`'s first
tick. Whether the rail actually drops during the float depends on the board's
pull on the enable line and on the PDU, which a bench test settles:

```bash
# with the payload computer up and heartbeating, reset the Teensy, then:
ssh artemis4@rpi-c3m-02.local 'uptime; systemctl show -p NRestarts fprime-artemis-core'
```

A fresh `uptime` means the reset power-cycled the payload computer. An abrupt
power loss can leave its filesystem unclean. Keeping it powered across a flight
controller reset would need a board-level pull-up on the enable line, plus
opening the pin with an explicit initial level rather than `ZephyrGpioDriver`'s
unset one.

**Downgrades that only match reality are automatic; upgrades are not.** If the
payload computer is powered off while in `BASE`, the mode drops to `STANDBY` —
nothing is actuated, the mode just stops claiming something false. Powering the
payload computer on with an engineering command while in `STANDBY` does *not*
enter `BASE`: an upgrade always goes through `ENTER_BASE_MODE`.

**`ENTER_BASE_MODE` responds when the transition starts, not when `BASE` is
reached.** A Raspberry Pi boot takes tens of seconds, and holding a command open
that long blocks the dispatcher's sequence tracking. Progress is reported through
`ModeChanged` events and `CurrentMode` telemetry.

## Ports

| Port | Kind | Connects to | Purpose |
|---|---|---|---|
| `rpiPowerRequestOut` | output | `RpiPowerManager.powerRequestIn` | request power on or off; returns `Fw.Success` |
| `rpiStateIn` | async input | `RpiPowerManager.stateOut` | every payload computer power-state change |
| `run` | async input | 1 Hz rate group | boot enforcement and the entry timeout |
| `pingIn` / `pingOut` | health | `CdhCore.$health` | liveness |

`run` must be on a **1 Hz** rate group: `BASE_ENTRY_TIMEOUT_TICKS` counts ticks
as seconds.

`rpiStateIn` is `async` so that a state report triggered by `MissionApp`'s own
power request is queued rather than re-entering `MissionApp` mid-handler.

`MissionApp` caches the last reported payload computer state. The cache is
accurate because `RpiPowerManager` starts `OFF` and reports every change. It is
how `ENTER_BASE_MODE` detects a payload computer that is already `READY` — a
redundant power-on changes nothing, so no state report would arrive.

## Commands

| Command | Purpose |
|---|---|
| `ENTER_BASE_MODE` | power the payload computer and bring the spacecraft to `BASE` |
| `ENTER_STANDBY_MODE` | power the payload computer off and return to `STANDBY` |

## Events and telemetry

| Name | Kind | Meaning |
|---|---|---|
| `ModeChanged(mode)` | event, activity high | the mode changed |
| `BaseModeFailed(reason)` | event, warning high | entry failed and the mode returned to `STANDBY` |
| `ModeCommandRejected(mode)` | event, warning low | a mode command arrived during a transition |
| `CurrentMode` | telemetry | the current mode |

## Not yet implemented

- **Link check during `ENTER_BASE_MODE`.** Once the UART link check exists,
  entry will require a passing check after `READY` before entering `BASE`.
- **Payload preparation.** Needs payload components on the payload computer.
- **Collection modes** (`COLLECTION_PENDING`, `COLLECTING`, `SCIENCE_READY`,
  `DOWNLINKING`) and the reaction to a payload computer exit during a
  collection.
