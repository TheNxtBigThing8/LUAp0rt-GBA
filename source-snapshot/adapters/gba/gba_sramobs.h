/* LUAport M12C -- adapters/gba/gba_sramobs.h
 *
 * THE SRAM UPPER-HALF OBSERVER. READ-ONLY, AND THAT IS ITS ENTIRE CONTRACT.
 *
 * ===========================================================================
 * WHY THIS FILE EXISTS
 * ===========================================================================
 *
 * M12C can now prove, on hardware, that a persisted Metroid Fusion save travels
 * correctly from disk into gpSP:
 *
 *     HDR VALID / CLASS SRAM 32K / ROM HASH MATCH / SAV HASH MATCH
 *     RESTORE 32768 BYTES / BASELINE RESTORED / PASS -- SAVE RESTORED
 *
 * and the game STILL shows no save data. Every gate upstream of the emulator
 * passes, so the break is downstream of them.
 *
 * Source analysis of the FROZEN gpsp/gba_memory.c produced one candidate that
 * fits every observation at once:
 *
 *     read_backup8/16/32   (gba_memory.c:509-519)  value = read_backup(address & 0xFFFF)
 *     write_backup8        (gba_memory.c:1252)     write_backup(address & 0xFFFF, value)
 *
 * BOTH MASK TO 0xFFFF AND INDEX gamepak_backup[] DIRECTLY. There is no `&
 * 0x7FFF` anywhere on the SRAM path. gpSP therefore exposes a 65,536-byte
 * LINEAR SRAM window, and it does NOT mirror 0x0000-0x7FFF into 0x8000-0xFFFF
 * the way a real 32 KB SRAM chip -- which decodes only A0..A14 -- would.
 *
 * M12C persists and restores 32,768 bytes (gba_save.c's gs_region_of(SRAM)).
 * So after a restore:
 *
 *     0x0E000000-0x0E007FFF  ->  gamepak_backup[0x0000..0x7FFF]  restored bytes
 *     0x0E008000-0x0E00FFFF  ->  gamepak_backup[0x8000..0xFFFF]  STILL 0xFF
 *
 * Within ONE session that is invisible: the game writes the upper half and
 * reads back exactly what it wrote. ACROSS sessions the upper half is lost,
 * which is precisely the reported symptom -- a save that works while playing
 * and is gone after a relaunch.
 *
 * ***** THAT IS A HYPOTHESIS, NOT A FINDING. ***** It is only true if Metroid
 * actually stores save-critical bytes above 0x7FFF. Nothing in the tree can
 * answer that, because the ROM is not in the workspace and gba_over.h does not
 * list the game. THIS OBSERVER EXISTS TO MEASURE IT INSTEAD OF ASSUMING IT.
 *
 * ===========================================================================
 * WHAT THIS OBSERVER IS NOT
 * ===========================================================================
 *
 * IT DOES NOT FIX ANYTHING AND IT MUST NOT. It changes no persistence size, no
 * region, no header, no restore policy and no gpSP global. A run with this
 * observer compiled in must behave EXACTLY as the run without it, byte for
 * byte, or the measurement is worthless -- an observer that perturbs the thing
 * it observes cannot settle the question it was written to settle.
 *
 * IT NEVER CALLS read_backup(), write_backup(), read_eeprom() OR
 * write_eeprom(). This is the same prohibition gba_restore.c carries, for the
 * same reason: all four MUTATE backup_type (gba_memory.c:464-465, :1140-1141,
 * :542). Calling one to "look at" the save would MANUFACTURE the very
 * classification M12C reports, and the observer would be reporting its own
 * side effect. m12c-verify asserts the absence of all four from
 * gba_sramobs.o mechanically.
 *
 * IT TOUCHES gamepak_backup[] THROUGH A const POINTER ONLY. The array is a
 * plain static and is never demand paged, so no load_gamepak_page() call is
 * needed or made.
 *
 * No malloc. No file I/O. No PS5/platform call. No savedata call. No logging
 * dependency. All state is fixed-size .bss and totals well under 128 bytes.
 * NOT ONE BYTE OF gpsp/ IS MODIFIED.
 *
 * ===========================================================================
 * THE THREE SAMPLES
 * ===========================================================================
 *
 *   T1  IMMEDIATELY AFTER THE RESTORE IS APPLIED, after M12C's own baseline
 *       hash has been taken. This is the state the game is about to boot into.
 *
 *   T2  IMMEDIATELY BEFORE THE FIRST m5_exec_run(), i.e. before ONE emulated
 *       instruction has executed. T1 and T2 must be identical; if they are
 *       not, something between the restore and execution is touching the save
 *       array and THAT is the bug, not the region size.
 *
 *   T3  AT CLEAN SESSION END, after the frame loop and after
 *       gba_restore_finish(), before the result screen is drawn.
 *
 * T2 rather than T1 is the comparison base for "did the game change this",
 * because only T2->T3 is a window in which emulated cartridge code ran.
 *
 * ===========================================================================
 * READING THE ANSWER
 * ===========================================================================
 *
 *   BIT 2 SET   The game changed bytes in gamepak_backup[32768..65535].
 *               Those bytes are inside gpSP's SRAM window and OUTSIDE the
 *               region M12C persists, so they are lost on exit. This is
 *               STRONG EVIDENCE for the 64 KB-window hypothesis.
 *
 *   BIT 2 CLEAR
 *   BIT 3 SET   The game used the save, but stayed inside the persisted
 *               32 KB. The hypothesis is REFUTED and widening the region
 *               would fix nothing. STOP -- do not change the save size.
 *
 *   BIT 1 SET   The upper half was already non-0xFF BEFORE execution. The
 *               restore/init ordering is wrong and must be investigated
 *               before any conclusion is drawn about region size, because
 *               the T2->T3 comparison no longer starts from a known state.
 *
 * ***** NONE OF THESE BITS PASS OR FAIL M12C. ***** This is an OBSERVATION,
 * and a milestone that failed because a game happens not to use its upper
 * 32 KB would be reporting the fixture's expectations rather than the
 * cartridge's behaviour.
 */

#ifndef GBA_SRAMOBS_H
#define GBA_SRAMOBS_H

/* ---- GEOMETRY --------------------------------------------------------------
 *
 * These describe gpSP's SRAM WINDOW, which is NOT the same thing as M12C's
 * persisted region and must never be conflated with it. The window is what
 * `address & 0xFFFF` can reach; the region is what gba_save.c writes to disk.
 * The whole point of this observer is that today they DIFFER. */
#define GBA_SRAMOBS_HALF_BYTES    32768u   /* one half of the window           */
#define GBA_SRAMOBS_LOW_OFF           0u   /* persisted today                  */
#define GBA_SRAMOBS_HIGH_OFF      32768u   /* NOT persisted today              */
#define GBA_SRAMOBS_WINDOW_BYTES  65536u   /* gpSP's `address & 0xFFFF` span   */

/* ---- THE SAMPLE SLOTS ---------------------------------------------------- */
#define GBA_SRAMOBS_T1            0u   /* after restore                        */
#define GBA_SRAMOBS_T2            1u   /* before the first emulated instruction */
#define GBA_SRAMOBS_T3            2u   /* at clean session end                 */
#define GBA_SRAMOBS_SLOTS         3u

/* ---- THE INTERPRETATION BITS --------------------------------------------- */
#define GBA_SRAMOBS_B_HIGH_NONFF_T3  (1u << 0)
#define GBA_SRAMOBS_B_HIGH_NONFF_T1  (1u << 1)
#define GBA_SRAMOBS_B_HIGH_CHANGED   (1u << 2)  /* T2 -> T3, THE ANSWER        */
#define GBA_SRAMOBS_B_LOW_CHANGED    (1u << 3)  /* T2 -> T3                    */

/* ---- THE READ SELECTORS ----------------------------------------------------
 *
 * A SCALAR-ONLY ACCESSOR. No pointer into gamepak_backup[] -- or into this
 * module's own state -- ever crosses the adapter boundary, which is the same
 * rule gba_restore_read() follows. The per-slot selectors are BASE + slot, so
 * GBA_SRAMOBS_RD_LOW_FNV + GBA_SRAMOBS_T3 reads the low half's hash at T3. */
#define GBA_SRAMOBS_RD_LOW_FNV      0u   /* + slot */
#define GBA_SRAMOBS_RD_HIGH_FNV     4u   /* + slot */
#define GBA_SRAMOBS_RD_LOW_NONFF    8u   /* + slot */
#define GBA_SRAMOBS_RD_HIGH_NONFF  12u   /* + slot */
#define GBA_SRAMOBS_RD_TAKEN       16u   /* + slot -> 0 or 1                   */
#define GBA_SRAMOBS_RD_BITS        20u   /* the four bits above                */
#define GBA_SRAMOBS_RD_SAMPLES     21u   /* how many samples were taken        */

/* ---- THE API ---------------------------------------------------------------
 *
 * Boundary types are `unsigned int` throughout, exactly as gba_restore.h does,
 * so that this header stays includable from a translation unit that has never
 * seen gpSP's u8/u32 typedefs. */

/* Clear every slot. Safe to call more than once; MUST be called before T1. */
void m12c_sramobs_reset(void);

/* Hash and count both halves of gpSP's SRAM window into `slot`. Out-of-range
 * slots are IGNORED rather than clamped -- silently folding a bad slot onto a
 * good one would overwrite a real measurement with a later one. */
void m12c_sramobs_sample(unsigned int slot);

/* Scalar read-back. An unknown selector returns 0. */
unsigned int m12c_sramobs_read(unsigned int sel);

#endif /* GBA_SRAMOBS_H */
