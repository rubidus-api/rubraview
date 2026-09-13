#!/usr/bin/env python3
"""Every settings.ini key the program reads must be reachable from a tab.

RFC-0001 §11.2 makes this M9's completion criterion, and it is exactly
the kind of claim that rots: a module starts reading a new key, nobody
adds it to the settings window, and the setting becomes one only a
text editor can change.

So it is checked against the source rather than against anyone's memory.
Two directions:

  1. every key read through rubraview_ini_get* or rubraview_settings_get*
     outside the settings modules appears in the settings window's
     document, src/core/default_settings_doc.c (D-13: the one list);
  2. every setting there marked `wired` is in fact read somewhere — a row
     that claims to be live but is not would make the greying-out lie.

Run from the repository root.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOC_FILE = ROOT / "src" / "core" / "default_settings_doc.c"

SETTING_KINDS = {"toggle", "choice", "int", "float", "path"}

# The second one is what a setting read while viewing looks like, now that
# settings.ini is loaded at startup rather than when the window opens.
READ = re.compile(
    r'rubraview_(?:ini|settings)_get\w*\(\s*&?[\w.>-]+\s*,\s*U8\("([^"]*)"\)\s*,\s*U8\("([^"]*)"\)'
)


def document_text():
    """The document as the program sees it: the C string pieces joined,
    with their escapes undone."""
    source = DOC_FILE.read_text(encoding="utf-8")
    body = source[source.index("PARTS[] = {"):source.index("};", source.index("PARTS[] = {"))]
    pieces = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
    return "".join(pieces).encode("utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")


def schema_rows():
    """(section, key) -> wired, and how many pages, from the document."""
    rows, pages = {}, 0
    for line in document_text().split("\n"):
        # drop a comment, then the quoted parts, so words inside labels
        # (a label that says "wired") cannot count
        bare = re.sub(r'"[^"]*"', '""', line.split("#", 1)[0]).split()
        if not bare:
            continue
        if bare[0] == "page":
            pages += 1
        if bare[0] in SETTING_KINDS and len(bare) > 1 and "." in bare[1]:
            section, key = bare[1].split(".", 1)
            rows[(section, key)] = "wired" in bare[2:]
    return rows, pages


def source_reads():
    reads = {}
    for path in sorted((ROOT / "src").rglob("*.c")):
        if path.name in ("ini.c", "settings.c", "settings_doc.c", "default_settings_doc.c"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in READ.finditer(text):
            reads.setdefault((match.group(1), match.group(2)), []).append(
                path.relative_to(ROOT).as_posix()
            )
    return reads


def main():
    rows, pages = schema_rows()
    if not rows:
        print("check-settings: the settings document parsed as empty", file=sys.stderr)
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
            f"{section or '(global)'}/{key} is marked wired in the document "
            f"but nothing reads it"
        )

    if problems:
        print("check-settings: FAIL", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1

    wired_count = sum(1 for w in rows.values() if w)
    print(
        f"check-settings: ok — {len(rows)} settings on {pages} pages, "
        f"{wired_count} wired, {len(reads)} reads all reachable"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
