/* LUAport M1 -- adapters/gba/gba_compat.c
 *
 * A minimal sscanf, and nothing else.
 *
 * See adapters/gba/gba_compat.h for why this file exists rather than letting
 * --gc-sections hide the dependency. In one line: the authoritative M1
 * measurement links whole objects with no section GC, so an unsatisfied
 * reference has to be satisfied honestly or the baseline is not a baseline.
 *
 * WHAT IS SUPPORTED, AND WHY EXACTLY THIS MUCH
 * --------------------------------------------
 * The Tier 1 closure contains exactly ONE sscanf call:
 *
 *     gpsp/cheats.c:265
 *         u32 op1; u16 op2;
 *         if (2 != sscanf(&buf[pos], "%08x %04hx", &op1, &op2))
 *
 * So the required feature set is:
 *   - the 'x' conversion (hexadecimal, unsigned)
 *   - a field width  (the 8 in %08x, the 4 in %04hx)
 *   - the 'h' length modifier (%04hx stores through a u16 *)
 *   - a space in the format, meaning "skip any run of whitespace"
 *
 * 'd', 'i', 'u' and 'o' are implemented alongside 'x' because they fall out of
 * the same digit loop for no extra code of consequence, and because a scanf
 * that silently mis-parses an unsupported conversion is a far worse failure
 * mode than one that refuses it. Anything genuinely unsupported (%s, %c, %f,
 * %n, assignment suppression, scansets) stops the parse and returns the count
 * so far, which is the standard's matching-failure behaviour -- it never
 * invents a value and never writes through a pointer it did not parse.
 *
 * DELIBERATELY NOT IMPLEMENTED: floating point (no call site, and it would pull
 * in real work for nothing), %s/%c (no call site), %n, and the '*' suppression
 * flag (no call site).
 *
 * ACCOUNTING NOTE FOR THE SIZE REPORT: this object is LUAport code, not gpSP
 * code. Its .text is reported separately in the per-object table so that the
 * gpSP closure's own cost is never inflated by the adapter that supports it.
 */

#include <stdio.h>      /* runtime/libc/stdio.h -- declares sscanf, line 29  */
#include <stdarg.h>
#include <stddef.h>

/* ------------------------------------------------------------- helpers -- */

static int gc_isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\v' || c == '\f' || c == '\r';
}

/* Digit value of c in the given base, or -1 if it is not a digit of that
   base. Returning -1 rather than 0 is what lets the caller distinguish "the
   number ended" from "the number was a zero". */
static int gc_digit(int c, int base)
{
    int v;

    if (c >= '0' && c <= '9')      v = c - '0';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z') v = c - 'A' + 10;
    else                           return -1;

    return v < base ? v : -1;
}

/* Length modifier, decoded once so the store below is a single switch. */
enum { LEN_INT, LEN_CHAR, LEN_SHORT, LEN_LONG, LEN_LLONG };

/* ------------------------------------------------------------- sscanf --- */

int sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    const char *s = str;
    int assigned = 0;

    /* Distinguishes "matching failure" (return the count so far) from "input
       failure before any conversion" (return EOF, which is -1). The single
       gpSP call site tests `2 != result`, so both paths behave correctly
       there, but getting it right costs one variable. */
    int converted_any = 0;

    va_start(ap, fmt);

    while (*fmt) {
        unsigned char fc = (unsigned char)*fmt;

        /* ---- whitespace in the format: skip any run of input whitespace,
               including none at all. This is what matches the single space in
               "%08x %04hx". */
        if (gc_isspace(fc)) {
            while (*s && gc_isspace((unsigned char)*s))
                s++;
            fmt++;
            continue;
        }

        /* ---- an ordinary character must match literally. */
        if (fc != '%') {
            if (*s != (char)fc)
                goto done;              /* matching failure */
            s++;
            fmt++;
            continue;
        }

        /* ---- a conversion. */
        fmt++;                          /* step over '%' */

        if (*fmt == '%') {              /* "%%" is a literal '%'            */
            if (*s != '%')
                goto done;
            s++;
            fmt++;
            continue;
        }

        /* Field width. %08x gives width 8: in scanf the leading 0 is simply
           the first digit of the width, NOT a flag as it is in printf. */
        {
            int width = 0;
            int have_width = 0;
            int len = LEN_INT;
            int base;
            int negative = 0;
            int ndigits = 0;
            unsigned long long acc = 0;
            void *dst;

            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                have_width = 1;
                fmt++;
            }
            if (!have_width)
                width = -1;             /* unbounded */

            /* Length modifier. */
            if (*fmt == 'h') {
                fmt++;
                if (*fmt == 'h') { fmt++; len = LEN_CHAR; }
                else             { len = LEN_SHORT; }
            } else if (*fmt == 'l') {
                fmt++;
                if (*fmt == 'l') { fmt++; len = LEN_LLONG; }
                else             { len = LEN_LONG; }
            } else if (*fmt == 'z' || *fmt == 'j' || *fmt == 't') {
                fmt++;
                len = LEN_LONG;
            }

            /* Conversion specifier. Only the integer family is supported --
               see the header comment for why that is exactly right here. */
            switch (*fmt) {
                case 'x': case 'X': base = 16; break;
                case 'o':           base = 8;  break;
                case 'u': case 'd': base = 10; break;
                case 'i':           base = 0;  break;   /* 0x/0 prefixed     */
                default:
                    /* Unsupported conversion. Stop rather than guess. */
                    goto done;
            }
            fmt++;

            /* Leading whitespace is skipped by every integer conversion, and
               it does NOT count against the field width. */
            while (*s && gc_isspace((unsigned char)*s))
                s++;

            /* Optional sign. Consumes one character of the width. */
            if (width != 0 && (*s == '+' || *s == '-')) {
                negative = (*s == '-');
                s++;
                if (width > 0) width--;
            }

            /* Base prefix handling for %i, and the tolerated 0x on %x. */
            if ((base == 0 || base == 16) && width != 0 &&
                s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
                gc_digit((unsigned char)s[2], 16) >= 0) {
                s += 2;
                if (width > 0) {
                    width -= 2;
                    if (width < 0) width = 0;
                }
                base = 16;
            } else if (base == 0) {
                base = (*s == '0') ? 8 : 10;
            }

            /* The digits. */
            while (*s && (width < 0 || ndigits < width)) {
                int d = gc_digit((unsigned char)*s, base);
                if (d < 0)
                    break;
                acc = acc * (unsigned long long)base + (unsigned long long)d;
                ndigits++;
                s++;
            }

            if (ndigits == 0) {
                /* No digits: matching failure. If nothing at all has been
                   converted and the input was exhausted, that is an input
                   failure and the standard says EOF. */
                if (!converted_any && *s == '\0') {
                    va_end(ap);
                    return -1;
                }
                goto done;
            }

            if (negative)
                acc = (unsigned long long)(-(long long)acc);

            dst = va_arg(ap, void *);
            if (!dst)
                goto done;

            switch (len) {
                case LEN_CHAR:  *(unsigned char *)dst      = (unsigned char)acc;      break;
                case LEN_SHORT: *(unsigned short *)dst     = (unsigned short)acc;     break;
                case LEN_LONG:  *(unsigned long *)dst      = (unsigned long)acc;      break;
                case LEN_LLONG: *(unsigned long long *)dst = acc;                     break;
                default:        *(unsigned int *)dst       = (unsigned int)acc;       break;
            }

            assigned++;
            converted_any = 1;
        }
    }

done:
    va_end(ap);
    return assigned;
}
