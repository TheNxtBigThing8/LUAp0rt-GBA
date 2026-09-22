/* LUAport M16-0 -- adapters/gba/gba_session.c
 *
 * THE GBA SESSION BOUNDARY. See gba_session.h for why this layer exists, what
 * it deliberately is not, and the five once-per-process latches it converts
 * into once-per-session ones.
 *
 * =========================================================================
 * THIS FILE ADDS NO NEW MECHANISM. IT ORDERS EXISTING, PROVEN ONES.
 * =========================================================================
 *
 * Every call below already existed and every one of them has hardware evidence
 * behind it from an earlier milestone. Exactly two functions in the whole of
 * M16-0 are genuinely new -- gba_save_unlatch() and gba_restore_disarm() -- and
 * both are three-line .bss clears that live in the files owning the state,
 * because no existing API could clear it: gba_save.c has no writer of 0 to
 * gs_latched and gba_restore.c has no writer of 0 to gr_armed.
 *
 *   m8_input_apply()      M8   keypad adapter
 *   gba_audio_reset()     M10  ring, resampler and gpSP mix bus
 *   memory_term()         gpSP's OWN cleanup, gba_memory.c:2456
 *   gba_save_unlatch()    M16  NEW -- 3-line .bss clear in gba_save.c
 *   gba_restore_disarm()  M16  NEW -- 3-line .bss clear in gba_restore.c
 *   m12c_sramobs_reset()  M12C observation slots
 *   m10map_paging_reset() M10  paging observer baseline
 *
 * ***** NOT ONE LINE OF gpsp/ IS MODIFIED. ***** memory_term() is gpSP's own
 * teardown entry point. It is declared at gpsp/gba_memory.h:264, it is already
 * LINKED INTO M13C's frozen image, and it has simply never been called because
 * a one-cartridge launcher has no reason to call it. M16-0 calls it.
 */

#include "gba_session.h"

#include "gba_save.h"      /* gba_save_latched(), gba_save_unlatch()          */
#include "gba_restore.h"   /* gba_restore_read(), gba_restore_disarm()        */
#include "gba_sramobs.h"   /* m12c_sramobs_reset(), m12c_sramobs_read()       */
#include "gba_input.h"     /* m8_input_apply(), m8_input_read_reg_p1()        */
#include "gba_audio.h"     /* gba_audio_reset()                               */
#include "gba_m10map.h"    /* m10map_paging_reset(), m10map_read()            */

/* gba_probe.h / gba_bios.h / gba_rom.h arrive through gba_session.h. */

/* ------------------------------------------------ hand-declared gpSP entry --
 *
 * memory_term() is declared in gpsp/gba_memory.h, but including that header
 * would drag in gpsp/common.h and with it gpSP's type universe -- where u64 is
 * `unsigned long long` while runtime/core.h makes it `unsigned long`. Those are
 * DISTINCT TYPES, and the Makefile's gpSP-facing adapter rules exist precisely
 * because a translation unit that saw both would not compile.
 *
 * Declaring the one symbol by hand avoids the whole question and costs nothing
 * in safety: memory_term takes no arguments and returns nothing, so there is no
 * type to get wrong and no ABI detail to guess. THIS IS THE PROJECT'S OWN
 * ESTABLISHED TECHNIQUE for reaching a gpSP symbol that has no usable header --
 * adapters/gba/gba_save.c:51-59 does the same for gamepak_buffers[] and
 * gamepak_buffer_blocksize, citing gpsp/gba_memory.c:198 as upstream's own
 * precedent for it.
 *
 * WHAT IT DOES, from gba_memory.c:2456-2475, read rather than assumed:
 *   - filestream_close(gamepak_file_large) and NULLs it   -- THE HANDLE
 *   - walks gamepak_buffer_count down to 0, free()ing each buffer
 *   - frees gamepak_mini_rom and clears gamepak_mini_materialized
 *
 * The free() calls reclaim nothing on their own -- runtime/shim.c's free() is a
 * no-op -- which is exactly why the controller must follow this with
 * arena_rewind(). What memory_term() uniquely provides, and what nothing else
 * in the codebase provides, is CLOSING THE FILE and ZEROING THE BUFFER COUNT. */
void memory_term(void);

/* ============================================================== teardown == */

void gba_session_end(void)
{
    /* ---- 1: THE KEYPAD, FIRST ------------------------------------------
     *
     * REG_P1 back to the neutral 0x3FF (active low: all released). Done first
     * and deliberately so: it is the only step that writes emulated I/O state,
     * and doing it while the memory map is still coherent avoids any question
     * about writing a register through a window that step 3 is about to
     * invalidate.
     *
     * It also means a cartridge cannot leave a button latched into the next
     * session -- which would present as "the menu is scrolling by itself". */
    m8_input_apply(0u);

    /* ---- 2: AUDIO -- THE RING, NEVER THE PORT --------------------------
     *
     * gba_audio_reset() clears this adapter's ring and resampler accumulator
     * AND calls gpSP's sound_flush_ring(), so the next session does not begin
     * on samples the previous cartridge left behind.
     *
     * ***** plat_audio_shutdown() IS NOT CALLED AND MUST NOT BE. ***** That is
     * the RUNTIME-WIDE teardown: it really closes the sceAudioOut port. Closing
     * and reopening a port per cartridge would add a new failure mode -- a port
     * that fails to reopen ends the payload -- in exchange for nothing at all.
     * The port is a property of the process; the ring is a property of the
     * session. Only the session-scoped half is reset here. */
    gba_audio_reset();

    /* ---- 3: THE CORE -- THIS IS THE STEP THAT CLOSES THE ROM FILE ------
     *
     * ***** THE ONLY CALL IN LUAport THAT RELEASES THE GAMEPAK. ***** Without
     * it the demand-paging handle stays open for the life of the payload and
     * burns one RFILE pool slot of four per launch, and gamepak_buffer_count
     * stays at 2 so the next m4_rom_buffer_init() would try to allocate ON TOP
     * of buffers that were never released.
     *
     * ***** IT MUST PRECEDE THE CONTROLLER'S arena_rewind(). ***** The rewind
     * makes those 2 MB available for reallocation. Any gpSP pointer still
     * referencing them afterwards would be a use-after-free the moment the next
     * session allocates. memory_term() is what guarantees gpSP is no longer
     * holding them. */
    memory_term();

    /* ---- 4: THE SAVE IDENTITY -- STRICTLY AFTER THE COMMIT -------------
     *
     * ***** THE MOST DANGEROUS ORDERING CONSTRAINT IN M16. ***** The commit
     * path builds "/savedata0/gba/<ID>.sav" from the identity this destroys. If
     * a controller ever calls gba_session_end() before committing, the commit
     * does not silently misfile -- it refuses with GBA_SAVEFILE_ENOID -- but
     * the save is still lost. The commit belongs in the controller, before this
     * call, exactly where M13C already has it.
     *
     * M16-0 itself COMMITS NOTHING, so the constraint is documented and proven
     * by the unlatch measurement rather than exercised here. */
    gba_save_unlatch();

    /* ---- 5: THE RESTORE MODULE -> PRE-ARM ------------------------------
     *
     * Clears gr_armed plus the per-session measurements. Nothing else in
     * gba_restore.c ever stores 0 to gr_armed, and the passivity guarantee of
     * the validate-only pass rests entirely on gba_restore_apply() refusing
     * while disarmed. Leaving it set would let the NEXT session's read-only
     * probe write gamepak_backup[]. */
    gba_restore_disarm();

    /* ---- 6 and 7: THE TWO OBSERVERS ------------------------------------
     *
     * Pure measurement state, both with an existing reset. They are cleared
     * because a T1/T2/T3 sample or a paging baseline that describes a cartridge
     * which is no longer loaded is worse than no measurement at all: it reads
     * as a real figure. */
    m12c_sramobs_reset();
    m10map_paging_reset();

    /* ***** DELIBERATELY ABSENT FROM THIS FUNCTION. *****
     *
     *   the save commit        the CONTROLLER's, strictly BEFORE this call
     *   arena_rewind()         the CONTROLLER's, strictly AFTER this call --
     *                          the arena is a RUNTIME service and this file
     *                          must not know it exists
     *   m4_rom_backup_init()   belongs to session START, not session END. The
     *                          0xFF fill must sit immediately before the next
     *                          load so no window exists in which a stale
     *                          cartridge's save bytes could be restored over.
     *   plat_video_shutdown()  runtime-wide -- the menu needs the framebuffer
     *   plat_pad_shutdown()    runtime-wide -- the menu needs input
     *   plat_audio_shutdown()  runtime-wide -- see step 2
     *   reset_gba()            belongs to session START, where M13C has it
     */
}

/* ==================================================== relaunch invariants == */

unsigned int gba_session_probe_relaunch(void)
{
    unsigned int f = 0;

    /* THE ARENA-FACING PAIR. Both must be clear before the next
     * m4_rom_buffer_init(), and both are what memory_term() is responsible for.
     * A surviving buffer count means the next allocation would stack on top of
     * the old one; a surviving RFILE slot is the handle leak that only becomes
     * fatal on the fifth launch. */
    if (m4_rom_read(M4_RD_BUFFER_COUNT) != 0u)
        f |= GBA_SESS_R_BUFCOUNT;

    if (gba_rfile_inuse() != 0u)
        f |= GBA_SESS_R_RFILE;

    /* THE SAVE-SAFETY PAIR. These two are the difference between a correct
     * relaunch and silent data loss, so they are checked rather than trusted.
     *
     * ***** A SET GBA_SESS_R_LATCHED BIT MEANS THE NEXT CARTRIDGE WOULD COMMIT
     * INTO THIS ONE'S SAVE FILE. ***** The controller must treat it as fatal
     * and must not start another session. */
    if (gba_save_latched() != 0u)
        f |= GBA_SESS_R_LATCHED;

    if (gba_restore_read(GBA_RESTORE_RD_ARMED) != 0u)
        f |= GBA_SESS_R_ARMED;

    /* THE RESTORE MEASUREMENTS. Checked separately from the arm flag so that a
     * partial disarm is distinguishable from no disarm at all. */
    if (gba_restore_read(GBA_RESTORE_RD_BASEHASH) != 0u)
        f |= GBA_SESS_R_BASEHASH;

    if (gba_restore_read(GBA_RESTORE_RD_APPLIED) != 0u)
        f |= GBA_SESS_R_APPLIED;

    /* INPUT. Active low, so neutral is all bits SET. */
    if (m8_input_read_reg_p1() != GBA_SESS_P1_NEUTRAL)
        f |= GBA_SESS_R_INPUT;

    /* THE OBSERVER. Any slot still marked taken means the reset did not run. */
    if (m12c_sramobs_read(GBA_SRAMOBS_RD_SAMPLES) != 0u)
        f |= GBA_SESS_R_SRAMOBS;

    /* THE MINI-ROM PATH. memory_term() clears gamepak_mini_materialized; a
     * surviving flag would make the next load take the 1 MB mirror branch on a
     * cartridge that is not mirrored. Read through M4's accessor rather than by
     * declaring gpSP's bool, so no type crosses the boundary. */
    if (m4_rom_read(M4_RD_MINI) != 0u)
        f |= GBA_SESS_R_MINI;

    /* ***** DELIBERATELY NOT ASSERTED HERE: gamepak_size AND THE CARTRIDGE
     * WINDOW. ***** Both legitimately survive a correct teardown -- see the
     * derivation at GBA_SESS_PRE4_RELAUNCH in the header. They are not ignored:
     * they are accounted for by the controller's EXACT-EQUALITY comparison
     * against that mask, which is a stricter test than omitting them would be,
     * because any OTHER bit appearing there still fails. */

    return f;
}

/* =========================================================== scalar reads == */

unsigned int gba_session_read(unsigned int sel)
{
    switch (sel) {
        case GBA_SESS_RD_BUFCOUNT:    return m4_rom_read(M4_RD_BUFFER_COUNT);
        case GBA_SESS_RD_RFILE_INUSE: return gba_rfile_inuse();
        case GBA_SESS_RD_RFILE_SLOTS: return gba_rfile_slots();
        case GBA_SESS_RD_LATCHED:     return gba_save_latched();
        case GBA_SESS_RD_ARMED:       return gba_restore_read(GBA_RESTORE_RD_ARMED);
        case GBA_SESS_RD_REG_P1:      return m8_input_read_reg_p1();
        case GBA_SESS_RD_GAMEPAK_SZ:  return m4_rom_read(M4_RD_SIZE);
        case GBA_SESS_RD_MINI:        return m4_rom_read(M4_RD_MINI);
        case GBA_SESS_RD_RESIDENT:    return m10map_read(M10MAP_RD_RESIDENT);
        case GBA_SESS_RD_FILEBLOCKS:  return m10map_read(M10MAP_RD_FILEBLOCKS);
        case GBA_SESS_RD_LRUHEAD:     return m10map_read(M10MAP_RD_LRUHEAD);
        case GBA_SESS_RD_SRAMOBS:     return m12c_sramobs_read(GBA_SRAMOBS_RD_SAMPLES);
        case GBA_SESS_RD_BASEHASH:    return gba_restore_read(GBA_RESTORE_RD_BASEHASH);
        case GBA_SESS_RD_APPLIED:     return gba_restore_read(GBA_RESTORE_RD_APPLIED);
        default:                      return 0u;
    }
}
