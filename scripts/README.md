# Project Scripts

Reusable local automation for mechanical project work. Tokens are for judgment; these are for everything else.

| Script | Use |
|---|---|
| `gate.sh` | the minimum verification tier for what changed (`--explain` to see why); configure `TEST_CMD` in `gate.conf` |
| `project-check.sh` | the full local check (`gate.sh` runs it at t2) |
| `context-budget.sh` | fail when `AGENTS.md` + `CONTEXT.md` exceed the default-context budget |
| `brief.sh` | session start in one call: resume packet, git status, active focus, open plans |
| `worklog.sh` | the work log, from `git log` |
| `new-plan.sh`, `archive-plan.sh` | T3 plan files |
| `changelog-entry.sh` | one public changelog line |
| `check-tools.sh` | required local tools |
| `check-settings.py` | every setting read somewhere is offered in the settings window, and every "wired" one is read |
| `check-conf-format.py` | every configuration file the viewer writes parses the same under TOML (`tomllib`) and INI (`configparser`) — D-13 |

POSIX shell, Linux only. Scripts must not print secrets or local absolute paths, must check required tools before working, must keep generated output under declared project paths, and must not use the network unless the user asks.
