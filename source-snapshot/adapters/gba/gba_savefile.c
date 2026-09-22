/* LUAport M12B -- adapters/gba/gba_savefile.c
 *
 * The runtime-facing half of GBA cartridge save persistence.
 * adapters/gba/gba_savefile.h carries the contract -- the two-TU split, the
 * structural cleanup ladder and the fflush-before-fclose rule. READ IT FIRST,
 * and read adapters/gba/gba_save.h before that.
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO
 * ---------------------------------------
 *   - does NOT include a single gpSP header, and so cannot name backup_type,
 *     gamepak_backup, eeprom_size or flash_bank_cnt even by accident. It could
 *     not compile if it tried: runtime/savedata.h pulls in runtime/core.h,
 *     whose u64 typedef collides with gpsp/common.h's. Every save byte arrives
 *     through gba_save_chunk().
 *   - does NOT decide whether to persist. gba_save_is_dirty(),
 *     gba_save_class() and gba_save_region_bytes() make that call; this file
 *     only obeys, and refuses when they say there is nothing to write.
 *   - does NOT allocate. The staging buffer is 4096 bytes of STACK. There is no
 *     second copy of gamepak_backup[] anywhere in M12B.
 *   - does NOT keep the write window open longer than the two file writes.
 *     runtime/savedata.h:120 asks callers to prepare outside the window and
 *     copy in; both filenames and the whole refusal ladder are therefore
 *     evaluated BEFORE plat_savedata_begin_write() is called.
 */

#include "savedata.h"      /* PLAT_SD_*, plat_savedata_* -- and runtime/core.h */

#include <stdio.h>         /* fopen/fwrite/fflush/fclose, size_t              */

#include "gba_save.h"      /* the gpSP-side half. TYPEDEF-FREE BY DESIGN, which
                              is what makes it includable from this side too.  */
#include "gba_savefile.h"

/* ------------------------------------------------------------ module state --
 *
 * All .bss, all fixed size, well under 64 bytes in total. Published through
 * gba_savefile_read() so a failure path still carries its numbers back. */
static unsigned int sf_latch     = 0;
static int          sf_status    = GBA_SAVEFILE_OK;
static int          sf_begin_rc  = 0;
static int          sf_mkdir_rc  = 0;
static int          sf_end_rc    = 0;
static unsigned int sf_sav_bytes = 0;
static unsigned int sf_hdr_bytes = 0;
static unsigned int sf_payfnv    = 0;
static unsigned int sf_chunks    = 0;

/* --------------------------------------------------------------- FNV-1a 32 --
 *
 * THE SAME SEED AND PRIME AS EVERYWHERE ELSE IN THIS PROJECT --
 * adapters/gba/gba_save.c, adapters/gba/gba_rom.c, adapters/gba/gba_m11save.c,
 * apps/m12gpsp/main.c and tools/upload.py all use 2166136261 / 16777619.
 *
 * IT IS RESTATED HERE RATHER THAN SHARED, AND THAT IS THE TYPE BOUNDARY DOING
 * ITS JOB. gba_save.c's gs_fnv() is `static`, and exporting it would put a
 * function symbol on a boundary that is deliberately limited to scalars and
 * byte pointers. Six lines duplicated is cheaper than widening that contract,
 * and tools/gba_save_equiv.c asserts the two agree on the same input.
 *
 * ACCUMULATED OVER THE BYTES THIS FILE ACTUALLY WROTE rather than re-measured
 * from gamepak_backup[] afterwards. That is the whole point: the figure stored
 * in the .hdr then describes THE FILE, not a second reading of a region that
 * could in principle have moved in between. */
#define SF_FNV_OFFSET 2166136261u
#define SF_FNV_PRIME    16777619u

static unsigned int sf_fnv_add(unsigned int h, const unsigned char *p,
                               unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n; i++) {
        h ^= (unsigned int)p[i];
        h *= SF_FNV_PRIME;
    }

    return h;
}

/* ---------------------------------------------------------- the .sav writer --
 *
 * RAW AND HEADERLESS -- region bytes of the cartridge's save memory and nothing
 * else -- so any other GBA emulator can read the file. All metadata lives in
 * the sidecar .hdr.
 *
 * STREAMED IN 4 KB CHUNKS THROUGH A STACK BUFFER. A FLASH128 cartridge is
 * 131,072 bytes; staging it whole would cost 128 KB of .bss for a milestone
 * whose entire remaining headroom is measured against a 2 MB tripwire
 * (tools/check_forbidden.py:165). 4096 matches runtime/shim.c's WBUF_SIZE, so
 * each staged chunk fills the shim's buffer exactly once.
 *
 * "wb" IS DELIBERATE AND IS SETTLED. runtime/libc/stdio.h:43-46 records that
 * CREATING a file inside savedata may be refused from the game sandbox -- but
 * M12A PROVED ON HARDWARE that creation inside an open read-write window
 * succeeds (its status -293 never fired). This is that proven path. */
static int savefile_write_sav(const char *name, unsigned int region)
{
    unsigned char buf[GBA_SAVEFILE_CHUNK];
    unsigned int  off = 0;
    FILE *f;
    int fr, cr;

    f = fopen(name, "wb");
    if (!f)
        return GBA_SAVEFILE_EOPEN;

    sf_latch |= GBA_SAVEFILE_L_OPEN;

    sf_payfnv    = SF_FNV_OFFSET;
    sf_sav_bytes = 0;
    sf_chunks    = 0;

    while (off < region) {
        unsigned int got = gba_save_chunk(buf, off, GBA_SAVEFILE_CHUNK);
        size_t n;

        /* CANNOT HAPPEN while off < region -- gba_save_chunk() only returns 0
         * once off reaches the region size. Breaking rather than looping is
         * what makes that an assertion instead of a hang: the length check
         * after the loop turns it into a reported EWRITE. */
        if (got == 0)
            break;

        n = fwrite(buf, 1, (size_t)got, f);
        sf_chunks++;

        if (n != (size_t)got) {
            /* Flush and close even on the error path. The FILE slots are a
             * fixed static pool in the shim; leaking one would cost the .hdr
             * write below its handle and turn one reported failure into two. */
            (void)fflush(f);
            (void)fclose(f);
            return GBA_SAVEFILE_EWRITE;
        }

        sf_payfnv     = sf_fnv_add(sf_payfnv, buf, got);
        sf_sav_bytes += got;
        off          += got;
    }

    /* ***** fflush() BEFORE fclose(), AND ITS RESULT IS THE ONLY HONEST WRITE
     * STATUS THIS SHIM CAN PRODUCE. *****
     *
     * fwrite() returning the full count means the bytes were COPIED INTO A
     * BUFFER; it says nothing about the file. The syscall happens inside
     * fclose() -> wbuf_flush(), and fclose() DISCARDS that return value and
     * returns 0 unconditionally. On a container that is still effectively
     * read-only -- exactly what a silently-failed read-write mount produces --
     * the write fails with EROFS while fwrite() and fclose() BOTH REPORT
     * SUCCESS. fflush() returns wbuf_flush() directly, so this call is what
     * turns that silent failure into a reported one. Found in M12A; not being
     * reintroduced. */
    fr = fflush(f);
    cr = fclose(f);

    if (fr != 0 || cr != 0)
        return GBA_SAVEFILE_EWRITE;

    /* THE LENGTH IS CHECKED AGAINST WHAT WAS ASKED FOR, not against what the
     * loop believed it wrote. A short .sav beside a valid .hdr is the one
     * outcome M12C could not detect, because the header would describe bytes
     * that are not there. */
    if (sf_sav_bytes != region)
        return GBA_SAVEFILE_EWRITE;

    sf_latch |= GBA_SAVEFILE_L_WROTE;
    return GBA_SAVEFILE_OK;
}

/* ---------------------------------------------------------- the .hdr writer --
 *
 * WRITTEN SECOND, ALWAYS, AND THAT ORDERING IS THE VALIDITY RULE. The .hdr is
 * the commit marker: its presence beside a .sav asserts that the .sav is
 * complete. Writing it first would mean an interrupted run left a header
 * describing bytes that were never written.
 *
 * The payload FNV passed to gba_save_header_build() is the one ACCUMULATED
 * OVER THE BYTES ACTUALLY WRITTEN above, so the header's hash provably
 * describes the file beside it rather than a second measurement.
 *
 * ---- THE ONE HAZARD THIS ORDERING DOES NOT REMOVE, STATED PLAINLY ----
 *
 * If a PREVIOUS run committed a good .sav and .hdr, and THIS run overwrites the
 * .sav but then fails before rewriting the .hdr, the container is left holding
 * a STALE HEADER BESIDE NEW, PARTIAL BYTES.
 *
 * IT CANNOT BE PREVENTED HERE. The window must be closed on every path --
 * plat_savedata_end_write() is what restores the read-only mount, and skipping
 * it leaves the console unable to close the game -- and that unmount is also
 * what commits whatever is on disk at that moment. The shim exposes no unlink,
 * so the stale header cannot be removed first either.
 *
 * SO IT IS MADE DETECTABLE INSTEAD OF PREVENTABLE, and that is what the payload
 * FNV in the header is for. A stale header carries the PREVIOUS run's payload
 * hash, which will not match the bytes now in the .sav, so a reader that checks
 * the hash before trusting the file rejects the pair. That check belongs to the
 * restore path -- M12C -- which is precisely why the hash is recorded now, by
 * the milestone that cannot yet use it.
 *
 * THE FIXTURE ALSO REPORTS THE WRITE-PATH LATCH on every failure, so an
 * operator who sees GBA_SAVEFILE_L_WROTE set without GBA_SAVEFILE_L_HDR knows
 * this exact case occurred and that the save file should be treated as
 * suspect. */
static int savefile_write_hdr(const char *name, unsigned int payfnv)
{
    unsigned char hdr[GBA_SAVE_HDR_BYTES];
    FILE *f;
    size_t n;
    int fr, cr;
    unsigned int built;

    built = gba_save_header_build(hdr, (unsigned int)sizeof hdr, payfnv);
    if (built != GBA_SAVE_HDR_BYTES)
        return GBA_SAVEFILE_EHDR;

    f = fopen(name, "wb");
    if (!f)
        return GBA_SAVEFILE_EHDR;

    n  = fwrite(hdr, 1, (size_t)GBA_SAVE_HDR_BYTES, f);
    fr = fflush(f);              /* see the note in savefile_write_sav() */
    cr = fclose(f);

    if (n != (size_t)GBA_SAVE_HDR_BYTES || fr != 0 || cr != 0)
        return GBA_SAVEFILE_EHDR;

    sf_hdr_bytes = GBA_SAVE_HDR_BYTES;
    sf_latch |= GBA_SAVEFILE_L_HDR;
    return GBA_SAVEFILE_OK;
}

/* ------------------------------------------------ everything INSIDE the window
 *
 * THIS FUNCTION DOES NOT OWN THE WINDOW and must never try to close it. It may
 * return from anywhere; gba_savefile_commit() guarantees the commit runs. That
 * is the same structural property apps/m12gpsp/main.c relies on, and it is why
 * the ladder cannot be broken by a later edit adding one more failure case. */
static int savefile_inside(const char *sav, const char *hdr,
                           unsigned int region)
{
    int rc;

    /* /savedata0/gba ALREADY EXISTS -- M12A created it on hardware. It is
     * created again anyway: plat_savedata_mkdir() reports an existing directory
     * as success (runtime/savedata.h:136-138), so this costs nothing and stops
     * M12B depending on M12A having been run against this particular
     * container. */
    sf_mkdir_rc = plat_savedata_mkdir(GBA_SAVE_DIR);
    if (sf_mkdir_rc != PLAT_SD_OK)
        return GBA_SAVEFILE_EMKDIR;

    sf_latch |= GBA_SAVEFILE_L_MKDIR;

    rc = savefile_write_sav(sav, region);
    if (rc != GBA_SAVEFILE_OK)
        return rc;

    return savefile_write_hdr(hdr, sf_payfnv);
}

/* ============================== THE ENTRY POINT ========================== */

int gba_savefile_commit(void)
{
    char sav[GBA_SAVE_NAME_MAX];
    char hdr[GBA_SAVE_NAME_MAX];
    unsigned int region;
    int inner;

    sf_latch     = 0;
    sf_begin_rc  = 0;
    sf_mkdir_rc  = 0;
    sf_end_rc    = 0;
    sf_sav_bytes = 0;
    sf_hdr_bytes = 0;
    sf_payfnv    = 0;
    sf_chunks    = 0;

    /* ---- THE REFUSAL LADDER, ENTIRELY OUTSIDE THE WINDOW ----------------
     *
     * EVERY ONE OF THESE OPENS NO WINDOW AT ALL. Mounting read-write only to
     * discover there is nothing to write would put the console through a
     * mount/unmount cycle -- and a failed restore -- for no benefit whatever.
     * Refusing is also the SAFE DIRECTION in each case: a missing identity
     * would name the file wrongly, an UNKNOWN class would invent a size, and
     * an unchanged region would rewrite a good save with bytes no game
     * produced. */

    if (!plat_savedata_is_ready()) {
        sf_status = GBA_SAVEFILE_ENOTREADY;
        return sf_status;
    }

    if (!gba_save_latched()) {
        sf_status = GBA_SAVEFILE_ENOID;
        return sf_status;
    }

    /* UNKNOWN REFUSES PERSISTENCE. gpSP cannot distinguish a save-less
     * cartridge from an unexercised one -- BACKUP_UNKN collapses to BACKUP_SRAM
     * on the first backup-region access -- so an UNKNOWN class means no save
     * hardware was ever identified. Writing 32 KB of 0xFF under a guessed class
     * would create a file that looks exactly like a real save. */
    if (gba_save_class() == GBA_SAVE_UNKNOWN) {
        sf_status = GBA_SAVEFILE_EUNKNOWN;
        return sf_status;
    }

    region = gba_save_region_bytes();
    if (region == 0u || region > GBA_SAVE_BACKUP_BYTES) {
        sf_status = GBA_SAVEFILE_EUNKNOWN;
        return sf_status;
    }

    /* NOTHING CHANGED. gba_save_is_dirty() compares a full 131,072-byte hash
     * taken at the latch against one taken at exit, so this is a statement
     * about the BYTES and not about whether a write helper happened to run. */
    if (!gba_save_is_dirty()) {
        sf_status = GBA_SAVEFILE_ENOTDIRTY;
        return sf_status;
    }

    /* THE NAMES ARE BUILT BEFORE THE WINDOW OPENS. runtime/savedata.h:120 asks
     * callers to keep the window short; and a name that refuses to build is a
     * reason not to mount at all rather than a failure to discover inside. */
    if (gba_save_name_sav(sav, (unsigned int)sizeof sav) == 0u ||
        gba_save_name_hdr(hdr, (unsigned int)sizeof hdr) == 0u) {
        sf_status = GBA_SAVEFILE_ENOID;
        return sf_status;
    }

    /* ---- THE WINDOW. OPENED HERE AND CLOSED BELOW ON EVERY PATH ---------- */
    sf_begin_rc = plat_savedata_begin_write();
    if (sf_begin_rc != PLAT_SD_OK) {
        sf_status = GBA_SAVEFILE_EWINDOW;
        return sf_status;
    }

    sf_latch |= GBA_SAVEFILE_L_WINDOW;

    inner = savefile_inside(sav, hdr, region);

    /* ***** ALWAYS. THIS IS THE COMMIT AND THE READ-ONLY RESTORE. *****
     * The unmount inside plat_savedata_end_write() is what actually commits the
     * bytes; nothing reaches the container until it runs. It is unconditional
     * because savefile_inside() has no way to skip it. */
    sf_end_rc = plat_savedata_end_write();

    if (sf_end_rc == PLAT_SD_OK) {
        sf_latch |= GBA_SAVEFILE_L_COMMIT | GBA_SAVEFILE_L_RESTORE;
    } else if (sf_end_rc == PLAT_SD_ESTATTIMEOUT) {
        /* The unmount never visibly completed, but the read-only mount did come
         * back -- the console is still usable even though the commit is not
         * trustworthy. Two separate facts, latched separately. */
        sf_latch |= GBA_SAVEFILE_L_RESTORE;
    } else if (sf_end_rc == PLAT_SD_ERESTORE) {
        /* THE SERIOUS ONE: the bytes committed but the read-only mount did not
         * come back, so the game may not be closable from the PS menu. */
        sf_latch |= GBA_SAVEFILE_L_COMMIT;
    }

    /* A FAILURE INSIDE THE WINDOW IS REPORTED AHEAD OF A COMMIT FAILURE: it
     * happened first and it explains more. */
    if (inner != GBA_SAVEFILE_OK) {
        sf_status = inner;
        return sf_status;
    }

    if (sf_end_rc == PLAT_SD_ERESTORE) {
        sf_status = GBA_SAVEFILE_ERESTORE;
        return sf_status;
    }

    if (sf_end_rc != PLAT_SD_OK) {
        sf_status = GBA_SAVEFILE_ECOMMIT;
        return sf_status;
    }

    sf_status = GBA_SAVEFILE_OK;
    return sf_status;
}

/* ------------------------------------------------------- report and log --- */

unsigned int gba_savefile_latch(void)
{
    return sf_latch;
}

unsigned int gba_savefile_read(unsigned int sel)
{
    switch (sel) {
        case GBA_SAVEFILE_RD_LATCH:     return sf_latch;
        case GBA_SAVEFILE_RD_STATUS:    return (unsigned int)sf_status;
        case GBA_SAVEFILE_RD_BEGIN_RC:  return (unsigned int)sf_begin_rc;
        case GBA_SAVEFILE_RD_MKDIR_RC:  return (unsigned int)sf_mkdir_rc;
        case GBA_SAVEFILE_RD_END_RC:    return (unsigned int)sf_end_rc;
        case GBA_SAVEFILE_RD_SAV_BYTES: return sf_sav_bytes;
        case GBA_SAVEFILE_RD_HDR_BYTES: return sf_hdr_bytes;
        case GBA_SAVEFILE_RD_PAYFNV:    return sf_payfnv;
        case GBA_SAVEFILE_RD_CHUNKS:    return sf_chunks;
        default:                        return 0u;
    }
}
