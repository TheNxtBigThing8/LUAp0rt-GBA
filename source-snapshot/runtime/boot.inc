/* ------------------------------------------------------- data bootstrap -- */

/* Laid out by linker.ld. Because everything is compiled -fpie, taking the
   address of one of these yields its real runtime address, so no manual base
   arithmetic is needed. */
extern char __data_load[];
extern char __data_start[];
extern char __data_end[];
extern char __bss_start[];
extern char __bss_end[];
extern char __got_start[];
extern char __rela_start[];
extern char __rela_end[];
extern char __got_end[];

/* Entry point, and also the image's load base: _start is the first thing in
   .text and the script links .text at 0, so its runtime address IS the base. */
void _start(u64 eboot_base, u64 dlsym_addr, struct ext_args *ext);

/* FreeBSD mmap flags -- the PS5 kernel is FreeBSD-derived. */
#define BSD_MAP_PRIVATE   0x0002
#define BSD_MAP_FIXED     0x0010
#define BSD_MAP_ANON      0x1000
#define BSD_PROT_RW       0x0003

/* Sends "<label> 0x<hex>\n" using only stack and arguments, so it is usable
   before the .bss that klog() lives in has been mapped. */
static void bmsg(void *G, void *sendto, s32 fd, u8 *sa, const char *label, u64 v) {
    if (!sendto || fd < 0) return;

    char b[128];
    int p = 0;
    while (*label && p < 90) b[p++] = *label++;
    b[p++] = ' '; b[p++] = '0'; b[p++] = 'x';

    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        int d = (int)((v >> i) & 0xF);
        if (d || started || i == 0) {
            b[p++] = d < 10 ? (char)('0' + d) : (char)('a' + d - 10);
            started = 1;
        }
    }
    b[p++] = '\n';
    NC(G, sendto, (u64)fd, (u64)b, (u64)p, 0, (u64)sa, 16);
}

/* The payload executes from a PROT_READ|PROT_EXECUTE JIT alias, so every
 * global starts out unwritable. This maps anonymous RW memory over the address
 * range the linker reserved for .data and .bss -- placed directly after the
 * code so it stays inside RIP-relative range -- then installs .data's initial
 * contents and clears .bss.
 *
 * Nothing in here may touch a global: that is the very memory being set up.
 * Only arguments, locals and the linker symbols are in play.
 *
 * This memory is deliberately NOT part of the JIT mapping. ps5 JIT shared
 * memory is a scarce per-process resource -- asking for the whole 1.6MB span
 * returns ENOMEM (0x8002000C) inside ps2emu -- whereas plain anonymous pages
 * are cheap. The JIT mapping therefore covers only the 64KB of code, and the
 * writable region is requested separately at the address immediately after it.
 *
 * Two attempts, least destructive first: `addr` without MAP_FIXED is only a
 * hint, so it can never displace an existing mapping. MAP_FIXED is the
 * fallback, and it is what makes the placement non-negotiable.
 */
static int boot_data_region(void *G, void *mmap_fn, void *munmap_fn,
                            void *sendto, s32 fd, u8 *sa) {
    u64 base = (u64)__data_start;
    u64 span = (u64)__bss_end - base;

    if (span == 0) return 1;            /* nothing writable: nothing to do */

    bmsg(G, sendto, fd, sa, "boot: code base", (u64)__data_load);
    bmsg(G, sendto, fd, sa, "boot: want data at", base);
    bmsg(G, sendto, fd, sa, "boot: span", span);

    u64 got = NC(G, mmap_fn, base, span, BSD_PROT_RW,
                 BSD_MAP_ANON | BSD_MAP_PRIVATE, (u64)-1, 0);
    bmsg(G, sendto, fd, sa, "boot: hint mmap gave", got);

    if (got != base) {
        /* Landed somewhere else -- hand it back before forcing the address,
           otherwise the retry just leaks a 1.5MB mapping. */
        if (got != (u64)-1 && munmap_fn)
            NC(G, munmap_fn, got, span, 0, 0, 0, 0);

        got = NC(G, mmap_fn, base, span, BSD_PROT_RW,
                 BSD_MAP_FIXED | BSD_MAP_ANON | BSD_MAP_PRIVATE, (u64)-1, 0);
        bmsg(G, sendto, fd, sa, "boot: MAP_FIXED gave", got);
        if (got != base) return 0;
    }

    /* Copy .data's initial image out of the read-only code region. */
    const char *src = __data_load;
    char *dst = __data_start;
    u64 n = (u64)__data_end - (u64)__data_start;
    for (u64 i = 0; i < n; i++) dst[i] = src[i];

    /* Apply the relocations the discarded .rela.dyn would have done.
     *
     * -static-pie relaxes nearly every GOT reference into a RIP-relative lea,
     * but not all of them: a few real GOT slots survive holding link-time
     * addresses. Since .text is linked at 0, _start's runtime address is the
     * load base, and adding it to each non-zero slot is exactly the
     * R_X86_64_RELATIVE fixup a loader would apply.
     *
     * Zero slots are left alone -- those are the reserved .got.plt entries. */
    u64 img_base = (u64)&_start;
    u64 *gotp    = (u64 *)__got_start;
    u64 got_n    = ((u64)__got_end - (u64)__got_start) / 8;
    u64 fixed    = 0;
    for (u64 i = 0; i < got_n; i++) {
        if (gotp[i]) { gotp[i] += img_base; fixed++; }
    }
    bmsg(G, sendto, fd, sa, "boot: GOT entries relocated", fixed);

    /* Apply the dynamic relocations.
     *
     * -static-pie emits an R_X86_64_RELATIVE entry for every initialised
     * pointer, and linker.ld now keeps that table instead of discarding it.
     * Each entry means: the 64-bit word at (base + r_offset) should hold
     * (base + r_addend). Without this, a table like PCSX-ReARMed's interpreter
     * dispatch reads back as small link-time offsets and the first call through
     * one jumps to an address like 0x1.
     *
     * Order matters: this runs AFTER .data has been copied out, because most
     * targets live in .data / .data.rel.ro and would otherwise be overwritten
     * by the copy.
     *
     * Entry layout is the ELF64 Rela: { u64 r_offset; u64 r_info; s64 r_addend; }
     * with the relocation type in the low 32 bits of r_info. */
    {
        u64 *r   = (u64 *)__rela_start;
        u64 *end = (u64 *)__rela_end;
        u64 applied = 0, skipped = 0;

        while (r + 3 <= end) {
            u64 r_offset = r[0];
            u64 r_info   = r[1];
            u64 r_addend = r[2];

            if ((r_info & 0xFFFFFFFFu) == 8) {          /* R_X86_64_RELATIVE */
                *(u64 *)(img_base + r_offset) = img_base + r_addend;
                applied++;
            } else {
                /* Anything else would need symbol resolution, which a flat
                   image cannot do. Counted rather than ignored so an unexpected
                   relocation type is visible instead of silently wrong. */
                skipped++;
            }
            r += 3;
        }

        bmsg(G, sendto, fd, sa, "boot: relocations applied", applied);
        if (skipped)
            bmsg(G, sendto, fd, sa, "boot: RELOCATIONS SKIPPED (non-RELATIVE)", skipped);
    }

    /* Zero .bss. */
    char *b = __bss_start;
    u64 bn = (u64)__bss_end - (u64)__bss_start;
    for (u64 i = 0; i < bn; i++) b[i] = 0;

    return 1;
}

