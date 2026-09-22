/* LUAport M1 -- adapters/gba/gba_compat.h
 *
 * Declarations for the LUAport-owned pieces that the gpSP interpreter closure
 * needs and that runtime/shim.c does not provide.
 *
 * SCOPE DISCIPLINE: this header exists so that the M1 additions are a named,
 * reviewable set rather than a drift of "one more thing the core wanted".
 * Everything declared here is justified by a MEASURED call site in the Tier 1
 * closure, cited next to the declaration. Nothing is added speculatively.
 *
 * NOTE ON runtime/shim.c: it is NOT modified by M1 and must stay byte-identical
 * to LuaPSX/src/shim.c (that identity is a standing M0 invariant and is
 * re-verified at the end of the M1 run). Everything below is built ON TOP of
 * the shim, in adapters/, exactly so that invariant survives.
 */
#ifndef LUAPORT_GBA_COMPAT_H
#define LUAPORT_GBA_COMPAT_H

/* ---------------------------------------------------------------- sscanf --
 *
 * THE ONLY GENUINE libc GAP IN THE TIER 1 CLOSURE.
 *
 * A census of every libc identifier across the ten Tier 1 files returns:
 *   memset 38, memcpy 15, memcmp 13, printf 8, strncmp 7, memmove 6, time 4,
 *   free 3, strlen 2, malloc 2, localtime 2, strcmp 1, sscanf 1
 * Every one of those except sscanf is already defined in runtime/shim.c
 * (verified against the defined-symbol list in build/m0diag.map). sscanf is
 * DECLARED in runtime/libc/stdio.h line 29 but has no definition anywhere, so
 * the M1 link fails on it.
 *
 * The single call site is:
 *
 *   gpsp/cheats.c:265   if (2 != sscanf(&buf[pos], "%08x %04hx", &op1, &op2))
 *
 * -- the Code Breaker cheat-code PARSER. Note that this is not on the
 * interpreter execution path: update_gba() calls process_cheats()
 * (gpsp/cheats.c:184), not the parser. It is reached only when a cheat is
 * entered, which M1 and M2 do not do.
 *
 * WHY IMPLEMENT IT ANYWAY, rather than letting --gc-sections drop the parser:
 * the authoritative M1 measurement links whole objects with no section GC
 * precisely so that the closure's real dependency surface is visible. If the
 * parser were silently collected, M1 would under-report what gpSP needs and the
 * baseline would be dishonest by exactly the amount that was hidden. Making the
 * link succeed for a stated reason is the point; a few hundred bytes is the
 * price of the measurement being true.
 *
 * The declaration is NOT repeated here: runtime/libc/stdio.h already declares
 * sscanf with the format(scanf, 2, 3) attribute, and gba_compat.c includes that
 * header so the definition is checked against it. Declaring it twice with the
 * attribute in only one place is how a silent signature drift starts.
 */

/* ------------------------------------------------------------ filestream --
 *
 * gpsp/gba_memory.c line 21 includes <streams/file_stream.h> and calls exactly
 * five of its functions (measured; sites listed in gba_filestream.c). The
 * upstream header is used UNMODIFIED -- only the implementation is
 * LUAport-owned. RFILE is opaque there (file_stream.h line 45 is a bare
 * `typedef struct RFILE RFILE;`), which is what makes that possible without
 * touching gpsp/.
 *
 * No declarations are needed here either, for the same reason: file_stream.h is
 * the authority on those five prototypes and gba_filestream.c includes it.
 */

/* ----------------------------------------------------- fixture ownership --
 *
 * The frontend-owned globals and netpacket_send() live in gba_fixture.c and are
 * declared by gpsp/main.h. They are not re-declared here.
 */

#endif
