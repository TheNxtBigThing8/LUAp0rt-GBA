/* LUAport M1 -- <inttypes.h> for the gpSP closure ONLY.
 *
 * WHY THIS FILE EXISTS, AND WHY IT IS NOT runtime/libc/inttypes.h
 * --------------------------------------------------------------
 * gpsp/libretro/libretro-common/include/retro_common_api.h does this:
 *
 *     #include <inttypes.h>
 *     #ifndef PRId64
 *     #error "inttypes.h is being screwy"
 *     #endif
 *
 * runtime/libc/inttypes.h deliberately provides only <stdint.h> and states in
 * its own header comment that "the PRI* macros are never used by the core" --
 * which was true for fixGB and for M0. It is not true for gpSP: gba_memory.c
 * includes streams/file_stream.h, which pulls retro_common_api.h, which fails
 * that #error. That is a HARD COMPILE FAILURE, not a warning.
 *
 * This directory is on the M1 include path ONLY (see the Makefile's M1FLAGS).
 * runtime/libc/ is left byte-for-byte alone, so M0 keeps compiling against the
 * exact headers it was measured with and the M0 non-regression check stays
 * meaningful. Shadowing here rather than editing there is the whole point.
 *
 * The values below are the LP64 (x86_64 System V) ones, which is the only ABI
 * this payload is ever built for: int64_t is `long`, so the length modifier is
 * "l" and not "ll".
 */
#ifndef LUAPORT_M1_INTTYPES_H
#define LUAPORT_M1_INTTYPES_H

#include <stdint.h>

/* 64-bit */
#define PRId64  "ld"
#define PRIi64  "li"
#define PRIu64  "lu"
#define PRIo64  "lo"
#define PRIx64  "lx"
#define PRIX64  "lX"

#define SCNd64  "ld"
#define SCNi64  "li"
#define SCNu64  "lu"
#define SCNo64  "lo"
#define SCNx64  "lx"

/* 32-bit */
#define PRId32  "d"
#define PRIi32  "i"
#define PRIu32  "u"
#define PRIo32  "o"
#define PRIx32  "x"
#define PRIX32  "X"

#define SCNd32  "d"
#define SCNi32  "i"
#define SCNu32  "u"
#define SCNo32  "o"
#define SCNx32  "x"

/* 16-bit */
#define PRId16  "d"
#define PRIu16  "u"
#define PRIx16  "x"

/* 8-bit */
#define PRId8   "d"
#define PRIu8   "u"
#define PRIx8   "x"

/* pointer-sized -- retro_common_api.h uses PRIuPTR for STRING_REP_USIZE */
#define PRIdPTR "ld"
#define PRIiPTR "li"
#define PRIuPTR "lu"
#define PRIoPTR "lo"
#define PRIxPTR "lx"
#define PRIXPTR "lX"

/* fast/least aliases, for completeness */
#define PRIdMAX "ld"
#define PRIuMAX "lu"
#define PRIxMAX "lx"

#endif
