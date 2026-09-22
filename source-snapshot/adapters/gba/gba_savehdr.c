/* LUAport M12C -- adapters/gba/gba_savehdr.c
 *
 * The reader for M12B's 32-byte "LGS1" sidecar header.
 * adapters/gba/gba_savehdr.h carries the contract -- the exact on-disk layout,
 * why compatibility is by FAMILY rather than by exact class, and why
 * `confidence` is decoded but never gates a restore. READ THE HEADER FIRST.
 *
 * ***** THIS FILE INCLUDES NOTHING EXCEPT ITS OWN HEADER. *****
 *
 * That is not minimalism, it is the type boundary. gpsp/common.h:100 typedefs
 * u64 as `unsigned long long` and runtime/core.h:32 as `unsigned long`; a
 * translation unit that saw both would not compile, which is why M12B's adapter
 * is two files (gba_save.h:16-47). This file depends on NEITHER side, so it is
 * safe to link from either and safe to compile with the host compiler for
 * tools/gba_restore_equiv.c.
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO
 * ---------------------------------------
 *   - does NOT open, read, close or name a file. It is handed 32 bytes that
 *     someone else read. gba_restorefile.c owns every filesystem operation.
 *   - does NOT touch gamepak_backup[] or any gpSP global. It cannot: it names
 *     no gpSP symbol and includes no gpSP header. m12c-verify asserts that
 *     gba_savehdr.o carries no gamepak_backup relocation.
 *   - does NOT include adapters/gba/gba_save.h, even though that header
 *     declares the same layout. gba_save.h is FROZEN and belongs to the WRITER.
 *     M12C must be able to read a file produced by a build of M12B it was not
 *     compiled beside, so the format is RESTATED here and the two descriptions
 *     are held together by the ROUND-TRIP case in tools/gba_restore_equiv.c --
 *     which builds a header with the frozen writer and parses it back with this
 *     reader. A shared header would make that test impossible to fail, which is
 *     the same as not having it.
 *   - does NOT allocate, and holds NO module state at all. Every function below
 *     is pure. There is no "last error" to go stale and no ordering to get
 *     wrong.
 */

#include "gba_savehdr.h"

/* --------------------------------------------------------- little endian ---
 *
 * EXPLICIT BYTE LOADS, mirroring gba_save.c:629-641's explicit byte STORES.
 * No cast through a wider pointer and no memcpy of a native integer: the
 * decode is then a property of these two functions alone and cannot be changed
 * by an alignment rule or by a different target's endianness. It also means the
 * 32 raw bytes need no alignment guarantee, which matters because they arrive
 * in a stack array filled by fread(). */
static unsigned int gh_get16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int gh_get32(const unsigned char *p)
{
    return (unsigned int)p[0]
         | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16)
         | ((unsigned int)p[3] << 24);
}

/* ---------------------------------------------------------------- FNV-1a --
 *
 * Byte-for-byte the same arithmetic as gba_save.c:106-118 and
 * gba_savefile.c:67-78. Restated rather than shared for the reason in the
 * header: those live behind the type boundary and this file is deliberately
 * outside it. tools/gba_restore_equiv.c compares this implementation against an
 * independent one written from the specification, and against the frozen
 * writer's, on the same input. */
unsigned int gba_savehdr_fnv_add(unsigned int h, const unsigned char *p,
                                 unsigned int n)
{
    unsigned int i;

    if (!p)
        return h;

    for (i = 0; i < n; i++) {
        h ^= (unsigned int)p[i];
        h *= GBA_SAVEHDR_FNV_PRIME;
    }

    return h;
}

/* ------------------------------------------------------------ the sizes ----
 *
 * COMPUTED FROM THE CLASS, NEVER BY ECHOING A gpSP SIZE CONSTANT. This is the
 * UNIT-COUNT TRAP that gba_save.h:127-138 and gba_m11save.h:141-152 both
 * document: gpsp/gba_memory.h:294-298 defines EEPROM_8_KBYTE as 16 and
 * FLASH_SIZE_128KB as 2, so a reader that echoed either would accept a SIXTEEN
 * BYTE file as a valid 8 KB EEPROM save.
 *
 * THESE MUST AGREE WITH gba_save.c:153-188 EXACTLY. If they ever drift, M12C
 * would reject every file M12B writes -- or, far worse, accept one with the
 * wrong length and restore the wrong number of bytes. tools/gba_restore_equiv.c
 * asserts the agreement class by class against the frozen writer. */
unsigned int gba_savehdr_declared_of(unsigned int cls)
{
    switch (cls) {
        case GBA_SAVEHDR_CLS_SRAM:      return 32u * 1024u;
        case GBA_SAVEHDR_CLS_FLASH64:   return 64u * 1024u;
        case GBA_SAVEHDR_CLS_FLASH128:  return 128u * 1024u;
        case GBA_SAVEHDR_CLS_EEPROM512: return 512u;
        case GBA_SAVEHDR_CLS_EEPROM8K:  return 8u * 1024u;
        default:                        return 0u;
    }
}

/* THE PERSISTED REGION. Identical to the declared size for every class except
 * EEPROM512, which occupies the full 8192-byte EEPROM window because 512 is a
 * STRICT PREFIX of it -- same base address, and the bytes above 512 on a
 * 512-byte chip are never addressable and stay 0xFF. That is M12B's SIZE POLICY
 * (gba_save.h:141-175) and M12C consumes it unchanged: an EEPROM512 header is
 * REQUIRED to declare a region of 8192, and one declaring 512 is REJECTED as
 * EGEOM because M12B could not have produced it. */
unsigned int gba_savehdr_region_of(unsigned int cls)
{
    unsigned int n;

    if (cls == GBA_SAVEHDR_CLS_EEPROM512 || cls == GBA_SAVEHDR_CLS_EEPROM8K)
        n = 8u * 1024u;
    else
        n = gba_savehdr_declared_of(cls);

    /* BOUNDED ON EVERY PATH. Nothing downstream re-derives this, and a region
     * larger than the backup array would be a restore that walked off the end
     * of gamepak_backup[]. gba_restore_apply() clamps again -- two independent
     * bounds, because one of them is always the one that gets edited away. */
    if (n > GBA_SAVEHDR_BACKUP_BYTES)
        n = GBA_SAVEHDR_BACKUP_BYTES;

    return n;
}

/* ---------------------------------------------------------- the families -- */

unsigned int gba_savehdr_family(unsigned int cls)
{
    switch (cls) {
        case GBA_SAVEHDR_CLS_SRAM:
            return GBA_SAVEHDR_FAM_SRAM;
        case GBA_SAVEHDR_CLS_FLASH64:
        case GBA_SAVEHDR_CLS_FLASH128:
            return GBA_SAVEHDR_FAM_FLASH;
        case GBA_SAVEHDR_CLS_EEPROM512:
        case GBA_SAVEHDR_CLS_EEPROM8K:
            return GBA_SAVEHDR_FAM_EEPROM;
        default:
            return GBA_SAVEHDR_FAM_NONE;
    }
}

/* SEE THE FAMILIES NOTE IN THE HEADER for why this is not exact equality, and
 * THE ROM-BOUND FORM for why a live UNKNOWN is no longer refused in one
 * specific, provable case.
 *
 * The short version: init_memory() re-defaults eeprom_size to 512 B on EVERY
 * reset (gba_memory.c:2441) and flash_bank_cnt starts at 1, so a save committed
 * as EEPROM8K or FLASH128 ALWAYS comes back as EEPROM512 or FLASH64 on a fresh
 * boot. Requiring exact equality would reject the legitimate case with a
 * message indistinguishable from corruption. */
int gba_savehdr_compatible_ev(unsigned int hdr_cls, unsigned int live_cls,
                              int rom_bound, unsigned int evidence)
{
    unsigned int hf = gba_savehdr_family(hdr_cls);
    unsigned int lf = gba_savehdr_family(live_cls);

    /* A STORED UNKNOWN IS REFUSED UNCONDITIONALLY, AND FIRST. M12B never wrote
     * one; a file claiming it describes bytes whose meaning was never
     * established, and no amount of ROM binding can supply a meaning that was
     * never recorded. This check precedes the carve-out on purpose -- being
     * bound to the right cartridge does not make an unclassified save
     * restorable. */
    if (hf == GBA_SAVEHDR_FAM_NONE)
        return 0;

    /* ---- THE LIVE UNKNOWN --------------------------------------------------
     *
     * ACCEPTED FOR EXACTLY TWO FAMILIES, AND ONLY WHEN THE CALLER HAS ALREADY
     * PROVED THE FILE BELONGS TO THE CARTRIDGE LOADED RIGHT NOW. Without
     * `rom_bound` every case below is refused, exactly as before.
     *
     *   EEPROM + live UNKNOWN -- ACCEPTED WHEN ROM-BOUND.
     *     For a cartridge like ATHE this is not gpSP failing to identify
     *     anything -- it is gpSP not having been ALLOWED to yet. Its only route
     *     to BACKUP_EEPROM is write_eeprom() (gba_memory.c:542), which the game
     *     reaches strictly after execute_arm(), and a pre-boot restore is by
     *     definition before that. EEPROM is also classified BEFORE its first
     *     byte is read, because its traffic arrives by DMA and never touches
     *     read_backup() at all.
     *
     *   SRAM + live UNKNOWN -- ACCEPTED WHEN ROM-BOUND.
     *     NOT by analogy to EEPROM. By a stronger argument: SRAM IS THE
     *     COLLAPSE TARGET. read_backup() resolves UNKN to BACKUP_SRAM at
     *     gba_memory.c:465 and serves the byte through the SRAM path at :468 --
     *     classify and consume IN THE SAME CALL. An SRAM header under a live
     *     UNKNOWN is therefore not a mismatch, it is the PREDICTED outcome, and
     *     the very first backup read the game performs hands back the restored
     *     bytes. Refusing it refused every SRAM save on every undetected
     *     cartridge FOREVER -- which is what the Metroid Fusion hardware run
     *     demonstrated: a 32,768-byte save, written by this very build and
     *     exactly ROM-matched, came back CLASS INCOMPATIBLE on every relaunch.
     *
     *   FLASH + live UNKNOWN -- REFUSED UNLESS THE CALLER SUPPLIES THE ONE
     *     PIECE OF PRE-BOOT EVIDENCE THAT RESOLVES IT.
     *     The objection below is REAL and is NOT waived: UNKN does not collapse
     *     to BACKUP_FLASH, it collapses to BACKUP_SRAM (gba_memory.c:465), so a
     *     restored flash image would be served through the SRAM path at :468
     *     with no bank mapping and no flash command state until the game issues
     *     a real flash command sequence (write_backup(), :1144-1148). That is
     *     why a BARE flash header under a live UNKNOWN is STILL REFUSED here,
     *     and why gba_savehdr_compatible_bound() -- which supplies NO evidence
     *     -- refuses it exactly as it always did.
     *
     *     WHAT CHANGED IS THAT THERE IS NOW SOMETHING TO WEIGH. The refusal
     *     above treated a live UNKNOWN as "gpSP knows nothing about this
     *     cartridge". For a FLASH128 title present in gpSP's own database that
     *     is FALSE: gba_over.h's FLAGS_FLASH_128KB handler (gba_memory.c:1727)
     *     sets flash_bank_cnt AND the Sanyo device id and then falls through
     *     WITHOUT SETTING A TYPE -- only FLAGS_EEPROM sets one (:1740). gpSP
     *     has therefore ALREADY ASSERTED a 128 KB flash part; it merely failed
     *     to record the fact in backup_type. GBA_SAVEHDR_EV_DB_FLASH128 is that
     *     assertion, observed read-only by gba_restore_db_flash128(), and it is
     *     the same KIND of database claim that FLAGS_EEPROM is already trusted
     *     for.
     *
     *     HARDWARE EVIDENCE, AND IT IS THE THIRD TIME THIS EXACT DEFECT HAS
     *     BEEN PAID FOR. Super Mario Advance 4 (AX4E, present in gba_over.h
     *     with FLAGS_FLASH_128KB) committed 131,072 bytes as FLASH128 through
     *     this very build and was then refused CLASS INCOMPATIBLE on the next
     *     launch, losing the save -- the identical symptom Tony Hawk showed for
     *     EEPROM and Metroid Fusion showed for SRAM.
     *
     *     THE WIDENING IS DELIBERATELY NARROWER THAN THE FAMILY. It is granted
     *     ONLY to CLS_FLASH128, never to the FLASH family as a whole: the
     *     evidence is specifically an assertion of a 128 KB part, and a FLASH64
     *     header under that evidence is a DIFFERENT claim than the database
     *     made. FLASH64 + EV_DB_FLASH128 is therefore REFUSED IN EVERY
     *     CONFIGURATION, and the M16-2 cell below does not reach it either.
     *
     *   FLASH64 + live UNKNOWN -- M16-2, AND ONLY UNDER SESSION REUSE.
     *     THE FOURTH INSTANCE OF ONE DEFECT. MOTHER 3 (A3UJ) and THE SIMS 2
     *     (B46E) committed 65,536 bytes through this build and were refused
     *     CLASS INCOMPATIBLE on the next launch -- the identical symptom Tony
     *     Hawk showed for EEPROM, Metroid Fusion for SRAM and Super Mario
     *     Advance 4 for FLASH128. Neither cartridge can ever carry database
     *     evidence: FLAGS_FLASH_64KB DOES NOT EXIST IN gba_over.h AT ALL.
     *
     *     ##### THE KEY IS AN ELIGIBILITY OBSERVATION, NOT EVIDENCE ABOUT THE
     *     ##### CARTRIDGE. GBA_SAVEHDR_EV_F64_ACTIVATE reports only that
     *     backup_type is UNKN and flash_bank_cnt is at its 64 KB value -- which
     *     is ALSO gpSP's default for ANY unclassified cartridge and so proves
     *     nothing about the part. It proves gpSP has not already classified this
     *     cartridge as something ELSE. THE AUTHORITY FOR THE CLASS IS THE
     *     VALIDATED ROM-BOUND LGS1 FLASH64 HEADER; this bit only establishes
     *     that supplying that class now contradicts no observation gpSP made.
     *
     *     WHY IT IS COMPILED ONLY FOR M16. Admitting this header obliges the
     *     caller to ACTIVATE backup_type afterwards (gba_restore_flash64), or
     *     read_backup() would collapse the live type to BACKUP_SRAM at
     *     gba_memory.c:465 and the next commit would write a 32 KB SRAM-classed
     *     file over a 64 KB save. That activation is a WRITE to a gpSP global,
     *     which no frozen milestone may perform -- so the cell lives behind
     *     #ifdef LUAPORT_SESSION_REUSE and M13C's preprocessed source does not
     *     contain it. gba_savehdr_compatible_bound() supplies NO evidence, so it
     *     refuses FLASH64 in BOTH configurations exactly as it always did. */
    if (lf == GBA_SAVEHDR_FAM_NONE) {
        if (!rom_bound)
            return 0;

        if (hf == GBA_SAVEHDR_FAM_EEPROM || hf == GBA_SAVEHDR_FAM_SRAM)
            return 1;

        /* ***** THE ONE NEW CELL. ***** Note the THREE simultaneous
         * requirements: the cartridge binding, the exact class, and the
         * database evidence. Drop any one of them and this returns 0. */
        if (hf == GBA_SAVEHDR_FAM_FLASH &&
            hdr_cls == GBA_SAVEHDR_CLS_FLASH128 &&
            (evidence & GBA_SAVEHDR_EV_DB_FLASH128) != 0u)
            return 1;

#ifdef LUAPORT_SESSION_REUSE
        /* ***** THE M16-2 CELL -- SESSION REUSE ONLY. ***** The SAME THREE
         * simultaneous requirements as the cell above, against a different class
         * and a different bit: the cartridge binding, the exact class FLASH64,
         * and the caller's eligibility observation. Drop any one and this
         * returns 0.
         *
         * THE TWO CELLS CANNOT COVER FOR ONE ANOTHER. EV_DB_FLASH128 admits
         * CLS_FLASH128 alone and EV_F64_ACTIVATE admits CLS_FLASH64 alone, so a
         * caller offering one bit can never unlock the other's class -- which is
         * what tools/gba_restore_equiv.c pins at [4c] from both directions. */
        if (hf == GBA_SAVEHDR_FAM_FLASH &&
            hdr_cls == GBA_SAVEHDR_CLS_FLASH64 &&
            (evidence & GBA_SAVEHDR_EV_F64_ACTIVATE) != 0u)
            return 1;
#endif /* LUAPORT_SESSION_REUSE */

        return 0;
    }

    /* ---- BOTH FAMILIES ARE KNOWN ------------------------------------------
     *
     * NEITHER `rom_bound` NOR `evidence` IS CONSULTED HERE. A save bound to the
     * right cartridge is still refused when the families genuinely disagree: an
     * EEPROM save offered to a live SRAM chip is incoherent no matter whose
     * cartridge it came from, and no amount of database evidence about a flash
     * part changes that. Both carve-outs live ENTIRELY inside the live-UNKNOWN
     * branch above, so they can only ever ADD that one case and can never relax
     * a real mismatch. tools/gba_restore_equiv.c asserts this exhaustively over
     * every (header, live) pair whose live class is a real family. */
    return (hf == lf) ? 1 : 0;
}

/* THE ROM-BOUND FORM, BEHAVIOURALLY FROZEN IN ITS TURN. Delegating with NO
 * EVIDENCE makes it identical to what it computed before the FLASH128 cell
 * existed, line for line: with `evidence` empty the new FLASH branch cannot
 * fire, so a FLASH header under a live UNKNOWN returns 0 exactly as it always
 * did -- INCLUDING the FLASH128 case that tools/gba_restore_equiv.c pins.
 *
 * ***** THAT IS WHY THE WIDENING IS A NEW ENTRY POINT AND NOT AN EDIT TO THIS
 * ONE. ***** A caller who cannot observe gpSP -- the host harness, any future
 * offline validator, anything outside the adapter -- keeps the strict rule by
 * DEFAULT rather than by remembering to ask for it. Only gba_restorefile.c,
 * which has the live emulator in front of it, can supply evidence at all. */
int gba_savehdr_compatible_bound(unsigned int hdr_cls, unsigned int live_cls,
                                 int rom_bound)
{
    return gba_savehdr_compatible_ev(hdr_cls, live_cls, rom_bound,
                                     GBA_SAVEHDR_EV_NONE);
}

/* THE ORIGINAL API, BEHAVIOURALLY FROZEN. Delegating with rom_bound = 0 makes
 * it identical to what it computed before the carve-out existed, line for line:
 * with rom_bound false the live-UNKNOWN branch returns 0, which is exactly the
 * old unconditional refusal. Callers that cannot vouch for the ROM -- and every
 * existing harness case -- keep the strict rule. */
int gba_savehdr_compatible(unsigned int hdr_cls, unsigned int live_cls)
{
    return gba_savehdr_compatible_bound(hdr_cls, live_cls, 0);
}

/* ------------------------------------------------------------- the parse -- */

static void gh_zero(struct gba_savehdr *out)
{
    if (!out)
        return;

    out->version    = 0u;
    out->cls        = 0u;
    out->region     = 0u;
    out->declared   = 0u;
    out->rom_fnv    = 0u;
    out->pay_fnv    = 0u;
    out->confidence = 0u;
}

int gba_savehdr_parse(const unsigned char *raw, unsigned int n,
                      struct gba_savehdr *out)
{
    unsigned int i;
    unsigned int cls, region, declared;

    /* ZEROED FIRST, ON EVERY PATH. A caller that ignores the return value must
     * not be able to read a half-populated header whose surviving fields look
     * entirely plausible. */
    gh_zero(out);

    if (!raw || !out)
        return GBA_SAVEHDR_ESIZE;

    /* EXACTLY 32 BYTES. Not "at least": a short header means the writer was
     * interrupted, and a long one means the file is not what it claims to be.
     * The caller detects the long case by attempting a 33rd byte and passing
     * the total it managed to read. */
    if (n != GBA_SAVEHDR_BYTES)
        return GBA_SAVEHDR_ESIZE;

    if (raw[GBA_SAVEHDR_O_MAGIC + 0] != (unsigned char)GBA_SAVEHDR_MAGIC_0 ||
        raw[GBA_SAVEHDR_O_MAGIC + 1] != (unsigned char)GBA_SAVEHDR_MAGIC_1 ||
        raw[GBA_SAVEHDR_O_MAGIC + 2] != (unsigned char)GBA_SAVEHDR_MAGIC_2 ||
        raw[GBA_SAVEHDR_O_MAGIC + 3] != (unsigned char)GBA_SAVEHDR_MAGIC_3)
        return GBA_SAVEHDR_EMAGIC;

    /* EXACTLY VERSION 1, NOT ">= 1". There is one format and M12C knows it. A
     * future version 2 would differ in ways this reader cannot guess, and
     * "reading it anyway" is how a restore silently writes the wrong bytes. A
     * version 0 is equally refused -- it is what a zero-filled file looks like
     * once someone gets the magic right. */
    if (gh_get16(&raw[GBA_SAVEHDR_O_VERSION]) != GBA_SAVEHDR_VERSION)
        return GBA_SAVEHDR_EVERSION;

    /* THE SIX RESERVED BYTES MUST ALL BE ZERO. gba_save.c:659-660 zeroes the
     * whole 32-byte buffer before filling it and never writes 26..31, so a
     * non-zero byte there was not produced by M12B. It is cheap, it is exact,
     * and it catches a whole class of "nearly right" files that a magic check
     * alone would wave through. */
    for (i = 0; i < GBA_SAVEHDR_RESERVED_N; i++)
        if (raw[GBA_SAVEHDR_O_RESERVED + i] != 0u)
            return GBA_SAVEHDR_ERESERVED;

    cls = gh_get16(&raw[GBA_SAVEHDR_O_CLASS]);

    /* UNKNOWN (0) AND ANYTHING ABOVE 5 ARE BOTH REJECTED HERE. UNKNOWN is not
     * an out-of-range value -- it is a legitimate class M12B refused to write
     * and M12C refuses to read. */
    if (cls == GBA_SAVEHDR_CLS_UNKNOWN || cls > GBA_SAVEHDR_CLS_MAX)
        return GBA_SAVEHDR_ECLASS;

    region   = gh_get32(&raw[GBA_SAVEHDR_O_REGION]);
    declared = gh_get32(&raw[GBA_SAVEHDR_O_DECLARED]);

    /* ***** THE GEOMETRY IS CHECKED AGAINST THE CLASS, NOT MERELY AGAINST A
     * BOUND. ***** The header does not get to choose its own sizes: for a given
     * class M12B could only ever have written one (region, declared) pair, so
     * anything else means the file was not written by M12B or was damaged in a
     * way that left the magic intact.
     *
     * This is what rejects an EEPROM512 header claiming a 512-byte region --
     * which is exactly what a well-meaning "fix" to the size policy would
     * produce, and which would restore 512 bytes into a window the game
     * addresses 8192 bytes of. */
    if (region != gba_savehdr_region_of(cls))
        return GBA_SAVEHDR_EGEOM;

    if (declared != gba_savehdr_declared_of(cls))
        return GBA_SAVEHDR_EGEOM;

    /* REDUNDANT WITH gba_savehdr_region_of()'s OWN CLAMP, AND KEPT ANYWAY. The
     * clamp is inside a function a later edit could change; this is the
     * statement of the invariant at the point it matters. */
    if (region == 0u || region > GBA_SAVEHDR_BACKUP_BYTES)
        return GBA_SAVEHDR_EGEOM;

    out->version    = GBA_SAVEHDR_VERSION;
    out->cls        = cls;
    out->region     = region;
    out->declared   = declared;
    out->rom_fnv    = gh_get32(&raw[GBA_SAVEHDR_O_ROMFNV]);
    out->pay_fnv    = gh_get32(&raw[GBA_SAVEHDR_O_PAYFNV]);

    /* DECODED, REPORTED, AND NEVER ACTED ON. See the header: a PROVISIONAL
     * EEPROM512 is Tony Hawk 2's actual hardware result and is the NORMAL case.
     * Gating on it would reject the save this milestone exists to restore.
     * Values outside 0..2 are not an error either -- the field is informational,
     * so an unexpected value is reported as such rather than failing a file
     * whose every load-bearing field is correct. */
    out->confidence = gh_get16(&raw[GBA_SAVEHDR_O_CONF]);

    return GBA_SAVEHDR_OK;
}

const char *gba_savehdr_name(int rc)
{
    switch (rc) {
        case GBA_SAVEHDR_OK:        return "VALID";
        case GBA_SAVEHDR_ESIZE:     return "NOT 32 BYTES";
        case GBA_SAVEHDR_EMAGIC:    return "BAD MAGIC";
        case GBA_SAVEHDR_EVERSION:  return "UNSUPPORTED VERSION";
        case GBA_SAVEHDR_ERESERVED: return "RESERVED BYTES NOT ZERO";
        case GBA_SAVEHDR_ECLASS:    return "CLASS UNKNOWN OR OUT OF RANGE";
        case GBA_SAVEHDR_EGEOM:     return "GEOMETRY WRONG FOR THE CLASS";
        default:                    return "UNKNOWN HEADER RESULT";
    }
}
