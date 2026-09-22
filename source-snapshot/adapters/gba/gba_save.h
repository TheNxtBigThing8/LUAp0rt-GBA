/* LUAport M12B -- adapters/gba/gba_save.h
 *
 * THE gpSP-FACING HALF OF GBA CARTRIDGE SAVE PERSISTENCE.
 *
 * M11 observed gpSP's save memory and persisted nothing. M12A proved the PS5
 * savedata container can be mounted read-write, written, committed and read
 * back. M12B CONNECTS THEM -- and this file is the half that knows what a GBA
 * cartridge save IS. It classifies the save hardware, decides how many bytes
 * are worth persisting, computes the identity the file is named after, hashes
 * the region, decides whether anything actually changed, and hands the bytes
 * out 4 KB at a time.
 *
 * IT OPENS NO FILE AND MOUNTS NOTHING. Every filesystem operation lives in
 * adapters/gba/gba_savefile.c, on the other side of the type boundary below.
 *
 * ============================================================================
 * WHY THE ADAPTER IS TWO TRANSLATION UNITS AND NOT ONE
 * ============================================================================
 * THIS IS NOT A STYLE CHOICE -- ONE FILE CANNOT COMPILE.
 *
 *     gpsp/common.h:100   typedef unsigned long long int u64;
 *     runtime/core.h:32   typedef unsigned long          u64;
 *
 * Both are 64 bits on this ABI and they are DISTINCT TYPES in C, so a
 * translation unit that saw both would fail on the duplicate typedef.
 * runtime/savedata.h:4 includes runtime/core.h. A single gba_save.c that both
 * read gamepak_backup (needs gpsp/common.h) and called
 * plat_savedata_begin_write (needs runtime/savedata.h) would therefore not
 * build at all.
 *
 * So the responsibilities are split exactly along that seam:
 *
 *     gba_save.c      includes gpsp/common.h ONLY   -- classify, size, hash,
 *                                                      identity, name, header
 *     gba_savefile.c  includes runtime/savedata.h   -- mkdir, open, write,
 *                     and <stdio.h> ONLY               flush, close, commit
 *
 * and the bytes cross between them through gba_save_chunk() below. This is the
 * same rule adapters/gba/gba_rom.h:8-16 and adapters/gba/gba_m11save.h:157-163
 * were built around; M12B is the first milestone that needs BOTH sides at once,
 * which is why it is the first that needs two files.
 *
 * THE TYPE RULE FOR THIS HEADER. Every declaration below uses ONLY
 * `unsigned int`, `char *`, `const char *` and `unsigned char *`.
 * `unsigned int` is u32 in both worlds and `unsigned char` is u8 in both
 * (gpsp/common.h:94, runtime/core.h:35), so the prototypes the two sides see
 * are the SAME TYPES. NOTHING 64-BIT AND NO gpSP STRUCT CROSSES THIS BOUNDARY.
 *
 * ============================================================================
 * NAMING DISCIPLINE -- AND THE TRAP THAT SHAPED IT
 * ============================================================================
 * tools/check_forbidden.py:108-111 matches the BARE SUBSTRINGS "path_",
 * "retro_", "libretro_", "vfs_", "string_is_", "filestream_", "rtime_",
 * "encoding_" and (via the dynarec list at :79-85) "_stub" and "emit_" against
 * the LOWERCASED symbol name, in every object.
 *
 * "path_" IS THE ONE THAT BITES HERE. The obvious names for this milestone --
 * gba_save_path_build, save_path_of, sav_path_copy -- would every one of them
 * FAIL the M12B forbidden-symbol gate. That is why the two functions that build
 * a filename are called gba_save_name_sav() and gba_save_name_hdr(). NO
 * ALLOWLIST IS WIDENED FOR M12B, exactly as none was for M4 (gba_rom.h:79-84)
 * or M11 (gba_m11save.h:165-169).
 *
 * ============================================================================
 * WHAT IS FROZEN AND IS NOT TOUCHED
 * ============================================================================
 * gpsp/, runtime/savedata.{c,h}, apps/m12gpsp/main.c, lua/m12.lua.in,
 * adapters/gba/gba_m11save.{c,h}, apps/m11gpsp/main.c, lua/m11.lua.in,
 * adapters/gba/gba_rom.{c,h}, runtime/shim.c, runtime/linker.ld,
 * runtime/boot.inc and runtime/audio.c are all UNCHANGED by this milestone.
 * Every symbol M12B needs is either declared in a gpSP header already or is a
 * non-static global declared extern by hand -- the technique upstream itself
 * uses at gpsp/gba_memory.c:198 and that gba_probe.c:75-85 established for M2.
 *
 * gba_m11save.o IS DELIBERATELY NOT LINKED INTO M12B. M12B owns its own
 * baseline, its own end hash and its own dirty accounting (see the DIRTY MODEL
 * below), so linking the M11 observer would add a 128-block dirty map that
 * nothing reads. M11 stays hardware-frozen and completely untouched.
 */

#ifndef LUAPORT_GBA_SAVE_H
#define LUAPORT_GBA_SAVE_H

/* ------------------------------------------------------- the save classes --
 *
 * NUMERICALLY IDENTICAL TO gba_m11save.h's M11_SAVE_* AND THAT IS DELIBERATE.
 * M11 is hardware-frozen and its numbers are already printed in operator
 * screenshots and UDP logs; a second, differently-numbered enum for the same
 * six (type, size) pairs would make two milestones disagree about what "4"
 * means. The names are M12B's because gba_m11save.h is NOT included here --
 * M12B does not link the observer -- but the VALUES are held identical on
 * purpose so the two milestones' logs can be read side by side.
 *
 * These are gpSP's own distinctions and no others. THERE IS NO "SAVE_NONE":
 * BACKUP_UNKN collapses to BACKUP_SRAM on the first backup-region access
 * (gba_memory.c:465, :1141), so a save-less cartridge and an unexercised one
 * are INDISTINGUISHABLE by construction. UNKNOWN carries that case honestly,
 * and UNKNOWN REFUSES PERSISTENCE. */
#define GBA_SAVE_UNKNOWN      0u
#define GBA_SAVE_SRAM         1u   /* BACKUP_SRAM                             */
#define GBA_SAVE_FLASH64      2u   /* BACKUP_FLASH,  flash_bank_cnt == 1      */
#define GBA_SAVE_FLASH128     3u   /* BACKUP_FLASH,  flash_bank_cnt == 2      */
#define GBA_SAVE_EEPROM512    4u   /* BACKUP_EEPROM, eeprom_size    == 1      */
#define GBA_SAVE_EEPROM8K     5u   /* BACKUP_EEPROM, eeprom_size    == 16     */
#define GBA_SAVE_CLASS_MAX    5u

/* ------------------------------------------------------------ confidence --
 *
 * Same three values and same numbering as M11_STATE_*, for the reason above.
 *
 * M12B DERIVES THIS ITSELF rather than reading M11's, because M11 is not
 * linked. The derivation is deliberately narrower than M11's and is stated in
 * full in gba_save.c -- it uses only the LATCH-TIME versus EXIT-TIME values of
 * eeprom_size and flash_bank_cnt, and it never runs a signature scan. Where
 * M11 could say FINAL on the strength of a ROM signature, M12B says
 * PROVISIONAL. IT IS RECORDED IN THE HEADER AND IT NEVER CHANGES WHAT IS
 * WRITTEN -- see the EEPROM policy below. */
#define GBA_SAVE_CONF_UNKNOWN     0u
#define GBA_SAVE_CONF_PROVISIONAL 1u
#define GBA_SAVE_CONF_FINAL       2u

/* ------------------------------------------------------------- the sizes --
 *
 * gamepak_backup[] is 131,072 bytes (gba_memory.c:360). NOTHING here may ever
 * exceed it, and gba_save_region_bytes() is bounded by it.
 *
 * THE SIZE CONSTANTS IN gpSP ARE UNIT COUNTS, NOT BYTE COUNTS. This is the trap
 * M11 documented at gba_m11save.h:141-152 and it is just as fatal here:
 *
 *     gpsp/gba_memory.h:294-298
 *         FLASH_SIZE_64KB   1      FLASH_SIZE_128KB  2
 *         EEPROM_512_BYTE   1      EEPROM_8_KBYTE   16
 *
 * eeprom_size == 16 MEANS 8192 BYTES. A persistence layer that echoed
 * eeprom_size as a byte count would write a SIXTEEN BYTE save file for an 8 KB
 * EEPROM and it would look entirely plausible. Every byte figure this adapter
 * produces is computed from the CLASSIFIED CLASS and never by echoing one of
 * those constants; tools/gba_save_equiv.c asserts it. */
#define GBA_SAVE_BACKUP_BYTES 131072u

/* ------------------------------------------------------- THE SIZE POLICY --
 *
 * TWO NUMBERS, NOT ONE, AND THE DIFFERENCE IS THE WHOLE EEPROM POLICY.
 *
 *   DECLARED  what gpSP believes the chip actually holds RIGHT NOW.
 *   REGION    how many bytes M12B PERSISTS.
 *
 * They are equal for every class except EEPROM512:
 *
 *     class        declared   region
 *     SRAM            32768    32768
 *     FLASH64         65536    65536
 *     FLASH128       131072   131072
 *     EEPROM512         512     8192      <-- the only divergence
 *     EEPROM8K         8192     8192
 *     UNKNOWN             0        0      <-- REFUSES PERSISTENCE
 *
 * WHY EEPROM512 PERSISTS 8192 BYTES. A 512-byte EEPROM occupies [0, 512) of
 * gamepak_backup[] and an 8 KB one occupies [0, 8192) -- SAME BASE, so 512 is a
 * STRICT PREFIX of 8 KB. read_eeprom/write_eeprom index eeprom_address from
 * zero with a 6-bit or 14-bit width (gba_memory.c:565); the bytes above 512 on
 * a 512-byte chip are never addressable and stay at the 0xFF that
 * normalize_blank_backup_for_detected_type() writes (:2907, :3071).
 *
 * AND EEPROM512 IS ALMOST NEVER A MEASUREMENT. 512 B is what gba_memory.c:531
 * INITIALISES eeprom_size to and what :2441 RESTORES on every reset. Only the
 * 17-unit DMA at :841-843 ever MEASURES 8 KB. So a cartridge reported as
 * EEPROM512 may simply not have run that DMA yet -- Tony Hawk 2's M11 hardware
 * result was exactly this: EEPROM 512B, CONFIDENCE PROVISIONAL.
 *
 * Writing 512 bytes today and 8192 tomorrow would mean a FORMAT MIGRATION on a
 * file the operator cannot see, for a chip whose size was never measured.
 * Writing 8192 from the start costs 7,680 bytes of container space, is
 * byte-identical over the range that matters, and CANNOT need migrating. The
 * declared size is still recorded in the .hdr, so nothing is lost. */

/* -------------------------------------------------------- the file naming --
 *
 * /savedata0/gba ALREADY EXISTS -- M12A created it on hardware. It is created
 * again anyway (plat_savedata_mkdir reports an existing directory as success,
 * runtime/savedata.h:136-138) so M12B does not depend on M12A having been run
 * against this particular container.
 *
 * THE IDENTITY IS <SANITISED CODE>_<HASH8>, e.g. ATHE_1234ABCD -- see
 * gba_save_latch() for the derivation and for why it must be latched BEFORE
 * execution. */
#define GBA_SAVE_DIR      "/savedata0/gba"
#define GBA_SAVE_EXT_SAV  ".sav"
#define GBA_SAVE_EXT_HDR  ".hdr"

/* 4 code characters + '_' + 8 hex digits + NUL = 14. */
#define GBA_SAVE_ID_MAX   16u

/* GBA_SAVE_DIR (14) + '/' + id (13) + ".sav" (4) + NUL = 33. */
#define GBA_SAVE_NAME_MAX 48u

/* ----------------------------------------------------- the .hdr, 32 bytes --
 *
 * LITTLE-ENDIAN, FIXED OFFSETS, WRITTEN THROUGH EXPLICIT BYTE STORES so the
 * layout cannot depend on struct padding or on the compiler's alignment
 * choices. THE MAGIC IS A BYTE ARRAY, NOT A u32 LITERAL -- spelling it as a
 * number would make the on-disk bytes endian-dependent for no benefit.
 *
 *     off  size  field
 *       0     4  magic 'L','G','S','1'
 *       4     2  version
 *       6     2  save class      (GBA_SAVE_*)
 *       8     4  region bytes    -- how many bytes the .sav holds
 *      12     4  declared bytes  -- what gpSP believed the chip holds
 *      16     4  ROM FNV         -- M12B's identity hash, see gba_save_latch
 *      20     4  payload FNV     -- FNV-1a over the .sav's region bytes
 *      24     2  confidence      (GBA_SAVE_CONF_*)
 *      26     6  reserved, zero
 *
 * NO COMPRESSION AND NO TIMESTAMP. A timestamp would destroy the property that
 * the same save state produces the same 32 bytes, which is what lets M12C
 * compare a header against a freshly computed one; and the reserved field is
 * left zero rather than filled with something that would have to be parsed.
 *
 * THE .sav ITSELF IS RAW AND HEADERLESS -- region bytes of gamepak_backup[] and
 * nothing else -- so any other GBA emulator can read it. All metadata lives in
 * the sidecar .hdr. */
#define GBA_SAVE_HDR_BYTES     32u
#define GBA_SAVE_HDR_VERSION    1u

#define GBA_SAVE_HDR_O_MAGIC     0u
#define GBA_SAVE_HDR_O_VERSION   4u
#define GBA_SAVE_HDR_O_CLASS     6u
#define GBA_SAVE_HDR_O_REGION    8u
#define GBA_SAVE_HDR_O_DECLARED 12u
#define GBA_SAVE_HDR_O_ROMFNV   16u
#define GBA_SAVE_HDR_O_PAYFNV   20u
#define GBA_SAVE_HDR_O_CONF     24u
#define GBA_SAVE_HDR_O_RESERVED 26u

#define GBA_SAVE_MAGIC_0 'L'
#define GBA_SAVE_MAGIC_1 'G'
#define GBA_SAVE_MAGIC_2 'S'
#define GBA_SAVE_MAGIC_3 '1'

/* --------------------------------------------------- report-only findings --
 *
 * OBSERVATIONS ABOUT THE CARTRIDGE OR THE LIMITS OF THE EVIDENCE. NONE OF THEM
 * FAILS M12B and none of them changes what is written. Same polarity discipline
 * as M11's M11_R_* mask. */
#define GBA_SAVE_R_NOLATCH    (1u << 0) /* gba_save_latch() never ran         */
#define GBA_SAVE_R_NOROM      (1u << 1) /* no cartridge is resident           */
#define GBA_SAVE_R_UNKNOWN    (1u << 2) /* class UNKNOWN -- refuses to persist*/
#define GBA_SAVE_R_SANITISED  (1u << 3) /* the game code held a byte that is
                                           not [A-Za-z0-9] and was replaced   */
#define GBA_SAVE_R_BLANK      (1u << 4) /* the region is still entirely 0xFF  */
#define GBA_SAVE_R_CLEAN      (1u << 5) /* nothing changed since the latch    */
#define GBA_SAVE_R_CLASSMOVED (1u << 6) /* the class differs from the latch   */
#define GBA_SAVE_R_EEPDEFAULT (1u << 7) /* EEPROM sized 512 B BY DEFAULT --
                                           the 14-bit DMA never ran, so the
                                           size is assumed, not measured      */
#define GBA_SAVE_R_GREW       (1u << 8) /* the region is larger than it was at
                                           the latch -- the class resolved
                                           upward during execution            */

/* =========================================================================
 *                                THE CALLS
 * ========================================================================= */

/* LATCH THE IDENTITY AND THE BASELINE. Call ONCE, IMMEDIATELY AFTER
 * m4_rom_load() RETURNS AND BEFORE reset_gba() #2 AND BEFORE ANY EXECUTION.
 *
 * ***** THE ORDERING IS LOAD-BEARING AND IT IS NOT THE SAME REASON AS M11's. *
 *
 * M11 latched early so its runtime diff had a fixed baseline. M12B must latch
 * early for a HARDER reason: THE ROM BYTES DO NOT STAY PUT.
 *
 * With -DROM_BUFFER_SIZE=2 a cartridge larger than 2 MB is DEMAND PAGED.
 * load_gamepak_page() (gba_memory.c:2319-2357) calls evict_gamepak_page(), and
 * the block index it reclaims can name gamepak_buffers[0] -- there is NO
 * PINNING of the first buffer. So during execution the bytes at ROM offset
 * 0xAC (the game code) and every byte the identity hash covers can be replaced
 * by an arbitrary paged-in 32 KB block.
 *
 * An identity computed at EXIT would therefore be CACHE-DEPENDENT: the same
 * cartridge would produce a different <ID> -- and so a different .sav filename
 * -- on different runs, silently. Latched here, before a single ROM
 * instruction has executed, buffer 0 still holds the first megabyte of the
 * cartridge exactly as load_gamepak_raw() placed it.
 *
 * THE HASH IS BOUNDED TO min(gamepak_size, gamepak_buffer_blocksize) AND THAT
 * IS A SECOND, SEPARATE POINT. gamepak_buffers[0] is a SINGLE 1 MB allocation
 * (gba_memory.c:369, :378) but rom_resident_bytes() reports
 * buffer_count * 1 MB, which is 2 MB under this build's flags. Bounding by the
 * BLOCK SIZE keeps every read inside buffer 0 BY CONTRACT rather than by
 * relying on the allocator happening to place block 1 immediately after it.
 *
 * ***** THIS IS NOT THE SAME NUMBER AS m4_rom_fnv1a() AND MUST NOT BE
 * PRESENTED AS ONE. ***** M4's hash covers min(file_size, resident_bytes) and
 * is reported by M4-M11 as the ROM's integrity figure. M12B's covers
 * min(gamepak_size, 1 MB) -- gamepak_size is the 32 KB-rounded, 0xFF-padded
 * size, not the file size -- so for any cartridge over 1 MB, or any whose file
 * size is not a multiple of 32 KB, THE TWO NUMBERS DIFFER. M12B's is a stable
 * SAVE IDENTITY and is labelled as such everywhere it is printed.
 * adapters/gba/gba_rom.c is NOT modified and m4_rom_fnv1a() is NOT redefined.
 *
 * ALSO LATCHED HERE: backup_type, flash_bank_cnt and eeprom_size (for the
 * confidence derivation) and the FULL 131,072-byte baseline hash (for the
 * dirty decision).
 *
 * Idempotent by refusal: latching twice would absorb whatever ran in between
 * into the baseline, so a second call returns immediately.
 *
 * PURELY READ-ONLY. It walks only gamepak_buffers[0], which
 * gamepak_buffer_count already reports resident, so it cannot page the
 * cartridge in and cannot fault. */
void gba_save_latch(void);

/* 1 once gba_save_latch() has run. Everything below returns a safe zero
 * before that, but a fixture must check this rather than trust the zeros. */
unsigned int gba_save_latched(void);

#ifdef LUAPORT_SESSION_REUSE
/* ***** M16 ONLY. END THE CURRENT LATCH LIFECYCLE. *****
 *
 * Declared only when LUAPORT_SESSION_REUSE is defined, which is only on M16
 * targets -- M13C's preprocessed view of this header does not contain it.
 *
 * Clears the identity (gs_id, gs_rom_hash, hashed bytes, the sanitised flag),
 * the latched save-state baseline (backup_type, flash_bank_cnt, eeprom_size,
 * class, region), both dirty-decision hashes and the activity sampler, so the
 * NEXT cartridge can establish its own identity through gba_save_latch().
 *
 * ***** WITHOUT THIS, A SECOND CARTRIDGE COMMITS INTO THE FIRST CARTRIDGE'S
 * SAVE FILE. ***** gba_save_latch() refuses to re-latch, by design, so the
 * identity would otherwise persist for the whole payload lifetime.
 *
 * ***** MUST BE CALLED AFTER THE COMMIT, NEVER BEFORE. ***** The writer builds
 * its filename from the identity this clears; calling it first makes the commit
 * fail with GBA_SAVEFILE_ENOID.
 *
 * Changes no format, no policy and no file. Touches only this module's .bss. */
void gba_save_unlatch(void);
#endif

/* ---------------------------------------------------------- the answers --- */

/* The class RIGHT NOW, one of GBA_SAVE_*. Resolved from backup_type TOGETHER
 * WITH flash_bank_cnt / eeprom_size -- never from backup_type alone, because
 * BACKUP_FLASH covers two sizes and BACKUP_EEPROM covers two more. */
unsigned int gba_save_class(void);

/* The class as it was AT THE LATCH. The pair is what makes "the class moved
 * during execution" a statement rather than a guess. */
unsigned int gba_save_latch_class(void);

/* What gpSP believes the chip holds. 0 for UNKNOWN -- which is the honest
 * answer, not a reason to substitute 32768. */
unsigned int gba_save_declared_bytes(void);

/* HOW MANY BYTES M12B PERSISTS. See THE SIZE POLICY above: this is the only
 * place EEPROM512 diverges from its declared size. Never exceeds
 * GBA_SAVE_BACKUP_BYTES; 0 for UNKNOWN, which is what refuses persistence. */
unsigned int gba_save_region_bytes(void);

/* One of GBA_SAVE_CONF_*. Recorded in the .hdr. NEVER changes what is
 * written -- a PROVISIONAL EEPROM512 still persists its full 8192-byte
 * region. */
unsigned int gba_save_confidence(void);

/* ------------------------------------------------------------- identity --- */

/* Copy the latched save identity ("ATHE_1234ABCD") into a caller-owned buffer,
 * NUL-terminated. Writes an empty string if the latch never ran. */
void gba_save_id_copy(char *dst, unsigned int dstsz);

/* M12B's OWN ROM hash -- see the warning in gba_save_latch() about why this is
 * not m4_rom_fnv1a()'s number. */
unsigned int gba_save_rom_hash(void);

/* Build "/savedata0/gba/<ID>.sav" / ".hdr" into a caller-owned buffer.
 * Returns the length written, or 0 if the identity is not available or the
 * buffer is too small -- in which case dst is set to the empty string.
 *
 * NOT NAMED *_path_*: see the NAMING DISCIPLINE note at the top of this file.
 * tools/check_forbidden.py:109 matches the bare substring "path_". */
unsigned int gba_save_name_sav(char *dst, unsigned int dstsz);
unsigned int gba_save_name_hdr(char *dst, unsigned int dstsz);

/* ---------------------------------------------------------- THE DIRTY MODEL --
 *
 * M12B OWNS THIS AND DELIBERATELY DOES NOT REUSE M11's STICKY EVIDENCE.
 * M11's M11_EV_BACKUP_DIRTY answers "did the region change at any point during
 * the observation window", which is the right question for an observer and the
 * WRONG one for a persister: a game that wrote a byte and wrote it back would
 * latch M11 dirty forever while the bytes on exit are identical to the ones a
 * commit would already have stored.
 *
 * SO THE DECISION IS A HASH COMPARISON OVER THE FULL 131,072 BYTES, taken at
 * the latch and again on clean exit.
 *
 * WHY THE FULL ARRAY AND NOT THE REGION. THE REGION CAN CHANGE SIZE UNDERNEATH
 * US. backup_type can move from UNKN to EEPROM at gba_memory.c:542 and
 * eeprom_size from 512 B to 8 KB at :841-843, both mid-run. A baseline hashed
 * over a 32 KB region is not comparable with an exit hash over an 8192-byte
 * one, and comparing them would report DIRTY for a pure reclassification in
 * which not one byte changed. Hashing the whole array is immune to that, costs
 * one 128 KB pass at each end of the run, and cannot produce a false clean.
 *
 * THE PAYLOAD HASH IS A DIFFERENT QUANTITY and is computed over the REGION
 * only -- it is what goes in the .hdr and what the operator sees as SAV HASH.
 */

/* Take (or re-take) the exit hash over the full 131,072 bytes. Idempotent in
 * the sense that calling it again simply re-measures; the fixture calls it once
 * after the frame loop. */
void gba_save_finish(void);

/* 1 when the full-array hash differs from the latch-time one. Calls
 * gba_save_finish() itself if the fixture has not, so a missing call cannot
 * silently produce "COMMIT NOT NEEDED" for a game that saved. Returns 0 when
 * the latch never ran -- refusing to write is the safe direction. */
unsigned int gba_save_is_dirty(void);

unsigned int gba_save_baseline_hash(void);   /* full 131072, at the latch     */
unsigned int gba_save_exit_hash(void);       /* full 131072, at gba_save_finish */

/* FNV-1a over the PERSISTED REGION as it stands now. This is the payload hash
 * that goes into the .hdr and that the .sav must match. 0 for UNKNOWN. */
unsigned int gba_save_region_hash(void);

/* Bytes in the persisted region that are not 0xFF. Zero means nothing has ever
 * been saved -- the region is still the idle state gba_memory.c:2907
 * normalises a blank backup to. */
unsigned int gba_save_nonff(void);

/* ---- the coarse activity sampler ---------------------------------------
 *
 * OPTIONAL AND HONESTLY LABELLED. Called by the fixture every N frames rather
 * than every frame: a per-frame pass over 131,072 bytes would put 7.8 MB/s of
 * hashing inside the emulation loop for a FLASH128 cartridge, which is a real
 * cost for a number that is diagnostic only.
 *
 * IT IS THEREFORE NOT "WRITE FRAMES". M11's WRITE FRAMES counted frames on
 * which the region changed; this counts SAMPLES on which it changed, at a
 * coarser cadence, and it is spelled differently on the screen and in the log
 * for exactly that reason. It never participates in the commit decision --
 * that is gba_save_is_dirty()'s, which compares the two endpoints and cannot
 * miss a change that happened between samples. */
void         gba_save_sample(void);
unsigned int gba_save_samples(void);         /* how many samples were taken   */
unsigned int gba_save_active_samples(void);  /* how many saw a change         */

/* ---------------------------------------------------------- the byte tap --
 *
 * COPY UP TO n BYTES OF THE PERSISTED REGION, starting at off, into a
 * caller-owned buffer. Returns the number of bytes actually copied, which is 0
 * once off reaches the region size.
 *
 * THIS IS THE ONLY WAY THE SAVE BYTES CROSS THE TYPE BOUNDARY, and it is why
 * M12B needs NO second copy of gamepak_backup[]. gba_savefile.c stages through
 * a 4096-byte STACK buffer -- matching runtime/shim.c's WBUF_SIZE -- so the
 * whole persistence path adds ZERO bytes of .bss for the payload, as against
 * the 128 KB a shadow buffer would have cost.
 *
 * `unsigned char` is u8 on BOTH sides (gpsp/common.h:94, runtime/core.h:35),
 * so this pointer is exactly type-correct across the boundary. */
unsigned int gba_save_chunk(unsigned char *dst, unsigned int off,
                            unsigned int n);

/* ------------------------------------------------------------ the header --
 *
 * BUILD THE 32-BYTE .hdr INTO A CALLER-OWNED BUFFER. Returns GBA_SAVE_HDR_BYTES
 * on success, 0 if the buffer is too small or no class is persistable.
 *
 * IT LIVES ON THIS SIDE OF THE BOUNDARY ON PURPOSE. The header is pure byte
 * arithmetic over values this file already owns, and putting it here is what
 * lets tools/gba_save_equiv.c check the exact on-disk layout offline -- the
 * runtime half cannot be compiled by the host harness at all, because it
 * includes runtime/savedata.h and therefore runtime/core.h.
 *
 * payload_fnv is passed in rather than recomputed so the value written to the
 * header is PROVABLY the same one the caller reported and compared against the
 * .sav, instead of a second measurement that could differ if anything moved in
 * between. */
unsigned int gba_save_header_build(unsigned char *dst, unsigned int dstsz,
                                   unsigned int payload_fnv);

/* ------------------------------------------------------- report and log --- */

/* GBA_SAVE_R_* bits. Observations, never failures. */
unsigned int gba_save_report(void);

/* One raw value at a time, for the UDP log and for ext->dbg[]. Returns 0 for
 * an unknown selector. */
unsigned int gba_save_read(unsigned int sel);

#define GBA_SAVE_RD_CLASS        0u
#define GBA_SAVE_RD_DECLARED     1u
#define GBA_SAVE_RD_REGION       2u
#define GBA_SAVE_RD_CONF         3u
#define GBA_SAVE_RD_ROMHASH      4u
#define GBA_SAVE_RD_BASEHASH     5u
#define GBA_SAVE_RD_EXITHASH     6u
#define GBA_SAVE_RD_REGIONHASH   7u
#define GBA_SAVE_RD_NONFF        8u
#define GBA_SAVE_RD_DIRTY        9u
#define GBA_SAVE_RD_LATCH_CLASS 10u
#define GBA_SAVE_RD_LATCH_DECL  11u
#define GBA_SAVE_RD_LATCH_REGION 12u
#define GBA_SAVE_RD_BACKUP_TYPE 13u   /* backup_type            RAW           */
#define GBA_SAVE_RD_BANKCNT     14u   /* flash_bank_cnt         RAW (1 or 2)  */
#define GBA_SAVE_RD_EEPSIZE     15u   /* eeprom_size            RAW (1 or 16) */
#define GBA_SAVE_RD_SAMPLES     16u
#define GBA_SAVE_RD_ACTIVE      17u
#define GBA_SAVE_RD_REPORT      18u
#define GBA_SAVE_RD_ROMSIZE     19u   /* gamepak_size                          */
#define GBA_SAVE_RD_HASHED      20u   /* bytes the identity hash covered       */

#endif /* LUAPORT_GBA_SAVE_H */
