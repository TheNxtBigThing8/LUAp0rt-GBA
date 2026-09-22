#ifndef GBA_LIBRARY_H
#define GBA_LIBRARY_H

/* ===========================================================================
 * LUAport M13A -- adapters/gba/gba_library.h
 * THE GBA-SPECIFIC HALF OF ROM DISCOVERY. FILENAMES ONLY.
 * ===========================================================================
 *
 * m13store.* knows about directories and paths and nothing else. THIS file is
 * where "a GBA cartridge" first means anything, and its entire vocabulary at
 * M13A is: a regular file whose name ends in .gba, case-insensitively.
 *
 * ***** IT OPENS NOTHING AND READS NO FILE CONTENT. *****
 *
 * That is a deliberate scope line and not an oversight. The M13A authorisation
 * defers ROM header validation, size gating and FNV comparison to a later stage,
 * for a reason worth writing down: adapters/gba/gba_rom.h ALREADY owns that
 * gate -- m4_rom_probe_source() with M4_SRC_EMPTY / M4_SRC_TOOBIG /
 * M4_SRC_MIRROR_1M, plus the byte[3]==0xEA and byte[0xB2]==0x96 content checks,
 * all hardware-proven since M4. A picker that re-implemented them would become a
 * SECOND, DIVERGING validator, and the first time the two disagreed the operator
 * would have no way to tell which one was right. Stage 3 will call the existing
 * one. This layer only decides what is worth OFFERING to it.
 *
 * ---- WHY d_type IS TRUSTED FOR DIRECTORIES AND NOT FOR FILES ----
 *
 * runtime/shim.c:632 records that stat/fstat are deliberately unimplemented, so
 * d_type from getdents is the ONLY type information available. DT_DIR is
 * rejected outright. DT_UNKNOWN is ACCEPTED if the name passes, because some
 * filesystems -- plausibly including the FAT family this milestone targets --
 * report DT_UNKNOWN for everything, and rejecting it would make the picker
 * silently list nothing on exactly the drive M13A exists to read. A directory
 * named "something.gba" would slip through that gap; it is a pathological name,
 * it is harmless at M13A because nothing is opened, and Stage 3's open() will
 * reject it with EISDIR the moment it matters.
 *
 * ---- STORAGE IS STATIC, AND WHY ----
 *
 * runtime/shim.c's free() is a NO-OP over a bump arena (the point
 * adapters/gba/gba_filestream.c:66-67 makes), so every heap-allocated filename
 * would permanently consume arena that is never given back. A fixed .bss table
 * costs zero blob bytes, zero JIT and zero arena.
 * ========================================================================= */

#include "m13store.h"

/* Diagnostic-scale, not product-scale. M13B owns the real picker and can
   revisit this; 64 is enough to prove enumeration works and small enough that
   the table stays ~16 KB of .bss against a 2,097,152-byte tripwire. */
#define GBA_LIB_MAX_ENTRIES 64

#define GBA_LIB_PATH_MAX    M13STORE_PATH_MAX

/* The single rule, expressed once. gba_lib_scan() and the offline harness both
   go through gba_lib_classify(), so there is no second copy of the policy that
   could drift away from the one that ships. */
#define GBA_LIB_ACCEPT     0
#define GBA_LIB_REJ_EMPTY  1   /* namlen == 0                                 */
#define GBA_LIB_REJ_HIDDEN 2   /* leading '.' -- also ".", ".." and a bare .gba */
#define GBA_LIB_REJ_EXT    3   /* does not end in .gba                        */

struct gba_lib_entry {
    /* THE COMPLETE PATH. Never truncated -- an entry that could not be spelled
       in full is not stored at all, it is counted in rej_toolong. */
    char     path[GBA_LIB_PATH_MAX];
    /* Index into path[] at which the ORIGINAL filename begins. The user's
       spaces, apostrophes and parentheses are preserved byte for byte; nothing
       in this layer sanitises, renames or normalises a filename. */
    unsigned name_off;
};

struct gba_lib {
    struct gba_lib_entry entry[GBA_LIB_MAX_ENTRIES];
    unsigned count;        /* accepted candidates                             */

    /* Every rejection is COUNTED BY REASON rather than silently dropped: on
       hardware, "the directory listed nothing" and "the directory listed 40
       things and every one was rejected for the same reason" look identical
       from a bare count, and they are completely different problems. */
    unsigned seen;         /* dirents the parser produced                     */
    unsigned rej_empty;
    unsigned rej_hidden;
    unsigned rej_dir;
    unsigned rej_ext;
    unsigned rej_toolong;  /* PATH TOO LONG -- rejected, NOT truncated        */
    unsigned overflow;     /* passed the filter but the table was full        */

    unsigned calls;        /* getdents calls issued                           */
    unsigned capped;       /* 1 if M13STORE_MAX_DIRCALLS was reached -- the
                              directory did not end and the walk was STOPPED.
                              Never silent: the fixture reports it.           */
    s32      last_rc;      /* last raw kernel return, for the operator log    */
};

void gba_lib_reset(struct gba_lib *lib);

/* The policy. Pure, total, and testable with no filesystem at all. */
int  gba_lib_classify(const char *name, unsigned len);
int  gba_lib_name_ok(const char *name, unsigned len);
int  gba_lib_type_ok(unsigned dtype);

const char *gba_lib_reason(int code);

/* Enumerates dir and fills lib. dirbuf is caller-owned getdents scratch.
   Returns the accepted count, or the raw NEGATIVE kernel return if the
   directory could not be opened. */
int  gba_lib_scan(const struct m13store *st, const char *dir,
                  struct gba_lib *lib, u8 *dirbuf, unsigned dirbuf_cap);

/* THE READERS ARE TOTAL, AND THE SAFE RESULT IS "" AND NEVER NULL.
 *
 * Any lib (including NULL) and ANY index (including >= count, >= the table
 * bound, and UINT_MAX) return a readable NUL-terminated string. Callers are not
 * required to pre-validate i.
 *
 * ***** THE INVALID RESULT IS THE EMPTY STRING, DELIBERATELY, NOT NULL. *****
 * apps/m13agpsp/main.c:651-652 passes both straight into printf("%s"), so a NULL
 * return would turn a bounds violation into a NULL dereference in the fixture --
 * trading a wrong string for a crash. "" prints as nothing and cannot fault.
 *
 * The bounds tested, in order, before any element of entry[] is addressed:
 *     lib != NULL
 *     i < lib->count                 (nothing beyond what was actually stored)
 *     i < GBA_LIB_MAX_ENTRIES        (the STRUCTURAL bound -- holds even if
 *                                     count itself is wrong)
 *     name_off < GBA_LIB_PATH_MAX    (gba_lib_name only: the offset stays
 *                                     inside the path buffer it indexes) */
const char *gba_lib_path(const struct gba_lib *lib, unsigned i);
const char *gba_lib_name(const struct gba_lib *lib, unsigned i);

#endif
