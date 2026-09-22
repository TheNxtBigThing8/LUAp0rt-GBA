/* LUAport M12C -- adapters/gba/gba_restore.h
 *
 * THE gpSP-FACING HALF OF GBA CARTRIDGE SAVE RESTORE.
 *
 * M12B proved the WRITE direction on hardware: gpSP's save memory -> a
 * validated .sav + .hdr in /savedata0/gba. M12C PROVES THE REVERSE. This file
 * is the only place in the milestone that may write gamepak_backup[], and it is
 * deliberately the smallest surface that can do the job.
 *
 * ============================================================================
 * IT WRITES EXACTLY ONE THING, AND IT IS NOT A CLASSIFICATION GLOBAL
 * ============================================================================
 * THE ONLY OBJECT THIS ADAPTER MUTATES IS gamepak_backup[0 .. region).
 *
 * NONE of backup_type, backup_type_reset, eeprom_size, flash_bank_cnt,
 * flash_bank_num, flash_mode, flash_command_position, eeprom_mode,
 * eeprom_address or eeprom_counter is ever assigned. They are READ (the first
 * three, to derive the live class) and nothing more.
 *
 * ***** RAW BYTES ARE SUFFICIENT, AND SEEDING THE SIZE WOULD BE ACTIVELY
 * HARMFUL. ***** Every consumer of the restored bytes is a direct array index
 * whose offset comes from the GAME'S OWN address stream, not from anything this
 * adapter could set:
 *
 *     SRAM    gamepak_backup[address]                          gba_memory.c:468
 *     FLASH   gamepak_backup[address + 64*1024*flash_bank_num]           :503
 *     EEPROM  gamepak_backup[eeprom_address + (eeprom_counter / 8)]      :698
 *
 * and the classification globals are SELF-RESTORING. backup_type is reloaded
 * from backup_type_reset by init_memory() on every reset (:2438), and
 * backup_type_reset is derived by load_gamepak() from the ROM's own signature
 * scan (:2909-2962) or the gba_over database (:3008) -- deterministic for a
 * given cartridge, therefore identical on every launch.
 *
 * eeprom_size is the one worth spelling out. gba_memory.c:2441 re-defaults it
 * to EEPROM_512_BYTE on EVERY reset and :841-843 re-measures 8 KB from the
 * 17-unit DMA the game itself issues. :565 uses the current value to choose a
 * 6-bit versus a 14-bit EEPROM address width. FORCING IT TO 8 KB WOULD
 * THEREFORE DECODE EVERY ADDRESS WRONG until the game happened to agree. Since
 * M12B always persists the full 8192-byte window for both EEPROM classes, the
 * bytes are already correct at every offset the game will eventually address --
 * so the correct action is to restore the bytes and let gpSP re-measure. THIS
 * ADAPTER DOES NOT SEED IT.
 *
 * ============================================================================
 * WHY THIS IS A SEPARATE TRANSLATION UNIT FROM gba_restorefile.c
 * ============================================================================
 * gba_restore.c includes gpsp/common.h to reach gamepak_backup[]. That is the
 * same seam M12B's adapter was split along (gba_save.h:16-47), and the reason
 * is worth restating precisely because M12C's version is DIFFERENT from M12B's:
 *
 *   M12B was FORCED apart by a hard compile error. Its runtime half included
 *   runtime/savedata.h -> runtime/core.h, whose `typedef unsigned long u64`
 *   collides with gpsp/common.h:100's `unsigned long long`.
 *
 *   M12C's runtime half includes NEITHER. It does not need savedata.h at all
 *   (see gba_restorefile.h), and runtime/libc/stdio.h chains to no u64. SO THE
 *   HARD COLLISION DOES NOT APPLY HERE, and it would be dishonest to claim it
 *   does.
 *
 * The split is kept for two real reasons of its own:
 *
 *   1. THIS OBJECT NEEDS gpSP's COMPILE FLAGS. It must see the same
 *      -DROM_BUFFER_SIZE=2 configuration and the same gpsp include path as
 *      every other gpSP object, exactly as gba_rom.o and gba_save.o do.
 *
 *   2. CONFINING EVERY gamepak_backup WRITE TO ONE SMALL AUDITABLE OBJECT IS
 *      WHAT MAKES THE LINK-TIME GATES MEAN ANYTHING. m12c-verify asserts that
 *      gba_restore.o references gamepak_backup and that gba_restorefile.o,
 *      gba_savehdr.o and m12cmain.o do not. If the restore logic were spread
 *      across the fixture, that gate would have nothing to say.
 *
 * Every declaration below uses ONLY `unsigned int` and `const unsigned char *`,
 * which mean the same thing on both sides (gpsp/common.h:94, runtime/core.h:35).
 * NOTHING 64-BIT AND NO gpSP STRUCT CROSSES THIS BOUNDARY.
 *
 * ============================================================================
 * WHAT IS FROZEN AND IS NOT TOUCHED
 * ============================================================================
 * gpsp/, runtime/savedata.{c,h}, runtime/shim.c, runtime/audio.c,
 * runtime/linker.ld, runtime/boot.inc, adapters/gba/gba_save.{c,h},
 * adapters/gba/gba_savefile.{c,h}, adapters/gba/gba_m11save.{c,h},
 * adapters/gba/gba_rom.{c,h}, apps/m11gpsp/main.c, apps/m12gpsp/main.c,
 * apps/m12bgpsp/main.c, lua/m11.lua.in, lua/m12.lua.in and lua/m12b.lua.in are
 * ALL UNCHANGED by this milestone. Every symbol M12C needs is either declared
 * in a gpSP header already or is a non-static global declared extern by hand --
 * the technique upstream itself uses at gpsp/gba_memory.c:198.
 */

#ifndef LUAPORT_GBA_RESTORE_H
#define LUAPORT_GBA_RESTORE_H

/* gamepak_backup[] is 131,072 bytes (gba_memory.c:360). Restated rather than
 * included so this header stays dependency-free. */
#define GBA_RESTORE_BACKUP_BYTES 131072u

/* The save classes, NUMERICALLY IDENTICAL to GBA_SAVE_* (gba_save.h:99-105),
 * GBA_SAVEHDR_CLS_* and M11's M11_SAVE_*. Held identical across every
 * milestone so their logs can be read side by side. */
#define GBA_RESTORE_CLS_UNKNOWN   0u
#define GBA_RESTORE_CLS_SRAM      1u
#define GBA_RESTORE_CLS_FLASH64   2u
#define GBA_RESTORE_CLS_FLASH128  3u
#define GBA_RESTORE_CLS_EEPROM512 4u
#define GBA_RESTORE_CLS_EEPROM8K  5u

/* ------------------------------------------------------ THE BASELINE KINDS --
 *
 * M12C MUST NOT INHERIT A BLANK BASELINE, AND THIS IS WHY THE DISTINCTION IS
 * NAMED RATHER THAN INFERRED.
 *
 * gba_save_latch() (frozen, and still linked because M12C needs its identity
 * and naming logic) takes its baseline BEFORE the restore, over a backup array
 * that is still entirely 0xFF. If M12C reused that baseline, then the instant
 * it restored 8192 real bytes the array would differ from it and the run would
 * report DIRTY -- announcing that "the game modified the save" when in fact
 * nothing but the restore had happened. Worse, a milestone that later gained a
 * writeback path would immediately rewrite the very file it had just read.
 *
 * So M12C OWNS ITS OWN BASELINE, taken AFTER the restore settles, and records
 * WHICH OF THE TWO SITUATIONS IT DESCRIBES:
 *
 *   NEWGAME   no valid save was restored. The baseline is the ordinary
 *             post-load_gamepak blank state, and anything the game writes is
 *             genuinely new.
 *   RESTORED  a validated save was applied. The baseline IS the restored save,
 *             so "modified" means the game changed it during THIS run.
 */
#define GBA_RESTORE_BASE_NONE     0u   /* gba_restore_baseline() never ran     */
#define GBA_RESTORE_BASE_NEWGAME  1u
#define GBA_RESTORE_BASE_RESTORED 2u

/* =========================================================================
 *                                THE CALLS
 * ========================================================================= */

/* THE CARTRIDGE'S SAVE CLASS RIGHT NOW, one of GBA_RESTORE_CLS_*.
 *
 * Resolved from backup_type TOGETHER WITH flash_bank_cnt / eeprom_size -- never
 * from backup_type alone, because BACKUP_FLASH covers two sizes and
 * BACKUP_EEPROM covers two more. This is the same resolution gba_save.c:134-148
 * and gba_m11save.c:165-179 perform, and it is PURELY READ-ONLY.
 *
 * ***** IT DOES NOT CALL read_backup(), write_backup(), read_eeprom() OR
 * write_eeprom(). ***** Those four MUTATE backup_type (gba_memory.c:464-465,
 * :1140-1141, :542): calling one to "check the save type" would MANUFACTURE the
 * class it then reported, and an unexercised cartridge would suddenly classify
 * as SRAM -- at which point M12C would happily accept an SRAM header for it.
 * m12c-verify asserts the absence of all four from every M12C object. */
unsigned int gba_restore_live_class(void);

/* ------------------------------------- THE PRE-BOOT DATABASE FLASH128 FLAG --
 *
 * 1 WHEN gpSP's OWN CARTRIDGE DATABASE HAS ASSERTED A 128 KB FLASH PART AND
 * LEFT THE SAVE TYPE UNSET -- that is, backup_type is BACKUP_UNKN AND
 * flash_bank_cnt is FLASH_SIZE_128KB. Otherwise 0.
 *
 * ***** THIS IS EVIDENCE, NOT A CLASS, AND IT IS NOT A DETECTION. ***** The
 * live class is still UNKNOWN when this returns 1 and stays UNKNOWN until the
 * game's own bus traffic classifies it. Nothing in M12C seeds any gpSP
 * classification global, and this function reads two of them and writes none.
 *
 * WHY THE STATE IS UNAMBIGUOUS. Every pre-execution writer of flash_bank_cnt
 * that reaches FLASH_SIZE_128KB ALSO sets backup_type_reset to BACKUP_FLASH --
 * the FLASH1M_V signature match, the Pokemon short-circuit and the is_hack
 * override all do. THE ONE EXCEPTION IS gba_over.h's FLAGS_FLASH_128KB handler
 * (gba_memory.c:1727), which sets flash_bank_cnt and the Sanyo device id and
 * then falls through WITHOUT SETTING A TYPE, because only FLAGS_EEPROM sets one
 * (:1740). So the conjunction this function tests is reachable from EXACTLY ONE
 * place: gpSP's database asserting a flash cartridge it then failed to classify.
 * adapters/gba/gba_m11save.h:90-95 recorded that same gpSP defect during M11.
 *
 * WHAT IT IS FOR. gba_restorefile.c hands it to
 * gba_savehdr_compatible_ev() as the ONLY thing that may excuse a FLASH128
 * header under a live UNKNOWN. Without it that case stays refused, exactly as
 * it always has. THE UNIT COUNT IS COMPARED AND NEVER RETURNED --
 * FLASH_SIZE_128KB is 2, not 131072 -- so this is a 0/1 flag by design. */
unsigned int gba_restore_db_flash128(void);

/* ---------------------------------------------------------------- THE ARM --
 *
 * ***** MUTATION IS DISARMED UNTIL THIS IS CALLED, AND STAGE 0 NEVER CALLS IT.
 *
 * gba_restore_apply() returns 0 and writes NOTHING unless the adapter has been
 * armed. That makes STAGE 0's passivity a STRUCTURAL PROPERTY OF THE ADAPTER
 * rather than a consequence of the fixture happening not to reach a call site.
 * A later edit to apps/m12cgpsp/main.c cannot accidentally make the
 * validate-only stage write, because the arming call is the thing STAGE 0 omits
 * and the refusal lives down here.
 *
 * It is deliberately NOT armed by a successful validation: validation and
 * permission are different facts, and conflating them is how a "check" turns
 * into an "apply". */
void gba_restore_arm(void);

#ifdef LUAPORT_SESSION_REUSE
/* ***** M16 ONLY. RETURN THIS MODULE TO ITS PRE-ARM SESSION STATE. *****
 *
 * Declared only when LUAPORT_SESSION_REUSE is defined -- that is, only on M16
 * targets. M13C's preprocessed view of this header does not contain it.
 *
 * Clears gr_armed, the applied byte count, the rollback count, the baseline
 * kind and hash, and the exit hash and its taken flag.
 *
 * ***** IT IS NOT A ROLLBACK AND IT DOES NOT TOUCH gamepak_backup[]. *****
 * gba_restore_rollback() is the failure path that wipes the array; this is the
 * session boundary that forgets measurements describing a cartridge which is no
 * longer loaded.
 *
 * ***** WITHOUT IT, SESSION 2's VALIDATE-ONLY PASS RUNS ARMED. ***** Nothing
 * else in the module ever stores 0 to gr_armed, and the passivity guarantee of
 * the validate stage rests entirely on gba_restore_apply()'s disarmed refusal.
 *
 * No validation policy, clamp or class resolution is changed. */
void gba_restore_disarm(void);

/* --------- M16 ONLY: THE FLASH64 ELIGIBILITY, AND THE ONE ACTIVATION -------
 *
 * Declared only when LUAPORT_SESSION_REUSE is defined. M13C's preprocessed view
 * of this header does not contain it.
 *
 * `activate` == 0  PURELY READ-ONLY. Returns 1 when backup_type is BACKUP_UNKN
 *                  AND flash_bank_cnt is FLASH_SIZE_64KB, else 0. Writes
 *                  nothing. This is how gba_restorefile.c's class gate asks
 *                  whether the FLASH64 rule may apply.
 * `activate` != 0  RE-TESTS that same precondition and, only if it still holds,
 *                  stores backup_type = BACKUP_FLASH and returns 1. If the state
 *                  has moved it stores NOTHING and returns 0.
 *
 * ##### THE PRECONDITION IS NOT EVIDENCE ABOUT THE CARTRIDGE. ##### It is ALSO
 * gpSP's DEFAULT for any unclassified cartridge, so it proves only that gpSP has
 * not classified this one as something ELSE -- ELIGIBILITY, not detection. THE
 * AUTHORITY FOR THE CLASS IS THE VALIDATED ROM-BOUND LGS1 FLASH64 HEADER, which
 * gba_restorefile.c proves in full -- magic, version, reserved bytes, geometry,
 * ROM FNV and payload FNV -- before it ever passes 1. Contrast
 * gba_restore_db_flash128(), which reports a real gba_over.h assertion; no 64 KB
 * counterpart exists because FLAGS_FLASH_64KB is not in that database at all.
 *
 * ***** IT WRITES backup_type AND NOTHING ELSE. ***** backup_type_reset,
 * flash_bank_cnt, eeprom_size, flash_bank_num, flash_mode and
 * flash_command_position are all left exactly as found. The resulting state is
 * identical to a natively detected 64 KB flash cartridge after init_memory().
 *
 * ***** THE CALLER MUST NOT ACTIVATE UNTIL THE RESTORE HAS FULLY SUCCEEDED.
 * ***** gba_restorefile.c calls it with 1 only after PASS 2 has applied AND
 * re-hashed the array. Activating earlier would leave a cartridge classified
 * FLASH64 with no save behind it after a rollback, and the next commit would
 * write 65,536 bytes of blank flash over whatever was on disk. */
unsigned int gba_restore_flash64(int activate);
#endif
unsigned int gba_restore_armed(void);

/* ------------------------------------------------------------- THE APPLY ---
 *
 * COPY n BYTES FROM src INTO gamepak_backup[off .. off+n). Returns the number
 * of bytes actually written, which is 0 if the adapter is not armed, if src is
 * NULL, or if off is already at or past the bound.
 *
 * DOUBLE-CLAMPED, ON PURPOSE. The count is clamped to the caller's declared
 * region AND independently to GBA_RESTORE_BACKUP_BYTES. The first stops a
 * caller writing past the SAVE; the second stops any region ever writing past
 * the ARRAY. Two bounds because one of them is always the one a later edit
 * removes.
 *
 * `unsigned char` is u8 on BOTH sides (gpsp/common.h:94, runtime/core.h:35), so
 * this pointer is exactly type-correct across the boundary. THIS IS THE ONLY
 * WAY SAVE BYTES ENTER gpSP, which is what lets M12C stream a file straight
 * into the emulator through a 4096-byte STACK buffer and add ZERO bytes of .bss
 * for the payload. */
unsigned int gba_restore_apply(const unsigned char *src, unsigned int off,
                               unsigned int n, unsigned int region);

/* ---------------------------------------------------------- THE ROLLBACK ---
 *
 * RESTORE THE BACKUP ARRAY TO THE STATE m4_rom_backup_init() LEFT IT IN --
 * all 131,072 bytes to 0xFF.
 *
 * ***** THIS IS WHAT MAKES THE RESTORE ALL-OR-NOTHING. ***** The second pass
 * copies and re-hashes as it goes; if it short-reads, or if the hash of what it
 * actually wrote diverges from the header's, the array is left holding a
 * PARTIAL save -- bytes that are neither the old state nor a valid new one, and
 * that the game would read as a corrupt profile. Wiping to 0xFF puts the
 * cartridge back into the exact idle state a first boot produces
 * (gba_memory.c:2907 normalises a blank backup to precisely this), so the game
 * starts cleanly instead of loading half a save.
 *
 * 0xFF AND NOT 0x00. Real flash and EEPROM idle at 0xFF; a zero-filled region
 * is what gpSP's own normalize_blank_backup_for_detected_type() exists to
 * CORRECT (:2903-2906). Rolling back to zeros would leave a state gpSP would
 * then rewrite on the next boot. */
void gba_restore_rollback(void);

/* ---------------------------------------------------------- THE BASELINE ---
 *
 * Take the M12C baseline hash over the FULL 131,072 bytes and record which
 * situation it describes (GBA_RESTORE_BASE_NEWGAME or _RESTORED).
 *
 * THE FULL ARRAY, NOT THE REGION, for M12B's reason (gba_save.h:362-384): the
 * region can CHANGE SIZE mid-run when backup_type or eeprom_size resolves
 * upward, and a baseline hashed over one size is not comparable with an exit
 * hash over another. Comparing them would report "the game modified the save"
 * for a pure reclassification in which not one byte changed. */
void gba_restore_baseline(unsigned int kind);
unsigned int gba_restore_baseline_kind(void);
unsigned int gba_restore_baseline_hash(void);

/* Take the exit hash over the full array, and report whether it differs from
 * the baseline. M12C REPORTS THIS AND NEVER ACTS ON IT -- there is no writeback
 * in this milestone, by design, so that the original M12B artifact survives the
 * proof intact. */
void         gba_restore_finish(void);
unsigned int gba_restore_exit_hash(void);
unsigned int gba_restore_modified(void);

/* ------------------------------------------------------------ the answers -- */

/* How many bytes gba_restore_apply() has written in total since the last
 * rollback. The fixture compares this against the header's region: a mismatch
 * is an apply failure even if every individual chunk reported success. */
unsigned int gba_restore_applied(void);

/* FNV-1a over gamepak_backup[0 .. n). Used to re-verify the applied bytes
 * against the header's payload hash from the emulator's side -- a SECOND,
 * INDEPENDENT measurement of what actually landed, taken from the array rather
 * than from the file. */
unsigned int gba_restore_region_fnv(unsigned int n);

/* Bytes in [0, n) that are not 0xFF. Zero means the region is still the idle
 * state, which after a successful restore would mean the save that was
 * committed was itself blank. */
unsigned int gba_restore_nonff(unsigned int n);

/* One raw value at a time, for the UDP log and for ext->dbg[]. Returns 0 for an
 * unknown selector. */
unsigned int gba_restore_read(unsigned int sel);

#define GBA_RESTORE_RD_LIVECLASS    0u
#define GBA_RESTORE_RD_ARMED        1u
#define GBA_RESTORE_RD_APPLIED      2u
#define GBA_RESTORE_RD_BASEKIND     3u
#define GBA_RESTORE_RD_BASEHASH     4u
#define GBA_RESTORE_RD_EXITHASH     5u
#define GBA_RESTORE_RD_MODIFIED     6u
#define GBA_RESTORE_RD_ROLLBACKS    7u
#define GBA_RESTORE_RD_BACKUP_TYPE  8u  /* backup_type    RAW                  */
#define GBA_RESTORE_RD_BANKCNT      9u  /* flash_bank_cnt RAW (1 or 2)         */
#define GBA_RESTORE_RD_EEPSIZE     10u  /* eeprom_size    RAW (1 or 16)        */
#define GBA_RESTORE_RD_DBFLASH128  11u  /* 1 = DATABASE flash128, type UNSET   */

#endif /* LUAPORT_GBA_RESTORE_H */
