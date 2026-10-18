/* Hand-written for rubraview's vendored build (RV-069). See jconfig.h. */
#define BUILD  "rubraview-vendored"

#define HIDDEN  __attribute__((visibility("hidden")))
#define INLINE  inline __attribute__((always_inline))
#define THREAD_LOCAL  __thread

#define PACKAGE_NAME  "libjpeg-turbo"
#define VERSION  "3.0.4"

#if defined(__SIZEOF_SIZE_T__)
#define SIZEOF_SIZE_T  __SIZEOF_SIZE_T__
#else
#define SIZEOF_SIZE_T  8
#endif

/* __builtin_ctzl counts in a long, and jcphuff.c hands it a size_t: the two
   are one size on Linux, but a long is 32 bits on 64-bit Windows, where the
   upper half was lost and every progressive JPEG failed with "DCT coefficient
   out of range" (found 2026-10-18, 1.16.0). Elsewhere the plain loop counts. */
#if defined(__GNUC__) && defined(__SIZEOF_LONG__) && defined(__SIZEOF_SIZE_T__) && __SIZEOF_LONG__ == __SIZEOF_SIZE_T__
#define HAVE_BUILTIN_CTZL
#endif

#if defined(__has_attribute)
#if __has_attribute(fallthrough)
#define FALLTHROUGH  __attribute__((fallthrough));
#else
#define FALLTHROUGH
#endif
#else
#define FALLTHROUGH
#endif

#ifndef BITS_IN_JSAMPLE
#define BITS_IN_JSAMPLE  8
#endif

#undef C_ARITH_CODING_SUPPORTED
#undef D_ARITH_CODING_SUPPORTED
#undef WITH_SIMD
