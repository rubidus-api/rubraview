#!/usr/bin/env python3
"""gen-notices — the licence notices, built into the executable.

The executable is also offered on its own, beside the zip (owner,
2026-09-29), and the licences of the libraries linked into it ask that
their notices go wherever the binary goes. So the texts the zip carries in
`licences/` are compiled in and shown at the end of the F1 help window.

`src/core/notices_text.c` is written from the files below; `--check`
fails when it is not what they say today (run by project-check).
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "src" / "core" / "notices_text.c"
SOURCES = [
    "THIRD_PARTY_NOTICES.md",
    "LICENSE",
    "vendor/proven/LICENSE",
    "vendor/proven/THIRD_PARTY_NOTICES.md",
    "vendor/miniz/LICENSE",
    "vendor/lzma/LICENSE.txt",
    "vendor/unrar-c/LICENSE.txt",
    "vendor/libjpeg-turbo/LICENSE.md",
    "vendor/libjpeg-turbo/README.ijg",
]


def c_string(line: str) -> str:
    out = []
    for ch in line:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\t":
            out.append("    ")
        elif ord(ch) < 0x20:
            continue
        elif ord(ch) < 0x80:
            out.append(ch)
        else:
            # a hex escape swallows the hex digits after it: each one closes
            # its literal, and the next begins
            out.extend(f'\\x{b:02X}""' for b in ch.encode("utf-8"))
    return '"' + "".join(out) + '"'


def render() -> str:
    lines = []
    for rel in SOURCES:
        path = ROOT / rel
        lines.append(f"==== {rel} ====")
        lines.extend(path.read_text(encoding="utf-8", errors="replace").splitlines())
        lines.append("")
    body = []
    for line in lines:
        literal = c_string(line.rstrip())
        body.append(f"    {literal},")
    return (
        "/* Written by scripts/gen-notices.py from the licence files; do not edit.\n"
        "   The notices of what is linked into the executable, for the F1 help\n"
        "   window (owner, 2026-09-29: the executable is offered on its own). */\n"
        '#include "rubraview/notices.h"\n\n'
        "static const char *const LINES[] = {\n" + "\n".join(body) + "\n};\n\n"
        "size_t rubraview_notice_lines(const char *const **out) {\n"
        "    if (out) *out = LINES;\n"
        "    return sizeof(LINES) / sizeof(LINES[0]);\n"
        "}\n"
    )


def main() -> int:
    text = render()
    if "--check" in sys.argv:
        if not OUT.exists() or OUT.read_text(encoding="utf-8") != text:
            print("gen-notices: src/core/notices_text.c is not what the licence files say (run scripts/gen-notices.py)", file=sys.stderr)
            return 1
        print(f"gen-notices: ok — {text.count(chr(10))} lines from {len(SOURCES)} files")
        return 0
    OUT.write_text(text, encoding="utf-8")
    print(f"gen-notices: wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
