/* ===========================================================================
 * LUAport M13A -- adapters/gba/gba_library.c
 * .gba DISCOVERY. See gba_library.h for the scope line and its justification.
 * ===========================================================================
 *
 * This file opens no ROM, reads no header and validates no cartridge. It turns
 * a directory into a list of candidate paths and counts, by reason, everything
 * it declined.
 * ========================================================================= */

#include "gba_library.h"

void gba_lib_reset(struct gba_lib *lib) {
    unsigned i;

    if (!lib) return;

    lib->count       = 0u;
    lib->seen        = 0u;
    lib->rej_empty   = 0u;
    lib->rej_hidden  = 0u;
    lib->rej_dir     = 0u;
    lib->rej_ext     = 0u;
    lib->rej_toolong = 0u;
    lib->overflow    = 0u;
    lib->calls       = 0u;
    lib->capped      = 0u;
    lib->last_rc     = 0;

    /* The path bytes are cleared as well as the counts. A stale path left in a
       slot beyond count would be invisible to correct code and extremely
       convincing to a debugging operator. */
    for (i = 0; i < (unsigned)GBA_LIB_MAX_ENTRIES; i++) {
        lib->entry[i].path[0]  = '\0';
        lib->entry[i].name_off = 0u;
    }
}

/* ---------------------------------------------------------------- policy -- */

int gba_lib_classify(const char *name, unsigned len) {
    if (!name || len == 0u) return GBA_LIB_REJ_EMPTY;

    /* ONE TEST COVERS FOUR CASES, and that is why it comes first:
         "."            the directory itself
         ".."           the parent
         ".hidden.gba"  a hidden file
         ".gba"         an extension with NO filename in front of it
       The last one is the interesting member. It ends in "gba", it has a dot in
       the right place, and an extension test alone would accept it. */
    if (name[0] == '.') return GBA_LIB_REJ_HIDDEN;

    /* Case-insensitive, and a separator is REQUIRED, so:
         accepted   game.gba  GAME.GBA  Game.Gba  game.gBa
         rejected   game.gb  game.gbc  game.zip  game.7z
                    game.gba.bak   (ends in .bak)
                    gba            (no separator) */
    if (!m13store_ends_with_ci(name, len, "gba", 3)) return GBA_LIB_REJ_EXT;

    return GBA_LIB_ACCEPT;
}

int gba_lib_name_ok(const char *name, unsigned len) {
    return gba_lib_classify(name, len) == GBA_LIB_ACCEPT;
}

int gba_lib_type_ok(unsigned dtype) {
    /* Directories are the only type rejected outright. See the header for why
       DT_UNKNOWN must be accepted rather than filtered. */
    if (dtype == (unsigned)M13STORE_DT_DIR) return 0;
    return 1;
}

const char *gba_lib_reason(int code) {
    switch (code) {
    case GBA_LIB_ACCEPT:     return "ACCEPT";
    case GBA_LIB_REJ_EMPTY:  return "REJECT -- empty name";
    case GBA_LIB_REJ_HIDDEN: return "REJECT -- hidden or dot entry";
    case GBA_LIB_REJ_EXT:    return "REJECT -- not .gba";
    default:                 break;
    }
    return "REJECT -- unknown reason";
}

/* ------------------------------------------------------------------ scan -- */

int gba_lib_scan(const struct m13store *st, const char *dir,
                 struct gba_lib *lib, u8 *dirbuf, unsigned dirbuf_cap) {
    s32 fd;

    if (!st || !dir || !lib || !dirbuf) return -1;
    if (dirbuf_cap < 64u) return -1;

    gba_lib_reset(lib);

    fd = m13store_opendir(st, dir);
    lib->last_rc = fd;
    if (fd < 0) return (int)fd;      /* the RAW kernel return, deliberately */

    for (;;) {
        struct m13store_dirent de;
        unsigned off = 0u;
        s32 nread = m13store_getdents(st, fd, dirbuf, (u32)dirbuf_cap);

        lib->calls++;
        lib->last_rc = nread;

        /* 0 is a normal end of directory; negative is an error. Both stop the
           loop, and last_rc preserves which one happened. */
        if (nread <= 0) break;

        /* ***** THE TERMINATION BOUND. ***** A volume that returns bytes
           without ever advancing its own cursor would spin here forever and
           HANG THE CONSOLE. See M13STORE_MAX_DIRCALLS. Reaching it is recorded
           so the operator sees a stopped walk rather than a short one. */
        if (lib->calls >= (unsigned)M13STORE_MAX_DIRCALLS) {
            lib->capped = 1u;
            break;
        }

        while (m13store_dirent_next(dirbuf, (unsigned)nread, &off, &de)) {
            int cls;

            lib->seen++;

            /* Type first: a DIRECTORY named "roms.gba" should be counted as a
               directory rejection, not as an extension rejection. */
            if (!gba_lib_type_ok(de.type)) { lib->rej_dir++; continue; }

            cls = gba_lib_classify(de.name, de.namlen);
            if (cls == GBA_LIB_REJ_EMPTY)  { lib->rej_empty++;  continue; }
            if (cls == GBA_LIB_REJ_HIDDEN) { lib->rej_hidden++; continue; }
            if (cls != GBA_LIB_ACCEPT)     { lib->rej_ext++;    continue; }

            /* The name is wanted. Everything from here can still decline it,
               and every decline is counted. */
            if (lib->count >= (unsigned)GBA_LIB_MAX_ENTRIES) {
                lib->overflow++;
                continue;
            }

            {
                struct gba_lib_entry *e = &lib->entry[lib->count];

                /* ***** THE PATH RULE. ***** join() refuses rather than
                   truncates, so a name too long to spell in full is DROPPED AND
                   COUNTED. It is never stored as a shorter path that would open
                   a different file. */
                if (!m13store_join(e->path, (unsigned)GBA_LIB_PATH_MAX,
                                   dir, de.name, de.namlen)) {
                    e->path[0] = '\0';
                    lib->rej_toolong++;
                    continue;
                }

                /* join() wrote dir + '/' + name, so the filename begins exactly
                   namlen bytes before the end. Derived from the RESULT rather
                   than recomputed from dir, so it cannot disagree with what was
                   actually written. */
                e->name_off = m13store_strlen(e->path) - de.namlen;
                lib->count++;
            }
        }
    }

    m13store_close(st, fd);
    return (int)lib->count;
}

/* --------------------------------------------------------------- readers -- */

/* BOTH READERS ARE TOTAL: any lib, any index, and the result is always a
 * readable NUL-terminated C string. See gba_library.h for the contract.
 *
 * ***** WHY THE SECOND BOUND IS NOT REDUNDANT. ***** `i >= lib->count` alone is
 * correct only while `count` is correct, and `count` is a plain unsigned field
 * in a struct the caller owns -- so nothing a compiler (or a reader) can see
 * proves it is <= GBA_LIB_MAX_ENTRIES. GCC says so out loud: with only the count
 * test, a call with a literal index of 99 produced
 *
 *     array subscript 99 is above array bounds of struct gba_lib_entry[64]
 *
 * because a count of 100 would have let entry[99] through. Testing the
 * COMPILE-TIME bound as well turns that from a warning into a proof, and it
 * costs one comparison against a constant. NO POINTER INTO entry[] IS COMPUTED
 * BEFORE BOTH BOUNDS HAVE PASSED. */
const char *gba_lib_path(const struct gba_lib *lib, unsigned i) {
    if (!lib) return "";
    if (i >= lib->count) return "";
    if (i >= (unsigned)GBA_LIB_MAX_ENTRIES) return "";
    return lib->entry[i].path;
}

const char *gba_lib_name(const struct gba_lib *lib, unsigned i) {
    if (!lib) return "";
    if (i >= lib->count) return "";
    if (i >= (unsigned)GBA_LIB_MAX_ENTRIES) return "";

    /* name_off is derived inside gba_lib_scan() from a path that was just
       written, so it is in range by construction -- but it is stored, and this
       reader hands the result straight to printf(). A stale or corrupt offset
       would otherwise walk the pointer off the end of path[] and print whatever
       followed it in .bss. Bounded against the array, not against strlen, so the
       check holds even for a slot that was never filled. */
    if (lib->entry[i].name_off >= (unsigned)GBA_LIB_PATH_MAX) return "";

    return lib->entry[i].path + lib->entry[i].name_off;
}
