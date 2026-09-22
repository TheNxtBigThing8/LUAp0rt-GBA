/* LUAport M2 -- adapters/gba/gba_probe.h
 *
 * The NARROW, TYPE-SAFE BOUNDARY between the M2 fixture and the gpSP core.
 *
 * WHY THIS HEADER EXISTS AT ALL
 * -----------------------------
 * gpsp/common.h:100 typedefs u64 as `unsigned long long`; runtime/core.h:30
 * typedefs it as `unsigned long`. Both are 64 bits on this ABI but they are
 * DISTINCT TYPES in C, so any translation unit that includes both fails to
 * compile on the duplicate typedef. That is why apps/m1gpsp/main.c hand-declares
 * its three gpSP entry points instead of including gpsp headers.
 *
 * M2 needs to READ far more gpSP state than M1 did -- register file, memory map,
 * I/O registers, timers, DMA, sound channels -- and hand-declaring all of it in
 * the fixture would be both unreadable and fragile. So M2 puts the gpSP-facing
 * code in its own translation unit (adapters/gba/gba_probe.c, which includes
 * gpsp/common.h and NOTHING from runtime/) and exposes it through this header.
 *
 * THE TYPE RULE THIS HEADER FOLLOWS, AND WHY IT IS SAFE
 * ----------------------------------------------------
 * Every declaration below uses ONLY `unsigned int` and `unsigned short`, spelled
 * out rather than via u32/u16. Two consequences, both deliberate:
 *
 *   1. The header is SELF-CONTAINED. It depends on no typedef from either world,
 *      so it can be included from either side in any order without dragging in a
 *      conflicting definition. That is what makes the boundary actually hold.
 *   2. The types are IDENTICAL, not merely compatible. u32 is `unsigned int` and
 *      u16 is `unsigned short` in BOTH gpsp/common.h and runtime/core.h, so the
 *      prototypes the two sides see are the same types. Only u64/s64 disagree,
 *      and nothing here uses a 64-bit type.
 *
 * NO POINTER TO A gpSP STRUCT AND NO gpSP ENUM CROSSES THIS BOUNDARY. Results
 * come back as bitmasks of plain unsigned int, and raw values come back one at a
 * time through m2_probe_read(). A struct would reintroduce exactly the coupling
 * the boundary exists to prevent.
 *
 * BITMASK CONVENTION: A SET BIT IS A FAILED ASSERTION
 * ---------------------------------------------------
 * Every m2_probe_*() returns 0 when everything it checks holds. A set bit means
 * that specific assertion FAILED. This orientation is chosen so that the success
 * case is a single `== 0` test and so that a newly added assertion cannot
 * silently read as "pass" if the fixture forgets to name it.
 *
 * gpsp/ IS NOT MODIFIED BY ANY OF THIS. Every symbol read below is either
 * already declared in a gpSP header or is a non-static global that this file's
 * .c declares extern by hand -- the same technique gpsp/gba_memory.c:198 already
 * uses for `extern timer_type timer[4];`.
 */

#ifndef LUAPORT_GBA_PROBE_H
#define LUAPORT_GBA_PROBE_H

/* ------------------------------------------------------------- actions -- */

/* Point gpSP's output surface at a caller-owned buffer.
 *
 * The buffer must be GBA_SCREEN_PITCH * (GBA_SCREEN_HEIGHT + 1) 16-bit pixels
 * = 240 * 161 = 38,640 entries (77,280 bytes). The trailing scanline is NOT
 * padding: gpsp/common.h:118-119 reserves it for winobj rendering, so a 240*160
 * buffer would be overrun by the window code.
 *
 * The pointer is remembered so m2_probe_video() can assert that
 * gba_screen_pixels still holds exactly what we installed.
 *
 * NOTHING IS RENDERED INTO IT DURING M2. update_scanline() is unreachable
 * because execute_arm()/update_gba() are never called. */
void m2_set_screen(unsigned short *pixels);

/* init_sound() -- gpsp/sound.h:148, defined gpsp/sound.c:601.
 *
 * THIS IS THE ONE PIECE OF GENUINELY NEW INITIALIZATION IN M2, and the reason
 * the milestone is not purely observational.
 *
 * reset_gba() (gpsp/main.c:309-316) calls reset_sound(), NOT init_sound(). The
 * three things init_sound() does that reset_sound() does not are:
 *     gbc_sound_tick_step = 256          (sound.c:604)
 *     init_noise_table(noise_table15, 32767, 14)   (sound.c:606)
 *     init_noise_table(noise_table7,     127,  6)  (sound.c:607)
 * Its only caller anywhere in the tree is gpsp/libretro/libretro.c:685, and
 * libretro.c is excluded from the LUAport build. So without this call the two
 * noise tables and the GBC tick step stay zero forever: channel 4 would emit
 * silence and the GBC envelope/length clock would never advance.
 *
 * CALLED BEFORE reset_gba(), deliberately. init_sound() ENDS by calling
 * reset_sound() (sound.c:609), so calling it first and letting reset_gba() run
 * its own reset_sound() afterwards is idempotent and leaves reset_gba()'s
 * ordering untouched. It also mirrors upstream, where init_sound() runs in
 * retro_init long before retro_load_game's reset. */
void m2_call_init_sound(void);

/* reset_gba() -- gpsp/main.h:86, defined gpsp/main.c:309.
 * Calls gbp_reset, init_memory, init_main, init_cpu, reset_sound. Executes ZERO
 * emulated ARM instructions: all five callees are pure state assignment. */
void m2_call_reset_gba(void);

/* ------------------------------------------------------------- probes --- */

unsigned int m2_probe_pre(void);         /* before init_sound/reset_gba      */
unsigned int m2_probe_cpu(void);
unsigned int m2_probe_memory(void);
unsigned int m2_probe_video(void);
unsigned int m2_probe_sound(void);
unsigned int m2_probe_timers_dma(void);
unsigned int m2_probe_serial(void);

/* One raw value at a time, for the diagnostic log. Selector constants below.
 * Returns 0 for an unknown selector. */
unsigned int m2_probe_read(unsigned int sel);

/* ---- pre-check bits (m2_probe_pre) --------------------------------------
 * Establishes the BEFORE half of the init_sound proof. Without this, a passing
 * noise-table check after init_sound() could not distinguish "init_sound ran"
 * from "the tables were somehow already populated". */
#define M2_PRE_SCREEN_NULL     (1u << 0)  /* gba_screen_pixels == NULL       */
#define M2_PRE_NOISE15_ZERO    (1u << 1)  /* noise_table15 entirely zero     */
#define M2_PRE_NOISE7_ZERO     (1u << 2)  /* noise_table7 entirely zero      */

/* ---- CPU bits (m2_probe_cpu) --------------------------------------------
 * All from init_cpu(), gpsp/cpu.cc:3563-3601, on the selected_boot_mode ==
 * boot_game branch (adapters/gba/gba_fixture.c:59). */
#define M2_CPU_PC              (1u << 0)  /* reg[REG_PC]   == 0x08000000     */
#define M2_CPU_SP              (1u << 1)  /* reg[REG_SP]   == 0x03007F00     */
#define M2_CPU_CPSR            (1u << 2)  /* reg[REG_CPSR] == 0x0000001F     */
#define M2_CPU_MODE            (1u << 3)  /* reg[CPU_MODE] == MODE_SYSTEM    */
#define M2_CPU_HALT            (1u << 4)  /* reg[CPU_HALT_STATE]==CPU_ACTIVE */
#define M2_CPU_SLEEP           (1u << 5)  /* reg[REG_SLEEP_CYCLES] == 0      */
#define M2_CPU_SPSR            (1u << 6)  /* spsr[0..5] all == 0x00000010    */
#define M2_CPU_SP_USER         (1u << 7)  /* REG_MODE(MODE_USER)[5]          */
#define M2_CPU_SP_IRQ          (1u << 8)  /* REG_MODE(MODE_IRQ)[5]           */
#define M2_CPU_SP_FIQ          (1u << 9)  /* REG_MODE(MODE_FIQ)[5]           */
#define M2_CPU_SP_SVC          (1u << 10) /* REG_MODE(MODE_SUPERVISOR)[5]    */
/* THE TRAP. init_memory() sets reg[REG_BUS_VALUE] = 0xe129f000 at
 * gba_memory.c:2453, but reset_gba() calls init_cpu() AFTERWARDS, and init_cpu
 * does memset(reg, 0, REG_USERDEF * sizeof(u32)) at cpu.cc:3566. REG_BUS_VALUE
 * is 19 (cpu.h:87) and REG_USERDEF is 32 (cpu.h:105), so index 19 is INSIDE the
 * cleared range. The value is therefore 0 after reset_gba(), NOT 0xe129f000.
 * Asserting the "obvious" 0xe129f000 would fail on correct code. */
#define M2_CPU_BUS_VALUE       (1u << 11) /* reg[REG_BUS_VALUE] == 0         */
#define M2_CPU_GPREGS          (1u << 12) /* reg[0..12] and reg[REG_LR] == 0 */

/* ---- memory bits (m2_probe_memory) --------------------------------------
 * From init_memory(), gpsp/gba_memory.c:2393-2454. */
#define M2_MEM_MAP_BIOS        (1u << 0)  /* memory_map_read[0] == bios_rom  */
#define M2_MEM_MAP_EWRAM       (1u << 1)
#define M2_MEM_MAP_IWRAM       (1u << 2)
#define M2_MEM_MAP_IO          (1u << 3)
#define M2_MEM_MAP_VRAM        (1u << 4)
#define M2_MEM_MAP_NULL1       (1u << 5)  /* 0x1000000 region NULL           */
#define M2_MEM_MAP_NULL5       (1u << 6)  /* 0x5000000 region NULL           */
/* ROM BOUNDARY EVIDENCE, not a reset invariant. init_memory() deliberately does
 * NOT map the 0x8000000 gamepak window (it is absent from gba_memory.c:2410-2419),
 * so a NULL here is positive proof that no ROM is mapped -- which is exactly the
 * M4 boundary M2 must not cross. Documented as absence-of-ROM, not as something
 * reset_gba() actively set. */
#define M2_MEM_MAP_NOROM       (1u << 7)  /* gamepak window still NULL       */
#define M2_MEM_DISPCNT         (1u << 8)  /* 0x80, forced blank              */
#define M2_MEM_P1              (1u << 9)  /* 0x3FF, all keys released        */
#define M2_MEM_BG2PA           (1u << 10) /* 0x100, identity affine          */
#define M2_MEM_BG2PD           (1u << 11)
#define M2_MEM_BG3PA           (1u << 12)
#define M2_MEM_BG3PD           (1u << 13)
#define M2_MEM_BACKUP          (1u << 14) /* backup_type == backup_type_reset*/
#define M2_MEM_EEPROM          (1u << 15) /* size 512B, mode BASE            */
#define M2_MEM_FLASH           (1u << 16) /* mode BASE, bank 0               */
#define M2_MEM_RTC             (1u << 17) /* rtc_state == RTC_DISABLED (0)   */
#define M2_MEM_DMABUS          (1u << 18) /* dma_bus_val == 0                */
#define M2_MEM_RAMZERO         (1u << 19) /* RAM spot-checks all zero        */

/* ---- video bits (m2_probe_video) ---------------------------------------- */
#define M2_VID_SCREENPTR       (1u << 0)  /* gba_screen_pixels == ours       */
#define M2_VID_VIDEOCOUNT      (1u << 1)  /* video_count    == 960           */
#define M2_VID_EXECCYCLES      (1u << 2)  /* execute_cycles == 960           */
#define M2_VID_SKIPFRAME       (1u << 3)  /* skip_next_frame == 0            */

/* ---- sound bits (m2_probe_sound) ---------------------------------------- */
#define M2_SND_ON              (1u << 0)  /* sound_on == 0                   */
#define M2_SND_DS_STATUS       (1u << 1)  /* DIRECT_SOUND_INACTIVE           */
#define M2_SND_DS_VOLHALVE     (1u << 2)  /* volume_halve == 1               */
#define M2_SND_DS_FIFO         (1u << 3)  /* fifo base/top/index == 0        */
#define M2_SND_GBC_STATUS      (1u << 4)  /* GBC_SOUND_INACTIVE              */
#define M2_SND_GBC_TABLEIDX    (1u << 5)  /* sample_table_idx == 2           */
#define M2_SND_GBC_ACTIVE      (1u << 6)  /* active_flag == 0                */
#define M2_SND_MASTERVOL       (1u << 7)  /* all three master volumes == 0   */
#define M2_SND_GBC_BUFIDX      (1u << 8)  /* buffer index / last ticks == 0  */
#define M2_SND_WAVE_ZERO       (1u << 9)  /* wave_samples all zero           */
/* THE M2 PROOF BITS. These are the only assertions in the entire milestone that
 * would FAIL without m2_call_init_sound(). They are the milestone's substance. */
#define M2_SND_NOISE15         (1u << 10) /* noise_table15 populated         */
#define M2_SND_NOISE7          (1u << 11) /* noise_table7 populated          */

/* ---- timer/DMA bits (m2_probe_timers_dma) -------------------------------
 * Timers from init_main(), gpsp/main.c:93-112. DMA from init_memory(),
 * gpsp/gba_memory.c:2397-2407. */
#define M2_TIM_STATUS          (1u << 0)  /* all four TIMER_INACTIVE         */
#define M2_TIM_PRESCALE        (1u << 1)
#define M2_TIM_IRQ             (1u << 2)
#define M2_TIM_RELOAD          (1u << 3)  /* reload == 0x10000               */
#define M2_TIM_COUNT           (1u << 4)  /* count  == 0x10000               */
#define M2_TIM_FREQSTEP        (1u << 5)
/* The deliberate asymmetry in init_main(): the loop sets all four channels to
 * TIMER_DS_CHANNEL_NONE, then lines 106-107 override 0 and 1. timer[0] == BOTH
 * is a strong, non-obvious invariant -- a wrong value proves the override lines
 * did not run, which the uniform NONE case would hide. */
#define M2_TIM0_DS             (1u << 6)  /* timer[0] == DS_CHANNEL_BOTH (3) */
#define M2_TIM1_DS             (1u << 7)  /* timer[1] == DS_CHANNEL_NONE (0) */
#define M2_TIM23_DS            (1u << 8)  /* timers 2,3 == DS_CHANNEL_NONE   */
#define M2_DMA_START           (1u << 9)  /* DMA_INACTIVE                    */
#define M2_DMA_IRQ             (1u << 10) /* DMA_NO_IRQ                      */
#define M2_DMA_LENTYPE         (1u << 11) /* DMA_16BIT                       */
#define M2_DMA_REPEAT          (1u << 12) /* DMA_NO_REPEAT                   */
#define M2_DMA_DSCHAN          (1u << 13) /* DMA_NO_DIRECT_SOUND             */
#define M2_DMA_ADDR            (1u << 14) /* addresses/length/directions 0   */
#define M2_TIM_FRAMECOUNTER    (1u << 15) /* frame_counter == 0              */
#define M2_TIM_CPUTICKS        (1u << 16) /* cpu_ticks == 0                  */

/* ---- serial bits (m2_probe_serial) --------------------------------------
 * gbp_get_state() is gpSP's OWN accessor (gpsp/gbp.c:58, declared serial.h:65),
 * used here rather than reaching at gbp_seq_n/gbp_rumble/gbp_allow_rumble
 * directly -- which keeps the assertion inside a supported interface and needs
 * no new extern. After gbp_reset() (gbp.c:70-75) all three pack to 0. */
#define M2_SER_GBP_STATE       (1u << 0)  /* gbp_get_state() == 0            */
#define M2_SER_NETPLAY_CLIENTS (1u << 1)  /* netplay_num_clients == 0        */
#define M2_SER_NETPLAY_ID      (1u << 2)  /* netplay_client_id   == 0        */
#define M2_SER_RCNT            (1u << 3)  /* RCNT == 0x8000                  */

/* ---- m2_probe_read selectors -------------------------------------------- */
#define M2_RD_PC               0u
#define M2_RD_SP               1u
#define M2_RD_CPSR             2u
#define M2_RD_MODE             3u
#define M2_RD_BUS_VALUE        4u
#define M2_RD_DISPCNT          5u
#define M2_RD_P1               6u
#define M2_RD_RCNT             7u
#define M2_RD_VIDEO_COUNT      8u
#define M2_RD_EXEC_CYCLES      9u
#define M2_RD_TIMER0_DS        10u
#define M2_RD_GBP_STATE        11u
#define M2_RD_NOISE15_OR       12u  /* OR of all 1024 entries               */
#define M2_RD_NOISE7_OR        13u  /* OR of all 4 entries                  */
#define M2_RD_BACKUP_TYPE      14u
#define M2_RD_SOUND_ON         15u
#define M2_RD_SCREEN_SET       16u  /* 1 if gba_screen_pixels == ours       */

#endif /* LUAPORT_GBA_PROBE_H */
