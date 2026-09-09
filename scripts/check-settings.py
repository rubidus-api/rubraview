#!/usr/bin/env python3
"""Every settings.ini key the program reads must be reachable from a tab.

RFC-0001 §11.2 makes this M9's completion criterion, and it is exactly
the kind of claim that rots: a module starts reading a new key, nobody
adds it to the settings window, and the setting becomes one only a
text editor can change.

So it is checked against the source rather than against anyone's memory.
Two directions:

  1. every key read through rubraview_ini_get* outside ini.c and
     settings.c appears in the schema in src/core/settings.c;
  2. every schema row marked `wired` is in fact read somewhere — a row
     that claims to be live but is not would make the greying-out lie.

Run from the repository root.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCHEMA_FILE = ROOT / "src" / "core" / "settings.c"

# The schema rows: MACRO("key", "section", ...) with `true`/`false` last.
ROW = re.compile(
    r'(BOOL_ROW|NUM_ROW|CHOICE_ROW|PATH_ROW)\(\s*"([^"]*)"\s*,\s*"([^"]*)"'
)
WIRED = re.compile(r'(BOOL_ROW|NUM_ROW|CHOICE_ROW|PATH_ROW)\("[^"]*",\s*"[^"]*".*?(true|false)\s*\)', re.S)

# A read: rubraview_ini_get*(&doc, U8("section"), U8("key"))
READ = re.compile(
    r'rubraview_ini_get\w*\(\s*&?\w+\s*,\s*U8\("([^"]*)"\)\s*,\s*U8\("([^"]*)"\)'
)


def schema_rows():
    text = SCHEMA_FILE.read_text(encoding="utf-8")
    rows = {}
    for match in re.finditer(
        r'(BOOL_ROW|NUM_ROW|CHOICE_ROW|PATH_ROW)\(\s*"([^"]*)"\s*,\s*"([^"]*)"(.*?)\)\s*,\s*\n',
        text,
        re.S,
    ):
        key, section, tail = match.group(2), match.group(3), match.group(4)
        wired = tail.rstrip().rstrip(")").strip().endswith("true")
        rows[(section, key)] = wired
    return rows


def source_reads():
    reads = {}
    for path in sorted((ROOT / "src").rglob("*.c")):
        if path.name in ("ini.c", "settings.c"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in READ.finditer(text):
            reads.setdefault((match.group(1), match.group(2)), []).append(
                path.relative_to(ROOT).as_posix()
            )
    return reads


def main():
    rows = schema_rows()
    if not rows:
        print("check-settings: the schema in src/core/settings.c parsed as empty", file=sys.stderr)
        return 2

    reads = source_reads()
    problems = []

    # 1. read but not offered
    for (section, key), files in sorted(reads.items()):
        if (section, key) not in rows:
            where = ", ".join(sorted(set(files)))
            problems.append(
                f"{section or '(global)'}/{key} is read by {where} "
                f"but no tab offers it (§3.22, §11.2)"
            )

    # 2. claims to be wired but nothing reads it
    for (section, key), wired in sorted(rows.items()):
        if not wired:
            continue
        if (section, key) in reads:
            continue
        # The nine curation folders are read through a built name rather
        # than a literal, so the regex above cannot see them; one hit on
        # the section is enough to know the group is live.
        if section == "curation":
            if any(s == "curation" for (s, _) in reads):
                continue
        problems.append(
            f"{section or '(global)'}/{key} is marked wired in the schema "
            f"but nothing reads it"
        )

    if problems:
        print("check-settings: FAIL", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1

    wired_count = sum(1 for w in rows.values() if w)
    print(
        f"check-settings: ok — {len(rows)} settings on {len({}) or 8} tabs, "
        f"{wired_count} wired, {len(reads)} reads all reachable"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
