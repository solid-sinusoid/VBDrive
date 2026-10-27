## MODIFIED Requirements

### Requirement: Startup and calibration loading

Startup SHALL load communication/application configuration before normal operation.
Missing required configuration SHALL retain a setup path with Serial and Cyphal
access; node_id=0 SHALL use a UID-derived temporary CAN node identity for setup.
A configured startup SHALL initialize the motor with a neutral target and load
calibration. Power-on and application resets, including APPLY, SHALL leave the
bridge OFF without creating startup enabled intent. Explicit enable SHALL require
RUNNING, valid calibration and initialized temperature readings satisfying thermal
protection. Missing or incompatible calibration SHALL stop the bridge and enter
NOT_CALIBRATED. A calibrated startup SHALL NOT restore a previous runtime motion
command; is_on SHALL NOT be treated as persistent configuration. Sensor updates
and thermal servicing SHALL continue while OFF.

#### Scenario: Calibrated startup remains disabled

- **WHEN** a configured and calibrated drive starts or resets at valid temperatures
- **THEN** it enters RUNNING with the bridge OFF until explicit is_on=1.
- **AND** a motion command alone does not enable it.

#### Scenario: Calibration unavailable

- **WHEN** motor initialization finds a calibration record with the wrong type or no completed-calibration flag
- **THEN** the application enters NOT_CALIBRATED and leaves the bridge disabled.

#### Scenario: Schema update requires fresh setup

- **WHEN** the new firmware loads incompatible application and calibration types
- **THEN** application settings reset to defaults without migration and old calibration is rejected
- **AND** normal operation requires fresh configuration and successful calibration.
