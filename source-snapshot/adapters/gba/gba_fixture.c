/* LUAport M1 -- adapters/gba/gba_fixture.c
 *
 * The globals and the three functions that the gpSP CORE references but that
 * only the FRONTEND ever defined. Under Plan A there is no frontend, so LUAport
 * owns them.
 *
 * THE RULE THIS FILE FOLLOWS
 * --------------------------
 * Every symbol below is here because a Tier 1 core file references it and no
 * Tier 1 core file defines it. Each one carries its reference site and its
 * upstream declaration, so the list can be audited rather than trusted. Nothing
 * is defined "just in case": an unnecessary definition would silently satisfy a
 * reference that ought to have failed the link, which is exactly how a leak
 * from an excluded file hides itself.
 *
 * The initial VALUES are taken from libretro.c so that behaviour is identical
 * to upstream's defaults, and each divergence -- there is one, netpacket_send
 * -- is called out.
 *
 * DELIBERATELY NOT DEFINED HERE: dynarec_enable.
 * gpsp/main.h:97 declares it, but its only core reference is
 * gpsp/savestate.c:156, which sits inside `#ifdef HAVE_DYNAREC`
 * (gpsp/savestate.c:155). M1 does not define HAVE_DYNAREC, so nothing should
 * ask for it. Leaving it undefined turns "a dynarec reference leaked into the
 * build" into a LINK ERROR instead of a silent success -- the definition would
 * be a tripwire disabled.
 *
 *   THIS TRIPWIRE IS NOW LIVE RATHER THAN THEORETICAL. gpsp/savestate.c is in
 *   the link (it supplies the five BSON readers that cpu.o, main.o,
 *   gba_memory.o and sound.o reference -- see M1_GPSP_STATE in the Makefile),
 *   so the ONLY translation unit that mentions dynarec_enable is now actually
 *   compiled. If HAVE_DYNAREC were ever defined by accident, that reference
 *   would go live and the link would fail loudly with an undefined
 *   dynarec_enable -- which is precisely the intended behaviour, and is a
 *   stronger guarantee than it was when savestate.c sat outside the build.
 *
 * TYPE DISCIPLINE: this file includes gpsp/common.h and NOT runtime/core.h.
 * The two disagree: common.h:100 typedefs u64 as `unsigned long long` while
 * core.h:30 uses `unsigned long`. Those are distinct types in C even though
 * both are 64-bit on this ABI, so a TU including both fails to compile on the
 * duplicate typedef. Keeping the two worlds in separate translation units is
 * the boundary that makes the whole M1 build work; apps/m1gpsp/main.c sits on
 * the other side of it and declares its handful of gpSP entry points by hand.
 */

#include "common.h"     /* gpSP types + main.h/cpu.h declarations            */

/* ------------------------------------------------------ boot / rendering --
 *
 * selected_boot_mode -- gpsp/main.h:98, referenced gpsp/cpu.cc:3575, 3583.
 *   init_cpu() branches on it to decide whether to start executing at the BIOS
 *   reset vector or to fake a post-BIOS state and jump straight into the ROM.
 *   boot_game matches libretro.c:120 and is the correct default for a core that
 *   is not required to have an official BIOS image.
 *
 *   M1 NOTE: the fixture calls reset_gba(), which calls init_cpu(), which reads
 *   this. It does NOT then execute anything -- see apps/m1gpsp/main.c.
 */
boot_mode selected_boot_mode = boot_game;

/* sprite_limit -- gpsp/main.h:99, referenced gpsp/video.cc:1567.
 *   Selects between the hardware-accurate per-scanline OBJ render-cycle cap and
 *   an unlimited one. 1 (= enforce the real limit) matches libretro.c:121. */
int sprite_limit = 1;

/* skip_next_frame -- gpsp/main.h:74, referenced gpsp/video.cc:2354.
 *   The frameskip gate inside update_scanline(). 0 = render every frame, which
 *   matches libretro.c:98. M1 never presents a frame, but the variable is read
 *   on the path reset_gba() prepares, so it must exist and must be defined. */
u32 skip_next_frame = 0;

/* ------------------------------------------------------ idle-loop skipping --
 *
 * These three are NOT dynarec state, despite the "translation_gate" name, and
 * they are not on M1's forbidden-symbol list. They are the per-game override
 * data loaded from the gbaover table:
 *
 *   idle_loop_target_pc       -- cpu.h:161. WRITTEN by gpsp/gba_memory.c:1724
 *                                and reset at :2997. READ BY THE INTERPRETER at
 *                                gpsp/cpu.cc:3063 and :3543, where a PC match
 *                                cuts the remaining cycles to zero so a
 *                                spin-wait loop does not burn the slice. This
 *                                is a genuine interpreter feature.
 *   translation_gate_targets  -- cpu.h:162. Written by gba_memory.c:1749, 1755,
 *                                1761 and reset at :2998.
 *   translation_gate_target_pc-- cpu.h:163, MAX_TRANSLATION_GATES = 8
 *                                (cpu.h:159). Written by gba_memory.c:1748,
 *                                1754, 1760.
 *
 * The latter two are written unconditionally by retained code and read only by
 * the dynarec. They are defined here because the retained code genuinely
 * assigns to them -- omitting them would not remove the work, it would only
 * break the link. Their cost is 4 + 32 = 36 bytes of .bss/.data.
 *
 * 0xFFFFFFFF is libretro.c:127's initial value and is the "no idle loop known"
 * sentinel; a plain 0 would be a valid PC and would fire the skip constantly.
 */
u32 idle_loop_target_pc = 0xFFFFFFFF;
u32 translation_gate_targets = 0;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];

/* ------------------------------------------------------------- netplay --
 *
 * netplay_client_id / netplay_num_clients -- gpsp/main.h:100.
 *   Referenced by gpsp/serial.c:70, 71, 170, 175 (device identity in the SIOCNT
 *   register image, and the serial IRQ cycle count) and by ~40 sites in
 *   gpsp/serial_proto.c. Both zero matches libretro.c:479.
 *
 *   Zero means "single player, I am the parent". That is the correct and only
 *   meaningful value for LUAport: there is no netplay transport on this target,
 *   so the serial subsystem must behave as an unlinked cable.
 */
u32 netplay_num_clients = 0;
u32 netplay_client_id   = 0;

/* netpacket_send -- declared locally by the two files that call it:
 *   gpsp/rfu.c:180 and gpsp/serial_proto.c:40, both as
 *       void netpacket_send(uint16_t client_id, const void *buf, size_t len);
 *   Call sites: rfu.c:190, 204, 224; serial_proto.c:135, 426.
 *
 * THIS IS THE ONE DELIBERATE BEHAVIOURAL DIVERGENCE FROM libretro.c.
 * Upstream (libretro.c:488) forwards to a RetroArch netpacket callback. There
 * is no such transport here and M1 is not authorised to open a socket, so this
 * is a no-op.
 *
 * A no-op is CORRECT rather than merely convenient: with netplay_num_clients
 * held at 0 the serial code is already running as an unlinked cable, so the
 * packets it would emit have no peer. Dropping them is what an unlinked cable
 * does. The alternative -- leaving it undefined and letting the linker fail --
 * would only mean excluding rfu.c and serial_proto.c from the closure, which
 * would understate the honest Tier 1 measurement.
 *
 * The parameters are consumed with (void) casts so that -Wall stays clean
 * without disabling the warning globally.
 */
void netpacket_send(uint16_t client_id, const void *buf, size_t len);

void netpacket_send(uint16_t client_id, const void *buf, size_t len)
{
    (void)client_id;
    (void)buf;
    (void)len;
}

/* netpacket_poll_receive -- declared by its only caller, gpsp/rfu.c:181, as
 *       void netpacket_poll_receive();
 *   Upstream definition: gpsp/libretro/libretro.c:483. Call site: rfu.c:884,
 *   inside rfu_update() when rfu_comstate == RFU_COMSTATE_WAITEVENT.
 *
 * SIGNATURE AND RETURN SEMANTICS, FROM SOURCE.
 *   Upstream is:
 *       void netpacket_poll_receive() {
 *         if (netpacket_pollrcv_fn_ptr)
 *           netpacket_pollrcv_fn_ptr();
 *       }
 *   It returns void -- it does NOT report whether a packet arrived. Its whole
 *   effect is a synchronous pump: it asks the frontend to drain its socket,
 *   and the frontend re-enters the core through netpacket_receive() for each
 *   packet it found. "No packet available" is therefore expressed by the pump
 *   simply not calling back, NOT by a return value.
 *
 * WHY AN EMPTY BODY IS THE EXACT NO-NETWORK BEHAVIOUR, NOT A STUB.
 *   netpacket_pollrcv_fn_ptr (libretro.c:481) is a static initialised to NULL
 *   and is assigned in exactly one place: netpacket_start() (libretro.c:494),
 *   which RetroArch invokes only when a netplay session actually begins.
 *   With no netplay session upstream's function evaluates `if (NULL)` and
 *   returns having done nothing. LUAport has no netplay transport at all, so
 *   that is permanently the case here, and an empty body is bit-for-bit the
 *   same observable behaviour -- not an approximation of it.
 *
 *   rfu.c handles this correctly on its own: rfu_update() continues to the
 *   timeout accounting below the call (rfu.c:887-888) and eventually reports a
 *   disconnect (rfu.c:894), which is what an RFU adapter with no peer should
 *   do. The REENTRANCY warning at rfu.c:877-883 is vacuous for us, because
 *   nothing can call back into rfu_net_receive.
 *
 *   This is the same ownership as netpacket_send above: frontend-owned, and
 *   deliberately resolved WITHOUT pulling libretro/RetroArch networking in.
 */
void netpacket_poll_receive(void);

void netpacket_poll_receive(void)
{
}

/* ------------------------------------------------- fast-forward override --
 *
 * set_fastforward_override -- declared extern by its only caller,
 *   gpsp/input.c:39, as `void set_fastforward_override(bool fastforward);`
 *   Upstream definition: gpsp/libretro/libretro.c:388. Call site: input.c:168.
 *
 * This symbol only became necessary when input.c entered the closure as part
 * of the BSON reader dependency chain (see M1_GPSP_STATE in the Makefile).
 *
 * WHY AN EMPTY BODY IS EXACT HERE TOO.
 *   Upstream's first statement is
 *       if (!libretro_supports_ff_override) return;
 *   and libretro_supports_ff_override is defined in input.c:23 initialised to
 *   false. The ONLY code that ever sets it true is libretro.c's environment
 *   probe (RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE), and libretro.c is
 *   excluded from M1. So the flag is false for the entire life of the M1
 *   image and upstream's function returns immediately every time. An empty
 *   body reproduces that precisely.
 *
 *   The guarded remainder builds a `struct retro_fastforwarding_override` and
 *   hands it to the environment callback -- pure frontend work with no core
 *   side effect, so nothing is lost by not performing it.
 *
 *   input.c:166-169 additionally only calls this when libretro_ff_enabled
 *   changes, and libretro_ff_enabled is itself gated on
 *   libretro_supports_ff_override (input.c:85, :96), so it is pinned false and
 *   the call site is unreachable in M1 regardless.
 */
void set_fastforward_override(bool fastforward);

void set_fastforward_override(bool fastforward)
{
    (void)fastforward;
}
