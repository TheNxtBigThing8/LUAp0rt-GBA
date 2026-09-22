#ifndef LUAPORT_NET_H
#define LUAPORT_NET_H

#include "core.h"

/* =============================================================================
 * LUAport M18-0 -- runtime/net.h
 *
 * THE NATIVE UDP CAPABILITY PROBE. NOT A NETPLAY LAYER.
 * =============================================================================
 *
 * WHAT THIS FILE IS FOR, STATED NARROWLY
 * --------------------------------------
 * M18-0 answers exactly two questions on ONE console, and this header is sized
 * to those two questions and nothing else:
 *
 *   U1  Can the NATIVE C payload resolve and call the BSD socket primitives
 *       through the established SYM()/native_call mechanism?
 *   U3  Can it create and bind a UDP socket WITHOUT loading libSceNet.sprx?
 *
 * Everything the M18 PLAN describes above those two questions -- peer tables,
 * the LNP1 transport header, generations, sequence numbers, RTT, reliability,
 * ordering, retransmission, broadcast fan-out, session integration -- is
 * DELIBERATELY ABSENT. Those belong to M18-1 and later, AFTER hardware has
 * characterised the transport. Adding them now would mean shipping an
 * abstraction whose requirements have not been measured, which is the specific
 * mistake this rung exists to avoid.
 *
 * ***** THERE IS NO GBA CONCEPT IN THIS FILE OR IN net.c. ***** No SIOCNT, no
 * RCNT, no link cable, no RFU, no netpacket_send, no netplay_client_id, no
 * frames, no cycles, no gpSP symbol of any kind. The dependency direction is
 * the same one runtime/audio.c established and m10-verify enforces: a future
 * GBA link adapter may call DOWN into this layer, and this layer never knows
 * that such an adapter exists.
 *
 * WHY ITS OWN TRANSLATION UNIT -- THE audio.c PRECEDENT
 * ----------------------------------------------------
 * runtime/audio.h:12-34 records the rule in full: every LUAport image links
 * each named object IN ITS ENTIRETY and no target passes --gc-sections, so a
 * string literal or a relocation added to a SHARED object is carried into every
 * frozen milestone that links it. Putting socket code into runtime/platform.c
 * or runtime/shim.c would therefore change M13C's image and break the frozen
 * artifact.
 *
 * runtime/net.o is consequently a NEW object that appears in the M18DIAG object
 * list and in NO other. M13C, M16C and every milestone between them link
 * exactly the objects they linked before.
 *
 * ***** libSceNet.sprx IS NOT LOADED BY THIS LAYER. ***** Not proactively and
 * not as a hidden fallback. The primary M18-0 test is whether the BSD socket
 * entry points are reachable through libkernel alone, and a silent module-load
 * fallback would destroy the ability of the hardware run to distinguish the two
 * paths. plat_net_module_loaded() exists so the screen can assert the negative
 * as a MEASURED fact rather than as a comment.
 *
 * =============================================================================
 * THE ERROR-REPORTING CONTRACT: RAW RETURNS, NEVER A DECODED errno
 * =============================================================================
 * There is no errno in this runtime. A tree-wide search finds no __error(), no
 * sceKernelGetError() and no errno access in runtime/, adapters/ or any app;
 * runtime/shim.c:21-26 (klog) does not even read sendto's return value.
 *
 * So this layer reports the RAW s32 return of every call and interprets
 * NOTHING. A negative recvfrom on a non-blocking socket with no datagram
 * waiting is indistinguishable HERE from a genuine failure, and that ambiguity
 * is reported honestly as "EMPTY OR ERROR" rather than being resolved by a
 * guess about FreeBSD's EWOULDBLOCK value. A positive byte count is
 * unambiguous, and that is the only thing M18-0 claims from a receive.
 *
 * =============================================================================
 * BLOCKING IS FORBIDDEN, AND THIS IS A WATCHDOG PROPERTY, NOT A PREFERENCE
 * =============================================================================
 * The M18 PLAN records the hazard: the gameplay watchdog counts ITERATIONS and
 * reads no clock inside its loop, so a blocking call there does not merely slow
 * the watchdog down -- it stops the watchdog that is supposed to catch the
 * stall. M18-0 links no gameplay loop, but the rule is established HERE, at the
 * bottom of the stack, where every later rung inherits it:
 *
 *   * the socket is switched to O_NONBLOCK BEFORE the first receive is ever
 *     attempted, and plat_net_open() FAILS if that switch does not take;
 *   * plat_net_recv() never waits: it issues exactly ONE recvfrom and returns;
 *   * plat_net_send() never waits;
 *   * there is no poll(), no select(), and no sleep anywhere in net.c.
 *
 * poll() is NOT resolved. The M18 PLAN mentioned it; M18-0 does not need it,
 * and resolving an entry point this rung never calls would be exactly the
 * unused abstraction the ACT instruction forbids.
 * ========================================================================= */

/* Return codes. The -50.. band is this layer's alone.
 *
 * Already taken elsewhere, and deliberately not reused:
 *   -1..-7     runtime/savedata.h   PLAT_SD_*
 *   -20..-24   runtime/platform.h   PLAT_VID_*
 *   -26        runtime/platform.h   PLAT_TIME_ESYMBOL
 *   -28,-29    runtime/platform.h   PLAT_PAD_ESYMBOL / EINIT
 *   -31        runtime/platform.h   PLAT_PAD_EHANDLE
 *   -30        apps/m0diag/main.c   ST_SELFTEST
 *   -40..-43   runtime/audio.h      PLAT_AUD_*
 *   -40..-48   apps/m0diag/main.c   ST_HEAP_*      (app-local, M0 only)
 *   -25,-27    apps/m0diag/main.c   ST_VIDEO_FLIP / ST_RENDER_INIT
 *   -251..-299 various app-local bands
 *
 * -50..-54 is free across the whole tree, so no code a caller can observe from
 * the runtime layer is ambiguous. */
#define PLAT_NET_OK           0
#define PLAT_NET_ESYMBOL    -50   /* a required BSD entry point did not resolve */
#define PLAT_NET_ESOCKET    -51   /* socket() returned a negative descriptor    */
#define PLAT_NET_ENONBLOCK  -52   /* fcntl(F_SETFL, O_NONBLOCK) was refused     */
#define PLAT_NET_EBIND      -53   /* bind() returned non-zero                   */
#define PLAT_NET_ENOTREADY  -54   /* send/recv called before a successful open  */

/* The BSD sockaddr_in is SIXTEEN bytes on this kernel and carries a LEADING
 * LENGTH BYTE that Linux does not have. Proven on hardware by every LUAport
 * launch since M0 -- lua/m16c.lua.in:64-72 builds exactly this block and the
 * UDP log it addresses has worked on every milestone:
 *
 *     +0  u8   sin_len     = 16          <-- BSD ONLY. Linux has no such field.
 *     +1  u8   sin_family  = 2 (AF_INET)
 *     +2  u16  sin_port    BIG endian    <-- htons
 *     +4  u32  sin_addr    LITTLE endian <-- a.b.c.d packed d<<24|c<<16|b<<8|a
 *     +8  u8   sin_zero[8] = 0
 *
 * The address field's byte order is worth stating because it looks wrong: it is
 * built so the FIRST octet lands in the LOW byte, which is the little-endian
 * in_addr_t of the dotted quad. lua/m16c.lua.in:59-62 is the proven reference. */
#define PLAT_NET_ADDRLEN   16

/* The receive staging bound. Nothing in M18-0 sends more than 32 bytes; 256 is
 * the M18 PLAN's stated MTU and is kept here so the bound a later rung inherits
 * is already the right one. A datagram larger than this is TRUNCATED by the
 * kernel, never written past the caller's buffer. */
#define PLAT_NET_MTU      256

/* Everything the diagnostic screen needs, gathered so the renderer is a pure
 * function of its argument and cannot itself be the cause of a fault it is
 * meant to report -- the same discipline apps/m0diag/main.c:1041-1069 uses.
 *
 * EVERY rc FIELD IS A RAW RETURN VALUE. None is decoded, normalised or
 * clamped. */
struct plat_net_status {
    /* ---- U1: symbol resolution. 1 = resolved, 0 = NULL ------------------ */
    int sym_socket;
    int sym_bind;
    int sym_sendto;
    int sym_recvfrom;
    int sym_fcntl;
    int sym_close;
    int sym_kclose;     /* sceKernelClose -- CLEANUP FALLBACK ONLY, see net.c */

    /* ---- U3: the module that must NOT have been loaded ------------------ */
    int module_loaded;  /* ALWAYS 0 in M18-0. Reported, never assumed.       */

    /* ---- open sequence, raw returns -------------------------------------
     *
     * ***** A RAW RETURN IS ONLY MEANINGFUL IF THE CALL ACTUALLY HAPPENED. ***
     * Every field in this block starts at 0, because the whole status block is
     * .bss -- and 0 is ALSO an ordinary SUCCESS value for bind() and a legal
     * flag word for F_GETFL. So bind_rc alone cannot distinguish "bind()
     * returned 0" from "bind() was never reached".
     *
     * That is not hypothetical. The first M18-0 hardware run displayed
     *     BIND RC  0
     * in the success colour for a bind() that never ran at all, because the
     * fcntl before it had already failed and closed the descriptor. The number
     * was the .bss default and the screen presented it as a result.
     *
     * Each raw return is therefore paired with an *_attempted flag stating
     * whether the value beside it is a measurement at all. THE rc FIELDS STAY
     * RAW: no sentinel is written into them, nothing is decoded, normalised or
     * clamped. Encoding "not attempted" INTO the rc would destroy exactly the
     * raw-return contract this header sets out above. */
    int socket_attempted;
    s32 socket_rc;      /* socket(AF_INET, SOCK_DGRAM, 0)                    */

    /* fcntl(fd, F_GETFL, 0) -- THE CHARACTERIZATION PROBE, M18-0 only.
       READ-ONLY, and its result is REPORTED AND NEVER ACTED ON: no branch in
       net.c reads getfl_rc. A successful F_GETFL returns the descriptor's flag
       word, which may legitimately be 0 (O_RDONLY), so getfl_attempted is not
       optional here -- it is the only way to tell that apart from "not run". */
    int getfl_attempted;
    s32 getfl_rc;

    int nonblock_attempted;
    s32 nonblock_rc;    /* fcntl(fd, F_SETFL, O_NONBLOCK)                    */

    /* ---- M18-0b: THE FAILURE-BRANCH fcntl PROBE MATRIX -------------------
     *
     * ***** THESE RUN ONLY AFTER THE OPERATIVE F_SETFL HAS ALREADY FAILED,
     * ON A DESCRIPTOR THAT IS ABOUT TO BE CLOSED REGARDLESS. ***** They are
     * measurements taken on the way out. Nothing reads them except the screen
     * and the log, no branch in net.c consults them, and the open() result is
     * PLAT_NET_ENONBLOCK whatever they return -- including the case where
     * F_SETFL with a different argument SUCCEEDS. M18-0b measures; it does not
     * take the win.
     *
     * WHY: the second hardware run showed the SAME resolved fcntl pointer
     * answer F_GETFL with 2 and refuse F_SETFL/O_NONBLOCK with -1. That splits
     * the remaining explanations but does not choose between them, and -1
     * carries no errno in this runtime (see the contract above). Four more
     * calls on the doomed descriptor separate them:
     *
     *   F_GETFD      a SECOND read-only command. Control: is "reads work" a
     *                property of F_GETFL alone or of read-only commands?
     *   F_SETFD, 0   a command that WRITES and that CONSUMES the variadic
     *                third argument, but is not F_SETFL. This is the one that
     *                bears on whether passing a third argument is the problem.
     *   F_SETFL, 2   F_SETFL again with a DIFFERENT argument -- the flag word
     *                the previous run's F_GETFL reported, i.e. asking for the
     *                state the descriptor is already in.
     *   F_SETFL, 6   that same word with O_NONBLOCK added. The read-modify-
     *                write form, MEASURED ONLY, never adopted here.
     *
     * The four are recorded raw and interpreted nowhere in this layer. One
     * attempted flag covers all four because they are issued unconditionally
     * as a group, with no branch between them.
     *
     * ***** F_SETFD, 0 AND F_SETFL, 2 ARE CHOSEN TO BE HARMLESS. ***** The
     * first clears FD_CLOEXEC, already clear on a plain socket(); the second
     * requests flags the descriptor already has. Neither could make the
     * descriptor usable, and the descriptor is closed microseconds later in
     * any case. */
    int probe_attempted;
    s32 probe_getfd_rc;    /* fcntl(fd, F_GETFD)        -- read, no argument */
    s32 probe_setfd0_rc;   /* fcntl(fd, F_SETFD, 0)     -- write, takes arg  */
    s32 probe_setfl2_rc;   /* fcntl(fd, F_SETFL, 2)     -- other argument    */
    s32 probe_setfl6_rc;   /* fcntl(fd, F_SETFL, 6)     -- 2 | O_NONBLOCK    */

    int bind_attempted;
    s32 bind_rc;        /* bind(fd, &sin, 16)                                */

    s32 fd;             /* the descriptor, or -1                             */
    u16 port;           /* the port bind() was asked for                     */
    int opened;         /* 1 while a descriptor is live                      */
    int closed_with_k;  /* 1 if teardown had to use sceKernelClose           */

    /* ---- traffic counters ----------------------------------------------- */
    u64 tx_calls, tx_ok, tx_err;
    u64 rx_calls, rx_ok, rx_empty;
    s32 last_tx_rc, last_rx_rc;
};

/* ---- lifecycle -------------------------------------------------------------
 *
 * Resolves the BSD entry points and NOTHING ELSE. Opens no socket, binds no
 * port, loads no module, sends no datagram.
 *
 * Must be called AFTER boot_data_region() -- every resolved pointer and the
 * whole status block live in .bss -- and AFTER shim_init(), because the failure
 * paths report through printf()/klog().
 *
 *   G   the eboot's argument-shuffling gadget
 *   D   sceKernelDlsym, as handed in by the Lua loader
 *
 * Returns PLAT_NET_OK when EVERY primitive this rung actually calls resolved,
 * or PLAT_NET_ESYMBOL. The per-symbol detail is in plat_net_status() either
 * way, so a partial failure names the missing primitive on screen instead of
 * collapsing to one word. */
int plat_net_init(void *G, void *D);

/* socket(AF_INET, SOCK_DGRAM, 0) -> [F_GETFL probe] -> fcntl(F_SETFL,
 * O_NONBLOCK) -> bind(port).
 *
 * ***** THE F_GETFL STEP IS A MEASUREMENT AND CHANGES NOTHING. ***** It exists
 * because the first hardware run had fcntl(F_SETFL, O_NONBLOCK) return -1 with
 * no way to tell whether the fcntl ENTRY POINT is unusable or whether only that
 * particular command/argument was refused. A read-only F_GETFL on the same
 * descriptor, through the same resolved pointer and the same call shape,
 * separates those two cases and costs one call. Its return is recorded in
 * getfl_rc, printed, and then IGNORED: it gates nothing, it is not consulted by
 * the F_SETFL that follows, and it cannot change whether open() succeeds. The
 * F_SETFL call below is deliberately left BIT-FOR-BIT as it was, so this run
 * measures the unknown without simultaneously trying to fix it.
 *
 * ***** THE ORDER IS LOAD-BEARING AND IS THE M18-0 SPECIFICATION. ***** The
 * non-blocking switch happens BEFORE the bind and therefore before any receive
 * can possibly be attempted, so there is no window in which a blocking recvfrom
 * could be issued by mistake.
 *
 * FAILS CLOSED AND LEAVES NOTHING BEHIND. If the fcntl or the bind is refused,
 * the descriptor that was already created is CLOSED before returning, so a
 * failed open leaks no socket and the caller may simply report and exit.
 *
 * ***** THE M18-0b PROBE MATRIX DOES NOT WEAKEN THAT. ***** When the operative
 * F_SETFL fails, four extra fcntl calls are issued INSIDE that failure branch,
 * after the failure and before the close, purely to characterise it. They add
 * no return path, no goto and no early exit: the branch still closes the
 * descriptor, still clears fd and opened, and still returns PLAT_NET_ENONBLOCK,
 * and it does so no matter what the four calls report. A probe that SUCCEEDS
 * changes nothing -- see the note on the probe fields above.
 *
 * port is the local UDP port, host byte order; it is converted internally.
 *
 * Returns PLAT_NET_OK, PLAT_NET_ESYMBOL, PLAT_NET_ESOCKET, PLAT_NET_ENONBLOCK
 * or PLAT_NET_EBIND. */
int plat_net_open(u16 port);

/* Closes the descriptor if one is open. Safe to call when init failed, when
 * open failed, or when neither was ever called -- every exit path in the
 * diagnostic calls it unconditionally for exactly that reason.
 *
 * Prefers close(); falls back to sceKernelClose() ONLY if close() did not
 * resolve. That fallback is not speculative abstraction: requirement 10 of the
 * M18-0 ACT is that the socket closes on every exit and failure path, and
 * sceKernelClose is already proven-resolvable in this runtime (runtime/shim.c
 * consumes it on every milestone). Which one was used is reported in
 * plat_net_status()->closed_with_k so the screen states the fact. */
void plat_net_shutdown(void);

/* ---- datagrams. NEITHER CALL EVER BLOCKS. ---------------------------------
 *
 * addr4 is the destination in DOTTED-QUAD ORDER -- addr4[0] is the first octet,
 * so 127.0.0.1 is { 127, 0, 0, 1 }. The little-endian packing the kernel wants
 * is done internally, so no caller has to reproduce the byte-order rule.
 *
 * Returns the raw s32 return of sendto: the byte count on success, negative on
 * failure. Returns PLAT_NET_ENOTREADY if no socket is open. */
int plat_net_send(const u8 addr4[4], u16 port, const void *buf, unsigned len);

/* ONE recvfrom, then return. No wait, no retry, no loop.
 *
 * Returns the byte count (> 0) when a datagram was delivered, or the raw
 * negative return otherwise -- which on a non-blocking socket means EITHER "no
 * datagram is waiting" OR a genuine error, and this layer CANNOT tell the two
 * apart without an errno it has no way to read. Callers must treat a negative
 * as "nothing this time" and rely on the counters, not on the sign, to judge
 * health.
 *
 * from_addr4 (4 bytes, dotted-quad order) and from_port are filled when a
 * datagram arrives; either may be NULL. cap is clamped to PLAT_NET_MTU. */
int plat_net_recv(void *buf, unsigned cap, u8 from_addr4[4], u16 *from_port);

/* ---- reporting ------------------------------------------------------------- */

/* The live status block. Never NULL, valid before plat_net_init() (all zero),
 * and safe to read on every failure path. */
const struct plat_net_status *plat_net_status(void);

/* 1 only if THIS layer loaded a system module. M18-0 loads none, so this is a
 * measured zero and the screen can assert "MODULE LOAD  NO" as a fact.
 *
 * The screen label deliberately does NOT contain the network module's
 * filename. m18diag-verify proves the native path by searching the linked
 * image for that filename, so any literal naming it -- including one that
 * asserts its absence -- fails the gate. Keep such names in comments only;
 * comments do not reach the binary. */
int plat_net_module_loaded(void);

#endif
