#!/usr/bin/env python3
"""Source-level gates for the M14A frame profiler and the M14B attribution
fixture (apps/m14diag/main.c, apps/m14bdiag/main.c).

***** THESE ARE THE CHECKS THAT CANNOT BE MADE AGAINST THE BINARY. ***** The
linked image proves that a string is present and that a symbol is referenced. It
cannot prove that the ten frame-loop steps are still in M13C's ORDER, that the
loop takes exactly nine timestamps, that nothing composites a HUD over gameplay,
that no drawn string contains a glyph the font cannot render, or that the
profiler never WRITES skip_next_frame. Those are properties of the SOURCE, so
they are checked here.

***** --app SELECTS THE GATE SET, AND THE DEFAULT IS M14A SO EVERY EXISTING
CALL KEEPS WORKING UNCHANGED. *****

    m14a_loop_check.py apps/m14diag/main.c              M14A gates (default)
    m14a_loop_check.py --app m14a apps/m14diag/main.c   the same, stated
    m14a_loop_check.py --app m14b apps/m14bdiag/main.c  M14A gates + M14B gates

The M14B gates exist because an ATTRIBUTION fixture has a failure mode a
profiler does not: it is under pressure to PRODUCE A NUMBER for a quantity it
cannot measure. M14B cannot separate host pacing wait from CPU work inside
PRESENT or AUDIO SUBMIT -- that needs a timestamp inside runtime/platform.c or
adapters/gba/gba_audio.c, both FROZEN. The gates below make the two tempting
shortcuts into build failures: inventing a wait figure by SUBTRACTION, and
computing the covariance in the n*sum_ps form that OVERFLOWS u64 at the
240-minute backstop. They also pin the residual clamp and its witness, because
a clamp that silently hides an imbalance would make a broken decomposition look
perfect.

Invoked from `make m14diag-check`, `make m14diag-verify`, `make m14bdiag-check`
and `make m14bdiag-verify`. It lives in tools/ rather than in a Makefile
heredoc because this project's Makefile does not set .ONESHELL, so every recipe
line is a separate shell and a multi-line heredoc would silently not run --
which for a NEGATIVE gate means passing vacuously.

Run `m14a_loop_check.py --self-test` to exercise the skip_next_frame mutation
detector AND the M14B subtraction/overflow detectors against their controls.
Those controls also run, silently, ahead of the real check on every invocation:
a negative gate that has quietly stopped matching would pass any file at all, so
it must prove it can still fire before its verdict on the file is believed.

Exit status: 0 = every gate passed, 1 = at least one gate failed (details on
stdout), 2 = the file could not be read or the frame loop could not be located.
"""

import re
import sys

# The ten steps, in the order apps/m13cgpsp/main.c:2761-2902 performs them.
# ***** THE AUDIO BLOCK MUST STAY AFTER plat_video_present. ***** sceAudioOutOutput
# BLOCKS until its queued grain is consumed; submitting past the vsync edge
# overlaps the two waits instead of stacking them. Moving audio above the flip
# would silently re-pace the emulator, so this ordering is a correctness gate and
# not a style preference.
STEPS = [
    "plat_pad_read",
    "m8_input_from_pad",
    "m8_input_apply",
    "m8_input_irq_edge",
    "m5_exec_run",
    "gba_present_blit",
    "plat_video_present",
    "gba_audio_pull",
    "gba_audio_resample",
    "gba_audio_drain",
]

# T0..T8 inclusive. If a phase boundary is ever added or removed, the OVERHEAD
# line on the result screen must be updated with it -- which is why the count is
# pinned here as well as by M14A_TIMER_PER_FRAME in the source.
EXPECT_TIMESTAMPS = 9

LOOP_HEAD = "for (iter = 0; iter < M14A_PLAY_MAX_ITERS"
LOOP_TAIL = "run_t1     = plat_time_us();"

# Anything that would composite pixels. A HUD would add a text pass to every
# frame and charge it to the phase it was measuring.
DRAW_CALLS = (
    "draw_str",
    "draw_hline",
    "draw_centered",
    "ui_fill",
    "blit_ui",
    "m14a_flip",
    "m14a_masthead",
)

# ***** THE PROFILER MAY READ skip_next_frame BUT MAY NEVER WRITE IT. *****
# adapters/gba/gba_fixture.c:70 defines `u32 skip_next_frame = 0;`. Dropping a
# frame would remove that frame's work from every phase accumulator at once, so
# the cheapest possible way to make a profile look good is also the one that
# makes it worthless. M14A asserts the variable every frame and ends the session
# if it ever moves off zero; this gate proves the profiler cannot be the thing
# that moved it.
#
# This replaced a Makefile grep, `^[^/*]*skip_next_frame[ \t]*=[^=]`, which was
# wrong in BOTH directions. It false-positived on a block-comment continuation
# line that quoted the definition above (no '/' or '*' preceded the identifier
# on that line, and grep is line-oriented so it could not see the '/*' opening
# the comment one line earlier). It ALSO missed every compound assignment,
# both increment forms and the address-of escape. A gate that can be fooled by
# `++skip_next_frame` is worse than no gate, because it is trusted.
MUTATIONS = (
    (
        "plain assignment (skip_next_frame = ...)",
        r"\bskip_next_frame\s*=(?!=)",
    ),
    (
        "compound assignment (skip_next_frame op= ...)",
        r"\bskip_next_frame\s*(?:\+|-|\*|/|%|&|\||\^|<<|>>)=",
    ),
    (
        "pre-increment/decrement (++skip_next_frame / --skip_next_frame)",
        r"(?:\+\+|--)\s*\bskip_next_frame\b",
    ),
    (
        "post-increment/decrement (skip_next_frame++ / skip_next_frame--)",
        r"\bskip_next_frame\s*(?:\+\+|--)",
    ),
)

# The address-of case cannot be a bare regex. `&skip_next_frame` hands a writable
# pointer to a callee and is a mutation; `x & skip_next_frame` and
# `if (!stop && skip_next_frame != 0u)` are READS that share the character. The
# second of those is real M14A code, so getting this wrong re-introduces exactly
# the false positive this gate was written to retire.
ADDRESS_ESCAPE = r"&\s*\bskip_next_frame\b"

# A '&' is bitwise-AND or logical-AND -- not address-of -- when the nearest
# preceding non-space character can end an operand.
_OPERAND_TAIL = "_)]&"


# =============================================================================
# M14B -- THE ATTRIBUTION GATES
# =============================================================================

# ---- B1: NO HOST-WAIT FIGURE MAY BE MANUFACTURED BY SUBTRACTION -------------
#
# ***** THIS IS THE CENTRAL HONESTY GATE OF M14B. ***** The question the
# milestone asks is whether AUDIO SUBMIT and PRESENT are host pacing waits or
# CPU work. The fixture CANNOT answer it: the split needs a timestamp on either
# side of sceAudioOutOutput / sceVideoOutSubmitFlip, inside frozen files.
#
# The tempting shortcut is `host_wait = total - something_assumed_to_be_cpu`.
# That produces a plausible number with no measurement behind it, and on a
# photographed result screen it is INDISTINGUISHABLE from a measured one. Any
# identifier that names a wait must therefore never be assigned a difference.
#
# Names, not values, are what this matches: a variable called `flipfail_sum`
# may legitimately hold a subtraction, but one called `host_wait_us` may not.
WAIT_NAME = r"(?:host_?wait|wait_us|waited_us|pacing_wait|block_us|stall_us)"

WAIT_SUBTRACTION = (
    (
        "a wait figure assigned a subtraction",
        r"\b\w*" + WAIT_NAME + r"\w*\s*=(?!=)\s*[^;]*?(?<![-<>!=+*/%&|^])-(?!-)[^;]*;",
    ),
    (
        "a wait figure assigned a guarded-subtraction ternary",
        r"\b\w*" + WAIT_NAME + r"\w*\s*=(?!=)\s*[^;]*\?[^;]*-[^;]*:[^;]*;",
    ),
)

# ---- B2: THE COVARIANCE MUST BE COMPUTED DIVIDE-FIRST ------------------------
#
# ***** n * sum_ps OVERFLOWS u64 AND THE OVERFLOW IS SILENT. ***** At the
# 240-minute watchdog backstop n is about 854,800 sampled frames. With PRESENT
# and AUDIO SUBMIT near their measured means, sum_ps reaches roughly 6.2e14, so
# the textbook form
#
#     Cov = (n * sum_ps - sum_p * sum_s) / (n * n)
#
# needs n * sum_ps ~= 5.3e20 against a u64 ceiling of 1.8e19. It wraps, and a
# wrapped covariance is not a large number or a negative number -- it is an
# ARBITRARY number that will be read as a finding.
#
# The fixture must divide first:  sum_ps/n - (sum_p/n) * (sum_s/n).
COV_OVERFLOW = (
    (
        "n * sum_ps (or a second moment) -- the OVERFLOWING covariance form",
        r"\b(?:\(\s*u64\s*\)\s*)?n\b\s*\*\s*\(?\s*\w*sum_(?:ps|pp|ss)\b",
    ),
    (
        "sum_ps * n -- the same product written the other way round",
        r"\b\w*sum_(?:ps|pp|ss)\b\s*\*\s*\(?\s*(?:\(\s*u64\s*\)\s*)?n\b",
    ),
)

# ---- B3: THE UNSPLITTABLE ROWS MUST SAY SO ON THE SCREEN --------------------
#
# Reporting UNKNOWN is the finding. A row quietly filled in with a number would
# be the milestone failing while looking like it succeeded.
#
# ***** TWO TUPLES, BECAUSE THERE ARE TWO WAYS A LITERAL REACHES THE SCREEN.
# ***** These go STRAIGHT into draw_str / draw_centered / ln_puts, so they are
# required to appear in a drawn-string call:
M14B_REQUIRED_DRAWN = (
    "UNKNOWN",
    "NOT SPLITTABLE",
    "PROVENANCE",
    "MEASURED",
    "DERIVED EXACT",
    "REQUIRE 0",
)

# The eight attribution ROW LABELS are selected by a switch into a `lab`
# pointer which is then drawn, so they never appear as an argument to a draw
# call. Requiring them in a drawn-string call would make this gate fail on a
# correct fixture. They are required as string literals in the (comment-
# stripped) source instead -- prose cannot satisfy them, which is the property
# that matters.
M14B_REQUIRED_LITERALS = (
    "AUDIO SUBMIT TOTAL",
    "AUDIO SUBMIT HOST WAIT",
    "AUDIO SUBMIT NONWAIT",
    "AUDIO SUBMIT UNKNOWN",
    "PRESENT TOTAL",
    "PRESENT HOST WAIT",
    "PRESENT NONWAIT",
    "PRESENT UNKNOWN",
)

# ---- B4: THE RESIDUAL CLAMP IS PRESERVED *AND* WITNESSED --------------------
#
# M14A computes `resid = (d_total > named) ? (d_total - named) : 0u`. The clamp
# is correct -- an unsigned wrap would be worse -- but it is also SILENT: if
# `named` ever exceeded `d_total` the imbalance would vanish and the
# reconciliation would look perfect at precisely the moment it had broken.
#
# M14B must leave the clamp exactly as M14A wrote it AND count the event.
M14B_CLAMP_FORM = (
    r"resid\s*=\s*\(\s*d_total\s*>\s*named\s*\)\s*\?\s*"
    r"\(\s*d_total\s*-\s*named\s*\)\s*:\s*0u\s*;"
)
M14B_CLAMP_WITNESS = r"if\s*\(\s*named\s*>\s*d_total\s*\)"
M14B_CLAMP_COUNTER = r"\bm14b_resid_clamp_n\s*\+\+"

# ---- B5: THE PHASE-COUNT INVARIANTS ----------------------------------------
#
# M14B appends PACING and NONPACING ABOVE TOTAL so no inherited index moves,
# and neither is rankable -- for the same reason TOTAL is not, which is that an
# aggregate wins every ranking and pushes the real answer to rank 2. The
# on-screen table stays at NINE rows because the footer rule is drawn at y=184
# and an eleven-row table would overwrite it.
M14B_PHASE_DEFINES = (
    (r"#define\s+M14A_PH_COUNT\s+11u", "M14A_PH_COUNT must be 11"),
    (r"#define\s+M14A_PH_RANKABLE\s+8u", "M14A_PH_RANKABLE must stay 8"),
    (r"#define\s+M14A_PH_TOTAL\s+8u", "M14A_PH_TOTAL must keep M14A's index"),
    (r"#define\s+M14B_PH_TABLE\s+9u", "the screen table must stay 9 rows"),
    (r"#define\s+M14B_PH_PACING\s+9u", "PACING must sit above TOTAL"),
    (r"#define\s+M14B_PH_NONPACING\s+10u", "NONPACING must sit above PACING"),
)


def strip_comments(src):
    """Remove C comments, preserving newlines so offsets stay usable."""
    out = []
    i, n = 0, len(src)
    while i < n:
        if src[i] == "/" and i + 1 < n and src[i + 1] == "*":
            i += 2
            while i + 1 < n and not (src[i] == "*" and src[i + 1] == "/"):
                out.append("\n" if src[i] == "\n" else " ")
                i += 1
            i += 2
        elif src[i] == "/" and i + 1 < n and src[i + 1] == "/":
            while i < n and src[i] != "\n":
                i += 1
        else:
            out.append(src[i])
            i += 1
    return "".join(out)


def _closes_on_line(src, i, quote):
    """Does an unescaped `quote` close before the next newline?"""
    j, n = i + 1, len(src)
    while j < n and src[j] != "\n":
        if src[j] == "\\":
            j += 2
            continue
        if src[j] == quote:
            return True
        j += 1
    return False


def strip_strings(src):
    """Blank the CONTENTS of string and character literals.

    The delimiters and every newline are preserved, so the result is the same
    length as the input and byte offsets still map to the original line numbers.
    Run this AFTER strip_comments() -- a quote inside a comment would otherwise
    open a literal that swallows the rest of the file.

    This is what keeps `printf("skip_next_frame %u (require 0)\\n", skip_seen)`
    from reading as an assignment: by the time the mutation gate sees that line,
    everything between the quotes is spaces.

    ***** A QUOTE THAT DOES NOT CLOSE ON ITS OWN LINE IS TREATED AS ORDINARY
    TEXT, NOT AS AN OPENING DELIMITER. ***** Neither a C string nor a char
    literal may contain a raw newline, so this costs nothing on real source --
    but it means a stray apostrophe (an English possessive in a comment this
    stripper somehow failed to remove, say) blanks NOTHING instead of swallowing
    the rest of the file. That matters because the failure it prevents is a
    FALSE NEGATIVE: a runaway blanked region would hide a genuine
    `skip_next_frame = 1` from the gate and report the file clean.
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if (c == '"' or c == "'") and _closes_on_line(src, i, c):
            out.append(c)
            i += 1
            while i < n and src[i] != c:
                if src[i] == "\\" and i + 1 < n:
                    # Consume the escape AND its payload, so that a literal
                    # backslash-quote does not look like the closing delimiter.
                    out.append(" ")
                    i += 1
                    out.append("\n" if src[i] == "\n" else " ")
                    i += 1
                    continue
                out.append(" ")
                i += 1
            if i < n:
                out.append(c)
                i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def find_mutations(raw):
    """Return [(line_number, description)] for every write to skip_next_frame.

    Comments and string literals are removed first, so documentation quoting the
    definition and log text naming the variable cannot trip the gate -- while a
    genuine write in any of the forms C provides still does.
    """
    code = strip_strings(strip_comments(raw))
    hits = []

    for description, pattern in MUTATIONS:
        for match in re.finditer(pattern, code):
            hits.append((code.count("\n", 0, match.start()) + 1, description))

    for match in re.finditer(ADDRESS_ESCAPE, code):
        j = match.start() - 1
        while j >= 0 and code[j] in " \t\n":
            j -= 1
        prev = code[j] if j >= 0 else ""
        if prev and (prev.isalnum() or prev in _OPERAND_TAIL):
            continue  # `x & skip_next_frame` / `a && skip_next_frame`: a READ.
        hits.append(
            (
                code.count("\n", 0, match.start()) + 1,
                "address escape (&skip_next_frame hands out a writable pointer)",
            )
        )

    hits.sort()
    return hits


def find_wait_subtraction(raw):
    """Return [(line_number, description)] for every wait figure built by
    subtraction.

    Comments and string literals are stripped first, so the prose explaining
    WHY the split is impossible -- which necessarily uses the words this gate
    matches -- cannot trip it.
    """
    code = strip_strings(strip_comments(raw))
    hits = []

    for description, pattern in WAIT_SUBTRACTION:
        for match in re.finditer(pattern, code):
            hits.append((code.count("\n", 0, match.start()) + 1, description))

    hits.sort()
    return hits


def find_cov_overflow(raw):
    """Return [(line_number, description)] for every overflowing covariance
    product."""
    code = strip_strings(strip_comments(raw))
    hits = []

    for description, pattern in COV_OVERFLOW:
        for match in re.finditer(pattern, code):
            hits.append((code.count("\n", 0, match.start()) + 1, description))

    hits.sort()
    return hits


def drawn_strings(src):
    """Every string literal that actually reaches the screen.

    draw_str / draw_centered / ln_puts are the three paths to the framebuffer.
    printf text goes to the UDP log and is deliberately NOT collected here.
    """
    out = []
    for pattern in (
        r"draw_str\s*\([^;]*?\"((?:[^\"\\]|\\.)*)\"",
        r"draw_centered\s*\([^;]*?\"((?:[^\"\\]|\\.)*)\"",
        r"ln_puts\s*\(\s*&?\w+\s*,\s*\"((?:[^\"\\]|\\.)*)\"",
    ):
        for match in re.finditer(pattern, src, re.S):
            out.append(match.group(1))
    return out


def all_literals(src):
    """Every string literal in the (already comment-stripped) source.

    Used for text that reaches the screen through a variable rather than as a
    direct argument -- the eight attribution row labels are chosen by a switch
    into a `lab` pointer, so a drawn-string scan cannot see them.
    """
    return re.findall(r"\"((?:[^\"\\\n]|\\.)*)\"", src)


# ---- self-test ------------------------------------------------------------
#
# A negative gate that has never been shown to fire is indistinguishable from a
# gate that cannot fire. `--self-test` proves this one still catches every
# mutation form, including the ones the retired Makefile regex could not see.
MUST_FAIL = (
    "skip_next_frame = 1;",
    "skip_next_frame =0;",
    "skip_next_frame += 1;",
    "skip_next_frame -= 1;",
    "skip_next_frame *= 2;",
    "skip_next_frame /= 2;",
    "skip_next_frame %= 2;",
    "skip_next_frame &= 1;",
    "skip_next_frame |= 1;",
    "skip_next_frame ^= 1;",
    "skip_next_frame <<= 1;",
    "skip_next_frame >>= 1;",
    "++skip_next_frame;",
    "--skip_next_frame;",
    "skip_next_frame++;",
    "skip_next_frame--;",
    "foo(&skip_next_frame);",
    "skip_next_frame\t=\t1;",
    "    skip_next_frame     =     1;",
    "if (x) skip_next_frame = 1;",
    "u32 *p = &skip_next_frame;",
    # A mutation must stay visible no matter what precedes it on earlier lines.
    # These are the regression tests for the literal-stripper: an unbalanced
    # quote must not blank the rest of the file and hide the write below it.
    "/* M13C's order */\nskip_next_frame = 1;",
    "const char *s = \"M13C's\";\nskip_next_frame = 1;",
    "a = b; '\nskip_next_frame = 1;",
    "char c = '\\'';\nskip_next_frame = 1;",
)

MUST_PASS = (
    "extern unsigned int skip_next_frame;",
    "if (skip_next_frame != 0u) {}",
    "if (skip_next_frame == 0u) {}",
    "if (!stop && skip_next_frame != 0u) {}",
    "x = skip_next_frame;",
    "skip_seen = skip_next_frame;",
    "/* skip_next_frame = 1; */",
    "// skip_next_frame = 1;",
    "/* `u32 skip_next_frame = 0;`. It is LUAport-owned, not a gpSP */",
    'printf("skip_next_frame = 1");',
    'printf("M14A: skip_next_frame %u (require 0)\\n", skip_seen);',
    "y = flags & skip_next_frame;",
    "return skip_next_frame;",
    # Char literals and English possessives must not make a legal READ look
    # like a write -- the mirror image of the MUST_FAIL stripper controls.
    "/* M13C's order */\nif (skip_next_frame != 0u) {}",
    "ln_putc(l, '-'); x = skip_next_frame;",
    "l->b[l->n] = '\\0'; x = skip_next_frame;",
)


# ***** THE M14B CONTROLS. ***** Same discipline as the skip_next_frame
# controls above: a gate that has never been shown to fire cannot be
# distinguished from one that cannot fire.
WAIT_MUST_FAIL = (
    "host_wait = total - cpu;",
    "host_wait_us = d_total - named;",
    "hostwait = c - work;",
    "wait_us = f3 - submit_cpu;",
    "submit_host_wait = f3 - grains * per_grain;",
    "present_wait_us = c - blit_cost;",
    "pacing_wait = (a > b) ? (a - b) : 0u;",
    "u64 host_wait = total-cpu;",
    "stall_us = t8 - t7;",
    "block_us = total - (a + b);",
)

WAIT_MUST_PASS = (
    # Reporting the quantity as unavailable is the whole point.
    'draw_str(s, x, y, "PRESENT HOST WAIT", C);',
    'draw_str(s, x, y, "UNKNOWN", C);',
    # A comment explaining the impossibility necessarily uses the words.
    "/* host_wait = total - cpu would be a guess */",
    # Additions, and sums, are not subtractions.
    "host_wait_sum += c;",
    "u64 host_wait_n = wait_cnt + 1u;",
    # Identifiers that merely CONTAIN a subtraction but name no wait.
    "flipfail_sum = a - b;",
    "resid = (d_total > named) ? (d_total - named) : 0u;",
    "u64 nonpacing = (d_total > pacing) ? (d_total - pacing) : 0u;",
    # A comparison is not an assignment.
    "if (host_wait_us > 0u) {}",
)

COV_MUST_FAIL = (
    "cov = (n * sum_ps - sum_p * sum_s) / (n * n);",
    "u64 num = n * m14b_sum_ps;",
    "x = (u64)n * sum_ps;",
    "y = sum_ps * n;",
    "z = m14b_sum_pp * (u64)n;",
    "w = n * (sum_ss);",
)

COV_MUST_PASS = (
    # The divide-first form, which is what the fixture must use.
    "eps = m14b_sum_ps / (u64)n;",
    "ex2 = (m14b_sum_pp / (u64)n) + 2u * (m14b_sum_ps / (u64)n);",
    "mean = m14a_ph[M14B_PH_PACING].sum / (u64)n;",
    # A different variable that merely ends in n.
    "x = len * sum_ps_scaled;",
    # Prose describing the forbidden form must not trip the gate.
    "/* n * sum_ps overflows u64 */",
)


def self_test():
    bad = []

    for snippet in MUST_FAIL:
        if not find_mutations(snippet):
            bad.append("NOT DETECTED (must fail): %s" % snippet)

    for snippet in MUST_PASS:
        hits = find_mutations(snippet)
        if hits:
            bad.append(
                "FALSE POSITIVE (must pass): %s  <-- %s"
                % (snippet, hits[0][1])
            )

    for snippet in WAIT_MUST_FAIL:
        if not find_wait_subtraction(snippet):
            bad.append("WAIT NOT DETECTED (must fail): %s" % snippet)

    for snippet in WAIT_MUST_PASS:
        hits = find_wait_subtraction(snippet)
        if hits:
            bad.append(
                "WAIT FALSE POSITIVE (must pass): %s  <-- %s"
                % (snippet, hits[0][1])
            )

    for snippet in COV_MUST_FAIL:
        if not find_cov_overflow(snippet):
            bad.append("COV NOT DETECTED (must fail): %s" % snippet)

    for snippet in COV_MUST_PASS:
        hits = find_cov_overflow(snippet)
        if hits:
            bad.append(
                "COV FALSE POSITIVE (must pass): %s  <-- %s"
                % (snippet, hits[0][1])
            )

    if bad:
        print("M14A/M14B DETECTOR SELF-TEST FAILED:")
        for b in bad:
            print("  * %s" % b)
        return 1

    print("M14A/M14B detector self-test OK:")
    print("  skip_next_frame: %d mutation forms detected "
          "(assign, 10 compound, ++/--, &escape)" % len(MUST_FAIL))
    print("  skip_next_frame: %d read/declaration/comment/string forms ignored"
          % len(MUST_PASS))
    print("  M14B wait-by-subtraction: %d forms detected, %d honest forms ignored"
          % (len(WAIT_MUST_FAIL), len(WAIT_MUST_PASS)))
    print("  M14B covariance overflow: %d forms detected, %d divide-first "
          "forms ignored" % (len(COV_MUST_FAIL), len(COV_MUST_PASS)))
    return 0


def main():
    argv = sys.argv[1:]

    if len(argv) == 1 and argv[0] == "--self-test":
        return self_test()

    # ***** --app IS OPTIONAL AND DEFAULTS TO m14a. ***** Every pre-existing
    # invocation -- `m14a_loop_check.py apps/m14diag/main.c` -- keeps working
    # with exactly the gate set it had before M14B existed.
    app = "m14a"
    if len(argv) >= 2 and argv[0] == "--app":
        app = argv[1].lower()
        argv = argv[2:]

    if app not in ("m14a", "m14b"):
        print("M14A LOOP CHECK FAILED: unknown --app %r (expected m14a or m14b)"
              % app)
        return 2

    if len(argv) != 1:
        print("usage: m14a_loop_check.py [--app m14a|m14b] <main.c>")
        print("       m14a_loop_check.py --self-test")
        return 2

    tag = app.upper()

    path = argv[0]
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            raw = fh.read()
    except OSError as exc:
        print("%s LOOP CHECK FAILED: cannot read %s: %s" % (tag, path, exc))
        return 2

    # The order and HUD gates run against comment-stripped source, so a step
    # named in a comment cannot satisfy a gate and a draw call mentioned in a
    # comment cannot trip one.
    src = strip_comments(raw)

    start = src.find(LOOP_HEAD)
    if start < 0:
        print("%s LOOP CHECK FAILED: the frame loop was not found." % tag)
        print("  Expected a line beginning: %s" % LOOP_HEAD)
        return 2

    end = src.find(LOOP_TAIL, start)
    if end < 0:
        print("%s LOOP CHECK FAILED: the end of the frame loop was not found."
              % tag)
        print("  Expected, after the loop: %s" % LOOP_TAIL)
        return 2

    body = src[start:end]
    failures = []

    # ---- gate 1: all ten steps present, IN ORDER --------------------------
    pos = {}
    for step in STEPS:
        found = re.search(r"\b%s\s*\(" % re.escape(step), body)
        pos[step] = found.start() if found else -1

    missing = [s for s in STEPS if pos[s] < 0]
    if missing:
        failures.append(
            "a frame-loop step is absent from the loop body: %s"
            % ", ".join(missing)
        )
    else:
        seen = [pos[s] for s in STEPS]
        if seen != sorted(seen):
            actual = sorted(STEPS, key=lambda s: pos[s])
            failures.append(
                "the ten steps are OUT OF ORDER in the loop body.\n"
                "  expected: %s\n"
                "  actual:   %s\n"
                "  The audio block must stay AFTER plat_video_present, or the\n"
                "  emulator is silently re-paced and the profile describes a\n"
                "  different program." % (" -> ".join(STEPS), " -> ".join(actual))
            )

    # ---- gate 2: exactly nine timestamps ----------------------------------
    n_ts = len(re.findall(r"\bplat_time_us\s*\(\s*\)", body))
    if n_ts != EXPECT_TIMESTAMPS:
        failures.append(
            "the loop body takes %d plat_time_us() readings, expected %d.\n"
            "  The OVERHEAD line multiplies the measured per-call cost by\n"
            "  M14A_TIMER_PER_FRAME, so a boundary added or removed here must\n"
            "  be reflected there or the corrected figure becomes wrong."
            % (n_ts, EXPECT_TIMESTAMPS)
        )

    # ---- gate 3: no HUD composited during gameplay ------------------------
    drawn = [c for c in DRAW_CALLS if re.search(r"\b%s\s*\(" % re.escape(c), body)]
    if drawn:
        failures.append(
            "the frame loop composites UI: %s\n"
            "  A per-frame HUD would add a text pass to every frame and charge\n"
            "  it to the phase it was measuring." % ", ".join(drawn)
        )

    # ---- gate 4: the loop never ends on the clock -------------------------
    # It reads plat_time_us() nine times a frame and must COMPARE IT TO NOTHING.
    # A comparison of a timestamp against a budget is the one thing that would
    # turn this instrument back into M13B-5's timed window.
    for pat in (r"M14A_SESSION_SECONDS", r"\brun_max_us\b", r"M14A_END_SESSION"):
        if re.search(pat, src):
            failures.append(
                "a session-duration terminator survives: %s\n"
                "  Measuring time is not the same as being governed by it."
                % pat.strip("\\b")
            )

    # ---- gate 5: no percent sign in any DRAWN string ----------------------
    # runtime/gfx.h:30-46: font_data covers ASCII 32..90, so '%' renders as a
    # BLANK CELL. printf format strings are exempt -- they go to the UDP log and
    # never reach draw_str. A profiler wants to write "20 % of frame cost" more
    # than any other fixture, which is why this gate matters most here.
    bad_glyph = []
    for pattern in (
        r"draw_str\s*\([^;]*?\"((?:[^\"\\]|\\.)*)\"",
        r"draw_centered\s*\([^;]*?\"((?:[^\"\\]|\\.)*)\"",
        r"ln_puts\s*\(\s*&?\w+\s*,\s*\"((?:[^\"\\]|\\.)*)\"",
    ):
        for match in re.finditer(pattern, src, re.S):
            text = match.group(1)
            if "%" in text:
                bad_glyph.append(text)

    if bad_glyph:
        failures.append(
            "a DRAWN string contains a percent sign, which the font renders as\n"
            "  a blank cell: %s" % "; ".join(repr(t) for t in bad_glyph)
        )

    # ---- gate 6: the profiler never WRITES skip_next_frame ----------------
    # Whole-file, not just the loop body: a write anywhere -- in setup, in a
    # helper, in teardown -- would skip frames the profiler then fails to
    # account for.
    #
    # The controls run FIRST. A negative gate that has silently stopped
    # matching would pass this file no matter what it contained, so the
    # detector must demonstrate it still fires before its verdict is believed.
    control_bad = []
    for snippet in MUST_FAIL:
        if not find_mutations(snippet):
            control_bad.append("did not detect: %s" % snippet)
    for snippet in MUST_PASS:
        if find_mutations(snippet):
            control_bad.append("false positive on: %s" % snippet)

    if control_bad:
        failures.append(
            "the skip_next_frame mutation detector FAILED ITS OWN CONTROLS and\n"
            "  its verdict on this file cannot be trusted:\n    %s"
            % "\n    ".join(control_bad)
        )
    else:
        lines = raw.splitlines()
        mutations = find_mutations(raw)
        if mutations:
            detail = "\n".join(
                "    line %d: %s\n      %s"
                % (ln, why, lines[ln - 1].strip() if ln <= len(lines) else "?")
                for ln, why in mutations
            )
            failures.append(
                "the profiler WRITES skip_next_frame. It must only ever READ it:\n"
                "%s\n"
                "  Skipping a frame removes that frame's work from every phase\n"
                "  accumulator at once, so it is the cheapest way to make a\n"
                "  profile look good and the fastest way to make it worthless."
                % detail
            )

    # =====================================================================
    # THE M14B GATES. They run ONLY under --app m14b, so M14A's verdict is
    # bit-for-bit what it was before this milestone existed.
    # =====================================================================
    if app == "m14b":
        lines = raw.splitlines()

        def _detail(hits):
            return "\n".join(
                "    line %d: %s\n      %s"
                % (ln, why, lines[ln - 1].strip() if ln <= len(lines) else "?")
                for ln, why in hits
            )

        # ---- gate B0: the M14B detectors prove they still fire ------------
        b_control = []
        for snippet in WAIT_MUST_FAIL:
            if not find_wait_subtraction(snippet):
                b_control.append("wait detector did not detect: %s" % snippet)
        for snippet in WAIT_MUST_PASS:
            if find_wait_subtraction(snippet):
                b_control.append("wait detector false positive on: %s" % snippet)
        for snippet in COV_MUST_FAIL:
            if not find_cov_overflow(snippet):
                b_control.append("cov detector did not detect: %s" % snippet)
        for snippet in COV_MUST_PASS:
            if find_cov_overflow(snippet):
                b_control.append("cov detector false positive on: %s" % snippet)

        if b_control:
            failures.append(
                "an M14B detector FAILED ITS OWN CONTROLS and its verdict on\n"
                "  this file cannot be trusted:\n    %s"
                % "\n    ".join(b_control)
            )
        else:
            # ---- gate B1: no wait figure manufactured by subtraction ------
            waits = find_wait_subtraction(raw)
            if waits:
                failures.append(
                    "a HOST WAIT figure is produced by SUBTRACTION:\n%s\n"
                    "  M14B cannot separate host pacing wait from CPU work --\n"
                    "  that needs a timestamp inside runtime/platform.c or\n"
                    "  adapters/gba/gba_audio.c, both FROZEN. A subtraction\n"
                    "  produces a plausible number with no measurement behind\n"
                    "  it, and on a photographed screen it is indistinguishable\n"
                    "  from a measured one. The honest output is UNKNOWN."
                    % _detail(waits)
                )

            # ---- gate B2: the covariance must divide first ----------------
            covs = find_cov_overflow(raw)
            if covs:
                failures.append(
                    "the covariance uses the OVERFLOWING n*sum_ps form:\n%s\n"
                    "  At the 240-minute backstop n is about 854,800 and\n"
                    "  n*sum_ps reaches about 5.3e20 against a u64 ceiling of\n"
                    "  1.8e19. It wraps SILENTLY, and a wrapped covariance is\n"
                    "  an arbitrary number that will be read as a finding.\n"
                    "  Use sum_ps/n - (sum_p/n)*(sum_s/n)."
                    % _detail(covs)
                )

        # ---- gate B3: the unsplittable rows must say so ------------------
        blob = "\n".join(drawn_strings(src))
        missing_rows = [s for s in M14B_REQUIRED_DRAWN if s not in blob]
        if missing_rows:
            failures.append(
                "an attribution label is absent from the DRAWN strings: %s\n"
                "  Every figure must carry a PROVENANCE label, the\n"
                "  unsplittable rows must read UNKNOWN / NOT SPLITTABLE, and\n"
                "  the residual clamp witness must sit under REQUIRE 0.\n"
                "  Reporting what cannot be known IS the finding."
                % ", ".join(repr(s) for s in missing_rows)
            )

        lit = "\n".join(all_literals(src))
        missing_lit = [s for s in M14B_REQUIRED_LITERALS if s not in lit]
        if missing_lit:
            failures.append(
                "an attribution ROW LABEL is absent from the source: %s\n"
                "  The eight rows name AUDIO SUBMIT and PRESENT explicitly and\n"
                "  each is followed by TOTAL / HOST WAIT / NONWAIT / UNKNOWN,\n"
                "  so a reader of a photograph can see exactly which part of\n"
                "  the phase was measured and which part could not be."
                % ", ".join(repr(s) for s in missing_lit)
            )

        # ---- gate B4: the residual clamp is preserved AND witnessed -------
        if not re.search(M14B_CLAMP_FORM, src):
            failures.append(
                "M14A's residual clamp has been altered or removed.\n"
                "  It must stay EXACTLY:\n"
                "    resid = (d_total > named) ? (d_total - named) : 0u;\n"
                "  M14B's job is to WITNESS that clamp, not to rewrite it."
            )
        if not re.search(M14B_CLAMP_WITNESS, src):
            failures.append(
                "the residual clamp is not witnessed: no `if (named > d_total)`.\n"
                "  The clamp is SILENT. If `named` ever exceeded `d_total` the\n"
                "  imbalance would vanish and the reconciliation would look\n"
                "  perfect at precisely the moment it had broken."
            )
        if not re.search(M14B_CLAMP_COUNTER, src):
            failures.append(
                "the residual clamp witness never increments its counter.\n"
                "  m14b_resid_clamp_n++ must run inside the witness branch, or\n"
                "  the gate above passes while counting nothing."
            )

        # ---- gate B5: the phase-count invariants --------------------------
        for pattern, why in M14B_PHASE_DEFINES:
            if not re.search(pattern, src):
                failures.append(
                    "a phase-count invariant is broken: %s\n"
                    "  Expected to find: %s\n"
                    "  PACING and NONPACING are appended ABOVE TOTAL so no\n"
                    "  inherited index moves, neither is rankable (an aggregate\n"
                    "  wins every ranking and pushes the real answer to rank 2),\n"
                    "  and the on-screen table stays at NINE rows because the\n"
                    "  footer rule is drawn at y=184." % (why, pattern)
                )

    if failures:
        print("%s LOOP CHECK FAILED:" % tag)
        for f in failures:
            print("  * %s" % f)
        return 1

    print("%s loop check OK:" % tag)
    print("  the ten frame-loop steps are present and IN M13C's ORDER")
    print("  the loop body takes exactly %d timestamps" % EXPECT_TIMESTAMPS)
    print("  the frame loop composites nothing -- no per-frame HUD")
    print("  no session-duration terminator: the loop cannot time out")
    print("  no drawn string contains a percent sign")
    print("  skip_next_frame is never written: no assignment, no compound")
    print("    assignment, no ++/--, no address escape  (%d/%d controls)"
          % (len(MUST_FAIL), len(MUST_PASS)))
    if app == "m14b":
        print("  no HOST WAIT figure is produced by subtraction  (%d/%d controls)"
              % (len(WAIT_MUST_FAIL), len(WAIT_MUST_PASS)))
        print("  the covariance divides first: no n*sum_ps  (%d/%d controls)"
              % (len(COV_MUST_FAIL), len(COV_MUST_PASS)))
        print("  all %d provenance labels are drawn, including UNKNOWN, and all"
              % len(M14B_REQUIRED_DRAWN))
        print("    %d attribution row labels are present"
              % len(M14B_REQUIRED_LITERALS))
        print("  M14A's residual clamp is preserved, witnessed and counted")
        print("  the phase-count invariants hold: 11 accumulators, 8 rankable,")
        print("    9 screen rows, PACING and NONPACING above TOTAL")
    return 0


if __name__ == "__main__":
    sys.exit(main())
