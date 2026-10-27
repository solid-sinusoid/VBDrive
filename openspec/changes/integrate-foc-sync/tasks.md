## Application integration

- [x] Import final cycle machine and its C++ regressions without restoring obsolete app files.
- [x] Exercise production command/sync/IRQ adapters and truncated packet rejection RED→GREEN.
- [x] Resolve normal legacy/new CAN FD ambiguity by user-approved separate 2300+node_id subject; verify production filters and coordinate client transition.
- [x] Verify actual state-controller OFF/CONFIG/thermal reset, mode persistence and watchdog diagnostics.
- [x] Add 200 Hz extended telemetry and timer-wrap regression RED→GREEN.
- [x] Adapt critical FIFO handling, status preemption, calibration flush and TDC option.
- [x] Connect the monotonic microsecond snapshot and test the counter-wrap failure first.
- [x] Re-run the complete host suite, CTest and Release build after final application edits (20 Python host checks, 9 CTest cases, Release PASS).
- [ ] Qualify EEPROM preservation/transition, VBBoot and actual GetInfo image CRC before delivery. (Read-only fail-closed guard, VBBoot merge and CRC host tests added; legacy migration, detection of arbitrary corruption without existing checksums, and full integration review remain.)
- [ ] Complete final build variants and whole-integration review before publication approval.
- [ ] Perform authorized physical power/reset, timing, thermal and CAN network acceptance.
