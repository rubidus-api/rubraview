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

#if defined(__GNUC__)
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
