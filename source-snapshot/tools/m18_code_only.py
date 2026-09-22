#!/usr/bin/env python3
"""Emit a C source with comments and literal CONTENT blanked out, one line in,
one line out, so that structural greps can talk about EXECUTABLE CODE.

WHY THIS EXISTS -- THE DEFECT IT REPAIRS
----------------------------------------
m18diag-verify proves structural invariants about runtime/net.c by grepping it
and comparing line numbers. Those greps read RAW LINES, and a raw line cannot
tell code from commentary. On the M18-0b host run that difference stopped a
build for no reason:

    net.c:435  if (rc < 0) {
    net.c:442      * cleared fd, the cleared opened flag and the
                     PLAT_NET_ENONBLOCK return          <-- PROSE
    net.c:534      return PLAT_NET_ENONBLOCK;           <-- the real exit

The gate "the nonblock failure branch has exactly one exit" found TWO lines
matching \\<return\\>, reported two exits, and failed. Line 442 is the fourth
line of a sentence inside the block comment opened at 436 and closed at 492. It
emits no instructions. The invariant held; the PREDICATE was wrong, because it
was a claim about line text pretending to be a claim about control flow.

The fix is not to special-case line 442. It is to stop handing commentary to
checks whose stated invariant is about executable code.

WHAT IT GUARANTEES
------------------
  * EXACTLY one output line per input line, and -- stronger -- every output
    line has EXACTLY the same LENGTH as its input line. Comment and literal
    characters are replaced by spaces, never deleted. Both line NUMBERS and
    COLUMN positions therefore survive, so a grep -n on the output names the
    true line in the original file and the Makefile can keep comparing line
    numbers exactly as it does today.
  * Delimiters are preserved, content is blanked: printf("return x;") becomes
    printf("          "). The call is still visibly a call; the word `return`
    inside the literal is gone.

PRIOR ART, AND WHY THIS IS NOT IT
---------------------------------
tools/m8_syntax_check.py already carries strip_c(), which removes comments and
literals for brace balancing, and it is the right idea. It is deliberately NOT
reused here, for two reasons:

  1. It DELETES characters and re-emits newlines, so it preserves the line
     count only incidentally. Inside a string literal it advances past a
     backslash pair with `i += 2`; if that pair is a backslash-newline (a legal
     C line continuation) the newline is consumed and the output silently
     loses a line. Every line-number invariant downstream would then be off by
     one -- the exact failure mode that is most dangerous here, because it is
     silent. This module is line-preserving BY CONSTRUCTION: it iterates over
     lines and emits one per input line, so no code path can change the count.
  2. Its contract is "is this file syntactically balanced", not "what is the
     executable subset". Coupling a safety gate to a helper with a different
     stated purpose invites someone to change one and break the other.

LIMITATIONS, STATED PLAINLY
---------------------------
This is a LEXER, not a compiler. It does not evaluate the preprocessor, so code
inside a false #if is still reported as code -- that is deliberate and
conservative: it can only ever cause a gate to fail loudly, never to pass
silently. Trigraphs are not handled, and neither is a KEYWORD split across a
backslash-continued line (a "ret", a backslash, a newline, then "urn"). Neither
appears in this tree, and both would fail closed rather than open.

USAGE
    m18_code_only.py --selftest            run the built-in synthetic tests
    m18_code_only.py <src.c> <out.c>       write the code-only view of src.c
"""

import re
import sys

DQ = '"'
SQ = "'"


def filter_line(line, st):
    """Blank comments and literal content in ONE physical line.

    `st` is the carry-over lexer state, mutated in place:
        st['blk'] -- inside a /* ... */ block comment
        st['q']   -- inside a "..." or '...' literal (holds the delimiter)
        st['lc']  -- inside a // comment continued by a trailing backslash

    Returns a string of EXACTLY len(line) characters.
    """
    n = len(line)

    # A // comment carried over by a backslash at the end of the previous line
    # swallows this whole line, and keeps swallowing while the backslash
    # repeats.
    if st['lc']:
        st['lc'] = line.endswith('\\')
        return ' ' * n

    out = []
    i = 0
    while i < n:
        if st['blk']:
            j = line.find('*/', i)
            if j < 0:
                out.append(' ' * (n - i))
                i = n
            else:
                out.append(' ' * (j + 2 - i))
                i = j + 2
                st['blk'] = False
        elif st['q']:
            c = line[i]
            if c == '\\':
                # Escape pair. Blank BOTH characters, so that \" does not close
                # the literal and \\ does not make the next quote look escaped.
                # A trailing backslash is a 1-character tail, not 2 -- taking 2
                # there would make the output line longer than the input.
                take = 2 if i + 1 < n else 1
                out.append(' ' * take)
                i += take
            elif c == st['q']:
                out.append(c)
                st['q'] = ''
                i += 1
            else:
                out.append(' ')
                i += 1
        else:
            c = line[i]
            two = line[i:i + 2]
            if two == '/*':
                out.append('  ')
                st['blk'] = True
                i += 2
            elif two == '//':
                out.append(' ' * (n - i))
                st['lc'] = line.endswith('\\')
                i = n
            elif c == DQ or c == SQ:
                st['q'] = c
                out.append(c)
                i += 1
            else:
                out.append(c)
                i += 1

    return ''.join(out)


def filter_text(data):
    """Filter a whole file. Returns (text, per_line_length_mismatches)."""
    st = {'blk': False, 'q': '', 'lc': False}
    src_lines = data.split('\n')
    out_lines = []
    bad = []
    for idx, line in enumerate(src_lines, 1):
        got = filter_line(line, st)
        if len(got) != len(line):
            bad.append((idx, len(line), len(got)))
        out_lines.append(got)
    return '\n'.join(out_lines), bad


# --------------------------------------------------------------------------
# Self-test.
#
# Every line that MUST lose its `return` carries the marker MUSTVANISH; every
# line whose `return` MUST survive carries MUSTSURVIVE. The test then asserts
# BOTH directions, because a filter that blanks everything would satisfy the
# first on its own and prove nothing.
# --------------------------------------------------------------------------
SELFTEST = [
    'return MUSTSURVIVE_1;',
    '/* return MUSTVANISH_2; */',
    '// return MUSTVANISH_3;',
    'printf("return MUSTVANISH_4;");',
    'if (x) return MUSTSURVIVE_5;',
    '/* comment */ return MUSTSURVIVE_6;',
    's = "escaped quote: \\" return MUSTVANISH_7";',
    '/* a multi-line block comment opens here',
    ' * return MUSTVANISH_9;',
    ' * and closes here */ return MUSTSURVIVE_10;',
    "c = '\"'; return MUSTSURVIVE_11;",
    "c = '\\''; return MUSTSURVIVE_12;",
    '/* he said "hi" // still inside the comment */ return MUSTSURVIVE_13;',
    's = "a \\\\"; return MUSTSURVIVE_14;',
    '// a line comment continued by a backslash MUSTVANISH_15 \\',
    '   return MUSTVANISH_16;',
    'return MUSTSURVIVE_17;',
]

# 1-based line numbers whose filtered form must still contain the WORD return.
SELFTEST_RETURNS = {1, 5, 6, 10, 11, 12, 13, 14, 17}

WORD_RETURN = re.compile(r'\breturn\b')


def selftest():
    src = '\n'.join(SELFTEST)
    out, bad = filter_text(src)
    out_lines = out.split('\n')
    problems = []

    if len(out_lines) != len(SELFTEST):
        problems.append('line count changed: %d in, %d out'
                        % (len(SELFTEST), len(out_lines)))
    for idx, want, got in bad:
        problems.append('line %d changed LENGTH: %d in, %d out'
                        % (idx, want, got))

    for idx, line in enumerate(out_lines, 1):
        if 'MUSTVANISH' in line:
            problems.append(
                'line %d: a commented/quoted token SURVIVED the filter: %r'
                % (idx, line))

    got_returns = {idx for idx, line in enumerate(out_lines, 1)
                   if WORD_RETURN.search(line)}
    for idx in sorted(SELFTEST_RETURNS - got_returns):
        problems.append(
            'line %d: an EXECUTABLE return DISAPPEARED. The filter is eating '
            'code, and every gate built on it would pass vacuously. Source: %r'
            % (idx, SELFTEST[idx - 1]))
    for idx in sorted(got_returns - SELFTEST_RETURNS):
        problems.append(
            'line %d: a NON-executable return survived. Source: %r'
            % (idx, SELFTEST[idx - 1]))

    if problems:
        print('M18 VERIFY FAILED: the code-only filter self-test FAILED.')
        for p in problems:
            print('    ' + p)
        print('    Refusing to run structural gates through a filter that '
              'cannot separate code from commentary.')
        return 1

    print('code-only filter self-test: %d synthetic lines, %d executable '
          'returns kept, every commented/quoted return removed, all line '
          'lengths preserved'
          % (len(SELFTEST), len(SELFTEST_RETURNS)))
    return 0


def produce(src_path, out_path):
    with open(src_path, 'r', encoding='utf-8', errors='replace',
              newline='') as f:
        data = f.read()

    if '\r' in data:
        print('M18 VERIFY FAILED: %s contains carriage returns. The code-only'
              % src_path)
        print('    view and the line-number invariants built on it assume LF'
              ' endings.')
        return 1

    out, bad = filter_text(data)

    if bad:
        print('M18 VERIFY FAILED: the code-only filter changed the LENGTH of '
              '%d line(s) of %s.' % (len(bad), src_path))
        for idx, want, got in bad[:10]:
            print('    line %d: %d characters in, %d out' % (idx, want, got))
        print('    Column positions must be preserved exactly, or a gate that '
              'anchors a pattern could silently change meaning.')
        return 1

    n_src = data.count('\n')
    n_out = out.count('\n')
    if n_src != n_out:
        print('M18 VERIFY FAILED: the code-only filter changed the LINE COUNT '
              'of %s (%d -> %d).' % (src_path, n_src, n_out))
        print('    Every line-number invariant in m18diag-verify would be '
              'nonsense. Refusing to emit it.')
        return 1

    with open(out_path, 'w', encoding='utf-8', newline='') as f:
        f.write(out)

    print('code-only view of %s -> %s (%d lines, unchanged count)'
          % (src_path, out_path, n_src))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == '--selftest':
        return selftest()
    if len(argv) == 3:
        return produce(argv[1], argv[2])
    print('usage: m18_code_only.py --selftest')
    print('       m18_code_only.py <src.c> <out.c>')
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
