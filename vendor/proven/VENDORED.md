# Vendored proven_c_lib

This directory is a copied vendor snapshot of proven_c_lib.

- Upstream source: `proven_c_lib`
- Snapshot commit: `2e5a35f` (tag `v0.76.0`), taken with
  `git archive v0.76.0 include src platform LICENSE THIRD_PARTY_NOTICES.md`; every file matches the tag (RV-160, D-194)
- Snapshot date: `2026-10-22`
- FultaArc (`vendor/fultaarc`) is built on this copy too: one copy for both. Its own upstream still names `981911b`;
  every FultaArc source compiles against this one unchanged, and the gate's archive tests read and write with it.
- Previous snapshots: `981911b` (`v0.6.0` + 3, 2026-10-08); `v0.6.0` (`e62cee2`, 2026-10-01); `v0.1.1` (2026-09-22)
- Upstream license: MIT (see LICENSE)

Do not modify the original sibling checkout from this project. If a proven defect is
found, report it upstream and resync after the source project is fixed.
