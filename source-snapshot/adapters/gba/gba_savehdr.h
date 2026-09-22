/* LUAport M12C -- adapters/gba/gba_savehdr.h
 *
 * THE READER FOR M12B's 32-BYTE "LGS1" SIDECAR HEADER.
 *
 * M12B wrote the header (adapters/gba/gba_save.c:643-678). THIS FILE READS IT
 * BACK AND REFUSES IT WHEN IT IS WRONG. Nothing else in M12C is allowed to
 * decide whether a persisted file may be trusted.
 *
 * ============================================================================
 * THIS TRANSLATION UNIT INCLUDES NOTHING AT ALL, AND THAT IS THE POINT
 * ============================================================================
 * gba_savehdr.c has ZERO #include lines. It names no gpSP type, no runtime
 * type, no libc function and no global. It is pure byte arithmetic over a
 * caller-supplied buffer.
 *
 * THAT IS WHAT LETS THE SAME SOURCE BE COMPILED INTO BOTH WORLDS AND INTO THE
 * HOST HARNESS. gpsp/common.h:100 typedefs u64 as `unsigned long long` and
 * runtime/core.h:32 as `unsigned long` -- DISTINCT TYPES that cannot coexist in
 * one translation unit. M12B solved that by splitting its adapter in two
 * (gba_save.h:16-47). M12C keeps the same discipline, and this file sits
 * OUTSIDE the seam entirely: it depends on neither side, so it is safe from
 * either.
 *
 * Every declaration below uses ONLY `unsigned int`, `unsigned char` and
 * `const char *`. `unsigned int` is u32 in both worlds and `unsigned char` is
 * u8 in both (gpsp/common.h:94, runtime/core.h:35).
 *
 * ============================================================================
 * VALIDATION-FIRST IS THE WHOLE MILESTONE
 * ============================================================================
 * M12B could afford to be optimistic: the worst a bug could do was write a bad
 * file. M12C RESTORES INTO A LIVE EMULATOR, so a bug here corrupts a running
 * cartridge's save memory instead of merely failing. Every field is therefore
 * checked against a value derived independently of the file, and the file is
 * treated as HOSTILE until every check has passed.
 *
 * THE ONE FIELD THAT IS NOT A GATE IS `confidence`. M12B recorded how much its
 * class was worth at commit time (gba_save.h:107-120). A PROVISIONAL EEPROM512
 * -- which is exactly Tony Hawk 2's hardware result -- is the NORMAL case, not
 * a suspicious one, so gating on it would reject the very save this milestone
 * exists to restore. It is decoded, reported to the operator, and ignored.
 *
 * ============================================================================
 * NAMING DISCIPLINE
 * ============================================================================
 * tools/check_forbidden.py:108-111 matches the BARE SUBSTRINGS "path_",
 * "retro_", "libretro_", "vfs_", "string_is_", "filestream_", "rtime_" and
 * "encoding_" against the lowercased symbol name, and :79-85 adds "_stub" and
 * "emit_". Every symbol below is gba_savehdr_* and contains none of them.
 * NO ALLOWLIST IS WIDENED FOR M12C, exactly as none was for M12B.
 */

#ifndef LUAPORT_GBA_SAVEHDR_H
#define LUAPORT_GBA_SAVEHDR_H

/* ------------------------------------------------- THE ON-DISK LAYOUT ------
 *
 * TRANSCRIBED FROM THE SHIPPING WRITER, NOT FROM A DESIGN NOTE. Every constant
 * below was read out of adapters/gba/gba_save.h:223-239 and cross-checked
 * against the byte stores in gba_save.c:662-673. gba_save.h is FROZEN and is
 * deliberately NOT included here: M12C must be able to read a file written by
 * a build of M12B it was not compiled beside, so the format is restated rather
 * than shared. tools/gba_restore_equiv.c's ROUND-TRIP case is what proves the
 * two descriptions still agree -- it builds a header with the frozen writer and
 * parses it back with this reader.
 *
 *     off  size  field
 *       0     4  magic 'L','G','S','1'
 *       4     2  version          (little endian)
 *       6     2  save class       (GBA_SAVEHDR_CLS_*)
 *       8     4  region bytes     -- how many bytes the .sav holds
 *      12     4  declared bytes   -- what gpSP believed the chip holds
 *      16     4  ROM FNV          -- M12B's save identity hash
 *      20     4  payload FNV      -- FNV-1a over the .sav's region bytes
 *      24     2  confidence       -- RECORDED, NEVER A GATE
 *      26     6  reserved, all zero
 */
#define GBA_SAVEHDR_BYTES        32u
#define GBA_SAVEHDR_VERSION       1u

#define GBA_SAVEHDR_O_MAGIC       0u
#define GBA_SAVEHDR_O_VERSION     4u
#define GBA_SAVEHDR_O_CLASS       6u
#define GBA_SAVEHDR_O_REGION      8u
#define GBA_SAVEHDR_O_DECLARED   12u
#define GBA_SAVEHDR_O_ROMFNV     16u
#define GBA_SAVEHDR_O_PAYFNV     20u
#define GBA_SAVEHDR_O_CONF       24u
#define GBA_SAVEHDR_O_RESERVED   26u
#define GBA_SAVEHDR_RESERVED_N    6u

#define GBA_SAVEHDR_MAGIC_0 'L'
#define GBA_SAVEHDR_MAGIC_1 'G'
#define GBA_SAVEHDR_MAGIC_2 'S'
#define GBA_SAVEHDR_MAGIC_3 '1'

/* ------------------------------------------------------- the save classes --
 *
 * NUMERICALLY IDENTICAL TO GBA_SAVE_* (gba_save.h:99-105) AND TO M11's
 * M11_SAVE_*. The values are held identical on purpose across all three
 * milestones so their logs and screenshots can be read side by side. They are
 * restated rather than included for the reason given above.
 *
 * UNKNOWN IS NEVER RESTORABLE. gpSP cannot distinguish a save-less cartridge
 * from an unexercised one -- BACKUP_UNKN collapses to BACKUP_SRAM on the first
 * backup-region access (gba_memory.c:465, :1141) -- so a header claiming
 * UNKNOWN describes bytes whose meaning was never established. M12B refused to
 * WRITE one; M12C refuses to READ one. */
#define GBA_SAVEHDR_CLS_UNKNOWN   0u
#define GBA_SAVEHDR_CLS_SRAM      1u
#define GBA_SAVEHDR_CLS_FLASH64   2u
#define GBA_SAVEHDR_CLS_FLASH128  3u
#define GBA_SAVEHDR_CLS_EEPROM512 4u
#define GBA_SAVEHDR_CLS_EEPROM8K  5u
#define GBA_SAVEHDR_CLS_MAX       5u

/* gamepak_backup[] is 131,072 bytes (gba_memory.c:360). NO region may ever
 * exceed it -- gba_restore_apply() clamps again, but a header claiming more is
 * rejected here, before a single byte is read. */
#define GBA_SAVEHDR_BACKUP_BYTES  131072u

/* --------------------------------------------------------- the families ----
 *
 * WHY COMPATIBILITY IS BY FAMILY AND NOT BY EXACT CLASS -- THE SUBTLEST
 * DECISION IN M12C.
 *
 * The class recorded in the header is the one gpSP had resolved AT COMMIT TIME,
 * possibly after a mid-run measurement. The class M12C sees at restore time is
 * the one gpSP has on a FRESH BOOT, before the game has run a single
 * instruction. THOSE TWO ARE ROUTINELY DIFFERENT, and the difference is not
 * corruption:
 *
 *   eeprom_size is re-defaulted to EEPROM_512_BYTE by init_memory() on EVERY
 *   reset (gba_memory.c:2441) and is only ever MEASURED as 8 KB by the 17-unit
 *   DMA at :841-843. So a cartridge committed as EEPROM8K comes back as
 *   EEPROM512 until the game re-runs that DMA.
 *
 *   flash_bank_cnt reaches 2 only when the game issues the 0xB0 bank-switch
 *   command at :1186, so a FLASH128 save likewise comes back as FLASH64.
 *
 * AN EXACT-EQUALITY GATE WOULD THEREFORE REJECT PRECISELY THE SAVES THE SIZE
 * POLICY EXISTS TO PROTECT -- and it would do so with a message that looked
 * like corruption. Matching by family accepts the legitimate case and still
 * rejects every genuinely incoherent one (an EEPROM save offered to an SRAM
 * cartridge, or any save at all offered to a cartridge whose class is UNKNOWN).
 *
 * THE HEADER'S REGION, NOT THE LIVE CLASS, DECIDES HOW MANY BYTES ARE COPIED.
 * That is what makes the family rule safe: within a family the restore length
 * is identical for EEPROM (8192 either way), and for FLASH the header's own
 * region is authoritative over the range gpSP will address. */
#define GBA_SAVEHDR_FAM_NONE   0u
#define GBA_SAVEHDR_FAM_SRAM   1u
#define GBA_SAVEHDR_FAM_FLASH  2u
#define GBA_SAVEHDR_FAM_EEPROM 3u

/* ------------------------------------------------------- the return codes --
 *
 * 0 IS SUCCESS. Every rejection is DISTINCT so an operator reading one off a
 * screen knows which check failed without consulting a log, and so
 * apps/m12cgpsp/main.c can map each to its own -3xx status. */
#define GBA_SAVEHDR_OK          0
#define GBA_SAVEHDR_ESIZE      -1  /* not exactly GBA_SAVEHDR_BYTES bytes     */
#define GBA_SAVEHDR_EMAGIC     -2  /* the four magic bytes are not "LGS1"     */
#define GBA_SAVEHDR_EVERSION   -3  /* version is not 1                        */
#define GBA_SAVEHDR_ERESERVED  -4  /* a reserved byte is non-zero             */
#define GBA_SAVEHDR_ECLASS     -5  /* class is UNKNOWN or out of range        */
#define GBA_SAVEHDR_EGEOM      -6  /* region/declared wrong for the class     */

/* -------------------------------------------------------- the decoded form --
 *
 * PLAIN `unsigned int` FIELDS ONLY -- no typedef, no gpSP type, nothing 64-bit.
 * A struct of unsigned ints means exactly the same thing on both sides of the
 * type boundary, which is what lets gba_restorefile.c (runtime flags) and
 * tools/gba_restore_equiv.c (host compiler) share this declaration with
 * gba_restore.c (gpSP flags). */
struct gba_savehdr {
    unsigned int version;
    unsigned int cls;
    unsigned int region;
    unsigned int declared;
    unsigned int rom_fnv;
    unsigned int pay_fnv;
    unsigned int confidence;   /* RECORDED ONLY -- never gates a restore      */
};

/* =========================================================================
 *                                THE CALLS
 * ========================================================================= */

/* HOW MANY BYTES A CLASS PERSISTS, and what gpSP believes the chip holds.
 * Both are computed FROM THE CLASS and never by echoing a gpSP size constant:
 * EEPROM_8_KBYTE is 16 and FLASH_SIZE_128KB is 2 (gpsp/gba_memory.h:294-298),
 * so an implementation that echoed either would accept a SIXTEEN BYTE save file
 * for an 8 KB EEPROM. gba_save.c:153-188 computes the same two numbers the same
 * way; tools/gba_restore_equiv.c asserts they agree.
 *
 * Returns 0 for UNKNOWN and for any class out of range, which is what makes
 * UNKNOWN unrestorable rather than merely unusual. */
unsigned int gba_savehdr_region_of(unsigned int cls);
unsigned int gba_savehdr_declared_of(unsigned int cls);

/* SRAM / FLASH / EEPROM, or GBA_SAVEHDR_FAM_NONE for UNKNOWN and out of
 * range. See THE FAMILIES above. */
unsigned int gba_savehdr_family(unsigned int cls);

/* 1 when a save recorded as hdr_cls may be restored into a cartridge gpSP
 * currently classifies as live_cls.
 *
 * REJECTS A LIVE UNKNOWN UNCONDITIONALLY. If gpSP has not identified the
 * cartridge's save hardware, it has not decided how the bytes will be
 * addressed, and restoring into that is writing into a region whose meaning is
 * not yet fixed.
 *
 * ***** THIS FUNCTION'S BEHAVIOUR IS UNCHANGED AND WILL NOT CHANGE. ***** It is
 * now a thin delegation to gba_savehdr_compatible_bound(h, l, 0) -- the
 * un-bound form -- so every caller and every harness case written against it
 * keeps its exact previous verdict. See below for why the carve-out was NOT
 * folded into this signature. */
int gba_savehdr_compatible(unsigned int hdr_cls, unsigned int live_cls);

/* ============ THE ROM-BOUND FORM -- THE ONE POLICY CHANGE IN M12C ==========
 *
 * SAME RULE, PLUS EXACTLY ONE CARVE-OUT: an EEPROM-family save whose header has
 * been bound to THIS EXACT CARTRIDGE may be restored even though gpSP's live
 * class is still UNKNOWN.
 *
 * WHY THE CARVE-OUT IS NECESSARY -- THE HARDWARE FINDING THAT FORCED IT
 * --------------------------------------------------------------------
 * The rule above assumed a live UNKNOWN means "gpSP could not identify the
 * cartridge". For Tony Hawk (game code ATHE) that assumption is simply FALSE.
 * gpSP has four load-time detection routes and ATHE takes none of them:
 *
 *     backup_type_reset is set to BACKUP_UNKN (gba_memory.c:3003), the
 *     gba_over.h database has NO 'ATHE' entry, the signature scan finds no
 *     EEPROM_V/SRAM_V/FLASH*_V string, and it is not a Pokemon title. So
 *     detect_backup_subcircuit() falls off its end assigning nothing, and
 *     init_memory() then copies that UNKN into backup_type on EVERY reset
 *     (gba_memory.c:2438).
 *
 * The ONLY assignment of BACKUP_EEPROM anywhere in gpSP is inside write_eeprom()
 * (gba_memory.c:542), reached only from the game's own DMA to 0x0D. THAT IS
 * STRICTLY AFTER execute_arm(). A pre-boot restore therefore CANNOT EVER see an
 * EEPROM class on this cartridge -- not because anything failed, but because the
 * classifying event has not been allowed to happen yet.
 *
 * M12B's header is itself the proof: it records EEPROM512, and the only code
 * path that could have produced that class is the runtime one. M11 observed the
 * same event independently (SOURCE RUNTIME BUS EVIDENCE, write frames 5).
 *
 * WHY THE HEADER MAY BE TRUSTED FOR ITS FAMILY, AND ONLY ITS FAMILY
 * ----------------------------------------------------------------
 * `rom_bound` must be non-zero ONLY when the caller has ALREADY PROVED that the
 * header's ROM FNV equals the hash of the cartridge loaded RIGHT NOW. That
 * turns the class from a claim in a file into a RECORDING OF A RUNTIME
 * MEASUREMENT taken from this exact ROM. It is not filename trust and it is not
 * game-code trust: a patched or different ROM produces a different hash and is
 * rejected before this function is ever reached.
 *
 * The header's `confidence` may be PROVISIONAL -- Tony Hawk's is -- but that
 * qualifies THE SIZE WITHIN THE FAMILY (512 B is gpSP's initialiser, never a
 * measurement), NEVER THE FAMILY ITSELF. The family is exactly the part that was
 * measured, which is precisely why family-level acceptance is sound here and
 * exact-class acceptance would not be.
 *
 * ***** WHY EEPROM AND SRAM, AND WHY FLASH STAYS REFUSED *****
 * This is a FAMILY rule, not a per-game special case -- but it does not
 * generalise to all three families, and the narrowness is deliberate:
 *
 *   EEPROM  PROVABLY SAFE. EEPROM traffic never enters read_backup(); it
 *           arrives by DMA. Classification (:542) therefore STRICTLY PRECEDES
 *           the first byte read (:698), and that read uses the same addressing
 *           M12B used when it wrote the file.
 *
 *   SRAM    PROVABLY SAFE -- AND BY A STRONGER ARGUMENT THAN EEPROM'S, NOT A
 *           WEAKER ONE. SRAM IS THE COLLAPSE TARGET. read_backup() resolves
 *           UNKN to BACKUP_SRAM at :465 and serves the byte through the SRAM
 *           path at :468 -- it classifies and consumes IN THE SAME CALL. An
 *           SRAM header under a live UNKNOWN is therefore not a mismatch, it is
 *           the PREDICTED outcome: the game's very first backup read is served
 *           the restored bytes through exactly the path M12B wrote them with.
 *           EEPROM needed ROM binding to justify a class the collapse would
 *           NEVER produce; SRAM needs LESS trust than that, not more.
 *             The earlier caveat still stands and is still true -- an SRAM class
 *           MAY ITSELF BE the silent UNKN->SRAM collapse rather than a detection
 *           (gba_save.c:217-219), which is why its confidence can never rise
 *           above PROVISIONAL. It does not defeat the argument, because the
 *           restore is SYMMETRIC: bytes written through the SRAM path are read
 *           back through the SRAM path, on a cartridge whose ROM hash has
 *           already been proved identical. HARDWARE EVIDENCE: Metroid Fusion
 *           committed 32,768 bytes as SRAM and was then refused CLASS
 *           INCOMPATIBLE on every relaunch, losing the save forever.
 *
 *   FLASH   REFUSED -- probably safe, NOT PROVEN, AND THE REFUSAL IS LOAD
 *           BEARING. The same collapse that JUSTIFIES SRAM is what CONDEMNS
 *           FLASH: UNKN does not collapse to BACKUP_FLASH, it collapses to
 *           BACKUP_SRAM (:465). A restored flash image would be served through
 *           the SRAM path at :468 with no bank mapping and no flash command
 *           state; bank 0 reads identically, so it would most likely work -- but
 *           the first observation is made under the WRONG class and that is not
 *           a proof. Flash layout semantics are guaranteed only once the game
 *           issues a real flash command sequence (write_backup(), :1144-1148),
 *           which is strictly after the restore point.
 *
 * Widen FLASH ONLY with per-family hardware evidence. tools/gba_restore_equiv.c
 * pins the FLASH64 and FLASH128 refusals so a later widening has to be a
 * conscious act, exactly as it pinned SRAM's until hardware settled it.
 *
 * ***** THAT CONSCIOUS ACT HAS SINCE BEEN TAKEN TWICE, AND NEITHER TIME HERE.
 * ***** M15 widened ONE cell -- CLS_FLASH128 under a live UNKNOWN -- and M16-2
 * widened ONE more -- CLS_FLASH64 under a live UNKNOWN, and ONLY in builds
 * carrying LUAPORT_SESSION_REUSE. BOTH live in a SEPARATE ENTRY POINT,
 * gba_savehdr_compatible_ev(), and each requires the caller to supply its OWN
 * distinct key: GBA_SAVEHDR_EV_DB_FLASH128 for the first, and the
 * GBA_SAVEHDR_EV_F64_ACTIVATE eligibility observation for the second.
 *
 * THIS FUNCTION IS UNCHANGED BY EITHER: it supplies NO evidence, so every
 * sentence above still describes exactly what it computes, and ITS FLASH64 AND
 * FLASH128 REFUSALS REMAIN ABSOLUTE IN EVERY CONFIGURATION. That is precisely
 * why the widenings were new entry points rather than edits to this one -- a
 * caller who cannot observe the emulator keeps the strict rule BY DEFAULT rather
 * than by remembering to ask for it. See THE EVIDENCE-TAKING FORM below.
 *
 * ***** THIS FUNCTION AUTHORISES BYTES AND NOTHING ELSE. ***** Nothing in M12C
 * writes backup_type, backup_type_reset, eeprom_size, flash_bank_cnt or any
 * other gpSP classification global. The game must still classify EEPROM
 * naturally from its own bus traffic -- observing that it does is the entire
 * point of the experiment, and seeding it would destroy the observation.
 *
 * `rom_bound` is ignored unless the live class is UNKNOWN, so passing 1 can
 * never loosen a family-vs-family verdict. */
int gba_savehdr_compatible_bound(unsigned int hdr_cls, unsigned int live_cls,
                                 int rom_bound);

/* ========== THE EVIDENCE-TAKING FORM -- THE M15 POLICY CORRECTION ==========
 *
 * SAME RULE AS gba_savehdr_compatible_bound(), PLUS ONE FURTHER CELL that a
 * caller must EARN by supplying evidence it has actually observed.
 *
 * `evidence` is a bitmask of GBA_SAVEHDR_EV_*. Passing GBA_SAVEHDR_EV_NONE
 * reproduces gba_savehdr_compatible_bound() EXACTLY -- which is how that
 * function is now implemented, so the two cannot drift.
 *
 * ---------------------------------------------------------------------------
 * WHY A THIRD ENTRY POINT RATHER THAN A WIDER RULE IN THE SECOND
 * ---------------------------------------------------------------------------
 * The FLASH refusal documented above is SOUND and is NOT being waived. UNKN
 * collapses to BACKUP_SRAM, not BACKUP_FLASH, so a flash image restored under a
 * live UNKNOWN is served through the SRAM path with no bank mapping until the
 * game issues a real flash command sequence. A BARE flash header under a live
 * UNKNOWN therefore remains refused on every existing entry point.
 *
 * WHAT THE EVIDENCE ADDS IS A FACT THE OLD RULE NEVER LOOKED AT. The refusal
 * assumed a live UNKNOWN means "gpSP knows nothing about this cartridge". For a
 * 128 KB flash title listed in gpSP's OWN database that is false:
 *
 *     gba_over.h's FLAGS_FLASH_128KB handler (gba_memory.c:1727) sets
 *     flash_bank_cnt to FLASH_SIZE_128KB and sets the Sanyo device id, and then
 *     FALLS THROUGH WITHOUT SETTING backup_type_reset. Only FLAGS_EEPROM sets a
 *     type (:1740). The cartridge is therefore left at BACKUP_UNKN even though
 *     the database has explicitly asserted a 128 KB flash part.
 *
 * gba_restore_db_flash128() observes exactly that state, read-only, and it is
 * UNAMBIGUOUS: every OTHER pre-execution writer that drives flash_bank_cnt to 2
 * -- the FLASH1M_V signature match, the Pokemon short-circuit, the is_hack
 * override -- ALSO sets BACKUP_FLASH. adapters/gba/gba_m11save.h:90-95 recorded
 * this same gpSP defect during M11, a milestone before it cost a save.
 *
 * ***** THE HARDWARE EVIDENCE -- THE THIRD INSTANCE OF ONE DEFECT. *****
 * Super Mario Advance 4 (AX4E) committed 131,072 bytes as FLASH128 through this
 * build and was refused CLASS INCOMPATIBLE on the very next launch. That is the
 * identical failure Tony Hawk suffered for EEPROM and Metroid Fusion suffered
 * for SRAM, in the one family whose carve-out had been deliberately withheld.
 *
 * ---------------------------------------------------------------------------
 * ***** WHAT IS DELIBERATELY *NOT* WIDENED *****
 * ---------------------------------------------------------------------------
 *   FLASH64 + live UNKNOWN + EV_DB_FLASH128
 *                                   REFUSED IN EVERY CONFIGURATION. The
 *                                   database evidence asserts a 128 KB part; a
 *                                   64 KB header is a DIFFERENT claim from the
 *                                   one the database made. THE FLASH128 CELL IS
 *                                   GRANTED TO CLS_FLASH128 ALONE, never to the
 *                                   family, and no widening below changes that.
 *   FLASH64 + live UNKNOWN, no EV_F64_ACTIVATE
 *                                   REFUSED. See the M16-2 bit below: FLASH64 is
 *                                   admitted ONLY under LUAPORT_SESSION_REUSE
 *                                   and ONLY when the caller has observed the
 *                                   eligibility precondition. The frozen
 *                                   milestones do not compile that cell at all,
 *                                   so for them FLASH64 + live UNKNOWN remains
 *                                   REFUSED ALWAYS, evidence or not.
 *   FLASH128 + live UNKNOWN, no evidence
 *                                   REFUSED. No database assertion exists, so
 *                                   the original objection stands in full.
 *   FLASH128 + live UNKNOWN, evidence, NOT rom_bound
 *                                   REFUSED. The binding is still mandatory and
 *                                   is checked FIRST.
 *   a stored UNKNOWN                REFUSED, before any of this is reached.
 *   any genuine family mismatch     REFUSED. Evidence is consulted ONLY inside
 *                                   the live-UNKNOWN branch.
 *
 * ***** THIS FUNCTION AUTHORISES BYTES AND NOTHING ELSE, exactly as its two
 * predecessors do. ***** Supplying GBA_SAVEHDR_EV_DB_FLASH128 does NOT claim
 * gpSP detected flash, does not make the live class anything other than
 * UNKNOWN, and seeds nothing. gba_restorefile.c latches
 * GBA_RESTOREFILE_L_DBFLASH separately so no log can imply a detection that did
 * not happen.
 *
 * ***** THAT REMAINS TRUE OF THIS FUNCTION UNDER M16-2, BUT NOT OF THE CALLER.
 * ***** GBA_SAVEHDR_EV_F64_ACTIVATE likewise authorises BYTES here and seeds
 * nothing -- this file writes no gpSP global in any configuration. It differs
 * from its predecessor in what it OBLIGES THE CALLER TO DO AFTERWARDS: a FLASH64
 * image admitted under a live UNKNOWN must be followed by
 * gba_restore_flash64(1), or read_backup() will collapse the live type to
 * BACKUP_SRAM and the next commit will write a 32 KB SRAM-classed file over a
 * 64 KB save. gba_restorefile.c performs that store ONCE, only after the payload
 * has been applied AND re-verified. THE OBLIGATION IS THE CALLER'S; the decision
 * here is still nothing but a yes or no about bytes. */
int gba_savehdr_compatible_ev(unsigned int hdr_cls, unsigned int live_cls,
                              int rom_bound, unsigned int evidence);

/* NO EVIDENCE. The default for every caller that cannot observe the emulator,
 * and the value gba_savehdr_compatible_bound() passes. */
#define GBA_SAVEHDR_EV_NONE        0u

/* gpSP's DATABASE asserted a 128 KB FLASH part and left the save type UNSET.
 * Observed by gba_restore_db_flash128(); see the derivation above. Adding
 * another bit must be as deliberate as this one was -- the M16-2 bit below is
 * the only one that has met that bar since. */
#define GBA_SAVEHDR_EV_DB_FLASH128 (1u << 0)

/* ***** M16-2: THE FLASH64 ELIGIBILITY OBSERVATION (MOTHER 3, THE SIMS 2).
 *
 * ##### THIS BIT IS *NOT* EVIDENCE THAT THE CARTRIDGE IS A 64 KB FLASH PART,
 * ##### AND IT MUST NEVER BE DESCRIBED OR DOCUMENTED AS SUCH.
 *
 * It reports one fact about THE EMULATOR'S STATE, observed read-only by
 * gba_restore_flash64(0):
 *
 *     backup_type == BACKUP_UNKN  &&  flash_bank_cnt == FLASH_SIZE_64KB
 *
 * That conjunction is ALSO gpSP's DEFAULT for any unclassified cartridge --
 * init_memory() leaves exactly those two values, so an EEPROM cartridge, an SRAM
 * cartridge and a genuine 64 KB flash cartridge are ALL in this state before the
 * first instruction executes. READ ALONE IT PROVES NOTHING ABOUT THE HARDWARE.
 *
 * WHAT IT DOES PROVE IS ELIGIBILITY -- that gpSP has NOT already classified this
 * cartridge as something else, so a narrowly scoped FLASH64 activation cannot
 * contradict an observation the emulator actually made. It is a PRECONDITION ON
 * THE EMULATOR'S STATE, never a claim about the part.
 *
 * ***** THE AUTHORITY FOR THE CLASS IS THE HEADER, AND NOTHING ELSE. ***** A
 * fully validated, ROM-bound LGS1 FLASH64 header -- correct magic, version, zero
 * reserved bytes, legal geometry, THIS cartridge's ROM FNV and a payload FNV over
 * the exact bytes on disk -- is what decides that the restored save is FLASH64.
 * This bit only decides that supplying the class NOW cannot overwrite something
 * gpSP already knows. Drop the bit and the save is refused; drop any header gate
 * and the save is refused. BOTH are required and neither substitutes for the
 * other.
 *
 * WHY NO DATABASE ROUTE EXISTS FOR IT. FLAGS_FLASH_64KB DOES NOT EXIST IN
 * gpsp/gba_over.h AT ALL -- only FLAGS_FLASH_128KB and FLAGS_EEPROM set anything
 * -- so the EV_DB_FLASH128 argument has no 64 KB counterpart for ANY game, and
 * inventing one would be fabrication rather than observation.
 *
 * ***** WHY THE CELL IT UNLOCKS IS COMPILED ONLY UNDER LUAPORT_SESSION_REUSE.
 * ***** Unlike the FLASH128 cell, admitting this header obliges the caller to
 * ACTIVATE backup_type afterwards -- see gba_restore_flash64() -- because a
 * FLASH64 image left under a live UNKNOWN would be served through read_backup()'s
 * SRAM collapse (gba_memory.c:465) and the next commit would write a 32 KB
 * SRAM-classed file over a 64 KB save. Activation is a WRITE to a gpSP global,
 * which no frozen milestone is permitted to perform, so the rule ships in M16C
 * and in NO other image. tools/gba_restore_equiv.c is compiled BOTH ways and
 * asserts the frozen refusal and the M16-2 acceptance from one source.
 *
 * THE MACRO ITSELF IS UNCONDITIONAL -- it emits ZERO bytes and merely names a
 * bit, so M13C's preprocessed view is unchanged by its presence. Only the CELL
 * in gba_savehdr.c is guarded. */
#define GBA_SAVEHDR_EV_F64_ACTIVATE (1u << 1)

/* ------------------------------------------------------------- FNV-1a 32 ---
 *
 * THE SAME SEED AND PRIME AS EVERYWHERE ELSE IN THIS PROJECT --
 * adapters/gba/gba_save.c:103-104, adapters/gba/gba_rom.c:488,
 * adapters/gba/gba_m11save.c:143-144, apps/m12gpsp/main.c:206 and
 * tools/upload.py all use 2166136261 / 16777619. That is what lets a figure
 * printed on the console be compared directly with one computed on a PC, and it
 * is what lets M12C's recomputed payload hash be compared with the one M12B
 * stored.
 *
 * EXPOSED INCREMENTALLY BECAUSE M12C HASHES A FILE IT NEVER HOLDS WHOLE. The
 * .sav is read in 4096-byte stack chunks and folded in one chunk at a time, so
 * validating a 131,072-byte FLASH128 save costs 4 KB of stack and ZERO bytes of
 * .bss. Start from GBA_SAVEHDR_FNV_OFFSET. */
#define GBA_SAVEHDR_FNV_OFFSET 2166136261u
#define GBA_SAVEHDR_FNV_PRIME    16777619u

unsigned int gba_savehdr_fnv_add(unsigned int h, const unsigned char *p,
                                 unsigned int n);

/* PARSE AND VALIDATE 32 RAW BYTES. `n` is how many bytes were actually read
 * from the file, and it must be EXACTLY GBA_SAVEHDR_BYTES -- a short header is
 * rejected as ESIZE and so is a long one, which the caller detects by trying to
 * read a 33rd byte.
 *
 * Returns GBA_SAVEHDR_OK and fills *out, or one of the codes above and leaves
 * *out zeroed. THE OUTPUT IS ZEROED ON EVERY FAILURE PATH so a caller that
 * ignores the return value cannot read a half-populated header full of
 * plausible values.
 *
 * IT DOES NOT CHECK THE ROM HASH OR THE PAYLOAD HASH. Neither is knowable from
 * the 32 bytes alone: the first must be compared against the cartridge actually
 * loaded, the second against the .sav actually on disk. Those are the caller's
 * two remaining gates and gba_restorefile.c applies both. */
int gba_savehdr_parse(const unsigned char *raw, unsigned int n,
                      struct gba_savehdr *out);

/* The rejection, in words, for the screen and the UDP log. Never returns
 * NULL. */
const char *gba_savehdr_name(int rc);

#endif /* LUAPORT_GBA_SAVEHDR_H */
