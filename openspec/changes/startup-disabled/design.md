# Disabled startup without changing recovery

The motor already initializes with PWM MOE disabled and `_is_on=false`. Remove
the application call that creates startup enabled intent, rather than adding a
second stop/enable mechanism. Keep `service_temperature()` and its recovery flag
for actual thermal trips and CONFIG restoration. Explicit OFF continues to clear
that flag. No interrupt, storage, heap or communication changes are needed.

The host regression executes the production startup sequence with hardware setup
doubled and the real controller implementation. It checks cold/hot boot, cooling,
a command while OFF and explicit enable. Existing thermal/calibration and Servo
regressions cover the unchanged recovery and reset paths. These checks do not
prove physical PWM state; power/reset and bridge measurements remain hardware work.
