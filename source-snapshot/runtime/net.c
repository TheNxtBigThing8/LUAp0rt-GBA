/* LUAport M18-0 -- runtime/net.c
 *
 * =============================================================================
 * THE NATIVE UDP CAPABILITY PROBE. THE ONLY FILE IN THE TREE THAT CALLS
 * socket(), bind() OR recvfrom().
 * =============================================================================
 *
 * Read runtime/net.h first: it carries the scope contract, the reason this is
 * its own object, the raw-return error policy and the no-blocking rule. This
 * file is the implementation and the record of where every constant came from.
 *
 * THIS FILE CONTAINS NO EMULATOR CONCEPT. No GBA, no gpSP, no netplay protocol,
 * no peer table, no sequence number, no generation, no reliability. M18-0 is a
 * capability measurement and nothing above that has been earned yet.
 *
 * runtime/platform.c, runtime/shim.c, runtime/audio.c AND runtime/savedata.c
 * ARE NOT MODIFIED BY M18-0. Every frozen milestone links exactly the objects
 * it linked before, and net.o appears in the M18DIAG object list alone.
 *
 * =============================================================================
 * PROVENANCE OF EVERY CONSTANT BELOW
 * =============================================================================
 * The PS5 kernel is FreeBSD-derived and its socket constants are the BSD ones,
 * NOT the Linux ones. lua/m16c.lua.in:88-90 states this explicitly and was
 * written after a hardware failure caused by the difference. Each value is
 * therefore tagged with how it is known, and the two tiers are kept distinct:
 *
 *   PROVEN IN THIS TREE -- a literal that appears in a Lua loader which has run
 *   on this console on every milestone since M0, driving a socket that works.
 *
 *   DERIVED FROM BSD -- taken from the FreeBSD headers the PS5 kernel inherits.
 *   NOT independently proven here. Each such value is one the hardware run
 *   itself will settle, because a wrong one produces a NEGATIVE RETURN THAT
 *   THIS DIAGNOSTIC PRINTS rather than silent misbehaviour. That is precisely
 *   why M18-0 exists and why nothing is being assumed into correctness.
 * ========================================================================= */

#include "core.h"
#include "shim.h"
#include "net.h"

/* runtime/libc/stdio.h -- LUAport's freestanding stand-in, NOT a system libc.
   Reached through the -Iruntime/libc that the M18 include set carries, exactly
   as runtime/audio.c:37, runtime/platform.c:4 and runtime/shim.c:5 do. No
   target in this tree links a system libc. */
#include <stdio.h>

/* ---- address family -------------------------------------------------------
 * PROVEN IN THIS TREE. lua/m16c.lua.in:68 writes the literal 2 into sin_family
 * of the sockaddr_in that the working UDP log is addressed with:
 *     write8(sa + 1, 2)
 * so AF_INET == 2 on this kernel is a measured fact, not a header lookup. */
#define NET_AF_INET        2

/* ---- socket type ----------------------------------------------------------
 * DERIVED FROM BSD. The Lua loaders call create_socket(AF_INET, SOCK_DGRAM, 0)
 * using Luac0re's own globals, so the NUMERIC value of SOCK_DGRAM does not
 * appear anywhere in this tree and is NOT proven here.
 *
 * FreeBSD sys/socket.h has SOCK_STREAM 1, SOCK_DGRAM 2, SOCK_RAW 3. Linux
 * agrees on all three, so this is one of the few constants with no BSD/Linux
 * divergence to fall into -- but it is still marked derived rather than proven,
 * because "Linux and BSD happen to agree" is an argument, not a measurement.
 *
 * IF THIS VALUE IS WRONG, socket() RETURNS NEGATIVE AND THE SCREEN SHOWS IT.
 * That is the designed outcome: M18-0 converts an unproven constant into a
 * printed number rather than into a silent assumption. */
#define NET_SOCK_DGRAM     2

/* ---- protocol selector ----------------------------------------------------
 * PROVEN IN THIS TREE. Every create_socket() call in every Lua loader passes 0
 * as the third argument, letting the kernel choose UDP for a SOCK_DGRAM
 * AF_INET socket. Passing IPPROTO_UDP explicitly would be a value this tree has
 * never exercised, so 0 is used for the same reason the Lua side uses it. */
#define NET_PROTO_DEFAULT  0

/* ---- fcntl: the non-blocking switch ---------------------------------------
 * PROVEN IN THIS TREE, and proven TOGETHER as a pair. lua/m16c.lua.in:338-339:
 *     local F_SETFL, O_NONBLOCK = 4, 4
 *     syscall.fcntl(srv, F_SETFL, O_NONBLOCK)
 * The blob listener has been switched to non-blocking by exactly these two
 * numbers on every launch since M0, and the 30-second bounded accept loop that
 * depends on it has never wedged.
 *
 * Both being 4 looks like a typo and is not: on BSD F_SETFL is command 4 and
 * O_NONBLOCK is bit 0x0004. They are unrelated constants that coincide. The
 * comment at that line exists because the coincidence has been "corrected" by
 * eye before. */
#define NET_F_SETFL        4
#define NET_O_NONBLOCK     4

/* ---- O_NONBLOCK, CORROBORATED FROM REAL FreeBSD SOURCE --------------------
 * The paragraph above says "proven in this tree", which was too strong on its
 * own: the Lua call sites DISCARD fcntl's return, so this tree had never
 * actually observed the call succeed -- only that the surrounding accept loop
 * behaved acceptably, which it would have done with a blocking listener too.
 *
 * O_NONBLOCK is now independently confirmed against the real header rather than
 * against tree habit. The sibling LuaPSX project in this same working tree
 * carries tools/bsd_fcntl.h, GENERATED by tools/gen_bsd_fcntl.sh from
 * FreeBSD's own sys/fcntl.h, and it states:
 *     LuaPSX/tools/bsd_fcntl.h:13   #define BSD_O_NONBLOCK 0x0004
 * 0x0004 == 4, so NET_O_NONBLOCK is correct by SOURCE, not by recollection.
 *
 * F_SETFL == 4 is NOT confirmed that way: that generator extracts only the
 * hexadecimal O_* flags, and FreeBSD spells the F_* COMMANDS in decimal, so no
 * F_* constant appears in the generated header. F_SETFL therefore rests on tree
 * usage alone -- the weaker tier described above. */

/* ---- F_GETFL: THE CHARACTERIZATION PROBE ----------------------------------
 *
 * ***** NOT PROVEN, AND NOT CONFIRMABLE FROM ANYTHING IN THIS PROJECT. *****
 * This is stated plainly because the M18-0 ACT required the value to be
 * verified before use, and it could NOT be:
 *
 *   * no F_* constant is defined anywhere in LUAport or in LuaPSX;
 *   * LuaPSX/tools/bsd_fcntl.h contains only O_* flags, for the reason above;
 *   * LuaPSX/tools/check_bsd_consts.py COULD check it -- its regex already
 *     accepts decimal defines -- but it reads FreeBSD's headers from
 *     /mnt/e/work/sdk-source/include/freebsd, which does not exist on this
 *     machine, so the check cannot be run here;
 *   * LuaPSX/psx/libpcsxcore/socket.c:133 does use F_GETFL, but symbolically,
 *     from the HOST's headers in a host build. That proves the IDIOM, not the
 *     number the PS5 kernel uses.
 *
 * So 3 is DERIVED FROM BSD, the same tier as SOCK_DGRAM above, and for the same
 * reason it is acceptable HERE and nowhere else: this call's entire purpose is
 * to be measured. A wrong command number produces A NEGATIVE RETURN THAT THIS
 * DIAGNOSTIC PRINTS. Nothing depends on it being right.
 *
 * The supporting argument, which is an argument and not a measurement: FreeBSD
 * and Linux both order the file-control commands F_DUPFD 0, F_GETFD 1,
 * F_SETFD 2, F_GETFL 3, F_SETFL 4 -- there is no BSD/Linux divergence to fall
 * into, and F_SETFL == 4 (used by this tree since M0) is the immediately
 * following member of that same sequence.
 *
 * WHY A WRONG VALUE HERE CANNOT CORRUPT THE F_SETFL MEASUREMENT: the third
 * argument is passed as 0, and the only commands numerically adjacent to 3 are
 * F_SETFD (whose argument 0 merely clears FD_CLOEXEC on a descriptor this
 * function is about to close anyway) and F_GETFD/F_DUPFD (both harmless reads
 * or a duplicate fd that is never stored). None of them touches the file STATUS
 * flags that F_SETFL then sets, so the number this run reports for F_SETFL
 * remains comparable with the previous run's -1. */
#define NET_F_GETFL        3

/* ---- F_GETFD / F_SETFD: THE M18-0b PROBE COMMANDS -------------------------
 *
 * ***** SAME TIER AS F_GETFL: DERIVED FROM BSD, NOT PROVEN HERE. ***** Stated
 * plainly rather than dressed up, because the evidence available is weaker than
 * it looks at first glance:
 *
 *   * no F_* constant is defined numerically anywhere in LUAport or LuaPSX;
 *   * LuaPSX/tools/bsd_fcntl.h is generated from FreeBSD's real sys/fcntl.h but
 *     its generator extracts only the HEXADECIMAL O_* flags, and FreeBSD spells
 *     the F_* commands in decimal, so no F_* constant reaches that header;
 *   * mgba/src/third-party/sqlite3/sqlite3.c:40069 does write exactly the
 *     idiom used below --
 *         osFcntl(fd, F_SETFD, osFcntl(fd, F_GETFD, 0) | FD_CLOEXEC)
 *     -- and LuaPSX/psx/libpcsxcore/socket.c:133 uses F_GETFL the same way, but
 *     BOTH are host builds resolving the names from the HOST's headers. They
 *     prove the idiom and the argument shape. They do not prove the number the
 *     PS5 kernel uses.
 *
 * So these rest on the same sequence argument F_GETFL does -- F_DUPFD 0,
 * F_GETFD 1, F_SETFD 2, F_GETFL 3, F_SETFL 4, with no BSD/Linux divergence --
 * anchored at the one end by F_SETFL 4, which this tree has used since M0, and
 * at the other by the SECOND hardware run, in which command 3 returned 2. That
 * return is itself corroboration: 2 is not a possible F_GETFD answer (FD_CLOEXEC
 * is bit 0, so only 0 or 1) and not a plausible F_DUPFD answer (that allocates a
 * free descriptor, and 2 is stderr). Command 3 behaved as F_GETFL and as nothing
 * else, which pins the sequence these two are members of.
 *
 * ***** AND, AS WITH EVERY OTHER CONSTANT HERE, BEING WRONG IS SAFE AND
 * VISIBLE. ***** A wrong command number produces a negative return that this
 * diagnostic prints. These four calls are issued on a descriptor that has
 * already failed its non-blocking switch and is closed a few statements later,
 * so there is no state left for a misfire to corrupt. */
#define NET_F_GETFD        1
#define NET_F_SETFD        2

/* ---- the probe arguments, AS LITERALS ON PURPOSE --------------------------
 *
 * ***** NOT COMPUTED FROM getfl_rc. ***** 2 is written here as a constant
 * because it is what the PREVIOUS hardware run's F_GETFL reported, and 6 is
 * that value with O_NONBLOCK added. Deriving either one from this run's
 * getfl_rc at runtime would create a data dependency from a measurement into a
 * call argument -- which is the precise thing M18-0b must not do, because the
 * two probes would then no longer be the same experiment from run to run.
 * Freezing them as literals keeps every probe argument a property of the SOURCE
 * rather than of the console, so two runs are comparable.
 *
 * NET_PROBE_SETFL_B is deliberately spelled 6 and not
 * (NET_PROBE_SETFL_A | NET_O_NONBLOCK): identical value, but the literal cannot
 * silently follow a future edit to either operand. */
#define NET_PROBE_SETFD_ARG  0   /* clears FD_CLOEXEC; already clear here    */
#define NET_PROBE_SETFL_A    2   /* the flag word run 2 reported: O_RDWR     */
#define NET_PROBE_SETFL_B    6   /* 2 | 4 -- read-modify-write, MEASURED ONLY */

/* ---- sockaddr_in field offsets --------------------------------------------
 * PROVEN IN THIS TREE. lua/m16c.lua.in:64-72. The leading sin_len byte is the
 * BSD-only field; a Linux-shaped 16-byte sockaddr_in with a 16-bit family at
 * offset 0 would put the family where sin_len belongs and fail. */
#define NET_SA_LEN_OFF     0
#define NET_SA_FAM_OFF     1
#define NET_SA_PORT_OFF    2
#define NET_SA_ADDR_OFF    4

/* Resolved entry points, the descriptor and the whole reported status. All in
   .bss, which is why plat_net_init() must run after boot_data_region(). Same
   shape as platform.c's V/T structures and audio.c's A. */
static struct {
    void *gadget;
    void *fn_socket;
    void *fn_bind;
    void *fn_sendto;
    void *fn_recvfrom;
    void *fn_fcntl;
    void *fn_close;
    void *fn_kclose;              /* sceKernelClose -- teardown fallback only */
    struct plat_net_status st;
} N;

/* ------------------------------------------------------------- helpers ---- */

/* Host u16 -> network (big-endian) u16, written as two bytes so the result does
   not depend on this compiler's byte order or on a libc htons that does not
   exist in this freestanding build. Mirrors lua/m16c.lua.in:57. */
static void net_put_port(u8 *dst, u16 port) {
    dst[0] = (u8)((port >> 8) & 0xFFu);
    dst[1] = (u8)(port & 0xFFu);
}

static u16 net_get_port(const u8 *src) {
    return (u16)(((u16)src[0] << 8) | (u16)src[1]);
}

/* Builds the 16-byte BSD sockaddr_in described in net.h.
 *
 * addr4 is in DOTTED-QUAD ORDER and may be NULL, which means INADDR_ANY (all
 * zero) -- the form bind() wants when listening on every interface.
 *
 * The address bytes are copied STRAIGHT THROUGH, first octet into the lowest
 * address. That produces the little-endian in_addr_t the kernel expects and is
 * the byte-for-byte equivalent of lua/m16c.lua.in:59-62, which packs
 * d<<24 | c<<16 | b<<8 | a into a 32-bit little-endian store. Writing it as
 * four byte stores removes the endianness question entirely. */
static void net_build_sockaddr(u8 sa[PLAT_NET_ADDRLEN],
                               const u8 addr4[4], u16 port) {
    int i;
    for (i = 0; i < PLAT_NET_ADDRLEN; i++) sa[i] = 0;

    sa[NET_SA_LEN_OFF] = (u8)PLAT_NET_ADDRLEN;
    sa[NET_SA_FAM_OFF] = (u8)NET_AF_INET;
    net_put_port(&sa[NET_SA_PORT_OFF], port);

    if (addr4) {
        sa[NET_SA_ADDR_OFF + 0] = addr4[0];
        sa[NET_SA_ADDR_OFF + 1] = addr4[1];
        sa[NET_SA_ADDR_OFF + 2] = addr4[2];
        sa[NET_SA_ADDR_OFF + 3] = addr4[3];
    }
}

/* The one place a descriptor is released, so no exit path can invent its own.
   Reports which entry point did it; see the note in net.h about why the
   sceKernelClose fallback is load-bearing rather than speculative. */
static void net_close_fd(s32 fd) {
    if (fd < 0) return;

    if (N.fn_close) {
        NC(N.gadget, N.fn_close, (u64)(s64)fd, 0, 0, 0, 0, 0);
    } else if (N.fn_kclose) {
        NC(N.gadget, N.fn_kclose, (u64)(s64)fd, 0, 0, 0, 0, 0);
        N.st.closed_with_k = 1;
    }
}

/* ---------------------------------------------------------------- init ---- */

int plat_net_init(void *G, void *D) {
    int i;
    u8 *z = (u8 *)&N;

    /* Whole-structure reset. plat_net_init() is called once, but clearing
       explicitly means a status read before init cannot show stale values from
       a previous payload in the same game session. */
    for (i = 0; i < (int)sizeof(N); i++) z[i] = 0;

    N.gadget  = G;
    N.st.fd   = -1;
    N.st.port = 0;

    klog("NET: resolving BSD socket primitives from libkernel\n");

    /* ***** NO MODULE IS LOADED HERE. *****
       No sceKernelLoadStartModule, no libSceNet.sprx, not proactively and not
       as a fallback. The whole point of M18-0 is to learn whether libkernel
       alone is sufficient, and a hidden module load would make a PASS
       uninterpretable. module_loaded stays 0 and the screen reports it. */
    N.st.module_loaded = 0;

    /* The five primitives this rung actually calls, plus close(). poll() is
       deliberately NOT resolved -- M18-0 never calls it. */
    N.fn_socket   = SYM(G, D, LIBKERNEL_HANDLE, "socket");
    N.fn_bind     = SYM(G, D, LIBKERNEL_HANDLE, "bind");
    N.fn_sendto   = SYM(G, D, LIBKERNEL_HANDLE, "sendto");
    N.fn_recvfrom = SYM(G, D, LIBKERNEL_HANDLE, "recvfrom");
    N.fn_fcntl    = SYM(G, D, LIBKERNEL_HANDLE, "fcntl");
    N.fn_close    = SYM(G, D, LIBKERNEL_HANDLE, "close");

    /* Teardown fallback only, and already proven-resolvable: runtime/shim.c
       consumes sceKernelClose on every milestone in this tree. Resolving it
       costs one dlsym and guarantees requirement 10 -- the socket closes on
       every exit path -- even in the case where close() itself is the primitive
       that turns out to be missing. Its resolution status is ALSO a genuine U1
       data point, so it is reported rather than hidden. */
    N.fn_kclose   = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelClose");

    N.st.sym_socket   = N.fn_socket   ? 1 : 0;
    N.st.sym_bind     = N.fn_bind     ? 1 : 0;
    N.st.sym_sendto   = N.fn_sendto   ? 1 : 0;
    N.st.sym_recvfrom = N.fn_recvfrom ? 1 : 0;
    N.st.sym_fcntl    = N.fn_fcntl    ? 1 : 0;
    N.st.sym_close    = N.fn_close    ? 1 : 0;
    N.st.sym_kclose   = N.fn_kclose   ? 1 : 0;

    printf("NET: socket=%p bind=%p sendto=%p recvfrom=%p fcntl=%p close=%p "
           "sceKernelClose=%p\n",
           N.fn_socket, N.fn_bind, N.fn_sendto, N.fn_recvfrom,
           N.fn_fcntl, N.fn_close, N.fn_kclose);

    /* Every primitive M18-0 CALLS must be present. close() is included because
       a socket that cannot be released is a leak across the whole game session,
       which is the failure lua/m16c.lua.in:313-315 records paying for -- but a
       resolvable sceKernelClose satisfies that requirement just as well, so the
       test is "at least one way to close exists". */
    if (!N.fn_socket || !N.fn_bind || !N.fn_sendto ||
        !N.fn_recvfrom || !N.fn_fcntl || (!N.fn_close && !N.fn_kclose)) {
        klog("NET: FAIL a required BSD primitive did not resolve from "
             "libkernel. THIS IS THE U1 ANSWER: the native payload cannot "
             "reach the socket layer without another module.\n");
        return PLAT_NET_ESYMBOL;
    }

    klog("NET: all required primitives resolved, no module loaded\n");
    return PLAT_NET_OK;
}

/* ---------------------------------------------------------------- open ---- */

int plat_net_open(u16 port) {
    u8  sa[PLAT_NET_ADDRLEN];
    s32 fd, rc;
    s32 gfl;                       /* the F_GETFL probe result. NEVER BRANCHED
                                      ON -- kept in its own variable so it
                                      cannot be confused with rc below. */

    if (!N.fn_socket || !N.fn_bind || !N.fn_fcntl)
        return PLAT_NET_ESYMBOL;

    /* ---- 1. socket ------------------------------------------------------- */
    N.st.socket_attempted = 1;
    fd = (s32)NC(N.gadget, N.fn_socket,
                 (u64)NET_AF_INET, (u64)NET_SOCK_DGRAM, (u64)NET_PROTO_DEFAULT,
                 0, 0, 0);
    N.st.socket_rc = fd;

    printf("NET: socket(AF_INET=%d, SOCK_DGRAM=%d, 0) -> %d\n",
           NET_AF_INET, NET_SOCK_DGRAM, (int)fd);

    if (fd < 0) {
        klog("NET: FAIL socket() refused. If SOCK_DGRAM is the wrong number "
             "for this kernel this is where it shows.\n");
        N.st.fd = -1;
        return PLAT_NET_ESOCKET;
    }

    N.st.fd     = fd;
    N.st.opened = 1;

    /* ---- 1b. THE F_GETFL CHARACTERIZATION PROBE ---------------------------
     *
     * ***** THIS MEASURES. IT DOES NOT CORRECT, AND IT DECIDES NOTHING. *****
     *
     * The first M18-0 hardware run returned fd 40 from socket() and then -1
     * from fcntl(fd, F_SETFL, O_NONBLOCK). -1 is libc's bare "failed, see
     * errno" sentinel and this runtime has no errno to read, so that number
     * cannot distinguish between:
     *
     *   (a) the resolved fcntl entry point is unusable in this payload -- a
     *       stub, a restricted export, or a call-shape problem affecting every
     *       fcntl regardless of command;
     *   (b) the entry point works, and only F_SETFL / this argument was
     *       refused.
     *
     * F_GETFL separates them with ONE extra call: same resolved pointer, same
     * NC() call shape, same descriptor, same variadic signature -- only the
     * command differs, and this one only READS.
     *
     *   a sane flag word back  -> the fcntl path works, so (b): the refusal is
     *                             specific to setting the flags;
     *   negative again         -> (a): nothing about fcntl is usable here, and
     *                             no amount of changing the F_SETFL arguments
     *                             would have helped.
     *
     * ***** THE RESULT IS NOT ACTED ON. ***** It is stored, printed, and then
     * left alone. There is deliberately NO `if (gfl < 0)` here: a branch would
     * turn this measurement into a behaviour change and would mean the F_SETFL
     * number the next run reports could no longer be compared with the -1 the
     * last one reported. The flags it returns are likewise NOT fed into the
     * F_SETFL call below -- the read-modify-write form is the eventual likely
     * correction, and mixing it in now would make a PASS uninterpretable,
     * because two things would have changed at once. */
    N.st.getfl_attempted = 1;
    gfl = (s32)NC(N.gadget, N.fn_fcntl,
                  (u64)(s64)fd, (u64)NET_F_GETFL, 0, 0, 0, 0);
    N.st.getfl_rc = gfl;

    printf("NET: fcntl(%d, F_GETFL=%d, 0) -> %d  [CHARACTERIZATION ONLY -- "
           "this value is reported and NOT acted on]\n",
           (int)fd, NET_F_GETFL, (int)gfl);

    /* ---- 2. NON-BLOCKING, BEFORE ANY RECEIVE IS POSSIBLE ------------------
       Ordered ahead of bind() on purpose. There is no point in the lifetime of
       this descriptor at which it is bound AND still blocking, so no caller can
       issue a blocking recvfrom by mistake. See the watchdog note in net.h. */
    N.st.nonblock_attempted = 1;
    rc = (s32)NC(N.gadget, N.fn_fcntl,
                 (u64)(s64)fd, (u64)NET_F_SETFL, (u64)NET_O_NONBLOCK, 0, 0, 0);
    N.st.nonblock_rc = rc;

    printf("NET: fcntl(%d, F_SETFL=%d, O_NONBLOCK=%d) -> %d\n",
           (int)fd, NET_F_SETFL, NET_O_NONBLOCK, (int)rc);

    if (rc < 0) {
        /* ---- M18-0b: THE FAILURE-BRANCH PROBE MATRIX ----------------------
         *
         * ***** EVERYTHING IN THIS BLOCK IS A MEASUREMENT TAKEN ON A
         * DESCRIPTOR THAT IS ALREADY BEING THROWN AWAY. ***** Control reached
         * here because the operative F_SETFL above returned negative. That
         * decision is already made and is not revisited: the close below, the
         * cleared fd, the cleared opened flag and the PLAT_NET_ENONBLOCK return
         * all happen unconditionally, whatever these four calls report.
         *
         * ***** THERE IS NO BRANCH, NO GOTO AND NO RETURN IN THIS BLOCK. *****
         * That is not a stylistic note -- it is the property that makes the
         * probes safe, and m18diag-verify checks it structurally rather than
         * trusting this comment.
         *
         * WHAT THE SECOND HARDWARE RUN LEFT OPEN. It showed the same resolved
         * fcntl pointer answer F_GETFL with 2 and refuse F_SETFL/O_NONBLOCK
         * with -1. So fcntl is NOT wholly unusable, and the failure is specific
         * to something -- but "something" still covers at least:
         *
         *   (i)  the command: setting status flags is refused where reading
         *        them is allowed, through this particular call path;
         *   (ii) the ARGUMENT CHANNEL: F_GETFL ignores its third argument, so
         *        that run never tested whether a third argument arrives intact
         *        at a variadic callee through the gadget;
         *   (iii) the argument VALUE: O_NONBLOCK alone may be rejected where a
         *        full flag word would not be.
         *
         * The four calls below were chosen so that each one moves exactly one
         * of those:
         *
         *   F_GETFD      a second READ-ONLY command, no third argument. Tells
         *                whether reads in general work, or only F_GETFL.
         *   F_SETFD, 0   WRITES, and CONSUMES a third argument, but is not
         *                F_SETFL. This is the discriminator for (ii): it is the
         *                only probe that exercises argument delivery to a
         *                variadic setter without touching the status flags.
         *   F_SETFL, 2   F_SETFL with a different argument -- bears on (iii).
         *   F_SETFL, 6   the read-modify-write form, 2 | O_NONBLOCK.
         *
         * ***** F_SETFL, 6 IS MEASURED AND NOT ADOPTED, EVEN IF IT SUCCEEDS.
         * ***** This is the whole discipline of the rung. If it returns 0 that
         * is a strong result, and the correct response is to report it and let
         * the NEXT rung make it operative deliberately -- not to silently
         * inherit a working socket from a diagnostic and lose the ability to
         * say which change was responsible. The descriptor is closed below
         * regardless and plat_net_open() still fails.
         *
         * THE RESULTS ARE NOT INTERPRETED HERE. They are raw returns, stored
         * and printed. -1 carries no errno in this runtime (net.h), so this
         * layer states no cause; the screen and the log lay the four numbers
         * out and the reading is done by a human.
         *
         * WHY THIS IS HARMLESS. F_GETFD reads. F_SETFD with argument 0 clears
         * FD_CLOEXEC, which is already clear on a descriptor straight out of
         * socket(). F_SETFL with 2 asks for the flag word the descriptor
         * already has. None of them can make a blocking socket acceptable, and
         * all of them are followed within microseconds by close(). */
        N.st.probe_attempted = 1;

        N.st.probe_getfd_rc = (s32)NC(N.gadget, N.fn_fcntl,
                     (u64)(s64)fd, (u64)NET_F_GETFD, 0, 0, 0, 0);

        N.st.probe_setfd0_rc = (s32)NC(N.gadget, N.fn_fcntl,
                     (u64)(s64)fd, (u64)NET_F_SETFD,
                     (u64)NET_PROBE_SETFD_ARG, 0, 0, 0);

        N.st.probe_setfl2_rc = (s32)NC(N.gadget, N.fn_fcntl,
                     (u64)(s64)fd, (u64)NET_F_SETFL,
                     (u64)NET_PROBE_SETFL_A, 0, 0, 0);

        N.st.probe_setfl6_rc = (s32)NC(N.gadget, N.fn_fcntl,
                     (u64)(s64)fd, (u64)NET_F_SETFL,
                     (u64)NET_PROBE_SETFL_B, 0, 0, 0);

        printf("NET: PROBE MATRIX on doomed fd %d -- "
               "fcntl(F_GETFD=%d) -> %d, "
               "fcntl(F_SETFD=%d, %d) -> %d, "
               "fcntl(F_SETFL=%d, %d) -> %d, "
               "fcntl(F_SETFL=%d, %d) -> %d\n",
               (int)fd,
               NET_F_GETFD,                       (int)N.st.probe_getfd_rc,
               NET_F_SETFD, NET_PROBE_SETFD_ARG,  (int)N.st.probe_setfd0_rc,
               NET_F_SETFL, NET_PROBE_SETFL_A,    (int)N.st.probe_setfl2_rc,
               NET_F_SETFL, NET_PROBE_SETFL_B,    (int)N.st.probe_setfl6_rc);

        klog("NET: PROBE MATRIX is CHARACTERIZATION ONLY. These returns are "
             "reported and acted on NOWHERE. The socket is closed and the open "
             "still fails, even if one of them succeeded.\n");

        /* FAIL CLOSED. A blocking socket is not an acceptable degraded mode --
           it is the watchdog hazard itself -- so the descriptor is released
           rather than handed back in a state the rest of the payload is
           forbidden to use. */
        klog("NET: FAIL fcntl refused. Refusing to continue with a BLOCKING "
             "socket; closing it.\n");
        net_close_fd(fd);
        N.st.fd     = -1;
        N.st.opened = 0;
        return PLAT_NET_ENONBLOCK;
    }

    /* ---- 3. bind --------------------------------------------------------- */
    net_build_sockaddr(sa, 0, port);          /* NULL addr4 => INADDR_ANY     */
    N.st.port = port;

    N.st.bind_attempted = 1;
    rc = (s32)NC(N.gadget, N.fn_bind,
                 (u64)(s64)fd, (u64)sa, (u64)PLAT_NET_ADDRLEN, 0, 0, 0);
    N.st.bind_rc = rc;

    printf("NET: bind(%d, 0.0.0.0:%u, %d) -> %d\n",
           (int)fd, (unsigned)port, PLAT_NET_ADDRLEN, (int)rc);

    if (rc != 0) {
        klog("NET: FAIL bind refused. Closing the socket rather than leaving "
             "an unbound descriptor behind.\n");
        net_close_fd(fd);
        N.st.fd     = -1;
        N.st.opened = 0;
        return PLAT_NET_EBIND;
    }

    printf("NET: up -- fd %d bound to UDP %u, NON-BLOCKING, no module loaded\n",
           (int)fd, (unsigned)port);
    return PLAT_NET_OK;
}

/* ------------------------------------------------------------ datagrams --- */

int plat_net_send(const u8 addr4[4], u16 port, const void *buf, unsigned len) {
    u8  sa[PLAT_NET_ADDRLEN];
    s32 rc;

    if (!N.st.opened || N.st.fd < 0 || !N.fn_sendto || !buf)
        return PLAT_NET_ENOTREADY;

    if (len > PLAT_NET_MTU) len = PLAT_NET_MTU;

    net_build_sockaddr(sa, addr4, port);

    /* Exactly six arguments, which is the full width native_call() carries --
       the same call shape runtime/shim.c:25 already uses for the UDP log.
       flags is 0: this layer sends no MSG_* option. DOES NOT BLOCK: the
       descriptor is O_NONBLOCK from before it was bound. */
    rc = (s32)NC(N.gadget, N.fn_sendto,
                 (u64)(s64)N.st.fd, (u64)buf, (u64)len, 0,
                 (u64)sa, (u64)PLAT_NET_ADDRLEN);

    N.st.tx_calls++;
    N.st.last_tx_rc = rc;
    if (rc < 0) N.st.tx_err++;
    else        N.st.tx_ok++;

    /* Bounded logging. A per-call line would flood the log it is meant to
       explain; the counters carry the long-run picture. */
    if (N.st.tx_calls <= 4 || rc < 0)
        printf("NET: sendto len %u -> %d\n", len, (int)rc);

    return (int)rc;
}

int plat_net_recv(void *buf, unsigned cap, u8 from_addr4[4], u16 *from_port) {
    u8  sa[PLAT_NET_ADDRLEN];
    u32 salen;
    s32 rc;
    int i;

    if (!N.st.opened || N.st.fd < 0 || !N.fn_recvfrom || !buf)
        return PLAT_NET_ENOTREADY;

    if (cap > PLAT_NET_MTU) cap = PLAT_NET_MTU;

    for (i = 0; i < PLAT_NET_ADDRLEN; i++) sa[i] = 0;

    /* recvfrom's last argument is a POINTER to the address length, in/out. It
       must be seeded with the buffer size or the kernel has no room to report
       the source. */
    salen = (u32)PLAT_NET_ADDRLEN;

    /* ***** ONE CALL. NO LOOP, NO WAIT, NO RETRY. ***** The socket is
       O_NONBLOCK, so this returns immediately whether or not a datagram is
       waiting. This is the single most important line in the file with respect
       to the watchdog hazard recorded in net.h. */
    rc = (s32)NC(N.gadget, N.fn_recvfrom,
                 (u64)(s64)N.st.fd, (u64)buf, (u64)cap, 0,
                 (u64)sa, (u64)&salen);

    N.st.rx_calls++;
    N.st.last_rx_rc = rc;

    if (rc > 0) {
        N.st.rx_ok++;
        if (from_addr4) {
            from_addr4[0] = sa[NET_SA_ADDR_OFF + 0];
            from_addr4[1] = sa[NET_SA_ADDR_OFF + 1];
            from_addr4[2] = sa[NET_SA_ADDR_OFF + 2];
            from_addr4[3] = sa[NET_SA_ADDR_OFF + 3];
        }
        if (from_port) *from_port = net_get_port(&sa[NET_SA_PORT_OFF]);

        if (N.st.rx_ok <= 4)
            printf("NET: recvfrom -> %d bytes from %u.%u.%u.%u:%u\n",
                   (int)rc,
                   (unsigned)sa[NET_SA_ADDR_OFF + 0],
                   (unsigned)sa[NET_SA_ADDR_OFF + 1],
                   (unsigned)sa[NET_SA_ADDR_OFF + 2],
                   (unsigned)sa[NET_SA_ADDR_OFF + 3],
                   (unsigned)net_get_port(&sa[NET_SA_PORT_OFF]));
        return (int)rc;
    }

    /* NEGATIVE OR ZERO IS NOT DECODED.
       On a non-blocking socket this is "no datagram waiting" in the ordinary
       case and a genuine error in the rare one, and there is no errno in this
       runtime to tell them apart (see net.h). It is counted as EMPTY and the
       raw value is retained in last_rx_rc for the screen. Not logged: it
       happens on most frames by design. */
    N.st.rx_empty++;
    return (int)rc;
}

/* ------------------------------------------------------------ shutdown ---- */

void plat_net_shutdown(void) {
    if (N.st.opened && N.st.fd >= 0) {
        printf("NET: closing fd %d after tx %llu (ok %llu err %llu), "
               "rx %llu (ok %llu empty %llu)\n",
               (int)N.st.fd,
               (unsigned long long)N.st.tx_calls,
               (unsigned long long)N.st.tx_ok,
               (unsigned long long)N.st.tx_err,
               (unsigned long long)N.st.rx_calls,
               (unsigned long long)N.st.rx_ok,
               (unsigned long long)N.st.rx_empty);
        net_close_fd(N.st.fd);
    }

    N.st.fd     = -1;
    N.st.opened = 0;

    /* The resolved pointers are deliberately RETAINED. Nothing calls them after
       this point -- every entry point above checks st.opened first -- and
       keeping them means a second shutdown is harmless and the screen can still
       report what resolved after the socket is gone. The same reasoning
       runtime/audio.c:200-202 records for A.aud_close. */
}

const struct plat_net_status *plat_net_status(void) { return &N.st; }

int plat_net_module_loaded(void) { return N.st.module_loaded; }
