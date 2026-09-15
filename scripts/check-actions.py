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
     menu item, or be listed in INTERNAL below with the reason;
  3. the key tables in the manual and in RFC-0001 §3.7.2, between their
     `keys:begin` / `keys:end` marks, are what the keymap says today
     (D-16: they are generated, never typed). `--write` regenerates them.

Run from the repository root.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAIN = ROOT / "src" / "app" / "main.c"
KEYMAP = ROOT / "src" / "core" / "default_keymap.c"
BOXES = ROOT / "src" / "core" / "default_boxes_doc.c"
GENERATED = [ROOT / "docs" / "manual" / "rubraview-manual.md", ROOT / "docs" / "rfc" / "rfc-0001-rubraview-architecture.md"]
BEGIN = "<!-- keys:begin — generated from src/core/default_keymap.c by scripts/check-actions.py --write; do not edit -->"
END = "<!-- keys:end -->"

# What each bound action does, in the words the tables use. One line each;
# an action bound to a key with no line here fails the check.
DESCRIPTIONS = {
    "toggle_fullscreen": "Full screen",
    "toggle_menu": "Open or close the menu box",
    "toggle_toolbox": "Open or close the toolbox",
    "toggle_toolbox_pin": "Pin the toolbox open",
    "toggle_toolbox_detach": "Give the toolbox a window of its own, or dock it",
    "toggle_always_on_top": "Always on top of other windows",
    "open_picker": "Open a file",
    "open_folder": "Open a folder",
    "toggle_filmstrip": "Filmstrip",
    "toggle_osd": "Information bar",
    "open_edit": "Adjust the picture",
    "quick_export": "Export",
    "save_as": "Save a copy as",
    "open_batch": "Convert many files",
    "open_settings": "Settings",
    "toggle_pixel_grid": "Pixel grid past 400%",
    "quit": "Quit",
    "delete_file": "To the recycle bin",
    "purge_file": "Delete for good (asks first)",
    "undo": "Undo a move, copy or rename",
    "rename_file": "Rename, keeping the extension",
    "window_narrower": "Window narrower",
    "window_wider": "Window wider",
    "window_shorter": "Window shorter",
    "window_taller": "Window taller",
    "window_move_left": "Move the window left",
    "window_move_right": "Move the window right",
    "window_move_up": "Move the window up",
    "window_move_down": "Move the window down",
    "next_page": "Next page",
    "prev_page": "Previous page",
    "first_page": "First page",
    "last_page": "Last page",
    "skip_forward": "Ten pages on",
    "skip_backward": "Ten pages back",
    "up_to_folder": "Up to the folder",
    "toggle_layout": "Single page / two pages / book",
    "toggle_reading_order": "Left-to-right / right-to-left (manga)",
    "toggle_spread_detect": "Detect two-page spreads",
    "next_archive": "Next archive in the folder",
    "prev_archive": "Previous archive in the folder",
    "fit_window": "Fit to the window",
    "fit_width": "Fit to the width",
    "fit_height": "Fit to the height",
    "actual_size": "Actual size (1:1)",
    "smart_fit": "Smart fit (shrink only)",
    "fit_stretch": "Stretch to fill",
    "toggle_fit_lock": "Keep the fit for the next files",
    "zoom_in": "Zoom in",
    "zoom_out": "Zoom out",
    "rotate_cw": "Rotate clockwise",
    "rotate_ccw": "Rotate anticlockwise",
    "flip_horizontal": "Flip left-right",
    "flip_vertical": "Flip upside down",
    "toggle_nearest": "Crisp scaling for pixel art",
    "toggle_slideshow": "Start or stop the slide show",
    "interval_up": "Slower slides (0.5 s)",
    "interval_down": "Faster slides (0.5 s)",
    "interval_up_fine": "Slower slides (0.1 s)",
    "interval_down_fine": "Faster slides (0.1 s)",
    "media_play_pause": "Play / pause",
    "anim_step_forward": "One frame forward",
    "anim_step_back": "One frame back",
    "anim_speed_up": "Faster (0.25x a step)",
    "anim_speed_down": "Slower (0.25x a step)",
    "media_speed_reset": "Normal speed",
    "media_seek_forward": "5 seconds on",
    "media_seek_back": "5 seconds back",
    "media_volume_up": "Volume up 5%",
    "media_volume_down": "Volume down 5%",
    "media_mute": "Mute / sound",
    "media_ab_a": "Repeat from here (A)",
    "media_ab_b": "Repeat to here (B)",
    "media_ab_clear": "Repeat off",
    "subtitle_earlier": "Subtitles half a second earlier",
    "subtitle_later": "Subtitles half a second later",
    "next_audio_track": "Next sound track",
    "next_subtitle_track": "Next subtitles",
    "subpage_next": "Next page of the file",
    "subpage_prev": "Previous page of the file",
}

CONTEXTS = [
    ("ui", "Everywhere"),
    ("navigation", "Moving between pages"),
    ("view", "The view"),
    ("slideshow", "While a slide show runs"),
    ("media", "While a video, music or animated picture is on screen"),
    ("subpage", "A multi-page TIFF or ICO"),
]

KEY_WORDS = {"Plus": "+", "Minus": "-", "BracketRight": "]", "BracketLeft": "[", "Backslash": "\\",
             "Comma": ",", "Period": ".", "Escape": "Esc", "Semicolon": ";", "Slash": "/",
             "Quote": "'", "Backquote": "`"}

# Handled on purpose with no key, tile or menu item of their own.
INTERNAL = {
    "resume_accept": "answered from the resume prompt, which has its own keys",
    "anim_toggle_pause": "the name media_play_pause had before D-16, kept for keymap.ini files saved earlier",
}


def c_strings(text):
    """The C string literals of a source, joined per statement is not needed:
    every action id sits inside one literal."""
    return re.findall(r'"((?:[^"\\]|\\.)*)"', text)


def keymap_bindings():
    """(context, action, keys) in the keymap's order."""
    found, context = [], None
    for piece in c_strings(KEYMAP.read_text(encoding="utf-8")):
        line = piece.replace('\\"', '"').replace("\\n", "")
        m = re.match(r"^\[([a-z]+)\]$", line)
        if m:
            context = m.group(1)
            continue
        m = re.match(r'^([a-z0-9_]+) = "(.*)"$', line)
        if m and context:
            found.append((context, m.group(1), [k.strip() for k in m.group(2).split(",") if k.strip()]))
    return found


def key_text(combo):
    parts = combo.split("+")
    parts[-1] = KEY_WORDS.get(parts[-1], parts[-1])
    return "`" + "+".join(parts) + "`"


def generated_tables():
    bindings = keymap_bindings()
    lines = [BEGIN, ""]
    for context, title in CONTEXTS:
        rows = [(a, k) for c, a, k in bindings if c == context]
        if not rows:
            continue
        lines += [f"**{title}**", "", "| Keys | What it does |", "|---|---|"]
        for action, keys in rows:
            text = DESCRIPTIONS.get(action, f"({action})")
            lines.append(f"| {', '.join(key_text(k) for k in keys)} | {text} |")
        lines.append("")
    lines.append(END)
    return "\n".join(lines)


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

    for context, action, _ in keymap_bindings():
        if action not in DESCRIPTIONS:
            problems.append(f"{action}: bound to a key but has no line in DESCRIPTIONS — the key tables cannot say what it does")
    if not any(c == ctx for c, _, _ in keymap_bindings() for ctx, _ in CONTEXTS):
        problems.append("no keymap context matched — the key tables would be empty")

    table = generated_tables()
    for path in GENERATED:
        text = path.read_text(encoding="utf-8")
        if BEGIN not in text or END not in text:
            problems.append(f"{path.relative_to(ROOT)}: no keys:begin / keys:end marks")
            continue
        before, rest = text.split(BEGIN, 1)
        _, after = rest.split(END, 1)
        fresh = before + table + after
        if fresh != text:
            if "--write" in sys.argv:
                path.write_text(fresh, encoding="utf-8")
                print(f"check-actions: rewrote the key table in {path.relative_to(ROOT)}")
            else:
                problems.append(f"{path.relative_to(ROOT)}: the key table is not what the keymap says "
                                "(run scripts/check-actions.py --write)")

    if problems:
        for p in problems:
            print(f"check-actions: {p}", file=sys.stderr)
        return 1
    print(f"check-actions: ok — {len(handled)} actions handled, {len(keys)} bound to keys, "
          f"{len(boxes)} on tiles or in the menu, {len(INTERNAL)} internal")
    return 0


if __name__ == "__main__":
    sys.exit(main())
