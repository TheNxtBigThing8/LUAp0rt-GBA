/* LUAport M12C -- adapters/gba/gba_restorefile.c
 *
 * The runtime-facing half of GBA cartridge save restore.
 * adapters/gba/gba_restorefile.h carries the contract -- why M12C links no
 * savedata layer at all, why the restore is TWO PASSES rather than a 128 KB
 * staging buffer, and what every rejection code means. READ IT FIRST, and read
 * gba_savehdr.h and gba_restore.h before that.
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO
 * ---------------------------------------
 *   - does NOT include runtime/savedata.h and calls NONE of
 *     plat_savedata_init, plat_savedata_begin_write, plat_savedata_end_write or
 *     plat_savedata_mkdir. /savedata0 is ALREADY mounted read-only by the
 *     sandbox (runtime/savedata.h:14-19), which is all a reader needs -- proved
 *     on hardware by gba_rom.c:118 (M4-M12B) and by M12A's read-only STAGE 1.
 *     savedata.o is not even in the M12C link line.
 *   - does NOT open any file for writing. Both fopen() calls below are "rb".
 *     There is no writeback, no repair, no rewrite, no .tmp and no unlink in
 *     this milestone: the M12B artifact must survive the proof untouched.
 *   - does NOT include a single gpSP header, and so cannot name gamepak_backup,
 *     backup_type, eeprom_size or flash_bank_cnt even by accident. Every
 *     restored byte crosses into the emulator through gba_restore_apply().
 *     m12c-verify asserts this object carries no gamepak_backup relocation.
 *   - does NOT decide the format. gba_savehdr.c owns every field rule; this
 *     file only reads bytes and reports which rule rejected them.
 *   - does NOT allocate. The staging buffer is 4096 bytes of STACK. There is no
 *     copy of the save anywhere in M12C.
 *   - does NOT mutate anything before the whole file has been proved. In
 *     VALIDATE mode it never mutates at all -- and that is enforced BELOW it,
 *     by gba_restore_apply() refusing while disarmed, not merely by this file
 *     choosing not to call it.
 */

#include <stdio.h>            /* fopen/fread/fclose, size_t                   */

#include "gba_save.h"         /* FROZEN M12B: the identity, the ROM hash and
                                 the two filename builders. TYPEDEF-FREE BY
                                 DESIGN, which is what makes it includable from
                                 this side of the boundary too.               */
#include "gba_savehdr.h"
#include "gba_restore.h"
#include "gba_restorefile.h"

/* ------------------------------------------------------------ module state --
 *
 * All .bss, all fixed size, well under 96 bytes in total. Published through
 * gba_restorefile_read() so a rejection still carries its numbers back to the
 * screen and the log. */
static unsigned int rf_latch      = 0;
static int          rf_status     = GBA_RESTOREFILE_OK;
static unsigned int rf_mode       = GBA_RESTOREFILE_MODE_VALIDATE;

static unsigned int rf_cls        = 0;
static unsigned int rf_liveclass  = 0;
static unsigned int rf_region     = 0;
static unsigned int rf_declared   = 0;
static unsigned int rf_conf       = 0;
static unsigned int rf_hdr_romfnv = 0;
static unsigned int rf_hdr_payfnv = 0;
static unsigned int rf_cur_romfnv = 0;
static unsigned int rf_calc_fnv   = 0;
static unsigned int rf_apply_fnv  = 0;
static unsigned int rf_sav_bytes  = 0;
static unsigned int rf_hdr_bytes  = 0;
static unsigned int rf_chunks     = 0;

/* 1 when the live class was UNKNOWN and the ROM-BOUND EEPROM FAMILY rule is what
 * granted eligibility. See GBA_RESTOREFILE_L_FAMTRUST. */
static unsigned int rf_famtrust   = 0;

/* 1 when gpSP's DATABASE had asserted a 128 KB flash part while leaving the type
 * UNSET at the moment the class gate ran -- the evidence, recorded whether or
 * not it ended up mattering. */
static unsigned int rf_dbflash128 = 0;

/* 1 when that evidence is what ADMITTED this save: a FLASH128 header accepted
 * under a live UNKNOWN, which no other rule can do.
 * See GBA_RESTOREFILE_L_DBFLASH. */
static unsigned int rf_dbtrust    = 0;

#ifdef LUAPORT_SESSION_REUSE
/* M16-2 ONLY. 1 when the FLASH64 rule is what admitted this save -- a validated
 * ROM-bound FLASH64 header under a live UNKNOWN on a cartridge whose emulator
 * state was ELIGIBLE. It is the note that says "if this restore completes, the
 * activation is owed", and it is consumed exactly once, AFTER PASS 2.
 *
 * ***** IT RECORDS AN OBLIGATION, NOT A PERMISSION. ***** It cannot admit
 * anything by itself: gba_savehdr_compatible_ev() has already decided, and this
 * is derived from what was actually decided rather than passed into it. */
static unsigned int rf_f64act     = 0;
#endif

static void rf_reset(void)
{
    rf_latch      = 0;
    rf_status     = GBA_RESTOREFILE_OK;
    rf_cls        = 0;
    rf_liveclass  = 0;
    rf_region     = 0;
    rf_declared   = 0;
    rf_conf       = 0;
    rf_hdr_romfnv = 0;
    rf_hdr_payfnv = 0;
    rf_cur_romfnv = 0;
    rf_calc_fnv   = 0;
    rf_apply_fnv  = 0;
    rf_sav_bytes  = 0;
    rf_hdr_bytes  = 0;
    rf_chunks     = 0;
    rf_famtrust   = 0;
    rf_dbflash128 = 0;
    rf_dbtrust    = 0;
#ifdef LUAPORT_SESSION_REUSE
    /* CLEARED WITH EVERYTHING ELSE. A stale obligation from a previous call
     * could otherwise activate on the strength of a header this one refused. */
    rf_f64act     = 0;
#endif
}

static int rf_fail(int rc)
{
    rf_status = rc;
    return rc;
}

/* Translate gba_savehdr.c's field verdict into this layer's reported code, so
 * the operator sees ONE numbering rather than two. Each header rule keeps its
 * own distinct destination -- collapsing them into a single "bad header" would
 * throw away exactly the information that tells an operator whether they are
 * looking at a foreign file, a damaged one, or a format they do not have code
 * for. */
static int rf_map_hdr(int rc)
{
    switch (rc) {
        case GBA_SAVEHDR_OK:        return GBA_RESTOREFILE_OK;
        case GBA_SAVEHDR_ESIZE:     return GBA_RESTOREFILE_EHDRSIZE;
        case GBA_SAVEHDR_EMAGIC:    return GBA_RESTOREFILE_EMAGIC;
        case GBA_SAVEHDR_EVERSION:  return GBA_RESTOREFILE_EVERSION;
        case GBA_SAVEHDR_ERESERVED: return GBA_RESTOREFILE_ERESERVED;
        case GBA_SAVEHDR_ECLASS:    return GBA_RESTOREFILE_ECLASS;
        case GBA_SAVEHDR_EGEOM:     return GBA_RESTOREFILE_EGEOM;
        default:                    return GBA_RESTOREFILE_EHDRSIZE;
    }
}

/* ---------------------------------------------------------- does it exist? --
 *
 * OPENED AND IMMEDIATELY CLOSED. The orphan classification has to know whether
 * BOTH files are present BEFORE either is parsed, so that a .hdr sitting beside
 * a missing .sav is reported as an ORPHAN rather than as whatever header error
 * its contents happen to produce.
 *
 * ONE HANDLE AT A TIME, NEVER TWO. runtime/shim.c backs FILE with a fixed
 * static pool; holding two handles across a parse would tie up a slot for no
 * reason and turn one reported failure into two if the pool were ever tight.
 * Four sequential opens cost nothing measurable and cannot exhaust anything. */
static int rf_exists(const char *name)
{
    FILE *f;

    if (!name || name[0] == '\0')
        return 0;

    f = fopen(name, "rb");
    if (!f)
        return 0;

    (void)fclose(f);
    return 1;
}

/* ------------------------------------------------------------ the .hdr read --
 *
 * READS 32 BYTES AND THEN DELIBERATELY ATTEMPTS A 33rd.
 *
 * A file that is LONGER than the header is not a valid header with junk after
 * it -- it is a different file that happens to start with the right bytes.
 * Without the extra read, a 4096-byte file whose first 32 bytes were a perfect
 * LGS1 header would validate, and M12C would then trust everything it said. The
 * read costs one syscall on a path that runs once per launch. */
static int rf_read_hdr(const char *name, struct gba_savehdr *out)
{
    unsigned char raw[GBA_SAVEHDR_BYTES];
    unsigned char extra;
    FILE  *f;
    size_t got, tail;
    int    rc;

    /* DEFENSIVE ONLY -- rf_exists() already opened this file successfully a few
     * lines earlier. Reported as EHDRSIZE ("could not read 32 bytes") rather
     * than as an orphan, because at this point the .sav IS present and calling
     * it an orphan would name the wrong problem. */
    f = fopen(name, "rb");
    if (!f)
        return GBA_RESTOREFILE_EHDRSIZE;

    rf_latch |= GBA_RESTOREFILE_L_HDROPEN;

    got  = fread(raw, 1, (size_t)GBA_SAVEHDR_BYTES, f);
    tail = fread(&extra, 1, (size_t)1, f);
    (void)fclose(f);

    rf_hdr_bytes = (unsigned int)got;

    if (got != (size_t)GBA_SAVEHDR_BYTES || tail != (size_t)0)
        return GBA_RESTOREFILE_EHDRSIZE;

    rc = gba_savehdr_parse(raw, (unsigned int)got, out);
    if (rc != GBA_SAVEHDR_OK)
        return rf_map_hdr(rc);

    rf_latch |= GBA_RESTOREFILE_L_HDRVALID;
    return GBA_RESTOREFILE_OK;
}

/* ============================== PASS 1 ===================================
 *
 * PROVE THE FILE. MUTATE NOTHING.
 *
 * Reads the whole region in 4096-byte stack chunks, folds each into a running
 * FNV-1a, and checks three things: that the file holds AT LEAST the region (a
 * short file is a truncated save), that it holds NO MORE than the region (a
 * long file is not the save the header describes), and that its hash is
 * EXACTLY the one M12B recorded.
 *
 * ***** THE PAYLOAD HASH IS WHAT CATCHES A STALE HEADER. *****
 * gba_savefile.c:181-203 states the one hazard M12B's write ordering could not
 * remove: a run that overwrites the .sav and then fails before rewriting the
 * .hdr leaves a PREVIOUS header beside NEW, PARTIAL bytes. The .hdr is the
 * commit marker, so its mere presence is not enough -- but its payload hash
 * describes the OLD bytes and cannot match the new ones. That check was written
 * into the format by the milestone that could not yet use it. This is the line
 * that uses it. */
static int rf_pass1(const char *name, unsigned int region)
{
    unsigned char buf[GBA_RESTOREFILE_CHUNK];
    unsigned char extra;
    FILE  *f;
    size_t tail;
    unsigned int off = 0;
    unsigned int h   = GBA_SAVEHDR_FNV_OFFSET;

    /* DEFENSIVE ONLY -- rf_exists() already opened this file successfully.
     * Reported as a short read (zero bytes of the region were obtained) rather
     * than as an orphan: the .hdr is present and validated by now, so "orphan"
     * would describe a situation that demonstrably does not hold. */
    f = fopen(name, "rb");
    if (!f)
        return GBA_RESTOREFILE_ESHORTREAD;

    rf_latch |= GBA_RESTOREFILE_L_SAVOPEN;

    while (off < region) {
        unsigned int want = region - off;
        size_t got;

        if (want > GBA_RESTOREFILE_CHUNK)
            want = GBA_RESTOREFILE_CHUNK;

        got = fread(buf, 1, (size_t)want, f);
        if (got == (size_t)0)
            break;                     /* end of file, or a read that failed */

        h    = gba_savehdr_fnv_add(h, buf, (unsigned int)got);
        off += (unsigned int)got;
        rf_chunks++;
    }

    /* Only meaningful once the region has been consumed; harmless otherwise. */
    tail = fread(&extra, 1, (size_t)1, f);
    (void)fclose(f);

    rf_sav_bytes = off;
    rf_calc_fnv  = h;

    /* TOO SHORT. The header promises `region` bytes and the file does not hold
     * them, so what is there is a fragment of a save, not a save. */
    if (off != region)
        return GBA_RESTOREFILE_ESHORTREAD;

    /* TOO LONG. Reported separately from "too short" because the two mean
     * different things to an operator: truncation suggests an interrupted
     * write, extra bytes suggest the wrong file entirely. */
    if (tail != (size_t)0)
        return GBA_RESTOREFILE_ESAVSIZE;

    rf_latch |= GBA_RESTOREFILE_L_SIZEOK;

    if (h != rf_hdr_payfnv)
        return GBA_RESTOREFILE_EPAYFNV;

    rf_latch |= GBA_RESTOREFILE_L_PAYOK;
    return GBA_RESTOREFILE_OK;
}

/* ============================== PASS 2 ===================================
 *
 * APPLY, RE-HASH WHILE COPYING, AND ROLL BACK ON ANY DISAGREEMENT.
 *
 * The file cannot have changed since pass 1 -- /savedata0 is mounted READ-ONLY,
 * this process is single-threaded, and nothing else holds the container
 * writable while the game runs. Pass 2 therefore re-reads the same bytes. It
 * hashes them AGAIN ANYWAY, because "cannot have changed" is an argument and a
 * hash is a measurement, and the cost of the measurement is one multiply per
 * byte on a path that runs once.
 *
 * THREE INDEPENDENT READINGS END UP AGREEING BEFORE THIS RETURNS OK:
 *   1. pass 1's hash of the FILE                         (rf_calc_fnv)
 *   2. pass 2's hash of the bytes it COPIED              (rf_apply_fnv)
 *   3. the caller's hash of the ARRAY afterwards         (gba_restore_region_fnv)
 * The third is taken from gamepak_backup[] itself, so it is the only one that
 * can catch a defect in gba_restore_apply()'s own bounds arithmetic.
 *
 * ANY FAILURE WIPES THE WHOLE ARRAY TO 0xFF. A partial save is worse than no
 * save: the game would read it as a corrupt profile and might offer to repair
 * or overwrite it. 0xFF over all 131,072 bytes is exactly the state
 * m4_rom_backup_init() produces, so the cartridge boots as if nothing had ever
 * been restored. */
static int rf_pass2(const char *name, unsigned int region)
{
    unsigned char buf[GBA_RESTOREFILE_CHUNK];
    FILE *f;
    unsigned int off = 0;
    unsigned int h   = GBA_SAVEHDR_FNV_OFFSET;
    int bad = 0;

    f = fopen(name, "rb");
    if (!f) {
        gba_restore_rollback();
        rf_latch |= GBA_RESTOREFILE_L_ROLLBACK;
        return GBA_RESTOREFILE_EAPPLY;
    }

    while (off < region) {
        unsigned int want = region - off;
        unsigned int put;
        size_t got;

        if (want > GBA_RESTOREFILE_CHUNK)
            want = GBA_RESTOREFILE_CHUNK;

        got = fread(buf, 1, (size_t)want, f);
        if (got == (size_t)0)
            break;

        /* THE ONLY WRITE INTO THE EMULATOR IN ALL OF M12C. It returns the count
         * it accepted; anything less than the chunk means its own clamps
         * disagreed with this file's arithmetic, which is a defect rather than
         * a data problem -- and is treated as fatal for exactly that reason. */
        put = gba_restore_apply(buf, off, (unsigned int)got, region);
        if (put != (unsigned int)got) {
            bad = 1;
            break;
        }

        h    = gba_savehdr_fnv_add(h, buf, (unsigned int)got);
        off += (unsigned int)got;
    }

    (void)fclose(f);

    rf_apply_fnv = h;

    if (bad || off != region || h != rf_hdr_payfnv ||
        gba_restore_applied() != region) {
        gba_restore_rollback();
        rf_latch |= GBA_RESTOREFILE_L_ROLLBACK;
        return GBA_RESTOREFILE_EAPPLY;
    }

    rf_latch |= GBA_RESTOREFILE_L_APPLIED;

    /* THE THIRD READING, TAKEN FROM THE ARRAY. Everything above measured bytes
     * on their way past; this measures where they landed. */
    if (gba_restore_region_fnv(region) != rf_hdr_payfnv) {
        gba_restore_rollback();
        rf_latch |= GBA_RESTOREFILE_L_ROLLBACK;
        return GBA_RESTOREFILE_EAPPLY;
    }

    rf_latch |= GBA_RESTOREFILE_L_VERIFIED;
    return GBA_RESTOREFILE_OK;
}

/* ============================== THE LADDER ============================== */

int gba_restorefile_with(const char *sav, const char *hdr, unsigned int mode)
{
    struct gba_savehdr h;
    int have_sav, have_hdr;
    int rc;
#ifdef LUAPORT_SESSION_REUSE
    /* M16-2 ONLY, so that the frozen configuration's preprocessed source -- and
     * therefore its object code -- is character-for-character what it was. */
    unsigned int ev;
#endif

    rf_reset();
    rf_mode = (mode == GBA_RESTOREFILE_MODE_RESTORE)
                ? GBA_RESTOREFILE_MODE_RESTORE
                : GBA_RESTOREFILE_MODE_VALIDATE;

    if (!sav || !hdr || sav[0] == '\0' || hdr[0] == '\0')
        return rf_fail(GBA_RESTOREFILE_ENOID);

    rf_latch |= GBA_RESTOREFILE_L_NAMED;

    /* ---- THE ORPHAN LADDER, BEFORE ANYTHING IS PARSED -------------------
     *
     * The four cases are distinguished because they mean completely different
     * things and only one of them is a problem:
     *
     *   NEITHER   this cartridge has never been saved by M12B. A FIRST RUN.
     *             Not an error in any sense -- the game simply boots new.
     *   .sav ONLY the bytes were written but the .hdr -- THE COMMIT MARKER --
     *             never was, so M12B did not finish. The bytes must NOT be
     *             trusted: gba_savefile.c writes the header second precisely so
     *             that its absence means "incomplete".
     *   .hdr ONLY a header describing bytes that are not there.
     *   BOTH      proceed to validation. */
    have_hdr = rf_exists(hdr);
    have_sav = rf_exists(sav);

    if (!have_hdr && !have_sav)
        return rf_fail(GBA_RESTOREFILE_ENOSAVE);
    if (!have_hdr)
        return rf_fail(GBA_RESTOREFILE_EORPHANSAV);
    if (!have_sav)
        return rf_fail(GBA_RESTOREFILE_EORPHANHDR);

    /* ---- THE HEADER ---------------------------------------------------- */
    rc = rf_read_hdr(hdr, &h);
    if (rc != GBA_RESTOREFILE_OK)
        return rf_fail(rc);

    rf_cls        = h.cls;
    rf_region     = h.region;
    rf_declared   = h.declared;
    rf_conf       = h.confidence;
    rf_hdr_romfnv = h.rom_fnv;
    rf_hdr_payfnv = h.pay_fnv;

    /* ---- IS THIS SAVE EVEN OURS? ---------------------------------------
     *
     * The filename already encodes the identity, so a mismatch here should be
     * impossible -- which is exactly why it is checked. The name is
     * <CODE>_<HASH8> and carries only 32 bits of hash in TEXT; the header
     * carries the same 32 bits in BINARY. If they ever disagree, the file was
     * renamed, hand-edited, or copied from another console -- and restoring
     * another cartridge's save into this one would produce a corrupt profile
     * that looked entirely plausible.
     *
     * gba_save_rom_hash() is M12B's OWN identity hash, bounded to
     * min(gamepak_size, 1 MB) and latched BEFORE execution. It is NOT
     * m4_rom_fnv1a()'s number (gba_save.h:293-300) and the two differ for any
     * cartridge over 1 MB, by design. Comparing against the wrong one would
     * reject every large cartridge. */
    rf_cur_romfnv = gba_save_rom_hash();
    if (rf_hdr_romfnv != rf_cur_romfnv)
        return rf_fail(GBA_RESTOREFILE_EROMID);

    rf_latch |= GBA_RESTOREFILE_L_ROMOK;

    /* ---- IS THE CARTRIDGE SHAPED LIKE THE SAVE? ------------------------
     *
     * BY FAMILY, NOT BY EXACT CLASS -- see gba_savehdr.h. init_memory()
     * re-defaults eeprom_size to 512 B on every reset (gba_memory.c:2441) and
     * flash_bank_cnt starts at 1, so a save committed as EEPROM8K or FLASH128
     * ALWAYS presents as EEPROM512 or FLASH64 on a fresh boot. An exact-match
     * gate would reject precisely the saves the size policy exists to protect.
     *
     * ***** THE ROM-BOUND FORM IS CALLED, AND `1` IS LEGITIMATE HERE. *****
     * The literal 1 is not an assumption: the ROM FNV comparison a dozen lines
     * above has ALREADY RETURNED EROMID on failure, so reaching this line is
     * itself the proof that this save belongs to the cartridge now loaded.
     * L_ROMOK is set. Passing rom_bound = 1 therefore states a fact that has
     * been established, not a hope.
     *
     * WHAT THAT ACTUALLY BUYS is the live-UNKNOWN row: an EEPROM- or
     * SRAM-family header, and -- WITH EVIDENCE, see below -- a FLASH128 one,
     * against a live class of UNKNOWN. On a cartridge like ATHE gpSP CANNOT have
     * a save class yet: its only route to BACKUP_EEPROM is write_eeprom()
     * (gba_memory.c:542), which the game reaches strictly after execute_arm(),
     * and this runs before the first instruction. Refusing it refuses every save
     * on that cartridge forever. A genuine family mismatch is still refused
     * regardless of the ROM binding. ROM VALIDATION IS NOT WEAKENED ANYWHERE:
     * it remains mandatory, unconditional and unchanged.
     *
     * ***** THE EVIDENCE ARGUMENT -- READ-ONLY, AND GATHERED HERE BECAUSE THIS
     * IS THE ONLY PLACE THAT CAN SEE BOTH SIDES. *****
     * gba_restore_db_flash128() reports whether gpSP's OWN database asserted a
     * 128 KB flash part while leaving backup_type UNSET -- the gba_over.h
     * FLAGS_FLASH_128KB defect at gba_memory.c:1727, which sets the bank count
     * and the device id but no type. That state is reachable from exactly one
     * place, so it is an assertion by gpSP rather than an inference by M12C.
     * Without it a FLASH128 header under a live UNKNOWN is REFUSED exactly as it
     * always was. A FLASH64 header is refused by THIS bit in every
     * configuration -- the database asserted a 128 KB part and 64 KB is a
     * different claim -- and is admitted only by the SEPARATE, SESSION-REUSE-ONLY
     * observation immediately below.
     *
     * IT IS OBSERVED, NEVER SEEDED. The call reads two globals and writes none;
     * the live class below is STILL UNKNOWN after a restore this evidence
     * admitted, and stays that way until the game's own bus traffic classifies
     * the cartridge. */
    rf_liveclass  = gba_restore_live_class();
    rf_dbflash128 = gba_restore_db_flash128();

#ifdef LUAPORT_SESSION_REUSE
    /* ***** M16-2: THE SECOND OBSERVATION, AND IT IS READ-ONLY TOO. *****
     *
     * gba_restore_flash64(0) WRITES NOTHING. It reports whether the emulator is
     * ELIGIBLE for a FLASH64 activation -- backup_type still UNKN and
     * flash_bank_cnt still at its 64 KB value.
     *
     * ##### THAT IS NOT EVIDENCE THAT THE CARTRIDGE IS A 64 KB FLASH PART.
     * ##### It is ALSO gpSP's default for ANY unclassified cartridge, so read
     * alone it proves nothing about the hardware. It proves only that gpSP has
     * not already classified this cartridge as something ELSE. THE AUTHORITY FOR
     * THE CLASS IS THE HEADER -- the LGS1 magic, the version, the zero reserved
     * bytes and the geometry proved by gba_savehdr_parse() above, the ROM FNV
     * proved a dozen lines up, and the payload FNV PASS 1 is about to prove.
     * Every one of those gates is still fatal and none of them is relaxed here.
     *
     * THE TWO BITS ARE INDEPENDENT AND NEITHER COVERS FOR THE OTHER: EV_DB_FLASH128
     * admits CLS_FLASH128 alone, EV_F64_ACTIVATE admits CLS_FLASH64 alone. On a
     * cartridge carrying the 128 KB database assertion this observation is FALSE
     * by construction -- the bank count is not 64 KB -- so a FLASH64 header is
     * refused there in BOTH configurations. */
    ev = rf_dbflash128 ? GBA_SAVEHDR_EV_DB_FLASH128 : GBA_SAVEHDR_EV_NONE;

    if (gba_restore_flash64(0))
        ev |= GBA_SAVEHDR_EV_F64_ACTIVATE;

    if (!gba_savehdr_compatible_ev(rf_cls, rf_liveclass, 1, ev))
#else
    if (!gba_savehdr_compatible_ev(rf_cls, rf_liveclass, 1,
                                   rf_dbflash128
                                     ? GBA_SAVEHDR_EV_DB_FLASH128
                                     : GBA_SAVEHDR_EV_NONE))
#endif
        return rf_fail(GBA_RESTOREFILE_EINCOMPAT);

    rf_latch |= GBA_RESTOREFILE_L_CLASSOK;

    /* WHICH RULE LET IT THROUGH -- RECORDED, BECAUSE THEY ARE NOT THE SAME
     * CLAIM. A live family that matched is gpSP agreeing with the file. A live
     * UNKNOWN that was accepted anyway is M12C trusting the file's FAMILY on the
     * strength of the ROM binding, with gpSP still holding no opinion at all.
     * Reported separately so no log can imply gpSP had detected EEPROM when it
     * had not. Derived from the live class rather than passed back out of the
     * decision, so it cannot drift from what was actually decided. */
    rf_famtrust = (gba_savehdr_family(rf_liveclass) == GBA_SAVEHDR_FAM_NONE)
                    ? 1u : 0u;
    if (rf_famtrust)
        rf_latch |= GBA_RESTOREFILE_L_FAMTRUST;

    /* AND WHETHER THE DATABASE EVIDENCE IS WHAT ADMITTED IT -- A NARROWER AND
     * STRONGER STATEMENT THAN FAMTRUST ALONE.
     *
     * DERIVED, NOT ASSUMED. A FLASH-family header that passed the gate while the
     * live class was UNKNOWN could only have been admitted by the evidence cell:
     * every other route out of that branch requires an EEPROM or SRAM header.
     * So this condition is exactly "the new rule fired", computed from what was
     * actually decided rather than from what was passed in.
     *
     * ***** IT IS NOT A CLAIM THAT gpSP DETECTED FLASH. ***** It says the
     * DATABASE asserted a 128 KB part and the type was never recorded. The log
     * must be able to show that distinction at a glance, because "gpSP detected
     * flash" and "gpSP's table says flash and gpSP forgot" are very different
     * facts and only one of them is true here.
     *
     * ***** THE THIRD TERM IS M16-2's, AND IT IS WHAT KEEPS THE LOG HONEST.
     * ***** Before M16-2 the first two terms were sufficient, because a
     * FLASH-family header admitted under a live UNKNOWN could ONLY have come
     * through the EV_DB_FLASH128 cell. That is no longer true: a FLASH64 header
     * now reaches this line through the eligibility cell instead, and no
     * gba_over.h assertion exists for it -- FLAGS_FLASH_64KB is not in that
     * database at all. Without `&& rf_dbflash128` the latch would state that the
     * 128 KB database admitted this save, which would be A LIE IN THE OPERATOR'S
     * LOG about why the bytes were allowed.
     *
     * IT IS A PROVABLE NO-OP FOR THE FLASH128 PATH. A FLASH128 header admitted
     * under a live UNKNOWN REQUIRES rf_dbflash128 to have been 1 -- that is the
     * only key its cell accepts -- so the added term cannot clear a bit that was
     * previously set. It is guarded all the same, so the frozen configuration's
     * object code is not merely equivalent but identical. */
    rf_dbtrust = (rf_famtrust &&
                  gba_savehdr_family(rf_cls) == GBA_SAVEHDR_FAM_FLASH
#ifdef LUAPORT_SESSION_REUSE
                  && rf_dbflash128
#endif
                 ) ? 1u : 0u;
    if (rf_dbtrust)
        rf_latch |= GBA_RESTOREFILE_L_DBFLASH;

#ifdef LUAPORT_SESSION_REUSE
    /* ***** M16-2: RECORD THE OBLIGATION. NOTHING IS ACTIVATED YET. *****
     *
     * DERIVED FROM WHAT WAS ACTUALLY DECIDED, exactly as rf_dbtrust is. A
     * FLASH64 header that passed the gate while the live class was UNKNOWN could
     * only have been admitted by the eligibility cell -- every other route out of
     * that branch requires an EEPROM or SRAM header, and the FLASH128 cell
     * accepts CLS_FLASH128 alone. So this is precisely "the M16-2 rule fired".
     *
     * ***** THE ACTIVATION IS DELIBERATELY NOT PERFORMED HERE. ***** At this
     * point the payload has not been hashed, applied or verified. Activating now
     * would leave a cartridge classified FLASH64 with no save behind it whenever
     * a lower gate refuses -- and the next commit would write 65,536 bytes of
     * blank flash over the file on disk. The store happens once, after PASS 2. */
    rf_f64act = (rf_famtrust &&
                 rf_cls == GBA_SAVEHDR_CLS_FLASH64 &&
                 (ev & GBA_SAVEHDR_EV_F64_ACTIVATE) != 0u) ? 1u : 0u;
#endif

    /* ---- PASS 1: PROVE IT. NOTHING IS MUTATED BY ANY PATH ABOVE OR HERE. */
    rc = rf_pass1(sav, rf_region);
    if (rc != GBA_RESTOREFILE_OK)
        return rf_fail(rc);

    /* ---- VALIDATE-ONLY STOPS HERE ---------------------------------------
     *
     * Everything above ran identically to a restore. The ONLY difference
     * between the two modes is the line below, which is what makes the STAGE 0
     * versus STAGE 1 comparison an experiment with one variable. */
    if (rf_mode != GBA_RESTOREFILE_MODE_RESTORE) {
        rf_status = GBA_RESTOREFILE_OK;
        return rf_status;
    }

    /* ---- PASS 2: APPLY ------------------------------------------------- */
    rc = rf_pass2(sav, rf_region);
    if (rc != GBA_RESTOREFILE_OK)
        return rf_fail(rc);

#ifdef LUAPORT_SESSION_REUSE
    /* ---- ***** M16-2: THE ACTIVATION, AND IT IS THE LAST THING DONE *****
     *
     * ONE STORE, backup_type = BACKUP_FLASH, AND NOTHING ELSE. With
     * flash_bank_cnt already at its 64 KB value -- the very condition that made
     * this cartridge eligible -- the emulator now holds EXACTLY the state a
     * natively detected 64 KB flash cartridge holds after init_memory().
     *
     * ***** EVERY GATE HAS PASSED BEFORE THIS LINE IS REACHED. ***** The header
     * parsed, the ROM FNV matched this cartridge, the class gate admitted it, the
     * .sav was exactly one region long, PASS 1 proved the payload hash, PASS 2
     * applied all of it AND re-hashed the array from gamepak_backup[] itself
     * (L_VERIFIED). rf_pass2() returns OK on NO other path -- every failure
     * inside it rolls the array back to 0xFF and returns EAPPLY, which the check
     * immediately above turns into an early return. So a rolled-back restore can
     * never leave an activation behind.
     *
     * ***** MODE_VALIDATE NEVER GETS HERE. ***** The validate-only return sits
     * above PASS 2, so STAGE 0 remains provably passive: it cannot activate, and
     * not because it chooses not to.
     *
     * THE RE-TEST INSIDE gba_restore_flash64() IS THE FINAL SAFETY. If anything
     * had classified the cartridge between the gate and this line, the store is
     * skipped rather than overwriting a real detection with a file-derived class.
     *
     * THE RETURN IS DELIBERATELY DISCARDED. The restore SUCCEEDED -- the bytes
     * are in the array and verified -- so there is no honest failure to report
     * here, and turning a skipped activation into a rejection would throw away a
     * good save. */
    if (rf_f64act)
        (void)gba_restore_flash64(1);
#endif

    rf_status = GBA_RESTOREFILE_OK;
    return rf_status;
}

int gba_restorefile_run(unsigned int mode)
{
    char sav[GBA_SAVE_NAME_MAX];
    char hdr[GBA_SAVE_NAME_MAX];

    /* THE NAMES COME FROM THE FROZEN M12B BUILDERS. Using M12B's own
     * gba_save_name_sav()/_hdr() rather than reconstructing the string here is
     * what guarantees M12C reads exactly what M12B wrote: if the two ever
     * disagreed about the directory, the separator or the extension, the
     * restore would silently look in the wrong place and report "no save"
     * forever. They REFUSE rather than truncate (gba_save.c:444-450), so a zero
     * return is a real refusal and not a short buffer. */
    if (gba_save_name_sav(sav, (unsigned int)sizeof sav) == 0u ||
        gba_save_name_hdr(hdr, (unsigned int)sizeof hdr) == 0u) {
        rf_reset();
        return rf_fail(GBA_RESTOREFILE_ENOID);
    }

    return gba_restorefile_with(sav, hdr, mode);
}

/* ------------------------------------------------------- report and log --- */

unsigned int gba_restorefile_latch(void)
{
    return rf_latch;
}

const char *gba_restorefile_name(int rc)
{
    switch (rc) {
        case GBA_RESTOREFILE_OK:         return "VALID";
        case GBA_RESTOREFILE_ENOID:      return "NO SAVE IDENTITY";
        case GBA_RESTOREFILE_ENOSAVE:    return "NO SAVE ON DISK";
        case GBA_RESTOREFILE_EORPHANSAV: return "SAV WITHOUT HDR - NOT COMMITTED";
        case GBA_RESTOREFILE_EORPHANHDR: return "HDR WITHOUT SAV";
        case GBA_RESTOREFILE_EHDRSIZE:   return "HDR IS NOT 32 BYTES";
        case GBA_RESTOREFILE_EMAGIC:     return "BAD MAGIC";
        case GBA_RESTOREFILE_EVERSION:   return "UNSUPPORTED VERSION";
        case GBA_RESTOREFILE_ERESERVED:  return "RESERVED BYTES NOT ZERO";
        case GBA_RESTOREFILE_ECLASS:     return "CLASS UNKNOWN OR OUT OF RANGE";
        case GBA_RESTOREFILE_EGEOM:      return "GEOMETRY WRONG FOR THE CLASS";
        case GBA_RESTOREFILE_EROMID:     return "BELONGS TO ANOTHER CARTRIDGE";
        case GBA_RESTOREFILE_EINCOMPAT:  return "CLASS INCOMPATIBLE";
        case GBA_RESTOREFILE_ESAVSIZE:   return "SAV IS LONGER THAN THE REGION";
        case GBA_RESTOREFILE_EPAYFNV:    return "PAYLOAD HASH MISMATCH";
        case GBA_RESTOREFILE_ESHORTREAD: return "SAV IS SHORTER THAN THE REGION";
        case GBA_RESTOREFILE_EAPPLY:     return "APPLY FAILED - ROLLED BACK";
        default:                         return "UNKNOWN RESTORE RESULT";
    }
}

unsigned int gba_restorefile_read(unsigned int sel)
{
    switch (sel) {
        case GBA_RESTOREFILE_RD_LATCH:     return rf_latch;
        case GBA_RESTOREFILE_RD_STATUS:    return (unsigned int)rf_status;
        case GBA_RESTOREFILE_RD_MODE:      return rf_mode;
        case GBA_RESTOREFILE_RD_CLASS:     return rf_cls;
        case GBA_RESTOREFILE_RD_LIVECLASS: return rf_liveclass;
        case GBA_RESTOREFILE_RD_REGION:    return rf_region;
        case GBA_RESTOREFILE_RD_DECLARED:  return rf_declared;
        case GBA_RESTOREFILE_RD_CONF:      return rf_conf;
        case GBA_RESTOREFILE_RD_HDRROMFNV: return rf_hdr_romfnv;
        case GBA_RESTOREFILE_RD_HDRPAYFNV: return rf_hdr_payfnv;
        case GBA_RESTOREFILE_RD_CURROMFNV: return rf_cur_romfnv;
        case GBA_RESTOREFILE_RD_CALCFNV:   return rf_calc_fnv;
        case GBA_RESTOREFILE_RD_APPLYFNV:  return rf_apply_fnv;
        case GBA_RESTOREFILE_RD_SAVBYTES:  return rf_sav_bytes;
        case GBA_RESTOREFILE_RD_HDRBYTES:  return rf_hdr_bytes;
        case GBA_RESTOREFILE_RD_CHUNKS:    return rf_chunks;
        case GBA_RESTOREFILE_RD_APPLIED:   return gba_restore_applied();
        case GBA_RESTOREFILE_RD_FAMTRUST:  return rf_famtrust;
        case GBA_RESTOREFILE_RD_DBFLASH128:return rf_dbflash128;
        case GBA_RESTOREFILE_RD_DBTRUST:   return rf_dbtrust;
        default:                           return 0u;
    }
}
