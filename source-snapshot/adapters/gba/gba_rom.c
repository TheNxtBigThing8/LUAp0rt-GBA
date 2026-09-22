/* LUAport M4 -- adapters/gba/gba_rom.c
 *
 * The gpSP-facing half of the M4 ROM-loading milestone.
 *
 * WHAT THIS FILE IS
 * -----------------
 * The ONLY translation unit in the M4 build that touches gpSP's cartridge
 * (gamepak) state. It is the only new gpSP-side TU M4 adds at all; everything
 * else in the closure is M3's, linked unchanged.
 *
 * apps/m4gpsp/main.c sits on the other side of the type boundary (runtime/
 * core.h, shim.h, boot.inc) and cannot include gpSP headers: gpsp/common.h:100
 * typedefs u64 as `unsigned long long` while runtime/core.h:30 uses `unsigned
 * long`, and a TU including both fails on the duplicate typedef. See
 * adapters/gba/gba_rom.h.
 *
 * gpsp/ IS NOT MODIFIED. Not one line. Every symbol read below is either
 * already declared in a gpSP header or is a non-static global declared extern
 * by hand -- the same technique upstream itself uses at gpsp/gba_memory.c:198
 * (`extern timer_type timer[4];`) and that adapters/gba/gba_probe.c:75-85
 * already established for M2.
 *
 * WHAT IT DELIBERATELY DOES NOT DO
 * --------------------------------
 *   - does NOT call execute_arm() or execute_arm_translate()
 *                                          -- ZERO GBA instructions execute
 *   - does NOT call update_gba() or update_scanline() -- nothing is rendered
 *   - does NOT call load_gamepak_page(). That function is reached ONLY from
 *     execution-time read handlers and the interpreter's fetch path
 *     (gpsp/cpu.cc:586, cpu.cc:1492, gba_memory.c:616/1848/1860), so with a
 *     fully resident ROM it is never entered. Paging is an M5+ concern.
 *   - does NOT open, read, create or write a save file. load_gamepak performs
 *     NO save I/O whatsoever -- it only DETECTS a backup type in memory. The
 *     .sav path belongs to M11/M12 and no such path is resolved here.
 *   - does NOT change selected_boot_mode -- it stays boot_game
 *     (adapters/gba/gba_fixture.c:59), the init_cpu branch that does NOT read
 *     the cartridge header.
 *   - does NOT embed a ROM. The file is uploaded out of band, so the cartridge
 *     contributes ZERO bytes to .text/.rodata/.data.
 *
 * THE ONE THING M4 IS THE FIRST MILESTONE TO EXERCISE
 * ---------------------------------------------------
 * load_gamepak ends by calling rtc_init_base_time() (gba_memory.c:3088), which
 * calls time(). M1-M3 LINKED that path but never executed it; M4 is the first
 * milestone to actually enter it. runtime/shim.c:499 backs time() with
 * gettimeofday, which apps/m4gpsp/main.c resolves and installs, and it degrades
 * safely to 0 if the symbol is absent.
 */

#include "common.h"                  /* pulls gba_memory.h -> load_gamepak,
                                        init_gamepak_buffer, gamepak_size,
                                        memory_map_read, bios_rom,
                                        gamepak_backup, backup_type          */

#include <streams/file_stream.h>     /* upstream header, used UNMODIFIED --
                                        the implementation behind it is
                                        adapters/gba/gba_filestream.c        */

#include "gba_rom.h"

/* The memory_map_read[] index for an address. map_region/map_null/map_rom_entry
 * (gba_memory.c:2254-2288) all bucket the map in 32 KB (0x8000) units. Same
 * definition gba_probe.c and gba_bios.c each keep privately, so no file depends
 * on another's internals. */
#define MAP_IDX(addr) ((addr) / 0x8000)

/* The three cartridge windows map_rom_entry writes, gba_memory.c:2254-2263.
 * 0x08000000 and 0x0A000000 get 1024 entries each; 0x0C000000 gets 512. */
#define ROM_IDX_08  MAP_IDX(0x8000000)   /* 4096 */
#define ROM_IDX_0A  MAP_IDX(0xA000000)   /* 5120 */
#define ROM_IDX_0C  MAP_IDX(0xC000000)   /* 6144 */
#define ROM_IDX_0D  MAP_IDX(0xD000000)   /* 6656 -- the EEPROM window        */

/* ---------------------------------------------------- hand-declared state --
 *
 * Each of these is a NON-STATIC global in gpSP that simply has no extern in a
 * gpSP header. Everything M4 needs that IS in a header (gamepak_size,
 * gamepak_buffer_count, gamepak_mini_materialized, gamepak_header_nonstandard,
 * backup_type, gamepak_backup, memory_map_read, bios_rom, load_gamepak,
 * init_gamepak_buffer) is used through that header and is not restated here. */
extern u8      *gamepak_buffers[32];         /* gba_memory.c:369             */
extern u32      gamepak_file_blocks;         /* gba_memory.c:372             */
extern bool     gamepak_mirror_1m;           /* gba_memory.c:373             */
extern const unsigned gamepak_buffer_blocksize;  /* gba_memory.c:378         */
extern u16      gamepak_lru_head;            /* gba_memory.c:386             */
extern u16      gamepak_lru_tail;            /* gba_memory.c:387             */
extern bool     is_known_game;               /* gba_memory.c:289             */
extern u32      backup_type_reset;           /* gba_memory.c:415             */

/* gamepak_blk_queue is an UNTAGGED struct array defined at gba_memory.c:381-384
 * and declared in no header. The redeclaration below has the same (absent) tag,
 * the same member count, the same member names and the same member types, which
 * is precisely the condition C11 6.2.7p1 requires for two structure types in
 * SEPARATE TRANSLATION UNITS to be COMPATIBLE. It is therefore a legal
 * declaration of the same object, not a type pun.
 *
 * NOT declared static: this must bind to gpSP's definition. */
extern struct {
  u16 next_lru;
  s16 phy_rom;
} gamepak_blk_queue[1024];

/* ------------------------------------------------------ candidate sources --
 *
 * Switch-returns-literal rather than a static pointer table, for the same
 * measured reason as gba_bios.c:104 and apps/m2gpsp/main.c's bit-name
 * functions: a `static const char *cands[] = {...}` puts one POINTER PER ENTRY
 * in .data.rel.ro and each needs its own R_X86_64_RELATIVE relocation, whereas
 * returning a literal compiles to a RIP-relative LEA and needs none. At M4 that
 * matters more than ever -- the milestone has only 19,188 bytes of headroom
 * before __data_load crosses 0x40000.
 *
 * Order is significant -- first hit wins. See gba_rom.h for why /temp0 leads. */
static const char *rom_candidate(unsigned int idx)
{
    switch (idx) {
    case 0:  return "/temp0/test.gba";
    case 1:  return "/savedata0/rom/test.gba";
    default: return 0;
    }
}

unsigned int m4_rom_candidate_count(void)
{
    return M4_ROM_CANDIDATES;
}

const char *m4_rom_candidate_name(unsigned int idx)
{
    return rom_candidate(idx);
}

/* load_gamepak takes `const char *name` (gba_memory.h:257), so unlike M3's
 * load_bios -- which takes a non-const `char *` and forced a writable copy --
 * no copy is strictly required here. One is kept anyway, in .bss, so that the
 * name the fixture reports and the name gpSP opened are provably the same
 * object even if the candidate list is ever built dynamically. It costs ZERO
 * bytes in the transferred blob. */
static char rom_name[M4_ROM_NAME_MAX];

/* The buffer and the advertised bound are the same number, checked by the
 * compiler rather than by a comment. If either ever moves without the other,
 * this is a build error and not a runtime surprise. */
_Static_assert(sizeof(rom_name) == M4_ROM_NAME_MAX,
               "rom_name must be exactly M4_ROM_NAME_MAX bytes");

/* Copy s into rom_name, or REFUSE.
 *
 * ***** THE CHANGE THAT MATTERS AT M13B. *****
 * The M4 version of this function clamped at sizeof(rom_name)-1 and returned
 * void, so an over-long name was SILENTLY SHORTENED and then handed to
 * load_gamepak. With two compiled-in literals of 16 and 24 bytes that could
 * never fire. With a user-chosen path from the M13B picker it can, and the
 * failure mode is not "load fails" -- it is "a DIFFERENT file is opened",
 * which on a ROM library is indistinguishable from the picker working.
 *
 * So the length is now tested BEFORE anything is written:
 *
 *   fits     -> copied EXACTLY, NUL-terminated, returns 1
 *   does not -> rom_name is set to "" and NOTHING is copied, returns 0
 *
 * Clearing on refusal is deliberate: m4_rom_name() is what the fixture prints,
 * and reporting a stale name next to a failed load would misdirect the operator
 * to the wrong file. This mirrors m13store_join(), which on failure "writes
 * nothing and truncates nothing" (m13store.h:153-155).
 *
 * BEHAVIOUR FOR EVERY PRE-M13B INPUT IS UNCHANGED: both candidates are far
 * shorter than 128, so they fitted before and are copied byte-for-byte now. */
static int rom_name_set(const char *s)
{
    unsigned int i = 0;

    rom_name[0] = '\0';

    if (!s)
        return 0;

    /* Measure first. The loop below must not begin unless the WHOLE string,
     * plus its terminator, is known to fit. */
    while (s[i] != '\0') {
        if (i >= (unsigned int)(sizeof(rom_name) - 1))
            return 0;              /* REFUSED -- nothing written, nothing cut */
        i++;
    }

    for (i = 0; s[i] != '\0'; i++)
        rom_name[i] = s[i];

    rom_name[i] = '\0';
    return 1;
}

const char *m4_rom_name(void)
{
    return rom_name;
}

/* Is a ROM resident at all? Every content probe below must refuse to
 * dereference gamepak_buffers[0] when init_gamepak_buffer got nothing. */
static int rom_resident(void)
{
    return (gamepak_buffer_count > 0 && gamepak_buffers[0] != 0) ? 1 : 0;
}

/* How many bytes of ROM are actually in RAM. init_gamepak_buffer allocates
 * whole 1 MB blocks and load_gamepak_raw fills `ldblks` of them
 * (gba_memory.c:2754-2757), so residency is bounded by both the buffer count
 * and the ROM size. */
static unsigned int rom_resident_bytes(void)
{
    unsigned int have = gamepak_buffer_count * (unsigned int)gamepak_buffer_blocksize;
    return (gamepak_size < have) ? gamepak_size : have;
}

/* --------------------------------------------------------------- actions -- */

unsigned int m4_rom_buffer_init(void)
{
    /* gpSP's OWN allocator loop, called unmodified (gba_memory.c:2359). It is
     * GREEDY: it mallocs 1 MB at a time until one fails or ROM_BUFFER_SIZE is
     * reached. M4 compiles its gpSP objects with -DROM_BUFFER_SIZE=2, so the
     * bound is the flag and not arena exhaustion -- which is what leaves the
     * arena with genuine headroom instead of zero. */
    init_gamepak_buffer();
    return gamepak_buffer_count;
}

/* THE INDEX API, PRESERVED BY DELEGATION.
 *
 * The prologue order below is the M4 original's, verbatim: *size_out is cleared
 * BEFORE the NULL checks, so a caller that passes a bad index still sees 0
 * rather than an untouched variable. Resolving the index here and clearing in
 * the callee reproduces that exactly -- m4_rom_query_named() opens with the same
 * two statements. Nothing about the index path's observable behaviour changes. */
int m4_rom_query(unsigned int idx, unsigned int *size_out)
{
    return m4_rom_query_named(rom_candidate(idx), size_out);
}

int m4_rom_query_named(const char *name, unsigned int *size_out)
{
    const char *nm = name;
    RFILE      *fd;
    int64_t     sz;

    if (size_out)
        *size_out = 0;

    if (!nm || !size_out)
        return 0;

    /* Read-only, exactly as load_gamepak_raw opens it (gba_memory.c:2676-2677).
     * The LUAport filestream adapter REFUSES anything but a pure read
     * (gba_filestream.c:107-108), so this cannot create or truncate the file. */
    fd = filestream_open(nm, RETRO_VFS_FILE_ACCESS_READ,
                         RETRO_VFS_FILE_ACCESS_HINT_NONE);
    if (!fd)
        return 0;                   /* candidate not present / not readable  */

    /* filestream_get_size RESTORES the file position (gba_filestream.c:204), so
     * measuring here does not disturb a later open. */
    sz = filestream_get_size(fd);
    filestream_close(fd);

    /* A negative size means the handle opened but could not be measured. It is
     * reported as 0 rather than swallowed, so the source gate rejects it
     * instead of proceeding on an unknown length. */
    if (sz < 0 || sz > (int64_t)0xFFFFFFFF)
        return 1;

    *size_out = (unsigned int)sz;
    return 1;
}

unsigned int m4_rom_expected_size(unsigned int file_size)
{
    /* gba_memory.c:2701 -- `raw_size = (raw_size + 0x7FFF) & ~0x7FFF`.
     * THE ASSERTION THAT WOULD HAVE BEEN WRONG is `gamepak_size == file_size`:
     * for a 4,096-byte ROM the correct expectation is 32,768. */
    return (file_size + (M4_ROM_PAGE - 1u)) & ~(M4_ROM_PAGE - 1u);
}

unsigned int m4_rom_probe_source(unsigned int file_size)
{
    unsigned int f = 0;

    if (file_size < M4_ROM_MIN_SIZE)
        f |= M4_SRC_EMPTY;

    if (file_size > M4_ROM_MAX_SIZE)
        f |= M4_SRC_TOOBIG;

    /* See gba_rom.h: a file of exactly 1 MB takes the mirror path and attempts
     * malloc(4 MB). Rejecting it up front is honest; letting it silently take
     * the degraded fallback at gba_memory.c:2748 is not. */
    if (file_size == M4_ROM_MIRROR_SIZE)
        f |= M4_SRC_MIRROR_1M;

    return f;
}

void m4_rom_backup_init(void)
{
    /* UPSTREAM PARITY: gpsp/libretro/libretro.c:1278 performs exactly this
     * memset immediately before its load_gamepak call. gamepak_backup is 128 KB
     * of .bss (gba_memory.c:360). 0xFF is the real idle state of flash and
     * EEPROM; 0x00 is what a blank frontend-created save file looks like.
     *
     * THIS IS A BUFFER, NOT A FILE. No save file is opened here or anywhere
     * else in M4. */
    memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));
}

/* THE INDEX API, PRESERVED BY DELEGATION. rom_candidate() is untouched, the
 * NULL check keeps the same -1, and the resulting load_gamepak call is
 * ARGUMENT-IDENTICAL to the one M4 through M12 have always issued. */
int m4_rom_load(unsigned int idx)
{
    const char *nm = rom_candidate(idx);

    if (!nm)
        return -1;

    return m4_rom_load_named(nm);
}

int m4_rom_load_named(const char *nm)
{
    u32 rc;

    if (!nm)
        return -1;

    /* REFUSAL IS A HARD STOP, AND IT HAPPENS BEFORE gpSP IS INVOLVED. A name
     * that does not fit is never shortened and never passed on: load_gamepak is
     * not called at all, so there is no opportunity for it to open a neighbour
     * of the file the user actually chose. */
    if (!rom_name_set(nm))
        return -1;

    /* gpSP's OWN loader, called unmodified. The three autodetect arguments are
     * the ones upstream passes at libretro.c:1279, so M4 exercises the real
     * default path rather than a specially configured one.
     *
     * The `info` parameter is passed NULL: load_gamepak's body never references
     * it (gba_memory.c:2964-3091), and `struct retro_game_info` has no
     * definition anywhere in this build. */
    rc = load_gamepak(NULL, rom_name, FEAT_AUTODETECT, FEAT_AUTODETECT,
                      SERIAL_MODE_AUTO);

    /* load_gamepak is declared u32 and returns -1 on failure, so 0xFFFFFFFF is
     * the failure value. Normalised to a plain int for the boundary. */
    return (rc == 0) ? 0 : -1;
}

/* ------------------------------------------------------------- helpers ---- */

/* Are all 1024+1024+512 entries of the three cartridge windows exactly
 * gamepak_buffers[0]? True only for a single-page ROM, where map_rom_entry is
 * called once with idx 0 and mirror_blocks 1 and therefore writes every slot.
 * For a multi-page ROM this is legitimately false, so the caller only asserts
 * it when gamepak_file_blocks == 1. */
static unsigned int rom_mirror_ok(void)
{
    unsigned int i;
    u8 *p;

    if (!rom_resident())
        return 0;

    p = gamepak_buffers[0];

    for (i = 0; i < 1024; i++) {
        if (memory_map_read[ROM_IDX_08 + i] != p)
            return 0;
        if (memory_map_read[ROM_IDX_0A + i] != p)
            return 0;
    }
    for (i = 0; i < 512; i++) {
        if (memory_map_read[ROM_IDX_0C + i] != p)
            return 0;
    }
    return 1;
}

/* Does the LRU queue record physical page 0 as resident? load_gamepak_raw takes
 * an entry from evict_gamepak_page() and stamps phy_rom with the page number
 * (gba_memory.c:2786-2787). The entry INDEX is whatever the queue handed out,
 * so M4 searches rather than assuming entry 0 -- asserting
 * gamepak_blk_queue[0].phy_rom == 0 would be an assumption about the queue's
 * internal ordering, not a fact about the load. */
static unsigned int rom_page0_recorded(void)
{
    unsigned int i;

    for (i = 0; i < 1024; i++)
        if (gamepak_blk_queue[i].phy_rom == 0)
            return 1;
    return 0;
}

/* --------------------------------------------------------------- probes --- */

unsigned int m4_rom_probe_pre(void)
{
    unsigned int f = 0;

    /* FATAL if zero: load_gamepak_raw computes ldblks from this and returns -1
     * when it is 0, leaving an unmapped ROM space (gba_memory.c:2763-2768). */
    if (gamepak_buffer_count == 0)
        f |= M4_PRE_NO_BUFFERS;

    if (gamepak_buffers[0] == 0)
        f |= M4_PRE_BUF0_NULL;

    /* Nothing has loaded yet, so gpSP's own size must still be its .bss zero. */
    if (gamepak_size != 0)
        f |= M4_PRE_SIZE_SET;

    /* THE M3 BOUNDARY, RE-ASSERTED. init_memory deliberately never maps the
     * gamepak window (it is absent from gba_memory.c:2410-2419), so this must
     * still be NULL before M4 loads anything. */
    if (memory_map_read[ROM_IDX_08] != 0)
        f |= M4_PRE_ROM_MAPPED;

    return f;
}

unsigned int m4_rom_probe_meta(unsigned int file_size)
{
    unsigned int f = 0;
    unsigned int want = m4_rom_expected_size(file_size);

    /* gba_memory.c:2701-2704. NOT the raw file size -- see m4_rom_expected_size. */
    if (gamepak_size != want)
        f |= M4_META_SIZE;

    /* gba_memory.c:2702 -- gamepak_file_blocks = raw_size >> 15. */
    if (gamepak_file_blocks != (want >> 15))
        f |= M4_META_BLOCKS;

    /* Both must be false for an ordinary ROM. mirror_1m is only set for a file
     * of exactly 1 MB (gba_memory.c:2703), which the source gate already
     * refuses; mini_materialized is only set on that same path at :2735. */
    if (gamepak_mirror_1m)
        f |= M4_META_MIRROR1M;

    if (gamepak_mini_materialized)
        f |= M4_META_MINI;

    if (gamepak_buffer_count < 1)
        f |= M4_META_BUFCOUNT;

    if (gamepak_buffers[0] == 0)
        f |= M4_META_BUF0_NULL;

    /* gba_memory.c:2972-2973. A set flag means byte[3] != 0xEA or
     * byte[0xB2] != 0x96 -- a legitimate state for a ROM hack, but not for the
     * known-good test cartridge M4 is gated on. */
    if (gamepak_header_nonstandard)
        f |= M4_META_HEADER;

    if (!rom_page0_recorded())
        f |= M4_META_LRU;

    return f;
}

unsigned int m4_rom_probe_content(unsigned int file_size)
{
    unsigned int f = 0;
    unsigned int i, all_zero = 1;
    unsigned int resident = rom_resident_bytes();
    unsigned int scan;

    if (!rom_resident())
        return M4_CONTENT_ALLZERO;   /* nothing to inspect -- report, do not
                                        dereference a NULL buffer            */

    scan = (resident < M4_ROM_PAGE) ? resident : M4_ROM_PAGE;

    for (i = 0; i < scan; i++) {
        if (gamepak_buffers[0][i] != 0x00) {
            all_zero = 0;
            break;
        }
    }
    if (all_zero)
        f |= M4_CONTENT_ALLZERO;

    /* The two fixed header bytes gpSP itself tests at gba_memory.c:2973.
     * 0xEA is the ARM `b` opcode of the entry branch; 0x96 is the GBA fixed
     * value. Guarded so a ROM shorter than the header cannot read past it. */
    if (file_size > 3 && gamepak_buffers[0][3] != 0xEA)
        f |= M4_CONTENT_EA;

    if (file_size > 0xB2 && gamepak_buffers[0][0xB2] != 0x96)
        f |= M4_CONTENT_96;

    /* THE SHORT-READ PADDING CONTRACT, gba_memory.c:2779-2780. filestream_read
     * returns the TRUE byte count, and load_gamepak_raw memsets the remainder
     * of the 1 MB block to 0xFF. So every byte from the true file length to the
     * end of the resident page must be 0xFF. This is what proves the file was
     * not silently truncated in transit -- the one failure load_gamepak_raw
     * cannot itself report. */
    if (file_size < scan) {
        for (i = file_size; i < scan; i++) {
            if (gamepak_buffers[0][i] != 0xFF) {
                f |= M4_CONTENT_PAD;
                break;
            }
        }
    }

    return f;
}

unsigned int m4_rom_probe_map(void)
{
    unsigned int f = 0;
    u8 *p;

    if (!rom_resident())
        return M4_MAP_ROM08 | M4_MAP_ROM0A | M4_MAP_ROM0C | M4_MAP_MIRROR;

    p = gamepak_buffers[0];

    /* The three windows map_rom_entry writes, gba_memory.c:2257-2261. */
    if (memory_map_read[ROM_IDX_08] != p)
        f |= M4_MAP_ROM08;

    if (memory_map_read[ROM_IDX_0A] != p)
        f |= M4_MAP_ROM0A;

    if (memory_map_read[ROM_IDX_0C] != p)
        f |= M4_MAP_ROM0C;

    /* Full-window mirroring is only the single-page contract. For a multi-page
     * ROM each page gets its own pointer and this would legitimately differ. */
    if (gamepak_file_blocks == 1 && !rom_mirror_ok())
        f |= M4_MAP_MIRROR;

    /* THE EEPROM BOUNDARY. load_gamepak_raw's map_null clears 0x8000000 to
     * 0xD000000 only (gba_memory.c:2771), and map_rom_entry's 0x0C loop stops
     * after 512 entries -- so 0x0D000000 must remain NULL. A non-NULL here
     * means the cartridge was mapped over the EEPROM window. */
    if (memory_map_read[ROM_IDX_0D] != 0)
        f |= M4_MAP_EEPROM;

    /* THE BIOS MUST SURVIVE. init_memory maps [0, 0x1000000) to bios_rom
     * (gba_memory.c:2410) and load_gamepak touches nothing below 0x8000000, so
     * M3's guarantee must still hold after the cartridge is in place. */
    if (memory_map_read[0] != bios_rom)
        f |= M4_MAP_BIOS;

    return f;
}

/* ------------------------------------------------------------- integrity -- */

unsigned int m4_rom_fnv1a(unsigned int file_size)
{
    unsigned int h = 2166136261u, i;
    unsigned int n = rom_resident_bytes();

    if (!rom_resident())
        return 0;

    /* Over the TRUE ROM length, never the 0xFF padding -- so the console figure
     * is directly comparable with the one tools/upload.py prints for the source
     * file. Bounded by residency so a paged ROM hashes only what is in RAM. */
    if (file_size < n)
        n = file_size;

    for (i = 0; i < n; i++) {
        h ^= (unsigned int)gamepak_buffers[0][i];
        h *= 16777619u;
    }
    return h;
}

/* ---------------------------------------------------------------- header -- */

/* Render one header byte printable. A GBA title field is space-padded ASCII,
 * but a homebrew or NSP-extracted ROM can carry zeros or high bytes there, and
 * a raw 0x00 would truncate the log line. */
static char rom_printable(u8 b)
{
    return (b >= 0x20 && b < 0x7F) ? (char)b : '.';
}

static void rom_field_copy(char *dst, unsigned int dstsz,
                           unsigned int off, unsigned int len)
{
    unsigned int i;

    if (!dst || dstsz == 0)
        return;

    if (!rom_resident()) {
        dst[0] = '\0';
        return;
    }

    if (len > dstsz - 1)
        len = dstsz - 1;

    for (i = 0; i < len; i++)
        dst[i] = rom_printable(gamepak_buffers[0][off + i]);

    dst[len] = '\0';
}

void m4_rom_title_copy(char *dst, unsigned int dstsz)
{
    /* 12 bytes at 0xA0 -- the field load_gamepak itself reads at
     * gba_memory.c:3027 for its Pokemon-family check. */
    rom_field_copy(dst, dstsz, 0xA0, 12);
}

void m4_rom_code_copy(char *dst, unsigned int dstsz)
{
    /* 4 bytes at 0xAC -- the field load_gamepak copies into its LOCAL
     * game_code[5] at gba_memory.c:2978 and hands to load_game_config_over.
     * M4 re-reads it here because that local is not observable and the
     * gamepak_code global it looks like is never defined. */
    rom_field_copy(dst, dstsz, 0xAC, 4);
}

unsigned int m4_rom_header_byte(unsigned int off)
{
    if (!rom_resident() || off >= M4_ROM_PAGE)
        return 0;
    return (unsigned int)gamepak_buffers[0][off];
}

/* ------------------------------------------------------------- raw reads -- */

unsigned int m4_rom_read(unsigned int sel)
{
    switch (sel) {
    case M4_RD_SIZE:         return gamepak_size;
    case M4_RD_FILE_BLOCKS:  return gamepak_file_blocks;
    case M4_RD_MIRROR_1M:    return gamepak_mirror_1m ? 1u : 0u;
    case M4_RD_MINI:         return gamepak_mini_materialized ? 1u : 0u;
    case M4_RD_BUFFER_COUNT: return gamepak_buffer_count;
    case M4_RD_BLOCKSIZE:    return (unsigned int)gamepak_buffer_blocksize;
    case M4_RD_HDR_NONSTD:   return gamepak_header_nonstandard ? 1u : 0u;
    case M4_RD_KNOWN_GAME:   return is_known_game ? 1u : 0u;
    case M4_RD_BACKUP_TYPE:  return backup_type;
    case M4_RD_BACKUP_RESET: return backup_type_reset;
    case M4_RD_LRU_HEAD:     return (unsigned int)gamepak_lru_head;
    case M4_RD_LRU_TAIL:     return (unsigned int)gamepak_lru_tail;
    case M4_RD_PAGE0_ENTRY:  return rom_page0_recorded();
    case M4_RD_MAP08_OK:
        return (rom_resident() &&
                memory_map_read[ROM_IDX_08] == gamepak_buffers[0]) ? 1u : 0u;
    case M4_RD_MAP0A_OK:
        return (rom_resident() &&
                memory_map_read[ROM_IDX_0A] == gamepak_buffers[0]) ? 1u : 0u;
    case M4_RD_MAP0C_OK:
        return (rom_resident() &&
                memory_map_read[ROM_IDX_0C] == gamepak_buffers[0]) ? 1u : 0u;
    case M4_RD_MAP0D_NULL:
        return (memory_map_read[ROM_IDX_0D] == 0) ? 1u : 0u;
    case M4_RD_MIRROR_OK:    return rom_mirror_ok();
    case M4_RD_BIOS_MAPPED:  return (memory_map_read[0] == bios_rom) ? 1u : 0u;
    case M4_RD_ROM_RESIDENT: return rom_resident() ? rom_resident_bytes() : 0u;
    default:                 return 0u;
    }
}
