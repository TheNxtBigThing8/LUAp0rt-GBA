/* ===========================================================================
 * tools/gba_library_equiv.c -- THE OFFLINE ROM-LIBRARY HARNESS (M13A)
 * ===========================================================================
 *
 * Builds and runs with the HOST compiler, NOT the PS5 toolchain, and #includes
 * BOTH shipping translation units VERBATIM:
 *
 *     adapters/gba/m13store.c     (M13A -- paths, dirents, kernel plumbing)
 *     adapters/gba/gba_library.c  (M13A -- .gba filtering and the scan loop)
 *
 * ---- HOW IT COMPILES A FILE THAT CALLS THE PS5 KERNEL ----
 *
 * m13store.c reaches the kernel ONLY through the NC() and SYM() macros that
 * runtime/core.h supplies. This file pre-defines that header's own guard
 * (CORE_H, runtime/core.h:1-2) so core.h's body expands to NOTHING, then
 * supplies the handful of typedefs it would have provided plus its OWN NC and
 * SYM backed by a synthetic filesystem.
 *
 * That is exactly the technique tools/gba_restore_equiv.c:27-37 uses to
 * neutralise gpsp/common.h, and it has the same two consequences: the
 * preprocessor must still FIND core.h (hence -I runtime in the build rule), and
 * NO TEST-ONLY #ifdef EXISTS IN ANY PRODUCTION FILE. m13store.c and
 * gba_library.c are byte-identical to what ships on the console.
 *
 * ***** THE PAYOFF: THE SCAN LOOP ITSELF IS UNDER TEST. ***** A harness that
 * could only reach the pure helpers would leave gba_lib_scan() -- the function
 * that actually walks a user's ROM directory, counts rejections and builds every
 * path -- completely unexercised. Here a fake getdents feeds it real
 * FreeBSD-shaped records, including deliberately malformed ones.
 *
 * ---- WHAT CANNOT BE PROVED HERE, AND IS NOT CLAIMED ----
 *
 * Nothing in this file says anything about whether a PS5 exposes USB storage,
 * at what path, or with what permissions. That is Stage 0 on hardware and it is
 * the one question a host harness is structurally unable to answer. What IS
 * proved is everything layered above it: the filter, the path rule, the dirent
 * parser and the scan.
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

/* u64 is `unsigned long` here, matching runtime/core.h:32 EXACTLY so this
   harness compiles the adapters under the same integer semantics the console
   build uses. (tools/gba_restore_equiv.c:88-89 makes the opposite choice for
   the opposite reason -- it had to match gpsp/common.h.)
   The fake kernel casts pointers through u64, so a host where `unsigned long`
   is not 64-bit would TRUNCATE every path pointer and produce baffling
   failures. Fail at COMPILE time instead. */
typedef char m13a_u64_must_be_64_bit[(sizeof(unsigned long) == 8) ? 1 : -1];

/* ===========================================================================
 * A SYNTHETIC FILESYSTEM
 * ========================================================================= */

#define VFS_MAX_DIRS   8
#define VFS_DIR_BYTES  16384
#define VFS_MAX_FDS    64

struct vfs_dir {
    const char *path;
    int         open_err;              /* nonzero -> open fails with -err     */
    u8          buf[VFS_DIR_BYTES];
    unsigned    len;
};

static struct vfs_dir vfs[VFS_MAX_DIRS];
static unsigned       vfs_n;

static int      fd_dir[VFS_MAX_FDS];
static unsigned fd_off[VFS_MAX_FDS];
static int      fd_open[VFS_MAX_FDS];
static int      fd_next;

/* Counts getdents calls, so the multi-call test can assert the loop really did
   iterate. */
static unsigned vfs_getdents_calls;

/* ***** A HOSTILE VOLUME. ***** When set, getdents returns bytes but NEVER
   advances its cursor -- the one directory-walk failure mode that a correct
   dirent parser cannot detect, because every individual buffer it is handed is
   perfectly well formed. On hardware this would spin forever inside a milestone
   with no UI and no frame loop, i.e. a hung console. It cannot be provoked
   safely on a real drive, which is exactly why it is simulated here. */
static int vfs_stuck;

/* ***** A FIXTURE THAT CANNOT LIE ABOUT NAME LENGTHS. *****
 *
 * A FreeBSD dirent spells its name length in ONE BYTE (d_namlen at +7), so 255
 * is the longest name any real getdents can report. vfs_add() used to store
 * `(u8)namlen` unconditionally, which meant a 294-byte test name was written to
 * the record as 294 & 0xFF == 38 -- and the production parser then correctly
 * produced a 38-byte name. Every downstream assertion was measuring a completely
 * different input than the test author had written, silently. Refusals are now
 * COUNTED so a test can assert its own fixture built what it asked for. */
static unsigned vfs_unrepresentable;

static void vfs_reset(void) {
    unsigned i;
    for (i = 0; i < VFS_MAX_DIRS; i++) {
        vfs[i].path     = 0;
        vfs[i].open_err = 0;
        vfs[i].len      = 0;
    }
    vfs_n = 0;
    for (i = 0; i < VFS_MAX_FDS; i++) { fd_dir[i] = -1; fd_off[i] = 0; fd_open[i] = 0; }
    fd_next = 3;
    vfs_getdents_calls = 0;
    vfs_stuck = 0;
    vfs_unrepresentable = 0;
}

static struct vfs_dir *vfs_new(const char *path) {
    struct vfs_dir *d = &vfs[vfs_n++];
    d->path     = path;
    d->open_err = 0;
    d->len      = 0;
    return d;
}

/* Appends a well-formed FreeBSD dirent:
      +0 u32 d_fileno   +4 u16 d_reclen   +6 u8 d_type   +7 u8 d_namlen
      +8 char d_name[]  (NUL-terminated and padded to an 8-byte multiple) */
static void vfs_add(struct vfs_dir *d, const char *name, unsigned type) {
    unsigned namlen = (unsigned)strlen(name);
    unsigned reclen = (8u + namlen + 1u + 7u) & ~7u;
    unsigned i;
    u8 *p;

    /* REFUSE rather than truncate namlen into the u8 field. See the note on
       vfs_unrepresentable: a silent (u8) cast here does not fail a test, it
       QUIETLY CHANGES THE INPUT the test believes it is using.
       Both refusals come BEFORE the buffer pointer is formed. */
    if (namlen > 255u) { vfs_unrepresentable++; return; }

    if (d->len + reclen > VFS_DIR_BYTES) return;

    p = d->buf + d->len;

    for (i = 0; i < reclen; i++) p[i] = 0;
    p[0] = 1; p[1] = 0; p[2] = 0; p[3] = 0;          /* d_fileno */
    p[4] = (u8)(reclen & 0xFF);
    p[5] = (u8)((reclen >> 8) & 0xFF);
    p[6] = (u8)type;
    p[7] = (u8)namlen;
    for (i = 0; i < namlen; i++) p[8 + i] = (u8)name[i];

    d->len += reclen;
}

/* Appends a RAW record so malformations can be injected exactly. */
static void vfs_add_raw(struct vfs_dir *d, unsigned reclen, unsigned type,
                        unsigned namlen, const char *name, unsigned advance) {
    u8 *p = d->buf + d->len;
    unsigned i, n = name ? (unsigned)strlen(name) : 0u;

    if (d->len + advance > VFS_DIR_BYTES) return;

    for (i = 0; i < advance; i++) p[i] = 0;
    if (advance >= 8u) {
        p[4] = (u8)(reclen & 0xFF);
        p[5] = (u8)((reclen >> 8) & 0xFF);
        p[6] = (u8)type;
        p[7] = (u8)namlen;
        for (i = 0; i < n && 8u + i < advance; i++) p[8 + i] = (u8)name[i];
    }
    d->len += advance;
}

/* ---- the fake kernel ----------------------------------------------------- */

static int tok_open, tok_close, tok_lseek, tok_getdents;

static void *fake_sym(void *gadget, void *dlsym_fn, s32 handle,
                      const char *name) {
    (void)gadget; (void)dlsym_fn; (void)handle;
    if (!strcmp(name, "sceKernelOpen"))     return &tok_open;
    if (!strcmp(name, "sceKernelClose"))    return &tok_close;
    if (!strcmp(name, "sceKernelLseek"))    return &tok_lseek;
    /* DELIBERATELY UNRESOLVED, so the production fallback in m13store_init()
       is the path this harness exercises. The bare "getdents" answers. */
    if (!strcmp(name, "sceKernelGetdents")) return 0;
    if (!strcmp(name, "getdents"))          return &tok_getdents;
    return 0;
}

static u64 fake_nc(void *gadget, void *fn, u64 a1, u64 a2, u64 a3,
                   u64 a4, u64 a5, u64 a6) {
    (void)gadget; (void)a4; (void)a5; (void)a6;

    if (fn == (void *)&tok_open) {
        const char *path = (const char *)a1;
        unsigned i;
        for (i = 0; i < vfs_n; i++) {
            if (vfs[i].path && !strcmp(vfs[i].path, path)) {
                int fd;
                if (vfs[i].open_err) return (u64)(s64)(-vfs[i].open_err);
                fd = fd_next++;
                if (fd >= VFS_MAX_FDS) return (u64)(s64)(-24); /* EMFILE */
                fd_dir[fd]  = (int)i;
                fd_off[fd]  = 0;
                fd_open[fd] = 1;
                return (u64)(s64)fd;
            }
        }
        return (u64)(s64)(-2);                     /* ENOENT */
    }

    if (fn == (void *)&tok_getdents) {
        int      fd  = (int)a1;
        u8      *out = (u8 *)a2;
        unsigned cap = (unsigned)a3;
        struct vfs_dir *d;
        unsigned produced = 0;
        unsigned start;

        vfs_getdents_calls++;

        if (fd < 0 || fd >= VFS_MAX_FDS || !fd_open[fd]) return (u64)(s64)(-9);
        d = &vfs[fd_dir[fd]];
        start = fd_off[fd];

        /* Emits WHOLE RECORDS ONLY, exactly as a real getdents does, so the
           production loop is forced to call again for a large directory. */
        while (fd_off[fd] < d->len) {
            unsigned off = fd_off[fd];
            unsigned reclen = (unsigned)d->buf[off + 4] |
                              ((unsigned)d->buf[off + 5] << 8);
            unsigned adv = reclen ? reclen : 8u;
            if (off + adv > d->len) adv = d->len - off;
            if (produced + adv > cap) break;
            memcpy(out + produced, d->buf + off, adv);
            produced   += adv;
            fd_off[fd] += adv;
            if (!reclen) break;   /* a zero reclen ends the stream for us too */
        }
        /* The hostile volume: hand back perfectly valid bytes, then rewind, so
           the directory never ends. */
        if (vfs_stuck) fd_off[fd] = start;
        return (u64)produced;
    }

    if (fn == (void *)&tok_close) {
        int fd = (int)a1;
        if (fd >= 0 && fd < VFS_MAX_FDS) fd_open[fd] = 0;
        return 0;
    }

    if (fn == (void *)&tok_lseek) return 0;
    return (u64)(s64)(-22);
}

#define NC  fake_nc
#define SYM fake_sym

/* ---- the shipping source, compiled verbatim ------------------------------ */
#include "m13store.c"
#include "gba_library.c"

/* ===========================================================================
 * CHECK PLUMBING
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

/* A store wired to the fake kernel. */
static struct m13store ST;

static void store_up(void) {
    int ok = m13store_init(&ST, (void *)1, (void *)1);
    ck(ok == 1, "m13store_init reports ready against the fake kernel");
    ck_str(ST.getdents_name, "getdents",
           "the sceKernelGetdents -> getdents FALLBACK is what resolved");
}

/* ===========================================================================
 * MAIN
 * ========================================================================= */

int main(void) {
    static u8 dirbuf[M13STORE_DIRBUF];
    static struct gba_lib lib;

    printf("=== M13A ROM-LIBRARY EQUIVALENCE HARNESS ===\n");
    printf("    adapters/gba/m13store.c and adapters/gba/gba_library.c,\n");
    printf("    compiled VERBATIM against a synthetic filesystem.\n\n");

    vfs_reset();
    store_up();

    /* ===================================================================
     * 1. EXTENSION FILTERING -- CASE-INSENSITIVE, SEPARATOR REQUIRED
     * =================================================================== */
    printf("[1] extension filtering\n");
    {
        /* ACCEPTED. exFAT is case-insensitive and case-preserving, so all four
           spellings name the same file to the filesystem. */
        ck(gba_lib_name_ok("game.gba", 8), "game.gba is accepted");
        ck(gba_lib_name_ok("GAME.GBA", 8), "GAME.GBA is accepted");
        ck(gba_lib_name_ok("Game.Gba", 8), "Game.Gba is accepted");
        ck(gba_lib_name_ok("game.gBa", 8), "game.gBa is accepted");
        ck(gba_lib_name_ok("a.gba", 5),    "a single-character stem is accepted");
        ck(gba_lib_name_ok("Tony Hawk's Pro Skater 2 (USA, Europe).gba", 42),
           "spaces, an apostrophe and parentheses are all accepted");

        /* REJECTED. */
        ck(!gba_lib_name_ok("game.gb", 7),      ".gb is rejected");
        ck(!gba_lib_name_ok("game.gbc", 8),     ".gbc is rejected");
        ck(!gba_lib_name_ok("game.zip", 8),     ".zip is rejected");
        ck(!gba_lib_name_ok("game.7z", 7),      ".7z is rejected");
        ck(!gba_lib_name_ok("game.gba.bak", 12),
           "game.gba.bak is rejected -- it ends in .bak, not .gba");
        ck(!gba_lib_name_ok("gba", 3),
           "a file called 'gba' is rejected -- the '.' separator is REQUIRED");
        ck(!gba_lib_name_ok(".gba", 4),
           "'.gba' is rejected -- an extension with no filename in front of it");
        ck(!gba_lib_name_ok("", 0),             "an empty name is rejected");
        ck(!gba_lib_name_ok(".", 1),            "'.' is rejected");
        ck(!gba_lib_name_ok("..", 2),           "'..' is rejected");
        ck(!gba_lib_name_ok(".hidden.gba", 11), "a hidden .gba is rejected");
        ck(!gba_lib_name_ok("game.GBAX", 9),    "a longer extension is rejected");
        ck(!gba_lib_name_ok("gba.", 4),         "a trailing dot is rejected");

        /* The length argument is authoritative -- the parser hands a namlen and
           the buffer is not guaranteed to be terminated where we think. */
        ck(gba_lib_name_ok("game.gbaXXXX", 8),
           "only the first namlen bytes are considered");
        ck(!gba_lib_name_ok("game.gba", 7),
           "a short namlen turns game.gba into game.gb and it is rejected");
    }

    /* ===================================================================
     * 2. THE REASON CODES ARE DISTINCT AND CORRECT
     * =================================================================== */
    printf("[2] classification reasons\n");
    {
        ck_int(gba_lib_classify("game.gba", 8), GBA_LIB_ACCEPT,  "accept");
        ck_int(gba_lib_classify("", 0),         GBA_LIB_REJ_EMPTY,  "empty");
        ck_int(gba_lib_classify(".x.gba", 6),   GBA_LIB_REJ_HIDDEN, "hidden");
        ck_int(gba_lib_classify("game.zip", 8), GBA_LIB_REJ_EXT,    "extension");
        ck(gba_lib_type_ok(M13STORE_DT_REG),     "DT_REG is accepted");
        ck(!gba_lib_type_ok(M13STORE_DT_DIR),    "DT_DIR is rejected");
        ck(gba_lib_type_ok(M13STORE_DT_UNKNOWN),
           "DT_UNKNOWN is ACCEPTED -- a FAT-family volume may report it for "
           "everything, and rejecting it would list nothing at all");
        ck(strcmp(gba_lib_reason(GBA_LIB_ACCEPT),
                  gba_lib_reason(GBA_LIB_REJ_EXT)) != 0,
           "reason spellings are distinct");
    }

    /* ===================================================================
     * 3. PATH JOINING -- REJECT, NEVER TRUNCATE
     * =================================================================== */
    printf("[3] path joining and the overflow rule\n");
    {
        char out[64];

        ck(m13store_join(out, sizeof(out), "/mnt/usb0/LUAport/roms/gba",
                         "game.gba", 8), "a normal join succeeds");
        ck_str(out, "/mnt/usb0/LUAport/roms/gba/game.gba", "normal join");

        ck(m13store_join(out, sizeof(out), "/mnt/usb0/LUAport/roms/gba/",
                         "game.gba", 8), "a trailing slash is tolerated");
        ck_str(out, "/mnt/usb0/LUAport/roms/gba/game.gba",
               "a trailing slash produces NO doubled separator");

        ck(m13store_join(out, sizeof(out), "/mnt/usb0///", "g.gba", 5),
           "several trailing slashes are tolerated");
        ck_str(out, "/mnt/usb0/g.gba", "repeated separators collapse");

        ck(m13store_join(out, sizeof(out), "/", "g.gba", 5),
           "a root directory joins");
        ck_str(out, "/g.gba", "root does not produce a doubled separator");

        /* THE BOUNDARY. "/ab" + '/' + name + NUL. */
        {
            char small[12];
            ck(m13store_join(small, sizeof(small), "/ab", "1234567", 7),
               "an EXACT fit is accepted (3 + 1 + 7 + 1 == 12)");
            ck_str(small, "/ab/1234567", "the exact-fit path is complete");

            ck(!m13store_join(small, sizeof(small), "/ab", "12345678", 8),
               "ONE BYTE of overflow is REJECTED");
            ck_str(small, "/ab/1234567",
                   "***** THE REJECTED JOIN WROTE NOTHING -- the buffer still "
                   "holds the previous value, it was NOT truncated *****");
        }

        /* A maximal exFAT name: 255 UTF-16 units can reach ~765 UTF-8 bytes,
           so this is a routine case on the target filesystem. */
        {
            char big[M13STORE_PATH_MAX];
            char longname[400];
            unsigned i;
            for (i = 0; i < 395; i++) longname[i] = 'x';
            longname[395] = '.'; longname[396] = 'g';
            longname[397] = 'b'; longname[398] = 'a'; longname[399] = 0;

            ck(gba_lib_name_ok(longname, 399),
               "a 399-byte .gba name passes the NAME filter");
            ck(!m13store_join(big, sizeof(big), "/mnt/usb0/LUAport/roms/gba",
                              longname, 399),
               "...and is then REJECTED by the PATH rule, not truncated");
        }

        ck(!m13store_join(out, 0, "/a", "b.gba", 5), "a zero capacity refuses");
        ck(!m13store_join(0, sizeof(out), "/a", "b.gba", 5), "a NULL out refuses");

        /* THE WRAPAROUND CLASS. If the size computation could overflow, an
           absurd namlen would produce a SMALL `need`, pass the capacity test
           and then run off the end of the buffer. Both components are bounded
           before they are added, so it refuses instead. */
        ck(!m13store_join(out, sizeof(out), "/a", "x.gba", 0xFFFFFFF0u),
           "***** an absurd namlen cannot wrap the size computation *****");
        ck(!m13store_join(out, sizeof(out),
                          "/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                          "a.gba", 5),
           "a directory longer than the whole buffer refuses");
        /* `out` still holds the result of the last SUCCESSFUL join in this
           section -- the "/" case at step 4 -- which is the point: four
           consecutive refusals wrote nothing at all. */
        ck_str(out, "/g.gba",
               "and not one of the four refusals disturbed the buffer");
    }

    /* ===================================================================
     * 4. THE DIRENT PARSER, INCLUDING EVERY MALFORMATION
     * =================================================================== */
    printf("[4] dirent parsing\n");
    {
        struct vfs_dir d;
        struct m13store_dirent de;
        unsigned off;

        /* -- a single well-formed regular file -- */
        d.len = 0;
        vfs_add(&d, "game.gba", M13STORE_DT_REG);
        off = 0;
        ck(m13store_dirent_next(d.buf, d.len, &off, &de) == 1, "one entry parses");
        ck_int((long)de.namlen, 8, "namlen");
        ck_int((long)de.type, M13STORE_DT_REG, "d_type is read from +6");
        ck(!memcmp(de.name, "game.gba", 8), "the name bytes are correct");
        ck(m13store_dirent_next(d.buf, d.len, &off, &de) == 0,
           "iteration ends at the end of the buffer");

        /* -- several entries, mixed types -- */
        d.len = 0;
        vfs_add(&d, "a.gba", M13STORE_DT_REG);
        vfs_add(&d, "subdir", M13STORE_DT_DIR);
        vfs_add(&d, ".hidden", M13STORE_DT_REG);
        vfs_add(&d, "b.GBA", M13STORE_DT_REG);
        off = 0;
        {
            int n = 0;
            while (m13store_dirent_next(d.buf, d.len, &off, &de)) n++;
            ck_int(n, 4, "all four entries are produced");
        }

        /* -- reclen == 0 : THE INFINITE-LOOP GUARD -- */
        d.len = 0;
        vfs_add(&d, "a.gba", M13STORE_DT_REG);
        vfs_add_raw(&d, 0u, M13STORE_DT_REG, 5, "b.gba", 24);
        off = 0;
        ck(m13store_dirent_next(d.buf, d.len, &off, &de) == 1, "first entry ok");
        {
            unsigned before = off;
            ck(m13store_dirent_next(d.buf, d.len, &off, &de) == 0,
               "***** reclen == 0 STOPS iteration *****");
            ck_int((long)off, (long)before,
                   "...and does NOT advance -- an advance of zero would hang "
                   "the console inside the picker");
        }

        /* -- reclen < 8 : smaller than its own header -- */
        d.len = 0;
        vfs_add_raw(&d, 4u, M13STORE_DT_REG, 0, 0, 16);
        off = 0;
        ck(m13store_dirent_next(d.buf, d.len, &off, &de) == 0,
           "a reclen below the 8-byte header is refused");

        /* -- reclen running past the returned byte count -- */
        d.len = 0;
        vfs_add_raw(&d, 4096u, M13STORE_DT_REG, 5, "b.gba", 24);
        off = 0;
        ck(m13store_dirent_next(d.buf, d.len, &off, &de) == 0,
           "a record claiming more bytes than getdents returned is refused");

        /* -- namlen running past its own record -- */
        d.len = 0;
        vfs_add_raw(&d, 16u, M13STORE_DT_REG, 200, "b.gba", 16);
        off = 0;
        ck(m13store_dirent_next(d.buf, d.len, &off, &de) == 0,
           "a name that overruns its own record is refused");

        /* -- namlen == 0 : PRODUCED, then rejected by the filter -- */
        d.len = 0;
        vfs_add_raw(&d, 16u, M13STORE_DT_REG, 0, 0, 16);
        off = 0;
        ck(m13store_dirent_next(d.buf, d.len, &off, &de) == 1,
           "a zero-length name is PRODUCED rather than aborting the directory");
        ck_int((long)de.namlen, 0, "...with namlen 0");
        ck_int(gba_lib_classify(de.name, de.namlen), GBA_LIB_REJ_EMPTY,
               "...and the FILTER is what rejects it");

        /* -- a truncated tail: a record header that is itself cut short --
           Two 16-byte records, the first parsed normally (off -> 16), then the
           buffer is presented as only 20 bytes long. Four bytes remain, which is
           less than the 8-byte header, so the parser must refuse rather than
           read fields that are not there. */
        d.len = 0;
        vfs_add(&d, "a.gba", M13STORE_DT_REG);
        vfs_add(&d, "b.gba", M13STORE_DT_REG);
        ck_int((long)d.len, 32, "two 16-byte records were built");
        off = 0;
        ck(m13store_dirent_next(d.buf, d.len, &off, &de) == 1, "entry parses");
        ck_int((long)off, 16, "the cursor advanced by one whole record");
        ck(m13store_dirent_next(d.buf, 20u, &off, &de) == 0,
           "a PARTIAL trailing header (4 bytes left, 8 needed) is refused");
        ck(m13store_dirent_next(d.buf, 16u, &off, &de) == 0,
           "a cursor exactly at the end is refused");

        ck(m13store_dirent_next(0, 16, &off, &de) == 0, "a NULL buffer refuses");
    }

    /* ===================================================================
     * 5. THE SHIPPING SCAN LOOP, END TO END
     * =================================================================== */
    printf("[5] gba_lib_scan over a realistic mixed directory\n");
    {
        struct vfs_dir *d;
        int rc;

        vfs_reset();
        store_up();

        d = vfs_new("/mnt/usb0/LUAport/roms/gba");
        vfs_add(d, "Tony Hawk's Pro Skater 2 (USA, Europe).gba", M13STORE_DT_REG);
        vfs_add(d, ".", M13STORE_DT_DIR);
        vfs_add(d, "..", M13STORE_DT_DIR);
        vfs_add(d, "DOOM II.GBA", M13STORE_DT_REG);
        vfs_add(d, "notes.txt", M13STORE_DT_REG);
        vfs_add(d, "archive.zip", M13STORE_DT_REG);
        vfs_add(d, "backup.gba.bak", M13STORE_DT_REG);
        vfs_add(d, ".hidden.gba", M13STORE_DT_REG);
        vfs_add(d, "covers", M13STORE_DT_DIR);
        vfs_add(d, "unknown.Gba", M13STORE_DT_UNKNOWN);

        rc = gba_lib_scan(&ST, "/mnt/usb0/LUAport/roms/gba", &lib,
                          dirbuf, sizeof(dirbuf));

        ck_int(rc, 3, "three cartridges are accepted");
        ck_int((long)lib.count, 3, "count agrees with the return");
        ck_int((long)lib.seen, 10, "all ten entries were seen");
        ck_int((long)lib.rej_dir, 3, "three directories rejected (., .., covers)");
        ck_int((long)lib.rej_hidden, 1, "one hidden .gba rejected");
        ck_int((long)lib.rej_ext, 3, "txt, zip and .gba.bak rejected");
        ck_int((long)lib.rej_empty, 0, "no empty names");
        ck_int((long)lib.rej_toolong, 0, "nothing was too long");
        ck_int((long)lib.overflow, 0, "no overflow");

        /* ***** THE USER'S FILENAME SURVIVES BYTE FOR BYTE. ***** */
        ck_str(gba_lib_name(&lib, 0),
               "Tony Hawk's Pro Skater 2 (USA, Europe).gba",
               "the original filename is preserved -- spaces, apostrophe, parens");
        ck_str(gba_lib_path(&lib, 0),
               "/mnt/usb0/LUAport/roms/gba/Tony Hawk's Pro Skater 2 (USA, Europe).gba",
               "the complete path is built correctly");
        ck_str(gba_lib_name(&lib, 1), "DOOM II.GBA",
               "an uppercase extension is preserved, not normalised");
        ck_str(gba_lib_name(&lib, 2), "unknown.Gba",
               "a DT_UNKNOWN regular file is accepted");

        /* ---- THE READER CONTRACT AT EVERY INTERESTING INDEX ----
         *
         * lib.count is 3 here and GBA_LIB_MAX_ENTRIES is 64.
         *
         * ***** NONE OF THESE CALLS PERFORMS AN OUT-OF-BOUNDS C ACCESS. ***** An
         * earlier version of this section passed 99 to readers whose only guard
         * was `i >= lib->count`. Because count is a plain unsigned that nothing
         * proves is <= 64, GCC could not rule out entry[99] being addressed and
         * said so:
         *
         *     array subscript 99 is above array bounds of
         *     struct gba_lib_entry entry[64]
         *
         * That warning was correct and the right answer was to make the readers
         * total, not to silence it -- so the guard now tests the compile-time
         * table bound too, and safety comes from the CHECK rather than from the
         * access happening to land in adjacent memory. */
        ck_str(gba_lib_name(&lib, 0),
               "Tony Hawk's Pro Skater 2 (USA, Europe).gba",
               "index 0 -- the first VALID entry still reads correctly");
        ck_str(gba_lib_path(&lib, lib.count - 1u),
               "/mnt/usb0/LUAport/roms/gba/unknown.Gba",
               "index count-1 -- the LAST VALID entry still reads correctly");

        ck_str(gba_lib_name(&lib, lib.count), "",
               "index == count is invalid and yields the empty string");
        ck_str(gba_lib_path(&lib, lib.count), "",
               "...and so does the path reader at index == count");
        ck_str(gba_lib_name(&lib, (unsigned)GBA_LIB_MAX_ENTRIES), "",
               "index == GBA_LIB_MAX_ENTRIES is invalid -- the first index the "
               "table does not have");
        ck_str(gba_lib_path(&lib, (unsigned)GBA_LIB_MAX_ENTRIES), "",
               "...and so is the path at the table bound");
        ck_str(gba_lib_name(&lib, 99u), "",
               "a large out-of-range index (99) is safe");
        ck_str(gba_lib_path(&lib, 99u), "",
               "a large out-of-range path index (99) is safe");
        ck_str(gba_lib_name(&lib, 0xFFFFFFFFu), "",
               "UINT_MAX is safe -- the bound is a COMPARISON, so there is no "
               "index arithmetic left to overflow");
        ck_str(gba_lib_path(&lib, 0xFFFFFFFFu), "",
               "...for the path reader too");
        ck_str(gba_lib_name(0, 0), "", "a NULL library is safe");
        ck_str(gba_lib_path(0, 0), "", "...for the path reader too");

        /* THE RESULT IS NEVER NULL, which is what lets main.c:651-652 hand it
           straight to printf("%s") without a guard of its own. */
        ck(gba_lib_name(&lib, 99u) != 0 && gba_lib_path(&lib, 99u) != 0 &&
           gba_lib_name(0, 0) != 0 && gba_lib_path(0, 0) != 0,
           "***** an invalid read returns \"\" and NEVER NULL -- the fixture "
           "printf()s these directly *****");
    }

    /* ===================================================================
     * 6. FAILURE PATHS
     * =================================================================== */
    printf("[6] absent and refused directories\n");
    {
        int rc;

        vfs_reset();
        store_up();
        vfs_new("/mnt/usb0");                       /* exists, but empty      */
        { struct vfs_dir *d = vfs_new("/mnt/usb1"); d->open_err = 13; } /* EACCES */

        rc = gba_lib_scan(&ST, "/mnt/usb0/LUAport/roms/gba", &lib,
                          dirbuf, sizeof(dirbuf));
        ck_int(rc, -2, "an absent directory returns the RAW -ENOENT");
        ck_int((long)lib.count, 0, "nothing is listed");
        ck_str(m13store_errname(-2), "ENOENT -- no such file or directory",
               "ENOENT is spelled out for the operator");

        rc = gba_lib_scan(&ST, "/mnt/usb1", &lib, dirbuf, sizeof(dirbuf));
        ck_int(rc, -13, "a refused directory returns the RAW -EACCES");
        ck(strstr(m13store_errname(-13), "SANDBOX REFUSAL") != 0,
           "***** EACCES is flagged as a SANDBOX REFUSAL -- the one answer that "
           "would end M13 as designed, and it must not read like ENOENT *****");
        ck(strstr(m13store_errname(-1), "SANDBOX REFUSAL") != 0,
           "EPERM is flagged the same way");
        ck_str(m13store_errname(0), "OK", "a non-negative return is OK");

        rc = gba_lib_scan(&ST, "/mnt/usb0", &lib, dirbuf, sizeof(dirbuf));
        ck_int(rc, 0, "an EMPTY but readable directory yields zero, not an error");
        ck_int((long)lib.seen, 0, "and saw nothing");

        ck_int(gba_lib_scan(&ST, "/mnt/usb0", &lib, dirbuf, 8), -1,
               "an undersized getdents buffer is refused outright");
        ck_int(gba_lib_scan(&ST, 0, &lib, dirbuf, sizeof(dirbuf)), -1,
               "a NULL directory is refused");
    }

    /* ===================================================================
     * 7. TABLE OVERFLOW AND THE PATH RULE, INSIDE THE REAL SCAN
     * =================================================================== */
    printf("[7] overflow and over-long paths inside gba_lib_scan\n");
    {
        struct vfs_dir *d;
        char nm[32];
        int i, rc;

        /* 400 entries at a uniform 24-byte record is 9,600 bytes against an
           8,192-byte transfer buffer, so this directory GENUINELY spans several
           getdents calls rather than merely provoking one trailing zero-return.
           A directory that fitted in a single call would leave the outer loop
           untested. */
        vfs_reset();
        store_up();
        d = vfs_new("/roms");
        for (i = 0; i < 400; i++) {
            sprintf(nm, "rom%03d.gba", i);
            vfs_add(d, nm, M13STORE_DT_REG);
        }

        rc = gba_lib_scan(&ST, "/roms", &lib, dirbuf, sizeof(dirbuf));
        ck_int(rc, GBA_LIB_MAX_ENTRIES, "the table caps at GBA_LIB_MAX_ENTRIES");
        ck_int((long)lib.overflow, 400 - GBA_LIB_MAX_ENTRIES,
               "every surplus entry is COUNTED rather than silently dropped");
        ck_int((long)lib.seen, 400, "all 400 were seen across the whole walk");
        ck(vfs_getdents_calls >= 3,
           "the directory spanned MULTIPLE NON-EMPTY getdents calls, so the "
           "outer refill loop is genuinely exercised");
        ck(lib.calls >= 3, "the adapter counted those calls too");
        ck_str(gba_lib_name(&lib, 0), "rom000.gba", "the first entry is intact");
        ck_str(gba_lib_name(&lib, GBA_LIB_MAX_ENTRIES - 1), "rom063.gba",
               "the last stored entry is intact");

        /* ---- WHY THE NAME LENGTHS BELOW ARE 249 AND 250 AND NOT 294 ----
         *
         * The over-long-path assertion USED TO FAIL, reporting rej_toolong 0, and
         * the defect was in this harness rather than in the adapters.
         *
         * d_namlen is ONE BYTE. The old test built a 294-byte name; vfs_add's
         * `p[7] = (u8)namlen` stored 294 & 0xFF == 38, so the record on the wire
         * described a 38-byte name. gba_lib_scan() read exactly that, and 38 bytes
         * of 'y' do not end in ".gba" -- so the NAME FILTER rejected it as
         * rej_ext and m13store_join() was never even called. rej_toolong was
         * legitimately 0: THE PATH RULE HAD NEVER BEEN REACHED, let alone broken.
         *
         * Proven below before the real test runs, so this cannot silently recur. */
        {
            struct vfs_dir tmp;
            char huge[300];
            unsigned k, before = vfs_unrepresentable;

            for (k = 0; k < 290u; k++) huge[k] = 'y';
            memcpy(huge + 290u, ".gba", 5);       /* 294 bytes -- the OLD name */
            ck_int((long)strlen(huge), 294, "the old test name was 294 bytes");

            tmp.len = 0;
            vfs_add(&tmp, huge, M13STORE_DT_REG);
            ck_int((long)tmp.len, 0,
                   "a 294-byte name is NOT appended -- no real getdents could "
                   "report it, because d_namlen is a single byte");
            ck_int((long)(vfs_unrepresentable - before), 1,
                   "***** and the FIXTURE now COUNTS that refusal instead of "
                   "truncating namlen to 38 and quietly testing the wrong "
                   "input *****");
        }

        /* An over-long path, inside the shipping scan rather than in isolation.
         *
         * The real boundary is arithmetic on the JOIN, not on d_namlen. dir is
         * "/roms" (5 bytes) and the cap is M13STORE_PATH_MAX (256), so a stored
         * path costs 5 + 1 + namlen + 1:
         *
         *     namlen 249  ->  256   EXACTLY FITS   -- stored complete
         *     namlen 250  ->  257   ONE BYTE OVER  -- REJECTED, not truncated
         *
         * Both are expressible in a u8, so both reach gba_lib_scan() exactly as a
         * real kernel would deliver them. Testing the pair together is what makes
         * the result meaningful: if the accepted side were also refused the code
         * would merely be broken in the safe direction. */
        {
            char fits[256];
            char over[256];
            unsigned k;

            for (k = 0; k < 245u; k++) fits[k] = 'y';
            memcpy(fits + 245u, ".gba", 5);            /* 245 + 4 == 249 */

            for (k = 0; k < 246u; k++) over[k] = 'z';
            memcpy(over + 246u, ".gba", 5);            /* 246 + 4 == 250 */

            ck_int((long)strlen(fits), 249, "the exact-fit name is 249 bytes");
            ck_int((long)strlen(over),  250, "the over-long name is 250 bytes");

            /* Both pass the NAME filter, so the only gate left that can decline
               them is the PATH rule -- which is the gate under test. */
            ck(gba_lib_name_ok(fits, 249), "the exact-fit name passes the FILTER");
            ck(gba_lib_name_ok(over, 250),
               "the over-long name ALSO passes the filter, so anything that "
               "declines it can only be the PATH rule");

            vfs_reset();
            store_up();
            d = vfs_new("/roms");
            vfs_add(d, "ok.gba", M13STORE_DT_REG);
            vfs_add(d, fits, M13STORE_DT_REG);
            vfs_add(d, over,  M13STORE_DT_REG);
            ck_int((long)vfs_unrepresentable, 0,
                   "every synthetic name was representable -- the FIXTURE "
                   "shortened nothing before the code saw it");

            rc = gba_lib_scan(&ST, "/roms", &lib, dirbuf, sizeof(dirbuf));

            ck_int(rc, 2, "only the two spellable cartridges are listed");
            ck_int((long)lib.count, 2, "count agrees with the return");
            ck_int((long)lib.seen, 3, "all three entries were seen");
            ck_int((long)lib.rej_toolong, 1,
                   "***** the over-long path is REJECTED AND COUNTED, never "
                   "silently shortened into a path that opens another file *****");
            ck_int((long)lib.rej_ext, 0,
                   "...and it was the PATH RULE that rejected it, NOT the name "
                   "filter -- an rej_ext of 1 here is exactly how this test "
                   "previously fooled itself");
            ck_int((long)lib.rej_dir, 0, "no directory rejections");
            ck_int((long)lib.overflow, 0, "no overflow");

            ck_str(gba_lib_name(&lib, 0), "ok.gba", "the good entry is unharmed");
            ck_str(gba_lib_name(&lib, 1), fits,
                   "the EXACT-FIT name is stored WHOLE, byte for byte");
            ck_int((long)m13store_strlen(gba_lib_path(&lib, 1)), 255,
                   "its complete path is 255 bytes + NUL, i.e. the 256-byte cap "
                   "used to the last byte");

            /* ***** THE INVARIANT ITSELF. ***** Not one stored path may be a
               shortened form of the path the rejected entry WOULD have had. The
               'z' name shares no prefix with anything legitimately stored, so a
               single "/roms/z" hit here would mean a truncated path had been
               written -- the outcome that opens a DIFFERENT file and looks
               exactly like the picker working. */
            {
                unsigned j, bad = 0;
                for (j = 0; j < lib.count; j++) {
                    if (!strncmp(gba_lib_path(&lib, j), "/roms/z", 7)) bad++;
                }
                ck_int((long)bad, 0,
                       "***** NO stored path is a shortened form of the rejected "
                       "entry -- the over-long name left NOTHING behind *****");
            }
        }
    }

    /* ===================================================================
     * 8. RESET IS COMPLETE
     * =================================================================== */
    printf("[8] gba_lib_reset leaves no stale state\n");
    {
        unsigned i;
        gba_lib_reset(&lib);
        ck_int((long)lib.count, 0, "count cleared");
        ck_int((long)lib.seen, 0, "seen cleared");
        ck_int((long)lib.rej_ext, 0, "rejections cleared");
        ck_int((long)lib.overflow, 0, "overflow cleared");
        ck_int((long)lib.calls, 0, "call count cleared");
        for (i = 0; i < (unsigned)GBA_LIB_MAX_ENTRIES; i++) {
            if (lib.entry[i].path[0] != '\0') break;
        }
        ck_int((long)i, GBA_LIB_MAX_ENTRIES,
               "every path slot is blanked -- a stale path beyond count would "
               "be invisible to code and very convincing to an operator");
    }

    /* ===================================================================
     * 9. THE SUB-PATH THE USER IS ASKED TO CREATE
     * =================================================================== */
    printf("[9] the documented USB layout composes correctly\n");
    {
        char out[M13STORE_PATH_MAX];
        ck(m13store_join(out, sizeof(out), "/mnt/usb0", "LUAport/roms/gba",
                         (unsigned)strlen("LUAport/roms/gba")),
           "root + the fixed sub-path joins");
        ck_str(out, "/mnt/usb0/LUAport/roms/gba",
               "the folder the user is told to create is the folder that is "
               "opened");
        ck_int((long)m13store_strlen("LUAport/roms/gba"), 16, "sub-path length");
    }

    /* ===================================================================
     * 10. ***** A VOLUME THAT NEVER ENDS CANNOT HANG THE CONSOLE *****
     * =================================================================== */
    printf("[10] the getdents termination bound\n");
    {
        struct vfs_dir *d;
        int rc;

        vfs_reset();
        store_up();
        d = vfs_new("/roms");
        vfs_add(d, "a.gba", M13STORE_DT_REG);

        /* Every buffer handed to the parser is WELL FORMED. The fault is one
           level up -- the volume never advances -- so no amount of per-record
           validation can catch it. Only the call bound can.
           NOTE: if the bound were ever removed, this test does not fail, it
           HANGS. That is the correct signal for a guard against a hang. */
        vfs_stuck = 1;
        rc = gba_lib_scan(&ST, "/roms", &lib, dirbuf, sizeof(dirbuf));
        vfs_stuck = 0;

        ck(rc >= 0, "***** THE SCAN RETURNED AT ALL -- it did not spin *****");
        ck_int((long)lib.capped, 1, "the cap is REPORTED, never silent");
        ck_int((long)lib.calls, M13STORE_MAX_DIRCALLS,
               "the walk stopped at exactly M13STORE_MAX_DIRCALLS refills");
        ck_int((long)lib.count, GBA_LIB_MAX_ENTRIES,
               "the table still filled and never overran");
        ck(lib.overflow > 0, "surplus repeats were counted, not written");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("M13A EQUIV FAILED\n");
        return 1;
    }
    printf("M13A EQUIV OK\n");
    return 0;
}
