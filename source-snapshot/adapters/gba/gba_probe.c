/* LUAport M2 -- adapters/gba/gba_probe.c
 *
 * The gpSP-facing half of the M2 core-initialization diagnostic.
 *
 * WHAT THIS FILE IS
 * -----------------
 * M2 must prove that the gpSP interpreter core reaches a well-defined
 * initialized state -- not merely that reset_gba() returned. That means READING
 * a lot of gpSP state. This translation unit is the only place in the M2 build
 * that may do so, because it is the only place that includes gpsp/common.h.
 *
 * apps/m2gpsp/main.c sits on the other side of the boundary (runtime/core.h,
 * shim.h, boot.inc) and cannot include gpsp headers: common.h:100 typedefs u64
 * as `unsigned long long` while core.h:30 uses `unsigned long`, and a TU that
 * includes both fails on the duplicate typedef. See adapters/gba/gba_probe.h.
 *
 * gpsp/ IS NOT MODIFIED. Not one line. Everything below is read through either
 * an existing gpSP header declaration or a hand-written `extern` for a symbol
 * that is already a non-static global -- the same technique upstream itself uses
 * at gpsp/gba_memory.c:198 (`extern timer_type timer[4];`).
 *
 * WHAT IT DELIBERATELY DOES NOT DO
 * --------------------------------
 *   - does NOT call execute_arm() or update_gba()  -- zero GBA instructions
 *   - does NOT call update_scanline()              -- nothing is rendered
 *   - does NOT load or touch a BIOS                -- bios_rom is only compared
 *                                                     as a POINTER, never read
 *   - does NOT load a ROM, allocate gamepak blocks, or call load_gamepak*()
 *   - does NOT modify selected_boot_mode           -- stays boot_game
 *   - does NOT allocate                            -- no malloc anywhere here
 *
 * ASSERTION DISCIPLINE
 * --------------------
 * Only values that are GENUINE RESET INVARIANTS are asserted -- each one is
 * written by a specific line of init_memory/init_main/init_cpu/reset_sound/
 * gbp_reset, and that line is cited. State that merely happens to be zero
 * because it lives in .bss is NOT asserted as if reset had established it.
 *
 * Explicitly NOT asserted, and each for a reason proven from source:
 *   affine_reference_x/y   video.cc:126-127 are reset by video_reload_counters(),
 *                          which is reached only from main.c:206 inside
 *                          update_gba() -- never on the reset path.
 *   palette_ram_converted  absent from init_memory's memset list
 *                          (gba_memory.c:2421-2426); zero only via .bss.
 *   RFU / serial_proto     have no reset entry point called by reset_gba().
 *   cheat state            cheats.c:32-34 are .data/.bss initializers, not reset.
 *   gbc_sound_tick_step    sound.c:39 is `static` -- not externally observable
 *                          at all. The noise tables, set by the same function,
 *                          are the observable proxy.
 *   sound_buffer           sound.c:36 is `static` -- likewise unobservable.
 */

#include "common.h"      /* pulls cpu.h, gba_memory.h, video.h, sound.h,
                            main.h, serial.h -- see common.h:188-196          */

#include "gba_probe.h"

/* ---------------------------------------------------- hand-declared state --
 *
 * Each of these is a NON-STATIC global in gpSP that simply has no extern
 * declaration in any gpSP header. Declaring them here is reading, not
 * modifying; the definitions stay exactly where upstream put them.
 *
 *   timer[4]           defined gpsp/main.c:23   (type from main.h:37-46)
 *   video_count        defined gpsp/main.c:28
 *   noise_table15/7    defined gpsp/sound.c:186-187
 *   wave_samples       defined gpsp/sound.c:184
 *   dma_bus_val        defined gpsp/gba_memory.c:362
 *   backup_type_reset  defined gpsp/gba_memory.c:415
 *   flash_mode         defined gpsp/gba_memory.c:416
 *   flash_bank_num     defined gpsp/gba_memory.c:418
 *   eeprom_mode        defined gpsp/gba_memory.c:532
 *   rtc_state          defined gpsp/gba_memory.c:1297
 */
extern timer_type timer[4];
extern s32 video_count;
extern u32 noise_table15[1024];
extern u32 noise_table7[4];
extern s8  wave_samples[64];
extern u32 dma_bus_val;
extern u32 backup_type_reset;
extern u32 flash_mode;
extern u32 flash_bank_num;
extern u32 eeprom_mode;
extern u32 rtc_state;

/* RTC_DISABLED is a #define local to gpsp/gba_memory.c:1272 and is therefore
 * NOT visible from here. Its value is 0. Asserting the literal 0 with the
 * citation is honest; inventing a duplicate #define would risk silently
 * diverging from upstream if that value ever changed. */
#define M2_RTC_DISABLED_VALUE 0u

/* The memory_map_read[] index for an address. map_region/map_null/map_vram
 * (gba_memory.c:2265-2286) all bucket the map in 32 KB (0x8000) units. */
#define MAP_IDX(addr) ((addr) / 0x8000)

/* Remembered so the video probe can assert the pointer is still OURS rather
 * than merely non-NULL -- a stale or overwritten pointer would otherwise pass. */
static unsigned short *m2_screen_installed;

/* --------------------------------------------------------------- actions -- */

void m2_set_screen(unsigned short *pixels)
{
    m2_screen_installed = pixels;
    gba_screen_pixels   = (u16 *)pixels;
}

void m2_call_init_sound(void)
{
    init_sound();
}

void m2_call_reset_gba(void)
{
    reset_gba();
}

/* ------------------------------------------------------------- helpers --- */

/* OR-reduce, so "was this table ever written?" is a single comparison. A plain
 * "entry 0 is non-zero" test would be brittle: init_noise_table's output may
 * legitimately contain zero entries. */
static u32 or_u32(const u32 *p, u32 n)
{
    u32 acc = 0, i;
    for (i = 0; i < n; i++)
        acc |= p[i];
    return acc;
}

/* Spot-check rather than full scan: first, middle and last element. The memsets
 * in init_memory (gba_memory.c:2421-2426) are unconditional whole-array calls,
 * so a partial failure is not a mode these can fail in -- and scanning 1.28 MB
 * on the console would cost time while proving nothing extra. */
static int spot_zero_u8(const u8 *p, u32 n)
{
    return p[0] == 0 && p[n / 2] == 0 && p[n - 1] == 0;
}

static int spot_zero_u16(const u16 *p, u32 n)
{
    return p[0] == 0 && p[n / 2] == 0 && p[n - 1] == 0;
}

/* ---------------------------------------------------------- pre-checks --- */

unsigned int m2_probe_pre(void)
{
    unsigned int f = 0;

    /* video.cc:25 initialises gba_screen_pixels to NULL and nothing in the
     * reset path touches it, so it must still be NULL before we install ours. */
    if (gba_screen_pixels != 0)
        f |= M2_PRE_SCREEN_NULL;

    /* The BEFORE half of the init_sound proof: both tables must be untouched.
     * Only init_sound() (sound.c:606-607) ever fills them, and its sole caller
     * is the excluded libretro.c:685. */
    if (or_u32(noise_table15, 1024) != 0)
        f |= M2_PRE_NOISE15_ZERO;
    if (or_u32(noise_table7, 4) != 0)
        f |= M2_PRE_NOISE7_ZERO;

    return f;
}

/* ----------------------------------------------------------------- CPU --- */

unsigned int m2_probe_cpu(void)
{
    unsigned int f = 0;
    u32 i;

    /* init_cpu(), cpu.cc:3583-3587, boot_game branch. */
    if (reg[REG_PC]   != 0x08000000u) f |= M2_CPU_PC;
    if (reg[REG_SP]   != 0x03007F00u) f |= M2_CPU_SP;
    if (reg[REG_CPSR] != 0x0000001Fu) f |= M2_CPU_CPSR;
    if (reg[CPU_MODE] != (u32)MODE_SYSTEM) f |= M2_CPU_MODE;

    /* cpu.cc:3571-3572, set immediately after the memset. */
    if (reg[CPU_HALT_STATE]   != (u32)CPU_ACTIVE) f |= M2_CPU_HALT;
    if (reg[REG_SLEEP_CYCLES] != 0u)              f |= M2_CPU_SLEEP;

    /* cpu.cc:3568-3569 -- explicit loop over all six saved PSRs. */
    for (i = 0; i < 6; i++)
        if (spsr[i] != 0x00000010u) { f |= M2_CPU_SPSR; break; }

    /* cpu.cc:3597-3600. NOTE the REG_MODE macro (cpu.h:41) masks the mode with
     * & 0xF before indexing, so MODE_IRQ (0x11) selects reg_mode[1], not [17].
     * reg_mode is only [7][7]; indexing by the raw mode value would be out of
     * bounds. The masked indices are used here for exactly that reason. */
    if (reg_mode[MODE_USER       & 0xF][5] != 0x03007F00u) f |= M2_CPU_SP_USER;
    if (reg_mode[MODE_IRQ        & 0xF][5] != 0x03007FA0u) f |= M2_CPU_SP_IRQ;
    if (reg_mode[MODE_FIQ        & 0xF][5] != 0x03007FA0u) f |= M2_CPU_SP_FIQ;
    if (reg_mode[MODE_SUPERVISOR & 0xF][5] != 0x03007FE0u) f |= M2_CPU_SP_SVC;

    /* THE TRAP -- see gba_probe.h. init_memory sets 0xe129f000 at
     * gba_memory.c:2453, then init_cpu's memset (cpu.cc:3566) clears reg[0..31]
     * and REG_BUS_VALUE is index 19. Zero is the CORRECT post-reset value. */
    if (reg[REG_BUS_VALUE] != 0u)
        f |= M2_CPU_BUS_VALUE;

    /* r0..r12 and LR are cleared by the same memset and never written again on
     * the boot_game path. */
    for (i = 0; i <= 12; i++)
        if (reg[i] != 0u) { f |= M2_CPU_GPREGS; break; }
    if (reg[REG_LR] != 0u)
        f |= M2_CPU_GPREGS;

    return f;
}

/* -------------------------------------------------------------- memory --- */

unsigned int m2_probe_memory(void)
{
    unsigned int f = 0;

    /* init_memory's map_region/map_null calls, gba_memory.c:2410-2419.
     * The mirror arithmetic in map_region (gba_memory.c:2270) adds
     * (map_offset % mirror_blocks) * 0x8000; for each base index checked here
     * that term is zero, so the first entry equals the region base. */
    if (memory_map_read[MAP_IDX(0x0000000)] != bios_rom)        f |= M2_MEM_MAP_BIOS;
    if (memory_map_read[MAP_IDX(0x2000000)] != ewram)           f |= M2_MEM_MAP_EWRAM;
    if (memory_map_read[MAP_IDX(0x3000000)] != &iwram[0x8000])  f |= M2_MEM_MAP_IWRAM;
    if (memory_map_read[MAP_IDX(0x4000000)] != (u8 *)io_registers) f |= M2_MEM_MAP_IO;
    if (memory_map_read[MAP_IDX(0x6000000)] != vram)            f |= M2_MEM_MAP_VRAM;
    if (memory_map_read[MAP_IDX(0x1000000)] != 0)               f |= M2_MEM_MAP_NULL1;
    if (memory_map_read[MAP_IDX(0x5000000)] != 0)               f |= M2_MEM_MAP_NULL5;

    /* ROM BOUNDARY. The gamepak window is absent from init_memory's mapping
     * list entirely, so this stays NULL unless a ROM has been loaded. A
     * non-NULL here would mean M2 had crossed into M4 territory. */
    if (memory_map_read[MAP_IDX(0x8000000)] != 0)               f |= M2_MEM_MAP_NOROM;

    /* The seven I/O registers init_memory writes, gba_memory.c:2428-2434. */
    if (read_ioreg(REG_DISPCNT) != 0x80u)   f |= M2_MEM_DISPCNT;
    if (read_ioreg(REG_P1)      != 0x3FFu)  f |= M2_MEM_P1;
    if (read_ioreg(REG_BG2PA)   != 0x100u)  f |= M2_MEM_BG2PA;
    if (read_ioreg(REG_BG2PD)   != 0x100u)  f |= M2_MEM_BG2PD;
    if (read_ioreg(REG_BG3PA)   != 0x100u)  f |= M2_MEM_BG3PA;
    if (read_ioreg(REG_BG3PD)   != 0x100u)  f |= M2_MEM_BG3PD;

    /* Backup / save-hardware state, gba_memory.c:2438-2451. backup_type is
     * compared against backup_type_reset rather than a literal, because that is
     * literally what line 2438 assigns -- a literal would encode BACKUP_UNKN
     * and break if the reset default were ever changed. */
    if (backup_type != backup_type_reset)          f |= M2_MEM_BACKUP;
    if (eeprom_size != (u32)EEPROM_512_BYTE ||
        eeprom_mode != (u32)EEPROM_BASE_MODE)      f |= M2_MEM_EEPROM;
    if (flash_mode  != (u32)FLASH_BASE_MODE ||
        flash_bank_num != 0u)                      f |= M2_MEM_FLASH;
    if (rtc_state   != M2_RTC_DISABLED_VALUE)      f |= M2_MEM_RTC;
    if (dma_bus_val != 0u)                         f |= M2_MEM_DMABUS;

    /* The six unconditional memsets, gba_memory.c:2421-2426. */
    if (!spot_zero_u8 (ewram,       sizeof(ewram))       ||
        !spot_zero_u8 (iwram,       sizeof(iwram))       ||
        !spot_zero_u8 (vram,        sizeof(vram))        ||
        !spot_zero_u16(oam_ram,     512)                 ||
        !spot_zero_u16(palette_ram, 512))
        f |= M2_MEM_RAMZERO;

    return f;
}

/* --------------------------------------------------------------- video --- */

unsigned int m2_probe_video(void)
{
    unsigned int f = 0;

    /* Ours specifically, not merely non-NULL. */
    if (gba_screen_pixels != (u16 *)m2_screen_installed || m2_screen_installed == 0)
        f |= M2_VID_SCREENPTR;

    /* init_main(), main.c:111-112. */
    if (video_count    != 960) f |= M2_VID_VIDEOCOUNT;
    if (execute_cycles != 960u) f |= M2_VID_EXECCYCLES;

    /* Frontend-owned, adapters/gba/gba_fixture.c:70. Checked because the
     * render path reads it and a non-zero value would silently skip frames
     * from the first frame M6 ever draws. */
    if (skip_next_frame != 0u) f |= M2_VID_SKIPFRAME;

    return f;
}

/* --------------------------------------------------------------- sound --- */

unsigned int m2_probe_sound(void)
{
    unsigned int f = 0;
    u32 i;

    /* reset_sound(), sound.c:562-599. */
    if (sound_on != 0u) f |= M2_SND_ON;

    for (i = 0; i < 2; i++) {
        const direct_sound_struct *ds = &direct_sound_channel[i];
        if (ds->status       != (u32)DIRECT_SOUND_INACTIVE) f |= M2_SND_DS_STATUS;
        if (ds->volume_halve != 1u)                          f |= M2_SND_DS_VOLHALVE;
        if (ds->fifo_top != 0u || ds->fifo_base != 0u ||
            ds->buffer_index != 0u)                          f |= M2_SND_DS_FIFO;
    }

    for (i = 0; i < 4; i++) {
        const gbc_sound_struct *gs = &gbc_sound_channel[i];
        if (gs->status           != (u32)GBC_SOUND_INACTIVE) f |= M2_SND_GBC_STATUS;
        if (gs->sample_table_idx != 2u)                      f |= M2_SND_GBC_TABLEIDX;
        if (gs->active_flag      != 0u)                      f |= M2_SND_GBC_ACTIVE;
    }

    if (gbc_sound_master_volume_left  != 0u ||
        gbc_sound_master_volume_right != 0u ||
        gbc_sound_master_volume       != 0u)
        f |= M2_SND_MASTERVOL;

    if (gbc_sound_buffer_index != 0u || gbc_sound_last_cpu_ticks != 0u)
        f |= M2_SND_GBC_BUFIDX;

    for (i = 0; i < 64; i++)
        if (wave_samples[i] != 0) { f |= M2_SND_WAVE_ZERO; break; }

    /* THE M2 PROOF. These two are the only assertions here that reset_gba()
     * alone cannot satisfy -- they require m2_call_init_sound(). Combined with
     * m2_probe_pre() having observed both tables zero beforehand, a pass is
     * positive evidence that init_sound() ran and did its work. */
    if (or_u32(noise_table15, 1024) == 0) f |= M2_SND_NOISE15;
    if (or_u32(noise_table7,  4)    == 0) f |= M2_SND_NOISE7;

    return f;
}

/* ---------------------------------------------------------- timers/DMA --- */

unsigned int m2_probe_timers_dma(void)
{
    unsigned int f = 0;
    u32 i;

    /* init_main(), main.c:96-107. */
    for (i = 0; i < 4; i++) {
        if (timer[i].status   != (u32)TIMER_INACTIVE) f |= M2_TIM_STATUS;
        if (timer[i].prescale != 0u)                  f |= M2_TIM_PRESCALE;
        if (timer[i].irq      != 0u)                  f |= M2_TIM_IRQ;
        if (timer[i].reload   != 0x10000u)            f |= M2_TIM_RELOAD;
        if (timer[i].count    != 0x10000)             f |= M2_TIM_COUNT;
        if (timer[i].frequency_step != 0)             f |= M2_TIM_FREQSTEP;
    }

    /* The asymmetric override, main.c:106-107. */
    if (timer[0].direct_sound_channels != (u32)TIMER_DS_CHANNEL_BOTH)
        f |= M2_TIM0_DS;
    if (timer[1].direct_sound_channels != (u32)TIMER_DS_CHANNEL_NONE)
        f |= M2_TIM1_DS;
    for (i = 2; i < 4; i++)
        if (timer[i].direct_sound_channels != (u32)TIMER_DS_CHANNEL_NONE)
            f |= M2_TIM23_DS;

    /* init_memory(), gba_memory.c:2397-2407. */
    for (i = 0; i < DMA_CHAN_CNT; i++) {
        if (dma[i].start_type   != (u32)DMA_INACTIVE)         f |= M2_DMA_START;
        if (dma[i].irq          != (u32)DMA_NO_IRQ)           f |= M2_DMA_IRQ;
        if (dma[i].length_type  != (u32)DMA_16BIT)            f |= M2_DMA_LENTYPE;
        if (dma[i].repeat_type  != (u32)DMA_NO_REPEAT)        f |= M2_DMA_REPEAT;
        if (dma[i].direct_sound_channel != (u32)DMA_NO_DIRECT_SOUND)
            f |= M2_DMA_DSCHAN;
        if (dma[i].source_address != 0u || dma[i].dest_address != 0u ||
            dma[i].length != 0u ||
            dma[i].source_direction != 0u || dma[i].dest_direction != 0u)
            f |= M2_DMA_ADDR;
    }

    /* init_main(), main.c:109-110. */
    if (frame_counter != 0u) f |= M2_TIM_FRAMECOUNTER;
    if (cpu_ticks     != 0u) f |= M2_TIM_CPUTICKS;

    return f;
}

/* -------------------------------------------------------------- serial --- */

unsigned int m2_probe_serial(void)
{
    unsigned int f = 0;

    /* gbp_reset(), gbp.c:70-75, observed through gpSP's own accessor
     * gbp_get_state() (gbp.c:58): seq_n | rumble<<4 | allow_rumble<<5. */
    if (gbp_get_state() != 0u) f |= M2_SER_GBP_STATE;

    /* Frontend-owned, gba_fixture.c:113-114. Zero means "single player, I am
     * the parent" -- correct unlinked-cable behaviour. */
    if (netplay_num_clients != 0u) f |= M2_SER_NETPLAY_CLIENTS;
    if (netplay_client_id   != 0u) f |= M2_SER_NETPLAY_ID;

    /* init_memory(), gba_memory.c:2434. */
    if (read_ioreg(REG_RCNT) != 0x8000u) f |= M2_SER_RCNT;

    return f;
}

/* ------------------------------------------------------------ raw reads --- */

unsigned int m2_probe_read(unsigned int sel)
{
    switch (sel) {
    case M2_RD_PC:           return reg[REG_PC];
    case M2_RD_SP:           return reg[REG_SP];
    case M2_RD_CPSR:         return reg[REG_CPSR];
    case M2_RD_MODE:         return reg[CPU_MODE];
    case M2_RD_BUS_VALUE:    return reg[REG_BUS_VALUE];
    case M2_RD_DISPCNT:      return read_ioreg(REG_DISPCNT);
    case M2_RD_P1:           return read_ioreg(REG_P1);
    case M2_RD_RCNT:         return read_ioreg(REG_RCNT);
    case M2_RD_VIDEO_COUNT:  return (u32)video_count;
    case M2_RD_EXEC_CYCLES:  return execute_cycles;
    case M2_RD_TIMER0_DS:    return timer[0].direct_sound_channels;
    case M2_RD_GBP_STATE:    return gbp_get_state();
    case M2_RD_NOISE15_OR:   return or_u32(noise_table15, 1024);
    case M2_RD_NOISE7_OR:    return or_u32(noise_table7, 4);
    case M2_RD_BACKUP_TYPE:  return backup_type;
    case M2_RD_SOUND_ON:     return sound_on;
    case M2_RD_SCREEN_SET:
        return (gba_screen_pixels != 0 &&
                gba_screen_pixels == (u16 *)m2_screen_installed) ? 1u : 0u;
    default:                 return 0u;
    }
}
