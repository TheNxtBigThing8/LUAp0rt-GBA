/* LUAport M12B -- adapters/gba/gba_savefile.h
 *
 * THE RUNTIME-FACING HALF OF GBA CARTRIDGE SAVE PERSISTENCE.
 *
 * adapters/gba/gba_save.h owns everything that knows what a GBA save IS --
 * the class, the size policy, the identity, the hashes, the header layout and
 * the byte tap. THIS file owns everything that knows what a FILE is: the write
 * window, the directory, the two file handles, the flush and the commit.
 *
 * READ gba_save.h FIRST. The two-translation-unit split, the EEPROM 512/8K
 * decision, the identity-latch ordering and the dirty model are all derived
 * there and are not repeated here.
 *
 * ============================================================================
 * WHY THIS IS A SECOND TRANSLATION UNIT AND NOT THE BOTTOM OF gba_save.c
 * ============================================================================
 * IT IS NOT A STYLE CHOICE -- ONE FILE CANNOT COMPILE.
 *
 *     gpsp/common.h:100   typedef unsigned long long int u64;
 *     runtime/core.h:32   typedef unsigned long          u64;
 *
 * runtime/savedata.h:4 includes runtime/core.h, and gba_save.c must include
 * gpsp/common.h to reach gamepak_backup[]. Both typedefs in one TU is a
 * duplicate-typedef error, so the seam is structural.
 *
 * THE BYTES CROSS THROUGH gba_save_chunk(). gba_save.h declares ONLY
 * `unsigned int`, `char *`, `const char *` and `unsigned char *`, all of which
 * mean the same thing on both sides (gpsp/common.h:94, runtime/core.h:35), so
 * THIS file may include gba_save.h freely -- the header contains no typedef at
 * all, which is exactly what makes it safe to include from either world.
 *
 * ============================================================================
 * THE CLEANUP LADDER IS STRUCTURAL, NOT DISCIPLINARY
 * ============================================================================
 * COPIED IN SHAPE FROM apps/m12gpsp/main.c, WHICH IS HARDWARE-PROVEN.
 *
 * Once plat_savedata_begin_write() has succeeded, EVERY path out has to reach
 * plat_savedata_end_write(). Skipping it leaves the container mounted
 * read-write with no read-only mount behind it, and the operator's only escape
 * from that state is a hard power cycle of the console.
 *
 * So the window is held by exactly ONE function -- gba_savefile_commit() --
 * whose body is
 *
 *     begin  ->  savefile_inside()  ->  end
 *
 * savefile_inside() may return from anywhere it likes and CANNOT skip the
 * commit, because it does not own it. That property is expressed structurally
 * so a later edit that adds one more failure case cannot break it.
 *
 * ============================================================================
 * fflush() BEFORE EVERY fclose() IS MANDATORY, NOT DEFENSIVE
 * ============================================================================
 * runtime/shim.c sets WBUF_SIZE to 4096 and buffers any write smaller than
 * that, so fwrite() returns the full count for bytes that have only been
 * COPIED INTO A BUFFER. The syscall happens inside fclose() -> wbuf_flush(),
 * and fclose() DISCARDS wbuf_flush()'s return value and returns 0
 * unconditionally.
 *
 * On a container that is still effectively read-only -- exactly what happens if
 * the read-write mount silently did not take -- the write fails with EROFS and
 * a caller would see fwrite() = n and fclose() = 0. BOTH REPORT SUCCESS FOR A
 * WRITE THAT WENT NOWHERE.
 *
 * fflush() returns wbuf_flush() directly, so calling it before every fclose()
 * is what turns that silent failure into a reported one. This exact defect was
 * found and fixed in M12A; it is not being reintroduced here.
 *
 * ============================================================================
 * NAMING DISCIPLINE
 * ============================================================================
 * tools/check_forbidden.py:109 matches the BARE SUBSTRING "path_" (along with
 * "vfs_", "retro_", "filestream_", "string_is_", "encoding_", "rtime_") and
 * :79-85 adds "_stub" and "emit_". Every symbol below is gba_savefile_* and
 * contains none of them. In particular NOTHING here is named *_path_*, which
 * is why gba_save.h spells its two builders gba_save_name_sav() and
 * gba_save_name_hdr(). NO ALLOWLIST IS WIDENED FOR M12B.
 *
 * ============================================================================
 * WHAT THIS FILE NEVER DOES
 * ============================================================================
 *   - it never reads gamepak_backup[] or any gpSP global. It cannot: it does
 *     not include a single gpSP header. Every save byte arrives through
 *     gba_save_chunk().
 *   - it never decides WHETHER to persist. gba_save_is_dirty() and
 *     gba_save_region_bytes() make that call; this file only refuses when they
 *     say there is nothing to write.
 *   - it never allocates. The staging buffer is 4096 bytes of STACK, matching
 *     the shim's WBUF_SIZE, so the whole persistence path adds ZERO bytes of
 *     .bss for the payload -- against the 128 KB a shadow copy would cost.
 */

#ifndef LUAPORT_GBA_SAVEFILE_H
#define LUAPORT_GBA_SAVEFILE_H

/* ------------------------------------------------------------- the returns --
 *
 * 0 IS SUCCESS, matching runtime/savedata.h:64-85's normalisation. Every code
 * below is distinct so an operator reading one off a screen knows which of the
 * eight failure modes happened without consulting a log.
 *
 * ENOTDIRTY AND EUNKNOWN ARE NOT FAILURES OF THE MECHANISM. They mean the run
 * correctly declined to write: nothing changed, or the cartridge's save class
 * was never identified. The fixture reports them as outcomes rather than as
 * errors -- see the status mapping in apps/m12bgpsp/main.c. */
#define GBA_SAVEFILE_OK           0
#define GBA_SAVEFILE_ENOTREADY   -1  /* the savedata layer is not initialised  */
#define GBA_SAVEFILE_ENOID       -2  /* no identity -- gba_save_latch() never
                                        ran, or no cartridge is resident       */
#define GBA_SAVEFILE_EUNKNOWN    -3  /* class UNKNOWN -- REFUSES to persist    */
#define GBA_SAVEFILE_ENOTDIRTY   -4  /* nothing changed since the latch        */
#define GBA_SAVEFILE_EWINDOW     -5  /* the read-write mount was refused       */
#define GBA_SAVEFILE_EMKDIR      -6  /* mkdir inside the window failed         */
#define GBA_SAVEFILE_EOPEN       -7  /* could not CREATE the .sav              */
#define GBA_SAVEFILE_EWRITE      -8  /* short write, or fflush reported failure*/
#define GBA_SAVEFILE_EHDR        -9  /* the .hdr could not be built or written */
#define GBA_SAVEFILE_ECOMMIT    -10  /* the unmount did not complete           */
#define GBA_SAVEFILE_ERESTORE   -11  /* THE READ-ONLY MOUNT DID NOT COME BACK  */

/* ---------------------------------------------------------- the progress latch
 *
 * SET MEANS REACHED. Published even on a failure path, so a run that stopped
 * half way says exactly how far it got rather than only that it stopped. */
#define GBA_SAVEFILE_L_WINDOW   (1u << 0)  /* the read-write window opened     */
#define GBA_SAVEFILE_L_MKDIR    (1u << 1)  /* /savedata0/gba exists            */
#define GBA_SAVEFILE_L_OPEN     (1u << 2)  /* the .sav opened for writing      */
#define GBA_SAVEFILE_L_WROTE    (1u << 3)  /* every region byte was flushed    */
#define GBA_SAVEFILE_L_HDR      (1u << 4)  /* the 32-byte .hdr was flushed     */
#define GBA_SAVEFILE_L_COMMIT   (1u << 5)  /* the unmount completed            */
#define GBA_SAVEFILE_L_RESTORE  (1u << 6)  /* the read-only mount came back    */
#define GBA_SAVEFILE_L_BITS      7u

/* THE STAGING BUFFER. 4096 bytes, matching runtime/shim.c's WBUF_SIZE exactly
 * so a full staging buffer is a full shim buffer and the flush cadence is one
 * syscall per chunk rather than a partial write per chunk. It lives on the
 * STACK inside the writer -- see the note above about .bss. */
#define GBA_SAVEFILE_CHUNK 4096u

/* =========================================================================
 *                                THE CALL
 * ========================================================================= */

/* PERSIST THE CARTRIDGE SAVE. The single entry point, and the ONLY place in
 * LUAport that opens a write window for a GBA save.
 *
 * Call ONCE, AFTER the frame loop and AFTER gba_save_finish(), and ONLY on a
 * clean exit -- see apps/m12bgpsp/main.c for why a watchdog or a fault exit
 * must not commit.
 *
 * ***** IT REFUSES BEFORE IT WRITES, AND THE REFUSALS ARE THE SAFE DIRECTION.
 * A missing identity, an UNKNOWN class or an unchanged region all return a
 * distinct non-zero code and open NO window at all. Writing on any of those
 * would either name the file wrongly, invent a size, or overwrite a good save
 * with bytes no game produced.
 *
 * On success both /savedata0/gba/<ID>.sav and <ID>.hdr are committed and the
 * read-only mount is restored. Returns one of GBA_SAVEFILE_*. */
int gba_savefile_commit(void);

/* ------------------------------------------------------- report and log --- */

/* The GBA_SAVEFILE_L_* progress mask from the last gba_savefile_commit(). */
unsigned int gba_savefile_latch(void);

/* One raw value at a time, for the UDP log and for ext->dbg[]. Returns 0 for
 * an unknown selector. */
unsigned int gba_savefile_read(unsigned int sel);

#define GBA_SAVEFILE_RD_LATCH     0u
#define GBA_SAVEFILE_RD_STATUS    1u  /* the last return, as an unsigned      */
#define GBA_SAVEFILE_RD_BEGIN_RC  2u  /* plat_savedata_begin_write()'s rc     */
#define GBA_SAVEFILE_RD_MKDIR_RC  3u  /* plat_savedata_mkdir()'s rc           */
#define GBA_SAVEFILE_RD_END_RC    4u  /* plat_savedata_end_write()'s rc       */
#define GBA_SAVEFILE_RD_SAV_BYTES 5u  /* bytes actually written to the .sav   */
#define GBA_SAVEFILE_RD_HDR_BYTES 6u  /* bytes actually written to the .hdr   */
#define GBA_SAVEFILE_RD_PAYFNV    7u  /* FNV-1a over the bytes WRITTEN        */
#define GBA_SAVEFILE_RD_CHUNKS    8u  /* how many staging chunks were used    */

#endif /* LUAPORT_GBA_SAVEFILE_H */
