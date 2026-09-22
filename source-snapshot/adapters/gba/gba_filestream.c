/* LUAport M1 -- adapters/gba/gba_filestream.c
 *
 * The five libretro filestream entry points gpSP actually calls, implemented
 * over LUAport's existing FILE* shim.
 *
 * WHY THIS FILE IS THE WHOLE COST OF "PLAN A"
 * -------------------------------------------
 * M1 compiles the gpSP core files directly behind a LUAport fixture and drops
 * the libretro frontend entirely. That removes libretro.c, all ten
 * libretro-common/ *.c translation units, the RetroArch options UI, the
 * netpacket transport and the RGB565 blit paths -- and, as a direct
 * consequence, two large data objects that are referenced ONLY from
 * libretro.c: gba_cc_lut (65,536 B of JIT-backed .rodata) and
 * open_gba_bios_rom (16,384 B of .data).
 *
 * The one thing Plan A does NOT get for free is file access. gpsp/gba_memory.c
 * line 21 includes <streams/file_stream.h>, and calls exactly five of its
 * functions. Measured call sites, all in gpsp/gba_memory.c:
 *
 *     filestream_open      2676, 3095
 *     filestream_seek      2342, 2750
 *     filestream_read      2344, 2715, 2778, 3101
 *     filestream_close     2460, 2694, 2765, 3102
 *     filestream_get_size  2691
 *
 * Retaining upstream's implementation would mean pulling in file_stream.c +
 * vfs_implementation.c + file_path.c -- roughly 89 KB of source and a POSIX/VFS
 * surface the shim does not have -- to satisfy five calls.
 *
 * It is cheap to avoid because RFILE IS FULLY OPAQUE upstream:
 *
 *     gpsp/libretro/libretro-common/include/streams/file_stream.h:45
 *         typedef struct RFILE RFILE;
 *
 * No definition is exposed, so this file may define `struct RFILE` however it
 * likes. THE UPSTREAM HEADER IS USED UNMODIFIED -- only the implementation is
 * LUAport-owned, which is what keeps the "do not modify gpsp/" rule intact
 * while still resolving the dependency honestly.
 *
 * NOT IMPLEMENTED, DELIBERATELY: filestream_write, filestream_truncate,
 * filestream_tell, filestream_flush, filestream_getc/gets/putc, printf,
 * filestream_read_file, filestream_vfs_init, and everything else the header
 * declares. They have no call site in the Tier 1 closure. Implementing them
 * "for completeness" would add unmeasured code to a size gate whose entire
 * purpose is to measure the minimum -- and would quietly import write support
 * that M1 is explicitly not authorised to have.
 *
 * M1 SCOPE REMINDER: nothing here is called during the M1 run. The fixture
 * does not load a ROM and does not load a BIOS. This file exists so the link
 * is honest and complete, and so its cost appears in the baseline.
 */

#include <stdio.h>      /* runtime/libc/stdio.h -- FILE, fopen, fread, ...   */
#include <stddef.h>
#include <stdint.h>

#include <streams/file_stream.h>

/* --------------------------------------------------------------- RFILE --
 *
 * Storage strategy: a small STATIC POOL, not malloc.
 *
 * runtime/shim.c's allocator is a bump allocator whose free() is an
 * unconditional no-op (shim.c:63 -- note that the source comment there
 * contradicts the code, which is why this is stated from the code). Every
 * malloc'd RFILE would therefore permanently consume arena, and gpSP opens and
 * closes the gamepak file repeatedly while paging ROM. A fixed pool makes
 * close() genuinely reclaim the slot, costs the arena nothing, and puts the
 * whole cost in .bss -- which, per the size model, contributes ZERO bytes to
 * the blob and ZERO to the JIT reservation.
 *
 * Four slots: gpSP holds at most two open at once (gamepak_file_large, plus a
 * transient BIOS handle in load_bios). Four is two spare and 4 * 24 = 96 bytes
 * of .bss.
 */
#define RFILE_SLOTS 4

struct RFILE {
    FILE *f;
    int   in_use;
};

static struct RFILE rfile_pool[RFILE_SLOTS];

/* ---------------------------------------------------------------- open --
 *
 * mode is a RETRO_VFS_FILE_ACCESS_* bitmask (libretro.h:1883). Only
 * RETRO_VFS_FILE_ACCESS_READ is honoured; both gpSP call sites pass exactly
 * that. A write request is REFUSED rather than silently downgraded to a read:
 * a downgrade would hand back a handle that fails later at an unrelated place,
 * and M1 is not authorised to write anything anyway.
 *
 * hints (RETRO_VFS_FILE_ACCESS_HINT_*) are advisory; both call sites pass
 * HINT_NONE and the shim has no buffering to tune, so it is ignored.
 */
RFILE *filestream_open(const char *path, unsigned mode, unsigned hints)
{
    int i;

    (void)hints;

    if (!path)
        return NULL;

    /* RETRO_VFS_FILE_ACCESS_WRITE is (1 << 1). Anything but a pure read is
       out of scope for M1. */
    if (mode & ~(unsigned)RETRO_VFS_FILE_ACCESS_READ)
        return NULL;

    for (i = 0; i < RFILE_SLOTS; i++) {
        if (!rfile_pool[i].in_use) {
            FILE *f = fopen(path, "rb");
            if (!f)
                return NULL;
            rfile_pool[i].f      = f;
            rfile_pool[i].in_use = 1;
            return &rfile_pool[i];
        }
    }

    return NULL;                        /* pool exhausted */
}

/* ---------------------------------------------------------------- seek --
 *
 * RETRO_VFS_SEEK_POSITION_START/CURRENT/END are 0/1/2 (libretro.h:1896-1898)
 * and SEEK_SET/SEEK_CUR/SEEK_END are also 0/1/2 (runtime/libc/stdio.h:37-39).
 * gpSP passes SEEK_SET at both of its seek call sites, so the two vocabularies
 * coincide -- but the mapping is written out explicitly rather than relying on
 * that coincidence, because a silent renumbering upstream would otherwise turn
 * into a wrong ROM offset with no diagnostic.
 *
 * Returns the resulting absolute offset, or -1. The shim's fseek returns 0 on
 * success, so ftell supplies the return value.
 */
int64_t filestream_seek(RFILE *stream, int64_t offset, int seek_position)
{
    int whence;

    if (!stream || !stream->in_use)
        return -1;

    switch (seek_position) {
        case RETRO_VFS_SEEK_POSITION_START:   whence = SEEK_SET; break;
        case RETRO_VFS_SEEK_POSITION_CURRENT: whence = SEEK_CUR; break;
        case RETRO_VFS_SEEK_POSITION_END:     whence = SEEK_END; break;
        default:                              return -1;
    }

    if (fseek(stream->f, (long)offset, whence) != 0)
        return -1;

    return (int64_t)ftell(stream->f);
}

/* ---------------------------------------------------------------- read --
 *
 * The shim's fread takes (size, count) and returns the COUNT of complete items
 * read. Calling it with size = 1 makes the return value a byte count directly,
 * which is what filestream_read's contract wants -- and it makes a short read
 * (the last, partial 32 KB page of a ROM) report its true length instead of
 * rounding down to zero whole items.
 */
int64_t filestream_read(RFILE *stream, void *data, int64_t len)
{
    size_t got;

    if (!stream || !stream->in_use || !data)
        return -1;
    if (len <= 0)
        return 0;

    got = fread(data, 1, (size_t)len, stream->f);
    return (int64_t)got;
}

/* --------------------------------------------------------------- close --
 *
 * Frees the pool slot. Returns 0 on success, -1 on error, matching upstream.
 */
int filestream_close(RFILE *stream)
{
    int r;

    if (!stream || !stream->in_use)
        return -1;

    r = fclose(stream->f);
    stream->f      = NULL;
    stream->in_use = 0;

    return r == 0 ? 0 : -1;
}

/* ------------------------------------------------------------ get_size --
 *
 * Seek to the end, read the offset, restore the original position. Restoring
 * matters: gpsp/gba_memory.c:2691 calls this on an already-open handle and then
 * continues reading from where it was.
 *
 * gba_memory.c:2687-2694 already guards against a negative or absurd result,
 * so returning -1 on failure is handled correctly by the one caller.
 */
int64_t filestream_get_size(RFILE *stream)
{
    long here;
    long end;

    if (!stream || !stream->in_use)
        return -1;

    here = ftell(stream->f);
    if (here < 0)
        return -1;

    if (fseek(stream->f, 0, SEEK_END) != 0)
        return -1;

    end = ftell(stream->f);

    /* Restore, even if the size read failed -- leaving the handle parked at
       EOF would corrupt the caller's next read in a way that looks like a
       truncated ROM. */
    if (fseek(stream->f, here, SEEK_SET) != 0)
        return -1;

    return end < 0 ? -1 : (int64_t)end;
}

#ifdef LUAPORT_SESSION_REUSE
/* --------------------------------------------- THE POOL CENSUS (M16 ONLY) --
 *
 * ***** COMPILED ONLY FOR M16 TARGETS, AND STRICTLY READ-ONLY. ***** M13C
 * preprocesses this file with LUAPORT_SESSION_REUSE undefined, so
 * gba_filestream.o is byte-identical for every frozen milestone. Neither
 * function opens, closes, seeks, reads or writes anything; they count a static
 * array. No file behaviour changes by one instruction.
 *
 * ***** WHY THIS HAS TO EXIST BEFORE THE LAUNCHER IS RESTRUCTURED. *****
 * gpSP keeps the gamepak file OPEN for the whole session -- demand paging reads
 * from it every time a page is evicted -- and releases it only in memory_term(),
 * which M13C never calls because M13C exits instead of starting a second
 * session. A return-to-menu launcher that forgot memory_term() would therefore
 * leak ONE POOL SLOT PER LAUNCH out of the four declared above.
 *
 * That leak is INVISIBLE UNTIL IT IS FATAL. Launches 1 through 3 would behave
 * perfectly; launch 5 would fail inside filestream_open()'s "pool exhausted"
 * return with no diagnostic naming the real cause, and the failure would look
 * like a corrupt ROM rather than a handle leak. Counting the slots turns a
 * fifth-launch mystery into a first-teardown measurement.
 *
 * THE CONTRACT THE DIAGNOSTIC ASSERTS: the count must return to its pre-session
 * baseline after every session teardown, and must not grow across sessions. */
unsigned int gba_rfile_inuse(void)
{
    int          i;
    unsigned int n = 0;

    for (i = 0; i < RFILE_SLOTS; i++)
        if (rfile_pool[i].in_use)
            n++;

    return n;
}

/* The pool ceiling, so a report can print "1 of 4" rather than a bare count and
 * so the diagnostic never hard-codes a number this file owns. */
unsigned int gba_rfile_slots(void)
{
    return (unsigned int)RFILE_SLOTS;
}
#endif /* LUAPORT_SESSION_REUSE */
