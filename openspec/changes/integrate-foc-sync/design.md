# Adaptation and boundaries

- Reuse FocCycleSync from origin/fix/foc-status-coalescing (8d57aa6), with a
  bounded-queue correction that retains TIMEOUT under saturation. Adapt its
  subscriptions, status preemption and IRQ application into the upstream
  state controller and shared parameter catalog, not the deleted legacy app files.
- A 28-byte legacy command and a 30-byte new command can both produce 31 data
  bytes after CAN FD padding, then truncate to the same 30-byte receive extent.
  Source ID and length cannot discriminate them, so the user approved a new
  command2 subject 2300+node_id and removal of the legacy subject filter.
- COMMAND stages only. PREPARE acknowledges readiness without modifying physical
  targets or PI gains; RUN requires explicit ON and revalidates the target in IRQ.
- Mask interrupts around foreground access to the cycle machine. Session reset
  and its bounded status queues must not race application of a half-staged target.
- Route watchdog/invalid-target OFF through the real state controller so pending
  thermal enable intent is cancelled. A thermal trip resets the cycle session but
  retains upstream neutral recovery; ignore sync markers during COOLING_DOWN.
- Preserve hardware TX FIFO mode. Preempt pending hardware frames only when full
  for critical status, then service one libcanard TX item. Critical RX uses FIFO1;
  the existing transport drains bounded critical3/regular1 snapshots.
- Telemetry uses cached safety temperature samples; only its stator output is
  downward-slope-limited. Safety conversion/interlock remains independent at 1 ms.
- Use a protected 64-bit millisecond epoch and the fork's pending-update/counter
  snapshot for micros_64; the 32-bit scheduling clock retains wrap semantics.

Host tests use production adapters, cycle machine and generated serializers with
hardware doubles and actual libcanard framing. They do not prove interrupt timing,
bus throughput, motor tuning, EEPROM transition or recovery on real hardware.
