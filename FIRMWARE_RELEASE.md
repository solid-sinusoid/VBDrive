# VBDrive firmware release path

> **Integration not qualified. Do not run the flashing commands below.** The
> inherited wrapper is pinned to an older historical release branch and checkout;
> it is not a release procedure for `integrate-foc-sync`. The new image still
> requires the updated client on its new CAN command subject, and automatic EEPROM
> migration is not implemented. Publication and all hardware actions require separate
> approval after the integration review, target identification and backups.

The only supported source tree for this robot is:

`/home/vladimir/rbs_ws/.worktrees/vbdrive-temperature-safety`

Build and validate without touching CAN:

```bash
cd /home/vladimir/rbs_ws/.worktrees/vbdrive-temperature-safety
python3 tools/flash_vbdrive_release.py --joint 4
```

Flash only after the named joint is physically identified, its motor is disabled,
and it is the only drive intentionally placed into VBBoot recovery:

```bash
python3 tools/flash_vbdrive_release.py --joint 4 --execute
```

For a normal `command.bootloader` reboot, the bootloader CAN ID is the joint
number and the wrapper derives it from `--joint`. Use `--boot-can-id 0x444`
only for a deliberately selected cold-recovery fallback target.

The wrapper refuses a wrong branch, a history predating the approved safety
bases, modified or uncommitted source, and firmware without the fixed-address
compatibility manifest. It records exact source revisions and the image SHA-256
in `build/Release/flash_release_audit.json`.

Do not invoke copies of `flash_bootloader_socketcan.py` from another checkout.
The lower-level flasher is retained for development and recovery, but it also
rejects images without the expected VBDrive board ID, EEPROM config ABI, memory
map, and boot protocol before it opens CAN or sends an erase request.

After every transfer, verify both conditions before calling it successful:

1. The application answers on the intended Cyphal node ID.
2. The current application's `is_on` reads `false`.

Never activate a motor as part of firmware verification.
