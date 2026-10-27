# Firmware verification

Run host checks from the repository root after generating the Release DSDL headers.
Tests compile production implementations with hardware/transport doubles where
required. They do not establish physical motor behavior or MCU timing.

| Command | Coverage |
| --- | --- |
| `python3 tests/temperature_conversion.py` | Factory temperature conversion, invalid ADC endpoints/reference/calibration and uninitialized data. |
| `python3 tests/thermal_protection.py` | Production bridge gate, all calibration cancellation boundaries and Heartbeat fields with hardware doubles. |
| `python3 tests/bus_current.py` | Applied PWM power estimate, saturation, sampled-interval alignment, signed filtering, consumption/regeneration, zero bus and OFF/bootstrap. |
| `python3 tests/servo_input.py` | Standalone generators and abstract lifecycle: filter, ramp, trapezoidal/triangular profiles, reversal, overspeed, completion, invalid inputs and retargeting. |
| `python3 tests/servo_control.py` | Production FOC command handling and non-mutating staging, PID, DIRECT/MIT, optional generator, indices, rated/runtime torque clipping including R36/R10, ang_dir, 1:8/1:5 scheduling, interrupt-safe publication and POLY time horizon. |
| `python3 tests/rated_limits.py` | Production BLDC rating priority, effective current/torque limits, larger stored user limits, target validation and stall entry/exit. |
| `python3 tests/parameter_interfaces.py` | Production configuration, Serial controller and Cyphal register adapter: 60 registers, mode persistence/OFF gate, session reset on OFF/CONFIG/thermal, thermal recovery, shared command dispatch and FIFO framing. |
| `python3 tests/refactor_contracts.py` | Shared C/C++ configuration ABI, storage addresses, independent schema validation, staging, VBBoot baud/config loading, profiling counters and README defaults. |
| `python3 tests/mit_control.py` | Upstream CAN Servo handler, UART MIT/Servo adapters and historical serializer layouts. This does not imply legacy CAN MIT compatibility. |
| `python3 tests/sync_wire.py` | Generated command2, PREPARE/RUN sync2 and status1 serializers against independent wire vectors (requires Release DSDL headers). |
| `python3 tests/kalman_filter.py` | Observer initialization, instance isolation, wrap/tracking and shaft angle from the encoder before filtering. |
| `python3 tests/cyphal_tx.py` | Bounded/full servicing, TX expiry, available/full hardware FIFO, disabled TX event storage and RX overwrite wrap; native and bundled providers. |
| `python3 tests/cyphal_rx.py` | Critical FIFO priority, bounded processing on a continuously replenished bus, single-frame API and HAL receive failures; native and bundled providers. |
| `python3 tests/cyphal_telemetry.py` | Production 200 Hz scheduler, no catch-up burst, timer wrap, extended state, zero current/torque while OFF, Kelvin and stator telemetry filtering. |
| `python3 tests/synchronized_control.py` | Production command2/sync2 handlers and IRQ with actual FocCycleSync; master gate, truncated packets, PREPARE/RUN, OFF/fault/watchdog, serialized CAN reassembly across Classic/FD MTUs. Motor hardware is doubled. |
| `python3 tests/cyphal_subjects.py` | Execute production subscription/filter setup: command2 uses 2300 + node_id, disjoint from legacy subjects for all IDs 1–127, sync and Servo preserved. |
| `VBDRIVE_CLIENT_REPO=/path/to/client python3 tests/client_firmware_wire.py` | Optional cross-repository test: actual client codec and vendored types → actual libcanard → production firmware command/sync IRQ → client ACK and state decoders; firmware-serialized GetInfo → client decoder. Hardware doubled. Records codec SHA256; without the environment variable it explicitly skips. |
| `python3 tests/cyphal_lifecycle.py` | Calibration transport pause, arena reset and flushing both RX FIFOs before resumed command processing. |
| `python3 tests/eeprom_migration.py` | Read-only startup guard rejects legacy/unknown/partial records without writes; a genuinely blank used EEPROM area and valid current layout pass. No automatic legacy migration is performed. |
| `python3 tests/boot_request.py` | Production application reboot handoff and VBBoot unpacking for node ID and all valid nominal/data CAN baud combinations. |
| `python3 tests/firmware_identity.py` | CRC-64/ECMA-182 known vector and real generated GetInfo image CRC serialization; does not verify deployed hardware image identity. |
| `python3 tests/control_counters.py` | Production counters-only recording, accepted/rejected totals, one-second timestamps and ring wrap. |
| `python3 tests/pwm_foc_sync.py` | TIM1 compare IRQ gating, PWM/FOC phase configuration, shaft encoder ordering and cadence in ON/OFF/bootstrap. |
| `python3 tests/millis_clock.py` | Millisecond polling/IRQ interleaving, single consumption of a timer tick, interrupt mask preservation, microsecond snapshot during counter wrap and a 64-bit epoch beyond 49 days. |
| `python3 tests/cyphal_arena.py` | Actual libcanard/O1Heap: release allocations, overwrite/reset shared arena, retain five subscription listeners and resume RX/TX. |

Build ordinary Release, CyphalProfile (`VBDRIVE_CYPHAL_PROFILE=ON`),
FOCProfile (`VBDRIVE_FOC_PROFILE=ON`) and Debug variants. Inspect linker Flash/RAM
usage. Profiling-disabled builds omit profiling counters; protocol diagnostics remain enabled.
Configure both the application and VBBoot with the same `VB_CONFIG_ADDRESS`;
the parent build forwards it automatically. Configuration must not overlap
calibration or inductive sensor state.

## Hardware checks

Hardware scripts require explicit authorization for their effects and a verified
physical target, node ID and ST-Link serial. Preserve actual settings before
writes; complete testing with those settings restored and the driver disabled.
Allow firmware initialization to finish before starting a Serial test.

- `serial_hardware.py --port PORT --revision REV --output FILE` checks the live
  catalog, readonly/invalid requests, CONFIG/EXIT/RESET/SAVE/APPLY, runtime enable,
  logging and STOP. It reboots and enables/disables the driver.
- `serial_stress.py --port PORT --output FILE [--motion] --rate 100` exercises
  concurrent queries and state logging; `--motion` enables bounded targets.
- `cyphal_control_stream.py` sends timed MIT/Servo targets. Record both sender
  pacing and MCU arrival/handler intervals to distinguish delivery bursts from
  firmware execution time.

The legacy CAN stream script is not compatible with this integration's command2/sync2
ports. Do not use it as a hardware acceptance test for the new protocol. EEPROM,
bootloader compatibility and real GetInfo image CRC remain delivery gates.
- Compare ordinary and profiled firmware under equal parameters, including all
  generator modes with zero/nonzero I and 1 kHz command/State traffic. Inspect
  rolling FOC frequency and reference/integral divider ratios separately.

Persistent configuration at address zero remains compatible across these builds.
Nonzero-address firmware must be provisioned at its selected address; changing the
build option does not relocate EEPROM data.

For a low-overhead measurement build, enable `VBDRIVE_COUNTERS_ONLY=ON` in Release
with FOC/Cyphal profiling disabled. It increments FOC, command and State counters
and records cumulative snapshots with MCU timestamps about once per second in
`counter_windows`. Counter differences divided by actual timestamp differences
give rates; select windows entirely inside the active input stream. No DWT reads,
handler histograms or RX-arrival profiling IRQ are enabled in this mode.
