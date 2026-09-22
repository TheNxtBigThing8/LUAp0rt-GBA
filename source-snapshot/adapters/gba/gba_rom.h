/* LUAport M4 -- adapters/gba/gba_rom.h
 *
 * The NARROW, TYPE-SAFE BOUNDARY between the M4 fixture and gpSP's cartridge
 * (gamepak) path.
 *
 * WHY THIS HEADER EXISTS AT ALL
 * -----------------------------
 * Exactly the reason adapters/gba/gba_probe.h and adapters/gba/gba_bios.h
 * exist, and the rule is unchanged at M4: gpsp/common.h:100 typedefs u64 as
 * `unsigned long long` while runtime/core.h:30 typedefs it as `unsigned long`.
 * Both are 64 bits on this ABI but they are DISTINCT TYPES in C, so any
 * translation unit including both fails on the duplicate typedef.
 *
 * apps/m4gpsp/main.c therefore never includes a gpSP header. All gpSP contact
 * lives in adapters/gba/gba_rom.c, which includes gpsp/common.h and NOTHING
 * from runtime/, and is reached only through this file.
 *
 * THE TYPE RULE, UNCHANGED FROM M2 AND M3
 * ---------------------------------------
 * Every declaration below uses ONLY `unsigned int`, `int`, `char *` and
 * `const char *`. `unsigned int` is `u32` in BOTH worlds, and `int`/`char *`
 * are not typedef'd by either, so the prototypes the two sides see are the SAME
 * TYPES. Nothing 64-bit crosses this boundary. No gpSP struct, enum, or
 * pointer-to-gpSP-state ever appears here.
 *
 * WHY M4 NEEDS ITS OWN ADAPTER RATHER THAN EXTENDING gba_bios.h
 * -------------------------------------------------------------
 * adapters/gba/gba_probe.{c,h} are FROZEN M2 evidence and adapters/gba/
 * gba_bios.{c,h} are FROZEN M3 evidence; both are on the protected list. M4
 * links gba_probe.o and gba_bios.o UNMODIFIED and reuses them verbatim -- the
 * six M2 probes for the step-68 regression check and the CPU snapshot, and the
 * M3 BIOS loader and its probes for step 69. The cartridge-specific code is
 * additive and lives here.
 *
 * WHAT THE M4 CARTRIDGE CONTRACT ACTUALLY IS
 * ------------------------------------------
 * Traced read-only before any of this was written:
 *
 *   gpsp/gba_memory.c:2964  u32 load_gamepak(const struct retro_game_info *,
 *                                            const char *name, int force_rtc,
 *                                            int force_rumble, int force_serial)
 *   gpsp/gba_memory.c:2671  static s32 load_gamepak_raw(const char *name)
 *   gpsp/gba_memory.c:2359  void init_gamepak_buffer(void)
 *   gpsp/gba_memory.c:2319  u8 *load_gamepak_page(u32 physical_index)
 *   gpsp/gba_memory.c:369   u8 *gamepak_buffers[32]
 *   gpsp/gba_memory.c:378   const unsigned gamepak_buffer_blocksize = 1024*1024
 *
 * THE `info` PARAMETER IS ENTIRELY UNUSED. The body of load_gamepak
 * (gba_memory.c:2964-3091) references only `name`. Passing NULL is therefore
 * safe and lets M4 avoid the libretro type completely -- which matters, because
 * `struct retro_game_info` has no definition anywhere in this build.
 *
 * THREE FINDINGS THIS HEADER IS BUILT AROUND
 * ------------------------------------------
 * 1. gamepak_size IS NOT THE FILE SIZE. load_gamepak_raw rounds the on-disk
 *    size UP TO A 32 KB PAGE (gba_memory.c:2701) before storing it. For a
 *    4,096-byte ROM, gamepak_size is 32,768. Asserting equality with the raw
 *    file size would FAIL ON CORRECT CODE. See m4_rom_expected_size().
 *
 * 2. gamepak_code AND gamepak_filename ARE DECLARED BUT NEVER DEFINED.
 *    gpsp/gba_memory.h:252-253 declares both `extern`, but gamepak_code at
 *    gba_memory.c:1686 is a MEMBER of the ini_t struct, not a global, and
 *    gamepak_filename has no definition anywhere in the tree. load_gamepak uses
 *    a LOCAL `char game_code[5]`. Referencing either symbol would create an
 *    unresolved symbol and break the zero-unresolved-dependency gate, so M4
 *    re-reads the title and code from gamepak_buffers[0] itself.
 *
 * 3. init_gamepak_buffer() IS GREEDY. It loops `while (gamepak_buffer_count <
 *    ROM_BUFFER_SIZE)` calling malloc(1 MB) until one FAILS (gba_memory.c:
 *    2364-2370). Against the shim's bump allocator it therefore consumes the
 *    whole arena and always logs one "arena exhausted" line -- which is EXPECTED
 *    and is not a fault. M4 builds its gpSP objects with -DROM_BUFFER_SIZE=2 so
 *    the count is bounded by the flag rather than by arena exhaustion.
 *
 * BITMASK CONVENTION: A SET BIT IS A FAILED ASSERTION -- identical to M2 and M3,
 * so a success is a single `== 0` test and a newly added assertion cannot read
 * as "pass" if the fixture forgets to name it.
 *
 * NAMING DISCIPLINE. tools/check_forbidden.py:108-111 matches the BARE
 * SUBSTRINGS "path_", "retro_", "vfs_", "filestream_", "_stub" and "emit_"
 * against the lowercased symbol name. Every symbol below is m4_rom_* and none
 * contains any of them -- which is why the source accessors are called
 * m4_rom_candidate_name rather than the more obvious m4_rom_path_name. NO
 * ALLOWLIST IS WIDENED FOR M4.
 *
 * gpsp/ IS NOT MODIFIED BY ANY OF THIS.
 */

#ifndef LUAPORT_GBA_ROM_H
#define LUAPORT_GBA_ROM_H

/* ---------------------------------------------------------- the contract --
 *
 * gpSP rounds the on-disk size up to this granularity (gba_memory.c:2701)
 * and indexes memory_map_read[] in the same unit (gba_memory.c:2254-2278). */
#define M4_ROM_PAGE          32768u

/* The 1 MB allocation unit, gba_memory.c:378. Spelled here so the fixture can
 * report the expected runtime allocation without reaching into gpSP. */
#define M4_ROM_BLOCK         1048576u

/* PC-side and fixture-side source gate. load_gamepak_raw's OWN guards are
 * size-only -- it rejects `fsize <= 0` and `fsize > 0x20000000` (512 MB) at
 * gba_memory.c:2692 and will otherwise happily accept a text file. M4 adds a
 * tighter, honest gate around it rather than relaxing anything inside. */
#define M4_ROM_MIN_SIZE      1u
#define M4_ROM_MAX_SIZE      33554432u      /* 32 MB -- the ROM_BUFFER_SIZE=32
                                               ceiling upstream designs for   */

/* THE ONE SIZE M4 REFUSES. A file of EXACTLY 1 MB takes the mirror path at
 * gba_memory.c:2708 and attempts malloc(4 MB) for a materialised mirror image.
 * Against a 4 MB bump arena that is already partly consumed by the ROM buffers
 * it cannot succeed, and the fallback at :2748 then quietly reinterprets the
 * size. Rejecting the input up front is honest; silently taking the degraded
 * path is not. */
#define M4_ROM_MIRROR_SIZE   1048576u

/* ----------------------------------------------------- candidate  sources --
 *
 * ORDERED LIST, FIRST HIT WINS -- the same shape, and the same rationale, as
 * M3's BIOS candidate list (adapters/gba/gba_bios.h:84-104).
 *
 *   0  /temp0/test.gba              PRIMARY -- the M4 bring-up path.
 *   1  /savedata0/rom/test.gba      FALLBACK -- the persistent convention.
 *
 * /temp0 is primary because it needs no save-manager ritual and survives a game
 * relaunch (though NOT a console reboot); /savedata0 may be mounted READ-ONLY,
 * which is fine because M4 only ever opens for reading.
 *
 * NO ROM IS EMBEDDED IN THE IMAGE OR IN THE DELIVERY SCRIPT. The file is
 * uploaded out of band by an explicit separate action, exactly as the BIOS is
 * at M3, so the cartridge contributes ZERO bytes to .text/.rodata/.data. */
#define M4_ROM_CANDIDATES    2u

unsigned int m4_rom_candidate_count(void);
const char  *m4_rom_candidate_name(unsigned int idx);

/* ------------------------------------------------- M13B: names, not indices --
 *
 * THE BOUND ON A LOADABLE NAME, AND WHY IT IS 256 AND NOT 128.
 *
 * Until M13B every name reaching this file was one of the two compiled-in
 * literals above -- 16 and 24 bytes. The copy buffer was therefore 128 bytes and
 * the copy loop clamped at 127 silently. M13B hands this layer a path the USER
 * chose: "/temp0/" plus a filename the uploader preserves byte-for-byte, and
 * exFAT permits names far beyond what 128 bytes can hold.
 *
 * A SILENTLY TRUNCATED PATH IS THE WORST AVAILABLE OUTCOME. It does not fail
 * loudly; it either fails to open (merely confusing) or OPENS A DIFFERENT FILE.
 * adapters/gba/m13store.h:38-46 already refuses rather than truncates for exactly
 * this reason, and gba_library.c drops an over-long entry into rej_toolong rather
 * than storing a shortened one. This layer must not undo that one level lower.
 *
 * 256 is M13STORE_PATH_MAX. It is SPELLED SEPARATELY here, rather than by
 * including m13store.h, so that gba_rom.c keeps its M4-era include set (gpSP
 * headers and nothing from the M13 layer) and no milestone ordering is created
 * between them. A _Static_assert in gba_rom.c pins the buffer to this value. */
#define M4_ROM_NAME_MAX      256u

/* The name gpSP was last asked to open, as a NUL-terminated string.
 *
 * EXISTS SO THE EQUIVALENCE IS OBSERVABLE. tools/m13b_picker_equiv.c asserts
 * that m4_rom_load(0) and m4_rom_load_named(m4_rom_candidate_name(0)) leave
 * BYTE-IDENTICAL contents here -- which is a behavioural proof of the delegation
 * below, and is not something `nm` or a string grep could establish.
 *
 * Returns "" (never NULL) when nothing has been loaded or the last name was
 * REFUSED, so a caller may pass it straight to printf("%s"). */
const char  *m4_rom_name(void);

/* ------------------------------------------------------------- actions --- */

/* STEP 65 -- init_gamepak_buffer(), gba_memory.c:2359.
 *
 * MUST BE THE FIRST ALLOCATION IN THE PROCESS. The loop is greedy (see finding
 * 3 above), so anything that allocates before it reduces the block count. The
 * fixture asserts arena_used() == 0 immediately before calling this.
 *
 * Returns gamepak_buffer_count. A 0 here is FATAL: load_gamepak_raw computes
 * `ldblks` from it and returns -1 when it is zero (gba_memory.c:2763-2768). */
unsigned int m4_rom_buffer_init(void);

/* STEP 70/71 -- open a candidate, measure it, close it. NOTHING IS LOADED.
 *
 * Returns 1 if the candidate OPENED (and *size_out is its TRUE byte length), 0
 * if it could not be opened at all. A candidate that opens but cannot be
 * measured reports size 0, which the source gate then rejects.
 *
 * Separated from loading on purpose: a file-transfer failure and a cartridge
 * loading failure must be distinguishable, which is the same reason the
 * uploader is a separate Makefile target from m4-send. */
int m4_rom_query(unsigned int idx, unsigned int *size_out);

/* M13B -- the same measurement, by ABSOLUTE PATH instead of candidate index.
 *
 * IDENTICAL CONTRACT to m4_rom_query() in every respect except where the name
 * comes from: returns 1 if the file OPENED (and *size_out is its TRUE byte
 * length), 0 if it could not be opened at all, and 1 with *size_out == 0 if it
 * opened but could not be measured.
 *
 * m4_rom_query() is now a two-line delegation to this function, so there is ONE
 * implementation of the measurement and the index path cannot drift from the
 * path path. No length gate is applied here -- opening is a read-only probe and
 * the underlying filestream layer is free to fail on its own terms. The length
 * REFUSAL lives in m4_rom_load_named(), where it can actually do harm. */
int m4_rom_query_named(const char *name, unsigned int *size_out);

/* STEP 71 -- the source gate, applied to the TRUE file size BEFORE loading.
 * Returns a mask of M4_SRC_* bits; 0 means the file is an acceptable input. */
unsigned int m4_rom_probe_source(unsigned int file_size);

/* What gamepak_size MUST become for a given true file size: the size rounded UP
 * to a whole 32 KB page (gba_memory.c:2701). This is the ONLY correct form of
 * the size assertion -- see finding 1 above. */
unsigned int m4_rom_expected_size(unsigned int file_size);

/* STEP 72 -- memset(gamepak_backup, 0xFF, sizeof gamepak_backup).
 *
 * UPSTREAM PARITY, not an M4 invention: gpsp/libretro/libretro.c:1278 does
 * exactly this immediately before its load_gamepak call. gamepak_backup is
 * 128 KB of .bss (gba_memory.c:360) -- a buffer, NOT a file. No save file is
 * opened, read, created or written anywhere in M4. */
void m4_rom_backup_init(void);

/* STEP 73 -- call gpSP's own load_gamepak(), unmodified.
 *
 * Invoked as load_gamepak(NULL, name, FEAT_AUTODETECT, FEAT_AUTODETECT,
 * SERIAL_MODE_AUTO) -- the same three autodetect arguments upstream passes at
 * libretro.c:1279, so M4 exercises the real default path rather than a
 * specially-configured one.
 *
 * Returns 0 on success. load_gamepak's declared return type is u32 and it
 * returns -1 on failure, so the value is normalised to int here: any non-zero
 * result is a failure. */
int m4_rom_load(unsigned int idx);

/* M13B -- the same load, by ABSOLUTE PATH instead of candidate index.
 *
 * THIS IS THE ONLY PLACE load_gamepak() IS CALLED. m4_rom_load() delegates here
 * after resolving its index, so both entry points issue the SAME call with the
 * SAME five arguments in the SAME order. gpsp/ is not modified, and gpSP has
 * no idea an index API ever existed -- it has always taken `const char *name`.
 *
 * ***** REFUSAL, NOT TRUNCATION. ***** Returns -1 WITHOUT CALLING gpSP if
 * `name` is NULL or does not fit in M4_ROM_NAME_MAX including its NUL. The
 * refusal is the assertion: a path this layer cannot spell in full is a path it
 * must not open a NEIGHBOUR of. On refusal m4_rom_name() reports "" rather than
 * a stale or partial name.
 *
 * Returns 0 on success, -1 on failure (load_gamepak is declared u32 and returns
 * -1 on failure, so the value is normalised here exactly as before). */
int m4_rom_load_named(const char *name);

/* -------------------------------------------------------------- probes --- */

/* STEP 68 -- the BEFORE half of the cartridge proof, run after reset_gba() #1
 * and before any ROM is loaded. */
unsigned int m4_rom_probe_pre(void);

/* STEP 75 -- cartridge metadata, read straight out of gpSP's own globals. */
unsigned int m4_rom_probe_meta(unsigned int file_size);

/* STEP 75 -- header/content validation on the RESIDENT bytes in
 * gamepak_buffers[0], including the 0xFF short-read padding contract. */
unsigned int m4_rom_probe_content(unsigned int file_size);

/* STEP 77/78 -- the mapping contract: the three cartridge windows, their
 * mirrors, the 0x0D EEPROM boundary, and the BIOS window's survival. */
unsigned int m4_rom_probe_map(void);

/* FNV-1a 32 over the TRUE ROM length (not the 0xFF-padded page). Byte-for-byte
 * the same algorithm as adapters/gba/gba_bios.c:225 and tools/upload.py, so the
 * console figure and the PC figure can be compared directly.
 *
 * REPORTED, NEVER ASSERTED. No ROM hash is pinned. */
unsigned int m4_rom_fnv1a(unsigned int file_size);

/* Copy the 12-byte title at 0xA0 / the 4-byte game code at 0xAC out of the
 * resident ROM image into a caller-owned buffer, NUL-terminated, with any
 * non-printable byte rendered as '.' so a hardware log is always readable.
 *
 * These exist because gamepak_code and gamepak_filename are UNDEFINED symbols
 * (finding 2 above) -- M4 must read the header itself. */
void m4_rom_title_copy(char *dst, unsigned int dstsz);
void m4_rom_code_copy(char *dst, unsigned int dstsz);

/* One raw value at a time, for the diagnostic log. Selectors below. Returns 0
 * for an unknown selector. */
unsigned int m4_rom_read(unsigned int sel);

/* One header byte out of the resident image, for the log. Returns 0 if no ROM
 * is resident or the offset is outside the first 32 KB page. */
unsigned int m4_rom_header_byte(unsigned int off);

/* ---- source-gate bits (m4_rom_probe_source) -----------------------------
 *
 * Applied to the TRUE on-disk size before load_gamepak sees it. */
#define M4_SRC_EMPTY        (1u << 0)  /* 0 bytes, or could not be measured   */
#define M4_SRC_TOOBIG       (1u << 1)  /* larger than M4_ROM_MAX_SIZE         */
#define M4_SRC_MIRROR_1M    (1u << 2)  /* exactly 1 MB -- the 4 MB mirror path*/

/* ---- pre-load bits (m4_rom_probe_pre) ------------------------------------ */
#define M4_PRE_NO_BUFFERS   (1u << 0)  /* gamepak_buffer_count == 0 -- FATAL  */
#define M4_PRE_BUF0_NULL    (1u << 1)  /* gamepak_buffers[0] == NULL          */
#define M4_PRE_SIZE_SET     (1u << 2)  /* gamepak_size != 0 before any load   */
#define M4_PRE_ROM_MAPPED   (1u << 3)  /* gamepak window already NOT NULL     */

/* ---- metadata bits (m4_rom_probe_meta) ----------------------------------
 *
 * Every one is a STRUCTURAL fact derived from load_gamepak_raw, with the line
 * that establishes it cited in gba_rom.c. */
#define M4_META_SIZE        (1u << 0)  /* gamepak_size != round_up(file,32K)  */
#define M4_META_BLOCKS      (1u << 1)  /* gamepak_file_blocks wrong           */
#define M4_META_MIRROR1M    (1u << 2)  /* gamepak_mirror_1m set               */
#define M4_META_MINI        (1u << 3)  /* gamepak_mini_materialized set       */
#define M4_META_BUFCOUNT    (1u << 4)  /* gamepak_buffer_count < 1            */
#define M4_META_BUF0_NULL   (1u << 5)  /* gamepak_buffers[0] == NULL          */
#define M4_META_HEADER      (1u << 6)  /* gamepak_header_nonstandard set      */
#define M4_META_LRU         (1u << 7)  /* page 0 not recorded in the LRU queue*/

/* ---- content bits (m4_rom_probe_content) --------------------------------
 *
 * DELIBERATELY NOT ASSERTED: any particular hash. The FNV-1a value is REPORTED
 * FOR DIAGNOSIS and never causes a failure -- pinning it would hard-code a
 * fingerprint of a specific ROM. */
#define M4_CONTENT_ALLZERO  (1u << 0)  /* resident page entirely zero         */
#define M4_CONTENT_EA       (1u << 1)  /* byte[3]    != 0xEA                  */
#define M4_CONTENT_96       (1u << 2)  /* byte[0xB2] != 0x96                  */
#define M4_CONTENT_PAD      (1u << 3)  /* short-read tail is not 0xFF         */

/* ---- mapping bits (m4_rom_probe_map) ------------------------------------
 *
 * From map_rom_entry (gba_memory.c:2254-2263) with mirror_blocks == rom_blocks.
 * For a single-page ROM every entry from 0x08000000 through 0x0CFFFFFF holds
 * gamepak_buffers[0], and 0x0D000000 -- the EEPROM window -- stays NULL because
 * load_gamepak_raw's map_null only clears up to 0xD000000 (gba_memory.c:2771). */
#define M4_MAP_ROM08        (1u << 0)  /* 0x08000000 != gamepak_buffers[0]    */
#define M4_MAP_ROM0A        (1u << 1)  /* 0x0A000000 mirror missing           */
#define M4_MAP_ROM0C        (1u << 2)  /* 0x0C000000 mirror missing           */
#define M4_MAP_MIRROR       (1u << 3)  /* some entry in 0x08..0x0C differs    */
#define M4_MAP_EEPROM       (1u << 4)  /* 0x0D000000 is NOT NULL -- overmapped*/
#define M4_MAP_BIOS         (1u << 5)  /* memory_map_read[0] != bios_rom      */

/* ---- m4_rom_read selectors ---------------------------------------------- */
#define M4_RD_SIZE            0u  /* gamepak_size                            */
#define M4_RD_FILE_BLOCKS     1u  /* gamepak_file_blocks                     */
#define M4_RD_MIRROR_1M       2u  /* gamepak_mirror_1m                       */
#define M4_RD_MINI            3u  /* gamepak_mini_materialized               */
#define M4_RD_BUFFER_COUNT    4u  /* gamepak_buffer_count                    */
#define M4_RD_BLOCKSIZE       5u  /* gamepak_buffer_blocksize                */
#define M4_RD_HDR_NONSTD      6u  /* gamepak_header_nonstandard              */
#define M4_RD_KNOWN_GAME      7u  /* is_known_game (gba_over lookup result)  */
#define M4_RD_BACKUP_TYPE     8u  /* backup_type                             */
#define M4_RD_BACKUP_RESET    9u  /* backup_type_reset                       */
#define M4_RD_LRU_HEAD       10u  /* gamepak_lru_head                        */
#define M4_RD_LRU_TAIL       11u  /* gamepak_lru_tail                        */
#define M4_RD_PAGE0_ENTRY    12u  /* 1 if some LRU entry maps physical page 0*/
#define M4_RD_MAP08_OK       13u  /* 1 if 0x08000000 == gamepak_buffers[0]   */
#define M4_RD_MAP0A_OK       14u  /* 1 if 0x0A000000 == gamepak_buffers[0]   */
#define M4_RD_MAP0C_OK       15u  /* 1 if 0x0C000000 == gamepak_buffers[0]   */
#define M4_RD_MAP0D_NULL     16u  /* 1 if 0x0D000000 is NULL                 */
#define M4_RD_MIRROR_OK      17u  /* 1 if every 0x08..0x0C entry matches     */
#define M4_RD_BIOS_MAPPED    18u  /* 1 if memory_map_read[0] == bios_rom     */
#define M4_RD_ROM_RESIDENT   19u  /* bytes of ROM actually resident in RAM   */

/* DELIBERATELY ABSENT: rtc_enabled and rumble_enabled.
 *
 * gpsp/gba_memory.c:1291 declares them `static bool rtc_enabled = false,
 * rumble_enabled = false;` -- they have INTERNAL LINKAGE and are therefore not
 * externally observable AT ALL. Reporting them would require either an edit to
 * gpsp/ (forbidden) or a fabricated value (dishonest), so M4 reports neither
 * and says so here instead. This is the same discipline gba_probe.c applies to
 * gbc_sound_tick_step and sound_buffer (adapters/gba/gba_probe.c:47-50).
 *
 * The OBSERVABLE proxy for the same question is is_known_game
 * (M4_RD_KNOWN_GAME): both flags are only ever set away from false by
 * load_game_config_over, and that function sets is_known_game in the same
 * branch (gba_memory.c:1710-1763). */

#endif /* LUAPORT_GBA_ROM_H */
