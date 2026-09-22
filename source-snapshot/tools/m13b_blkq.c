/* ===========================================================================
 * tools/m13b_blkq.c -- ONE OBJECT, FOR THE HOST HARNESS ONLY
 * ===========================================================================
 *
 * This file exists to satisfy exactly one symbol that tools/m13b_picker_equiv.c
 * cannot legally define in its own translation unit.
 *
 * ---- THE PROBLEM ----
 *
 * adapters/gba/gba_rom.c:98-101 declares gpSP's block queue like this:
 *
 *     extern struct {
 *       u16 next_lru;
 *       s16 phy_rom;
 *     } gamepak_blk_queue[1024];
 *
 * The struct is UNTAGGED. Two untagged struct definitions written in the SAME
 * translation unit are DISTINCT, INCOMPATIBLE types, so the harness cannot write
 * that same declaration a second time alongside the extern in order to define
 * the storage -- it is a redeclaration with a conflicting type and the compiler
 * rejects it. There is also no tag to name the type by, so the object cannot be
 * defined by referring to gba_rom.c's version either.
 *
 * ---- WHY A SEPARATE FILE IS THE CORRECT FIX AND NOT A WORKAROUND ----
 *
 * C11 6.2.7p1 makes two structure types declared in SEPARATE translation units
 * COMPATIBLE when they have the same member count, the same member names and
 * compatible member types -- untagged or not. Defining the object here is
 * therefore a definition of THE SAME OBJECT gba_rom.c declares, not a type pun.
 *
 * That is not a new argument invented for the harness. It is the EXACT rule
 * adapters/gba/gba_rom.c:90-97 already cites, in writing, to justify its own
 * hand-written extern against gpSP's definition at gba_memory.c:381-384. On the
 * console the definition comes from gpSP; offline it comes from here; the
 * language rule that makes both legal is the same one.
 *
 * ---- SCOPE ----
 *
 * HOST HARNESS ONLY. This file appears in NO object list, is linked into NO
 * console image, and nothing in adapters/ or apps/ references it. It is reached
 * only by the `m13b-equiv` Makefile target. No production source is modified to
 * accommodate it and no test-only #ifdef exists anywhere.
 *
 * The contents are never inspected by any assertion -- gba_rom.c's
 * rom_page0_recorded() merely SCANS it, and a zero-initialised queue correctly
 * reports "page 0 is not recorded", which is the truthful answer when no ROM has
 * been loaded by a real gpSP. The harness asserts nothing about the LRU.
 * ========================================================================= */

/* The two integer types the members use, spelled locally. This file includes NO
   header at all -- not core.h, not common.h -- precisely so it cannot drag a
   conflicting u64 typedef into the link. */
typedef unsigned short m13b_blkq_u16;
typedef short          m13b_blkq_s16;

/* Member NAMES and TYPES match gba_rom.c:98-101 exactly, which is the condition
   6.2.7p1 requires. The local typedef spellings are irrelevant to compatibility
   -- `unsigned short` is `unsigned short` however it is named. */
struct {
    m13b_blkq_u16 next_lru;
    m13b_blkq_s16 phy_rom;
} gamepak_blk_queue[1024];
