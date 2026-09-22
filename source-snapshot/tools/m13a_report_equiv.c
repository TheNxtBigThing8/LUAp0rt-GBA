/* ===========================================================================
 * tools/m13a_report_equiv.c -- THE OFFLINE STAGE-0 REPORT HARNESS (M13A)
 * ===========================================================================
 *
 * Builds and runs with the HOST compiler, NOT the PS5 toolchain, and #includes
 * the shipping report builder VERBATIM:
 *
 *     apps/m13agpsp/m13a_report.inc   (M13A -- the on-console report sink)
 *     adapters/gba/m13store.c         (M13A -- for the REAL m13store_errname)
 *
 * ---- WHY THIS EXISTS ----
 *
 * M13A's evidence no longer travels over UDP 9027, which has never delivered a
 * datagram to the PC. It is written into a buffer and paged onto NOTIFICATIONS.
 * That moves a load-bearing piece of the milestone into a bounded string
 * builder -- and a bounded string builder that gets its limits wrong does not
 * fail loudly on a console. It either overruns a buffer Lua allocated, or, far
 * worse, quietly prints HALF A FILESYSTEM PATH and invites the operator to
 * conclude something false about their drive.
 *
 * Neither of those can be provoked safely or repeatably on hardware. Both are
 * trivial to provoke here.
 *
 * ---- HOW IT COMPILES A FILE MEANT FOR THE PS5 ----
 *
 * Same technique as tools/gba_library_equiv.c:11-23. This file pre-defines
 * runtime/core.h's own guard (CORE_H, runtime/core.h:1-2) so that header's body
 * expands to NOTHING, then supplies the handful of typedefs it would have
 * provided, its own NC/SYM, and a struct ext_args laid out EXACTLY as
 * runtime/core.h:150-159 lays it out -- because dbg[6] and dbg[7] are the whole
 * interface under test.
 *
 * ***** NO TEST-ONLY #ifdef EXISTS IN ANY PRODUCTION FILE. ***** What is
 * compiled below is byte-for-byte what ships on the console.
 *
 * ---- WHAT IS NOT CLAIMED ----
 *
 * Nothing here says a PS5 exposes USB storage, and nothing here says a console
 * will render a given notification -- send_notification is a Luac0re host
 * builtin with no in-tree source and no documented size limit, so the PAGE
 * THRESHOLDS on the Lua side are empirical and remain unproven until hardware.
 * What IS proved is the part a host can prove: that the report is bounded,
 * always NUL-terminated, never cuts a path, never overruns, and never loses the
 * verdict.
 * ========================================================================= */

#include <stdio.h>
#include <string.h>

/* ---- neutralise runtime/core.h ------------------------------------------ */
#define CORE_H

/* ---- the types core.h would have supplied -------------------------------- */
typedef unsigned long  u64;
typedef unsigned int   u32;
typedef unsigned short u16;
typedef unsigned char  u8;
typedef long           s64;
typedef int            s32;
typedef short          s16;
typedef signed char    s8;

#define LIBKERNEL_HANDLE 0x2001

/* u64 is `unsigned long` here, matching runtime/core.h:32 EXACTLY, for
   tools/gba_library_equiv.c:58-65's reason: the report sink casts dbg[6]
   through u64 to a char*, so a host where `unsigned long` is not 64-bit would
   truncate the buffer pointer. Fail at COMPILE time instead. */
typedef char m13a_u64_must_be_64_bit[(sizeof(unsigned long) == 8) ? 1 : -1];

/* ***** LAID OUT EXACTLY AS runtime/core.h:150-159. *****
   dbg[6] and dbg[7] are the interface under test, so if this drifts from the
   frozen header the harness would prove something about the wrong offsets. */
struct ext_args {
    s64 status;
    s64 step;
    u32 frame_count;
    u32 _pad;
    s32 log_fd;
    s32 pad_fd;
    u8  log_addr[16];
    u64 dbg[8];
};

/* The report sink reaches NO kernel entry point at all -- that is one of its
   design properties. These exist only so m13store.c, included below for the
   real m13store_errname(), can compile. Being called would be a defect, so
   they say so rather than returning a plausible value. */
static int m13a_kernel_was_called;

static void *fake_sym(void *gadget, void *dlsym_fn, s32 handle,
                      const char *name) {
    (void)gadget; (void)dlsym_fn; (void)handle; (void)name;
    m13a_kernel_was_called = 1;
    return 0;
}

static u64 fake_nc(void *gadget, void *fn, u64 a1, u64 a2, u64 a3,
                   u64 a4, u64 a5, u64 a6) {
    (void)gadget; (void)fn; (void)a1; (void)a2; (void)a3;
    (void)a4; (void)a5; (void)a6;
    m13a_kernel_was_called = 1;
    return (u64)(s64)(-22);
}

#define NC  fake_nc
#define SYM fake_sym

/* ---- the shipping source, compiled verbatim ------------------------------ */
#include "m13store.c"
#include "m13a_report.inc"

/* ===========================================================================
 * CHECK PLUMBING -- identical in shape to tools/gba_library_equiv.c:279-302
 * ========================================================================= */

static int failures = 0;
static int checks   = 0;

static void ck(int cond, const char *what) {
    checks++;
    if (!cond) { failures++; printf("    FAIL: %s\n", what); }
}

static void ck_int(long got, long want, const char *what) {
    checks++;
    if (got != want) {
        failures++;
        printf("    FAIL: %s (got %ld, want %ld)\n", what, got, want);
    }
}

static void ck_str(const char *got, const char *want, const char *what) {
    checks++;
    if (!got || strcmp(got, want) != 0) {
        failures++;
        printf("    FAIL: %s\n      got  '%s'\n      want '%s'\n",
               what, got ? got : "(null)", want);
    }
}

static void ck_has(const char *hay, const char *needle, const char *what) {
    checks++;
    if (!hay || !strstr(hay, needle)) {
        failures++;
        printf("    FAIL: %s (missing '%s')\n", what, needle);
    }
}

static void ck_hasnt(const char *hay, const char *needle, const char *what) {
    checks++;
    if (hay && strstr(hay, needle)) {
        failures++;
        printf("    FAIL: %s (unexpectedly present: '%s')\n", what, needle);
    }
}

/* ===========================================================================
 * THE BUFFER UNDER TEST, WITH A CANARY
 * =========================================================================
 *
 * The sink is told the buffer is `cap` bytes. Everything past `cap` is filled
 * with 0x7E and must still be 0x7E afterwards. On the console this memory
 * belongs to the Lua VM's heap, so an overrun here would corrupt the very
 * interpreter that is about to read the report back -- a fault that would
 * present as "the loader died after the payload returned" and would be
 * essentially undebuggable from a notification. */
#define ARENA_BYTES 4096
#define CANARY      ((char)0x7E)

static char            arena[ARENA_BYTES];
static struct ext_args EXT;

static void rep_up(unsigned cap) {
    unsigned i;

    for (i = 0; i < ARENA_BYTES; i++) arena[i] = CANARY;
    memset(&EXT, 0, sizeof(EXT));
    EXT.dbg[6] = (u64)(unsigned long)arena;
    EXT.dbg[7] = (u64)cap;
    m13a_rep_init(&EXT);
}

static int canary_ok(unsigned cap) {
    unsigned i;
    for (i = cap; i < ARENA_BYTES; i++)
        if (arena[i] != CANARY) return 0;
    return 1;
}

/* Appends one plain line, the way every caller in the fixture does. */
static void put_line(const char *s) {
    m13a_lb_reset();
    m13a_lb_puts(s);
    m13a_rep_commit(0u);
}

int main(void) {
    printf("M13A STAGE-0 REPORT EQUIVALENCE\n\n");

    /* ===================================================================
     * 1. NO BUFFER MEANS NO REPORT, AND NO CRASH
     * ===================================================================
     * Lua may legitimately pass nothing -- an older script paired with this
     * payload, or a future one that turns the feature off. Every entry point
     * must then be a silent no-op rather than a null dereference. */
    printf("[1] a payload handed no buffer degrades to a no-op\n");
    {
        memset(&EXT, 0, sizeof(EXT));
        EXT.dbg[6] = 0;
        EXT.dbg[7] = 8192;
        m13a_rep_init(&EXT);

        m13a_rep_section("#CTRL");
        m13a_rep_section2("#ROOT", "/");
        m13a_rep_probe("/mnt/usb0", -2);
        put_line("anything at all");
        m13a_rep_verdict(-346, "NO REMOVABLE ROOT VISIBLE");

        ck(1, "***** nothing dereferenced a null buffer *****");
        ck_int((long)m13a_rep_len, 0, "nothing was written");
        ck_int((long)m13a_rep_cap, 0, "no capacity was latched");
    }

    /* ===================================================================
     * 2. AN IMPLAUSIBLE CAPACITY IS REFUSED, NOT TRUSTED
     * ===================================================================
     * dbg[7] arrives from outside this image. A value we never handed out is
     * evidence of a mismatched pair, and trusting it would mean writing to an
     * address range nobody reserved. */
    printf("[2] capacity validation\n");
    {
        rep_up(64);
        ck_int((long)m13a_rep_cap, 0, "a capacity below the reserve is refused");

        memset(&EXT, 0, sizeof(EXT));
        EXT.dbg[6] = (u64)(unsigned long)arena;
        EXT.dbg[7] = 0x400000u;              /* 4 MB -- never handed out */
        m13a_rep_init(&EXT);
        ck_int((long)m13a_rep_cap, 0, "an implausibly large capacity is refused");

        rep_up(320);
        ck_int((long)m13a_rep_cap, 320, "the smallest workable capacity is taken");
    }

    /* ===================================================================
     * 3. THE SHAPE OF AN ORDINARY REPORT
     * =================================================================== */
    printf("[3] sections, probes and the verdict compose as the Lua reader expects\n");
    {
        rep_up(2048);
        m13a_rep_section("#CTRL");
        put_line("/temp0 PASS n=7");
        m13a_rep_section2("#ROOT", "/");
        put_line("[DIR ] temp0");
        m13a_rep_verdict(-346, "NO REMOVABLE ROOT VISIBLE");

        ck_str(arena,
               "#CTRL\n"
               "/temp0 PASS n=7\n"
               "#ROOT /\n"
               "[DIR ] temp0\n"
               "#VERDICT\n"
               "-346 NO REMOVABLE ROOT VISIBLE\n",
               "the whole report is exactly what the Lua parser splits on");
        ck_int((long)strlen(arena), (long)m13a_rep_len,
               "the reported length agrees with the NUL terminator");
        ck(canary_ok(2048), "nothing was written past the capacity");
    }

    /* ===================================================================
     * 4. ***** THE PROBE LINE CARRIES THE RAW RETURN AND THE REAL ERRNO *****
     * ===================================================================
     * ENOENT ("the guess was wrong") and EACCES ("the sandbox refused a path
     * that may well exist") are the two answers Stage 0 exists to tell apart.
     * Both must survive onto the screen intact, spelled by the SHIPPING
     * m13store_errname() rather than by a paraphrase in this file. */
    printf("[4] probe lines preserve the raw return and the real errno name\n");
    {
        rep_up(2048);
        m13a_rep_probe("/mnt/usb0", -2);
        ck_str(arena, "/mnt/usb0 -> -2 ENOENT -- no such file or directory\n",
               "ENOENT probe line");

        rep_up(2048);
        m13a_rep_probe("/mnt/usb1", -13);
        ck_has(arena, "EACCES", "EACCES survives to the screen");
        ck_has(arena, "SANDBOX REFUSAL",
               "***** the sandbox-refusal wording is preserved *****");

        rep_up(2048);
        m13a_rep_probe("/mnt/usb2", 5);
        ck_str(arena, "/mnt/usb2 -> 5 OK\n", "a successful open reports OK");
    }

    /* ===================================================================
     * 5. THE HAND-ROLLED DECIMAL FORMATTER
     * ===================================================================
     * Hand-rolled because m13a-verify forbids every M13A object from even
     * REFERENCING fprintf/fputs/fputc/fwrite. It is fed raw kernel returns, so
     * the full int range has to be right -- including INT_MIN, where the
     * obvious `-v` implementation is undefined behaviour. */
    printf("[5] signed decimal formatting across the whole int range\n");
    {
        rep_up(2048);
        m13a_lb_reset(); m13a_lb_dec(0);           m13a_rep_commit(0u);
        m13a_lb_reset(); m13a_lb_dec(7);           m13a_rep_commit(0u);
        m13a_lb_reset(); m13a_lb_dec(-2);          m13a_rep_commit(0u);
        m13a_lb_reset(); m13a_lb_dec(-346);        m13a_rep_commit(0u);
        m13a_lb_reset(); m13a_lb_dec(2147483647);  m13a_rep_commit(0u);
        m13a_lb_reset(); m13a_lb_dec(-2147483647 - 1); m13a_rep_commit(0u);
        m13a_lb_reset(); m13a_lb_udec(0u);         m13a_rep_commit(0u);
        m13a_lb_reset(); m13a_lb_udec(4294967295u); m13a_rep_commit(0u);

        ck_str(arena,
               "0\n7\n-2\n-346\n2147483647\n-2147483648\n0\n4294967295\n",
               "***** INT_MIN included -- no undefined negation *****");
    }

    /* ===================================================================
     * 6. ***** A LINE THAT DOES NOT FIT IS DROPPED WHOLE, NEVER CUT *****
     * ===================================================================
     * THE CENTRAL SAFETY PROPERTY, and the same rule m13store_join() applies
     * to real paths (m13store.h:38-46). A truncated path is not weaker
     * evidence than none -- it is MISLEADING evidence. An operator who reads
     * "/mnt/usb0/LUAport/roms" on a notification will go and look for a folder
     * that was never the one probed. */
    printf("[6] ***** a line that will not fit is dropped ENTIRELY *****\n");
    {
        unsigned before;

        rep_up(400);                 /* soft ceiling = 400 - 1 - 256 = 143 */
        put_line("0123456789012345678901234567890123456789"); /* 40 + NL */
        put_line("1123456789012345678901234567890123456789");
        put_line("2123456789012345678901234567890123456789");
        before = m13a_rep_len;
        ck_int((long)before, 123, "three 41-byte lines fit under the soft ceiling");
        ck_int((long)m13a_rep_trunc, 0, "nothing dropped yet");

        /* This one cannot fit. It carries a path-shaped, unmistakable prefix. */
        put_line("/mnt/usb7/LUAport/roms/gba/UNMISTAKABLE-TAIL.gba");

        ck_int((long)m13a_rep_trunc, 1, "the drop was recorded");
        ck(m13a_rep_lost > 0, "the dropped bytes were counted");
        ck_hasnt(arena, "/mnt/usb7",
                 "***** NOT ONE BYTE of the dropped path was written *****");
        ck_hasnt(arena, "UNMISTAKABLE",
                 "no fragment of the dropped line survives anywhere");
        ck_has(arena, "*** REPORT TRUNCATED",
               "***** the truncation is STAMPED, never silent *****");
        ck_int((long)strlen(arena), (long)m13a_rep_len,
               "still exactly NUL-terminated after a refusal");
        ck(canary_ok(400), "the refusal did not overrun the buffer");
    }

    /* ===================================================================
     * 7. ***** THE VERDICT SURVIVES A FULL BUFFER *****
     * ===================================================================
     * The reserved tail exists for exactly this. A "/" listing long enough to
     * exhaust the buffer must not be able to cost the operator the one line
     * that says what actually happened -- which is the line every other page
     * is interpreted against. */
    printf("[7] ***** the verdict still fits after the buffer is full *****\n");
    {
        int i;

        rep_up(400);
        for (i = 0; i < 200; i++)
            put_line("/mnt/usbX/filler/entry/that/keeps/going/and/going.gba");

        ck_int((long)m13a_rep_trunc, 1, "the filler did overflow the buffer");

        m13a_rep_verdict(-346, "NO REMOVABLE ROOT VISIBLE");

        ck_has(arena, "#VERDICT", "the verdict SECTION reached the report");
        ck_has(arena, "-346 NO REMOVABLE ROOT VISIBLE",
               "***** the verdict TEXT reached the report *****");
        ck_int((long)strlen(arena), (long)m13a_rep_len,
               "NUL-terminated after the reserved append");
        ck(m13a_rep_len < 400, "the report stayed inside its capacity");
        ck(canary_ok(400),
           "***** 200 overflowing appends never touched the canary *****");
    }

    /* ===================================================================
     * 8. THE DROPPED-BYTE COUNT REACHES dbg[7]
     * ===================================================================
     * dbg[7] arrives as the capacity and leaves as the loss. Lua needs the
     * outgoing value to say "N bytes dropped" on screen instead of leaving the
     * operator to guess how much of the listing they are missing. */
    printf("[8] the loss is published back through dbg[7]\n");
    {
        int i;

        rep_up(400);
        ck_int((long)EXT.dbg[7], 400, "dbg[7] starts out as the capacity");

        for (i = 0; i < 50; i++) put_line("droppable filler line goes here");
        m13a_rep_verdict(-347, "USB FOUND, ROM DIRECTORY MISSING");

        ck(EXT.dbg[7] > 0, "a nonzero loss was published");
        ck_int((long)EXT.dbg[7], (long)m13a_rep_lost,
               "dbg[7] carries exactly the internal lost count");
        ck(EXT.dbg[7] != 400,
           "the published value is a loss, NOT the stale capacity");
    }

    /* A clean run must publish ZERO, so Lua can tell "complete" from
       "truncated" without heuristics. */
    printf("[8b] a report that fitted publishes a zero loss\n");
    {
        rep_up(2048);
        m13a_rep_section("#CTRL");
        put_line("/temp0 PASS n=7");
        m13a_rep_verdict(0, "USB STORAGE PASS");

        ck_int((long)EXT.dbg[7], 0, "nothing was lost, and dbg[7] says so");
        ck_int((long)m13a_rep_trunc, 0, "no truncation marker was stamped");
        ck_hasnt(arena, "*** REPORT TRUNCATED",
                 "a complete report carries no truncation marker");
    }

    /* ===================================================================
     * 9. THE VERDICT IS WRITTEN ONCE
     * ===================================================================
     * Two verdicts in one report would be worse than none: the Lua side pages
     * the VERDICT section first and highest, so a stale second one could be
     * the only thing an operator reads. */
    printf("[9] a second verdict cannot displace the first\n");
    {
        rep_up(2048);
        m13a_rep_verdict(-345, "CONTROL FAILED");
        m13a_rep_verdict(0,    "USB STORAGE PASS");

        ck_has(arena, "-345 CONTROL FAILED", "the first verdict stands");
        ck_hasnt(arena, "USB STORAGE PASS",
                 "***** the second verdict was refused *****");
        ck_int((long)m13a_rep_done, 1, "the guard is set");
    }

    /* ===================================================================
     * 10. AN OVER-LONG SINGLE LINE IS MARKED, NOT SILENTLY SHORTENED
     * ===================================================================
     * The assembler is sized so this cannot happen with any path this fixture
     * builds. It is a guard, and a guard that failed silently would reintroduce
     * exactly the ambiguity section 6 exists to prevent. */
    printf("[10] an over-long line is marked as overflowed\n");
    {
        int i;

        rep_up(2048);
        m13a_lb_reset();
        for (i = 0; i < 600; i++) m13a_lb_putc('A');
        m13a_rep_commit(0u);

        ck_has(arena, "<LINE OVERFLOW>",
               "***** the truncated line SAYS it was truncated *****");
        ck_int((long)strlen(arena), (long)m13a_rep_len, "still NUL-terminated");
        ck(canary_ok(2048), "the over-long line did not overrun");
    }

    /* ===================================================================
     * 12. ***** THE SCE-CODED ERROR NORMALISER *****
     * ===================================================================
     * THE STAGE-0B DEFECT. The last hardware run returned -2147352574 from all
     * eight candidate probes and every line rendered "unrecognised negative
     * return", because m13store_errname() computes `e = -rc` and so only ever
     * understood the RAW BSD spelling. The value is an ordinary ENOENT wearing
     * the SCE facility code.
     *
     * The encoding is proven twice in-tree -- runtime/boot.inc:59 names
     * 0x8002000C as ENOMEM, runtime/savedata.c:229 accepts 0x80020011 and -17 as
     * the same EEXIST -- giving SCE = 0x8002_0000 | bsd_errno. */
    printf("[12] ***** SCE-coded returns decode to their real BSD errno *****\n");
    {
        unsigned e;

        /* The observed value, tied to its hex so the two can never drift. */
        ck_int((long)(s32)(u32)0x80020002u, -2147352574L,
               "***** 0x80020002 IS the -2147352574 the console reported *****");

        e = 999u;
        ck_int(m13a_sce_split((s32)(u32)0x80020002u, &e), 1,
               "the observed value is recognised as SCE-coded");
        ck_int((long)e, 2, "***** its BSD errno is 2 *****");
        ck_str(m13a_errname((s32)(u32)0x80020002u),
               "ENOENT -- no such file or directory",
               "***** and it names ENOENT, not 'unrecognised' *****");

        /* THE DISTINCTION THE WHOLE MILESTONE TURNS ON. ENOENT means the
           guessed path is absent; EACCES/EPERM means the sandbox refused a path
           that may well exist. Those are different findings and only the second
           would end M13 as designed. */
        ck_str(m13a_errname((s32)(u32)0x8002000Du),
               "EACCES -- permission denied (SANDBOX REFUSAL)",
               "***** SCE-coded EACCES is spelled as a SANDBOX REFUSAL *****");
        ck_str(m13a_errname((s32)(u32)0x80020001u),
               "EPERM -- operation not permitted (SANDBOX REFUSAL)",
               "SCE-coded EPERM is spelled as a SANDBOX REFUSAL");
        ck_str(m13a_errname((s32)(u32)0x80020013u),
               "ENODEV -- no such device", "SCE-coded ENODEV decodes");

        /* ***** BACKWARD COMPATIBILITY. ***** The raw BSD spelling still works,
           so a firmware entry point that returns -2 is unaffected. */
        ck_int(m13a_sce_split(-2, &e), 0, "a raw BSD errno is NOT SCE-coded");
        ck_str(m13a_errname(-2), "ENOENT -- no such file or directory",
               "the raw BSD spelling still decodes exactly as before");
        ck_str(m13a_errname(-13), "EACCES -- permission denied (SANDBOX REFUSAL)",
               "raw BSD EACCES is unchanged");

        /* ***** IT DOES NOT CONVERT BLINDLY. ***** */
        ck_int(m13a_sce_split((s32)(u32)0x80030002u, &e), 0,
               "***** another facility is NOT rewritten as errno 2 *****");
        ck_str(m13a_errname((s32)(u32)0x80030002u),
               "unrecognised negative return (see the raw hex)",
               "a foreign facility stays honestly unrecognised");
        ck_int(m13a_sce_split((s32)(u32)0x80020000u, &e), 0,
               "facility 0x8002 with a ZERO errno half is not split");
        ck_str(m13a_errname((s32)(u32)0x80020000u),
               "unrecognised negative return (see the raw hex)",
               "***** 0x80020000 is never mis-spelled as 'OK' *****");

        /* An SCE-coded errno with no entry in the table must stay UNKNOWN
           rather than be invented. 0x8002000C is ENOMEM (12), which
           m13store_errname deliberately does not carry. */
        ck_str(m13a_errname((s32)(u32)0x8002000Cu),
               "unrecognised negative return (see the raw hex)",
               "***** an unknown errno is NOT hidden or guessed *****");

        ck_int(m13a_sce_split(0, &e), 0, "a zero return is not an error");
        ck_int(m13a_sce_split(7, &e), 0, "a positive return is not an error");
        ck_str(m13a_errname(7), "OK", "a successful return still reads OK");
    }

    /* ===================================================================
     * 13. THE FIXED-WIDTH HEX FORMATTER
     * ===================================================================
     * Eight digits ALWAYS. A facility code is compared by eye, and 0x80020002
     * only lines up against 0x8002000D when both are printed to one width. */
    printf("[13] raw returns print as fixed-width 8-digit hex\n");
    {
        rep_up(2048);
        m13a_lb_reset(); m13a_lb_hex32(0x80020002u);  m13a_rep_commit(0u);
        m13a_lb_reset(); m13a_lb_hex32(0u);           m13a_rep_commit(0u);
        m13a_lb_reset(); m13a_lb_hex32(0xFFFFFFFFu);  m13a_rep_commit(0u);
        m13a_lb_reset(); m13a_lb_hex32(0xABCDEF01u);  m13a_rep_commit(0u);
        m13a_lb_reset(); m13a_lb_hex32(2u);           m13a_rep_commit(0u);

        ck_str(arena,
               "0x80020002\n0x00000000\n0xFFFFFFFF\n0xABCDEF01\n0x00000002\n",
               "***** never shortened, never widened *****");
        ck(canary_ok(2048), "the hex formatter did not overrun");
    }

    /* ===================================================================
     * 14. ***** THE #ERR SECTION: DISTINCT VALUES, DECODED ONCE EACH *****
     * ===================================================================
     * Eight identical probe failures must not spend eight notification pages
     * saying the same thing. They are deduplicated and counted. */
    printf("[14] #ERR deduplicates raw returns and decodes each one\n");
    {
        int i;

        /* THE EXACT HARDWARE SCENARIO: eight candidates, one raw value. */
        rep_up(2048);
        for (i = 0; i < 8; i++) m13a_err_note((s32)(u32)0x80020002u);
        m13a_rep_errors();

        ck_str(arena,
               "#ERR\n"
               "SIGNED -2147352574\n"
               "HEX    0x80020002\n"
               "ERRNO  2\n"
               "NAME   ENOENT -- no such file or directory\n"
               "SEEN   8 time(s)\n",
               "***** the exact page the operator will read on the console *****");
        ck_int((long)m13a_errtab_n, 1, "eight identical returns are ONE entry");

        /* A positive return is not an error and must not enter the table. */
        rep_up(2048);
        m13a_err_note(0);
        m13a_err_note(5);
        ck_int((long)m13a_errtab_n, 0, "successful returns are not recorded");
        m13a_rep_errors();
        ck_str(arena, "#ERR\nno negative kernel return was recorded\n",
               "an error-free run says so explicitly");

        /* Two distinct values are BOTH reported -- this is precisely the case
           where one candidate returned EACCES and the rest ENOENT, which is the
           finding the probe loop refuses to stop early for. */
        rep_up(2048);
        m13a_err_note((s32)(u32)0x80020002u);
        m13a_err_note((s32)(u32)0x8002000Du);
        m13a_err_note((s32)(u32)0x80020002u);
        m13a_rep_errors();
        ck_int((long)m13a_errtab_n, 2, "two distinct values, two entries");
        ck_has(arena, "SEEN   2 time(s)", "the repeated value counted twice");
        ck_has(arena, "SANDBOX REFUSAL",
               "***** the lone EACCES is NOT lost among the ENOENTs *****");

        /* The table is bounded, and its overflow is reported rather than
           silently dropped. */
        rep_up(2048);
        for (i = 0; i < 12; i++) m13a_err_note(-(100 + i));
        m13a_rep_errors();
        ck_int((long)m13a_errtab_n, 8, "the table stops at its bound");
        ck_int((long)m13a_errtab_ovf, 4, "the surplus was counted");
        ck_has(arena, "4 further distinct value(s) NOT RECORDED",
               "***** the overflow is REPORTED, never silent *****");
        ck(canary_ok(2048), "#ERR did not overrun the buffer");
    }

    /* A probe line must ALSO carry the decoded name, not just the #ERR page --
       and the act of probing must feed the table by itself. */
    printf("[14b] probe lines decode SCE returns and feed the error table\n");
    {
        rep_up(2048);
        m13a_rep_probe("/mnt/usb0", (s32)(u32)0x80020002u);

        ck_str(arena,
               "/mnt/usb0 -> -2147352574 ENOENT -- no such file or directory\n",
               "***** the probe line no longer says 'unrecognised' *****");
        ck_int((long)m13a_errtab_n, 1, "the probe recorded its own raw return");
    }

    /* ===================================================================
     * 15. ***** THE /dev NAME FILTER *****
     * ===================================================================
     * A dirent name is NOT NUL-terminated (m13store.h:105-112) -- it is a
     * pointer plus a length INTO the getdents buffer. A prefix test that walked
     * off the end of `namlen` would read the NEXT entry's bytes and could match
     * a device that is not there, which is the worst possible failure for a
     * probe whose entire output is "is a storage node visible". */
    printf("[15] ***** the device filter respects namlen and case *****\n");
    {
        static const char raw[] = "daXYZ";

        ck_int(m13a_dev_interesting("da0", 3),     1, "da0 matches");
        ck_int(m13a_dev_interesting("ada0", 4),    1, "ada0 matches");
        ck_int(m13a_dev_interesting("ugen0.1", 7), 1, "ugen0.1 matches");
        ck_int(m13a_dev_interesting("usbctl", 6),  1, "usbctl matches");
        ck_int(m13a_dev_interesting("md0", 3),     1, "md0 matches");
        ck_int(m13a_dev_interesting("cd0", 3),     1, "cd0 matches");
        ck_int(m13a_dev_interesting("sd0", 3),     1, "sd0 matches");
        ck_int(m13a_dev_interesting("disk0", 5),   1, "disk0 matches");
        ck_int(m13a_dev_interesting("nvd0", 4),    1, "nvd0 matches");

        ck_int(m13a_dev_interesting("null", 4),   0, "null is not storage");
        ck_int(m13a_dev_interesting("zero", 4),   0, "zero is not storage");
        ck_int(m13a_dev_interesting("random", 6), 0, "random is not storage");
        ck_int(m13a_dev_interesting("ttyu0", 5),  0, "a tty is not storage");
        ck_int(m13a_dev_interesting("DA0", 3),    0,
               "the comparison is CASE-SENSITIVE");

        /* ***** THE BOUNDS TEST. ***** The same buffer, two lengths. */
        ck_int(m13a_dev_interesting(raw, 2), 1,
               "a 2-byte name 'da' inside a longer buffer matches");
        ck_int(m13a_dev_interesting(raw, 1), 0,
               "***** a 1-byte name does NOT match the 2-char prefix 'da' *****");
        ck_int(m13a_dev_interesting(raw, 0), 0, "a zero-length name matches nothing");
        ck_int(m13a_dev_interesting(0, 4),   0, "a null name matches nothing");
    }

    /* ===================================================================
     * 16. THE #DEV SECTION REPORTS WHAT IT HID
     * ===================================================================
     * A filtered listing that did not say how much it dropped would look
     * exactly like a devfs that really did contain three entries. */
    printf("[16] #DEV reports total and shown before any name\n");
    {
        int i;

        rep_up(2048);
        m13a_dev_collect("da0", 3);
        m13a_dev_collect("null", 4);          /* filtered out */
        m13a_dev_collect("ugen0.2", 7);
        m13a_rep_devices(214u, 0);

        ck_str(arena,
               "#DEV\n"
               "total=214\n"
               "shown=2\n"
               "da0\n"
               "ugen0.2\n",
               "***** counts come FIRST, so page 1 states the filter's reach *****");
        ck_int((long)m13a_dev_matched, 2, "only matching names were counted");

        /* An empty match under a nonzero total is a RESULT, not a no-op. */
        rep_up(2048);
        m13a_dev_collect("null", 4);
        m13a_dev_collect("console", 7);
        m13a_rep_devices(180u, 0);
        ck_has(arena, "total=180", "the total is still reported");
        ck_has(arena, "NO STORAGE-LIKE DEVICE NAME MATCHED",
               "***** an empty result is stated, never left blank *****");

        /* A failed open reports its raw return, decoded. */
        rep_up(2048);
        m13a_rep_devices(0u, (int)(s32)(u32)0x80020002u);
        ck_str(arena,
               "#DEV\n"
               "OPEN FAILED rc=-2147352574 ENOENT -- no such file or directory\n",
               "a refused /dev still reports a decoded reason");

        /* The stored list is bounded, and the cap is declared. */
        rep_up(2048);
        for (i = 0; i < 30; i++) m13a_dev_collect("da0", 3);
        ck_int((long)m13a_devname_n, 24, "the name list stops at its bound");
        ck_int((long)m13a_dev_matched, 30, "every match was still COUNTED");
        m13a_rep_devices(300u, 0);
        ck_has(arena, "matched=30", "the uncapped match count is reported");
        ck_has(arena, "*** LIST CAPPED ***",
               "***** the cap is declared, not silent *****");
        ck(canary_ok(2048), "#DEV did not overrun the buffer");

        /* A pathological name is truncated for DISPLAY -- safe here and only
           here, because nothing in M13A ever opens a device node. */
        {
            char big[160];
            for (i = 0; i < 159; i++) big[i] = (i < 2) ? "da"[i] : 'Z';
            big[159] = '\0';

            rep_up(2048);
            m13a_dev_collect(big, 159u);
            ck_int((long)m13a_devname_n, 1, "the over-long name was stored");
            ck_int((long)strlen(m13a_devname[0]), 47,
                   "it was cut to the slot and NUL-terminated");
            ck(canary_ok(2048), "***** the over-long name did not overrun *****");
        }
    }

    /* ===================================================================
     * 11. THE REPORT SINK NEVER TOUCHES THE KERNEL
     * ===================================================================
     * It is a memory formatter. If it ever reached a syscall it would also be
     * capable of reaching a write path, and M13A's central claim -- that it
     * cannot write one byte anywhere -- would need re-proving. */
    printf("[11] ***** the sink reached no kernel entry point at all *****\n");
    {
        ck_int((long)m13a_kernel_was_called, 0,
               "no NC()/SYM() call was made by any report function");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("M13A REPORT EQUIV FAILED\n");
        return 1;
    }
    printf("M13A REPORT EQUIV OK\n");
    return 0;
}
