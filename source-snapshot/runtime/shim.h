#ifndef SHIM_H
#define SHIM_H

#include "core.h"

/* The freestanding libc that lets the fixGB core compile unmodified.
 *
 * fixGB includes <stdio.h>, <string.h>, <malloc.h> and <time.h>; src/libc/
 * shadows those with headers that declare exactly what the core touches, and
 * this translation unit implements them on top of PS5 sceKernel calls. */

struct shim_ps5 {
    void *gadget;
    void *open;
    void *read;
    void *write;
    void *close;
    void *lseek;
    void *mkdir;
    void *sendto;
    void *gettimeofday;
    s32   log_fd;
    u8   *log_sa;      /* sockaddr_in of the PC listening for UDP logs */
};

/* Must be the first thing _start does -- every other shim entry point
   assumes these pointers are live. */
void shim_init(const struct shim_ps5 *ps5, void *arena, u64 arena_size);

/* Raw log line, bypassing printf formatting. Safe before shim_init. */
void klog(const char *msg);

/* Set to 0 to compile every core printf down to nothing. Diagnostics from
   the emulation core are chatty at load time but silent during play. */
#ifndef GB_VERBOSE
#define GB_VERBOSE 1
#endif

/* The arena backs malloc(). fixGB allocates the ROM buffer and the APU output
   buffer per game and frees them in its deinit path, so the whole arena is
   simply rewound between games instead of maintaining a real free list. */
void  arena_reset(void);
u64   arena_used(void);

#ifdef LUAPORT_SESSION_REUSE
/* ------------------------------------------------- M16 session reuse -------
 *
 * ***** COMPILED ONLY WHEN LUAPORT_SESSION_REUSE IS DEFINED, AND THAT DEFINE
 * IS SET ONLY ON M16 TARGETS. ***** Every frozen milestone -- M13C above all --
 * preprocesses this header with the macro UNDEFINED, so the declarations below
 * do not exist in those translation units and shim.o is byte-identical to the
 * one the frozen artifact was linked from.
 *
 * WHY A MARK/REWIND PAIR AND NOT A REAL free(). malloc() here is a bump
 * allocator (see the note above free()), so the only reclamation it can
 * honestly offer is a stack discipline: remember the watermark, then return to
 * it. That is sufficient for the ONE thing M16 needs -- gpSP's per-game ROM
 * buffers are allocated together, used together and released together -- and it
 * is a GENERIC allocator service with no knowledge of GBA, gpSP or any core.
 *
 * arena_reset() already exists and is NOT enough: it drops the watermark to
 * zero, which would invalidate the video and pad allocations the runtime makes
 * before any game starts. The mark is how a caller says "keep everything up to
 * here".
 *
 * ***** IT NEVER GROWS THE ARENA. ***** Neither function moves arena_size or
 * touches arena_base; the only field either one writes is arena_pos, and
 * arena_rewind only ever moves it DOWNWARDS. */

/* The current watermark. Pair it with arena_rewind() to release everything
 * allocated after this point. */
u64   arena_mark(void);

/* Return the allocator to `mark`, releasing every allocation made after it and
 * PRESERVING every allocation made before it.
 *
 * ***** FAILS CLOSED. ***** Returns 1 on success and 0 without changing one
 * byte of allocator state if `mark` is not a watermark this arena can currently
 * be at -- that is, if it lies AHEAD of the current position. A mark from a
 * future that never happened, a mark from a different arena, or an
 * uninitialised variable therefore REFUSES rather than silently handing out
 * memory that is still in use.
 *
 * The bytes between `mark` and the old position are NOT zeroed, exactly as
 * malloc() does not zero what it returns. A caller that needs zeroed memory
 * calls calloc(), as it always did. */
int   arena_rewind(u64 mark);

#endif /* LUAPORT_SESSION_REUSE */

#endif
