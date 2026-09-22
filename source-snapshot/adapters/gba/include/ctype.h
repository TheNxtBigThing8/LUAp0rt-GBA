/* LUAport M1 -- <ctype.h> for the gpSP closure ONLY.
 *
 * gpsp/main.c line 21 includes <ctype.h>. No equivalent exists under
 * runtime/libc/, so without this file the include resolves to the HOST glibc
 * header -- which on glibc is not a set of plain functions but a locale-aware
 * table lookup through __ctype_b_loc(), an external symbol this freestanding
 * image can never satisfy. That would turn a header bleed into a link failure
 * only if something called it, and into silently host-dependent behaviour if
 * the compiler inlined the table access.
 *
 * MEASURED FACT: a census of the Tier 1 closure for every ctype identifier
 * (isalpha/isdigit/isspace/isxdigit/toupper/tolower/...) returns ZERO call
 * sites. gpsp/main.c includes the header and never uses it. So this file only
 * has to exist and be self-contained.
 *
 * Everything is `static inline` on purpose:
 *   - no external symbol is created, so nothing new appears in the link;
 *   - no .text is emitted unless something calls it, and nothing does;
 *   - therefore this file contributes exactly 0 bytes to the M1 measurement,
 *     which keeps the size gate honest.
 *
 * ASCII-only, locale-free. That is correct here: the only consumers would be
 * ROM-title parsing, which is 7-bit by the GBA cartridge header spec.
 *
 * M1-include-path only; runtime/libc/ is untouched so M0 is unaffected.
 */
#ifndef LUAPORT_M1_CTYPE_H
#define LUAPORT_M1_CTYPE_H

static inline int isascii(int c) { return (unsigned)c < 128; }
static inline int isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int isxdigit(int c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}
static inline int islower(int c) { return c >= 'a' && c <= 'z'; }
static inline int isupper(int c) { return c >= 'A' && c <= 'Z'; }
static inline int isalpha(int c) { return islower(c) || isupper(c); }
static inline int isalnum(int c) { return isalpha(c) || isdigit(c); }
static inline int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\v' || c == '\f' || c == '\r';
}
static inline int isblank(int c) { return c == ' ' || c == '\t'; }
static inline int isprint(int c) { return c >= 0x20 && c < 0x7F; }
static inline int isgraph(int c) { return c > 0x20 && c < 0x7F; }
static inline int iscntrl(int c) { return (unsigned)c < 0x20 || c == 0x7F; }
static inline int ispunct(int c) { return isgraph(c) && !isalnum(c); }

static inline int tolower(int c) { return isupper(c) ? c + ('a' - 'A') : c; }
static inline int toupper(int c) { return islower(c) ? c - ('a' - 'A') : c; }

#endif
