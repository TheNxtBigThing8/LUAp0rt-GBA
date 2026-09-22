OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* Adapted from LuaPSX/linker.ld.
 *
 * THE ONLY CHANGE IS THE ADDITION OF MEASUREMENT MARKER SYMBOLS.
 *
 *   __text_start  __text_end  __rodata_start  __rodata_end
 *
 * Approved for M0 as MEASUREMENT ONLY. They are address labels; they emit no
 * bytes, allocate nothing, and move nothing. Output-section membership, PHDR
 * assignment, alignment and ordering are all byte-for-byte as LuaPSX.
 *
 * .rodata REMAINS inside the text PHDR and therefore REMAINS part of the
 * read-only JIT-backed mapping, exactly as in the proven layout. Nothing here
 * attempts ordinary-RW .rodata and nothing here redesigns the JIT architecture.
 * The markers exist so that `.text` and `.rodata` can be reported as separate
 * quantities -- which the unmodified script makes impossible, because after
 * linking there is only one output section and readelf can no longer tell the
 * two apart. Report section 16 requires them separately, so without this the
 * gba_cc_lut question at M1 could not be answered from the ELF at all.
 *
 * ---------------------------------------------------------------------------
 * Original LuaPSX commentary, retained because it is the rationale for a layout
 * that must not drift:
 *
 * The payload executes from a PS5 JIT shared-memory alias mapped
 * PROT_READ|PROT_EXECUTE. That mapping is not writable, so unlike a normal
 * flat binary the image cannot simply fold .data and .bss in with .text --
 * the first write to any global would fault.
 *
 * Instead the writable data gets its own region, placed immediately after the
 * code in virtual address space. _start maps anonymous RW memory over that
 * range before touching a single global, which keeps every reference from the
 * code RIP-relative and in range. The initial contents of .data ride along
 * inside the read-only code image at __data_load and are copied across during
 * that same bootstrap; .bss is NOLOAD and never occupies a byte of the blob.
 */

PHDRS
{
    text PT_LOAD FLAGS(5);   /* R-X: the JIT mapping                        */
    data PT_LOAD FLAGS(6);   /* RW-: mapped by _start, not present in file  */
}

SECTIONS
{
    . = 0;

    .text : {
        __text_start = .;
        *(.text._start)
        *(.text)
        *(.text.*)

        /* MEASUREMENT ONLY -- see the header. The ALIGN(16) below is inserted
           so that __text_end and __rodata_start are not the same address by
           accident of packing, which would make the split unreadable. It costs
           at most 15 bytes and changes no section membership. */
        . = ALIGN(16);
        __text_end = .;

        __rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        __rodata_end = .;

        . = ALIGN(16);
    } :text

    /* Dynamic relocations, KEPT rather than discarded.
     *
     * -static-pie emits R_X86_64_RELATIVE entries for every initialised
     * pointer. LuaGB and LuaMD discarded these and hand-patched their cores to
     * build such tables at runtime instead -- workable for a Game Boy core with
     * two offenders, hopeless for PCSX-ReARMed, whose interpreter dispatch,
     * GTE function tables and plugin structs are all initialised pointer data.
     * A GBA core will be in the same position.
     *
     * Keeping the section and applying it in _start is what a real loader does,
     * and it removes the entire class of bug. The section must live below
     * __data_load so it travels inside the blob. */
    . = ALIGN(8);
    .rela.dyn : {
        __rela_start = .;
        *(.rela.dyn)
        *(.rela.data*)
        *(.rela.rodata*)
        *(.rela.text*)
        *(.rela.got*)

        /* The four below are created UNCONDITIONALLY by ld, not by our code.
         *
         * -static-pie makes the link internally dynamic, so the x86-64 ELF
         * backend synthesises these in the linker's "dynobj" -- which is simply
         * the FIRST input object. That is why the orphan errors named m1main.o:
         * it is first in $(M1OBJS), not because apps/m1gpsp/main.c contains
         * anything. M0's map shows the identical four attributed to
         * build/main.o for exactly the same reason.
         *
         * All four are ZERO-SIZED, in M0 and in M1. M0 has shipped with them
         * since Stage 1 and is hardware-verified that way; M0 only links
         * because its LDFLAGS omit --orphan-handling=error. M1 adds that flag,
         * so the same empty sections become hard errors. Naming them here costs
         * nothing and moves nothing.
         *
         * They go INSIDE __rela_start/__rela_end deliberately. /DISCARD/ would
         * silently drop a future real relocation, and leaving them after
         * __rela_end (where ld had been putting them) leaves them outside the
         * range _start walks. Inside the markers, anything that ever lands here
         * is caught loudly: boot.inc reports "RELOCATIONS SKIPPED
         * (non-RELATIVE)" at runtime and tools/verify_reloc.py fails offline.
         *
         * For this target they should stay empty forever: .rela.bss needs a
         * copy reloc (impossible with no DT_NEEDED), .rela.tls needs __thread
         * (absent repo-wide), .rela.ifunc needs an ifunc resolver (none), and
         * .rela.plt needs a PLT (.plt is discarded, .got.plt is 0). */
        *(.rela.plt)
        *(.rela.bss)
        *(.rela.ifunc)
        *(.rela.tls)
        __rela_end = .;
    } :text
    . = ALIGN(16);

    /* Load address of .data: straight after the code, so `objcopy -O binary`
       emits one contiguous blob of code + initial data and nothing else. */
    __data_load = .;

    /* Virtual address of the RW region. Aligned to 64KB, not 16KB: the blob is
       code PLUS .data's initial image, and with a core this size that image ran
       past a 16KB-aligned boundary -- which would put __data_start inside the
       image and let _start's RW mapping clobber data it has not copied out yet.
       64KB still leaves __data_start inside a 2-mapping reservation.

       ALIGNMENT ALONE IS NOT ENOUGH: it only guarantees a gap when the code
       happens to end well below the boundary. Here it ended just under one, so
       the gap was a few bytes and .data's image still overran. A full extra
       64KB is added unconditionally, which makes the gap always larger than
       .data (~18KB) regardless of where the code lands.

       NOTE FOR M1: this unconditional +64KB is the "alignment gap" the size
       report measures. It is a MEASUREMENT TARGET ONLY at M0 and is
       deliberately NOT optimised here. */
    . = ALIGN(0x10000) + 0x10000;

    .data : AT(__data_load) {
        __data_start = .;
        *(.data)
        *(.data.*)

        /* Even -static-pie leaves a couple of real GOT slots that it could not
           relax into `lea`, and each holds a link-time address that the
           discarded .rela.dyn was supposed to fix up. _start adds the load base
           to every non-zero entry between these two symbols; without that, the
           first call through one jumps into unmapped memory. */
        . = ALIGN(8);          /* loose bytes in .data would misalign the walk */
        __got_start = .;
        *(.got)
        *(.got.plt)
        . = ALIGN(8);
        __got_end = .;

        . = ALIGN(16);
        __data_end = .;
    } :data

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)

        /* Same origin as the .rela.* four above: ld creates .dynbss in the
           dynobj under -static-pie regardless of need. It is zero-sized here
           (copy relocations require a shared object to import data from, and
           this image has none).

           It belongs INSIDE .bss rather than after __bss_end. Left as an orphan
           ld placed it at __image_end -- outside the region _start actually
           maps and zeroes -- so a non-empty .dynbss would have faulted on first
           touch. Here it is mapped and zeroed like any other BSS. */
        *(.dynbss)
        . = ALIGN(16);
        __bss_end = .;
    } :data

    /* Total span the Lua launcher must reserve: code + data + bss. */
    __image_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
        *(.interp)
        *(.dynsym)
        *(.dynstr)
        *(.dynamic)
        *(.gnu.hash)
        *(.gnu.version*)
        *(.rel.*)
        *(.plt)
        *(.plt.*)
    }
}
