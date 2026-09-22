/* ---------------------------------------------------------- fault trap -- */

/* Enough of the PS5's FreeBSD-derived signal ABI to report a fault.
 *   siginfo_t: si_signo 0, si_errno 4, si_code 8, si_pid 12, si_uid 16,
 *              si_status 20, si_addr 24
 *   ucontext_t: uc_sigmask 0 (16 bytes), then mcontext_t, whose mc_rip sits
 *              160 bytes in -- so RIP is at ucontext + 176.
 * Both are sanity-checked against the known code range before being trusted. */
#define SIG_SEGV 11
#define SIG_BUS  10
#define SA_SIGINFO_BSD 0x0040
#define SI_ADDR_OFF 24
#define UC_RIP_OFF  176

static void *fault_G, *fault_sendto, *fault_sigaction;
static s32   fault_fd;
static u8   *fault_sa;

static void fault_handler(int sig, void *info, void *uctx) {
    u64 addr = info ? *(u64 *)((u8 *)info + SI_ADDR_OFF) : 0;
    u64 rip  = uctx ? *(u64 *)((u8 *)uctx + UC_RIP_OFF)  : 0;

    bmsg(fault_G, fault_sendto, fault_fd, fault_sa, "FAULT: signal", (u64)sig);
    bmsg(fault_G, fault_sendto, fault_fd, fault_sa, "FAULT: touched addr", addr);
    bmsg(fault_G, fault_sendto, fault_fd, fault_sa, "FAULT: rip", rip);

    /* Offsets relative to the image make the log directly usable with
       addr2line without knowing where the JIT mapping landed this run. */
    u64 code = (u64)__data_load;              /* end of .text, inside the image */
    bmsg(fault_G, fault_sendto, fault_fd, fault_sa, "FAULT: rip-image_off",
         rip - ((code) & ~0xFFFFULL));
    bmsg(fault_G, fault_sendto, fault_fd, fault_sa, "FAULT: data_start", (u64)__data_start);
    bmsg(fault_G, fault_sendto, fault_fd, fault_sa, "FAULT: bss_end", (u64)__bss_end);

    if (addr >= (u64)__bss_end)
        bmsg(fault_G, fault_sendto, fault_fd, fault_sa,
             "FAULT: PAST bss_end by", addr - (u64)__bss_end);

    /* Hand the signal back to the default action so the process dies on the
       re-executed instruction instead of looping in here forever. */
    if (fault_sigaction) {
        u8 sa[32];
        for (int i = 0; i < 32; i++) sa[i] = 0;   /* handler = SIG_DFL (0) */
        NC(fault_G, fault_sigaction, (u64)sig, (u64)sa, 0, 0, 0, 0);
    }
}

static void install_fault_trap(void *G, void *D, void *sendto, s32 fd, u8 *sa) {
    fault_G = G; fault_sendto = sendto; fault_fd = fd; fault_sa = sa;
    fault_sigaction = SYM(G, D, LIBKERNEL_HANDLE, "sigaction");
    if (!fault_sigaction) { klog("fault trap: no sigaction\n"); return; }

    u8 act[32];
    for (int i = 0; i < 32; i++) act[i] = 0;
    *(u64 *)(act + 0) = (u64)&fault_handler;
    *(u32 *)(act + 8) = SA_SIGINFO_BSD;

    s32 r1 = (s32)NC(G, fault_sigaction, SIG_SEGV, (u64)act, 0, 0, 0, 0);
    s32 r2 = (s32)NC(G, fault_sigaction, SIG_BUS,  (u64)act, 0, 0, 0, 0);
    if (r1 == 0 && r2 == 0) klog("fault trap: armed\n");
    else klog("fault trap: sigaction rejected\n");
}

