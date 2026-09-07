---
name: agy-change-blast-radius
description: Use before modifying anything outside ordinary source code — Dockerfiles, compose files, CI config, package manifests, lockfiles, toolchain or PATH setup, system packages — or before editing a file that may be generated, or before adding/upgrading an external dependency.
---

# Blast radius

## Environment and build system

Environment changes reach further than source changes. Be conservative with
`Dockerfile`, `compose.yaml`, Gradle configuration, package manifests, compiler
configuration, CI configuration, PATH/toolchain setup, and system packages.

Before changing the environment, ask whether the problem can be solved inside the
project instead. Do not make irreversible container or system modifications
because a command is missing. A persistent environment change MUST be intentional
and written down.

Do not replace a project's build system in order to verify one change.

## Generated files

Before editing a file, determine whether it is generated:

`source / template / schema → generator / compiler → generated artifact`

Edit the authoritative input, not the output. Regenerate only when the project's
convention says to.

Common signs: a header comment saying so, a matching `.in`/`.tmpl`/schema file, a
build rule that writes the path, or the path living under `build/` or `dist/`.

## External dependencies and APIs

Do not invent API behavior from memory when correctness depends on a
version-specific interface. Prefer, in order:

1. the version pinned in this repository;
2. locally installed headers, type definitions, or docs;
3. authoritative documentation, if lookup is available;
4. existing verified usage elsewhere in this project.

Do not upgrade a dependency because newer documentation describes a nicer API,
and do not regenerate a lockfile unless the task requires it.
