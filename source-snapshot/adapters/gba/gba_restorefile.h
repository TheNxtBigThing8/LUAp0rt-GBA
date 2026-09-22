/* LUAport M12C -- adapters/gba/gba_restorefile.h
 *
 * THE RUNTIME-FACING HALF OF GBA CARTRIDGE SAVE RESTORE.
 *
 * adapters/gba/gba_savehdr.h owns the 32-byte header format and its validation
 * rules. adapters/gba/gba_restore.h owns everything that touches gpSP. THIS
 * file owns everything that knows what a FILE is: the two handles, the reads,
 * the two passes and the ordering between them.
 *
 * READ gba_savehdr.h AND gba_restore.h FIRST.
 *
 * ============================================================================
 * ***** M12C OPENS NO WRITE WINDOW, AND DOES NOT LINK THE SAVEDATA LAYER *****
 * ============================================================================
 * THIS IS THE SINGLE MOST IMPORTANT PROPERTY OF THE MILESTONE.
 *
 * runtime/savedata.h:14-19 records that /savedata0 is mounted READ-ONLY inside
 * the ps2emu sandbox by the host process itself. Reading from it needs no mount
 * and no window -- only a plain fopen(name, "rb"). Two independent proofs that
 * this already works on hardware:
 *
 *   adapters/gba/gba_rom.c:118 opens /savedata0/rom/test.gba with a bare
 *   fopen and no savedata call anywhere in its path. M4 through M12B are all
 *   hardware-PASS on exactly that read.
 *
 *   apps/m12gpsp/main.c:592-594 -- M12A's STAGE 1 -- is documented as
 *   "READ ONLY. No init, no mount, no window, no write", and it read back the
 *   32-byte probe M12A had committed in a PREVIOUS process. That stage is
 *   hardware-PASS.
 *
 * So M12C does not include runtime/savedata.h, does not call
 * plat_savedata_init, plat_savedata_begin_write, plat_savedata_end_write or
 * plat_savedata_mkdir, AND savedata.o IS NOT IN THE M12C LINK LINE AT ALL.
 *
 * THAT IS A STRONGER GUARANTEE THAN ANY RUNTIME CHECK. A restore fixture that
 * cannot link a write window cannot open one by accident, cannot mis-commit,
 * and cannot leave the container mounted read-write with no read-only mount
 * behind it -- the state whose only escape is a hard power cycle of the
 * console. m12c-verify asserts all four symbols are absent from every M12C
 * object and that savedata.o is absent from the object list.
 *
 * IT ALSO MEANS M12C CANNOT DAMAGE THE M12B ARTIFACT. The .sav and .hdr that
 * M12B committed on hardware are opened "rb" and nothing else. There is no
 * writeback, no repair, no rewrite, no .tmp and no unlink anywhere in this
 * milestone.
 *
 * ============================================================================
 * THE TWO-PASS DESIGN, AND WHY IT BEATS A 128 KB BUFFER
 * ============================================================================
 * The restore must be ALL-OR-NOTHING: gamepak_backup[] must not be mutated
 * until the ENTIRE file has been proved to match its header. But the file is up
 * to 131,072 bytes and M12C will not hold it whole. Three options were
 * considered:
 *
 *   A. TWO PASSES over the file.        <-- CHOSEN
 *   B. One pass into a 128 KB .bss staging buffer, then a bulk copy.
 *   C. Apply optimistically and undo on failure.
 *
 * (C) is not all-or-nothing at all -- it is exactly the partial-write hazard
 * with extra steps -- and is rejected outright.
 *
 * (B) IS AFFORDABLE BUT BUYS NOTHING. M12B's measured .bss is 1,424,352 bytes
 * against a 2,097,152 tripwire (tools/check_forbidden.py:165), so 131,072 more
 * would land at 1,555,424 and still leave ~493 KB of margin. It is not a
 * shortage of memory that rules it out: it is that (B) delivers the SAME
 * guarantee as (A) while costing a second full copy of every byte and adding a
 * 128 KB object the verifier then has to police. The reason to prefer it would
 * be a file that could change between the two passes -- and it cannot.
 *
 * (A) IS SAFE PRECISELY BECAUSE THE MOUNT IS READ-ONLY. Nothing in this process
 * can write the file, the fixture is single-threaded, and no other process has
 * the container mounted read-write while the game is running. The bytes pass 1
 * hashed are the bytes pass 2 reads.
 *
 *   PASS 1  open, read the whole region in 4096-byte STACK chunks, accumulate
 *           FNV-1a, verify the length is EXACTLY the header's region and that
 *           the hash matches. MUTATES NOTHING.
 *   PASS 2  reopen, read again, push each chunk into gamepak_backup[] through
 *           gba_restore_apply(), and re-accumulate the hash AS IT COPIES.
 *
 * PASS 2 RE-HASHES RATHER THAN TRUSTING PASS 1, and then the fixture takes a
 * THIRD measurement from the array itself via gba_restore_region_fnv(). Three
 * independent readings of the same bytes: the file as validated, the file as
 * applied, and the array as it now stands. A discrepancy between any two is an
 * apply failure and triggers gba_restore_rollback().
 *
 * TOTAL COST: 4096 bytes of STACK and ZERO bytes of .bss for the payload.
 *
 * ============================================================================
 * NAMING DISCIPLINE
 * ============================================================================
 * tools/check_forbidden.py:108-111 matches the BARE SUBSTRINGS "path_",
 * "retro_", "libretro_", "vfs_", "string_is_", "filestream_", "rtime_" and
 * "encoding_", and :79-85 adds "_stub" and "emit_". Every symbol below is
 * gba_restorefile_* and contains none of them. In particular NOTHING here is
 * named *_path_*, which is why the frozen writer spells its two builders
 * gba_save_name_sav() and gba_save_name_hdr() (gba_save.h:50-62) and why M12C
 * simply reuses them. NO ALLOWLIST IS WIDENED FOR M12C.
 */

#ifndef LUAPORT_GBA_RESTOREFILE_H
#define LUAPORT_GBA_RESTOREFILE_H

/* THE STAGING BUFFER. 4096 bytes on the STACK, matching runtime/shim.c's
 * WBUF_SIZE so a staged chunk is one shim buffer and the read cadence is one
 * syscall per chunk. It is the ONLY buffer in the restore path. */
#define GBA_RESTOREFILE_CHUNK 4096u

/* ------------------------------------------------------------- THE MODES ---
 *
 * ONE IMAGE, TWO BEHAVIOURS, AND THE DIFFERENCE IS THE WHOLE HARDWARE PROOF.
 *
 * VALIDATE reads and checks EVERYTHING -- both files, every header field, the
 * ROM identity, the class compatibility, the exact length and the full payload
 * hash -- and then DELIBERATELY DOES NOT APPLY. It is the control run: it shows
 * the operator what the game looks like with no save restored, while proving
 * the file on disk is sound.
 *
 * RESTORE performs the IDENTICAL validation and then applies.
 *
 * Because both modes run the same code down to the last branch, a difference in
 * what the GAME shows between the two can only be caused by the bytes. That is
 * what makes the STAGE 0 / STAGE 1 comparison a real experiment rather than an
 * anecdote -- see the PASS criterion in lua/m12c.lua.in. */
#define GBA_RESTOREFILE_MODE_VALIDATE 0u
#define GBA_RESTOREFILE_MODE_RESTORE  1u

/* ------------------------------------------------------------- the returns --
 *
 * 0 IS SUCCESS. EVERY REJECTION IS DISTINCT, because an operator reading one
 * off a screen must know which of the fifteen conditions occurred without
 * consulting a log, and because apps/m12cgpsp/main.c maps each to its own
 * -3xx status code.
 *
 * ***** NONE OF THESE IS A FAILURE OF M12C. ***** Every one of them means the
 * validator did its job: it found persisted state it could not vouch for and
 * REFUSED TO RESTORE IT. gamepak_backup[] is left exactly as
 * m4_rom_backup_init() and load_gamepak() left it, the files on disk are not
 * touched, and the game boots clean. The fixture reports the reason and
 * continues -- a declined restore is a PASS for the mechanism and an
 * observation about the data. */
#define GBA_RESTOREFILE_OK           0
#define GBA_RESTOREFILE_ENOID       -1  /* no identity -- no filename exists   */
#define GBA_RESTOREFILE_ENOSAVE     -2  /* neither file present -- FIRST RUN   */
#define GBA_RESTOREFILE_EORPHANSAV  -3  /* .sav with no .hdr -- NOT COMMITTED  */
#define GBA_RESTOREFILE_EORPHANHDR  -4  /* .hdr with no .sav                   */
#define GBA_RESTOREFILE_EHDRSIZE    -5  /* .hdr is not exactly 32 bytes        */
#define GBA_RESTOREFILE_EMAGIC      -6
#define GBA_RESTOREFILE_EVERSION    -7
#define GBA_RESTOREFILE_ERESERVED   -8
#define GBA_RESTOREFILE_ECLASS      -9  /* UNKNOWN or out of range             */
#define GBA_RESTOREFILE_EGEOM      -10  /* region/declared wrong for the class */
#define GBA_RESTOREFILE_EROMID     -11  /* the save belongs to another cartridge*/
#define GBA_RESTOREFILE_EINCOMPAT  -12  /* family mismatch, or a live UNKNOWN
                                           that neither the ROM-BOUND
                                           EEPROM/SRAM rule nor the DATABASE
                                           FLASH128 evidence rule covers --
                                           see gba_savehdr.h                   */
#define GBA_RESTOREFILE_ESAVSIZE   -13  /* .sav length is not the header region */
#define GBA_RESTOREFILE_EPAYFNV    -14  /* payload hash mismatch -- STALE HEADER*/
#define GBA_RESTOREFILE_ESHORTREAD -15  /* pass 1 could not read the region     */
#define GBA_RESTOREFILE_EAPPLY     -16  /* PASS 2 FAILED -- ROLLED BACK TO 0xFF */

/* ---------------------------------------------------------- the progress latch
 *
 * SET MEANS REACHED. Published even on a failure path, so a validation that
 * stopped half way says exactly how far it got rather than only that it
 * stopped. Same polarity and purpose as GBA_SAVEFILE_L_* (gba_savefile.h:120-131). */
#define GBA_RESTOREFILE_L_NAMED    (1u << 0)  /* both filenames were built     */
#define GBA_RESTOREFILE_L_HDROPEN  (1u << 1)  /* the .hdr opened               */
#define GBA_RESTOREFILE_L_HDRVALID (1u << 2)  /* all 32 bytes parsed and passed*/
#define GBA_RESTOREFILE_L_ROMOK    (1u << 3)  /* the ROM identity matched      */
#define GBA_RESTOREFILE_L_CLASSOK  (1u << 4)  /* the class family was compatible*/
#define GBA_RESTOREFILE_L_SAVOPEN  (1u << 5)  /* the .sav opened               */
#define GBA_RESTOREFILE_L_SIZEOK   (1u << 6)  /* the .sav is EXACTLY the region*/
#define GBA_RESTOREFILE_L_PAYOK    (1u << 7)  /* PASS 1: the payload hash matched*/
#define GBA_RESTOREFILE_L_APPLIED  (1u << 8)  /* PASS 2: every byte was applied*/
#define GBA_RESTOREFILE_L_VERIFIED (1u << 9)  /* the ARRAY re-hashed correctly */
#define GBA_RESTOREFILE_L_ROLLBACK (1u << 10) /* PASS 2 failed and was undone  */

/* ***** ELIGIBILITY CAME FROM THE ROM-BOUND EEPROM FAMILY RULE. *****
 *
 * SET ONLY WHEN THE LIVE CLASS WAS UNKNOWN and the save was accepted anyway,
 * because the header is EEPROM-family and its ROM FNV had already been proved
 * equal to this cartridge's. Clear whenever the live class was a real family
 * that matched -- see gba_savehdr.h's ROM-BOUND FORM.
 *
 * IT EXISTS SO THE LOG CANNOT LIE ABOUT WHY A RESTORE WAS ALLOWED. Without it,
 * "class compatible" would read identically whether gpSP had actually detected
 * EEPROM or whether M12C had trusted a file's family in the absence of any live
 * detection at all. Those are very different claims and an operator must be able
 * to tell them apart at a glance:
 *
 *     LIVE CLASS UNKNOWN + TRUST ROM-BOUND EEPROM FAMILY   <- this bit set
 *     LIVE CLASS EEPROM  + TRUST LIVE FAMILY MATCH         <- this bit clear
 *
 * IT IS NOT A CLAIM THAT gpSP DETECTED ANYTHING. Nothing in M12C seeds
 * backup_type, backup_type_reset, eeprom_size or flash_bank_cnt; the live class
 * is still UNKNOWN after the restore and stays that way until the game's own bus
 * traffic classifies it. */
#define GBA_RESTOREFILE_L_FAMTRUST (1u << 11)

/* ***** ELIGIBILITY REQUIRED gpSP's DATABASE FLASH128 EVIDENCE. *****
 *
 * SET ONLY WHEN a FLASH128 header was accepted under a LIVE CLASS OF UNKNOWN --
 * which no rule other than the evidence cell can do. L_FAMTRUST is set too (the
 * live class really was UNKNOWN); this bit says WHICH carve-out fired.
 *
 *     LIVE UNKNOWN + EEPROM/SRAM header, ROM-bound       L_FAMTRUST only
 *     LIVE UNKNOWN + FLASH128 header, ROM-bound + DB     L_FAMTRUST + L_DBFLASH
 *     LIVE class matched the header's family             neither
 *
 * ***** IT IS NOT A CLAIM THAT gpSP DETECTED FLASH. ***** It records that
 * gba_over.h ASSERTED a 128 KB flash part (FLAGS_FLASH_128KB,
 * gba_memory.c:1727) and that gpSP then left backup_type UNSET, which is the
 * defect this carve-out exists to survive. The live class is STILL UNKNOWN after
 * such a restore and nothing seeds it; the game must classify the cartridge from
 * its own bus traffic exactly as before. An operator reading the log must be
 * able to tell "detected" from "asserted but unrecorded" at a glance. */
#define GBA_RESTOREFILE_L_DBFLASH  (1u << 12)
#define GBA_RESTOREFILE_L_BITS      13u

/* =========================================================================
 *                                THE CALLS
 * ========================================================================= */

/* VALIDATE, AND IN RESTORE MODE APPLY, THE PERSISTED SAVE FOR THE CARTRIDGE
 * CURRENTLY LOADED.
 *
 * Call ONCE, AFTER gba_save_latch() has produced an identity and AFTER
 * reset_gba() #2 has finished re-defaulting gpSP's save state, and BEFORE the
 * frame loop. apps/m12cgpsp/main.c derives that call site at length; the short
 * version is that init_memory() provably never touches gamepak_backup[] (the
 * only writers in gpsp/ are gba_memory.c:581, :587, :1195, :1206, :1220, :1239,
 * :1247 -- all reachable only from executing code -- plus the blank-normalise
 * at :2906), so a restore placed after the reset survives to the first
 * instruction the game executes.
 *
 * The filenames come from the FROZEN M12B builders gba_save_name_sav() and
 * gba_save_name_hdr(), so M12C reads exactly the names M12B wrote and the two
 * milestones cannot disagree about where a save lives.
 *
 * Returns one of GBA_RESTOREFILE_*. */
int gba_restorefile_run(unsigned int mode);

/* THE SAME LADDER AGAINST CALLER-SUPPLIED FILENAMES.
 *
 * IT EXISTS SO THE OFFLINE HARNESS CAN TEST THE SHIPPING CODE. tools/
 * gba_restore_equiv.c writes crafted files to a temporary directory and drives
 * THIS function, so the whole two-pass ladder -- every rejection, the rollback
 * and the passivity guarantee -- is exercised by the same source that runs on
 * the console. Without it the harness could only test a paraphrase.
 *
 * It is NOT a test-only hook bolted onto production code: gba_restorefile_run()
 * is a thin wrapper that builds the two names and calls this, so the tested
 * path IS the production path. There is no #ifdef anywhere in this adapter. */
int gba_restorefile_with(const char *sav, const char *hdr, unsigned int mode);

/* ------------------------------------------------------- report and log --- */

/* The GBA_RESTOREFILE_L_* progress mask from the last run. */
unsigned int gba_restorefile_latch(void);

/* The rejection, in words, for the screen and the UDP log. Never NULL. */
const char *gba_restorefile_name(int rc);

/* One raw value at a time, for the UDP log and for ext->dbg[]. Returns 0 for an
 * unknown selector. */
unsigned int gba_restorefile_read(unsigned int sel);

#define GBA_RESTOREFILE_RD_LATCH     0u
#define GBA_RESTOREFILE_RD_STATUS    1u
#define GBA_RESTOREFILE_RD_MODE      2u
#define GBA_RESTOREFILE_RD_CLASS     3u  /* the class the HEADER recorded      */
#define GBA_RESTOREFILE_RD_LIVECLASS 4u  /* the class gpSP has RIGHT NOW       */
#define GBA_RESTOREFILE_RD_REGION    5u  /* header region -- the restore length*/
#define GBA_RESTOREFILE_RD_DECLARED  6u
#define GBA_RESTOREFILE_RD_CONF      7u  /* recorded, never a gate             */
#define GBA_RESTOREFILE_RD_HDRROMFNV 8u
#define GBA_RESTOREFILE_RD_HDRPAYFNV 9u
#define GBA_RESTOREFILE_RD_CURROMFNV 10u /* the cartridge actually loaded      */
#define GBA_RESTOREFILE_RD_CALCFNV   11u /* PASS 1's hash of the file          */
#define GBA_RESTOREFILE_RD_APPLYFNV  12u /* PASS 2's hash of what it copied    */
#define GBA_RESTOREFILE_RD_SAVBYTES  13u /* bytes the .sav actually held       */
#define GBA_RESTOREFILE_RD_HDRBYTES  14u
#define GBA_RESTOREFILE_RD_CHUNKS    15u
#define GBA_RESTOREFILE_RD_APPLIED   16u /* bytes pushed into gamepak_backup[] */
#define GBA_RESTOREFILE_RD_FAMTRUST  17u /* 1 = ROM-BOUND EEPROM FAMILY trust  */
#define GBA_RESTOREFILE_RD_DBFLASH128 18u/* 1 = DB asserted FLASH128, type UNSET*/
#define GBA_RESTOREFILE_RD_DBTRUST   19u /* 1 = that evidence ADMITTED the save */

#endif /* LUAPORT_GBA_RESTOREFILE_H */
