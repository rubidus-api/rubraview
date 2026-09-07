# `.agents/` — agy only

This directory configures the `agy` (Antigravity) CLI for this project:

```
rules/GEMINI.md   always-on project rules (the filename matters — agy ignores others here)
skills/*/SKILL.md loaded on demand, when the skill's description matches the task
agy.conf          what counts as verification, what is safe, what is forbidden
hooks.json        lifecycle hooks; hooks/hook.py implements them
mcp_config.json   local only, never committed (see mcp_config.example.json)
```

Other AI agents do not read this directory and should not be pointed at it.
The general policy is installed machine-wide at `~/.gemini/config/GEMINI.md`
from the rootsator kit (`dist/boot/agy/global/install-agy-global.sh`).

## Which model

`agyp` does not choose the model. It reads the workspace-wide file

```
$AGY_CONF                         when explicitly set; missing means no fallback
$AI_SHARE/agy-model.conf          workspace selected by the environment
~/ai-share/agy-model.conf         conventional personal workspace
~/.config/agy/model.conf          per-user fallback
```

The chosen file carries `AGY_MODEL=<model-id>` and `AGY_EFFORT=<effort>`.

and passes `--model`/`--effort` for you. Models are retired and renamed on
Google's schedule, and several projects here call agy; keeping the name in one
file makes a bump one line instead of a tour of repositories. A `--model` or
`--effort` you type yourself wins, and `AGY_MODEL=... agyp ...` overrides for one
call. With no config file agyp passes nothing and agy uses its own default.

## Invoking agy

Use **`agyp`**, not `agy`. agy does not treat your shell's directory as its
workspace: called plainly it runs in a scratch directory and sees neither your
files nor these rules. `agyp` finds the project root and passes `--add-dir`.

```
agyp                       interactive here
agyp -p 'question'         one-shot   (attach the prompt: --print='...')
agyp plan '<task>'         agy plan mode, no edits
agyp codex-plan '<task>'   codex drafts a plan, agy checks it and proceeds
```

`plan` and `codex-plan` start interactively at a terminal and non-interactively
when piped, so the same command works from a prompt and from a script.

## Two passes, because one pass of a small model is not evidence

Both of these are **opt-in**: nothing about an ordinary `agyp` run changes, and
neither costs anything unless you type it. They exist because the two things this
model actually gets wrong are *believing its own first answer* and *not reading
the diagnostic* — and both are fixable by a script rather than by more rules.

```
agyp review ['<what was asked>']   a FRESH session reads `git diff HEAD`
agyp loop '<task>'                 work, verify, feed the failure back, bounded
```

**`review`** starts a new session that did not write the change, hands it the diff
and nothing else, and asks for defects it can point at — `file:line what breaks` —
ending in `VERDICT: PASS` or `VERDICT: FAIL`. Fresh context is the entire point:
the same session asked to check itself re-reads its own reasoning and agrees with
it. `AGYP_REVIEWER=codex` sends the same prompt to codex instead, so the two can
be compared on real work.

**`loop`** runs agy, then runs the project's verification, and on failure hands
back **only the squeezed diagnostics** — head, the lines that look like errors,
tail — never the whole log, which a small model reads worst of all. It stops when
verification passes, after `AGYP_ROUNDS` rounds (default 3), or **as soon as the
same failure appears twice**: an identical diagnostic means the hypothesis is
wrong, and another round would only stack a second speculative patch on the first.
The verification command is `VERIFY_CMD` from `agy.conf`, or `scripts/gate.sh`,
or `scripts/project-check.sh`; with none of them, `loop` refuses to run rather
than iterate without evidence.

Both are unmeasured as quality interventions. What is measured is that this repo's
*in-session* gates (plan, stop) cost ~2.5x tokens for no better outcome — which is
why these two are outside the session and outside the default path. The harness
that would settle it is `docs/benchmark/` in the rootsator repository.

Headless runs (`--print`, no terminal) auto-deny anything the guard answers with
`ask`, and settings allow-rules do not apply. Add
`--dangerously-skip-permissions` there: the guard's hard denials still win, so
the destructive set stays blocked while ordinary work stops stalling.

## What the hooks enforce

By default (`HOOKS=guard-only` in `agy.conf`) one thing, cheaply:

- Destructive or outward-facing commands are **denied**, including under
  `--dangerously-skip-permissions`. Verification and read-only inspection are
  auto-approved, so headless runs work without blanket approval.

Two further gates ship but are **off by default**, because measurement did not
justify their cost (they roughly doubled tokens and tripled wall clock without
improving compliance or outcome):

- **Plan gate** — refuses the first edit of a session until a one-line plan is
  recorded with `sh .agents/hooks/plan.sh 'objective | files | approach |
  verification command'`. Gives up after three refusals.
- **Stop gate** — if files changed and no verification succeeded, blocks one
  attempt to finish and names `VERIFY_CMD`. Once per conversation.

Turn them on deliberately, for work where flailing is expensive: set
`HOOKS=gates` (and `PLAN_GATE=on`) in `agy.conf`, then reinstall the layer.

Edit `agy.conf` when a command is missing from the allow or deny sets. `VERIFY_CMD`
is worth setting even with the gates off: `agyp loop` uses it as its evidence.

## rules/GEMINI.md is the part that measurably works

Of everything in this directory, the measured benefit sits in the project rules:
real build and test commands, the files worth reading first, what must not be
touched. The template ships unfilled, and `agy-isolation-check.sh` reports a file
that still contains its placeholders — a rule file saying `<command>` teaches that
model nothing.
