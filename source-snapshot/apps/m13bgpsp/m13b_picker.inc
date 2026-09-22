/* ===========================================================================
 * LUAport M13B -- apps/m13bgpsp/m13b_picker.inc
 * THE PICKER MODEL. PURE LOGIC, NO KERNEL, NO VIDEO, NO gpSP.
 * ===========================================================================
 *
 * WHAT THIS FILE IS, AND WHY IT IS AN .inc AND NOT A .c
 * -----------------------------------------------------
 * Exactly the shape apps/m13agpsp/m13a_report.inc established at M13A: a body of
 * logic that BOTH the shipping fixture and an OFFLINE HOST HARNESS include
 * VERBATIM, so the thing under test is the thing that ships. It is not a library
 * and it is not linked separately; it is textually included by
 *
 *     apps/m13bgpsp/main.c          (the console fixture -- draws it)
 *     tools/m13b_picker_equiv.c     (the host harness   -- proves it)
 *
 * ***** NOTHING IN THIS FILE DRAWS, OPENS, ALLOCATES OR CALLS THE KERNEL. *****
 *
 * That is the deliberate scope line, and it is what makes the picker TESTABLE AT
 * ALL. Sorting, scrolling, selection clamping, display shortening and the
 * CROSS-release state machine are the parts that can be WRONG IN A WAY THAT IS
 * INVISIBLE ON SCREEN -- an off-by-one in the scroll window shows the operator a
 * plausible list with the wrong row highlighted, and a comparator that is not a
 * total order makes the list order change between runs on the same drive. None
 * of that can be provoked reliably on hardware. All of it is trivial here.
 *
 * The DRAWING lives in apps/m13bgpsp/main.c, which owns runtime/gfx.* and the
 * 480x270 surface. This file only decides WHAT the fixture should draw.
 *
 * ---- WHAT THE INCLUDING TRANSLATION UNIT MUST HAVE ALREADY PROVIDED ----
 *
 *     u8 / u16 / unsigned            (runtime/core.h on console, the harness's
 *                                     own typedefs offline)
 *     adapters/gba/gba_library.h     struct gba_lib, gba_lib_name(),
 *                                     GBA_LIB_MAX_ENTRIES
 *     adapters/gba/gba_rom.h         M4_SRC_EMPTY / M4_SRC_TOOBIG /
 *                                     M4_SRC_MIRROR_1M
 *
 * Both headers are safe on both sides: gba_rom.h deliberately uses ONLY
 * `unsigned int`, `int` and `char *` (gba_rom.h:18-24), so including it next to
 * runtime/core.h creates no u64 typedef collision.
 *
 * ---- THE ONE RULE THAT OUTRANKS EVERYTHING ELSE HERE ----
 *
 * ***** A LABEL MAY BE SHORTENED. A PATH MAY NOT. *****
 *
 * m13b_label() exists precisely so that the fixture NEVER shortens a real path
 * to make it fit a row. It writes into a caller-owned display buffer and CANNOT
 * touch the source string -- its `src` argument is `const char *`. The full path
 * that is eventually handed to m4_rom_load_named() is always read straight back
 * out of the library entry, never out of a display buffer. This is the same
 * separation m13store.h:38-46 and gba_rom.h:148-152 both insist on, restated at
 * the one layer that actually has a reason to abbreviate something.
 * ========================================================================= */

/* Sixteen rows is the authorised picker height. It is spelled once, here, and
   every piece of scroll arithmetic below derives from it. */
#define M13B_ROWS            16u

/* Visible characters of a filename in one row, NOT counting the NUL. The row is
   drawn with a 2-column cursor gutter, so this is the width that remains.
   DISPLAY ONLY -- see the rule above. */
#define M13B_LABEL_MAX       44u
#define M13B_LABEL_CAP       (M13B_LABEL_MAX + 1u)

/* ---- why-an-entry-cannot-be-launched codes -------------------------------
 *
 * Every rejected entry STAYS VISIBLE and carries one of these. A picker that
 * silently hid what it would not load would leave the operator staring at a
 * short list with no way to tell whether the drive was wrong, the file was
 * damaged, or the picker itself was filtering too hard -- the same argument
 * gba_library.h:76-79 makes for counting rejections by reason. */
/* ***** M13B_PICK_OK, NOT M13B_OK. ***** This is a PICKER-MODEL reason code and
   it lives in a DIFFERENT NAMESPACE from the app-level payload status that
   apps/m13bgpsp/main.c owns (main.c's M13B_OK is what goes into ext->status).
   The two happen to be spelled 0, and that coincidence is exactly why they must
   not share a name: a reader who saw `it->reason = M13B_OK` beside
   `ext->status = M13B_OK` would have no way to tell that one is "this row is
   launchable" and the other is "the whole fixture succeeded". The picker owns
   reasons; main.c owns statuses. */
#define M13B_PICK_OK         0u
#define M13B_R_LONG          1u  /* path longer than m4_rom_load_named accepts */
#define M13B_R_OPEN          2u  /* m4_rom_query_named could not open it       */
#define M13B_R_EMPTY         3u  /* M4_SRC_EMPTY -- 0 bytes / unmeasurable     */
#define M13B_R_BIG           4u  /* M4_SRC_TOOBIG                              */
#define M13B_R_1MB           5u  /* M4_SRC_MIRROR_1M -- the 4 MB mirror path   */
#define M13B_R_SRC           6u  /* a source-gate bit this build does not name */

/* One row's worth of validation result. Parallel to the library table and
   indexed by the SAME scan index, never by the sorted display position -- see
   m13b_order below. */
struct m13b_item {
    unsigned size;               /* true byte length, when it could be measured */
    u8       reason;             /* M13B_PICK_OK or why it cannot be launched   */
    u8       sel;                /* 1 only if it is launchable                  */
};

/* ***** THE SORT IS AN INDEX PERMUTATION, NOT A REORDERING. *****
 *
 * The library entries themselves are NEVER moved. m13b_order[display_row] gives
 * the SCAN INDEX to read, so gba_lib_path() keeps returning the exact bytes
 * gba_lib_scan() stored and the "original scan index" tie-break below stays
 * meaningful. Moving 260-byte entries around would also cost 16 KB of copying
 * for no benefit. */
static u8 m13b_order[GBA_LIB_MAX_ENTRIES];

/* ---------------------------------------------------------------- sorting -- */

/* ASCII-only case fold. DELIBERATELY NOT tolower(): the console build has no
   locale, and a locale-sensitive fold would make the on-screen order depend on
   something neither the operator nor this milestone controls. */
static int m13b_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
}

/* Two-pass filename comparison.
 *
 * PASS 1 is case-INSENSITIVE, because the drive is FAT/exFAT -- case-preserving
 * but case-insensitive -- so "ZELDA.GBA" and "zelda.gba" are neighbours to the
 * filesystem and must be neighbours on screen. A purely byte-wise sort would
 * file every upper-case name before every lower-case one and scatter a real
 * library into two alphabets.
 *
 * PASS 2 is case-SENSITIVE and runs ONLY when pass 1 ties. Without it the
 * comparator would report "equal" for two DIFFERENT files, the sort would be at
 * the mercy of its own internal order, and the list could reorder between runs
 * on an unchanged drive. */
static int m13b_cmp_name(const char *a, const char *b)
{
    unsigned i;

    if (!a) a = "";
    if (!b) b = "";

    for (i = 0; ; i++) {
        int ca = m13b_lower((int)(unsigned char)a[i]);
        int cb = m13b_lower((int)(unsigned char)b[i]);
        if (ca != cb) return (ca < cb) ? -1 : 1;
        if (ca == 0)  break;
    }

    for (i = 0; ; i++) {
        int ca = (int)(unsigned char)a[i];
        int cb = (int)(unsigned char)b[i];
        if (ca != cb) return (ca < cb) ? -1 : 1;
        if (ca == 0)  break;
    }

    return 0;
}

/* THE TOTAL ORDER. The scan index is the FINAL tie-break, so the comparator can
   never return 0 for two distinct entries. That is what makes the result a
   deterministic permutation even when a directory somehow lists the same
   filename twice. */
static int m13b_cmp(const struct gba_lib *lib, unsigned ia, unsigned ib)
{
    int r = m13b_cmp_name(gba_lib_name(lib, ia), gba_lib_name(lib, ib));

    if (r != 0)
        return r;

    if (ia != ib)
        return (ia < ib) ? -1 : 1;

    return 0;
}

/* Insertion sort. n <= 64, so the worst case is ~2,016 comparisons -- far below
   anything a frame budget would notice -- and it needs NO recursion, NO scratch
   array and NO libc. qsort() is deliberately not used: it would pull a libc
   dependency into the console image for a 64-element list and its ordering of
   "equal" elements is unspecified, which is exactly the property this comparator
   works to eliminate. */
static void m13b_sort(const struct gba_lib *lib, u8 *order, unsigned n)
{
    unsigned i, j;

    if (!order)
        return;

    if (n > GBA_LIB_MAX_ENTRIES)
        n = GBA_LIB_MAX_ENTRIES;

    for (i = 0; i < n; i++)
        order[i] = (u8)i;

    if (!lib)
        return;

    for (i = 1; i < n; i++) {
        u8 v = order[i];
        j = i;
        while (j > 0 && m13b_cmp(lib, (unsigned)order[j - 1], (unsigned)v) > 0) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = v;
    }
}

/* -------------------------------------------------------------- scrolling -- */

/* The visible window's first row, given where the cursor is now.
 *
 * TOTAL AND CLAMPED. Every argument combination -- including sel >= count, a
 * count of 0, and a top left stale by a previous larger library -- returns a
 * window that is fully inside [0, count). The fixture therefore never has to
 * pre-validate, which is the same "the reader is total" discipline
 * gba_library.h:110-127 applies to gba_lib_path(). */
static unsigned m13b_scroll(unsigned sel, unsigned count, unsigned top)
{
    if (count == 0u || count <= M13B_ROWS)
        return 0u;

    if (sel >= count)
        sel = count - 1u;

    if (sel < top)
        top = sel;
    else if (sel >= top + M13B_ROWS)
        top = sel - (M13B_ROWS - 1u);

    if (top + M13B_ROWS > count)
        top = count - M13B_ROWS;

    return top;
}

/* Move the cursor by delta, CLAMPING at both ends.
 *
 * Clamp rather than wrap, deliberately: on a 64-entry list a wrap turns one
 * extra D-pad press at the top into a jump to the bottom of the library, which
 * reads as the picker having lost the operator's place. */
static unsigned m13b_move(unsigned sel, unsigned count, int delta)
{
    if (count == 0u)
        return 0u;

    if (sel >= count)
        sel = count - 1u;

    if (delta < 0) {
        unsigned d = (unsigned)(-delta);
        return (sel >= d) ? (sel - d) : 0u;
    } else {
        unsigned d = (unsigned)delta;
        unsigned s = sel + d;
        return (s >= count) ? (count - 1u) : s;
    }
}

/* ---------------------------------------------------------------- display -- */

/* Build a ROW LABEL from a filename.
 *
 * ***** THIS IS THE ONLY FUNCTION IN M13B ALLOWED TO SHORTEN ANYTHING, AND IT
 * CANNOT REACH A PATH. ***** `src` is const and `dst` is a caller-owned display
 * buffer. A shortened label is marked with a trailing "..." so the operator can
 * SEE that a name was abbreviated rather than silently believing the drive
 * contains a shorter filename than it does.
 *
 * Always NUL-terminates, for any cap >= 1. */
static void m13b_label(char *dst, unsigned cap, const char *src)
{
    unsigned n = 0, i;

    if (!dst || cap == 0u)
        return;

    dst[0] = '\0';

    if (!src)
        return;

    while (src[n] != '\0')
        n++;

    if (n <= cap - 1u) {
        for (i = 0; i < n; i++)
            dst[i] = src[i];
        dst[n] = '\0';
        return;
    }

    /* Too long: keep the leading characters and stamp the cut. A GBA filename
       carries its distinguishing text at the FRONT ("Pokemon Emerald (USA)"),
       so the head is what the operator needs to tell two entries apart. */
    if (cap >= 5u) {
        unsigned keep = cap - 4u;
        for (i = 0; i < keep; i++)
            dst[i] = src[i];
        dst[keep + 0u] = '.';
        dst[keep + 1u] = '.';
        dst[keep + 2u] = '.';
        dst[keep + 3u] = '\0';
    } else {
        for (i = 0; i + 1u < cap; i++)
            dst[i] = src[i];
        dst[cap - 1u] = '\0';
    }
}

/* ------------------------------------------------------------- validation -- */

/* Turn one entry's measurement into a launchability verdict.
 *
 * `fits`   -- the FULL path is short enough for m4_rom_load_named() to accept.
 *             Checked by the caller against M4_ROM_NAME_MAX, because refusing
 *             here is the difference between "the picker offered a row that
 *             cannot possibly load" and "the picker said why".
 * `opened` -- m4_rom_query_named()'s return.
 * `srcmask`-- m4_rom_probe_source()'s bits, applied to the TRUE size.
 *
 * ***** sel == 1 IMPLIES srcmask == 0 EXACTLY. ***** The final `srcmask != 0`
 * arm is not defensive padding: if a later milestone adds a fourth M4_SRC_* bit
 * and this chain did not have it, an entry the ROM layer had REJECTED would be
 * drawn as launchable. Failing closed keeps the two layers from drifting. */
static void m13b_item_set(struct m13b_item *it, int fits, int opened,
                          unsigned size, unsigned srcmask)
{
    if (!it)
        return;

    it->size = size;
    it->sel  = 0u;

    if (!fits)
        it->reason = (u8)M13B_R_LONG;
    else if (!opened)
        it->reason = (u8)M13B_R_OPEN;
    else if (srcmask & M4_SRC_EMPTY)
        it->reason = (u8)M13B_R_EMPTY;
    else if (srcmask & M4_SRC_TOOBIG)
        it->reason = (u8)M13B_R_BIG;
    else if (srcmask & M4_SRC_MIRROR_1M)
        it->reason = (u8)M13B_R_1MB;
    else if (srcmask != 0u)
        it->reason = (u8)M13B_R_SRC;
    else {
        it->reason = (u8)M13B_PICK_OK;
        it->sel    = 1u;
    }
}

/* Short enough to sit at the right-hand end of a 16-row list without wrapping. */
static const char *m13b_reason_text(unsigned r)
{
    switch (r) {
    case M13B_PICK_OK:return "OK";
    case M13B_R_LONG: return "NAME TOO LONG";
    case M13B_R_OPEN: return "CANNOT OPEN";
    case M13B_R_EMPTY:return "EMPTY";
    case M13B_R_BIG:  return "TOO BIG";
    case M13B_R_1MB:  return "1MB MIRROR";
    case M13B_R_SRC:  return "REJECTED";
    default:          return "REJECTED";
    }
}

/* ------------------------------------------------------ the CROSS contract -- */

#define M13B_IN_IDLE         0u
#define M13B_IN_WAIT         1u  /* CROSS is down; waiting for it to come up   */
#define M13B_IN_LAUNCH       2u  /* TERMINAL -- released cleanly               */
#define M13B_IN_TIMEOUT      3u  /* TERMINAL -- never released in time         */

/* ***** WHY THE LAUNCH HAPPENS ON RELEASE AND NOT ON PRESS. *****
 *
 * The ROM the operator picks is loaded into a live emulator and, from M13B-4
 * onwards, begins executing. If the launch fired on the PRESS edge, the CROSS
 * that started the game would still be physically held when the GBA's own input
 * poll ran, and the first thing the new cartridge would see is a phantom A
 * press -- straight through any "PRESS A" title screen the operator never saw.
 *
 * The wait is BOUNDED because an unbounded one is a hung console: a stuck pad, a
 * disconnected controller mid-press or a driver that latches the button would
 * otherwise park the fixture in a loop with no exit and no way to report why.
 * 600 frames is ~10 s at 60 Hz -- far beyond any human press, short enough that
 * the operator gets a diagnosable TIMEOUT instead of a dead screen. */
#define M13B_RELEASE_FRAMES  600u

struct m13b_input {
    unsigned state;
    unsigned frames;
};

static void m13b_input_reset(struct m13b_input *in)
{
    if (!in)
        return;
    in->state  = M13B_IN_IDLE;
    in->frames = 0u;
}

/* One frame of the state machine. Returns the state AFTER the step.
 *
 * LAUNCH AND TIMEOUT ARE TERMINAL, which is what makes "launches once" a
 * structural property rather than a convention the caller has to remember: once
 * the machine has answered, no further frame can make it answer again. */
static unsigned m13b_input_step(struct m13b_input *in, int cross_down)
{
    if (!in)
        return M13B_IN_IDLE;

    switch (in->state) {
    case M13B_IN_IDLE:
        if (cross_down) {
            in->state  = M13B_IN_WAIT;
            in->frames = 0u;
        }
        break;

    case M13B_IN_WAIT:
        if (!cross_down) {
            in->state = M13B_IN_LAUNCH;
            break;
        }
        in->frames++;
        if (in->frames >= M13B_RELEASE_FRAMES)
            in->state = M13B_IN_TIMEOUT;
        break;

    default:
        break;                       /* terminal -- deliberately inert */
    }

    return in->state;
}
