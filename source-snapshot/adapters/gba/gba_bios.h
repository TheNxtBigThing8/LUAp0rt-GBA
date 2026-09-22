/* LUAport M3 -- adapters/gba/gba_bios.h
 *
 * The NARROW, TYPE-SAFE BOUNDARY between the M3 fixture and gpSP's BIOS path.
 *
 * WHY THIS HEADER EXISTS AT ALL
 * -----------------------------
 * Exactly the reason adapters/gba/gba_probe.h exists, and the rule is unchanged
 * at M3: gpsp/common.h:100 typedefs u64 as `unsigned long long` while
 * runtime/core.h:30 typedefs it as `unsigned long`. Both are 64 bits on this ABI
 * but they are DISTINCT TYPES in C, so any translation unit including both fails
 * on the duplicate typedef.
 *
 * apps/m3gpsp/main.c therefore never includes a gpSP header. All gpSP contact
 * lives in adapters/gba/gba_bios.c, which includes gpsp/common.h and NOTHING
 * from runtime/, and is reached only through this file.
 *
 * THE TYPE RULE, UNCHANGED FROM M2
 * --------------------------------
 * Every declaration below uses ONLY `unsigned int`, `int` and `const char *`.
 * `unsigned int` is `u32` in BOTH worlds, and `int`/`const char *` are not
 * typedef'd by either, so the prototypes the two sides see are the SAME TYPES.
 * Nothing 64-bit crosses this boundary. No gpSP struct, enum, or pointer-to-gpSP
 * -state ever appears here.
 *
 * WHY M3 NEEDS ITS OWN ADAPTER RATHER THAN EXTENDING gba_probe.h
 * -------------------------------------------------------------
 * adapters/gba/gba_probe.{c,h} are FROZEN M2 evidence and are on the protected
 * list. M3 links gba_probe.o UNMODIFIED and reuses it verbatim for the step-48
 * M2 regression check and for the CPU snapshot that proves nothing executed.
 * The BIOS-specific code is additive and lives here.
 *
 * WHAT THE M3 BIOS CONTRACT ACTUALLY IS
 * -------------------------------------
 * Traced read-only before any of this was written. The whole surface is small:
 *
 *   gpsp/gba_memory.c:357   u8 bios_rom[1024 * 16];      <- .bss, exactly 16 KB
 *   gpsp/gba_memory.c:3093  s32 load_bios(char *name)
 *   gpsp/gba_memory.c:2410  map_region(read, 0, 0x1000000, 1, bios_rom)
 *   gpsp/gba_memory.h:277   extern u8 bios_rom[1024 * 16];
 *   gpsp/gba_memory.h:259   s32 load_bios(char *name);
 *
 * load_bios() and all five filestream entry points are ALREADY LINKED into the
 * M2 image (gba_filestream.c was paid for at M1), so M3's loader costs ZERO new
 * .text for the loading itself.
 *
 * THE CENTRAL FINDING THIS HEADER IS BUILT AROUND
 * ----------------------------------------------
 * load_bios() VALIDATES NOTHING:
 *
 *     RFILE *fd = filestream_open(name, ...);
 *     if(!fd) return -1;
 *     filestream_read(fd, bios_rom, 0x4000);   <- RETURN VALUE DISCARDED
 *     filestream_close(fd);
 *     return 0;
 *
 * It returns -1 only if the OPEN fails. A 1-byte file, an empty file and a 4 MB
 * file all return 0 -- "success". A short read silently leaves the tail of
 * bios_rom as whatever it was; an oversized file is silently truncated to 16 KB.
 * There is no checksum, no size check and no signature check anywhere in it.
 *
 * So "BIOS handling" CANNOT mean "call load_bios and check the return code".
 * All meaningful validation is LUAport-side, and that is what the probes below
 * are for. This is structurally the same move M2 made when it found that
 * reset_gba() never calls init_sound().
 *
 * BITMASK CONVENTION: A SET BIT IS A FAILED ASSERTION -- identical to M2, so a
 * success is a single `== 0` test and a newly added assertion cannot read as
 * "pass" if the fixture forgets to name it.
 *
 * gpsp/ IS NOT MODIFIED BY ANY OF THIS. bios_rom and load_bios are both
 * already declared in gpsp/gba_memory.h, so M3 needs no new extern and no edit.
 */

#ifndef LUAPORT_GBA_BIOS_H
#define LUAPORT_GBA_BIOS_H

/* ---------------------------------------------------------- the contract --
 *
 * The exact size gpSP reads, from gpsp/gba_memory.c:3101 (`0x4000`) and the
 * definition at :357 (`u8 bios_rom[1024 * 16]`). Spelled once here so the
 * fixture and the adapter cannot drift apart. */
#define M3_BIOS_SIZE 16384u

/* ----------------------------------------------------- candidate  sources --
 *
 * ORDERED LIST, FIRST HIT WINS. Resolved at run time; the index of the one that
 * opened is reported so a hardware log says WHICH file was used.
 *
 *   0  /temp0/gba_bios.bin            PRIMARY -- the M3 bring-up path.
 *   1  /savedata0/bios/gba_bios.bin   FALLBACK -- the persistent convention.
 *
 * Both are established by the LuaPSX precedent (LuaPSX/README.md:78-100,
 * LuaPSX/src/mcard.c:3-19): /temp0 is writable from the game sandbox and is
 * where its network uploader puts bulk content; /savedata0 is where it keeps a
 * BIOS persistently, and may be mounted READ-ONLY -- which is fine, M3 only
 * ever opens for reading.
 *
 * /temp0 is primary because it needs no save-manager ritual and survives a game
 * relaunch (though NOT a console reboot). Trying both costs about fifteen bytes
 * of .rodata and makes M3 robust to an unknown savedata mount state. This is not
 * a bring-up hack that M4 deletes: M4 keeps the same list and adds ROM paths.
 *
 * NO BIOS IS EMBEDDED IN THE IMAGE OR IN THE DELIVERY SCRIPT. The file is
 * user-supplied and uploaded out of band, as a separate explicit action. */
#define M3_BIOS_CANDIDATES 2u

/* Number of candidates, and the name of one. Returns 0 (NULL) for an index that
 * is out of range. The strings are returned from a switch rather than held in a
 * static pointer table: a table would need one R_X86_64_RELATIVE relocation per
 * entry, whereas a returned literal compiles to a RIP-relative LEA and needs
 * none. Same technique, and the same reason, as apps/m2gpsp/main.c's bit-name
 * functions. */
unsigned int m3_bios_candidate_count(void);
const char  *m3_bios_candidate_name(unsigned int idx);

/* ------------------------------------------------------------- actions --- */

/* STEP 49/50 -- open a candidate, measure it, close it. NOTHING IS LOADED.
 *
 * Returns 1 if the candidate OPENED (and *size_out is its byte length), 0 if it
 * could not be opened at all. A candidate that opens but whose size cannot be
 * determined reports size 0, which the fixture's exact-size gate then rejects.
 *
 * This exists because load_bios() CANNOT report a short read (see the header
 * comment above). Measuring the file BEFORE handing it to load_bios is the only
 * way to know that the 0x4000 bytes it blindly reads were all really there. */
int m3_bios_query(unsigned int idx, unsigned int *size_out);

/* STEP 51 -- call gpSP's own load_bios() on a candidate.
 *
 * Returns load_bios()'s s32 verbatim: 0 on success, -1 if the open failed.
 * REMEMBER THAT A 0 HERE PROVES ONLY THAT THE FILE OPENED. The content probes
 * below are what actually establish that a BIOS arrived.
 *
 * load_bios takes `char *`, not `const char *` (gba_memory.h:259), so the name
 * is copied into a writable static buffer first rather than casting away const
 * on a string literal. load_bios does not modify it; the copy is for
 * type-correctness, not for safety against gpSP. */
int m3_bios_load(unsigned int idx);

/* -------------------------------------------------------------- probes --- */

/* STEP 44 -- the BEFORE half of the BIOS proof.
 *
 * bios_rom lives in .bss (confirmed in build/m2/m2gpsp.map: `bios_rom` at
 * 0x148080, inside .bss 0x52860..0x1ABCC0), so it must be entirely zero before
 * anything loads. NOTHING ON THE reset_gba() PATH MEMSETS IT -- init_memory's
 * memset list (gba_memory.c:2421-2426) covers io_registers, oam_ram,
 * palette_ram, iwram, ewram and vram, and NOT bios_rom. That is what makes the
 * load order free, and it is why observing zero here is meaningful even though
 * this runs after reset_gba().
 *
 * A failure here means .bss was not zeroed, which would invalidate every other
 * assertion in the fixture -- so the fixture treats it as FATAL. */
unsigned int m3_bios_probe_pre(void);

/* STEP 52 -- the AFTER half. Real validation, none of which gpSP performs. */
unsigned int m3_bios_probe_content(void);

/* STEP 53 -- the mapping contract and the ROM boundary. */
unsigned int m3_bios_probe_map(void);

/* One raw value at a time, for the diagnostic log. Selectors below. Returns 0
 * for an unknown selector. */
unsigned int m3_bios_read(unsigned int sel);

/* ---- pre-check bits (m3_bios_probe_pre) --------------------------------- */
#define M3_PRE_BIOS_ZERO       (1u << 0)  /* bios_rom was NOT all zero        */

/* ---- content bits (m3_bios_probe_content) -------------------------------
 *
 * The four hard checks, in ascending strictness. Every one of them is a
 * STRUCTURAL fact about the file, never a claim about specific BIOS content.
 *
 * DELIBERATELY NOT ASSERTED: any particular hash. Pinning the official Nintendo
 * BIOS's CRC would (a) reject the GPL2 open-source replacement BIOS, which is a
 * perfectly valid input, and (b) hard-code a fingerprint of copyrighted data we
 * do not distribute. The checksum below is REPORTED FOR DIAGNOSIS and never
 * causes a failure. */
#define M3_CONTENT_ALLZERO     (1u << 0)  /* still entirely zero -- no read   */
#define M3_CONTENT_ALLFF       (1u << 1)  /* entirely 0xFF -- blank/erased    */
/* bios_rom[0] must be 0x18. This is UPSTREAM'S OWN heuristic, not one invented
 * here: gpsp/libretro/libretro.c:1265 tests exactly `bios_rom[0] != 0x18`.
 * 0x18 is the low byte of the GBA BIOS's first instruction, `b 0x000000E0`,
 * which little-endian encodes as 18 00 00 EA. */
#define M3_CONTENT_FIRSTBYTE   (1u << 2)  /* bios_rom[0] != 0x18              */

/* ---- mapping bits (m3_bios_probe_map) -----------------------------------
 *
 * From init_memory(), gpsp/gba_memory.c:2410:
 *     map_region(read, 0x0000000, 0x1000000, 1, bios_rom);
 * map_region (gba_memory.c:2265-2271) walks map_offset over
 * [0 .. 0x1000000/0x8000) = [0 .. 512) and stores
 *     bios_rom + ((map_offset % mirror_blocks) * 0x8000)
 * with mirror_blocks == 1, so that offset term is ZERO for every entry: ALL 512
 * entries hold exactly bios_rom. That is the full 16 MB BIOS window and it is
 * asserted in full rather than sampled. */
#define M3_MAP_BIOS0           (1u << 0)  /* memory_map_read[0] != bios_rom   */
#define M3_MAP_WINDOW          (1u << 1)  /* some entry of [0..511] differs   */
/* THE ROM BOUNDARY. init_memory never maps the 0x8000000 gamepak window -- it
 * is simply absent from the mapping list at gba_memory.c:2410-2419 -- so this
 * stays NULL unless a ROM has been loaded. A non-NULL here means M3 crossed
 * into M4 territory and the run must FAIL. */
#define M3_MAP_ROM_PRESENT     (1u << 2)  /* gamepak window is NOT NULL       */

/* ---- m3_bios_read selectors --------------------------------------------- */
#define M3_RD_BIOS_B0          0u  /* bios_rom[0]                            */
#define M3_RD_BIOS_B1          1u  /* bios_rom[1]                            */
#define M3_RD_BIOS_B2          2u  /* bios_rom[2]                            */
#define M3_RD_BIOS_B3          3u  /* bios_rom[3]                            */
#define M3_RD_BIOS_FNV         4u  /* FNV-1a 32 over all 16384 bytes         */
#define M3_RD_BIOS_SUM         5u  /* plain additive sum over all bytes      */
#define M3_RD_BIOS_NONZERO     6u  /* count of non-zero bytes                */
#define M3_RD_MAP_BIOS_OK      7u  /* 1 if all 512 window entries == bios_rom*/
#define M3_RD_MAP_ROM_NULL     8u  /* 1 if the gamepak window is NULL        */

#endif /* LUAPORT_GBA_BIOS_H */
