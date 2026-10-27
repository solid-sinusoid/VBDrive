## MODIFIED Requirements

### Requirement: Protected entry and movement rejection

Startup SHALL NOT create enabled intent. Explicit enable, configuration restoration
and calibration entry SHALL evaluate protection using initialized current measurements
before energizing the bridge. Motion commands processed during COOLING_DOWN SHALL
be rejected and SHALL NOT become pending targets. OFF and configuration access
SHALL remain available. Once protection clears, previously pending motion SHALL
NOT be replayed; recovery of pre-existing enabled intent requires a fresh command
accepted after neutral enable. A hot boot SHALL remain OFF after cooling until an
explicit enable; recovery of a drive enabled before its thermal trip is unchanged.

#### Scenario: Hot boot

- **WHEN** a calibrated drive boots with initialized stator temperature at 368.15 K
- **THEN** it enters COOLING_DOWN without enabling PWM or creating enabled intent.
- **AND** valid recovery alone does not enable the bridge.
