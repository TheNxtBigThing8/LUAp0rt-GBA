/* ===========================================================================
 * LUAport M13A -- adapters/gba/m13store.c
 * GENERIC READABLE-STORAGE PRIMITIVES. See m13store.h for the design notes.
 * ===========================================================================
 *
 * EVERY kernel entry point in this file goes through the NC()/SYM() macros from
 * runtime/core.h. That is not decoration: it is what lets tools/gba_library_equiv.c
 * pre-define CORE_H, supply its own NC/SYM, and then compile THIS EXACT SOURCE
 * against a synthetic filesystem. The harness therefore tests the shipping
 * parser and the shipping scan loop rather than a paraphrase of them, and no
 * test-only #ifdef exists anywhere below.
 *
 * NOTHING HERE LOGS. The fixture owns every diagnostic line.
 * ========================================================================= */

#include "m13store.h"

/* ------------------------------------------------------------ resolution -- */

int m13store_init(struct m13store *st, void *G, void *dlsym_fn) {
    if (!st) return 0;

    st->G             = G;
    st->open          = 0;
    st->close         = 0;
    st->lseek         = 0;
    st->getdents      = 0;
    st->getdents_name = "(unresolved)";
    st->ready         = 0u;

    if (!G || !dlsym_fn) return 0;

    st->open  = SYM(G, dlsym_fn, LIBKERNEL_HANDLE, "sceKernelOpen");
    st->close = SYM(G, dlsym_fn, LIBKERNEL_HANDLE, "sceKernelClose");
    st->lseek = SYM(G, dlsym_fn, LIBKERNEL_HANDLE, "sceKernelLseek");

    /* TWO SPELLINGS, IN THIS ORDER. LuaPSX/src/main.c resolves the sce-prefixed
       name first and falls back to the bare BSD one; which of the two answers is
       reported by the fixture, because "getdents did not resolve" and "getdents
       resolved under the other name" are different console states and only one
       of them is a defect. */
    st->getdents = SYM(G, dlsym_fn, LIBKERNEL_HANDLE, "sceKernelGetdents");
    if (st->getdents) {
        st->getdents_name = "sceKernelGetdents";
    } else {
        st->getdents = SYM(G, dlsym_fn, LIBKERNEL_HANDLE, "getdents");
        if (st->getdents) st->getdents_name = "getdents";
    }

    /* lseek is NOT required at M13A -- nothing here measures a file. It is
       resolved so Stage 3 can size a ROM without another resolution pass, and
       its absence is deliberately not fatal. */
    st->ready = (st->open && st->close && st->getdents) ? 1u : 0u;
    return (int)st->ready;
}

/* ----------------------------------------------------------- directories -- */

s32 m13store_opendir(const struct m13store *st, const char *path) {
    if (!st || !st->open || !path) return -1;

    /* O_RDONLY|O_DIRECTORY, and the mode argument is 0 because no file is being
       created. Identical in shape to LuaPSX/src/discs.c:66. */
    return (s32)NC(st->G, st->open, (u64)path,
                   (u64)(M13STORE_O_RDONLY | M13STORE_O_DIRECTORY),
                   0, 0, 0, 0);
}

s32 m13store_getdents(const struct m13store *st, s32 fd, void *buf, u32 cap) {
    if (!st || !st->getdents || !buf || fd < 0) return -1;
    return (s32)NC(st->G, st->getdents, (u64)fd, (u64)buf, (u64)cap, 0, 0, 0);
}

void m13store_close(const struct m13store *st, s32 fd) {
    if (!st || !st->close || fd < 0) return;
    NC(st->G, st->close, (u64)fd, 0, 0, 0, 0, 0);
}

/* --------------------------------------------------------- dirent parsing -- */

int m13store_dirent_next(const u8 *buf, unsigned nread, unsigned *off,
                         struct m13store_dirent *out) {
    unsigned o, reclen, namlen;

    if (!buf || !off || !out) return 0;

    o = *off;

    /* FreeBSD dirent, the layout LuaPSX/src/discs.c:95-98 has read on PS5
       hardware:
           +0  u32 d_fileno
           +4  u16 d_reclen
           +6  u8  d_type
           +7  u8  d_namlen
           +8  char d_name[]
       Eight bytes of header must be present before any field can be read. */
    if (o >= nread) return 0;
    if (nread - o < 8u) return 0;

    reclen = (unsigned)buf[o + 4] | ((unsigned)buf[o + 5] << 8);

    /* THE INFINITE-LOOP GUARD. Everything else in this function is hygiene;
       this one line is the difference between a malformed directory being
       skipped and the console hanging inside the picker. */
    if (reclen == 0u) return 0;

    if (reclen < 8u) return 0;              /* smaller than its own header    */
    if (reclen > nread - o) return 0;       /* runs past the returned bytes   */

    namlen = buf[o + 7];
    if (namlen > reclen - 8u) return 0;     /* name runs past its own record  */

    out->type   = buf[o + 6];
    out->namlen = namlen;
    out->name   = (const char *)(buf + o + 8);

    *off = o + reclen;
    return 1;
}

/* ---------------------------------------------------------------- strings -- */

unsigned m13store_strlen(const char *s) {
    unsigned n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

int m13store_join(char *out, unsigned cap, const char *dir,
                  const char *name, unsigned namlen) {
    unsigned dl, need, i, p;

    if (!out || cap == 0u || !dir || !name) return 0;

    dl = m13store_strlen(dir);

    /* Normalise trailing separators so "/mnt/usb0" and "/mnt/usb0/" produce the
       same path and neither produces a doubled slash. A dir of "/" collapses to
       length 0 and correctly yields "/name". */
    while (dl > 0u && dir[dl - 1u] == '/') dl--;

    /* Bound BOTH components before adding them. namlen reaches this function
       from a dirent's u8 d_namlen and so is <= 255 in practice, but the
       parameter is `unsigned` and this is the one function in M13A whose whole
       job is to be untrickable: if either part alone cannot fit, the sum cannot
       either, and checking first means `need` can never wrap around and produce
       a small value that then passes the capacity test. */
    if (dl >= cap || namlen >= cap) return 0;

    /* dir + '/' + name + NUL */
    need = dl + 1u + namlen + 1u;

    /* ***** REJECT. NEVER TRUNCATE. ***** A short path does not fail safely:
       it either does not open, or it opens SOMETHING ELSE. */
    if (need > cap) return 0;

    p = 0;
    for (i = 0; i < dl; i++)     out[p++] = dir[i];
    out[p++] = '/';
    for (i = 0; i < namlen; i++) out[p++] = name[i];
    out[p] = '\0';
    return 1;
}

int m13store_ends_with_ci(const char *name, unsigned len,
                          const char *ext, unsigned elen) {
    unsigned i;

    if (!name || !ext || elen == 0u) return 0;

    /* The +1 REQUIRES a separator character, so a file literally called "gba"
       does not match the extension "gba". */
    if (len < elen + 1u) return 0;
    if (name[len - elen - 1u] != '.') return 0;

    for (i = 0; i < elen; i++) {
        char c = name[len - elen + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != ext[i]) return 0;
    }
    return 1;
}

/* ----------------------------------------------------------------- errors -- */

/* Returned literals, deliberately from a switch rather than a static table:
   gba_rom.c:114-121 uses the same idiom because a `static const char *[]` costs
   one R_X86_64_RELATIVE relocation PER ENTRY, and a switch costs none.

   THE THREE CASES M13A STAGE 0 EXISTS TO DISTINGUISH are ENOENT (the path is
   simply wrong), EACCES/EPERM (the path may well exist but the game sandbox
   refuses it -- the answer that would end M13 as designed) and success. */
const char *m13store_errname(s32 rc) {
    s32 e;

    if (rc >= 0) return "OK";

    e = -rc;
    switch (e) {
    case 1:  return "EPERM -- operation not permitted (SANDBOX REFUSAL)";
    case 2:  return "ENOENT -- no such file or directory";
    case 5:  return "EIO -- I/O error";
    case 9:  return "EBADF -- bad descriptor";
    case 13: return "EACCES -- permission denied (SANDBOX REFUSAL)";
    case 14: return "EFAULT -- bad address";
    case 19: return "ENODEV -- no such device";
    case 20: return "ENOTDIR -- a path component is not a directory";
    case 22: return "EINVAL -- invalid argument";
    case 24: return "EMFILE -- too many open files";
    case 78: return "ENOSYS -- not implemented";
    default: break;
    }
    return "unrecognised negative return (see the raw hex)";
}
