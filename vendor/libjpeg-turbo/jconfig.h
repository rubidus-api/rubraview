/* Hand-written for rubraview's vendored build (RV-069, ledger R003).
   Upstream generates this from jconfig.h.in with CMake; this project
   does not use CMake, so the choices are made here and written down.
   Arithmetic coding and SIMD are off: the lossless transform path
   touches DCT coefficients only, and never runs an IDCT. */
#define JPEG_LIB_VERSION  62
#define LIBJPEG_TURBO_VERSION  3.0.4
#define LIBJPEG_TURBO_VERSION_NUMBER  3000004

/* Arithmetic coding is patent-free now but rare; a JPEG that uses it is
   refused rather than silently mangled. */
/* #undef C_ARITH_CODING_SUPPORTED */
/* #undef D_ARITH_CODING_SUPPORTED */

#define MEM_SRCDST_SUPPORTED  1

/* #undef WITH_SIMD */

#ifndef BITS_IN_JSAMPLE
#define BITS_IN_JSAMPLE  8
#endif

#ifdef _WIN32

#undef RIGHT_SHIFT_IS_UNSIGNED

#ifndef __RPCNDR_H__
typedef unsigned char boolean;
#endif
#define HAVE_BOOLEAN

#if !(defined(_BASETSD_H_) || defined(_BASETSD_H))
typedef short INT16;
typedef signed int INT32;
#endif
#define XMD_H

#else

/* #undef RIGHT_SHIFT_IS_UNSIGNED */

#endif
