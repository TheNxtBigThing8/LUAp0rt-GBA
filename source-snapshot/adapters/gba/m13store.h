#ifndef M13STORE_H
#define M13STORE_H

/* ===========================================================================
 * LUAport M13A -- adapters/gba/m13store.h
 * GENERIC READABLE-STORAGE PRIMITIVES. M13-OWNED, DELIBERATELY NOT runtime/.
 * ===========================================================================
 *
 * WHY THIS FILE EXISTS AT ALL, AND WHY IT IS NOT runtime/storage.*
 * ----------------------------------------------------------------
 * runtime/shim.c is HARDWARE-FROZEN and exposes no directory primitive:
 *
 *     struct shim_ps5 { gadget, open, read, write, close, lseek, mkdir,
 *                       sendto, gettimeofday, log_fd, log_sa }
 *
 * and runtime/shim.c:632 records that stat/fstat are DELIBERATELY NOT
 * IMPLEMENTED. Enumerating a directory therefore needs a symbol the shim does
 * not carry, and adding one would mean editing a frozen, hardware-proven file
 * for a milestone that has not yet proven a single thing on hardware.
 *
 * IT DOES NOT NEED TO. runtime/core.h:129-141 exposes resolve_sym()/SYM and
 * native_call()/NC as STATIC INLINES, so any translation unit that includes
 * core.h can resolve its own kernel symbols through the same eboot gadget the
 * shim uses. That is exactly what LuaPSX/src/main.c:316-317 does for
 * sceKernelGetdents, and it is what this file does. shim.c is not touched.
 *
 * It is M13-OWNED rather than promoted into runtime/ because NOTHING HERE IS
 * HARDWARE-PROVEN YET. Generalising an unproven layer into the frozen runtime
 * would be exactly the "refactor proven runtime for cleanliness" the M13A
 * authorisation forbids. If Stage 0-2 pass on hardware, promotion becomes a
 * defensible separate step.
 *
 * NO EMULATOR LOGIC LIVES HERE. This file knows about bytes, directories and
 * paths. It does not know what a ROM is -- that is adapters/gba/gba_library.*.
 * It also does NO LOGGING: the fixture owns every diagnostic line, which is
 * what keeps this file testable by a host harness that has no klog().
 *
 * ---- THE PATH RULE, WHICH IS THE ONE SAFETY PROPERTY THAT MATTERS ----
 *
 * A REAL FILESYSTEM PATH IS NEVER SILENTLY TRUNCATED. m13store_join() either
 * produces the complete path or REFUSES and returns 0. A truncated path does
 * not fail loudly -- it either fails to open (merely confusing) or OPENS A
 * DIFFERENT FILE (actively dangerous), and on a user's ROM library the second
 * outcome is indistinguishable from the picker working. Display labels may be
 * shortened; paths may not. exFAT permits names up to 255 UTF-16 units, which
 * is ~765 UTF-8 bytes, so this is a routine case and not a corner one.
 * ========================================================================= */

#include "core.h"

/* The real-path ceiling. A discovered USB root plus "/LUAport/roms/gba/" is on
   the order of 26 bytes, leaving ~229 for a filename -- comfortably more than
   the 42 bytes of "Tony Hawk's Pro Skater 2 (USA, Europe).gba". It is NOT big
   enough for a maximal exFAT name, which is precisely why the overflow path is
   a REJECTION rather than a truncation. */
#define M13STORE_PATH_MAX 256

/* FreeBSD open() flags. The PS5 kernel is FreeBSD-derived, so these are the BSD
   values and NOT the Linux ones -- runtime/shim.c:325-332 makes the same point
   about O_CREAT/O_TRUNC. O_DIRECTORY is 0x20000 and is the flag
   LuaPSX/src/discs.c:66 has used on hardware. */
#define M13STORE_O_RDONLY     0x0000
#define M13STORE_O_DIRECTORY  0x20000

/* FreeBSD dirent d_type values. */
#define M13STORE_DT_UNKNOWN   0
#define M13STORE_DT_DIR       4
#define M13STORE_DT_REG       8

/* getdents transfer buffer size, matching LuaPSX/src/discs.c:70's 0x2000.
   The CALLER owns the storage: discs.c mmap'd it, but a static .bss array costs
   zero blob bytes and zero JIT, which is the same reasoning
   adapters/gba/gba_filestream.c:76 applies to its FILE pool. */
#define M13STORE_DIRBUF       8192

/* ***** THE TERMINATION BOUND. *****
 *
 * m13store_dirent_next() cannot loop forever WITHIN one buffer -- a zero reclen
 * stops it. But the OUTER loop, "call getdents until it returns <= 0", has a
 * second failure mode that the parser cannot see: a filesystem that keeps
 * returning bytes without ever advancing its own cursor. On a correct kernel
 * that cannot happen; M13A's entire purpose is to read a drive formatted and
 * filled by a user on another machine, and a corrupt directory chain is exactly
 * the input this milestone is most likely to meet first.
 *
 * The consequence of being wrong here is not a bad result -- it is a HUNG
 * CONSOLE inside a milestone that has no UI to report from and no frame loop to
 * break out of. So the number of refills is bounded.
 *
 * 4096 calls x 8192 bytes is 33 MB of directory data, which is orders of
 * magnitude beyond any real ROM folder, so the cap can only ever be reached by
 * a filesystem that is misbehaving. Reaching it is REPORTED, never silent. */
#define M13STORE_MAX_DIRCALLS 4096

struct m13store {
    void       *G;             /* the eboot argument-shuffling gadget         */
    void       *open;          /* sceKernelOpen                               */
    void       *close;         /* sceKernelClose                              */
    void       *lseek;         /* sceKernelLseek -- resolved, unused at M13A  */
    void       *getdents;      /* whichever spelling resolved                 */
    const char *getdents_name; /* WHICH one, so the operator log is honest    */
    unsigned    ready;         /* 1 only if open+close+getdents all resolved  */
};

/* One directory entry, pointing INTO the caller's getdents buffer. It does not
   own the bytes and the name is NOT guaranteed NUL-terminated -- always use
   namlen. */
struct m13store_dirent {
    const char *name;
    unsigned    namlen;
    unsigned    type;
};

/* Resolves the kernel symbols. Returns 1 when the minimum viable set
   (open + close + getdents) is live, 0 otherwise. Tries "sceKernelGetdents"
   first and falls back to the bare "getdents", which is the two-name pattern
   LuaPSX resolves on hardware. */
int  m13store_init(struct m13store *st, void *G, void *dlsym_fn);

/* Opens a DIRECTORY. Returns a descriptor, or the kernel's RAW NEGATIVE RETURN
   -- which the caller is expected to report verbatim, because ENOENT ("the path
   is wrong") and EACCES/EPERM ("the sandbox refuses") are the two answers M13A
   Stage 0 exists to tell apart. */
s32  m13store_opendir(const struct m13store *st, const char *path);

s32  m13store_getdents(const struct m13store *st, s32 fd, void *buf, u32 cap);
void m13store_close(const struct m13store *st, s32 fd);

/* Steps one entry out of a getdents buffer.
 *
 * Returns 1 and advances *off when an entry was produced; returns 0 when
 * iteration must stop. IT IS THE ONLY LOOP CONDITION -- a caller never
 * increments *off itself, which is what makes the malformed-input rules below
 * impossible to bypass:
 *
 *   reclen == 0            STOP. Advancing by zero is an INFINITE LOOP, so this
 *                          is the one malformation that must never be tolerated.
 *   reclen < 8             STOP. A record cannot be smaller than its own header.
 *   off + reclen > nread   STOP. The record runs past what the kernel returned.
 *   8 + namlen > reclen    STOP. The name runs past its own record.
 *   namlen == 0            RETURNED, with namlen 0. A nameless entry is a
 *                          skippable oddity, not a reason to abandon the rest
 *                          of a user's ROM directory; the FILTER rejects it.
 *
 * The 16-bit reclen is assembled BYTE BY BYTE rather than cast through a
 * u16*, so the parser is correct regardless of the buffer's alignment and
 * behaves identically on the console and in the host harness. */
int  m13store_dirent_next(const u8 *buf, unsigned nread, unsigned *off,
                          struct m13store_dirent *out);

unsigned m13store_strlen(const char *s);

/* Joins dir + '/' + name into out, ALWAYS NUL-terminated.
 * Returns 1 on success, 0 if the complete path would not fit.
 * ***** ON FAILURE IT WRITES NOTHING AND TRUNCATES NOTHING. ***** */
int  m13store_join(char *out, unsigned cap, const char *dir,
                   const char *name, unsigned namlen);

/* Case-insensitive extension test, requiring a literal '.' separator, so "gba"
   with no dot does NOT match "gba". Carried from LuaPSX/src/discs.c:19-28,
   which needed it for the same reason M13A does: the user's drive is formatted
   FAT/exFAT, which is case-insensitive and case-preserving, so GAME.GBA and
   game.gba are the same file to the filesystem and must be to us. */
int  m13store_ends_with_ci(const char *name, unsigned len,
                           const char *ext, unsigned elen);

/* Operator-facing spelling of a raw negative kernel return. */
const char *m13store_errname(s32 rc);

#endif
