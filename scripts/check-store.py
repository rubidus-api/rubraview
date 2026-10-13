#!/usr/bin/env python3
"""Checks the Store package's file types against the viewer's own list (D-122).

resources/distribution/rubraview-store.toml declares one [assoc.*] for each
extension the viewer registers (rubraview_shell_extensions() in
src/core/filemanage.c). A type added to one and not the other would be
opened by the installed program and not by the Store's, or the reverse.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).absolute().parent.parent


def main():
    code = (ROOT / "src" / "core" / "filemanage.c").read_text(encoding="utf-8")
    body = code[code.index("rubraview_shell_extensions(void)"):]
    viewer = set(re.search(r'U8\("([^"]*)"\)', body).group(1).split(";"))
    source = (ROOT / "resources" / "distribution" / "rubraview-store.toml").read_text(encoding="utf-8")
    store = re.findall(r'^extension = "\.([^"]+)"$', source, re.M)
    problems = []
    if len(store) != len(set(store)):
        problems.append("an extension is declared twice")
    for ext in sorted(viewer - set(store)):
        problems.append(".%s is the viewer's and not the Store package's" % ext)
    for ext in sorted(set(store) - viewer):
        problems.append(".%s is the Store package's and not the viewer's" % ext)
    for problem in problems:
        print("check-store: " + problem, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
