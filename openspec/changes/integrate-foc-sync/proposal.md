# Integrate phased FOC synchronization with the upstream application

Preserve the selected fork's protocol revision 5, cycle/session recovery and
diagnostics in the current split application structure. Keep upstream physical
output-shaft Kt, Servo generators, thermal protection and startup-OFF. Do not
restore the legacy register aliases or the three excluded mechanical filters.

The required sync listener is added without removing the existing CAN Servo
listener. A successful Servo command resets the synchronized session.
The persistent mode byte changes the application configuration ABI to 0x44AAAC06;
EEPROM preservation, VBBoot compatibility and actual image CRC are separate
unfinished delivery gates. This change is not permission to flash or publish.
