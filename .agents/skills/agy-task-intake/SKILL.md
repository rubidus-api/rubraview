---
name: agy-task-intake
description: Use at the start of any non-trivial code-changing task in this repository, before editing files — to establish project root, private sidecar, repository state, build system, and which verification will prove the change. Also use when a task turns out to be larger than expected, or when unsure how much of the repository to read.
---

# Task intake

Do this proportionally: a typo needs one step, a schema change needs all of them.

## 1. Ground yourself

1. Project root (the directory containing `.git`).
2. Applicable `AGENTS.md` — workspace and project.
3. Whether a `<project>_private/` or `<project>_internal/` sidecar exists.
4. `git status --short` — is the tree already modified?
5. Build system and language from evidence (lockfiles, `Makefile`, `nob.c`, `build.gradle`, `package.json`).
6. Whether existing uncommitted changes overlap the files you are about to touch.
   If they do, read `git diff` on those files before editing.

## 2. Classify

- **T0** typo, one constant, isolated config — act directly.
- **T1** one function/component/class/section — read callers and tests, patch locally.
- **T2** several modules, API + implementation, parser + tests — write a short plan first.
- **T3** concurrency, memory ownership, persistence format, protocol, schema,
  crypto, public API, build migration, large refactor — inspect the architecture
  and the verification path before writing anything.

## 3. Acquire context narrowly

`search → identify → read the relevant region → check callers and tests`

Priority: interfaces and declarations → implementation → direct callers → tests →
build/config → the doc that states the contract. When behavior is unclear, get
executable evidence (compiler diagnostic, failing test, runtime output, sanitizer
report) instead of guessing from names.

## 4. Decide the verification before you edit

Name the exact command that will show the change works. If no such command
exists, say so in the final report rather than implying it was verified.

## Startup checklist

```
[ ] Read applicable AGENTS.md files.
[ ] Identify project root.
[ ] Identify private companion if relevant.
[ ] Inspect git status.
[ ] Inspect overlapping existing diff.
[ ] Locate implementation and relevant tests.
[ ] Make the smallest coherent change.
[ ] Run targeted verification.
[ ] Run broader verification when appropriate.
[ ] Inspect final diff and status.
[ ] Check public/private boundary.
[ ] Report actual verification results.
```
