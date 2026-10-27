# Start the motor disabled

## Why

A calibrated upstream startup creates enabled intent automatically. The integration
must instead require an explicit enable after power-on or reset, including after
cooling from a hot boot, without changing recovery of a motor already running.

## What Changes

- Remove the startup-only enable request and its unused controller API.
- Keep sensor/thermal servicing, explicit enable, calibration and existing recovery.
- Check the production startup sequence with the actual state controller on the host.

## Impact

Affected capabilities: drive-lifecycle and temperature-protection.
No wire types, register names, EEPROM layouts or dependency APIs change in this step.
The separate synchronization/EEPROM integration remains unfinished.
