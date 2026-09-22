/* LUAport M11 -- adapters/gba/gba_m11save.h
 *
 * THE M11-OWNED, PASSIVE CARTRIDGE SAVE-MEMORY OBSERVER.
 *
 * M11 IS DETECTION ONLY. NOTHING BELOW OPENS, CREATES, READS OR WRITES A SAVE
 * FILE. There is no path string, no filename rule, no autosave and no
 * load-on-boot anywhere in this adapter or in the M11 fixture; persistence is
 * M12's milestone and M11 deliberately does not prepare a byte of it. What this
 * file produces is an ANSWER -- what save hardware this cartridge has, how big
 * it is, where that conclusion came from, and whether it is FINAL or still
 * PROVISIONAL.
 *
 * ============================================================================
 * WHY AN OBSERVER AND NOT A gpSP EDIT
 * ============================================================================
 * gpsp/** is FROZEN. Every value M11 needs is either declared in
 * gpsp/gba_memory.h or is a non-static global with external linkage that can be
 * declared extern by hand -- the technique upstream itself uses at
 * gpsp/gba_memory.c:198 and that adapters/gba/gba_probe.c:75-85 established for
 * M2 and adapters/gba/gba_rom.c:81-88 for M4. So M11 needs ZERO changes to
 * gpsp/**, and gba_m11save.o carries the relocations that prove it reads the
 * emulator's real state rather than a copy.
 *
 * ============================================================================
 * THE THREE THINGS THAT ARE NOT OBSERVABLE, STATED UP FRONT
 * ============================================================================
 * A milestone that quietly papers over what it cannot see is worse than one
 * that reports less. These are the three, and each has a named consequence.
 *
 *   1. sram_bankcount IS DECLARED AND NEVER DEFINED.
 *      gpsp/gba_memory.h:316 declares `extern u32 sram_bankcount;` and NO
 *      translation unit in the tree defines it. Referencing it would create an
 *      undefined symbol and fail the zero-undefined-symbols gate that every
 *      milestone from M1 onward has passed. THIS FILE NEVER REFERENCES IT.
 *      SRAM's size is taken from the one place gpSP actually commits to a
 *      number: normalize_blank_backup_for_detected_type(), gba_memory.c:2889,
 *      which sizes an SRAM cartridge's backup region at exactly 32 KB.
 *
 *   2. backup_type_eeprom_guessed AND eeprom_really_used ARE `static`.
 *      gba_memory.c:412 and :414. They are the two flags that say whether an
 *      EEPROM classification was a whole-ROM-sweep GUESS and whether EEPROM was
 *      ever actually driven. No adapter can read them and gpSP is not edited to
 *      expose them. M11 RE-DERIVES the first from the same inputs gpSP used --
 *      it re-runs the signature logic itself and compares -- and reports it as
 *      DERIVED, never as read. The second is replaced by a direct observation
 *      of eeprom_mode leaving EEPROM_BASE_MODE, which is strictly weaker and is
 *      labelled as such.
 *
 *   3. THE SWEEP IS CACHE-DEPENDENT ON A DEMAND-PAGED CARTRIDGE.
 *      rom_scan_signatures_in_memory() (gba_memory.c:2823-2860) walks only
 *      buffers that are RESIDENT (`buf_idx < gamepak_buffer_count`). With
 *      -DROM_BUFFER_SIZE=2 on a 32 MB cartridge most of the ROM is never
 *      scanned, so a sweep result is not reproducible across runs. M11 reports
 *      the SCANNED FRACTION (M11_RD_SWEEP_BYTES / M11_RD_SWEEP_TOTAL) and sets
 *      M11_R_SWEEP_PARTIAL rather than implying a whole-cartridge scan.
 *
 * ============================================================================
 * THE OBSERVER IS PASSIVE, AND THAT IS A CORRECTNESS REQUIREMENT
 * ============================================================================
 * NOTHING HERE CALLS read_backup(), write_backup(), read_eeprom() OR
 * write_eeprom(). That is not tidiness -- those four functions MUTATE THE
 * DETECTION STATE:
 *
 *     read_backup()  gba_memory.c:464-465   BACKUP_UNKN -> BACKUP_SRAM
 *     write_backup() gba_memory.c:1140-1141 BACKUP_UNKN -> BACKUP_SRAM
 *     read_backup()  gba_memory.c:457-461   revokes a GUESSED EEPROM
 *     write_backup() gba_memory.c:1134-1137 revokes a GUESSED EEPROM
 *     write_eeprom() gba_memory.c:542       -> BACKUP_EEPROM
 *
 * A probe that called any of them would CAUSE the result it then reported. An
 * UNKNOWN cartridge would read back as SRAM purely because something looked at
 * it. Every function in this header only reads globals, and m11-verify asserts
 * that gba_m11save.o carries no relocation to those four symbols.
 *
 * FOR THE SAME REASON THE FIXTURE MUST NOT reset_gba() AFTER THE OBSERVATION
 * WINDOW. reset_gba() (gba_memory.c:2441) assigns eeprom_size = EEPROM_512_BYTE
 * unconditionally, which DESTROYS a runtime-discovered EEPROM-8K result. M11's
 * two resets both happen before execution, exactly as M10's do.
 *
 * ============================================================================
 * WHAT gpSP ACTUALLY DOES, TRACED READ-ONLY BEFORE ANY OF THIS WAS WRITTEN
 * ============================================================================
 * DETECTION HAS FOUR STATIC TIERS AND THEN NEVER STOPS. All four run inside
 * load_gamepak() (gba_memory.c:2964-3091), in this order:
 *
 *   1 DEFAULTS      :2999-3003  flash_device_id = MACRONIX_64KB,
 *                               flash_bank_cnt  = FLASH_SIZE_64KB,
 *                               backup_type_reset = BACKUP_UNKN
 *
 *   2 DATABASE      :3008       load_game_config_over(game_code) -> :1710-1763.
 *                               FLAGS_EEPROM sets backup_type_reset = EEPROM.
 *                               FLAGS_FLASH_128KB sets flash_bank_cnt AND the
 *                               Sanyo id BUT NOT THE TYPE -- so a 128 KB flash
 *                               entry still falls through to tier 3. Sets
 *                               is_known_game.
 *
 *   3 SIGNATURES    :3010-3014  only if still UNKN. detect_backup_subcircuit():
 *                               a Pokemon-family cartridge with NO signature at
 *                               all short-circuits to FLASH 128 KB (:2918-2925);
 *                               otherwise the first 1 MB is scanned for
 *                               EEPROM_V / SRAM_V / FLASH1M_V / FLASH512_V /
 *                               FLASH_V, and if none hit, the whole RESIDENT ROM
 *                               is swept (:2928). Priority is
 *                               EEPROM -> SRAM -> FLASH1M -> FLASH512/FLASH
 *                               (:2930-2961).
 *
 *   4 HACK OVERRIDE :3040-3052  is_hack = header_nonstandard || !is_known_game
 *                               || title_altered || (size > 16 MB && pokemon).
 *                               If that AND a Pokemon engine, FORCE FLASH 128 KB
 *                               + Sanyo. THIS OVERRIDES TIERS 2 AND 3.
 *
 *   then :3071 normalises an all-zero backup region to 0xFF, and :3073 commits
 *   backup_type = backup_type_reset.
 *
 * reset_gba() RESTORES rather than destroys: :2438 re-applies backup_type from
 * backup_type_reset. So the static verdict survives both of M11's resets.
 *
 * THE FIFTH TIER IS THE BUS, AND IT NEVER STOPS RUNNING.
 *
 *     any backup access while UNKN     :465, :1141   -> SRAM   (SILENT)
 *     access while EEPROM was GUESSED  :457, :1134   -> UNKN -> SRAM
 *     write 0xAA to 0x5555             :1148         -> FLASH
 *     flash command 0xB0               :1186         -> bank_cnt = 128 KB
 *     write_eeprom in base mode        :542          -> EEPROM
 *     DMA3 -> 0x0D, (len & 0x1F) == 17 :841-843      -> eeprom_size = 8 KB
 *
 * A ROM SCAN IS THEREFORE NOT AUTHORITATIVE, and the two most common outcomes
 * are precisely the two that a naive observer would misreport:
 *
 *   BACKUP_SRAM WITH NO SRAM SIGNATURE IS NOT PROOF OF SRAM. It is the value
 *   every UNKNOWN cartridge collapses to the instant anything touches the
 *   backup region. Reporting it as a confident SRAM detection would make every
 *   unexercised cartridge look like a successful identification. M11 reports it
 *   as PROVISIONAL with source RUNTIME FALLBACK and sets M11_R_SRAM_FALLBACK.
 *
 *   eeprom_size == EEPROM_512_BYTE IS A DEFAULT, NOT A MEASUREMENT. It is what
 *   gba_memory.c:531 initialises and what :2441 restores on every reset. Only
 *   the 17-unit DMA at :841-843 ever MEASURES it. M11 reports an unmeasured 512
 *   as PROVISIONAL and sets M11_R_EEPROM_DEFAULT.
 *
 * ============================================================================
 * THE SIZE CONSTANTS ARE UNIT COUNTS, NOT BYTE COUNTS
 * ============================================================================
 * THIS IS THE TRAP IN THIS MILESTONE. gpsp/gba_memory.h:294-298:
 *
 *     FLASH_SIZE_64KB   1      FLASH_SIZE_128KB  2
 *     EEPROM_512_BYTE   1      EEPROM_8_KBYTE   16
 *
 * eeprom_size == 16 MEANS 8192 BYTES. An observer that printed eeprom_size as a
 * byte count would report "16 bytes" for an 8 KB EEPROM, and it would look
 * plausible. Every byte figure this adapter reports is computed by
 * m11save_bytes() from the CLASSIFIED TYPE and never by echoing one of these.
 *
 * ============================================================================
 * THE TYPE RULE, UNCHANGED SINCE M2
 * ============================================================================
 * gpsp/common.h:100 typedefs u64 as `unsigned long long`; runtime/core.h:30
 * typedefs it as `unsigned long`. Both are 64 bits and they are DISTINCT TYPES,
 * so a translation unit including both fails on the duplicate typedef.
 * apps/m11gpsp/main.c therefore never includes a gpSP header, and every
 * declaration below uses ONLY `unsigned int` -- u32 in both worlds. NO POINTER
 * AND NOTHING 64-BIT CROSSES THIS BOUNDARY, exactly as gba_m10map.h:146-154 and
 * gba_exec.h:397 already require.
 *
 * NAMING DISCIPLINE. tools/check_forbidden.py:83-84,109 matches the bare
 * substrings "path_", "retro_", "libretro_", "vfs_", "string_is_",
 * "filestream_", "_stub" and "emit_" against the lowercased symbol name. Every
 * symbol below is m11save_* and contains none of them. NO ALLOWLIST IS WIDENED
 * FOR THIS FILE, and in particular nothing here is named *_path or *_file.
 *
 * THIS FILE IS LINKED INTO M11 ONLY. M0-M10 gain nothing and lose nothing;
 * m11-verify proves no m11save_ symbol resolves in any earlier image, and that
 * adapters/gba/gba_m10map.{c,h} and gba_audio.{c,h} are byte-identical to the
 * frozen M10D evidence.
 */

#ifndef LUAPORT_GBA_M11SAVE_H
#define LUAPORT_GBA_M11SAVE_H

/* ------------------------------------------------------- the result model --
 *
 * THESE ARE gpSP's OWN DISTINCTIONS AND NO OTHERS.
 *
 * THERE IS NO "SAVE_NONE", AND ITS ABSENCE IS A FINDING RATHER THAN AN
 * OVERSIGHT. gpSP has no state meaning "this cartridge has no save hardware":
 * BACKUP_UNKN collapses to BACKUP_SRAM on the first backup-region access
 * (gba_memory.c:465, :1141), so a save-less cartridge and an unexercised one
 * are INDISTINGUISHABLE by construction. Inventing SAVE_NONE would mean
 * inventing a distinction the emulator does not expose, and it would be
 * reported on evidence that cannot exist. UNKNOWN carries that case honestly.
 *
 * The six values below are exactly the six (type, size) pairs gpSP can hold. */
#define M11_SAVE_UNKNOWN      0u   /* BACKUP_UNKN -- nothing decided yet       */
#define M11_SAVE_SRAM         1u   /* BACKUP_SRAM                    32768 B   */
#define M11_SAVE_FLASH64      2u   /* BACKUP_FLASH,  bank_cnt == 1   65536 B   */
#define M11_SAVE_FLASH128     3u   /* BACKUP_FLASH,  bank_cnt == 2  131072 B   */
#define M11_SAVE_EEPROM512    4u   /* BACKUP_EEPROM, size == 1         512 B   */
#define M11_SAVE_EEPROM8K     5u   /* BACKUP_EEPROM, size == 16       8192 B   */
#define M11_SAVE_MAX          5u

/* WHERE THE ANSWER CAME FROM. Derived by replaying gpSP's own tier order
 * against gpSP's own inputs -- see m11save_latch_static(). */
#define M11_SRC_NONE          0u   /* undecided                                */
#define M11_SRC_DB            1u   /* gba_over.h FLAGS_EEPROM, tier 2          */
#define M11_SRC_SIG_HEADER    2u   /* signature in the first 1 MB, tier 3      */
#define M11_SRC_SIG_SWEEP     3u   /* signature found only by the ROM sweep    */
#define M11_SRC_POKEMON       4u   /* the no-signature Pokemon short-circuit   */
#define M11_SRC_HACK          5u   /* the is_hack FLASH-128K override, tier 4  */
#define M11_SRC_RUNTIME       6u   /* the bus proved it during execution       */
#define M11_SRC_FALLBACK      7u   /* the silent UNKN -> SRAM collapse         */
/* THE ONE SOURCE THE PLAN DID NOT ANTICIPATE, AND WHY IT HAS TO EXIST.
 * gbaover[] is `static const` inside gpsp/gba_over.h (:1), which is included
 * ONLY by gba_memory.c. It is therefore NOT LINKABLE and M11 cannot ask whether
 * a matched entry carried FLAGS_EEPROM. When is_known_game is set AND the
 * committed type is EEPROM AND an EEPROM signature also exists, tier 2 and
 * tier 3 would BOTH have produced this answer and the tiers cannot be
 * separated from outside. That is reported as its own value rather than by
 * picking whichever looks tidier. */
#define M11_SRC_DB_OR_SIG     8u
#define M11_SRC_MAX           8u

/* HOW MUCH THE ANSWER IS WORTH. This is the field that stops M11 declaring
 * success on a cartridge that has not saved anything yet. */
#define M11_STATE_UNKNOWN     0u   /* no type at all                           */
#define M11_STATE_PROVISIONAL 1u   /* a type, but the evidence can still be
                                      revoked or the size is still a DEFAULT   */
#define M11_STATE_FINAL       2u   /* the evidence cannot be revoked           */

/* ------------------------------------------------------ the evidence latch --
 *
 * STICKY. Once a bit is set it stays set for the rest of the run, so a
 * transient that happened at second 3 is still visible on the result screen at
 * second 40. Read with m11save_evidence(). */
#define M11_EV_FLASH_CMD    (1u <<  0) /* flash_command_position left 0 -- a
                                          0xAA/0x55 unlock sequence is running */
#define M11_EV_FLASH_ID     (1u <<  1) /* flash_mode reached FLASH_ID_MODE --
                                          the game read the chip id            */
#define M11_EV_FLASH_ERASE  (1u <<  2) /* flash_mode reached FLASH_ERASE_MODE  */
#define M11_EV_FLASH_WRITE  (1u <<  3) /* flash_mode reached FLASH_WRITE_MODE  */
#define M11_EV_FLASH_BANKSW (1u <<  4) /* flash_mode reached BANKSWITCH_MODE   */
#define M11_EV_FLASH_BANK1  (1u <<  5) /* flash_bank_num became 1 -- the upper
                                          64 KB was actually selected          */
#define M11_EV_FLASH_128RT  (1u <<  6) /* flash_bank_cnt went 1 -> 2 AT RUNTIME
                                          -- the 0xB0 command at :1186. THIS is
                                          what PROVES 128 KB, as opposed to
                                          assuming it from the DB or a string   */
#define M11_EV_EEPROM_MODE  (1u <<  7) /* eeprom_mode left EEPROM_BASE_MODE --
                                          real EEPROM traffic occurred         */
#define M11_EV_EEPROM_8K    (1u <<  8) /* eeprom_size went 1 -> 16 -- the
                                          14-bit DMA at :841-843 MEASURED it   */
#define M11_EV_TYPE_CHANGED (1u <<  9) /* backup_type moved after the static
                                          verdict was latched                  */
#define M11_EV_TYPE_REVOKED (1u << 10) /* a GUESSED EEPROM was revoked -- the
                                          :457/:1134 path fired                */
#define M11_EV_BACKUP_DIRTY (1u << 11) /* the backup region's hash changed     */
#define M11_EV_SIG_EEPROM   (1u << 12) /* "EEPROM_V"   in the first 1 MB       */
#define M11_EV_SIG_SRAM     (1u << 13) /* "SRAM_V"     in the first 1 MB       */
#define M11_EV_SIG_FLASH1M  (1u << 14) /* "FLASH1M_V"  in the first 1 MB       */
#define M11_EV_SIG_FLASH5   (1u << 15) /* "FLASH512_V" or "FLASH_V", first 1MB */
#define M11_EV_SWP_EEPROM   (1u << 16) /* the same four, found ONLY by the     */
#define M11_EV_SWP_SRAM     (1u << 17) /* resident-buffer sweep -- see the     */
#define M11_EV_SWP_FLASH1M  (1u << 18) /* SWEEP note at the top of this file   */
#define M11_EV_SWP_FLASH5   (1u << 19)
#define M11_EV_POKEMON      (1u << 20) /* rom_is_pokemon_family() is true      */
#define M11_EV_KNOWN_GAME   (1u << 21) /* is_known_game -- gba_over.h matched  */
#define M11_EV_EEP_GUESSED  (1u << 22) /* DERIVED, NOT READ: the conditions of
                                          gba_memory.c:2934 held, so gpSP's own
                                          backup_type_eeprom_guessed would be 1*/
#define M11_EV_EEPROM_RT    (1u << 23) /* backup_type ARRIVED AT BACKUP_EEPROM
                                          at runtime, having been latched as
                                          something else.
                                          WHY THIS IS POSITIVE BUS EVIDENCE:
                                          gba_memory.c has exactly ONE runtime
                                          producer of BACKUP_EEPROM -- the
                                          assignment at :542, which lives INSIDE
                                          write_eeprom(). Every other writer of
                                          backup_type produces UNKN (:407, :461,
                                          :1137), SRAM (:465, :1141), FLASH
                                          (:1148), or copy backup_type_reset at
                                          :2438/:3073. So this transition CANNOT
                                          have occurred without a 16-bit store
                                          to 0x0D reaching write_eeprom().
                                          WHAT IT DOES NOT PROVE: :542 fires on
                                          the FIRST BIT of ANY command, read or
                                          write, so this is EEPROM TRAFFIC, not
                                          a completed write, and it says nothing
                                          about the chip's SIZE.
                                          This exists because M11_EV_EEPROM_MODE
                                          requires catching eeprom_mode away
                                          from BASE_MODE AT a frame boundary,
                                          and a whole EEPROM DMA completes well
                                          inside one frame -- the transaction
                                          resets the mode at :603. That transient
                                          is routinely missed; this latch is
                                          not.                                  */
#define M11_EV_BITS         24u

/* ------------------------------------------------ FATAL bits, m11save_probe --
 *
 * A SET BIT IS A FAILED ASSERTION, identical in polarity to M2-M10, so success
 * is a single `== 0` test and a newly added assertion cannot read as "pass".
 *
 * THESE ASSERT THE COHERENCE OF gpSP's SAVE STATE, NOT THE OUTCOME OF
 * DETECTION. An UNKNOWN save type is NOT a failure and has no bit here -- it is
 * a legitimate observation about a cartridge that has not saved yet, and that
 * is exactly what M11_STATE_* is for. What is fatal is state that no correct
 * execution of gpSP could have produced. */
#define M11SAVE_NOLATCH    (1u << 0)  /* the static verdict was never latched --
                                         every derived field would be garbage  */
#define M11SAVE_NOROM      (1u << 1)  /* gamepak_size == 0 / no buffer         */
#define M11SAVE_BADTYPE    (1u << 2)  /* backup_type > BACKUP_UNKN             */
#define M11SAVE_BADBANKCNT (1u << 3)  /* flash_bank_cnt is neither 1 nor 2     */
#define M11SAVE_BADBANKNUM (1u << 4)  /* flash_bank_num > 1                    */
#define M11SAVE_BANKOOB    (1u << 5)  /* bank 1 selected on a 64 KB chip -- the
                                         :502/:1238 fulladdr would run past the
                                         64 KB the chip actually has           */
#define M11SAVE_BADEEPSIZE (1u << 6)  /* eeprom_size is neither 1 nor 16       */
#define M11SAVE_BADFLMODE  (1u << 7)  /* flash_mode > FLASH_BANKSWITCH_MODE    */
#define M11SAVE_BADEEPMODE (1u << 8)  /* eeprom_mode > WRITE_FOOTER_MODE       */
#define M11SAVE_BADCMDPOS  (1u << 9)  /* flash_command_position > 2            */
#define M11SAVE_SIZE0     (1u << 10)  /* a classified type reported 0 bytes    */
#define M11SAVE_OVERRUN   (1u << 11)  /* the active region exceeds the 128 KB
                                         gamepak_backup[] actually has         */

/* ------------------------------------------ REPORT-ONLY bits, m11save_report --
 *
 * These describe the CARTRIDGE or the LIMITS OF THE EVIDENCE. They are printed
 * and published and THEY DO NOT FAIL M11. */
#define M11_R_SRAM_FALLBACK  (1u << 0) /* SRAM with no SRAM signature anywhere:
                                          this is the silent UNKN -> SRAM
                                          collapse and NOT a positive result   */
#define M11_R_EEPROM_DEFAULT (1u << 1) /* EEPROM sized 512 B by DEFAULT -- the
                                          14-bit DMA never ran                 */
#define M11_R_SWEEP_PARTIAL  (1u << 2) /* the sweep saw only the resident part
                                          of a demand-paged cartridge          */
#define M11_R_SRC_AMBIG      (1u << 3) /* tier 2 and tier 3 both explain this
                                          answer -- see M11_SRC_DB_OR_SIG      */
#define M11_R_HACK           (1u << 4) /* the is_hack FLASH-128K override fired*/
#define M11_R_TYPE_MOVED     (1u << 5) /* the runtime type differs from the
                                          latched static one                   */
#define M11_R_BLANK          (1u << 6) /* the active region is still entirely
                                          0xFF -- nothing has been saved       */
#define M11_R_NOSIG          (1u << 7) /* no signature at all, header or sweep */
#define M11_R_PAGED          (1u << 8) /* gamepak_must_swap() -- the cartridge
                                          is demand paged, so the sweep result
                                          is CACHE-DEPENDENT                   */

/* --------------------------------------------------------------- the calls -- */

/* LATCH THE STATIC VERDICT. Call ONCE, IMMEDIATELY AFTER load_gamepak()
 * RETURNS AND BEFORE ANY reset_gba() OR ANY EXECUTION.
 *
 * WHY THE ORDERING IS LOAD-BEARING. This is the only instant at which the
 * emulator's save state is purely the product of the four static tiers, with no
 * bus activity mixed in. Everything this function records -- the committed
 * type, the flash geometry, the signature masks, the Pokemon and known-game
 * flags -- is the BASELINE that every later observation is diffed against. Call
 * it late and a runtime transition is silently absorbed into the "static"
 * answer; call it after a reset and nothing breaks (reset_gba() restores
 * backup_type from backup_type_reset at :2438) but the EEPROM size baseline is
 * re-defaulted, which is why M11 latches before the reset rather than after.
 *
 * IT RE-RUNS THE SIGNATURE SCANS ITSELF. That is the whole mechanism by which
 * the two `static` flags are recovered without touching gpSP: the same five
 * literals, the same 1 MB header window, the same resident-buffer sweep, and
 * then a comparison against what gpSP actually committed.
 *
 * PURELY READ-ONLY. It cannot change any emulator state and cannot cause a page
 * fault -- it walks only buffers that gamepak_buffer_count already says are
 * resident. */
void m11save_latch_static(void);

/* THE PER-FRAME SAMPLE. Call once per emulated frame, AFTER the frame has run.
 * Updates the sticky evidence latch and the backup-region hash. Cheap: a fixed
 * scan of eleven scalars plus one hash pass over the ACTIVE region only.
 *
 * PASSIVE. Reads globals and gamepak_backup[] bytes. Calls nothing. */
void m11save_snapshot(void);

/* THE FATAL PROBE. Returns a mask of M11SAVE_* bits; 0 means gpSP's save state
 * is internally coherent. AN UNKNOWN SAVE TYPE RETURNS 0 -- not knowing is not
 * a failure. */
unsigned int m11save_probe(void);

/* THE REPORT-ONLY OBSERVATIONS. Returns a mask of M11_R_* bits. Never fatal.
 * Safe to call when m11save_probe() failed. */
unsigned int m11save_report(void);

/* THE ANSWER. One of M11_SAVE_*. Derived from backup_type together with
 * flash_bank_cnt / eeprom_size -- never from backup_type alone, because
 * BACKUP_FLASH and BACKUP_EEPROM each cover two different sizes. */
unsigned int m11save_type(void);

/* THE SIZE IN BYTES. Computed from the classified type, NEVER by echoing
 * eeprom_size or flash_bank_cnt -- those are UNIT COUNTS. Returns 0 for
 * M11_SAVE_UNKNOWN, which is the honest answer for a type that is not known. */
unsigned int m11save_bytes(void);

/* WHERE THE ANSWER CAME FROM. One of M11_SRC_*. */
unsigned int m11save_source(void);

/* HOW MUCH IT IS WORTH. One of M11_STATE_*. See the FINAL/PROVISIONAL contract
 * in gba_m11save.c -- this is the function that refuses to call a default a
 * measurement. */
unsigned int m11save_state(void);

/* THE STICKY EVIDENCE MASK, M11_EV_* bits. */
unsigned int m11save_evidence(void);

/* One raw value at a time, for the log and for ext->dbg[]. Returns 0 for an
 * unknown selector. */
unsigned int m11save_read(unsigned int sel);

/* ---------------------------------------------- m11save_read selectors ---- */
#define M11_RD_TYPE            0u  /* m11save_type()                          */
#define M11_RD_BYTES           1u  /* m11save_bytes()                         */
#define M11_RD_SOURCE          2u  /* m11save_source()                        */
#define M11_RD_STATE           3u  /* m11save_state()                         */
#define M11_RD_BACKUP_TYPE     4u  /* backup_type            RAW              */
#define M11_RD_BACKUP_RESET    5u  /* backup_type_reset      RAW              */
#define M11_RD_FLASH_MODE      6u  /* flash_mode             RAW              */
#define M11_RD_FLASH_CMDPOS    7u  /* flash_command_position RAW              */
#define M11_RD_FLASH_BANKNUM   8u  /* flash_bank_num         RAW              */
#define M11_RD_FLASH_BANKCNT   9u  /* flash_bank_cnt         RAW (1 or 2)     */
#define M11_RD_FLASH_DEVID    10u  /* flash_device_id        RAW              */
#define M11_RD_FLASH_MANUF    11u  /* the manufacturer byte read_backup would
                                      return in FLASH_ID_MODE at address 0 --
                                      DERIVED from :471-497, not a bus read   */
#define M11_RD_EEPROM_MODE    12u  /* eeprom_mode            RAW              */
#define M11_RD_EEPROM_SIZE    13u  /* eeprom_size            RAW (1 or 16)    */
#define M11_RD_EEPROM_ADDR    14u  /* eeprom_address         RAW              */
#define M11_RD_EEPROM_COUNT   15u  /* eeprom_counter         RAW              */
#define M11_RD_EEPROM_BITS    16u  /* the ADDRESS WIDTH the counter implies:
                                      6 for 512 B, 14 for 8 KB (:565)        */
#define M11_RD_STATIC_TYPE    17u  /* the type latched before execution       */
#define M11_RD_STATIC_SOURCE  18u  /* the source latched before execution     */
#define M11_RD_STATIC_BYTES   19u  /* the size latched before execution       */
#define M11_RD_EVIDENCE       20u  /* m11save_evidence()                      */
#define M11_RD_SIG_HEADER     21u  /* the four header-scan bits, packed       */
#define M11_RD_SIG_SWEEP      22u  /* the four sweep bits, packed             */
#define M11_RD_SWEEP_BYTES    23u  /* bytes the sweep actually examined       */
#define M11_RD_SWEEP_TOTAL    24u  /* gamepak_size -- the honest denominator  */
#define M11_RD_HASH           25u  /* FNV-1a of the ACTIVE backup region      */
#define M11_RD_HASH_FIRST     26u  /* the same hash at latch time             */
#define M11_RD_DIRTY_BLOCKS   27u  /* 1 KB blocks whose hash ever changed     */
#define M11_RD_FIRST_BLOCK    28u  /* first block that changed, or NOBLOCK    */
#define M11_RD_WRITE_FRAMES   29u  /* frames on which the region changed      */
#define M11_RD_NONFF          30u  /* bytes in the active region that are not
                                      0xFF -- 0 means nothing was ever saved  */
#define M11_RD_KNOWN_GAME     31u  /* is_known_game                           */
#define M11_RD_POKEMON        32u  /* rom_is_pokemon_family()                 */
#define M11_RD_HDR_NONSTD     33u  /* gamepak_header_nonstandard              */
#define M11_RD_SNAPSHOTS      34u  /* how many times m11save_snapshot() ran   */
#define M11_RD_PROBE          35u  /* the last m11save_probe() mask           */
#define M11_RD_REPORT         36u  /* the last m11save_report() mask          */

/* Returned by M11_RD_FIRST_BLOCK when no block has changed. */
#define M11_NOBLOCK       0xFFFFFFFFu

/* THE GEOMETRY OF THE DIRTY MAP. gamepak_backup[] is 131,072 bytes
 * (gba_memory.c:360) and is covered by 128 blocks of 1,024 bytes. 128 u32
 * hashes is 512 bytes of .bss -- as against a 128 KB shadow copy, which would
 * consume a fifth of the remaining .bss headroom to answer the same question. */
#define M11_BACKUP_BYTES  131072u
#define M11_BLOCKS           128u
#define M11_BLOCK_BYTES  (M11_BACKUP_BYTES / M11_BLOCKS)   /* 1024 */

#endif /* LUAPORT_GBA_M11SAVE_H */
