#ifndef LC_COMMON_H
#define LC_COMMON_H

#if __GNUC__ >= 3
/* Assume that a flexible array member at the end of a struct
 * can be defined thus: T arr[]; */
#define FLEXIBLE_ARRAY
#else
/* Assume that it must be defined thus: T arr[0]; */
#define FLEXIBLE_ARRAY 0
#endif

#include "Arch.h"
#include <stdint.h>
#include <stddef.h>
#include <assert.h>

/* Static (compile-time) assertions. */
#define LC_ASSERT_NAME2(name, line)    name ## line
#define LC_ASSERT_NAME(line)           LC_ASSERT_NAME2(lc_assert_, line)
#define LC_STATIC_ASSERT(cond) \
  enum { LC_ASSERT_NAME(__LINE__) = 1/!!(cond) }
/*   extern void LC_ASSERT_NAME(__LINE__)(int STATIC_ASSERTION_FAILED[(cond)?1:-1]) */

#define LC_ASSERT(expr)                assert(expr)

#if LC_ARCH_BITS == 32

typedef uint16_t HalfWord;
typedef uint32_t Word;
typedef int32_t  WordInt;
#define FMT_Word  "u"
#define FMT_WordX "x"
#define FMT_Int   "d"
#define FMT_WordLen "8"

#elif LC_ARCH_BITS == 64

typedef uint32_t HalfWord;
typedef uint64_t Word;
typedef int64_t  WordInt;
#define FMT_Word  FMT_Word64
#define FMT_WordX FMT_Word64X
#define FMT_Int   FMT_Int64
#define FMT_WordLen "16"

#else
#error "Only 32 bit and 64 bit architectures supported."
#endif

/* LC_STATIC_ASSERT(sizeof(void*) == sizeof(Word)); */

typedef uint8_t  u1;
typedef uint16_t u2;
typedef uint32_t u4;
typedef uint64_t u8;

typedef int8_t   i1;
typedef int16_t  i2;
typedef int32_t  i4;
typedef int64_t  i8;

/* LC_STATIC_ASSERT(sizeof(u1) == 1); */
/* LC_STATIC_ASSERT(sizeof(u2) == 2); */
/* LC_STATIC_ASSERT(sizeof(u4) == 4); */
/* LC_STATIC_ASSERT(sizeof(u8) == 8); */

#define u4ptr(p) ((u4)(intptr_t)(void *)(p))

#if LC_ARCH_ENDIAN == LAMBDACHINE_BE
#define LC_ENDIAN_LOHI(lo, hi)  hi lo
#else
#define LC_ENDIAN_LOHI(lo, hi)  lo hi
#endif

#ifndef cast
#define cast(t, exp)    ((t)(exp))
#endif

#define cast_byte(i)	cast(u1, (i))

typedef int bool;
enum { false = 0, true = 1 };

#define wordsof(x)  ((sizeof(x) + sizeof(Word)-1) / sizeof(Word))
#define countof(x)  (sizeof(x) / sizeof(*x))

/* 

Inlining 
--------

INLINE_HEADER: inline functions in header files (like macros)
EXTERN_INLINE: functions that should be inlined but also be callable
from separately compiled modules.

*/
#if defined(__GNUC__) || defined( __INTEL_COMPILER)

# define INLINE_HEADER static inline

// Comment from GHC's Rts.h:
// 
// The special "extern inline" behaviour is now only supported by gcc
// when _GNUC_GNU_INLINE__ is defined, and you have to use
// __attribute__((gnu_inline)).  So when we don't have this, we use
// ordinary static inline.
//
// Apple's gcc defines __GNUC_GNU_INLINE__ without providing
// gnu_inline, so we exclude MacOS X and fall through to the safe
// version.
//
#if defined(__GNUC_GNU_INLINE__) && !defined(__APPLE__)
#  if defined(KEEP_INLINES)
#    define EXTERN_INLINE inline
#  else
#    define EXTERN_INLINE extern inline __attribute__((gnu_inline))
#  endif
#else
#  if defined(KEEP_INLINES)
#    define EXTERN_INLINE
#  else
#    define EXTERN_INLINE INLINE_HEADER
#  endif
#endif

#elif defined(_MSC_VER)
# define INLINE_HEADER __inline static
# if defined(KEEP_INLINES)
#  define EXTERN_INLINE __inline
# else
#  define EXTERN_INLINE __inline extern
# endif
#else
# error "Don't know how to inline functions with your C compiler."
#endif

#define MSB_u4(hh,hl,lh,ll) \
  ((hh) << 24 | (hl) << 16 | (lh) << 8 | (ll))
  

#endif