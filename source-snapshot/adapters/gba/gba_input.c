/* LUAport M8 -- adapters/gba/gba_input.c
 *
 * The gpSP-facing half of the M8 input milestone, and the ONLY place in the
 * entire LUAport tree that writes REG_P1.
 *
 * See adapters/gba/gba_input.h for the full derivation: the register address,
 * the ten key bits, the active-low convention, the reset value, the finding
 * that update_input() is linked but never called, and the argument for why the
 * keypad-IRQ replication below is inert unless the cartridge itself asks for
 * it. This file is the implementation only.
 *
 * apps/m8gpsp/main.c sits on the other side of the type boundary (runtime/
 * core.h, runtime/platform.h, shim.h, boot.inc) and cannot include a gpSP
 * header: gpsp/common.h:100 typedefs u64 as `unsigned long long` while
 * runtime/core.h:30 uses `unsigned long`, and a TU including both fails on the
 * duplicate typedef. Everything gpSP-shaped stops here.
 *
 * NO PS5 API IS CALLED FROM THIS FILE, and none may ever be. It never sees a
 * scePad handle, never includes runtime/platform.h, and receives the pad state
 * as a plain `unsigned int` of raw button bits. That separation is what lets
 * `make m8-verify` prove on the CALL GRAPH that the pad layer is reached from
 * m8main.o and from nowhere else, and that this object depends on gpSP and on
 * nothing platform-shaped.
 *
 * gpsp/ IS NOT MODIFIED. Not one line. Every symbol touched below is declared
 * in a gpSP header that common.h already pulls in:
 *
 *     io_registers[512]              gpsp/gba_memory.h:275
 *     REG_P1 / REG_P1CNT / REG_IE
 *     REG_IF / REG_IME               gpsp/gba_memory.h:185-193
 *     read_ioreg / write_ioreg       gpsp/common.h:176-177
 *     flag_interrupt                 gpsp/cpu.h:115
 *     check_and_raise_interrupts     gpsp/cpu.h:113
 *     IRQ_KEYPAD                     gpsp/cpu.h:72
 *     BUTTON_A .. BUTTON_L           gpsp/input.h:27-37  (cross-check only)
 *
 * Nothing is hand-externed, because nothing needed to be: unlike M5, which had
 * to declare video_count and gamepak_file_blocks itself, every name M8 touches
 * is already public.
 */

#include "common.h"      /* pulls cpu.h (flag_interrupt, check_and_raise_
                            interrupts, IRQ_KEYPAD), gba_memory.h (io_registers,
                            REG_P1, REG_P1CNT, REG_IE, REG_IF, REG_IME) and
                            input.h (BUTTON_*, for the enum cross-check)      */

#include "gba_input.h"

/* ============================================================ the mapping ==
 *
 * TWO PARALLEL INTEGER ARRAYS, NOT AN ARRAY OF STRUCTS OR OF POINTERS.
 *
 * The reason is the one M2-M7 already measured and recorded: a table of
 * POINTERS lands in .data.rel.ro and costs one R_X86_64_RELATIVE relocation per
 * entry. Plain integer arrays are ordinary .rodata and cost none. gpSP's own
 * btn_map (input.h:46-57) is an array of structs of plain integers and would
 * also be relocation-free, but it maps RETRO_DEVICE_ID_JOYPAD_* -- libretro
 * button ids -- which are meaningless here and whose header is excluded from
 * the closure by policy.
 *
 * THE ORDER IS DELIBERATE: the four directions first, then the PASS-CRITICAL
 * CROSS, then the rest. m8_input_map_pad(0..3) is therefore always the D-pad,
 * which the fixture relies on when it prints the table.
 *
 * OPTIONS and TOUCHPAD are LAST because their DualSense bit values are the two
 * in this table that are NOT confirmed by LuaPSX/src/main.c:157-171. They are
 * mapped, they are reported, and NOTHING M8 CAN FAIL ON DEPENDS ON THEM. */
#define M8_MAP_N  10u

static const unsigned int m8_map_pad[M8_MAP_N] = {
    M8_PAD_UP,       M8_PAD_DOWN,     M8_PAD_LEFT,   M8_PAD_RIGHT,
    M8_PAD_CROSS,    M8_PAD_CIRCLE,   M8_PAD_L1,     M8_PAD_R1,
    M8_PAD_OPTIONS,  M8_PAD_TOUCHPAD
};

static const unsigned int m8_map_key[M8_MAP_N] = {
    M8_KEY_UP,       M8_KEY_DOWN,     M8_KEY_LEFT,   M8_KEY_RIGHT,
    M8_KEY_A,        M8_KEY_B,        M8_KEY_L,      M8_KEY_R,
    M8_KEY_START,    M8_KEY_SELECT
};

/* The two tables state one relation and must stay the same length. */
_Static_assert(sizeof(m8_map_pad) / sizeof(m8_map_pad[0]) == M8_MAP_N,
               "pad side of the mapping table changed length");
_Static_assert(sizeof(m8_map_key) / sizeof(m8_map_key[0]) == M8_MAP_N,
               "key side of the mapping table changed length");
_Static_assert(M8_MAP_N == M8_MAP_COUNT,
               "M8_MAP_COUNT in gba_input.h disagrees with the table");

/* ---- the restatement, checked against gpSP's OWN enum ---------------------
 *
 * gba_input.h RESTATES gpsp/input.h:27-37 because apps/m8gpsp/main.c cannot
 * include a gpSP header. THIS FILE CAN SEE BOTH, so the restatement is verified
 * here at COMPILE TIME. If upstream ever renumbers input_buttons_type, this
 * build fails with a named message instead of silently mapping CROSS to the
 * wrong key.
 *
 * This is the same technique gba_present.h uses for the geometry constants and
 * gba_exec.h uses for the entry opcode: restate for the boundary, then assert
 * the restatement against its source wherever both are visible. */
_Static_assert((unsigned int)BUTTON_A      == M8_KEY_A,      "BUTTON_A moved");
_Static_assert((unsigned int)BUTTON_B      == M8_KEY_B,      "BUTTON_B moved");
_Static_assert((unsigned int)BUTTON_SELECT == M8_KEY_SELECT, "BUTTON_SELECT moved");
_Static_assert((unsigned int)BUTTON_START  == M8_KEY_START,  "BUTTON_START moved");
_Static_assert((unsigned int)BUTTON_RIGHT  == M8_KEY_RIGHT,  "BUTTON_RIGHT moved");
_Static_assert((unsigned int)BUTTON_LEFT   == M8_KEY_LEFT,   "BUTTON_LEFT moved");
_Static_assert((unsigned int)BUTTON_UP     == M8_KEY_UP,     "BUTTON_UP moved");
_Static_assert((unsigned int)BUTTON_DOWN   == M8_KEY_DOWN,   "BUTTON_DOWN moved");
_Static_assert((unsigned int)BUTTON_R      == M8_KEY_R,      "BUTTON_R moved");
_Static_assert((unsigned int)BUTTON_L      == M8_KEY_L,      "BUTTON_L moved");
_Static_assert((unsigned int)BUTTON_NONE   == 0u,            "BUTTON_NONE moved");

/* The ten bits are contiguous and fill exactly the 10-bit domain, which is what
 * makes M8_KEY_MASK a complete statement rather than an approximation. */
_Static_assert((M8_KEY_A | M8_KEY_B | M8_KEY_SELECT | M8_KEY_START |
                M8_KEY_RIGHT | M8_KEY_LEFT | M8_KEY_UP | M8_KEY_DOWN |
                M8_KEY_R | M8_KEY_L) == M8_KEY_MASK,
               "the ten GBA key bits do not fill the 10-bit domain");

/* gpsp/gba_memory.c:2429's reset value is the complement of "no keys". */
_Static_assert(((~0u) & M8_KEY_MASK) == M8_P1_NEUTRAL,
               "M8_P1_NEUTRAL is not the active-low form of an empty key mask");

/* ================================================== the pure conversions ==
 *
 * No gpSP state is touched by either of these. They are the two functions the
 * offline self-test exercises, and they are the reason that self-test can run
 * before the pad is opened and before the display is taken. */

unsigned int m8_input_from_pad(unsigned int pad_buttons, int connected) {
    unsigned int keys = 0u;
    unsigned int i;

    /* THE ANTI-PHANTOM-INPUT GUARD, AND IT COMES FIRST.
     *
     * A dropped scePadRead must produce a NEUTRAL keypad, never a latched or
     * synthesised one. runtime/platform.c:484-489 already zeroes `buttons` on a
     * failed read -- it holds only the last good STICK values, and deliberately
     * so, because zeroed sticks read as hard left-and-up rather than as centre.
     * This is the second, independent guard on our side of the boundary, and it
     * ignores `pad_buttons` entirely rather than trusting that the platform
     * layer zeroed it. Two guards, no shared assumption. */
    if (!connected)
        return 0u;

    for (i = 0u; i < M8_MAP_N; i++)
        if (pad_buttons & m8_map_pad[i])
            keys |= m8_map_key[i];

    /* Structurally redundant -- every value in m8_map_key is inside the domain
     * and the selftest proves it -- and kept because it makes the return type's
     * contract true by construction rather than by inspection of a table that
     * lives forty lines away. */
    return keys & M8_KEY_MASK;
}

unsigned int m8_input_to_reg_p1(unsigned int gba_keys) {
    /* gpsp/input.c:163, character for character:
     *     write_ioreg(REG_P1, (~old_key) & 0x3FF);
     * The mask on the way IN matters as much as the one on the way out: a
     * caller that passed a stray high bit would otherwise clear a REG_P1 bit
     * that no GBA button owns. */
    return (~(gba_keys & M8_KEY_MASK)) & M8_KEY_MASK;
}

unsigned int m8_input_map_count(void) { return M8_MAP_N; }

unsigned int m8_input_map_pad(unsigned int i) {
    return (i < M8_MAP_N) ? m8_map_pad[i] : 0u;
}

unsigned int m8_input_map_key(unsigned int i) {
    return (i < M8_MAP_N) ? m8_map_key[i] : 0u;
}

/* ====================================================== REG_P1 PROPAGATION ==
 *
 * THE MILESTONE, AND IT IS FOUR LINES LONG.
 *
 * This is the whole of "input reaches the GBA keypad state". It writes one
 * hardware register through gpSP's own macro and does nothing else -- no
 * interrupt, no REG_IF, no REG_IE, no REG_IME, no CPU state. Keeping it this
 * small is what makes the M8 proof decomposable: if the differential test
 * shows a framebuffer change, THIS is what caused it, and the interrupt path
 * below can be examined separately rather than as a confound. */
void m8_input_apply(unsigned int gba_keys) {
    write_ioreg(REG_P1, (u16)m8_input_to_reg_p1(gba_keys));
}

unsigned int m8_input_read_reg_p1(void)    { return (unsigned int)read_ioreg(REG_P1); }
unsigned int m8_input_read_reg_p1cnt(void) { return (unsigned int)read_ioreg(REG_P1CNT); }
unsigned int m8_input_read_reg_ie(void)    { return (unsigned int)read_ioreg(REG_IE); }
unsigned int m8_input_read_reg_if(void)    { return (unsigned int)read_ioreg(REG_IF); }
unsigned int m8_input_read_reg_ime(void)   { return (unsigned int)read_ioreg(REG_IME); }

/* ================================================ KEYPAD INTERRUPT GENERATION
 *
 * A SEPARATE CONCEPT FROM THE FOUR LINES ABOVE, AND DELIBERATELY A SEPARATE
 * FUNCTION.
 *
 * gpsp/input.c:41-66 defines trigger_key() as `static`, so it cannot be called
 * from outside that translation unit, and its only caller -- update_input() --
 * is itself unreachable because gpsp/libretro/libretro.c is excluded from every
 * LUAport build. The CONDITION is therefore restated here; the ACTION is
 * delegated to gpSP's own public entry points and is NOT reimplemented.
 *
 * THE GUARD ORDER IS UPSTREAM'S AND IS THE ENTIRE SAFETY ARGUMENT:
 *
 *   input.c:159   the EDGE.  Only a key going DOWN can raise a keypad IRQ;
 *                 releases never do. (new|old) != old is true exactly when
 *                 `new` sets a bit `old` did not.
 *   input.c:45    the ENABLE. If KEYCNT bit 14 is clear, trigger_key()'s whole
 *                 body is skipped -- so this function returns having changed
 *                 NOTHING. That is what makes it safe to call unconditionally
 *                 on every frame: a cartridge that never enables keypad IRQs
 *                 cannot be given one by M8.
 *   input.c:49    the MODE. Bit 15 selects AND (every selected key must be
 *                 down) over OR (any selected key). Both branches are
 *                 reproduced; neither is guessed.
 *
 * WHY THIS IS NOT "DUPLICATING gpSP BECAUSE THE LIBRETRO PATH IS MISSING":
 * the replication fires only where gpSP's own code would have fired, under
 * gpSP's own condition, reading gpSP's own register, calling gpSP's own
 * functions. The return value REPORTS which of those conditions held, so
 * whether the cartridge needed this at all is settled by measurement on the
 * console rather than by assumption here.
 *
 * THE CALL SITE IS UPSTREAM'S TOO. gpsp/libretro/libretro.c:1368-1373 shows
 * retro_run() calling update_input() -- hence trigger_key() -- from OUTSIDE the
 * interpreter, immediately BEFORE execute_arm(). apps/m8gpsp/main.c calls this
 * in exactly that position. */
unsigned int m8_input_irq_edge(unsigned int new_keys, unsigned int old_keys) {
    unsigned int flags = 0u;
    unsigned int p1_cnt;
    unsigned int intersection;
    unsigned int fire = 0u;

    new_keys &= M8_KEY_MASK;
    old_keys &= M8_KEY_MASK;

    /* gpsp/input.c:159 -- the edge. Reported even when the enable bit is clear,
     * because "a key went down but the cartridge had not armed KEYCNT" is a
     * meaningful and very likely observation, and the fixture prints it. */
    if ((new_keys | old_keys) != old_keys)
        flags |= M8_IRQ_EDGE;

    /* gpsp/input.c:43 */
    p1_cnt = (unsigned int)read_ioreg(REG_P1CNT);

    /* gpsp/input.c:45 -- THE ENABLE. Everything below is inside this test in
     * upstream and is inside it here. */
    if (!((p1_cnt >> 14) & 0x01u))
        return flags;

    flags |= M8_IRQ_ENABLED;
    if ((p1_cnt >> 15) & 0x01u)
        flags |= M8_IRQ_ANDMODE;

    /* No edge means trigger_key() was never reached in upstream either
     * (input.c:159 guards the call), so the enable/mode reporting above is all
     * that can honestly be said this frame. */
    if (!(flags & M8_IRQ_EDGE))
        return flags;

    /* gpsp/input.c:47 */
    intersection = (p1_cnt & new_keys) & M8_KEY_MASK;

    if (flags & M8_IRQ_ANDMODE) {
        /* input.c:51 -- AND mode: every selected key must be held. */
        if (intersection == (p1_cnt & M8_KEY_MASK))
            fire = 1u;
    } else {
        /* input.c:59 -- OR mode: any selected key. */
        if (intersection)
            fire = 1u;
    }

    if (!fire)
        return flags;

    flags |= M8_IRQ_MATCH;

    /* input.c:53-54 / :61-62. gpSP's OWN public entry points, gpsp/cpu.h:113-115.
     * Nothing about the interrupt is reimplemented here -- the vectoring, the
     * IE/IME test and the CPU mode switch are all gpSP's. */
    flag_interrupt(IRQ_KEYPAD);
    check_and_raise_interrupts();

    flags |= M8_IRQ_RAISED;
    return flags;
}

/* ========================================================= THE SELF-TEST ====
 *
 * A SET BIT IS A FAILED ASSERTION, as in M2-M7.
 *
 * Pure: it touches no gpSP state, no io_registers entry and no hardware, so the
 * fixture runs it BEFORE plat_pad_init() and BEFORE plat_video_init(). An
 * inverted sign convention or a swapped bit is then caught while the host game
 * is still on screen and a clean refusal is still possible -- the same ordering
 * rule that puts gba_present_selftest() ahead of the display takeover in M7.
 *
 * EVERY EXPECTED VALUE BELOW WAS DERIVED BY HAND from (~keys) & 0x3FF against
 * gpsp/input.h:27-37. None was captured from a run of this code, which is what
 * makes the table a test rather than a snapshot. */
unsigned int m8_input_selftest(void) {
    /* keys -> the REG_P1 value that must result. The twelve rows the M8 brief
     * names, plus the all-keys row. Two integer arrays, so no relocations. */
    static const unsigned int st_keys[13] = {
        0x000u, 0x001u, 0x002u, 0x003u, 0x004u, 0x008u, 0x010u,
        0x020u, 0x040u, 0x080u, 0x100u, 0x200u, 0x3FFu
    };
    static const unsigned int st_p1[13] = {
        0x3FFu, 0x3FEu, 0x3FDu, 0x3FCu, 0x3FBu, 0x3F7u, 0x3EFu,
        0x3DFu, 0x3BFu, 0x37Fu, 0x2FFu, 0x1FFu, 0x000u
    };
    /* Row i fails bit i, so M8_ST_NEUTRAL..M8_ST_ALL are positional and the
     * notification can name the exact row without a second table. */
    unsigned int f = 0u;
    unsigned int i, j;

    for (i = 0u; i < 13u; i++)
        if (m8_input_to_reg_p1(st_keys[i]) != st_p1[i])
            f |= (1u << i);

    /* ---- the mapping, one button at a time --------------------------------
     * Each DualSense bit alone must produce exactly its GBA key and nothing
     * else. `connected` is 1 here; the disconnected case is below. */
    for (i = 0u; i < M8_MAP_N; i++) {
        if (m8_input_from_pad(m8_map_pad[i], 1) != m8_map_key[i])
            f |= M8_ST_MAPPING;

        /* Inside the 10-bit GBA domain... */
        if (m8_map_key[i] & ~M8_KEY_MASK)
            f |= M8_ST_RANGE;

        /* ...and inside the 21 bits scePadRead actually reports
         * (runtime/platform.c:501). A mapping to a bit the platform layer masks
         * away would never fire and would present as a dead button. */
        if (m8_map_pad[i] & ~M8_PAD_REPORTED)
            f |= M8_ST_REPORTED;

        /* INJECTIVE. Two DualSense buttons sharing one GBA key would make the
         * mapping ambiguous and would be invisible in every single-button row
         * above. */
        for (j = i + 1u; j < M8_MAP_N; j++) {
            if (m8_map_key[i] == m8_map_key[j])
                f |= M8_ST_UNIQUE;
            if (m8_map_pad[i] == m8_map_pad[j])
                f |= M8_ST_UNIQUE;
        }
    }

    /* ---- COMPOSITION IS OR, NOT PRIORITY ----------------------------------
     * gpsp/input.c:83 accumulates with |= and input.c contains no precedence
     * logic of any kind, so holding two buttons must set two bits. Walking
     * every ordered pair proves it for the whole table rather than for one
     * sampled combination -- 45 pairs, all at build-time-constant cost. */
    for (i = 0u; i < M8_MAP_N; i++) {
        for (j = i + 1u; j < M8_MAP_N; j++) {
            unsigned int both = m8_input_from_pad(m8_map_pad[i] | m8_map_pad[j], 1);
            if (both != (m8_map_key[i] | m8_map_key[j]))
                f |= M8_ST_COMPOSE;
        }
    }

    /* The named case from the M8 brief, stated explicitly as well as being
     * covered by the pair walk above: D-pad + A must carry BOTH bits. */
    if (m8_input_from_pad(M8_PAD_UP | M8_PAD_CROSS, 1) != (M8_KEY_UP | M8_KEY_A))
        f |= M8_ST_COMPOSE;

    /* ---- a failed read must never synthesise input ------------------------
     * Every button pressed at once, with connected == 0, must still be a
     * neutral keypad. This is the assertion that stands between a dropped
     * scePadRead and a stuck direction on screen. */
    if (m8_input_from_pad(0xFFFFFFFFu, 0) != 0u)  f |= M8_ST_DISCONN;
    if (m8_input_from_pad(M8_PAD_CROSS, 0) != 0u) f |= M8_ST_DISCONN;
    if (m8_input_to_reg_p1(m8_input_from_pad(0xFFFFFFFFu, 0)) != M8_P1_NEUTRAL)
        f |= M8_ST_DISCONN;

    /* ---- unmapped buttons contribute nothing ------------------------------
     * SQUARE, TRIANGLE, L2, R2, L3 and R3 are deliberately unmapped at M8.
     * Pressing all six together must leave the keypad neutral -- which is also
     * the check that would catch a stray extra row appearing in the table. */
    if (m8_input_from_pad(M8_PAD_SQUARE | M8_PAD_TRIANGLE | M8_PAD_L2 |
                          M8_PAD_R2 | M8_PAD_L3 | M8_PAD_R3, 1) != 0u)
        f |= M8_ST_UNMAPPED;

    /* ---- the conversion inverts cleanly -----------------------------------
     * to_reg_p1 is its own inverse on the 10-bit domain. Walking all 1024
     * values states in one line that the conversion is a bijection and loses
     * nothing -- and it is exhaustive, not sampled. */
    for (i = 0u; i <= M8_KEY_MASK; i++) {
        if (m8_input_to_reg_p1(m8_input_to_reg_p1(i)) != i)
            f |= M8_ST_INVOLUTE;
    }

    /* ---- the restatement against gpSP's own enum --------------------------
     * The _Static_asserts at the top of this file already make a mismatch a
     * BUILD failure, so this can never fire in a binary that linked. It is kept
     * because the fixture reports the selftest mask on screen, and a runtime
     * bit that is provably unreachable is a cheap, permanent statement that the
     * restatement was checked -- the compile-time proof leaves no trace in the
     * image otherwise. */
    if ((unsigned int)BUTTON_A != M8_KEY_A ||
        (unsigned int)BUTTON_B != M8_KEY_B ||
        (unsigned int)BUTTON_L != M8_KEY_L ||
        (unsigned int)BUTTON_R != M8_KEY_R ||
        (unsigned int)BUTTON_UP != M8_KEY_UP ||
        (unsigned int)BUTTON_DOWN != M8_KEY_DOWN ||
        (unsigned int)BUTTON_LEFT != M8_KEY_LEFT ||
        (unsigned int)BUTTON_RIGHT != M8_KEY_RIGHT ||
        (unsigned int)BUTTON_START != M8_KEY_START ||
        (unsigned int)BUTTON_SELECT != M8_KEY_SELECT)
        f |= M8_ST_GPSPENUM;

    return f;
}
