#!/usr/bin/env python3
"""Every key, tile and menu item does something, and everything the viewer
does can be reached (RFC-0003 §4, D-16).

Three lists name actions, and nothing but this keeps them together:

  - the built-in keymap, src/core/default_keymap.c;
  - the floating boxes — the menu tree and the toolbox tiles;
  - handle_action in src/app/main.c, which is what actually happens.

Two directions:

  1. an action a key, a tile or a menu item names must be handled — a
     binding nothing handles is a key that silently does nothing (the four
     pan_* bindings were exactly that until D-16);
  2. an action the viewer handles must be reachable by a key, a tile or a
     menu item, or be listed in INTERNAL below with the reason.

Run from the repository root.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAIN = ROOT / "src" / "app" / "main.c"
KEYMAP = ROOT / "src" / "core" / "default_keymap.c"
BOXES = ROOT / "src" / "core" / "default_boxes_doc.c"

# Handled on purpose with no key, tile or menu item of their own.
INTERNAL = {
    "resume_accept": "answered from the resume prompt, which has its own keys",
    "anim_toggle_pause": "the name media_play_pause had before D-16, kept for keymap.ini files saved earlier",
}


def c_strings(text):
    """The C string literals of a source, joined per statement is not needed:
    every action id sits inside one literal."""
    return re.findall(r'"((?:[^"\\]|\\.)*)"', text)


def keymap_actions():
    found = set()
    for piece in c_strings(KEYMAP.read_text(encoding="utf-8")):
        line = piece.replace('\\"', '"').replace("\\n", "")
        m = re.match(r"^([a-z0-9_]+) = ", line)
        if m:
            found.add(m.group(1))
    return found


def box_actions(main_text):
    found = set()
    if BOXES.exists():
        for piece in c_strings(BOXES.read_text(encoding="utf-8")):
            line = piece.replace("\\n", "")
            for m in re.finditer(r"\b(?:tile|item)\s+([a-z0-9_]+)", line):
                found.add(m.group(1))
    found |= set(re.findall(r'\.action = MENU_STR\("([a-z0-9_]+)"\)', main_text))
    block = re.search(r"TOOLBOX_ACTIONS\[[A-Z_]*\] = \{(.*?)\};", main_text, re.S)
    if block:
        found |= set(re.findall(r'"([a-z0-9_]+)"', block.group(1)))
    return found


def handled_actions(main_text):
    found = set(re.findall(r'action_is\(action, "([a-z0-9_]+)"\)', main_text))
    steps = re.search(r"STEPS\[\] = \{(.*?)\};", main_text, re.S)
    if steps:
        found |= set(re.findall(r'\{ "([a-z0-9_]+)"', steps.group(1)))
    return found


def main():
    main_text = MAIN.read_text(encoding="utf-8")
    keys = keymap_actions()
    boxes = box_actions(main_text)
    handled = handled_actions(main_text)
    problems = []

    for action in sorted((keys | boxes) - handled):
        where = "a key" if action in keys else "a tile or menu item"
        problems.append(f"{action}: named by {where}, handled by nothing — it would do nothing")
    for action in sorted(handled - keys - boxes - set(INTERNAL)):
        problems.append(f"{action}: handled, but no key, tile or menu item reaches it "
                        "(bind it, place it, or list it in INTERNAL with the reason)")
    for action in sorted(set(INTERNAL) - handled):
        problems.append(f"{action}: listed as internal but no longer handled — take it off the list")

    if problems:
        for p in problems:
            print(f"check-actions: {p}", file=sys.stderr)
        return 1
    print(f"check-actions: ok — {len(handled)} actions handled, {len(keys)} bound to keys, "
          f"{len(boxes)} on tiles or in the menu, {len(INTERNAL)} internal")
    return 0


if __name__ == "__main__":
    sys.exit(main())
