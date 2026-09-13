#!/usr/bin/env python3
"""check-conf-format — every configuration file rubraview writes is both
valid TOML and valid INI, with the same meaning (D-13).

It builds tools/conf_samples.c against the real writer, lets it write its
sample files, and reads each one twice:

  - with tomllib (TOML 1.0), and
  - with configparser, as plain INI: no interpolation, since `%` in a value
    is allowed; keys are lower case in the subset, so case folding is moot.

"The same meaning" is: the INI text, with the subset's one quoting rule
applied — outer double quotes off, `\\\\` and `\\"` undone — equals the TOML
value written as text (`true`, `48`, `0.5`, or the string).

A run that measured no file fails: a gate that looked at nothing is not a
pass.
"""
import configparser
import os
import pathlib
import subprocess
import sys
import tempfile
import tomllib

ROOT = pathlib.Path(__file__).resolve().parent.parent

SOURCES = [
    "tools/conf_samples.c",
    "src/core/ini.c",
    "vendor/proven/src/proven/arena.c",
    "vendor/proven/src/proven/memory.c",
    "vendor/proven/src/proven/panic.c",
    "vendor/proven/platform/proven_sys_mem.c",
]


def ini_meaning(text: str) -> str:
    if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
        inner, out, i = text[1:-1], [], 0
        while i < len(inner):
            if inner[i] == "\\" and i + 1 < len(inner) and inner[i + 1] in '\\"':
                out.append(inner[i + 1])
                i += 2
            else:
                out.append(inner[i])
                i += 1
        return "".join(out)
    return text


def toml_as_text(value) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        return repr(value)
    return str(value)


def check_file(path: pathlib.Path) -> list:
    problems = []
    raw = path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        problems.append(f"{path.name}: starts with a byte-order mark")
    text = raw.decode("utf-8")

    try:
        toml = tomllib.loads(text)
    except tomllib.TOMLDecodeError as e:
        return problems + [f"{path.name}: TOML refuses it — {e}"]

    ini = configparser.ConfigParser(interpolation=None, strict=True)
    ini.optionxform = str  # report case differences instead of hiding them
    try:
        ini.read_string(text)
    except configparser.Error as e:
        return problems + [f"{path.name}: INI refuses it — {type(e).__name__}: {e}"]

    for section, table in toml.items():
        if not isinstance(table, dict):
            problems.append(f"{path.name}: `{section}` is a key outside any section")
            continue
        if not ini.has_section(section):
            problems.append(f"{path.name}: INI has no [{section}]")
            continue
        for key, value in table.items():
            if isinstance(value, (dict, list)):
                problems.append(f"{path.name}: [{section}] {key} is a table or array")
                continue
            if not ini.has_option(section, key):
                problems.append(f"{path.name}: INI has no [{section}] {key}")
                continue
            want = toml_as_text(value)
            got = ini_meaning(ini.get(section, key))
            # A float TOML reads as 2.0 was written `2.0`; compare as numbers.
            if isinstance(value, float):
                try:
                    same = float(got) == value
                except ValueError:
                    same = False
            else:
                same = got == want
            if not same:
                problems.append(f"{path.name}: [{section}] {key}: TOML means {want!r}, INI means {got!r}")
    for section in ini.sections():
        if section not in toml:
            problems.append(f"{path.name}: TOML has no [{section}]")
    return problems


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "conf_samples")
        build = ["cc", "-std=c23", "-Wall", "-Wextra", "-Werror",
                 "-Iinclude", "-Ivendor/proven/include", "-Ivendor/proven/platform",
                 *SOURCES, "-lm", "-o", exe]
        done = subprocess.run(build, cwd=ROOT, capture_output=True, text=True)
        if done.returncode != 0:
            print("check-conf-format: the sample writer did not build", file=sys.stderr)
            print(done.stderr[-2000:], file=sys.stderr)
            return 2
        out = os.path.join(tmp, "out")
        os.mkdir(out)
        done = subprocess.run([exe, out], capture_output=True, text=True)
        if done.returncode != 0:
            print(f"check-conf-format: the sample writer failed\n{done.stderr}", file=sys.stderr)
            return 2

        files = sorted(pathlib.Path(out).glob("*.ini"))
        if not files:
            print("check-conf-format: no sample files were written — nothing measured", file=sys.stderr)
            return 1

        problems, values = [], 0
        for path in files:
            problems += check_file(path)
            values += sum(len(t) for t in tomllib.loads(path.read_text("utf-8")).values()
                          if isinstance(t, dict)) if not problems else 0
        if problems:
            for p in problems:
                print(f"  - {p}", file=sys.stderr)
            print(f"check-conf-format: {len(problems)} problem(s) in {len(files)} file(s)", file=sys.stderr)
            return 1
        print(f"check-conf-format: ok — {len(files)} files, {values} values, the same under TOML and INI")
        return 0


if __name__ == "__main__":
    sys.exit(main())
