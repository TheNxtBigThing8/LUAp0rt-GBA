/* LUAport M3 -- adapters/gba/gba_bios.c
 *
 * The gpSP-facing half of the M3 BIOS-handling milestone.
 *
 * WHAT THIS FILE IS
 * -----------------
 * The ONLY translation unit in the M3 build that touches gpSP's BIOS state. It
 * is the only new gpSP-side TU M3 adds at all; everything else in the closure is
 * M2's, linked unchanged.
 *
 * apps/m3gpsp/main.c sits on the other side of the type boundary (runtime/core.h,
 * shim.h, boot.inc) and cannot include gpSP headers: gpsp/common.h:100 typedefs
 * u64 as `unsigned long long` while runtime/core.h:30 uses `unsigned long`, and
 * a TU including both fails on the duplicate typedef. See adapters/gba/gba_bios.h.
 *
 * gpsp/ IS NOT MODIFIED. Not one line. Both symbols this file needs are
 * already declared by gpSP itself:
 *     gpsp/gba_memory.h:259   s32 load_bios(char *name);
 *     gpsp/gba_memory.h:277   extern u8 bios_rom[1024 * 16];
 * so unlike gba_probe.c, this file does not even need a hand-written extern.
 *
 * THE FINDING THIS FILE EXISTS TO ACT ON
 * --------------------------------------
 * load_bios() (gpsp/gba_memory.c:3093-3104) validates NOTHING:
 *
 *     RFILE *fd = filestream_open(name, RETRO_VFS_FILE_ACCESS_READ,
 *                                 RETRO_VFS_FILE_ACCESS_HINT_NONE);
 *     if(!fd) return -1;
 *     filestream_read(fd, bios_rom, 0x4000);    <- return value DISCARDED
 *     filestream_close(fd);
 *     return 0;
 *
 * It returns -1 ONLY when the open fails. An empty file, a 1-byte file and a
 * 4 MB file all return 0. There is no size check, no checksum, and no signature
 * test. The single content check that exists anywhere in the tree is
 * frontend-side and excluded from this build (libretro.c:1265, `bios_rom[0] !=
 * 0x18`).
 *
 * Therefore M3's real substance is the LUAport-side gate: MEASURE THE FILE
 * BEFORE LOADING IT (m3_bios_query) and PROVE THE BYTES ARRIVED AFTERWARDS
 * (m3_bios_probe_pre / m3_bios_probe_content). That before/after pair is to M3
 * what the noise-table pair was to M2.
 *
 * WHAT IT DELIBERATELY DOES NOT DO
 * --------------------------------
 *   - does NOT call execute_arm(), update_gba() or update_scanline()
 *                                          -- ZERO GBA instructions execute
 *   - does NOT read bios_rom through the emulated bus. gba_memory.c:736 (the
 *     `case 0x00` BIOS read in read_memory) is reachable ONLY from the
 *     interpreter, i.e. only under execute_arm(). Every read below is a plain
 *     C array subscript on a linked global.
 *   - does NOT load a ROM, call load_gamepak*(), or set up ROM paging
 *   - does NOT change selected_boot_mode -- it stays boot_game (gba_fixture.c:59)
 *   - does NOT allocate. bios_rom is static .bss and gba_filestream.c's RFILE
 *     pool is a static 4-slot array, so the arena delta across the whole BIOS
 *     path is expected to be exactly ZERO.
 *   - does NOT embed a BIOS. gpsp/bios_data.S and its 16,384-byte
 *     open_gba_bios_rom stay excluded, as at M1 and M2.
 *
 * WHY boot_bios IS NOT SELECTED
 * -----------------------------
 * init_cpu()'s boot_bios branch (gpsp/cpu.cc:3575-3581) reads the CARTRIDGE
 * header to validate the Nintendo logo:
 *     u8 *rom0 = memory_map_read[0x8000000 / (32 * 1024)];
 *     if (!rom0 || memcmp(&rom0[0x04], gba_header_logo, 156) != 0)
 *       force_game_boot = true;
 * With no ROM loaded that pointer is NULL, the `!rom0` short-circuit fires
 * before the memcmp (so there is no null dereference), force_game_boot becomes
 * true, and control falls into the boot_game branch anyway. Setting boot_bios at
 * M3 would therefore produce register state BYTE-IDENTICAL to boot_game while
 * introducing a ROM dependency for no gain. Testing it meaningfully requires a
 * ROM and belongs to M4/M5.
 */

#include "common.h"                  /* pulls gba_memory.h -> bios_rom,
                                        load_bios, memory_map_read           */

#include <streams/file_stream.h>     /* upstream header, used UNMODIFIED --
                                        the implementation behind it is
                                        adapters/gba/gba_filestream.c        */

#include "gba_bios.h"

/* The memory_map_read[] index for an address. map_region/map_null/map_vram
 * (gba_memory.c:2265-2286) all bucket the map in 32 KB (0x8000) units.
 * Same definition as gba_probe.c uses; each TU keeps its own so neither file
 * depends on the other's internals. */
#define MAP_IDX(addr) ((addr) / 0x8000)

/* The BIOS window is [0 .. 0x1000000) in 0x8000 units = 512 entries.
 * gba_memory.c:2410 maps it with mirror_blocks == 1, so every one of those
 * entries holds exactly bios_rom with no offset added. */
#define BIOS_WINDOW_ENTRIES 512u

/* ------------------------------------------------------ candidate sources --
 *
 * Switch-returns-literal rather than a static pointer table. A
 * `static const char *cands[] = {...}` puts one POINTER PER ENTRY in
 * .data.rel.ro and each needs its own R_X86_64_RELATIVE relocation; returning a
 * literal compiles to a RIP-relative LEA and needs none. Same technique, and
 * the same reason, as the bit-name functions in apps/m2gpsp/main.c.
 *
 * Order is significant -- first hit wins. See gba_bios.h for why /temp0 leads. */
static const char *bios_candidate(unsigned int idx)
{
    switch (idx) {
    case 0:  return "/temp0/gba_bios.bin";
    case 1:  return "/savedata0/bios/gba_bios.bin";
    default: return 0;
    }
}

unsigned int m3_bios_candidate_count(void)
{
    return M3_BIOS_CANDIDATES;
}

const char *m3_bios_candidate_name(unsigned int idx)
{
    return bios_candidate(idx);
}

/* load_bios() takes `char *`, not `const char *` (gba_memory.h:259). Rather
 * than cast away const on a string literal -- which would be undefined behaviour
 * the moment anything wrote through it -- the name is copied into this writable
 * buffer. It lives in .bss, so it costs ZERO bytes in the transferred blob and
 * ZERO against the JIT reservation.
 *
 * 128 bytes is ample: the longest candidate is 29 characters. The copy is
 * bounded and always NUL-terminates. */
static char bios_name[128];

static void bios_name_set(const char *s)
{
    unsigned int i = 0;

    while (s[i] != '\0' && i < (unsigned int)(sizeof(bios_name) - 1)) {
        bios_name[i] = s[i];
        i++;
    }
    bios_name[i] = '\0';
}

/* --------------------------------------------------------------- actions -- */

int m3_bios_query(unsigned int idx, unsigned int *size_out)
{
    const char *nm = bios_candidate(idx);
    RFILE      *fd;
    int64_t     sz;

    if (size_out)
        *size_out = 0;

    if (!nm || !size_out)
        return 0;

    /* Read-only, exactly as load_bios opens it (gba_memory.c:3095-3096). The
     * LUAport filestream adapter REFUSES anything but a pure read
     * (gba_filestream.c:107-108), so this cannot accidentally create or
     * truncate the user's BIOS file. */
    fd = filestream_open(nm, RETRO_VFS_FILE_ACCESS_READ,
                         RETRO_VFS_FILE_ACCESS_HINT_NONE);
    if (!fd)
        return 0;                   /* candidate not present / not readable  */

    sz = filestream_get_size(fd);
    filestream_close(fd);

    /* A negative size means the handle opened but could not be measured. It is
     * reported as 0 rather than swallowed, so the fixture's exact-size gate
     * rejects it instead of proceeding on an unknown length. */
    if (sz < 0)
        return 1;

    *size_out = (unsigned int)sz;
    return 1;
}

int m3_bios_load(unsigned int idx)
{
    const char *nm = bios_candidate(idx);

    if (!nm)
        return -1;

    bios_name_set(nm);

    /* gpSP's OWN loader, called unmodified. M3 deliberately does not reimplement
     * the read: the milestone is about proving gpSP's real BIOS path works on
     * PS5, not about routing around it. The validation that load_bios omits is
     * added AROUND it, never inside it. */
    return (int)load_bios(bios_name);
}

/* ------------------------------------------------------------- helpers ---- */

/* Single pass, answering both "all zero?" and "all 0xFF?" at once and counting
 * the non-zero bytes for the diagnostic log. One walk over 16 KB rather than
 * three. */
static void bios_scan(unsigned int *all_zero, unsigned int *all_ff,
                      unsigned int *nonzero)
{
    unsigned int i, z = 1, f = 1, nz = 0;

    for (i = 0; i < M3_BIOS_SIZE; i++) {
        u8 b = bios_rom[i];
        if (b != 0x00) { z = 0; nz++; }
        if (b != 0xFF) { f = 0; }
    }

    if (all_zero) *all_zero = z;
    if (all_ff)   *all_ff   = f;
    if (nonzero)  *nonzero  = nz;
}

/* FNV-1a, 32-bit. Chosen over CRC32 deliberately: a CRC table would cost 1 KB
 * of .rodata plus its relocations, while this is a couple of dozen bytes of
 * code and no data at all.
 *
 * REPORTED, NEVER ASSERTED. Pinning a specific value would reject the GPL2
 * replacement BIOS and would hard-code a fingerprint of data we do not
 * distribute. It exists so two runs can be compared, and so a support log can
 * say WHICH BIOS was used without shipping any of its bytes. */
static unsigned int bios_fnv1a(void)
{
    unsigned int h = 2166136261u, i;

    for (i = 0; i < M3_BIOS_SIZE; i++) {
        h ^= (unsigned int)bios_rom[i];
        h *= 16777619u;
    }
    return h;
}

static unsigned int bios_sum(void)
{
    unsigned int s = 0, i;

    for (i = 0; i < M3_BIOS_SIZE; i++)
        s += (unsigned int)bios_rom[i];
    return s;
}

/* Is the whole 16 MB BIOS window mapped to bios_rom? See gba_bios.h for why
 * every one of the 512 entries must hold bios_rom exactly. */
static unsigned int bios_window_ok(void)
{
    unsigned int i;

    for (i = 0; i < BIOS_WINDOW_ENTRIES; i++)
        if (memory_map_read[i] != bios_rom)
            return 0;
    return 1;
}

/* ---------------------------------------------------------------- probes -- */

unsigned int m3_bios_probe_pre(void)
{
    unsigned int f = 0, all_zero;

    /* bios_rom is .bss and NOTHING on the reset path memsets it -- init_memory's
     * memset list (gba_memory.c:2421-2426) does not include it. So a non-zero
     * byte here means boot_data_region did not zero .bss, which would invalidate
     * every other assertion in the fixture. The caller treats it as fatal. */
    bios_scan(&all_zero, 0, 0);
    if (!all_zero)
        f |= M3_PRE_BIOS_ZERO;

    return f;
}

unsigned int m3_bios_probe_content(void)
{
    unsigned int f = 0, all_zero, all_ff;

    bios_scan(&all_zero, &all_ff, 0);

    /* Still entirely zero after a "successful" load means the read produced
     * nothing -- precisely the failure load_bios cannot report, because it
     * discards filestream_read's return value. */
    if (all_zero)
        f |= M3_CONTENT_ALLZERO;

    /* Entirely 0xFF is blank or erased media, not a BIOS. */
    if (all_ff)
        f |= M3_CONTENT_ALLFF;

    /* Upstream's own signature test, libretro.c:1265. 0x18 is the low byte of
     * the BIOS's first instruction, `b 0x000000E0` = 18 00 00 EA little-endian.
     * This is the strongest content claim M3 makes, and it is deliberately the
     * only one: no hash is compared, so the GPL2 replacement BIOS passes. */
    if (bios_rom[0] != 0x18)
        f |= M3_CONTENT_FIRSTBYTE;

    return f;
}

unsigned int m3_bios_probe_map(void)
{
    unsigned int f = 0;

    /* init_memory(), gba_memory.c:2410. */
    if (memory_map_read[MAP_IDX(0x0000000)] != bios_rom)
        f |= M3_MAP_BIOS0;

    if (!bios_window_ok())
        f |= M3_MAP_WINDOW;

    /* THE ROM BOUNDARY. Absent from init_memory's mapping list entirely, so a
     * non-NULL here is positive proof that something loaded a ROM -- which M3
     * is forbidden to do. This is the assertion that keeps M3 out of M4. */
    if (memory_map_read[MAP_IDX(0x8000000)] != 0)
        f |= M3_MAP_ROM_PRESENT;

    return f;
}

/* ------------------------------------------------------------- raw reads -- */

unsigned int m3_bios_read(unsigned int sel)
{
    unsigned int nz;

    switch (sel) {
    case M3_RD_BIOS_B0:      return (unsigned int)bios_rom[0];
    case M3_RD_BIOS_B1:      return (unsigned int)bios_rom[1];
    case M3_RD_BIOS_B2:      return (unsigned int)bios_rom[2];
    case M3_RD_BIOS_B3:      return (unsigned int)bios_rom[3];
    case M3_RD_BIOS_FNV:     return bios_fnv1a();
    case M3_RD_BIOS_SUM:     return bios_sum();
    case M3_RD_BIOS_NONZERO: bios_scan(0, 0, &nz); return nz;
    case M3_RD_MAP_BIOS_OK:  return bios_window_ok();
    case M3_RD_MAP_ROM_NULL:
        return (memory_map_read[MAP_IDX(0x8000000)] == 0) ? 1u : 0u;
    default:                 return 0u;
    }
}
