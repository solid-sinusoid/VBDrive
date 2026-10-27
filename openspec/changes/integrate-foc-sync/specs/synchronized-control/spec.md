## ADDED Requirements

### Requirement: Phased command contract

The application SHALL use command.2.0 on 2300+node_id, sync.2.0 on 3809,
command_status.1.0 on 3810 and extended state_simple.1.0 on 3811. COMMAND and
SYNC SHALL accept only master node 42. Truncated sealed commands/markers SHALL
be rejected instead of implicitly zero-extended. Legacy publishers on the old
2107+node_id subject SHALL NOT reach the new command.2.0 subscription. A sender
deliberately transmitting old payloads on the new subject remains indistinguishable
by length and is outside the permitted client contract.
These ports SHALL NOT provide a legacy MIT/command.1.0 compatibility mode. Standard Servo CAN, upstream Serial
Servo and generators SHALL remain available. A successful Servo command SHALL
clear any pending synchronized session before returning.

#### Scenario: A target waits for RUN

- **WHEN** a valid command is received
- **THEN** STAGED is published without changing the target or PI gains.
- **WHEN** a matching PREPARE is consumed in the control IRQ
- **THEN** readiness is acknowledged without changing the target or enabling PWM.
- **WHEN** a matching RUN for a staged cycle is consumed with explicit ON and nominal thermal status
- **THEN** the target is revalidated, applied once with its PI gains and acknowledged.

### Requirement: Session and disable safety

OFF, CONFIG, calibration and thermal trip SHALL discard pending synchronized
commands. RUN watchdog SHALL expire at 50 ms and PREPARE watchdog at 250 ms;
watchdog evaluation SHALL continue during transport error recovery. Watchdog and
invalid apply SHALL disable through the state controller, cancelling enabled intent.
When the status queue is full, watchdog TIMEOUT SHALL replace its oldest queued
status and SHALL be submitted for transmission after disabling the bridge if the
transport is available; receipt is not guaranteed under CAN/queue failure.
Rejected sync traffic during cooling SHALL NOT create a watchdog session that
cancels the upstream neutral thermal recovery. OFF→ON SHALL reset the sync session
so a command staged while OFF cannot be applied after enabling. Repeated ON when
already enabled SHALL preserve the active target. Existing duplicate, wrap and
retry handling SHALL remain intact.

#### Scenario: Cooling traffic does not cancel recovery

- **WHEN** an enabled motor trips thermally and delayed markers arrive while cooling
- **THEN** the cycle session remains reset.
- **AND** cooling still permits upstream neutral recovery unless explicitly disabled.

### Requirement: Registers and diagnostics

sync.version SHALL read natural32=5. sync.mode SHALL be persistent natural32 0/1,
default1, and mutable only while OFF. An accepted change SHALL reset the session.
The shared catalog SHALL expose disable, CAN/RX-FIFO and all five synchronization
progress diagnostics without restoring excluded filter registers or legacy aliases.

### Requirement: Fresh extended telemetry

Telemetry SHALL publish at most one fresh sample each 5 ms, without replaying
overdue slots, including across the 32-bit scheduling wrap. It SHALL include
Kelvin temperatures and report current/torque zero while OFF. Invalid stator
readings SHALL report NaN; rises SHALL not be delayed. A downward limit of 1 K/s
SHALL affect telemetry only. Critical statuses SHALL not wait for telemetry.
