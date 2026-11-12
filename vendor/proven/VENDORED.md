# Vendored proven_c_lib

This directory is a copied vendor snapshot of proven_c_lib.

- Upstream source: `proven_c_lib`
- Snapshot commit: `5236974e` (tag `v0.114.0`), taken with
  `git archive v0.114.0 include src platform crypto LICENSE THIRD_PARTY_NOTICES.md` into an emptied copy; every file
  matches the tag (D-336)
- Snapshot date: `2026-11-13`
- v0.114.0 adds printf's %g to the formatter (RFC-0046): `{:g}` is now %g (six significant digits, `{:.Ng}` N), the
  shortest round-trip form moved to `{:r}`; `{:G}`, `{:E}`, `#` on f/e/g; the scanner reads a double under f/e/E/g/G.
  Rubraview calls no `{:g}` (its own %g is `rubraview_g_text`).
- v0.113.0 moved cryptography and certificates to proven_crypto (`crypto/`) and compression to proven_codec
  (`codec/`). `crypto/` is taken for SHA-256 (`proven/sha2.h` since v0.113.0: FultaArc's 7z and AES keys, the
  viewer's plug-in check, D-329) and built into the same static library as the base (`PROVEN_SRCS`,
  `PROVEN_INCLUDES` in the Makefile); `crypto/include` is on the viewer's include path.
- Not taken: `codec/` (DEFLATE, Zstandard, Brotli; FultaArc has its own decoders and Rubraview calls none) and `net/`
  (event loop, TLS, QUIC, HTTP, WebSocket); Rubraview calls neither.
- FultaArc (`vendor/fultaarc`) is built on this copy too: one copy for both. Its own upstream vendored
  v0.113.0 (since `0dccf16`) and includes `proven/sha2.h` itself; it is compiled with
  `-Ivendor/proven/crypto/include`, its sources unchanged, and the gate's archive tests read and write with it.
- Previous snapshots: `bd93f44d` (`v0.113.0`, 2026-11-11, D-328); `f4002aca` (`v0.112.4`, 2026-11-10, D-320); `cbbaf08d` (`v0.110.0`, 2026-11-03, D-273); `1d9989c4` (`v0.108.0`, 2026-11-03, D-264); `2e5a35f` (`v0.76.0`, 2026-10-22, RV-160, D-194); `981911b` (`v0.6.0` + 3, 2026-10-08); `v0.6.0` (`e62cee2`, 2026-10-01); `v0.1.1` (2026-09-22)
- Upstream license: MIT (see LICENSE)

Do not modify the original sibling checkout from this project. If a proven defect is
found, report it upstream and resync after the source project is fixed.
