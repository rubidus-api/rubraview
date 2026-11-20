# Vendored proven_c_lib

This directory is a copied vendor snapshot of proven_c_lib.

- Upstream source: `proven_c_lib`
- Snapshot commit: `bbabaf2a` (tag `v0.119.1`), taken with
  `git archive v0.119.1 include src platform crypto LICENSE THIRD_PARTY_NOTICES.md` into an emptied copy; every file
  matches the tag (D-336, D-371, D-381)
- Snapshot date: `2026-11-20`
- v0.119.1 (five files): the ChaCha generator's counter stepped once at its low word's wrap with the reseed every MiB
  on eight lanes, the `%e`/`%g` digits' room check one byte larger, the vector keystream's state wiped as
  `secure_zero` does. Rubraview seeds no generator; its formatting calls `{:g}` nowhere (its own %g is
  `rubraview_g_text`).
- v0.115.0 to v0.119.0 in the parts taken: `proven_chacha_rng_t` erases its key every 1 KiB (from byte 960 the same
  seed gives other bytes; `PROVEN_CHACHA_RNG_NO_ERASURE` the old stream) and reseeds in a forked child (POSIX);
  `proven_random_fast_bytes`, `proven_random_fork_generation` (0 on Windows); `proven_aes_rng_t` in proven_crypto; the
  decimal reader's exponent found in one step. Rubraview seeds no generator and calls none of them; FultaArc's AES
  and SHA-256 calls are unchanged. The rest (WebSocket, DEFLATE, Zstandard, Brotli) is in `net/` and `codec/`, not
  taken.
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
- Previous snapshots: `eb852acf` (`v0.119.0`, 2026-11-19, D-371); `5236974e` (`v0.114.0`, 2026-11-13, D-336); `bd93f44d` (`v0.113.0`, 2026-11-11, D-328); `f4002aca` (`v0.112.4`, 2026-11-10, D-320); `cbbaf08d` (`v0.110.0`, 2026-11-03, D-273); `1d9989c4` (`v0.108.0`, 2026-11-03, D-264); `2e5a35f` (`v0.76.0`, 2026-10-22, RV-160, D-194); `981911b` (`v0.6.0` + 3, 2026-10-08); `v0.6.0` (`e62cee2`, 2026-10-01); `v0.1.1` (2026-09-22)
- Upstream license: MIT (see LICENSE)

Do not modify the original sibling checkout from this project. If a proven defect is
found, report it upstream and resync after the source project is fixed.
