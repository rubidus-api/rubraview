---
name: agy-handoff
description: Use for long multi-session work when this project has no CONTEXT.md resume packet — to keep a short recovery checkpoint, or to resume from one. Do not use in projects that already track state in CONTEXT.md.
---

# Working state checkpoint

**First check whether this project already has `CONTEXT.md` with a resume packet.**
If it does, update that and stop — do not create a second state file.

Otherwise keep one short file at `<project>_private/.agents/STATE.md`:

```markdown
# Objective
# Current hypothesis
# Files relevant to the task
# Changes completed
# Verification
# Known failures
# Next action
```

It is a checkpoint, not a transcript. Do not record internal reasoning, raw
secrets, large command output, or copies of source files. Its purpose is recovery
and handoff, not archival logging.

The repository's actual contents and executable behavior always outrank this file.
If they disagree, the file is stale — fix the file.
