#!/usr/bin/env python3
"""Checks the Store package's file types against the viewer's own list (D-122).

resources/distribution/rubraview-store.toml declares one [assoc.*] for each
extension the viewer registers (rubraview_shell_extensions() in
src/core/filemanage.c). A type added to one and not the other would be
opened by the installed program and not by the Store's, or the reverse.

Its [menu.*] tables are Explorer's right-click menu (RV-113 (3)): one for each
item of MENU_ITEMS in src/core/shellreq.c, with the same flag, texts and
"single", on the extensions of the item's kinds (SHELL_GROUPS in
src/core/filemanage.c).
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).absolute().parent.parent


def c_text(literal):
    return literal.encode("latin-1").decode("unicode_escape").encode("latin-1").decode("utf-8")


def check_menu(code, source):
    groups = {}
    for bit, exts in re.findall(r'\{ RUBRAVIEW_SHELL_(\w+), *"\w+", *"([^"]*)" \}', code):
        groups[bit] = exts.split(";")
    shell = (ROOT / "src" / "core" / "shellreq.c").read_text(encoding="utf-8")
    macros = {name: re.findall(r"RUBRAVIEW_SHELL_(\w+)", body)
              for name, body in re.findall(r"^#define (\w+) +\(([^)]*)\)", shell, re.M)}
    flags = re.search(r"VERB_FLAGS\[[^]]*\] = \{([^}]*)\}", shell).group(1)
    flags = re.findall(r'"([^"]*)"', flags)
    verbs = re.search(r"enum rubraview_shell_verb \{(.*?)\}", (ROOT / "include" / "rubraview" / "shellreq.h")
                      .read_text(encoding="utf-8"), re.S).group(1)
    verbs = re.findall(r"RUBRAVIEW_SHELL_VERB_(\w+)", verbs)
    want = []
    for verb, text, text_ko, kinds, single in re.findall(
            r'\{ RUBRAVIEW_SHELL_VERB_(\w+), *"\w+", *"([^"]*)", *"([^"]*)",\s*([^,]+), (true|false) \}', shell):
        exts = []
        for word in re.split(r"\s*\|\s*", kinds.strip()):
            for bit in macros.get(word, [word.replace("RUBRAVIEW_SHELL_", "")]):
                exts += groups[bit]
        want.append((flags[verbs.index(verb)], c_text(text), c_text(text_ko), single == "true", sorted(exts)))
    have = []
    for body in re.split(r"^\[menu\.\w+\]$", source, flags=re.M)[1:]:
        body = body.split("\n[", 1)[0]
        args = re.search(r'^args = "(\S+) --whole %\*"$', body, re.M)
        have.append((args.group(1) if args else None,
                     re.search(r'^text = "([^"]*)"$', body, re.M).group(1),
                     re.search(r'^text-ko = "([^"]*)"$', body, re.M).group(1),
                     re.search(r'^multi = "single"$', body, re.M) is not None,
                     sorted(e[1:] for e in re.findall(r'"(\.[a-z0-9]+)"', body))))
    problems = []
    for item in want:
        if item not in have:
            problems.append("the menu item %s %r is not in the Store package as it is in the viewer" % (item[0], item[1]))
    for item in have:
        if item not in want:
            problems.append("the Store package's menu item %s %r is not the viewer's" % (item[0], item[1]))
    if not want:
        problems.append("MENU_ITEMS was not read")
    return problems


def main():
    code = (ROOT / "src" / "core" / "filemanage.c").read_text(encoding="utf-8")
    body = code[code.index("rubraview_shell_extensions(void)"):]
    viewer = set(re.search(r'PROVEN_LIT\("([^"]*)"\)', body).group(1).split(";"))
    source = (ROOT / "resources" / "distribution" / "rubraview-store.toml").read_text(encoding="utf-8")
    store = re.findall(r'^extension = "\.([^"]+)"$', source, re.M)
    problems = []
    if len(store) != len(set(store)):
        problems.append("an extension is declared twice")
    for ext in sorted(viewer - set(store)):
        problems.append(".%s is the viewer's and not the Store package's" % ext)
    for ext in sorted(set(store) - viewer):
        problems.append(".%s is the Store package's and not the viewer's" % ext)
    problems += check_menu(code, source)
    for problem in problems:
        print("check-store: " + problem, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
