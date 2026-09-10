#!/usr/bin/env python3
"""The version must exist in exactly one place.

A version written twice is a version that eventually disagrees with
itself, and the day it does, a bug report can no longer say which build
it came from. So `include/rubraview/version.h` is the only place it is
written, and everything else derives from it.

Three things are checked:

  1. the string and the three numbers in the header agree with each
     other — `"0.0.1"` next to MAJOR 0 / MINOR 1 / PATCH 0 is the kind of
     slip nobody notices until a release;
  2. the Makefile *derives* the version rather than restating it;
  3. no other tracked file hard-codes a version-looking literal that
     disagrees with the header.

Run from the repository root.
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "include" / "rubraview" / "version.h"
MAKEFILE = ROOT / "Makefile"

# Files that legitimately name past versions: a changelog is a record of
# what was released, and rewriting it to match today's number would be
# falsifying it.
EXEMPT = {"CHANGELOG.md"}


def read_header():
    text = HEADER.read_text(encoding="utf-8")

    string = re.search(r'^#define\s+RUBRAVIEW_VERSION_STRING\s+"([^"]+)"', text, re.M)
    major = re.search(r"^#define\s+RUBRAVIEW_VERSION_MAJOR\s+(\d+)", text, re.M)
    minor = re.search(r"^#define\s+RUBRAVIEW_VERSION_MINOR\s+(\d+)", text, re.M)
    patch = re.search(r"^#define\s+RUBRAVIEW_VERSION_PATCH\s+(\d+)", text, re.M)

    if not (string and major and minor and patch):
        return None, None
    return string.group(1), f"{major.group(1)}.{minor.group(1)}.{patch.group(1)}"


def tracked_files():
    out = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True)
    if out.returncode != 0:
        return []
    return [ROOT / line for line in out.stdout.splitlines() if line]


def main():
    problems = []

    if not HEADER.exists():
        print(f"check-version: {HEADER} is missing", file=sys.stderr)
        return 2

    version, from_numbers = read_header()
    if version is None:
        problems.append("the header does not define all four of STRING/MAJOR/MINOR/PATCH")
    elif version != from_numbers:
        problems.append(
            f'the header disagrees with itself: string "{version}" '
            f"but the numbers say {from_numbers}"
        )

    make_text = MAKEFILE.read_text(encoding="utf-8")
    if "version.h" not in make_text:
        problems.append("the Makefile does not read the version from version.h — it is restating it")
    if re.search(r"^VERSION\s*[:?]?=\s*\d+\.\d+\.\d+", make_text, re.M):
        problems.append("the Makefile hard-codes a version number instead of deriving it")

    # A literal that looks like this project's version, somewhere it was
    # written by hand and will be forgotten.
    if version:
        pattern = re.compile(r"rubraview[- ]v?(\d+\.\d+\.\d+)", re.I)
        for path in tracked_files():
            if path.name in EXEMPT or path == HEADER:
                continue
            if path.suffix not in {".md", ".c", ".h", ".sh", ".py"} and path.name != "Makefile":
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            for found in set(pattern.findall(text)):
                if found != version:
                    problems.append(
                        f"{path.relative_to(ROOT)} names version {found}, "
                        f"but the header says {version}"
                    )

    if problems:
        print("check-version: FAIL", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1

    print(f"check-version: ok — {version}, named in one place and derived everywhere else")
    return 0


if __name__ == "__main__":
    sys.exit(main())
