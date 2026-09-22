/* LUAport M12B -- adapters/gba/gba_save.c
 *
 * The gpSP-facing half of GBA cartridge save persistence.
 * adapters/gba/gba_save.h carries the full derivation of the contract -- the
 * two-translation-unit split, the size policy, the EEPROM 512/8K decision, the
 * identity-latch ordering and the dirty model. READ THE HEADER FIRST. In
 * particular the note that eeprom_size and flash_bank_cnt are UNIT COUNTS and
 * that an identity computed at exit is CACHE-DEPENDENT on a demand-paged
 * cartridge.
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO
 * ---------------------------------------
 *   - does NOT open, create, read, write, flush or close a file, and names no
 *     mount point. Everything filesystem-shaped is in gba_savefile.c. There is
 *     no way for this file to persist anything by itself.
 *   - does NOT include anything from runtime/. It cannot: runtime/savedata.h
 *     pulls in runtime/core.h, whose u64 typedef collides with gpsp/common.h's.
 *     That collision is the reason this adapter is two files.
 *   - does NOT call read_backup(), write_backup(), read_eeprom() or
 *     write_eeprom(). Those four MUTATE backup_type (gba_memory.c:464-465,
 *     :1140-1141, :542): calling one to "check the save type" would MANUFACTURE
 *     the class it then persisted, and every unexercised cartridge would be
 *     written out as a 32 KB SRAM file full of 0xFF. M11 established this rule
 *     for an observer; it matters more here, because here the answer decides
 *     what lands on disk. m12b-verify asserts the absence of all four from
 *     gba_save.o.
 *   - does NOT call load_gamepak_page(). Reads touch only gamepak_buffers[0],
 *     which is resident whenever gamepak_buffer_count is non-zero, so measuring
 *     cannot itself page the cartridge and change what is measured.
 *   - does NOT reference sram_bankcount. gpsp/gba_memory.h:316 declares it and
 *     NO translation unit defines it, so a reference would break the
 *     zero-undefined-symbols gate every milestone since M1 has passed. SRAM's
 *     32 KB comes from the one place gpSP commits to a number,
 *     normalize_blank_backup_for_detected_type() (gba_memory.c:2889).
 *   - does NOT allocate. All state below is fixed-size .bss and totals well
 *     under 200 bytes. There is NO second copy of gamepak_backup[] anywhere in
 *     M12B -- see gba_save_chunk().
 *
 * THE gpsp TREE IS NOT MODIFIED. Every symbol used below is either already
 * declared in a gpSP header or is a non-static global declared extern by hand,
 * the technique upstream itself uses at gpsp/gba_memory.c:198.
 */

#include "common.h"                  /* u8/u32, gamepak_size, gamepak_backup,
                                        gamepak_buffer_count, backup_type,
                                        flash_bank_cnt, eeprom_size and the
                                        BACKUP_* / FLASH_* / EEPROM_* constants */

#include "gba_save.h"

/* ---------------------------------------------------- hand-declared state --
 *
 * Both are NON-STATIC globals in gpSP that simply have no extern in a gpSP
 * header. TRANSCRIBED CHARACTER FOR CHARACTER from adapters/gba/gba_rom.c:81-84
 * and adapters/gba/gba_m11save.c:67-68, deliberately: translation units
 * declaring the same object must agree, and copying the declaration verbatim is
 * how that is guaranteed rather than hoped for. */
extern u8      *gamepak_buffers[32];             /* gba_memory.c:369          */
extern const unsigned gamepak_buffer_blocksize;  /* gba_memory.c:378          */

/* ------------------------------------------------------------ module state --
 *
 * All .bss, all fixed size. The largest single item is the 16-byte identity
 * string. THERE IS NO SAVE BUFFER HERE: the bytes are read straight out of
 * gamepak_backup[] by gba_save_chunk() as the writer asks for them. */
static u32  gs_latched      = 0;

/* the identity, latched once before execution -- see gba_save_latch() */
static char gs_id[GBA_SAVE_ID_MAX];
static u32  gs_rom_hash     = 0;
static u32  gs_hashed_bytes = 0;   /* what the identity hash actually covered */
static u32  gs_sanitised    = 0;   /* the game code held a non-alphanumeric   */

/* the save-state baseline, latched at the same instant */
static u32  gs_lat_backup   = 0;
static u32  gs_lat_bankcnt  = 0;
static u32  gs_lat_eepsize  = 0;
static u32  gs_lat_class    = GBA_SAVE_UNKNOWN;
static u32  gs_lat_region   = 0;

/* the dirty decision: two hashes over the FULL 131,072 bytes */
static u32  gs_base_hash    = 0;
static u32  gs_exit_hash    = 0;
static u32  gs_exit_taken   = 0;

/* the coarse activity sampler */
static u32  gs_samples      = 0;
static u32  gs_active       = 0;
static u32  gs_last_sample  = 0;

/* --------------------------------------------------------------- FNV-1a 32 --
 *
 * THE SAME SEED AND PRIME AS EVERYWHERE ELSE IN THIS PROJECT --
 * adapters/gba/gba_rom.c:488, adapters/gba/gba_m11save.c:143-144,
 * apps/m12gpsp/main.c:206 and tools/upload.py all use 2166136261 / 16777619.
 * That is what lets a figure printed on the console be compared directly with
 * one computed on the PC.
 *
 * Chosen over a checksum for M11's reason, which applies harder here: a save
 * region rewrite frequently MOVES bytes without changing their sum, and a
 * persister that failed to notice would decide COMMIT NOT NEEDED for a game
 * that had just saved. */
#define GS_FNV_OFFSET 2166136261u
#define GS_FNV_PRIME    16777619u

static u32 gs_fnv(const u8 *p, u32 n)
{
  u32 h = GS_FNV_OFFSET;
  u32 i;

  for (i = 0; i < n; i++)
  {
    h ^= (u32)p[i];
    h *= GS_FNV_PRIME;
  }

  return h;
}

/* The whole backup array, which is the quantity the DIRTY DECISION uses. See
 * THE DIRTY MODEL in the header for why this is the full array and not the
 * persisted region. */
static u32 gs_full_hash(void)
{
  return gs_fnv(gamepak_backup, GBA_SAVE_BACKUP_BYTES);
}

/* ------------------------------------------------------------ classification --
 *
 * THE ONLY PLACE A CLASS IS DECIDED, and a transcription of the same resolution
 * gba_m11save.c:165-179 performs. backup_type ALONE IS NOT AN ANSWER:
 * BACKUP_FLASH covers 64 KB and 128 KB, BACKUP_EEPROM covers 512 B and 8 KB, so
 * each is resolved against its companion UNIT COUNT. */
static u32 gs_class_now(void)
{
  if (backup_type == BACKUP_SRAM)
    return GBA_SAVE_SRAM;

  if (backup_type == BACKUP_FLASH)
    return (flash_bank_cnt == FLASH_SIZE_128KB) ? GBA_SAVE_FLASH128
                                                : GBA_SAVE_FLASH64;

  if (backup_type == BACKUP_EEPROM)
    return (eeprom_size == EEPROM_8_KBYTE) ? GBA_SAVE_EEPROM8K
                                           : GBA_SAVE_EEPROM512;

  return GBA_SAVE_UNKNOWN;   /* BACKUP_UNKN, and anything gpSP cannot hold */
}

/* WHAT gpSP BELIEVES THE CHIP HOLDS. Computed from the CLASS, never by echoing
 * eeprom_size or flash_bank_cnt -- echoing either would declare 16 bytes for an
 * 8 KB EEPROM and 2 bytes for a 128 KB flash. UNKNOWN IS 0, not a guess. */
static u32 gs_declared_of(u32 cls)
{
  switch (cls)
  {
    case GBA_SAVE_SRAM:      return 32u * 1024u;    /* :2889                  */
    case GBA_SAVE_FLASH64:   return 64u * 1024u;    /* :2885                  */
    case GBA_SAVE_FLASH128:  return 128u * 1024u;   /* :2885                  */
    case GBA_SAVE_EEPROM512: return 512u;
    case GBA_SAVE_EEPROM8K:  return 8u * 1024u;
    default:                 return 0u;
  }
}

/* HOW MANY BYTES ARE PERSISTED. Identical to the declared size for every class
 * except EEPROM512, which persists the full 8192-byte EEPROM window because 512
 * is a STRICT PREFIX of it and because 512 is almost always gpSP's DEFAULT
 * rather than a measurement. The whole argument is in the header under THE SIZE
 * POLICY -- it is a format-migration decision, not a rounding.
 *
 * BOUNDED BY gamepak_backup[]'s 131,072 BYTES ON EVERY PATH. Nothing downstream
 * re-checks this, so a class that ever reported more would walk off the end of
 * the array in gba_save_chunk(). */
static u32 gs_region_of(u32 cls)
{
  u32 n;

  if (cls == GBA_SAVE_EEPROM512 || cls == GBA_SAVE_EEPROM8K)
    n = 8u * 1024u;
  else
    n = gs_declared_of(cls);

  if (n > GBA_SAVE_BACKUP_BYTES)
    n = GBA_SAVE_BACKUP_BYTES;

  return n;
}

/* ------------------------------------------------------------- confidence --
 *
 * M12B's OWN DERIVATION, AND IT IS NARROWER THAN M11's ON PURPOSE.
 *
 * M11 could reach FINAL on the strength of a ROM signature because it re-ran
 * gpSP's signature scanners itself. M12B does not link that observer and does
 * not re-implement the scan, so it has exactly two pieces of positive evidence
 * available -- both of them TRANSITIONS from the latched baseline, which is the
 * only form in which either is proof:
 *
 *   eeprom_size 1 -> 16   the 17-unit DMA at gba_memory.c:841-843 MEASURED an
 *                         8 KB chip. The value alone is not proof: a value of
 *                         16 already present at the latch could have come from
 *                         elsewhere, and :2441 re-defaults it on every reset.
 *
 *   flash_bank_cnt 1 -> 2 the 0xB0 bank-switch command at :1186 PROVED a 128 KB
 *                         chip. A 2 already present at the latch may have come
 *                         from the gba_over.h database or a FLASH1M_V string,
 *                         both of which are claims rather than measurements.
 *
 * EVERYTHING ELSE IS PROVISIONAL, and that is deliberately conservative:
 *
 *   EEPROM512  512 B is the INITIALISER (:531) and the RESET value (:2441). It
 *              is never a measurement, so it is never FINAL -- and this is
 *              exactly Tony Hawk 2's M11 hardware result.
 *   FLASH64    64 KB is only the ABSENCE of a 0xB0 SO FAR. The game may still
 *              switch banks later in a session M12B did not observe.
 *   SRAM       may be the silent UNKN -> SRAM collapse (:465, :1141) rather
 *              than a detection. M12B runs no signature scan and so cannot tell
 *              the two apart; M11 can, and says so separately.
 *
 * THE CONFIDENCE NEVER CHANGES WHAT IS WRITTEN. It is recorded in the .hdr so
 * that M12C, and a human, can see how much the class was worth at the time the
 * bytes were committed. A PROVISIONAL EEPROM512 still persists its full
 * 8192-byte region -- that is the entire point of the size policy. */
static u32 gs_conf_now(void)
{
  u32 cls = gs_class_now();

  switch (cls)
  {
    case GBA_SAVE_UNKNOWN:
      return GBA_SAVE_CONF_UNKNOWN;

    case GBA_SAVE_EEPROM8K:
      if (gs_latched && gs_lat_eepsize != EEPROM_8_KBYTE &&
          eeprom_size == EEPROM_8_KBYTE)
        return GBA_SAVE_CONF_FINAL;
      return GBA_SAVE_CONF_PROVISIONAL;

    case GBA_SAVE_FLASH128:
      if (gs_latched && gs_lat_bankcnt != FLASH_SIZE_128KB &&
          flash_bank_cnt == FLASH_SIZE_128KB)
        return GBA_SAVE_CONF_FINAL;
      return GBA_SAVE_CONF_PROVISIONAL;

    default:
      return GBA_SAVE_CONF_PROVISIONAL;
  }
}

/* --------------------------------------------------------- small string bits --
 *
 * Written out rather than taken from a libc, for gba_m11save.c's reason: this
 * translation unit sees only gpsp/common.h, and reaching for runtime/libc would
 * drag runtime/core.h and its conflicting u64 typedef into scope. They are also
 * what lets tools/gba_save_equiv.c exercise the real name construction. */

static const char gs_hexdigit[16] = {
  '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
};

/* Append s to dst at offset `at`, NUL-terminating, never writing past dstsz.
 * Returns the new offset. Truncation is silent HERE and is caught by the
 * caller, which compares the final length against what it needed. */
static u32 gs_append(char *dst, u32 dstsz, u32 at, const char *s)
{
  u32 i = 0;

  if (!dst || dstsz == 0u)
    return at;

  while (s[i] != '\0' && (at + 1u) < dstsz)
  {
    dst[at] = s[i];
    at++;
    i++;
  }

  dst[at] = '\0';
  return at;
}

/* ONE BYTE OF THE GAME CODE, MADE SAFE FOR A FILENAME.
 *
 * gpSP's own game_code is four bytes read straight out of the cartridge header
 * at 0xAC (gba_memory.c:2978). On a retail cartridge they are printable ASCII,
 * but a homebrew, a hack or an NSP-extracted ROM can carry ANYTHING there --
 * including 0x00, which would truncate the filename; '/', which would create a
 * subdirectory that does not exist; and '.', which could produce a name like
 * "..AB" inside the container.
 *
 * So every byte outside [A-Za-z0-9] becomes '_' and gs_sanitised is set so the
 * substitution is REPORTED rather than silent.
 *
 * A CODE THAT SANITISES TO "____" IS STILL ACCEPTED, and that is a deliberate
 * decision rather than an oversight. The identity's uniqueness comes from the
 * 32-bit ROM hash, not from the code -- "____1234ABCD" is exactly as unique as
 * "ATHE_1234ABCD". Refusing to persist would deny a save to a legitimate
 * homebrew cartridge purely because its header field is unprintable, which is a
 * functional regression traded for no safety at all. The report bit tells the
 * operator it happened. */
static char gs_code_byte(u8 b)
{
  if (b >= 'A' && b <= 'Z') return (char)b;
  if (b >= 'a' && b <= 'z') return (char)b;
  if (b >= '0' && b <= '9') return (char)b;

  gs_sanitised = 1;
  return '_';
}

/* ---------------------------------------------------------------- the latch --
 *
 * See gba_save.h for the two independent reasons the call site is fixed:
 * demand paging can move the ROM bytes under us, and the baseline must predate
 * every byte the cartridge writes. */
void gba_save_latch(void)
{
  const u8 *rom;
  u32 n;
  u32 i;

  /* Idempotent by refusal rather than by repetition -- latching twice would
   * silently absorb whatever ran in between into the baseline, which is exactly
   * the failure the early call site exists to prevent. */
  if (gs_latched)
    return;

  for (i = 0; i < GBA_SAVE_ID_MAX; i++)
    gs_id[i] = '\0';

  gs_lat_backup  = backup_type;
  gs_lat_bankcnt = flash_bank_cnt;
  gs_lat_eepsize = eeprom_size;

  if (gamepak_buffer_count != 0u && gamepak_size != 0u &&
      gamepak_buffers[0] != 0)
  {
    rom = gamepak_buffers[0];

    /* THE FOUR-BYTE GAME CODE AT 0xAC. Read directly rather than through
     * m4_rom_code_copy() so this file needs no adapter-to-adapter dependency
     * and so the harness can exercise it; the offset and length are
     * gba_memory.c:2978's own. 0xAC + 4 is inside the first 32 KB page, which
     * is always resident whenever buffer 0 is. */
    gs_id[0] = gs_code_byte(rom[0xAC]);
    gs_id[1] = gs_code_byte(rom[0xAD]);
    gs_id[2] = gs_code_byte(rom[0xAE]);
    gs_id[3] = gs_code_byte(rom[0xAF]);
    gs_id[4] = '_';

    /* ***** BOUNDED BY THE BLOCK SIZE, NOT BY rom_resident_bytes(). *****
     * gamepak_buffers[0] is ONE 1 MB allocation. rom_resident_bytes() reports
     * gamepak_buffer_count * 1 MB, which is 2 MB under -DROM_BUFFER_SIZE=2, and
     * indexing buffer 0 with it would read past the block. Bounding by
     * gamepak_buffer_blocksize -- gpSP's own constant for the size of one
     * buffer -- keeps every read inside buffer 0 BY CONTRACT.
     *
     * gamepak_size, not the file size: it is the 32 KB-rounded, 0xFF-padded
     * length, and the padding is deterministic, so the hash is reproducible.
     * This is what makes M12B's number differ from m4_rom_fnv1a()'s, which is
     * documented at length in the header and reported under its own name. */
    n = gamepak_size;
    if (n > (u32)gamepak_buffer_blocksize)
      n = (u32)gamepak_buffer_blocksize;

    gs_hashed_bytes = n;
    gs_rom_hash     = gs_fnv(rom, n);

    gs_id[5]  = gs_hexdigit[(gs_rom_hash >> 28) & 0xFu];
    gs_id[6]  = gs_hexdigit[(gs_rom_hash >> 24) & 0xFu];
    gs_id[7]  = gs_hexdigit[(gs_rom_hash >> 20) & 0xFu];
    gs_id[8]  = gs_hexdigit[(gs_rom_hash >> 16) & 0xFu];
    gs_id[9]  = gs_hexdigit[(gs_rom_hash >> 12) & 0xFu];
    gs_id[10] = gs_hexdigit[(gs_rom_hash >>  8) & 0xFu];
    gs_id[11] = gs_hexdigit[(gs_rom_hash >>  4) & 0xFu];
    gs_id[12] = gs_hexdigit[ gs_rom_hash        & 0xFu];
    gs_id[13] = '\0';
  }

  gs_lat_class  = gs_class_now();
  gs_lat_region = gs_region_of(gs_lat_class);

  /* THE BASELINE, OVER THE FULL ARRAY. Taken last so it reflects the state at
   * the instant the latch completes. */
  gs_base_hash   = gs_full_hash();
  gs_last_sample = gs_base_hash;

  gs_latched = 1;
}

unsigned int gba_save_latched(void)
{
  return (unsigned int)gs_latched;
}

#ifdef LUAPORT_SESSION_REUSE
/* ------------------------------------------------ THE UNLATCH (M16 ONLY) --
 *
 * ***** COMPILED ONLY FOR M16 TARGETS. ***** M13C and every other frozen
 * milestone preprocess this file with LUAPORT_SESSION_REUSE undefined, so this
 * function does not exist in gba_save.o for those builds.
 *
 * ***** WHAT THIS FIXES, AND WHY IT IS A DATA-LOSS DEFECT WITHOUT IT. *****
 * gba_save_latch() above is idempotent BY REFUSAL: `if (gs_latched) return;`.
 * That is exactly right for a process that runs one cartridge and exits, which
 * is the only thing M13C can do. The instant a second cartridge can be launched
 * inside one payload lifetime, that refusal becomes a defect: session 2 would
 * keep session 1's gs_id and gs_rom_hash, gba_save_name_sav() would still build
 * GAME A's filename, and the commit at the end of session 2 would write GAME
 * B's SRAM into GAME A's .sav and .hdr. The save would be silently destroyed.
 *
 * ***** THE FIX IS TO MAKE THE LATCH SESSION-SCOPED, NOT TO WEAKEN IT. *****
 * Nothing here relaxes the refusal. gba_save_latch() still returns immediately
 * while gs_latched is set, so latching twice WITHIN one session is still
 * impossible and the baseline still cannot absorb bytes the cartridge wrote.
 * The only new thing is an explicit, auditable end to the lifecycle -- and it
 * is the session controller, at a point where no cartridge is loaded, that says
 * when that is.
 *
 * ***** CALL ORDER IS NOT OPTIONAL: COMMIT FIRST, THEN THIS. ***** Every
 * persistence decision keys off the identity this clears. Calling it before
 * gba_savefile_commit() would leave the writer with no identity and it would
 * refuse with GBA_SAVEFILE_ENOID.
 *
 * NOTHING ABOUT THE SAVE FORMAT, THE HEADER FORMAT, THE COMMIT POLICY, THE
 * COMPATIBILITY POLICY OR THE DIRTY POLICY IS CHANGED BY THIS FUNCTION. It
 * writes no file, reads no file and does not touch gamepak_backup[]. It clears
 * this module's own .bss and nothing else. */
void gba_save_unlatch(void)
{
  u32 i;

  /* The identity. Cleared to a real empty string, not merely marked stale, so
   * that a caller which skips gba_save_latched() and copies the id anyway gets
   * "" rather than the previous cartridge's name. */
  for (i = 0; i < GBA_SAVE_ID_MAX; i++)
    gs_id[i] = '\0';

  gs_rom_hash     = 0;
  gs_hashed_bytes = 0;
  gs_sanitised    = 0;

  /* The latched save-state baseline. */
  gs_lat_backup  = 0;
  gs_lat_bankcnt = 0;
  gs_lat_eepsize = 0;
  gs_lat_class   = GBA_SAVE_UNKNOWN;
  gs_lat_region  = 0;

  /* The dirty decision. Both hashes AND the taken flag -- leaving gs_exit_taken
   * set would let the next session report an exit hash it never computed. */
  gs_base_hash  = 0;
  gs_exit_hash  = 0;
  gs_exit_taken = 0;

  /* The coarse activity sampler. */
  gs_samples     = 0;
  gs_active      = 0;
  gs_last_sample = 0;

  /* LAST, mirroring gba_save_latch() setting it last: until this store the
   * module still reports "latched", so a fault midway through leaves a state
   * that reads as latched-but-cleared rather than unlatched-but-stale. */
  gs_latched = 0;
}
#endif /* LUAPORT_SESSION_REUSE */

/* ---------------------------------------------------------------- answers -- */

unsigned int gba_save_class(void)
{
  return (unsigned int)gs_class_now();
}

unsigned int gba_save_latch_class(void)
{
  return (unsigned int)gs_lat_class;
}

unsigned int gba_save_declared_bytes(void)
{
  return (unsigned int)gs_declared_of(gs_class_now());
}

unsigned int gba_save_region_bytes(void)
{
  return (unsigned int)gs_region_of(gs_class_now());
}

unsigned int gba_save_confidence(void)
{
  return (unsigned int)gs_conf_now();
}

/* --------------------------------------------------------------- identity -- */

void gba_save_id_copy(char *dst, unsigned int dstsz)
{
  u32 i;

  if (!dst || dstsz == 0u)
    return;

  for (i = 0; i + 1u < (u32)dstsz && gs_id[i] != '\0'; i++)
    dst[i] = gs_id[i];

  dst[i] = '\0';
}

unsigned int gba_save_rom_hash(void)
{
  return (unsigned int)gs_rom_hash;
}

/* "/savedata0/gba/" + <ID> + <ext>. Returns the length, or 0 on refusal.
 *
 * REFUSES RATHER THAN TRUNCATES. A truncated name is a DIFFERENT FILE, and
 * writing a save to it would look like success while silently colliding with
 * every other cartridge whose name truncated to the same prefix. The length is
 * checked against what was actually produced, so the check cannot be defeated
 * by gs_append()'s silent clamp. */
static u32 gs_name(char *dst, u32 dstsz, const char *ext)
{
  u32 at;

  if (!dst || dstsz == 0u)
    return 0u;

  dst[0] = '\0';

  if (!gs_latched || gs_id[0] == '\0')
    return 0u;

  at = gs_append(dst, dstsz, 0u, GBA_SAVE_DIR);
  at = gs_append(dst, dstsz, at, "/");
  at = gs_append(dst, dstsz, at, gs_id);
  at = gs_append(dst, dstsz, at, ext);

  /* The exact length the pieces should have produced. Anything shorter means
   * gs_append() ran out of buffer partway. */
  {
    u32 want = 0u;
    const char *p;

    for (p = GBA_SAVE_DIR; *p; p++) want++;
    want++;                                   /* the '/'                     */
    for (p = gs_id;        *p; p++) want++;
    for (p = ext;          *p; p++) want++;

    if (at != want)
    {
      dst[0] = '\0';
      return 0u;
    }
  }

  return at;
}

unsigned int gba_save_name_sav(char *dst, unsigned int dstsz)
{
  return (unsigned int)gs_name(dst, (u32)dstsz, GBA_SAVE_EXT_SAV);
}

unsigned int gba_save_name_hdr(char *dst, unsigned int dstsz)
{
  return (unsigned int)gs_name(dst, (u32)dstsz, GBA_SAVE_EXT_HDR);
}

/* ------------------------------------------------------------ dirty model -- */

void gba_save_finish(void)
{
  if (!gs_latched)
    return;

  gs_exit_hash  = gs_full_hash();
  gs_exit_taken = 1;
}

unsigned int gba_save_is_dirty(void)
{
  /* REFUSING TO WRITE IS THE SAFE DIRECTION when nothing was ever latched:
   * without a baseline there is no evidence the game saved, and a spurious
   * commit would overwrite a good file with whatever happens to be in
   * gamepak_backup[]. */
  if (!gs_latched)
    return 0u;

  /* Calling finish() here rather than requiring the fixture to is deliberate.
   * A forgotten call would otherwise make gs_exit_hash zero, which compares
   * unequal to virtually any baseline and would report DIRTY for a run in
   * which nothing happened -- or, worse under a different ordering, CLEAN for
   * one in which the game saved. Making the query self-sufficient removes the
   * ordering trap entirely. */
  if (!gs_exit_taken)
    gba_save_finish();

  return (gs_exit_hash != gs_base_hash) ? 1u : 0u;
}

unsigned int gba_save_baseline_hash(void)
{
  return (unsigned int)gs_base_hash;
}

unsigned int gba_save_exit_hash(void)
{
  return (unsigned int)gs_exit_hash;
}

unsigned int gba_save_region_hash(void)
{
  u32 n = gs_region_of(gs_class_now());

  if (n == 0u)
    return 0u;

  return (unsigned int)gs_fnv(gamepak_backup, n);
}

unsigned int gba_save_nonff(void)
{
  u32 n = gs_region_of(gs_class_now());
  u32 i;
  u32 c = 0;

  for (i = 0; i < n; i++)
    if (gamepak_backup[i] != 0xFF)
      c++;

  return (unsigned int)c;
}

/* ---- the coarse activity sampler ---------------------------------------
 *
 * DIAGNOSTIC ONLY. It never participates in the commit decision -- see the
 * header for why it is called ACTIVITY SAMPLES and not WRITE FRAMES. */
void gba_save_sample(void)
{
  u32 h;

  if (!gs_latched)
    return;

  gs_samples++;
  h = gs_full_hash();

  if (h != gs_last_sample)
  {
    gs_active++;
    gs_last_sample = h;
  }
}

unsigned int gba_save_samples(void)
{
  return (unsigned int)gs_samples;
}

unsigned int gba_save_active_samples(void)
{
  return (unsigned int)gs_active;
}

/* ----------------------------------------------------------- the byte tap -- */

unsigned int gba_save_chunk(unsigned char *dst, unsigned int off,
                            unsigned int n)
{
  u32 region = gs_region_of(gs_class_now());
  u32 i;

  if (!dst || n == 0u || region == 0u)
    return 0u;

  if (off >= region)
    return 0u;

  /* Clamp to the region, and the region is itself already clamped to
   * GBA_SAVE_BACKUP_BYTES by gs_region_of(). Both bounds are needed: the first
   * stops a caller reading past the SAVE, the second stops any class ever
   * reading past the ARRAY. */
  if (n > region - off)
    n = region - off;

  for (i = 0; i < n; i++)
    dst[i] = gamepak_backup[off + i];

  return (unsigned int)n;
}

/* ------------------------------------------------------------- the header --
 *
 * EXPLICIT LITTLE-ENDIAN BYTE STORES. No struct, no cast through a wider
 * pointer, no memcpy of a native integer: the on-disk layout is then a property
 * of this function alone and cannot be changed by a padding rule, an alignment
 * choice or a different target's endianness. tools/gba_save_equiv.c compares
 * the produced buffer against a hand-written expected array byte for byte. */
static void gs_put16(u8 *p, u32 v)
{
  p[0] = (u8)(v & 0xFFu);
  p[1] = (u8)((v >> 8) & 0xFFu);
}

static void gs_put32(u8 *p, u32 v)
{
  p[0] = (u8)(v & 0xFFu);
  p[1] = (u8)((v >> 8) & 0xFFu);
  p[2] = (u8)((v >> 16) & 0xFFu);
  p[3] = (u8)((v >> 24) & 0xFFu);
}

unsigned int gba_save_header_build(unsigned char *dst, unsigned int dstsz,
                                   unsigned int payload_fnv)
{
  u32 cls = gs_class_now();
  u32 region = gs_region_of(cls);
  u32 i;

  if (!dst || dstsz < GBA_SAVE_HDR_BYTES)
    return 0u;

  /* NO HEADER FOR A CLASS THAT IS NOT PERSISTED. Producing one would create a
   * commit marker for a .sav that must never be written, and M12C's whole
   * validity rule is that a .hdr means the bytes beside it are complete. */
  if (cls == GBA_SAVE_UNKNOWN || region == 0u)
    return 0u;

  for (i = 0; i < GBA_SAVE_HDR_BYTES; i++)
    dst[i] = 0;

  dst[GBA_SAVE_HDR_O_MAGIC + 0] = (u8)GBA_SAVE_MAGIC_0;
  dst[GBA_SAVE_HDR_O_MAGIC + 1] = (u8)GBA_SAVE_MAGIC_1;
  dst[GBA_SAVE_HDR_O_MAGIC + 2] = (u8)GBA_SAVE_MAGIC_2;
  dst[GBA_SAVE_HDR_O_MAGIC + 3] = (u8)GBA_SAVE_MAGIC_3;

  gs_put16(&dst[GBA_SAVE_HDR_O_VERSION],  GBA_SAVE_HDR_VERSION);
  gs_put16(&dst[GBA_SAVE_HDR_O_CLASS],    cls);
  gs_put32(&dst[GBA_SAVE_HDR_O_REGION],   region);
  gs_put32(&dst[GBA_SAVE_HDR_O_DECLARED], gs_declared_of(cls));
  gs_put32(&dst[GBA_SAVE_HDR_O_ROMFNV],   gs_rom_hash);
  gs_put32(&dst[GBA_SAVE_HDR_O_PAYFNV],   (u32)payload_fnv);
  gs_put16(&dst[GBA_SAVE_HDR_O_CONF],     gs_conf_now());

  /* bytes 26..31 stay zero -- see the reserved note in the header */

  return GBA_SAVE_HDR_BYTES;
}

/* ------------------------------------------------------------- the report -- */

unsigned int gba_save_report(void)
{
  u32 r = 0;
  u32 cls = gs_class_now();
  u32 region = gs_region_of(cls);

  if (!gs_latched)
    r |= GBA_SAVE_R_NOLATCH;

  if (gamepak_size == 0u || gamepak_buffer_count == 0u ||
      gamepak_buffers[0] == 0)
    r |= GBA_SAVE_R_NOROM;

  if (cls == GBA_SAVE_UNKNOWN)
    r |= GBA_SAVE_R_UNKNOWN;

  if (gs_sanitised)
    r |= GBA_SAVE_R_SANITISED;

  /* EEPROM SIZED 512 B BY DEFAULT. gba_memory.c:531 initialises eeprom_size to
   * EEPROM_512_BYTE and :2441 restores it on every reset; only the 17-unit DMA
   * at :841-843 ever measures 8 KB. So a 512 result is an assumption, and the
   * operator is told so. It does NOT change what is written -- 8192 bytes are
   * persisted either way. */
  if (cls == GBA_SAVE_EEPROM512)
    r |= GBA_SAVE_R_EEPDEFAULT;

  if (gs_latched && cls != gs_lat_class)
    r |= GBA_SAVE_R_CLASSMOVED;

  if (gs_latched && region > gs_lat_region)
    r |= GBA_SAVE_R_GREW;

  /* NOTHING HAS EVER BEEN SAVED -- the region is still the 0xFF idle state that
   * normalize_blank_backup_for_detected_type() writes at :2907. */
  if (region != 0u && gba_save_nonff() == 0u)
    r |= GBA_SAVE_R_BLANK;

  if (gs_latched && gs_exit_taken && gs_exit_hash == gs_base_hash)
    r |= GBA_SAVE_R_CLEAN;

  return (unsigned int)r;
}

/* ---------------------------------------------------------------- the log -- */

unsigned int gba_save_read(unsigned int sel)
{
  switch (sel)
  {
    case GBA_SAVE_RD_CLASS:        return gba_save_class();
    case GBA_SAVE_RD_DECLARED:     return gba_save_declared_bytes();
    case GBA_SAVE_RD_REGION:       return gba_save_region_bytes();
    case GBA_SAVE_RD_CONF:         return gba_save_confidence();

    case GBA_SAVE_RD_ROMHASH:      return (unsigned int)gs_rom_hash;
    case GBA_SAVE_RD_BASEHASH:     return (unsigned int)gs_base_hash;
    case GBA_SAVE_RD_EXITHASH:     return (unsigned int)gs_exit_hash;
    case GBA_SAVE_RD_REGIONHASH:   return gba_save_region_hash();
    case GBA_SAVE_RD_NONFF:        return gba_save_nonff();
    case GBA_SAVE_RD_DIRTY:        return gba_save_is_dirty();

    case GBA_SAVE_RD_LATCH_CLASS:  return (unsigned int)gs_lat_class;
    case GBA_SAVE_RD_LATCH_DECL:   return (unsigned int)gs_declared_of(gs_lat_class);
    case GBA_SAVE_RD_LATCH_REGION: return (unsigned int)gs_lat_region;

    /* RAW UNIT COUNTS, LABELLED AS SUCH AT EVERY PRINT SITE. flash_bank_cnt is
     * 1 or 2 and eeprom_size is 1 or 16 -- neither is a byte count. */
    case GBA_SAVE_RD_BACKUP_TYPE:  return (unsigned int)backup_type;
    case GBA_SAVE_RD_BANKCNT:      return (unsigned int)flash_bank_cnt;
    case GBA_SAVE_RD_EEPSIZE:      return (unsigned int)eeprom_size;

    case GBA_SAVE_RD_SAMPLES:      return (unsigned int)gs_samples;
    case GBA_SAVE_RD_ACTIVE:       return (unsigned int)gs_active;
    case GBA_SAVE_RD_REPORT:       return gba_save_report();

    case GBA_SAVE_RD_ROMSIZE:      return (unsigned int)gamepak_size;
    case GBA_SAVE_RD_HASHED:       return (unsigned int)gs_hashed_bytes;

    default:                       return 0u;
  }
}
