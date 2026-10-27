#!/usr/bin/env python3

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


RELEASE_BRANCH = "safety/temperature-telemetry-20260729"
RELEASE_BASE = "1dcb838f0351623bb87bd0b9f3002422224a30c7"
LIBVOLTBRO_BASE = "44716f9d6d831db8748dca1f28467bdab998b521"
VBBOOT_BASE = "1e8156d175c08a171c5c86719de9bfebf71c44d5"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build and flash the canonical, compatibility-checked VBDrive release"
    )
    parser.add_argument("--joint", type=int, required=True, choices=range(1, 7))
    parser.add_argument("--channel", default="can0")
    parser.add_argument(
        "--boot-can-id",
        help="Override the bootloader CAN ID; defaults to the explicitly named joint",
    )
    parser.add_argument(
        "--execute",
        action="store_true",
        help="Actually use CAN. Without this flag only build and validate the artifact.",
    )
    args = parser.parse_args()
    boot_can_id = args.boot_can_id if args.boot_can_id is not None else str(args.joint)

    root = Path(__file__).resolve().parents[1]
    expected_root = Path("/home/vladimir/rbs_ws/.worktrees/vbdrive-temperature-safety")
    if root != expected_root:
        raise RuntimeError(f"non-canonical source tree: {root}; expected {expected_root}")

    branch = subprocess.check_output(
        ["git", "branch", "--show-current"], cwd=root, text=True
    ).strip()
    if branch != RELEASE_BRANCH:
        raise RuntimeError(f"wrong release branch: {branch!r}; expected {RELEASE_BRANCH!r}")

    checks = (
        (root, RELEASE_BASE, "VBDrive release base"),
        (root / "Drivers/libvoltbro", LIBVOLTBRO_BASE, "libvoltbro safety base"),
        (root / "VBBoot", VBBOOT_BASE, "VBBoot hardened base"),
    )
    for repository, ancestor, label in checks:
        result = subprocess.run(
            ["git", "merge-base", "--is-ancestor", ancestor, "HEAD"], cwd=repository
        )
        if result.returncode != 0:
            raise RuntimeError(f"{label} {ancestor} is not an ancestor of {repository}/HEAD")

    status = subprocess.check_output(
        ["git", "status", "--porcelain", "--untracked-files=all"], cwd=root, text=True
    ).strip()
    if status:
        raise RuntimeError("release tree is dirty; commit and review all firmware changes first:\n" + status)

    subprocess.run(["cmake", "--preset", "Release"], cwd=root, check=True)
    subprocess.run(["cmake", "--build", "--preset", "Release"], cwd=root, check=True)

    image = root / "build/Release/VBDrive.hex"
    flasher = root / "VBBoot/tools/flash_bootloader_socketcan.py"
    image_sha256 = hashlib.sha256(image.read_bytes()).hexdigest()
    command = [
        sys.executable,
        str(flasher),
        "--hex",
        str(image),
        "--channel",
        args.channel,
        "--node-id",
        boot_can_id,
    ]
    if not args.execute:
        command.append("--dry-run")

    audit = {
        "utc": datetime.now(timezone.utc).isoformat(),
        "joint": args.joint,
        "execute": args.execute,
        "source_root": str(root),
        "branch": branch,
        "vbdrive_commit": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=root, text=True
        ).strip(),
        "libvoltbro_commit": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=root / "Drivers/libvoltbro", text=True
        ).strip(),
        "vbboot_commit": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=root / "VBBoot", text=True
        ).strip(),
        "image": str(image),
        "image_sha256": image_sha256,
        "channel": args.channel,
        "boot_can_id": boot_can_id,
    }
    audit_path = root / "build/Release/flash_release_audit.json"
    audit_path.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    print(f"release_audit={audit_path}")
    print(f"target_joint={args.joint} execute={int(args.execute)} image_sha256={image_sha256}")
    subprocess.run(command, cwd=root, check=True)
    if args.execute:
        print("CAN transfer finished; application node and state.is_on=false must be verified before success")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"release_flash_refused: {exc}", file=sys.stderr)
        raise SystemExit(1)
