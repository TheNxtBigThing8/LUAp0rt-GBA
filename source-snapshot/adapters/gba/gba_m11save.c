/* LUAport M11 -- adapters/gba/gba_m11save.c
 *
 * The gpSP-facing half of M11's PASSIVE cartridge save-memory observer.
 * adapters/gba/gba_m11save.h carries the full derivation of the contract and
 * the three things that are NOT observable; this file is the mechanism.
 * READ THE HEADER FIRST -- in particular the note that the size constants are
 * UNIT COUNTS and that BACKUP_SRAM without an SRAM signature is a COLLAPSE and
 * not a detection.
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO
 * ---------------------------------------
 *   - does NOT call read_backup(), write_backup(), read_eeprom() or
 *     write_eeprom(). Those four MUTATE backup_type (gba_memory.c:464-465,
 *     :1140-1141, :542) and a probe that called them would CAUSE the result it
 *     then reported. m11-verify asserts gba_m11save.o carries no relocation to
 *     any of the four.
 *   - does NOT open, create, read or write a save file. There is no path
 *     string and no filename rule anywhere in this milestone; persistence is
 *     M12's and M11 does not prepare a byte of it.
 *   - does NOT reference sram_bankcount. gpsp/gba_memory.h:316 declares it and
 *     NO translation unit defines it, so a reference would break the
 *     zero-undefined-symbols gate. SRAM's 32 KB comes from the one place gpSP
 *     commits to a number, normalize_blank_backup_for_detected_type()
 *     (gba_memory.c:2889).
 *   - does NOT allocate and does NOT touch runtime/. See the type rule in the
 *     header: only `unsigned int` crosses the boundary to the fixture.
 *   - does NOT call load_gamepak_page(). The sweep walks ONLY buffers that
 *     gamepak_buffer_count already reports resident, so observing cannot
 *     itself page the cartridge in and change what is observed.
 *
 * THE gpsp TREE IS NOT MODIFIED. Every symbol below is either already declared
 * in a gpSP header or is a non-static global declared extern by hand, which is
 * the technique upstream itself uses at gpsp/gba_memory.c:198 and that
 * adapters/gba/gba_probe.c:75-85 established for M2.
 */

#include "common.h"                  /* u8/u16/u32/bool, gamepak_size,
                                        gamepak_buffer_count, gamepak_backup,
                                        gamepak_header_nonstandard,
                                        gamepak_must_swap, backup_type,
                                        flash_bank_cnt, eeprom_size, and the
                                        BACKUP_*/FLASH_*/EEPROM_* constants   */

#include "gba_m11save.h"

/* ---------------------------------------------------- hand-declared state --
 *
 * Each of these is a NON-STATIC global in gpSP that simply has no extern in a
 * gpSP header. Everything M11 needs that IS in a header (backup_type,
 * flash_bank_cnt, eeprom_size, gamepak_backup, gamepak_size,
 * gamepak_buffer_count, gamepak_header_nonstandard, gamepak_must_swap) is used
 * through that header and is not restated here.
 *
 * The last two are transcribed CHARACTER FOR CHARACTER from
 * adapters/gba/gba_rom.c:81-84 and adapters/gba/gba_m10map.c:41-43, which is
 * deliberate: translation units declaring the same object must agree, and
 * copying the declaration verbatim is how that is guaranteed. */
extern u32      backup_type_reset;           /* gba_memory.c:415             */
extern u32      flash_mode;                  /* gba_memory.c:416             */
extern u32      flash_command_position;      /* gba_memory.c:417             */
extern u32      flash_bank_num;              /* gba_memory.c:418             */
extern u32      flash_device_id;             /* gba_memory.c:421             */
extern u32      eeprom_mode;                 /* gba_memory.c:532             */
extern u32      eeprom_address;              /* gba_memory.c:533             */
extern u32      eeprom_counter;              /* gba_memory.c:534             */
extern bool     is_known_game;               /* gba_memory.c:289             */
extern u8      *gamepak_buffers[32];         /* gba_memory.c:369             */
extern const unsigned gamepak_buffer_blocksize;  /* gba_memory.c:378         */

/* is_known_game and gamepak_header_nonstandard are `bool`, NOT u32. Declaring
 * either as unsigned int would read three bytes that do not belong to it. They
 * are used through <stdbool.h>'s bool exactly as gpSP defines them, and only
 * their truth value ever crosses to the fixture. */

/* ------------------------------------------------------------ module state --
 *
 * All of it is .bss and all of it is fixed-size. The dirty map is 128 u32
 * hashes plus a 128-bit bitmap -- 528 bytes -- as against a 128 KB shadow copy
 * of gamepak_backup[], which would answer exactly the same question at a fifth
 * of the milestone's remaining .bss headroom. */
static u32 m11_latched      = 0;   /* m11save_latch_static() has run          */

/* the baseline, captured at latch time and never rewritten */
static u32 m11_st_type      = M11_SAVE_UNKNOWN;
static u32 m11_st_source    = M11_SRC_NONE;
static u32 m11_st_bytes     = 0;
static u32 m11_st_backup    = 0;   /* backup_type            at latch         */
static u32 m11_st_bankcnt   = 0;   /* flash_bank_cnt         at latch         */
static u32 m11_st_eepsize   = 0;   /* eeprom_size            at latch         */
static u32 m11_st_devid     = 0;   /* flash_device_id        at latch         */

/* the signature evidence, computed once at latch time */
static u32 m11_sig_hdr      = 0;   /* M11_SIG_* bits, first 1 MB              */
static u32 m11_sig_swp      = 0;   /* M11_SIG_* bits, resident sweep only     */
static u32 m11_swept        = 0;   /* bytes the sweep actually examined       */
static u32 m11_pokemon      = 0;   /* rom_is_pokemon_family()                 */
static u32 m11_eep_guessed  = 0;   /* DERIVED -- see the header               */
static u32 m11_hack         = 0;   /* the tier-4 override would have fired    */

/* the sticky evidence latch and the running observations */
static u32 m11_ev           = 0;
static u32 m11_hash         = 0;
static u32 m11_hash_first   = 0;
static u32 m11_nonff        = 0;
static u32 m11_snapshots    = 0;
static u32 m11_write_frames = 0;
static u32 m11_dirty_blocks = 0;
static u32 m11_first_block  = M11_NOBLOCK;
static u32 m11_probe_last   = 0;
static u32 m11_report_last  = 0;

static u32 m11_blk_hash[M11_BLOCKS];
static u32 m11_blk_seen[(M11_BLOCKS + 31) / 32];   /* block has a baseline    */
static u32 m11_blk_dirty[(M11_BLOCKS + 31) / 32];  /* block has ever changed  */

/* The four signature bits, in gpSP's own order (gba_memory.c:2815-2820). Kept
 * private: the packed masks reach the fixture through M11_RD_SIG_HEADER and
 * M11_RD_SIG_SWEEP, and are re-expanded into M11_EV_* bits by the latch. */
#define M11_SIG_EEPROM   (1u << 0)
#define M11_SIG_SRAM     (1u << 1)
#define M11_SIG_FLASH1M  (1u << 2)
#define M11_SIG_FLASH5   (1u << 3)
#define M11_SIG_ALL      (M11_SIG_EEPROM | M11_SIG_SRAM | \
                          M11_SIG_FLASH1M | M11_SIG_FLASH5)

/* ------------------------------------------------------------- bitmap util -- */

static void m11_bm_set(u32 *bm, u32 i)
{
  bm[i >> 5] |= (1u << (i & 31));
}

static u32 m11_bm_test(const u32 *bm, u32 i)
{
  return (bm[i >> 5] >> (i & 31)) & 1u;
}

/* --------------------------------------------------------------- FNV-1a 32 --
 *
 * Chosen over a checksum because M11 needs to notice a save region whose bytes
 * MOVED without its total changing -- a sum cannot see a transposition and a
 * save file rewrite is exactly that. */
#define M11_FNV_OFFSET 2166136261u
#define M11_FNV_PRIME    16777619u

static u32 m11_fnv(const u8 *p, u32 n)
{
  u32 h = M11_FNV_OFFSET;
  u32 i;

  for (i = 0; i < n; i++)
  {
    h ^= (u32)p[i];
    h *= M11_FNV_PRIME;
  }

  return h;
}

/* ------------------------------------------------------------ classification --
 *
 * THE ONLY PLACE A (type, size) PAIR IS DECIDED. backup_type ALONE IS NOT AN
 * ANSWER: BACKUP_FLASH covers 64 KB and 128 KB and BACKUP_EEPROM covers 512 B
 * and 8 KB, so each is resolved against its companion UNIT COUNT. */
static u32 m11_classify(void)
{
  if (backup_type == BACKUP_SRAM)
    return M11_SAVE_SRAM;

  if (backup_type == BACKUP_FLASH)
    return (flash_bank_cnt == FLASH_SIZE_128KB) ? M11_SAVE_FLASH128
                                                : M11_SAVE_FLASH64;

  if (backup_type == BACKUP_EEPROM)
    return (eeprom_size == EEPROM_8_KBYTE) ? M11_SAVE_EEPROM8K
                                           : M11_SAVE_EEPROM512;

  return M11_SAVE_UNKNOWN;   /* BACKUP_UNKN, and anything gpSP cannot hold */
}

/* BYTES FROM THE CLASSIFIED TYPE, NEVER FROM eeprom_size OR flash_bank_cnt.
 * Echoing either would report "16 bytes" for an 8 KB EEPROM. SRAM's 32 KB is
 * gba_memory.c:2889's number, NOT sram_bankcount, which is never defined. */
static u32 m11_bytes_of(u32 type)
{
  switch (type)
  {
    case M11_SAVE_SRAM:      return 32u * 1024u;   /* :2889                  */
    case M11_SAVE_FLASH64:   return 64u * 1024u;   /* :2885                  */
    case M11_SAVE_FLASH128:  return 128u * 1024u;  /* :2885                  */
    case M11_SAVE_EEPROM512: return 512u;
    case M11_SAVE_EEPROM8K:  return 8u * 1024u;
    default:                 return 0u;
  }
}

/* THE REGION THE HASH ACTUALLY COVERS.
 *
 * For a classified cartridge this is the save size. For an UNKNOWN one it is
 * 32 KB -- the SRAM window -- because that is precisely the region gpSP would
 * use the instant BACKUP_UNKN collapses to BACKUP_SRAM at :465/:1141. Watching
 * it BEFORE the collapse is what lets M11 report the collapse as a collapse
 * rather than as a discovery. Never exceeds gamepak_backup[]'s 128 KB. */
static u32 m11_active_bytes(void)
{
  u32 n = m11_bytes_of(m11_classify());

  if (n == 0)
    n = 32u * 1024u;

  if (n > M11_BACKUP_BYTES)
    n = M11_BACKUP_BYTES;

  return n;
}

/* ------------------------------------------------------- signature scanning --
 *
 * A TRANSCRIPTION OF gpSP's OWN TWO SCANNERS, and it has to stay one.
 * m11save_source() and the DERIVED backup_type_eeprom_guessed are only
 * trustworthy while these agree byte for byte with rom_has_signature()
 * (gba_memory.c:2798-2812) and rom_scan_signatures_in_memory() (:2823-2860). */

static u32 m11_strlen(const char *s)
{
  u32 n = 0;
  while (s[n] != '\0')
    n++;
  return n;
}

static bool m11_match(const u8 *p, const char *s, u32 n)
{
  u32 i;
  for (i = 0; i < n; i++)
  {
    if (p[i] != (u8)s[i])
      return false;
  }
  return true;
}

/* rom_has_signature(), gba_memory.c:2798. BYTE-GRANULAR and unanchored: it
 * slides one byte at a time, NOT four. The sweep below deliberately does not. */
static bool m11_sig_at(const u8 *rom, u32 rom_size, const char *sig)
{
  u32 i;
  u32 sig_len = m11_strlen(sig);

  if (rom_size < sig_len)
    return false;

  for (i = 0; i + sig_len <= rom_size; i++)
  {
    if (m11_match(&rom[i], sig, sig_len))
      return true;
  }

  return false;
}

/* THE HEADER SCAN, tier 3's first half. load_gamepak() passes
 * min(gamepak_size, 1 MB) over gamepak_buffers[0] (:3011-3013) -- buffer 0
 * always holds the first 1 MB chunk, so this needs no paging and cannot fault. */
static u32 m11_scan_header(void)
{
  const u8 *rom;
  u32 scan_size;
  u32 found = 0;

  if (gamepak_buffer_count == 0 || gamepak_size == 0)
    return 0;

  rom = gamepak_buffers[0];
  if (rom == 0)
    return 0;

  scan_size = (gamepak_size < (1024u * 1024u)) ? gamepak_size : (1024u * 1024u);

  if (m11_sig_at(rom, scan_size, "EEPROM_V"))   found |= M11_SIG_EEPROM;
  if (m11_sig_at(rom, scan_size, "SRAM_V"))     found |= M11_SIG_SRAM;
  if (m11_sig_at(rom, scan_size, "FLASH1M_V"))  found |= M11_SIG_FLASH1M;
  if (m11_sig_at(rom, scan_size, "FLASH512_V") ||
      m11_sig_at(rom, scan_size, "FLASH_V"))    found |= M11_SIG_FLASH5;

  return found;
}

/* THE RESIDENT SWEEP, rom_scan_signatures_in_memory(), gba_memory.c:2823.
 *
 * FOUR DETAILS ARE LOAD-BEARING AND ALL FOUR ARE COPIED, NOT IMPROVED:
 *   - the stride is 4, not 1, so a signature at a non-multiple-of-4 offset is
 *     INVISIBLE to this scan even though the header scan would find it;
 *   - the bound is `i < chunk_size - 10`, so the last ten bytes of every chunk
 *     and any signature STRADDLING two buffers are missed;
 *   - the first-byte test and the else-if chain mean at most one signature
 *     class is tested per offset;
 *   - it stops at gamepak_buffer_count, so on a demand-paged cartridge it sees
 *     only what happens to be resident.
 * "Fixing" any of them here would make M11 report a signature gpSP never saw,
 * and the derived guess flag would then disagree with the emulator.
 *
 * m11_swept records the bytes ACTUALLY examined so the fixture can print a
 * scanned fraction instead of implying a whole-cartridge scan. */
static u32 m11_scan_sweep(void)
{
  u32 found = 0;
  u32 size_left = gamepak_size;
  u32 buf_idx = 0;

  m11_swept = 0;

  while (size_left > 0 && buf_idx < gamepak_buffer_count && buf_idx < 32)
  {
    u32 chunk_size = (size_left > gamepak_buffer_blocksize)
                       ? gamepak_buffer_blocksize : size_left;
    const u8 *chunk = gamepak_buffers[buf_idx];
    u32 i;

    if (chunk == 0)
      break;

    /* chunk_size - 10 underflows on a chunk of ten bytes or fewer; gpSP is
     * saved from it by a 1 MB blocksize, and M11 is explicit rather than
     * lucky. */
    if (chunk_size > 10)
    {
      for (i = 0; i < chunk_size - 10; i += 4)
      {
        if (chunk[i] == 'E' && !(found & M11_SIG_EEPROM) &&
            m11_match(&chunk[i], "EEPROM_V", 8))
          found |= M11_SIG_EEPROM;
        else if (chunk[i] == 'S' && !(found & M11_SIG_SRAM) &&
                 m11_match(&chunk[i], "SRAM_V", 6))
          found |= M11_SIG_SRAM;
        else if (chunk[i] == 'F' && !(found & M11_SIG_FLASH1M) &&
                 m11_match(&chunk[i], "FLASH1M_V", 9))
          found |= M11_SIG_FLASH1M;
        else if (chunk[i] == 'F' && !(found & M11_SIG_FLASH5))
        {
          if (m11_match(&chunk[i], "FLASH512_V", 10) ||
              m11_match(&chunk[i], "FLASH_V", 7))
            found |= M11_SIG_FLASH5;
        }
      }
    }

    m11_swept += chunk_size;

    if ((found & M11_SIG_ALL) == M11_SIG_ALL)
      break;

    size_left -= chunk_size;
    buf_idx++;
  }

  return found;
}

/* rom_is_pokemon_family(), gba_memory.c:2862-2876, transcribed. Reads only the
 * 12-byte title at 0xA0 and the 4-byte game code at 0xAC, both inside the
 * always-resident first buffer. */
static u32 m11_is_pokemon(const u8 *rom)
{
  if (m11_match(&rom[0xA0], "POKEMON", 7))
    return 1;

  if (m11_match(&rom[0xAC], "AXV", 3) ||   /* Ruby      */
      m11_match(&rom[0xAC], "AXP", 3) ||   /* Sapphire  */
      m11_match(&rom[0xAC], "BPE", 3) ||   /* Emerald   */
      m11_match(&rom[0xAC], "BPR", 3) ||   /* FireRed   */
      m11_match(&rom[0xAC], "BPG", 3))     /* LeafGreen */
    return 1;

  return 0;
}

/* title_altered, gba_memory.c:3026-3038. Only meaningful for a Pokemon-family
 * cartridge; gpSP does not compute it otherwise. */
static u32 m11_title_altered(const u8 *rom)
{
  const u8 *t = &rom[0xA0];

  if (!m11_is_pokemon(rom))
    return 0;

  if (!m11_match(t, "POKEMON FIRE", 12) &&
      !m11_match(t, "POKEMON LEAF", 12) &&
      !m11_match(t, "POKEMON EMER", 12) &&
      !m11_match(t, "POKEMON RUBY", 12) &&
      !m11_match(t, "POKEMON SAPP", 12))
    return 1;

  return 0;
}

/* ------------------------------------------------------- source derivation --
 *
 * REPLAYS gpSP's FOUR TIERS AGAINST gpSP's OWN INPUTS. Runs once, at latch
 * time, against a save state that no bus activity has touched yet.
 *
 * THE TIERS ARE EVALUATED LAST-WRITER-FIRST because that is how they compose:
 * tier 4 overrides tiers 2 and 3, and tier 2 suppresses tier 3 entirely (the
 * signature scan at :3010 runs ONLY while backup_type_reset is still UNKN).
 *
 * WHAT CANNOT BE SEPARATED IS REPORTED AS INSEPARABLE. gbaover[] is
 * `static const` inside gpsp/gba_over.h and is not linkable, so when a known
 * game holds an EEPROM verdict that a signature would equally explain, both
 * tiers are live and M11 returns M11_SRC_DB_OR_SIG rather than guessing. */
static u32 m11_derive_source(u32 type)
{
  u32 sig = m11_sig_hdr | m11_sig_swp;

  if (type == M11_SAVE_UNKNOWN)
    return M11_SRC_NONE;

  /* TIER 4 -- the is_hack FLASH-128K override, :3040-3052. Last writer, so it
   * is tested first. It forces exactly FLASH 128 KB with the Sanyo id; if the
   * committed state is not that, the override did not fire. */
  if (m11_hack && type == M11_SAVE_FLASH128 &&
      m11_st_devid == FLASH_DEVICE_SANYO_128KB)
    return M11_SRC_HACK;

  /* TIER 3 -- the no-signature Pokemon short-circuit, :2918-2925. Requires the
   * complete absence of all four signatures. */
  if (type == M11_SAVE_FLASH128 && m11_pokemon && sig == 0)
    return M11_SRC_POKEMON;

  if (type == M11_SAVE_EEPROM512 || type == M11_SAVE_EEPROM8K)
  {
    /* TIER 2 is the ONLY tier that can set EEPROM without a signature, and the
     * only one that sets is_known_game. */
    if (is_known_game && (sig & M11_SIG_EEPROM))
      return M11_SRC_DB_OR_SIG;          /* genuinely inseparable */
    if (is_known_game)
      return M11_SRC_DB;
    if (m11_sig_hdr & M11_SIG_EEPROM)
      return M11_SRC_SIG_HEADER;
    if (m11_sig_swp & M11_SIG_EEPROM)
      return M11_SRC_SIG_SWEEP;
    return M11_SRC_NONE;
  }

  if (type == M11_SAVE_SRAM)
  {
    if (m11_sig_hdr & M11_SIG_SRAM)
      return M11_SRC_SIG_HEADER;
    if (m11_sig_swp & M11_SIG_SRAM)
      return M11_SRC_SIG_SWEEP;
    /* SRAM WITH NO SRAM SIGNATURE IS THE SILENT COLLAPSE, not a detection.
     * At latch time nothing has run, so this can only be a save file that was
     * already sized -- either way it is not positive evidence. */
    return M11_SRC_FALLBACK;
  }

  if (type == M11_SAVE_FLASH128)
  {
    if (m11_sig_hdr & M11_SIG_FLASH1M)
      return M11_SRC_SIG_HEADER;
    if (m11_sig_swp & M11_SIG_FLASH1M)
      return M11_SRC_SIG_SWEEP;
    /* FLAGS_FLASH_128KB sets flash_bank_cnt and the Sanyo id but NEVER the
     * type (:1725-1728), so tier 2 alone cannot land here. */
    return M11_SRC_NONE;
  }

  if (type == M11_SAVE_FLASH64)
  {
    if (m11_sig_hdr & M11_SIG_FLASH5)
      return M11_SRC_SIG_HEADER;
    if (m11_sig_swp & M11_SIG_FLASH5)
      return M11_SRC_SIG_SWEEP;
    return M11_SRC_NONE;
  }

  return M11_SRC_NONE;
}

/* ---------------------------------------------------------------- the latch -- */

void m11save_latch_static(void)
{
  const u8 *rom;
  u32 i;

  /* Idempotent by refusal rather than by repetition: latching twice would
   * silently absorb whatever ran in between into the "static" baseline. */
  if (m11_latched)
    return;

  for (i = 0; i < M11_BLOCKS; i++)
    m11_blk_hash[i] = 0;
  for (i = 0; i < (M11_BLOCKS + 31) / 32; i++)
  {
    m11_blk_seen[i]  = 0;
    m11_blk_dirty[i] = 0;
  }

  m11_st_backup  = backup_type;
  m11_st_bankcnt = flash_bank_cnt;
  m11_st_eepsize = eeprom_size;
  m11_st_devid   = flash_device_id;

  if (gamepak_buffer_count != 0 && gamepak_size != 0 && gamepak_buffers[0] != 0)
  {
    rom = gamepak_buffers[0];

    m11_pokemon = m11_is_pokemon(rom);
    m11_sig_hdr = m11_scan_header();

    /* gpSP sweeps ONLY when the header scan found nothing at all
     * (gba_memory.c:2927-2928). Sweeping unconditionally would report sweep
     * evidence the emulator never collected. */
    if (m11_sig_hdr == 0)
      m11_sig_swp = m11_scan_sweep();

    /* DERIVED backup_type_eeprom_guessed, gba_memory.c:2934. The flag is
     * `static` and cannot be read; this is the same expression over the same
     * inputs, and it is reported as DERIVED and never as read. */
    if (!(m11_sig_hdr & M11_SIG_EEPROM) && (m11_sig_swp & M11_SIG_EEPROM))
      m11_eep_guessed = 1;

    /* is_hack, gba_memory.c:3053. is_pokemon_engine folds in the 128 KB flash
     * case (:3023), which is why a NON-Pokemon 128 KB cartridge can also take
     * the tier-4 path. */
    {
      u32 is_128k = (m11_st_backup == BACKUP_FLASH &&
                     m11_st_bankcnt == FLASH_SIZE_128KB) ? 1u : 0u;
      u32 engine  = (m11_pokemon || is_128k) ? 1u : 0u;
      u32 expanded = (gamepak_size > 16777216u) ? 1u : 0u;
      u32 hack = (gamepak_header_nonstandard || !is_known_game ||
                  m11_title_altered(rom) || (expanded && engine)) ? 1u : 0u;

      m11_hack = (hack && engine) ? 1u : 0u;
    }
  }

  m11_st_type   = m11_classify();
  m11_st_bytes  = m11_bytes_of(m11_st_type);
  m11_st_source = m11_derive_source(m11_st_type);

  /* Fold the static findings into the sticky latch so the fixture reads one
   * mask rather than reconstructing this. */
  if (m11_sig_hdr & M11_SIG_EEPROM)  m11_ev |= M11_EV_SIG_EEPROM;
  if (m11_sig_hdr & M11_SIG_SRAM)    m11_ev |= M11_EV_SIG_SRAM;
  if (m11_sig_hdr & M11_SIG_FLASH1M) m11_ev |= M11_EV_SIG_FLASH1M;
  if (m11_sig_hdr & M11_SIG_FLASH5)  m11_ev |= M11_EV_SIG_FLASH5;
  if (m11_sig_swp & M11_SIG_EEPROM)  m11_ev |= M11_EV_SWP_EEPROM;
  if (m11_sig_swp & M11_SIG_SRAM)    m11_ev |= M11_EV_SWP_SRAM;
  if (m11_sig_swp & M11_SIG_FLASH1M) m11_ev |= M11_EV_SWP_FLASH1M;
  if (m11_sig_swp & M11_SIG_FLASH5)  m11_ev |= M11_EV_SWP_FLASH5;
  if (m11_pokemon)                   m11_ev |= M11_EV_POKEMON;
  if (is_known_game)                 m11_ev |= M11_EV_KNOWN_GAME;
  if (m11_eep_guessed)               m11_ev |= M11_EV_EEP_GUESSED;

  m11_latched = 1;

  /* The baseline hash and dirty map. Taken AFTER m11_latched is set because
   * m11save_snapshot() refuses to run before the latch. */
  m11save_snapshot();
  m11_hash_first = m11_hash;

  /* The first snapshot establishes the baseline; it is not an observation of
   * the running game, so it must not count as one. */
  m11_snapshots    = 0;
  m11_write_frames = 0;
  m11_ev &= ~(u32)M11_EV_BACKUP_DIRTY;
}

/* -------------------------------------------------------------- the sample --
 *
 * Called once per emulated frame, AFTER the frame has run. Everything here is
 * a read. */
void m11save_snapshot(void)
{
  u32 active;
  u32 blocks;
  u32 i;
  u32 nonff = 0;
  u32 h = M11_FNV_OFFSET;
  u32 changed = 0;

  if (!m11_latched)
    return;

  m11_snapshots++;

  /* ---- the eleven scalars, folded into the sticky latch ---- */

  if (flash_command_position != 0)          m11_ev |= M11_EV_FLASH_CMD;
  if (flash_mode == FLASH_ID_MODE)          m11_ev |= M11_EV_FLASH_ID;
  if (flash_mode == FLASH_ERASE_MODE)       m11_ev |= M11_EV_FLASH_ERASE;
  if (flash_mode == FLASH_WRITE_MODE)       m11_ev |= M11_EV_FLASH_WRITE;
  if (flash_mode == FLASH_BANKSWITCH_MODE)  m11_ev |= M11_EV_FLASH_BANKSW;
  if (flash_bank_num == 1)                  m11_ev |= M11_EV_FLASH_BANK1;
  if (eeprom_mode != EEPROM_BASE_MODE)      m11_ev |= M11_EV_EEPROM_MODE;

  /* THE TWO THAT ARE ONLY MEANINGFUL AS A TRANSITION FROM THE BASELINE.
   * flash_bank_cnt == 2 on its own may have come from the DB or a FLASH1M_V
   * string; only 1 -> 2 AFTER the latch is the 0xB0 command at :1186, and only
   * that PROVES a 128 KB chip. Likewise eeprom_size == 16 is a measurement
   * only if the 14-bit DMA at :841-843 moved it. */
  if (flash_bank_cnt == FLASH_SIZE_128KB &&
      m11_st_bankcnt != FLASH_SIZE_128KB)
    m11_ev |= M11_EV_FLASH_128RT;

  if (eeprom_size == EEPROM_8_KBYTE && m11_st_eepsize != EEPROM_8_KBYTE)
    m11_ev |= M11_EV_EEPROM_8K;

  if (backup_type != m11_st_backup)
  {
    m11_ev |= M11_EV_TYPE_CHANGED;

    /* THE REVOCATION, :457-461 / :1134-1137. A GUESSED EEPROM that leaves
     * BACKUP_EEPROM was revoked by a real access to the SRAM/flash region --
     * gpSP routes it EEPROM -> UNKN -> SRAM inside a single call, so the
     * intermediate UNKN is never observable from here and SRAM is the only
     * state that can be seen afterwards. */
    if (m11_st_backup == BACKUP_EEPROM && m11_eep_guessed &&
        backup_type != BACKUP_EEPROM)
      m11_ev |= M11_EV_TYPE_REVOKED;

    /* THE ARRIVAL, :542. The ONLY runtime producer of BACKUP_EEPROM is the
     * assignment inside write_eeprom(); every other writer of backup_type
     * yields UNKN, SRAM, FLASH or backup_type_reset. Reaching BACKUP_EEPROM
     * from anything else therefore PROVES a 16-bit store to 0x0D was executed.
     * Unlike M11_EV_EEPROM_MODE this needs no transient to be caught at a frame
     * boundary -- backup_type is sticky in gpSP itself. */
    if (m11_st_backup != BACKUP_EEPROM && backup_type == BACKUP_EEPROM)
      m11_ev |= M11_EV_EEPROM_RT;
  }

  /* ---- the backup region: one pass, hash + dirty map + non-0xFF count ---- */

  active = m11_active_bytes();

  /* CEILING, NOT TRUNCATION. M11_BLOCK_BYTES is 1024, so a truncating divide
   * returns ZERO BLOCKS for any active region smaller than one block -- and
   * EEPROM512's region is 512 B. The dirty loop below would then never execute,
   * making M11_EV_BACKUP_DIRTY, DIRTY BLOCKS and WRITE FRAMES STRUCTURALLY
   * INCAPABLE of firing for a 512-byte EEPROM no matter what the game wrote.
   * That is exactly the defect that made Tony Hawk 2 report zero write evidence
   * on hardware while it was demonstrably driving the EEPROM bus.
   *
   * THE HASH BLOCK STAYS A FIXED 1024 BYTES. A partial final block is hashed
   * over its full 1024-byte span, deliberately: it keeps block 0's baseline
   * BYTE-IDENTICAL when the active region changes size underneath us -- which it
   * does, every time UNKNOWN (32 KB) resolves to EEPROM512 (512 B). Hashing only
   * `active` bytes would change block 0's hash at that moment and manufacture a
   * DIRTY BLOCK out of a pure reclassification.
   *
   * BOUNDS: blocks <= M11_BLOCKS == 128 and each block reads 1024 bytes, so the
   * last byte touched is 131071 -- the final byte of gamepak_backup[131072]. */
  blocks = (active + M11_BLOCK_BYTES - 1u) / M11_BLOCK_BYTES;
  if (blocks > M11_BLOCKS)
    blocks = M11_BLOCKS;

  for (i = 0; i < active; i++)
  {
    u8 b = gamepak_backup[i];
    h ^= (u32)b;
    h *= M11_FNV_PRIME;
    if (b != 0xFF)
      nonff++;
  }

  m11_hash  = h;
  m11_nonff = nonff;

  for (i = 0; i < blocks; i++)
  {
    u32 bh = m11_fnv(&gamepak_backup[i * M11_BLOCK_BYTES], M11_BLOCK_BYTES);

    if (!m11_bm_test(m11_blk_seen, i))
    {
      /* First sight of this block -- a baseline, not a change. A block that
       * only becomes active later (the region grew when the type resolved)
       * must not be reported as written. */
      m11_bm_set(m11_blk_seen, i);
      m11_blk_hash[i] = bh;
      continue;
    }

    if (bh != m11_blk_hash[i])
    {
      m11_blk_hash[i] = bh;
      changed = 1;

      if (!m11_bm_test(m11_blk_dirty, i))
      {
        m11_bm_set(m11_blk_dirty, i);
        m11_dirty_blocks++;
        if (m11_first_block == M11_NOBLOCK)
          m11_first_block = i;
      }
    }
  }

  if (changed)
  {
    m11_ev |= M11_EV_BACKUP_DIRTY;
    m11_write_frames++;
  }
}

/* ---------------------------------------------------------------- the answer -- */

unsigned int m11save_type(void)
{
  return (unsigned int)m11_classify();
}

unsigned int m11save_bytes(void)
{
  return (unsigned int)m11_bytes_of(m11_classify());
}

unsigned int m11save_source(void)
{
  u32 type = m11_classify();

  if (type == M11_SAVE_UNKNOWN)
    return (unsigned int)M11_SRC_NONE;

  /* While the committed type still matches the latched one, the static
   * derivation stands. */
  if (type == m11_st_type)
    return (unsigned int)m11_st_source;

  /* IT MOVED. Which of the two runtime paths moved it is decidable from the
   * evidence: a flash unlock sequence, an EEPROM transaction or a measured
   * 128 KB bank switch are all POSITIVE bus events, whereas a type that
   * arrived with none of them is the silent UNKN -> SRAM collapse. */
  /* M11_EV_EEPROM_RT BELONGS IN THIS SET. A runtime arrival at BACKUP_EEPROM
   * can ONLY have come from write_eeprom() (:542), so it is as positive as a
   * flash unlock. It was previously absent, and because a completed EEPROM DMA
   * returns eeprom_mode to BASE_MODE inside a single frame (:603), EEPROM_MODE
   * is routinely missed -- so a real EEPROM cartridge fell through to FALLBACK.
   * That is a MISREPORT: FALLBACK means the silent UNKN -> SRAM collapse, and
   * that collapse can only ever produce BACKUP_SRAM (:465/:1141), never EEPROM.
   * This is what made Tony Hawk 2 report SOURCE FALLBACK for a transition that
   * was in fact proof of EEPROM bus traffic. */
  if (m11_ev & (M11_EV_FLASH_CMD | M11_EV_FLASH_ID | M11_EV_FLASH_ERASE |
                M11_EV_FLASH_WRITE | M11_EV_FLASH_BANKSW |
                M11_EV_FLASH_128RT | M11_EV_EEPROM_MODE | M11_EV_EEPROM_8K |
                M11_EV_EEPROM_RT))
    return (unsigned int)M11_SRC_RUNTIME;

  return (unsigned int)M11_SRC_FALLBACK;
}

/* HOW MUCH THE ANSWER IS WORTH.
 *
 * THE RULE: a result is FINAL only when the evidence behind it CANNOT BE
 * REVOKED AND THE SIZE WAS MEASURED RATHER THAN DEFAULTED. Everything else is
 * PROVISIONAL. This is the function that refuses to let a default look like a
 * measurement, and it is deliberately conservative -- a wrong FINAL would make
 * M12 persist the wrong number of bytes. */
unsigned int m11save_state(void)
{
  u32 type = m11_classify();
  u32 sig  = m11_sig_hdr | m11_sig_swp;

  if (type == M11_SAVE_UNKNOWN)
    return (unsigned int)M11_STATE_UNKNOWN;

  /* A GUESSED EEPROM IS REVOCABLE BY CONSTRUCTION until real EEPROM traffic
   * happens: :457/:1134 will throw it away on the first SRAM-region access. */
  if ((type == M11_SAVE_EEPROM512 || type == M11_SAVE_EEPROM8K) &&
      m11_eep_guessed && !(m11_ev & M11_EV_EEPROM_MODE))
    return (unsigned int)M11_STATE_PROVISIONAL;

  switch (type)
  {
    case M11_SAVE_EEPROM8K:
      /* 8 KB is only ever reached by the 14-bit DMA MEASURING it. */
      if (m11_ev & M11_EV_EEPROM_8K)
        return (unsigned int)M11_STATE_FINAL;
      /* Latched at 8 KB means a savestate restored it; the measurement was
       * not observed in THIS run. */
      return (unsigned int)M11_STATE_PROVISIONAL;

    case M11_SAVE_EEPROM512:
      /* 512 B IS THE INITIALISER (:531) AND THE RESET VALUE (:2441). Even with
       * real EEPROM traffic, the 14-bit DMA may simply not have run yet, so
       * the SIZE is never final. */
      return (unsigned int)M11_STATE_PROVISIONAL;

    case M11_SAVE_FLASH128:
      /* The runtime 0xB0 bank switch is the only thing that PROVES 128 KB. A
       * FLASH1M_V string or a DB flag is a strong claim, not a measurement. */
      if (m11_ev & M11_EV_FLASH_128RT)
        return (unsigned int)M11_STATE_FINAL;
      return (unsigned int)M11_STATE_PROVISIONAL;

    case M11_SAVE_FLASH64:
      /* Flash is proven by the unlock sequence, but 64 KB is only the ABSENCE
       * of a 0xB0 so far -- the game may still switch banks later. Final only
       * when a FLASH512_V/FLASH_V string independently caps it. */
      if ((m11_ev & M11_EV_FLASH_CMD) && (sig & M11_SIG_FLASH5) &&
          !(sig & M11_SIG_FLASH1M))
        return (unsigned int)M11_STATE_FINAL;
      return (unsigned int)M11_STATE_PROVISIONAL;

    case M11_SAVE_SRAM:
      /* An SRAM signature is positive evidence and SRAM has exactly one size,
       * so there is nothing left to revoke. Without one this is the collapse. */
      if (sig & M11_SIG_SRAM)
        return (unsigned int)M11_STATE_FINAL;
      return (unsigned int)M11_STATE_PROVISIONAL;

    default:
      return (unsigned int)M11_STATE_PROVISIONAL;
  }
}

unsigned int m11save_evidence(void)
{
  return (unsigned int)m11_ev;
}

/* ---------------------------------------------------------------- the probe --
 *
 * COHERENCE OF gpSP's SAVE STATE, NOT THE OUTCOME OF DETECTION. An UNKNOWN
 * type is not a failure and has no bit here. */
unsigned int m11save_probe(void)
{
  u32 bad = 0;
  u32 type;
  u32 bytes;

  if (!m11_latched)
    bad |= M11SAVE_NOLATCH;

  if (gamepak_size == 0 || gamepak_buffer_count == 0 || gamepak_buffers[0] == 0)
    bad |= M11SAVE_NOROM;

  if (backup_type > BACKUP_UNKN)
    bad |= M11SAVE_BADTYPE;

  if (flash_bank_cnt != FLASH_SIZE_64KB && flash_bank_cnt != FLASH_SIZE_128KB)
    bad |= M11SAVE_BADBANKCNT;

  if (flash_bank_num > 1)
    bad |= M11SAVE_BADBANKNUM;

  /* BANK 1 ON A 64 KB CHIP. read_backup()/write_backup() form the address as
   * `address + 64*1024*flash_bank_num` (:502, :1238); with bank 1 selected on
   * a chip gpSP believes is 64 KB, that runs past the region the chip has. */
  if (flash_bank_num == 1 && flash_bank_cnt != FLASH_SIZE_128KB)
    bad |= M11SAVE_BANKOOB;

  if (eeprom_size != EEPROM_512_BYTE && eeprom_size != EEPROM_8_KBYTE)
    bad |= M11SAVE_BADEEPSIZE;

  if (flash_mode > FLASH_BANKSWITCH_MODE)
    bad |= M11SAVE_BADFLMODE;

  if (eeprom_mode > EEPROM_WRITE_FOOTER_MODE)
    bad |= M11SAVE_BADEEPMODE;

  /* flash_command_position is only ever 0, 1 or 2 (:1149, :1153, :1247). */
  if (flash_command_position > 2)
    bad |= M11SAVE_BADCMDPOS;

  type  = m11_classify();
  bytes = m11_bytes_of(type);

  if (type != M11_SAVE_UNKNOWN && bytes == 0)
    bad |= M11SAVE_SIZE0;

  if (bytes > M11_BACKUP_BYTES)
    bad |= M11SAVE_OVERRUN;

  m11_probe_last = bad;
  return (unsigned int)bad;
}

/* ------------------------------------------------------------- the report --
 *
 * The CARTRIDGE and the LIMITS OF THE EVIDENCE. Never fatal. */
unsigned int m11save_report(void)
{
  u32 r = 0;
  u32 type = m11_classify();
  u32 sig  = m11_sig_hdr | m11_sig_swp;

  if (type == M11_SAVE_SRAM && !(sig & M11_SIG_SRAM))
    r |= M11_R_SRAM_FALLBACK;

  if (type == M11_SAVE_EEPROM512)
    r |= M11_R_EEPROM_DEFAULT;

  /* THE SWEEP SAW LESS THAN THE CARTRIDGE. Only meaningful when a sweep ran at
   * all -- gpSP sweeps only when the header scan found nothing. */
  if (m11_sig_hdr == 0 && m11_swept < gamepak_size)
    r |= M11_R_SWEEP_PARTIAL;

  if (m11_st_source == M11_SRC_DB_OR_SIG)
    r |= M11_R_SRC_AMBIG;

  if (m11_hack)
    r |= M11_R_HACK;

  if (m11_latched && type != m11_st_type)
    r |= M11_R_TYPE_MOVED;

  /* NOTHING HAS EVER BEEN SAVED. The active region is still the 0xFF idle
   * state that :2907 normalises it to. */
  if (m11_latched && m11_nonff == 0)
    r |= M11_R_BLANK;

  if (sig == 0)
    r |= M11_R_NOSIG;

  if (gamepak_must_swap())
    r |= M11_R_PAGED;

  m11_report_last = r;
  return (unsigned int)r;
}

/* ------------------------------------------------------------------ the log -- */

unsigned int m11save_read(unsigned int sel)
{
  switch (sel)
  {
    case M11_RD_TYPE:           return m11save_type();
    case M11_RD_BYTES:          return m11save_bytes();
    case M11_RD_SOURCE:         return m11save_source();
    case M11_RD_STATE:          return m11save_state();

    case M11_RD_BACKUP_TYPE:    return (unsigned int)backup_type;
    case M11_RD_BACKUP_RESET:   return (unsigned int)backup_type_reset;

    case M11_RD_FLASH_MODE:     return (unsigned int)flash_mode;
    case M11_RD_FLASH_CMDPOS:   return (unsigned int)flash_command_position;
    case M11_RD_FLASH_BANKNUM:  return (unsigned int)flash_bank_num;
    case M11_RD_FLASH_BANKCNT:  return (unsigned int)flash_bank_cnt;
    case M11_RD_FLASH_DEVID:    return (unsigned int)flash_device_id;

    /* THE MANUFACTURER BYTE read_backup() WOULD RETURN AT ADDRESS 0 IN
     * FLASH_ID_MODE -- DERIVED from :471-497, NOT a bus read. Calling
     * read_backup() to obtain it would collapse an UNKNOWN cartridge to SRAM. */
    case M11_RD_FLASH_MANUF:
      if (flash_bank_cnt == FLASH_SIZE_128KB)
        return (flash_device_id == FLASH_DEVICE_SANYO_128KB)
                 ? (unsigned int)FLASH_MANUFACTURER_SANYO
                 : (unsigned int)FLASH_MANUFACTURER_MACRONIX;
      return (unsigned int)FLASH_MANUFACTURER_PANASONIC;

    case M11_RD_EEPROM_MODE:    return (unsigned int)eeprom_mode;
    case M11_RD_EEPROM_SIZE:    return (unsigned int)eeprom_size;
    case M11_RD_EEPROM_ADDR:    return (unsigned int)eeprom_address;
    case M11_RD_EEPROM_COUNT:   return (unsigned int)eeprom_counter;

    /* THE ADDRESS WIDTH THE COUNTER IMPLIES, :565. 6 bits for 512 B, 14 for
     * 8 KB -- the two are what write_eeprom() compares eeprom_counter against. */
    case M11_RD_EEPROM_BITS:
      return (eeprom_size == EEPROM_8_KBYTE) ? 14u : 6u;

    case M11_RD_STATIC_TYPE:    return (unsigned int)m11_st_type;
    case M11_RD_STATIC_SOURCE:  return (unsigned int)m11_st_source;
    case M11_RD_STATIC_BYTES:   return (unsigned int)m11_st_bytes;

    case M11_RD_EVIDENCE:       return (unsigned int)m11_ev;
    case M11_RD_SIG_HEADER:     return (unsigned int)m11_sig_hdr;
    case M11_RD_SIG_SWEEP:      return (unsigned int)m11_sig_swp;
    case M11_RD_SWEEP_BYTES:    return (unsigned int)m11_swept;
    case M11_RD_SWEEP_TOTAL:    return (unsigned int)gamepak_size;

    case M11_RD_HASH:           return (unsigned int)m11_hash;
    case M11_RD_HASH_FIRST:     return (unsigned int)m11_hash_first;
    case M11_RD_DIRTY_BLOCKS:   return (unsigned int)m11_dirty_blocks;
    case M11_RD_FIRST_BLOCK:    return (unsigned int)m11_first_block;
    case M11_RD_WRITE_FRAMES:   return (unsigned int)m11_write_frames;
    case M11_RD_NONFF:          return (unsigned int)m11_nonff;

    case M11_RD_KNOWN_GAME:     return is_known_game ? 1u : 0u;
    case M11_RD_POKEMON:        return (unsigned int)m11_pokemon;
    case M11_RD_HDR_NONSTD:     return gamepak_header_nonstandard ? 1u : 0u;

    case M11_RD_SNAPSHOTS:      return (unsigned int)m11_snapshots;
    case M11_RD_PROBE:          return (unsigned int)m11_probe_last;
    case M11_RD_REPORT:         return (unsigned int)m11_report_last;

    default:                    return 0u;
  }
}
