#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import plistlib
import re
import shlex
import subprocess
import sys
from pathlib import Path

DEFAULT_APP = "/Applications/Binary Ninja.app"


def read_api_revision(resources_dir: Path) -> str:
    path = resources_dir / "api_REVISION.txt"
    if not path.exists():
        raise FileNotFoundError(f"missing {path}")
    candidates: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        candidates.extend(re.findall(r"[0-9a-fA-F]{7,40}", line))
    if candidates:
        for candidate in candidates:
            if len(candidate) == 40:
                return candidate
        return max(candidates, key=len)
    raise ValueError(f"no commit hash found in {path}")


def read_qt_version(app_path: Path) -> str:
    candidates = [
        app_path / "Contents/Frameworks/QtCore.framework/Resources/Info.plist",
        app_path
        / "Contents/Frameworks/QtCore.framework/Versions/Current/Resources/Info.plist",
        app_path
        / "Contents/Frameworks/QtCore.framework/Versions/A/Resources/Info.plist",
        app_path / "Contents/Frameworks/QtGui.framework/Resources/Info.plist",
        app_path / "Contents/Frameworks/QtWidgets.framework/Resources/Info.plist",
    ]

    for plist_path in candidates:
        if not plist_path.exists():
            continue
        try:
            with plist_path.open("rb") as handle:
                data = plistlib.load(handle)
        except Exception:
            continue
        version = data.get("CFBundleVersion") or data.get("CFBundleShortVersionString")
        if version:
            return str(version)

    raise FileNotFoundError("could not determine qt version from app bundle")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="configure cmake using binary ninja app metadata",
        formatter_class=argparse.RawTextHelpFormatter,
    )
    parser.add_argument(
        "--app",
        default=os.environ.get("BINJA_APP", DEFAULT_APP),
        help="path to binary ninja app",
    )
    parser.add_argument("-S", "--source-dir", default=".", help="cmake source dir")
    parser.add_argument(
        "-B", "--build-dir", default="build-release", help="cmake build dir"
    )
    parser.add_argument("-G", "--generator", default="Ninja", help="cmake generator")
    parser.add_argument("--build-type", default="Release", help="cmake build type")
    parser.add_argument(
        "--api-revision",
        help="override api version instead of reading the revision file",
    )
    parser.add_argument(
        "--qt-version",
        help="override qt version instead of reading framework metadata",
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="print the command and exit"
    )
    parser.add_argument(
        "extra",
        nargs=argparse.REMAINDER,
        help="extra cmake arguments after --",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    app_path = Path(args.app).expanduser()
    if not app_path.exists():
        print(f"error: app not found at {app_path}", file=sys.stderr)
        return 2

    resources_dir = app_path / "Contents/Resources"
    if not resources_dir.exists():
        print(f"error: missing resources dir at {resources_dir}", file=sys.stderr)
        return 2

    try:
        api_revision = args.api_revision or read_api_revision(resources_dir)
    except (FileNotFoundError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    try:
        qt_version = args.qt_version or read_qt_version(app_path)
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    extra_args = list(args.extra)
    if extra_args[:1] == ["--"]:
        extra_args = extra_args[1:]

    cmd = [
        "cmake",
        "-G",
        args.generator,
        "-S",
        str(args.source_dir),
        "-B",
        str(args.build_dir),
        f"-DCMAKE_BUILD_TYPE={args.build_type}",
        f"-DBINJA_API_VERSION={api_revision}",
        f"-DBINJA_QT_VERSION={qt_version}",
        *extra_args,
    ]

    if args.dry_run:
        print(f"{shlex.join(cmd)}")
        return 0

    print("cmake configure")
    print(f"app: {app_path}")
    print(f"api: {api_revision}")
    print(f"qt: {qt_version}")

    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as exc:
        return exc.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
