---
name: agy-lang-c
description: Use when changing, debugging, or verifying C code in this project — including crashes, memory errors, undefined behavior, compiler or sanitizer diagnostics, and questions about how to build or test the C sources.
---

# C work

## Build system

Detect what the project already uses (Make, CMake, Meson, `nob.c`, or custom
scripts) and use it. Do not replace it to perform verification, and do not add
aggressive global compiler flags to an existing project to test one change unless
it is safe and temporary.

## Failure classes worth checking

undefined behavior · out-of-bounds access · use-after-free · lifetime errors ·
integer overflow and conversion · signed/unsigned mistakes · aliasing · alignment ·
object representation · incorrect error paths · resource leaks · portability
assumptions.

## Evidence

Use the project's own warning, sanitizer, static-analysis, and test targets when
they exist. For memory and lifetime bugs prefer sanitizer evidence (ASan/UBSan)
over reading the code and reasoning about it — a sanitizer report names the line
and the allocation; a plausible explanation does not.

Compiling clean is `V0`. It is not verification when tests exist.
