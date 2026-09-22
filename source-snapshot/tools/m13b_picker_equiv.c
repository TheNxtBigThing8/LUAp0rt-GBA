/* ===========================================================================
 * tools/m13b_picker_equiv.c -- THE OFFLINE PICKER + NAMED-ROM HARNESS (M13B)
 * ===========================================================================
 *
 * Builds and runs with the HOST compiler, NOT the PS5 toolchain, and #includes
 * FOUR SHIPPING SOURCES VERBATIM:
 *
 *     adapters/gba/m13store.c        (M13A -- paths, dirents, kernel plumbing)
 *     adapters/gba/gba_library.c     (M13A -- .gba filtering and the scan loop)
 *     adapters/gba/gba_rom.c         (M4 + M13B -- the gpSP cartridge boundary)
 *     apps/m13bgpsp/m13b_picker.inc  (M13B -- the picker model)
 *
 * ***** WHY THIS HARNESS EXISTS, IN ONE SENTENCE. *****
 *
 * M13B is the first milestone in which a PATH THE USER CHOSE reaches
 * load_gamepak(), and the failure mode of a mishandled path is not a crash --
 * it is OPENING A DIFFERENT FILE, which on a ROM library is indistinguishable
 * from the picker working correctly.
 *
 * Until M13B every name reaching gba_rom.c was one of two compiled-in literals
 * of 16 and 24 bytes, and the copy buffer was 128 bytes with a loop that clamped
 * at 127 SILENTLY. That could never fire. With a user-chosen "/temp0/<filename>"
 * it can. The buffer is now M4_ROM_NAME_MAX (256) and the copy REFUSES rather
 * than truncates -- and a refusal that is merely believed, rather than proved,
 * is worth nothing. Test E below proves it by COUNTING load_gamepak CALLS.
 *
 * ---- HOW A FILE THAT CALLS gpSP AND THE PS5 KERNEL COMPILES ON A HOST ----
 *
 * Three include guards are pre-defined so the headers expand to NOTHING:
 *
 *     COMMON_H                         gpsp/common.h:20-21
 *     __LIBRETRO_SDK_FILE_STREAM_H     gpsp/libretro/.../streams/file_stream.h:23
 *     CORE_H                           runtime/core.h:1-2
 *
 * and this file then supplies the handful of types, constants and globals the
 * adapters actually use. It is the same technique as tools/gba_restore_equiv.c
 * :27-37 (for common.h) and tools/gba_library_equiv.c:43-44 (for core.h), used
 * together for the first time here because M13B is the first milestone whose
 * logic spans BOTH sides of that type boundary.
 *
 * The preprocessor must still FIND each header before it can skip it, which is
 * why the build rule passes -I gpsp and
 * -I gpsp/libretro/libretro-common/include. THAT IS A BUILD RULE CHANGE ONLY.
 * No shipping source is modified to suit this harness, nothing is copied out of
 * gpsp/, and NO TEST-ONLY #ifdef EXISTS IN ANY PRODUCTION FILE. Neither
 * m13b_picker_equiv nor m13b_blkq appears in any object list.
 *
 * u64 IS `unsigned long long` HERE, matching gpsp/common.h:100 and NOT
 * runtime/core.h:32. tools/gba_library_equiv.c:58-65 makes the opposite choice
 * because it compiles only the M13A pair; this file must also satisfy
 * gba_rom.c, which is a gpSP-side translation unit. m13store.c casts pointers
 * through u64 and is correct either way, since both spellings are 64-bit on
 * every host this builds on -- and `unsigned long long` is 64-bit on Windows
 * LLP64 too, where `unsigned long` is NOT.
 *
 * ---- WHAT IS ACTUALLY BEING PROVED ----
 *
 *   A. THE LEGACY INDEX API IS BYTE-FOR-BYTE UNCHANGED by the delegation.
 *   B. The named API opens and loads THE EXACT SUPPLIED PATH.
 *   C. Spaces, apostrophes, parentheses and brackets survive intact.
 *   D. m4_rom_load(0) and m4_rom_load_named(candidate 0) are INDISTINGUISHABLE.
 *   E. ***** A 255-BYTE PATH LOADS; A 256-BYTE PATH IS REFUSED WITHOUT gpSP
 *      EVER BEING CALLED, AND NOTHING TRUNCATED IS EVER OPENED. *****
 *   F. Library sizes: empty, one, many, exactly 64, and the 65th overflowing.
 *   G. The .gba extension test is case-insensitive and requires a real dot.
 *   H. The sort is a DETERMINISTIC TOTAL ORDER and moves no library entry.
 *   I. Selection and scrolling are clamped and total for every input.
 *   J. A shortened LABEL never alters the PATH it was built from.
 *   K. CROSS held does not launch; CROSS released launches exactly ONCE; a
 *      button that never comes up TIMES OUT instead of hanging the console.
 *   L. An entry is selectable only when it opened AND its source mask is 0.
 *
 * ---- WHAT CANNOT BE PROVED HERE, AND IS NOT CLAIMED ----
 *
 * Nothing here says whether a PS5 exposes /temp0, what it contains, or what
 * arena_used() reports after video and pad init. Those are hardware questions
 * and a host harness is structurally unable to answer them.
 * ========================================================================= */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ---- neutralise the three headers --------------------------------------- */
#define COMMON_H
#define __LIBRETRO_SDK_FILE_STREAM_H
#define CORE_H

/* ---- the types gpSP and core.h would have supplied ----------------------- */
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed char        s8;
typedef short              s16;
typedef int                s32;
typedef long long          s64;

/* The fake kernel casts pointers through u64. A host where that spelling is not
   64-bit would silently TRUNCATE every path pointer and produce baffling
   failures at run time. Fail at COMPILE time instead. */
typedef char m13b_u64_must_be_64_bit[(sizeof(u64) == 8) ? 1 : -1];

#define LIBKERNEL_HANDLE 0x2001

/* ---- gpSP constants the cartridge call site uses ------------------------- */
#define FEAT_AUTODETECT   -1        /* gpsp/gba_memory.h:25                   */
#define SERIAL_MODE_AUTO   6        /* gpsp/serial.h:26                       */

/* ===========================================================================
 * A SYNTHETIC FILE TABLE, BEHIND THE filestream_* INTERFACE
 *
 * gba_rom.c reaches files ONLY through filestream_open / filestream_get_size /
 * filestream_close. Backing those three with an exact-match table is what makes
 * "the EXACT supplied path was used" a testable claim: a near miss does not
 * quietly succeed, it fails to open.
 * ========================================================================= */

#define RETRO_VFS_FILE_ACCESS_READ       1
#define RETRO_VFS_FILE_ACCESS_HINT_NONE  0

#define FS_MAX     40
#define FS_NAMEBUF 1024

typedef struct RFILE RFILE;
struct RFILE { unsigned idx; };

static struct {
    const char *name;
    long        size;
} fsent[FS_MAX];

static unsigned fs_n;
static RFILE    fs_handle[FS_MAX];

/* Instrumentation. fs_last_open records the name EXACTLY as it was handed to
   filestream_open, in a buffer far larger than any path under test, so the
   harness itself can never be the thing that truncated it. */
static unsigned fs_open_calls;
static char     fs_last_open[FS_NAMEBUF];
static unsigned fs_last_open_len;

static void fs_reset(void)
{
    unsigned i;
    for (i = 0; i < FS_MAX; i++) { fsent[i].name = 0; fsent[i].size = 0; }
    fs_n             = 0;
    fs_open_calls    = 0;
    fs_last_open[0]  = '\0';
    fs_last_open_len = 0;
}

static void fs_add(const char *name, long size)
{
    if (fs_n >= FS_MAX) return;
    fsent[fs_n].name = name;
    fsent[fs_n].size = size;
    fs_n++;
}

static RFILE *filestream_open(const char *name, unsigned mode, unsigned hint)
{
    unsigned i, n = 0;

    (void)mode; (void)hint;

    fs_open_calls++;

    if (name) {
        while (name[n] != '\0') n++;
        fs_last_open_len = n;
        if (n < FS_NAMEBUF) {
            memcpy(fs_last_open, name, n);
            fs_last_open[n] = '\0';
        } else {
            /* Would only happen if a test fed something absurd; record the
               overflow rather than silently storing a short prefix. */
            fs_last_open[0] = '\0';
        }
    } else {
        fs_last_open[0]  = '\0';
        fs_last_open_len = 0;
    }

    if (!name) return 0;

    for (i = 0; i < fs_n; i++) {
        if (fsent[i].name && strcmp(fsent[i].name, name) == 0) {
            fs_handle[i].idx = i;
            return &fs_handle[i];
        }
    }
    return 0;
}

static int64_t filestream_get_size(RFILE *f)
{
    if (!f) return -1;
    return (int64_t)fsent[f->idx].size;
}

static int filestream_close(RFILE *f)
{
    (void)f;
    return 0;
}

/* ===========================================================================
 * THE gpSP GLOBALS AND THE TWO gpSP ENTRY POINTS
 *
 * Defined NON-static, so gba_rom.c's hand-written `extern` declarations
 * (gba_rom.c:81-101) bind to them exactly as they bind to gpSP's real
 * definitions on the console.
 *
 * gamepak_blk_queue is the ONE exception and lives in tools/m13b_blkq.c --
 * see that file for the C11 6.2.7p1 reason it cannot be defined here.
 * ========================================================================= */

u32  gamepak_size               = 0;
u32  gamepak_buffer_count       = 0;
u32  gamepak_file_blocks        = 0;
u32  backup_type                = 3;
u32  backup_type_reset          = 3;
bool gamepak_mirror_1m          = false;
bool gamepak_mini_materialized  = false;
bool gamepak_header_nonstandard = false;
bool is_known_game              = false;
u16  gamepak_lru_head           = 0;
u16  gamepak_lru_tail           = 0;

u8  *gamepak_buffers[32];
u8   gamepak_backup[1024 * 128];
u8  *memory_map_read[8 * 1024];
u8   bios_rom[1024 * 16];

const unsigned gamepak_buffer_blocksize = 1024 * 1024;

/* ***** THE INSTRUMENTED gpSP LOADER. *****
 *
 * Counting calls is the whole point. "m4_rom_load_named() refuses an over-long
 * path" is a claim about something that MUST NOT HAPPEN, and the only way to
 * prove a call did not occur is to count the calls that did. lg_name records
 * the exact bytes gpSP was handed, so a truncation could not hide behind a
 * return value either. */
static unsigned lg_calls;
static char     lg_name[FS_NAMEBUF];
static unsigned lg_name_len;
static int      lg_fail;

static void lg_reset(void)
{
    lg_calls    = 0;
    lg_name[0]  = '\0';
    lg_name_len = 0;
    lg_fail     = 0;
}

static u32 load_gamepak(const void *info, const char *name,
                        int force_rtc, int force_rumble, int force_serial)
{
    unsigned n = 0;

    (void)info; (void)force_rtc; (void)force_rumble; (void)force_serial;

    lg_calls++;

    if (name) {
        while (name[n] != '\0') n++;
        lg_name_len = n;
        if (n < FS_NAMEBUF) { memcpy(lg_name, name, n); lg_name[n] = '\0'; }
        else                { lg_name[0] = '\0'; }
    } else {
        lg_name[0]  = '\0';
        lg_name_len = 0;
    }

    return lg_fail ? (u32)-1 : 0u;
}

static unsigned igb_calls;

static void init_gamepak_buffer(void)
{
    igb_calls++;
    gamepak_buffer_count = 2;        /* ROM_BUFFER_SIZE=2, the M4 build flag */
}

/* ===========================================================================
 * A SYNTHETIC DIRECTORY, BEHIND getdents
 *
 * Only what M13B needs: well-formed FreeBSD records. The MALFORMED-record cases
 * (zero reclen, overrunning records, the never-ending volume) are already proved
 * by tools/gba_library_equiv.c against this same parser and are deliberately not
 * duplicated here.
 * ========================================================================= */

#define DFS_BYTES 65536

static u8       dfs_buf[DFS_BYTES];
static unsigned dfs_len;
static unsigned dfs_off;
static int      dfs_open_err;
static int      dfs_fd_live;

static void dfs_reset(void)
{
    dfs_len      = 0;
    dfs_off      = 0;
    dfs_open_err = 0;
    dfs_fd_live  = 0;
}

/* +0 u32 d_fileno  +4 u16 d_reclen  +6 u8 d_type  +7 u8 d_namlen  +8 name[] */
static void dfs_add(const char *name, unsigned type)
{
    unsigned namlen = (unsigned)strlen(name);
    unsigned reclen = (8u + namlen + 1u + 7u) & ~7u;
    unsigned i;
    u8 *p;

    if (namlen > 255u)                  return;   /* not representable        */
    if (dfs_len + reclen > DFS_BYTES)   return;

    p = dfs_buf + dfs_len;
    for (i = 0; i < reclen; i++) p[i] = 0;

    p[0] = 1;
    p[4] = (u8)(reclen & 0xFF);
    p[5] = (u8)((reclen >> 8) & 0xFF);
    p[6] = (u8)type;
    p[7] = (u8)namlen;
    for (i = 0; i < namlen; i++) p[8 + i] = (u8)name[i];

    dfs_len += reclen;
}

static int tok_open, tok_close, tok_lseek, tok_getdents;

static void *fake_sym(void *gadget, void *dlsym_fn, s32 handle, const char *name)
{
    (void)gadget; (void)dlsym_fn; (void)handle;
    if (!strcmp(name, "sceKernelOpen"))     return &tok_open;
    if (!strcmp(name, "sceKernelClose"))    return &tok_close;
    if (!strcmp(name, "sceKernelLseek"))    return &tok_lseek;
    if (!strcmp(name, "sceKernelGetdents")) return &tok_getdents;
    if (!strcmp(name, "getdents"))          return &tok_getdents;
    return 0;
}

static u64 fake_nc(void *gadget, void *fn, u64 a1, u64 a2, u64 a3,
                   u64 a4, u64 a5, u64 a6)
{
    (void)gadget; (void)a4; (void)a5; (void)a6;

    if (fn == (void *)&tok_open) {
        if (dfs_open_err) return (u64)(s64)(-dfs_open_err);
        dfs_off     = 0;
        dfs_fd_live = 1;
        return (u64)(s64)7;
    }

    if (fn == (void *)&tok_getdents) {
        u8      *out = (u8 *)a2;
        unsigned cap = (unsigned)a3;
        unsigned produced = 0;

        if ((int)a1 != 7 || !dfs_fd_live) return (u64)(s64)(-9);

        /* WHOLE RECORDS ONLY, exactly as a real getdents does, so the
           production loop is forced to call again for a large directory. */
        while (dfs_off < dfs_len) {
            unsigned reclen = (unsigned)dfs_buf[dfs_off + 4] |
                              ((unsigned)dfs_buf[dfs_off + 5] << 8);
            if (reclen == 0) break;
            if (produced + reclen > cap) break;
            memcpy(out + produced, dfs_buf + dfs_off, reclen);
            produced += reclen;
            dfs_off  += reclen;
        }
        return (u64)produced;
    }

    if (fn == (void *)&tok_close) { dfs_fd_live = 0; return 0; }
    if (fn == (void *)&tok_lseek) return 0;
    return (u64)(s64)(-22);
}

#define NC  fake_nc
#define SYM fake_sym

/* ---- THE SHIPPING SOURCE, COMPILED VERBATIM ----------------------------- */
#include "m13store.c"
#include "gba_library.c"
#include "gba_rom.c"
#include "m13b_picker.inc"

/* ===========================================================================
 * CHECK PLUMBING
 * ========================================================================= */

static int failures = 0;
static int checks   = 0;

static void ck(int cond, const char *what)
{
    checks++;
    if (!cond) { failures++; printf("    FAIL: %s\n", what); }
}

static void ck_int(long got, long want, const char *what)
{
    checks++;
    if (got != want) {
        failures++;
        printf("    FAIL: %s (got %ld, want %ld)\n", what, got, want);
    }
}

static void ck_str(const char *got, const char *want, const char *what)
{
    checks++;
    if (!got || strcmp(got, want) != 0) {
        failures++;
        printf("    FAIL: %s\n      got  '%s'\n      want '%s'\n",
               what, got ? got : "(null)", want);
    }
}

/* ---- fixtures ------------------------------------------------------------ */

static struct m13store ST;
static u8              dirbuf[M13STORE_DIRBUF];
static struct gba_lib  LIB;

static void store_up(void)
{
    int ok = m13store_init(&ST, (void *)1, (void *)1);
    ck(ok == 1, "m13store_init reports ready against the fake kernel");
}

/* Clears every piece of cross-test state. Called before each section so one
   test's load can never be mistaken for the next test's. */
static void world_reset(void)
{
    fs_reset();
    lg_reset();
    dfs_reset();
    igb_calls = 0;
    /* Put rom_name back to "" through the production refusal path, so no test
       inherits a previous test's name. m4_rom_load_named(NULL) returns -1
       WITHOUT calling gpSP, which is itself the behaviour test E relies on. */
    (void)m4_rom_load_named(0);
    lg_reset();
}

/* Fills dst with "/temp0/" + (total-11) 'A's + ".gba", producing a path of
   EXACTLY `total` characters. Used to sit precisely on both sides of the
   M4_ROM_NAME_MAX boundary. */
static void make_len(char *dst, unsigned total)
{
    const char *pre = "/temp0/";
    unsigned    plen = 7u, i, body;

    body = total - plen - 4u;                    /* 4 = strlen(".gba") */
    memcpy(dst, pre, plen);
    for (i = 0; i < body; i++) dst[plen + i] = 'A';
    memcpy(dst + plen + body, ".gba", 4);
    dst[total] = '\0';
}

/* ===========================================================================
 * MAIN
 * ========================================================================= */

int main(void)
{
    static char exact_fit[M4_ROM_NAME_MAX + 64];
    static char over_long[M4_ROM_NAME_MAX + 64];
    static char way_long [400];
    static char lbl[M13B_LABEL_CAP];
    static char keep_name[FS_NAMEBUF];
    unsigned sz, i;
    int      rc;

    printf("=== M13B PICKER + NAMED-ROM EQUIVALENCE HARNESS ===\n");
    printf("    m13store.c, gba_library.c, gba_rom.c and m13b_picker.inc,\n");
    printf("    compiled VERBATIM against a synthetic filesystem and a\n");
    printf("    CALL-COUNTING gpSP.\n\n");

    store_up();

    /* ==================================================================== A
     * THE LEGACY INDEX API, UNCHANGED BY THE DELEGATION
     * ------------------------------------------------------------------- */
    printf("A. legacy index API preserved\n");
    {
        world_reset();
        fs_add("/temp0/test.gba", 4096);
        fs_add("/savedata0/rom/test.gba", 8192);

        ck_int((long)m4_rom_candidate_count(), 2, "two candidates");
        ck_str(m4_rom_candidate_name(0), "/temp0/test.gba",  "candidate 0");
        ck_str(m4_rom_candidate_name(1), "/savedata0/rom/test.gba",
               "candidate 1");
        ck(m4_rom_candidate_name(2) == 0, "candidate 2 is NULL");

        sz = 0xDEAD;
        ck_int(m4_rom_query(0, &sz), 1, "m4_rom_query(0) opens");
        ck_int((long)sz, 4096, "m4_rom_query(0) reports the TRUE size");
        ck_str(fs_last_open, "/temp0/test.gba",
               "the index path opened the EXACT candidate name");

        /* A BAD INDEX MUST STILL CLEAR *size_out -- the M4 prologue order,
           preserved by the delegation (gba_rom.c:228-238). */
        sz = 0xDEAD;
        fs_open_calls = 0;
        ck_int(m4_rom_query(9, &sz), 0, "m4_rom_query(bad index) fails");
        ck_int((long)sz, 0, "m4_rom_query(bad index) still CLEARS size_out");
        ck_int((long)fs_open_calls, 0, "a bad index opens nothing at all");

        lg_reset();
        ck_int(m4_rom_load(0), 0, "m4_rom_load(0) succeeds");
        ck_int((long)lg_calls, 1, "gpSP was called exactly once");
        ck_str(lg_name, "/temp0/test.gba", "gpSP received the exact candidate");
        ck_str(m4_rom_name(), "/temp0/test.gba", "m4_rom_name reports it");

        lg_reset();
        ck_int(m4_rom_load(9), -1, "m4_rom_load(bad index) fails");
        ck_int((long)lg_calls, 0, "a bad index NEVER reaches gpSP");

        ck_int((long)m4_rom_buffer_init(), 2, "buffer init returns 2 buffers");
        ck_int((long)igb_calls, 1, "gpSP's own init_gamepak_buffer ran once");
    }

    /* ==================================================================== B
     * THE NAMED API USES THE EXACT SUPPLIED PATH
     * ------------------------------------------------------------------- */
    printf("B. named API uses the exact supplied path\n");
    {
        world_reset();
        fs_add("/temp0/Zelda.gba", 4194304);

        sz = 0;
        ck_int(m4_rom_query_named("/temp0/Zelda.gba", &sz), 1,
               "m4_rom_query_named opens an existing file");
        ck_int((long)sz, 4194304, "and reports its TRUE size");
        ck_str(fs_last_open, "/temp0/Zelda.gba",
               "query opened the EXACT string it was given");

        /* A MISS MUST STILL HAVE ASKED FOR THE WHOLE NAME. If the layer had
           truncated, the recorded request would be a prefix. */
        sz = 0xBEEF;
        ck_int(m4_rom_query_named("/temp0/Missing Game.gba", &sz), 0,
               "a missing file reports not-opened");
        ck_int((long)sz, 0, "and clears size_out");
        ck_str(fs_last_open, "/temp0/Missing Game.gba",
               "the FULL name was requested, not a prefix");

        ck_int(m4_rom_query_named(0, &sz), 0, "a NULL name is refused");

        lg_reset();
        ck_int(m4_rom_load_named("/temp0/Zelda.gba"), 0,
               "m4_rom_load_named succeeds");
        ck_int((long)lg_calls, 1, "gpSP called once");
        ck_str(lg_name, "/temp0/Zelda.gba", "gpSP got the exact path");
        ck_str(m4_rom_name(), "/temp0/Zelda.gba",
               "m4_rom_name returns the exact selected path");

        lg_reset();
        ck_int(m4_rom_load_named(0), -1, "a NULL path is refused");
        ck_int((long)lg_calls, 0, "and never reaches gpSP");
        /* A NULL is rejected at gba_rom.c:331, which sits ABOVE the name layer
           at :338 -- so it cannot, and MUST NOT, disturb the name of the ROM
           that is genuinely resident. Clearing here would make m4_rom_name()
           lie about a load that really did succeed, and the fixture would then
           print an empty filename next to a working game. Only an OVER-LONG
           path reaches rom_name_set() and clears the name; section E proves
           that boundary exactly. */
        ck_str(m4_rom_name(), "/temp0/Zelda.gba",
               "the last ACCEPTED name survives a NULL refusal");
    }

    /* ==================================================================== C
     * SPACES, APOSTROPHES, PARENTHESES, BRACKETS
     *
     * These are not exotic -- they are what a real GBA library looks like. The
     * uploader preserves them byte for byte and nothing in this stack is
     * allowed to sanitise, escape or normalise them.
     * ------------------------------------------------------------------- */
    printf("C. spaces, apostrophes, parentheses and brackets survive\n");
    {
        static const char *names[] = {
            "/temp0/Mario Kart Super Circuit.gba",       /* spaces          */
            "/temp0/Tony Hawk's Pro Skater 2.gba",       /* apostrophe      */
            "/temp0/Advance Wars (USA, Europe).gba",     /* parentheses     */
            "/temp0/Metroid Fusion [!].gba",             /* brackets        */
            "/temp0/Kirby & The Amazing Mirror (U) [f1].gba" /* all at once */
        };
        unsigned n = (unsigned)(sizeof(names) / sizeof(names[0]));

        world_reset();
        for (i = 0; i < n; i++) fs_add(names[i], 2097152);

        for (i = 0; i < n; i++) {
            sz = 0;
            ck_int(m4_rom_query_named(names[i], &sz), 1,
                   "query opens a real-world filename");
            ck_int((long)sz, 2097152, "true size reported");
            ck_str(fs_last_open, names[i], "query used the EXACT bytes");

            lg_reset();
            ck_int(m4_rom_load_named(names[i]), 0, "load succeeds");
            ck_str(lg_name, names[i], "gpSP received the EXACT bytes");
            ck_str(m4_rom_name(), names[i], "m4_rom_name echoes them exactly");
        }
    }

    /* ==================================================================== D
     * INDEX AND NAMED PATHS ARE INDISTINGUISHABLE
     *
     * The delegation's whole claim. Proved BEHAVIOURALLY -- on the resulting
     * rom_name and on the bytes gpSP received -- which is something neither
     * `nm` nor a string grep could establish.
     * ------------------------------------------------------------------- */
    printf("D. index and named paths produce identical state\n");
    {
        world_reset();
        fs_add("/temp0/test.gba", 4096);

        lg_reset();
        ck_int(m4_rom_load(0), 0, "index load succeeds");
        memcpy(keep_name, lg_name, sizeof(keep_name));
        ck_str(m4_rom_name(), "/temp0/test.gba", "index path set rom_name");

        /* The FINAL assertion in this section is only meaningful if rom_name is
           known to be empty first -- otherwise "/temp0/test.gba" could simply be
           the index load's leftover, and the named route would be proving
           nothing at all. A NULL will not clear it: gba_rom.c:331 rejects NULL
           above the name layer and deliberately leaves the resident name alone
           (section B asserts precisely that). The OVER-LONG path is the one
           that genuinely reaches rom_name_set() and clears it (gba_rom.c:173),
           so that is what is used to reset the state between the two halves.
           It is refused before any filesystem access, so it does not matter
           that this name is absent from the synthetic drive. */
        make_len(over_long, M4_ROM_NAME_MAX);        /* 256 bytes => refused */
        (void)m4_rom_load_named(over_long);
        ck_str(m4_rom_name(), "", "rom_name cleared between the two halves");

        lg_reset();
        ck_int(m4_rom_load_named(m4_rom_candidate_name(0)), 0,
               "named load of candidate 0 succeeds");
        ck_str(lg_name, keep_name,
               "gpSP received BYTE-IDENTICAL names by both routes");
        ck_str(m4_rom_name(), "/temp0/test.gba",
               "rom_name is identical by both routes");
    }

    /* ==================================================================== E
     * ***** THE LENGTH BOUNDARY -- THE REASON THIS HARNESS EXISTS *****
     * ------------------------------------------------------------------- */
    printf("E. the length boundary: 255 loads, 256 is REFUSED\n");
    {
        world_reset();

        make_len(exact_fit, M4_ROM_NAME_MAX - 1u);   /* 255 chars */
        make_len(over_long, M4_ROM_NAME_MAX);        /* 256 chars */
        make_len(way_long,  300u);                   /* 300 chars */

        ck_int((long)strlen(exact_fit), 255, "exact-fit path is 255 bytes");
        ck_int((long)strlen(over_long), 256, "over-long path is 256 bytes");

        /* Both exist on the synthetic drive, so the ONLY thing that can stop
           the second one is the refusal itself -- not a missing file. */
        fs_add(exact_fit, 65536);
        fs_add(over_long, 65536);
        fs_add(way_long,  65536);

        /* --- the exact fit MUST load ---------------------------------- */
        lg_reset();
        ck_int(m4_rom_load_named(exact_fit), 0,
               "a 255-byte path (the exact fit) LOADS");
        ck_int((long)lg_calls, 1, "gpSP was called for the exact fit");
        ck_str(lg_name, exact_fit, "gpSP received all 255 bytes");
        ck_str(m4_rom_name(), exact_fit, "rom_name holds all 255 bytes");

        /* --- the over-long path MUST be refused, loudly ---------------- */
        lg_reset();
        fs_open_calls = 0;
        rc = m4_rom_load_named(over_long);

        ck_int(rc, -1, "a 256-byte path is REFUSED");
        ck_int((long)lg_calls, 0,
               "***** THE REFUSAL NEVER CALLS gpSP *****");
        ck_int((long)fs_open_calls, 0,
               "***** AND NEVER OPENS A TRUNCATED NEIGHBOUR *****");
        ck_str(m4_rom_name(), "",
               "the refusal leaves \"\" -- not a truncated name, not a stale one");
        ck(strlen(m4_rom_name()) != 255,
           "the refusal did NOT silently shorten to the 255-byte fit");

        /* --- query has NO length gate, and still does not truncate ----- */
        sz = 0;
        ck_int(m4_rom_query_named(way_long, &sz), 1,
               "query_named has no length gate and opens a 300-byte path");
        ck_int((long)sz, 65536, "and measures it");
        ck_int((long)fs_last_open_len, 300,
               "query asked for all 300 bytes -- no truncation anywhere");
        ck_str(fs_last_open, way_long, "byte-for-byte the requested name");

        /* --- and loading that same 300-byte path is still refused ------ */
        lg_reset();
        ck_int(m4_rom_load_named(way_long), -1, "a 300-byte path is refused");
        ck_int((long)lg_calls, 0, "still no gpSP call");

        /* --- THE STRUCTURAL INVARIANT --------------------------------- *
         * Every path gba_lib_scan can store is at most GBA_LIB_PATH_MAX-1
         * bytes. If that ever exceeded what m4_rom_load_named accepts, the
         * picker could offer a row that is impossible to launch. */
        ck(GBA_LIB_PATH_MAX <= (int)M4_ROM_NAME_MAX,
           "every storable library path FITS the ROM name bound");
    }

    /* ==================================================================== F
     * LIBRARY SIZES: EMPTY, ONE, MANY, EXACTLY 64, AND THE 65th
     * ------------------------------------------------------------------- */
    printf("F. empty / one / many / 64 / 65-overflow\n");
    {
        char nm[64];

        /* --- empty ---------------------------------------------------- */
        world_reset();
        rc = gba_lib_scan(&ST, "/temp0", &LIB, dirbuf, sizeof(dirbuf));
        ck_int(rc, 0, "an empty directory yields 0 candidates");
        ck_int((long)LIB.count, 0, "count is 0");
        m13b_sort(&LIB, m13b_order, LIB.count);
        ck_int((long)m13b_scroll(0, LIB.count, 0), 0,
               "scroll on an empty library is 0");
        ck_str(gba_lib_name(&LIB, 0), "",
               "reading row 0 of an empty library is \"\" and not a fault");

        /* --- exactly one ---------------------------------------------- */
        world_reset();
        dfs_add("Solo.gba", M13STORE_DT_REG);
        rc = gba_lib_scan(&ST, "/temp0", &LIB, dirbuf, sizeof(dirbuf));
        ck_int(rc, 1, "one ROM is found");
        ck_int((long)LIB.count, 1, "count is 1");
        ck_str(gba_lib_path(&LIB, 0), "/temp0/Solo.gba", "its path is joined");
        ck_str(gba_lib_name(&LIB, 0), "Solo.gba", "its name is exact");

        /* --- several, with noise -------------------------------------- */
        world_reset();
        dfs_add("alpha.gba",  M13STORE_DT_REG);
        dfs_add("beta.gba",   M13STORE_DT_REG);
        dfs_add("notes.txt",  M13STORE_DT_REG);
        dfs_add("subdir",     M13STORE_DT_DIR);
        dfs_add(".hidden.gba",M13STORE_DT_REG);
        dfs_add("gamma.gba",  M13STORE_DT_UNKNOWN);
        rc = gba_lib_scan(&ST, "/temp0", &LIB, dirbuf, sizeof(dirbuf));
        ck_int(rc, 3, "three of six entries are candidates");
        ck_int((long)LIB.rej_ext, 1, "the .txt was counted as a bad extension");
        ck_int((long)LIB.rej_dir, 1, "the directory was counted");
        ck_int((long)LIB.rej_hidden, 1, "the dotfile was counted");
        ck(LIB.seen >= 6, "every dirent was seen");

        /* --- exactly 64 ----------------------------------------------- */
        world_reset();
        for (i = 0; i < GBA_LIB_MAX_ENTRIES; i++) {
            sprintf(nm, "rom%03u.gba", i);
            dfs_add(nm, M13STORE_DT_REG);
        }
        rc = gba_lib_scan(&ST, "/temp0", &LIB, dirbuf, sizeof(dirbuf));
        ck_int(rc, GBA_LIB_MAX_ENTRIES, "exactly 64 ROMs all fit");
        ck_int((long)LIB.count, GBA_LIB_MAX_ENTRIES, "count is 64");
        ck_int((long)LIB.overflow, 0, "nothing overflowed at exactly 64");

        /* --- the 65th ------------------------------------------------- */
        world_reset();
        for (i = 0; i < GBA_LIB_MAX_ENTRIES + 1u; i++) {
            sprintf(nm, "rom%03u.gba", i);
            dfs_add(nm, M13STORE_DT_REG);
        }
        rc = gba_lib_scan(&ST, "/temp0", &LIB, dirbuf, sizeof(dirbuf));
        ck_int(rc, GBA_LIB_MAX_ENTRIES, "the scan still returns 64");
        ck_int((long)LIB.count, GBA_LIB_MAX_ENTRIES, "the table is capped");
        ck_int((long)LIB.overflow, 1,
               "the 65th was COUNTED as overflow, not written and not dropped "
               "silently");
        ck_str(gba_lib_name(&LIB, GBA_LIB_MAX_ENTRIES), "",
               "reading one past the cap is \"\" and not a fault");

        /* The sort must survive a FULL table without running off the end. */
        m13b_sort(&LIB, m13b_order, LIB.count);
        ck_int((long)m13b_order[0], 0, "a full table sorts from index 0");
        ck_int((long)m13b_order[GBA_LIB_MAX_ENTRIES - 1u],
               GBA_LIB_MAX_ENTRIES - 1, "and ends at index 63");
    }

    /* ==================================================================== G
     * THE .gba TEST IS CASE-INSENSITIVE AND NEEDS A REAL DOT
     * ------------------------------------------------------------------- */
    printf("G. case-insensitive .gba extension\n");
    {
        world_reset();
        dfs_add("upper.GBA",  M13STORE_DT_REG);
        dfs_add("mixed.Gba",  M13STORE_DT_REG);
        dfs_add("odd.gBa",    M13STORE_DT_REG);
        dfs_add("lower.gba",  M13STORE_DT_REG);
        dfs_add("nodotgba",   M13STORE_DT_REG);   /* no separator -> reject   */
        dfs_add("short.gb",   M13STORE_DT_REG);   /* wrong extension          */
        dfs_add(".gba",       M13STORE_DT_REG);   /* bare extension -> hidden */

        rc = gba_lib_scan(&ST, "/temp0", &LIB, dirbuf, sizeof(dirbuf));
        ck_int(rc, 4, "all four case spellings of .gba are accepted");
        ck_int((long)LIB.rej_ext, 2, "nodotgba and .gb are extension rejects");
        ck_int((long)LIB.rej_hidden, 1, "a bare '.gba' is a hidden reject");

        ck_int(gba_lib_classify("A.GBA", 5), GBA_LIB_ACCEPT, "A.GBA accepted");
        ck_int(gba_lib_classify("A.gba", 5), GBA_LIB_ACCEPT, "A.gba accepted");
        ck_int(gba_lib_classify("gba",   3), GBA_LIB_REJ_EXT,
               "'gba' with no dot is NOT an extension match");
    }

    /* ==================================================================== H
     * A DETERMINISTIC TOTAL ORDER THAT MOVES NO LIBRARY ENTRY
     * ------------------------------------------------------------------- */
    printf("H. deterministic sort, index permutation only\n");
    {
        world_reset();
        dfs_add("zelda.gba",   M13STORE_DT_REG);   /* scan 0 */
        dfs_add("Alpha.gba",   M13STORE_DT_REG);   /* scan 1 */
        dfs_add("mario.gba",   M13STORE_DT_REG);   /* scan 2 */
        dfs_add("ALPHA.gba",   M13STORE_DT_REG);   /* scan 3 -- ci-ties w/ 1 */
        dfs_add("Banjo.gba",   M13STORE_DT_REG);   /* scan 4 */

        rc = gba_lib_scan(&ST, "/temp0", &LIB, dirbuf, sizeof(dirbuf));
        ck_int(rc, 5, "five entries scanned");

        m13b_sort(&LIB, m13b_order, LIB.count);

        /* CASE-INSENSITIVE ALPHABETICAL. A byte-wise sort would have put every
           capitalised name first and split the alphabet in two. */
        ck_str(gba_lib_name(&LIB, m13b_order[2]), "Banjo.gba",
               "row 2 is Banjo -- the ci order is A, A, B, m, z");
        ck_str(gba_lib_name(&LIB, m13b_order[3]), "mario.gba",
               "row 3 is mario, ahead of zelda and behind Banjo");
        ck_str(gba_lib_name(&LIB, m13b_order[4]), "zelda.gba",
               "row 4 is zelda");

        /* THE CASE-SENSITIVE TIE-BREAK. 'A' (0x41) sorts before 'l' (0x6C),
           so ALPHA precedes Alpha once the fold has tied them. */
        ck_str(gba_lib_name(&LIB, m13b_order[0]), "ALPHA.gba",
               "the ci tie is broken CASE-SENSITIVELY: ALPHA before Alpha");
        ck_str(gba_lib_name(&LIB, m13b_order[1]), "Alpha.gba",
               "and Alpha follows it");

        /* DETERMINISM: sorting again must produce the identical permutation. */
        {
            u8 first[GBA_LIB_MAX_ENTRIES];
            memcpy(first, m13b_order, sizeof(first));
            m13b_sort(&LIB, m13b_order, LIB.count);
            ck(memcmp(first, m13b_order, LIB.count) == 0,
               "re-sorting yields the IDENTICAL permutation");
        }

        /* THE ENTRIES THEMSELVES MUST NOT HAVE MOVED. */
        ck_str(gba_lib_name(&LIB, 0), "zelda.gba",
               "scan index 0 is STILL zelda -- the sort moved no entry");
        ck_str(gba_lib_name(&LIB, 4), "Banjo.gba",
               "scan index 4 is STILL Banjo");

        /* THE SCAN-INDEX FINAL TIE-BREAK. Two byte-identical filenames can only
           be separated by where they were found, and the comparator must still
           be a strict total order or the sort's output would be arbitrary. */
        ck(m13b_cmp(&LIB, 1, 1) == 0, "an entry compares equal to itself");
        ck(m13b_cmp(&LIB, 1, 3) != 0, "two distinct entries never tie");
        ck(m13b_cmp_name("same.gba", "same.gba") == 0,
           "identical names tie on name alone");
        ck(m13b_cmp(&LIB, 0, 4) > 0 && m13b_cmp(&LIB, 4, 0) < 0,
           "the comparator is antisymmetric");
    }

    /* ==================================================================== I
     * SELECTION AND SCROLLING ARE CLAMPED AND TOTAL
     * ------------------------------------------------------------------- */
    printf("I. selection and scrolling math\n");
    {
        const unsigned N = 40u;      /* 40 entries, 16 visible rows */

        ck_int((long)M13B_ROWS, 16, "sixteen visible rows");

        /* selection index 0 and count-1 */
        ck_int((long)m13b_move(0, N, -1), 0, "UP at the top stays at 0");
        ck_int((long)m13b_move(0, N, +1), 1, "DOWN from 0 goes to 1");
        ck_int((long)m13b_move(N - 1u, N, +1), (long)N - 1,
               "DOWN at the bottom stays at count-1");
        ck_int((long)m13b_move(N - 1u, N, -1), (long)N - 2, "UP from the bottom");

        /* an INVALID index is clamped rather than trusted */
        ck_int((long)m13b_move(9999u, N, 0), (long)N - 1,
               "an out-of-range selection clamps to count-1");
        ck_int((long)m13b_move(5u, 0u, +1), 0, "a move in an empty library is 0");

        /* scrolling */
        ck_int((long)m13b_scroll(0, N, 0), 0, "row 0 needs no scroll");
        ck_int((long)m13b_scroll(15u, N, 0), 0,
               "row 15 is the last visible row -- still no scroll");
        ck_int((long)m13b_scroll(16u, N, 0), 1,
               "row 16 scrolls the window by exactly one");
        ck_int((long)m13b_scroll(N - 1u, N, 0), (long)N - 16,
               "the last row pins the window to the end");
        ck_int((long)m13b_scroll(0, N, 20u), 0,
               "jumping back to row 0 scrolls the window home");
        ck_int((long)m13b_scroll(5u, 10u, 0), 0,
               "a library shorter than the window never scrolls");
        ck_int((long)m13b_scroll(3u, N, 99u), 3,
               "a stale top is repaired, not trusted");
        ck_int((long)m13b_scroll(0, 0, 7u), 0,
               "an empty library always reports top 0");

        /* the window is ALWAYS inside the library, for every selection */
        for (i = 0; i < N; i++) {
            unsigned top = m13b_scroll(i, N, 0);
            if (top + M13B_ROWS > N || i < top || i >= top + M13B_ROWS) {
                ck(0, "the window always contains the selection and fits");
                break;
            }
        }
        ck(i == N, "every one of 40 selections produced a valid window");
    }

    /* ==================================================================== J
     * A SHORTENED LABEL NEVER ALTERS THE PATH
     * ------------------------------------------------------------------- */
    printf("J. display truncation does not touch the path\n");
    {
        static const char *longname =
            "A Very Long Game Name That Will Not Fit In One Row At All.gba";
        static char guard[FS_NAMEBUF];

        /* short name: copied whole */
        m13b_label(lbl, sizeof(lbl), "Zelda.gba");
        ck_str(lbl, "Zelda.gba", "a short name is copied unchanged");

        /* long name: shortened AND STAMPED */
        memcpy(guard, longname, strlen(longname) + 1u);
        m13b_label(lbl, sizeof(lbl), guard);
        ck_int((long)strlen(lbl), (long)M13B_LABEL_MAX,
               "a long label fills exactly the visible width");
        ck(strlen(lbl) < strlen(longname), "and is shorter than the source");
        ck_str(lbl + M13B_LABEL_MAX - 3u, "...",
               "the cut is STAMPED so the operator can see it happened");

        /* ***** THE SOURCE IS UNTOUCHED ***** */
        ck_str(guard, longname,
               "***** the SOURCE STRING IS BYTE-IDENTICAL after labelling *****");

        /* and the real path a load would use is still complete */
        world_reset();
        fs_add("/temp0/A Very Long Game Name That Will Not Fit In One Row At "
               "All.gba", 4096);
        m13b_label(lbl, sizeof(lbl), "A Very Long Game Name That Will Not Fit "
                                     "In One Row At All.gba");
        lg_reset();
        ck_int(m4_rom_load_named("/temp0/A Very Long Game Name That Will Not "
                                 "Fit In One Row At All.gba"), 0,
               "the FULL path still loads after its label was shortened");
        ck_str(lg_name, "/temp0/A Very Long Game Name That Will Not Fit In One "
                        "Row At All.gba",
               "gpSP received the COMPLETE path, never the label");

        /* degenerate caps must still terminate */
        m13b_label(lbl, 1u, "anything");
        ck_str(lbl, "", "a 1-byte buffer yields an empty, terminated string");
        m13b_label(lbl, 3u, "anything");
        ck_int((long)strlen(lbl), 2, "a 3-byte buffer holds 2 characters");
        m13b_label(lbl, sizeof(lbl), 0);
        ck_str(lbl, "", "a NULL source yields an empty, terminated string");
    }

    /* ==================================================================== K
     * THE CROSS CONTRACT
     * ------------------------------------------------------------------- */
    printf("K. CROSS press/release/timeout\n");
    {
        struct m13b_input in;

        /* no press at all */
        m13b_input_reset(&in);
        for (i = 0; i < 100u; i++)
            ck_int((long)m13b_input_step(&in, 0), M13B_IN_IDLE, "idle stays idle");

        /* HELD -- must NOT launch */
        m13b_input_reset(&in);
        ck_int((long)m13b_input_step(&in, 1), M13B_IN_WAIT,
               "the press arms the wait");
        for (i = 0; i < 300u; i++) {
            unsigned st = m13b_input_step(&in, 1);
            if (st != M13B_IN_WAIT) break;
        }
        ck_int((long)in.state, M13B_IN_WAIT,
               "***** CROSS HELD FOR 300 FRAMES DOES NOT LAUNCH *****");

        /* RELEASE -- launches, exactly once */
        ck_int((long)m13b_input_step(&in, 0), M13B_IN_LAUNCH,
               "the RELEASE launches");
        ck_int((long)m13b_input_step(&in, 0), M13B_IN_LAUNCH,
               "LAUNCH is terminal -- a later frame cannot re-launch");
        ck_int((long)m13b_input_step(&in, 1), M13B_IN_LAUNCH,
               "not even a second press can re-arm it");

        /* TIMEOUT -- a button that never comes up must not hang the console */
        m13b_input_reset(&in);
        for (i = 0; i < M13B_RELEASE_FRAMES + 10u; i++)
            (void)m13b_input_step(&in, 1);
        ck_int((long)in.state, M13B_IN_TIMEOUT,
               "***** A STUCK CROSS TIMES OUT INSTEAD OF HANGING *****");
        ck_int((long)m13b_input_step(&in, 0), M13B_IN_TIMEOUT,
               "TIMEOUT is terminal too -- releasing afterwards does not launch");
        ck_int((long)M13B_RELEASE_FRAMES, 600,
               "the bound is 600 frames, about 10 s at 60 Hz");

        /* the boundary itself */
        m13b_input_reset(&in);
        (void)m13b_input_step(&in, 1);                 /* arm, frames = 0 */
        for (i = 1; i < M13B_RELEASE_FRAMES; i++)
            (void)m13b_input_step(&in, 1);
        ck_int((long)in.state, M13B_IN_WAIT,
               "one frame before the bound it is still waiting");
        ck_int((long)m13b_input_step(&in, 1), M13B_IN_TIMEOUT,
               "and the bounding frame trips it");
        ck(m13b_input_step(0, 0) == M13B_IN_IDLE, "a NULL state is safe");
    }

    /* ==================================================================== L
     * SELECTABILITY
     * ------------------------------------------------------------------- */
    printf("L. validation and selectability\n");
    {
        struct m13b_item it;

        m13b_item_set(&it, 1, 1, 4096u, m4_rom_probe_source(4096u));
        ck_int((long)it.sel, 1, "a good 4 KB ROM is selectable");
        ck_int((long)it.reason, M13B_PICK_OK, "and its reason is OK");
        ck_str(m13b_reason_text(it.reason), "OK", "reason text");

        m13b_item_set(&it, 1, 0, 0u, 0u);
        ck_int((long)it.sel, 0, "an unopenable entry is NOT selectable");
        ck_int((long)it.reason, M13B_R_OPEN, "reason is CANNOT OPEN");
        ck_str(m13b_reason_text(it.reason), "CANNOT OPEN", "reason text");

        m13b_item_set(&it, 0, 1, 4096u, 0u);
        ck_int((long)it.sel, 0, "an over-long path is NOT selectable");
        ck_int((long)it.reason, M13B_R_LONG, "reason is NAME TOO LONG");

        m13b_item_set(&it, 1, 1, 0u, m4_rom_probe_source(0u));
        ck_int((long)it.sel, 0, "a 0-byte file is NOT selectable");
        ck_int((long)it.reason, M13B_R_EMPTY, "reason is EMPTY");

        m13b_item_set(&it, 1, 1, M4_ROM_MAX_SIZE + 1u,
                      m4_rom_probe_source(M4_ROM_MAX_SIZE + 1u));
        ck_int((long)it.sel, 0, "an oversized file is NOT selectable");
        ck_int((long)it.reason, M13B_R_BIG, "reason is TOO BIG");

        m13b_item_set(&it, 1, 1, M4_ROM_MIRROR_SIZE,
                      m4_rom_probe_source(M4_ROM_MIRROR_SIZE));
        ck_int((long)it.sel, 0,
               "an EXACTLY 1 MB file is NOT selectable -- the mirror path");
        ck_int((long)it.reason, M13B_R_1MB, "reason is 1MB MIRROR");

        /* FAILING CLOSED on a gate bit this build does not name. */
        m13b_item_set(&it, 1, 1, 4096u, 0x80u);
        ck_int((long)it.sel, 0, "an UNKNOWN source-gate bit is NOT selectable");
        ck_int((long)it.reason, M13B_R_SRC, "reason is the generic REJECTED");

        m13b_item_set(0, 1, 1, 0u, 0u);            /* must not fault */
        ck(1, "a NULL item is safe");

        /* the size is carried through for the footer/report */
        m13b_item_set(&it, 1, 1, 131072u, 0u);
        ck_int((long)it.size, 131072, "the measured size is carried through");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("M13B EQUIV FAILED\n");
        return 1;
    }
    printf("M13B EQUIV OK\n");
    return 0;
}
