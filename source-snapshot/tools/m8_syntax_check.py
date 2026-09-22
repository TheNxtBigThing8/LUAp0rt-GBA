#!/usr/bin/env python3
"""Offline structural sanity check for the M8 sources.

NOT A COMPILER AND NOT A SUBSTITUTE FOR ONE. This exists because gcc, make and
lua are absent from this workstation's PATH, so `make m8-link` and `make
m8-verify` cannot be run here at all. It catches the one class of defect that a
large structural edit is most likely to introduce -- unbalanced braces,
parentheses or brackets, and unterminated comments or string literals -- and
says nothing about types, declarations or semantics.

It is deliberately conservative: it strips comments and string/char literals
before counting, so a brace inside a comment or a quoted "}" cannot skew the
result.
"""

import sys


def strip_c(src):
    """Remove C comments and string/char literals, preserving newlines."""
    out = []
    i, n = 0, len(src)
    line = 1
    unterminated = []
    while i < n:
        c = src[i]
        if c == '\n':
            line += 1
            out.append(c)
            i += 1
        elif c == '/' and i + 1 < n and src[i + 1] == '*':
            start = line
            i += 2
            while i + 1 < n and not (src[i] == '*' and src[i + 1] == '/'):
                if src[i] == '\n':
                    line += 1
                    out.append('\n')
                i += 1
            if i + 1 >= n:
                unterminated.append(('block comment', start))
            i += 2
        elif c == '/' and i + 1 < n and src[i + 1] == '/':
            while i < n and src[i] != '\n':
                i += 1
        elif c in '"\'':
            quote, start = c, line
            i += 1
            closed = False
            while i < n:
                if src[i] == '\\':
                    i += 2
                    continue
                if src[i] == '\n':
                    break
                if src[i] == quote:
                    closed = True
                    i += 1
                    break
                i += 1
            if not closed:
                unterminated.append(('%s literal' % quote, start))
        else:
            out.append(c)
            i += 1
    return ''.join(out), unterminated


def check(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        src = f.read()

    stripped, unterminated = strip_c(src)

    problems = []
    for kind, ln in unterminated:
        problems.append('unterminated %s starting at line %d' % (kind, ln))

    pairs = {')': '(', ']': '[', '}': '{'}
    stack = []
    line = 1
    for ch in stripped:
        if ch == '\n':
            line += 1
        elif ch in '([{':
            stack.append((ch, line))
        elif ch in ')]}':
            if not stack:
                problems.append('unmatched closing %r at line %d' % (ch, line))
            elif stack[-1][0] != pairs[ch]:
                problems.append(
                    'mismatched %r at line %d (opened %r at line %d)'
                    % (ch, line, stack[-1][0], stack[-1][1]))
                stack.pop()
            else:
                stack.pop()
    for ch, ln in stack:
        problems.append('unclosed %r opened at line %d' % (ch, ln))

    total = len(src.splitlines())
    if problems:
        print('FAIL %s (%d lines)' % (path, total))
        for p in problems[:20]:
            print('   ' + p)
        return 1
    print('ok   %s (%d lines) -- braces, parens, brackets, comments, strings all balanced'
          % (path, total))
    return 0


def main(argv):
    rc = 0
    for p in argv[1:]:
        rc |= check(p)
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv))
