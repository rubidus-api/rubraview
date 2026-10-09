/* alz_bzip2_rename.h - FultaArc: gives the ALZ-framed bzip2 decoder in this directory its own symbol names, so that
 * it links beside the standard bzip2 1.0.8 in vendor/bzip2 (D-005 #2: the two are kept apart). Force-included when
 * this directory's sources are compiled (Makefile: -include), and included by alz_bzlib.h for its users. */
#ifndef FULTA_ALZ_BZIP2_RENAME_H
#define FULTA_ALZ_BZIP2_RENAME_H
#define BZ2_bzBuffToBuffDecompress ALZBZ2_bzBuffToBuffDecompress
#define BZ2_bzDecompress           ALZBZ2_bzDecompress
#define BZ2_bzDecompressEnd        ALZBZ2_bzDecompressEnd
#define BZ2_bzDecompressInit       ALZBZ2_bzDecompressInit
#define BZ2_bzlibVersion           ALZBZ2_bzlibVersion
#define BZ2_crc32Table             ALZBZ2_crc32Table
#define BZ2_decompress             ALZBZ2_decompress
#define BZ2_hbAssignCodes          ALZBZ2_hbAssignCodes
#define BZ2_hbCreateDecodeTables   ALZBZ2_hbCreateDecodeTables
#define BZ2_hbMakeCodeLengths      ALZBZ2_hbMakeCodeLengths
#define BZ2_indexIntoF             ALZBZ2_indexIntoF
#define BZ2_rNums                  ALZBZ2_rNums
#define bz_internal_error          alzbz_internal_error
#endif
