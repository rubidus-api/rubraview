/* lhadec_rename.h - FultaArc: gives the vendored lhadec library (src/lha/lhadec/, the LHA clean room's deliverable,
 * kept byte-identical) FultaArc-prefixed link symbols, so a program that links libfultaarc.a beside another LHA
 * library (lhasa also exports lha_* names) sees no clash. Force-included, through lhadec_shim.h, when lhadec's own
 * sources are compiled (Makefile: -include), and included by the glue (lha.c) before lhadec.h, so every definition
 * and every call is renamed the same way. The list is every global the two objects define (nm, 2026-10-16); a symbol
 * missing here would show up as an unprefixed `T lha_*` in `nm` of the library. */
#ifndef FULTA_LHADEC_RENAME_H
#define FULTA_LHADEC_RENAME_H
#define lha_close           fa_lha_close
#define lha_crc16           fa_lha_crc16
#define lha_decode_buffer   fa_lha_decode_buffer
#define lha_decode_method   fa_lha_decode_method
#define lha_dos_time_fields fa_lha_dos_time_fields
#define lha_extract         fa_lha_extract
#define lha_in_need         fa_lha_in_need
#define lha_method_name     fa_lha_method_name
#define lha_next            fa_lha_next
#define lha_open            fa_lha_open
#define lha_sink_write      fa_lha_sink_write
#define lha_skip            fa_lha_skip
#define lha_src_getc        fa_lha_src_getc
#define lha_src_read        fa_lha_src_read
#define lha_strerror        fa_lha_strerror
#endif
