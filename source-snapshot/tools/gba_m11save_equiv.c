/* ===========================================================================
 * tools/gba_m11save_equiv.c -- THE OFFLINE SAVE-OBSERVER HARNESS
 * ===========================================================================
 *
 * Builds and runs with the HOST compiler, NOT the PS5 toolchain, and
 * #includes adapters/gba/gba_m11save.c VERBATIM. It therefore tests the
 * SHIPPING observer rather than a paraphrase of it.
 *
 * RUN THIS BEFORE SPENDING ANY CONSOLE TIME. Every property below is one that
 * would otherwise have to be diagnosed from a UDP log, on hardware, from a
 * relaunch -- and several of them are silent-wrong rather than loud.
 *
 * ---- WHY A HARNESS IS POSSIBLE AT ALL ----
 *
 * gba_m11save.c opens with #include "common.h". This file pre-defines that
 * header's own guard (COMMON_H, gpsp/common.h:20-21, whose #endif closes at end
 * of file) BEFORE including the production .c, so common.h's body -- and every
 * header it chains to -- expands to NOTHING. The harness then supplies the
 * handful of types, constants and globals the observer actually uses.
 *
 * The preprocessor must still FIND common.h before it can skip it, which is why
 * the build rule passes -I gpsp. That is a BUILD RULE change only:
 * adapters/gba/gba_m11save.c is not modified to suit the harness, nothing is
 * copied out of gpsp/, and no test-only #ifdef exists in the production source.
 * gba_m11save_equiv appears in NO object list and no payload rule builds it.
 *
 * ---- WHAT IS ACTUALLY BEING PROVED ----
 *
 *   1. THE UNIT-COUNT TRAP. eeprom_size and flash_bank_cnt are UNIT COUNTS, not
 *      byte counts: EEPROM_8_KBYTE is 16 and FLASH_SIZE_128KB is 2
 *      (gpsp/gba_memory.h:294-298). An observer that echoed either would report
 *      "16 bytes" for an 8 KB EEPROM. THIS IS THE SINGLE EASIEST WAY TO GET
 *      M11 WRONG and it would look entirely plausible on screen.
 *
 *   2. backup_type ALONE IS NOT AN ANSWER. BACKUP_FLASH covers 64 KB and
 *      128 KB; BACKUP_EEPROM covers 512 B and 8 KB. Both must resolve against
 *      their companion unit count.
 *
 *   3. UNKNOWN IS 0 BYTES, NOT A GUESS. Reporting 32768 for an undetected
 *      cartridge would be inventing a size.
 *
 *   4. THE ACTIVE REGION NEVER EXCEEDS gamepak_backup[]. 128 KB is all there
 *      is; a hash that walked past it would read someone else's memory.
 *
 *   5. THE OBSERVER IS PASSIVE. Running latch + snapshots must not move
 *      backup_type, flash_bank_cnt, eeprom_size or any byte of the backup
 *      region. This is the property the whole milestone rests on, and it is
 *      checked here by byte-comparison rather than by inspection.
 *
 *   6. COHERENCE ASSERTIONS FIRE. States that no correct execution of
 *      gba_memory.c can produce must set their fatal bit -- a probe that never
 *      fires is indistinguishable from one that always passes.
 *
 *   7. THE DIRTY MAP TRACKS REAL WRITES. A changed byte must mark exactly one
 *      block, and the blank/non-FF accounting must agree with the bytes.
 * ========================================================================= */

#include <stdio.h>
#include <string.h>

/* ---- neutralise gpsp/common.h ------------------------------------------- */
#define COMMON_H

/* ---- the types gpSP would have supplied --------------------------------- */
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed char        s8;
typedef short              s16;
typedef int                s32;

#include <stdbool.h>

/* ---- gpsp/gba_memory.h's constants, transcribed ------------------------- */
#define BACKUP_SRAM       0
#define BACKUP_FLASH      1
#define BACKUP_EEPROM     2
#define BACKUP_UNKN       3

#define FLASH_SIZE_64KB   1
#define FLASH_SIZE_128KB  2

#define EEPROM_512_BYTE   1
#define EEPROM_8_KBYTE   16

#define EEPROM_BASE_MODE              0
#define EEPROM_READ_MODE              1
#define EEPROM_READ_HEADER_MODE       2
#define EEPROM_ADDRESS_MODE           3
#define EEPROM_WRITE_MODE             4
#define EEPROM_WRITE_ADDRESS_MODE     5
#define EEPROM_ADDRESS_FOOTER_MODE    6
#define EEPROM_WRITE_FOOTER_MODE      7

#define FLASH_BASE_MODE               0
#define FLASH_ERASE_MODE              1
#define FLASH_ID_MODE                 2
#define FLASH_WRITE_MODE              3
#define FLASH_BANKSWITCH_MODE         4

#define FLASH_DEVICE_UNDEFINED       0x00
#define FLASH_DEVICE_MACRONIX_64KB   0x1C
#define FLASH_DEVICE_AMTEL_64KB      0x3D
#define FLASH_DEVICE_SST_64K         0xD4
#define FLASH_DEVICE_PANASONIC_64KB  0x1B
#define FLASH_DEVICE_MACRONIX_128KB  0x09
#define FLASH_DEVICE_SANYO_128KB     0x13

#define FLASH_MANUFACTURER_MACRONIX  0xC2
#define FLASH_MANUFACTURER_AMTEL     0x1F
#define FLASH_MANUFACTURER_PANASONIC 0x32
#define FLASH_MANUFACTURER_SST       0xBF
#define FLASH_MANUFACTURER_SANYO     0x62

/* ---- the gpSP globals the observer reads -------------------------------- */
u32  gamepak_size          = 0;
u32  gamepak_buffer_count  = 0;
u32  backup_type           = BACKUP_UNKN;
u32  flash_bank_cnt        = FLASH_SIZE_64KB;
u32  eeprom_size           = EEPROM_512_BYTE;
u8   gamepak_backup[1024 * 128];
bool gamepak_header_nonstandard = false;

u32  backup_type_reset     = BACKUP_UNKN;
u32  flash_mode            = FLASH_BASE_MODE;
u32  flash_command_position = 0;
u32  flash_bank_num        = 0;
u32  flash_device_id       = FLASH_DEVICE_MACRONIX_64KB;
u32  eeprom_mode           = EEPROM_BASE_MODE;
u32  eeprom_address        = 0;
u32  eeprom_counter        = 0;
bool is_known_game         = false;
u8  *gamepak_buffers[32];
const unsigned gamepak_buffer_blocksize = 1024 * 1024;

/* gamepak_must_swap() is a FUNCTION in gpSP (gba_memory.h:263), not a variable.
 * The harness models it the way gpSP computes it: the cartridge needs paging
 * when it is larger than the buffers can hold. */
bool gamepak_must_swap(void)
{
    return (u64)gamepak_size >
           (u64)gamepak_buffer_count * (u64)gamepak_buffer_blocksize;
}

/* ---- THE PRODUCTION OBSERVER, INCLUDED VERBATIM ------------------------- */
#include "gba_m11save.c"

/* ========================================================================= */

static int failures = 0;
static int checks   = 0;

static void ck(int cond, const char *what)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL: %s\n", what);
    }
}

static void ck_u(u32 got, u32 want, const char *what)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL: %s -- got %u, want %u\n", what, got, want);
    }
}

/* A fake 1 MB cartridge the sweep and the header scan can walk. */
static u8 rom_image[1024 * 1024];

/* RESET EVERY PIECE OF MODULE STATE.
 *
 * The observer is deliberately idempotent-by-refusal -- m11save_latch_static()
 * returns immediately if it has already run, so that latching twice cannot
 * silently absorb whatever happened in between into the "static" baseline.
 * That is correct for production and means a harness testing more than one
 * cartridge MUST clear the state itself. It can, because #include'ing the .c
 * puts these statics in this translation unit. */
static void reset_module(void)
{
    u32 i;
    m11_latched = 0;
    m11_st_type = M11_SAVE_UNKNOWN;
    m11_st_source = M11_SRC_NONE;
    m11_st_bytes = 0;
    m11_st_backup = 0;
    m11_st_bankcnt = 0;
    m11_st_eepsize = 0;
    m11_st_devid = 0;
    m11_sig_hdr = 0;
    m11_sig_swp = 0;
    m11_swept = 0;
    m11_pokemon = 0;
    m11_eep_guessed = 0;
    m11_hack = 0;
    m11_ev = 0;
    m11_hash = 0;
    m11_hash_first = 0;
    m11_nonff = 0;
    m11_snapshots = 0;
    m11_write_frames = 0;
    m11_dirty_blocks = 0;
    m11_first_block = M11_NOBLOCK;
    m11_probe_last = 0;
    m11_report_last = 0;
    for (i = 0; i < M11_BLOCKS; i++) m11_blk_hash[i] = 0;
    for (i = 0; i < (M11_BLOCKS + 31) / 32; i++) {
        m11_blk_seen[i] = 0;
        m11_blk_dirty[i] = 0;
    }
}

/* A clean, plausible cartridge. Defaults are what gpSP holds after
 * init_memory() and before any detection has committed anything. */
static void setup_cart(u32 btype, u32 bankcnt, u32 eepsize)
{
    reset_module();

    memset(rom_image, 0, sizeof(rom_image));
    /* A minimal, standard-looking header: title at 0xA0, code at 0xAC. */
    memcpy(rom_image + 0xA0, "TESTCART", 8);
    memcpy(rom_image + 0xAC, "ABCD", 4);

    memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));

    gamepak_buffers[0]   = rom_image;
    gamepak_buffer_count = 1;
    gamepak_size         = sizeof(rom_image);

    backup_type   = btype;
    flash_bank_cnt = bankcnt;
    eeprom_size    = eepsize;

    backup_type_reset      = btype;
    flash_mode             = FLASH_BASE_MODE;
    flash_command_position = 0;
    flash_bank_num         = 0;
    flash_device_id        = FLASH_DEVICE_MACRONIX_64KB;
    eeprom_mode            = EEPROM_BASE_MODE;
    eeprom_address         = 0;
    eeprom_counter         = 0;
    is_known_game          = false;
    gamepak_header_nonstandard = false;
}

/* ---- 1 + 2 + 3: THE (type, size) MATRIX -------------------------------- */
static void test_type_and_size(void)
{
    printf("test: the (type, size) matrix -- THE UNIT-COUNT TRAP\n");

    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    ck_u(m11save_type(),  M11_SAVE_SRAM, "SRAM type");
    ck_u(m11save_bytes(), 32768u,        "SRAM is 32768 bytes");

    setup_cart(BACKUP_FLASH, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    ck_u(m11save_type(),  M11_SAVE_FLASH64, "FLASH 64K type");
    ck_u(m11save_bytes(), 65536u,           "FLASH 64K is 65536 bytes");

    /* THE TRAP: flash_bank_cnt is 2, and 2 is not a size. */
    setup_cart(BACKUP_FLASH, FLASH_SIZE_128KB, EEPROM_512_BYTE);
    m11save_latch_static();
    ck_u(m11save_type(),  M11_SAVE_FLASH128, "FLASH 128K type");
    ck_u(m11save_bytes(), 131072u,           "FLASH 128K is 131072 bytes "
                                             "(NOT flash_bank_cnt == 2)");

    setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    ck_u(m11save_type(),  M11_SAVE_EEPROM512, "EEPROM 512B type");
    ck_u(m11save_bytes(), 512u,               "EEPROM 512B is 512 bytes");

    /* THE TRAP AGAIN: eeprom_size is 16, and 16 is not a size. */
    setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_8_KBYTE);
    m11save_latch_static();
    ck_u(m11save_type(),  M11_SAVE_EEPROM8K, "EEPROM 8K type");
    ck_u(m11save_bytes(), 8192u,             "EEPROM 8K is 8192 bytes "
                                             "(NOT eeprom_size == 16)");

    /* UNKNOWN IS 0 BYTES. Reporting 32768 here would be inventing a size for a
     * cartridge whose save hardware has not been identified. */
    setup_cart(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    ck_u(m11save_type(),  M11_SAVE_UNKNOWN, "UNKNOWN type");
    ck_u(m11save_bytes(), 0u,               "UNKNOWN is 0 bytes, NOT a guess");
}

/* ---- 4: THE ACTIVE REGION IS BOUNDED ------------------------------------ */
static void test_active_region(void)
{
    printf("test: the active region never exceeds gamepak_backup[]\n");

    /* UNKNOWN watches the 32 KB SRAM window -- the region gpSP would use the
     * instant BACKUP_UNKN collapses to BACKUP_SRAM at :465/:1141. */
    setup_cart(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    ck_u(m11_active_bytes(), 32768u, "UNKNOWN watches the 32 KB SRAM window");

    setup_cart(BACKUP_FLASH, FLASH_SIZE_128KB, EEPROM_512_BYTE);
    m11save_latch_static();
    ck_u(m11_active_bytes(), M11_BACKUP_BYTES,
         "FLASH 128K fills gamepak_backup[] exactly");
    ck(m11_active_bytes() <= M11_BACKUP_BYTES,
       "the active region never exceeds 128 KB");
}

/* ---- 5: THE OBSERVER IS PASSIVE ----------------------------------------- */
static void test_passivity(void)
{
    static u8 before[sizeof(gamepak_backup)];
    u32 bt, bc, es, fm, fbn, em, ea, ec;
    int i;

    printf("test: PASSIVITY -- the observer changes nothing\n");

    setup_cart(BACKUP_FLASH, FLASH_SIZE_128KB, EEPROM_512_BYTE);

    /* Put recognisable data in the backup region so a stray write shows up. */
    for (i = 0; i < (int)sizeof(gamepak_backup); i++)
        gamepak_backup[i] = (u8)(i * 7 + 3);
    memcpy(before, gamepak_backup, sizeof(gamepak_backup));

    bt = backup_type; bc = flash_bank_cnt; es = eeprom_size;
    fm = flash_mode;  fbn = flash_bank_num;
    em = eeprom_mode; ea = eeprom_address; ec = eeprom_counter;

    m11save_latch_static();
    for (i = 0; i < 64; i++) m11save_snapshot();
    (void)m11save_probe();
    (void)m11save_report();
    (void)m11save_type();
    (void)m11save_bytes();
    (void)m11save_source();
    (void)m11save_state();
    (void)m11save_evidence();
    for (i = 0; i <= (int)M11_RD_REPORT; i++) (void)m11save_read((unsigned)i);

    ck(memcmp(before, gamepak_backup, sizeof(gamepak_backup)) == 0,
       "not one byte of gamepak_backup[] was modified");
    ck_u(backup_type,    bt,  "backup_type was not modified");
    ck_u(flash_bank_cnt, bc,  "flash_bank_cnt was not modified");
    ck_u(eeprom_size,    es,  "eeprom_size was not modified");
    ck_u(flash_mode,     fm,  "flash_mode was not modified");
    ck_u(flash_bank_num, fbn, "flash_bank_num was not modified");
    ck_u(eeprom_mode,    em,  "eeprom_mode was not modified");
    ck_u(eeprom_address, ea,  "eeprom_address was not modified");
    ck_u(eeprom_counter, ec,  "eeprom_counter was not modified");
}

/* ---- 6: THE COHERENCE ASSERTIONS ACTUALLY FIRE -------------------------- */
static void test_coherence(void)
{
    printf("test: the fatal coherence assertions fire\n");

    /* A clean cartridge must be coherent. If this fails, every other result
     * below is meaningless. */
    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    ck_u(m11save_probe(), 0u, "a clean SRAM cartridge probes clean");

    /* AN UNKNOWN TYPE IS NOT A FAILURE. This is the single most important
     * negative result in the milestone: not knowing must not read as broken. */
    setup_cart(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    ck_u(m11save_probe(), 0u, "an UNKNOWN save type is NOT a failure");

    /* NOLATCH: every derived field would be garbage. */
    reset_module();
    ck((m11save_probe() & M11SAVE_NOLATCH) != 0,
       "NOLATCH fires before the static latch");

    /* Bank 1 selected on a 64 KB chip: the :502/:1238 fulladdr would run past
     * the 64 KB the chip actually has. */
    setup_cart(BACKUP_FLASH, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    flash_bank_num = 1;
    ck((m11save_probe() & M11SAVE_BANKOOB) != 0,
       "BANKOOB fires for bank 1 on a 64 KB chip");
    flash_bank_num = 0;

    setup_cart(BACKUP_FLASH, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    flash_bank_num = 3;
    ck((m11save_probe() & M11SAVE_BADBANKNUM) != 0,
       "BADBANKNUM fires for flash_bank_num > 1");
    flash_bank_num = 0;

    setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    eeprom_size = 7;
    ck((m11save_probe() & M11SAVE_BADEEPSIZE) != 0,
       "BADEEPSIZE fires for an eeprom_size that is neither 1 nor 16");
    eeprom_size = EEPROM_512_BYTE;

    setup_cart(BACKUP_FLASH, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    flash_bank_cnt = 5;
    ck((m11save_probe() & M11SAVE_BADBANKCNT) != 0,
       "BADBANKCNT fires for a flash_bank_cnt that is neither 1 nor 2");
    flash_bank_cnt = FLASH_SIZE_64KB;

    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    flash_mode = 9;
    ck((m11save_probe() & M11SAVE_BADFLMODE) != 0,
       "BADFLMODE fires past FLASH_BANKSWITCH_MODE");
    flash_mode = FLASH_BASE_MODE;

    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    eeprom_mode = 9;
    ck((m11save_probe() & M11SAVE_BADEEPMODE) != 0,
       "BADEEPMODE fires past WRITE_FOOTER_MODE");
    eeprom_mode = EEPROM_BASE_MODE;

    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    flash_command_position = 4;
    ck((m11save_probe() & M11SAVE_BADCMDPOS) != 0,
       "BADCMDPOS fires for flash_command_position > 2");
    flash_command_position = 0;

    /* NOROM: no cartridge is loaded at all. */
    reset_module();
    gamepak_buffers[0]   = 0;
    gamepak_buffer_count = 0;
    gamepak_size         = 0;
    backup_type          = BACKUP_SRAM;
    m11save_latch_static();
    ck((m11save_probe() & M11SAVE_NOROM) != 0,
       "NOROM fires when no cartridge is resident");
}

/* ---- 7: THE DIRTY MAP AND THE BLANK ACCOUNTING -------------------------- */
static void test_dirty_map(void)
{
    printf("test: the dirty map tracks real writes\n");

    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();

    /* A pristine 0xFF region is BLANK and has nothing dirty. */
    ck_u(m11save_read(M11_RD_DIRTY_BLOCKS), 0u, "nothing dirty at the latch");
    ck_u(m11save_read(M11_RD_NONFF),        0u, "a pristine region has 0 "
                                                "non-FF bytes");
    ck((m11save_report() & M11_R_BLANK) != 0,
       "a pristine region reports BLANK");
    ck_u(m11save_read(M11_RD_FIRST_BLOCK), M11_NOBLOCK,
         "no first dirty block yet");

    /* Write one byte in block 3 and sample. */
    gamepak_backup[3 * M11_BLOCK_BYTES + 17] = 0x42;
    m11save_snapshot();

    ck_u(m11save_read(M11_RD_DIRTY_BLOCKS), 1u, "exactly one block went dirty");
    ck_u(m11save_read(M11_RD_FIRST_BLOCK),  3u, "the first dirty block is 3");
    ck_u(m11save_read(M11_RD_NONFF),        1u, "exactly one non-FF byte");
    ck_u(m11save_read(M11_RD_WRITE_FRAMES), 1u, "one frame saw a change");
    ck((m11save_evidence() & M11_EV_BACKUP_DIRTY) != 0,
       "BACKUP_DIRTY latched");
    ck((m11save_report() & M11_R_BLANK) == 0,
       "the region no longer reports BLANK");

    /* An unchanged frame must NOT count as a write frame -- otherwise the
     * count measures sampling rather than saving. */
    m11save_snapshot();
    ck_u(m11save_read(M11_RD_WRITE_FRAMES), 1u,
         "an unchanged frame is not a write frame");
    ck_u(m11save_read(M11_RD_DIRTY_BLOCKS), 1u,
         "an unchanged frame adds no dirty block");

    /* A second block. */
    gamepak_backup[10 * M11_BLOCK_BYTES] = 0x00;
    m11save_snapshot();
    ck_u(m11save_read(M11_RD_DIRTY_BLOCKS), 2u, "a second block went dirty");
    ck_u(m11save_read(M11_RD_FIRST_BLOCK),  3u,
         "the FIRST dirty block is still 3, not the most recent");

    /* The snapshot counter excludes the baseline taken inside the latch. */
    ck_u(m11save_read(M11_RD_SNAPSHOTS), 3u,
         "the latch's baseline snapshot is not counted as an observation");
}

/* ---- the hash is order-sensitive and actually a function of the bytes --- */
static void test_hash(void)
{
    u32 h1, h2;

    printf("test: the FNV-1a hash responds to the bytes\n");

    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    h1 = m11save_read(M11_RD_HASH);

    ck_u(m11save_read(M11_RD_HASH_FIRST), h1,
         "the latch hash and the first sample agree");

    gamepak_backup[0] ^= 0xFF;
    m11save_snapshot();
    h2 = m11save_read(M11_RD_HASH);
    ck(h1 != h2, "one flipped byte changes the hash");

    /* Restoring the byte must restore the hash -- the hash is a function of the
     * region's CONTENT, not of how many times it has been sampled. */
    gamepak_backup[0] ^= 0xFF;
    m11save_snapshot();
    ck_u(m11save_read(M11_RD_HASH), h1,
         "restoring the byte restores the hash");

    /* ...but the DIRTY record is STICKY. A block that changed and changed back
     * has still changed, and reporting it as clean would hide a real write. */
    ck_u(m11save_read(M11_RD_DIRTY_BLOCKS), 1u,
         "the dirty record is STICKY across a revert");
}

/* ---- the report-only observations are report-only ----------------------- */
static void test_report_only(void)
{
    printf("test: the report-only mask never fails the probe\n");

    /* SRAM with no SRAM signature anywhere is the silent UNKN -> SRAM collapse.
     * It must be REPORTED and must NOT be fatal. */
    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    ck((m11save_report() & M11_R_SRAM_FALLBACK) != 0,
       "SRAM with no signature reports SRAM_FALLBACK");
    ck((m11save_report() & M11_R_NOSIG) != 0,
       "a signature-free cartridge reports NOSIG");
    ck_u(m11save_probe(), 0u, "...and NONE of that is fatal");

    /* An EEPROM sized 512 B by default -- the 14-bit DMA never ran. */
    setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    ck((m11save_report() & M11_R_EEPROM_DEFAULT) != 0,
       "a defaulted EEPROM size reports EEPROM_DEFAULT");
    ck_u(m11save_probe(), 0u, "...and that is not fatal either");
}

/* ---- the signature scanner finds gpSP's own literals --------------------- */
static void test_signatures(void)
{
    printf("test: the signature scanner matches gpSP's literals\n");

    setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    memcpy(rom_image + 0x8000, "EEPROM_V122", 11);
    m11save_latch_static();
    ck((m11save_evidence() & M11_EV_SIG_EEPROM) != 0,
       "EEPROM_V is found in the header window");
    ck((m11save_report() & M11_R_NOSIG) == 0,
       "a cartridge with a signature does not report NOSIG");

    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    memcpy(rom_image + 0x1000, "SRAM_V113", 9);
    m11save_latch_static();
    ck((m11save_evidence() & M11_EV_SIG_SRAM) != 0,
       "SRAM_V is found in the header window");
    ck((m11save_report() & M11_R_SRAM_FALLBACK) == 0,
       "SRAM WITH a signature is NOT the fallback collapse");

    setup_cart(BACKUP_FLASH, FLASH_SIZE_128KB, EEPROM_512_BYTE);
    memcpy(rom_image + 0x2000, "FLASH1M_V102", 12);
    m11save_latch_static();
    ck((m11save_evidence() & M11_EV_SIG_FLASH1M) != 0,
       "FLASH1M_V is found in the header window");
}

/* ---- 8: THE EEPROM512 DIRTY-BLOCK REGRESSION ---------------------------- *
 *
 * THE DEFECT THIS PINS DOWN. m11save_snapshot() sized its dirty-block loop with
 * a TRUNCATING divide:
 *
 *     blocks = active / M11_BLOCK_BYTES;
 *
 * M11_BLOCK_BYTES is 1024 and an EEPROM512 region is 512 bytes, so this
 * evaluated to ZERO and the dirty loop never ran a single iteration. DIRTY
 * BLOCKS, WRITE FRAMES and M11_EV_BACKUP_DIRTY were therefore STRUCTURALLY
 * INCAPABLE of reporting anything for a 512-byte EEPROM, no matter what the
 * cartridge wrote. On hardware that produced Tony Hawk 2's "zero write
 * evidence" -- which was an ARTEFACT OF INTEGER DIVISION, not an observation
 * about the game.
 *
 * WHY NO EXISTING TEST CAUGHT IT: every other scenario in this file calls
 * setup_cart(BACKUP_SRAM, ...), giving a 32 KB region and 32 whole blocks. No
 * test had ever made the active region SMALLER THAN ONE BLOCK. That is the
 * entire blind spot, and it is closed here. */
static void test_eeprom512_dirty(void)
{
    printf("test: a 512-byte EEPROM region is actually watched\n");

    setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();

    /* A. The region really is the sub-block size that broke the divide. */
    ck_u(m11save_read(M11_RD_TYPE),  M11_SAVE_EEPROM512, "type is EEPROM512");
    ck_u(m11save_read(M11_RD_BYTES), 512u,
         "the active region is 512 B -- LESS THAN ONE 1024-BYTE BLOCK");

    ck_u(m11save_read(M11_RD_DIRTY_BLOCKS), 0u, "nothing dirty at the latch");
    ck_u(m11save_read(M11_RD_NONFF), 0u, "a pristine EEPROM region is all 0xFF");
    ck((m11save_report() & M11_R_BLANK) != 0, "a pristine region reports BLANK");

    /* C. Mutate one byte of the backing store, exactly as write_eeprom() would
     *    at gba_memory.c:581/:587. */
    gamepak_backup[0] = 0x5A;

    /* D. */
    m11save_snapshot();

    /* B. The ceiling divide must cover the partial block. THIS IS THE
     *    ASSERTION THAT FAILS AGAINST THE OLD TRUNCATING DIVIDE. */
    ck_u(m11save_read(M11_RD_DIRTY_BLOCKS), 1u,
         "the partial 512-byte block IS covered -- ceiling, not truncation");
    ck_u(m11save_read(M11_RD_FIRST_BLOCK), 0u, "the first dirty block is 0");

    /* E. */
    ck((m11save_evidence() & M11_EV_BACKUP_DIRTY) != 0,
       "BACKUP_DIRTY latches for a 512-byte EEPROM");

    /* F. */
    ck_u(m11save_read(M11_RD_WRITE_FRAMES), 1u,
         "WRITE FRAMES increments for a 512-byte EEPROM");

    /* G. */
    ck_u(m11save_read(M11_RD_NONFF), 1u,
         "the changed byte is counted as non-FF");
    ck((m11save_report() & M11_R_BLANK) == 0, "the region is no longer BLANK");

    /* A quiet frame still must not be counted as a write frame. */
    m11save_snapshot();
    ck_u(m11save_read(M11_RD_WRITE_FRAMES), 1u,
         "an unchanged frame is not a write frame");

    /* H. BOUNDS. With active == 512 the ceiling gives EXACTLY ONE block, so
     *    block 1 must not be scanned. Writing there proves the loop stops where
     *    it should rather than walking the whole 128 KB array. The non-FF count
     *    must also ignore it, because that loop is bounded by `active`. */
    gamepak_backup[M11_BLOCK_BYTES] = 0x11;
    m11save_snapshot();
    ck_u(m11save_read(M11_RD_DIRTY_BLOCKS), 1u,
         "block 1 is NOT scanned for a 512-byte region -- the loop is bounded");
    ck_u(m11save_read(M11_RD_NONFF), 1u,
         "the non-FF count stays bounded by the 512-byte active region");
    ck_u(m11save_probe(), 0u, "none of this is fatal");

    /* THE EXACT-MULTIPLE CASE MUST NOT REGRESS. A ceiling divide would be a bad
     * fix if it invented a 33rd block for SRAM's exact 32 KB. */
    setup_cart(BACKUP_SRAM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    gamepak_backup[31 * M11_BLOCK_BYTES] = 0x01;   /* the LAST whole block */
    m11save_snapshot();
    ck_u(m11save_read(M11_RD_DIRTY_BLOCKS), 1u,
         "an exact 32 KB region still resolves to exactly 32 blocks");
    ck_u(m11save_read(M11_RD_FIRST_BLOCK), 31u,
         "the last block of an exact-multiple region is block 31");
}

/* ---- 9: RUNTIME EEPROM ARRIVAL IS POSITIVE EVIDENCE, NOT A FALLBACK ----- *
 *
 * gba_memory.c has exactly ONE runtime producer of BACKUP_EEPROM: the
 * assignment at :542, inside write_eeprom(). Every other writer of backup_type
 * yields UNKN (:407, :461, :1137), SRAM (:465, :1141), FLASH (:1148) or
 * backup_type_reset (:2438, :3073). An observer that latched a non-EEPROM type
 * and later sees BACKUP_EEPROM has therefore PROVED a 16-bit store to 0x0D.
 *
 * That case used to fall through m11save_source() to M11_SRC_FALLBACK, which is
 * a MISREPORT: FALLBACK denotes the silent UNKN -> SRAM collapse, and that
 * collapse cannot produce EEPROM. It happened because the only EEPROM bit in
 * the positive set, M11_EV_EEPROM_MODE, needs eeprom_mode to be caught away
 * from BASE_MODE AT a frame boundary -- and a real EEPROM DMA finishes inside a
 * frame, resetting the mode at :603. */
static void test_runtime_eeprom_source(void)
{
    printf("test: a runtime arrival at EEPROM reports RUNTIME, not FALLBACK\n");

    /* Latch UNKNOWN -- Tony Hawk 2's actual Stage 0 result: no DB entry, no
     * signature, not Pokemon. */
    setup_cart(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    ck_u(m11save_read(M11_RD_STATIC_TYPE), M11_SAVE_UNKNOWN,
         "the static verdict is UNKNOWN");
    ck((m11save_evidence() & M11_EV_EEPROM_RT) == 0,
       "no runtime EEPROM evidence before the bus does anything");

    /* write_eeprom() runs: :542 commits BACKUP_EEPROM. The mode has ALREADY
     * returned to BASE_MODE by the time the frame ends (:603), which is
     * precisely why EEPROM_MODE cannot be relied on here. */
    backup_type = BACKUP_EEPROM;
    eeprom_mode = EEPROM_BASE_MODE;
    m11save_snapshot();

    ck((m11save_evidence() & M11_EV_EEPROM_RT) != 0,
       "EEPROM_RT latches on the UNKN -> EEPROM arrival");
    ck((m11save_evidence() & M11_EV_TYPE_CHANGED) != 0, "TYPE_CHANGED latches");
    ck((m11save_evidence() & M11_EV_EEPROM_MODE) == 0,
       "...and it did NOT need the sub-frame EEPROM_MODE transient");

    ck_u(m11save_source(), M11_SRC_RUNTIME,
         "the source is RUNTIME BUS EVIDENCE, not the UNKN->SRAM FALLBACK");
    ck_u(m11save_type(), M11_SAVE_EEPROM512, "the type is EEPROM512");
    ck_u(m11save_bytes(), 512u, "the size is 512 B");

    /* HONEST CONFIDENCE. Proving the bus ran does NOT prove the SIZE: 512 B is
     * still gpSP's initialiser (:531) and reset value (:2441), and the 14-bit
     * DMA that would MEASURE 8 KB may simply not have happened yet. */
    ck_u(m11save_state(), M11_STATE_PROVISIONAL,
         "EEPROM512 stays PROVISIONAL even WITH runtime evidence");
    ck((m11save_report() & M11_R_EEPROM_DEFAULT) != 0,
       "...and still reports that the size is a DEFAULT");
    ck_u(m11save_probe(), 0u, "none of this is fatal");

    /* THE NEGATIVE. The genuine silent collapse must STILL report FALLBACK --
     * the fix must not turn every type change into positive evidence. */
    setup_cart(BACKUP_UNKN, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    backup_type = BACKUP_SRAM;
    m11save_snapshot();
    ck((m11save_evidence() & M11_EV_EEPROM_RT) == 0,
       "an UNKN -> SRAM collapse sets no EEPROM evidence");
    ck_u(m11save_source(), M11_SRC_FALLBACK,
         "the silent UNKN -> SRAM collapse is STILL reported as FALLBACK");

    /* A cartridge latched AS EEPROM has proved nothing at runtime -- the bit is
     * about the ARRIVAL, not about the value. */
    setup_cart(BACKUP_EEPROM, FLASH_SIZE_64KB, EEPROM_512_BYTE);
    m11save_latch_static();
    m11save_snapshot();
    ck((m11save_evidence() & M11_EV_EEPROM_RT) == 0,
       "a statically-latched EEPROM does NOT claim runtime evidence");
}

int main(void)
{
    printf("==== gba_m11save offline harness ====\n");
    printf("Testing the SHIPPING observer, included verbatim.\n\n");

    test_type_and_size();
    test_active_region();
    test_passivity();
    test_coherence();
    test_dirty_map();
    test_hash();
    test_report_only();
    test_signatures();
    test_eeprom512_dirty();
    test_runtime_eeprom_source();

    printf("\n%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("M11 EQUIV FAILED\n");
        return 1;
    }
    printf("M11 EQUIV OK -- the observer classifies, bounds, tracks and stays "
           "PASSIVE\n");
    return 0;
}
