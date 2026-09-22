# LUAport -- M0 Stage 1 (implementation-plan steps 1-3, plus Stage 1 packaging).
#
#   make            build the payload, report its size, and package the script
#   make link       link only, and list anything still undefined
#   make size       size report only (needs a prior build)
#   make lua        package the delivery script only (needs a prior build)
#   make verify     every offline check that does not need a console
#   make send       transmit to a console -- REQUIRES PS5_IP, see below
#   make clean
#
# SCOPE: this builds the M0 STAGE 5 PAYLOAD -- everything Stages 1-4 proved
# (delivery, video, renderer, microsecond clock, DualSense input), plus heap
# validation: a seven-test integrity check of the shim's bump allocator run once
# before video comes up. See the header of apps/m0diag/main.c for what it does
# and, more importantly, the list of what it deliberately still does not touch:
# no GBA key mapping, no emulator input adapter, no audio, no ROM, no gpSP, no
# mGBA, and nothing belonging to M1.
#
# STAGE 5 IS THE FINAL M0 STAGE. On a hardware pass this binary is the frozen
# M0 generic-runtime baseline that M1 builds against.
#
# Still no new translation unit at Stage 5, and no new object: the heap test is
# ~250 lines inside apps/m0diag/main.c, testing an allocator that already lives
# in runtime/shim.c. OBJS is unchanged from Stage 2, so every stage-to-stage
# size comparison measures the same four objects rather than a different set.
#
# Stage 4 was the FIRST stage to change lua/m0.lua.in functionally:
# scePadGetHandle needs the real userId, which only the Lua side can obtain.
# Stage 5 changes only that file's result labels. Transport is untouched at both
# stages -- same ports, same handshake, same ext_args size.
#
# `make send` is the ONLY target that talks to hardware. Everything else is
# offline, so the whole build and verification sequence can be run and reviewed
# before anything is transmitted. That separation is deliberate: it is what
# makes "build and check it, then decide" possible.
#
# Modelled on LuaPSX/Makefile with the PCSX core and its flags removed.

CC      ?= gcc
OBJCOPY ?= objcopy
READELF ?= readelf
OBJDUMP ?= objdump
NM      ?= nm
PYTHON  ?= python3

TARGET := m0diag
BUILD  := build

# The payload spans MULTIPLE JIT mappings: one caps at 256KB, they land
# adjacent, and code executes across the seam (verified on hardware in LuaPSX).
# The pool is 768KB on a fresh launch and less mid-session, so 512KB is the
# number to design against. Carried over unchanged as the M1 reference budget.
JIT_LIMIT := 524288

# Identical to LuaPSX's BASEFLAGS except that -mssse3 is dropped: it existed for
# PCSX-ReARMed's SIMD paths and nothing in the M0 closure uses SSSE3. Recorded
# as a deliberate difference so a later codegen comparison against LuaPSX is not
# mistaken for drift.
BASEFLAGS = -Os -ffreestanding -fno-stack-protector -fno-builtin \
            -fpie -mno-red-zone -fomit-frame-pointer -fcf-protection=none \
            -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables \
            -fno-strict-aliasing -fvisibility=hidden

# No psx/ include path exists, by design: any PSX dependency that survived
# extraction becomes a compile error here rather than a surprise later.
INCLUDES = -Iruntime/libc -Iruntime -Iapps/m0diag

SRCFLAGS = $(BASEFLAGS) $(INCLUDES) -Wall -Wno-unused-function

# -static-pie is not optional. Plain -static (even alongside -pie) yields
# ET_EXEC, and the linker then relaxes GOT loads into absolute immediates,
# which silently breaks every cross-TU function pointer. tools/check_image.sh
# exists to catch exactly that.
#
# -Wl,-Map is the LUAport addition: per-object-file size attribution is what
# makes "identify the large contributors" a measurement at M1 rather than a
# guess.
LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
          -Wl,--build-id=none -Wl,-z,norelro -Wl,-Map=$(BUILD)/$(TARGET).map

OBJS = $(BUILD)/main.o $(BUILD)/shim.o $(BUILD)/gfx.o $(BUILD)/platform.o

# boot.inc and fault.inc are #included into main.c, so they are dependencies
# exactly like a header. Leaving them out of LuaPSX's HEADERS once meant an edit
# to the relocation code did not rebuild main.o -- the payload shipped with the
# old boot path while the blob grew from the linker script change, which made it
# look like the fix had been applied when it had not.
HEADERS = runtime/core.h runtime/shim.h runtime/gfx.h runtime/tables.h \
          runtime/platform.h runtime/boot.inc runtime/fault.inc

LUA_TEMPLATE := lua/m0.lua.in
LUA_OUT      := $(BUILD)/$(TARGET).lua

all: $(BUILD)/$(TARGET).bin $(LUA_OUT)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/main.o: apps/m0diag/main.c $(HEADERS) | $(BUILD)
	$(CC) $(SRCFLAGS) -c $< -o $@

$(BUILD)/shim.o: runtime/shim.c $(HEADERS) | $(BUILD)
	$(CC) $(SRCFLAGS) -c $< -o $@

$(BUILD)/gfx.o: runtime/gfx.c $(HEADERS) | $(BUILD)
	$(CC) $(SRCFLAGS) -c $< -o $@

# Stage 2. PS5 VideoOut lives here and nowhere else; keeping it in its own
# object is what lets the size report attribute the cost of video bring-up
# separately from the Stage 1 baseline.
$(BUILD)/platform.o: runtime/platform.c $(HEADERS) | $(BUILD)
	$(CC) $(SRCFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) runtime/linker.ld | $(BUILD)
	$(CC) $(BASEFLAGS) $(LDFLAGS) -o $@ $(OBJS)

# Link-only target: the fastest way to see what glue is still missing without
# waiting on packaging.
#
# The linker's exit status is captured into $$status BEFORE anything is piped.
# The earlier form ran gcc into `grep | sed | sort`, so the recipe's status was
# sort's -- and sort exits 0 on empty input. That made every outcome look
# identical: a clean link printed nothing (the `|| echo "LINK OK"` could never
# fire), and a failure with no "undefined reference" text -- a bad flag, a
# linker-script error -- also printed nothing and still exited 0. The target
# could not distinguish success from failure, so its result was not reportable.
link: $(OBJS) | $(BUILD)
	@$(CC) $(BASEFLAGS) $(LDFLAGS) -o $(BUILD)/$(TARGET).elf $(OBJS) \
	     > $(BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(BUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(BUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "LINK OK: linker exit 0, no undefined references"

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf check
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin \
	   --map $(BUILD)/$(TARGET).map --budget $(JIT_LIMIT)

# An image with NO relocations means .rela.dyn was dropped again, and every
# function-pointer table would read back as a link-time offset. The check is
# therefore inverted: zero is the failure.
check: $(BUILD)/$(TARGET).elf
	@n=`$(READELF) -r $(BUILD)/$(TARGET).elf 2>/dev/null | grep -c R_X86_64_RELATIVE`; \
	 echo "check: $$n RELATIVE relocations kept (applied by _start at Stage 1)"; \
	 if [ "$$n" -eq 0 ]; then \
	     echo "FAIL: no relocations in image"; exit 1; \
	 fi

size: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin
	@$(PYTHON) tools/sizereport.py $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin \
	   --map $(BUILD)/$(TARGET).map --budget $(JIT_LIMIT)

# Stage 1 packaging.
#
# The blob is NOT embedded in the script. Luac0re's remote loader caps scripts
# at 500KB and hex encoding doubles the payload, so an embedded blob tops out
# near 240KB; the blob travels separately over TCP instead. mklua.py therefore
# substitutes only @@JIT_SIZE@@ and leaves the binary alone.
#
# The .elf is a dependency as well as the .bin because mklua.py reads
# __data_start and __bss_end out of the symbol table to compute the JIT
# reservation. That reservation runs to __data_start, NOT to the blob length --
# it has to cover .data's initial image, which _start copies out before mapping
# the RW region over that same address range.
#
# PC_IP is passed through: unset (or "auto") makes the console derive the log
# destination from its own subnet, which works on any network. Set it to a
# specific address for a unicast log:  make lua PC_IP=192.168.1.42
$(LUA_OUT): $(LUA_TEMPLATE) tools/mklua.py $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).elf
	@$(PYTHON) tools/mklua.py $(LUA_TEMPLATE) $(BUILD)/$(TARGET).bin $@ \
	   $(BUILD)/$(TARGET).elf

lua: $(LUA_OUT)

# The only target that touches hardware.
#
# Guarded on PS5_IP rather than defaulting to an address: a default would be
# wrong for everyone except whoever set it, and sending a payload to the wrong
# host is silent -- it looks exactly like a console that never responded.
send: $(LUA_OUT) $(BUILD)/$(TARGET).bin
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(LUA_OUT) $(BUILD)/$(TARGET).bin

# Everything checkable WITHOUT a console. Each of these caught a real bug in
# LuaPSX that would otherwise have cost a game relaunch to find.
verify: $(BUILD)/$(TARGET).elf
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(BUILD)/$(TARGET).elf
	@echo "== relocations =="
	@$(PYTHON) tools/verify_reloc.py $(BUILD)/$(TARGET).elf | tail -12
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(OBJS) | tail -4
	@echo "== psx leakage =="
	@$(PYTHON) tools/check_closure.py $(OBJS)

clean:
	rm -rf $(BUILD)

.PHONY: all clean check link size verify lua send

# =============================================================================
# M1 -- MINIMAL gpSP INTERPRETER CLOSURE, OFFLINE SIZE GATE
# =============================================================================
#
#   make m1              build + link Tier 1 and print the image size report
#   make m1-link         link only, and list anything still undefined
#   make m1-objs         compile only (used by the staged size measurement)
#   make m1-size         size report only (needs a prior m1 build)
#   make m1-stages       INCREMENTAL per-group object-size table
#   make m1-verify       every offline check for the M1 image
#   make m1-syms         top symbols from cpu.o / video.o / gba_memory.o
#   make m1-tier12       RETIRED -- savestate.c/input.c are now mandatory
#   make m1-gc           NON-AUTHORITATIVE --gc-sections variant, for the delta
#   make m1-lua          package the M1 delivery script only
#   make m1-send         transmit M1 to a console -- REQUIRES PS5_IP
#   make m1-clean        removes build/m1 and build/m1gc ONLY
#
# EVERYTHING ABOVE THIS LINE IS UNTOUCHED. M0's targets, flags, object list,
# paths and outputs are exactly as they were when the frozen Stage 5 baseline
# was measured, so `make clean && make` still reproduces it byte for byte. That
# is checked by `make m1-verify`, and it is the reason this is an additive block
# rather than a refactor: a shared-variable "cleanup" here would invalidate the
# baseline M1 is supposed to be measured against.
#
# M1's VERDICT IS OFFLINE AND ALREADY DECIDED. The size gate is `make m1` plus
# `make m1-verify`, and neither touches a console. That has not changed.
#
# `m1-send` was added AFTER the gate passed, and it is the ONLY M1 target that
# talks to hardware -- exactly as `send` is for M0. Its purpose is narrow: prove
# that the image which was MEASURED is an image that actually loads, maps across
# two JIT mappings, relocates and runs its fixture. It cannot widen M1's scope,
# because scope is a property of apps/m1gpsp/main.c (no ROM, no BIOS, no
# execute_arm, no video, no audio) and this target only delivers that file.
#
# Everything else here remains offline, so the whole build and verification
# sequence can still be run and reviewed before anything is transmitted.

CXX ?= g++

M1TARGET := m1gpsp
M1BUILD  := build/m1

# ---------------------------------------------------------------- includes --
#
# Order matters, and the first entry is the interesting one.
#
# adapters/gba/include/ is an M1-ONLY header shadow. It supplies three headers
# the gpSP closure needs and runtime/libc/ does not have:
#
#   inttypes.h    libretro-common/include/retro_common_api.h:95-99 includes
#                 <inttypes.h> and then `#error`s if PRId64 is undefined.
#                 runtime/libc/inttypes.h deliberately provides only <stdint.h>
#                 ("the PRI* macros are never used by the core" -- true for M0,
#                 false for gpSP). Hard compile failure without this.
#   sys/types.h   streams/file_stream.h:31 includes it unconditionally.
#   ctype.h       gpsp/main.c:21 includes it.
#
# They live HERE rather than in runtime/libc/ so that M0 compiles against the
# byte-identical header set it was measured with. Shadowing instead of editing
# is what keeps the M0 non-regression check meaningful.
#
# Without these three, the includes would silently resolve to the HOST glibc
# headers -- the build is -ffreestanding but NOT -nostdinc, so nothing else
# prevents it.
M1_SHIMINC  = -Iadapters/gba/include
M1_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m1gpsp
GPSP_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iadapters/gba \
                -Igpsp -Igpsp/libretro/libretro-common/include

# ------------------------------------------------------------------- flags --
#
# BASEFLAGS is shared with M0 deliberately: -Os, -ffreestanding, -fpie,
# -mno-red-zone, -fcf-protection=none, -fno-exceptions, -fno-unwind-tables,
# -fno-asynchronous-unwind-tables, -fno-strict-aliasing, -fvisibility=hidden.
# Using the same base is what makes the M1 and M0 numbers comparable.
#
# -Os is kept even though upstream gpSP builds at -O3 (gpsp/Makefile:563). That
# is CONSISTENCY WITH THE MEASURED BASELINE, not size optimisation -- and the
# report must state it, because a later performance-driven move to -O3 would
# materially increase .text.
#
# ONLY TWO DEFINES ARE ADDED, and no others:
#   -DINLINE=inline   gpsp/sound.h:40,45,50 declare `static INLINE` helpers.
#   -DNDEBUG          gpsp/serial_proto.c:25 includes <assert.h>.
#
# NOT DEFINED, and each would be actively wrong here:
#   HAVE_DYNAREC  the whole point -- see gpsp/Makefile.common:34-45. Leaving it
#                 undefined is also what turns the CPU/memory state arrays into
#                 ordinary .bss globals (gpsp/cpu.cc:1461-1477) and what keeps
#                 rom_translation_cache[10MB] / ram_translation_cache[512KB]
#                 (gpsp/cpu.h:153-154) undefined, since they are only DEFINED in
#                 the excluded cpu_threaded.c.
#   X86_ARCH      would enable __attribute__((regparm(2))) (gpsp/common.h:61-62),
#                 which is invalid on x86_64.
#   ARM_ARCH ARM64_ARCH MIPS_ARCH MMAP_JIT_CACHE _3DS VITA PSP   wrong target.
#   MSB_FIRST     x86_64 is little-endian; gpsp/common.h:29-41 auto-detects.
#
# NOTE the upstream build system is NOT reused. gpsp/Makefile:24-26 maps an
# x86_64 host onto its x86_32 dynarec configuration (HAVE_DYNAREC := 1,
# CPU_ARCH := x86_32, -m32). M1 intentionally does not define HAVE_DYNAREC and
# excludes the dynarec path entirely, so LUAport owns its flags outright.
M1DEFS   = -DINLINE=inline -DNDEBUG

M1FLAGS  = $(BASEFLAGS) $(M1_INCLUDES) -Wall -Wno-unused-function
GPSPFLAGS = $(BASEFLAGS) $(GPSP_INCLUDES) $(M1DEFS) -Wall -Wno-unused-function

# C++ is used for exactly two files, cpu.cc and video.cc (gpsp/Makefile.common:7-8).
# A census of both for `new`, `delete`, `std::`, `class `, `virtual `, `throw`,
# `catch`, `typeid`, `dynamic_cast` and `operator ` returns ZERO matches: they
# are C compiled as C++, and video.cc needs it only for function templates.
#
# So no libstdc++, no RTTI, no exceptions, no STL, no global constructors. The
# three extra flags make that structural instead of merely true today:
#   -fno-rtti                nothing uses RTTI
#   -fno-threadsafe-statics  video.cc:823,1941,1949,2099 declare function-local
#                            `static const` tables. They are constant-initialised
#                            so no guard should be emitted -- this makes
#                            __cxa_guard_acquire/__cxa_guard_release impossible.
#   -fno-use-cxa-atexit      guarantees no __cxa_atexit / __dso_handle
GPSPCXXFLAGS = $(GPSPFLAGS) -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit

# --orphan-handling=error is the M1 addition to the link line.
#
# runtime/linker.ld places neither .init_array nor .fini_array and does not
# discard them either. A static constructor would therefore produce an ORPHAN
# SECTION that the linker silently places somewhere, and _start would never call
# it -- a whole class of surprise that would be invisible in the size report.
# Turning orphans into a link failure converts that into a build error.
M1LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
            -Wl,--build-id=none -Wl,-z,norelro \
            -Wl,--orphan-handling=error \
            -Wl,-Map=$(M1BUILD)/$(M1TARGET).map

# ----------------------------------------------------------------- objects --
#
# LUAport-owned. runtime/shim.c is compiled UNMODIFIED and is byte-identical to
# LuaPSX/src/shim.c; M1 adds nothing to it. The three adapters are the entire
# LUAport-side cost of dropping the libretro frontend.
M1_OWN_OBJS = $(M1BUILD)/m1main.o \
              $(M1BUILD)/shim.o \
              $(M1BUILD)/gba_fixture.o \
              $(M1BUILD)/gba_filestream.o \
              $(M1BUILD)/gba_compat.o

# The Tier 1 gpSP interpreter closure: the ten files reachable from
# execute_arm() / update_gba(). Every one is on a real execution path, not a
# link-completeness filler -- see the M1 plan for the per-file citation.
M1_GPSP_CPU    = $(M1BUILD)/gpsp_cpu.o
M1_GPSP_MEM    = $(M1BUILD)/gpsp_main.o $(M1BUILD)/gpsp_gba_memory.o
M1_GPSP_VIDEO  = $(M1BUILD)/gpsp_video.o
M1_GPSP_SOUND  = $(M1BUILD)/gpsp_sound.o
M1_GPSP_SERIAL = $(M1BUILD)/gpsp_serial.o $(M1BUILD)/gpsp_serial_proto.o \
                 $(M1BUILD)/gpsp_rfu.o $(M1BUILD)/gpsp_gbp.o
M1_GPSP_CHEATS = $(M1BUILD)/gpsp_cheats.o

M1_GPSP_TIER1 = $(M1_GPSP_CPU) $(M1_GPSP_MEM) $(M1_GPSP_VIDEO) \
                $(M1_GPSP_SOUND) $(M1_GPSP_SERIAL) $(M1_GPSP_CHEATS)

# MANDATORY -- NOT the optional "Tier 2" the plan assumed.
#
# The plan listed savestate.c and input.c as an optional Tier 2 to be measured
# only after Tier 1 had a number. The first real link disproved that: Tier 1 as
# specified IS NOT LINK-COMPLETE. gpsp/cpu.cc, main.c, gba_memory.c and sound.c
# each define *_check_savestate / *_read_savestate / *_write_savestate, and
# those call the five BSON READER functions
#     bson_contains_key  bson_find_key  bson_read_int32
#     bson_read_int32_array  bson_read_bytes
# which are defined ONLY in gpsp/savestate.c (lines 25, 46, 67, 76, 100).
# (The bson_write_* counterparts are macros in savestate.h, so they inline into
# the callers and never appear as undefined symbols -- which is exactly why the
# READERS were the only BSON names the linker asked for.)
#
# There is NO upstream compile-time exclusion for this: grep finds zero #ifdef
# guards around any *_savestate function in cpu.cc, main.c, gba_memory.c,
# sound.c or input.c. The one #ifdef HAVE_DYNAREC in savestate.c (line 155)
# only guards the dynarec cache flush INSIDE gba_load_state, and M1 does not
# define HAVE_DYNAREC, so that branch compiles out and dynarec_enable stays
# undefined -- the tripwire in gba_fixture.c is preserved.
#
# Because M1 links whole objects with no --gc-sections, those savestate
# functions are already in the authoritative baseline whether or not anything
# calls them. Their callees must therefore be in it too. Adding the REAL
# upstream savestate.c is the honest resolution; a hand-written BSON stub would
# have understated the measurement by hiding cost the rule says we must pay.
#
# savestate.c then drags in input.c, and the closure stops there:
#   savestate.c : gba_load_state -> input_check_savestate / input_read_savestate
#                 gba_save_state -> input_write_savestate   (input.c:175/183/202)
#                 everything else it touches -- video_reload_counters,
#                 convert_palette, palette_ram, instruction_count, reg -- is
#                 already Tier 1.
#   input.c     : btn_map is `static const` (input.h:46), so it costs no
#                 external symbol. The ONLY new external is
#                 set_fastforward_override (libretro.c:388), owned by the
#                 frontend and now defined in adapters/gba/gba_fixture.c.
# No further file is pulled in. Measured as its own group by `make m1-stages`.
M1_GPSP_STATE = $(M1BUILD)/gpsp_savestate.o $(M1BUILD)/gpsp_input.o

M1OBJS = $(M1_OWN_OBJS) $(M1_GPSP_TIER1) $(M1_GPSP_STATE)

# EXCLUDED, and none of these may ever appear above:
#   cpu_threaded.c  memmap.c  gba_cc_lut.c  bios_data.S
#   libretro/libretro.c  libretro/libretro-common/*.c
#   x86/**  arm/**  mips/**  3ds/**  bios/**  tests/**  tools/**  jni/**
# `make m1-verify` proves the exclusion from the linked image rather than from
# this comment.

$(M1BUILD):
	mkdir -p $(M1BUILD)

# ---- LUAport-owned objects -------------------------------------------------

$(M1BUILD)/m1main.o: apps/m1gpsp/main.c $(HEADERS) | $(M1BUILD)
	$(CC) $(M1FLAGS) -c $< -o $@

$(M1BUILD)/shim.o: runtime/shim.c $(HEADERS) | $(M1BUILD)
	$(CC) $(M1FLAGS) -c $< -o $@

$(M1BUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M1BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M1BUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M1BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M1BUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M1BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects -----------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags. gpsp/** is never
# modified and its own Makefile is never invoked.

$(M1BUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M1BUILD)
	$(CXX) $(GPSPCXXFLAGS) -c $< -o $@

$(M1BUILD)/gpsp_video.o: gpsp/video.cc | $(M1BUILD)
	$(CXX) $(GPSPCXXFLAGS) -c $< -o $@

$(M1BUILD)/gpsp_main.o: gpsp/main.c | $(M1BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M1BUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M1BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M1BUILD)/gpsp_sound.o: gpsp/sound.c | $(M1BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M1BUILD)/gpsp_serial.o: gpsp/serial.c | $(M1BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M1BUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M1BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M1BUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M1BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M1BUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M1BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M1BUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M1BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M1BUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M1BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M1BUILD)/gpsp_input.o: gpsp/input.c | $(M1BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

m1-objs: $(M1OBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX). -nostdlib already prevents an implicit
# libstdc++, and linking through gcc means it cannot be reintroduced by a
# driver default. The C++ runtime audit says nothing needs it.
#
# NO -ffunction-sections, NO -fdata-sections, NO --gc-sections. Every object is
# named on the link line and is therefore included in its ENTIRETY. That is the
# authoritative measurement: it measures the claim actually being made -- "these
# files are the minimum closure" -- rather than the subset one fixture happens
# to reach, and it is what forces the sscanf dependency (gpsp/cheats.c:265) to
# be satisfied honestly instead of silently collected. See `m1-gc` for the
# non-authoritative comparison.
$(M1BUILD)/$(M1TARGET).elf: $(M1OBJS) runtime/linker.ld | $(M1BUILD)
	$(CC) $(BASEFLAGS) $(M1LDFLAGS) -o $@ $(M1OBJS)

m1-link: $(M1OBJS) | $(M1BUILD)
	@$(CC) $(BASEFLAGS) $(M1LDFLAGS) -o $(M1BUILD)/$(M1TARGET).elf $(M1OBJS) \
	     > $(M1BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(M1BUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M1BUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M1 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M1 LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M1 LINK OK: linker exit 0, no undefined references"

$(M1BUILD)/$(M1TARGET).bin: $(M1BUILD)/$(M1TARGET).elf
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M1BUILD)/$(M1TARGET).elf $@ \
	   --map $(M1BUILD)/$(M1TARGET).map --budget $(JIT_LIMIT) --arena 1048576

m1: $(M1BUILD)/$(M1TARGET).bin

m1-size: $(M1BUILD)/$(M1TARGET).elf $(M1BUILD)/$(M1TARGET).bin
	@$(PYTHON) tools/sizereport.py $(M1BUILD)/$(M1TARGET).elf $(M1BUILD)/$(M1TARGET).bin \
	   --map $(M1BUILD)/$(M1TARGET).map --budget $(JIT_LIMIT) --arena 1048576

# ---- incremental measurement ----------------------------------------------
#
# The implementation order calls for a measurement after each group. An
# intermediate LINK is not possible -- cpu.o alone references update_gba from
# main.c, so a partial object set has undefined symbols by construction -- so
# the per-group figures come from the OBJECTS, which is the honest way to
# attribute cost per subsystem. The final linked image is the authoritative
# number and is reported separately by `m1-size`.
m1-stages: $(M1OBJS)
	@$(PYTHON) tools/m1stages.py $(M1BUILD)

# ---- Tier 1 + Tier 2 -------------------------------------------------------
#
# RETIRED. This target used to link Tier 1 plus savestate.c/input.c into a
# separate ELF so the Tier 1 baseline was not overwritten by an optional extra.
# That distinction no longer exists: savestate.c and input.c are MANDATORY for
# link completeness (see M1_GPSP_STATE above), so they are in $(M1OBJS) and the
# `m1` image already IS the Tier 1+2 image. Keeping a target that silently
# reported the same number twice would invite mistaking one for a delta.
m1-tier12:
	@echo "m1-tier12 is retired: savestate.c and input.c are mandatory, not"
	@echo "optional -- the BSON reader symbols they define are referenced by"
	@echo "retained code in cpu.o/main.o/gba_memory.o/sound.o. They are part"
	@echo "of \$$(M1OBJS), so 'make m1' already measures Tier 1+2."
	@echo "Use 'make m1-stages' to see their cost as a separate group."

# ---- NON-AUTHORITATIVE gc-sections variant ---------------------------------
#
# Built into a SEPARATE directory so it can never be mistaken for the baseline.
# The delta against `m1` quantifies genuinely unreachable code and is the
# evidence base for a YELLOW cleanup. IT IS NOT THE GATE NUMBER.
M1GCBUILD := build/m1gc
M1GCFLAGS  = -ffunction-sections -fdata-sections

$(M1GCBUILD):
	mkdir -p $(M1GCBUILD)

m1-gc: | $(M1GCBUILD)
	@echo "=== NON-AUTHORITATIVE: --gc-sections variant ==="
	$(CC)  $(M1FLAGS)      $(M1GCFLAGS) -c apps/m1gpsp/main.c          -o $(M1GCBUILD)/m1main.o
	$(CC)  $(M1FLAGS)      $(M1GCFLAGS) -c runtime/shim.c              -o $(M1GCBUILD)/shim.o
	$(CC)  $(GPSPFLAGS)    $(M1GCFLAGS) -c adapters/gba/gba_fixture.c  -o $(M1GCBUILD)/gba_fixture.o
	$(CC)  $(GPSPFLAGS)    $(M1GCFLAGS) -c adapters/gba/gba_filestream.c -o $(M1GCBUILD)/gba_filestream.o
	$(CC)  $(GPSPFLAGS)    $(M1GCFLAGS) -c adapters/gba/gba_compat.c   -o $(M1GCBUILD)/gba_compat.o
	$(CXX) $(GPSPCXXFLAGS) $(M1GCFLAGS) -c gpsp/cpu.cc                 -o $(M1GCBUILD)/gpsp_cpu.o
	$(CXX) $(GPSPCXXFLAGS) $(M1GCFLAGS) -c gpsp/video.cc               -o $(M1GCBUILD)/gpsp_video.o
	$(CC)  $(GPSPFLAGS)    $(M1GCFLAGS) -c gpsp/main.c                 -o $(M1GCBUILD)/gpsp_main.o
	$(CC)  $(GPSPFLAGS)    $(M1GCFLAGS) -c gpsp/gba_memory.c           -o $(M1GCBUILD)/gpsp_gba_memory.o
	$(CC)  $(GPSPFLAGS)    $(M1GCFLAGS) -c gpsp/sound.c                -o $(M1GCBUILD)/gpsp_sound.o
	$(CC)  $(GPSPFLAGS)    $(M1GCFLAGS) -c gpsp/serial.c               -o $(M1GCBUILD)/gpsp_serial.o
	$(CC)  $(GPSPFLAGS)    $(M1GCFLAGS) -c gpsp/serial_proto.c         -o $(M1GCBUILD)/gpsp_serial_proto.o
	$(CC)  $(GPSPFLAGS)    $(M1GCFLAGS) -c gpsp/rfu.c                  -o $(M1GCBUILD)/gpsp_rfu.o
	$(CC)  $(GPSPFLAGS)    $(M1GCFLAGS) -c gpsp/gbp.c                  -o $(M1GCBUILD)/gpsp_gbp.o
	$(CC)  $(GPSPFLAGS)    $(M1GCFLAGS) -c gpsp/cheats.c               -o $(M1GCBUILD)/gpsp_cheats.o
	$(CC) $(BASEFLAGS) -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
	   -Wl,--build-id=none -Wl,-z,norelro -Wl,--gc-sections \
	   -Wl,-Map=$(M1GCBUILD)/$(M1TARGET).map \
	   -o $(M1GCBUILD)/$(M1TARGET).elf $(M1GCBUILD)/*.o
	$(OBJCOPY) -O binary $(M1GCBUILD)/$(M1TARGET).elf $(M1GCBUILD)/$(M1TARGET).bin
	@$(PYTHON) tools/sizereport.py $(M1GCBUILD)/$(M1TARGET).elf $(M1GCBUILD)/$(M1TARGET).bin \
	   --map $(M1GCBUILD)/$(M1TARGET).map --budget $(JIT_LIMIT)
	@echo "=== the number above is NOT the M1 gate: 'make m1-size' is ==="

# ---- verification ----------------------------------------------------------
#
# check_closure.py is run with --core gpsp, which allows the INTENTIONAL gpSP
# and GBA symbols while still rejecting PSX leakage -- and additionally forbids
# the dynarec symbol set outright. M0's invocation above is unchanged and stays
# strict: it must keep rejecting gpsp/gba_/arm7/tdmi.
m1-verify: $(M1BUILD)/$(M1TARGET).elf
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M1BUILD)/$(M1TARGET).elf
	@echo "== relocations =="
	@$(PYTHON) tools/verify_reloc.py $(M1BUILD)/$(M1TARGET).elf | tail -12
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M1OBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M1OBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M1BUILD)/$(M1TARGET).elf $(M1OBJS)

m1-syms: $(M1OBJS)
	@for o in gpsp_cpu gpsp_video gpsp_gba_memory; do \
	   echo "=== $$o.o -- 25 largest symbols ==="; \
	   $(NM) -C --size-sort -S $(M1BUILD)/$$o.o 2>/dev/null | tail -25; \
	   echo; \
	 done

# ---- hardware smoke-test delivery ------------------------------------------
#
# THE ONLY M1 TARGETS THAT TOUCH HARDWARE. Everything above is offline.
#
# The delivery architecture is M0's, reused wholesale and unmodified:
# tools/mklua.py for packaging, tools/send.py for transport, TCP 9026 for the
# script, UDP 9027 for the log, TCP 9028-9045 for the blob, same 'K' ACK
# handshake, same ext_args layout. Neither tool is changed and neither is
# forked -- both are invoked exactly as M0 invokes them.
#
# WHAT IS *NOT* SHARED IS THE SCRIPT. build/m0diag.lua is not reused, and could
# not be: it checks `step == 99` for success (apps/m0diag/main.c:2045), whereas
# the M1 fixture completes at `step == 8` (apps/m1gpsp/main.c:305). Reusing it
# would report every successful M1 run as INCOMPLETE. lua/m1.lua.in is the M1
# template and it documents the four differences from lua/m0.lua.in.
#
# THE JIT RESERVATION IS DERIVED, NEVER HARD-CODED. mklua.py reads __data_start
# out of the LINKED M1 ELF (0x50000) and rounds it to a 0x4000 page, so the
# reservation is a fact about the image that was actually built rather than a
# constant that can silently go stale. It also refuses to package a blob that
# overruns __data_start, which would clobber the .data init image. At 243,968 B
# against 327,680 B there is ample margin, and 0x50000 / 0x40000 -> 2 mappings.
M1_LUA_TEMPLATE := lua/m1.lua.in
M1_LUA_OUT      := $(M1BUILD)/$(M1TARGET).lua

# The .elf is a dependency as well as the .bin because mklua.py reads
# __data_start and __bss_end from its symbol table.
#
# PC_IP is passed through: unset (or "auto") makes the console derive the log
# destination from its own subnet.  make m1-lua PC_IP=192.168.1.42
$(M1_LUA_OUT): $(M1_LUA_TEMPLATE) tools/mklua.py \
               $(M1BUILD)/$(M1TARGET).bin $(M1BUILD)/$(M1TARGET).elf
	@$(PYTHON) tools/mklua.py $(M1_LUA_TEMPLATE) $(M1BUILD)/$(M1TARGET).bin $@ \
	   $(M1BUILD)/$(M1TARGET).elf

m1-lua: $(M1_LUA_OUT)

# Guarded on PS5_IP rather than defaulting to an address: a default would be
# wrong for everyone except whoever set it, and sending a payload to the wrong
# host is silent -- it looks exactly like a console that never responded.
#
# m1-verify is a PREREQUISITE, not a suggestion. It is the difference between
# "a blob exists" and "the authoritative image exists and still passes every
# offline check" -- position independence, relocation safety, symbol kinds, the
# closure/dynarec/frontend exclusion and the forbidden-symbol scan. Nothing is
# transmitted unless all of them pass first.
m1-send: $(M1_LUA_OUT) $(M1BUILD)/$(M1TARGET).bin m1-verify
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m1-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M1_LUA_OUT) $(M1BUILD)/$(M1TARGET).bin

m1-clean:
	rm -rf $(M1BUILD) $(M1GCBUILD)

.PHONY: m1 m1-objs m1-link m1-size m1-stages m1-verify m1-syms m1-tier12 \
        m1-gc m1-lua m1-send m1-clean

# =============================================================================
# M2 -- gpSP CORE INITIALIZATION, PROVEN BY ASSERTION
# =============================================================================
#
#   make m2              build + link the M2 fixture and print the size report
#   make m2-link         link only, and list anything still undefined
#   make m2-objs         compile only
#   make m2-size         size report only (needs a prior m2 build)
#   make m2-verify       every offline check for the M2 image
#   make m2-lua          package the M2 delivery script only
#   make m2-send         transmit M2 to a console -- REQUIRES PS5_IP
#   make m2-clean        removes build/m2 ONLY
#
# EVERYTHING ABOVE THIS LINE IS UNTOUCHED, for the second time. M0's and M1's
# targets, flags, object lists, paths and outputs are exactly as they were when
# their baselines were measured, so `make clean && make` and `make m1-clean &&
# make m1` still reproduce both byte for byte. This is an ADDITIVE block for the
# same reason the M1 block was: a shared-variable "cleanup" here would invalidate
# the very baselines M2 is required not to regress.
#
# M1 IS NOT REUSED AND NOT REPLACED. M2 gets its own fixture
# (apps/m2gpsp/main.c), its own build directory (build/m2) and its own objects,
# INCLUDING ITS OWN COPIES OF THE gpSP OBJECTS. Sharing build/m1/*.o would have
# been shorter, but it would couple the two images: a future M2-only flag change
# would silently rebuild the frozen M1 objects and the M1 non-regression check
# would stop meaning anything. The duplication is the price of keeping M1 frozen
# and independently buildable, which the milestone plan requires.
#
# WHAT M2 ADDS TO THE CLOSURE: exactly ONE new object, gba_probe.o. No new gpSP
# file is compiled -- init_sound() already lives in gpsp_sound.o, which M1
# already linked in its entirety (no --gc-sections). The M2 sound fix therefore
# costs ZERO additional .text.

M2TARGET := m2gpsp
M2BUILD  := build/m2

# ---------------------------------------------------------------- includes --
#
# M1's include set plus -Iadapters/gba, which is what lets apps/m2gpsp/main.c
# find gba_probe.h.
#
# THE TYPE BOUNDARY STILL HOLDS. adapters/gba/ contains only gba_compat.h and
# gba_probe.h at its top level; neither is a gpSP header and neither typedefs
# u64, so putting that directory on the fixture's include path cannot pull
# gpsp/common.h's `unsigned long long` typedef into a TU that already has
# runtime/core.h's `unsigned long`. gba_probe.h is deliberately written in plain
# `unsigned int` / `unsigned short` so it depends on no typedef from either side.
M2_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m2gpsp -Iadapters/gba

M2FLAGS = $(BASEFLAGS) $(M2_INCLUDES) -Wall -Wno-unused-function

# gpSP-side flags are M1's, REUSED VERBATIM ($(GPSPFLAGS)/$(GPSPCXXFLAGS)):
# same -Os, same -DINLINE=inline -DNDEBUG, and the same set of defines
# deliberately left undefined -- HAVE_DYNAREC above all. Using the same flags is
# what makes the M2 and M1 numbers directly comparable, and it is why M2 cannot
# quietly enable the dynarec: it does not own those flags.

M2LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
            -Wl,--build-id=none -Wl,-z,norelro \
            -Wl,--orphan-handling=error \
            -Wl,-Map=$(M2BUILD)/$(M2TARGET).map

# ----------------------------------------------------------------- objects --
#
# M1's object set, plus gba_probe.o. runtime/shim.c is compiled UNMODIFIED.
M2_OWN_OBJS = $(M2BUILD)/m2main.o \
              $(M2BUILD)/shim.o \
              $(M2BUILD)/gba_fixture.o \
              $(M2BUILD)/gba_filestream.o \
              $(M2BUILD)/gba_compat.o \
              $(M2BUILD)/gba_probe.o

M2_GPSP_OBJS = $(M2BUILD)/gpsp_cpu.o $(M2BUILD)/gpsp_main.o \
               $(M2BUILD)/gpsp_gba_memory.o $(M2BUILD)/gpsp_video.o \
               $(M2BUILD)/gpsp_sound.o $(M2BUILD)/gpsp_serial.o \
               $(M2BUILD)/gpsp_serial_proto.o $(M2BUILD)/gpsp_rfu.o \
               $(M2BUILD)/gpsp_gbp.o $(M2BUILD)/gpsp_cheats.o \
               $(M2BUILD)/gpsp_savestate.o $(M2BUILD)/gpsp_input.o

M2OBJS = $(M2_OWN_OBJS) $(M2_GPSP_OBJS)

# EXCLUDED, unchanged from M1 and none of these may ever appear above:
#   cpu_threaded.c  memmap.c  gba_cc_lut.c  bios_data.S
#   libretro/libretro.c  libretro/libretro-common/*.c
#   x86/**  arm/**  mips/**  3ds/**  bios/**  tests/**  tools/**  jni/**
# `make m2-verify` proves the exclusion from the linked image, not from this
# comment.

$(M2BUILD):
	mkdir -p $(M2BUILD)

# ---- LUAport-owned objects -------------------------------------------------

$(M2BUILD)/m2main.o: apps/m2gpsp/main.c adapters/gba/gba_probe.h $(HEADERS) | $(M2BUILD)
	$(CC) $(M2FLAGS) -c $< -o $@

$(M2BUILD)/shim.o: runtime/shim.c $(HEADERS) | $(M2BUILD)
	$(CC) $(M2FLAGS) -c $< -o $@

$(M2BUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M2BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M2BUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M2BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M2BUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M2BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

# The only NEW translation unit in M2. Compiled with the gpSP flags because it
# includes gpsp/common.h -- it is on the gpSP side of the type boundary.
$(M2BUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h | $(M2BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects -----------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, identical to M1's.
# gpsp/** is never modified and its own Makefile is never invoked.

$(M2BUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M2BUILD)
	$(CXX) $(GPSPCXXFLAGS) -c $< -o $@

$(M2BUILD)/gpsp_video.o: gpsp/video.cc | $(M2BUILD)
	$(CXX) $(GPSPCXXFLAGS) -c $< -o $@

$(M2BUILD)/gpsp_main.o: gpsp/main.c | $(M2BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M2BUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M2BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M2BUILD)/gpsp_sound.o: gpsp/sound.c | $(M2BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M2BUILD)/gpsp_serial.o: gpsp/serial.c | $(M2BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M2BUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M2BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M2BUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M2BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M2BUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M2BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M2BUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M2BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M2BUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M2BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M2BUILD)/gpsp_input.o: gpsp/input.c | $(M2BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

m2-objs: $(M2OBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- both exactly as M1.
# Every object is named on the link line and is therefore included in its
# ENTIRETY, so the M2 measurement stays directly comparable with the frozen M1
# one instead of measuring whatever subset this fixture happens to reach.
$(M2BUILD)/$(M2TARGET).elf: $(M2OBJS) runtime/linker.ld | $(M2BUILD)
	$(CC) $(BASEFLAGS) $(M2LDFLAGS) -o $@ $(M2OBJS)

m2-link: $(M2OBJS) | $(M2BUILD)
	@$(CC) $(BASEFLAGS) $(M2LDFLAGS) -o $(M2BUILD)/$(M2TARGET).elf $(M2OBJS) \
	     > $(M2BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(M2BUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M2BUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M2 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M2 LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M2 LINK OK: linker exit 0, no undefined references"

$(M2BUILD)/$(M2TARGET).bin: $(M2BUILD)/$(M2TARGET).elf
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M2BUILD)/$(M2TARGET).elf $@ \
	   --map $(M2BUILD)/$(M2TARGET).map --budget $(JIT_LIMIT) --arena 1048576

m2: $(M2BUILD)/$(M2TARGET).bin

m2-size: $(M2BUILD)/$(M2TARGET).elf $(M2BUILD)/$(M2TARGET).bin
	@$(PYTHON) tools/sizereport.py $(M2BUILD)/$(M2TARGET).elf $(M2BUILD)/$(M2TARGET).bin \
	   --map $(M2BUILD)/$(M2TARGET).map --budget $(JIT_LIMIT) --arena 1048576

# ---- verification ----------------------------------------------------------
#
# IDENTICAL IN SUBSTANCE TO m1-verify, with $(M2OBJS) and the M2 ELF substituted.
# Every one of the five tools is invoked UNMODIFIED and with the same arguments,
# so all thirteen required checks carry over unweakened: ET_DYN, no absolute
# function immediates, R_X86_64_RELATIVE-only validation, writable and in-range
# relocation targets, symbol-kind validation, dependency closure, zero
# unresolved symbols, dynarec/libretro/C++-runtime/static-init exclusion, and
# the .bss tripwire.
#
# NO ALLOWLIST IS BROADENED FOR M2. check_forbidden.py keeps exactly the five
# defined-only input.c entries it gained at M1. If M2 ever needs a new one, that
# is a finding to report, not a change to make quietly.
m2-verify: $(M2BUILD)/$(M2TARGET).elf
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M2BUILD)/$(M2TARGET).elf
	@echo "== relocations =="
	@$(PYTHON) tools/verify_reloc.py $(M2BUILD)/$(M2TARGET).elf | tail -12
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M2OBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M2OBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M2BUILD)/$(M2TARGET).elf $(M2OBJS)

# ---- hardware smoke-test delivery ------------------------------------------
#
# THE ONLY M2 TARGETS THAT TOUCH HARDWARE. Everything above is offline.
#
# The delivery architecture is M0's, reused wholesale and unmodified for the
# third time: tools/mklua.py for packaging, tools/send.py for transport, TCP 9026
# for the script, UDP 9027 for the log, TCP 9028-9045 for the blob, same 'K' ACK
# handshake, same ext_args layout. Neither tool is changed and neither is forked.
#
# THE SCRIPT IS NOT SHARED. build/m1/m1gpsp.lua cannot be reused: it checks
# `step == 8` for success (apps/m1gpsp/main.c:305), whereas the M2 fixture
# completes at `step == 34` (apps/m2gpsp/main.c). Reusing it would report every
# successful M2 run as INCOMPLETE. lua/m2.lua.in is the M2 template.
#
# THE JIT RESERVATION IS DERIVED, NEVER HARD-CODED. mklua.py reads __data_start
# out of the LINKED M2 ELF and rounds it to a 0x4000 page, so the reservation is
# a fact about the image that was actually built rather than a constant that can
# go stale as M2 grows.
M2_LUA_TEMPLATE := lua/m2.lua.in
M2_LUA_OUT      := $(M2BUILD)/$(M2TARGET).lua

$(M2_LUA_OUT): $(M2_LUA_TEMPLATE) tools/mklua.py \
               $(M2BUILD)/$(M2TARGET).bin $(M2BUILD)/$(M2TARGET).elf
	@$(PYTHON) tools/mklua.py $(M2_LUA_TEMPLATE) $(M2BUILD)/$(M2TARGET).bin $@ \
	   $(M2BUILD)/$(M2TARGET).elf

m2-lua: $(M2_LUA_OUT)

# m2-verify is a PREREQUISITE, not a suggestion -- same rule as m1-send.
# Nothing is transmitted unless every offline check passes first.
m2-send: $(M2_LUA_OUT) $(M2BUILD)/$(M2TARGET).bin m2-verify
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m2-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M2_LUA_OUT) $(M2BUILD)/$(M2TARGET).bin

m2-clean:
	rm -rf $(M2BUILD)

.PHONY: m2 m2-objs m2-link m2-size m2-verify m2-lua m2-send m2-clean

# =============================================================================
# M3 -- BIOS HANDLING, PROVEN BY ASSERTION
# =============================================================================
#
#   make m3                build + link the M3 fixture and print the size report
#   make m3-link           link only, and list anything still undefined
#   make m3-objs           compile only
#   make m3-size           size report only (needs a prior m3 build)
#   make m3-verify         every offline check for the M3 image
#   make m3-lua            package the M3 delivery script only
#   make m3-upload-bios    upload a user-supplied BIOS -- REQUIRES PS5_IP + BIOS
#   make m3-send           transmit M3 to a console -- REQUIRES PS5_IP
#   make m3-clean          removes build/m3 ONLY
#
# EVERYTHING ABOVE THIS LINE IS UNTOUCHED, for the third time. M0's, M1's and
# M2's targets, flags, object lists, paths and outputs are exactly as they were
# when their baselines were measured, so `make clean && make`, `make m1-clean &&
# make m1` and `make m2-clean && make m2` still reproduce all three byte for
# byte. This is an ADDITIVE block for the same reason the M1 and M2 blocks were:
# a shared-variable "cleanup" here would invalidate the very baselines M3 is
# required not to regress.
#
# M2 IS NOT REUSED AND NOT REPLACED. M3 gets its own fixture
# (apps/m3gpsp/main.c), its own build directory (build/m3) and its own objects,
# INCLUDING ITS OWN COPIES OF THE gpSP OBJECTS AND OF gba_probe.o. Sharing
# build/m2/*.o would have been shorter, but it would couple the two images: a
# future M3-only flag change would silently rebuild the frozen M2 objects and the
# M2 non-regression check would stop meaning anything.
#
# WHAT M3 ADDS TO THE CLOSURE: exactly ONE new object, gba_bios.o. No new gpSP
# file is compiled -- load_bios() already lives in gpsp_gba_memory.o and all five
# filestream entry points already live in gba_filestream.o, both of which M1 and
# M2 already linked in their entirety (no --gc-sections). bios_rom is 16,384
# bytes of .bss that is ALREADY PRESENT in the M2 image. The BIOS loader
# therefore costs ZERO additional .text for the loading itself, and M3's growth
# is almost entirely its own validation and diagnostics.
#
# NO BIOS IS EMBEDDED. gpsp/bios_data.S stays excluded exactly as at M1 and M2:
# its open_gba_bios_rom symbol is a check_forbidden.py LIBRETRO_PATTERNS entry,
# so linking it would force an allowlist widening, and it would add 16,384 bytes
# of .data to every transfer. The BIOS is user-supplied and uploaded out of band.

M3TARGET := m3gpsp
M3BUILD  := build/m3

# ---------------------------------------------------------------- includes --
#
# M2's include set with -Iapps/m3gpsp substituted for -Iapps/m2gpsp.
# -Iadapters/gba is what lets apps/m3gpsp/main.c find BOTH gba_probe.h (M2's,
# reused unmodified) and gba_bios.h (M3's).
#
# THE TYPE BOUNDARY STILL HOLDS, and M3 does not weaken it. adapters/gba/
# contains only gba_compat.h, gba_probe.h and now gba_bios.h at its top level;
# none is a gpSP header and none typedefs u64, so putting that directory on the
# fixture's include path cannot pull gpsp/common.h's `unsigned long long`
# typedef into a TU that already has runtime/core.h's `unsigned long`.
# gba_bios.h is deliberately written in plain `unsigned int` / `int` /
# `const char *` so it depends on no typedef from either side.
M3_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m3gpsp -Iadapters/gba

M3FLAGS = $(BASEFLAGS) $(M3_INCLUDES) -Wall -Wno-unused-function

# gpSP-side flags are M1's, REUSED VERBATIM ($(GPSPFLAGS)/$(GPSPCXXFLAGS)):
# same -Os, same -DINLINE=inline -DNDEBUG, and the same set of defines
# deliberately left undefined -- HAVE_DYNAREC above all. Using the same flags is
# what makes the M3, M2 and M1 numbers directly comparable, and it is why M3
# cannot quietly enable the dynarec: it does not own those flags.

M3LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
            -Wl,--build-id=none -Wl,-z,norelro \
            -Wl,--orphan-handling=error \
            -Wl,-Map=$(M3BUILD)/$(M3TARGET).map

# ----------------------------------------------------------------- objects --
#
# M2's object set, plus gba_bios.o. runtime/shim.c, gba_probe.c, gba_fixture.c,
# gba_filestream.c and gba_compat.c are all compiled UNMODIFIED.
M3_OWN_OBJS = $(M3BUILD)/m3main.o \
              $(M3BUILD)/shim.o \
              $(M3BUILD)/gba_fixture.o \
              $(M3BUILD)/gba_filestream.o \
              $(M3BUILD)/gba_compat.o \
              $(M3BUILD)/gba_probe.o \
              $(M3BUILD)/gba_bios.o

M3_GPSP_OBJS = $(M3BUILD)/gpsp_cpu.o $(M3BUILD)/gpsp_main.o \
               $(M3BUILD)/gpsp_gba_memory.o $(M3BUILD)/gpsp_video.o \
               $(M3BUILD)/gpsp_sound.o $(M3BUILD)/gpsp_serial.o \
               $(M3BUILD)/gpsp_serial_proto.o $(M3BUILD)/gpsp_rfu.o \
               $(M3BUILD)/gpsp_gbp.o $(M3BUILD)/gpsp_cheats.o \
               $(M3BUILD)/gpsp_savestate.o $(M3BUILD)/gpsp_input.o

M3OBJS = $(M3_OWN_OBJS) $(M3_GPSP_OBJS)

# EXCLUDED, unchanged from M1 and M2 and none of these may ever appear above:
#   cpu_threaded.c  memmap.c  gba_cc_lut.c  bios_data.S
#   libretro/libretro.c  libretro/libretro-common/*.c
#   x86/**  arm/**  mips/**  3ds/**  bios/**  tests/**  tools/**  jni/**
# bios_data.S is worth naming twice at M3: it is the one exclusion this
# milestone might plausibly have been tempted to relax, and it was not.
# `make m3-verify` proves the exclusion from the linked image, not this comment.

$(M3BUILD):
	mkdir -p $(M3BUILD)

# ---- LUAport-owned objects -------------------------------------------------

$(M3BUILD)/m3main.o: apps/m3gpsp/main.c adapters/gba/gba_probe.h \
                     adapters/gba/gba_bios.h $(HEADERS) | $(M3BUILD)
	$(CC) $(M3FLAGS) -c $< -o $@

$(M3BUILD)/shim.o: runtime/shim.c $(HEADERS) | $(M3BUILD)
	$(CC) $(M3FLAGS) -c $< -o $@

$(M3BUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M3BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M3BUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M3BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M3BUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M3BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

# M2's probe TU, compiled from UNMODIFIED source into M3's own build directory.
$(M3BUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h | $(M3BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

# The only NEW translation unit in M3. Compiled with the gpSP flags because it
# includes gpsp/common.h -- it is on the gpSP side of the type boundary.
# $(GPSP_INCLUDES) already carries -Igpsp/libretro/libretro-common/include,
# which is where its <streams/file_stream.h> comes from.
$(M3BUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h | $(M3BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects -----------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, identical to M1's and
# M2's. gpsp/** is never modified and its own Makefile is never invoked.

$(M3BUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M3BUILD)
	$(CXX) $(GPSPCXXFLAGS) -c $< -o $@

$(M3BUILD)/gpsp_video.o: gpsp/video.cc | $(M3BUILD)
	$(CXX) $(GPSPCXXFLAGS) -c $< -o $@

$(M3BUILD)/gpsp_main.o: gpsp/main.c | $(M3BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M3BUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M3BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M3BUILD)/gpsp_sound.o: gpsp/sound.c | $(M3BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M3BUILD)/gpsp_serial.o: gpsp/serial.c | $(M3BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M3BUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M3BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M3BUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M3BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M3BUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M3BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M3BUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M3BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M3BUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M3BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

$(M3BUILD)/gpsp_input.o: gpsp/input.c | $(M3BUILD)
	$(CC) $(GPSPFLAGS) -c $< -o $@

m3-objs: $(M3OBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- exactly as M1/M2.
# Every object is named on the link line and is therefore included in its
# ENTIRETY, so the M3 measurement stays directly comparable with the frozen M2
# one instead of measuring whatever subset this fixture happens to reach.
$(M3BUILD)/$(M3TARGET).elf: $(M3OBJS) runtime/linker.ld | $(M3BUILD)
	$(CC) $(BASEFLAGS) $(M3LDFLAGS) -o $@ $(M3OBJS)

m3-link: $(M3OBJS) | $(M3BUILD)
	@$(CC) $(BASEFLAGS) $(M3LDFLAGS) -o $(M3BUILD)/$(M3TARGET).elf $(M3OBJS) \
	     > $(M3BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(M3BUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M3BUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M3 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M3 LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M3 LINK OK: linker exit 0, no undefined references"

$(M3BUILD)/$(M3TARGET).bin: $(M3BUILD)/$(M3TARGET).elf
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M3BUILD)/$(M3TARGET).elf $@ \
	   --map $(M3BUILD)/$(M3TARGET).map --budget $(JIT_LIMIT) --arena 1048576

m3: $(M3BUILD)/$(M3TARGET).bin

m3-size: $(M3BUILD)/$(M3TARGET).elf $(M3BUILD)/$(M3TARGET).bin
	@$(PYTHON) tools/sizereport.py $(M3BUILD)/$(M3TARGET).elf $(M3BUILD)/$(M3TARGET).bin \
	   --map $(M3BUILD)/$(M3TARGET).map --budget $(JIT_LIMIT) --arena 1048576

# ---- verification ----------------------------------------------------------
#
# IDENTICAL IN SUBSTANCE TO m2-verify, with $(M3OBJS) and the M3 ELF substituted.
# Every one of the five tools is invoked UNMODIFIED and with the same arguments,
# so all thirteen required checks carry over unweakened: ET_DYN, no absolute
# function immediates, R_X86_64_RELATIVE-only validation, writable and in-range
# relocation targets, symbol-kind validation, dependency closure, zero
# unresolved symbols, dynarec/libretro/C++-runtime/static-init exclusion, and
# the .bss tripwire.
#
# NO ALLOWLIST IS BROADENED FOR M3. check_forbidden.py keeps exactly the five
# defined-only input.c entries it gained at M1 and the five filestream entries.
# M3's new symbols were named with that gate in mind: every one is m3_bios_* or
# bios_*, and NONE contains the LIBRETRO_PATTERNS substring "path_" -- which is
# why the BIOS candidate accessors are called m3_bios_candidate_name rather than
# the more obvious m3_bios_path_name. If M3 ever needs a new allowlist entry,
# that is a finding to report, not a change to make quietly.
m3-verify: $(M3BUILD)/$(M3TARGET).elf
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M3BUILD)/$(M3TARGET).elf
	@echo "== relocations =="
	@$(PYTHON) tools/verify_reloc.py $(M3BUILD)/$(M3TARGET).elf | tail -12
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M3OBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M3OBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M3BUILD)/$(M3TARGET).elf $(M3OBJS)

# ---- hardware smoke-test delivery ------------------------------------------
#
# THE ONLY M3 TARGETS THAT TOUCH HARDWARE. Everything above is offline.
#
# The delivery architecture is M0's, reused wholesale and unmodified for the
# fourth time: tools/mklua.py for packaging, tools/send.py for transport, TCP
# 9026 for the script, UDP 9027 for the log, TCP 9028-9045 for the blob, same
# 'K' ACK handshake, same ext_args layout. Neither tool is changed nor forked.
#
# THE SCRIPT IS NOT SHARED. build/m2/m2gpsp.lua cannot be reused: it checks
# `step == 34` for success, whereas the M3 fixture completes at `step == 54`.
# Reusing it would report every successful M3 run as INCOMPLETE. lua/m3.lua.in
# is the M3 template.
#
# THE JIT RESERVATION IS DERIVED, NEVER HARD-CODED. mklua.py reads __data_start
# out of the LINKED M3 ELF and rounds it to a 0x4000 page, so the reservation is
# a fact about the image that was actually built rather than a constant that can
# go stale as M3 grows.
M3_LUA_TEMPLATE := lua/m3.lua.in
M3_LUA_OUT      := $(M3BUILD)/$(M3TARGET).lua

$(M3_LUA_OUT): $(M3_LUA_TEMPLATE) tools/mklua.py \
               $(M3BUILD)/$(M3TARGET).bin $(M3BUILD)/$(M3TARGET).elf
	@$(PYTHON) tools/mklua.py $(M3_LUA_TEMPLATE) $(M3BUILD)/$(M3TARGET).bin $@ \
	   $(M3BUILD)/$(M3TARGET).elf

m3-lua: $(M3_LUA_OUT)

# ---- BIOS upload -----------------------------------------------------------
#
# A SEPARATE, EXPLICIT, USER-DRIVEN ACTION. No BIOS is embedded in the image or
# in the delivery script, and this target is never a prerequisite of m3-send --
# uploading someone's BIOS must be something they ask for by name.
#
# The destination is the PRIMARY candidate that apps/m3gpsp/main.c tries first
# (adapters/gba/gba_bios.c:107). To place the FALLBACK copy instead, override:
#     make m3-upload-bios PS5_IP=<addr> BIOS=<file> \
#          M3_BIOS_REMOTE=/savedata0/bios/gba_bios.bin
#
# --expect-size 16384 is a PC-SIDE gate that refuses to transfer a file the
# console's step-50 gate would reject anyway. It is a convenience, NOT the
# authority: the binding size check is the one the fixture performs on the
# console against the file it actually opened.
M3_BIOS_REMOTE ?= /temp0/gba_bios.bin

m3-upload-bios:
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m3-upload-bios PS5_IP=<console address> BIOS=<file>)
endif
ifndef BIOS
	$(error BIOS is not set -- use: make m3-upload-bios PS5_IP=<console address> BIOS=<path to a 16384-byte gba_bios.bin>)
endif
	$(PYTHON) tools/upload.py $(PS5_IP) $(BIOS) $(M3_BIOS_REMOTE) --expect-size 16384

# m3-verify is a PREREQUISITE, not a suggestion -- same rule as m1-send/m2-send.
# Nothing is transmitted unless every offline check passes first.
m3-send: $(M3_LUA_OUT) $(M3BUILD)/$(M3TARGET).bin m3-verify
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m3-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M3_LUA_OUT) $(M3BUILD)/$(M3TARGET).bin

m3-clean:
	rm -rf $(M3BUILD)

.PHONY: m3 m3-objs m3-link m3-size m3-verify m3-lua m3-upload-bios m3-send m3-clean

# =============================================================================
# M4 -- CARTRIDGE (ROM) LOADING, PROVEN BY ASSERTION
# =============================================================================
#
#   make m4                build + link the M4 fixture and print the size report
#   make m4-link           link only, and list anything still undefined
#   make m4-objs           compile only
#   make m4-size           size report only (needs a prior m4 build)
#   make m4-verify         every offline check for the M4 image
#   make m4-lua            package the M4 delivery script only
#   make m4-upload-rom     upload a user-supplied ROM -- REQUIRES PS5_IP + ROM
#   make m4-send           transmit M4 to a console -- REQUIRES PS5_IP
#   make m4-clean          removes build/m4 ONLY
#
# THE BIOS UPLOAD TARGET IS NOT DUPLICATED. M4 loads a BIOS as well as a ROM,
# but `make m3-upload-bios PS5_IP=<addr> BIOS=<file>` already places it at the
# same /temp0/gba_bios.bin the M4 fixture probes first. Forking that target
# would be a second copy of one scp-equivalent with no behavioural difference.
#
# EVERYTHING ABOVE THIS LINE IS UNTOUCHED, for the fourth time. M0's, M1's, M2's
# and M3's targets, flags, object lists, paths and outputs are exactly as they
# were when their baselines were measured, so `make clean && make`, `make
# m1-clean && make m1`, `make m2-clean && make m2` and `make m3-clean && make
# m3` still reproduce all four byte for byte. This is an ADDITIVE block for
# exactly the reason the M1, M2 and M3 blocks were.
#
# M3 IS NOT REUSED AND NOT REPLACED. M4 gets its own fixture
# (apps/m4gpsp/main.c), its own build directory (build/m4) and its own objects,
# INCLUDING ITS OWN COPIES OF THE gpSP OBJECTS, gba_probe.o AND gba_bios.o.
# At M4 that separation stops being mere hygiene and becomes REQUIRED: M4's gpSP
# objects are compiled with -DROM_BUFFER_SIZE=2 (see below), so they are NOT
# interchangeable with M3's. Sharing build/m3/*.o would silently rebuild the
# frozen M3 objects with a different -D and destroy the M3 baseline.
#
# WHAT M4 ADDS TO THE CLOSURE: exactly ONE new object, gba_rom.o. No new gpSP
# file is compiled -- load_gamepak(), load_gamepak_raw() and
# init_gamepak_buffer() all already live in gpsp_gba_memory.o, which M1, M2 and
# M3 each linked in its ENTIRETY (no --gc-sections). The cartridge code has
# therefore been present and linked since M1; M4's growth is its own assertions,
# inspectors and diagnostic strings, not the loader.
#
# NO ROM IS EMBEDDED, exactly as no BIOS was embedded at M3. The cartridge is
# user-supplied and uploaded out of band by m4-upload-rom, so it contributes
# ZERO bytes to .text/.rodata/.data and nothing copyrighted enters the image or
# the delivery script.

M4TARGET := m4gpsp
M4BUILD  := build/m4

# ---------------------------------------------------------------- includes --
#
# M3's include set with -Iapps/m4gpsp substituted for -Iapps/m3gpsp.
# -Iadapters/gba is what lets apps/m4gpsp/main.c find all three adapter headers:
# gba_probe.h (M2's, reused unmodified), gba_bios.h (M3's, reused unmodified)
# and gba_rom.h (M4's).
#
# THE TYPE BOUNDARY STILL HOLDS. adapters/gba/ now contains gba_compat.h,
# gba_probe.h, gba_bios.h and gba_rom.h at its top level; none is a gpSP header
# and none typedefs u64, so putting that directory on the fixture's include path
# still cannot pull gpsp/common.h's `unsigned long long` typedef into a TU that
# already has runtime/core.h's `unsigned long`. gba_rom.h is written in plain
# `unsigned int` / `int` / `const char *` for that reason.
M4_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m4gpsp -Iadapters/gba

M4FLAGS = $(BASEFLAGS) $(M4_INCLUDES) -Wall -Wno-unused-function

# ---- the one flag M4 adds, and why --------------------------------------
#
# init_gamepak_buffer() (gpsp/gba_memory.c:2359-2371) is GREEDY BY DESIGN: it
# loops `while (gamepak_buffer_count < ROM_BUFFER_SIZE)` calling malloc(1 MB)
# until one FAILS. gpsp/gpsp_config.h:10-11 defaults ROM_BUFFER_SIZE to 32, so
# against runtime/shim.c's bump allocator -- whose free() is a no-op -- the
# default would consume the WHOLE arena at whatever size it is set, leaving zero
# measurable headroom and making the arena report meaningless.
#
# Bounding it with -DROM_BUFFER_SIZE=2 is a SUPPORTED UPSTREAM CONFIGURATION,
# not a hack: gpsp/Makefile:451 uses 16 for PS2 and gpsp/Makefile:496 uses 4 for
# small-cache targets. Two 1 MB blocks hold any ROM up to 2 MB, which covers the
# 4,096-byte test cartridge with room to spare, and it leaves the 4 MB arena
# with genuine MEASURED headroom that step 79 reports.
#
# ROM_BUFFER_SIZE IS SAFE TO VARY PER IMAGE. It appears exactly twice in the
# tree -- the gpsp_config.h default and the loop bound at gba_memory.c:2364 --
# and sizes NO struct and NO array (gamepak_buffers[] is a fixed [32]), so there
# is no ODR hazard between TUs. It is applied to every M4 gpSP-side object so
# all of them agree.
M4_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M4_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M4LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
            -Wl,--build-id=none -Wl,-z,norelro \
            -Wl,--orphan-handling=error \
            -Wl,-Map=$(M4BUILD)/$(M4TARGET).map

# ----------------------------------------------------------------- objects --
#
# M3's object set, plus gba_rom.o. runtime/shim.c, gba_probe.c, gba_bios.c,
# gba_fixture.c, gba_filestream.c and gba_compat.c are all compiled UNMODIFIED.
M4_OWN_OBJS = $(M4BUILD)/m4main.o \
              $(M4BUILD)/shim.o \
              $(M4BUILD)/gba_fixture.o \
              $(M4BUILD)/gba_filestream.o \
              $(M4BUILD)/gba_compat.o \
              $(M4BUILD)/gba_probe.o \
              $(M4BUILD)/gba_bios.o \
              $(M4BUILD)/gba_rom.o

M4_GPSP_OBJS = $(M4BUILD)/gpsp_cpu.o $(M4BUILD)/gpsp_main.o \
               $(M4BUILD)/gpsp_gba_memory.o $(M4BUILD)/gpsp_video.o \
               $(M4BUILD)/gpsp_sound.o $(M4BUILD)/gpsp_serial.o \
               $(M4BUILD)/gpsp_serial_proto.o $(M4BUILD)/gpsp_rfu.o \
               $(M4BUILD)/gpsp_gbp.o $(M4BUILD)/gpsp_cheats.o \
               $(M4BUILD)/gpsp_savestate.o $(M4BUILD)/gpsp_input.o

M4OBJS = $(M4_OWN_OBJS) $(M4_GPSP_OBJS)

# EXCLUDED, unchanged from M1, M2 and M3 and none of these may ever appear above:
#   cpu_threaded.c  memmap.c  gba_cc_lut.c  bios_data.S
#   libretro/libretro.c  libretro/libretro-common/*.c
#   x86/**  arm/**  mips/**  3ds/**  bios/**  tests/**  tools/**  jni/**
# cpu_threaded.c is worth naming twice at M4: it is the dynarec, and M4 is the
# first milestone with a real cartridge resident in memory, so it is the first
# point at which "just translate a block" is even superficially tempting. It was
# not linked. `make m4-verify` proves the exclusion from the linked image.

$(M4BUILD):
	mkdir -p $(M4BUILD)

# ---- LUAport-owned objects -------------------------------------------------

$(M4BUILD)/m4main.o: apps/m4gpsp/main.c adapters/gba/gba_probe.h \
                     adapters/gba/gba_bios.h adapters/gba/gba_rom.h \
                     $(HEADERS) | $(M4BUILD)
	$(CC) $(M4FLAGS) -c $< -o $@

$(M4BUILD)/shim.o: runtime/shim.c $(HEADERS) | $(M4BUILD)
	$(CC) $(M4FLAGS) -c $< -o $@

$(M4BUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

$(M4BUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

$(M4BUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

# M2's probe TU, compiled from UNMODIFIED source into M4's own build directory.
$(M4BUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

# M3's BIOS TU, compiled from UNMODIFIED source into M4's own build directory.
$(M4BUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

# The only NEW translation unit in M4. Compiled with the gpSP flags because it
# includes gpsp/common.h -- it is on the gpSP side of the type boundary.
# $(GPSP_INCLUDES) already carries -Igpsp/libretro/libretro-common/include,
# which is where its <streams/file_stream.h> comes from.
$(M4BUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects -----------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags: M1's, plus M4's single
# -DROM_BUFFER_SIZE=2. gpsp/** is never modified and its own Makefile is never
# invoked.

$(M4BUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M4BUILD)
	$(CXX) $(M4_GPSPCXXFLAGS) -c $< -o $@

$(M4BUILD)/gpsp_video.o: gpsp/video.cc | $(M4BUILD)
	$(CXX) $(M4_GPSPCXXFLAGS) -c $< -o $@

$(M4BUILD)/gpsp_main.o: gpsp/main.c | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

$(M4BUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

$(M4BUILD)/gpsp_sound.o: gpsp/sound.c | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

$(M4BUILD)/gpsp_serial.o: gpsp/serial.c | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

$(M4BUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

$(M4BUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

$(M4BUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

$(M4BUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

$(M4BUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

$(M4BUILD)/gpsp_input.o: gpsp/input.c | $(M4BUILD)
	$(CC) $(M4_GPSPFLAGS) -c $< -o $@

m4-objs: $(M4OBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- exactly as
# M1/M2/M3. Every object is named on the link line and is therefore included in
# its ENTIRETY, so the M4 measurement stays directly comparable with the frozen
# M3 one instead of measuring whatever subset this fixture happens to reach.
$(M4BUILD)/$(M4TARGET).elf: $(M4OBJS) runtime/linker.ld | $(M4BUILD)
	$(CC) $(BASEFLAGS) $(M4LDFLAGS) -o $@ $(M4OBJS)

m4-link: $(M4OBJS) | $(M4BUILD)
	@$(CC) $(BASEFLAGS) $(M4LDFLAGS) -o $(M4BUILD)/$(M4TARGET).elf $(M4OBJS) \
	     > $(M4BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(M4BUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M4BUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M4 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M4 LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M4 LINK OK: linker exit 0, no undefined references"

# --arena 4194304, NOT M3's 1048576. The arena is a RUNTIME mmap (see
# apps/m4gpsp/main.c:529), so this argument only tells tools/sizereport.py how
# much ordinary RW the process reserves at runtime. It adds ZERO IMAGE BYTES and
# moves no frozen size figure -- the .text/.rodata/.data/.bss numbers and the
# __data_load gate are entirely unaffected by it.
$(M4BUILD)/$(M4TARGET).bin: $(M4BUILD)/$(M4TARGET).elf
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M4BUILD)/$(M4TARGET).elf $@ \
	   --map $(M4BUILD)/$(M4TARGET).map --budget $(JIT_LIMIT) --arena 4194304

m4: $(M4BUILD)/$(M4TARGET).bin

m4-size: $(M4BUILD)/$(M4TARGET).elf $(M4BUILD)/$(M4TARGET).bin
	@$(PYTHON) tools/sizereport.py $(M4BUILD)/$(M4TARGET).elf $(M4BUILD)/$(M4TARGET).bin \
	   --map $(M4BUILD)/$(M4TARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification ----------------------------------------------------------
#
# IDENTICAL IN SUBSTANCE TO m3-verify, with $(M4OBJS) and the M4 ELF
# substituted. Every one of the five tools is invoked UNMODIFIED and with the
# same arguments, so all thirteen required checks carry over unweakened: ET_DYN,
# no absolute function immediates, R_X86_64_RELATIVE-only validation, writable
# and in-range relocation targets, symbol-kind validation, dependency closure,
# zero unresolved symbols, dynarec/libretro/C++-runtime/static-init exclusion,
# and the .bss tripwire.
#
# NO ALLOWLIST IS BROADENED FOR M4. tools/check_forbidden.py keeps exactly the
# five defined-only input.c entries it gained at M1 and the five filestream
# entries. M4's new symbols were named with that gate in mind: every one is
# m4_rom_* or rom_*, and NONE contains the LIBRETRO_PATTERNS bare substrings
# "path_", "retro_", "vfs_", "filestream_", "_stub" or "emit_" -- which is why
# the cartridge candidate accessor is m4_rom_candidate_name and NOT the more
# obvious m4_rom_path_name, and why nothing here is called rom_path_*.
# If M4 ever needs a new allowlist entry, that is a finding to report, not a
# change to make quietly.
m4-verify: $(M4BUILD)/$(M4TARGET).elf
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M4BUILD)/$(M4TARGET).elf
	@echo "== relocations =="
	@$(PYTHON) tools/verify_reloc.py $(M4BUILD)/$(M4TARGET).elf | tail -12
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M4OBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M4OBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M4BUILD)/$(M4TARGET).elf $(M4OBJS)

# ---- hardware smoke-test delivery ------------------------------------------
#
# THE ONLY M4 TARGETS THAT TOUCH HARDWARE. Everything above is offline.
#
# The delivery architecture is M0's, reused wholesale and unmodified for the
# fifth time: tools/mklua.py for packaging, tools/send.py for transport, TCP
# 9026 for the script, UDP 9027 for the log, TCP 9028-9045 for the blob, same
# 'K' ACK handshake, same ext_args layout. Neither tool is changed nor forked.
#
# THE SCRIPT IS NOT SHARED. build/m3/m3gpsp.lua cannot be reused: it checks
# `step == 54` for success, whereas the M4 fixture completes at `step == 79`.
# Reusing it would report every successful M4 run as INCOMPLETE. lua/m4.lua.in
# is the M4 template.
#
# THE JIT RESERVATION IS DERIVED, NEVER HARD-CODED. mklua.py reads __data_start
# out of the LINKED M4 ELF and rounds it to a 0x4000 page, so the reservation is
# a fact about the image that was actually built rather than a constant that can
# go stale as M4 grows.
M4_LUA_TEMPLATE := lua/m4.lua.in
M4_LUA_OUT      := $(M4BUILD)/$(M4TARGET).lua

$(M4_LUA_OUT): $(M4_LUA_TEMPLATE) tools/mklua.py \
               $(M4BUILD)/$(M4TARGET).bin $(M4BUILD)/$(M4TARGET).elf
	@$(PYTHON) tools/mklua.py $(M4_LUA_TEMPLATE) $(M4BUILD)/$(M4TARGET).bin $@ \
	   $(M4BUILD)/$(M4TARGET).elf

m4-lua: $(M4_LUA_OUT)

# ---- ROM upload ------------------------------------------------------------
#
# A SEPARATE, EXPLICIT, USER-DRIVEN ACTION -- same rule as m3-upload-bios. No
# ROM is embedded in the image or in the delivery script, and this target is
# never a prerequisite of m4-send: uploading someone's cartridge must be
# something they ask for by name.
#
# The destination is the PRIMARY candidate that apps/m4gpsp/main.c tries first
# (adapters/gba/gba_rom.c:117). To place a FALLBACK copy instead, override:
#     make m4-upload-rom PS5_IP=<addr> ROM=<file> \
#          M4_ROM_REMOTE=/savedata0/roms/test.gba
#
# tools/upload.py and lua/upload.lua are reused VERBATIM -- neither is modified
# nor forked for M4. The default is replace/truncate, so re-uploading a
# different cartridge to the same path does not leave a tail of the previous
# one behind.
#
# THERE IS NO --expect-size GATE HERE, and that is deliberate. M3's BIOS is
# EXACTLY 16,384 bytes so a PC-side size gate was free; a GBA cartridge is any
# power-of-two-ish size from 4 KB to 32 MB, so there is no single correct value
# to check against. The binding validation is the one the fixture performs on
# the console against the file it actually opened: header byte[3] == 0xEA,
# byte[0xB2] == 0x96, non-zero length and a 32 MB ceiling.
M4_ROM_REMOTE ?= /temp0/test.gba

m4-upload-rom:
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m4-upload-rom PS5_IP=<console address> ROM=<file>)
endif
ifndef ROM
	$(error ROM is not set -- use: make m4-upload-rom PS5_IP=<console address> ROM=<path to a .gba cartridge>)
endif
	$(PYTHON) tools/upload.py $(PS5_IP) $(ROM) $(M4_ROM_REMOTE)

# m4-verify is a PREREQUISITE, not a suggestion -- same rule as
# m1-send/m2-send/m3-send. Nothing is transmitted unless every offline check
# passes first.
m4-send: $(M4_LUA_OUT) $(M4BUILD)/$(M4TARGET).bin m4-verify
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m4-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M4_LUA_OUT) $(M4BUILD)/$(M4TARGET).bin

m4-clean:
	rm -rf $(M4BUILD)

.PHONY: m4 m4-objs m4-link m4-size m4-verify m4-lua m4-upload-rom m4-send m4-clean

# =============================================================================
# M5 -- FIRST CONTROLLED execute_arm() CALL
# =============================================================================
#
#   make m5                build the M5 image and print its size report
#   make m5-link           link only, and list anything still undefined
#   make m5-objs           compile only
#   make m5-size           size report only (needs a prior m5 build)
#   make m5-verify         every offline check for the M5 image
#   make m5-lua            package the M5 delivery script only
#   make m5-send           deliver to a console (runs m5-verify first)
#   make m5-clean          removes build/m5 ONLY
#
# THERE IS DELIBERATELY NO m5-upload-rom AND NO m5-upload-bios.
# M5 reuses the BIOS and the cartridge that `make m3-upload-bios` and
# `make m4-upload-rom` already placed on the console, at the same paths the M5
# fixture probes. Forking either target would be a second copy of one
# scp-equivalent with no behavioural difference -- and, more importantly,
# introducing a SECOND cartridge would mean M4 and M5 differed in two respects
# instead of one. The whole value of M5 is that it differs from M4 in exactly
# one way: it executes an instruction.
#
# EVERYTHING ABOVE THIS LINE IS UNTOUCHED, for the fifth time. M0's, M1's, M2's,
# M3's and M4's targets, flags, object lists, paths and outputs are exactly as
# they were when their baselines were measured, so `make clean && make`,
# `make m1-clean && make m1` ... `make m4-clean && make m4` still reproduce all
# five byte for byte. This is an ADDITIVE block for exactly the reason the M1-M4
# blocks were.
#
# M4 IS NOT REUSED AND NOT REPLACED. M5 gets its own fixture
# (apps/m5gpsp/main.c), its own build directory (build/m5) and its own objects,
# INCLUDING ITS OWN COPIES OF THE gpSP OBJECTS, gba_probe.o, gba_bios.o and
# gba_rom.o. As at M4 that separation is REQUIRED rather than merely tidy: these
# gpSP objects are compiled with -DROM_BUFFER_SIZE=2, so sharing build/m4/*.o
# would silently rebuild the frozen M4 objects and destroy the M4 baseline.
#
# WHAT M5 ADDS TO THE CLOSURE: exactly ONE new object, gba_exec.o. No new gpSP
# file is compiled -- execute_arm() lives in gpsp_cpu.o and update_gba() in
# gpsp_main.o, both of which M1-M4 each linked in their ENTIRETY (no
# --gc-sections). THE INTERPRETER HAS BEEN PRESENT AND LINKED SINCE M1; M5 is
# merely the first milestone to CALL it. M5's growth is its own assertions,
# snapshots and diagnostic strings, not the interpreter.
#
# ---------------------------------------------------------------------------
# THE SIZE GATE THIS MILESTONE HAS TO WATCH, AND WHY IT IS NOT 68,016
# ---------------------------------------------------------------------------
# The frozen M4 image measured __data_load == 249,328 and __data_start ==
# 327,680, an "alignment gap" of 68,016 bytes. THAT GAP IS NOT A GROWTH BUDGET
# and must not be read as one. runtime/linker.ld:146 computes
#
#     . = ALIGN(0x10000) + 0x10000;
#
# so __data_start is ALWAYS at least 64 KB past __data_load, and the gap SHRINKS
# as the code grows until __data_load crosses the next 64 KB boundary, at which
# point __data_start steps up by 65,536 and the gap resets. The real figure is:
#
#     262,144 - 249,328 = 12,816 bytes
#
# of .text + .rodata + .rela.dyn growth before __data_start moves from 0x50000
# to 0x60000.
#
# CROSSING IT IS NOT A BUILD FAILURE AND NOTHING HERE ENFORCES IT.
# tools/sizereport.py computes jit_footprint = align(__data_start, 0x4000) and
# jit_maps = ceil(jit_footprint / 0x40000). At 327,680 that is 2 mappings; at
# 393,216 it is STILL 2 mappings (1.5 rounds up), and the footprint stays well
# under the 524,288 budget with 131,072 bytes of headroom. The delivery script
# derives its reservation from the linked ELF, so it tracks the change by
# itself.
#
# What crossing it DOES do is MOVE A FROZEN NUMBER, which must be reported
# rather than discovered. `make m5-size` prints it and apps/m5gpsp/main.c step
# 98 prints it again on the console.
#
# THE HARD LIMIT IS ELSEWHERE: __data_start must stay <= 524,288 (JIT_LIMIT),
# i.e. __data_load <= 458,752. From M4's 249,328 that is 209,424 bytes of room,
# which no assertion fixture is going to consume.

M5TARGET := m5gpsp
M5BUILD  := build/m5

# ---------------------------------------------------------------- includes --
#
# M4's include set with -Iapps/m5gpsp substituted for -Iapps/m4gpsp.
# -Iadapters/gba is what lets apps/m5gpsp/main.c find all four adapter headers:
# gba_probe.h (M2's), gba_bios.h (M3's), gba_rom.h (M4's) -- ALL THREE REUSED
# UNMODIFIED -- and gba_exec.h (M5's).
#
# THE TYPE BOUNDARY STILL HOLDS. gba_exec.h is written in plain `unsigned int` /
# `int` and typedefs nothing, so putting adapters/gba on the fixture's include
# path still cannot pull gpsp/common.h's `unsigned long long` u64 typedef into a
# TU that already has runtime/core.h's `unsigned long`.
M5_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m5gpsp -Iadapters/gba

M5FLAGS = $(BASEFLAGS) $(M5_INCLUDES) -Wall -Wno-unused-function

# IDENTICAL TO M4's, including -DROM_BUFFER_SIZE=2. M5 adds NO new compile flag
# and changes NO existing one -- deliberately, so that any size or behaviour
# difference between the M4 and M5 images is attributable to M5's own source and
# not to a changed build configuration.
M5_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M5_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M5LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
            -Wl,--build-id=none -Wl,-z,norelro \
            -Wl,--orphan-handling=error \
            -Wl,-Map=$(M5BUILD)/$(M5TARGET).map

# ----------------------------------------------------------------- objects --
#
# M4's object set, plus gba_exec.o. runtime/shim.c, gba_probe.c, gba_bios.c,
# gba_rom.c, gba_fixture.c, gba_filestream.c and gba_compat.c are all compiled
# from UNMODIFIED source.
#
# gba_fixture.c IS NOT EDITED FOR M5 and that is worth stating explicitly: it
# DEFINES skip_next_frame (line 70) as a non-static u32, which M5 needs to write.
# adapters/gba/gba_exec.c simply declares it extern -- the same technique
# gpsp/gba_memory.c:198 itself uses -- so no shared adapter changes.
M5_OWN_OBJS = $(M5BUILD)/m5main.o \
              $(M5BUILD)/shim.o \
              $(M5BUILD)/gba_fixture.o \
              $(M5BUILD)/gba_filestream.o \
              $(M5BUILD)/gba_compat.o \
              $(M5BUILD)/gba_probe.o \
              $(M5BUILD)/gba_bios.o \
              $(M5BUILD)/gba_rom.o \
              $(M5BUILD)/gba_exec.o

M5_GPSP_OBJS = $(M5BUILD)/gpsp_cpu.o $(M5BUILD)/gpsp_main.o \
               $(M5BUILD)/gpsp_gba_memory.o $(M5BUILD)/gpsp_video.o \
               $(M5BUILD)/gpsp_sound.o $(M5BUILD)/gpsp_serial.o \
               $(M5BUILD)/gpsp_serial_proto.o $(M5BUILD)/gpsp_rfu.o \
               $(M5BUILD)/gpsp_gbp.o $(M5BUILD)/gpsp_cheats.o \
               $(M5BUILD)/gpsp_savestate.o $(M5BUILD)/gpsp_input.o

M5OBJS = $(M5_OWN_OBJS) $(M5_GPSP_OBJS)

# EXCLUDED, unchanged from M1-M4 and none of these may ever appear above:
#   cpu_threaded.c  memmap.c  gba_cc_lut.c  bios_data.S
#   libretro/libretro.c  libretro/libretro-common/*.c
#   x86/**  arm/**  mips/**  3ds/**  bios/**  tests/**  tools/**  jni/**
#
# cpu_threaded.c IS WORTH NAMING THREE TIMES AT M5. It is the dynarec, and M5 is
# the first milestone that executes a GBA instruction at all -- so it is the
# first point at which "just use the fast one" is a real temptation rather than
# a theoretical one. It is NOT linked. M5 calls execute_arm() and never
# execute_arm_translate(); dynarec_enable is left UNDEFINED by gba_fixture.c so
# any leak is a LINK ERROR; and `make m5-verify` proves the exclusion from the
# linked image.

$(M5BUILD):
	mkdir -p $(M5BUILD)

# ---- LUAport-owned objects -------------------------------------------------

$(M5BUILD)/m5main.o: apps/m5gpsp/main.c adapters/gba/gba_probe.h \
                     adapters/gba/gba_bios.h adapters/gba/gba_rom.h \
                     adapters/gba/gba_exec.h \
                     $(HEADERS) | $(M5BUILD)
	$(CC) $(M5FLAGS) -c $< -o $@

$(M5BUILD)/shim.o: runtime/shim.c $(HEADERS) | $(M5BUILD)
	$(CC) $(M5FLAGS) -c $< -o $@

$(M5BUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

$(M5BUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

$(M5BUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

# M2's probe TU, compiled from UNMODIFIED source into M5's own build directory.
$(M5BUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

# M3's BIOS TU, compiled from UNMODIFIED source into M5's own build directory.
$(M5BUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

# M4's cartridge TU, compiled from UNMODIFIED source into M5's build directory.
$(M5BUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

# The only NEW translation unit in M5, and the ONLY place in the entire LUAport
# tree that calls execute_arm(). Compiled with the gpSP flags because it
# includes gpsp/common.h -- it is on the gpSP side of the type boundary.
$(M5BUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects -----------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, identical to M4's.
# gpsp/** is never modified and its own Makefile is never invoked.

$(M5BUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M5BUILD)
	$(CXX) $(M5_GPSPCXXFLAGS) -c $< -o $@

$(M5BUILD)/gpsp_video.o: gpsp/video.cc | $(M5BUILD)
	$(CXX) $(M5_GPSPCXXFLAGS) -c $< -o $@

$(M5BUILD)/gpsp_main.o: gpsp/main.c | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

$(M5BUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

$(M5BUILD)/gpsp_sound.o: gpsp/sound.c | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

$(M5BUILD)/gpsp_serial.o: gpsp/serial.c | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

$(M5BUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

$(M5BUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

$(M5BUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

$(M5BUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

$(M5BUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

$(M5BUILD)/gpsp_input.o: gpsp/input.c | $(M5BUILD)
	$(CC) $(M5_GPSPFLAGS) -c $< -o $@

m5-objs: $(M5OBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- exactly as M1-M4.
# Every object is named on the link line and is therefore included in its
# ENTIRETY, so the M5 measurement stays directly comparable with the frozen M4
# one instead of measuring whatever subset this fixture happens to reach.
$(M5BUILD)/$(M5TARGET).elf: $(M5OBJS) runtime/linker.ld | $(M5BUILD)
	$(CC) $(BASEFLAGS) $(M5LDFLAGS) -o $@ $(M5OBJS)

m5-link: $(M5OBJS) | $(M5BUILD)
	@$(CC) $(BASEFLAGS) $(M5LDFLAGS) -o $(M5BUILD)/$(M5TARGET).elf $(M5OBJS) \
	     > $(M5BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(M5BUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M5BUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M5 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M5 LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M5 LINK OK: linker exit 0, no undefined references"

# --arena 4194304, IDENTICAL TO M4's. The arena is a RUNTIME mmap, so this
# argument only tells tools/sizereport.py how much ordinary RW the process
# reserves at runtime. It adds ZERO IMAGE BYTES and moves no frozen size figure.
$(M5BUILD)/$(M5TARGET).bin: $(M5BUILD)/$(M5TARGET).elf
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M5BUILD)/$(M5TARGET).elf $@ \
	   --map $(M5BUILD)/$(M5TARGET).map --budget $(JIT_LIMIT) --arena 4194304

m5: $(M5BUILD)/$(M5TARGET).bin

m5-size: $(M5BUILD)/$(M5TARGET).elf $(M5BUILD)/$(M5TARGET).bin
	@$(PYTHON) tools/sizereport.py $(M5BUILD)/$(M5TARGET).elf $(M5BUILD)/$(M5TARGET).bin \
	   --map $(M5BUILD)/$(M5TARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification ----------------------------------------------------------
#
# IDENTICAL IN SUBSTANCE TO m4-verify, with $(M5OBJS) and the M5 ELF
# substituted. Every one of the five tools is invoked UNMODIFIED and with the
# same arguments, so all thirteen required checks carry over unweakened: ET_DYN,
# no absolute function immediates, R_X86_64_RELATIVE-only validation, writable
# and in-range relocation targets, symbol-kind validation, dependency closure,
# zero unresolved symbols, dynarec/libretro/C++-runtime/static-init exclusion,
# and the .bss tripwire.
#
# NO ALLOWLIST IS BROADENED FOR M5, AND NONE NEEDED TO BE.
# tools/check_forbidden.py keeps exactly the five defined-only input.c entries it
# gained at M1 and the five filestream entries. M5's new symbols were named with
# that gate in mind: every one is m5_exec_* or M5_*, and NONE contains the
# LIBRETRO_PATTERNS bare substrings "path_", "retro_", "vfs_", "filestream_",
# "_stub" or "emit_".
#
# THE ONE THAT LOOKS LIKE IT SHOULD TRIP AND DOES NOT: execute_arm.
# tools/check_forbidden.py:67 lists execute_arm_TRANSLATE, not execute_arm. The
# plain interpreter entry point has been linked since M1 as part of gpsp_cpu.o;
# M5 is simply the first milestone to CALL it. The dynarec entry point remains
# forbidden and remains absent, which is exactly the distinction the gate is
# there to enforce.
#
# If M5 ever needs a new allowlist entry, that is a finding to report, not a
# change to make quietly.
m5-verify: $(M5BUILD)/$(M5TARGET).elf
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M5BUILD)/$(M5TARGET).elf
	@echo "== relocations =="
	@$(PYTHON) tools/verify_reloc.py $(M5BUILD)/$(M5TARGET).elf | tail -12
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M5OBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M5OBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M5BUILD)/$(M5TARGET).elf $(M5OBJS)

# ---- hardware smoke-test delivery ------------------------------------------
#
# THE ONLY M5 TARGET THAT TOUCHES HARDWARE. Everything above is offline.
#
# The delivery architecture is M0's, reused wholesale and unmodified for the
# sixth time: tools/mklua.py for packaging, tools/send.py for transport, TCP
# 9026 for the script, UDP 9027 for the log, TCP 9028-9045 for the blob, same
# 'K' ACK handshake, same ext_args layout. Neither tool is changed nor forked.
#
# THE SCRIPT IS NOT SHARED. build/m4/m4gpsp.lua cannot be reused: it checks
# `step == 79` for success, whereas the M5 fixture completes at `step == 99`.
# Reusing it would report every successful M5 run as INCOMPLETE, and it has no
# decode for the four execution-outcome statuses (-95..-98) that only M5 can
# produce. lua/m5.lua.in is the M5 template.
#
# THE JIT RESERVATION IS DERIVED, NEVER HARD-CODED. mklua.py reads __data_start
# out of the LINKED M5 ELF and rounds it to a 0x4000 page, so the reservation is
# a fact about the image that was actually built. That matters more at M5 than
# at any earlier milestone: see the size-gate note at the top of this block --
# if M5's growth moves __data_start from 327,680 to 393,216, this tracks it
# automatically and nothing here needs editing.
M5_LUA_TEMPLATE := lua/m5.lua.in
M5_LUA_OUT      := $(M5BUILD)/$(M5TARGET).lua

$(M5_LUA_OUT): $(M5_LUA_TEMPLATE) tools/mklua.py \
               $(M5BUILD)/$(M5TARGET).bin $(M5BUILD)/$(M5TARGET).elf
	@$(PYTHON) tools/mklua.py $(M5_LUA_TEMPLATE) $(M5BUILD)/$(M5TARGET).bin $@ \
	   $(M5BUILD)/$(M5TARGET).elf

m5-lua: $(M5_LUA_OUT)

# m5-verify is a PREREQUISITE, not a suggestion -- same rule as m1-send through
# m4-send. Nothing is transmitted unless every offline check passes first.
#
# THE BIOS AND THE CARTRIDGE MUST ALREADY BE ON THE CONSOLE. M5 deliberately
# provides no upload target of its own:
#     make m3-upload-bios PS5_IP=<addr> BIOS=<gba_bios.bin>
#     make m4-upload-rom  PS5_IP=<addr> ROM=<a .gba cartridge>
# then RELAUNCH THE GAME (the JIT pool depletes per session), then m5-send.
m5-send: $(M5_LUA_OUT) $(M5BUILD)/$(M5TARGET).bin m5-verify
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m5-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M5_LUA_OUT) $(M5BUILD)/$(M5TARGET).bin

m5-clean:
	rm -rf $(M5BUILD)

.PHONY: m5 m5-objs m5-link m5-size m5-verify m5-lua m5-send m5-clean

# =============================================================================
# M6 -- FIRST VALID GBA FRAMEBUFFER
# =============================================================================
#
#   make m6                build the M6 image and print its size report
#   make m6-link           link only, and list anything still undefined
#   make m6-objs           compile only
#   make m6-size           size report only (needs a prior m6 build)
#   make m6-verify         every offline check for the M6 image
#   make m6-lua            package the M6 delivery script only
#   make m6-send           deliver to a console (runs m6-verify first)
#   make m6-clean          removes build/m6 ONLY
#
# THERE IS DELIBERATELY NO m6-upload-rom AND NO m6-upload-bios.
# M6 reuses the BIOS and the cartridge that `make m3-upload-bios` and
# `make m4-upload-rom` already placed on the console, at the same paths the M6
# fixture probes. Introducing a second cartridge would mean M5 and M6 differed
# in two respects instead of one. The whole value of M6 is that it differs from
# M5 in exactly one way: it lets the machine run NATURAL frames instead of one
# artificially-seeded instruction.
#
# EVERYTHING ABOVE THIS LINE IS UNTOUCHED, for the sixth time. M0's .. M5's
# targets, flags, object lists, paths and outputs are exactly as they were when
# their baselines were measured, so `make clean && make`, `make m1-clean &&
# make m1` ... `make m5-clean && make m5` still reproduce all six byte for byte.
# This is an ADDITIVE block for exactly the reason the M1-M5 blocks were.
#
# M5 IS NOT REUSED AND NOT REPLACED. M6 gets its own fixture
# (apps/m6gpsp/main.c), its own build directory (build/m6) and its own objects,
# including its own copies of the gpSP objects and of all four adapters.
#
# ------------------------------------------------------------- THE SIZE GATE --
#
# READ THIS BEFORE INTERPRETING THE M6 SIZE REPORT.
#
# The LINKED M5 image measured, from build/m5/m5gpsp.map:
#     __data_load   = 0x3EFE0 = 258,016
#     __data_start  = 0x50000 = 327,680
#     .data         = 0x2860  =  10,336
#     .bss          = 0x159660 = 1,414,752
#     .rela.dyn     = 0x1590  =   5,520
#     binary                  = 268,352
#
# The next 64 KB boundary above __data_load is 262,144, so the ENTIRE M6
# milestone has 4,128 BYTES of .text+.rodata+.rela growth before __data_start
# steps from 0x50000 to 0x60000.
#
# THAT IS FOUR THOUSAND BYTES, NOT TWELVE. An earlier note in the M5 block
# quoted 12,816, which was M4's margin; M5 consumed most of it. M6's fixture is
# larger than M5's -- it carries FNV hashing, a distinct-value sweep, richer
# statistics and more assertion tables -- so CROSSING THE BOUNDARY IS THE
# EXPECTED OUTCOME, NOT A SURPRISE.
#
# CROSSING IT IS NOT A FAILURE. tools/mklua.py derives the JIT reservation from
# the linked ELF's __data_start, so it tracks automatically; the reservation
# still fits in two 0x40000 mappings and stays far under the 524,288 JIT_LIMIT.
# THE HARD LIMIT IS ELSEWHERE: __data_start must stay <= 524,288, i.e.
# __data_load <= 458,752, which is 200,736 bytes away.
#
# But it MOVES A FROZEN NUMBER, so it must be REPORTED rather than discovered.
# `make m6-size` prints it and apps/m6gpsp/main.c step 119 prints it again on
# the console. DO NOT OPTIMISE MERELY TO PRESERVE 0x50000.
#
# M6 ADDS NO NEW gpSP CODE. gpsp_video.o has been linked since M1 and
# update_scanline() is already in the M5 image (build/m5/m5gpsp.map:305), so it
# brings its whole static call graph with it already. All M6 growth is fixture
# growth.
#
# M6 ADDS NO NEW ADAPTER EITHER. gba_probe.c (M2), gba_bios.c (M3), gba_rom.c
# (M4) and gba_exec.c (M5) are compiled from BYTE-IDENTICAL source. M6 reuses
# m5_exec_run(), m5_exec_snapshot(), m5_exec_snap_read() and m5_exec_probe_seed()
# rather than forking them.
#
# .bss SHOULD BE ROUGHLY NEUTRAL: M6's 77,280-byte framebuffer replaces M5's
# identically sized one, and gpSP's 262,144-byte sound_buffer is already counted.
# M6 adds one new 8,192-byte array (the distinct-value bitmap), which is noise
# against 1,414,752 and far below the 2 MB tripwire in tools/check_forbidden.py.

M6TARGET := m6gpsp
M6BUILD  := build/m6

# ---------------------------------------------------------------- includes --
#
# M5's include set with -Iapps/m6gpsp substituted for -Iapps/m5gpsp.
# -Iadapters/gba is what lets apps/m6gpsp/main.c find all four adapter headers,
# ALL FOUR REUSED UNMODIFIED.
#
# THE TYPE BOUNDARY STILL HOLDS. Every adapter header is written in plain
# `unsigned int` / `int` and typedefs nothing, so putting adapters/gba on the
# fixture's include path still cannot pull gpsp/common.h's `unsigned long long`
# u64 typedef into a TU that already has runtime/core.h's `unsigned long`.
M6_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m6gpsp -Iadapters/gba

M6FLAGS = $(BASEFLAGS) $(M6_INCLUDES) -Wall -Wno-unused-function

# IDENTICAL TO M5's, including -DROM_BUFFER_SIZE=2. M6 adds NO new compile flag
# and changes NO existing one.
#
# USE_XBGR1555_FORMAT IS NOT DEFINED HERE AND MUST NOT BE. Its absence is what
# selects gpsp/common.h:108's convert_palette(), i.e. the RGB565 output path
# that apps/m6gpsp/main.c documents and that M6 validates. Defining it would
# silently change every pixel value in the framebuffer and invalidate the
# reported hash.
M6_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M6_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M6LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
            -Wl,--build-id=none -Wl,-z,norelro \
            -Wl,--orphan-handling=error \
            -Wl,-Map=$(M6BUILD)/$(M6TARGET).map

# ----------------------------------------------------------------- objects --
#
# EXACTLY M5's object set with m6main.o substituted for m5main.o. Every other
# object is compiled from UNMODIFIED source, gba_exec.c included.
M6_OWN_OBJS = $(M6BUILD)/m6main.o \
              $(M6BUILD)/shim.o \
              $(M6BUILD)/gba_fixture.o \
              $(M6BUILD)/gba_filestream.o \
              $(M6BUILD)/gba_compat.o \
              $(M6BUILD)/gba_probe.o \
              $(M6BUILD)/gba_bios.o \
              $(M6BUILD)/gba_rom.o \
              $(M6BUILD)/gba_exec.o

M6_GPSP_OBJS = $(M6BUILD)/gpsp_cpu.o $(M6BUILD)/gpsp_main.o \
               $(M6BUILD)/gpsp_gba_memory.o $(M6BUILD)/gpsp_video.o \
               $(M6BUILD)/gpsp_sound.o $(M6BUILD)/gpsp_serial.o \
               $(M6BUILD)/gpsp_serial_proto.o $(M6BUILD)/gpsp_rfu.o \
               $(M6BUILD)/gpsp_gbp.o $(M6BUILD)/gpsp_cheats.o \
               $(M6BUILD)/gpsp_savestate.o $(M6BUILD)/gpsp_input.o

M6OBJS = $(M6_OWN_OBJS) $(M6_GPSP_OBJS)

# EXCLUDED, unchanged from M1-M5 and none of these may ever appear above:
#   cpu_threaded.c  memmap.c  gba_cc_lut.c  bios_data.S
#   libretro/libretro.c  libretro/libretro-common/*.c
#   x86/**  arm/**  mips/**  3ds/**  bios/**  tests/**  tools/**  jni/**
#
# cpu_threaded.c IS WORTH NAMING AGAIN AT M6. M6 runs two FULL frames -- roughly
# 561,792 emulated cycles -- so it is the first milestone where the interpreter's
# speed is even observable and "just use the dynarec" becomes a real temptation.
# IT IS NOT LINKED. M6 calls execute_arm() through M5's adapter and never
# execute_arm_translate(); dynarec_enable is left UNDEFINED by gba_fixture.c so
# any leak is a LINK ERROR; and `make m6-verify` proves the exclusion from the
# linked image.
#
# NO PLATFORM VIDEO OBJECT IS LINKED EITHER, and that is M6-specific: build/
# platform.o and build/gfx.o exist for M0 and are deliberately ABSENT from the
# list above, so no sceVideoOut* or sceGnm* symbol can reach the M6 image.

$(M6BUILD):
	mkdir -p $(M6BUILD)

# ---- LUAport-owned objects -------------------------------------------------

$(M6BUILD)/m6main.o: apps/m6gpsp/main.c adapters/gba/gba_probe.h \
                     adapters/gba/gba_bios.h adapters/gba/gba_rom.h \
                     adapters/gba/gba_exec.h \
                     $(HEADERS) | $(M6BUILD)
	$(CC) $(M6FLAGS) -c $< -o $@

$(M6BUILD)/shim.o: runtime/shim.c $(HEADERS) | $(M6BUILD)
	$(CC) $(M6FLAGS) -c $< -o $@

$(M6BUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

$(M6BUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

$(M6BUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

$(M6BUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

$(M6BUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

$(M6BUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

# M5's execution TU, compiled from UNMODIFIED source into M6's build directory.
# It is still the ONLY place in the tree that calls execute_arm().
$(M6BUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects -----------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, identical to M5's.
# gpsp/** is never modified and its own Makefile is never invoked.

$(M6BUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M6BUILD)
	$(CXX) $(M6_GPSPCXXFLAGS) -c $< -o $@

$(M6BUILD)/gpsp_video.o: gpsp/video.cc | $(M6BUILD)
	$(CXX) $(M6_GPSPCXXFLAGS) -c $< -o $@

$(M6BUILD)/gpsp_main.o: gpsp/main.c | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

$(M6BUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

$(M6BUILD)/gpsp_sound.o: gpsp/sound.c | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

$(M6BUILD)/gpsp_serial.o: gpsp/serial.c | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

$(M6BUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

$(M6BUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

$(M6BUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

$(M6BUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

$(M6BUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

$(M6BUILD)/gpsp_input.o: gpsp/input.c | $(M6BUILD)
	$(CC) $(M6_GPSPFLAGS) -c $< -o $@

m6-objs: $(M6OBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- exactly as M1-M5.
# Every object is named on the link line and is therefore included in its
# ENTIRETY, so the M6 measurement stays directly comparable with the frozen M5
# one instead of measuring whatever subset this fixture happens to reach.
$(M6BUILD)/$(M6TARGET).elf: $(M6OBJS) runtime/linker.ld | $(M6BUILD)
	$(CC) $(BASEFLAGS) $(M6LDFLAGS) -o $@ $(M6OBJS)

m6-link: $(M6OBJS) | $(M6BUILD)
	@$(CC) $(BASEFLAGS) $(M6LDFLAGS) -o $(M6BUILD)/$(M6TARGET).elf $(M6OBJS) \
	     > $(M6BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(M6BUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M6BUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M6 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M6 LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M6 LINK OK: linker exit 0, no undefined references"

# --arena 4194304, IDENTICAL TO M4's and M5's. The arena is a RUNTIME mmap, so
# this argument only tells tools/sizereport.py how much ordinary RW the process
# reserves at runtime. It adds ZERO IMAGE BYTES and moves no frozen size figure.
$(M6BUILD)/$(M6TARGET).bin: $(M6BUILD)/$(M6TARGET).elf
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M6BUILD)/$(M6TARGET).elf $@ \
	   --map $(M6BUILD)/$(M6TARGET).map --budget $(JIT_LIMIT) --arena 4194304

m6: $(M6BUILD)/$(M6TARGET).bin

m6-size: $(M6BUILD)/$(M6TARGET).elf $(M6BUILD)/$(M6TARGET).bin
	@$(PYTHON) tools/sizereport.py $(M6BUILD)/$(M6TARGET).elf $(M6BUILD)/$(M6TARGET).bin \
	   --map $(M6BUILD)/$(M6TARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification ----------------------------------------------------------
#
# IDENTICAL IN SUBSTANCE TO m5-verify, with $(M6OBJS) and the M6 ELF
# substituted. Every one of the five tools is invoked UNMODIFIED and with the
# same arguments, so all thirteen required checks carry over UNWEAKENED: ET_DYN,
# no absolute function immediates, R_X86_64_RELATIVE-only validation, writable
# and in-range relocation targets, symbol-kind validation, dependency closure,
# zero unresolved symbols, dynarec/libretro/C++-runtime/static-init exclusion,
# and the .bss tripwire.
#
# NO ALLOWLIST IS BROADENED FOR M6, AND NONE NEEDED TO BE.
# M6's new symbols were named with tools/check_forbidden.py's LIBRETRO_PATTERNS
# in mind: every one is m6_* or M6_*, and NONE contains the bare substrings
# "path_", "retro_", "libretro_", "vfs_", "filestream_", "string_is_", "rtime_",
# "encoding_", "_stub", "emit_", "translate_block", "translation_cache" or
# "block_lookup_address".
#
# If M6 ever needs a new allowlist entry, that is a finding to report, not a
# change to make quietly.
#
# ---- THE M6-SPECIFIC CHECK: NO HOST PRESENTATION ----
#
# M6 produces a real, complete, displayable frame and MUST NOT DISPLAY IT. This
# is a LINK-LEVEL check on the built image, not a source grep, because a source
# grep would pass even if a platform object were accidentally added to M6OBJS.
# sceVideoOutOpen / SubmitFlip / RegisterBuffers / Close and any sceGnm* symbol
# must be absent from the dynamic symbol table and from every relocation.
m6-verify: $(M6BUILD)/$(M6TARGET).elf
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M6BUILD)/$(M6TARGET).elf
	@echo "== relocations =="
	@$(PYTHON) tools/verify_reloc.py $(M6BUILD)/$(M6TARGET).elf | tail -12
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M6OBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M6OBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M6BUILD)/$(M6TARGET).elf $(M6OBJS)
	@echo "== NO HOST PRESENTATION (M6-specific) =="
	@if $(READELF) -sW $(M6BUILD)/$(M6TARGET).elf 2>/dev/null \
	     | grep -qE "sceVideoOut|sceGnm"; then \
	     echo "M6 VERIFY FAILED: a presentation symbol reached the image:"; \
	     $(READELF) -sW $(M6BUILD)/$(M6TARGET).elf | grep -E "sceVideoOut|sceGnm"; \
	     exit 1; \
	 fi
	@if $(OBJDUMP) -s -j .rodata $(M6BUILD)/$(M6TARGET).elf 2>/dev/null \
	     | grep -qiE "scevideoout|scegnm"; then \
	     echo "M6 VERIFY FAILED: a presentation symbol NAME is embedded in .rodata"; \
	     exit 1; \
	 fi
	@echo "no sceVideoOut*/sceGnm* symbol or name is present -- M6 cannot present"

# ---- hardware smoke-test delivery ------------------------------------------
#
# THE ONLY M6 TARGET THAT TOUCHES HARDWARE. Everything above is offline.
#
# The delivery architecture is M0's, reused wholesale and unmodified for the
# seventh time: tools/mklua.py for packaging, tools/send.py for transport, TCP
# 9026 for the script, UDP 9027 for the log, TCP 9028-9045 for the blob, same
# 'K' ACK handshake, same ext_args layout. Neither tool is changed nor forked.
#
# THE SCRIPT IS NOT SHARED. build/m5/m5gpsp.lua cannot be reused: it checks
# `step == 99` for success, whereas the M6 fixture completes at `step == 119`,
# and it has no decode for the six framebuffer statuses (-114..-119) that only
# M6 can produce. lua/m6.lua.in is the M6 template.
#
# THE JIT RESERVATION IS DERIVED, NEVER HARD-CODED. mklua.py reads __data_start
# out of the LINKED M6 ELF and rounds it to a 0x4000 page, so the reservation is
# a fact about the image that was actually built. That matters more at M6 than
# at any earlier milestone: see the size-gate note at the top of this block --
# if M6's growth moves __data_start from 327,680 to 393,216, this tracks it
# automatically and nothing here needs editing.
M6_LUA_TEMPLATE := lua/m6.lua.in
M6_LUA_OUT      := $(M6BUILD)/$(M6TARGET).lua

$(M6_LUA_OUT): $(M6_LUA_TEMPLATE) tools/mklua.py \
               $(M6BUILD)/$(M6TARGET).bin $(M6BUILD)/$(M6TARGET).elf
	@$(PYTHON) tools/mklua.py $(M6_LUA_TEMPLATE) $(M6BUILD)/$(M6TARGET).bin $@ \
	   $(M6BUILD)/$(M6TARGET).elf

m6-lua: $(M6_LUA_OUT)

# m6-verify is a PREREQUISITE, not a suggestion -- same rule as m1-send through
# m5-send. Nothing is transmitted unless every offline check passes first.
#
# THE BIOS AND THE CARTRIDGE MUST ALREADY BE ON THE CONSOLE. M6 deliberately
# provides no upload target of its own:
#     make m3-upload-bios PS5_IP=<addr> BIOS=<gba_bios.bin>
#     make m4-upload-rom  PS5_IP=<addr> ROM=<a .gba cartridge>
# then RELAUNCH THE GAME (the JIT pool depletes per session), then m6-send.
m6-send: $(M6_LUA_OUT) $(M6BUILD)/$(M6TARGET).bin m6-verify
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m6-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M6_LUA_OUT) $(M6BUILD)/$(M6TARGET).bin

m6-clean:
	rm -rf $(M6BUILD)

.PHONY: m6 m6-objs m6-link m6-size m6-verify m6-lua m6-send m6-clean

# =============================================================================
# M7 -- PS5 VIDEOOUT PRESENTATION
# =============================================================================
#
# APPEND-ONLY. Nothing above this line is edited, and no M0-M6 variable, rule or
# .PHONY entry is redefined. M6 remains buildable and verifiable exactly as it
# was, which is what keeps "M6 is still the gate" a checkable claim.
#
# ---- WHAT M7 ADDS TO THE LINK, AND WHAT IT STILL DOES NOT ----
#
# TWO new objects, and only two:
#   platform.o     runtime/platform.c, compiled from BYTE-IDENTICAL, UNMODIFIED
#                  source. This is M0's hardware-proven VideoOut layer. It is
#                  REUSED, not reimplemented -- the 300 ms and 100 ms sleeps
#                  inside plat_video_init() are load-bearing and were established
#                  on hardware at M0.
#   gba_present.o  adapters/gba/gba_present.c, the ONLY genuinely new code in
#                  this milestone: BGR555 -> ARGB8888 conversion and the 6x blit.
#
# runtime/gfx.c IS DELIBERATELY NOT LINKED. gfx.h:12-14 already recorded that the
# GBA adapter wants its own 240x160 scaler and that it belongs in adapters/gba/,
# NOT in runtime/. gba_present.c is that scaler. Adding gfx.o would pull in a
# second, unused blitter and cost image bytes for nothing.
#
# NO AUDIO OBJECT, NO GNM OBJECT, AND NO PAD CALL. plat_pad_* is compiled --
# platform.c is a single TU and the link uses no --gc-sections -- but it is NEVER
# CALLED. That is proved by RELOCATIONS in m7-verify, not by string absence,
# because the scePad* strings legitimately survive in .rodata and a string-based
# test would report a false failure.
#
# ---- THE SIZE GATE ----
#
# M6 froze __data_load at 263,728 and __data_start at 393,216, in two mappings
# with 131,072 bytes of headroom against JIT_LIMIT (524,288). The linker script
# computes __data_start = ALIGN(__data_load, 0x10000) + 0x10000, so the hard
# ceiling is __data_load <= 458,752.
#
# M7 REINTRODUCES platform.c, which is the single largest size risk here. The
# payload PRINTS both numbers and the margin at step 139, so the gate is observed
# on the console rather than discovered offline. If the JIT footprint exceeds
# 524,288 that is a STOP-AND-REPORT condition, not something to optimise around.

M7TARGET := m7gpsp
M7BUILD  := build/m7
M7ELF    := $(M7BUILD)/$(M7TARGET).elf
M7BIN    := $(M7BUILD)/$(M7TARGET).bin

# ---------------------------------------------------------------- includes --
#
# M6's include set with -Iapps/m7gpsp substituted. -Iruntime is what lets
# apps/m7gpsp/main.c find platform.h, and -Iadapters/gba finds gba_present.h
# alongside M2-M5's four frozen adapter headers.
M7_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m7gpsp -Iadapters/gba

M7FLAGS = $(BASEFLAGS) $(M7_INCLUDES) -Wall -Wno-unused-function

# IDENTICAL TO M6's. M7 adds NO new compile flag and changes NO existing one.
#
# USE_XBGR1555_FORMAT IS STILL NOT DEFINED AND MUST NOT BE. Its absence selects
# gpsp/common.h:108's convert_palette(), the BGR555-in-RGB565 path that
# gba_present.c's conversion is derived from and that its eight-row self-test
# asserts. Defining it would change every pixel value AND silently invalidate the
# conversion, which is precisely why gba_present.h documents the dependency.
M7_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M7_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M7LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
            -Wl,--build-id=none -Wl,-z,norelro \
            -Wl,--orphan-handling=error \
            -Wl,-Map=$(M7BUILD)/$(M7TARGET).map

# ----------------------------------------------------------------- objects --
#
# EXACTLY M6's object set, with m7main.o substituted for m6main.o and the two new
# objects appended. Every other object is compiled from UNMODIFIED source.
M7_OWN_OBJS = $(M7BUILD)/m7main.o \
              $(M7BUILD)/shim.o \
              $(M7BUILD)/gba_fixture.o \
              $(M7BUILD)/gba_filestream.o \
              $(M7BUILD)/gba_compat.o \
              $(M7BUILD)/gba_probe.o \
              $(M7BUILD)/gba_bios.o \
              $(M7BUILD)/gba_rom.o \
              $(M7BUILD)/gba_exec.o \
              $(M7BUILD)/gba_present.o \
              $(M7BUILD)/platform.o

M7_GPSP_OBJS = $(M7BUILD)/gpsp_cpu.o $(M7BUILD)/gpsp_main.o \
               $(M7BUILD)/gpsp_gba_memory.o $(M7BUILD)/gpsp_video.o \
               $(M7BUILD)/gpsp_sound.o $(M7BUILD)/gpsp_serial.o \
               $(M7BUILD)/gpsp_serial_proto.o $(M7BUILD)/gpsp_rfu.o \
               $(M7BUILD)/gpsp_gbp.o $(M7BUILD)/gpsp_cheats.o \
               $(M7BUILD)/gpsp_savestate.o $(M7BUILD)/gpsp_input.o

M7OBJS = $(M7_OWN_OBJS) $(M7_GPSP_OBJS)

# EXCLUDED, unchanged from M1-M6 and none of these may ever appear above:
#   cpu_threaded.c  memmap.c  gba_cc_lut.c  bios_data.S
#   libretro/libretro.c  libretro/libretro-common/*.c
#   x86/**  arm/**  mips/**  3ds/**  bios/**  tests/**  tools/**  jni/**
#   runtime/gfx.c  -- see the note at the top of this block
#
# cpu_threaded.c IS STILL NOT LINKED. M7 executes exactly the same two frames M6
# did, through M5's adapter, and never execute_arm_translate(). dynarec_enable is
# left UNDEFINED by gba_fixture.c so any leak is a LINK ERROR, and m7-verify
# proves the exclusion from the linked image.

$(M7BUILD):
	mkdir -p $(M7BUILD)

# ---- LUAport-owned objects -------------------------------------------------

$(M7BUILD)/m7main.o: apps/m7gpsp/main.c adapters/gba/gba_probe.h \
                     adapters/gba/gba_bios.h adapters/gba/gba_rom.h \
                     adapters/gba/gba_exec.h adapters/gba/gba_present.h \
                     $(HEADERS) | $(M7BUILD)
	$(CC) $(M7FLAGS) -c $< -o $@

$(M7BUILD)/shim.o: runtime/shim.c $(HEADERS) | $(M7BUILD)
	$(CC) $(M7FLAGS) -c $< -o $@

# M0's PS5 VideoOut layer, COMPILED FROM UNMODIFIED SOURCE into M7's build
# directory. runtime/platform.c is on the frozen list and is not edited for M7;
# this rule exists solely to give M7 its own object file so the M0 build's
# build/platform.o is neither reused nor disturbed.
$(M7BUILD)/platform.o: runtime/platform.c $(HEADERS) | $(M7BUILD)
	$(CC) $(M7FLAGS) -c $< -o $@

# THE ONLY NEW CODE IN M7. Self-contained: it includes gba_present.h and nothing
# else, takes a `const u16 *` source, and cannot write through it.
$(M7BUILD)/gba_present.o: adapters/gba/gba_present.c adapters/gba/gba_present.h \
                          runtime/core.h | $(M7BUILD)
	$(CC) $(M7FLAGS) -c $< -o $@

$(M7BUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

$(M7BUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

$(M7BUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

$(M7BUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

$(M7BUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

$(M7BUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

$(M7BUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects -----------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, identical to M6's.
# gpsp/** is never modified and its own Makefile is never invoked.

$(M7BUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M7BUILD)
	$(CXX) $(M7_GPSPCXXFLAGS) -c $< -o $@

$(M7BUILD)/gpsp_video.o: gpsp/video.cc | $(M7BUILD)
	$(CXX) $(M7_GPSPCXXFLAGS) -c $< -o $@

$(M7BUILD)/gpsp_main.o: gpsp/main.c | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

$(M7BUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

$(M7BUILD)/gpsp_sound.o: gpsp/sound.c | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

$(M7BUILD)/gpsp_serial.o: gpsp/serial.c | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

$(M7BUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

$(M7BUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

$(M7BUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

$(M7BUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

$(M7BUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

$(M7BUILD)/gpsp_input.o: gpsp/input.c | $(M7BUILD)
	$(CC) $(M7_GPSPFLAGS) -c $< -o $@

m7-objs: $(M7OBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- exactly as M1-M6.
# Every object is named on the link line and is therefore included in its
# ENTIRETY, so the M7 measurement stays directly comparable with the frozen M6
# one. It is also WHY the uncalled scePad* code is present in the image and why
# the no-input proof has to be a relocation check rather than a string check.
$(M7ELF): $(M7OBJS) runtime/linker.ld | $(M7BUILD)
	$(CC) $(BASEFLAGS) $(M7LDFLAGS) -o $@ $(M7OBJS)

m7-link: $(M7OBJS) | $(M7BUILD)
	@$(CC) $(BASEFLAGS) $(M7LDFLAGS) -o $(M7ELF) $(M7OBJS) \
	     > $(M7BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(M7BUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M7BUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M7 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M7 LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M7 LINK OK: linker exit 0, no undefined references"

# --arena 4194304, IDENTICAL TO M4's, M5's and M6's.
$(M7BIN): $(M7ELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M7ELF) $@ \
	   --map $(M7BUILD)/$(M7TARGET).map --budget $(JIT_LIMIT) --arena 4194304

m7: $(M7BIN)

m7-size: $(M7ELF) $(M7BIN)
	@$(PYTHON) tools/sizereport.py $(M7ELF) $(M7BIN) \
	   --map $(M7BUILD)/$(M7TARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification ----------------------------------------------------------
#
# EVERY M6 GATE IS KEPT AND NONE IS WEAKENED. All five tools are invoked
# UNMODIFIED and with the same arguments, so ET_DYN, no absolute function
# immediates, R_X86_64_RELATIVE-only validation, writable and in-range
# relocation targets, symbol-kind validation, dependency closure, zero
# unresolved symbols, dynarec/libretro/C++-runtime/static-init exclusion and the
# 2 MB .bss tripwire all carry over.
#
# ---- THE ONE CHECK THAT HAD TO CHANGE, AND WHY ----
#
# M6 rejected "sceVideoOut|sceGnm" outright. M7 EXISTS to link sceVideoOut, so a
# blanket rejection would now fail by design. It is replaced by an ALLOWLIST, not
# removed: exactly the six entry points runtime/platform.c resolves, plus the
# module name it loads. ANY OTHER sceVideoOut* symbol is still a failure, sceGnm*
# is still categorically forbidden, and sceAudioOut* is added because M7 must not
# start audio either.
#
# THE ALLOWLIST IS BUILT FROM .rodata STRINGS, NOT FROM THE SYMBOL TABLE. The
# image is -static-pie and resolves everything through sceKernelDlsym at runtime,
# so it has ZERO undefined dynamic symbols -- a symtab-based check would pass
# vacuously and prove nothing at all. The dlsym NAME STRINGS are the only durable
# evidence in the image that a given entry point can be reached.
#
# THE ALLOWLIST IS ALSO A POSITIVE TEST. The six names and libSceVideoOut.sprx
# are REQUIRED TO BE PRESENT: their absence would mean video was silently not
# linked, which would make a "presentation" pass meaningless.
#
# ---- HOW .rodata IS EXTRACTED, AND WHY NOT WITH --only-section ----
#
# THERE IS NO OUTPUT SECTION NAMED .rodata. runtime/linker.ld deliberately folds
# *(.rodata) and *(.rodata.*) INTO the single .text output section so the strings
# stay inside the text PHDR. After linking, .text is the only output section and
# readelf can no longer tell the two apart.
#
# The first version of this rule used `objcopy --only-section=.rodata`. That
# matched nothing, wrote a ZERO-BYTE file, and EXITED 0. The positive test then
# failed with "sceVideoOutOpen is NOT in the image" while the image in fact
# contained it twice -- and, far more dangerous, the three NEGATIVE tests below
# all PASSED VACUOUSLY because grep found nothing in an empty file. A check that
# fails open is worse than no check at all.
#
# So the window is carved by the linker's own marker symbols: .text is emitted at
# vaddr 0, therefore a symbol value IS its byte offset inside the extracted .text
# image, and [__rodata_start, __rodata_end) is exactly the logical rodata range.
# That assumption is not trusted blindly -- __text_start is asserted to be 0, and
# the carve fails closed on a missing marker, an empty or inverted window, a short
# .text extraction, a dd error, a zero-byte result, or a byte count that does not
# equal __rodata_end - __rodata_start. This rule must never again be able to
# "pass" against an empty file.
M7_VIDEOOUT_ALLOW = sceVideoOutOpen sceVideoOutClose sceVideoOutRegisterBuffers \
                    sceVideoOutSubmitFlip sceVideoOutSetFlipRate \
                    sceVideoOutAddFlipEvent

m7-verify: $(M7ELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M7ELF)
	@echo "== relocations =="
	@$(PYTHON) tools/verify_reloc.py $(M7ELF) | tail -12
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M7OBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M7OBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M7ELF) $(M7OBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M7ELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M7 VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== PRESENTATION ALLOWLIST (M7-specific) =="
	@$(OBJCOPY) -O binary --only-section=.text $(M7ELF) $(M7BUILD)/text.bin
	@tx=$$($(NM) $(M7ELF) | awk '$$NF == "__text_start"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 ra=$$($(NM) $(M7ELF) | awk '$$NF == "__rodata_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 rz=$$($(NM) $(M7ELF) | awk '$$NF == "__rodata_end"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 if [ -z "$$tx" ] || [ -z "$$ra" ] || [ -z "$$rz" ]; then \
	     echo "M7 VERIFY FAILED: linker marker symbol missing -- need"; \
	     echo "  __text_start, __rodata_start and __rodata_end in $(M7ELF)."; \
	     exit 1; \
	 fi; \
	 t=$$((0x$$tx)); a=$$((0x$$ra)); z=$$((0x$$rz)); n=$$((z - a)); \
	 if [ "$$t" -ne 0 ]; then \
	     echo "M7 VERIFY FAILED: .text does not start at vaddr 0 (__text_start ="; \
	     echo "  $$t), so a symbol value is no longer a file offset in the"; \
	     echo "  extracted binary and this carve would read the wrong bytes."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M7 VERIFY FAILED: empty rodata window -- __rodata_start=$$a"; \
	     echo "  __rodata_end=$$z gives $$n bytes."; \
	     exit 1; \
	 fi; \
	 have=$$(wc -c < $(M7BUILD)/text.bin | tr -d " "); \
	 if [ "$$have" -lt "$$z" ]; then \
	     echo "M7 VERIFY FAILED: .text extraction is $$have bytes but the rodata"; \
	     echo "  window ends at $$z -- the extraction is short or .text is absent."; \
	     exit 1; \
	 fi; \
	 dd if=$(M7BUILD)/text.bin of=$(M7BUILD)/rodata.bin bs=1 skip=$$a count=$$n \
	    2>/dev/null || { \
	     echo "M7 VERIFY FAILED: dd could not carve the rodata window."; exit 1; \
	 }; \
	 if [ ! -s $(M7BUILD)/rodata.bin ]; then \
	     echo "M7 VERIFY FAILED: rodata extraction produced 0 bytes -- every check"; \
	     echo "  below would pass vacuously. Fix the extraction, not the checks."; \
	     exit 1; \
	 fi; \
	 got=$$(wc -c < $(M7BUILD)/rodata.bin | tr -d " "); \
	 if [ "$$got" -ne "$$n" ]; then \
	     echo "M7 VERIFY FAILED: rodata.bin is $$got bytes, expected $$n ="; \
	     echo "  __rodata_end - __rodata_start. Extraction is not trustworthy."; \
	     exit 1; \
	 fi; \
	 echo "rodata carved from .text by linker markers: [$$a,$$z) = $$n bytes"
	@for s in $(M7_VIDEOOUT_ALLOW) libSceVideoOut.sprx; do \
	     if ! grep -aq "$$s" $(M7BUILD)/rodata.bin; then \
	         echo "M7 VERIFY FAILED: required presentation name '$$s' is NOT in"; \
	         echo "  the image -- VideoOut was not linked and any 'presentation'"; \
	         echo "  pass would be meaningless."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 6 VideoOut entry points + libSceVideoOut.sprx are present"
	@allow=$$(echo $(M7_VIDEOOUT_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "sceVideoOut[A-Za-z0-9_]*" $(M7BUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M7 VERIFY FAILED: an UNEXPECTED sceVideoOut* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     echo "  M7 allows exactly the six entry points runtime/platform.c uses."; \
	     exit 1; \
	 fi; \
	 echo "no sceVideoOut* name outside the six-symbol allowlist"
	@if grep -aqE "sceGnm|sceAudioOut" $(M7BUILD)/rodata.bin; then \
	     echo "M7 VERIFY FAILED: a graphics-pipeline or audio name reached the image:"; \
	     grep -aoE "sceGnm[A-Za-z0-9_]*|sceAudioOut[A-Za-z0-9_]*" \
	        $(M7BUILD)/rodata.bin | sort -u | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no sceGnm* and no sceAudioOut* name is present"
	@echo "== NO INPUT -- PROVED BY RELOCATIONS, NOT BY STRINGS (M7-specific) =="
	@echo "   scePad* strings DO legitimately appear: platform.c is one TU and the"
	@echo "   link uses no --gc-sections, so uncalled pad code is in the image."
	@echo "   What must be absent is any REFERENCE to it from M7's own objects."
	@pad=$$($(NM) -u $(M7BUILD)/m7main.o $(M7BUILD)/gba_present.o 2>/dev/null \
	          | grep -E "plat_pad_|scePad" || true); \
	 if [ -n "$$pad" ]; then \
	     echo "M7 VERIFY FAILED: M7's own code references the pad layer:"; \
	     echo "$$pad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "m7main.o and gba_present.o carry NO plat_pad_*/scePad* relocation"

# ---- hardware smoke-test delivery ------------------------------------------
#
# THE ONLY M7 TARGET THAT TOUCHES HARDWARE. Everything above is offline.
#
# The delivery architecture is M0's, reused wholesale and unmodified for the
# eighth time: tools/mklua.py for packaging, tools/send.py for transport, TCP
# 9026 for the script, UDP 9027 for the log, TCP 9028-9045 for the blob, same
# 'K' ACK handshake, same ext_args layout. Neither tool is changed nor forked.
#
# THE SCRIPT IS NOT SHARED. build/m6/m6gpsp.lua cannot be reused: it checks
# `step == 119` for success, whereas the M7 fixture completes at `step == 139`,
# and it has no decode for the seven presentation statuses (-120..-126) that only
# M7 can produce. lua/m7.lua.in is the M7 template.
M7_LUA_TEMPLATE := lua/m7.lua.in
M7_LUA_OUT      := $(M7BUILD)/$(M7TARGET).lua

$(M7_LUA_OUT): $(M7_LUA_TEMPLATE) tools/mklua.py $(M7BIN) $(M7ELF)
	@$(PYTHON) tools/mklua.py $(M7_LUA_TEMPLATE) $(M7BIN) $@ $(M7ELF)

m7-lua: $(M7_LUA_OUT)

# m7-verify is a PREREQUISITE, not a suggestion -- same rule as m1-send through
# m6-send. Nothing is transmitted unless every offline check passes first.
#
# THERE IS DELIBERATELY NO m7-upload-rom. M7 reuses M4's cartridge and M3's BIOS,
# which must ALREADY be on the console:
#     make m3-upload-bios PS5_IP=<addr> BIOS=<gba_bios.bin>
#     make m4-upload-rom  PS5_IP=<addr> ROM=<a .gba cartridge>
#
# THEN RELAUNCH THE HOST GAME, AND RUN M7 ONCE PER LAUNCH. This matters more at
# M7 than at any earlier milestone: plat_video_init() reserves 16 MB of direct
# memory and DELIBERATELY NEVER RELEASES IT, so a second run in the same session
# fails with PLAT_VID_EALLOC (-24), reported as status -121. That is expected
# behaviour, not a defect, and lua/m7.lua.in labels it with that exact wording.
m7-send: $(M7_LUA_OUT) $(M7BIN) m7-verify
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m7-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M7_LUA_OUT) $(M7BIN)

m7-clean:
	rm -rf $(M7BUILD)

.PHONY: m7 m7-objs m7-link m7-size m7-verify m7-lua m7-send m7-clean

# =============================================================================
# M8 -- DUALSENSE INPUT REACHES THE GBA KEYPAD
# =============================================================================
#
# APPEND-ONLY. Nothing above this line is edited, and no M0-M7 variable, rule or
# .PHONY entry is redefined. M7 remains buildable and verifiable exactly as it
# was, which is what keeps "M7 is still the gate" a checkable claim.
#
# ---- WHAT M8 ADDS TO THE LINK, AND WHAT IT STILL DOES NOT ----
#
# ONE new object, and only one:
#   gba_input.o    adapters/gba/gba_input.c -- the DualSense -> GBA keypad
#                  translation and the ONLY write to REG_P1 in the tree.
#
# platform.o is ALREADY in the M7 link. M8 does not add the pad layer to the
# image; the pad layer has been in the image since M7 and was proved UNREACHED
# by relocation. WHAT CHANGES AT M8 IS THE CALL GRAPH, NOT THE CLOSURE -- and
# that is exactly what m8-verify inverts and re-proves below.
#
# NO AUDIO OBJECT AND NO GNM OBJECT. Still true, still enforced.
#
# runtime/gfx.c IS STILL NOT LINKED, for M7's reason, unchanged.
#
# ---- WHY THE ROM CHANGES, AND WHY THAT IS SAFE ----
#
# M8 runs mgba/cinema/gba/irq/keyirq/test.gba rather than M4's cartridge. It is
# the only ROM in the tree that READS THE KEYPAD: it carries one KEYINPUT and
# four KEYCNT literals, and mGBA's own config.ini scripts A and A+B presses.
#
# TWO FROZEN ASSERTIONS SURVIVE THE SWAP, AND BOTH WERE CHECKED BEFORE CHOOSING
# IT rather than after:
#   - its entry word is 0xEA00002E, IDENTICAL to M4's cartridge, so M5's
#     first-opcode and predicted-PC assertions hold unchanged;
#   - header[3] = 0xEA and header[0xB2] = 0x96, so gamepak_header_nonstandard is
#     false and M4's metadata assertion survives.
# The cartridge is uploaded with M4's EXISTING target -- there is deliberately no
# m8-upload-rom, because the transport is M4's and forking it would fork the
# evidence too:
#     make m4-upload-rom PS5_IP=<addr> ROM=mgba/cinema/gba/irq/keyirq/test.gba
#
# ---- THE SIZE GATE ----
#
# M7 froze __data_load at 275,824 and __data_start at 393,216, in two mappings
# with 131,072 bytes of headroom against JIT_LIMIT (524,288). The linker script
# computes __data_start = ALIGN(__data_load, 0x10000) + 0x10000, so the hard
# ceiling is __data_load <= 458,752.
#
# gba_input.o is small and adds no table of pointers, so the risk here is far
# below M7's. The payload still PRINTS both numbers and the margin at its final
# step, so the gate is observed on the console rather than discovered offline.

M8TARGET := m8gpsp
M8BUILD  := build/m8
M8ELF    := $(M8BUILD)/$(M8TARGET).elf
M8BIN    := $(M8BUILD)/$(M8TARGET).bin

# ---------------------------------------------------------------- includes --
#
# M7's include set with -Iapps/m8gpsp substituted. -Iruntime is what lets
# apps/m8gpsp/main.c find platform.h, and -Iadapters/gba finds gba_input.h
# alongside M2-M7's five frozen adapter headers.
M8_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m8gpsp -Iadapters/gba

M8FLAGS = $(BASEFLAGS) $(M8_INCLUDES) -Wall -Wno-unused-function

# IDENTICAL TO M7's. M8 adds NO new compile flag and changes NO existing one.
#
# USE_XBGR1555_FORMAT IS STILL NOT DEFINED AND MUST NOT BE, for M7's reason:
# its absence selects the BGR555-in-RGB565 path that gba_present.c's conversion
# is derived from and that its eight-row self-test asserts.
M8_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M8_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M8LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
            -Wl,--build-id=none -Wl,-z,norelro \
            -Wl,--orphan-handling=error \
            -Wl,-Map=$(M8BUILD)/$(M8TARGET).map

# ----------------------------------------------------------------- objects --
#
# EXACTLY M7's object set, with m8main.o substituted for m7main.o and TWO new
# objects appended. Every other object is compiled from UNMODIFIED source.
#
#   gba_input.o    adapters/gba/gba_input.c -- the DualSense -> GBA keypad
#                  translation. M8's original addition.
#
#   gba_m8map.o    adapters/gba/gba_m8map.c -- M8's ROM-SIZE-AWARE cartridge
#                  mapping probe. IT EXISTS SO THAT gba_rom.c DOES NOT HAVE TO
#                  CHANGE. m4_rom_probe_map()'s M4_MAP_ROM0A/ROM0C/EEPROM bits
#                  encode a POWER-OF-TWO PAGE COUNT, which held for M4-M7's
#                  4,096-byte one-page cartridge and is false on correct code for
#                  M8's 69,160-byte three-page one. gba_rom.o is linked
#                  UNMODIFIED into M4, M5, M6, M7 AND M8; editing it to suit M8
#                  would invalidate four hardware-verified milestones. The new
#                  object is linked ONLY here and is STRICTLY STRONGER than the
#                  three bits it displaces -- it validates all 2,561 reachable
#                  map slots against a replay of gpSP's own map_rom_entry loops
#                  instead of four slots against a hard-coded layout.
M8_OWN_OBJS = $(M8BUILD)/m8main.o \
              $(M8BUILD)/shim.o \
              $(M8BUILD)/gba_fixture.o \
              $(M8BUILD)/gba_filestream.o \
              $(M8BUILD)/gba_compat.o \
              $(M8BUILD)/gba_probe.o \
              $(M8BUILD)/gba_bios.o \
              $(M8BUILD)/gba_rom.o \
              $(M8BUILD)/gba_m8map.o \
              $(M8BUILD)/gba_exec.o \
              $(M8BUILD)/gba_present.o \
              $(M8BUILD)/gba_input.o \
              $(M8BUILD)/platform.o

M8_GPSP_OBJS = $(M8BUILD)/gpsp_cpu.o $(M8BUILD)/gpsp_main.o \
               $(M8BUILD)/gpsp_gba_memory.o $(M8BUILD)/gpsp_video.o \
               $(M8BUILD)/gpsp_sound.o $(M8BUILD)/gpsp_serial.o \
               $(M8BUILD)/gpsp_serial_proto.o $(M8BUILD)/gpsp_rfu.o \
               $(M8BUILD)/gpsp_gbp.o $(M8BUILD)/gpsp_cheats.o \
               $(M8BUILD)/gpsp_savestate.o $(M8BUILD)/gpsp_input.o

M8OBJS = $(M8_OWN_OBJS) $(M8_GPSP_OBJS)

# EXCLUDED, unchanged from M1-M7 and none of these may ever appear above:
#   cpu_threaded.c  memmap.c  gba_cc_lut.c  bios_data.S
#   libretro/libretro.c  libretro/libretro-common/*.c
#   x86/**  arm/**  mips/**  3ds/**  bios/**  tests/**  tools/**  jni/**
#   runtime/gfx.c  -- see the note at the top of this block
#
# gpsp_input.o IS LINKED, AND HAS BEEN SINCE M1 -- savestate.c needs
# input_check/read/write_savestate. Its update_input() is NEVER CALLED, because
# the only call site in the tree is libretro/libretro.c:1373 and libretro.c is
# excluded above. That is the finding M8 is built on: nothing else in the image
# writes REG_P1, so the value M8 writes persists and there is no race.
#
# cpu_threaded.c IS STILL NOT LINKED. dynarec_enable is left UNDEFINED by
# gba_fixture.c so any leak is a LINK ERROR, and m8-verify proves the exclusion
# from the linked image.

$(M8BUILD):
	mkdir -p $(M8BUILD)

# ---- LUAport-owned objects -------------------------------------------------

$(M8BUILD)/m8main.o: apps/m8gpsp/main.c adapters/gba/gba_probe.h \
                     adapters/gba/gba_bios.h adapters/gba/gba_rom.h \
                     adapters/gba/gba_exec.h adapters/gba/gba_present.h \
                     adapters/gba/gba_input.h adapters/gba/gba_m8map.h \
                     $(HEADERS) | $(M8BUILD)
	$(CC) $(M8FLAGS) -c $< -o $@

$(M8BUILD)/shim.o: runtime/shim.c $(HEADERS) | $(M8BUILD)
	$(CC) $(M8FLAGS) -c $< -o $@

# M0's PS5 VideoOut AND PAD layer, COMPILED FROM UNMODIFIED SOURCE into M8's
# build directory. runtime/platform.c is on the frozen list and is not edited for
# M8 -- the pad entry points it already exports are simply CALLED for the first
# time. This rule exists solely to give M8 its own object file.
$(M8BUILD)/platform.o: runtime/platform.c $(HEADERS) | $(M8BUILD)
	$(CC) $(M8FLAGS) -c $< -o $@

# M7's presentation adapter, compiled from BYTE-IDENTICAL, UNMODIFIED source.
# It does NOT learn that input exists, and m8-verify re-proves that below.
$(M8BUILD)/gba_present.o: adapters/gba/gba_present.c adapters/gba/gba_present.h \
                          runtime/core.h | $(M8BUILD)
	$(CC) $(M8FLAGS) -c $< -o $@

# THE ONLY NEW CODE IN M8.
#
# Compiled with the GPSP flags, not the app flags, because it includes gpsp/
# common.h -- it is on the gpSP side of the u64 type boundary. It includes
# runtime/platform.h NOWHERE and calls no PS5 API; the pad state reaches it as a
# plain `unsigned int`. m8-verify proves both halves of that claim on the call
# graph rather than trusting this comment.
$(M8BUILD)/gba_input.o: adapters/gba/gba_input.c adapters/gba/gba_input.h | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

# M8's OWN cartridge mapping probe. Compiled with $(M8_GPSPFLAGS) -- the same
# flags as every other gpSP-facing adapter, INCLUDING -DROM_BUFFER_SIZE=2 -- so
# that the gamepak_buffer_count it reads is the one the image actually allocates.
$(M8BUILD)/gba_m8map.o: adapters/gba/gba_m8map.c adapters/gba/gba_m8map.h | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects -----------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, identical to M7's.
# gpsp/** is never modified and its own Makefile is never invoked.

$(M8BUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M8BUILD)
	$(CXX) $(M8_GPSPCXXFLAGS) -c $< -o $@

$(M8BUILD)/gpsp_video.o: gpsp/video.cc | $(M8BUILD)
	$(CXX) $(M8_GPSPCXXFLAGS) -c $< -o $@

$(M8BUILD)/gpsp_main.o: gpsp/main.c | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gpsp_sound.o: gpsp/sound.c | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gpsp_serial.o: gpsp/serial.c | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

$(M8BUILD)/gpsp_input.o: gpsp/input.c | $(M8BUILD)
	$(CC) $(M8_GPSPFLAGS) -c $< -o $@

m8-objs: $(M8OBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- exactly as M1-M7.
# Every object is named on the link line and is therefore included in its
# ENTIRETY, so the M8 measurement stays directly comparable with the frozen M7
# one. It is ALSO why the scePad* strings were already in the M7 image, and why
# the "input is now reached" proof has to be a RELOCATION check rather than a
# string check -- in the opposite direction from M7's.
$(M8ELF): $(M8OBJS) runtime/linker.ld | $(M8BUILD)
	$(CC) $(BASEFLAGS) $(M8LDFLAGS) -o $@ $(M8OBJS)

m8-link: $(M8OBJS) | $(M8BUILD)
	@$(CC) $(BASEFLAGS) $(M8LDFLAGS) -o $(M8ELF) $(M8OBJS) \
	     > $(M8BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(M8BUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M8BUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M8 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M8 LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M8 LINK OK: linker exit 0, no undefined references"

# --arena 4194304, IDENTICAL TO M4's, M5's, M6's and M7's.
$(M8BIN): $(M8ELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M8ELF) $@ \
	   --map $(M8BUILD)/$(M8TARGET).map --budget $(JIT_LIMIT) --arena 4194304

m8: $(M8BIN)

m8-size: $(M8ELF) $(M8BIN)
	@$(PYTHON) tools/sizereport.py $(M8ELF) $(M8BIN) \
	   --map $(M8BUILD)/$(M8TARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification ----------------------------------------------------------
#
# EVERY M7 GATE IS KEPT AND NONE IS WEAKENED. All five tools are invoked
# UNMODIFIED and with the same arguments, so ET_DYN, no absolute function
# immediates, R_X86_64_RELATIVE-only validation, writable and in-range
# relocation targets, symbol-kind validation, dependency closure, zero
# unresolved symbols, dynarec/libretro/C++-runtime/static-init exclusion and the
# 2 MB .bss tripwire all carry over. The VideoOut allowlist, the sceGnm/
# sceAudioOut prohibition and the marker-based rodata carve carry over VERBATIM.
#
# ---- THE ONE CHECK THAT HAD TO INVERT, AND WHY ----
#
# M7's final gate proved that m7main.o and gba_present.o carry NO plat_pad_*
# relocation -- that the pad layer, though linked, was UNREACHABLE from M7's own
# code. M8 EXISTS to reach it, so that exact test would now fail by design.
#
# IT IS INVERTED, NOT DELETED, AND IT IS INVERTED PRECISELY:
#
#   m8main.o        MUST reference plat_pad_init, plat_pad_read,
#                   plat_pad_shutdown AND the m8_input_* adapter. A missing
#                   reference would mean the fixture silently stopped reading the
#                   pad and the differential test was comparing a run against
#                   itself -- which would still LOOK like a clean pass.
#
#   gba_input.o     MUST NOT reference plat_pad_* or scePad*, and MUST reference
#                   gpSP's io_registers. This is the type-boundary claim made
#                   checkable: the adapter talks to gpSP and to nobody else.
#
#   gba_present.o   MUST STILL carry NO plat_pad_*/scePad* reference, exactly as
#                   at M7. M8 adds input WITHOUT teaching the presentation path
#                   that input exists, and that separation is re-proved here
#                   rather than assumed to have survived.
#
# THE scePad STRINGS ARE NOW A POSITIVE REQUIREMENT TOO. They were already in the
# M7 image (platform.c is one TU and the link uses no --gc-sections), so their
# PRESENCE proves nothing on its own and M7 correctly refused to test for it.
# What makes them meaningful at M8 is that they are required TOGETHER WITH the
# call-graph inversion above: strings prove the entry points can be resolved,
# relocations prove they are actually reached.
M8_VIDEOOUT_ALLOW = sceVideoOutOpen sceVideoOutClose sceVideoOutRegisterBuffers \
                    sceVideoOutSubmitFlip sceVideoOutSetFlipRate \
                    sceVideoOutAddFlipEvent

# EXACTLY the three entry points runtime/platform.c:408-410 resolves. scePadClose
# is deliberately NOT here: platform.c:523 documents that it is never called, so
# the name must not appear in .rodata and an appearance would mean the frozen
# teardown had changed.
M8_PAD_ALLOW = scePadInit scePadGetHandle scePadRead

m8-verify: $(M8ELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M8ELF)
	@echo "== relocations =="
	@$(PYTHON) tools/verify_reloc.py $(M8ELF) | tail -12
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M8OBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M8OBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M8ELF) $(M8OBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M8ELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M8 VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== PRESENTATION ALLOWLIST (carried from M7) =="
	@$(OBJCOPY) -O binary --only-section=.text $(M8ELF) $(M8BUILD)/text.bin
	@tx=$$($(NM) $(M8ELF) | awk '$$NF == "__text_start"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 ra=$$($(NM) $(M8ELF) | awk '$$NF == "__rodata_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 rz=$$($(NM) $(M8ELF) | awk '$$NF == "__rodata_end"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 if [ -z "$$tx" ] || [ -z "$$ra" ] || [ -z "$$rz" ]; then \
	     echo "M8 VERIFY FAILED: linker marker symbol missing -- need"; \
	     echo "  __text_start, __rodata_start and __rodata_end in $(M8ELF)."; \
	     exit 1; \
	 fi; \
	 t=$$((0x$$tx)); a=$$((0x$$ra)); z=$$((0x$$rz)); n=$$((z - a)); \
	 if [ "$$t" -ne 0 ]; then \
	     echo "M8 VERIFY FAILED: .text does not start at vaddr 0 (__text_start ="; \
	     echo "  $$t), so a symbol value is no longer a file offset in the"; \
	     echo "  extracted binary and this carve would read the wrong bytes."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M8 VERIFY FAILED: empty rodata window -- __rodata_start=$$a"; \
	     echo "  __rodata_end=$$z gives $$n bytes."; \
	     exit 1; \
	 fi; \
	 have=$$(wc -c < $(M8BUILD)/text.bin | tr -d " "); \
	 if [ "$$have" -lt "$$z" ]; then \
	     echo "M8 VERIFY FAILED: .text extraction is $$have bytes but the rodata"; \
	     echo "  window ends at $$z -- the extraction is short or .text is absent."; \
	     exit 1; \
	 fi; \
	 dd if=$(M8BUILD)/text.bin of=$(M8BUILD)/rodata.bin bs=1 skip=$$a count=$$n \
	    2>/dev/null || { \
	     echo "M8 VERIFY FAILED: dd could not carve the rodata window."; exit 1; \
	 }; \
	 if [ ! -s $(M8BUILD)/rodata.bin ]; then \
	     echo "M8 VERIFY FAILED: rodata extraction produced 0 bytes -- every check"; \
	     echo "  below would pass vacuously. Fix the extraction, not the checks."; \
	     exit 1; \
	 fi; \
	 got=$$(wc -c < $(M8BUILD)/rodata.bin | tr -d " "); \
	 if [ "$$got" -ne "$$n" ]; then \
	     echo "M8 VERIFY FAILED: rodata.bin is $$got bytes, expected $$n ="; \
	     echo "  __rodata_end - __rodata_start. Extraction is not trustworthy."; \
	     exit 1; \
	 fi; \
	 echo "rodata carved from .text by linker markers: [$$a,$$z) = $$n bytes"
	@for s in $(M8_VIDEOOUT_ALLOW) libSceVideoOut.sprx; do \
	     if ! grep -aq "$$s" $(M8BUILD)/rodata.bin; then \
	         echo "M8 VERIFY FAILED: required presentation name '$$s' is NOT in"; \
	         echo "  the image -- VideoOut was not linked and any 'presentation'"; \
	         echo "  pass would be meaningless."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 6 VideoOut entry points + libSceVideoOut.sprx are present"
	@allow=$$(echo $(M8_VIDEOOUT_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "sceVideoOut[A-Za-z0-9_]*" $(M8BUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M8 VERIFY FAILED: an UNEXPECTED sceVideoOut* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     echo "  M8 allows exactly the six entry points runtime/platform.c uses."; \
	     exit 1; \
	 fi; \
	 echo "no sceVideoOut* name outside the six-symbol allowlist"
	@if grep -aqE "sceGnm|sceAudioOut" $(M8BUILD)/rodata.bin; then \
	     echo "M8 VERIFY FAILED: a graphics-pipeline or audio name reached the image:"; \
	     grep -aoE "sceGnm[A-Za-z0-9_]*|sceAudioOut[A-Za-z0-9_]*" \
	        $(M8BUILD)/rodata.bin | sort -u | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no sceGnm* and no sceAudioOut* name is present"
	@echo "== PAD ALLOWLIST (M8-specific) =="
	@for s in $(M8_PAD_ALLOW) libScePad.sprx; do \
	     if ! grep -aq "$$s" $(M8BUILD)/rodata.bin; then \
	         echo "M8 VERIFY FAILED: required pad name '$$s' is NOT in the image"; \
	         echo "  -- the DualSense entry points cannot be resolved and any"; \
	         echo "  'input' pass would be meaningless."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 3 scePad entry points + libScePad.sprx are present"
	@allow=$$(echo $(M8_PAD_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "scePad[A-Za-z0-9_]*" $(M8BUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M8 VERIFY FAILED: an UNEXPECTED scePad* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     echo "  M8 allows exactly the three entry points platform.c resolves;"; \
	     echo "  scePadClose in particular is documented as never called."; \
	     exit 1; \
	 fi; \
	 echo "no scePad* name outside the three-symbol allowlist"
	@echo "== INPUT IS REACHED -- THE M7 PROOF, INVERTED (M8-specific) =="
	@echo "   M7 proved the pad layer was linked but UNREACHABLE. M8 must prove"
	@echo "   the opposite for its own fixture, and the SAME separation for the"
	@echo "   adapter and the presentation path."
	@miss=""; \
	 for s in plat_pad_init plat_pad_read plat_pad_shutdown; do \
	     $(NM) -u $(M8BUILD)/m8main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M8 VERIFY FAILED: m8main.o does NOT reference:$$miss"; \
	     echo "  The fixture is not reading the pad, so its differential test"; \
	     echo "  would compare a run against itself and PASS VACUOUSLY."; \
	     exit 1; \
	 fi; \
	 echo "m8main.o references plat_pad_init, plat_pad_read and plat_pad_shutdown"
	@miss=""; \
	 for s in m8_input_selftest m8_input_from_pad m8_input_to_reg_p1 \
	          m8_input_apply m8_input_read_reg_p1; do \
	     $(NM) -u $(M8BUILD)/m8main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M8 VERIFY FAILED: m8main.o does NOT reference:$$miss"; \
	     echo "  The keypad adapter is not being driven by the fixture."; \
	     exit 1; \
	 fi; \
	 echo "m8main.o drives the full m8_input_* adapter surface"
	@echo "== THE KEYPAD INTERRUPT IS REALLY DRIVEN (M8-specific) =="
	@echo "   M8's headline claim moved from 'two frames differed' to 'a keypad"
	@echo "   INTERRUPT was raised and observed'. Without the two checks below, a"
	@echo "   regression that quietly dropped the IRQ call would still link, still"
	@echo "   pass every other gate, and still report a PASS."
	@miss=""; \
	 for s in m8_input_irq_edge m8_input_read_reg_p1cnt m8_input_read_reg_if \
	          m8_input_read_reg_ie m8_input_read_reg_ime; do \
	     $(NM) -u $(M8BUILD)/m8main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M8 VERIFY FAILED: m8main.o does NOT reference:$$miss"; \
	     echo "  The fixture is not evaluating the keypad IRQ edge or reading the"; \
	     echo "  interrupt registers, so it CANNOT observe REG_IF bit 12 and its"; \
	     echo "  input-chain latch mask would be unreachable by construction."; \
	     exit 1; \
	 fi; \
	 echo "m8main.o drives the keypad-IRQ surface (edge + KEYCNT/IF/IE/IME reads)"
	@if ! $(NM) -u $(M8BUILD)/gba_input.o 2>/dev/null | grep -q "flag_interrupt"; then \
	     echo "M8 VERIFY FAILED: gba_input.o does NOT reference flag_interrupt --"; \
	     echo "  the adapter is not raising the interrupt through gpSP's OWN"; \
	     echo "  mechanism, so any 'the keypad IRQ fired' claim would be about"; \
	     echo "  LUAport's bookkeeping rather than about the emulated machine."; \
	     exit 1; \
	 fi; \
	 echo "gba_input.o references gpSP's flag_interrupt (the IRQ is really raised)"
	@bad=$$($(NM) -u $(M8BUILD)/gba_input.o 2>/dev/null \
	          | grep -E "plat_pad_|plat_video_|scePad|sceVideoOut" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M8 VERIFY FAILED: the keypad adapter reaches the platform layer:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     echo "  gba_input.c must take raw bits and touch NO PS5 API -- that is"; \
	     echo "  the type boundary the whole adapter design rests on."; \
	     exit 1; \
	 fi; \
	 echo "gba_input.o carries NO plat_*/scePad*/sceVideoOut* relocation"
	@if ! $(NM) -u $(M8BUILD)/gba_input.o 2>/dev/null | grep -q "io_registers"; then \
	     echo "M8 VERIFY FAILED: gba_input.o does NOT reference io_registers --"; \
	     echo "  it is not actually writing the GBA keypad register."; \
	     exit 1; \
	 fi; \
	 echo "gba_input.o references gpSP's io_registers (REG_P1 is really written)"
	@pad=$$($(NM) -u $(M8BUILD)/gba_present.o 2>/dev/null \
	          | grep -E "plat_pad_|scePad" || true); \
	 if [ -n "$$pad" ]; then \
	     echo "M8 VERIFY FAILED: the PRESENTATION path learned about input:"; \
	     echo "$$pad" | sed "s/^/  /"; \
	     echo "  M8 adds input WITHOUT touching gba_present.c. This separation"; \
	     echo "  held at M7 and must still hold."; \
	     exit 1; \
	 fi; \
	 echo "gba_present.o still carries NO plat_pad_*/scePad* relocation"
	@echo "== THE M8 MAPPING PROBE IS OWNED, REACHED AND ISOLATED (M8-specific) =="
	@echo "   gba_rom.c stays FROZEN. The three OBJTEST-shaped bits it carries are"
	@echo "   NARROWED by the fixture and replaced by a stronger, ROM-size-aware"
	@echo "   probe in a SEPARATE object -- this proves that object exists, is"
	@echo "   actually called, and talks to gpSP and to nobody else."
	@miss=""; \
	 for s in m8map_probe m8map_report m8map_read m8map_expected_blocks; do \
	     $(NM) -u $(M8BUILD)/m8main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M8 VERIFY FAILED: m8main.o does NOT reference:$$miss"; \
	     echo "  The fixture is not driving the M8 mapping probe, so the"; \
	     echo "  cartridge windows are being taken on trust."; \
	     exit 1; \
	 fi; \
	 echo "m8main.o drives the full m8map_* probe surface"
	@bad=$$($(NM) -u $(M8BUILD)/gba_m8map.o 2>/dev/null \
	          | grep -E "plat_pad_|plat_video_|scePad|sceVideoOut|m4_rom_|m5_exec_" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M8 VERIFY FAILED: the mapping probe reaches outside gpSP:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     echo "  gba_m8map.c must read gpSP globals and NOTHING else -- it must"; \
	     echo "  not touch the platform layer, and it must not depend on the"; \
	     echo "  FROZEN M4/M5 adapters it is designed to be independent of."; \
	     exit 1; \
	 fi; \
	 echo "gba_m8map.o carries NO plat_*/scePad*/sceVideoOut*/m4_*/m5_* relocation"
	@miss=""; \
	 for s in memory_map_read gamepak_buffers gamepak_size gamepak_file_blocks \
	          gamepak_must_swap bios_rom; do \
	     $(NM) -u $(M8BUILD)/gba_m8map.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M8 VERIFY FAILED: gba_m8map.o does NOT reference:$$miss"; \
	     echo "  It is not reading gpSP's real mapping state, so a 'mapping OK'"; \
	     echo "  result from it would be vacuous."; \
	     exit 1; \
	 fi; \
	 echo "gba_m8map.o reads gpSP's real mapping state (memory_map_read,"; \
	 echo "  gamepak_buffers, gamepak_size, gamepak_file_blocks, gamepak_must_swap,"; \
	 echo "  bios_rom)"
	@if $(NM) -u $(M8BUILD)/gba_rom.o 2>/dev/null | grep -q "m8map_"; then \
	     echo "M8 VERIFY FAILED: gba_rom.o references the M8 probe -- the FROZEN"; \
	     echo "  M4 adapter has been edited. It is linked unmodified into M4, M5,"; \
	     echo "  M6 and M7 and must stay byte-identical for all five."; \
	     exit 1; \
	 fi; \
	 echo "gba_rom.o is untouched -- no m8map_* reference (M4-M7 stay frozen)"

# ---- hardware smoke-test delivery ------------------------------------------
#
# THE ONLY M8 TARGET THAT TOUCHES HARDWARE. Everything above is offline.
#
# The delivery architecture is M0's, reused wholesale and unmodified for the
# ninth time: tools/mklua.py for packaging, tools/send.py for transport, TCP
# 9026 for the script, UDP 9027 for the log, TCP 9028-9045 for the blob, same
# 'K' ACK handshake, same ext_args layout. Neither tool is changed nor forked.
#
# THE SCRIPT IS NOT SHARED, AND AT M8 IT CANNOT BE. build/m7/m7gpsp.lua does not
# resolve a user id -- M1-M7 deliberately zeroed ext.dbg[3], and M7 reuses it for
# diagnostics. M8 NEEDS THE REAL userId in dbg[3] for scePadGetHandle, so
# lua/m8.lua.in restores M0's sceUserServiceGetInitialUser block. It also decodes
# the M8-specific input statuses, which no earlier script knows about.
M8_LUA_TEMPLATE := lua/m8.lua.in
M8_LUA_OUT      := $(M8BUILD)/$(M8TARGET).lua

$(M8_LUA_OUT): $(M8_LUA_TEMPLATE) tools/mklua.py $(M8BIN) $(M8ELF)
	@$(PYTHON) tools/mklua.py $(M8_LUA_TEMPLATE) $(M8BIN) $@ $(M8ELF)

m8-lua: $(M8_LUA_OUT)

# m8-verify is a PREREQUISITE, not a suggestion -- same rule as m1-send through
# m7-send. Nothing is transmitted unless every offline check passes first.
#
# THERE IS DELIBERATELY NO m8-upload-rom. M8 reuses M4's TRANSPORT with a
# DIFFERENT CARTRIDGE -- the keypad test ROM is the only one in the tree that
# reads REG_P1 -- and M3's BIOS. Both must ALREADY be on the console:
#     make m3-upload-bios PS5_IP=<addr> BIOS=<gba_bios.bin>
#     make m4-upload-rom  PS5_IP=<addr> ROM=mgba/cinema/gba/irq/keyirq/test.gba
#
# THEN RELAUNCH THE HOST GAME, AND RUN M8 ONCE PER LAUNCH, for M7's reason
# unchanged: plat_video_init() reserves 16 MB of direct memory and DELIBERATELY
# NEVER RELEASES IT, so a second run in the same session fails with PLAT_VID_EALLOC
# (-24). lua/m8.lua.in labels it with that exact wording.
#
# M8 ALSO REQUIRES A PHYSICAL DUALSENSE, PAIRED AND ASSIGNED TO THE SIGNED-IN
# USER, and an operator to press CROSS when the payload asks. If nobody presses
# anything the run ends INCOMPLETE, never PASS -- a timeout is not a result.
m8-send: $(M8_LUA_OUT) $(M8BIN) m8-verify
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m8-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M8_LUA_OUT) $(M8BIN)

m8-clean:
	rm -rf $(M8BUILD)

.PHONY: m8 m8-objs m8-link m8-size m8-verify m8-lua m8-send m8-clean

# =============================================================================
# M9 -- THE SUSTAINED-RUNTIME / PERFORMANCE BASELINE
# =============================================================================
#
# PURELY ADDITIVE. Not one M0-M8 variable, rule or prerequisite above is edited,
# reordered or reused destructively. M9 gets its OWN build directory, its OWN
# object set and its OWN targets, exactly as M8 got its own from M7.
#
# WHAT M9 ADDS TO THE IMAGE: ONE OBJECT, m9main.o. No new adapter, no new gpSP
# file, no new library. gba_input.o and gba_m8map.o -- M8's two original
# additions -- are linked here UNMODIFIED, and every M2-M7 adapter is linked from
# BYTE-IDENTICAL source. M9 measures the M8 machine; it does not extend it.
#
# ---- THE SIZE GATE, AND WHY IT IS READ AFTER THE FIRST LINK ----
#
# M8 froze __data_load at 304,064 (0x4A3C0) and __data_start at 393,216, in 2
# mappings with 131,072 bytes of headroom against JIT_LIMIT (524,288).
#
# runtime/linker.ld:146 is `. = ALIGN(0x10000) + 0x10000`, so __data_start moves
# in 64 KiB STEPS, not smoothly:
#
#     __data_load <= 327,679   ->  __data_start 393,216   headroom 131,072
#     __data_load <= 393,215   ->  __data_start 458,752   headroom  65,536
#     __data_load <= 458,751   ->  __data_start 524,288   headroom       0  FAIL
#
# M9 therefore has 23,616 bytes of image growth before the footprint STEPS to
# 458,752 (still a pass), and 154,176 before the 524,288 gate fails outright.
# m9main.c is large in COMMENTS, which cost nothing, and its only new .rodata is
# the 8x8 font copy from runtime/tables.h (a few hundred bytes) plus format
# strings. `make m9-size` is expected to be run IMMEDIATELY after the first
# successful link, BEFORE any further work, so the step is observed and not
# discovered later.
#
# IF THE FOOTPRINT EXCEEDS 524,288 THE RULE IS: STOP AND REPORT. Do not optimise,
# do not delete a feature and do not touch runtime/linker.ld -- the linker script
# is frozen and is shared with eight verified milestones.
#
# ---- runtime/gfx.c IS STILL NOT LINKED ----
#
# M9 draws its result screen with an M9-OWNED routine (m9_fb_char/str/line) over
# the `static const font_data[][8]` in runtime/tables.h. gfx.c is NOT compiled
# into M9 for the same reason it was excluded from M1-M8: it carries a
# 1920x1080x4 = 8,294,400-byte back buffer that would dwarf the entire .bss
# budget. m9-verify PROVES the exclusion below rather than trusting this comment.

M9TARGET := m9gpsp
M9BUILD  := build/m9
M9ELF    := $(M9BUILD)/$(M9TARGET).elf
M9BIN    := $(M9BUILD)/$(M9TARGET).bin

# ---------------------------------------------------------------- includes --
#
# M8's include set with -Iapps/m9gpsp substituted. -Iruntime is what lets
# apps/m9gpsp/main.c find platform.h AND tables.h; -Iadapters/gba finds M2-M8's
# seven frozen adapter headers.
M9_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m9gpsp -Iadapters/gba

M9FLAGS = $(BASEFLAGS) $(M9_INCLUDES) -Wall -Wno-unused-function

# IDENTICAL TO M8's. M9 adds NO new compile flag and changes NO existing one.
# In particular there is no -DM9_* tuning knob: a fixture that measures the build
# must not be able to change the build.
#
# USE_XBGR1555_FORMAT IS STILL NOT DEFINED AND MUST NOT BE, for M7's reason: its
# absence selects the BGR555-in-RGB565 path that gba_present.c's conversion is
# derived from and that its eight-row self-test asserts.
M9_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M9_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M9LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
            -Wl,--build-id=none -Wl,-z,norelro \
            -Wl,--orphan-handling=error \
            -Wl,-Map=$(M9BUILD)/$(M9TARGET).map

# ----------------------------------------------------------------- objects --
#
# EXACTLY M8's object set with m9main.o substituted for m8main.o. NOTHING IS
# ADDED AND NOTHING IS REMOVED -- that is what makes the M9 size number directly
# comparable with the frozen M8 one.
M9_OWN_OBJS = $(M9BUILD)/m9main.o \
              $(M9BUILD)/shim.o \
              $(M9BUILD)/gba_fixture.o \
              $(M9BUILD)/gba_filestream.o \
              $(M9BUILD)/gba_compat.o \
              $(M9BUILD)/gba_probe.o \
              $(M9BUILD)/gba_bios.o \
              $(M9BUILD)/gba_rom.o \
              $(M9BUILD)/gba_m8map.o \
              $(M9BUILD)/gba_exec.o \
              $(M9BUILD)/gba_present.o \
              $(M9BUILD)/gba_input.o \
              $(M9BUILD)/platform.o

M9_GPSP_OBJS = $(M9BUILD)/gpsp_cpu.o $(M9BUILD)/gpsp_main.o \
               $(M9BUILD)/gpsp_gba_memory.o $(M9BUILD)/gpsp_video.o \
               $(M9BUILD)/gpsp_sound.o $(M9BUILD)/gpsp_serial.o \
               $(M9BUILD)/gpsp_serial_proto.o $(M9BUILD)/gpsp_rfu.o \
               $(M9BUILD)/gpsp_gbp.o $(M9BUILD)/gpsp_cheats.o \
               $(M9BUILD)/gpsp_savestate.o $(M9BUILD)/gpsp_input.o

M9OBJS = $(M9_OWN_OBJS) $(M9_GPSP_OBJS)

# EXCLUDED, unchanged from M1-M8 and none of these may ever appear above:
#   cpu_threaded.c  memmap.c  gba_cc_lut.c  bios_data.S
#   libretro/libretro.c  libretro/libretro-common/*.c
#   x86/**  arm/**  mips/**  3ds/**  bios/**  tests/**  tools/**  jni/**
#   runtime/gfx.c  -- see the note at the top of this block
#
# cpu_threaded.c IS STILL NOT LINKED. dynarec_enable is left UNDEFINED by
# gba_fixture.c so any leak is a LINK ERROR, and m9-verify proves the exclusion
# from the linked image. This matters more at M9 than anywhere earlier: M9 is the
# milestone that reports a SPEED, and a speed measured against a secretly
# recompiled core would be the most misleading number this project could publish.

$(M9BUILD):
	mkdir -p $(M9BUILD)

# ---- offline structural check ----------------------------------------------
#
# tools/m8_syntax_check.py is a C STRUCTURAL CHECKER: it strips /* */ and //
# comments and "..."/'...' literals, then balances (), [] and {}. It is pointed
# at the C FIXTURE, which is what it was written for.
#
# IT IS DELIBERATELY *NOT* POINTED AT lua/m9.lua.in. Lua comments start with --,
# which this tool does not know; every apostrophe inside a Lua comment would be
# lexed as an unterminated char literal and the check would FAIL ON CORRECT
# INPUT. Using it there would be a broken gate, not a stronger one.
m9-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m9gpsp/main.c

# ---- LUAport-owned objects -------------------------------------------------

$(M9BUILD)/m9main.o: apps/m9gpsp/main.c adapters/gba/gba_probe.h \
                     adapters/gba/gba_bios.h adapters/gba/gba_rom.h \
                     adapters/gba/gba_exec.h adapters/gba/gba_present.h \
                     adapters/gba/gba_input.h adapters/gba/gba_m8map.h \
                     $(HEADERS) | $(M9BUILD)
	$(CC) $(M9FLAGS) -c $< -o $@

$(M9BUILD)/shim.o: runtime/shim.c $(HEADERS) | $(M9BUILD)
	$(CC) $(M9FLAGS) -c $< -o $@

# M0's PS5 VideoOut AND PAD layer, COMPILED FROM UNMODIFIED SOURCE into M9's
# build directory. runtime/platform.c is frozen and is not edited for M9 -- M9 is
# simply the FIRST fixture since M0 to call plat_time_init(), an entry point it
# has always exported.
$(M9BUILD)/platform.o: runtime/platform.c $(HEADERS) | $(M9BUILD)
	$(CC) $(M9FLAGS) -c $< -o $@

$(M9BUILD)/gba_present.o: adapters/gba/gba_present.c adapters/gba/gba_present.h \
                          runtime/core.h | $(M9BUILD)
	$(CC) $(M9FLAGS) -c $< -o $@

# M8's keypad adapter, compiled from BYTE-IDENTICAL, UNMODIFIED source.
$(M9BUILD)/gba_input.o: adapters/gba/gba_input.c adapters/gba/gba_input.h | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gba_m8map.o: adapters/gba/gba_m8map.c adapters/gba/gba_m8map.h | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects -----------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, identical to M8's.
# gpsp/** is never modified and its own Makefile is never invoked.

$(M9BUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M9BUILD)
	$(CXX) $(M9_GPSPCXXFLAGS) -c $< -o $@

$(M9BUILD)/gpsp_video.o: gpsp/video.cc | $(M9BUILD)
	$(CXX) $(M9_GPSPCXXFLAGS) -c $< -o $@

$(M9BUILD)/gpsp_main.o: gpsp/main.c | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gpsp_sound.o: gpsp/sound.c | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gpsp_serial.o: gpsp/serial.c | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

$(M9BUILD)/gpsp_input.o: gpsp/input.c | $(M9BUILD)
	$(CC) $(M9_GPSPFLAGS) -c $< -o $@

m9-objs: $(M9OBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- exactly as M1-M8.
# Every object is named on the link line and is therefore included in its
# ENTIRETY, so the M9 measurement stays directly comparable with the frozen M8
# one.
$(M9ELF): $(M9OBJS) runtime/linker.ld | $(M9BUILD)
	$(CC) $(BASEFLAGS) $(M9LDFLAGS) -o $@ $(M9OBJS)

m9-link: $(M9OBJS) | $(M9BUILD)
	@$(CC) $(BASEFLAGS) $(M9LDFLAGS) -o $(M9ELF) $(M9OBJS) \
	     > $(M9BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(M9BUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M9BUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M9 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M9 LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M9 LINK OK: linker exit 0, no undefined references"

# --arena 4194304, IDENTICAL TO M4's through M8's.
$(M9BIN): $(M9ELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M9ELF) $@ \
	   --map $(M9BUILD)/$(M9TARGET).map --budget $(JIT_LIMIT) --arena 4194304

m9: $(M9BIN)

m9-size: $(M9ELF) $(M9BIN)
	@$(PYTHON) tools/sizereport.py $(M9ELF) $(M9BIN) \
	   --map $(M9BUILD)/$(M9TARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification ----------------------------------------------------------
#
# EVERY M8 GATE IS KEPT AND NONE IS WEAKENED. All five tools are invoked
# UNMODIFIED and with the same arguments, so ET_DYN, no absolute function
# immediates, R_X86_64_RELATIVE-only validation, writable and in-range relocation
# targets, symbol-kind validation, dependency closure, zero unresolved symbols,
# dynarec/libretro/C++-runtime/static-init exclusion and the 2 MB .bss tripwire
# all carry over. The VideoOut allowlist, the PAD allowlist, the sceGnm/
# sceAudioOut prohibition, the marker-based rodata carve and ALL FOUR adapter
# isolation checks carry over VERBATIM.
#
# ---- WHAT M9 ADDS, AND WHY EACH ONE IS NECESSARY ----
#
#   THE CLOCK. m9main.o must reference plat_time_init AND plat_time_us.
#   runtime/platform.c:89-92 returns 0 from plat_time_us() unless plat_time_init()
#   has resolved sceKernelGetProcessTime -- and apps/m0diag is the ONLY fixture
#   before M9 that ever called it. A regression that dropped the init call would
#   leave every span, every rate and every deadline count at ZERO, and the run
#   would still complete and still look orderly. For a milestone whose entire
#   output is timing, that is the single highest-consequence silent failure, so
#   it is a LINK-TIME gate and not only a runtime guard.
#
#   THE PACING SOURCE. m9main.o must reference plat_video_is_vsync_paced.
#   plat_video_present() either blocks on the flip event queue or falls back to
#   usleep(16000), and the two produce different presentation numbers. A report
#   that did not say which one was active would be ambiguous by construction.
#
#   THE UI IS M9's OWN. m9main.o must NOT reference any runtime/gfx.c entry
#   point, and no gfx object may be linked. gfx.c carries an 8,294,400-byte
#   1920x1080 back buffer -- linking it would blow the .bss tripwire and silently
#   change the very footprint M9 exists to report.
M9_VIDEOOUT_ALLOW = sceVideoOutOpen sceVideoOutClose sceVideoOutRegisterBuffers \
                    sceVideoOutSubmitFlip sceVideoOutSetFlipRate \
                    sceVideoOutAddFlipEvent

M9_PAD_ALLOW = scePadInit scePadGetHandle scePadRead

m9-verify: $(M9ELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M9ELF)
	@echo "== relocations =="
	@$(PYTHON) tools/verify_reloc.py $(M9ELF) | tail -12
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M9OBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M9OBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M9ELF) $(M9OBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M9ELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M9 VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== PRESENTATION ALLOWLIST (carried from M7/M8) =="
	@$(OBJCOPY) -O binary --only-section=.text $(M9ELF) $(M9BUILD)/text.bin
	@tx=$$($(NM) $(M9ELF) | awk '$$NF == "__text_start"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 ra=$$($(NM) $(M9ELF) | awk '$$NF == "__rodata_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 rz=$$($(NM) $(M9ELF) | awk '$$NF == "__rodata_end"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 if [ -z "$$tx" ] || [ -z "$$ra" ] || [ -z "$$rz" ]; then \
	     echo "M9 VERIFY FAILED: linker marker symbol missing -- need"; \
	     echo "  __text_start, __rodata_start and __rodata_end in $(M9ELF)."; \
	     exit 1; \
	 fi; \
	 t=$$((0x$$tx)); a=$$((0x$$ra)); z=$$((0x$$rz)); n=$$((z - a)); \
	 if [ "$$t" -ne 0 ]; then \
	     echo "M9 VERIFY FAILED: .text does not start at vaddr 0 (__text_start ="; \
	     echo "  $$t), so a symbol value is no longer a file offset in the"; \
	     echo "  extracted binary and this carve would read the wrong bytes."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M9 VERIFY FAILED: empty rodata window -- __rodata_start=$$a"; \
	     echo "  __rodata_end=$$z gives $$n bytes."; \
	     exit 1; \
	 fi; \
	 have=$$(wc -c < $(M9BUILD)/text.bin | tr -d " "); \
	 if [ "$$have" -lt "$$z" ]; then \
	     echo "M9 VERIFY FAILED: .text extraction is $$have bytes but the rodata"; \
	     echo "  window ends at $$z -- the extraction is short or .text is absent."; \
	     exit 1; \
	 fi; \
	 dd if=$(M9BUILD)/text.bin of=$(M9BUILD)/rodata.bin bs=1 skip=$$a count=$$n \
	    2>/dev/null || { \
	     echo "M9 VERIFY FAILED: dd could not carve the rodata window."; exit 1; \
	 }; \
	 if [ ! -s $(M9BUILD)/rodata.bin ]; then \
	     echo "M9 VERIFY FAILED: rodata extraction produced 0 bytes -- every check"; \
	     echo "  below would pass vacuously. Fix the extraction, not the checks."; \
	     exit 1; \
	 fi; \
	 got=$$(wc -c < $(M9BUILD)/rodata.bin | tr -d " "); \
	 if [ "$$got" -ne "$$n" ]; then \
	     echo "M9 VERIFY FAILED: rodata.bin is $$got bytes, expected $$n ="; \
	     echo "  __rodata_end - __rodata_start. Extraction is not trustworthy."; \
	     exit 1; \
	 fi; \
	 echo "rodata carved from .text by linker markers: [$$a,$$z) = $$n bytes"
	@for s in $(M9_VIDEOOUT_ALLOW) libSceVideoOut.sprx; do \
	     if ! grep -aq "$$s" $(M9BUILD)/rodata.bin; then \
	         echo "M9 VERIFY FAILED: required presentation name '$$s' is NOT in"; \
	         echo "  the image -- VideoOut was not linked and any 'presentation"; \
	         echo "  rate' would be meaningless."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 6 VideoOut entry points + libSceVideoOut.sprx are present"
	@allow=$$(echo $(M9_VIDEOOUT_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "sceVideoOut[A-Za-z0-9_]*" $(M9BUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M9 VERIFY FAILED: an UNEXPECTED sceVideoOut* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no sceVideoOut* name outside the six-symbol allowlist"
	@if grep -aqE "sceGnm|sceAudioOut" $(M9BUILD)/rodata.bin; then \
	     echo "M9 VERIFY FAILED: a graphics-pipeline or audio name reached the image:"; \
	     grep -aoE "sceGnm[A-Za-z0-9_]*|sceAudioOut[A-Za-z0-9_]*" \
	        $(M9BUILD)/rodata.bin | sort -u | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no sceGnm* and no sceAudioOut* name is present"
	@echo "== PAD ALLOWLIST (carried from M8) =="
	@for s in $(M9_PAD_ALLOW) libScePad.sprx; do \
	     if ! grep -aq "$$s" $(M9BUILD)/rodata.bin; then \
	         echo "M9 VERIFY FAILED: required pad name '$$s' is NOT in the image."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 3 scePad entry points + libScePad.sprx are present"
	@allow=$$(echo $(M9_PAD_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "scePad[A-Za-z0-9_]*" $(M9BUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M9 VERIFY FAILED: an UNEXPECTED scePad* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no scePad* name outside the three-symbol allowlist"
	@echo "== THE CLOCK IS REALLY INITIALISED (M9-specific) =="
	@echo "   plat_time_us() returns 0 unless plat_time_init() ran. M9 is the"
	@echo "   first fixture since m0diag to need it, and every number M9 prints"
	@echo "   would be zero if this call were ever dropped."
	@miss=""; \
	 for s in plat_time_init plat_time_us; do \
	     $(NM) -u $(M9BUILD)/m9main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M9 VERIFY FAILED: m9main.o does NOT reference:$$miss"; \
	     echo "  The timing baseline would be measured with a DEAD CLOCK and"; \
	     echo "  would report zeros as though they were results."; \
	     exit 1; \
	 fi; \
	 echo "m9main.o references plat_time_init AND plat_time_us"
	@if ! $(NM) -u $(M9BUILD)/m9main.o 2>/dev/null | grep -q "plat_video_is_vsync_paced"; then \
	     echo "M9 VERIFY FAILED: m9main.o does NOT reference"; \
	     echo "  plat_video_is_vsync_paced -- the report could not state whether"; \
	     echo "  presentation was paced by real flip events or by the 16 ms"; \
	     echo "  usleep fallback, leaving every rate ambiguous."; \
	     exit 1; \
	 fi; \
	 echo "m9main.o reports the pacing source (plat_video_is_vsync_paced)"
	@echo "== THE RESULT SCREEN IS M9-OWNED, gfx.c IS NOT LINKED (M9-specific) =="
	@bad=$$(echo "$(M9OBJS)" | tr ' ' '\n' | grep -E "/gfx\.o$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M9 VERIFY FAILED: a gfx object is in the M9 link line:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no gfx object appears in the M9 object set"
	@bad=$$($(NM) -u $(M9BUILD)/m9main.o 2>/dev/null \
	          | grep -E "(draw_char|draw_str|draw_centered|draw_hline|ui_fill|blit_ui|clear_fb|fill_rect|draw_border)" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M9 VERIFY FAILED: m9main.o reaches runtime/gfx.c:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     echo "  gfx.c carries a 1920x1080x4 back buffer (8,294,400 bytes) that"; \
	     echo "  would blow the .bss tripwire and change the footprint M9 exists"; \
	     echo "  to measure. M9 draws with its own routine over tables.h's font."; \
	     exit 1; \
	 fi; \
	 echo "m9main.o carries NO runtime/gfx.c relocation"
	@echo "== THE MEASURED PATH IS REALLY DRIVEN (M9-specific) =="
	@miss=""; \
	 for s in plat_pad_init plat_pad_read plat_pad_shutdown \
	          plat_video_init plat_video_present plat_video_get_framebuffer \
	          m5_exec_run gba_present_blit; do \
	     $(NM) -u $(M9BUILD)/m9main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M9 VERIFY FAILED: m9main.o does NOT reference:$$miss"; \
	     echo "  One of the five measured spans has no call site, so its timing"; \
	     echo "  would be a constant zero rather than a measurement."; \
	     exit 1; \
	 fi; \
	 echo "m9main.o drives input, emulation, conversion and presentation"
	@miss=""; \
	 for s in m8_input_selftest m8_input_from_pad m8_input_to_reg_p1 \
	          m8_input_apply m8_input_irq_edge m8_input_read_reg_if; do \
	     $(NM) -u $(M9BUILD)/m9main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M9 VERIFY FAILED: m9main.o does NOT reference:$$miss"; \
	     echo "  M8's proven input chain is not being exercised during the"; \
	     echo "  sustained run, so the baseline would describe a DIFFERENT and"; \
	     echo "  cheaper workload than the one M8 verified."; \
	     exit 1; \
	 fi; \
	 echo "m9main.o carries M8's keypad chain through the whole run"
	@miss=""; \
	 for s in m8map_probe m8map_report m8map_read m8map_expected_blocks; do \
	     $(NM) -u $(M9BUILD)/m9main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M9 VERIFY FAILED: m9main.o does NOT reference:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "m9main.o drives the full m8map_* probe surface"
	@echo "== ADAPTER ISOLATION (all four carried VERBATIM from M8) =="
	@if ! $(NM) -u $(M9BUILD)/gba_input.o 2>/dev/null | grep -q "flag_interrupt"; then \
	     echo "M9 VERIFY FAILED: gba_input.o does NOT reference flag_interrupt."; \
	     exit 1; \
	 fi; \
	 echo "gba_input.o references gpSP's flag_interrupt (the IRQ is really raised)"
	@bad=$$($(NM) -u $(M9BUILD)/gba_input.o 2>/dev/null \
	          | grep -E "plat_pad_|plat_video_|scePad|sceVideoOut" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M9 VERIFY FAILED: the keypad adapter reaches the platform layer:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "gba_input.o carries NO plat_*/scePad*/sceVideoOut* relocation"
	@if ! $(NM) -u $(M9BUILD)/gba_input.o 2>/dev/null | grep -q "io_registers"; then \
	     echo "M9 VERIFY FAILED: gba_input.o does NOT reference io_registers."; \
	     exit 1; \
	 fi; \
	 echo "gba_input.o references gpSP's io_registers (REG_P1 is really written)"
	@pad=$$($(NM) -u $(M9BUILD)/gba_present.o 2>/dev/null \
	          | grep -E "plat_pad_|scePad" || true); \
	 if [ -n "$$pad" ]; then \
	     echo "M9 VERIFY FAILED: the PRESENTATION path learned about input:"; \
	     echo "$$pad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "gba_present.o still carries NO plat_pad_*/scePad* relocation"
	@bad=$$($(NM) -u $(M9BUILD)/gba_m8map.o 2>/dev/null \
	          | grep -E "plat_pad_|plat_video_|scePad|sceVideoOut|m4_rom_|m5_exec_" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M9 VERIFY FAILED: the mapping probe reaches outside gpSP:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "gba_m8map.o carries NO plat_*/scePad*/sceVideoOut*/m4_*/m5_* relocation"
	@miss=""; \
	 for s in memory_map_read gamepak_buffers gamepak_size gamepak_file_blocks \
	          gamepak_must_swap bios_rom; do \
	     $(NM) -u $(M9BUILD)/gba_m8map.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M9 VERIFY FAILED: gba_m8map.o does NOT reference:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "gba_m8map.o reads gpSP's real mapping state"
	@if $(NM) -u $(M9BUILD)/gba_rom.o 2>/dev/null | grep -q "m8map_"; then \
	     echo "M9 VERIFY FAILED: gba_rom.o references the M8 probe -- the FROZEN"; \
	     echo "  M4 adapter has been edited."; \
	     exit 1; \
	 fi; \
	 echo "gba_rom.o is untouched -- no m8map_* reference (M4-M8 stay frozen)"

# ---- hardware smoke-test delivery ------------------------------------------
#
# THE ONLY M9 TARGET THAT TOUCHES HARDWARE. Everything above is offline.
#
# The delivery architecture is M0's, reused wholesale and unmodified for the
# tenth time: tools/mklua.py for packaging, tools/send.py for transport, TCP 9026
# for the script, UDP 9027 for the log, TCP 9028-9045 for the blob, same 'K' ACK
# handshake, same ext_args layout. Neither tool is changed nor forked.
#
# lua/m9.lua.in resolves the userId into ext.dbg[3] exactly as lua/m8.lua.in
# does -- scePadGetHandle needs it -- and decodes the M9-specific statuses and
# the M9 dbg[] measurement packing, which no earlier script knows about.
M9_LUA_TEMPLATE := lua/m9.lua.in
M9_LUA_OUT      := $(M9BUILD)/$(M9TARGET).lua

$(M9_LUA_OUT): $(M9_LUA_TEMPLATE) tools/mklua.py $(M9BIN) $(M9ELF)
	@$(PYTHON) tools/mklua.py $(M9_LUA_TEMPLATE) $(M9BIN) $@ $(M9ELF)

m9-lua: m9-check $(M9_LUA_OUT)

# m9-verify is a PREREQUISITE, not a suggestion -- same rule as m1-send through
# m8-send. Nothing is transmitted unless every offline check passes first.
#
# THERE IS DELIBERATELY NO m9-upload-rom. M9 reuses M4's TRANSPORT with M8's
# CARTRIDGE -- the keypad test ROM -- and M3's BIOS. Both must ALREADY be on the
# console:
#     make m3-upload-bios PS5_IP=<addr> BIOS=<gba_bios.bin>
#     make m4-upload-rom  PS5_IP=<addr> ROM=mgba/cinema/gba/irq/keyirq/test.gba
#
# THEN RELAUNCH THE HOST GAME, AND RUN M9 ONCE PER LAUNCH, for M7's reason
# unchanged: plat_video_init() reserves 16 MB of direct memory and DELIBERATELY
# NEVER RELEASES IT, so a second run in the same session fails with
# PLAT_VID_EALLOC (-24). lua/m9.lua.in labels it with that exact wording.
m9-send: $(M9_LUA_OUT) $(M9BIN) m9-verify
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m9-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M9_LUA_OUT) $(M9BIN)

m9-clean:
	rm -rf $(M9BUILD)

.PHONY: m9 m9-objs m9-check m9-link m9-size m9-verify m9-lua m9-send m9-clean

# =============================================================================
# M9-D -- THE CONVERSION-COST DIAGNOSTIC
# =============================================================================
#
# PURELY ADDITIVE, AND M9 IS NOT TOUCHED. Not one M0-M9 variable, rule or
# prerequisite above is edited, reordered or reused destructively. M9-D gets its
# OWN build directory (build/m9d), its OWN object set and its OWN targets,
# exactly as M9 got its own from M8.
#
# THE M9 BASELINE REMAINS INDEPENDENTLY REPRODUCIBLE. build/m9/m9gpsp.bin,
# build/m9/m9gpsp.elf and build/m9/m9gpsp.lua are never written by any target
# below -- M9BUILD does not appear in a single M9-D recipe -- so the ~298 ms
# control result can be re-run at any time without rebuilding anything.
#
# ---- WHAT M9-D ADDS TO THE IMAGE ----
#
# ONE OBJECT, m9dmain.o, substituted for m9main.o. NO NEW ADAPTER, no new gpSP
# file, no new library, and NOT ONE BYTE of adapters/gba/** or runtime/** is
# modified. In particular adapters/gba/gba_present.c is compiled from the SAME
# UNMODIFIED SOURCE and linked into M9-D, where it remains THE CONTROL: the
# fixture times the real gba_present_blit() on half its frames and its own
# instrumented replica on the other half, and prints the agreement between them.
#
# m9d-verify proves all of that from the linked image rather than trusting this
# comment -- including a byte comparison of build/m9d/gba_present.o against
# build/m9/gba_present.o, which is the strongest available statement that the
# frozen presentation adapter was not edited for this milestone.
#
# ---- THE SIZE GATE ----
#
# M9 linked with __data_load at 270,528 and __data_start at 393,216: 2 mappings,
# 131,072 bytes of headroom against JIT_LIMIT (524,288).
#
# runtime/linker.ld:146 is `. = ALIGN(0x10000) + 0x10000`, so __data_start moves
# in 64 KiB STEPS, not smoothly:
#
#     __data_load <= 327,679   ->  __data_start 393,216   headroom 131,072
#     __data_load <= 393,215   ->  __data_start 458,752   headroom  65,536
#     __data_load <= 458,751   ->  __data_start 524,288   headroom       0  FAIL
#
# M9-D therefore has 57,152 bytes of image growth before the footprint STEPS to
# 458,752 (still a pass), and 188,224 before the 524,288 gate fails outright.
# `make m9d-size` is to be run IMMEDIATELY after the first successful link.
#
# IF THE FOOTPRINT EXCEEDS 524,288 THE RULE IS: STOP AND REPORT. Do not optimise
# the fixture to force it under the gate, do not delete a probe and do not touch
# runtime/linker.ld -- the linker script is frozen and shared with nine verified
# milestones.
#
# ---- .bss ----
#
# M9 linked with 1,415,200 bytes of .bss against check_forbidden.py's 2,097,152
# tripwire. M9-D adds exactly two objects: m9d_stage_row (1440 x 4 = 5,760) and
# m9d_bss_pool (16384 x 4 = 65,536), i.e. 71,296 bytes, for a predicted
# 1,486,496 -- 610,656 below the tripwire. Both are asserted at COMPILE TIME by
# _Static_assert in the fixture, so a geometry drift is a build failure.

M9DTARGET := m9diag
M9DBUILD  := build/m9d
M9DELF    := $(M9DBUILD)/$(M9DTARGET).elf
M9DBIN    := $(M9DBUILD)/$(M9DTARGET).bin

# ---------------------------------------------------------------- includes --
#
# M9's include set with -Iapps/m9diag substituted. NOTHING ELSE CHANGES.
M9D_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m9diag -Iadapters/gba

M9DFLAGS = $(BASEFLAGS) $(M9D_INCLUDES) -Wall -Wno-unused-function

# IDENTICAL TO M9's. M9-D adds NO new compile flag and changes NO existing one.
# THE OPTIMISATION LEVEL IS NOT TOUCHED: -Os comes from BASEFLAGS unchanged, and
# changing it would invalidate the very comparison against the M9 baseline that
# this milestone exists to make.
M9D_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M9D_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M9DLDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
             -Wl,--build-id=none -Wl,-z,norelro \
             -Wl,--orphan-handling=error \
             -Wl,-Map=$(M9DBUILD)/$(M9DTARGET).map

# ----------------------------------------------------------------- objects --
#
# EXACTLY M9's object set with m9dmain.o substituted for m9main.o. NOTHING IS
# ADDED AND NOTHING IS REMOVED -- that is what makes the M9-D size number
# directly comparable with the M9 one, and it is what keeps gba_present.o in the
# image as the control.
M9D_OWN_OBJS = $(M9DBUILD)/m9dmain.o \
               $(M9DBUILD)/shim.o \
               $(M9DBUILD)/gba_fixture.o \
               $(M9DBUILD)/gba_filestream.o \
               $(M9DBUILD)/gba_compat.o \
               $(M9DBUILD)/gba_probe.o \
               $(M9DBUILD)/gba_bios.o \
               $(M9DBUILD)/gba_rom.o \
               $(M9DBUILD)/gba_m8map.o \
               $(M9DBUILD)/gba_exec.o \
               $(M9DBUILD)/gba_present.o \
               $(M9DBUILD)/gba_input.o \
               $(M9DBUILD)/platform.o

M9D_GPSP_OBJS = $(M9DBUILD)/gpsp_cpu.o $(M9DBUILD)/gpsp_main.o \
                $(M9DBUILD)/gpsp_gba_memory.o $(M9DBUILD)/gpsp_video.o \
                $(M9DBUILD)/gpsp_sound.o $(M9DBUILD)/gpsp_serial.o \
                $(M9DBUILD)/gpsp_serial_proto.o $(M9DBUILD)/gpsp_rfu.o \
                $(M9DBUILD)/gpsp_gbp.o $(M9DBUILD)/gpsp_cheats.o \
                $(M9DBUILD)/gpsp_savestate.o $(M9DBUILD)/gpsp_input.o

M9DOBJS = $(M9D_OWN_OBJS) $(M9D_GPSP_OBJS)

# EXCLUDED, unchanged from M1-M9 and none of these may ever appear above:
#   cpu_threaded.c  memmap.c  gba_cc_lut.c  bios_data.S
#   libretro/libretro.c  libretro/libretro-common/*.c
#   x86/**  arm/**  mips/**  3ds/**  bios/**  tests/**  tools/**  jni/**
#   runtime/gfx.c
#
# cpu_threaded.c IS STILL NOT LINKED. This matters as much at M9-D as at M9: a
# conversion cost measured beside a secretly recompiled CPU core would let a
# reader attribute the wrong share of the frame to the wrong stage.

$(M9DBUILD):
	mkdir -p $(M9DBUILD)

# ---- offline structural check ----------------------------------------------
#
# Same tool, same reason, pointed at the M9-D fixture. It is deliberately NOT
# pointed at lua/m9diag.lua.in: Lua comments start with --, which this C
# checker does not know, so every apostrophe in a Lua comment would be lexed as
# an unterminated char literal and the gate would FAIL ON CORRECT INPUT.
m9d-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m9diag/main.c

# ---- LUAport-owned objects -------------------------------------------------

$(M9DBUILD)/m9dmain.o: apps/m9diag/main.c adapters/gba/gba_probe.h \
                       adapters/gba/gba_bios.h adapters/gba/gba_rom.h \
                       adapters/gba/gba_exec.h adapters/gba/gba_present.h \
                       adapters/gba/gba_input.h adapters/gba/gba_m8map.h \
                       $(HEADERS) | $(M9DBUILD)
	$(CC) $(M9DFLAGS) -c $< -o $@

$(M9DBUILD)/shim.o: runtime/shim.c $(HEADERS) | $(M9DBUILD)
	$(CC) $(M9DFLAGS) -c $< -o $@

$(M9DBUILD)/platform.o: runtime/platform.c $(HEADERS) | $(M9DBUILD)
	$(CC) $(M9DFLAGS) -c $< -o $@

# M7's presentation adapter, compiled from BYTE-IDENTICAL, UNMODIFIED source
# with the SAME flags M9 used. THIS OBJECT IS THE CONTROL. m9d-verify byte-
# compares it against build/m9/gba_present.o when that file is present.
$(M9DBUILD)/gba_present.o: adapters/gba/gba_present.c adapters/gba/gba_present.h \
                           runtime/core.h | $(M9DBUILD)
	$(CC) $(M9DFLAGS) -c $< -o $@

$(M9DBUILD)/gba_input.o: adapters/gba/gba_input.c adapters/gba/gba_input.h | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gba_m8map.o: adapters/gba/gba_m8map.c adapters/gba/gba_m8map.h | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects -----------------------------------------------------

$(M9DBUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M9DBUILD)
	$(CXX) $(M9D_GPSPCXXFLAGS) -c $< -o $@

$(M9DBUILD)/gpsp_video.o: gpsp/video.cc | $(M9DBUILD)
	$(CXX) $(M9D_GPSPCXXFLAGS) -c $< -o $@

$(M9DBUILD)/gpsp_main.o: gpsp/main.c | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gpsp_sound.o: gpsp/sound.c | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gpsp_serial.o: gpsp/serial.c | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

$(M9DBUILD)/gpsp_input.o: gpsp/input.c | $(M9DBUILD)
	$(CC) $(M9D_GPSPFLAGS) -c $< -o $@

m9d-objs: $(M9DOBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- exactly as M1-M9.
$(M9DELF): $(M9DOBJS) runtime/linker.ld | $(M9DBUILD)
	$(CC) $(BASEFLAGS) $(M9DLDFLAGS) -o $@ $(M9DOBJS)

m9d-link: $(M9DOBJS) | $(M9DBUILD)
	@$(CC) $(BASEFLAGS) $(M9DLDFLAGS) -o $(M9DELF) $(M9DOBJS) \
	     > $(M9DBUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(M9DBUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M9DBUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M9-D LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M9-D LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M9-D LINK OK: linker exit 0, no undefined references"

# --arena 4194304, IDENTICAL TO M4's through M9's.
$(M9DBIN): $(M9DELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M9DELF) $@ \
	   --map $(M9DBUILD)/$(M9DTARGET).map --budget $(JIT_LIMIT) --arena 4194304

m9d: $(M9DBIN)

m9d-size: $(M9DELF) $(M9DBIN)
	@$(PYTHON) tools/sizereport.py $(M9DELF) $(M9DBIN) \
	   --map $(M9DBUILD)/$(M9DTARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification ----------------------------------------------------------
#
# EVERY M9 GATE IS KEPT AND NOT ONE IS WEAKENED. All five tools are invoked
# UNMODIFIED and with the same arguments; the VideoOut allowlist, the PAD
# allowlist, the sceGnm/sceAudioOut prohibition, the marker-based rodata carve,
# the clock gates, the gfx.c exclusion and ALL FOUR adapter isolation checks
# carry over VERBATIM with M9D paths substituted.
#
# ---- WHAT M9-D ADDS, AND WHY EACH ONE IS NECESSARY ----
#
#   THE CONTROL IS REALLY THE PRODUCTION BLIT. m9dmain.o must reference
#   gba_present_blit, and gba_present.o must be in the link line. If M9-D ever
#   stopped calling the real adapter and measured only its own replica, the
#   agreement check on the result screen would be comparing the replica with
#   itself and would pass vacuously while proving nothing.
#
#   M9-D AND M9 COMPILE THE SAME ADAPTER. build/m9d/gba_present.o is byte-
#   compared against build/m9/gba_present.o. Same source, same flags, same
#   compiler -- so a difference means the two builds are NOT looking at the same
#   adapter, which would make the control something other than the production
#   blit. Skipped with a warning, never silently, when the M9 build is absent.
#
#   CORRECTED AT M9-E. This gate was previously described as proving "the
#   presentation adapter was not edited", which it never did and cannot do: both
#   objects are rebuilt from the SAME adapters/gba/gba_present.c, so editing that
#   file changes both of them and the comparison still passes. What it actually
#   proves is that the two builds agree -- which is the property the control
#   depends on, and which is worth keeping. M9-E DID edit the adapter (the blit
#   is now write-only; see adapters/gba/gba_present.c), the gate still passes,
#   and that is correct behaviour rather than a hole. NOTE that it compares
#   BUILT OBJECTS, so a STALE build/m9/gba_present.o will fail it spuriously:
#   run 'make m9-objs' after touching the adapter. The historical read-back
#   algorithm is preserved verbatim, in fixture-owned code, as M9-D's own
#   m9d_blit_split replica -- which the next gate below requires to exist.
#
#   THE REPLICA AND THE PROBES ARE M9-D-OWNED. m9d_blit_split, m9d_probe_p1,
#   m9d_probe_read and m9d_probe_write must appear in m9dmain.o and must NOT
#   appear in gba_present.o or in any adapter object. A probe that leaked into
#   the adapter could be reached from the production path.
#
#   THE PROBES ARE NOT THE PRESENTATION PATH. gba_present.o must not reference
#   any m9d_* symbol, which is the link-time statement that P1/P2/P3 cannot be
#   invoked by anything the production blit calls.
M9D_VIDEOOUT_ALLOW = sceVideoOutOpen sceVideoOutClose sceVideoOutRegisterBuffers \
                     sceVideoOutSubmitFlip sceVideoOutSetFlipRate \
                     sceVideoOutAddFlipEvent

M9D_PAD_ALLOW = scePadInit scePadGetHandle scePadRead

m9d-verify: $(M9DELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M9DELF)
	@echo "== relocations =="
	@$(PYTHON) tools/verify_reloc.py $(M9DELF) | tail -12
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M9DOBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M9DOBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M9DELF) $(M9DOBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M9DELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M9-D VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== PRESENTATION ALLOWLIST (carried from M7/M8/M9) =="
	@$(OBJCOPY) -O binary --only-section=.text $(M9DELF) $(M9DBUILD)/text.bin
	@tx=$$($(NM) $(M9DELF) | awk '$$NF == "__text_start"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 ra=$$($(NM) $(M9DELF) | awk '$$NF == "__rodata_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 rz=$$($(NM) $(M9DELF) | awk '$$NF == "__rodata_end"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 if [ -z "$$tx" ] || [ -z "$$ra" ] || [ -z "$$rz" ]; then \
	     echo "M9-D VERIFY FAILED: linker marker symbol missing."; \
	     exit 1; \
	 fi; \
	 t=$$((0x$$tx)); a=$$((0x$$ra)); z=$$((0x$$rz)); n=$$((z - a)); \
	 if [ "$$t" -ne 0 ]; then \
	     echo "M9-D VERIFY FAILED: .text does not start at vaddr 0."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M9-D VERIFY FAILED: empty rodata window."; \
	     exit 1; \
	 fi; \
	 have=$$(wc -c < $(M9DBUILD)/text.bin | tr -d " "); \
	 if [ "$$have" -lt "$$z" ]; then \
	     echo "M9-D VERIFY FAILED: .text extraction is short."; \
	     exit 1; \
	 fi; \
	 dd if=$(M9DBUILD)/text.bin of=$(M9DBUILD)/rodata.bin bs=1 skip=$$a count=$$n \
	    2>/dev/null || { \
	     echo "M9-D VERIFY FAILED: dd could not carve the rodata window."; exit 1; \
	 }; \
	 if [ ! -s $(M9DBUILD)/rodata.bin ]; then \
	     echo "M9-D VERIFY FAILED: rodata extraction produced 0 bytes."; \
	     exit 1; \
	 fi; \
	 got=$$(wc -c < $(M9DBUILD)/rodata.bin | tr -d " "); \
	 if [ "$$got" -ne "$$n" ]; then \
	     echo "M9-D VERIFY FAILED: rodata.bin is $$got bytes, expected $$n."; \
	     exit 1; \
	 fi; \
	 echo "rodata carved from .text by linker markers: [$$a,$$z) = $$n bytes"
	@for s in $(M9D_VIDEOOUT_ALLOW) libSceVideoOut.sprx; do \
	     if ! grep -aq "$$s" $(M9DBUILD)/rodata.bin; then \
	         echo "M9-D VERIFY FAILED: required presentation name '$$s' is NOT"; \
	         echo "  in the image."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 6 VideoOut entry points + libSceVideoOut.sprx are present"
	@allow=$$(echo $(M9D_VIDEOOUT_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "sceVideoOut[A-Za-z0-9_]*" $(M9DBUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M9-D VERIFY FAILED: an UNEXPECTED sceVideoOut* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no sceVideoOut* name outside the six-symbol allowlist"
	@if grep -aqE "sceGnm|sceAudioOut" $(M9DBUILD)/rodata.bin; then \
	     echo "M9-D VERIFY FAILED: a graphics-pipeline or audio name reached the image:"; \
	     grep -aoE "sceGnm[A-Za-z0-9_]*|sceAudioOut[A-Za-z0-9_]*" \
	        $(M9DBUILD)/rodata.bin | sort -u | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no sceGnm* and no sceAudioOut* name is present (NO AUDIO, NO GNM)"
	@echo "== PAD ALLOWLIST (carried from M8/M9) =="
	@for s in $(M9D_PAD_ALLOW) libScePad.sprx; do \
	     if ! grep -aq "$$s" $(M9DBUILD)/rodata.bin; then \
	         echo "M9-D VERIFY FAILED: required pad name '$$s' is NOT in the image."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 3 scePad entry points + libScePad.sprx are present"
	@allow=$$(echo $(M9D_PAD_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "scePad[A-Za-z0-9_]*" $(M9DBUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M9-D VERIFY FAILED: an UNEXPECTED scePad* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no scePad* name outside the three-symbol allowlist"
	@echo "== THE CLOCK IS REALLY INITIALISED (carried from M9) =="
	@miss=""; \
	 for s in plat_time_init plat_time_us; do \
	     $(NM) -u $(M9DBUILD)/m9dmain.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M9-D VERIFY FAILED: m9dmain.o does NOT reference:$$miss"; \
	     echo "  Every span, every probe and every bandwidth figure would be"; \
	     echo "  zero and the run would still look orderly."; \
	     exit 1; \
	 fi; \
	 echo "m9dmain.o references plat_time_init AND plat_time_us"
	@if ! $(NM) -u $(M9DBUILD)/m9dmain.o 2>/dev/null | grep -q "plat_video_is_vsync_paced"; then \
	     echo "M9-D VERIFY FAILED: m9dmain.o does NOT reference"; \
	     echo "  plat_video_is_vsync_paced -- the pacing source would be ambiguous."; \
	     exit 1; \
	 fi; \
	 echo "m9dmain.o reports the pacing source (plat_video_is_vsync_paced)"
	@echo "== THE RESULT SCREEN IS M9-D-OWNED, gfx.c IS NOT LINKED =="
	@bad=$$(echo "$(M9DOBJS)" | tr ' ' '\n' | grep -E "/gfx\.o$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M9-D VERIFY FAILED: a gfx object is in the M9-D link line:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no gfx object appears in the M9-D object set"
	@bad=$$($(NM) -u $(M9DBUILD)/m9dmain.o 2>/dev/null \
	          | grep -E "(draw_char|draw_str|draw_centered|draw_hline|ui_fill|blit_ui|clear_fb|fill_rect|draw_border)" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M9-D VERIFY FAILED: m9dmain.o reaches runtime/gfx.c:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "m9dmain.o carries NO runtime/gfx.c relocation"
	@echo "== THE MEASURED PATH IS REALLY DRIVEN (carried from M9) =="
	@miss=""; \
	 for s in plat_pad_init plat_pad_read plat_pad_shutdown \
	          plat_video_init plat_video_present plat_video_get_framebuffer \
	          plat_video_fill m5_exec_run gba_present_blit; do \
	     $(NM) -u $(M9DBUILD)/m9dmain.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M9-D VERIFY FAILED: m9dmain.o does NOT reference:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "m9dmain.o drives input, emulation, conversion and presentation"
	@miss=""; \
	 for s in m8_input_selftest m8_input_from_pad m8_input_to_reg_p1 \
	          m8_input_apply m8_input_irq_edge m8_input_read_reg_if; do \
	     $(NM) -u $(M9DBUILD)/m9dmain.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M9-D VERIFY FAILED: m9dmain.o does NOT reference:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "m9dmain.o carries M8's keypad chain through the whole run"
	@miss=""; \
	 for s in m8map_probe m8map_report m8map_read m8map_expected_blocks; do \
	     $(NM) -u $(M9DBUILD)/m9dmain.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M9-D VERIFY FAILED: m9dmain.o does NOT reference:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "m9dmain.o drives the full m8map_* probe surface"
	@echo "== M9-D SPECIFIC: THE PRODUCTION BLIT IS STILL THE CONTROL =="
	@if ! echo "$(M9DOBJS)" | tr ' ' '\n' | grep -q "/gba_present\.o$$"; then \
	     echo "M9-D VERIFY FAILED: gba_present.o is NOT in the M9-D link line."; \
	     echo "  The control would not exist and the B1/B2 split would be"; \
	     echo "  compared against nothing."; \
	     exit 1; \
	 fi; \
	 echo "gba_present.o IS linked -- the production blit is in the image"
	@if ! $(NM) -u $(M9DBUILD)/m9dmain.o 2>/dev/null | grep -q "gba_present_blit"; then \
	     echo "M9-D VERIFY FAILED: m9dmain.o does NOT call gba_present_blit."; \
	     echo "  M9-D would be timing only its own replica, and the control/"; \
	     echo "  replica agreement check would pass vacuously."; \
	     exit 1; \
	 fi; \
	 echo "m9dmain.o CALLS the real gba_present_blit (the control is live)"
	@echo "== M9-D SPECIFIC: THE FROZEN ADAPTER WAS NOT EDITED =="
	@if [ -f $(M9BUILD)/gba_present.o ]; then \
	     if cmp -s $(M9DBUILD)/gba_present.o $(M9BUILD)/gba_present.o; then \
	         echo "gba_present.o is BYTE-IDENTICAL to the M9 build -- "; \
	         echo "  adapters/gba/gba_present.c was not modified for M9-D"; \
	     else \
	         echo "M9-D VERIFY FAILED: build/m9d/gba_present.o differs from"; \
	         echo "  build/m9/gba_present.o. Same source, same flags, same"; \
	         echo "  compiler -- so adapters/gba/gba_present.c HAS BEEN EDITED."; \
	         echo "  That file is frozen. Revert it; do not update this gate."; \
	         exit 1; \
	     fi; \
	 else \
	     echo "WARNING: $(M9BUILD)/gba_present.o is absent, so the byte"; \
	     echo "  comparison against the M9 build was SKIPPED. Run 'make m9-objs'"; \
	     echo "  to enable this gate. It is skipped, never passed silently."; \
	 fi
	@echo "== M9-D SPECIFIC: THE REPLICA AND PROBES ARE M9-D-OWNED =="
	@miss=""; \
	 for s in m9d_blit_split m9d_probe_p1 m9d_probe_read m9d_probe_write; do \
	     $(NM) $(M9DBUILD)/m9dmain.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M9-D VERIFY FAILED: m9dmain.o does not define:$$miss"; \
	     echo "  The diagnostic would report zeros for a measurement it never"; \
	     echo "  took."; \
	     exit 1; \
	 fi; \
	 echo "m9dmain.o defines the replica and all three bandwidth probes"
	@bad=""; \
	 for o in $(M9D_OWN_OBJS); do \
	     case "$$o" in *m9dmain.o) continue;; esac; \
	     if $(NM) $$o 2>/dev/null | grep -q "m9d_"; then bad="$$bad $$o"; fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M9-D VERIFY FAILED: an M9-D diagnostic symbol leaked into:"; \
	     echo "$$bad" | tr ' ' '\n' | sed "s/^/  /"; \
	     echo "  The replica and the probes are FIXTURE-OWNED. A probe reachable"; \
	     echo "  from an adapter could be invoked by the production path."; \
	     exit 1; \
	 fi; \
	 echo "no m9d_* symbol appears in ANY adapter or runtime object"
	@if $(NM) -u $(M9DBUILD)/gba_present.o 2>/dev/null | grep -q "m9d_\|plat_time_"; then \
	     echo "M9-D VERIFY FAILED: gba_present.o reaches the diagnostic or the"; \
	     echo "  clock. The production blit must not know it is being measured."; \
	     exit 1; \
	 fi; \
	 echo "gba_present.o carries NO m9d_*/plat_time_* relocation"
	@echo "== ADAPTER ISOLATION (all four carried VERBATIM from M8/M9) =="
	@if ! $(NM) -u $(M9DBUILD)/gba_input.o 2>/dev/null | grep -q "flag_interrupt"; then \
	     echo "M9-D VERIFY FAILED: gba_input.o does NOT reference flag_interrupt."; \
	     exit 1; \
	 fi; \
	 echo "gba_input.o references gpSP's flag_interrupt (the IRQ is really raised)"
	@bad=$$($(NM) -u $(M9DBUILD)/gba_input.o 2>/dev/null \
	          | grep -E "plat_pad_|plat_video_|scePad|sceVideoOut" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M9-D VERIFY FAILED: the keypad adapter reaches the platform layer:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "gba_input.o carries NO plat_*/scePad*/sceVideoOut* relocation"
	@if ! $(NM) -u $(M9DBUILD)/gba_input.o 2>/dev/null | grep -q "io_registers"; then \
	     echo "M9-D VERIFY FAILED: gba_input.o does NOT reference io_registers."; \
	     exit 1; \
	 fi; \
	 echo "gba_input.o references gpSP's io_registers (REG_P1 is really written)"
	@pad=$$($(NM) -u $(M9DBUILD)/gba_present.o 2>/dev/null \
	          | grep -E "plat_pad_|scePad" || true); \
	 if [ -n "$$pad" ]; then \
	     echo "M9-D VERIFY FAILED: the PRESENTATION path learned about input:"; \
	     echo "$$pad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "gba_present.o still carries NO plat_pad_*/scePad* relocation"
	@bad=$$($(NM) -u $(M9DBUILD)/gba_m8map.o 2>/dev/null \
	          | grep -E "plat_pad_|plat_video_|scePad|sceVideoOut|m4_rom_|m5_exec_" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M9-D VERIFY FAILED: the mapping probe reaches outside gpSP:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "gba_m8map.o carries NO plat_*/scePad*/sceVideoOut*/m4_*/m5_* relocation"
	@miss=""; \
	 for s in memory_map_read gamepak_buffers gamepak_size gamepak_file_blocks \
	          gamepak_must_swap bios_rom; do \
	     $(NM) -u $(M9DBUILD)/gba_m8map.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M9-D VERIFY FAILED: gba_m8map.o does NOT reference:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "gba_m8map.o reads gpSP's real mapping state"
	@if $(NM) -u $(M9DBUILD)/gba_rom.o 2>/dev/null | grep -q "m8map_"; then \
	     echo "M9-D VERIFY FAILED: gba_rom.o references the M8 probe -- the"; \
	     echo "  FROZEN M4 adapter has been edited."; \
	     exit 1; \
	 fi; \
	 echo "gba_rom.o is untouched -- no m8map_* reference (M4-M9 stay frozen)"
	@echo "== M9-D SPECIFIC: THE M9 BASELINE IS UNTOUCHED =="
	@if [ -f $(M9BIN) ] && [ -f $(M9DBIN) ]; then \
	     if cmp -s $(M9BIN) $(M9DBIN); then \
	         echo "M9-D VERIFY FAILED: $(M9DBIN) is identical to $(M9BIN)."; \
	         echo "  M9-D would be the M9 image under another name."; \
	         exit 1; \
	     fi; \
	 fi; \
	 echo "M9-D is a distinct image; build/m9 is written by no M9-D rule"

# ---- hardware smoke-test delivery ------------------------------------------
#
# THE ONLY M9-D TARGET THAT TOUCHES HARDWARE. Everything above is offline.
#
# THERE IS DELIBERATELY NO m9d-upload-rom. M9-D reuses M4's TRANSPORT with M8's
# CARTRIDGE and M3's BIOS, exactly as M9 does. Both must ALREADY be on the
# console:
#     make m3-upload-bios PS5_IP=<addr> BIOS=<gba_bios.bin>
#     make m4-upload-rom  PS5_IP=<addr> ROM=mgba/cinema/gba/irq/keyirq/test.gba
#
# THEN RELAUNCH THE HOST GAME, AND RUN M9-D ONCE PER LAUNCH: plat_video_init()
# reserves 16 MB of direct memory and DELIBERATELY NEVER RELEASES IT, so a
# second run in the same session fails with PLAT_VID_EALLOC (-24).
M9D_LUA_TEMPLATE := lua/m9diag.lua.in
M9D_LUA_OUT      := $(M9DBUILD)/$(M9DTARGET).lua

$(M9D_LUA_OUT): $(M9D_LUA_TEMPLATE) tools/mklua.py $(M9DBIN) $(M9DELF)
	@$(PYTHON) tools/mklua.py $(M9D_LUA_TEMPLATE) $(M9DBIN) $@ $(M9DELF)

m9d-lua: m9d-check $(M9D_LUA_OUT)

# m9d-verify is a PREREQUISITE, not a suggestion -- same rule as m1-send
# through m9-send. Nothing is transmitted unless every offline check passes.
m9d-send: $(M9D_LUA_OUT) $(M9DBIN) m9d-verify
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m9d-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M9D_LUA_OUT) $(M9DBIN)

m9d-clean:
	rm -rf $(M9DBUILD)

.PHONY: m9d m9d-objs m9d-check m9d-link m9d-size m9d-verify m9d-lua m9d-send \
        m9d-clean

# =============================================================================
# M10 -- SYNCHRONISED GBA AUDIO
# =============================================================================
#
# THIS BLOCK IS PURELY ADDITIVE. It defines new variables and new targets and
# CHANGES NOTHING ABOVE IT. M0-M9 and M9-D build, link, verify and send exactly
# as they did before this block existed -- which is the property that lets the
# M9-E hardware REALTIME result stand as evidence rather than being re-litigated.
#
# WHAT M10 ADDS TO THE M9 IMAGE: exactly TWO translation units.
#
#   runtime/audio.o        the PS5 sceAudioOut backend
#   adapters/gba/gba_audio.o   65536 -> 48000 Hz, exact integer 512:375
#
# gpsp/** IS NOT MODIFIED AND NOT RECOMPILED DIFFERENTLY. gpsp_sound.o is
# already in M9's object list (see M9_GPSP_OBJS above) and render_gbc_sound()
# has been running since M5 -- what was missing was a CONSUMER. M10 is a DRAIN,
# not a port, and the M10 gpSP flags below are byte-identical to M9's.
#
# ---- THE ONE GATE THAT CHANGES POLARITY, AND WHY THAT IS SAFE --------------
#
# m7-verify, m8-verify, m9-verify and m9d-verify all FAIL if the string
# `sceAudioOut` appears in the linked image. THOSE FOUR GATES ARE UNTOUCHED and
# keep their full force.
#
# M10 could not have satisfied them, so it does what M6 -> M7 did for
# sceVideoOut: the PROHIBITION becomes a STRICT ALLOWLIST, for M10 ONLY. The
# reason M7/M8/M9 keep passing is STRUCTURAL, not a relaxation -- audio lives in
# its own translation unit that appears in NO other target's object list, so no
# earlier image gains a single byte of .rodata. See runtime/audio.h.

M10TARGET := m10gpsp
M10BUILD  := build/m10
M10ELF    := $(M10BUILD)/$(M10TARGET).elf
M10BIN    := $(M10BUILD)/$(M10TARGET).bin

# ---------------------------------------------------------------- includes --
#
# M9's include set with -Iapps/m10gpsp substituted. -Iadapters/gba is what lets
# BOTH apps/m10gpsp/main.c and adapters/gba/gba_audio.c find gba_audio.h -- the
# header that is deliberately typedef-free so it can be included from both sides
# of the gpSP/runtime type boundary.
M10_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m10gpsp -Iadapters/gba

M10FLAGS = $(BASEFLAGS) $(M10_INCLUDES) -Wall -Wno-unused-function

# IDENTICAL TO M9's. M10 adds NO new compile flag and changes NO existing one --
# in particular there is no -DM10_* audio tuning knob. The stage is chosen at
# RUNTIME through ext->frame_count, so one binary serves all four stages and a
# fixture still cannot change the build it measures.
M10_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M10_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M10LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
             -Wl,--build-id=none -Wl,-z,norelro \
             -Wl,--orphan-handling=error \
             -Wl,-Map=$(M10BUILD)/$(M10TARGET).map

# ----------------------------------------------------------------- objects --
#
# EXACTLY M9's object set, with m10main.o substituted for m9main.o and THREE
# objects added. Nothing is removed. The three additions are the entire delta
# between the M9-E image and the M10 image, which is what keeps the size and
# timing numbers directly comparable with the frozen M9 ones.
#
# gba_m10map.o IS THE THIRD, AND IT IS M10-ONLY BY CONSTRUCTION. It appears in
# NO other target's object list -- M8OBJS, M9OBJS and M9DOBJS are untouched --
# so M8, M9 and M9-D gain not one byte from it. m10-verify proves that by
# asserting no m10map_ symbol resolves in any of those three images, and
# separately asserts that gba_m8map.c/.h are unmodified. The two probes are
# INDEPENDENT: gba_m10map.o must carry no m8map_ relocation either, so a defect
# in one cannot mask the other.
M10_OWN_OBJS = $(M10BUILD)/m10main.o \
               $(M10BUILD)/shim.o \
               $(M10BUILD)/gba_fixture.o \
               $(M10BUILD)/gba_filestream.o \
               $(M10BUILD)/gba_compat.o \
               $(M10BUILD)/gba_probe.o \
               $(M10BUILD)/gba_bios.o \
               $(M10BUILD)/gba_rom.o \
               $(M10BUILD)/gba_m8map.o \
               $(M10BUILD)/gba_m10map.o \
               $(M10BUILD)/gba_exec.o \
               $(M10BUILD)/gba_present.o \
               $(M10BUILD)/gba_input.o \
               $(M10BUILD)/gba_audio.o \
               $(M10BUILD)/platform.o \
               $(M10BUILD)/audio.o

M10_GPSP_OBJS = $(M10BUILD)/gpsp_cpu.o $(M10BUILD)/gpsp_main.o \
                $(M10BUILD)/gpsp_gba_memory.o $(M10BUILD)/gpsp_video.o \
                $(M10BUILD)/gpsp_sound.o $(M10BUILD)/gpsp_serial.o \
                $(M10BUILD)/gpsp_serial_proto.o $(M10BUILD)/gpsp_rfu.o \
                $(M10BUILD)/gpsp_gbp.o $(M10BUILD)/gpsp_cheats.o \
                $(M10BUILD)/gpsp_savestate.o $(M10BUILD)/gpsp_input.o

M10OBJS = $(M10_OWN_OBJS) $(M10_GPSP_OBJS)

# EXCLUDED, unchanged from M1-M9 and none of these may ever appear above:
#   cpu_threaded.c  memmap.c  gba_cc_lut.c  bios_data.S
#   libretro/libretro.c  libretro/libretro-common/*.c
#   x86/**  arm/**  mips/**  3ds/**  bios/**  tests/**  tools/**  jni/**
#   runtime/gfx.c
#
# libretro/libretro.c MATTERS MORE AT M10 THAN ANYWHERE EARLIER. It is the ONLY
# other caller of sound_read_samples() in the tree (libretro.c:437), AND it is
# the only code that can change gpSP's sample rate away from 65536 Hz
# (gpsp_apply_sound_rate(), libretro.c:66-87). Linking it would both duplicate
# the drain and make the 512:375 ratio a runtime variable instead of a constant.
# m10-verify proves it is absent.

$(M10BUILD):
	mkdir -p $(M10BUILD)

# ---- offline structural check ----------------------------------------------
#
# Same C structural checker M8/M9 use, pointed at the M10 fixture AND at the two
# new translation units. It is deliberately NOT pointed at lua/m10.lua.in, for
# m9-check's reason: Lua comments start with --, which the tool does not know,
# so every apostrophe in a Lua comment would lex as an unterminated char literal
# and the check would FAIL ON CORRECT INPUT.
m10-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m10gpsp/main.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_audio.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_m10map.c
	@$(PYTHON) tools/m8_syntax_check.py runtime/audio.c

# ---- THE OFFLINE DECIMATOR HARNESS -----------------------------------------
#
# Builds and runs tools/gba_audio_equiv.c with the HOST compiler, NOT the PS5
# toolchain. It #includes adapters/gba/gba_audio.c verbatim, so it proves the
# SHIPPING converter -- not a paraphrase of it -- satisfies all seven properties
# (exact 375-per-512 ratio, bounded accumulator, zero long-term drift over
# 4,194,304 frames, group size only ever 1 or 2, L/R order preserved through the
# grain copy, an exact hand-computed vector, and produced == consumed + fill).
#
# RUN THIS BEFORE SPENDING ANY CONSOLE TIME. Every property it checks is one
# that would otherwise be diagnosed by ear, on hardware, from a relaunch.
HOSTCC ?= cc

# -I gpsp IS REQUIRED AND IS NOT A gpSP DEPENDENCY.
#
# gba_audio.c opens with #include "common.h". The harness pre-defines that
# header's own guard (COMMON_H, gpsp/common.h:20-21, whose #endif closes at end
# of file) BEFORE including the production .c, so the body -- and every header
# it chains to -- expands to NOTHING. But the preprocessor must still FIND the
# file before it can skip it, and tools/ is not on any gpSP search path. Without
# this flag the harness dies at "common.h: No such file or directory" while the
# production object compiles fine, purely because the production object gets
# $(GPSP_INCLUDES) (Makefile:286-287) and the host command did not.
#
# -Igpsp/libretro/libretro-common/include is DELIBERATELY NOT ADDED. Production
# needs it for headers reached THROUGH common.h's body; here that body never
# expands, so the path can never be consulted. One flag is the true minimum.
#
# This changes the BUILD RULE ONLY. adapters/gba/gba_audio.c is not modified to
# suit the harness, nothing is copied out of gpsp/, and no test-only #ifdef
# exists in the production source. gba_audio_equiv stays absent from every
# object list -- $(M10OBJS) does not contain it and no payload rule builds it.
m10-equiv: | $(M10BUILD)
	@$(HOSTCC) -std=c11 -O2 -Wall -Wextra -I adapters/gba -I gpsp \
	    -o $(M10BUILD)/gba_audio_equiv tools/gba_audio_equiv.c
	@$(M10BUILD)/gba_audio_equiv

# ---- LUAport-owned objects -------------------------------------------------

$(M10BUILD)/m10main.o: apps/m10gpsp/main.c adapters/gba/gba_probe.h \
                       adapters/gba/gba_bios.h adapters/gba/gba_rom.h \
                       adapters/gba/gba_exec.h adapters/gba/gba_present.h \
                       adapters/gba/gba_input.h adapters/gba/gba_m8map.h \
                       adapters/gba/gba_m10map.h \
                       adapters/gba/gba_audio.h runtime/audio.h \
                       $(HEADERS) | $(M10BUILD)
	$(CC) $(M10FLAGS) -c $< -o $@

$(M10BUILD)/shim.o: runtime/shim.c $(HEADERS) | $(M10BUILD)
	$(CC) $(M10FLAGS) -c $< -o $@

# M0's PS5 VideoOut AND PAD layer, COMPILED FROM UNMODIFIED SOURCE. M10 does not
# edit runtime/platform.c -- THE AUDIO BACKEND IS A SEPARATE TRANSLATION UNIT
# PRECISELY SO THAT IT DOES NOT HAVE TO. Adding sceAudioOut strings to
# platform.c would push them into the M7, M8, M9 and M9-D images (every target
# links platform.o whole, and no target passes --gc-sections) and would fail all
# four of those frozen verifiers.
$(M10BUILD)/platform.o: runtime/platform.c $(HEADERS) | $(M10BUILD)
	$(CC) $(M10FLAGS) -c $< -o $@

# M10's PS5 AUDIO backend. NEW, and linked ONLY here.
$(M10BUILD)/audio.o: runtime/audio.c runtime/audio.h $(HEADERS) | $(M10BUILD)
	$(CC) $(M10FLAGS) -c $< -o $@

$(M10BUILD)/gba_present.o: adapters/gba/gba_present.c adapters/gba/gba_present.h \
                           runtime/core.h | $(M10BUILD)
	$(CC) $(M10FLAGS) -c $< -o $@

# M10's GBA->PS5 audio adapter. NEW.
#
# COMPILED WITH THE gpSP FLAGS AND NOTHING ELSE, exactly like gba_input.o and
# gba_exec.o above, and that is load-bearing in BOTH directions:
#
#   IT NEEDS gpSP's FLAGS because it includes gpsp/common.h to reach
#   sound_read_samples(), so it must see the same gpSP configuration every other
#   gpSP object sees.
#
#   IT MUST NOT GET $(M10_INCLUDES), whose -Iruntime would put runtime/core.h
#   and runtime/audio.h on the include path. gpsp/common.h:100 typedefs u64 as
#   `unsigned long long` and runtime/core.h:30 as `unsigned long` -- DISTINCT
#   types -- so a TU that saw both would not compile. `gba_audio.h` is found
#   without any -I because it sits beside the .c file, which is how every other
#   adapter here resolves its own header.
#
# NOTE THE ABSENT PREREQUISITE: runtime/audio.h is deliberately NOT listed,
# because gba_audio.c deliberately does NOT include it. It hand-declares the one
# function it needs -- `extern int plat_audio_submit(const short *)`, whose
# signature mentions only builtins both sides agree on. Listing the header here
# would assert a dependency that must never exist.
$(M10BUILD)/gba_audio.o: adapters/gba/gba_audio.c adapters/gba/gba_audio.h | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

# M8's keypad adapter, compiled from BYTE-IDENTICAL, UNMODIFIED source.
$(M10BUILD)/gba_input.o: adapters/gba/gba_input.c adapters/gba/gba_input.h | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gba_m8map.o: adapters/gba/gba_m8map.c adapters/gba/gba_m8map.h | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

# M10-ONLY. Same flags as every other gpSP-facing adapter -- no new define, no
# new include path, no new library. It is a pure observer of gpSP's cartridge
# globals and does not depend on adapters/gba/gba_m8map.h.
$(M10BUILD)/gba_m10map.o: adapters/gba/gba_m10map.c adapters/gba/gba_m10map.h | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects -----------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, IDENTICAL to M9's.
# gpsp/** is never modified and its own Makefile is never invoked. M10 needed
# ZERO changes here: sound.c already exports sound_read_samples() and
# sound_flush_ring(), and both were already being compiled into M9.

$(M10BUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M10BUILD)
	$(CXX) $(M10_GPSPCXXFLAGS) -c $< -o $@

$(M10BUILD)/gpsp_video.o: gpsp/video.cc | $(M10BUILD)
	$(CXX) $(M10_GPSPCXXFLAGS) -c $< -o $@

$(M10BUILD)/gpsp_main.o: gpsp/main.c | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gpsp_sound.o: gpsp/sound.c | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gpsp_serial.o: gpsp/serial.c | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

$(M10BUILD)/gpsp_input.o: gpsp/input.c | $(M10BUILD)
	$(CC) $(M10_GPSPFLAGS) -c $< -o $@

m10-objs: $(M10OBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- exactly as M1-M9.
# Every object is named on the link line and is included in its ENTIRETY.
$(M10ELF): $(M10OBJS) runtime/linker.ld | $(M10BUILD)
	$(CC) $(BASEFLAGS) $(M10LDFLAGS) -o $@ $(M10OBJS)

m10-link: $(M10OBJS) | $(M10BUILD)
	@$(CC) $(BASEFLAGS) $(M10LDFLAGS) -o $(M10ELF) $(M10OBJS) \
	     > $(M10BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(M10BUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M10BUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M10 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M10 LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M10 LINK OK: linker exit 0, no undefined references"

# --arena 4194304, IDENTICAL TO M4's through M9's.
$(M10BIN): $(M10ELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M10ELF) $@ \
	   --map $(M10BUILD)/$(M10TARGET).map --budget $(JIT_LIMIT) --arena 4194304

m10: $(M10BIN)

# THE TWO HARD CEILINGS. sizereport.py enforces the JIT budget ($(JIT_LIMIT) =
# 524,288) and tools/check_forbidden.py enforces the 2,097,152-byte .bss
# tripwire. M10 adds about 42 KB of .bss -- the 32,768-byte ring, the 8,192-byte
# pull buffer and the 1,024-byte grain, all STATIC because M9's arena tripwire
# terminates a run on ONE byte of steady-state allocation.
#
# IF EITHER CEILING IS EXCEEDED, STOP AND REPORT IT. Do not shrink unrelated
# code to make room: the size of the M10 image is one of the results.
m10-size: $(M10ELF) $(M10BIN)
	@$(PYTHON) tools/sizereport.py $(M10ELF) $(M10BIN) \
	   --map $(M10BUILD)/$(M10TARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification ----------------------------------------------------------
#
# EVERY M9 GATE IS KEPT AND ONLY ONE CHANGES POLARITY. All five tools run
# unmodified with the same arguments; the VideoOut allowlist, the PAD allowlist,
# the sceGnm prohibition, the marker-based rodata carve, the clock gate, the
# pacing gate, the gfx.c exclusion and ALL FOUR adapter isolation checks carry
# over VERBATIM.
#
# THE ONE CHANGE: `sceAudioOut` was FORBIDDEN at M7/M8/M9; at M10 it becomes a
# STRICT FOUR-NAME ALLOWLIST plus the module. That is the same transition
# sceVideoOut made at M6 -> M7, and M7/M8/M9/M9-D keep their prohibition.
#
# ---- WHAT M10 ADDS, AND WHY EACH ONE IS NECESSARY ----
#
#   THE DRAIN EXISTS, AND IT IS IN THE RIGHT PLACE. gba_audio.o MUST reference
#   sound_read_samples, and m10main.o MUST NOT. Both halves matter: the first
#   proves the adapter really pulls from gpSP rather than synthesising
#   something, and the second proves the fixture goes through the adapter
#   instead of reaching around it -- which is exactly how a "working" audio path
#   can be one that never touches the emulator.
#
#   THE ADAPTER NEVER TOUCHES HARDWARE. gba_audio.o must carry NO sceAudioOut*
#   relocation. Rate conversion and console I/O are different jobs, and the
#   layering is what lets tools/gba_audio_equiv.c compile the adapter on a host.
#
#   NEITHER NEW OBJECT ALLOCATES. No malloc/free from gba_audio.o or audio.o.
#   M9's arena tripwire ends a run on ONE byte of steady-state growth, and M10
#   keeps that gate -- so a heap call on the audio path would not be a leak, it
#   would be a terminated measurement.
#
#   gpSP DID NOT LEARN ABOUT THE PS5. No gpsp_*.o may reference plat_audio_* or
#   sceAudioOut*. This is the link-time proof of the claim that M10 required
#   ZERO changes to gpsp/**.
M10_VIDEOOUT_ALLOW = sceVideoOutOpen sceVideoOutClose sceVideoOutRegisterBuffers \
                     sceVideoOutSubmitFlip sceVideoOutSetFlipRate \
                     sceVideoOutAddFlipEvent

M10_PAD_ALLOW = scePadInit scePadGetHandle scePadRead

# THE FOUR NAMES runtime/audio.c RESOLVES, AND NOTHING ELSE. Deliberately
# minimal: no sceAudioOutSetVolume, no sceAudioOutGetPortState. A name outside
# this list means the backend grew a capability nobody reviewed.
M10_AUDIO_ALLOW = sceAudioOutInit sceAudioOutOpen sceAudioOutOutput \
                  sceAudioOutClose

m10-verify: $(M10ELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M10ELF)
	@echo "== relocations =="
	@$(PYTHON) tools/verify_reloc.py $(M10ELF) | tail -12
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M10OBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M10OBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M10ELF) $(M10OBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M10ELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M10 VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== PRESENTATION ALLOWLIST (carried from M7/M8/M9) =="
	@$(OBJCOPY) -O binary --only-section=.text $(M10ELF) $(M10BUILD)/text.bin
	@tx=$$($(NM) $(M10ELF) | awk '$$NF == "__text_start"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 ra=$$($(NM) $(M10ELF) | awk '$$NF == "__rodata_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 rz=$$($(NM) $(M10ELF) | awk '$$NF == "__rodata_end"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 if [ -z "$$tx" ] || [ -z "$$ra" ] || [ -z "$$rz" ]; then \
	     echo "M10 VERIFY FAILED: linker marker symbol missing -- need"; \
	     echo "  __text_start, __rodata_start and __rodata_end in $(M10ELF)."; \
	     exit 1; \
	 fi; \
	 t=$$((0x$$tx)); a=$$((0x$$ra)); z=$$((0x$$rz)); n=$$((z - a)); \
	 if [ "$$t" -ne 0 ]; then \
	     echo "M10 VERIFY FAILED: .text does not start at vaddr 0 (__text_start ="; \
	     echo "  $$t), so a symbol value is no longer a file offset in the"; \
	     echo "  extracted binary and this carve would read the wrong bytes."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M10 VERIFY FAILED: empty rodata window -- __rodata_start=$$a"; \
	     echo "  __rodata_end=$$z gives $$n bytes."; \
	     exit 1; \
	 fi; \
	 have=$$(wc -c < $(M10BUILD)/text.bin | tr -d " "); \
	 if [ "$$have" -lt "$$z" ]; then \
	     echo "M10 VERIFY FAILED: .text extraction is $$have bytes but the rodata"; \
	     echo "  window ends at $$z -- the extraction is short or .text is absent."; \
	     exit 1; \
	 fi; \
	 dd if=$(M10BUILD)/text.bin of=$(M10BUILD)/rodata.bin bs=1 skip=$$a count=$$n \
	    2>/dev/null || { \
	     echo "M10 VERIFY FAILED: dd could not carve the rodata window."; exit 1; \
	 }; \
	 if [ ! -s $(M10BUILD)/rodata.bin ]; then \
	     echo "M10 VERIFY FAILED: rodata extraction produced 0 bytes -- every check"; \
	     echo "  below would pass vacuously. Fix the extraction, not the checks."; \
	     exit 1; \
	 fi; \
	 got=$$(wc -c < $(M10BUILD)/rodata.bin | tr -d " "); \
	 if [ "$$got" -ne "$$n" ]; then \
	     echo "M10 VERIFY FAILED: rodata.bin is $$got bytes, expected $$n ="; \
	     echo "  __rodata_end - __rodata_start. Extraction is not trustworthy."; \
	     exit 1; \
	 fi; \
	 echo "rodata carved from .text by linker markers: [$$a,$$z) = $$n bytes"
	@for s in $(M10_VIDEOOUT_ALLOW) libSceVideoOut.sprx; do \
	     if ! grep -aq "$$s" $(M10BUILD)/rodata.bin; then \
	         echo "M10 VERIFY FAILED: required presentation name '$$s' is NOT in"; \
	         echo "  the image."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 6 VideoOut entry points + libSceVideoOut.sprx are present"
	@allow=$$(echo $(M10_VIDEOOUT_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "sceVideoOut[A-Za-z0-9_]*" $(M10BUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M10 VERIFY FAILED: an UNEXPECTED sceVideoOut* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no sceVideoOut* name outside the six-symbol allowlist"
	@echo "== THE GRAPHICS-PIPELINE PROHIBITION IS UNCHANGED =="
	@if grep -aqE "sceGnm" $(M10BUILD)/rodata.bin; then \
	     echo "M10 VERIFY FAILED: a graphics-pipeline name reached the image:"; \
	     grep -aoE "sceGnm[A-Za-z0-9_]*" $(M10BUILD)/rodata.bin \
	        | sort -u | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no sceGnm* name is present (NO GNM -- unchanged from M6-M9)"
	@echo "== PAD ALLOWLIST (carried from M8/M9) =="
	@for s in $(M10_PAD_ALLOW) libScePad.sprx; do \
	     if ! grep -aq "$$s" $(M10BUILD)/rodata.bin; then \
	         echo "M10 VERIFY FAILED: required pad name '$$s' is NOT in the image."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 3 scePad entry points + libScePad.sprx are present"
	@allow=$$(echo $(M10_PAD_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "scePad[A-Za-z0-9_]*" $(M10BUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M10 VERIFY FAILED: an UNEXPECTED scePad* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no scePad* name outside the three-symbol allowlist"
	@echo "== AUDIO ALLOWLIST (M10-SPECIFIC -- THIS IS THE ONE GATE THAT FLIPS) =="
	@echo "   sceAudioOut was FORBIDDEN at M7/M8/M9/M9-D and STILL IS there."
	@echo "   Those four verifiers are untouched; audio is a separate object"
	@echo "   that appears in NO other target's link line."
	@for s in $(M10_AUDIO_ALLOW) libSceAudioOut.sprx; do \
	     if ! grep -aq "$$s" $(M10BUILD)/rodata.bin; then \
	         echo "M10 VERIFY FAILED: required audio name '$$s' is NOT in the"; \
	         echo "  image -- the audio backend was not linked, and every M10"; \
	         echo "  stage including the init-only one requires it."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 4 sceAudioOut entry points + libSceAudioOut.sprx are present"
	@allow=$$(echo $(M10_AUDIO_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "sceAudioOut[A-Za-z0-9_]*" $(M10BUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M10 VERIFY FAILED: an UNEXPECTED sceAudioOut* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     echo "  The allowlist is deliberately minimal -- a new name means the"; \
	     echo "  backend grew a capability that was never reviewed."; \
	     exit 1; \
	 fi; \
	 echo "no sceAudioOut* name outside the four-symbol allowlist"
	@echo "== THE CLOCK IS REALLY INITIALISED (carried from M9) =="
	@miss=""; \
	 for s in plat_time_init plat_time_us; do \
	     $(NM) -u $(M10BUILD)/m10main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M10 VERIFY FAILED: m10main.o does NOT reference:$$miss"; \
	     echo "  Every span, rate, deadline count AND THE FILL SLOPE would be"; \
	     echo "  zero, and the run would still look orderly."; \
	     exit 1; \
	 fi; \
	 echo "m10main.o references plat_time_init AND plat_time_us"
	@if ! $(NM) -u $(M10BUILD)/m10main.o 2>/dev/null | grep -q "plat_video_is_vsync_paced"; then \
	     echo "M10 VERIFY FAILED: m10main.o does NOT reference"; \
	     echo "  plat_video_is_vsync_paced. At M10 this is worse than at M9: the"; \
	     echo "  PACING SOURCE is what makes the audio ring's fill slope mean"; \
	     echo "  anything, because the whole +170.8 frames/s prediction comes"; \
	     echo "  from the 59.94 vs 59.7275 Hz mismatch."; \
	     exit 1; \
	 fi; \
	 echo "m10main.o reports the pacing source (plat_video_is_vsync_paced)"
	@echo "== THE RESULT SCREEN IS M10-OWNED, gfx.c IS NOT LINKED =="
	@bad=$$(echo "$(M10OBJS)" | tr ' ' '\n' | grep -E "/gfx\.o$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M10 VERIFY FAILED: a gfx object is in the M10 link line:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no gfx object appears in the M10 object set"
	@bad=$$($(NM) -u $(M10BUILD)/m10main.o 2>/dev/null \
	          | grep -E "(draw_char|draw_str|draw_centered|draw_hline|ui_fill|blit_ui|clear_fb|fill_rect|draw_border)" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M10 VERIFY FAILED: m10main.o reaches runtime/gfx.c:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "m10main.o carries NO runtime/gfx.c relocation"
	@echo "== THE MEASURED PATH IS REALLY DRIVEN (carried from M9) =="
	@miss=""; \
	 for s in plat_pad_init plat_pad_read plat_pad_shutdown \
	          plat_video_init plat_video_present plat_video_get_framebuffer \
	          m5_exec_run gba_present_blit; do \
	     $(NM) -u $(M10BUILD)/m10main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M10 VERIFY FAILED: m10main.o does NOT reference:$$miss"; \
	     echo "  One of the measured spans has no call site, so its timing would"; \
	     echo "  be a constant zero rather than a measurement."; \
	     exit 1; \
	 fi; \
	 echo "m10main.o drives input, emulation, conversion and presentation"
	@miss=""; \
	 for s in m8_input_selftest m8_input_from_pad m8_input_to_reg_p1 \
	          m8_input_apply m8_input_irq_edge m8_input_read_reg_if; do \
	     $(NM) -u $(M10BUILD)/m10main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M10 VERIFY FAILED: m10main.o does NOT reference:$$miss"; \
	     echo "  M8's proven input chain is not being exercised, so the baseline"; \
	     echo "  would describe a DIFFERENT and cheaper workload than M9-E's."; \
	     exit 1; \
	 fi; \
	 echo "m10main.o carries M8's keypad chain through the whole run"
	@echo "== THE AUDIO PATH IS REALLY DRIVEN (M10-specific) =="
	@miss=""; \
	 for s in plat_audio_init plat_audio_shutdown; do \
	     $(NM) -u $(M10BUILD)/m10main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M10 VERIFY FAILED: m10main.o does NOT reference:$$miss"; \
	     echo "  The port would never open, or never close."; \
	     exit 1; \
	 fi; \
	 echo "m10main.o opens AND closes the audio port"
	@miss=""; \
	 for s in gba_audio_reset gba_audio_pull gba_audio_tone gba_audio_resample \
	          gba_audio_drain gba_audio_stats gba_audio_is_silent; do \
	     $(NM) -u $(M10BUILD)/m10main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M10 VERIFY FAILED: m10main.o does NOT reference:$$miss"; \
	     echo "  One of the audio spans has no call site."; \
	     exit 1; \
	 fi; \
	 echo "m10main.o drives the full gba_audio_* surface"
	@echo "== THE DRAIN EXISTS AND IS IN THE RIGHT PLACE (M10-specific) =="
	@if ! $(NM) -u $(M10BUILD)/gba_audio.o 2>/dev/null | grep -q "sound_read_samples"; then \
	     echo "M10 VERIFY FAILED: gba_audio.o does NOT reference"; \
	     echo "  sound_read_samples -- the adapter is not draining gpSP at all,"; \
	     echo "  so any sound it produced would not be the emulator's."; \
	     exit 1; \
	 fi; \
	 echo "gba_audio.o references gpSP's sound_read_samples (the drain is real)"
	@if ! $(NM) -u $(M10BUILD)/gba_audio.o 2>/dev/null | grep -q "sound_flush_ring"; then \
	     echo "M10 VERIFY FAILED: gba_audio.o does NOT reference"; \
	     echo "  sound_flush_ring. Every milestone up to M9 ran render_gbc_sound()"; \
	     echo "  with NO consumer, so gpSP's 65536-entry mix bus has been"; \
	     echo "  re-accumulating. Without this flush the first samples M10 reports"; \
	     echo "  are historical residue rather than this run's output."; \
	     exit 1; \
	 fi; \
	 echo "gba_audio.o flushes gpSP's mix bus before the measured run"
	@if $(NM) -u $(M10BUILD)/m10main.o 2>/dev/null | grep -q "sound_read_samples"; then \
	     echo "M10 VERIFY FAILED: m10main.o references sound_read_samples"; \
	     echo "  DIRECTLY. The fixture must go THROUGH the adapter, not around"; \
	     echo "  it -- a second drain would race the first for the same samples"; \
	     echo "  and the span F1 timing would describe neither."; \
	     exit 1; \
	 fi; \
	 echo "m10main.o does NOT call sound_read_samples (it goes through gba_audio.o)"
	@echo "== ADAPTER ISOLATION (all four carried from M8/M9, plus audio) =="
	@bad=$$($(NM) -u $(M10BUILD)/gba_audio.o 2>/dev/null \
	          | grep -E "sceAudioOut|sceVideoOut|scePad" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M10 VERIFY FAILED: the audio ADAPTER reaches the console directly:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     echo "  Rate conversion and console I/O are different jobs. This"; \
	     echo "  separation is also what lets tools/gba_audio_equiv.c compile the"; \
	     echo "  shipping adapter on a host machine."; \
	     exit 1; \
	 fi; \
	 echo "gba_audio.o carries NO sceAudioOut*/sceVideoOut*/scePad* relocation"
	@bad=$$($(NM) -u $(M10BUILD)/audio.o 2>/dev/null \
	          | grep -E "sound_read_samples|sound_flush_ring|gba_audio_|m5_exec_" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M10 VERIFY FAILED: the audio BACKEND learned about the emulator:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     echo "  runtime/audio.c must know about exactly one thing: handing a"; \
	     echo "  finished grain to the console."; \
	     exit 1; \
	 fi; \
	 echo "audio.o carries NO gpSP/gba_audio_* relocation (no emulator concepts)"
	@for o in gba_audio audio; do \
	     bad=$$($(NM) -u $(M10BUILD)/$$o.o 2>/dev/null \
	              | grep -E "(^| )(malloc|free|calloc|realloc)$$" || true); \
	     if [ -n "$$bad" ]; then \
	         echo "M10 VERIFY FAILED: $$o.o allocates:"; \
	         echo "$$bad" | sed "s/^/  /"; \
	         echo "  M9's arena tripwire ends a run on ONE byte of steady-state"; \
	         echo "  growth and M10 keeps it, so a heap call on the audio path is"; \
	         echo "  a TERMINATED MEASUREMENT, not merely untidy. Every M10 buffer"; \
	         echo "  is static by design."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "neither gba_audio.o nor audio.o references malloc/free/calloc/realloc"
	@echo "== gpSP DID NOT LEARN ABOUT THE PS5 (M10-specific) =="
	@bad=""; \
	 for o in $(M10_GPSP_OBJS); do \
	     hit=$$($(NM) -u $$o 2>/dev/null | grep -E "plat_audio_|sceAudioOut" || true); \
	     if [ -n "$$hit" ]; then bad="$$bad $$o"; fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M10 VERIFY FAILED: a gpSP object reaches the PS5 audio layer:"; \
	     echo "$$bad" | tr ' ' '\n' | sed "s/^/  /"; \
	     echo "  M10 is a DRAIN, not a port. gpsp/** must remain unmodified."; \
	     exit 1; \
	 fi; \
	 echo "no gpsp_*.o references plat_audio_* or sceAudioOut* (gpsp/** untouched)"
	@if ! $(NM) -u $(M10BUILD)/gba_input.o 2>/dev/null | grep -q "flag_interrupt"; then \
	     echo "M10 VERIFY FAILED: gba_input.o does NOT reference flag_interrupt."; \
	     exit 1; \
	 fi; \
	 echo "gba_input.o references gpSP's flag_interrupt (the IRQ is really raised)"
	@bad=$$($(NM) -u $(M10BUILD)/gba_input.o 2>/dev/null \
	          | grep -E "plat_pad_|plat_video_|plat_audio_|scePad|sceVideoOut" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M10 VERIFY FAILED: the keypad adapter reaches the platform layer:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "gba_input.o carries NO plat_*/scePad*/sceVideoOut* relocation"
	@pad=$$($(NM) -u $(M10BUILD)/gba_present.o 2>/dev/null \
	          | grep -E "plat_pad_|scePad|plat_audio_|sceAudioOut" || true); \
	 if [ -n "$$pad" ]; then \
	     echo "M10 VERIFY FAILED: the PRESENTATION path learned about input or audio:"; \
	     echo "$$pad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "gba_present.o still carries NO plat_pad_*/scePad*/audio relocation"
	@bad=$$($(NM) -u $(M10BUILD)/gba_m8map.o 2>/dev/null \
	          | grep -E "plat_pad_|plat_video_|scePad|sceVideoOut|m4_rom_|m5_exec_" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M10 VERIFY FAILED: the mapping probe reaches outside gpSP:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "gba_m8map.o carries NO plat_*/scePad*/sceVideoOut*/m4_*/m5_* relocation"
	@if $(NM) -u $(M10BUILD)/gba_rom.o 2>/dev/null | grep -q "m8map_"; then \
	     echo "M10 VERIFY FAILED: gba_rom.o references the M8 probe -- the FROZEN"; \
	     echo "  M4 adapter has been edited."; \
	     exit 1; \
	 fi; \
	 echo "gba_rom.o is untouched -- no m8map_* reference (M4-M9 stay frozen)"

# ---- THE M10-ONLY MAPPING PROBE, FIVE ADDITIVE GATES -----------------------
#
# NOTHING BELOW WEAKENS OR RE-THRESHOLDS AN EXISTING CHECK. Every one of these
# is a new assertion about a new object, in the style Makefile:5361-5374 already
# established for gba_m8map.o.
	@bad=$$($(NM) -u $(M10BUILD)/gba_m10map.o 2>/dev/null \
	          | grep -E "plat_pad_|plat_video_|plat_audio_|scePad|sceVideoOut|sceAudioOut|m4_rom_|m5_exec_" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M10 VERIFY FAILED: the M10 mapping probe reaches outside gpSP:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "gba_m10map.o carries NO plat_*/scePad*/sceVideoOut*/sceAudioOut*/m4_*/m5_* relocation"
	@if $(NM) -u $(M10BUILD)/gba_m10map.o 2>/dev/null | grep -q "m8map_"; then \
	     echo "M10 VERIFY FAILED: gba_m10map.o references the M8 probe. The two"; \
	     echo "  probes must be INDEPENDENT -- a defect in one must not be able"; \
	     echo "  to mask the other, and the A' dispatch relies on that."; \
	     exit 1; \
	 fi; \
	 echo "gba_m10map.o is INDEPENDENT of gba_m8map.o (no m8map_* reference)"
	@miss=""; \
	 for s in memory_map_read gamepak_buffers gamepak_blk_queue gamepak_size bios_rom; do \
	     $(NM) -u $(M10BUILD)/gba_m10map.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M10 VERIFY FAILED: gba_m10map.o does NOT reference:$$miss"; \
	     echo "  A probe that does not read gpSP's real cartridge state, its LRU"; \
	     echo "  queue included, is not measuring the mapping."; \
	     exit 1; \
	 fi; \
	 echo "gba_m10map.o reads gpSP's real mapping state (memory_map_read,"; \
	 echo "  gamepak_buffers, gamepak_blk_queue, gamepak_size, bios_rom)"
	@miss=""; \
	 for s in m10map_probe m10map_report m10map_read m10map_paging_reset \
	          m10map_paging_delta; do \
	     $(NM) -u $(M10BUILD)/m10main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M10 VERIFY FAILED: m10main.o does NOT reference:$$miss"; \
	     echo "  The demand-paged mapping contract is not actually being checked,"; \
	     echo "  or the ROM page-load count is not actually being measured."; \
	     exit 1; \
	 fi; \
	 echo "m10main.o drives the M10 mapping probe AND the queue-based page-load count"
	@bad=""; \
	 for o in $(M8OBJS) $(M9OBJS) $(M9DOBJS); do \
	     case "$$o" in *gba_m10map*) bad="$$bad $$o";; esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M10 VERIFY FAILED: the M10-only mapping probe leaked into an"; \
	     echo "  earlier milestone's object list:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "gba_m10map.o is in NO other target -- M8/M9/M9-D link set unchanged"
# THE FREEZE PIN. gba_m8map.{c,h} are M8 hardware evidence that M8, M9, M9-D and
# M10's fully-resident path all still take their verdict from. If either file
# moves by one byte, four green hardware results stop describing the shipped
# code. Content hashes, not timestamps, because a touch is not a change and an
# edit-then-revert is not either.
	@exp_c=86fdb9c175241621ebf8381c71f47ae79a3c80518dbf16092e8048cf9ee52f42; \
	 exp_h=7ad4a335a4f57cc7dbd08fa838e41d18bffed8e29dcb14209f64125b433ce672; \
	 got_c=$$($(PYTHON) -c "import hashlib;print(hashlib.sha256(open('adapters/gba/gba_m8map.c','rb').read()).hexdigest())"); \
	 got_h=$$($(PYTHON) -c "import hashlib;print(hashlib.sha256(open('adapters/gba/gba_m8map.h','rb').read()).hexdigest())"); \
	 if [ "$$got_c" != "$$exp_c" ] || [ "$$got_h" != "$$exp_h" ]; then \
	     echo "M10 VERIFY FAILED: adapters/gba/gba_m8map.{c,h} HAVE CHANGED."; \
	     echo "  gba_m8map.c want $$exp_c"; \
	     echo "  gba_m8map.c got  $$got_c"; \
	     echo "  gba_m8map.h want $$exp_h"; \
	     echo "  gba_m8map.h got  $$got_h"; \
	     echo "  These files are FROZEN M8 evidence. If the only difference is"; \
	     echo "  line endings from a fresh checkout, re-pin these two constants;"; \
	     echo "  otherwise the M8/M9/M9-D hardware results no longer apply."; \
	     exit 1; \
	 fi; \
	 echo "gba_m8map.c/.h are BYTE-IDENTICAL to the frozen M8 evidence"

# ---- M10D's THREE ADDITIVE GATES -------------------------------------------
#
# NOTHING BELOW WEAKENS OR RE-THRESHOLDS AN EXISTING CHECK. Each is a new
# assertion about M10D's own additions.
#
# GATE 1 IS A DRIFT PIN, AND IT EXISTS BECAUSE THE DRIFT ALREADY HAPPENED.
# lua/m10.lua.in documented STAGE 3 as 45 s while M10_D_SECONDS was 40 -- a
# comment nothing depended on, which is exactly why nobody noticed and exactly
# how an operator ends up reporting a 45-second result from a 40-second run.
# The two are now compared at BUILD TIME so the same drift cannot come back.
	@c=$$(sed -n 's/^#define M10_D_SECONDS[[:space:]]*\([0-9][0-9]*\)u.*/\1/p' \
	        apps/m10gpsp/main.c | head -1); \
	 l=$$(sed -n 's/^--   STAGE 3  M10D  SUSTAINED  \([0-9][0-9]*\) s.*/\1/p' \
	        lua/m10.lua.in | head -1); \
	 if [ -z "$$c" ] || [ -z "$$l" ]; then \
	     echo "M10 VERIFY FAILED: could not extract the M10D duration from both"; \
	     echo "  apps/m10gpsp/main.c (M10_D_SECONDS, got '$$c') and"; \
	     echo "  lua/m10.lua.in (the STAGE 3 line, got '$$l')."; \
	     echo "  A gate that cannot read either side would pass vacuously."; \
	     exit 1; \
	 fi; \
	 if [ "$$c" != "$$l" ]; then \
	     echo "M10 VERIFY FAILED: THE M10D DURATION HAS DRIFTED."; \
	     echo "  apps/m10gpsp/main.c M10_D_SECONDS = $$c"; \
	     echo "  lua/m10.lua.in STAGE 3 line       = $$l"; \
	     echo "  The C constant is what RUNS. Fix the comment, never the"; \
	     echo "  constant, unless the duration change is intended."; \
	     exit 1; \
	 fi; \
	 echo "M10D duration agrees in C and Lua ($$c s) -- the 45-vs-40 s drift is pinned"
# GATE 2: THE UNDERRUN GATE REALLY LANDED IN THE IMAGE. -281 is the one
# pass/fail change M10D makes, and a mask edited in only one of the two places
# would produce a run that PASSES on screen and FAILS in the decoder, or the
# reverse. Both required masks are compared against each other.
	@c=$$(sed -n 's/^local L_REQUIRED = 0x\([0-9A-Fa-f]*\).*/\1/p' \
	        lua/m10.lua.in | head -1); \
	 if [ -z "$$c" ]; then \
	     echo "M10 VERIFY FAILED: could not read L_REQUIRED from lua/m10.lua.in."; \
	     exit 1; \
	 fi; \
	 if [ "$$c" != "643FF" ]; then \
	     echo "M10 VERIFY FAILED: lua/m10.lua.in L_REQUIRED is 0x$$c, expected"; \
	     echo "  0x643FF = bits 0..9 | 14 AUDIOUP | 17 NOOVERRUN | 18 NOUNDERRUN."; \
	     echo "  If a latch bit was added or removed, update M10_L_REQUIRED in"; \
	     echo "  apps/m10gpsp/main.c AND this constant AND this gate together."; \
	     exit 1; \
	 fi; \
	 if ! grep -q "M10_L_NOUNDERRUN" apps/m10gpsp/main.c; then \
	     echo "M10 VERIFY FAILED: M10_L_NOUNDERRUN is absent from the payload"; \
	     echo "  while the decoder requires bit 18. Underruns would be counted,"; \
	     echo "  printed, and then ignored -- the exact hole M10D closes."; \
	     exit 1; \
	 fi; \
	 echo "the -281 underrun gate is present in BOTH the payload and the decoder"
# GATE 3: M10D's OBSERVERS ARE REALLY COMPILED IN. Every one of them is a
# report-only counter, so a dead-code eliminator removing one would be SILENT
# -- the screen would simply show a confident 0. These strings only exist if
# the rows that print them survived to .rodata.
	@miss=""; \
	 for s in "LATE WITH PAGE" "MAP PROBES" "SUBMISSION A/V DRIFT" \
	          "AUDIO SUBMIT AVG" "AUDIO RING STARVED"; do \
	     grep -aq "$$s" $(M10BUILD)/rodata.bin || miss="$$miss [$$s]"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M10 VERIFY FAILED: an M10D observer string is NOT in the image:"; \
	     echo "  $$miss"; \
	     echo "  Each is a report-only counter, so its absence would show as a"; \
	     echo "  confident 0 on the result screen rather than as an error."; \
	     exit 1; \
	 fi; \
	 echo "all 5 M10D observer strings reached .rodata (late attribution, map"; \
	 echo "  probes, A/V drift, submit worst, the -281 message)"

# ---- the Lua loader --------------------------------------------------------
#
# ONE TEMPLATE, FOUR STAGES. The stage travels in ext.frame_count (+0x10), which
# the M9 payload deliberately left at 0, so no earlier template changes.
#
#     make m10-lua              STAGE 0, M10A INIT ONLY
#     make m10-lua STAGE=1      M10B TONE
#     make m10-lua STAGE=2      M10C GPSP (the fill-slope experiment)
#     make m10-lua STAGE=3      M10D SUSTAINED
#
# tools/mklua.py is NOT modified: it substitutes only @@JIT_SIZE@@ (and rewrites
# the PC_IP line). The STAGE line is rewritten here afterwards, by exactly the
# same single-line-replacement technique mklua.py uses for PC_IP.
#
# m10-lua is PHONY and always regenerates, because STAGE is a command-line
# variable that make cannot see as a file dependency -- a cached .lua from a
# previous STAGE would silently run the wrong stage, which is the one failure
# this target must not have.
STAGE ?= 0

M10_LUA_TEMPLATE := lua/m10.lua.in
M10_LUA_OUT      := $(M10BUILD)/$(M10TARGET).lua

m10-lua: m10-check $(M10BIN) $(M10ELF)
	@case "$(STAGE)" in \
	   0|1|2|3) ;; \
	   *) echo "M10 LUA FAILED: STAGE='$(STAGE)' is not 0, 1, 2 or 3."; \
	      echo "  0=INIT ONLY  1=TONE  2=GPSP  3=SUSTAINED"; \
	      exit 1;; \
	 esac
	@$(PYTHON) tools/mklua.py $(M10_LUA_TEMPLATE) $(M10BIN) $(M10_LUA_OUT) $(M10ELF)
	@sed -i.bak "s/^local STAGE = .*/local STAGE = $(STAGE)/" $(M10_LUA_OUT)
	@rm -f $(M10_LUA_OUT).bak
	@if ! grep -q "^local STAGE = $(STAGE)$$" $(M10_LUA_OUT); then \
	     echo "M10 LUA FAILED: the STAGE line was not rewritten in"; \
	     echo "  $(M10_LUA_OUT) -- it would run STAGE 0 regardless of STAGE=$(STAGE)."; \
	     exit 1; \
	 fi
	@echo "M10 LUA OK: $(M10_LUA_OUT) -- STAGE $(STAGE)"

# ---- m10-send -- HARDWARE DELIVERY, AUTHORISED FOR M10A ---------------------
#
# HARDWARE DELIVERY IS AUTHORISED FOR M10A (STAGE 0) ONLY. The target below is
# the three-line target this block previously described as pending; it is
# modelled on m9-send, with m10-verify as a PREREQUISITE, not a suggestion.
#
#   * RELAUNCH THE HOST GAME BETWEEN EVERY M10 STAGE. plat_video_init()
#     reserves 16 MB of direct memory and DELIBERATELY never releases it, so a
#     second run in one session fails with PLAT_VID_EALLOC (-24). Four stages =
#     four launches.
#   * M10A MUST BE RUN FIRST. It is three seconds and it answers the only
#     question that blocks every other stage: does the sceAudioOut port open at
#     all.
#   * M10A is audio init/open/close ONLY. It emits no samples.
#   * M10B/M10C/M10D ARE NOT AUTHORISED merely because this target exists.
#     STAGE=1/2/3 build and transmit perfectly well -- that is deliberate, the
#     stage selector is untouched -- but running them is an OPERATOR decision
#     that has to be taken explicitly, per stage, after M10A reports PASS.
#     This is an authorisation rule, not a code-enforced one.
#   * M10 reuses M3's BIOS and M8's keypad cartridge. Neither is re-uploaded.
#
# WHY m10-lua IS A PREREQUISITE AND $(M10_LUA_OUT) IS NOT. m10-lua is PHONY and
# STAGE is a command-line variable, so a file prerequisite on $(M10_LUA_OUT)
# would be satisfied by a .lua left behind by a PREVIOUS stage and would ship
# the wrong stage silently. Depending on the PHONY target forces regeneration
# for the STAGE actually requested. m10-lua in turn depends on m10-check,
# $(M10BIN) and $(M10ELF), so the binary is built and the payload regenerated
# before send.py runs; restating $(M10BIN) here would be redundant. STAGE
# validation is m10-lua's -- it is not duplicated here.
m10-send: m10-verify m10-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m10-send PS5_IP=<console address> STAGE=<0|1|2|3>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M10_LUA_OUT) $(M10BIN)

m10-clean:
	rm -rf $(M10BUILD)

.PHONY: m10 m10-objs m10-check m10-equiv m10-link m10-size m10-verify m10-lua \
        m10-send m10-clean

# =============================================================================
# M11 -- GBA SAVE-MEMORY OBSERVER
# =============================================================================
#
# PURELY ADDITIVE. Not one M0-M10 variable, rule, object list or target is
# modified by this block. M11 introduces ONE new translation unit
# (adapters/gba/gba_m11save.o) and REMOVES TWO from the M10 set
# (adapters/gba/gba_audio.o and runtime/audio.o, which M11 does not link).
#
# M11 KEEPS THE sceAudioOut PROHIBITION. M10 was the one milestone that relaxed
# it into a four-name allowlist, and it could do that safely because audio lived
# in its own translation unit that appeared in no other target's object list.
# M11 simply does not link those objects, so the M7/M8/M9 prohibition applies
# here unchanged and m11-verify enforces it. That is a NARROWER image than M10's
# for a milestone whose subject is memory, not a regression from it.
#
# gba_rom.o IS REUSED UNMODIFIED, as are M2's probes, M3's BIOS, M5's execution,
# M7's presentation, M8's keypad and mapping probe, and M10's demand-paging
# mapping probe. gpsp/** is untouched.

M11TARGET := m11gpsp
M11BUILD  := build/m11
M11ELF    := $(M11BUILD)/$(M11TARGET).elf
M11BIN    := $(M11BUILD)/$(M11TARGET).bin

# M10's include set with -Iapps/m11gpsp substituted. -Iadapters/gba is what lets
# apps/m11gpsp/main.c find gba_m11save.h -- the header that is deliberately
# typedef-free so it can be included from both sides of the gpSP/runtime type
# boundary (gpsp/common.h typedefs u64 as `unsigned long long` and
# runtime/core.h as `unsigned long` -- DISTINCT types).
M11_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m11gpsp -Iadapters/gba

M11FLAGS = $(BASEFLAGS) $(M11_INCLUDES) -Wall -Wno-unused-function

# IDENTICAL TO M10's. M11 adds NO new compile flag and changes NO existing one.
# The stage is chosen at RUNTIME through ext->frame_count, so one binary serves
# both stages and a fixture still cannot change the build it measures.
M11_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M11_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M11LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
             -Wl,--build-id=none -Wl,-z,norelro \
             -Wl,--orphan-handling=error \
             -Wl,-Map=$(M11BUILD)/$(M11TARGET).map

# ----------------------------------------------------------------- objects --
#
# gba_m11save.o IS M11-ONLY BY CONSTRUCTION. It appears in NO other target's
# object list -- M1OBJS..M10OBJS are untouched -- so no earlier milestone gains
# one byte from it. m11-verify proves that mechanically below.
M11_OWN_OBJS = $(M11BUILD)/m11main.o \
               $(M11BUILD)/shim.o \
               $(M11BUILD)/gba_fixture.o \
               $(M11BUILD)/gba_filestream.o \
               $(M11BUILD)/gba_compat.o \
               $(M11BUILD)/gba_probe.o \
               $(M11BUILD)/gba_bios.o \
               $(M11BUILD)/gba_rom.o \
               $(M11BUILD)/gba_m8map.o \
               $(M11BUILD)/gba_m10map.o \
               $(M11BUILD)/gba_m11save.o \
               $(M11BUILD)/gba_exec.o \
               $(M11BUILD)/gba_present.o \
               $(M11BUILD)/gba_input.o \
               $(M11BUILD)/platform.o

M11_GPSP_OBJS = $(M11BUILD)/gpsp_cpu.o $(M11BUILD)/gpsp_main.o \
                $(M11BUILD)/gpsp_gba_memory.o $(M11BUILD)/gpsp_video.o \
                $(M11BUILD)/gpsp_sound.o $(M11BUILD)/gpsp_serial.o \
                $(M11BUILD)/gpsp_serial_proto.o $(M11BUILD)/gpsp_rfu.o \
                $(M11BUILD)/gpsp_gbp.o $(M11BUILD)/gpsp_cheats.o \
                $(M11BUILD)/gpsp_savestate.o $(M11BUILD)/gpsp_input.o

M11OBJS = $(M11_OWN_OBJS) $(M11_GPSP_OBJS)

# EXCLUDED, unchanged from M1-M10 and none of these may ever appear above:
#   cpu_threaded.c  memmap.c  gba_cc_lut.c  bios_data.S
#   libretro/libretro.c  libretro/libretro-common/*.c
#   x86/**  arm/**  mips/**  3ds/**  bios/**  tests/**  tools/**  jni/**
#   runtime/gfx.c  runtime/audio.c  adapters/gba/gba_audio.c

$(M11BUILD):
	mkdir -p $(M11BUILD)

# ---- offline structural check ----------------------------------------------
#
# Same C structural checker M8/M9/M10 use, pointed at the M11 fixture AND at the
# new translation unit. It is deliberately NOT pointed at lua/m11.lua.in, for
# m9-check's reason: Lua comments start with --, which the tool does not know,
# so every apostrophe in a Lua comment would lex as an unterminated char literal
# and the check would FAIL ON CORRECT INPUT.
m11-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m11gpsp/main.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_m11save.c

# ---- THE OFFLINE SAVE-OBSERVER HARNESS -------------------------------------
#
# Builds and runs tools/gba_m11save_equiv.c with the HOST compiler, NOT the PS5
# toolchain. It #includes adapters/gba/gba_m11save.c VERBATIM, so it proves the
# SHIPPING observer -- not a paraphrase of it -- classifies correctly, bounds
# its region, tracks writes, and STAYS PASSIVE.
#
# RUN THIS BEFORE SPENDING ANY CONSOLE TIME. The property it exists for is the
# UNIT-COUNT TRAP: eeprom_size and flash_bank_cnt are UNIT COUNTS, not byte
# counts (EEPROM_8_KBYTE is 16, FLASH_SIZE_128KB is 2 --
# gpsp/gba_memory.h:294-298). An observer that echoed either would report
# "16 bytes" for an 8 KB EEPROM, and it would look entirely plausible on a
# result screen. That is a defect you cannot see; it has to be tested for.
#
# It also byte-compares gamepak_backup[] across a full latch + 64 snapshots to
# prove PASSIVITY mechanically rather than by inspection -- the property the
# entire milestone rests on.
#
# -I gpsp IS REQUIRED AND IS NOT A gpSP DEPENDENCY. gba_m11save.c opens with
# #include "common.h". The harness pre-defines that header's guard (COMMON_H,
# gpsp/common.h:20-21) BEFORE including the production .c, so the body expands
# to NOTHING -- but the preprocessor must still FIND the file before it can skip
# it, and tools/ is on no gpSP search path.
#
# This changes the BUILD RULE ONLY. adapters/gba/gba_m11save.c is not modified
# to suit the harness, nothing is copied out of gpsp/, and no test-only #ifdef
# exists in the production source. gba_m11save_equiv stays absent from every
# object list -- $(M11OBJS) does not contain it and no payload rule builds it.
m11-equiv: | $(M11BUILD)
	@$(HOSTCC) -std=c11 -O2 -Wall -Wextra -I adapters/gba -I gpsp \
	    -o $(M11BUILD)/gba_m11save_equiv tools/gba_m11save_equiv.c
	@$(M11BUILD)/gba_m11save_equiv

# ---- LUAport-owned objects -------------------------------------------------

$(M11BUILD)/m11main.o: apps/m11gpsp/main.c adapters/gba/gba_probe.h \
                       adapters/gba/gba_bios.h adapters/gba/gba_rom.h \
                       adapters/gba/gba_exec.h adapters/gba/gba_present.h \
                       adapters/gba/gba_input.h adapters/gba/gba_m8map.h \
                       adapters/gba/gba_m10map.h \
                       adapters/gba/gba_m11save.h \
                       $(HEADERS) | $(M11BUILD)
	$(CC) $(M11FLAGS) -c $< -o $@

$(M11BUILD)/shim.o: runtime/shim.c $(HEADERS) | $(M11BUILD)
	$(CC) $(M11FLAGS) -c $< -o $@

# M0's PS5 VideoOut AND PAD layer, COMPILED FROM UNMODIFIED SOURCE.
$(M11BUILD)/platform.o: runtime/platform.c $(HEADERS) | $(M11BUILD)
	$(CC) $(M11FLAGS) -c $< -o $@

$(M11BUILD)/gba_present.o: adapters/gba/gba_present.c adapters/gba/gba_present.h \
                           runtime/core.h | $(M11BUILD)
	$(CC) $(M11FLAGS) -c $< -o $@

# M11's SAVE OBSERVER. NEW, and linked ONLY here.
#
# COMPILED WITH THE gpSP FLAGS AND NOTHING ELSE, exactly like gba_rom.o and
# gba_m10map.o, and that is load-bearing in BOTH directions:
#
#   IT NEEDS gpSP's FLAGS because it includes gpsp/common.h to reach
#   backup_type, gamepak_backup[] and the BACKUP_*/FLASH_*/EEPROM_* constants,
#   so it must see the same gpSP configuration every other gpSP object sees.
#
#   IT MUST NOT GET $(M11_INCLUDES), whose -Iruntime would put runtime/core.h on
#   the include path. gpsp/common.h:100 typedefs u64 as `unsigned long long` and
#   runtime/core.h:30 as `unsigned long` -- DISTINCT types -- so a TU that saw
#   both would not compile. `gba_m11save.h` is found without any -I because it
#   sits beside the .c file, which is how every other adapter resolves its own
#   header.
$(M11BUILD)/gba_m11save.o: adapters/gba/gba_m11save.c adapters/gba/gba_m11save.h | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

# M8's keypad adapter, compiled from BYTE-IDENTICAL, UNMODIFIED source.
$(M11BUILD)/gba_input.o: adapters/gba/gba_input.c adapters/gba/gba_input.h | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

# M4's cartridge adapter, REUSED UNMODIFIED.
$(M11BUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gba_m8map.o: adapters/gba/gba_m8map.c adapters/gba/gba_m8map.h | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gba_m10map.o: adapters/gba/gba_m10map.c adapters/gba/gba_m10map.h | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects -----------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, IDENTICAL to M10's.
# gpsp/** is never modified and its own Makefile is never invoked. M11 needed
# ZERO changes here -- the save state it observes was already being compiled
# into every image from M1 onward; nothing had ever looked at it.

$(M11BUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M11BUILD)
	$(CXX) $(M11_GPSPCXXFLAGS) -c $< -o $@

$(M11BUILD)/gpsp_video.o: gpsp/video.cc | $(M11BUILD)
	$(CXX) $(M11_GPSPCXXFLAGS) -c $< -o $@

$(M11BUILD)/gpsp_main.o: gpsp/main.c | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gpsp_sound.o: gpsp/sound.c | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gpsp_serial.o: gpsp/serial.c | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

$(M11BUILD)/gpsp_input.o: gpsp/input.c | $(M11BUILD)
	$(CC) $(M11_GPSPFLAGS) -c $< -o $@

m11-objs: $(M11OBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- exactly as M1-M10.
# Every object is named on the link line and is included in its ENTIRETY.
$(M11ELF): $(M11OBJS) runtime/linker.ld | $(M11BUILD)
	$(CC) $(BASEFLAGS) $(M11LDFLAGS) -o $@ $(M11OBJS)

m11-link: $(M11OBJS) | $(M11BUILD)
	@$(CC) $(BASEFLAGS) $(M11LDFLAGS) -o $(M11ELF) $(M11OBJS) \
	     > $(M11BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(M11BUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M11BUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M11 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M11 LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M11 LINK OK: linker exit 0, no undefined references"

# --arena 4194304, IDENTICAL TO M4's through M10's.
$(M11BIN): $(M11ELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M11ELF) $@ \
	   --map $(M11BUILD)/$(M11TARGET).map --budget $(JIT_LIMIT) --arena 4194304

m11: $(M11BIN)

# THE TWO HARD CEILINGS. sizereport.py enforces the JIT budget ($(JIT_LIMIT) =
# 524,288) and tools/check_forbidden.py enforces the 2,097,152-byte .bss
# tripwire.
#
# M11's STATIC FOOTPRINT IS DELIBERATELY TINY: 128 u32 block hashes plus two
# 128-bit bitmaps is 528 bytes. The alternative considered and REJECTED was a
# 128 KB shadow copy of gamepak_backup[], which would have answered exactly the
# same question -- which blocks changed -- while consuming a fifth of the
# milestone's remaining .bss headroom.
#
# M11 SHOULD BE SMALLER THAN M10, not larger: it adds gba_m11save.o and drops
# both gba_audio.o and audio.o along with M10's 42 KB of audio .bss.
#
# IF EITHER CEILING IS EXCEEDED, STOP AND REPORT IT. Do not shrink unrelated
# code to make room: the size of the M11 image is one of the results.
m11-size: $(M11ELF) $(M11BIN)
	@$(PYTHON) tools/sizereport.py $(M11ELF) $(M11BIN) \
	   --map $(M11BUILD)/$(M11TARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification ----------------------------------------------------------
#
# EVERY M10 GATE IS KEPT AND ONE CHANGES POLARITY BACK. All five tools run
# unmodified with the same arguments; the VideoOut allowlist, the PAD allowlist,
# the sceGnm prohibition, the marker-based rodata carve, the gfx.c exclusion and
# the adapter isolation checks carry over VERBATIM.
#
# THE ONE CHANGE: `sceAudioOut` was a STRICT ALLOWLIST at M10 and returns to
# being FORBIDDEN here, exactly as it is at M7/M8/M9. M11 links neither
# runtime/audio.o nor adapters/gba/gba_audio.o, so no audio name can reach the
# image and the prohibition is the honest gate.
#
# ---- WHAT M11 ADDS, AND WHY EACH ONE IS NECESSARY ----
#
#   THE OBSERVER IS PASSIVE, PROVED AT LINK TIME. gba_m11save.o MUST NOT
#   reference read_backup, write_backup, read_eeprom or write_eeprom. Those four
#   are part of gpSP's DETECTOR -- backup_type collapses from BACKUP_UNKN to
#   BACKUP_SRAM on the first backup-region access (gba_memory.c:465, :1141) --
#   so an observer that called one would MANUFACTURE the answer it then
#   reported, and every unknown cartridge would confidently report SRAM. This is
#   the single most important gate in the milestone.
#
#   THE OBSERVER REALLY READS gpSP's SAVE STATE. gba_m11save.o must reference
#   backup_type, gamepak_backup, flash_bank_cnt and eeprom_size. An observer
#   that does not read the emulator's real save globals is not observing
#   anything, and its clean report would be vacuous rather than informative.
#
#   THE FIXTURE GOES THROUGH THE ADAPTER. m11main.o must reference
#   m11save_latch_static, m11save_snapshot and m11save_probe. All three matter:
#   the first proves the static baseline is actually captured, the second that
#   sampling happens, and the third that coherence is checked rather than
#   assumed.
#
#   THE FIXTURE DOES NOT REACH AROUND THE ADAPTER. m11main.o must carry NO
#   backup_type / gamepak_backup relocation of its own. If the fixture read the
#   emulator's save globals directly, the adapter's passivity guarantee would
#   cover nothing that mattered.
#
#   gpSP DID NOT LEARN ABOUT M11. No gpsp_*.o may reference any m11save_ symbol.
#   This is the link-time proof that M11 required ZERO changes to gpsp/**.
#
#   M11's OBSERVER LEAKED INTO NOTHING. gba_m11save.o must appear in no earlier
#   milestone's object list, so M1-M10 gain not one byte from it.

M11_VIDEOOUT_ALLOW = sceVideoOutOpen sceVideoOutClose sceVideoOutRegisterBuffers \
                     sceVideoOutSubmitFlip sceVideoOutSetFlipRate \
                     sceVideoOutAddFlipEvent

M11_PAD_ALLOW = scePadInit scePadGetHandle scePadRead

# THE FOUR gpSP SAVE-PATH ENTRY POINTS THAT MUST NEVER BE CALLED.
M11_FORBIDDEN_SAVE = read_backup write_backup read_eeprom write_eeprom

m11-verify: $(M11ELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M11ELF)
	@echo "== relocations =="
	@$(PYTHON) tools/verify_reloc.py $(M11ELF) | tail -12
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M11OBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M11OBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M11ELF) $(M11OBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M11ELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M11 VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== PRESENTATION ALLOWLIST (carried from M7/M8/M9/M10) =="
	@$(OBJCOPY) -O binary --only-section=.text $(M11ELF) $(M11BUILD)/text.bin
	@tx=$$($(NM) $(M11ELF) | awk '$$NF == "__text_start"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 ra=$$($(NM) $(M11ELF) | awk '$$NF == "__rodata_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 rz=$$($(NM) $(M11ELF) | awk '$$NF == "__rodata_end"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 if [ -z "$$tx" ] || [ -z "$$ra" ] || [ -z "$$rz" ]; then \
	     echo "M11 VERIFY FAILED: linker marker symbol missing -- need"; \
	     echo "  __text_start, __rodata_start and __rodata_end in $(M11ELF)."; \
	     exit 1; \
	 fi; \
	 t=$$((0x$$tx)); a=$$((0x$$ra)); z=$$((0x$$rz)); n=$$((z - a)); \
	 if [ "$$t" -ne 0 ]; then \
	     echo "M11 VERIFY FAILED: .text does not start at vaddr 0 (__text_start ="; \
	     echo "  $$t), so a symbol value is no longer a file offset in the"; \
	     echo "  extracted binary and this carve would read the wrong bytes."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M11 VERIFY FAILED: empty rodata window -- __rodata_start=$$a"; \
	     echo "  __rodata_end=$$z gives $$n bytes."; \
	     exit 1; \
	 fi; \
	 have=$$(wc -c < $(M11BUILD)/text.bin | tr -d " "); \
	 if [ "$$have" -lt "$$z" ]; then \
	     echo "M11 VERIFY FAILED: .text extraction is $$have bytes but the rodata"; \
	     echo "  window ends at $$z -- the extraction is short or .text is absent."; \
	     exit 1; \
	 fi; \
	 dd if=$(M11BUILD)/text.bin of=$(M11BUILD)/rodata.bin bs=1 skip=$$a count=$$n \
	    2>/dev/null || { \
	     echo "M11 VERIFY FAILED: dd could not carve the rodata window."; exit 1; \
	 }; \
	 if [ ! -s $(M11BUILD)/rodata.bin ]; then \
	     echo "M11 VERIFY FAILED: rodata extraction produced 0 bytes -- every check"; \
	     echo "  below would pass vacuously. Fix the extraction, not the checks."; \
	     exit 1; \
	 fi; \
	 got=$$(wc -c < $(M11BUILD)/rodata.bin | tr -d " "); \
	 if [ "$$got" -ne "$$n" ]; then \
	     echo "M11 VERIFY FAILED: rodata.bin is $$got bytes, expected $$n ="; \
	     echo "  __rodata_end - __rodata_start. Extraction is not trustworthy."; \
	     exit 1; \
	 fi; \
	 echo "rodata carved from .text by linker markers: [$$a,$$z) = $$n bytes"
	@for s in $(M11_VIDEOOUT_ALLOW) libSceVideoOut.sprx; do \
	     if ! grep -aq "$$s" $(M11BUILD)/rodata.bin; then \
	         echo "M11 VERIFY FAILED: required presentation name '$$s' is NOT in"; \
	         echo "  the image."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 6 VideoOut entry points + libSceVideoOut.sprx are present"
	@allow=$$(echo $(M11_VIDEOOUT_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "sceVideoOut[A-Za-z0-9_]*" $(M11BUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M11 VERIFY FAILED: an UNEXPECTED sceVideoOut* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no sceVideoOut* name outside the six-symbol allowlist"
	@echo "== THE GRAPHICS-PIPELINE PROHIBITION IS UNCHANGED =="
	@if grep -aqE "sceGnm" $(M11BUILD)/rodata.bin; then \
	     echo "M11 VERIFY FAILED: a graphics-pipeline name reached the image:"; \
	     grep -aoE "sceGnm[A-Za-z0-9_]*" $(M11BUILD)/rodata.bin \
	        | sort -u | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no sceGnm* name is present (NO GNM -- unchanged from M6-M10)"
	@echo "== PAD ALLOWLIST (carried from M8/M9/M10) =="
	@for s in $(M11_PAD_ALLOW) libScePad.sprx; do \
	     if ! grep -aq "$$s" $(M11BUILD)/rodata.bin; then \
	         echo "M11 VERIFY FAILED: required pad name '$$s' is NOT in the image."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 3 scePad entry points + libScePad.sprx are present"
	@allow=$$(echo $(M11_PAD_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "scePad[A-Za-z0-9_]*" $(M11BUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M11 VERIFY FAILED: an UNEXPECTED scePad* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no scePad* name outside the three-symbol allowlist"
	@echo "== THE AUDIO PROHIBITION IS RESTORED (M10 was the exception) =="
	@if grep -aqE "sceAudioOut" $(M11BUILD)/rodata.bin; then \
	     echo "M11 VERIFY FAILED: an audio name reached the image:"; \
	     grep -aoE "sceAudioOut[A-Za-z0-9_]*" $(M11BUILD)/rodata.bin \
	        | sort -u | sed "s/^/  /"; \
	     echo "  M11 links neither runtime/audio.o nor gba_audio.o, so no such"; \
	     echo "  name can be present unless an object list changed."; \
	     exit 1; \
	 fi; \
	 echo "no sceAudioOut* name is present (the M7/M8/M9 prohibition, restored)"
	@echo "== THE PASSIVITY GATE -- THE MOST IMPORTANT CHECK IN M11 =="
	@bad=""; \
	 for s in $(M11_FORBIDDEN_SAVE); do \
	     if $(NM) -u $(M11BUILD)/gba_m11save.o 2>/dev/null | grep -qw "$$s"; then \
	         bad="$$bad $$s"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M11 VERIFY FAILED: the observer references gpSP's SAVE-PATH"; \
	     echo "  entry points:$$bad"; \
	     echo "  Those four are part of gpSP's DETECTOR, not its reporting"; \
	     echo "  surface: backup_type collapses from BACKUP_UNKN to BACKUP_SRAM"; \
	     echo "  on the first backup-region access (gba_memory.c:465, :1141)."; \
	     echo "  An observer that calls one MANUFACTURES the answer it then"; \
	     echo "  reports, and EVERY unknown cartridge would confidently report"; \
	     echo "  SRAM. This is not a style rule."; \
	     exit 1; \
	 fi; \
	 echo "gba_m11save.o calls NONE of read_backup/write_backup/read_eeprom/write_eeprom"
	@bad=""; \
	 for s in $(M11_FORBIDDEN_SAVE); do \
	     if $(NM) -u $(M11BUILD)/m11main.o 2>/dev/null | grep -qw "$$s"; then \
	         bad="$$bad $$s"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M11 VERIFY FAILED: the FIXTURE references gpSP's save-path entry"; \
	     echo "  points:$$bad -- reaching around the adapter defeats the"; \
	     echo "  adapter's passivity guarantee entirely."; \
	     exit 1; \
	 fi; \
	 echo "m11main.o calls none of them either"
	@echo "== THE OBSERVER READS gpSP's REAL SAVE STATE =="
	@miss=""; \
	 for s in backup_type gamepak_backup flash_bank_cnt eeprom_size; do \
	     $(NM) -u $(M11BUILD)/gba_m11save.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M11 VERIFY FAILED: gba_m11save.o does NOT reference:$$miss"; \
	     echo "  An observer that does not read the emulator's real save globals"; \
	     echo "  is not observing anything, and its clean report would be"; \
	     echo "  VACUOUS rather than informative."; \
	     exit 1; \
	 fi; \
	 echo "gba_m11save.o reads gpSP's real save state (backup_type,"; \
	 echo "  gamepak_backup, flash_bank_cnt, eeprom_size)"
	@echo "== THE FIXTURE DRIVES THE OBSERVER, AND ONLY THROUGH THE ADAPTER =="
	@miss=""; \
	 for s in m11save_latch_static m11save_snapshot m11save_probe; do \
	     $(NM) -u $(M11BUILD)/m11main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M11 VERIFY FAILED: m11main.o does NOT reference:$$miss"; \
	     echo "  The static baseline is not actually being captured, or the save"; \
	     echo "  state's coherence is not actually being checked."; \
	     exit 1; \
	 fi; \
	 echo "m11main.o drives the latch, the per-frame sample AND the probe"
	@bad=""; \
	 for s in backup_type gamepak_backup eeprom_size flash_bank_cnt; do \
	     if $(NM) -u $(M11BUILD)/m11main.o 2>/dev/null | grep -qw "$$s"; then \
	         bad="$$bad $$s"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M11 VERIFY FAILED: m11main.o reads gpSP's save globals DIRECTLY:$$bad"; \
	     echo "  Everything must go through the observer, or the adapter's"; \
	     echo "  passivity guarantee covers nothing that matters."; \
	     exit 1; \
	 fi; \
	 echo "m11main.o reaches gpSP's save state ONLY through the adapter"
	@echo "== gpSP DID NOT LEARN ABOUT M11 =="
	@bad=""; \
	 for o in $(M11_GPSP_OBJS); do \
	     if $(NM) -u $$o 2>/dev/null | grep -q "m11save_"; then bad="$$bad $$o"; fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M11 VERIFY FAILED: a gpSP object references the observer:$$bad"; \
	     echo "  M11 requires ZERO changes to gpsp/** and this is the proof."; \
	     exit 1; \
	 fi; \
	 echo "no gpsp_*.o references any m11save_ symbol"
	@echo "== THE OBSERVER LEAKED INTO NO EARLIER MILESTONE =="
	@bad=""; \
	 for o in $(M1OBJS) $(M2OBJS) $(M3OBJS) $(M4OBJS) $(M5OBJS) $(M6OBJS) \
	          $(M7OBJS) $(M8OBJS) $(M9OBJS) $(M9DOBJS) $(M10OBJS); do \
	     case "$$o" in *gba_m11save*) bad="$$bad $$o";; esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M11 VERIFY FAILED: the M11-only save observer leaked into an"; \
	     echo "  earlier milestone's object list:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "gba_m11save.o is in NO other target -- M1-M10 link sets unchanged"
	@echo "== NO AUDIO OBJECT IS LINKED =="
	@bad=""; \
	 for o in $(M11OBJS); do \
	     case "$$o" in *gba_audio*|*/audio.o) bad="$$bad $$o";; esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M11 VERIFY FAILED: an audio object is in the M11 link line:$$bad"; \
	     echo "  M11 has no audio path and its verifier asserts the sceAudioOut"; \
	     echo "  PROHIBITION -- linking one would make that gate a lie."; \
	     exit 1; \
	 fi; \
	 echo "neither gba_audio.o nor audio.o is in the M11 object list"
	@echo "== M11 VERIFY OK =="

# ---- the Lua payload -------------------------------------------------------

M11_LUA_TEMPLATE := lua/m11.lua.in
M11_LUA_OUT      := $(M11BUILD)/$(M11TARGET).lua

m11-lua: m11-check $(M11BIN) $(M11ELF)
	@case "$(STAGE)" in \
	   0|1) ;; \
	   *) echo "M11 LUA FAILED: STAGE='$(STAGE)' is not 0 or 1."; \
	      echo "  0=STATIC (3 s, no frame loop)   1=OBSERVE (40 s window)"; \
	      exit 1;; \
	 esac
	@$(PYTHON) tools/mklua.py $(M11_LUA_TEMPLATE) $(M11BIN) $(M11_LUA_OUT) $(M11ELF)
	@sed -i.bak "s/^local STAGE = .*/local STAGE = $(STAGE)/" $(M11_LUA_OUT)
	@rm -f $(M11_LUA_OUT).bak
	@if ! grep -q "^local STAGE = $(STAGE)$$" $(M11_LUA_OUT); then \
	     echo "M11 LUA FAILED: the STAGE line was not rewritten in"; \
	     echo "  $(M11_LUA_OUT) -- it would run STAGE 0 regardless of STAGE=$(STAGE)."; \
	     exit 1; \
	 fi
	@echo "M11 LUA OK: $(M11_LUA_OUT) -- STAGE $(STAGE)"

# ---- m11-send -- HARDWARE DELIVERY -----------------------------------------
#
# NOT AUTHORISED BY THE EXISTENCE OF THIS TARGET. STAGE=0 and STAGE=1 both build
# and transmit perfectly well -- that is deliberate and the stage selector is
# untouched -- but RUNNING either is an OPERATOR decision taken explicitly,
# after the offline chain is green. This is an authorisation rule, not a
# code-enforced one.
#
#   * RUN STAGE 0 FIRST. It is three seconds, it never enters the frame loop,
#     and it answers the only question that blocks the other stage: did the
#     static latch take, and does the observer work at all?
#   * RELAUNCH THE HOST GAME BETWEEN EVERY RUN. plat_video_init() reserves 16 MB
#     of direct memory and DELIBERATELY never releases it, so a second run in
#     one session fails with PLAT_VID_EALLOC (-24).
#   * M11 reuses M3's BIOS and M4's cartridge. Neither is re-uploaded, and there
#     is deliberately no m11-upload-rom: M11's result is a property of the
#     CARTRIDGE, so which cartridge is present must stay an operator variable.
#   * TO GET A NON-UNKNOWN ANSWER, SAVE IN-GAME DURING STAGE 1. Static detection
#     alone cannot distinguish a save-less cartridge from an unexercised one.
#
# WHY m11-lua IS A PREREQUISITE AND $(M11_LUA_OUT) IS NOT. m11-lua is PHONY and
# STAGE is a command-line variable, so a file prerequisite on $(M11_LUA_OUT)
# would be satisfied by a .lua left behind by a PREVIOUS stage and would ship
# the wrong stage silently. Depending on the PHONY target forces regeneration
# for the STAGE actually requested. STAGE validation is m11-lua's -- it is not
# duplicated here.
m11-send: m11-verify m11-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m11-send PS5_IP=<console address> STAGE=<0|1>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M11_LUA_OUT) $(M11BIN)

m11-clean:
	rm -rf $(M11BUILD)

.PHONY: m11 m11-objs m11-check m11-equiv m11-link m11-size m11-verify m11-lua \
        m11-send m11-clean

# =============================================================================
# M12A -- THE PS5 SAVEDATA FOUNDATION PROBE
# =============================================================================
#
#   make m12-check       offline C structural check of the new sources
#   make m12-objs        compile only
#   make m12-link        link only, and list anything still undefined
#   make m12             build + link + print the image size report
#   make m12-size        size report only (needs a prior m12 build)
#   make m12-verify      every offline check for the M12 image
#   make m12-lua         package the M12 delivery script only
#   make m12-send        transmit M12 to a console -- REQUIRES PS5_IP
#   make m12-clean       removes build/m12 ONLY
#
# PURELY ADDITIVE. Not one M0-M11 variable, rule, object list or target is
# modified by this block. M12 introduces ONE new translation unit
# (runtime/savedata.o) and one new fixture (apps/m12gpsp/main.c).
#
# M12A IS THE NARROWEST IMAGE SINCE M0, AND DELIBERATELY SO. It links THREE
# objects: the fixture, the shim, and the savedata layer. It does NOT link
# gpsp/**, any adapter, runtime/gfx.o, runtime/audio.o or even
# runtime/platform.o -- so there is no ROM, no BIOS, no dynarec, no video, no
# audio, no pad and no M11 observer anywhere in it.
#
# THAT NARROWNESS IS THE POINT. M11 takes the display, loads a BIOS and a
# cartridge and runs an emulator; every one of those is a way for a savedata
# probe to fail for a reason that has nothing to do with savedata, and each
# costs a game relaunch to tell apart. Here, a failure is a savedata failure and
# cannot be anything else.
#
# THERE IS NO GBA SAVE PERSISTENCE IN THIS MILESTONE. No .sav file is created,
# opened or written; adapters/gba/gba_save.* deliberately does not exist yet.
# That is M12B.

M12TARGET := m12gpsp
M12BUILD  := build/m12
M12ELF    := $(M12BUILD)/$(M12TARGET).elf
M12BIN    := $(M12BUILD)/$(M12TARGET).bin

# M0's include set with -Iapps/m12gpsp substituted. NOTE WHAT IS ABSENT:
# $(M1_SHIMINC) and -Iadapters/gba are BOTH gone, because M12 compiles no gpSP
# object and no adapter. Nothing here can reach a gpSP header even by accident,
# so the u64 typedef clash that constrains every adapter rule (gpsp/common.h
# says `unsigned long long`, runtime/core.h says `unsigned long`) cannot arise.
M12_INCLUDES = -Iruntime/libc -Iruntime -Iapps/m12gpsp

M12FLAGS = $(BASEFLAGS) $(M12_INCLUDES) -Wall -Wno-unused-function

# $(HEADERS) IS EXTENDED LOCALLY RATHER THAN EDITED. Adding runtime/savedata.h
# to the shared HEADERS variable would add a dependency to every M0-M11 rule and
# force a rebuild of images whose measured baselines this milestone is required
# not to disturb.
M12HEADERS = $(HEADERS) runtime/savedata.h

M12LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
             -Wl,--build-id=none -Wl,-z,norelro \
             -Wl,--orphan-handling=error \
             -Wl,-Map=$(M12BUILD)/$(M12TARGET).map

# ----------------------------------------------------------------- objects --
#
# savedata.o IS M12-ONLY BY CONSTRUCTION. It appears in NO other target's object
# list -- M1OBJS..M11OBJS are untouched -- so no earlier milestone gains one byte
# from it. m12-verify proves that mechanically below.
M12OBJS = $(M12BUILD)/m12main.o \
          $(M12BUILD)/shim.o \
          $(M12BUILD)/savedata.o

$(M12BUILD):
	mkdir -p $(M12BUILD)

# ---- offline structural check ----------------------------------------------
#
# Same C structural checker M8-M11 use, pointed at the M12 fixture AND at the
# new runtime translation unit. It is deliberately NOT pointed at
# lua/m12.lua.in, for m9-check's reason: Lua comments start with --, which the
# tool does not know, so every apostrophe in a Lua comment would lex as an
# unterminated char literal and the check would FAIL ON CORRECT INPUT.
m12-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m12gpsp/main.c
	@$(PYTHON) tools/m8_syntax_check.py runtime/savedata.c

# ---- LUAport-owned objects -------------------------------------------------

$(M12BUILD)/m12main.o: apps/m12gpsp/main.c $(M12HEADERS) | $(M12BUILD)
	$(CC) $(M12FLAGS) -c $< -o $@

$(M12BUILD)/shim.o: runtime/shim.c $(M12HEADERS) | $(M12BUILD)
	$(CC) $(M12FLAGS) -c $< -o $@

# M12's SAVEDATA LAYER. NEW, and linked ONLY here.
#
# Compiled with the ORDINARY LUAport flags, not gpSP's -- unlike every adapter
# rule in this Makefile. It includes runtime/core.h and runtime/shim.h and
# nothing from gpsp/, which is exactly why it can: there is no second u64
# typedef in scope to collide with.
$(M12BUILD)/savedata.o: runtime/savedata.c runtime/savedata.h $(M12HEADERS) | $(M12BUILD)
	$(CC) $(M12FLAGS) -c $< -o $@

m12-objs: $(M12OBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- exactly as M0-M11.
# Every object is named on the link line and is included in its ENTIRETY.
$(M12ELF): $(M12OBJS) runtime/linker.ld | $(M12BUILD)
	$(CC) $(BASEFLAGS) $(M12LDFLAGS) -o $@ $(M12OBJS)

m12-link: $(M12OBJS) | $(M12BUILD)
	@$(CC) $(BASEFLAGS) $(M12LDFLAGS) -o $(M12ELF) $(M12OBJS) \
	     > $(M12BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(M12BUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M12BUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M12 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M12 LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M12 LINK OK: linker exit 0, no undefined references"

# --arena 1048576, M0's value. M12A allocates NOTHING from the arena -- the
# probe buffers are stack arrays and the shim's FILE slots are static .bss -- so
# the arena exists purely because shim_init() requires one.
$(M12BIN): $(M12ELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M12ELF) $@ \
	   --map $(M12BUILD)/$(M12TARGET).map --budget $(JIT_LIMIT) --arena 1048576

m12: $(M12BIN)

# THE TWO HARD CEILINGS, unchanged: sizereport.py enforces the JIT budget
# ($(JIT_LIMIT) = 524,288) and tools/check_forbidden.py enforces the
# 2,097,152-byte .bss tripwire.
#
# M12A SHOULD BE THE SMALLEST IMAGE SINCE M0 BY A LARGE MARGIN -- three objects,
# no gpSP, no adapters, no framebuffer. Its .bss is the shim's FILE slots plus a
# 36-byte result struct.
#
# IF EITHER CEILING IS EXCEEDED, STOP AND REPORT IT. An M12A image anywhere near
# the JIT budget would mean something got linked that this milestone does not
# name, and the size is one of the results.
m12-size: $(M12ELF) $(M12BIN)
	@$(PYTHON) tools/sizereport.py $(M12ELF) $(M12BIN) \
	   --map $(M12BUILD)/$(M12TARGET).map --budget $(JIT_LIMIT) --arena 1048576

# ---- verification ----------------------------------------------------------
#
# check_closure.py RUNS WITH THE DEFAULT --core none, exactly as M0's `verify`
# does, and NOT with --core gpsp. That is not an oversight: --core gpsp exists to
# ALLOW the intentional gpSP closure into the image. M12 has no such closure, so
# allowing one would be granting a permission this milestone must not have -- any
# gpSP symbol appearing here is a leak and has to fail.
#
# ---- WHAT M12 ADDS, AND WHY EACH ONE IS NECESSARY ----
#
#   SD_CREATE MUST NEVER BE DEFINED. With a wrong dirName that flag silently
#   builds a SECOND, EMPTY container and mounts that, so the write appears to
#   succeed and goes nowhere -- a failure that REPORTS SUCCESS is the worst kind
#   this milestone can have. The constant is absent by construction and the gate
#   below keeps it that way.
#
#   THE GATE MATCHES THE #define, NOT THE BARE NAME, and that is a correctness
#   point rather than a convenience. BOTH new files DOCUMENT this flag by name,
#   at length, because the reason it is absent has to be written down somewhere
#   or the next person reinstates it. A gate grepping for the bare token would
#   FAIL ON CORRECT INPUT -- the same defect class as the M11 check_symtypes
#   false positive, and exactly the kind of check that gets "fixed" by deleting
#   the comment that made it fire.
#
#   Matching the #define is not a weaker check, it is a COMPLETE one: SD_CREATE
#   is not a symbol that exists in any PS5 header this project uses, so using it
#   WITHOUT defining it first is a compile error that never reaches this gate.
#   The #define is a necessary precondition for the bug, so forbidding the
#   #define forbids the bug.
#
#   THE FIXTURE GOES THROUGH THE ADAPTER. m12main.o must reference all five
#   plat_savedata_* entry points, and must carry NO sceSaveData* string of its
#   own. If the fixture resolved the mount itself, the layer's guarantees --
#   restore-on-every-path above all -- would cover nothing that mattered.
#
#   THE IMAGE TAKES NO HARDWARE. No sceVideoOut, scePad, sceAudioOut or sceGnm
#   name may appear. M12A links neither platform.o nor gfx.o, so none CAN appear
#   unless an object list changed -- and an M12A that quietly took the display
#   would cost a game relaunch per run for no reason.
#
#   NO GBA CONTENT REACHED THE IMAGE. No gpSP or adapter object may be in the
#   link line. This is the link-time proof that M12A is a platform milestone.
#
#   THE SAVEDATA LAYER LEAKED INTO NOTHING. savedata.o must appear in no earlier
#   milestone's object list, so M0-M11 gain not one byte from it.
#
# WHY THE RELOCATION GATE BELOW IS NOT WRITTEN "verify_reloc.py ... | tail -12"
# LIKE M0-M11:
#
#   A pipeline's exit status in POSIX sh is the status of the LAST command, so
#   `tail` -- which always succeeds -- discards the checker's. M12A is the first
#   image ever to make that checker actually fail, which is how the masking was
#   found: verify_reloc.py printed "VERDICT: NOT SAFE", returned 1, and the
#   target still went on to print "M12 VERIFY OK".
#
#   So the status is captured before anything else runs, and the output is still
#   trimmed to the last 12 lines from a log file. `set -o pipefail` would be the
#   one-word fix but it is a bashism, and setting it repository-wide would
#   silently make every milestone's currently-advisory gates fatal at once --
#   a much larger change than this milestone is scoped to make. M0-M11 keep
#   their existing call sites here; the masking they share is recorded, not
#   repaired, by this milestone.
#
#   M12OBJS is passed as well: verify_reloc.py consults the input objects ONLY
#   when the linked image has an empty table, to tell "nothing required a
#   relocation" (M12A: no initialised pointer exists) apart from "relocations
#   were required and got dropped". It cannot relax a check on a populated table.

M12_REQUIRED_NAMES = libSceSaveData.sprx sceSaveDataUmount sceKernelMkdir \
                     sceKernelStat sceKernelUsleep SLUS-20268 SLES-50366

M12_FORBIDDEN_HW = sceVideoOut scePad sceAudioOut sceGnm

m12-verify: $(M12ELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M12ELF)
	@echo "== relocations =="
	@$(PYTHON) tools/verify_reloc.py $(M12ELF) $(M12OBJS) \
	     > $(M12BUILD)/reloc.log 2>&1; \
	 st=$$?; \
	 tail -12 $(M12BUILD)/reloc.log; \
	 if [ $$st -ne 0 ]; then \
	     echo "M12 VERIFY FAILED: the relocation check exited $$st."; \
	     echo "  Full output: $(M12BUILD)/reloc.log"; \
	     exit $$st; \
	 fi
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M12OBJS) | tail -4
	@echo "== closure -- NO core is permitted at M12 =="
	@$(PYTHON) tools/check_closure.py $(M12OBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M12ELF) $(M12OBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M12ELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M12 VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== SD_CREATE IS NEVER DEFINED -- THE MOST IMPORTANT GATE =="
	@if grep -nE "^[[:space:]]*#[[:space:]]*define[[:space:]]+SD_CREATE" \
	        runtime/savedata.c runtime/savedata.h; then \
	     echo "M12 VERIFY FAILED: SD_CREATE is DEFINED in the savedata layer."; \
	     echo "  With a wrong dirName that flag silently builds a SECOND, EMPTY"; \
	     echo "  container and mounts THAT, so the write appears to succeed and"; \
	     echo "  goes nowhere. A failure that REPORTS SUCCESS is the worst"; \
	     echo "  outcome this milestone can produce. This is not a style rule."; \
	     exit 1; \
	 fi; \
	 echo "SD_CREATE is not defined in runtime/savedata.{c,h}"
	@echo "== rodata carve (marker-based, as M7-M11) =="
	@$(OBJCOPY) -O binary --only-section=.text $(M12ELF) $(M12BUILD)/text.bin
	@tx=$$($(NM) $(M12ELF) | awk '$$NF == "__text_start"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 ra=$$($(NM) $(M12ELF) | awk '$$NF == "__rodata_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 rz=$$($(NM) $(M12ELF) | awk '$$NF == "__rodata_end"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 if [ -z "$$tx" ] || [ -z "$$ra" ] || [ -z "$$rz" ]; then \
	     echo "M12 VERIFY FAILED: linker marker symbol missing -- need"; \
	     echo "  __text_start, __rodata_start and __rodata_end in $(M12ELF)."; \
	     exit 1; \
	 fi; \
	 t=$$((0x$$tx)); a=$$((0x$$ra)); z=$$((0x$$rz)); n=$$((z - a)); \
	 if [ "$$t" -ne 0 ]; then \
	     echo "M12 VERIFY FAILED: .text does not start at vaddr 0 (__text_start ="; \
	     echo "  $$t), so a symbol value is no longer a file offset in the"; \
	     echo "  extracted binary and this carve would read the wrong bytes."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M12 VERIFY FAILED: empty rodata window -- __rodata_start=$$a"; \
	     echo "  __rodata_end=$$z gives $$n bytes."; \
	     exit 1; \
	 fi; \
	 have=$$(wc -c < $(M12BUILD)/text.bin | tr -d " "); \
	 if [ "$$have" -lt "$$z" ]; then \
	     echo "M12 VERIFY FAILED: .text extraction is $$have bytes but the rodata"; \
	     echo "  window ends at $$z -- the extraction is short or .text is absent."; \
	     exit 1; \
	 fi; \
	 dd if=$(M12BUILD)/text.bin of=$(M12BUILD)/rodata.bin bs=1 skip=$$a count=$$n \
	    2>/dev/null || { \
	     echo "M12 VERIFY FAILED: dd could not carve the rodata window."; exit 1; \
	 }; \
	 if [ ! -s $(M12BUILD)/rodata.bin ]; then \
	     echo "M12 VERIFY FAILED: rodata extraction produced 0 bytes -- every check"; \
	     echo "  below would pass vacuously. Fix the extraction, not the checks."; \
	     exit 1; \
	 fi; \
	 got=$$(wc -c < $(M12BUILD)/rodata.bin | tr -d " "); \
	 if [ "$$got" -ne "$$n" ]; then \
	     echo "M12 VERIFY FAILED: rodata.bin is $$got bytes, expected $$n ="; \
	     echo "  __rodata_end - __rodata_start. Extraction is not trustworthy."; \
	     exit 1; \
	 fi; \
	 echo "rodata carved from .text by linker markers: [$$a,$$z) = $$n bytes"
	@echo "== THE SAVEDATA LAYER REALLY REACHED THE IMAGE =="
	@for s in $(M12_REQUIRED_NAMES); do \
	     if ! grep -aq "$$s" $(M12BUILD)/rodata.bin; then \
	         echo "M12 VERIFY FAILED: required savedata name '$$s' is NOT in the"; \
	         echo "  image. A probe that cannot name the module it mounts is not"; \
	         echo "  mounting anything, and a clean run would be VACUOUS."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "libSceSaveData.sprx, sceSaveDataUmount, both region dirNames and the"; \
	 echo "  three kernel entry points are all present"
	@if ! grep -aq "/savedata0/gba/m12_probe.bin" $(M12BUILD)/rodata.bin; then \
	     echo "M12 VERIFY FAILED: the probe file path is not in the image."; \
	     exit 1; \
	 fi; \
	 echo "the probe path /savedata0/gba/m12_probe.bin is present"
	@echo "== NO .sav IS NAMED ANYWHERE -- M12A PERSISTS NO GBA SAVE =="
	@if grep -aq "\.sav" $(M12BUILD)/rodata.bin; then \
	     echo "M12 VERIFY FAILED: a .sav name reached the image:"; \
	     grep -aoE "[A-Za-z0-9_/.-]*\.sav[A-Za-z0-9_]*" $(M12BUILD)/rodata.bin \
	        | sort -u | sed "s/^/  /"; \
	     echo "  M12A writes a 32-byte probe and NOTHING ELSE. Cartridge save"; \
	     echo "  persistence is M12B and does not exist yet."; \
	     exit 1; \
	 fi; \
	 echo "no .sav name is present"
	@echo "== THE IMAGE TAKES NO HARDWARE =="
	@bad=""; \
	 for s in $(M12_FORBIDDEN_HW); do \
	     if grep -aq "$$s" $(M12BUILD)/rodata.bin; then bad="$$bad $$s"; fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12 VERIFY FAILED: a hardware name reached the image:$$bad"; \
	     echo "  M12A links neither platform.o nor gfx.o, so no such name can be"; \
	     echo "  present unless an object list changed. An M12A that quietly took"; \
	     echo "  the display would cost a game relaunch per run for no reason."; \
	     exit 1; \
	 fi; \
	 echo "no sceVideoOut*, scePad*, sceAudioOut* or sceGnm* name is present"
	@echo "== THE FIXTURE DRIVES THE LAYER, AND ONLY THROUGH THE LAYER =="
	@miss=""; \
	 for s in plat_savedata_init plat_savedata_is_ready plat_savedata_begin_write \
	          plat_savedata_end_write plat_savedata_mkdir; do \
	     $(NM) -u $(M12BUILD)/m12main.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M12 VERIFY FAILED: m12main.o does NOT reference:$$miss"; \
	     echo "  A fixture that does not drive all five entry points is not"; \
	     echo "  exercising the layer it exists to measure."; \
	     exit 1; \
	 fi; \
	 echo "m12main.o drives all five plat_savedata_* entry points"
	@if grep -aq "sceSaveData" $(M12BUILD)/m12main.o; then \
	     echo "M12 VERIFY FAILED: m12main.o carries a sceSaveData* name of its"; \
	     echo "  own -- the fixture is reaching around the savedata layer."; \
	     echo "  Everything must go through runtime/savedata.c, or its"; \
	     echo "  restore-the-read-only-mount guarantee covers nothing."; \
	     exit 1; \
	 fi; \
	 echo "m12main.o names no sceSaveData* symbol -- it goes through the layer"
	@echo "== NO GBA CONTENT IS LINKED =="
	@bad=""; \
	 for o in $(M12OBJS); do \
	     case "$$o" in \
	       *gpsp_*|*gba_*|*/gfx.o|*/platform.o|*/audio.o) bad="$$bad $$o";; \
	     esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12 VERIFY FAILED: a core, adapter or hardware object is in the"; \
	     echo "  M12 link line:$$bad"; \
	     echo "  M12A is a PLATFORM milestone. It links exactly three objects."; \
	     exit 1; \
	 fi; \
	 echo "the M12 link line is exactly m12main.o, shim.o and savedata.o"
	@echo "== THE SAVEDATA LAYER LEAKED INTO NO EARLIER MILESTONE =="
	@bad=""; \
	 for o in $(OBJS) $(M1OBJS) $(M2OBJS) $(M3OBJS) $(M4OBJS) $(M5OBJS) \
	          $(M6OBJS) $(M7OBJS) $(M8OBJS) $(M9OBJS) $(M9DOBJS) $(M10OBJS) \
	          $(M11OBJS); do \
	     case "$$o" in *savedata*) bad="$$bad $$o";; esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12 VERIFY FAILED: the M12-only savedata layer leaked into an"; \
	     echo "  earlier milestone's object list:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "savedata.o is in NO other target -- M0-M11 link sets unchanged"
	@echo "== M12 VERIFY OK =="

# ---- the Lua payload -------------------------------------------------------

M12_LUA_TEMPLATE := lua/m12.lua.in
M12_LUA_OUT      := $(M12BUILD)/$(M12TARGET).lua

m12-lua: m12-check $(M12BIN) $(M12ELF)
	@case "$(STAGE)" in \
	   0|1) ;; \
	   *) echo "M12 LUA FAILED: STAGE='$(STAGE)' is not 0 or 1."; \
	      echo "  0=PROBE (mount RW, write, commit, read back)"; \
	      echo "  1=VERIFY (read-only; mounts nothing, writes nothing)"; \
	      exit 1;; \
	 esac
	@$(PYTHON) tools/mklua.py $(M12_LUA_TEMPLATE) $(M12BIN) $(M12_LUA_OUT) $(M12ELF)
	@sed -i.bak "s/^local STAGE = .*/local STAGE = $(STAGE)/" $(M12_LUA_OUT)
	@rm -f $(M12_LUA_OUT).bak
	@if ! grep -q "^local STAGE = $(STAGE)$$" $(M12_LUA_OUT); then \
	     echo "M12 LUA FAILED: the STAGE line was not rewritten in"; \
	     echo "  $(M12_LUA_OUT) -- it would run STAGE 0 regardless of STAGE=$(STAGE)."; \
	     exit 1; \
	 fi
	@echo "M12 LUA OK: $(M12_LUA_OUT) -- STAGE $(STAGE)"

# ---- m12-send -- HARDWARE DELIVERY -----------------------------------------
#
# NOT AUTHORISED BY THE EXISTENCE OF THIS TARGET. Both stages build and transmit
# perfectly well -- that is deliberate -- but RUNNING either is an OPERATOR
# decision taken explicitly, after the offline chain is green.
#
#   * RUN STAGE 0 FIRST. It is under a second and it answers the only question
#     that blocks the other stage: can the container be mounted read-write,
#     written and committed at all?
#   * THEN RELAUNCH THE GAME AND RUN STAGE 1. That is the only way to prove the
#     bytes outlived the process that wrote them, which is the entire reason for
#     using savedata rather than /temp0.
#   * A -293 IS A RESULT, NOT A BUG. It means the sandbox refuses to CREATE
#     files inside savedata, and it decides the shape of M12B.
#   * IF THE GAME WILL NOT CLOSE FROM THE PS MENU AFTERWARDS, the read-only
#     mount was not restored -- check latch bit 7 and status -297. Power cycle
#     the console. Nothing is corrupted; it is a mount-state problem.
#   * M12A UPLOADS NOTHING AND NEEDS NO ROM OR BIOS. It never takes the display,
#     so it does not strand the 16 MB direct-memory reservation that forces a
#     relaunch between M1-M11 runs.
#
# WHY m12-lua IS A PREREQUISITE AND $(M12_LUA_OUT) IS NOT. m12-lua is PHONY and
# STAGE is a command-line variable, so a file prerequisite on $(M12_LUA_OUT)
# would be satisfied by a .lua left behind by a PREVIOUS stage and would ship the
# wrong stage silently. Depending on the PHONY target forces regeneration for the
# STAGE actually requested. STAGE validation is m12-lua's -- not duplicated here.
m12-send: m12-verify m12-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m12-send PS5_IP=<console address> STAGE=<0|1>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M12_LUA_OUT) $(M12BIN)

m12-clean:
	rm -rf $(M12BUILD)

.PHONY: m12 m12-objs m12-check m12-link m12-size m12-verify m12-lua \
        m12-send m12-clean

# =============================================================================
# M12B -- GBA CARTRIDGE SAVE PERSISTENCE (WRITE SIDE)
# =============================================================================
#
#   make m12b-check      offline C structural check of the new sources
#   make m12b-equiv      THE OFFLINE ADAPTER HARNESS -- run this first
#   make m12b-objs       compile only
#   make m12b-link       link only, and list anything still undefined
#   make m12b            build + link + print the image size report
#   make m12b-size       size report only (needs a prior m12b build)
#   make m12b-verify     every offline check for the M12B image
#   make m12b-lua        package the M12B delivery script only
#   make m12b-send       transmit M12B to a console -- REQUIRES PS5_IP
#   make m12b-clean      removes build/m12b ONLY
#
# PURELY ADDITIVE. Not one M0-M12A variable, rule, object list or target is
# modified by this block.
#
# M12B IS M11's IMAGE WITH THE OBSERVER SWAPPED FOR A PERSISTER. It takes M11's
# fifteen own objects, REMOVES m11main.o and gba_m11save.o, and adds four:
# m12bmain.o, gba_save.o, gba_savefile.o and savedata.o.
#
# ---- WHY gba_m11save.o IS DELIBERATELY NOT LINKED ----
#
# M12B owns its own baseline, its own exit hash and its own dirty accounting.
# Linking M11's observer would add a 128-block dirty map that nothing reads, and
# would put TWO DISAGREEING ANSWERS to "did the save change" in one image --
# M11's sticky evidence says a byte written and written back is dirty, while
# M12B's endpoint comparison correctly says there is nothing to commit. M11
# stays hardware-frozen and completely untouched, and m12b-verify asserts the
# object is absent from this link line.
#
# ---- WHY THE ADAPTER IS TWO OBJECTS WITH TWO DIFFERENT FLAG SETS ----
#
# THIS IS THE LOAD-BEARING PART OF THIS BLOCK.
#
#     gpsp/common.h:100   typedef unsigned long long int u64;
#     runtime/core.h:32   typedef unsigned long          u64;
#
# DISTINCT TYPES. runtime/savedata.h:4 includes runtime/core.h, so a single
# translation unit that both read gamepak_backup[] and called
# plat_savedata_begin_write() would fail on the duplicate typedef. Hence:
#
#     gba_save.o      built with $(M12B_GPSPFLAGS) -- sees gpsp/common.h,
#                     NEVER -Iruntime. Classifies, sizes, identifies, hashes.
#     gba_savefile.o  built with $(M12BFLAGS)      -- sees runtime/savedata.h,
#                     NEVER a gpSP header. Mounts, writes, flushes, commits.
#
# gba_savefile.o is therefore the ONLY adapter object in this repository built
# with the ordinary LUAport flags rather than gpSP's. That is not an oversight
# and m12b-verify proves the boundary held by asserting gba_savefile.o carries
# no relocation to any gpSP save global.
#
# The bytes cross through gba_save_chunk(), which is why adapters/gba/gba_save.h
# contains NO typedef and uses only `unsigned int`/`unsigned char *` -- types
# that mean the same thing on both sides.

M12BTARGET := m12bgpsp
M12BBUILD  := build/m12b
M12BELF    := $(M12BBUILD)/$(M12BTARGET).elf
M12BBIN    := $(M12BBUILD)/$(M12BTARGET).bin

# M11's include set with -Iapps/m12bgpsp substituted. -Iruntime is what lets the
# fixture and gba_savefile.c find savedata.h; -Iadapters/gba is what lets the
# fixture find gba_save.h and gba_savefile.h, both of which are deliberately
# typedef-free so they can be included from either side of the type boundary.
M12B_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m12bgpsp -Iadapters/gba

M12BFLAGS = $(BASEFLAGS) $(M12B_INCLUDES) -Wall -Wno-unused-function

# IDENTICAL TO M11's. M12B adds NO new compile flag and changes NO existing one.
# -DROM_BUFFER_SIZE=2 is what makes a cartridge over 2 MB demand paged, which is
# precisely why the save identity has to be latched BEFORE execution -- see
# gba_save_latch() in adapters/gba/gba_save.h.
M12B_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M12B_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

# $(HEADERS) IS EXTENDED LOCALLY RATHER THAN EDITED, for M12A's reason: adding
# runtime/savedata.h to the shared HEADERS variable would add a dependency to
# every M0-M12A rule and force a rebuild of images whose measured baselines this
# milestone is required not to disturb.
M12BHEADERS = $(HEADERS) runtime/savedata.h

M12BLDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
              -Wl,--build-id=none -Wl,-z,norelro \
              -Wl,--orphan-handling=error \
              -Wl,-Map=$(M12BBUILD)/$(M12BTARGET).map

# ----------------------------------------------------------------- objects --
#
# gba_save.o and gba_savefile.o ARE M12B-ONLY BY CONSTRUCTION. They appear in NO
# other target's object list -- M1OBJS..M12OBJS are untouched -- so no earlier
# milestone gains one byte from them. savedata.o is SHARED WITH M12A BY SOURCE
# but built into build/m12b/ separately, so neither image's measured baseline
# moves. m12b-verify proves both mechanically below.
#
# NOTE WHAT IS ABSENT: gba_m11save.o. See the note at the top of this block.
M12B_OWN_OBJS = $(M12BBUILD)/m12bmain.o \
                $(M12BBUILD)/shim.o \
                $(M12BBUILD)/savedata.o \
                $(M12BBUILD)/gba_fixture.o \
                $(M12BBUILD)/gba_filestream.o \
                $(M12BBUILD)/gba_compat.o \
                $(M12BBUILD)/gba_probe.o \
                $(M12BBUILD)/gba_bios.o \
                $(M12BBUILD)/gba_rom.o \
                $(M12BBUILD)/gba_m8map.o \
                $(M12BBUILD)/gba_m10map.o \
                $(M12BBUILD)/gba_save.o \
                $(M12BBUILD)/gba_savefile.o \
                $(M12BBUILD)/gba_exec.o \
                $(M12BBUILD)/gba_present.o \
                $(M12BBUILD)/gba_input.o \
                $(M12BBUILD)/platform.o

M12B_GPSP_OBJS = $(M12BBUILD)/gpsp_cpu.o $(M12BBUILD)/gpsp_main.o \
                 $(M12BBUILD)/gpsp_gba_memory.o $(M12BBUILD)/gpsp_video.o \
                 $(M12BBUILD)/gpsp_sound.o $(M12BBUILD)/gpsp_serial.o \
                 $(M12BBUILD)/gpsp_serial_proto.o $(M12BBUILD)/gpsp_rfu.o \
                 $(M12BBUILD)/gpsp_gbp.o $(M12BBUILD)/gpsp_cheats.o \
                 $(M12BBUILD)/gpsp_savestate.o $(M12BBUILD)/gpsp_input.o

M12BOBJS = $(M12B_OWN_OBJS) $(M12B_GPSP_OBJS)

# EXCLUDED, unchanged from M1-M12A and none of these may ever appear above:
#   cpu_threaded.c  memmap.c  gba_cc_lut.c  bios_data.S
#   libretro/libretro.c  libretro/libretro-common/*.c
#   x86/**  arm/**  mips/**  3ds/**  bios/**  tests/**  tools/**  jni/**
#   runtime/gfx.c  runtime/audio.c  adapters/gba/gba_audio.c
#   adapters/gba/gba_m11save.c   <-- M11-only, see the note above

$(M12BBUILD):
	mkdir -p $(M12BBUILD)

# ---- offline structural check ----------------------------------------------
#
# Same C structural checker M8-M12A use, pointed at the fixture AND at both new
# translation units. It is deliberately NOT pointed at lua/m12b.lua.in, for
# m9-check's reason: Lua comments start with --, which the tool does not know,
# so every apostrophe in a Lua comment would lex as an unterminated char literal
# and the check would FAIL ON CORRECT INPUT.
m12b-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m12bgpsp/main.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_save.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_savefile.c

# ---- THE OFFLINE ADAPTER HARNESS -------------------------------------------
#
# Builds and runs tools/gba_save_equiv.c with the HOST compiler, NOT the PS5
# toolchain. It #includes adapters/gba/gba_save.c VERBATIM, so it proves the
# SHIPPING adapter -- not a paraphrase of it -- classifies, sizes, identifies,
# bounds and REFUSES correctly, and stays PASSIVE.
#
# ***** RUN THIS BEFORE SPENDING ANY CONSOLE TIME. ***** At M11 the equivalent
# harness protected against a MISREPORT; here it protects against a WRONG FILE
# ON DISK, which the operator cannot detect and cannot undo. The three
# properties that matter most:
#
#   THE UNIT-COUNT TRAP. eeprom_size and flash_bank_cnt are UNIT COUNTS, not
#   byte counts (EEPROM_8_KBYTE is 16, FLASH_SIZE_128KB is 2 --
#   gpsp/gba_memory.h:294-298). An adapter that echoed either would write a
#   SIXTEEN BYTE save file for an 8 KB EEPROM and it would look entirely
#   plausible.
#
#   THE HEADER IS CHECKED BYTE BY BYTE against a hand-written expected array.
#   That is only possible because gba_save_header_build() lives on the gpSP side
#   of the type boundary -- the runtime half CANNOT be compiled by this harness
#   at all, because runtime/savedata.h chains to runtime/core.h whose u64
#   typedef collides with the one the harness must supply for gpSP.
#
#   THE DIRTY MODEL. A cartridge that RECLASSIFIES mid-run without changing a
#   byte must be reported CLEAN. A region-sized baseline would compare two
#   different ranges and report DIRTY, and M12B would commit a save the game
#   never made.
#
# -I gpsp IS REQUIRED AND IS NOT A gpSP DEPENDENCY. gba_save.c opens with
# #include "common.h". The harness pre-defines that header's guard (COMMON_H,
# gpsp/common.h:20-21) BEFORE including the production .c, so the body expands
# to NOTHING -- but the preprocessor must still FIND the file before it can skip
# it, and tools/ is on no gpSP search path.
#
# This changes the BUILD RULE ONLY. adapters/gba/gba_save.c is not modified to
# suit the harness, nothing is copied out of gpsp/, and no test-only #ifdef
# exists in the production source. gba_save_equiv stays absent from every object
# list -- $(M12BOBJS) does not contain it and no payload rule builds it.
m12b-equiv: | $(M12BBUILD)
	@$(HOSTCC) -std=c11 -O2 -Wall -Wextra -I adapters/gba -I gpsp \
	    -o $(M12BBUILD)/gba_save_equiv tools/gba_save_equiv.c
	@$(M12BBUILD)/gba_save_equiv

# ---- LUAport-owned objects, RUNTIME side -----------------------------------

$(M12BBUILD)/m12bmain.o: apps/m12bgpsp/main.c adapters/gba/gba_probe.h \
                         adapters/gba/gba_bios.h adapters/gba/gba_rom.h \
                         adapters/gba/gba_exec.h adapters/gba/gba_present.h \
                         adapters/gba/gba_input.h adapters/gba/gba_m8map.h \
                         adapters/gba/gba_m10map.h \
                         adapters/gba/gba_save.h adapters/gba/gba_savefile.h \
                         $(M12BHEADERS) | $(M12BBUILD)
	$(CC) $(M12BFLAGS) -c $< -o $@

$(M12BBUILD)/shim.o: runtime/shim.c $(M12BHEADERS) | $(M12BBUILD)
	$(CC) $(M12BFLAGS) -c $< -o $@

# M12A's SAVEDATA LAYER, COMPILED FROM BYTE-IDENTICAL, UNMODIFIED SOURCE.
# Built into build/m12b/ rather than reused from build/m12/ so neither image's
# measured baseline depends on the other having been built.
$(M12BBUILD)/savedata.o: runtime/savedata.c runtime/savedata.h $(M12BHEADERS) | $(M12BBUILD)
	$(CC) $(M12BFLAGS) -c $< -o $@

# M0's PS5 VideoOut AND PAD layer, COMPILED FROM UNMODIFIED SOURCE.
$(M12BBUILD)/platform.o: runtime/platform.c $(M12BHEADERS) | $(M12BBUILD)
	$(CC) $(M12BFLAGS) -c $< -o $@

$(M12BBUILD)/gba_present.o: adapters/gba/gba_present.c adapters/gba/gba_present.h \
                            runtime/core.h | $(M12BBUILD)
	$(CC) $(M12BFLAGS) -c $< -o $@

# ***** M12B's RUNTIME-SIDE SAVE ADAPTER. THE ONLY ADAPTER OBJECT IN THIS
# REPOSITORY BUILT WITH THE ORDINARY LUAport FLAGS RATHER THAN gpSP's. *****
#
# IT NEEDS $(M12BFLAGS) because it includes runtime/savedata.h to reach
# plat_savedata_begin_write/end_write/mkdir, and runtime/libc/stdio.h for
# fopen/fwrite/fflush/fclose.
#
# IT MUST NOT GET $(M12B_GPSPFLAGS), whose include path would let it reach
# gpsp/common.h -- and gpsp/common.h:100 typedefs u64 as `unsigned long long`
# while runtime/core.h:32 says `unsigned long`. A TU that saw both would not
# compile. That collision is the entire reason this adapter is two files.
#
# `gba_save.h` and `gba_savefile.h` are found without any -I because they sit
# beside the .c file. gba_save.h is deliberately TYPEDEF-FREE, which is what
# makes it safe to include from this side as well as from the gpSP side.
$(M12BBUILD)/gba_savefile.o: adapters/gba/gba_savefile.c \
                             adapters/gba/gba_savefile.h \
                             adapters/gba/gba_save.h \
                             runtime/savedata.h | $(M12BBUILD)
	$(CC) $(M12BFLAGS) -c $< -o $@

# ---- LUAport-owned objects, gpSP side --------------------------------------

# ***** M12B's gpSP-SIDE SAVE ADAPTER. NEW, and linked ONLY here. *****
#
# COMPILED WITH THE gpSP FLAGS AND NOTHING ELSE, exactly like gba_rom.o and
# gba_m10map.o, and that is load-bearing in BOTH directions:
#
#   IT NEEDS gpSP's FLAGS because it includes gpsp/common.h to reach
#   backup_type, gamepak_backup[], gamepak_buffers[] and the
#   BACKUP_*/FLASH_*/EEPROM_* constants, so it must see the same gpSP
#   configuration every other gpSP object sees.
#
#   IT MUST NOT GET $(M12B_INCLUDES), whose -Iruntime would put runtime/core.h
#   on the include path and produce the duplicate u64 typedef described above.
$(M12BBUILD)/gba_save.o: adapters/gba/gba_save.c adapters/gba/gba_save.h | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

# M8's keypad adapter, compiled from BYTE-IDENTICAL, UNMODIFIED source.
$(M12BBUILD)/gba_input.o: adapters/gba/gba_input.c adapters/gba/gba_input.h | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

# M4's cartridge adapter, REUSED UNMODIFIED.
$(M12BBUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gba_m8map.o: adapters/gba/gba_m8map.c adapters/gba/gba_m8map.h | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gba_m10map.o: adapters/gba/gba_m10map.c adapters/gba/gba_m10map.h | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects -----------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, IDENTICAL to M11's.
# gpsp/** is never modified and its own Makefile is never invoked. M12B needed
# ZERO changes here.

$(M12BBUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M12BBUILD)
	$(CXX) $(M12B_GPSPCXXFLAGS) -c $< -o $@

$(M12BBUILD)/gpsp_video.o: gpsp/video.cc | $(M12BBUILD)
	$(CXX) $(M12B_GPSPCXXFLAGS) -c $< -o $@

$(M12BBUILD)/gpsp_main.o: gpsp/main.c | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gpsp_sound.o: gpsp/sound.c | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gpsp_serial.o: gpsp/serial.c | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

$(M12BBUILD)/gpsp_input.o: gpsp/input.c | $(M12BBUILD)
	$(CC) $(M12B_GPSPFLAGS) -c $< -o $@

m12b-objs: $(M12BOBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- exactly as M1-M12A.
# Every object is named on the link line and is included in its ENTIRETY.
$(M12BELF): $(M12BOBJS) runtime/linker.ld | $(M12BBUILD)
	$(CC) $(BASEFLAGS) $(M12BLDFLAGS) -o $@ $(M12BOBJS)

m12b-link: $(M12BOBJS) | $(M12BBUILD)
	@$(CC) $(BASEFLAGS) $(M12BLDFLAGS) -o $(M12BELF) $(M12BOBJS) \
	     > $(M12BBUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(M12BBUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M12BBUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M12B LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M12B LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M12B LINK OK: linker exit 0, no undefined references"

# --arena 4194304, IDENTICAL TO M4's through M11's.
$(M12BBIN): $(M12BELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M12BELF) $@ \
	   --map $(M12BBUILD)/$(M12BTARGET).map --budget $(JIT_LIMIT) --arena 4194304

m12b: $(M12BBIN)

# THE TWO HARD CEILINGS. sizereport.py enforces the JIT budget ($(JIT_LIMIT) =
# 524,288) and tools/check_forbidden.py enforces the 2,097,152-byte .bss
# tripwire.
#
# M12B's STATIC FOOTPRINT IS DELIBERATELY TINY -- under 200 bytes of adapter
# state in total. The alternative considered and REJECTED was staging the save
# in a 128 KB shadow copy of gamepak_backup[] before writing it; instead the
# .sav is STREAMED to disk 4096 bytes at a time through a STACK buffer sized to
# match runtime/shim.c's WBUF_SIZE, so the payload costs ZERO bytes of .bss.
#
# M12B SHOULD BE CLOSE TO M11 IN SIZE: it adds gba_save.o, gba_savefile.o and
# savedata.o and drops gba_m11save.o.
#
# IF EITHER CEILING IS EXCEEDED, STOP AND REPORT IT. Do not shrink unrelated
# code to make room: the size of the M12B image is one of the results.
m12b-size: $(M12BELF) $(M12BBIN)
	@$(PYTHON) tools/sizereport.py $(M12BELF) $(M12BBIN) \
	   --map $(M12BBUILD)/$(M12BTARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification ----------------------------------------------------------
#
# EVERY M11 GATE IS KEPT and M12A's SD_CREATE gate is added. The VideoOut
# allowlist, the PAD allowlist, the sceGnm prohibition, the sceAudioOut
# prohibition, the marker-based rodata carve and the adapter isolation checks
# all carry over VERBATIM.
#
# ---- WHAT M12B ADDS, AND WHY EACH ONE IS NECESSARY ----
#
#   THE ADAPTER IS STILL PASSIVE, PROVED AT LINK TIME. gba_save.o MUST NOT
#   reference read_backup, write_backup, read_eeprom or write_eeprom. Those four
#   are part of gpSP's DETECTOR -- backup_type collapses from BACKUP_UNKN to
#   BACKUP_SRAM on the first backup-region access (gba_memory.c:465, :1141) --
#   so an adapter that called one would MANUFACTURE the class it then
#   PERSISTED, and every unexercised cartridge would be written out as a 32 KB
#   SRAM file full of 0xFF. This mattered at M11; here it decides what lands on
#   disk, so it is the single most important gate in the milestone.
#
#   THE ADAPTER REALLY READS gpSP's SAVE STATE. gba_save.o must reference
#   backup_type, gamepak_backup, flash_bank_cnt and eeprom_size. An adapter that
#   does not read the emulator's real save globals would persist a copy of
#   nothing, and its clean report would be VACUOUS.
#
#   ***** THE TYPE BOUNDARY HELD. ***** gba_savefile.o must reference NONE of
#   those four globals. It is compiled with the RUNTIME flags and must never
#   have reached a gpSP header; this is the link-time proof that the two-object
#   split is real rather than merely intended, and that every save byte crosses
#   through gba_save_chunk().
#
#   ***** THE WRITE WINDOW HAS EXACTLY ONE OWNER. ***** gba_savefile.o must
#   reference plat_savedata_begin_write and plat_savedata_end_write, and
#   m12bmain.o must reference NEITHER. The commit is guaranteed to run on every
#   path out of the window by the STRUCTURE of gba_savefile_commit() -- begin,
#   call, end -- and a fixture that could open or close the window itself would
#   destroy that guarantee. Skipping the restore leaves the console unable to
#   close the game from the PS menu.
#
#   THE FIXTURE GOES THROUGH THE ADAPTERS. m12bmain.o must reference
#   gba_save_latch, gba_save_finish and gba_savefile_commit, and must carry NO
#   backup_type / gamepak_backup relocation of its own.
#
#   gpSP DID NOT LEARN ABOUT M12B. No gpsp_*.o may reference any gba_save_ or
#   gba_savefile_ symbol. This is the link-time proof that M12B required ZERO
#   changes to gpsp/**.
#
#   M11's OBSERVER IS NOT LINKED. gba_m11save.o must be absent from the M12B
#   link line -- see the note at the top of this block.
#
#   M12B's OBJECTS LEAKED INTO NOTHING. gba_save.o and gba_savefile.o must
#   appear in no earlier milestone's object list.
#
# WHY THE RELOCATION GATE IS NOT WRITTEN "verify_reloc.py ... | tail -12" LIKE
# M0-M11: a pipeline's exit status in POSIX sh is the status of the LAST
# command, so `tail` -- which always succeeds -- discards the checker's. M12A
# was the first image to make that checker actually fail, which is how the
# masking was found. The status is captured before anything else runs and the
# output is trimmed from a log file instead. `set -o pipefail` would be the
# one-word fix but it is a bashism, and setting it repository-wide would
# silently make every milestone's currently-advisory gates fatal at once.
# M0-M11 keep their existing call sites.

M12B_VIDEOOUT_ALLOW = sceVideoOutOpen sceVideoOutClose sceVideoOutRegisterBuffers \
                      sceVideoOutSubmitFlip sceVideoOutSetFlipRate \
                      sceVideoOutAddFlipEvent

M12B_PAD_ALLOW = scePadInit scePadGetHandle scePadRead

# THE FOUR gpSP SAVE-PATH ENTRY POINTS THAT MUST NEVER BE CALLED.
M12B_FORBIDDEN_SAVE = read_backup write_backup read_eeprom write_eeprom

# The gpSP save globals. gba_save.o MUST read them; gba_savefile.o and
# m12bmain.o MUST NOT.
M12B_SAVE_GLOBALS = backup_type gamepak_backup flash_bank_cnt eeprom_size

M12B_REQUIRED_NAMES = libSceSaveData.sprx sceSaveDataUmount sceKernelMkdir \
                      sceKernelStat sceKernelUsleep SLUS-20268 SLES-50366

m12b-verify: $(M12BELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M12BELF)
	@echo "== relocations (NON-MASKING pipeline -- see the note above) =="
	@$(PYTHON) tools/verify_reloc.py $(M12BELF) $(M12BOBJS) \
	     > $(M12BBUILD)/reloc.log 2>&1; \
	 st=$$?; \
	 tail -12 $(M12BBUILD)/reloc.log; \
	 if [ $$st -ne 0 ]; then \
	     echo "M12B VERIFY FAILED: the relocation check exited $$st."; \
	     echo "  Full output: $(M12BBUILD)/reloc.log"; \
	     exit $$st; \
	 fi
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M12BOBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M12BOBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M12BELF) $(M12BOBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M12BELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M12B VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== SD_CREATE IS NEVER DEFINED (M12A's gate, unchanged) =="
	@if grep -nE "^[[:space:]]*#[[:space:]]*define[[:space:]]+SD_CREATE" \
	        runtime/savedata.c runtime/savedata.h \
	        adapters/gba/gba_savefile.c adapters/gba/gba_savefile.h; then \
	     echo "M12B VERIFY FAILED: SD_CREATE is DEFINED."; \
	     echo "  With a wrong dirName that flag silently builds a SECOND, EMPTY"; \
	     echo "  container and mounts THAT, so the write appears to succeed and"; \
	     echo "  goes nowhere. A failure that REPORTS SUCCESS is the worst"; \
	     echo "  outcome this milestone can produce. This is not a style rule."; \
	     exit 1; \
	 fi; \
	 echo "SD_CREATE is not defined in the savedata layer or the save adapter"
	@echo "== rodata carve (marker-based, as M7-M12A) =="
	@$(OBJCOPY) -O binary --only-section=.text $(M12BELF) $(M12BBUILD)/text.bin
	@tx=$$($(NM) $(M12BELF) | awk '$$NF == "__text_start"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 ra=$$($(NM) $(M12BELF) | awk '$$NF == "__rodata_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 rz=$$($(NM) $(M12BELF) | awk '$$NF == "__rodata_end"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 if [ -z "$$tx" ] || [ -z "$$ra" ] || [ -z "$$rz" ]; then \
	     echo "M12B VERIFY FAILED: linker marker symbol missing -- need"; \
	     echo "  __text_start, __rodata_start and __rodata_end in $(M12BELF)."; \
	     exit 1; \
	 fi; \
	 t=$$((0x$$tx)); a=$$((0x$$ra)); z=$$((0x$$rz)); n=$$((z - a)); \
	 if [ "$$t" -ne 0 ]; then \
	     echo "M12B VERIFY FAILED: .text does not start at vaddr 0 (__text_start ="; \
	     echo "  $$t), so a symbol value is no longer a file offset in the"; \
	     echo "  extracted binary and this carve would read the wrong bytes."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M12B VERIFY FAILED: empty rodata window -- __rodata_start=$$a"; \
	     echo "  __rodata_end=$$z gives $$n bytes."; \
	     exit 1; \
	 fi; \
	 have=$$(wc -c < $(M12BBUILD)/text.bin | tr -d " "); \
	 if [ "$$have" -lt "$$z" ]; then \
	     echo "M12B VERIFY FAILED: .text extraction is $$have bytes but the rodata"; \
	     echo "  window ends at $$z -- the extraction is short or .text is absent."; \
	     exit 1; \
	 fi; \
	 dd if=$(M12BBUILD)/text.bin of=$(M12BBUILD)/rodata.bin bs=1 skip=$$a count=$$n \
	    2>/dev/null || { \
	     echo "M12B VERIFY FAILED: dd could not carve the rodata window."; exit 1; \
	 }; \
	 if [ ! -s $(M12BBUILD)/rodata.bin ]; then \
	     echo "M12B VERIFY FAILED: rodata extraction produced 0 bytes -- every check"; \
	     echo "  below would pass vacuously. Fix the extraction, not the checks."; \
	     exit 1; \
	 fi; \
	 got=$$(wc -c < $(M12BBUILD)/rodata.bin | tr -d " "); \
	 if [ "$$got" -ne "$$n" ]; then \
	     echo "M12B VERIFY FAILED: rodata.bin is $$got bytes, expected $$n ="; \
	     echo "  __rodata_end - __rodata_start. Extraction is not trustworthy."; \
	     exit 1; \
	 fi; \
	 echo "rodata carved from .text by linker markers: [$$a,$$z) = $$n bytes"
	@echo "== PRESENTATION ALLOWLIST (carried from M7-M11) =="
	@for s in $(M12B_VIDEOOUT_ALLOW) libSceVideoOut.sprx; do \
	     if ! grep -aq "$$s" $(M12BBUILD)/rodata.bin; then \
	         echo "M12B VERIFY FAILED: required presentation name '$$s' is NOT in"; \
	         echo "  the image."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 6 VideoOut entry points + libSceVideoOut.sprx are present"
	@allow=$$(echo $(M12B_VIDEOOUT_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "sceVideoOut[A-Za-z0-9_]*" $(M12BBUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M12B VERIFY FAILED: an UNEXPECTED sceVideoOut* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no sceVideoOut* name outside the six-symbol allowlist"
	@echo "== THE GRAPHICS-PIPELINE PROHIBITION IS UNCHANGED =="
	@if grep -aqE "sceGnm" $(M12BBUILD)/rodata.bin; then \
	     echo "M12B VERIFY FAILED: a graphics-pipeline name reached the image:"; \
	     grep -aoE "sceGnm[A-Za-z0-9_]*" $(M12BBUILD)/rodata.bin \
	        | sort -u | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no sceGnm* name is present (NO GNM -- unchanged from M6-M12A)"
	@echo "== PAD ALLOWLIST (carried from M8-M11) =="
	@for s in $(M12B_PAD_ALLOW) libScePad.sprx; do \
	     if ! grep -aq "$$s" $(M12BBUILD)/rodata.bin; then \
	         echo "M12B VERIFY FAILED: required pad name '$$s' is NOT in the image."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 3 scePad entry points + libScePad.sprx are present"
	@allow=$$(echo $(M12B_PAD_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "scePad[A-Za-z0-9_]*" $(M12BBUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M12B VERIFY FAILED: an UNEXPECTED scePad* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no scePad* name outside the three-symbol allowlist"
	@echo "== THE AUDIO PROHIBITION IS UNCHANGED (M11's, kept) =="
	@if grep -aqE "sceAudioOut" $(M12BBUILD)/rodata.bin; then \
	     echo "M12B VERIFY FAILED: an audio name reached the image:"; \
	     grep -aoE "sceAudioOut[A-Za-z0-9_]*" $(M12BBUILD)/rodata.bin \
	        | sort -u | sed "s/^/  /"; \
	     echo "  M12B links neither runtime/audio.o nor gba_audio.o, so no such"; \
	     echo "  name can be present unless an object list changed."; \
	     exit 1; \
	 fi; \
	 echo "no sceAudioOut* name is present"
	@echo "== THE SAVEDATA LAYER REALLY REACHED THE IMAGE =="
	@for s in $(M12B_REQUIRED_NAMES); do \
	     if ! grep -aq "$$s" $(M12BBUILD)/rodata.bin; then \
	         echo "M12B VERIFY FAILED: required savedata name '$$s' is NOT in the"; \
	         echo "  image. A milestone that cannot name the module it mounts is"; \
	         echo "  not mounting anything, and a clean run would be VACUOUS."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "libSceSaveData.sprx, sceSaveDataUmount, both region dirNames and the"; \
	 echo "  three kernel entry points are all present"
	@echo "== M12B NAMES THE FILES IT WRITES =="
	@miss=""; \
	 for s in "/savedata0/gba" ".sav" ".hdr"; do \
	     grep -aq "$$s" $(M12BBUILD)/rodata.bin || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M12B VERIFY FAILED: the image does not contain:$$miss"; \
	     echo "  M12B is the milestone that WRITES a .sav. If the directory or"; \
	     echo "  either extension is absent, no filename can be built, nothing"; \
	     echo "  can be written, and a clean run would be VACUOUS."; \
	     exit 1; \
	 fi; \
	 echo "/savedata0/gba, .sav and .hdr are all present in rodata"
	@echo "  NOTE the LGS1 header magic is deliberately NOT grepped for here."
	@echo "  gba_save.c writes it as four SEPARATE BYTE STORES from the"
	@echo "  GBA_SAVE_MAGIC_0..3 character constants, precisely so the on-disk"
	@echo "  layout cannot depend on struct padding or on target endianness --"
	@echo "  so no 'LGS1' string literal exists in rodata to find. A gate"
	@echo "  grepping for it would FAIL ON CORRECT INPUT, which is the same"
	@echo "  defect class as the M11 check_symtypes false positive and exactly"
	@echo "  the kind of check that later gets 'fixed' by weakening the code it"
	@echo "  was meant to protect. The magic is verified BYTE BY BYTE against a"
	@echo "  hand-written expected array by 'make m12b-equiv' instead, which is"
	@echo "  a strictly stronger check than a substring search."
	@echo "== THE PASSIVITY GATE -- THE MOST IMPORTANT CHECK IN M12B =="
	@bad=""; \
	 for s in $(M12B_FORBIDDEN_SAVE); do \
	     if $(NM) -u $(M12BBUILD)/gba_save.o 2>/dev/null | grep -qw "$$s"; then \
	         bad="$$bad $$s"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12B VERIFY FAILED: the save adapter references gpSP's SAVE-PATH"; \
	     echo "  entry points:$$bad"; \
	     echo "  Those four are part of gpSP's DETECTOR, not its reporting"; \
	     echo "  surface: backup_type collapses from BACKUP_UNKN to BACKUP_SRAM"; \
	     echo "  on the first backup-region access (gba_memory.c:465, :1141)."; \
	     echo "  An adapter that calls one MANUFACTURES the class it then"; \
	     echo "  PERSISTS, and EVERY unexercised cartridge would be written out"; \
	     echo "  as a 32 KB SRAM file full of 0xFF. This is not a style rule."; \
	     exit 1; \
	 fi; \
	 echo "gba_save.o calls NONE of read_backup/write_backup/read_eeprom/write_eeprom"
	@bad=""; \
	 for o in $(M12BBUILD)/m12bmain.o $(M12BBUILD)/gba_savefile.o; do \
	     for s in $(M12B_FORBIDDEN_SAVE); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12B VERIFY FAILED: the fixture or the file writer references"; \
	     echo "  gpSP's save-path entry points:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "neither m12bmain.o nor gba_savefile.o calls any of them either"
	@echo "== THE ADAPTER READS gpSP's REAL SAVE STATE =="
	@miss=""; \
	 for s in $(M12B_SAVE_GLOBALS); do \
	     $(NM) -u $(M12BBUILD)/gba_save.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M12B VERIFY FAILED: gba_save.o does NOT reference:$$miss"; \
	     echo "  An adapter that does not read the emulator's real save globals"; \
	     echo "  would persist a copy of nothing, and its clean report would be"; \
	     echo "  VACUOUS rather than informative."; \
	     exit 1; \
	 fi; \
	 echo "gba_save.o reads gpSP's real save state (backup_type,"; \
	 echo "  gamepak_backup, flash_bank_cnt, eeprom_size)"
	@echo "== ***** THE TYPE BOUNDARY HELD ***** =="
	@bad=""; \
	 for s in $(M12B_SAVE_GLOBALS); do \
	     if $(NM) -u $(M12BBUILD)/gba_savefile.o 2>/dev/null | grep -qw "$$s"; then \
	         bad="$$bad $$s"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12B VERIFY FAILED: gba_savefile.o references a gpSP save"; \
	     echo "  global:$$bad"; \
	     echo "  That object is compiled with the RUNTIME flags and must never"; \
	     echo "  have reached a gpSP header -- gpsp/common.h:100 typedefs u64 as"; \
	     echo "  'unsigned long long' and runtime/core.h:32 as 'unsigned long',"; \
	     echo "  which are DISTINCT TYPES. Every save byte must cross through"; \
	     echo "  gba_save_chunk(). This is the proof the two-object split is"; \
	     echo "  REAL and not merely intended."; \
	     exit 1; \
	 fi; \
	 echo "gba_savefile.o names NO gpSP save global -- the bytes cross through"; \
	 echo "  gba_save_chunk() and nowhere else"
	@echo "== ***** THE WRITE WINDOW HAS EXACTLY ONE OWNER ***** =="
	@miss=""; \
	 for s in plat_savedata_begin_write plat_savedata_end_write \
	          plat_savedata_mkdir plat_savedata_is_ready; do \
	     $(NM) -u $(M12BBUILD)/gba_savefile.o 2>/dev/null | grep -q "$$s" \
	        || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M12B VERIFY FAILED: gba_savefile.o does NOT reference:$$miss"; \
	     echo "  It is the sole owner of the write window and must drive all of"; \
	     echo "  them."; \
	     exit 1; \
	 fi; \
	 echo "gba_savefile.o owns begin_write, end_write, mkdir and is_ready"
	@bad=""; \
	 for s in plat_savedata_begin_write plat_savedata_end_write; do \
	     if $(NM) -u $(M12BBUILD)/m12bmain.o 2>/dev/null | grep -qw "$$s"; then \
	         bad="$$bad $$s"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12B VERIFY FAILED: the FIXTURE opens or closes the write window"; \
	     echo "  itself:$$bad"; \
	     echo "  The commit is guaranteed to run on EVERY path out of the window"; \
	     echo "  by the STRUCTURE of gba_savefile_commit() -- begin, call, end."; \
	     echo "  A fixture that could open or close the window itself destroys"; \
	     echo "  that guarantee, and skipping the read-only restore leaves the"; \
	     echo "  console unable to close the game from the PS menu."; \
	     exit 1; \
	 fi; \
	 echo "m12bmain.o opens NO write window of its own"
	@echo "== THE FIXTURE DRIVES THE ADAPTERS, AND ONLY THROUGH THEM =="
	@miss=""; \
	 for s in gba_save_latch gba_save_finish gba_savefile_commit \
	          plat_savedata_init; do \
	     $(NM) -u $(M12BBUILD)/m12bmain.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M12B VERIFY FAILED: m12bmain.o does NOT reference:$$miss"; \
	     echo "  The identity is not actually being latched before execution, the"; \
	     echo "  exit hash is not being taken, or the commit is never reached."; \
	     exit 1; \
	 fi; \
	 echo "m12bmain.o drives the latch, the exit measurement AND the commit"
	@bad=""; \
	 for s in $(M12B_SAVE_GLOBALS); do \
	     if $(NM) -u $(M12BBUILD)/m12bmain.o 2>/dev/null | grep -qw "$$s"; then \
	         bad="$$bad $$s"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12B VERIFY FAILED: m12bmain.o reads gpSP's save globals"; \
	     echo "  DIRECTLY:$$bad"; \
	     echo "  Everything must go through the adapter, or its passivity"; \
	     echo "  guarantee covers nothing that matters."; \
	     exit 1; \
	 fi; \
	 echo "m12bmain.o reaches gpSP's save state ONLY through the adapter"
	@if $(NM) -u $(M12BBUILD)/m12bmain.o 2>/dev/null | grep -q "sceSaveData"; then \
	     echo "M12B VERIFY FAILED: m12bmain.o carries a sceSaveData* name of its"; \
	     echo "  own -- the fixture is reaching around the savedata layer."; \
	     exit 1; \
	 fi; \
	 echo "m12bmain.o names no sceSaveData* symbol -- it goes through the layer"
	@echo "== gpSP DID NOT LEARN ABOUT M12B =="
	@bad=""; \
	 for o in $(M12B_GPSP_OBJS); do \
	     if $(NM) -u $$o 2>/dev/null | grep -qE "gba_save_|gba_savefile_"; then \
	         bad="$$bad $$o"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12B VERIFY FAILED: a gpSP object references the save adapter:$$bad"; \
	     echo "  M12B requires ZERO changes to gpsp/** and this is the proof."; \
	     exit 1; \
	 fi; \
	 echo "no gpsp_*.o references any gba_save_ or gba_savefile_ symbol"
	@echo "== M11's OBSERVER IS NOT LINKED =="
	@bad=""; \
	 for o in $(M12BOBJS); do \
	     case "$$o" in *gba_m11save*) bad="$$bad $$o";; esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12B VERIFY FAILED: M11's save observer is in the M12B link"; \
	     echo "  line:$$bad"; \
	     echo "  M12B owns its own baseline, its own exit hash and its own dirty"; \
	     echo "  accounting. Linking the observer would add a 128-block dirty map"; \
	     echo "  that nothing reads, and would put TWO DISAGREEING ANSWERS to"; \
	     echo "  'did the save change' in one image."; \
	     exit 1; \
	 fi; \
	 echo "gba_m11save.o is NOT in the M12B link line -- M11 stays frozen"
	@echo "== NO AUDIO OBJECT IS LINKED =="
	@bad=""; \
	 for o in $(M12BOBJS); do \
	     case "$$o" in *gba_audio*|*/audio.o) bad="$$bad $$o";; esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12B VERIFY FAILED: an audio object is in the M12B link line:$$bad"; \
	     echo "  M12B has no audio path and its verifier asserts the sceAudioOut"; \
	     echo "  PROHIBITION -- linking one would make that gate a lie."; \
	     exit 1; \
	 fi; \
	 echo "neither gba_audio.o nor audio.o is in the M12B object list"
	@echo "== THE SAVE ADAPTER LEAKED INTO NO EARLIER MILESTONE =="
	@bad=""; \
	 for o in $(OBJS) $(M1OBJS) $(M2OBJS) $(M3OBJS) $(M4OBJS) $(M5OBJS) \
	          $(M6OBJS) $(M7OBJS) $(M8OBJS) $(M9OBJS) $(M9DOBJS) $(M10OBJS) \
	          $(M11OBJS) $(M12OBJS); do \
	     case "$$o" in \
	       *gba_save.o|*gba_savefile.o) bad="$$bad $$o";; \
	     esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12B VERIFY FAILED: the M12B-only save adapter leaked into an"; \
	     echo "  earlier milestone's object list:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "gba_save.o and gba_savefile.o are in NO other target --"; \
	 echo "  M0-M12A link sets unchanged"
	@echo "== M12B VERIFY OK =="

# ---- the Lua payload -------------------------------------------------------

M12B_LUA_TEMPLATE := lua/m12b.lua.in
M12B_LUA_OUT      := $(M12BBUILD)/$(M12BTARGET).lua

m12b-lua: m12b-check $(M12BBIN) $(M12BELF)
	@case "$(STAGE)" in \
	   0|1) ;; \
	   *) echo "M12B LUA FAILED: STAGE='$(STAGE)' is not 0 or 1."; \
	      echo "  0=DRYRUN  (3 s, no frame loop, NO WRITE WINDOW EVER)"; \
	      echo "  1=PERSIST (40 s, commits on a clean exit)"; \
	      exit 1;; \
	 esac
	@$(PYTHON) tools/mklua.py $(M12B_LUA_TEMPLATE) $(M12BBIN) $(M12B_LUA_OUT) $(M12BELF)
	@sed -i.bak "s/^local STAGE = .*/local STAGE = $(STAGE)/" $(M12B_LUA_OUT)
	@rm -f $(M12B_LUA_OUT).bak
	@if ! grep -q "^local STAGE = $(STAGE)$$" $(M12B_LUA_OUT); then \
	     echo "M12B LUA FAILED: the STAGE line was not rewritten in"; \
	     echo "  $(M12B_LUA_OUT) -- it would run STAGE 0 regardless of STAGE=$(STAGE)."; \
	     exit 1; \
	 fi
	@echo "M12B LUA OK: $(M12B_LUA_OUT) -- STAGE $(STAGE)"

# ---- m12b-send -- HARDWARE DELIVERY ----------------------------------------
#
# NOT AUTHORISED BY THE EXISTENCE OF THIS TARGET. Both stages build and transmit
# perfectly well -- that is deliberate -- but RUNNING either is an OPERATOR
# decision taken explicitly, after the offline chain is green.
#
#   * RUN `make m12b-equiv` FIRST. It costs seconds and it is the only check
#     that can catch a wrong SIZE, a wrong IDENTITY or a wrong HEADER before
#     those become a wrong FILE that the operator cannot detect and cannot undo.
#   * RUN STAGE 0 FIRST ON HARDWARE. It is three seconds, it never enters the
#     frame loop, and it NEVER OPENS A WRITE WINDOW -- so it cannot damage a
#     container. It answers the questions that block the other stage: did the
#     identity latch, what class is this cartridge, and what filename would be
#     used?
#   * ***** TO GET A SAVE WRITTEN, SAVE IN-GAME DURING STAGE 1. ***** A
#     cartridge that never saves has nothing to persist, and M12B will correctly
#     decline to write. THAT IS A PASS, not a failure.
#   * RELAUNCH THE HOST GAME BETWEEN EVERY RUN. plat_video_init() reserves 16 MB
#     of direct memory and DELIBERATELY never releases it, so a second run in
#     one session fails with PLAT_VID_EALLOC (-24).
#   * M12B reuses M3's BIOS and M4's cartridge. Neither is re-uploaded, and
#     there is deliberately no m12b-upload-rom: the save identity is a property
#     of the CARTRIDGE, so which cartridge is present must stay an operator
#     variable.
#   * IF THE GAME WILL NOT CLOSE FROM THE PS MENU AFTERWARDS, the read-only
#     mount was not restored -- check status -307 and write-path bit 6. Power
#     cycle the console. NOTHING IS CORRUPTED; it is a mount-state problem.
#
# WHY m12b-lua IS A PREREQUISITE AND $(M12B_LUA_OUT) IS NOT. m12b-lua is PHONY
# and STAGE is a command-line variable, so a file prerequisite on
# $(M12B_LUA_OUT) would be satisfied by a .lua left behind by a PREVIOUS stage
# and would ship the wrong stage silently -- which at M12B means shipping a
# stage that WRITES when the operator asked for one that does not. Depending on
# the PHONY target forces regeneration for the STAGE actually requested.
m12b-send: m12b-verify m12b-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m12b-send PS5_IP=<console address> STAGE=<0|1>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M12B_LUA_OUT) $(M12BBIN)

m12b-clean:
	rm -rf $(M12BBUILD)

.PHONY: m12b m12b-objs m12b-check m12b-equiv m12b-link m12b-size m12b-verify \
        m12b-lua m12b-send m12b-clean

# =============================================================================
# M12C -- GBA CARTRIDGE SAVE RESTORE (READ SIDE)
# =============================================================================
#
#   make m12c-check      offline C structural check of the new sources
#   make m12c-equiv      THE OFFLINE ADAPTER HARNESS -- run this first
#   make m12c-objs       compile only
#   make m12c-link       link only, and list anything still undefined
#   make m12c            build + link + print the image size report
#   make m12c-size       size report only (needs a prior m12c build)
#   make m12c-verify     every offline check for the M12C image
#   make m12c-lua        package the M12C delivery script only
#   make m12c-send       transmit M12C to a console -- REQUIRES PS5_IP
#   make m12c-clean      removes build/m12c ONLY
#
# PURELY ADDITIVE. Not one M0-M12B variable, rule, object list or target is
# modified by this block.
#
# M12C IS M12B's IMAGE WITH THE WRITER REPLACED BY A READER. It takes M12B's
# seventeen own objects, REMOVES gba_savefile.o and savedata.o, renames the
# fixture, and adds three: gba_savehdr.o, gba_restore.o and gba_restorefile.o.
# That is EIGHTEEN own objects.
#
# ---- ***** WHY savedata.o IS DELIBERATELY NOT LINKED ***** ----
#
# THIS IS THE LOAD-BEARING DECISION OF THE MILESTONE, AND IT IS A DELETION.
#
# NO WRITE WINDOW IS NEEDED TO READ. /savedata0 is ALREADY mounted read-only at
# process start (runtime/savedata.h:14-19). adapters/gba/gba_rom.c:118 has
# opened the cartridge with a plain fopen() since M4, and M12A's STAGE 1 read
# its own file back the same way (apps/m12gpsp/main.c:592-594). BOTH ARE
# HARDWARE-PROVEN. So M12C reads its .hdr and .sav with plain fopen() and links
# no savedata layer at all.
#
# THAT IS NOT A SIMPLIFICATION, IT IS THE PROOF DISCIPLINE. The M12B artifact on
# disk is the INPUT to this experiment. An image that could also WRITE could
# consume the very artifact it exists to prove it can read, and the proof would
# be unrepeatable. m12c-verify asserts the absence MECHANICALLY -- no
# savedata.o in the link line, no plat_savedata_* reference in ANY object, and
# no sceSaveData* name in rodata. Writeback is not M12C.
#
# ---- WHY THE ADAPTER IS THREE OBJECTS ----
#
# THE u64 COLLISION THAT SPLIT M12B'S ADAPTER DOES NOT APPLY HERE, and saying so
# plainly matters more than reusing M12B's paragraph. M12C never includes
# runtime/savedata.h, and runtime/libc/stdio.h pulls in neither gpsp/common.h
# nor runtime/core.h -- so no translation unit here sees two u64 typedefs.
#
# The split is kept for a DIFFERENT and still-sufficient reason: the gpSP-side
# object must be built with $(M12C_GPSPFLAGS) -DROM_BUFFER_SIZE=2 to see the
# same emulator configuration as every other gpSP object, and confining every
# WRITE to gamepak_backup[] inside ONE auditable object is what makes the link
# gates below mean anything at all.
#
#   gba_savehdr.o     ZERO INCLUDES. Pure 32-byte LGS1 decode, field validation
#                     and FNV-1a. Flag-agnostic by construction; it is given the
#                     gpSP flags only for consistency with its neighbours.
#   gba_restore.o     $(M12C_GPSPFLAGS) -- sees gpsp/common.h. ***** THE SOLE
#                     WRITER OF gamepak_backup[]. *****
#   gba_restorefile.o $(M12CFLAGS) -- sees runtime/libc/stdio.h for
#                     fopen/fread/fclose. NEVER a gpSP header, and NEVER
#                     runtime/savedata.h.
#
# gba_save.o IS LINKED, UNMODIFIED AND FROZEN. M12C needs its identity latch,
# its ROM hash and its filename builders -- the .sav/.hdr/"/savedata0/gba"
# literals the verifier greps for come from THAT object, not from any new one.

M12CTARGET := m12cgpsp
M12CBUILD  := build/m12c
M12CELF    := $(M12CBUILD)/$(M12CTARGET).elf
M12CBIN    := $(M12CBUILD)/$(M12CTARGET).bin

# M12B's include set with -Iapps/m12cgpsp substituted. -Iruntime is what lets
# the fixture find core.h/shim.h/platform.h/tables.h; -Iadapters/gba is what
# lets it find gba_save.h and the three new headers, all of which are
# deliberately typedef-free.
#
# NOTE that -Iruntime also puts runtime/savedata.h ON THE PATH. Nothing includes
# it, and m12c-verify proves that at link time rather than trusting it.
M12C_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m12cgpsp -Iadapters/gba

M12CFLAGS = $(BASEFLAGS) $(M12C_INCLUDES) -Wall -Wno-unused-function

# IDENTICAL TO M11's AND M12B's. M12C adds NO new compile flag and changes NO
# existing one. -DROM_BUFFER_SIZE=2 is what makes a cartridge over 2 MB demand
# paged, which is why the save identity is latched BEFORE execution.
M12C_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M12C_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

# ***** NOTE WHAT IS ABSENT: runtime/savedata.h. ***** M12B extended $(HEADERS)
# with it; M12C does not, because M12C does not link the layer. This is the
# plain $(HEADERS), unmodified.
M12CHEADERS = $(HEADERS)

M12CLDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
              -Wl,--build-id=none -Wl,-z,norelro \
              -Wl,--orphan-handling=error \
              -Wl,-Map=$(M12CBUILD)/$(M12CTARGET).map

# ----------------------------------------------------------------- objects --
#
# gba_savehdr.o, gba_restore.o and gba_restorefile.o ARE M12C-ONLY BY
# CONSTRUCTION. They appear in NO other target's object list -- M1OBJS..M12BOBJS
# are untouched -- so no earlier milestone gains one byte from them.
#
# NOTE WHAT IS ABSENT:
#   savedata.o        M12C opens no write window. See the note at the top.
#   gba_savefile.o    that is M12B's WRITER. Linking it would put a write path
#                     in a read-only milestone.
#   gba_m11save.o     M11-only, as at M12B.
M12C_OWN_OBJS = $(M12CBUILD)/m12cmain.o \
                $(M12CBUILD)/shim.o \
                $(M12CBUILD)/gba_fixture.o \
                $(M12CBUILD)/gba_filestream.o \
                $(M12CBUILD)/gba_compat.o \
                $(M12CBUILD)/gba_probe.o \
                $(M12CBUILD)/gba_bios.o \
                $(M12CBUILD)/gba_rom.o \
                $(M12CBUILD)/gba_m8map.o \
                $(M12CBUILD)/gba_m10map.o \
                $(M12CBUILD)/gba_save.o \
                $(M12CBUILD)/gba_savehdr.o \
                $(M12CBUILD)/gba_restore.o \
                $(M12CBUILD)/gba_restorefile.o \
                $(M12CBUILD)/gba_sramobs.o \
                $(M12CBUILD)/gba_exec.o \
                $(M12CBUILD)/gba_present.o \
                $(M12CBUILD)/gba_input.o \
                $(M12CBUILD)/platform.o

M12C_GPSP_OBJS = $(M12CBUILD)/gpsp_cpu.o $(M12CBUILD)/gpsp_main.o \
                 $(M12CBUILD)/gpsp_gba_memory.o $(M12CBUILD)/gpsp_video.o \
                 $(M12CBUILD)/gpsp_sound.o $(M12CBUILD)/gpsp_serial.o \
                 $(M12CBUILD)/gpsp_serial_proto.o $(M12CBUILD)/gpsp_rfu.o \
                 $(M12CBUILD)/gpsp_gbp.o $(M12CBUILD)/gpsp_cheats.o \
                 $(M12CBUILD)/gpsp_savestate.o $(M12CBUILD)/gpsp_input.o

M12COBJS = $(M12C_OWN_OBJS) $(M12C_GPSP_OBJS)

# EXCLUDED, unchanged from M1-M12B and none of these may ever appear above:
#   cpu_threaded.c  memmap.c  gba_cc_lut.c  bios_data.S
#   libretro/libretro.c  libretro/libretro-common/*.c
#   x86/**  arm/**  mips/**  3ds/**  bios/**  tests/**  tools/**  jni/**
#   runtime/gfx.c  runtime/audio.c  adapters/gba/gba_audio.c
#   adapters/gba/gba_m11save.c    <-- M11-only
#   adapters/gba/gba_savefile.c   <-- M12B's WRITER
#   runtime/savedata.c            <-- ***** M12C OPENS NO WRITE WINDOW *****

$(M12CBUILD):
	mkdir -p $(M12CBUILD)

# ---- offline structural check ----------------------------------------------
#
# Same C structural checker M8-M12B use, pointed at the fixture AND at all three
# new translation units. It is deliberately NOT pointed at lua/m12c.lua.in, for
# m9-check's reason: Lua comments start with --, which the tool does not know,
# so every apostrophe in a Lua comment would lex as an unterminated char literal
# and the check would FAIL ON CORRECT INPUT.
m12c-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m12cgpsp/main.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_savehdr.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_restore.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_restorefile.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_sramobs.c

# ---- THE OFFLINE ADAPTER HARNESS -------------------------------------------
#
# Builds and runs tools/gba_restore_equiv.c with the HOST compiler, NOT the PS5
# toolchain. It #includes gba_save.c, gba_savehdr.c, gba_restore.c AND
# gba_restorefile.c VERBATIM, so it proves the SHIPPING adapters -- not a
# paraphrase of them -- decode, validate, REFUSE and apply correctly.
#
# ***** RUN THIS BEFORE SPENDING ANY CONSOLE TIME. *****
#
#   THE ROUND TRIP IS THE POINT. The harness builds a header with the FROZEN
#   M12B gba_save_header_build() and then parses it with the NEW
#   gba_savehdr_parse(). A reader that disagreed with the shipping writer by one
#   byte would decline every real save on disk with a plausible-looking reason,
#   and no amount of hand-written expected arrays would catch a shared
#   misunderstanding of the format. This is the check that cannot be fooled by
#   the reader and the test agreeing with each other.
#
#   PASSIVITY IS TESTED, NOT ASSUMED. A DISARMED gba_restore_apply() must refuse
#   every byte. That is STAGE 0's entire guarantee, and it is a property of the
#   adapter rather than of the fixture not reaching a call site.
#
#   ROLLBACK IS TESTED. A pass-2 failure must leave 131,072 bytes of 0xFF, so a
#   half-applied save can never reach the game.
#
# -I gpsp IS REQUIRED AND IS NOT A gpSP DEPENDENCY. gba_save.c and gba_restore.c
# open with #include "common.h". The harness pre-defines that header's guard
# (COMMON_H, gpsp/common.h:20-21) BEFORE including the production .c files, so
# the body expands to NOTHING -- but the preprocessor must still FIND the file
# before it can skip it, and tools/ is on no gpSP search path.
#
# This changes the BUILD RULE ONLY. No adapter is modified to suit the harness,
# nothing is copied out of gpsp/, and no test-only #ifdef exists in any
# production source. gba_restore_equiv stays absent from every object list --
# $(M12COBJS) does not contain it and no payload rule builds it.
m12c-equiv: | $(M12CBUILD)
	@$(HOSTCC) -std=c11 -O2 -Wall -Wextra -I adapters/gba -I gpsp \
	    -o $(M12CBUILD)/gba_restore_equiv tools/gba_restore_equiv.c
	@$(M12CBUILD)/gba_restore_equiv

# ---- LUAport-owned objects, RUNTIME side -----------------------------------

$(M12CBUILD)/m12cmain.o: apps/m12cgpsp/main.c adapters/gba/gba_probe.h \
                         adapters/gba/gba_bios.h adapters/gba/gba_rom.h \
                         adapters/gba/gba_exec.h adapters/gba/gba_present.h \
                         adapters/gba/gba_input.h adapters/gba/gba_m8map.h \
                         adapters/gba/gba_m10map.h \
                         adapters/gba/gba_save.h adapters/gba/gba_savehdr.h \
                         adapters/gba/gba_restore.h \
                         adapters/gba/gba_restorefile.h \
                         adapters/gba/gba_sramobs.h \
                         $(M12CHEADERS) | $(M12CBUILD)
	$(CC) $(M12CFLAGS) -c $< -o $@

$(M12CBUILD)/shim.o: runtime/shim.c $(M12CHEADERS) | $(M12CBUILD)
	$(CC) $(M12CFLAGS) -c $< -o $@

# M0's PS5 VideoOut AND PAD layer, COMPILED FROM UNMODIFIED SOURCE.
$(M12CBUILD)/platform.o: runtime/platform.c $(M12CHEADERS) | $(M12CBUILD)
	$(CC) $(M12CFLAGS) -c $< -o $@

$(M12CBUILD)/gba_present.o: adapters/gba/gba_present.c adapters/gba/gba_present.h \
                            runtime/core.h | $(M12CBUILD)
	$(CC) $(M12CFLAGS) -c $< -o $@

# ***** M12C's RUNTIME-SIDE RESTORE ADAPTER. *****
#
# IT NEEDS $(M12CFLAGS) for runtime/libc/stdio.h -- fopen/fread/fclose. It reads
# TWO files that /savedata0's read-only mount already makes reachable, exactly
# as gba_rom.c reads the cartridge.
#
# ***** IT MUST NOT INCLUDE runtime/savedata.h AND MUST NAME NO plat_savedata_*
# SYMBOL. ***** There is no write window in this milestone. m12c-verify asserts
# that against the OBJECT, not against the source text.
#
# It must also not get $(M12C_GPSPFLAGS): confining every gamepak_backup[] write
# to gba_restore.o is what makes the sole-writer gate below meaningful.
$(M12CBUILD)/gba_restorefile.o: adapters/gba/gba_restorefile.c \
                                adapters/gba/gba_restorefile.h \
                                adapters/gba/gba_restore.h \
                                adapters/gba/gba_savehdr.h \
                                adapters/gba/gba_save.h | $(M12CBUILD)
	$(CC) $(M12CFLAGS) -c $< -o $@

# ---- LUAport-owned objects, gpSP side --------------------------------------

# ***** M12C's HEADER READER. ZERO INCLUDES -- not even its own libc. *****
#
# It is pure arithmetic over an unsigned char array: LGS1 decode, field
# validation and FNV-1a. It is therefore FLAG-AGNOSTIC and is given the gpSP
# flags only so it sits with its neighbours. m12c-verify asserts it names no
# gpSP save global and no stdio entry point, which is what "pure" has to mean
# mechanically.
$(M12CBUILD)/gba_savehdr.o: adapters/gba/gba_savehdr.c \
                            adapters/gba/gba_savehdr.h | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

# ***** M12C's gpSP-SIDE RESTORE ADAPTER. THE SOLE WRITER OF gamepak_backup[].
#
# COMPILED WITH THE gpSP FLAGS AND NOTHING ELSE, exactly like gba_save.o. It
# includes gpsp/common.h to reach gamepak_backup[], backup_type, flash_bank_cnt
# and eeprom_size, so it must see the same gpSP configuration every other gpSP
# object sees.
#
# ***** IT WRITES gamepak_backup[0 .. region) AND SEEDS NO SIZE GLOBAL. *****
# backup_type self-restores from backup_type_reset, which is derived from the
# ROM signature and is deterministic. eeprom_size is DELIBERATELY LEFT ALONE:
# gba_memory.c:565 uses it to choose 6-bit versus 14-bit EEPROM addressing, so
# seeding it from a header would change how the GAME ADDRESSES ITS OWN SAVE --
# a far worse failure than declining to restore.
$(M12CBUILD)/gba_restore.o: adapters/gba/gba_restore.c \
                            adapters/gba/gba_restore.h | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

# ***** M12C's SRAM UPPER-HALF OBSERVER. READ-ONLY. *****
#
# COMPILED WITH THE gpSP FLAGS for gba_restore.o's reason: it includes
# gpsp/common.h to reach gamepak_backup[], so it must see the same gpSP
# configuration every other gpSP object sees. A differing configuration could
# change the array's declared size and silently move the 32 KB boundary this
# observer exists to measure.
#
# ***** IT IS THE SECOND OBJECT IN M12C THAT NAMES gamepak_backup, AND THE
# FIRST THAT ONLY READS IT. ***** gba_restore.o remains the SOLE WRITER. The
# observer holds the array through a `const u8 *` and computes FNV-1a and a
# non-0xFF count over two 32 KB halves -- nothing else. It seeds no
# classification global and calls none of gpSP's save entry points, all four of
# which MUTATE backup_type (gba_memory.c:464-465, :1140-1141, :542).
#
# `nm` cannot distinguish a load from a store, so the read-only claim is proved
# where it CAN be proved: 'make m12c-equiv' section 15 byte-compares all 131,072
# bytes of gamepak_backup across every single sample call.
#
# IT IS LINKED INTO M12C ONLY. No earlier milestone names it -- m12c-verify
# asserts that against the earlier build directories.
$(M12CBUILD)/gba_sramobs.o: adapters/gba/gba_sramobs.c \
                            adapters/gba/gba_sramobs.h | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

# M12B's SAVE ADAPTER, LINKED FROM BYTE-IDENTICAL, UNMODIFIED, FROZEN SOURCE.
# M12C needs its identity latch, its ROM hash and its two filename builders.
$(M12CBUILD)/gba_save.o: adapters/gba/gba_save.c adapters/gba/gba_save.h | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

# M8's keypad adapter, compiled from BYTE-IDENTICAL, UNMODIFIED source.
$(M12CBUILD)/gba_input.o: adapters/gba/gba_input.c adapters/gba/gba_input.h | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

# M4's cartridge adapter, REUSED UNMODIFIED. Note that gba_rom.c:118 already
# opens a file with a plain fopen() on the read-only mount -- the precedent
# M12C's whole no-write-window design rests on.
$(M12CBUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gba_m8map.o: adapters/gba/gba_m8map.c adapters/gba/gba_m8map.h | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gba_m10map.o: adapters/gba/gba_m10map.c adapters/gba/gba_m10map.h | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects -----------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, IDENTICAL to M12B's.
# gpsp/** is never modified and its own Makefile is never invoked. M12C needed
# ZERO changes here.

$(M12CBUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M12CBUILD)
	$(CXX) $(M12C_GPSPCXXFLAGS) -c $< -o $@

$(M12CBUILD)/gpsp_video.o: gpsp/video.cc | $(M12CBUILD)
	$(CXX) $(M12C_GPSPCXXFLAGS) -c $< -o $@

$(M12CBUILD)/gpsp_main.o: gpsp/main.c | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gpsp_sound.o: gpsp/sound.c | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gpsp_serial.o: gpsp/serial.c | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

$(M12CBUILD)/gpsp_input.o: gpsp/input.c | $(M12CBUILD)
	$(CC) $(M12C_GPSPFLAGS) -c $< -o $@

m12c-objs: $(M12COBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- exactly as M1-M12B.
# Every object is named on the link line and is included in its ENTIRETY.
$(M12CELF): $(M12COBJS) runtime/linker.ld | $(M12CBUILD)
	$(CC) $(BASEFLAGS) $(M12CLDFLAGS) -o $@ $(M12COBJS)

m12c-link: $(M12COBJS) | $(M12CBUILD)
	@$(CC) $(BASEFLAGS) $(M12CLDFLAGS) -o $(M12CELF) $(M12COBJS) \
	     > $(M12CBUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to \`[^']+'" $(M12CBUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M12CBUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M12C LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M12C LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M12C LINK OK: linker exit 0, no undefined references"

# --arena 4194304, IDENTICAL TO M4's through M12B's.
$(M12CBIN): $(M12CELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M12CELF) $@ \
	   --map $(M12CBUILD)/$(M12CTARGET).map --budget $(JIT_LIMIT) --arena 4194304

m12c: $(M12CBIN)

# THE TWO HARD CEILINGS. sizereport.py enforces the JIT budget ($(JIT_LIMIT) =
# 524,288) and tools/check_forbidden.py enforces the 2,097,152-byte .bss
# tripwire.
#
# M12C's STATIC FOOTPRINT IS DELIBERATELY TINY. The alternative considered and
# REJECTED was reading the .sav into a 128 KB staging buffer and validating it
# there; instead the file is read TWICE -- once to hash, once to copy -- through
# a 4096-byte STACK buffer, so the restore costs ZERO bytes of .bss. The file
# cannot change between the passes: /savedata0 is mounted read-only and the
# fixture is single-threaded.
#
# M12C SHOULD BE CLOSE TO M12B IN SIZE: it adds three small objects and drops
# gba_savefile.o and savedata.o.
#
# IF EITHER CEILING IS EXCEEDED, STOP AND REPORT IT. Do not shrink unrelated
# code to make room: the size of the M12C image is one of the results.
m12c-size: $(M12CELF) $(M12CBIN)
	@$(PYTHON) tools/sizereport.py $(M12CELF) $(M12CBIN) \
	   --map $(M12CBUILD)/$(M12CTARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification ----------------------------------------------------------
#
# EVERY M11/M12B GATE THAT STILL APPLIES IS KEPT. The VideoOut allowlist, the
# PAD allowlist, the sceGnm prohibition, the sceAudioOut prohibition, the
# marker-based rodata carve and the adapter isolation checks all carry over.
#
# M12A's SD_CREATE gate is REPLACED BY SOMETHING STRICTLY STRONGER: M12C asserts
# the savedata layer is not linked AT ALL, which makes SD_CREATE unreachable by
# construction rather than merely undefined.
#
# ---- WHAT M12C ADDS, AND WHY EACH ONE IS NECESSARY ----
#
#   ***** THERE IS NO WRITE PATH. ***** No object may reference
#   plat_savedata_begin_write / end_write / mkdir / init / is_ready; savedata.o
#   and gba_savefile.o must be absent from the link line; and no sceSaveData*
#   name may reach rodata. THIS IS THE MOST IMPORTANT GATE IN THE MILESTONE --
#   it is what makes the M12B artifact a repeatable input rather than something
#   this image could quietly consume and overwrite.
#
#   THE RESTORE ADAPTER IS STILL PASSIVE. gba_restore.o MUST NOT reference
#   read_backup, write_backup, read_eeprom or write_eeprom. Those four are part
#   of gpSP's DETECTOR -- backup_type collapses from BACKUP_UNKN to BACKUP_SRAM
#   on the first backup-region access (gba_memory.c:465, :1141) -- so an adapter
#   that called one would MANUFACTURE the live class it then compared against
#   the header, and a 32 KB SRAM restore could land on an EEPROM cartridge.
#
#   THE ADAPTER REALLY REACHES gpSP's SAVE STATE. gba_restore.o must reference
#   backup_type, gamepak_backup, flash_bank_cnt and eeprom_size. An adapter that
#   did not would restore into nothing and report success.
#
#   ***** gamepak_backup HAS A CONFINED SET OF NAMERS. ***** Only gba_restore.o
#   (which writes it) and the frozen gba_save.o (which only ever HASHES it) may
#   name it. m12cmain.o, gba_restorefile.o and gba_savehdr.o must NOT. `nm`
#   cannot distinguish a load from a store, so this gate proves CONFINEMENT, and
#   `make m12c-equiv` proves the write BEHAVIOUR -- including that a disarmed
#   adapter refuses every byte and that a failed pass 2 rolls back to 0xFF.
#
#   THE HEADER READER IS PURE. gba_savehdr.o must name no gpSP save global and
#   no stdio entry point. It has zero includes, and this is what that means
#   mechanically.
#
#   THE FILE READER REALLY READS FILES. gba_restorefile.o must reference fopen,
#   fread and fclose. A reader that named none of them would be validating
#   nothing, and every decline would be VACUOUS.
#
#   THE FIXTURE GOES THROUGH THE ADAPTERS. m12cmain.o must reference
#   gba_save_latch, gba_restorefile_run, gba_restore_arm and
#   gba_restore_baseline, and must carry NO gpSP save-global relocation.
#
#   gpSP DID NOT LEARN ABOUT M12C. No gpsp_*.o may reference any gba_restore_,
#   gba_restorefile_, gba_savehdr_ or gba_save_ symbol.
#
#   M12C's OBJECTS LEAKED INTO NOTHING. The three new objects must appear in no
#   earlier milestone's object list.
#
# THE RELOCATION GATE USES THE NON-MASKING FORM M12A INTRODUCED: a pipeline's
# exit status in POSIX sh is the status of the LAST command, so `| tail -12`
# would discard the checker's. The status is captured BEFORE anything else runs
# and the output is trimmed from a log file instead.

M12C_VIDEOOUT_ALLOW = sceVideoOutOpen sceVideoOutClose sceVideoOutRegisterBuffers \
                      sceVideoOutSubmitFlip sceVideoOutSetFlipRate \
                      sceVideoOutAddFlipEvent

M12C_PAD_ALLOW = scePadInit scePadGetHandle scePadRead

# THE FOUR gpSP SAVE-PATH ENTRY POINTS THAT MUST NEVER BE CALLED.
M12C_FORBIDDEN_SAVE = read_backup write_backup read_eeprom write_eeprom

# The gpSP save globals. gba_restore.o MUST read them; the fixture, the file
# reader and the header reader MUST NOT.
M12C_SAVE_GLOBALS = backup_type gamepak_backup flash_bank_cnt eeprom_size

# ***** THE WRITE-WINDOW ENTRY POINTS THAT MUST APPEAR NOWHERE. *****
M12C_FORBIDDEN_WRITE = plat_savedata_begin_write plat_savedata_end_write \
                       plat_savedata_mkdir plat_savedata_init \
                       plat_savedata_is_ready

m12c-verify: $(M12CELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M12CELF)
	@echo "== relocations (NON-MASKING pipeline) =="
	@$(PYTHON) tools/verify_reloc.py $(M12CELF) $(M12COBJS) \
	     > $(M12CBUILD)/reloc.log 2>&1; \
	 st=$$?; \
	 tail -12 $(M12CBUILD)/reloc.log; \
	 if [ $$st -ne 0 ]; then \
	     echo "M12C VERIFY FAILED: the relocation check exited $$st."; \
	     echo "  Full output: $(M12CBUILD)/reloc.log"; \
	     exit $$st; \
	 fi
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M12COBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M12COBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M12CELF) $(M12COBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M12CELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M12C VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== ***** THERE IS NO WRITE PATH -- THE DEFINING GATE OF M12C ***** =="
	@bad=""; \
	 for o in $(M12COBJS); do \
	     case "$$o" in \
	       */savedata.o|*gba_savefile.o) bad="$$bad $$o";; \
	     esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12C VERIFY FAILED: a WRITE-SIDE object is in the M12C link"; \
	     echo "  line:$$bad"; \
	     echo "  M12C is RESTORE-ONLY. The M12B artifact on disk is the INPUT to"; \
	     echo "  this experiment: an image that could also write could consume"; \
	     echo "  the very artifact it exists to prove it can read, and the proof"; \
	     echo "  would be UNREPEATABLE. This is not a style rule."; \
	     exit 1; \
	 fi; \
	 echo "neither savedata.o nor gba_savefile.o is in the M12C object list"
	@bad=""; \
	 for o in $(M12COBJS); do \
	     for s in $(M12C_FORBIDDEN_WRITE); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12C VERIFY FAILED: an object references a write-window entry"; \
	     echo "  point:$$bad"; \
	     echo "  No write window is needed to READ: /savedata0 is already"; \
	     echo "  mounted read-only at process start, which is why gba_rom.c has"; \
	     echo "  opened the cartridge with a plain fopen() since M4."; \
	     exit 1; \
	 fi; \
	 echo "NO object in the image names plat_savedata_begin_write, end_write,"; \
	 echo "  mkdir, init or is_ready -- there is no write window to open"
	@echo "== rodata carve (marker-based, as M7-M12B) =="
	@$(OBJCOPY) -O binary --only-section=.text $(M12CELF) $(M12CBUILD)/text.bin
	@tx=$$($(NM) $(M12CELF) | awk '$$NF == "__text_start"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 ra=$$($(NM) $(M12CELF) | awk '$$NF == "__rodata_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 rz=$$($(NM) $(M12CELF) | awk '$$NF == "__rodata_end"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 if [ -z "$$tx" ] || [ -z "$$ra" ] || [ -z "$$rz" ]; then \
	     echo "M12C VERIFY FAILED: linker marker symbol missing -- need"; \
	     echo "  __text_start, __rodata_start and __rodata_end in $(M12CELF)."; \
	     exit 1; \
	 fi; \
	 t=$$((0x$$tx)); a=$$((0x$$ra)); z=$$((0x$$rz)); n=$$((z - a)); \
	 if [ "$$t" -ne 0 ]; then \
	     echo "M12C VERIFY FAILED: .text does not start at vaddr 0 (__text_start ="; \
	     echo "  $$t), so a symbol value is no longer a file offset in the"; \
	     echo "  extracted binary and this carve would read the wrong bytes."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M12C VERIFY FAILED: empty rodata window -- __rodata_start=$$a"; \
	     echo "  __rodata_end=$$z gives $$n bytes."; \
	     exit 1; \
	 fi; \
	 have=$$(wc -c < $(M12CBUILD)/text.bin | tr -d " "); \
	 if [ "$$have" -lt "$$z" ]; then \
	     echo "M12C VERIFY FAILED: .text extraction is $$have bytes but the rodata"; \
	     echo "  window ends at $$z -- the extraction is short or .text is absent."; \
	     exit 1; \
	 fi; \
	 dd if=$(M12CBUILD)/text.bin of=$(M12CBUILD)/rodata.bin bs=1 skip=$$a count=$$n \
	    2>/dev/null || { \
	     echo "M12C VERIFY FAILED: dd could not carve the rodata window."; exit 1; \
	 }; \
	 if [ ! -s $(M12CBUILD)/rodata.bin ]; then \
	     echo "M12C VERIFY FAILED: rodata extraction produced 0 bytes -- every check"; \
	     echo "  below would pass vacuously. Fix the extraction, not the checks."; \
	     exit 1; \
	 fi; \
	 got=$$(wc -c < $(M12CBUILD)/rodata.bin | tr -d " "); \
	 if [ "$$got" -ne "$$n" ]; then \
	     echo "M12C VERIFY FAILED: rodata.bin is $$got bytes, expected $$n ="; \
	     echo "  __rodata_end - __rodata_start. Extraction is not trustworthy."; \
	     exit 1; \
	 fi; \
	 echo "rodata carved from .text by linker markers: [$$a,$$z) = $$n bytes"
	@echo "== ***** NO SAVEDATA NAME REACHED THE IMAGE ***** =="
	@if grep -aqE "sceSaveData" $(M12CBUILD)/rodata.bin; then \
	     echo "M12C VERIFY FAILED: a savedata name reached the image:"; \
	     grep -aoE "sceSaveData[A-Za-z0-9_]*" $(M12CBUILD)/rodata.bin \
	        | sort -u | sed "s/^/  /"; \
	     echo "  M12C links no savedata layer, so no such name can be present"; \
	     echo "  unless an object list changed. THE INVERSE OF M12B's GATE:"; \
	     echo "  M12B REQUIRED these names, M12C PROHIBITS them."; \
	     exit 1; \
	 fi; \
	 echo "no sceSaveData* name is present -- the layer is genuinely absent"
	@if grep -aqE "libSceSaveData" $(M12CBUILD)/rodata.bin; then \
	     echo "M12C VERIFY FAILED: libSceSaveData.sprx is named in the image."; \
	     exit 1; \
	 fi; \
	 echo "libSceSaveData.sprx is not named either"
	@echo "== M12C NAMES THE FILES IT READS =="
	@miss=""; \
	 for s in "/savedata0/gba" ".sav" ".hdr"; do \
	     grep -aq "$$s" $(M12CBUILD)/rodata.bin || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M12C VERIFY FAILED: the image does not contain:$$miss"; \
	     echo "  M12C is the milestone that READS a .sav. If the directory or"; \
	     echo "  either extension is absent, no filename can be built, nothing"; \
	     echo "  can be opened, and every 'NO SAVE ON DISK' result would be"; \
	     echo "  VACUOUS -- the worst possible outcome, because it looks exactly"; \
	     echo "  like a correct first-run PASS."; \
	     exit 1; \
	 fi; \
	 echo "/savedata0/gba, .sav and .hdr are all present in rodata"; \
	 echo "  (they come from the FROZEN gba_save.o, which builds both names)"
	@echo "  NOTE the LGS1 header magic is deliberately NOT grepped for here,"
	@echo "  for M12B's reason: gba_save.c writes it as four SEPARATE BYTE"
	@echo "  STORES and gba_savehdr.c compares it the same way, so no 'LGS1'"
	@echo "  string literal exists in rodata to find. A gate grepping for it"
	@echo "  would FAIL ON CORRECT INPUT. The magic is checked BYTE BY BYTE,"
	@echo "  and round-tripped against the frozen writer, by 'make m12c-equiv'."
	@echo "== PRESENTATION ALLOWLIST (carried from M7-M12B) =="
	@for s in $(M12C_VIDEOOUT_ALLOW) libSceVideoOut.sprx; do \
	     if ! grep -aq "$$s" $(M12CBUILD)/rodata.bin; then \
	         echo "M12C VERIFY FAILED: required presentation name '$$s' is NOT in"; \
	         echo "  the image."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 6 VideoOut entry points + libSceVideoOut.sprx are present"
	@allow=$$(echo $(M12C_VIDEOOUT_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "sceVideoOut[A-Za-z0-9_]*" $(M12CBUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M12C VERIFY FAILED: an UNEXPECTED sceVideoOut* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no sceVideoOut* name outside the six-symbol allowlist"
	@echo "== THE GRAPHICS-PIPELINE PROHIBITION IS UNCHANGED =="
	@if grep -aqE "sceGnm" $(M12CBUILD)/rodata.bin; then \
	     echo "M12C VERIFY FAILED: a graphics-pipeline name reached the image:"; \
	     grep -aoE "sceGnm[A-Za-z0-9_]*" $(M12CBUILD)/rodata.bin \
	        | sort -u | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no sceGnm* name is present (NO GNM -- unchanged from M6-M12B)"
	@echo "== PAD ALLOWLIST (carried from M8-M12B) =="
	@for s in $(M12C_PAD_ALLOW) libScePad.sprx; do \
	     if ! grep -aq "$$s" $(M12CBUILD)/rodata.bin; then \
	         echo "M12C VERIFY FAILED: required pad name '$$s' is NOT in the image."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all 3 scePad entry points + libScePad.sprx are present"
	@allow=$$(echo $(M12C_PAD_ALLOW) | tr ' ' '|'); \
	 bad=$$(grep -aoE "scePad[A-Za-z0-9_]*" $(M12CBUILD)/rodata.bin \
	          | sort -u | grep -vE "^($$allow)$$" || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M12C VERIFY FAILED: an UNEXPECTED scePad* name reached the image:"; \
	     echo "$$bad" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no scePad* name outside the three-symbol allowlist"
	@echo "== THE AUDIO PROHIBITION IS UNCHANGED =="
	@if grep -aqE "sceAudioOut" $(M12CBUILD)/rodata.bin; then \
	     echo "M12C VERIFY FAILED: an audio name reached the image:"; \
	     grep -aoE "sceAudioOut[A-Za-z0-9_]*" $(M12CBUILD)/rodata.bin \
	        | sort -u | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "no sceAudioOut* name is present"
	@echo "== THE PASSIVITY GATE =="
	@bad=""; \
	 for s in $(M12C_FORBIDDEN_SAVE); do \
	     if $(NM) -u $(M12CBUILD)/gba_restore.o 2>/dev/null | grep -qw "$$s"; then \
	         bad="$$bad $$s"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12C VERIFY FAILED: the restore adapter references gpSP's"; \
	     echo "  SAVE-PATH entry points:$$bad"; \
	     echo "  Those four are part of gpSP's DETECTOR, not its reporting"; \
	     echo "  surface: backup_type collapses from BACKUP_UNKN to BACKUP_SRAM"; \
	     echo "  on the first backup-region access (gba_memory.c:465, :1141)."; \
	     echo "  An adapter that calls one MANUFACTURES the live class it then"; \
	     echo "  compares against the header -- and a 32 KB SRAM restore could"; \
	     echo "  land on an EEPROM cartridge. This is not a style rule."; \
	     exit 1; \
	 fi; \
	 echo "gba_restore.o calls NONE of read_backup/write_backup/read_eeprom/write_eeprom"
	@bad=""; \
	 for o in $(M12CBUILD)/m12cmain.o $(M12CBUILD)/gba_restorefile.o \
	          $(M12CBUILD)/gba_savehdr.o; do \
	     for s in $(M12C_FORBIDDEN_SAVE); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12C VERIFY FAILED: the fixture, the file reader or the header"; \
	     echo "  reader references gpSP's save-path entry points:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "none of m12cmain.o, gba_restorefile.o, gba_savehdr.o calls them either"
	@echo "== THE ADAPTER REALLY REACHES gpSP's SAVE STATE =="
	@miss=""; \
	 for s in $(M12C_SAVE_GLOBALS); do \
	     $(NM) -u $(M12CBUILD)/gba_restore.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M12C VERIFY FAILED: gba_restore.o does NOT reference:$$miss"; \
	     echo "  An adapter that does not reach the emulator's real save state"; \
	     echo "  would restore into nothing and REPORT SUCCESS, which is the"; \
	     echo "  worst outcome this milestone can produce."; \
	     exit 1; \
	 fi; \
	 echo "gba_restore.o reaches gpSP's real save state (backup_type,"; \
	 echo "  gamepak_backup, flash_bank_cnt, eeprom_size)"
	@echo "== ***** gamepak_backup HAS A CONFINED SET OF NAMERS ***** =="
	@bad=""; \
	 for o in $(M12CBUILD)/m12cmain.o $(M12CBUILD)/gba_restorefile.o \
	          $(M12CBUILD)/gba_savehdr.o; do \
	     for s in $(M12C_SAVE_GLOBALS); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12C VERIFY FAILED: an object outside the restore adapter names a"; \
	     echo "  gpSP save global:$$bad"; \
	     echo "  Every byte must cross through gba_restore_apply(). Confining the"; \
	     echo "  writes to ONE object is what makes the passivity and rollback"; \
	     echo "  guarantees auditable at all."; \
	     exit 1; \
	 fi; \
	 echo "only gba_restore.o (THE SOLE WRITER), the FROZEN gba_save.o"; \
	 echo "  (hash-only reader) and gba_sramobs.o (READ-ONLY observer) name"; \
	 echo "  gamepak_backup. NOTE: nm cannot tell a load from a store, so this"; \
	 echo "  proves CONFINEMENT -- 'make m12c-equiv' proves the write"; \
	 echo "  BEHAVIOUR, including disarmed refusal and 0xFF rollback, and"; \
	 echo "  section 15 proves the observer mutates NOTHING."
	@echo "== THE HEADER READER IS PURE =="
	@bad=""; \
	 for s in fopen fread fclose fwrite $(M12C_SAVE_GLOBALS); do \
	     if $(NM) -u $(M12CBUILD)/gba_savehdr.o 2>/dev/null | grep -qw "$$s"; then \
	         bad="$$bad $$s"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12C VERIFY FAILED: gba_savehdr.o names:$$bad"; \
	     echo "  It has ZERO INCLUDES and is pure arithmetic over an unsigned"; \
	     echo "  char array. If it can reach a file or a gpSP global, the"; \
	     echo "  validation ladder is no longer separable from the I/O it"; \
	     echo "  validates."; \
	     exit 1; \
	 fi; \
	 echo "gba_savehdr.o names no file entry point and no gpSP save global"
	@echo "== THE FILE READER REALLY READS FILES =="
	@miss=""; \
	 for s in fopen fread fclose; do \
	     $(NM) -u $(M12CBUILD)/gba_restorefile.o 2>/dev/null | grep -q "$$s" \
	        || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M12C VERIFY FAILED: gba_restorefile.o does NOT reference:$$miss"; \
	     echo "  A reader that opens nothing would decline every save with a"; \
	     echo "  plausible reason, and 'NO SAVE ON DISK' would be VACUOUS --"; \
	     echo "  indistinguishable from a correct first-run PASS."; \
	     exit 1; \
	 fi; \
	 echo "gba_restorefile.o drives fopen, fread and fclose"
	@if $(NM) -u $(M12CBUILD)/gba_restorefile.o 2>/dev/null | grep -qw "fwrite"; then \
	     echo "M12C VERIFY FAILED: gba_restorefile.o references fwrite."; \
	     echo "  M12C READS. It must not be able to write a byte to disk."; \
	     exit 1; \
	 fi; \
	 echo "gba_restorefile.o does NOT reference fwrite -- it reads only"
	@echo "== THE FIXTURE DRIVES THE ADAPTERS, AND ONLY THROUGH THEM =="
	@miss=""; \
	 for s in gba_save_latch gba_restorefile_run gba_restore_arm \
	          gba_restore_baseline; do \
	     $(NM) -u $(M12CBUILD)/m12cmain.o 2>/dev/null | grep -q "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M12C VERIFY FAILED: m12cmain.o does NOT reference:$$miss"; \
	     echo "  The identity is not being latched before execution, the restore"; \
	     echo "  is never driven, the arm that separates the two stages is"; \
	     echo "  missing, or no baseline is taken."; \
	     exit 1; \
	 fi; \
	 echo "m12cmain.o drives the latch, the restore, the ARM and the baseline"
	@if $(NM) -u $(M12CBUILD)/m12cmain.o 2>/dev/null | grep -q "sceSaveData"; then \
	     echo "M12C VERIFY FAILED: m12cmain.o carries a sceSaveData* name of its"; \
	     echo "  own -- the fixture is reaching for a savedata layer that this"; \
	     echo "  image deliberately does not link."; \
	     exit 1; \
	 fi; \
	 echo "m12cmain.o names no sceSaveData* symbol"
	@echo "== ***** THE SRAM OBSERVER IS PRESENT, DRIVEN, AND READ-ONLY ***** =="
	@case " $(M12COBJS) " in \
	   *" $(M12CBUILD)/gba_sramobs.o "*) ;; \
	   *) echo "M12C VERIFY FAILED: gba_sramobs.o is not on the M12C link line."; \
	      exit 1;; \
	 esac; \
	 echo "gba_sramobs.o is linked into M12C"
	@miss=""; \
	 for s in m12c_sramobs_reset m12c_sramobs_sample m12c_sramobs_read; do \
	     $(NM) $(M12CELF) 2>/dev/null | grep -qw "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M12C VERIFY FAILED: the linked image does not define:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "the linked image defines the three observer entry points"
	@miss=""; \
	 for s in m12c_sramobs_reset m12c_sramobs_sample m12c_sramobs_read; do \
	     $(NM) -u $(M12CBUILD)/m12cmain.o 2>/dev/null | grep -qw "$$s" \
	        || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M12C VERIFY FAILED: m12cmain.o does NOT reference:$$miss"; \
	     echo "  The observer would be linked but never driven, and the run"; \
	     echo "  would publish an all-zero measurement that reads as 'UPPER"; \
	     echo "  HALF UNCHANGED' -- refuting the hypothesis on the strength of"; \
	     echo "  a run that never tested it. That is the single most dangerous"; \
	     echo "  wrong answer this instrument can give."; \
	     exit 1; \
	 fi; \
	 echo "m12cmain.o drives the observer: reset, sample and read"
	@if ! $(NM) -u $(M12CBUILD)/gba_sramobs.o 2>/dev/null \
	        | grep -qw "gamepak_backup"; then \
	     echo "M12C VERIFY FAILED: gba_sramobs.o does NOT reference"; \
	     echo "  gamepak_backup. An observer that never reaches the emulator's"; \
	     echo "  real save array measures nothing and reports it as a finding."; \
	     exit 1; \
	 fi; \
	 echo "gba_sramobs.o reaches gpSP's real save array (gamepak_backup)"
	@bad=""; \
	 for s in $(M12C_FORBIDDEN_SAVE) $(M12C_FORBIDDEN_WRITE) \
	          fopen fread fwrite fclose remove rename malloc free; do \
	     if $(NM) -u $(M12CBUILD)/gba_sramobs.o 2>/dev/null | grep -qw "$$s"; then \
	         bad="$$bad $$s"; \
	     fi; \
	 done; \
	 if $(NM) -u $(M12CBUILD)/gba_sramobs.o 2>/dev/null \
	      | grep -qE "plat_|sce"; then bad="$$bad plat_*-or-sce*"; fi; \
	 if [ -n "$$bad" ]; then \
	     echo "M12C VERIFY FAILED: gba_sramobs.o names:$$bad"; \
	     echo "  The observer must be able to do EXACTLY ONE thing: read bytes"; \
	     echo "  out of gamepak_backup. read_backup/write_backup/read_eeprom/"; \
	     echo "  write_eeprom all MUTATE backup_type (gba_memory.c:464-465,"; \
	     echo "  :1140-1141, :542), so an observer that called one would"; \
	     echo "  MANUFACTURE the live class M12C reports and would then be"; \
	     echo "  measuring its own side effect."; \
	     exit 1; \
	 fi; \
	 echo "gba_sramobs.o names no save entry point, no file call, no"; \
	 echo "  allocator, no plat_* and no sce* -- it can only read memory"
	@bad=""; \
	 for d in $(M1BUILD) $(M2BUILD) $(M3BUILD) $(M4BUILD) $(M5BUILD) \
	          $(M6BUILD) $(M7BUILD) $(M8BUILD) $(M9BUILD) $(M10BUILD) \
	          $(M11BUILD) $(M12BUILD) $(M12BBUILD); do \
	     if [ -f "$$d/gba_sramobs.o" ]; then bad="$$bad $$d"; fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12C VERIFY FAILED: gba_sramobs.o appears in an EARLIER"; \
	     echo "  milestone's build:$$bad"; \
	     echo "  The observer is M12C-only. An earlier image that gained it"; \
	     echo "  would no longer be the frozen artifact its own verifier"; \
	     echo "  passed against."; \
	     exit 1; \
	 fi; \
	 echo "no earlier milestone (M1-M12B) builds gba_sramobs.o"
	@echo "  NOTE: nm cannot tell a load from a store. The READ-ONLY claim is"; \
	 echo "  proved by 'make m12c-equiv' section 15, which byte-compares all"; \
	 echo "  131072 bytes of gamepak_backup across every sample call."
	@echo "== gpSP DID NOT LEARN ABOUT M12C =="
	@bad=""; \
	 for o in $(M12C_GPSP_OBJS); do \
	     if $(NM) -u $$o 2>/dev/null \
	          | grep -qE "gba_restore_|gba_restorefile_|gba_savehdr_|gba_save_"; then \
	         bad="$$bad $$o"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12C VERIFY FAILED: a gpSP object references an M12C adapter:$$bad"; \
	     echo "  M12C requires ZERO changes to gpsp/** and this is the proof."; \
	     exit 1; \
	 fi; \
	 echo "no gpsp_*.o references any M12C or M12B adapter symbol"
	@echo "== M11's OBSERVER IS NOT LINKED =="
	@bad=""; \
	 for o in $(M12COBJS); do \
	     case "$$o" in *gba_m11save*) bad="$$bad $$o";; esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12C VERIFY FAILED: M11's save observer is in the M12C link"; \
	     echo "  line:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "gba_m11save.o is NOT in the M12C link line -- M11 stays frozen"
	@echo "== NO AUDIO OBJECT IS LINKED =="
	@bad=""; \
	 for o in $(M12COBJS); do \
	     case "$$o" in *gba_audio*|*/audio.o) bad="$$bad $$o";; esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12C VERIFY FAILED: an audio object is in the M12C link line:$$bad"; \
	     echo "  M12C has no audio path and its verifier asserts the sceAudioOut"; \
	     echo "  PROHIBITION -- linking one would make that gate a lie."; \
	     exit 1; \
	 fi; \
	 echo "neither gba_audio.o nor audio.o is in the M12C object list"
	@echo "== THE RESTORE ADAPTER LEAKED INTO NO EARLIER MILESTONE =="
	@bad=""; \
	 for o in $(OBJS) $(M1OBJS) $(M2OBJS) $(M3OBJS) $(M4OBJS) $(M5OBJS) \
	          $(M6OBJS) $(M7OBJS) $(M8OBJS) $(M9OBJS) $(M9DOBJS) $(M10OBJS) \
	          $(M11OBJS) $(M12OBJS) $(M12BOBJS); do \
	     case "$$o" in \
	       *gba_savehdr.o|*gba_restore.o|*gba_restorefile.o) bad="$$bad $$o";; \
	     esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M12C VERIFY FAILED: the M12C-only restore adapter leaked into an"; \
	     echo "  earlier milestone's object list:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "gba_savehdr.o, gba_restore.o and gba_restorefile.o are in NO other"; \
	 echo "  target -- M0-M12B link sets unchanged"
	@echo "== M12C VERIFY OK =="

# ---- the Lua payload -------------------------------------------------------

M12C_LUA_TEMPLATE := lua/m12c.lua.in
M12C_LUA_OUT      := $(M12CBUILD)/$(M12CTARGET).lua

m12c-lua: m12c-check $(M12CBIN) $(M12CELF)
	@case "$(STAGE)" in \
	   0|1) ;; \
	   *) echo "M12C LUA FAILED: STAGE='$(STAGE)' is not 0 or 1."; \
	      echo "  0=VALIDATE (40 s, reads and checks the file, applies NOTHING)"; \
	      echo "  1=RESTORE  (40 s, applies the validated bytes)"; \
	      echo "  NEITHER stage writes to disk."; \
	      exit 1;; \
	 esac
	@$(PYTHON) tools/mklua.py $(M12C_LUA_TEMPLATE) $(M12CBIN) $(M12C_LUA_OUT) $(M12CELF)
	@sed -i.bak "s/^local STAGE = .*/local STAGE = $(STAGE)/" $(M12C_LUA_OUT)
	@rm -f $(M12C_LUA_OUT).bak
	@if ! grep -q "^local STAGE = $(STAGE)$$" $(M12C_LUA_OUT); then \
	     echo "M12C LUA FAILED: the STAGE line was not rewritten in"; \
	     echo "  $(M12C_LUA_OUT) -- it would run STAGE 0 regardless of STAGE=$(STAGE)."; \
	     exit 1; \
	 fi
	@echo "M12C LUA OK: $(M12C_LUA_OUT) -- STAGE $(STAGE)"

# ---- m12c-send -- HARDWARE DELIVERY ----------------------------------------
#
# NOT AUTHORISED BY THE EXISTENCE OF THIS TARGET. Both stages build and transmit
# perfectly well -- that is deliberate -- but RUNNING either is an OPERATOR
# decision taken explicitly, after the offline chain is green.
#
#   * RUN `make m12c-equiv` FIRST. It costs seconds and it is the only check
#     that can catch a reader that disagrees with the FROZEN M12B writer about
#     the byte layout -- a disagreement that would decline every real save on
#     disk with a plausible-looking reason.
#   * ***** M12C CANNOT DAMAGE ANYTHING ON DISK. ***** Neither stage opens a
#     write window; the image links no savedata layer at all. The worst outcome
#     is a refused restore, and gamepak_backup is rolled back to 0xFF if pass 2
#     ever fails part-way.
#   * ***** RUN STAGE 0 FIRST, AND IT IS THE CONTROL -- NOT A WARM-UP. *****
#     It reads and validates the file in FULL and applies nothing, so the game
#     should report NO SAVE DATA. That negative result is half the experiment.
#     Then run STAGE 1 and look for the save to appear.
#   * THE REAL RESULT IS ON THE GAME'S OWN LOAD/CONTINUE SCREEN. The diagnostic
#     screen can only prove the bytes reached gpSP; whether the GAME accepts
#     them is answered nowhere else.
#   * RELAUNCH THE HOST GAME BETWEEN EVERY RUN. plat_video_init() reserves 16 MB
#     of direct memory and DELIBERATELY never releases it, so a second run in
#     one session fails with -267/-24 -- and, more importantly, the whole
#     experiment is "a FRESH host-game relaunch".
#   * M12C reuses M3's BIOS and M4's cartridge, and READS the .sav/.hdr M12B
#     already wrote. Nothing is re-uploaded. Which cartridge is present must
#     stay an operator variable: the save identity is a property of the
#     CARTRIDGE, and a different dump of the same game produces a different
#     <ID> and a correct "NO SAVE ON DISK".
#
# WHY m12c-lua IS A PREREQUISITE AND $(M12C_LUA_OUT) IS NOT. m12c-lua is PHONY
# and STAGE is a command-line variable, so a file prerequisite on
# $(M12C_LUA_OUT) would be satisfied by a .lua left behind by a PREVIOUS stage
# and would ship the wrong stage silently -- which at M12C means running the
# CONTROL when the operator asked for the EXPERIMENT, or the reverse, and
# quietly invalidating the comparison the milestone exists to make.
m12c-send: m12c-verify m12c-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m12c-send PS5_IP=<console address> STAGE=<0|1>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M12C_LUA_OUT) $(M12CBIN)

m12c-clean:
	rm -rf $(M12CBUILD)

.PHONY: m12c m12c-objs m12c-check m12c-equiv m12c-link m12c-size m12c-verify \
        m12c-lua m12c-send m12c-clean

# =============================================================================
# M13A -- USB / exFAT ROM STORAGE RECONNAISSANCE (STAGES 0-2 ONLY)
# =============================================================================
#
# PURELY ADDITIVE. Not one M0-M12C variable, rule, target or gate is altered by
# this block; M13A introduces its own namespace and its own build directory.
#
# WHAT M13A IS
# ------------
# A storage-visibility probe and nothing else. It answers:
#
#   CAN THIS HOST PROCESS SEE A USB DRIVE, OPEN LUAport/roms/gba, AND ENUMERATE
#   .gba FILENAMES?
#
# STAGE 0 mount discovery, STAGE 1 the ROM directory, STAGE 2 enumeration.
#
# ***** IT IS THE SMALLEST IMAGE SINCE M1. ***** Four objects: the fixture, the
# shim, the storage layer and the .gba filter. THERE IS NO gpSP OBJECT IN IT AT
# ALL -- no cpu, no gba_memory, no video, no sound -- and no platform.o,
# no savedata.o and no gba_rom.o either. That is what makes "M13A cannot load a
# ROM, start an emulator or write a file" a property of the LINK rather than a
# promise in a comment, and m13a-verify checks it against the object list.
#
# WHY THERE IS NO STAGE VARIABLE
# ------------------------------
# M10-M12C take STAGE through ext.frame_count. M13A's three stages are STRICTLY
# DEPENDENT -- no mount, no directory; no directory, no enumeration -- so a
# selector could only ever request that a prerequisite be skipped. One run walks
# 0 -> 1 -> 2 and stops at the first unmet dependency. `make m13a-lua` therefore
# takes no STAGE and cannot ship the wrong one.

M13ATARGET := m13agpsp
M13ABUILD  := build/m13a
M13AELF    := $(M13ABUILD)/$(M13ATARGET).elf
M13ABIN    := $(M13ABUILD)/$(M13ATARGET).bin

M13A_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m13agpsp \
                -Iadapters/gba

M13AFLAGS = $(BASEFLAGS) $(M13A_INCLUDES) -Wall -Wno-unused-function

M13AHEADERS = $(HEADERS)

M13ALDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
              -Wl,--build-id=none -Wl,-z,norelro \
              -Wl,--orphan-handling=error \
              -Wl,-Map=$(M13ABUILD)/$(M13ATARGET).map

# ----------------------------------------------------------------- objects --
#
# FOUR. m13store.o and gba_library.o are M13A-ONLY BY CONSTRUCTION: they appear
# in NO other target's object list, so no earlier milestone gains one byte.
#
# NOTE WHAT IS ABSENT, all of it deliberate:
#   every gpsp_*.o     M13A runs no emulator
#   gba_rom.o          M13A opens no ROM
#   savedata.o         M13A opens no write window
#   gba_savefile.o     M13A writes nothing
#   gba_save*.o        M13A touches no save; M12 is FROZEN and untouched
#   gba_restore*.o     likewise
#   platform.o         no video, no pad
#   gfx.o / audio.o    no framebuffer, no sound
#   gba_filestream.o   M13A reads no file CONTENT -- only directory entries
M13A_OWN_OBJS = $(M13ABUILD)/m13amain.o \
                $(M13ABUILD)/shim.o \
                $(M13ABUILD)/m13store.o \
                $(M13ABUILD)/gba_library.o

M13AOBJS = $(M13A_OWN_OBJS)

$(M13ABUILD):
	mkdir -p $(M13ABUILD)

# ---- offline structural check ----------------------------------------------
#
# The same C structural checker M8-M12C use. Deliberately NOT pointed at
# lua/m13a.lua.in, for m9-check's reason: Lua comments start with --, which the
# tool does not know, so every apostrophe in a Lua comment would lex as an
# unterminated char literal and the check would FAIL ON CORRECT INPUT.
m13a-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m13agpsp/main.c
	@$(PYTHON) tools/m8_syntax_check.py apps/m13agpsp/m13a_report.inc
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/m13store.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_library.c

# ---- THE OFFLINE ROM-LIBRARY HARNESS ---------------------------------------
#
# Builds and runs tools/gba_library_equiv.c with the HOST compiler, NOT the PS5
# toolchain. It #includes adapters/gba/m13store.c and adapters/gba/gba_library.c
# VERBATIM, so it proves the SHIPPING filter, path rule, dirent parser and SCAN
# LOOP -- not a paraphrase of them.
#
# ***** RUN THIS BEFORE SPENDING ANY CONSOLE TIME. *****
#
#   THE SCAN LOOP ITSELF IS UNDER TEST. A fake getdents feeds gba_lib_scan()
#   real FreeBSD-shaped records, INCLUDING DELIBERATELY MALFORMED ONES: a zero
#   reclen (which would otherwise hang the console in an infinite loop), a
#   record that overruns the returned byte count, and a name that overruns its
#   own record. None of those can be provoked safely on hardware.
#
#   THE PATH RULE IS TESTED AS A REJECTION. An over-long name must be DROPPED
#   AND COUNTED, never shortened -- a truncated path either fails to open or
#   opens a DIFFERENT file, and on a user's ROM library the second outcome is
#   indistinguishable from the picker working.
#
# -I runtime IS REQUIRED AND IS NOT A RUNTIME DEPENDENCY. m13store.c opens with
# #include "core.h" (through m13store.h). The harness pre-defines that header's
# guard (CORE_H, runtime/core.h:1-2) BEFORE including the production .c files,
# so the body expands to NOTHING -- but the preprocessor must still FIND the
# file before it can skip it. Identical in kind to m12c-equiv's -I gpsp.
#
# This changes the BUILD RULE ONLY. No adapter is modified to suit the harness
# and no test-only #ifdef exists in any production source. gba_library_equiv
# stays absent from every object list.
m13a-equiv: | $(M13ABUILD)
	@$(HOSTCC) -std=c11 -O2 -Wall -Wextra -I adapters/gba -I runtime \
	    -o $(M13ABUILD)/gba_library_equiv tools/gba_library_equiv.c
	@$(M13ABUILD)/gba_library_equiv

# ---- THE OFFLINE STAGE-0 REPORT HARNESS ------------------------------------
#
# Builds and runs tools/m13a_report_equiv.c with the HOST compiler. It
# #includes apps/m13agpsp/m13a_report.inc VERBATIM -- the SHIPPING report
# builder, not a paraphrase -- plus adapters/gba/m13store.c for the real
# m13store_errname().
#
# ***** WHY A SECOND HARNESS EXISTS. ***** M13A's evidence no longer depends on
# UDP 9027, which never delivered a datagram. It is written into a bounded
# buffer and paged onto notifications, and a bounded string builder that gets
# its limits wrong does NOT fail loudly on a console: it either overruns memory
# the Lua VM owns -- corrupting the interpreter that is about to read the report
# back -- or it prints HALF A FILESYSTEM PATH and invites the operator to
# conclude something false about their drive. Neither can be provoked safely or
# repeatably on hardware; both are trivial here.
#
# It proves the buffer is bounded, ALWAYS NUL-terminated, that an over-long line
# is dropped WHOLE rather than cut, that the truncation is stamped and counted
# rather than silent, that the VERDICT survives a full buffer, and that the sink
# reaches no kernel entry point at all.
#
# -I apps/m13agpsp finds the .inc; -I runtime is required for the same reason
# m13a-equiv needs it (the harness pre-defines CORE_H so core.h's body expands
# to nothing, but the preprocessor must still FIND the file to skip it).
m13a-report-equiv: | $(M13ABUILD)
	@$(HOSTCC) -std=c11 -O2 -Wall -Wextra -I adapters/gba -I apps/m13agpsp \
	    -I runtime -o $(M13ABUILD)/m13a_report_equiv tools/m13a_report_equiv.c
	@$(M13ABUILD)/m13a_report_equiv

# ---- objects ---------------------------------------------------------------

# m13a_report.inc is a DEPENDENCY, not just an include. It carries the report
# sink, and without it named here a change to the report builder would leave a
# stale m13amain.o in place -- i.e. the console would run code the source no
# longer describes, which is the worst possible failure mode for a diagnostic.
$(M13ABUILD)/m13amain.o: apps/m13agpsp/main.c \
                         apps/m13agpsp/m13a_report.inc \
                         adapters/gba/m13store.h \
                         adapters/gba/gba_library.h \
                         $(M13AHEADERS) | $(M13ABUILD)
	$(CC) $(M13AFLAGS) -c $< -o $@

$(M13ABUILD)/shim.o: runtime/shim.c $(M13AHEADERS) | $(M13ABUILD)
	$(CC) $(M13AFLAGS) -c $< -o $@

# ***** M13A's STORAGE LAYER. *****
#
# It resolves sceKernelOpen/Close/Lseek and getdents THROUGH runtime/core.h's
# SYM/native_call inlines, which is what lets M13A enumerate a directory WITHOUT
# MODIFYING THE FROZEN runtime/shim.c -- the shim carries no getdents and
# deliberately implements no stat/fstat (shim.c:632).
$(M13ABUILD)/m13store.o: adapters/gba/m13store.c \
                         adapters/gba/m13store.h runtime/core.h | $(M13ABUILD)
	$(CC) $(M13AFLAGS) -c $< -o $@

# ***** M13A's .gba FILTER. *****
#
# Filenames only. It opens nothing and reads no file content, so it performs NO
# ROM VALIDATION: adapters/gba/gba_rom.h already owns that gate
# (m4_rom_probe_source, the 0xEA/0x96 content checks) and has since M4. A second
# validator that disagreed with the first would be worse than none.
$(M13ABUILD)/gba_library.o: adapters/gba/gba_library.c \
                            adapters/gba/gba_library.h \
                            adapters/gba/m13store.h | $(M13ABUILD)
	$(CC) $(M13AFLAGS) -c $< -o $@

m13a-objs: $(M13AOBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- exactly as M1-M12C.
# Every object is named on the link line and is included in its ENTIRETY.
$(M13AELF): $(M13AOBJS) runtime/linker.ld | $(M13ABUILD)
	$(CC) $(BASEFLAGS) $(M13ALDFLAGS) -o $@ $(M13AOBJS)

m13a-link: $(M13AOBJS) | $(M13ABUILD)
	@$(CC) $(BASEFLAGS) $(M13ALDFLAGS) -o $(M13AELF) $(M13AOBJS) \
	     > $(M13ABUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to .[^']+'" $(M13ABUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M13ABUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M13A LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M13A LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M13A LINK OK: linker exit 0, no undefined references"

# --arena 65536. M13A's arena is DELIBERATELY 64 KB rather than M4-M12C's 4 MB:
# nothing in this image calls malloc, and the arena exists only because
# shim_init() takes one.
$(M13ABIN): $(M13AELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M13AELF) $@ \
	   --map $(M13ABUILD)/$(M13ATARGET).map --budget $(JIT_LIMIT) --arena 65536

m13a: $(M13ABIN)

m13a-size: $(M13AELF) $(M13ABIN)
	@$(PYTHON) tools/sizereport.py $(M13AELF) $(M13ABIN) \
	   --map $(M13ABUILD)/$(M13ATARGET).map --budget $(JIT_LIMIT) --arena 65536

# ---- verification ----------------------------------------------------------
#
# EVERY STRUCTURAL GATE M1-M12C APPLY IS KEPT, plus five that are specific to a
# milestone whose entire claim is "this image only LOOKS at directories".

M13A_FORBIDDEN_WRITE = plat_savedata_begin_write plat_savedata_end_write \
                       plat_savedata_mkdir plat_savedata_init \
                       plat_savedata_is_ready

# ***** M13A MUST NOT DEPEND ON stat/fstat. ***** runtime/shim.c:632 records
# that they are deliberately unimplemented, so an M13A that referenced one would
# not fail here -- it would fail on the console, at the worst moment.
M13A_FORBIDDEN_STAT = stat fstat lstat fstatat

# The gpSP save globals and the M12 adapters. NONE may appear: M12 is FROZEN and
# M13A must be provably incapable of touching a save.
M13A_FORBIDDEN_SAVE = gba_save_latch gba_restore_apply gba_restorefile_run \
                      gamepak_backup backup_type eeprom_size

# ***** M13A MUST NOT BE ABLE TO WRITE A BYTE TO A FILE. *****
#
# fwrite is the only function in runtime/shim.c that reaches raw_write() for a
# real FILE*. The other three are listed because they are the INDIRECT routes to
# it -- shim.c:583, :590 and :628 each call fwrite when handed a file that is
# not stdout/stderr -- so an object could write bytes without ever naming
# fwrite itself. Forbidding the reference forbids the capability.
#
# fopen is deliberately NOT here. M13A opens directories through open() in
# m13store.c, and a future stage that reads a ROM header would legitimately need
# fopen; the read-only claim is about WRITING, and overreaching would make this
# gate something a later stage has to argue with rather than satisfy.
M13A_FORBIDDEN_FWRITE = fwrite fputc fputs fprintf

m13a-verify: $(M13AELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M13AELF)
	@echo "== relocations (NON-MASKING pipeline) =="
	@$(PYTHON) tools/verify_reloc.py $(M13AELF) $(M13AOBJS) \
	     > $(M13ABUILD)/reloc.log 2>&1; \
	 st=$$?; \
	 tail -12 $(M13ABUILD)/reloc.log; \
	 if [ $$st -ne 0 ]; then \
	     echo "M13A VERIFY FAILED: the relocation check exited $$st."; \
	     echo "  Full output: $(M13ABUILD)/reloc.log"; \
	     exit $$st; \
	 fi
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M13AOBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M13AOBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M13AELF) $(M13AOBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M13AELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M13A VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== ***** NO EMULATOR IS LINKED -- THE DEFINING GATE OF M13A ***** =="
	@bad=""; \
	 for o in $(M13AOBJS); do \
	     case "$$o" in \
	       */gpsp_*.o|*/gba_rom.o|*/gba_exec.o|*/gba_m8map.o|*/gba_m10map.o) \
	           bad="$$bad $$o";; \
	     esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13A VERIFY FAILED: an emulator or ROM-loader object is in the"; \
	     echo "  M13A link line:$$bad"; \
	     echo "  M13A is a STORAGE PROBE. It must be structurally incapable of"; \
	     echo "  loading a cartridge or executing an instruction."; \
	     exit 1; \
	 fi; \
	 echo "no gpSP, ROM-loader, exec or mapping object is linked"
	@echo "== ***** THERE IS NO WRITE PATH AND NO SAVE PATH ***** =="
	@bad=""; \
	 for o in $(M13AOBJS); do \
	     case "$$o" in \
	       */savedata.o|*/gba_savefile.o|*/gba_save.o|*/gba_savehdr.o|*/gba_restore.o|*/gba_restorefile.o|*/gba_m11save.o) \
	           bad="$$bad $$o";; \
	     esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13A VERIFY FAILED: a save-side object is in the M13A link"; \
	     echo "  line:$$bad"; \
	     echo "  M12 IS FROZEN AND HARDWARE-PASSED. M13A must not be able to"; \
	     echo "  read, write or perturb a save."; \
	     exit 1; \
	 fi; \
	 echo "no savedata, savefile, save, savehdr or restore object is linked"
	@bad=""; \
	 for o in $(M13AOBJS); do \
	     for s in $(M13A_FORBIDDEN_WRITE) $(M13A_FORBIDDEN_SAVE); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13A VERIFY FAILED: an object references a write-window or save"; \
	     echo "  entry point:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "NO object names a write-window entry point or a gpSP save global"
	@echo "== ***** NO stat/fstat DEPENDENCY ***** =="
	@bad=""; \
	 for o in $(M13AOBJS); do \
	     for s in $(M13A_FORBIDDEN_STAT); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13A VERIFY FAILED: an object references stat/fstat:$$bad"; \
	     echo "  runtime/shim.c:632 deliberately does NOT implement them, so"; \
	     echo "  this would link cleanly and fail on the console. M13A gets"; \
	     echo "  entry types from getdents' d_type instead."; \
	     exit 1; \
	 fi; \
	 echo "no object depends on stat, fstat, lstat or fstatat"
	@echo "== ***** NO fwrite REFERENCE FROM ANY M13A OBJECT ***** =="
# This gate used to run `$(NM) $(M13AELF) | grep -qw fwrite` -- every symbol in
# the linked image, definitions included. IT COULD NEVER PASS, and it could
# never fail for a reason worth knowing either:
#
#   * runtime/shim.c:445 DEFINES fwrite. shim.c is FROZEN and shim.o is
#     mandatory -- it carries klog, malloc, memcpy and the entire string layer.
#   * Makefile:496 records that the authoritative link uses NO --gc-sections and
#     names whole objects, so EVERY byte of shim.o is in the image whether or
#     not one instruction reaches it.
#
# So the definition's presence was a fact about the link model, not about M13A.
# The same is true of stat and fstat -- also defined by shim.o, also in the
# image -- and the gate directly above this one gets that right by asking
# `$(NM) -u` PER OBJECT: a reference, not a definition. M12C:8414 checks
# gba_restorefile.o for fwrite the same way. This gate was the only one in the
# M13A verifier that had drifted from that pattern; it is now back on it.
	@bad=""; \
	 for o in $(M13AOBJS); do \
	     for s in $(M13A_FORBIDDEN_FWRITE); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13A VERIFY FAILED: an object REFERENCES a file-write entry"; \
	     echo "  point:$$bad"; \
	     echo "  M13A reads directory entries. It writes NOTHING."; \
	     exit 1; \
	 fi; \
	 echo "no M13A object references fwrite, fputc, fputs or fprintf"
# The reference check above is only meaningful while fwrite has exactly one
# owner. If a NEW object ever carried its own copy, every M13A object could stay
# reference-free and the image would still have gained a writer. So: no object
# but the frozen shim may define it, and the shim must still define it -- that
# second half stops the gate degrading into a vacuous PASS if $(NM) changes
# spelling, the symbol is renamed, or shim.o is swapped for a stub.
	@bad=""; \
	 for o in $(M13AOBJS); do \
	     case "$$o" in *shim.o) continue ;; esac; \
	     if $(NM) --defined-only $$o 2>/dev/null | grep -qw "fwrite"; then \
	         bad="$$bad $$o"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13A VERIFY FAILED: fwrite is DEFINED outside the frozen shim,"; \
	     echo "  by:$$bad"; \
	     echo "  A second definition means the image gained a writer that the"; \
	     echo "  reference check above cannot see."; \
	     exit 1; \
	 fi; \
	 if ! $(NM) --defined-only $(M13ABUILD)/shim.o 2>/dev/null | grep -qw "fwrite"; then \
	     echo "M13A VERIFY FAILED: shim.o does not define fwrite."; \
	     echo "  This gate's premise no longer holds, so its PASS would be"; \
	     echo "  VACUOUS -- it would be reporting absence it never measured."; \
	     exit 1; \
	 fi; \
	 echo "fwrite is defined ONLY by the frozen shim, and referenced by NO M13A"; \
	 echo "  object -- it is in the image because the link takes whole objects,"; \
	 echo "  not because M13A can reach it"
	@echo "== rodata carve (marker-based, as M7-M12C) =="
	@$(OBJCOPY) -O binary --only-section=.text $(M13AELF) $(M13ABUILD)/text.bin
	@tx=$$($(NM) $(M13AELF) | awk '$$NF == "__text_start"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 ra=$$($(NM) $(M13AELF) | awk '$$NF == "__rodata_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 rz=$$($(NM) $(M13AELF) | awk '$$NF == "__rodata_end"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 if [ -z "$$tx" ] || [ -z "$$ra" ] || [ -z "$$rz" ]; then \
	     echo "M13A VERIFY FAILED: linker marker symbol missing."; \
	     exit 1; \
	 fi; \
	 t=$$((0x$$tx)); a=$$((0x$$ra)); z=$$((0x$$rz)); n=$$((z - a)); \
	 if [ "$$t" -ne 0 ]; then \
	     echo "M13A VERIFY FAILED: .text does not start at vaddr 0."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M13A VERIFY FAILED: empty rodata window."; \
	     exit 1; \
	 fi; \
	 if ! dd if=$(M13ABUILD)/text.bin of=$(M13ABUILD)/rodata.bin bs=1 skip=$$a count=$$n 2>/dev/null; then \
	     echo "M13A VERIFY FAILED: dd could not carve the rodata window."; \
	     exit 1; \
	 fi; \
	 if [ ! -s $(M13ABUILD)/rodata.bin ]; then \
	     echo "M13A VERIFY FAILED: rodata extraction produced 0 bytes -- every"; \
	     echo "  check below would pass VACUOUSLY. Fix the extraction."; \
	     exit 1; \
	 fi; \
	 echo "rodata carved from .text by linker markers: [$$a,$$z) = $$n bytes"
	@echo "== ***** THE getdents ARRANGEMENT IS PRESENT ***** =="
	@miss=""; \
	 for s in "sceKernelGetdents" "getdents" "sceKernelOpen" "sceKernelClose"; do \
	     grep -aq "$$s" $(M13ABUILD)/rodata.bin || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M13A VERIFY FAILED: the image does not name:$$miss"; \
	     echo "  These are resolved BY STRING through dlsym, so if a name is"; \
	     echo "  absent the resolution silently returns NULL and every probe"; \
	     echo "  reports a failure that has nothing to do with USB."; \
	     echo "  NOTE both spellings are required: 'getdents' is the FALLBACK"; \
	     echo "  and is NOT a substring of 'sceKernelGetdents' (capital G)."; \
	     exit 1; \
	 fi; \
	 echo "both getdents spellings and the open/close names are present"
	@echo "== ***** THE ROM DIRECTORY THIS MILESTONE OPENS IS NAMED ***** =="
	@if ! grep -aq "LUAport/roms/gba" $(M13ABUILD)/rodata.bin; then \
	     echo "M13A VERIFY FAILED: 'LUAport/roms/gba' is not in the image."; \
	     echo "  Without it no ROM directory can be built, and every"; \
	     echo "  'ROM DIRECTORY NOT FOUND' result would be VACUOUS -- which"; \
	     echo "  looks exactly like a correct 'the user has not made the"; \
	     echo "  folder yet' outcome."; \
	     exit 1; \
	 fi; \
	 echo "LUAport/roms/gba is present"
	@if ! grep -aq "/temp0" $(M13ABUILD)/rodata.bin; then \
	     echo "M13A VERIFY FAILED: the /temp0 POSITIVE CONTROL is not in the"; \
	     echo "  image. Without it, a total probe failure cannot be told apart"; \
	     echo "  from a broken probe, and the run proves NOTHING."; \
	     exit 1; \
	 fi; \
	 echo "/temp0 positive control is present"
	@echo "== ***** THE STAGE-0B EVIDENCE PATHS ARE NAMED ***** =="
# Same anti-vacuity argument as the /temp0 gate directly above. These paths are
# opened BY STRING, so if a literal were optimised away or misspelled the probe
# would report a failure that had nothing to do with the namespace -- and a
# "/dev is not visible" result is exactly the kind of finding that would be
# acted on. #ERR and #DEV are checked for the same reason: the Lua side splits
# the report on those exact tags, and a missing tag would silently drop a whole
# section from the notifications while everything still "passed".
	@miss=""; \
	 for s in "/dev" "/t5Y3jZjLdU" "/sce_sbx" "#ERR" "#DEV"; do \
	     grep -aq "$$s" $(M13ABUILD)/rodata.bin || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M13A VERIFY FAILED: a Stage-0B evidence name is absent:$$miss"; \
	     echo "  These are enumerated by string and reported under those exact"; \
	     echo "  section tags. A missing one turns a real result into a"; \
	     echo "  VACUOUS one."; \
	     exit 1; \
	 fi; \
	 echo "/dev, both unexplained roots and the #ERR/#DEV tags are present"
	@echo "== ***** NO DEVICE NODE PATH IS BUILT ***** =="
# M13A reads device NAMES and must never turn one into a path it opens. The
# collected names live in exactly one array, m13a_devname[], written by
# m13a_dev_collect() and read ONLY by m13a_rep_devices() when it prints them.
# If the fixture itself ever referenced that array it would be one m13store_join
# away from opening a raw device node, which is the single thing this stage
# promised not to do. The promise is therefore checked instead of asserted.
	@if grep -n "m13a_devname" apps/m13agpsp/main.c >/dev/null 2>&1; then \
	    echo "M13A VERIFY FAILED: apps/m13agpsp/main.c references"; \
	    echo "  m13a_devname[] -- a name harvested from /dev must never leave"; \
	    echo "  the report sink. Stage 0B enumerates device names; it does NOT"; \
	    echo "  open, read, ioctl or mount a device node."; \
	    exit 1; \
	fi; \
	echo "no collected /dev name is reachable from the fixture"
	@echo "== ***** NO SAVE PATH REACHED THE IMAGE ***** =="
	@bad=""; \
	 for s in "/savedata0" "sceSaveData" "libSceSaveData" ".hdr"; do \
	     if grep -aq "$$s" $(M13ABUILD)/rodata.bin; then bad="$$bad $$s"; fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13A VERIFY FAILED: a save-side name reached the image:$$bad"; \
	     echo "  M12 IS FROZEN. M13A names no save path, opens no container"; \
	     echo "  and links no savedata layer -- so no such string can be"; \
	     echo "  present unless an object list changed."; \
	     exit 1; \
	 fi; \
	 echo "no /savedata0, sceSaveData or .hdr name is present"
	@echo "== M13A VERIFY OK =="

# ---- the Lua payload -------------------------------------------------------

M13A_LUA_TEMPLATE := lua/m13a.lua.in
M13A_LUA_OUT      := $(M13ABUILD)/$(M13ATARGET).lua

# NO STAGE. See the note at the top of this block: M13A's stages are strictly
# dependent, so there is nothing to select and nothing that can be shipped
# wrong.
m13a-lua: m13a-check $(M13ABIN) $(M13AELF)
	@$(PYTHON) tools/mklua.py $(M13A_LUA_TEMPLATE) $(M13ABIN) $(M13A_LUA_OUT) $(M13AELF)
	@echo "M13A LUA OK: $(M13A_LUA_OUT) -- one run, stages 0 -> 1 -> 2"

# ---- m13a-send -- HARDWARE DELIVERY ----------------------------------------
#
# NOT AUTHORISED BY THE EXISTENCE OF THIS TARGET.
#
#   * RUN `make m13a-equiv` FIRST. It is the only check that can catch a dirent
#     parser that loops forever on a zero reclen, or a path rule that truncates
#     instead of refusing.
#   * ***** M13A CANNOT DAMAGE ANYTHING. ***** It opens directories read-only,
#     reads no file content and links no write path at all. The worst outcome is
#     a report that no USB is visible.
#   * PREPARE THE DRIVE FIRST: create LUAport/roms/gba/ on an exFAT USB stick
#     and put ONE known-good .gba in it. DO NOT rename it -- M13A preserves the
#     user's filename, and the M12 save identity is CONTENT-BASED, so renaming
#     or moving a ROM can never change which .sav it uses.
#   * ***** CAPTURE THE UDP LOG ON 9027. IT IS THE DELIVERABLE. ***** The
#     notification carries a verdict; the mount table, the "/" listing and the
#     per-candidate errno values exist only in the log.
#   * RELAUNCH THE HOST GAME BETWEEN RUNS, as at every previous milestone.
m13a-send: m13a-verify m13a-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m13a-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M13A_LUA_OUT) $(M13ABIN)

m13a-clean:
	rm -rf $(M13ABUILD)

.PHONY: m13a m13a-objs m13a-check m13a-equiv m13a-report-equiv m13a-link \
        m13a-size m13a-verify m13a-lua m13a-send m13a-clean

# ============================================================================
# M13B -- THE STAGED ROM PICKER
# ============================================================================
#
#   make m13b-equiv      THE OFFLINE HARNESS -- run this first, run it always
#   make m13b-upload ROM=<file>   place a cartridge in /temp0 (QUOTED recipe)
#   make m13b-lua STAGE=<0|1|2|3|35>
#   make m13b-send PS5_IP=<addr> STAGE=<...>
#
# THE HARNESS IS THE GATE AND IT IS WIRED AS ONE: m13b-verify depends on
# m13b-equiv, and m13b-send depends on m13b-verify. There is no ordering the
# operator has to remember, because the dependency graph enforces it.
#
# ***** THE FIVE RUNGS, AND WHY THERE IS A SELECTOR AT ALL. *****
# M13A refused a stage selector because its stages were strictly dependent.
# M13B's rungs are dependent too, but they differ in something M13A's did not:
# EACH ONE ENDS AT A DIFFERENT POINT OF NO RETURN. Rung 0 opens nothing; rung 2
# opens and measures the operator's files; rung 3 hands a user-chosen path to
# gpSP. An operator must be able to stop BEFORE the next one.
#
#    STAGE=0    enumerate only, no ROM opened
#    STAGE=1    the picker UI; CROSS does not launch
#    STAGE=2    validate + select; latches the path; still no load
#    STAGE=3    load the chosen cartridge; NO execute_arm()
#    STAGE=35   rung 3 then reset_gba() #2 and re-verify the mapping
#
# 35 rather than 4, so a photographed screen and a captured log say "35" and can
# never be read as a fifth rung this milestone does not have.
# ----------------------------------------------------------------------------

M13BTARGET := m13bgpsp
M13BBUILD  := build/m13b
M13BELF    := $(M13BBUILD)/$(M13BTARGET).elf
M13BBIN    := $(M13BBUILD)/$(M13BTARGET).bin

$(M13BBUILD):
	mkdir -p $(M13BBUILD)

# ---- THE OFFLINE PICKER + NAMED-ROM HARNESS --------------------------------
#
# Builds and runs tools/m13b_picker_equiv.c with the HOST compiler, NOT the PS5
# toolchain. It #includes FOUR SHIPPING SOURCES VERBATIM -- adapters/gba/
# m13store.c, adapters/gba/gba_library.c, adapters/gba/gba_rom.c and
# apps/m13bgpsp/m13b_picker.inc -- so it proves the code that ships rather than
# a paraphrase of it.
#
# ***** RUN THIS BEFORE SPENDING ANY CONSOLE TIME. *****
#
#   THE REFUSAL IS PROVED BY COUNTING gpSP CALLS. M13B is the first milestone
#   in which a path the USER chose reaches load_gamepak(). An over-long path
#   that were silently shortened would not fail loudly -- it would OPEN A
#   DIFFERENT FILE, which on a ROM library is indistinguishable from the picker
#   working. The harness puts BOTH a 255-byte and a 256-byte path on its
#   synthetic drive, so the only thing that can stop the second one is the
#   refusal itself, and then asserts that gpSP was called ZERO times and that
#   no truncated neighbour was ever opened.
#
#   THE PICKER MATH IS TESTED WHERE IT CAN ACTUALLY BE TESTED. A scroll window
#   that is off by one shows the operator a plausible list with the wrong row
#   highlighted, and a comparator that is not a total order makes the on-screen
#   order change between runs on an unchanged drive. Neither can be provoked
#   reliably on hardware.
#
# THREE -I FLAGS, ALL REQUIRED, NONE A RUNTIME DEPENDENCY. The harness
# pre-defines the include guards of gpsp/common.h, of the libretro-common
# streams/file_stream.h and of runtime/core.h, so all three expand to NOTHING --
# but the preprocessor must still FIND each file before it can skip it. This is
# the same technique as m12c-equiv's -I gpsp and m13a-equiv's -I runtime, used
# together because M13B is the first milestone whose logic spans both sides of
# that type boundary.
#
# TWO SOURCE FILES, FOR A LANGUAGE REASON AND NOT A STYLE ONE.
# adapters/gba/gba_rom.c:98-101 declares gpSP's gamepak_blk_queue through an
# UNTAGGED struct. Two untagged structs written in the SAME translation unit are
# incompatible types, so the harness cannot define that storage beside the
# extern; C11 6.2.7p1 makes them compatible ACROSS translation units, which is
# the very rule gba_rom.c:90-97 already cites for its own extern. Hence
# tools/m13b_blkq.c, which defines that one object and nothing else.
#
# This changes the BUILD RULE ONLY. No adapter is modified to suit the harness
# and no test-only #ifdef exists in any production source. Neither
# m13b_picker_equiv nor m13b_blkq appears in any object list.
m13b-equiv: | $(M13BBUILD)
	@$(HOSTCC) -std=c11 -O2 -Wall -Wextra \
	    -I adapters/gba -I apps/m13bgpsp -I runtime -I gpsp \
	    -I gpsp/libretro/libretro-common/include \
	    -o $(M13BBUILD)/m13b_picker_equiv \
	    tools/m13b_picker_equiv.c tools/m13b_blkq.c
	@$(M13BBUILD)/m13b_picker_equiv

# ---- flags -----------------------------------------------------------------
#
# IDENTICAL IN SHAPE TO M12C's. -Iruntime finds core.h/shim.h/platform.h/gfx.h/
# tables.h; -Iadapters/gba finds the four typedef-free boundary headers;
# -Iapps/m13bgpsp finds m13b_picker.inc.
M13B_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m13bgpsp \
                -Iadapters/gba

M13BFLAGS = $(BASEFLAGS) $(M13B_INCLUDES) -Wall -Wno-unused-function

# UNCHANGED FROM M4's THROUGH M12C's. M13B adds NO new compile flag and changes
# NO existing one. ***** -DROM_BUFFER_SIZE=2 IS NOT ALTERED BY THIS MILESTONE --
# it is what makes a cartridge over 2 MB demand paged, and it is why the save
# identity must be latched BEFORE execution. *****
M13B_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M13B_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M13BHEADERS = $(HEADERS)

M13BLDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
              -Wl,--build-id=none -Wl,-z,norelro \
              -Wl,--orphan-handling=error \
              -Wl,-Map=$(M13BBUILD)/$(M13BTARGET).map

# ----------------------------------------------------------------- objects --
#
# ***** NOTE WHAT IS ABSENT. EVERY ABSENCE IS A GATE, NOT AN OVERSIGHT. *****
#
#   gba_exec.o        THE DEFINING ONE. It is the ONLY translation unit in this
#                     repository that reaches execute_arm(). Without it on the
#                     link line, M13B is STRUCTURALLY INCAPABLE of executing an
#                     emulated instruction -- the capability is absent, not
#                     merely unused. M13B-4 adds it back deliberately.
#   savedata.o        no write window can be opened
#   gba_savefile.o    M12B's WRITER -- M13B writes no save
#   gba_savehdr.o     header parsing belongs to M12C's RESTORE path
#   gba_restore*.o    likewise
#   gba_m8map.o       keypad mapping -- nothing is played
#   gba_m10map.o      audio mapping  -- no audio port is opened
#   gba_present.o     the GBA->framebuffer blit -- no GBA frame is ever produced
#   gba_input.o       keypad -- the pad drives a MENU here, not a cartridge
#   audio.o           no sound is emitted
#
# gba_save.o IS present, and for exactly ONE read-only call: gba_save_latch(),
# which derives the CONTENT-BASED save identity so a hardware run can prove the
# identity did not become filename-dependent. It opens no file.
M13B_OWN_OBJS = $(M13BBUILD)/m13bmain.o \
                $(M13BBUILD)/shim.o \
                $(M13BBUILD)/gfx.o \
                $(M13BBUILD)/platform.o \
                $(M13BBUILD)/m13store.o \
                $(M13BBUILD)/gba_library.o \
                $(M13BBUILD)/gba_fixture.o \
                $(M13BBUILD)/gba_filestream.o \
                $(M13BBUILD)/gba_compat.o \
                $(M13BBUILD)/gba_probe.o \
                $(M13BBUILD)/gba_bios.o \
                $(M13BBUILD)/gba_rom.o \
                $(M13BBUILD)/gba_save.o

M13B_GPSP_OBJS = $(M13BBUILD)/gpsp_cpu.o $(M13BBUILD)/gpsp_main.o \
                 $(M13BBUILD)/gpsp_gba_memory.o $(M13BBUILD)/gpsp_video.o \
                 $(M13BBUILD)/gpsp_sound.o $(M13BBUILD)/gpsp_serial.o \
                 $(M13BBUILD)/gpsp_serial_proto.o $(M13BBUILD)/gpsp_rfu.o \
                 $(M13BBUILD)/gpsp_gbp.o $(M13BBUILD)/gpsp_cheats.o \
                 $(M13BBUILD)/gpsp_savestate.o $(M13BBUILD)/gpsp_input.o

M13BOBJS = $(M13B_OWN_OBJS) $(M13B_GPSP_OBJS)

# ---- offline structural check ----------------------------------------------
#
# Deliberately NOT pointed at lua/m13b.lua.in, for m9-check's reason: Lua
# comments start with --, which the tool does not know, so every apostrophe in a
# Lua comment would lex as an unterminated char literal and the check would FAIL
# ON CORRECT INPUT.
m13b-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m13bgpsp/main.c
	@$(PYTHON) tools/m8_syntax_check.py apps/m13bgpsp/m13b_picker.inc

# ---- objects ---------------------------------------------------------------
#
# m13b_picker.inc is a DEPENDENCY, not merely an include. Without it named here
# a change to the picker math would leave a stale m13bmain.o in place -- the
# console would run code the source no longer describes, which is the worst
# possible failure mode for a fixture whose whole job is to be believed.
$(M13BBUILD)/m13bmain.o: apps/m13bgpsp/main.c \
                         apps/m13bgpsp/m13b_picker.inc \
                         adapters/gba/m13store.h \
                         adapters/gba/gba_library.h \
                         adapters/gba/gba_rom.h \
                         adapters/gba/gba_probe.h \
                         adapters/gba/gba_bios.h \
                         adapters/gba/gba_save.h \
                         $(M13BHEADERS) | $(M13BBUILD)
	$(CC) $(M13BFLAGS) -c $< -o $@

$(M13BBUILD)/shim.o: runtime/shim.c $(M13BHEADERS) | $(M13BBUILD)
	$(CC) $(M13BFLAGS) -c $< -o $@

# ***** THE UI RENDERER, AND THE FIRST MILESTONE SINCE M9 TO LINK IT. *****
# It draws into the app's CALLER-OWNED 480x270 surface -- gfx.c takes `u32 *scr`
# and allocates nothing -- so the 518,400 bytes land in m13bmain.o's .bss and
# gfx.o itself stays tiny.
$(M13BBUILD)/gfx.o: runtime/gfx.c $(M13BHEADERS) | $(M13BBUILD)
	$(CC) $(M13BFLAGS) -c $< -o $@

# ***** VIDEO AND PAD COME UP BEFORE THE ROM BUFFERS AT THIS MILESTONE, WHICH
# THEY NEVER DID AT M4-M12C. ***** m4_rom_buffer_init() is greedy, so anything
# that consumed the shim arena first would reduce the block count. platform.c
# names no malloc and takes its display memory from sceKernelAllocateDirectMemory
# -- but that is an argument, not a measurement, so rung 0 MEASURES it.
$(M13BBUILD)/platform.o: runtime/platform.c $(M13BHEADERS) | $(M13BBUILD)
	$(CC) $(M13BFLAGS) -c $< -o $@

# M13A's storage and filter layers, LINKED FROM BYTE-IDENTICAL, UNMODIFIED
# SOURCE. M13B changes neither file.
$(M13BBUILD)/m13store.o: adapters/gba/m13store.c \
                         adapters/gba/m13store.h runtime/core.h | $(M13BBUILD)
	$(CC) $(M13BFLAGS) -c $< -o $@

$(M13BBUILD)/gba_library.o: adapters/gba/gba_library.c \
                            adapters/gba/gba_library.h \
                            adapters/gba/m13store.h | $(M13BBUILD)
	$(CC) $(M13BFLAGS) -c $< -o $@

# ---- the gpSP-facing adapters ----------------------------------------------
#
# All compiled with the gpSP flags because each includes gpsp/common.h and must
# see the same gpSP configuration every gpSP object sees.

$(M13BBUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

$(M13BBUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

$(M13BBUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

$(M13BBUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

$(M13BBUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

# ***** THE ONE ADAPTER M13B EXTENDED. ***** m4_rom_query_named() and
# m4_rom_load_named() were added here, and the LEGACY INDEX ENTRY POINTS NOW
# DELEGATE TO THEM -- so there is ONE implementation of the measurement and ONE
# call to load_gamepak(), and the index path cannot drift from the path path.
# tools/m13b_picker_equiv.c proves the two produce byte-identical state.
$(M13BBUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

# M12B's SAVE ADAPTER, LINKED FROM BYTE-IDENTICAL, UNMODIFIED, FROZEN SOURCE.
# M13B calls gba_save_latch() and the readers. IT CALLS NO WRITER.
$(M13BBUILD)/gba_save.o: adapters/gba/gba_save.c adapters/gba/gba_save.h | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects -----------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, IDENTICAL to M12C's.
# gpsp/** is never modified and its own Makefile is never invoked. M13B needed
# ZERO changes here.

$(M13BBUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M13BBUILD)
	$(CXX) $(M13B_GPSPCXXFLAGS) -c $< -o $@

$(M13BBUILD)/gpsp_video.o: gpsp/video.cc | $(M13BBUILD)
	$(CXX) $(M13B_GPSPCXXFLAGS) -c $< -o $@

$(M13BBUILD)/gpsp_main.o: gpsp/main.c | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

$(M13BBUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

$(M13BBUILD)/gpsp_sound.o: gpsp/sound.c | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

$(M13BBUILD)/gpsp_serial.o: gpsp/serial.c | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

$(M13BBUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

$(M13BBUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

$(M13BBUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

$(M13BBUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

$(M13BBUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

$(M13BBUILD)/gpsp_input.o: gpsp/input.c | $(M13BBUILD)
	$(CC) $(M13B_GPSPFLAGS) -c $< -o $@

m13b-objs: $(M13BOBJS)

# ---- link ------------------------------------------------------------------
#
# LINKED WITH $(CC), NOT $(CXX), and with NO --gc-sections -- exactly as
# M1-M13A. Every object is named on the link line and is included in its
# ENTIRETY, so "gba_exec.o is absent" is a fact about the image and not about
# what the linker felt like keeping.
$(M13BELF): $(M13BOBJS) runtime/linker.ld | $(M13BBUILD)
	$(CC) $(BASEFLAGS) $(M13BLDFLAGS) -o $@ $(M13BOBJS)

m13b-link: $(M13BOBJS) | $(M13BBUILD)
	@$(CC) $(BASEFLAGS) $(M13BLDFLAGS) -o $(M13BELF) $(M13BOBJS) \
	     > $(M13BBUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to .[^']+'" $(M13BBUILD)/link.log \
	            | sed "s/.*\`//; s/'$$//" | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	 fi; \
	 if [ $$status -ne 0 ]; then \
	     echo "---- linker output ----"; \
	     cat $(M13BBUILD)/link.log; \
	     echo "-----------------------"; \
	     echo "M13B LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M13B LINK FAILED: undefined references reported despite exit 0"; \
	     exit 1; \
	 fi; \
	 echo "M13B LINK OK: linker exit 0, no undefined references"

# --arena 4194304, IDENTICAL TO M4's THROUGH M12C's.
$(M13BBIN): $(M13BELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M13BELF) $@ \
	   --map $(M13BBUILD)/$(M13BTARGET).map --budget $(JIT_LIMIT) --arena 4194304

m13b: $(M13BBIN)

# THE TWO HARD CEILINGS. sizereport.py enforces the JIT budget ($(JIT_LIMIT) =
# 524,288) and tools/check_forbidden.py enforces the 2,097,152-byte .bss
# tripwire.
#
# ***** WATCH __data_load, NOT JUST .bss. ***** runtime/linker.ld:146 sets
# __data_start = ALIGN(0x10000) + 0x10000, so the JIT reservation is driven by
# .text + .rodata + .rela and NOT by .bss. M13B adds a UI renderer and a picker
# to an M12C-shaped image; if text+rodata crosses the next 64 KB bracket,
# __data_start moves and the JIT footprint moves with it.
m13b-size: $(M13BELF) $(M13BBIN)
	@$(PYTHON) tools/sizereport.py $(M13BELF) $(M13BBIN) \
	   --map $(M13BBUILD)/$(M13BTARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification ----------------------------------------------------------
#
# EVERY STRUCTURAL GATE M1-M13A APPLY IS KEPT, plus five that are specific to a
# milestone whose claim is "the operator chooses a cartridge and NOTHING RUNS".

# ***** M13B MUST NOT BE ABLE TO WRITE A BYTE TO A FILE. ***** fwrite is the
# only function in runtime/shim.c that reaches raw_write() for a real FILE*; the
# other three are the INDIRECT routes to it (shim.c:583, :590, :628). Forbidding
# the reference forbids the capability.
#
# fopen is deliberately NOT here: gba_rom.c and gba_bios.c legitimately open
# cartridges and BIOS images for READING. The claim is about WRITING.
M13B_FORBIDDEN_FWRITE = fwrite fputc fputs fprintf

# The M12 WRITE path and the PS5 savedata layer. NONE may appear.
M13B_FORBIDDEN_SAVE = plat_savedata_begin_write plat_savedata_end_write \
                      plat_savedata_mkdir plat_savedata_init \
                      gba_savefile_run gba_restorefile_run gba_restore_apply

# ***** THE DEFINING GATE. ***** execute_arm is gpSP's own symbol and IS present
# in gpsp_cpu.o -- that is unavoidable without modifying gpsp/**, which is
# forbidden. What M13B guarantees is that NO LUAport OBJECT REFERENCES IT, which
# is enforced below against gba_exec.o's absence from the link line rather than
# against the symbol table.
M13B_FORBIDDEN_EXEC = m5_exec_run gba_exec_run

m13b-verify: m13b-equiv $(M13BELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M13BELF)
	@echo "== relocations (NON-MASKING pipeline) =="
	@$(PYTHON) tools/verify_reloc.py $(M13BELF) $(M13BOBJS) \
	     > $(M13BBUILD)/reloc.log 2>&1; \
	 st=$$?; \
	 tail -12 $(M13BBUILD)/reloc.log; \
	 if [ $$st -ne 0 ]; then \
	     echo "M13B VERIFY FAILED: the relocation check exited $$st."; \
	     echo "  Full output: $(M13BBUILD)/reloc.log"; \
	     exit $$st; \
	 fi
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M13BOBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M13BOBJS)
	@echo "== forbidden symbols in the linked image =="
	@$(PYTHON) tools/check_forbidden.py $(M13BELF) $(M13BOBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M13BELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M13B VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== ***** NO EXECUTION PATH IS LINKED -- THE DEFINING GATE ***** =="
	@bad=""; \
	 for o in $(M13BOBJS); do \
	     case "$$o" in \
	       */gba_exec.o|*/gba_m8map.o|*/gba_m10map.o|*/gba_present.o|*/gba_input.o|*/audio.o) \
	           bad="$$bad $$o";; \
	     esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13B VERIFY FAILED: an execution, keypad, audio or presentation"; \
	     echo "  object is in the M13B link line:$$bad"; \
	     echo "  M13B LOADS A CARTRIDGE AND STOPS. It must be structurally"; \
	     echo "  incapable of executing an instruction."; \
	     exit 1; \
	 fi; \
	 echo "gba_exec.o is ABSENT -- execute_arm() is unreachable from this image"
	@echo "== ***** THERE IS NO WRITE PATH AND NO SAVE PATH ***** =="
	@bad=""; \
	 for o in $(M13BOBJS); do \
	     case "$$o" in \
	       */savedata.o|*/gba_savefile.o|*/gba_savehdr.o|*/gba_restore.o|*/gba_restorefile.o|*/gba_m11save.o) \
	           bad="$$bad $$o";; \
	     esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13B VERIFY FAILED: a save writer or restorer is linked:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "no savedata, savefile, restore or M11 observer object is linked"
# The SAVE and EXEC lists stay on the final-ELF PRESENCE test, and that is the
# correct predicate for them: every symbol in both lists is LUAport-owned and
# lives in an object that is already off the link line (gba_exec.o, savedata.o,
# gba_savefile.o, ...), so "absent from the image" is a STRONGER claim than
# "unreferenced" and it currently holds. Weakening these to reference checks
# would discard real safety for nothing.
	@for s in $(M13B_FORBIDDEN_SAVE) $(M13B_FORBIDDEN_EXEC); do \
	     if $(READELF) -sW $(M13BELF) 2>/dev/null \
	          | awk '$$7 != "UND" { print $$8 }' | grep -qx "$$s"; then \
	         echo "M13B VERIFY FAILED: forbidden symbol '$$s' is defined in the"; \
	         echo "  linked image."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "save/exec forbidden symbols are absent from the linked image"
	@echo "== ***** NO fwrite REFERENCE FROM ANY M13B OBJECT ***** =="
# fwrite was on the PRESENCE list above until now, and that was a BUG of exactly
# the kind M13A:8926 documents -- M13B had been written against the pre-fix
# pattern. The presence test asks "is this symbol DEFINED anywhere in the
# image", and for fwrite the answer is unconditionally YES:
#
#   * runtime/shim.c:445 DEFINES fwrite (and :580 fputc, :586 fputs,
#     :621 fprintf). shim.c is FROZEN and shim.o is mandatory -- it carries
#     klog, malloc, memcpy and the entire string layer.
#   * Makefile:496 records that the authoritative link uses NO --gc-sections and
#     names whole objects, so EVERY byte of shim.o is in the image whether or
#     not one instruction reaches it.
#
# So that gate COULD NEVER PASS for any correct M13B: it measured a fact about
# the link model, not about M13B. The comment at 9489 already said the right
# thing -- "forbidding the REFERENCE forbids the capability" -- while the code
# below it checked definitions. The gate now matches its own stated intent, and
# the pattern used by M13A:8942 and M12C:8414.
	@bad=""; \
	 for o in $(M13BOBJS); do \
	     for s in $(M13B_FORBIDDEN_FWRITE); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13B VERIFY FAILED: an object REFERENCES a file-write entry"; \
	     echo "  point:$$bad"; \
	     echo "  M13B enumerates and loads a cartridge. It writes NOTHING."; \
	     exit 1; \
	 fi; \
	 echo "no M13B object references fwrite/fputc/fputs/fprintf"
# The reference check above is only meaningful while each write function has
# exactly one owner. If a NEW object ever carried its own copy, every M13B
# object could stay reference-free and the image would still have gained a
# writer. So: no object but the frozen shim may define them, and the shim MUST
# still define them -- that second half stops the gate degrading into a vacuous
# PASS if $(NM) changes spelling, a symbol is renamed, or shim.o is swapped for
# a stub. All four are checked, not just fwrite.
	@bad=""; \
	 for o in $(M13BOBJS); do \
	     case "$$o" in *shim.o) continue ;; esac; \
	     for s in $(M13B_FORBIDDEN_FWRITE); do \
	         if $(NM) --defined-only $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13B VERIFY FAILED: a write entry point is DEFINED outside the"; \
	     echo "  frozen shim, by:$$bad"; \
	     echo "  A second definition means the image gained a writer that the"; \
	     echo "  reference check above cannot see."; \
	     exit 1; \
	 fi; \
	 missing=""; \
	 for s in $(M13B_FORBIDDEN_FWRITE); do \
	     if ! $(NM) --defined-only $(M13BBUILD)/shim.o 2>/dev/null \
	          | grep -qw "$$s"; then \
	         missing="$$missing $$s"; \
	     fi; \
	 done; \
	 if [ -n "$$missing" ]; then \
	     echo "M13B VERIFY FAILED: shim.o does not define:$$missing"; \
	     echo "  This gate's premise no longer holds, so its PASS would be"; \
	     echo "  VACUOUS -- it would be reporting absence it never measured."; \
	     exit 1; \
	 fi; \
	 echo "fwrite is defined ONLY by frozen shim.o and is unreachable from M13B"; \
	 echo "  -- it is in the image because the link takes whole objects, not"; \
	 echo "  because M13B can reach it"
	@echo "== ***** THE NAMED-ROM PATH IS THE ONE IN USE ***** =="
	@if ! $(READELF) -sW $(M13BELF) 2>/dev/null | grep -q "m4_rom_load_named"; then \
	     echo "M13B VERIFY FAILED: m4_rom_load_named is absent -- the picker"; \
	     echo "  cannot be loading the operator's chosen path."; \
	     exit 1; \
	 fi
	@if ! $(READELF) -sW $(M13BELF) 2>/dev/null | grep -q "gba_lib_scan"; then \
	     echo "M13B VERIFY FAILED: gba_lib_scan is absent -- nothing enumerates."; \
	     exit 1; \
	 fi
	@echo "m4_rom_load_named and gba_lib_scan are both present"
	@echo "== ***** NO DIRECT USB, DEVICE OR MOUNT LITERAL ***** =="
	@if $(OBJDUMP) -s -j .rodata $(M13BELF) 2>/dev/null \
	     | grep -qiE "/mnt|/usb|/dev/|ugen|usbctl"; then \
	     echo "M13B VERIFY FAILED: the image contains a USB, device or mount"; \
	     echo "  literal. M13B reads /temp0 and nothing else -- it does not"; \
	     echo "  probe, open or mount a device."; \
	     exit 1; \
	 fi; \
	 echo "no /mnt, /usb, /dev, ugen or usbctl literal is present"
	@echo "== ***** NO SAVEDATA IS TOUCHED AT ANY RUNG ***** =="
	@if $(OBJDUMP) -s -j .rodata $(M13BELF) 2>/dev/null \
	     | grep -qiE "sceSaveData"; then \
	     echo "M13B VERIFY FAILED: a sceSaveData name is present."; \
	     exit 1; \
	 fi; \
	 echo "no sceSaveData entry point is named"
	@echo "== M13B VERIFY OK =="

# ---- the cartridge uploader ------------------------------------------------
#
# ***** THE RECIPE IS QUOTED, AND THAT IS A CORRECTNESS FIX AND NOT A STYLE ONE.
#
# m4-upload-rom (Makefile:1616) passes $(ROM) UNQUOTED to a shell. A real GBA
# filename -- "Tony Hawk's Pro Skater 2 (USA, Europe).gba" -- contains spaces,
# an apostrophe and parentheses, so an unquoted expansion splits it into six
# shell words and uploads nothing, or worse uploads the wrong file. M13B's
# entire premise is that the OPERATOR'S OWN FILENAME is preserved byte for byte,
# so the uploader is the one place that must not mangle it.
#
# ***** basename(1), NOT $(notdir). ***** $(notdir) is a MAKE function that
# splits its argument on whitespace, so it returns garbage for exactly the
# filenames this milestone exists to support. The shell's basename takes one
# quoted argument and returns one answer.
#
# tools/upload.py and lua/upload.lua are reused VERBATIM. The default is
# replace/truncate, so re-uploading does not leave a tail of a previous file.
m13b-upload:
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m13b-upload PS5_IP=<console address> ROM=<file>)
endif
ifndef ROM
	$(error ROM is not set -- use: make m13b-upload PS5_IP=<console address> ROM=<path to a .gba cartridge>)
endif
	@base=$$(basename "$(ROM)"); \
	 echo "M13B: uploading \"$(ROM)\""; \
	 echo "M13B:        -> /temp0/$$base"; \
	 echo "M13B: the filename is preserved EXACTLY. Do not rename it -- and"; \
	 echo "M13B: note that renaming could not change the save identity anyway,"; \
	 echo "M13B: because that identity is derived from the cartridge BYTES."; \
	 $(PYTHON) tools/upload.py "$(PS5_IP)" "$(ROM)" "/temp0/$$base"

# ---- the Lua payload -------------------------------------------------------

M13B_LUA_TEMPLATE := lua/m13b.lua.in
M13B_LUA_OUT      := $(M13BBUILD)/$(M13BTARGET).lua

# THE STAGE IS SUBSTITUTED INTO THE GENERATED SCRIPT, NOT PASSED AT RUN TIME.
# tools/mklua.py only knows @@JIT_SIZE@@, so @@STAGE@@ is replaced here, AFTER
# it has written the output. The value is validated against the five legal
# rungs first: an unrecognised stage would otherwise be baked into a script that
# the payload then silently demotes to rung 0, and the operator would photograph
# a rung-0 screen believing it was rung 3.
m13b-lua: m13b-check $(M13BBIN) $(M13BELF)
ifndef STAGE
	$(error STAGE is not set -- use: make m13b-lua STAGE=0|1|2|3|35)
endif
	@case "$(STAGE)" in \
	   0|1|2|3|35) ;; \
	   *) echo "M13B: STAGE must be one of 0, 1, 2, 3, 35 -- got '$(STAGE)'"; \
	      exit 1;; \
	 esac
	@$(PYTHON) tools/mklua.py $(M13B_LUA_TEMPLATE) $(M13BBIN) $(M13B_LUA_OUT) $(M13BELF)
	@sed -i.bak "s/@@STAGE@@/$(STAGE)/" $(M13B_LUA_OUT) && rm -f $(M13B_LUA_OUT).bak
	@echo "M13B LUA OK: $(M13B_LUA_OUT) -- STAGE $(STAGE)"

# ---- m13b-send -- HARDWARE DELIVERY ----------------------------------------
#
# NOT AUTHORISED BY THE EXISTENCE OF THIS TARGET.
#
#   * m13b-verify IS A PREREQUISITE, and it in turn depends on m13b-equiv. The
#     283-check harness therefore runs before anything is transmitted, always.
#   * ***** CLIMB THE RUNGS IN ORDER: 0, 1, 2, 3, 35. ***** Each one ends at a
#     different point of no return, and the earlier ones cost nothing to run.
#   * UPLOAD A CARTRIDGE FIRST with `make m13b-upload ROM=<file>`, and a BIOS
#     with `make m3-upload-bios`. Rung 3 needs both.
#   * ***** M13B RUNS NO GAME AND WRITES NOTHING. ***** gba_exec.o is not
#     linked and no write path exists in the image.
#   * CAPTURE THE UDP LOG ON 9027. The picker screen carries the verdict; the
#     per-entry reasons and the exact chosen path are in the log.
#   * RELAUNCH THE HOST GAME BETWEEN RUNS, as at every previous milestone.
m13b-send: m13b-verify m13b-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m13b-send PS5_IP=<console address> STAGE=<n>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M13B_LUA_OUT) $(M13BBIN)

m13b-clean:
	rm -rf $(M13BBUILD)

.PHONY: m13b m13b-objs m13b-check m13b-equiv m13b-link m13b-size m13b-verify \
        m13b-lua m13b-upload m13b-send m13b-clean

# =============================================================================
# M13B-4 -- GAMEPLAY FROM THE PICKER
# =============================================================================
#
#   make m13b4
#   make m13b4-size
#   make m13b4-verify
#   make m13b4-lua
#   make m13b4-send PS5_IP=<addr>
#
# ***** THIS IS A SEPARATE TARGET, AND THAT IS THE WHOLE ARCHITECTURE. *****
#
# The M13B verifier above proves, as its DEFINING GATE, that gba_exec.o is
# ABSENT from the link line and that execute_arm() is therefore unreachable from
# that image by construction. That gate's PASS is the structural evidence behind
# FIVE hardware runs -- M13B-0, -1, -2, -3 and -3.5.
#
# M13B-4 needs the exact opposite property: gba_exec.o PRESENT and execute_arm
# REACHABLE. Those two claims cannot coexist in one link image, and object lists
# are decided at LINK time while stages are chosen at RUN time -- so there is no
# way to express "absent at stage 3, present at stage 4" as a link-line
# predicate. Adding gba_exec.o to $(M13BOBJS) would flip the gate for stages 0
# through 3.5 simultaneously and destroy the evidence to gain nothing.
#
# So: TWO TARGETS, ONE SOURCE.
#
#   build/m13b/    UNTOUCHED. Still the no-execution evidence image. m13b-verify
#                  is not modified by one character and still proves absence.
#   build/m13b4/   The gameplay image, with its own verifier that proves
#                  PRESENCE positively while keeping every safety gate.
#
# THE SOURCE IS SHARED, NOT DUPLICATED. apps/m13bgpsp/main.c compiles twice --
# once plain, once with -DM13B_GAMEPLAY. apps/m13bgpsp/m13b_picker.inc and
# tools/m13b_picker_equiv.c are BYTE-UNCHANGED, so the 283-check equivalence
# harness still tests the thing that ships in BOTH images, and m13b4-verify
# depends on it exactly as m13b-verify does.

M13B4TARGET := m13b4gpsp
M13B4BUILD  := build/m13b4
M13B4ELF    := $(M13B4BUILD)/$(M13B4TARGET).elf
M13B4BIN    := $(M13B4BUILD)/$(M13B4TARGET).bin

$(M13B4BUILD):
	mkdir -p $(M13B4BUILD)

# ---- flags ------------------------------------------------------------------
#
# IDENTICAL TO $(M13BFLAGS) PLUS ONE DEFINE. No include path is added, no
# warning is disabled and no optimisation is changed, so the objects this target
# shares with M13B compile from the same source under the same rules.
M13B4FLAGS = $(BASEFLAGS) $(M13B_INCLUDES) -Wall -Wno-unused-function \
             -DM13B_GAMEPLAY

# ***** THE gpSP-FACING FLAGS DELIBERATELY DO NOT CARRY -DM13B_GAMEPLAY. *****
# Not one adapter compiled with these tests that macro; they are the SAME
# translation units M10 builds. Passing it would make these objects differ from
# M10's for no reason and would weaken the claim that they are unmodified.
M13B4_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M13B4_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M13B4HEADERS = $(HEADERS)

M13B4LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
               -Wl,--build-id=none -Wl,-z,norelro \
               -Wl,--orphan-handling=error \
               -Wl,-Map=$(M13B4BUILD)/$(M13B4TARGET).map

# ----------------------------------------------------------------- objects ---
#
# M13B's own object set, PLUS the seven execution/presentation/input/audio
# objects M10 proved on hardware. Every one of the seven is compiled from
# UNMODIFIED source with the same flags M10 uses.
#
#   gba_exec.o     M5's execution adapter -- THE ONLY caller of execute_arm()
#   gba_m8map.o    M8's cartridge mapping probe (REPORTED, not gated -- its
#                  SWAP bit is correct for a demand-paged cartridge)
#   gba_m10map.o   M10's demand-paging-aware mapping probe and page-load counter
#   gba_present.o  M7's 240x160 -> 1440x960 conversion
#   gba_input.o    M8's DualSense -> REG_P1 keypad adapter
#   gba_audio.o    M10's 512:375 converter and ring
#   audio.o        M10's PS5 sceAudioOut backend
#
# STILL ABSENT, EXACTLY AS IN M13B: savedata.o, gba_savefile.o, gba_savehdr.o,
# gba_restore.o, gba_restorefile.o, gba_m11save.o. M13B-4 writes no save.
# ALSO ABSENT: cpu_threaded.c, every dynarec emitter and stub, libretro.c and
# the libretro VFS. HAVE_DYNAREC stays undefined and dynarec_enable stays an
# UNDEFINED symbol so any leak is a LINK ERROR rather than a silent success.
M13B4_OWN_OBJS = $(M13B4BUILD)/m13b4main.o \
                 $(M13B4BUILD)/shim.o \
                 $(M13B4BUILD)/gfx.o \
                 $(M13B4BUILD)/platform.o \
                 $(M13B4BUILD)/audio.o \
                 $(M13B4BUILD)/m13store.o \
                 $(M13B4BUILD)/gba_library.o \
                 $(M13B4BUILD)/gba_fixture.o \
                 $(M13B4BUILD)/gba_filestream.o \
                 $(M13B4BUILD)/gba_compat.o \
                 $(M13B4BUILD)/gba_probe.o \
                 $(M13B4BUILD)/gba_bios.o \
                 $(M13B4BUILD)/gba_rom.o \
                 $(M13B4BUILD)/gba_save.o \
                 $(M13B4BUILD)/gba_m8map.o \
                 $(M13B4BUILD)/gba_m10map.o \
                 $(M13B4BUILD)/gba_exec.o \
                 $(M13B4BUILD)/gba_present.o \
                 $(M13B4BUILD)/gba_input.o \
                 $(M13B4BUILD)/gba_audio.o

M13B4_GPSP_OBJS = $(M13B4BUILD)/gpsp_cpu.o $(M13B4BUILD)/gpsp_main.o \
                  $(M13B4BUILD)/gpsp_gba_memory.o $(M13B4BUILD)/gpsp_video.o \
                  $(M13B4BUILD)/gpsp_sound.o $(M13B4BUILD)/gpsp_serial.o \
                  $(M13B4BUILD)/gpsp_serial_proto.o $(M13B4BUILD)/gpsp_rfu.o \
                  $(M13B4BUILD)/gpsp_gbp.o $(M13B4BUILD)/gpsp_cheats.o \
                  $(M13B4BUILD)/gpsp_savestate.o $(M13B4BUILD)/gpsp_input.o

M13B4OBJS = $(M13B4_OWN_OBJS) $(M13B4_GPSP_OBJS)

# ---- offline structural check -----------------------------------------------
m13b4-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m13bgpsp/main.c
	@$(PYTHON) tools/m8_syntax_check.py apps/m13bgpsp/m13b_picker.inc

# ---- LUAport-owned objects --------------------------------------------------
#
# m13b_picker.inc is a DEPENDENCY, not merely an include -- without it named
# here a change to the picker math would leave a stale m13b4main.o in place.
$(M13B4BUILD)/m13b4main.o: apps/m13bgpsp/main.c \
                           apps/m13bgpsp/m13b_picker.inc \
                           adapters/gba/m13store.h \
                           adapters/gba/gba_library.h \
                           adapters/gba/gba_rom.h \
                           adapters/gba/gba_probe.h \
                           adapters/gba/gba_bios.h \
                           adapters/gba/gba_save.h \
                           adapters/gba/gba_exec.h \
                           adapters/gba/gba_present.h \
                           adapters/gba/gba_input.h \
                           adapters/gba/gba_m8map.h \
                           adapters/gba/gba_m10map.h \
                           adapters/gba/gba_audio.h runtime/audio.h \
                           $(M13B4HEADERS) | $(M13B4BUILD)
	$(CC) $(M13B4FLAGS) -c $< -o $@

$(M13B4BUILD)/shim.o: runtime/shim.c $(M13B4HEADERS) | $(M13B4BUILD)
	$(CC) $(M13B4FLAGS) -c $< -o $@

$(M13B4BUILD)/gfx.o: runtime/gfx.c $(M13B4HEADERS) | $(M13B4BUILD)
	$(CC) $(M13B4FLAGS) -c $< -o $@

# M0's PS5 VideoOut AND PAD layer, from UNMODIFIED source. The audio backend is
# a SEPARATE translation unit precisely so platform.c does not have to change --
# adding sceAudioOut strings to it would push them into every frozen image.
$(M13B4BUILD)/platform.o: runtime/platform.c $(M13B4HEADERS) | $(M13B4BUILD)
	$(CC) $(M13B4FLAGS) -c $< -o $@

# M10's PS5 AUDIO backend, from UNMODIFIED source.
$(M13B4BUILD)/audio.o: runtime/audio.c runtime/audio.h $(M13B4HEADERS) \
                       | $(M13B4BUILD)
	$(CC) $(M13B4FLAGS) -c $< -o $@

$(M13B4BUILD)/m13store.o: adapters/gba/m13store.c \
                          adapters/gba/m13store.h runtime/core.h | $(M13B4BUILD)
	$(CC) $(M13B4FLAGS) -c $< -o $@

$(M13B4BUILD)/gba_library.o: adapters/gba/gba_library.c \
                             adapters/gba/gba_library.h \
                             adapters/gba/m13store.h | $(M13B4BUILD)
	$(CC) $(M13B4FLAGS) -c $< -o $@

# M7's presentation adapter. Needs the RUNTIME includes (runtime/core.h for
# SCR_W/SCR_H), exactly as M10 compiles it.
$(M13B4BUILD)/gba_present.o: adapters/gba/gba_present.c \
                             adapters/gba/gba_present.h \
                             runtime/core.h | $(M13B4BUILD)
	$(CC) $(M13B4FLAGS) -c $< -o $@

# ---- gpSP-facing adapters ---------------------------------------------------
#
# ***** THESE GET THE gpSP FLAGS AND NOTHING ELSE, AND THAT IS LOAD-BEARING IN
# BOTH DIRECTIONS. ***** They include gpsp/common.h, so they must see the same
# gpSP configuration every other gpSP object sees. And they MUST NOT get
# $(M13B_INCLUDES), whose -Iruntime would put runtime/core.h on the include
# path: gpsp/common.h:100 typedefs u64 as `unsigned long long` and
# runtime/core.h:30 as `unsigned long` -- DISTINCT types -- so a TU that saw
# both would not compile. Their own headers sit beside the .c files and are
# found without any -I.
$(M13B4BUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h \
                           | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h \
                          | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h \
                         | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

# M13B-4 calls gba_save_latch() and the READERS. IT CALLS NO WRITER, and no
# writer object is on the link line at all.
$(M13B4BUILD)/gba_save.o: adapters/gba/gba_save.c adapters/gba/gba_save.h \
                          | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gba_m8map.o: adapters/gba/gba_m8map.c adapters/gba/gba_m8map.h \
                           | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gba_m10map.o: adapters/gba/gba_m10map.c \
                            adapters/gba/gba_m10map.h | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h \
                          | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gba_input.o: adapters/gba/gba_input.c adapters/gba/gba_input.h \
                           | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

# NOTE THE ABSENT PREREQUISITE: runtime/audio.h is deliberately NOT listed,
# because gba_audio.c deliberately does NOT include it. It hand-declares the one
# function it needs -- `extern int plat_audio_submit(const short *)`, whose
# signature mentions only builtins both sides agree on. Listing the header would
# assert a dependency that must never exist.
$(M13B4BUILD)/gba_audio.o: adapters/gba/gba_audio.c adapters/gba/gba_audio.h \
                           | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects ------------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, IDENTICAL to M13B's.
# gpsp/** is never modified and its own Makefile is never invoked.

$(M13B4BUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M13B4BUILD)
	$(CXX) $(M13B4_GPSPCXXFLAGS) -c $< -o $@

$(M13B4BUILD)/gpsp_video.o: gpsp/video.cc | $(M13B4BUILD)
	$(CXX) $(M13B4_GPSPCXXFLAGS) -c $< -o $@

$(M13B4BUILD)/gpsp_main.o: gpsp/main.c | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gpsp_sound.o: gpsp/sound.c | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gpsp_serial.o: gpsp/serial.c | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

$(M13B4BUILD)/gpsp_input.o: gpsp/input.c | $(M13B4BUILD)
	$(CC) $(M13B4_GPSPFLAGS) -c $< -o $@

m13b4-objs: $(M13B4OBJS)

# ---- link -------------------------------------------------------------------

$(M13B4ELF): $(M13B4OBJS) runtime/linker.ld | $(M13B4BUILD)
	$(CC) $(BASEFLAGS) $(M13B4LDFLAGS) -o $@ $(M13B4OBJS)

m13b4-link: $(M13B4OBJS) | $(M13B4BUILD)
	@$(CC) $(BASEFLAGS) $(M13B4LDFLAGS) -o $(M13B4ELF) $(M13B4OBJS) \
	     > $(M13B4BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to .[^']+'" $(M13B4BUILD)/link.log \
	            | sed -E "s/.*to .//; s/'$$//" | sort -u); \
	 if [ $$status -ne 0 ]; then \
	     cat $(M13B4BUILD)/link.log; \
	     echo "M13B4 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M13B4 LINK FAILED: undefined references reported despite exit 0"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "M13B4 LINK OK: linker exit 0, no undefined references"

$(M13B4BIN): $(M13B4ELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M13B4ELF) $@ \
	   --map $(M13B4BUILD)/$(M13B4TARGET).map --budget $(JIT_LIMIT) --arena 4194304

m13b4: $(M13B4BIN)

# THE TWO HARD CEILINGS, UNCHANGED AND UNRAISED. sizereport.py enforces the JIT
# budget ($(JIT_LIMIT) = 524,288) and tools/check_forbidden.py enforces the
# 2,097,152-byte .bss tripwire.
#
# ***** WATCH __data_load, NOT JUST .bss. ***** runtime/linker.ld sets
# __data_start = ALIGN(__data_load, 64K) + 64K, so the JIT reservation is driven
# by .text + .rodata + .rela and NOT by .bss. M13B-4 adds seven objects to an
# M13B-shaped image; if text+rodata crosses the next 64 KB bracket, __data_start
# moves and the JIT footprint moves with it.
m13b4-size: $(M13B4ELF) $(M13B4BIN)
	@$(PYTHON) tools/sizereport.py $(M13B4ELF) $(M13B4BIN) \
	   --map $(M13B4BUILD)/$(M13B4TARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification -----------------------------------------------------------
#
# ***** THIS VERIFIER INVERTS EXACTLY ONE GATE AND WEAKENS NONE. *****
#
# m13b-verify proves gba_exec.o is ABSENT. That target is NOT MODIFIED and still
# runs and still proves it. m13b4-verify proves the OPPOSITE for the gameplay
# image -- POSITIVELY, by requiring the objects to be present AND requiring the
# references between them to exist, so an image that linked gba_exec.o but never
# called into it could not pass.
#
# EVERY OTHER GATE IS RETAINED VERBATIM: relocation validation, symbol-kind
# validation, dependency closure, dynarec and libretro exclusion, zero undefined
# symbols, ET_DYN, the .bss tripwire, the JIT budget, the save-writer object
# exclusion, the forbidden-save symbol list, all THREE fwrite gates, the
# USB/mount literal gate and the sceSaveData gate.

# The M12 WRITE path and the PS5 savedata layer. NONE may appear. IDENTICAL to
# $(M13B_FORBIDDEN_SAVE) -- M13B-4 gained the right to EXECUTE, not to WRITE.
M13B4_FORBIDDEN_SAVE = $(M13B_FORBIDDEN_SAVE)

# fwrite is the only function in runtime/shim.c that reaches raw_write() for a
# real FILE*; the other three are the INDIRECT routes to it.
M13B4_FORBIDDEN_FWRITE = $(M13B_FORBIDDEN_FWRITE)

# ***** NOTE WHAT IS *NOT* HERE. ***** $(M13B_FORBIDDEN_EXEC) named m5_exec_run
# and gba_exec_run. M13B-4 REQUIRES m5_exec_run, so that list is deliberately
# not applied to this target -- and its absence is compensated by the POSITIVE
# gates below, which are strictly harder to satisfy accidentally. The dynarec
# entry point is a different matter and stays forbidden.
M13B4_FORBIDDEN_DYNAREC = execute_arm_translate dynarec_enable

# The seven objects rung 4 exists to link. Each must be PRESENT.
M13B4_REQUIRED_OBJS = gba_exec.o gba_m8map.o gba_m10map.o gba_present.o \
                      gba_input.o gba_audio.o audio.o

m13b4-verify: m13b-equiv $(M13B4ELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M13B4ELF)
	@echo "== relocations (NON-MASKING pipeline) =="
	@$(PYTHON) tools/verify_reloc.py $(M13B4ELF) $(M13B4OBJS) \
	     > $(M13B4BUILD)/reloc.log 2>&1; \
	 st=$$?; \
	 tail -12 $(M13B4BUILD)/reloc.log; \
	 if [ $$st -ne 0 ]; then \
	     echo "M13B4 VERIFY FAILED: the relocation check exited $$st."; \
	     echo "  Full output: $(M13B4BUILD)/reloc.log"; \
	     exit $$st; \
	 fi
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M13B4OBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M13B4OBJS)
	@echo "== forbidden symbols in the linked image (.bss tripwire included) =="
	@$(PYTHON) tools/check_forbidden.py $(M13B4ELF) $(M13B4OBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M13B4ELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M13B4 VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== ***** THE EXECUTION PATH IS LINKED -- THE DEFINING GATE ***** =="
# POSITIVE, AND IN THREE INDEPENDENT WAYS. Presence of an object is necessary
# but NOT sufficient: a link line can carry gba_exec.o while nothing calls into
# it, and the whole-object link model (no --gc-sections) would keep it there.
# So the REFERENCES are checked too, in both directions of the chain:
#
#     m13b4main.o  --references-->  m5_exec_run  (in gba_exec.o)
#     gba_exec.o   --references-->  execute_arm  (in gpsp_cpu.o)
#
# Both must hold. Together they prove a REACHABLE path from the fixture to
# gpSP's interpreter, which is what rung 4 actually claims.
	@missing=""; \
	 for want in $(M13B4_REQUIRED_OBJS); do \
	     found=0; \
	     for o in $(M13B4OBJS); do \
	         case "$$o" in */$$want) found=1;; esac; \
	     done; \
	     if [ $$found -eq 0 ]; then missing="$$missing $$want"; fi; \
	 done; \
	 if [ -n "$$missing" ]; then \
	     echo "M13B4 VERIFY FAILED: a required gameplay object is NOT on the"; \
	     echo "  link line:$$missing"; \
	     echo "  M13B-4 claims a complete gameplay path. An image missing one"; \
	     echo "  of these cannot be delivering it."; \
	     exit 1; \
	 fi; \
	 echo "all seven gameplay objects are PRESENT:$(M13B4_REQUIRED_OBJS)"
	@if ! $(NM) -u $(M13B4BUILD)/m13b4main.o 2>/dev/null | grep -qw "m5_exec_run"; then \
	     echo "M13B4 VERIFY FAILED: m13b4main.o does NOT reference m5_exec_run."; \
	     echo "  gba_exec.o would be linked but never entered, and the image"; \
	     echo "  would emulate nothing while claiming to."; \
	     exit 1; \
	 fi; \
	 echo "m13b4main.o REFERENCES m5_exec_run"
	@if ! $(NM) -u $(M13B4BUILD)/gba_exec.o 2>/dev/null | grep -qw "execute_arm"; then \
	     echo "M13B4 VERIFY FAILED: gba_exec.o does NOT reference execute_arm."; \
	     echo "  The adapter is present but is not calling gpSP's interpreter."; \
	     exit 1; \
	 fi; \
	 echo "gba_exec.o REFERENCES execute_arm -- the path to gpSP is REACHABLE"
	@echo "== ***** THE DYNAREC IS STILL ABSENT ***** =="
# execute_arm IS NOT FORBIDDEN -- it is gpSP's own interpreter and has been in
# gpsp_cpu.o since M1. execute_arm_translate IS the dynarec entry point and must
# never appear, defined or referenced. dynarec_enable is deliberately left
# UNDEFINED (gba_fixture.c:20-35) so that any leak is a LINK ERROR; since the
# link above already proved ZERO undefined symbols, its absence from the defined
# set is the remaining half of that tripwire.
	@for s in $(M13B4_FORBIDDEN_DYNAREC); do \
	     if $(READELF) -sW $(M13B4ELF) 2>/dev/null \
	          | awk '$$7 != "UND" { print $$8 }' | grep -qx "$$s"; then \
	         echo "M13B4 VERIFY FAILED: dynarec symbol '$$s' is DEFINED in the"; \
	         echo "  linked image. HAVE_DYNAREC must stay undefined and"; \
	         echo "  cpu_threaded.c must stay uncompiled."; \
	         exit 1; \
	     fi; \
	 done; \
	 bad=""; \
	 for o in $(M13B4OBJS); do \
	     for s in $(M13B4_FORBIDDEN_DYNAREC); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13B4 VERIFY FAILED: an object REFERENCES a dynarec entry"; \
	     echo "  point:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "execute_arm_translate and dynarec_enable are absent, defined and"; \
	 echo "  referenced alike -- the interpreter is the only execution path"
	@for o in $(M13B4OBJS); do \
	     case "$$o" in \
	       */cpu_threaded.o|*/dynarec*.o|*/libretro*.o) \
	           echo "M13B4 VERIFY FAILED: a dynarec or libretro object is"; \
	           echo "  linked: $$o"; \
	           exit 1;; \
	     esac; \
	 done; \
	 echo "no cpu_threaded, dynarec or libretro object is on the link line"
	@echo "== ***** THERE IS STILL NO WRITE PATH AND NO SAVE PATH ***** =="
	@bad=""; \
	 for o in $(M13B4OBJS); do \
	     case "$$o" in \
	       */savedata.o|*/gba_savefile.o|*/gba_savehdr.o|*/gba_restore.o|*/gba_restorefile.o|*/gba_m11save.o) \
	           bad="$$bad $$o";; \
	     esac; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13B4 VERIFY FAILED: a save writer or restorer is linked:$$bad"; \
	     echo "  M13B-4 PLAYS a cartridge. M13B-5 owns persistence."; \
	     exit 1; \
	 fi; \
	 echo "no savedata, savefile, restore or M11 observer object is linked"
	@for s in $(M13B4_FORBIDDEN_SAVE); do \
	     if $(READELF) -sW $(M13B4ELF) 2>/dev/null \
	          | awk '$$7 != "UND" { print $$8 }' | grep -qx "$$s"; then \
	         echo "M13B4 VERIFY FAILED: forbidden symbol '$$s' is defined in"; \
	         echo "  the linked image."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "save forbidden symbols are absent from the linked image"
	@echo "== ***** NO fwrite REFERENCE FROM ANY M13B4 OBJECT ***** =="
# GATE 1 of 3: no object may REFERENCE a write entry point.
	@bad=""; \
	 for o in $(M13B4OBJS); do \
	     for s in $(M13B4_FORBIDDEN_FWRITE); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13B4 VERIFY FAILED: an object REFERENCES a file-write entry"; \
	     echo "  point:$$bad"; \
	     echo "  M13B-4 plays a cartridge. It writes NOTHING."; \
	     exit 1; \
	 fi; \
	 echo "no M13B4 object references fwrite/fputc/fputs/fprintf"
# GATE 2 of 3: no object BUT the frozen shim may DEFINE one. The reference check
# above is only meaningful while each write function has exactly one owner; if a
# NEW object carried its own copy, every object could stay reference-free and
# the image would still have gained a writer.
	@bad=""; \
	 for o in $(M13B4OBJS); do \
	     case "$$o" in *shim.o) continue ;; esac; \
	     for s in $(M13B4_FORBIDDEN_FWRITE); do \
	         if $(NM) --defined-only $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13B4 VERIFY FAILED: a write entry point is DEFINED outside the"; \
	     echo "  frozen shim, by:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "no object but the frozen shim defines a write entry point"
# GATE 3 of 3: the shim MUST STILL DEFINE them. Without this the gate would
# degrade into a VACUOUS PASS if $(NM) changed spelling, a symbol were renamed,
# or shim.o were swapped for a stub -- it would be reporting an absence it never
# measured. All four are checked, not just fwrite.
	@missing=""; \
	 for s in $(M13B4_FORBIDDEN_FWRITE); do \
	     if ! $(NM) --defined-only $(M13B4BUILD)/shim.o 2>/dev/null \
	          | grep -qw "$$s"; then \
	         missing="$$missing $$s"; \
	     fi; \
	 done; \
	 if [ -n "$$missing" ]; then \
	     echo "M13B4 VERIFY FAILED: shim.o does not define:$$missing"; \
	     echo "  This gate's premise no longer holds, so its PASS would be"; \
	     echo "  VACUOUS -- it would be reporting absence it never measured."; \
	     exit 1; \
	 fi; \
	 echo "fwrite is defined ONLY by frozen shim.o and is unreachable from M13B4"
	@echo "== ***** THE NAMED-ROM PATH IS THE ONE IN USE ***** =="
	@if ! $(READELF) -sW $(M13B4ELF) 2>/dev/null | grep -q "m4_rom_load_named"; then \
	     echo "M13B4 VERIFY FAILED: m4_rom_load_named is absent -- the picker"; \
	     echo "  cannot be loading the operator's chosen path."; \
	     exit 1; \
	 fi
	@if ! $(READELF) -sW $(M13B4ELF) 2>/dev/null | grep -q "gba_lib_scan"; then \
	     echo "M13B4 VERIFY FAILED: gba_lib_scan is absent -- nothing enumerates."; \
	     exit 1; \
	 fi
	@echo "m4_rom_load_named and gba_lib_scan are both present"
	@echo "== ***** NO DIRECT USB, DEVICE OR MOUNT LITERAL ***** =="
	@if $(OBJDUMP) -s -j .rodata $(M13B4ELF) 2>/dev/null \
	     | grep -qiE "/mnt|/usb|/dev/|ugen|usbctl"; then \
	     echo "M13B4 VERIFY FAILED: the image contains a USB, device or mount"; \
	     echo "  literal. M13B-4 reads /temp0 and nothing else."; \
	     exit 1; \
	 fi; \
	 echo "no /mnt, /usb, /dev, ugen or usbctl literal is present"
	@echo "== ***** NO SAVEDATA IS TOUCHED AT ANY RUNG ***** =="
	@if $(OBJDUMP) -s -j .rodata $(M13B4ELF) 2>/dev/null \
	     | grep -qiE "sceSaveData"; then \
	     echo "M13B4 VERIFY FAILED: a sceSaveData name is present."; \
	     exit 1; \
	 fi; \
	 echo "no sceSaveData entry point is named"
	@echo "== M13B4 VERIFY OK =="

# ---- the Lua payload --------------------------------------------------------
#
# ***** THE SAME TEMPLATE AS M13B, NOT A COPY OF IT. ***** lua/m13b.lua.in
# already carries the loader, the stage plumbing and every status name; rung 4
# adds three status codes and one stage name to it and reuses everything else.
# Duplicating a 740-line script to change one number is how two payloads drift
# apart and how a photographed verdict starts describing the wrong build.
#
# THE STAGE IS FIXED AT 40 AND IS NOT AN OPERATOR CHOICE. Rungs 0-3.5 belong to
# the frozen m13b image and must be run from it; this target has exactly one
# rung, so offering a STAGE variable here could only ever be a way to run the
# gameplay binary at a stage its verifier did not describe.
M13B4_LUA_TEMPLATE := lua/m13b.lua.in
M13B4_LUA_OUT      := $(M13B4BUILD)/$(M13B4TARGET).lua

m13b4-lua: m13b4-check $(M13B4BIN) $(M13B4ELF)
	@$(PYTHON) tools/mklua.py $(M13B4_LUA_TEMPLATE) $(M13B4BIN) \
	     $(M13B4_LUA_OUT) $(M13B4ELF)
	@sed -i.bak "s/@@STAGE@@/40/" $(M13B4_LUA_OUT) && rm -f $(M13B4_LUA_OUT).bak
	@echo "M13B4 LUA OK: $(M13B4_LUA_OUT) -- STAGE 40 (RUNG 4 GAMEPLAY)"

# ---- m13b4-send -- HARDWARE DELIVERY ----------------------------------------
#
# NOT AUTHORISED BY THE EXISTENCE OF THIS TARGET.
#
#   * m13b4-verify IS A PREREQUISITE, and it in turn depends on m13b-equiv, so
#     the 283-check harness runs before anything is transmitted, always.
#   * ***** CLIMB THE RUNGS IN ORDER. ***** Rungs 0, 1, 2, 3 and 35 belong to
#     the frozen `m13b` target and are all hardware-PASSED. This target is rung
#     4 and must not be the first thing a fresh console sees.
#   * UPLOAD A CARTRIDGE FIRST with `make m13b-upload ROM=<file>`, and a BIOS
#     with `make m3-upload-bios`. Rung 4 needs both.
#   * ***** THE FIRST RUN SHOULD BE Metroid Fusion (USA).gba. ***** It is
#     already proven for 8 MB demand paging, gameplay, input, audio and
#     presentation, so a failure is attributable to THIS milestone.
#   * ***** M13B-4 RUNS A GAME AND WRITES NOTHING. ***** No save writer is on
#     the link line and no M13B4 object references fwrite.
#   * CAPTURE THE UDP LOG ON 9027. The result screen carries the verdict; the
#     per-frame numbers are in the log.
#   * RELAUNCH THE HOST GAME BETWEEN RUNS, as at every previous milestone.
m13b4-send: m13b4-verify m13b4-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m13b4-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M13B4_LUA_OUT) $(M13B4BIN)

m13b4-clean:
	rm -rf $(M13B4BUILD)

.PHONY: m13b4 m13b4-objs m13b4-check m13b4-link m13b4-size m13b4-verify \
        m13b4-lua m13b4-send m13b4-clean

# =============================================================================
# M13B-5 -- SELECTED-ROM SAVE PERSISTENCE
# =============================================================================
#
#   make m13b5-check
#   make m13b5-link
#   make m13b5
#   make m13b5-size
#   make m13b5-verify
#   make m13b5-lua
#   make m13b5-send PS5_IP=<addr>
#
# ***** A THIRD TARGET, FOR THE SAME REASON THERE WAS A SECOND. *****
#
# m13b-verify proves, as its defining gate, that gba_exec.o is ABSENT.
# m13b4-verify proves the opposite for execution while STILL proving that
# savedata.o, gba_savefile.o, gba_savehdr.o, gba_restore.o and gba_restorefile.o
# are all absent and that NO object references fwrite. That absence gate is the
# structural evidence behind the M13B-4 hardware PASS.
#
# M13B-5 needs the exact opposite property: the save stack PRESENT and a write
# window REACHABLE. Those claims cannot coexist in one link image, and object
# lists are decided at LINK time while stages are chosen at RUN time. So:
#
#   build/m13b/    UNTOUCHED. No execution. m13b-verify unmodified.
#   build/m13b4/   UNTOUCHED. Execution, NO persistence. m13b4-verify unmodified.
#   build/m13b5/   Execution AND persistence, with its own verifier that proves
#                  the save stack POSITIVELY while keeping every safety gate.
#
# ***** NEITHER m13b-verify NOR m13b4-verify IS WEAKENED BY ONE CHARACTER. *****
# They remain frozen evidence for six hardware PASSes. $(M13B4OBJS) and
# $(M13BOBJS) are not touched.
#
# THE SOURCE IS SHARED, NOT DUPLICATED. apps/m13bgpsp/main.c now compiles three
# times -- plain, with -DM13B_GAMEPLAY, and with -DM13B_GAMEPLAY -DM13B_PERSIST.
# apps/m13bgpsp/m13b_picker.inc and tools/m13b_picker_equiv.c stay BYTE-
# UNCHANGED, so the 283-check equivalence harness still tests the picker that
# ships in ALL THREE images.
#
# ***** EVERY ADAPTER IN THE SAVE STACK IS LINKED FROM BYTE-IDENTICAL, FROZEN
# M12B/M12C SOURCE. ***** Not one line of gba_save.c, gba_savefile.c,
# gba_savehdr.c, gba_restore.c, gba_restorefile.c or runtime/savedata.c is
# modified by this milestone. M13B-5 is a WIRING milestone: it connects two
# hardware-proven halves to a hardware-proven gameplay loop.

M13B5TARGET := m13b5gpsp
M13B5BUILD  := build/m13b5
M13B5ELF    := $(M13B5BUILD)/$(M13B5TARGET).elf
M13B5BIN    := $(M13B5BUILD)/$(M13B5TARGET).bin

$(M13B5BUILD):
	mkdir -p $(M13B5BUILD)

# ---- flags ------------------------------------------------------------------
#
# IDENTICAL TO $(M13B4FLAGS) PLUS ONE DEFINE. No include path is added, no
# warning is disabled and no optimisation is changed.
M13B5FLAGS = $(BASEFLAGS) $(M13B_INCLUDES) -Wall -Wno-unused-function \
             -DM13B_GAMEPLAY -DM13B_PERSIST

# ***** THE gpSP-FACING FLAGS ARE BYTE-IDENTICAL TO M13B4's AND CARRY NEITHER
# DEFINE. ***** Not one adapter compiled with these tests either macro. Passing
# them would make these objects differ from M10's, M12B's and M12C's for no
# reason and would weaken the claim that they are unmodified.
M13B5_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M13B5_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M13B5HEADERS = $(HEADERS)

M13B5LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
               -Wl,--build-id=none -Wl,-z,norelro \
               -Wl,--orphan-handling=error \
               -Wl,-Map=$(M13B5BUILD)/$(M13B5TARGET).map

# ----------------------------------------------------------------- objects ---
#
# M13B-4's object set PLUS the five that make persistence possible. gba_save.o
# was ALREADY on the M13B-4 link line (rung 3 latches the identity with it), so
# it is not new here -- only its WRITER and the RESTORE half are.
#
#   savedata.o          M12A's PS5 savedata mount/window layer
#   gba_savefile.o      M12B's writer -- THE ONLY fwrite caller in the image
#   gba_savehdr.o       M12C's 32-byte LGS1 header reader/validator
#   gba_restore.o       M12C's applier -- THE ONLY gamepak_backup writer
#   gba_restorefile.o   M12C's two-pass validator
#
# ***** MEASURED .bss COST OF THE FIVE: 228 BYTES. ***** From the M12B and M12C
# link maps: savedata.o 0x60, gba_savefile.o 0x24, gba_savehdr.o 0x0,
# gba_restore.o 0x1c, gba_restorefile.o 0x44. That is what the M12 design buys
# by staging every payload through a 4096-byte STACK buffer instead of a shadow
# copy of gamepak_backup -- 228 bytes against the 131,072 a buffer would cost.
#
# STILL ABSENT: gba_m11save.o (M11's observer -- M13B-5 owns its own dirty
# accounting and would gain a 128-block map nothing reads). ALSO ABSENT:
# cpu_threaded.c, every dynarec emitter and stub, libretro.c and the libretro
# VFS. HAVE_DYNAREC stays undefined and dynarec_enable stays an UNDEFINED symbol
# so any leak is a LINK ERROR rather than a silent success.
M13B5_OWN_OBJS = $(M13B5BUILD)/m13b5main.o \
                 $(M13B5BUILD)/shim.o \
                 $(M13B5BUILD)/gfx.o \
                 $(M13B5BUILD)/platform.o \
                 $(M13B5BUILD)/audio.o \
                 $(M13B5BUILD)/savedata.o \
                 $(M13B5BUILD)/m13store.o \
                 $(M13B5BUILD)/gba_library.o \
                 $(M13B5BUILD)/gba_fixture.o \
                 $(M13B5BUILD)/gba_filestream.o \
                 $(M13B5BUILD)/gba_compat.o \
                 $(M13B5BUILD)/gba_probe.o \
                 $(M13B5BUILD)/gba_bios.o \
                 $(M13B5BUILD)/gba_rom.o \
                 $(M13B5BUILD)/gba_save.o \
                 $(M13B5BUILD)/gba_savefile.o \
                 $(M13B5BUILD)/gba_savehdr.o \
                 $(M13B5BUILD)/gba_restore.o \
                 $(M13B5BUILD)/gba_restorefile.o \
                 $(M13B5BUILD)/gba_m8map.o \
                 $(M13B5BUILD)/gba_m10map.o \
                 $(M13B5BUILD)/gba_exec.o \
                 $(M13B5BUILD)/gba_present.o \
                 $(M13B5BUILD)/gba_input.o \
                 $(M13B5BUILD)/gba_audio.o

M13B5_GPSP_OBJS = $(M13B5BUILD)/gpsp_cpu.o $(M13B5BUILD)/gpsp_main.o \
                  $(M13B5BUILD)/gpsp_gba_memory.o $(M13B5BUILD)/gpsp_video.o \
                  $(M13B5BUILD)/gpsp_sound.o $(M13B5BUILD)/gpsp_serial.o \
                  $(M13B5BUILD)/gpsp_serial_proto.o $(M13B5BUILD)/gpsp_rfu.o \
                  $(M13B5BUILD)/gpsp_gbp.o $(M13B5BUILD)/gpsp_cheats.o \
                  $(M13B5BUILD)/gpsp_savestate.o $(M13B5BUILD)/gpsp_input.o

M13B5OBJS = $(M13B5_OWN_OBJS) $(M13B5_GPSP_OBJS)

# ---- offline structural check -----------------------------------------------
m13b5-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m13bgpsp/main.c
	@$(PYTHON) tools/m8_syntax_check.py apps/m13bgpsp/m13b_picker.inc

# ---- LUAport-owned objects, runtime side ------------------------------------
$(M13B5BUILD)/m13b5main.o: apps/m13bgpsp/main.c \
                           apps/m13bgpsp/m13b_picker.inc \
                           adapters/gba/m13store.h \
                           adapters/gba/gba_library.h \
                           adapters/gba/gba_rom.h \
                           adapters/gba/gba_probe.h \
                           adapters/gba/gba_bios.h \
                           adapters/gba/gba_save.h \
                           adapters/gba/gba_savefile.h \
                           adapters/gba/gba_restore.h \
                           adapters/gba/gba_restorefile.h \
                           adapters/gba/gba_exec.h \
                           adapters/gba/gba_present.h \
                           adapters/gba/gba_input.h \
                           adapters/gba/gba_m8map.h \
                           adapters/gba/gba_m10map.h \
                           adapters/gba/gba_audio.h runtime/audio.h \
                           runtime/savedata.h \
                           $(M13B5HEADERS) | $(M13B5BUILD)
	$(CC) $(M13B5FLAGS) -c $< -o $@

$(M13B5BUILD)/shim.o: runtime/shim.c $(M13B5HEADERS) | $(M13B5BUILD)
	$(CC) $(M13B5FLAGS) -c $< -o $@

$(M13B5BUILD)/gfx.o: runtime/gfx.c $(M13B5HEADERS) | $(M13B5BUILD)
	$(CC) $(M13B5FLAGS) -c $< -o $@

$(M13B5BUILD)/platform.o: runtime/platform.c $(M13B5HEADERS) | $(M13B5BUILD)
	$(CC) $(M13B5FLAGS) -c $< -o $@

$(M13B5BUILD)/audio.o: runtime/audio.c runtime/audio.h $(M13B5HEADERS) \
                       | $(M13B5BUILD)
	$(CC) $(M13B5FLAGS) -c $< -o $@

# ***** M12A's SAVEDATA LAYER, FROM UNMODIFIED, FROZEN SOURCE. *****
# It is the ONLY object that names sceSaveData*, and m13b5-verify asserts that.
$(M13B5BUILD)/savedata.o: runtime/savedata.c runtime/savedata.h \
                          $(M13B5HEADERS) | $(M13B5BUILD)
	$(CC) $(M13B5FLAGS) -c $< -o $@

$(M13B5BUILD)/m13store.o: adapters/gba/m13store.c \
                          adapters/gba/m13store.h runtime/core.h | $(M13B5BUILD)
	$(CC) $(M13B5FLAGS) -c $< -o $@

$(M13B5BUILD)/gba_library.o: adapters/gba/gba_library.c \
                             adapters/gba/gba_library.h \
                             adapters/gba/m13store.h | $(M13B5BUILD)
	$(CC) $(M13B5FLAGS) -c $< -o $@

$(M13B5BUILD)/gba_present.o: adapters/gba/gba_present.c \
                             adapters/gba/gba_present.h \
                             runtime/core.h | $(M13B5BUILD)
	$(CC) $(M13B5FLAGS) -c $< -o $@

# ***** M12B's WRITER. THE RUNTIME HALF, SO IT GETS THE RUNTIME FLAGS. *****
# It includes runtime/savedata.h (and therefore runtime/core.h) and <stdio.h>,
# and it includes NO gpSP header at all -- which is exactly why it may coexist
# with runtime/core.h's u64. Every save byte reaches it through
# gba_save_chunk(). THIS IS THE ONLY OBJECT IN THE IMAGE THAT MAY CALL fwrite.
$(M13B5BUILD)/gba_savefile.o: adapters/gba/gba_savefile.c \
                              adapters/gba/gba_savefile.h \
                              adapters/gba/gba_save.h \
                              runtime/savedata.h | $(M13B5BUILD)
	$(CC) $(M13B5FLAGS) -c $< -o $@

# ***** M12C's VALIDATOR. ALSO THE RUNTIME HALF. *****
# It opens both files "rb" and NEVER writes. It does not include
# runtime/savedata.h at all -- reading a read-only mount needs no window.
$(M13B5BUILD)/gba_restorefile.o: adapters/gba/gba_restorefile.c \
                                 adapters/gba/gba_restorefile.h \
                                 adapters/gba/gba_restore.h \
                                 adapters/gba/gba_savehdr.h \
                                 adapters/gba/gba_save.h | $(M13B5BUILD)
	$(CC) $(M13B5FLAGS) -c $< -o $@

# ---- gpSP-facing adapters ---------------------------------------------------
#
# ***** THESE GET THE gpSP FLAGS AND NOTHING ELSE. ***** They include
# gpsp/common.h, so they must see the same gpSP configuration every other gpSP
# object sees. And they MUST NOT get $(M13B_INCLUDES), whose -Iruntime would put
# runtime/core.h on the include path: gpsp/common.h:100 typedefs u64 as
# `unsigned long long` and runtime/core.h:30 as `unsigned long` -- DISTINCT
# types -- so a TU that saw both would not compile.
$(M13B5BUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h \
                           | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h \
                          | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h \
                         | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

# M12B's SAVE ADAPTER, FROZEN. It owns the identity, the class, the size policy,
# the hashes and the byte tap. M13B-5 calls its LATCH (at rung 3, unchanged) and
# now also its WRITER-side readers.
$(M13B5BUILD)/gba_save.o: adapters/gba/gba_save.c adapters/gba/gba_save.h \
                          | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

# M12C's HEADER READER. Pure arithmetic over an unsigned char array; it includes
# nothing at all and is flag-agnostic. Given the gpSP flags to sit with its
# neighbours.
$(M13B5BUILD)/gba_savehdr.o: adapters/gba/gba_savehdr.c \
                             adapters/gba/gba_savehdr.h | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

# ***** M12C's RESTORE ADAPTER. THE SOLE WRITER OF gamepak_backup[]. *****
# Confining every write to one small auditable object is what makes the
# link-time gate below mean anything.
$(M13B5BUILD)/gba_restore.o: adapters/gba/gba_restore.c \
                             adapters/gba/gba_restore.h | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gba_m8map.o: adapters/gba/gba_m8map.c adapters/gba/gba_m8map.h \
                           | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gba_m10map.o: adapters/gba/gba_m10map.c \
                            adapters/gba/gba_m10map.h | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h \
                          | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gba_input.o: adapters/gba/gba_input.c adapters/gba/gba_input.h \
                           | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

# NOTE THE ABSENT PREREQUISITE: runtime/audio.h is deliberately NOT listed,
# because gba_audio.c deliberately does NOT include it.
$(M13B5BUILD)/gba_audio.o: adapters/gba/gba_audio.c adapters/gba/gba_audio.h \
                           | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects ------------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, IDENTICAL to M13B4's.
# gpsp/** is never modified and its own Makefile is never invoked.

$(M13B5BUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M13B5BUILD)
	$(CXX) $(M13B5_GPSPCXXFLAGS) -c $< -o $@

$(M13B5BUILD)/gpsp_video.o: gpsp/video.cc | $(M13B5BUILD)
	$(CXX) $(M13B5_GPSPCXXFLAGS) -c $< -o $@

$(M13B5BUILD)/gpsp_main.o: gpsp/main.c | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gpsp_sound.o: gpsp/sound.c | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gpsp_serial.o: gpsp/serial.c | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

$(M13B5BUILD)/gpsp_input.o: gpsp/input.c | $(M13B5BUILD)
	$(CC) $(M13B5_GPSPFLAGS) -c $< -o $@

m13b5-objs: $(M13B5OBJS)

# ---- link -------------------------------------------------------------------

$(M13B5ELF): $(M13B5OBJS) runtime/linker.ld | $(M13B5BUILD)
	$(CC) $(BASEFLAGS) $(M13B5LDFLAGS) -o $@ $(M13B5OBJS)

m13b5-link: $(M13B5OBJS) | $(M13B5BUILD)
	@$(CC) $(BASEFLAGS) $(M13B5LDFLAGS) -o $(M13B5ELF) $(M13B5OBJS) \
	     > $(M13B5BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to .[^']+'" $(M13B5BUILD)/link.log \
	            | sed -E "s/.*to .//; s/'$$//" | sort -u); \
	 if [ $$status -ne 0 ]; then \
	     cat $(M13B5BUILD)/link.log; \
	     echo "M13B5 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M13B5 LINK FAILED: undefined references reported despite exit 0"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "M13B5 LINK OK: linker exit 0, no undefined references"

$(M13B5BIN): $(M13B5ELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M13B5ELF) $@ \
	   --map $(M13B5BUILD)/$(M13B5TARGET).map --budget $(JIT_LIMIT) --arena 4194304

m13b5: $(M13B5BIN)

# THE TWO HARD CEILINGS, UNCHANGED AND UNRAISED. sizereport.py enforces the JIT
# budget ($(JIT_LIMIT) = 524,288) and tools/check_forbidden.py enforces the
# 2,097,152-byte .bss tripwire.
#
# ***** THE .bss PROJECTION FOR THIS TARGET, AND WHY IT IS NOT TIGHT. *****
# M13B-4 measured 2,010,976 with 86,176 of headroom. The five new objects
# contribute a MEASURED 228 bytes of .bss between them (see the object list
# above), because the M12 design stages every payload through a 4096-byte STACK
# buffer and keeps NO shadow copy of gamepak_backup[]. Projected: ~2,011,204,
# leaving ~85,948. If the measured figure lands materially above that, STOP and
# read the map -- do not raise the tripwire.
#
# ***** WATCH __data_load, NOT JUST .bss. ***** runtime/linker.ld sets
# __data_start = ALIGN(__data_load, 64K) + 64K, so the JIT reservation is driven
# by .text + .rodata + .rela and NOT by .bss. M13B-5 adds five objects and a new
# result screen; if text+rodata crosses the next 64 KB bracket, __data_start
# moves and the JIT footprint moves with it.
m13b5-size: $(M13B5ELF) $(M13B5BIN)
	@$(PYTHON) tools/sizereport.py $(M13B5ELF) $(M13B5BIN) \
	   --map $(M13B5BUILD)/$(M13B5TARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification -----------------------------------------------------------
#
# ***** THIS VERIFIER FLIPS EXACTLY THE SAVE GATES AND WEAKENS NOTHING ELSE.
#
# m13b-verify and m13b4-verify are NOT MODIFIED and still run and still prove
# absence for their own images. m13b5-verify proves the OPPOSITE for the
# persistence image -- POSITIVELY, by requiring the objects to be present AND
# requiring the references between them to exist, so an image that linked the
# save stack but never called into it could not pass.
#
# EVERY OTHER GATE IS RETAINED VERBATIM: relocation validation, symbol-kind
# validation, dependency closure, dynarec and libretro exclusion, zero undefined
# symbols, ET_DYN, the .bss tripwire, the JIT budget, the USB/mount literal gate
# and M12A's SD_CREATE gate.

# The dynarec entry point stays forbidden. execute_arm is NOT forbidden -- it is
# gpSP's own interpreter and has been in gpsp_cpu.o since M1.
M13B5_FORBIDDEN_DYNAREC = execute_arm_translate dynarec_enable

# The seven gameplay objects, PLUS the five persistence objects and gba_save.o.
M13B5_REQUIRED_OBJS = gba_exec.o gba_m8map.o gba_m10map.o gba_present.o \
                      gba_input.o gba_audio.o audio.o \
                      savedata.o gba_save.o gba_savefile.o gba_savehdr.o \
                      gba_restore.o gba_restorefile.o

# ***** THE WRITE CAPABILITY IS NOW ALLOWED -- THROUGH EXACTLY ONE OBJECT. *****
# gba_savefile.o is the only translation unit permitted to reference a write
# entry point. shim.o DEFINES them, as it always has.
M13B5_FORBIDDEN_FWRITE = fwrite fputc fputs fprintf
M13B5_WRITER_OBJ       = gba_savefile.o

# The savedata layer must really have reached the image, or a clean run would be
# VACUOUS. Same list M12B requires.
M13B5_REQUIRED_NAMES = libSceSaveData.sprx sceSaveDataUmount sceKernelMkdir \
                       sceKernelStat sceKernelUsleep SLUS-20268 SLES-50366

m13b5-verify: m13b-equiv $(M13B5ELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M13B5ELF)
	@echo "== relocations (NON-MASKING pipeline) =="
	@$(PYTHON) tools/verify_reloc.py $(M13B5ELF) $(M13B5OBJS) \
	     > $(M13B5BUILD)/reloc.log 2>&1; \
	 st=$$?; \
	 tail -12 $(M13B5BUILD)/reloc.log; \
	 if [ $$st -ne 0 ]; then \
	     echo "M13B5 VERIFY FAILED: the relocation check exited $$st."; \
	     echo "  Full output: $(M13B5BUILD)/reloc.log"; \
	     exit $$st; \
	 fi
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M13B5OBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M13B5OBJS)
	@echo "== forbidden symbols in the linked image (.bss tripwire included) =="
	@$(PYTHON) tools/check_forbidden.py $(M13B5ELF) $(M13B5OBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M13B5ELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M13B5 VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== ***** EXECUTION AND PERSISTENCE ARE BOTH LINKED ***** =="
	@missing=""; \
	 for want in $(M13B5_REQUIRED_OBJS); do \
	     found=0; \
	     for o in $(M13B5OBJS); do \
	         case "$$o" in */$$want) found=1;; esac; \
	     done; \
	     if [ $$found -eq 0 ]; then missing="$$missing $$want"; fi; \
	 done; \
	 if [ -n "$$missing" ]; then \
	     echo "M13B5 VERIFY FAILED: a required object is NOT on the link"; \
	     echo "  line:$$missing"; \
	     echo "  M13B-5 claims a complete gameplay AND persistence path. An"; \
	     echo "  image missing one of these cannot be delivering it."; \
	     exit 1; \
	 fi; \
	 echo "all thirteen required objects are PRESENT"
# PRESENCE IS NECESSARY BUT NOT SUFFICIENT. A link line can carry an object
# while nothing calls into it, and the whole-object link model (no
# --gc-sections) would keep it there. So the REFERENCES are checked too, along
# the whole chain the milestone actually claims:
#
#     m13b5main.o      --references--> m5_exec_run          (gba_exec.o)
#     gba_exec.o       --references--> execute_arm          (gpsp_cpu.o)
#     m13b5main.o      --references--> gba_restorefile_run  (gba_restorefile.o)
#     m13b5main.o      --references--> gba_savefile_commit  (gba_savefile.o)
#     m13b5main.o      --references--> plat_savedata_init   (savedata.o)
#     gba_savefile.o   --references--> plat_savedata_begin_write
#     gba_savefile.o   --references--> plat_savedata_end_write
#     gba_restore.o    --references--> gamepak_backup       (gpsp_gba_memory.o)
	@fail=0; \
	 for pair in "m13b5main.o:m5_exec_run" \
	             "gba_exec.o:execute_arm" \
	             "m13b5main.o:gba_restorefile_run" \
	             "m13b5main.o:gba_savefile_commit" \
	             "m13b5main.o:plat_savedata_init" \
	             "gba_savefile.o:plat_savedata_begin_write" \
	             "gba_savefile.o:plat_savedata_end_write" \
	             "gba_restore.o:gamepak_backup"; do \
	     o=`echo "$$pair" | cut -d: -f1`; \
	     s=`echo "$$pair" | cut -d: -f2`; \
	     if ! $(NM) -u $(M13B5BUILD)/$$o 2>/dev/null | grep -qw "$$s"; then \
	         echo "M13B5 VERIFY FAILED: $$o does NOT reference $$s."; \
	         fail=1; \
	     else \
	         echo "  $$o REFERENCES $$s"; \
	     fi; \
	 done; \
	 if [ $$fail -ne 0 ]; then \
	     echo "  A required link in the restore/commit chain is missing, so the"; \
	     echo "  image would link the save stack and never enter it."; \
	     exit 1; \
	 fi; \
	 echo "the full picker -> execute -> restore -> commit chain is REACHABLE"
	@echo "== ***** THE FIXTURE STILL TOUCHES NO gpSP SAVE GLOBAL ***** =="
# m13b5main.o goes through the adapters and must not name a gpSP save global
# itself. gba_restore.o is the ONLY object allowed to write gamepak_backup.
	@bad=""; \
	 for s in backup_type gamepak_backup flash_bank_cnt eeprom_size; do \
	     if $(NM) -u $(M13B5BUILD)/m13b5main.o 2>/dev/null | grep -qw "$$s"; then \
	         bad="$$bad $$s"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13B5 VERIFY FAILED: m13b5main.o names a gpSP save global:$$bad"; \
	     echo "  The fixture must reach save state only through the adapters."; \
	     exit 1; \
	 fi; \
	 echo "m13b5main.o names no gpSP save global -- it goes through the adapters"
	@bad=""; \
	 for o in $(M13B5BUILD)/gba_savefile.o $(M13B5BUILD)/gba_restorefile.o \
	          $(M13B5BUILD)/gba_savehdr.o; do \
	     if $(NM) -u $$o 2>/dev/null | grep -qw "gamepak_backup"; then \
	         bad="$$bad $$o"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13B5 VERIFY FAILED: a runtime-side object references"; \
	     echo "  gamepak_backup:$$bad"; \
	     echo "  Only gba_restore.o may write it."; \
	     exit 1; \
	 fi; \
	 echo "only gba_restore.o reaches gamepak_backup"
	@echo "== ***** THE DYNAREC IS STILL ABSENT ***** =="
	@for s in $(M13B5_FORBIDDEN_DYNAREC); do \
	     if $(READELF) -sW $(M13B5ELF) 2>/dev/null \
	          | awk '$$7 != "UND" { print $$8 }' | grep -qx "$$s"; then \
	         echo "M13B5 VERIFY FAILED: dynarec symbol '$$s' is DEFINED in the"; \
	         echo "  linked image. HAVE_DYNAREC must stay undefined and"; \
	         echo "  cpu_threaded.c must stay uncompiled."; \
	         exit 1; \
	     fi; \
	 done; \
	 bad=""; \
	 for o in $(M13B5OBJS); do \
	     for s in $(M13B5_FORBIDDEN_DYNAREC); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13B5 VERIFY FAILED: an object REFERENCES a dynarec entry"; \
	     echo "  point:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "execute_arm_translate and dynarec_enable are absent, defined and"; \
	 echo "  referenced alike -- the interpreter is the only execution path"
	@for o in $(M13B5OBJS); do \
	     case "$$o" in \
	       */cpu_threaded.o|*/dynarec*.o|*/libretro*.o) \
	           echo "M13B5 VERIFY FAILED: a dynarec or libretro object is"; \
	           echo "  linked: $$o"; \
	           exit 1;; \
	     esac; \
	 done; \
	 echo "no cpu_threaded, dynarec or libretro object is on the link line"
	@echo "== ***** M11's OBSERVER IS STILL NOT LINKED ***** =="
# M13B-5 owns its own dirty accounting (M12C's post-restore baseline). Linking
# M11's observer would add a 128-block dirty map nothing reads, and would invite
# a later edit to use M11's STICKY evidence as the commit decision -- which is
# exactly the mistake gba_save.h:362-372 warns against.
	@for o in $(M13B5OBJS); do \
	     case "$$o" in \
	       */gba_m11save.o) \
	           echo "M13B5 VERIFY FAILED: M11's observer is linked: $$o"; \
	           exit 1;; \
	     esac; \
	 done; \
	 echo "gba_m11save.o is absent -- the dirty decision is M12C's baseline"
	@echo "== ***** THE WRITE CAPABILITY IS CONFINED TO ONE OBJECT ***** =="
# GATE 1 of 3: no object BUT gba_savefile.o may REFERENCE a write entry point.
# This is the gate M13B-4 had as a blanket prohibition; M13B-5 narrows it rather
# than dropping it, so "the image can write" stays a statement about exactly one
# translation unit.
	@bad=""; \
	 for o in $(M13B5OBJS); do \
	     case "$$o" in */$(M13B5_WRITER_OBJ)) continue ;; esac; \
	     for s in $(M13B5_FORBIDDEN_FWRITE); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13B5 VERIFY FAILED: an object outside the writer REFERENCES a"; \
	     echo "  file-write entry point:$$bad"; \
	     echo "  Only $(M13B5_WRITER_OBJ) may write. Arbitrary fwrite"; \
	     echo "  reachability from unrelated objects is exactly what this"; \
	     echo "  milestone must NOT introduce."; \
	     exit 1; \
	 fi; \
	 echo "no object but $(M13B5_WRITER_OBJ) references a write entry point"
# GATE 2 of 3: the writer MUST reference fwrite. Without this the gate above
# would pass VACUOUSLY if the writer were stubbed out or renamed -- it would be
# reporting a confinement it never measured.
	@if ! $(NM) -u $(M13B5BUILD)/$(M13B5_WRITER_OBJ) 2>/dev/null \
	        | grep -qw "fwrite"; then \
	     echo "M13B5 VERIFY FAILED: $(M13B5_WRITER_OBJ) does NOT reference"; \
	     echo "  fwrite. M13B-5 is the milestone that WRITES a .sav; if the"; \
	     echo "  writer cannot write, a clean run would be VACUOUS."; \
	     exit 1; \
	 fi; \
	 echo "$(M13B5_WRITER_OBJ) REFERENCES fwrite -- the write path is real"
# GATE 3 of 3: no object BUT the frozen shim may DEFINE a write entry point.
	@bad=""; \
	 for o in $(M13B5OBJS); do \
	     case "$$o" in *shim.o) continue ;; esac; \
	     for s in $(M13B5_FORBIDDEN_FWRITE); do \
	         if $(NM) --defined-only $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13B5 VERIFY FAILED: a write entry point is DEFINED outside the"; \
	     echo "  frozen shim, by:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "no object but the frozen shim defines a write entry point"
	@echo "== ***** SD_CREATE IS NEVER DEFINED (M12A's gate, unchanged) ***** =="
# With a wrong dirName that flag silently builds a SECOND, EMPTY container and
# mounts THAT, so the write appears to succeed and goes nowhere. A failure that
# REPORTS SUCCESS is the worst outcome this milestone can produce.
	@if grep -nE "^[[:space:]]*#[[:space:]]*define[[:space:]]+SD_CREATE" \
	        runtime/savedata.c runtime/savedata.h \
	        adapters/gba/gba_savefile.c adapters/gba/gba_savefile.h; then \
	     echo "M13B5 VERIFY FAILED: SD_CREATE is DEFINED."; \
	     exit 1; \
	 fi; \
	 echo "SD_CREATE is not defined in the savedata layer or the save adapter"
	@echo "== rodata carve (marker-based, as M7-M12C) =="
# runtime/linker.ld FOLDS .rodata INTO THE .text OUTPUT SECTION, so there is no
# .rodata section in the linked image and `objdump -s -j .rodata` prints NOTHING.
# Every string check driven off that command therefore FAILED CLOSED against an
# empty stream -- libSceSaveData.sprx is provably in savedata.o and in the ELF,
# yet the required-name gate reported it missing. The fix is the carve M7, M8,
# M10, M12B and M12C have all used: extract the .text output section, then use
# the linker markers to cut the logical rodata window out of it. Nothing about
# the checks themselves is relaxed -- the same names remain required, and the
# same absences remain fatal; they are simply pointed at bytes that exist.
	@$(OBJCOPY) -O binary --only-section=.text $(M13B5ELF) $(M13B5BUILD)/text.bin
	@tx=$$($(NM) $(M13B5ELF) | awk '$$NF == "__text_start"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 ra=$$($(NM) $(M13B5ELF) | awk '$$NF == "__rodata_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 rz=$$($(NM) $(M13B5ELF) | awk '$$NF == "__rodata_end"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 if [ -z "$$tx" ] || [ -z "$$ra" ] || [ -z "$$rz" ]; then \
	     echo "M13B5 VERIFY FAILED: linker marker symbol missing -- need"; \
	     echo "  __text_start, __rodata_start and __rodata_end in $(M13B5ELF)."; \
	     exit 1; \
	 fi; \
	 t=$$((0x$$tx)); a=$$((0x$$ra)); z=$$((0x$$rz)); n=$$((z - a)); \
	 if [ "$$t" -ne 0 ]; then \
	     echo "M13B5 VERIFY FAILED: .text does not start at vaddr 0 (__text_start ="; \
	     echo "  $$t), so a symbol value is no longer a file offset in the"; \
	     echo "  extracted binary and this carve would read the wrong bytes."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M13B5 VERIFY FAILED: empty rodata window -- __rodata_start=$$a"; \
	     echo "  __rodata_end=$$z gives $$n bytes."; \
	     exit 1; \
	 fi; \
	 have=$$(wc -c < $(M13B5BUILD)/text.bin | tr -d " "); \
	 if [ "$$have" -lt "$$z" ]; then \
	     echo "M13B5 VERIFY FAILED: .text extraction is $$have bytes but the rodata"; \
	     echo "  window ends at $$z -- the extraction is short or .text is absent."; \
	     exit 1; \
	 fi; \
	 dd if=$(M13B5BUILD)/text.bin of=$(M13B5BUILD)/rodata.bin bs=1 skip=$$a count=$$n \
	    2>/dev/null || { \
	     echo "M13B5 VERIFY FAILED: dd could not carve the rodata window."; exit 1; \
	 }; \
	 if [ ! -s $(M13B5BUILD)/rodata.bin ]; then \
	     echo "M13B5 VERIFY FAILED: rodata extraction produced 0 bytes -- every check"; \
	     echo "  below would pass vacuously. Fix the extraction, not the checks."; \
	     exit 1; \
	 fi; \
	 got=$$(wc -c < $(M13B5BUILD)/rodata.bin | tr -d " "); \
	 if [ "$$got" -ne "$$n" ]; then \
	     echo "M13B5 VERIFY FAILED: rodata.bin is $$got bytes, expected $$n ="; \
	     echo "  __rodata_end - __rodata_start. Extraction is not trustworthy."; \
	     exit 1; \
	 fi; \
	 echo "rodata carved from .text by linker markers: [$$a,$$z) = $$n bytes"
	@echo "== ***** THE SAVEDATA LAYER REALLY REACHED THE IMAGE ***** =="
	@for s in $(M13B5_REQUIRED_NAMES); do \
	     if ! grep -aq "$$s" $(M13B5BUILD)/rodata.bin; then \
	         echo "M13B5 VERIFY FAILED: required savedata name '$$s' is NOT in"; \
	         echo "  the image. A milestone that cannot name the module it"; \
	         echo "  mounts is not mounting anything, and a clean run would be"; \
	         echo "  VACUOUS."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "libSceSaveData.sprx, sceSaveDataUmount, both region dirNames and the"; \
	 echo "  three kernel entry points are all present"
	@echo "== ***** M13B5 NAMES THE FILES IT WRITES ***** =="
	@miss=""; \
	 for s in "/savedata0/gba" ".sav" ".hdr"; do \
	     grep -aq "$$s" $(M13B5BUILD)/rodata.bin \
	        || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M13B5 VERIFY FAILED: the image does not contain:$$miss"; \
	     echo "  If the directory or either extension is absent, no filename"; \
	     echo "  can be built and a clean run would be VACUOUS."; \
	     exit 1; \
	 fi; \
	 echo "/savedata0/gba, .sav and .hdr are all present in rodata"
	@echo "== ***** THE NAMED-ROM PATH IS THE ONE IN USE ***** =="
	@if ! $(READELF) -sW $(M13B5ELF) 2>/dev/null | grep -q "m4_rom_load_named"; then \
	     echo "M13B5 VERIFY FAILED: m4_rom_load_named is absent -- the picker"; \
	     echo "  cannot be loading the operator's chosen path."; \
	     exit 1; \
	 fi
	@if ! $(READELF) -sW $(M13B5ELF) 2>/dev/null | grep -q "gba_lib_scan"; then \
	     echo "M13B5 VERIFY FAILED: gba_lib_scan is absent -- nothing enumerates."; \
	     exit 1; \
	 fi
	@echo "m4_rom_load_named and gba_lib_scan are both present"
	@echo "== ***** NO DIRECT USB, DEVICE OR MOUNT LITERAL ***** =="
# /savedata0 is reached through the savedata layer, never by a raw mount path.
	@if $(OBJDUMP) -s -j .rodata $(M13B5ELF) 2>/dev/null \
	     | grep -qiE "/mnt|/usb|/dev/|ugen|usbctl"; then \
	     echo "M13B5 VERIFY FAILED: the image contains a USB, device or mount"; \
	     echo "  literal. M13B-5 reads /temp0 and /savedata0 and nothing else."; \
	     exit 1; \
	 fi; \
	 echo "no /mnt, /usb, /dev, ugen or usbctl literal is present"
	@echo "== M13B5 VERIFY OK =="

# ---- the Lua payload --------------------------------------------------------
#
# ***** THE SAME TEMPLATE AS M13B AND M13B4, NOT A COPY OF IT. *****
# lua/m13b.lua.in carries the loader, the stage plumbing and every status name;
# rung 5 adds three status codes, three step names and one stage branch to it.
#
# THE STAGE IS FIXED AT 50 AND IS NOT AN OPERATOR CHOICE. This target has
# exactly one rung.
M13B5_LUA_TEMPLATE := lua/m13b.lua.in
M13B5_LUA_OUT      := $(M13B5BUILD)/$(M13B5TARGET).lua

m13b5-lua: m13b5-check $(M13B5BIN) $(M13B5ELF)
	@$(PYTHON) tools/mklua.py $(M13B5_LUA_TEMPLATE) $(M13B5BIN) \
	     $(M13B5_LUA_OUT) $(M13B5ELF)
	@sed -i.bak "s/@@STAGE@@/50/" $(M13B5_LUA_OUT) && rm -f $(M13B5_LUA_OUT).bak
	@echo "M13B5 LUA OK: $(M13B5_LUA_OUT) -- STAGE 50 (RUNG 5 SAVE PERSISTENCE)"

# ---- m13b5-send -- HARDWARE DELIVERY ----------------------------------------
#
# NOT AUTHORISED BY THE EXISTENCE OF THIS TARGET.
#
#   * m13b5-verify IS A PREREQUISITE, and it in turn depends on m13b-equiv, so
#     the 283-check harness runs before anything is transmitted, always.
#   * ***** CLIMB THE RUNGS IN ORDER. ***** Rungs 0-3.5 belong to the frozen
#     `m13b` target and rung 4 to `m13b4`. All six are hardware-PASSED.
#   * ***** THIS IS THE FIRST LUAport IMAGE THAT CAN WRITE TO /savedata0 WHILE
#     A GAME IS RUNNING. ***** It creates or overwrites exactly two files, both
#     named from the CARTRIDGE'S OWN CONTENT, and only on a clean exit in which
#     the save actually changed.
#   * ***** THE FIRST RUN SHOULD BE Tony Hawk's Pro Skater 2. ***** Its EEPROM
#     path is hardware-proven by M11, its write by M12B and its restore by M12C,
#     and its content-derived identity ATHE_EA49DE28 is already known. That
#     isolates the ONE new variable -- picker-selected ROM to persistence --
#     rather than testing save technology again.
#   * ***** IT IS A TWO-RUN PROOF. ***** Run A writes; then FULLY RELAUNCH THE
#     HOST GAME; then Run B must restore and THE GAME ITSELF must recognise the
#     save. A file existing is NOT the pass criterion.
#   * CAPTURE THE UDP LOG ON 9027.
m13b5-send: m13b5-verify m13b5-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m13b5-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M13B5_LUA_OUT) $(M13B5BIN)

m13b5-clean:
	rm -rf $(M13B5BUILD)

.PHONY: m13b5 m13b5-objs m13b5-check m13b5-link m13b5-size m13b5-verify \
        m13b5-lua m13b5-send m13b5-clean

# =============================================================================
# M13C -- THE LAUNCHER
# =============================================================================
#
#   make m13c-check      offline structural check, no compiler needed
#   make m13c-objs
#   make m13c-link
#   make m13c
#   make m13c-size
#   make m13c-verify
#   make m13c-lua
#   make m13c-send PS5_IP=<addr>
#   make m13c-clean
#
# ***** A FOURTH TARGET, AND EVERY ONE OF THE FIRST THREE IS UNTOUCHED. *****
#
#   build/m13b/    UNTOUCHED. No execution.        m13b-verify unmodified.
#   build/m13b4/   UNTOUCHED. Execution, no save.  m13b4-verify unmodified.
#   build/m13b5/   UNTOUCHED. Execution and save.  m13b5-verify unmodified.
#   build/m13c/    THE LAUNCHER. Same capabilities as m13b5, presented as a
#                  product instead of as a test rig, with its own verifier.
#
# ***** SEVEN HARDWARE PASSES LIVE IN THOSE THREE DIRECTORIES AND NONE OF THEM
# IS DISTURBED. ***** $(M13BOBJS), $(M13B4OBJS) and $(M13B5OBJS) are not
# touched, apps/m13bgpsp/main.c is not edited, lua/m13b.lua.in is not edited,
# and the three verifiers are not weakened by one character.
#
# ***** THE PICKER MODEL IS SHARED, NOT COPIED. ***** apps/m13bgpsp/
# m13b_picker.inc is BYTE-UNCHANGED and is reached from apps/m13cgpsp/main.c
# through -Iapps/m13bgpsp, so tools/m13b_picker_equiv.c's 283 checks prove the
# picker that ships in ALL FOUR images. m13c-verify depends on m13b-equiv for
# exactly that reason.
#
# ***** EVERY ADAPTER IS LINKED FROM BYTE-IDENTICAL, FROZEN SOURCE. ***** Not
# one line of gpsp/**, gba_save.c, gba_savefile.c, gba_savehdr.c, gba_restore.c,
# gba_restorefile.c, gba_exec.c, gba_present.c, gba_input.c, gba_audio.c,
# gba_m8map.c, gba_m10map.c, gba_rom.c, gba_bios.c, gba_probe.c, gba_library.c,
# m13store.c, runtime/savedata.c, runtime/audio.c, runtime/platform.c,
# runtime/gfx.c, runtime/shim.c or runtime/linker.ld is modified by this
# milestone. M13C IS A PRESENTATION MILESTONE.

M13CTARGET := m13cgpsp
M13CBUILD  := build/m13c
M13CELF    := $(M13CBUILD)/$(M13CTARGET).elf
M13CBIN    := $(M13CBUILD)/$(M13CTARGET).bin

$(M13CBUILD):
	mkdir -p $(M13CBUILD)

# ---- flags ------------------------------------------------------------------
#
# ***** IDENTICAL TO $(M13B5FLAGS) EXCEPT FOR THE INCLUDE PATH, AND WITH NEITHER
# STAGE DEFINE. ***** apps/m13cgpsp/main.c has no #ifdef in it at all: there is
# one flow and it is always compiled. -Iapps/m13bgpsp is the ONE addition, and
# it exists solely so m13b_picker.inc resolves.
M13C_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m13cgpsp \
                -Iapps/m13bgpsp -Iadapters/gba

M13CFLAGS = $(BASEFLAGS) $(M13C_INCLUDES) -Wall -Wno-unused-function

# ***** THE gpSP-FACING FLAGS ARE BYTE-IDENTICAL TO M13B5's. ***** Not one
# adapter compiled with these sees an M13C define. Passing one would make these
# objects differ from M10's, M12B's, M12C's and M13B-5's for no reason and would
# weaken the claim that they are unmodified.
#
# ***** -DROM_BUFFER_SIZE=2 IS NOT ALTERED. ***** It is what makes a cartridge
# over 2 MB demand paged, and it is why the save identity must be latched BEFORE
# execution.
M13C_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M13C_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M13CHEADERS = $(HEADERS)

M13CLDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
              -Wl,--build-id=none -Wl,-z,norelro \
              -Wl,--orphan-handling=error \
              -Wl,-Map=$(M13CBUILD)/$(M13CTARGET).map

# ----------------------------------------------------------------- objects ---
#
# ***** THE SAME TWENTY-FIVE + TWELVE AS M13B-5, WITH m13b5main.o REPLACED BY
# m13cmain.o AND NOTHING ELSE CHANGED. ***** The launcher needs exactly the
# capabilities the persistence image needed:
#
#   picker / library    m13store.o gba_library.o
#   named-ROM loader    gba_rom.o gba_filestream.o gba_compat.o gba_fixture.o
#   BIOS                gba_bios.o
#   probes              gba_probe.o
#   execution           gba_exec.o          THE ONLY execute_arm CALLER
#   presentation        gba_present.o gfx.o platform.o
#   input               gba_input.o gba_m8map.o
#   audio               audio.o gba_audio.o
#   paging observer     gba_m10map.o
#   save identity       gba_save.o
#   save restore        gba_restore.o gba_restorefile.o gba_savehdr.o
#   save commit         savedata.o gba_savefile.o
#   SRAM observation    gba_sramobs.o   READ-ONLY, COMMIT-SIDE, DIAGNOSTIC
#
# ***** gba_sramobs.o IS NEW TO M13C AND IT PERSISTS NOTHING. ***** It was
# written for M12C, where it could only ever look at the save AFTER a restore.
# gba_restore.c:137-173 does not blank the bytes beyond the restored region, so
# M12C's view of the upper half is its own 0xFF initialisation and is evidence
# of nothing. Only the fixture that COMMITS can measure what the game left in
# gamepak_backup[0x8000..0xFFFF], and that is this one. The object is linked
# UNMODIFIED, and the gates below assert that it can read memory and do
# absolutely nothing else.
#
# STILL ABSENT, AND EVERY ABSENCE IS A GATE:
#   gba_m11save.o       M11's observer. M13C owns its own dirty accounting
#                       (M12C's post-restore baseline) and would gain a
#                       128-block dirty map nothing reads.
#   cpu_threaded.o      every dynarec emitter and stub. HAVE_DYNAREC stays
#                       undefined and dynarec_enable stays an UNDEFINED symbol
#                       so any leak is a LINK ERROR rather than a silent
#                       success.
#   libretro*.o         and the libretro VFS.
M13C_OWN_OBJS = $(M13CBUILD)/m13cmain.o \
                $(M13CBUILD)/shim.o \
                $(M13CBUILD)/gfx.o \
                $(M13CBUILD)/platform.o \
                $(M13CBUILD)/audio.o \
                $(M13CBUILD)/savedata.o \
                $(M13CBUILD)/m13store.o \
                $(M13CBUILD)/gba_library.o \
                $(M13CBUILD)/gba_fixture.o \
                $(M13CBUILD)/gba_filestream.o \
                $(M13CBUILD)/gba_compat.o \
                $(M13CBUILD)/gba_probe.o \
                $(M13CBUILD)/gba_bios.o \
                $(M13CBUILD)/gba_rom.o \
                $(M13CBUILD)/gba_save.o \
                $(M13CBUILD)/gba_savefile.o \
                $(M13CBUILD)/gba_savehdr.o \
                $(M13CBUILD)/gba_restore.o \
                $(M13CBUILD)/gba_restorefile.o \
                $(M13CBUILD)/gba_sramobs.o \
                $(M13CBUILD)/gba_m8map.o \
                $(M13CBUILD)/gba_m10map.o \
                $(M13CBUILD)/gba_exec.o \
                $(M13CBUILD)/gba_present.o \
                $(M13CBUILD)/gba_input.o \
                $(M13CBUILD)/gba_audio.o

M13C_GPSP_OBJS = $(M13CBUILD)/gpsp_cpu.o $(M13CBUILD)/gpsp_main.o \
                 $(M13CBUILD)/gpsp_gba_memory.o $(M13CBUILD)/gpsp_video.o \
                 $(M13CBUILD)/gpsp_sound.o $(M13CBUILD)/gpsp_serial.o \
                 $(M13CBUILD)/gpsp_serial_proto.o $(M13CBUILD)/gpsp_rfu.o \
                 $(M13CBUILD)/gpsp_gbp.o $(M13CBUILD)/gpsp_cheats.o \
                 $(M13CBUILD)/gpsp_savestate.o $(M13CBUILD)/gpsp_input.o

M13COBJS = $(M13C_OWN_OBJS) $(M13C_GPSP_OBJS)

# ---- offline structural check -----------------------------------------------
m13c-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m13cgpsp/main.c
	@$(PYTHON) tools/m8_syntax_check.py apps/m13bgpsp/m13b_picker.inc
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_sramobs.c
# ***** THE THREE SAVE/RESTORE ADAPTERS THIS IMAGE CARRIES. *****
# SYNTAX ONLY, OFFLINE, COMPILER-FREE. This sweep reads source TEXT and emits
# no object, so it cannot alter one byte of the shipping image -- that is why
# it can be added under a freeze.
#
# These are the three units the FLASH128/UNKNOWN restore correction touched.
# Checking them here catches a structural break on a machine with NO compiler
# at all, long before m12c-equiv -- which needs a host cc -- ever gets to run.
# The two gates are complementary, not redundant: this one proves the sources
# still PARSE, m12c-equiv proves they still BEHAVE.
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_restore.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_savehdr.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_restorefile.c

# ---- LUAport-owned objects, runtime side ------------------------------------
#
# ***** apps/m13bgpsp/m13b_picker.inc IS A PREREQUISITE OF m13cmain.o. *****
# It is textually included, so it is a dependency exactly like a header. Without
# it here, a change to the picker math would leave a stale m13cmain.o in place
# while m13b-equiv reported the new behaviour green -- the two would silently
# describe different code.
$(M13CBUILD)/m13cmain.o: apps/m13cgpsp/main.c \
                         apps/m13bgpsp/m13b_picker.inc \
                         adapters/gba/m13store.h \
                         adapters/gba/gba_library.h \
                         adapters/gba/gba_rom.h \
                         adapters/gba/gba_probe.h \
                         adapters/gba/gba_bios.h \
                         adapters/gba/gba_save.h \
                         adapters/gba/gba_savefile.h \
                         adapters/gba/gba_restore.h \
                         adapters/gba/gba_restorefile.h \
                         adapters/gba/gba_sramobs.h \
                         adapters/gba/gba_exec.h \
                         adapters/gba/gba_present.h \
                         adapters/gba/gba_input.h \
                         adapters/gba/gba_m8map.h \
                         adapters/gba/gba_m10map.h \
                         adapters/gba/gba_audio.h runtime/audio.h \
                         runtime/savedata.h \
                         $(M13CHEADERS) | $(M13CBUILD)
	$(CC) $(M13CFLAGS) -c $< -o $@

$(M13CBUILD)/shim.o: runtime/shim.c $(M13CHEADERS) | $(M13CBUILD)
	$(CC) $(M13CFLAGS) -c $< -o $@

$(M13CBUILD)/gfx.o: runtime/gfx.c $(M13CHEADERS) | $(M13CBUILD)
	$(CC) $(M13CFLAGS) -c $< -o $@

$(M13CBUILD)/platform.o: runtime/platform.c $(M13CHEADERS) | $(M13CBUILD)
	$(CC) $(M13CFLAGS) -c $< -o $@

$(M13CBUILD)/audio.o: runtime/audio.c runtime/audio.h $(M13CHEADERS) \
                      | $(M13CBUILD)
	$(CC) $(M13CFLAGS) -c $< -o $@

# ***** M12A's SAVEDATA LAYER, FROM UNMODIFIED, FROZEN SOURCE. *****
# It is the ONLY object that names sceSaveData*, and m13c-verify asserts that.
$(M13CBUILD)/savedata.o: runtime/savedata.c runtime/savedata.h \
                         $(M13CHEADERS) | $(M13CBUILD)
	$(CC) $(M13CFLAGS) -c $< -o $@

$(M13CBUILD)/m13store.o: adapters/gba/m13store.c \
                         adapters/gba/m13store.h runtime/core.h | $(M13CBUILD)
	$(CC) $(M13CFLAGS) -c $< -o $@

$(M13CBUILD)/gba_library.o: adapters/gba/gba_library.c \
                            adapters/gba/gba_library.h \
                            adapters/gba/m13store.h | $(M13CBUILD)
	$(CC) $(M13CFLAGS) -c $< -o $@

$(M13CBUILD)/gba_present.o: adapters/gba/gba_present.c \
                            adapters/gba/gba_present.h \
                            runtime/core.h | $(M13CBUILD)
	$(CC) $(M13CFLAGS) -c $< -o $@

# ***** M12B's WRITER. THE RUNTIME HALF, SO IT GETS THE RUNTIME FLAGS. *****
# It includes runtime/savedata.h (and therefore runtime/core.h) and <stdio.h>,
# and it includes NO gpSP header at all -- which is exactly why it may coexist
# with runtime/core.h's u64. THIS IS THE ONLY OBJECT IN THE IMAGE THAT MAY CALL
# fwrite.
$(M13CBUILD)/gba_savefile.o: adapters/gba/gba_savefile.c \
                             adapters/gba/gba_savefile.h \
                             adapters/gba/gba_save.h \
                             runtime/savedata.h | $(M13CBUILD)
	$(CC) $(M13CFLAGS) -c $< -o $@

# ***** M12C's VALIDATOR. ALSO THE RUNTIME HALF. *****
# It opens both files "rb" and NEVER writes.
$(M13CBUILD)/gba_restorefile.o: adapters/gba/gba_restorefile.c \
                                adapters/gba/gba_restorefile.h \
                                adapters/gba/gba_restore.h \
                                adapters/gba/gba_savehdr.h \
                                adapters/gba/gba_save.h | $(M13CBUILD)
	$(CC) $(M13CFLAGS) -c $< -o $@

# ---- gpSP-facing adapters ---------------------------------------------------
#
# ***** THESE GET THE gpSP FLAGS AND NOTHING ELSE. ***** They include
# gpsp/common.h, so they must see the same gpSP configuration every other gpSP
# object sees. And they MUST NOT get $(M13C_INCLUDES), whose -Iruntime would put
# runtime/core.h on the include path: gpsp/common.h:100 typedefs u64 as
# `unsigned long long` and runtime/core.h:30 as `unsigned long` -- DISTINCT
# types -- so a TU that saw both would not compile.
$(M13CBUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h \
                          | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h \
                         | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h \
                        | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

# M12B's SAVE ADAPTER, FROZEN. It owns the identity, the class, the size policy,
# the hashes and the byte tap.
$(M13CBUILD)/gba_save.o: adapters/gba/gba_save.c adapters/gba/gba_save.h \
                         | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

# M12C's HEADER READER. Pure arithmetic over an unsigned char array; it includes
# nothing at all and is flag-agnostic. Given the gpSP flags to sit with its
# neighbours.
$(M13CBUILD)/gba_savehdr.o: adapters/gba/gba_savehdr.c \
                            adapters/gba/gba_savehdr.h | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

# ***** M12C's RESTORE ADAPTER. THE SOLE WRITER OF gamepak_backup[]. *****
$(M13CBUILD)/gba_restore.o: adapters/gba/gba_restore.c \
                            adapters/gba/gba_restore.h | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

# ***** M12C's SRAM OBSERVER. A READER, AND ONLY A READER. *****
# It touches gamepak_backup through a `const u8 *` and holds nothing else, so
# "the observer cannot perturb the experiment" is enforced by the COMPILER
# rather than by review. gpSP flags because it includes gpsp/common.h for u8/u32
# and for gamepak_backup itself -- the same reason gba_restore.o gets them.
# THE SOURCE IS NOT MODIFIED BY M13C; it is the M12C file, compiled again.
$(M13CBUILD)/gba_sramobs.o: adapters/gba/gba_sramobs.c \
                            adapters/gba/gba_sramobs.h | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gba_m8map.o: adapters/gba/gba_m8map.c adapters/gba/gba_m8map.h \
                          | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gba_m10map.o: adapters/gba/gba_m10map.c \
                           adapters/gba/gba_m10map.h | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h \
                         | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gba_input.o: adapters/gba/gba_input.c adapters/gba/gba_input.h \
                          | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

# NOTE THE ABSENT PREREQUISITE: runtime/audio.h is deliberately NOT listed,
# because gba_audio.c deliberately does NOT include it.
$(M13CBUILD)/gba_audio.o: adapters/gba/gba_audio.c adapters/gba/gba_audio.h \
                          | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects ------------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, IDENTICAL to M13B5's.
# gpsp/** is never modified and its own Makefile is never invoked.

$(M13CBUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M13CBUILD)
	$(CXX) $(M13C_GPSPCXXFLAGS) -c $< -o $@

$(M13CBUILD)/gpsp_video.o: gpsp/video.cc | $(M13CBUILD)
	$(CXX) $(M13C_GPSPCXXFLAGS) -c $< -o $@

$(M13CBUILD)/gpsp_main.o: gpsp/main.c | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gpsp_sound.o: gpsp/sound.c | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gpsp_serial.o: gpsp/serial.c | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

$(M13CBUILD)/gpsp_input.o: gpsp/input.c | $(M13CBUILD)
	$(CC) $(M13C_GPSPFLAGS) -c $< -o $@

m13c-objs: $(M13COBJS)

# ---- link -------------------------------------------------------------------

$(M13CELF): $(M13COBJS) runtime/linker.ld | $(M13CBUILD)
	$(CC) $(BASEFLAGS) $(M13CLDFLAGS) -o $@ $(M13COBJS)

m13c-link: $(M13COBJS) | $(M13CBUILD)
	@$(CC) $(BASEFLAGS) $(M13CLDFLAGS) -o $(M13CELF) $(M13COBJS) \
	     > $(M13CBUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to .[^']+'" $(M13CBUILD)/link.log \
	            | sed -E "s/.*to .//; s/'$$//" | sort -u); \
	 if [ $$status -ne 0 ]; then \
	     cat $(M13CBUILD)/link.log; \
	     echo "M13C LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M13C LINK FAILED: undefined references reported despite exit 0"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "M13C LINK OK: linker exit 0, no undefined references"

$(M13CBIN): $(M13CELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M13CELF) $@ \
	   --map $(M13CBUILD)/$(M13CTARGET).map --budget $(JIT_LIMIT) --arena 4194304

m13c: $(M13CBIN)

# THE TWO HARD CEILINGS, UNCHANGED AND UNRAISED. sizereport.py enforces the JIT
# budget ($(JIT_LIMIT) = 524,288) and tools/check_forbidden.py enforces the
# 2,097,152-byte .bss tripwire.
#
# ***** THE .bss PROJECTION, AND WHY IT SHOULD LAND WHERE M13B-5's DID. *****
# M13C links the SAME object set as M13B-5 with m13b5main.o replaced by
# m13cmain.o, and the two fixtures own the same three large arrays: the 518,400
# byte UI surface, the 77,280 byte GBA screen and the ~16.7 KB library table.
# The difference is in SCALARS and in rodata, not in .bss. If the measured
# figure moves materially, STOP and read the map -- do not raise the tripwire.
#
# ***** WATCH __data_start, NOT JUST .bss. ***** runtime/linker.ld sets
# __data_start = ALIGN(__data_load, 64K) + 64K, so the JIT reservation is driven
# by .text + .rodata + .rela and NOT by .bss. M13C REMOVES a great deal of
# staged-fixture rodata (every RUNG banner, the two long result screens, the
# probe bit tables that are still present but no longer printed to screen) and
# ADDS a modest amount of its own, so text+rodata may cross a 64 KB bracket in
# EITHER direction. If __data_start moves, the JIT footprint moves with it.
m13c-size: $(M13CELF) $(M13CBIN)
	@$(PYTHON) tools/sizereport.py $(M13CELF) $(M13CBIN) \
	   --map $(M13CBUILD)/$(M13CTARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification -----------------------------------------------------------
#
# ***** THIS VERIFIER PROVES M13B-5's CLAIMS PLUS ONE M13C-SPECIFIC ONE, AND
# WEAKENS NOTHING. *****
#
# m13b-verify, m13b4-verify and m13b5-verify are NOT MODIFIED and still run and
# still prove their own images. m13c-verify proves, POSITIVELY, that every
# capability the launcher advertises is linked AND reachable, and NEGATIVELY
# that the dynarec, the libretro frontend, an uncontrolled write path and the
# milestone vocabulary are all absent.
#
# EVERY GATE M13B-5 HAD IS RETAINED VERBATIM: relocation validation, symbol-kind
# validation, dependency closure, dynarec and libretro exclusion, zero undefined
# symbols, ET_DYN, the .bss tripwire, the JIT budget, the USB/mount literal gate
# and M12A's SD_CREATE gate.
#
# THREE GATES ARE NEW:
#   * execute_arm is REACHABLE ONLY THROUGH gba_exec.o -- proved by counting
#     references, not by inspection.
#   * the picker, presentation, input, audio and paging entry points are all
#     PRESENT in the linked image.
#   * ***** NO MILESTONE VOCABULARY SURVIVES IN THE IMAGE. ***** This is the
#     structural form of "clean up user-facing text". A comment cannot reach
#     rodata, so this gate can only fire on a real on-screen or logged string.

# The dynarec entry point stays forbidden. execute_arm is NOT forbidden -- it is
# gpSP's own interpreter and has been in gpsp_cpu.o since M1. It is CONFINED
# instead, below.
M13C_FORBIDDEN_DYNAREC = execute_arm_translate dynarec_enable

# Every capability the launcher claims, as an object that must be on the line.
M13C_REQUIRED_OBJS = m13store.o gba_library.o gba_rom.o gba_bios.o \
                     gba_exec.o gba_m8map.o gba_m10map.o gba_present.o \
                     gba_input.o gba_audio.o audio.o gfx.o platform.o \
                     savedata.o gba_save.o gba_savefile.o gba_savehdr.o \
                     gba_restore.o gba_restorefile.o gba_sramobs.o

# ***** WHO MAY NAME gamepak_backup, BY ROLE. *****
# The confinement sweep below pins the set of objects allowed to reach gpSP's
# save array. The set is spelled out as THREE ROLE-NAMED LISTS rather than one
# flat skip-list, because the three roles are not interchangeable and a reviewer
# adding a name needs to say WHICH ONE they are claiming.
#
# NOTE, ONCE, FOR ALL THREE LISTS: nm CANNOT DISTINGUISH A LOAD FROM A STORE.
# A relocation against gamepak_backup proves only that the object REACHES the
# symbol. These lists therefore prove CONFINEMENT -- by symbol ownership -- and
# nothing about direction. The read-only claims are carried mechanically
# elsewhere: gba_sramobs.c takes its view through a `const u8 *`, and
# `make m12c-equiv` section 15 byte-compares all 131,072 bytes of the array
# across every sample call.

# INITIALIZATION WRITER. adapters/gba/gba_rom.c:311, inside
# m4_rom_backup_init(): memset(gamepak_backup, 0xFF, sizeof(gamepak_backup)).
# This is upstream parity -- gpsp/libretro/libretro.c:1278 does the identical
# fill immediately before its own load_gamepak -- and 0xFF, not 0x00, is the
# true idle state of flash and EEPROM.
#
# IT HAS BEEN HERE SINCE M4 and is called by every fixture M4..M13C. In M13C it
# runs ONCE, at apps/m13cgpsp/main.c:1876, in step (h), immediately before
# m4_rom_load_named() at :1879 -- that is 624 source lines BEFORE
# m12c_sramobs_reset() at :2500, and no loop in _start() spans the two. It
# therefore cannot perturb the baseline, T1 (:2565), T2 (:2629), the measured
# play loop (:2633) or T3 (:2800).
M13C_BACKUP_INIT_WRITERS   = gba_rom.o

# RESTORE WRITER. The sole writer of file-sourced save bytes, double-clamped in
# gba_restore_apply() (adapters/gba/gba_restore.c:137-172) to the caller's
# region AND independently to GBA_RESTORE_BACKUP_BYTES.
M13C_BACKUP_RESTORE_WRITER = gba_restore.o

# READ-ONLY. gba_save.o computes the identity and region hashes; gba_sramobs.o
# is the commit-side capture.
M13C_BACKUP_READERS        = gba_save.o gba_sramobs.o

# The complete permitted set, as bare object basenames.
M13C_BACKUP_NAMERS = $(M13C_BACKUP_INIT_WRITERS) \
                     $(M13C_BACKUP_RESTORE_WRITER) \
                     $(M13C_BACKUP_READERS)

# ***** WHAT THE COMMIT-SIDE OBSERVER MAY NOT NAME. *****
# The observer's ENTIRE contract is "read bytes out of gamepak_backup". These
# are spelled out for M13C rather than borrowed from the M12C variables so that
# this milestone's gate stands on its own and cannot be weakened by an edit made
# for a different image.
#
# read_backup/write_backup/read_eeprom/write_eeprom all MUTATE backup_type
# (gpsp/gba_memory.c:464-465, :1140-1141, :542). An observer that called one to
# "check the save type" would MANUFACTURE the very classification the capture
# screen then reports, and the run would be measuring its own side effect --
# which is the exact failure mode this whole investigation is trying to escape.
M13C_OBS_FORBIDDEN = read_backup write_backup read_eeprom write_eeprom \
                     plat_savedata_begin_write plat_savedata_end_write \
                     plat_savedata_mkdir plat_savedata_init \
                     plat_savedata_is_ready \
                     gba_savefile_commit gba_save_chunk \
                     fopen fread fwrite fclose remove rename malloc free

# The three observer entry points the fixture must actually drive.
M13C_OBS_ENTRIES = m12c_sramobs_reset m12c_sramobs_sample m12c_sramobs_read

# ***** THE WRITE CAPABILITY IS ALLOWED THROUGH EXACTLY ONE OBJECT. *****
# gba_savefile.o is the only translation unit permitted to reference a write
# entry point. shim.o DEFINES them, as it always has.
M13C_FORBIDDEN_FWRITE = fwrite fputc fputs fprintf
M13C_WRITER_OBJ       = gba_savefile.o

# The savedata layer must really have reached the image, or a clean session
# would be VACUOUS. Same list M12B and M13B-5 require.
M13C_REQUIRED_NAMES = libSceSaveData.sprx sceSaveDataUmount sceKernelMkdir \
                      sceKernelStat sceKernelUsleep SLUS-20268 SLES-50366

# ***** THE VOCABULARY THAT MAY NOT SURVIVE INTO THE LAUNCHER. *****
# RUNG and STAGE are checked as bare words because NEITHER APPEARS IN ANY
# LINKED SOURCE OUTSIDE apps/m13bgpsp/main.c -- gpsp/**, adapters/gba/**,
# runtime/** and m13b_picker.inc contain no string literal with either, so a hit
# here can only mean staged-fixture text leaked into this image.
M13C_FORBIDDEN_WORDS = RUNG STAGE MILESTONE

# ***** HOST COMPILER PRESENCE, CHECKED BEFORE ANY HARNESS IS BUILT. *****
#
# m12c-equiv is the ONLY gate in the shipping chain that exercises save and
# restore, and it needs a HOST compiler -- which nothing else in m13c-verify
# does. Without this guard, a box with no host cc dies in the middle of a
# harness compile with a bare "cc: command not found" and no indication that
# the save gate was the casualty.
#
# ***** IT MUST NEVER DEGRADE TO A SKIP. ***** A regression gate that quietly
# disappears when a tool is missing is strictly worse than no gate at all,
# because it reports GREEN while proving nothing. There is deliberately no
# soft path, no ifdef and no "warn and continue" branch below.
m13c-hostcc:
	@if command -v $(firstword $(HOSTCC)) >/dev/null 2>&1; then \
	    echo "HOSTCC = $(HOSTCC) (present)"; \
	 else \
	    echo "M13C VERIFY FAILED: no host compiler -- HOSTCC=$(HOSTCC) is not executable."; \
	    echo "  m12c-equiv is the save/restore regression gate and is MANDATORY."; \
	    echo "  It builds and runs tools/gba_restore_equiv.c on the HOST."; \
	    echo "  Re-run with an available compiler, for example:"; \
	    echo "      make m13c-verify HOSTCC=gcc"; \
	    echo "      make m13c-verify HOSTCC=\"zig cc\""; \
	    echo "  The gate is NOT skippable: shipping M13C requires it to pass."; \
	    exit 1; \
	 fi

# ***** THE SAVE/RESTORE REGRESSION GATE IS BOUND INTO THIS CHAIN. *****
#
# m13b-equiv proves the PICKER. It says NOTHING about save or restore. Until
# this rung, m13c-verify -- and therefore m13c-send -- could go green on an
# image whose save/restore path was broken, because no gate in the shipping
# chain ever exercised it.
#
# tools/gba_restore_equiv.c TEXTUALLY INCLUDES the five SHIPPED adapter
# sources (gba_save.c, gba_savehdr.c, gba_restore.c, gba_restorefile.c,
# gba_sramobs.c), so m12c-equiv is a true regression gate over EXACTLY the
# code this milestone froze -- not a paraphrase of it. Binding it here is what
# makes that freeze ENFORCEABLE BY THE BUILD rather than by intent alone.
#
# ***** THIS ADDS NO OBJECT AND CHANGES NO IMAGE BYTE. ***** m12c-equiv builds
# a HOST binary into build/m12c/ and runs it. gba_restore_equiv appears in no
# object list and no payload rule, so $(M13COBJS), the link and $(M13CBIN) are
# untouched by its presence here. The cost is build time, not image size.
#
# m13c-hostcc is FIRST on purpose -- see above.
m13c-verify: m13c-hostcc m13b-equiv m12c-equiv $(M13CELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M13CELF)
	@echo "== relocations (NON-MASKING pipeline) =="
	@$(PYTHON) tools/verify_reloc.py $(M13CELF) $(M13COBJS) \
	     > $(M13CBUILD)/reloc.log 2>&1; \
	 st=$$?; \
	 tail -12 $(M13CBUILD)/reloc.log; \
	 if [ $$st -ne 0 ]; then \
	     echo "M13C VERIFY FAILED: the relocation check exited $$st."; \
	     echo "  Full output: $(M13CBUILD)/reloc.log"; \
	     exit $$st; \
	 fi
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M13COBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M13COBJS)
	@echo "== forbidden symbols in the linked image (.bss tripwire included) =="
	@$(PYTHON) tools/check_forbidden.py $(M13CELF) $(M13COBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M13CELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M13C VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== ***** EVERY ADVERTISED CAPABILITY IS LINKED ***** =="
	@missing=""; \
	 for want in $(M13C_REQUIRED_OBJS); do \
	     found=0; \
	     for o in $(M13COBJS); do \
	         case "$$o" in */$$want) found=1;; esac; \
	     done; \
	     if [ $$found -eq 0 ]; then missing="$$missing $$want"; fi; \
	 done; \
	 if [ -n "$$missing" ]; then \
	     echo "M13C VERIFY FAILED: a required object is NOT on the link"; \
	     echo "  line:$$missing"; \
	     echo "  M13C claims a complete picker, gameplay and persistence path."; \
	     echo "  An image missing one of these cannot be delivering it."; \
	     exit 1; \
	 fi; \
	 echo "all twenty required objects are PRESENT"
	@echo "== ***** THE COMMIT-SIDE SRAM OBSERVER IS PRESENT, DRIVEN AND READ-ONLY ***** =="
# ***** THIS IS THE CAPTURE SIDE, AND THAT IS THE WHOLE POINT. ***** M12C can
# only observe AFTER a restore, and gba_restore.c:137-173 never blanks the bytes
# past the restored region -- so M12C's upper half is its own 0xFF init and
# proves nothing about what the game committed. Only this fixture can measure
# gamepak_backup[0x8000..0xFFFF] at commit time.
	@case " $(M13COBJS) " in \
	   *" $(M13CBUILD)/gba_sramobs.o "*) ;; \
	   *) echo "M13C VERIFY FAILED: gba_sramobs.o is not on the M13C link line."; \
	      exit 1;; \
	 esac; \
	 echo "gba_sramobs.o is linked into M13C"
	@miss=""; \
	 for s in $(M13C_OBS_ENTRIES); do \
	     $(NM) $(M13CELF) 2>/dev/null | grep -qw "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M13C VERIFY FAILED: the linked image does not define:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "the linked image defines the three observer entry points"
	@miss=""; \
	 for s in $(M13C_OBS_ENTRIES); do \
	     $(NM) -u $(M13CBUILD)/m13cmain.o 2>/dev/null | grep -qw "$$s" \
	        || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M13C VERIFY FAILED: m13cmain.o does NOT reference:$$miss"; \
	     echo "  The observer would be LINKED BUT NEVER DRIVEN, and every"; \
	     echo "  sample would read back zero. Zero high-half bytes is"; \
	     echo "  indistinguishable from a measured blank upper half, so the"; \
	     echo "  run would report 'UPPER DATA BLANK' and RETIRE A LIVE"; \
	     echo "  HYPOTHESIS on the strength of a measurement that never"; \
	     echo "  happened. That is the single most dangerous wrong answer"; \
	     echo "  this instrument can give."; \
	     exit 1; \
	 fi; \
	 echo "m13cmain.o drives the observer: reset, sample and read"
	@if ! $(NM) -u $(M13CBUILD)/gba_sramobs.o 2>/dev/null \
	        | grep -qw "gamepak_backup"; then \
	     echo "M13C VERIFY FAILED: gba_sramobs.o does NOT reference"; \
	     echo "  gamepak_backup. An observer that never reaches the emulator's"; \
	     echo "  real save array measures nothing and reports it as a finding."; \
	     exit 1; \
	 fi; \
	 echo "gba_sramobs.o reaches gpSP's real save array (gamepak_backup)"
	@bad=""; \
	 for s in $(M13C_OBS_FORBIDDEN); do \
	     if $(NM) -u $(M13CBUILD)/gba_sramobs.o 2>/dev/null | grep -qw "$$s"; then \
	         bad="$$bad $$s"; \
	     fi; \
	 done; \
	 if $(NM) -u $(M13CBUILD)/gba_sramobs.o 2>/dev/null \
	      | grep -qE "plat_|sce"; then bad="$$bad plat_*-or-sce*"; fi; \
	 if [ -n "$$bad" ]; then \
	     echo "M13C VERIFY FAILED: gba_sramobs.o names:$$bad"; \
	     echo "  The observer must be able to do EXACTLY ONE thing: read bytes"; \
	     echo "  out of gamepak_backup. It may not write the save, may not"; \
	     echo "  reach the commit path, may not open a file and may not"; \
	     echo "  allocate. An instrument that can alter the run cannot be"; \
	     echo "  trusted to describe it."; \
	     exit 1; \
	 fi; \
	 echo "gba_sramobs.o names no save entry point, no commit call, no file"; \
	 echo "  call, no allocator, no plat_* and no sce* -- it can only read"
	@echo "  NOTE: nm cannot tell a load from a store. The READ-ONLY claim is"; \
	 echo "  additionally carried by the const-qualified view in"; \
	 echo "  gba_sramobs.c:114 and by 'make m12c-equiv' section 15, which"; \
	 echo "  byte-compares all 131072 bytes across every sample call."
# PRESENCE IS NECESSARY BUT NOT SUFFICIENT. A link line can carry an object while
# nothing calls into it, and the whole-object link model (no --gc-sections) would
# keep it there. So the REFERENCES are checked too, along the whole chain the
# launcher actually claims:
#
#     m13cmain.o       --references--> gba_lib_scan         (gba_library.o)
#     m13cmain.o       --references--> m4_rom_load_named    (gba_rom.o)
#     m13cmain.o       --references--> m5_exec_run          (gba_exec.o)
#     gba_exec.o       --references--> execute_arm          (gpsp_cpu.o)
#     m13cmain.o       --references--> gba_present_blit     (gba_present.o)
#     m13cmain.o       --references--> m8_input_apply       (gba_input.o)
#     m13cmain.o       --references--> gba_audio_drain      (gba_audio.o)
#     m13cmain.o       --references--> plat_audio_init      (audio.o)
#     m13cmain.o       --references--> m10map_paging_delta  (gba_m10map.o)
#     m13cmain.o       --references--> gba_restorefile_run  (gba_restorefile.o)
#     m13cmain.o       --references--> gba_savefile_commit  (gba_savefile.o)
#     m13cmain.o       --references--> plat_savedata_init   (savedata.o)
#     m13cmain.o       --references--> plat_time_init       (platform.o)
#     m13cmain.o       --references--> plat_time_us         (platform.o)
#     gba_savefile.o   --references--> plat_savedata_begin_write
#     gba_savefile.o   --references--> plat_savedata_end_write
#     gba_restore.o    --references--> gamepak_backup       (gpsp_gba_memory.o)
	@fail=0; \
	 for pair in "m13cmain.o:gba_lib_scan" \
	             "m13cmain.o:m4_rom_load_named" \
	             "m13cmain.o:m5_exec_run" \
	             "gba_exec.o:execute_arm" \
	             "m13cmain.o:gba_present_blit" \
	             "m13cmain.o:m8_input_apply" \
	             "m13cmain.o:gba_audio_drain" \
	             "m13cmain.o:plat_audio_init" \
	             "m13cmain.o:m10map_paging_delta" \
	             "m13cmain.o:gba_restorefile_run" \
	             "m13cmain.o:gba_savefile_commit" \
	             "m13cmain.o:plat_savedata_init" \
	             "m13cmain.o:plat_time_init" \
	             "m13cmain.o:plat_time_us" \
	             "gba_savefile.o:plat_savedata_begin_write" \
	             "gba_savefile.o:plat_savedata_end_write" \
	             "gba_restore.o:gamepak_backup"; do \
	     o=`echo "$$pair" | cut -d: -f1`; \
	     s=`echo "$$pair" | cut -d: -f2`; \
	     if ! $(NM) -u $(M13CBUILD)/$$o 2>/dev/null | grep -qw "$$s"; then \
	         echo "M13C VERIFY FAILED: $$o does NOT reference $$s."; \
	         fail=1; \
	     else \
	         echo "  $$o REFERENCES $$s"; \
	     fi; \
	 done; \
	 if [ $$fail -ne 0 ]; then \
	     echo "  A required link in the picker -> play -> restore -> commit"; \
	     echo "  chain is missing, so the image would link a capability and"; \
	     echo "  never enter it."; \
	     exit 1; \
	 fi; \
	 echo "the full picker -> execute -> restore -> commit chain is REACHABLE"
	@echo "== ***** execute_arm IS REACHABLE ONLY THROUGH gba_exec.o ***** =="
# THE CONFINEMENT, MEASURED RATHER THAN ASSERTED. gpsp_cpu.o DEFINES
# execute_arm; adapters/gba/gba_exec.c is the only file in the tree that CALLS
# it. If any other object ever grew a reference, the "one execution path" claim
# would be false and nothing else in this verifier would notice.
	@bad=""; \
	 for o in $(M13COBJS); do \
	     case "$$o" in */gba_exec.o) continue ;; esac; \
	     if $(NM) -u $$o 2>/dev/null | grep -qw "execute_arm"; then \
	         bad="$$bad $$o"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13C VERIFY FAILED: an object outside gba_exec.o REFERENCES"; \
	     echo "  execute_arm:$$bad"; \
	     exit 1; \
	 fi; \
	 defs=""; \
	 for o in $(M13COBJS); do \
	     if $(NM) --defined-only $$o 2>/dev/null | grep -qw "execute_arm"; then \
	         defs="$$defs $$o"; \
	     fi; \
	 done; \
	 case "$$defs" in \
	   " $(M13CBUILD)/gpsp_cpu.o") ;; \
	   *) echo "M13C VERIFY FAILED: execute_arm is defined by:$$defs"; \
	      echo "  It must be defined by gpsp_cpu.o and by nothing else."; \
	      exit 1;; \
	 esac; \
	 echo "gba_exec.o is the ONLY caller and gpsp_cpu.o the ONLY definer"
	@echo "== ***** THE LAUNCHER TOUCHES NO gpSP SAVE GLOBAL ***** =="
# m13cmain.o goes through the adapters and must not name a gpSP save global
# itself. gba_restore.o is the ONLY object allowed to write gamepak_backup.
	@bad=""; \
	 for s in backup_type gamepak_backup flash_bank_cnt eeprom_size; do \
	     if $(NM) -u $(M13CBUILD)/m13cmain.o 2>/dev/null | grep -qw "$$s"; then \
	         bad="$$bad $$s"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13C VERIFY FAILED: m13cmain.o names a gpSP save global:$$bad"; \
	     echo "  The launcher must reach save state only through the adapters."; \
	     exit 1; \
	 fi; \
	 echo "m13cmain.o names no gpSP save global -- it goes through the adapters"
	@bad=""; \
	 for o in $(M13CBUILD)/gba_savefile.o $(M13CBUILD)/gba_restorefile.o \
	          $(M13CBUILD)/gba_savehdr.o; do \
	     if $(NM) -u $$o 2>/dev/null | grep -qw "gamepak_backup"; then \
	         bad="$$bad $$o"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13C VERIFY FAILED: a runtime-side object references"; \
	     echo "  gamepak_backup:$$bad"; \
	     echo "  Only gba_restore.o may write it."; \
	     exit 1; \
	 fi; \
	 echo "no runtime-side writer object reaches gamepak_backup"
# ***** THE NAMER LIST IS SPELLED OUT BY ROLE, SO GROWING IT IS A DELIBERATE
# ACT. *****
# This is the only gamepak_backup gate in the project that sweeps the WHOLE
# object list, so it is the only one that can actually justify the claim it
# prints. It is checked against $(M13C_BACKUP_NAMERS) -- the union of the
# INITIALIZATION WRITER, the RESTORE WRITER and the READ-ONLY readers declared
# above. gpsp_*.o is skipped because gpSP owns the array.
#
# A FIFTH object gaining access is a verify failure rather than a silent change.
# Matching is on the exact basename, so no wildcard can quietly widen the set.
	@bad=""; \
	 for o in $(M13COBJS); do \
	     case "$$o" in \
	       */gpsp_*.o) continue ;; \
	     esac; \
	     ok=""; \
	     for a in $(M13C_BACKUP_NAMERS); do \
	         if [ "$${o##*/}" = "$$a" ]; then ok=1; fi; \
	     done; \
	     if [ -n "$$ok" ]; then continue; fi; \
	     if $(NM) -u $$o 2>/dev/null | grep -qw "gamepak_backup"; then \
	         bad="$$bad $$o"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13C VERIFY FAILED: an unexpected object reaches"; \
	     echo "  gamepak_backup:$$bad"; \
	     echo "  The permitted set, BY ROLE, is:"; \
	     echo "    gba_rom.o      INITIALIZATION WRITER (0xFF pre-load fill,"; \
	     echo "                   once per launch, historical since M4)"; \
	     echo "    gba_restore.o  RESTORE WRITER (sole writer of restored bytes)"; \
	     echo "    gba_save.o     READ-ONLY (identity/region hashes)"; \
	     echo "    gba_sramobs.o  READ-ONLY (commit-side capture)"; \
	     echo "  Anything else reaching the save array is a new, unreviewed"; \
	     echo "  path into it."; \
	     exit 1; \
	 fi; \
	 echo "gamepak_backup is reached by exactly:"; \
	 echo "  gba_rom.o (INITIALIZATION WRITER), gba_restore.o (RESTORE WRITER),"; \
	 echo "  gba_save.o and gba_sramobs.o (READ-ONLY)"; \
	 echo "  NOTE: nm cannot distinguish a load from a store. This proves"; \
	 echo "  CONFINEMENT by symbol ownership, not direction. gba_sramobs.o's"; \
	 echo "  read-only behaviour is proven separately by m12c-equiv section 15"; \
	 echo "  and by its const-qualified implementation."
# ***** THE 0xFF PRE-LOAD FILL MUST STILL BE THERE. *****
# The sweep above is a NEGATIVE check -- it can only catch an object that gains
# access. This is the matching POSITIVE one. If m4_rom_backup_init() is ever
# deleted or refactored until it no longer touches the array, cartridges would
# silently boot against a backup array that was never filled with 0xFF -- the
# wrong blank-save state, and indistinguishable at runtime from a real save.
# No other gate in the project would catch that, so it is caught here.
	@if ! $(NM) -u $(M13CBUILD)/gba_rom.o 2>/dev/null \
	      | grep -qw "gamepak_backup"; then \
	     echo "M13C VERIFY FAILED: gba_rom.o does NOT reference gamepak_backup."; \
	     echo "  m4_rom_backup_init() (adapters/gba/gba_rom.c:311) must still"; \
	     echo "  memset the array to 0xFF before load_gamepak, as it has since"; \
	     echo "  M4 and as gpsp/libretro/libretro.c:1278 does upstream. Without"; \
	     echo "  it cartridges boot with the wrong blank-save state."; \
	     exit 1; \
	 fi; \
	 echo "gba_rom.o still reaches gamepak_backup -- the 0xFF pre-load fill is"; \
	 echo "  present"
	@echo "== ***** THE DYNAREC IS ABSENT ***** =="
	@for s in $(M13C_FORBIDDEN_DYNAREC); do \
	     if $(READELF) -sW $(M13CELF) 2>/dev/null \
	          | awk '$$7 != "UND" { print $$8 }' | grep -qx "$$s"; then \
	         echo "M13C VERIFY FAILED: dynarec symbol '$$s' is DEFINED in the"; \
	         echo "  linked image. HAVE_DYNAREC must stay undefined and"; \
	         echo "  cpu_threaded.c must stay uncompiled."; \
	         exit 1; \
	     fi; \
	 done; \
	 bad=""; \
	 for o in $(M13COBJS); do \
	     for s in $(M13C_FORBIDDEN_DYNAREC); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13C VERIFY FAILED: an object REFERENCES a dynarec entry"; \
	     echo "  point:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "execute_arm_translate and dynarec_enable are absent, defined and"; \
	 echo "  referenced alike -- the interpreter is the only execution path"
	@for o in $(M13COBJS); do \
	     case "$$o" in \
	       */cpu_threaded.o|*/dynarec*.o|*/libretro*.o) \
	           echo "M13C VERIFY FAILED: a dynarec or libretro object is"; \
	           echo "  linked: $$o"; \
	           exit 1;; \
	     esac; \
	 done; \
	 echo "no cpu_threaded, dynarec or libretro object is on the link line"
	@echo "== ***** M11's OBSERVER IS NOT LINKED ***** =="
# M13C owns its own dirty accounting (M12C's post-restore baseline). Linking
# M11's observer would add a 128-block dirty map nothing reads, and would invite
# a later edit to use M11's STICKY evidence as the commit decision -- which is
# exactly the mistake gba_save.h:362-372 warns against.
	@for o in $(M13COBJS); do \
	     case "$$o" in \
	       */gba_m11save.o) \
	           echo "M13C VERIFY FAILED: M11's observer is linked: $$o"; \
	           exit 1;; \
	     esac; \
	 done; \
	 echo "gba_m11save.o is absent -- the dirty decision is M12C's baseline"
	@echo "== ***** THE WRITE CAPABILITY IS CONFINED TO ONE OBJECT ***** =="
# GATE 1 of 3: no object BUT gba_savefile.o may REFERENCE a write entry point.
	@bad=""; \
	 for o in $(M13COBJS); do \
	     case "$$o" in */$(M13C_WRITER_OBJ)) continue ;; esac; \
	     for s in $(M13C_FORBIDDEN_FWRITE); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13C VERIFY FAILED: an object outside the writer REFERENCES a"; \
	     echo "  file-write entry point:$$bad"; \
	     echo "  Only $(M13C_WRITER_OBJ) may write."; \
	     exit 1; \
	 fi; \
	 echo "no object but $(M13C_WRITER_OBJ) references a write entry point"
# GATE 2 of 3: the writer MUST reference fwrite. Without this the gate above
# would pass VACUOUSLY if the writer were stubbed out or renamed.
	@if ! $(NM) -u $(M13CBUILD)/$(M13C_WRITER_OBJ) 2>/dev/null \
	        | grep -qw "fwrite"; then \
	     echo "M13C VERIFY FAILED: $(M13C_WRITER_OBJ) does NOT reference"; \
	     echo "  fwrite. The launcher WRITES a .sav; if the writer cannot"; \
	     echo "  write, a clean session would be VACUOUS."; \
	     exit 1; \
	 fi; \
	 echo "$(M13C_WRITER_OBJ) REFERENCES fwrite -- the write path is real"
# GATE 3 of 3: no object BUT the frozen shim may DEFINE a write entry point.
	@bad=""; \
	 for o in $(M13COBJS); do \
	     case "$$o" in *shim.o) continue ;; esac; \
	     for s in $(M13C_FORBIDDEN_FWRITE); do \
	         if $(NM) --defined-only $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13C VERIFY FAILED: a write entry point is DEFINED outside the"; \
	     echo "  frozen shim, by:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "no object but the frozen shim defines a write entry point"
	@echo "== ***** SD_CREATE IS NEVER DEFINED (M12A's gate, unchanged) ***** =="
# With a wrong dirName that flag silently builds a SECOND, EMPTY container and
# mounts THAT, so the write appears to succeed and goes nowhere. A failure that
# REPORTS SUCCESS is the worst outcome this launcher can produce.
	@if grep -nE "^[[:space:]]*#[[:space:]]*define[[:space:]]+SD_CREATE" \
	        runtime/savedata.c runtime/savedata.h \
	        adapters/gba/gba_savefile.c adapters/gba/gba_savefile.h; then \
	     echo "M13C VERIFY FAILED: SD_CREATE is DEFINED."; \
	     exit 1; \
	 fi; \
	 echo "SD_CREATE is not defined in the savedata layer or the save adapter"
	@echo "== rodata carve (marker-based, as M7-M13B5) =="
# runtime/linker.ld FOLDS .rodata INTO THE .text OUTPUT SECTION, so there is no
# .rodata section in the linked image and `objdump -s -j .rodata` prints NOTHING.
# Every string check driven off that command would FAIL CLOSED against an empty
# stream -- or, worse, PASS VACUOUSLY when it is a negative check. So the string
# gates below are pointed at bytes that exist: extract the .text output section,
# then use the linker markers to cut the logical rodata window out of it. This
# is the carve M7, M8, M10, M12B, M12C and M13B-5 all use.
	@$(OBJCOPY) -O binary --only-section=.text $(M13CELF) $(M13CBUILD)/text.bin
	@tx=$$($(NM) $(M13CELF) | awk '$$NF == "__text_start"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 ra=$$($(NM) $(M13CELF) | awk '$$NF == "__rodata_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 rz=$$($(NM) $(M13CELF) | awk '$$NF == "__rodata_end"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 if [ -z "$$tx" ] || [ -z "$$ra" ] || [ -z "$$rz" ]; then \
	     echo "M13C VERIFY FAILED: linker marker symbol missing -- need"; \
	     echo "  __text_start, __rodata_start and __rodata_end in $(M13CELF)."; \
	     exit 1; \
	 fi; \
	 t=$$((0x$$tx)); a=$$((0x$$ra)); z=$$((0x$$rz)); n=$$((z - a)); \
	 if [ "$$t" -ne 0 ]; then \
	     echo "M13C VERIFY FAILED: .text does not start at vaddr 0 (__text_start ="; \
	     echo "  $$t), so a symbol value is no longer a file offset in the"; \
	     echo "  extracted binary and this carve would read the wrong bytes."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M13C VERIFY FAILED: empty rodata window -- __rodata_start=$$a"; \
	     echo "  __rodata_end=$$z gives $$n bytes."; \
	     exit 1; \
	 fi; \
	 have=$$(wc -c < $(M13CBUILD)/text.bin | tr -d " "); \
	 if [ "$$have" -lt "$$z" ]; then \
	     echo "M13C VERIFY FAILED: .text extraction is $$have bytes but the rodata"; \
	     echo "  window ends at $$z -- the extraction is short or .text is absent."; \
	     exit 1; \
	 fi; \
	 dd if=$(M13CBUILD)/text.bin of=$(M13CBUILD)/rodata.bin bs=1 skip=$$a count=$$n \
	    2>/dev/null || { \
	     echo "M13C VERIFY FAILED: dd could not carve the rodata window."; exit 1; \
	 }; \
	 if [ ! -s $(M13CBUILD)/rodata.bin ]; then \
	     echo "M13C VERIFY FAILED: rodata extraction produced 0 bytes -- every check"; \
	     echo "  below would pass vacuously. Fix the extraction, not the checks."; \
	     exit 1; \
	 fi; \
	 got=$$(wc -c < $(M13CBUILD)/rodata.bin | tr -d " "); \
	 if [ "$$got" -ne "$$n" ]; then \
	     echo "M13C VERIFY FAILED: rodata.bin is $$got bytes, expected $$n ="; \
	     echo "  __rodata_end - __rodata_start. Extraction is not trustworthy."; \
	     exit 1; \
	 fi; \
	 echo "rodata carved from .text by linker markers: [$$a,$$z) = $$n bytes"
	@echo "== ***** THE SAVEDATA LAYER REALLY REACHED THE IMAGE ***** =="
	@for s in $(M13C_REQUIRED_NAMES); do \
	     if ! grep -aq "$$s" $(M13CBUILD)/rodata.bin; then \
	         echo "M13C VERIFY FAILED: required savedata name '$$s' is NOT in"; \
	         echo "  the image. A launcher that cannot name the module it"; \
	         echo "  mounts is not mounting anything, and a clean session"; \
	         echo "  would be VACUOUS."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "libSceSaveData.sprx, sceSaveDataUmount, both region dirNames and the"; \
	 echo "  three kernel entry points are all present"
	@echo "== ***** THE LAUNCHER NAMES THE FILES IT WRITES ***** =="
	@miss=""; \
	 for s in "/savedata0/gba" ".sav" ".hdr"; do \
	     grep -aq "$$s" $(M13CBUILD)/rodata.bin \
	        || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M13C VERIFY FAILED: the image does not contain:$$miss"; \
	     echo "  If the directory or either extension is absent, no filename"; \
	     echo "  can be built and a clean session would be VACUOUS."; \
	     exit 1; \
	 fi; \
	 echo "/savedata0/gba, .sav and .hdr are all present in rodata"
	@echo "== ***** THE NAMED-ROM PATH AND THE PICKER ARE THE ONES IN USE ***** =="
	@miss=""; \
	 for s in m4_rom_load_named gba_lib_scan gba_lib_path m13store_init \
	          m5_exec_run gba_present_blit m8_input_apply gba_audio_drain \
	          plat_audio_init plat_video_present plat_pad_read \
	          m10map_paging_delta gba_restorefile_run gba_savefile_commit \
	          plat_savedata_init gba_save_latch; do \
	     $(READELF) -sW $(M13CELF) 2>/dev/null | grep -q "$$s" \
	        || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M13C VERIFY FAILED: an advertised entry point is absent from the"; \
	     echo "  linked image:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "picker, named-ROM loader, execution, presentation, input, audio,"; \
	 echo "  paging observer, restore and commit are all present in the image"
	@echo "== ***** NO DIRECT USB, DEVICE OR MOUNT LITERAL ***** =="
# /savedata0 is reached through the savedata layer, never by a raw mount path,
# and M13C implements no removable storage at all -- it reads /temp0 and
# /savedata0 and nothing else. Driven off the CARVED rodata rather than off
# `objdump -j .rodata`, which would print nothing and pass vacuously.
	@if grep -aqiE "/mnt|/usb|/dev/|ugen|usbctl" $(M13CBUILD)/rodata.bin; then \
	     echo "M13C VERIFY FAILED: the image contains a USB, device or mount"; \
	     echo "  literal. M13C reads /temp0 and /savedata0 and nothing else."; \
	     exit 1; \
	 fi; \
	 echo "no /mnt, /usb, /dev, ugen or usbctl literal is present"
	@echo "== ***** NO MILESTONE VOCABULARY IN THE USER-FACING IMAGE ***** =="
# ***** THIS IS THE STRUCTURAL FORM OF "CLEAN UP THE USER-FACING TEXT". *****
# A comment cannot reach rodata, so this can only fire on a real string --
# something drawn on screen or sent to the log. The M13B fixtures keep every one
# of these words and m13b-verify does NOT check for them; they are evidence
# there and inappropriate here, and that distinction is now enforced rather than
# reviewed.
	@bad=""; \
	 for w in $(M13C_FORBIDDEN_WORDS); do \
	     if grep -aq "$$w" $(M13CBUILD)/rodata.bin; then bad="$$bad $$w"; fi; \
	 done; \
	 for p in "RE-RUN WITH" "NO GBA CODE EXECUTED" "NOTHING IS WRITTEN" \
	          "NO GAME RUNS AT THIS" "NO ROM WAS LOADED"; do \
	     if grep -aq "$$p" $(M13CBUILD)/rodata.bin; then bad="$$bad [$$p]"; fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M13C VERIFY FAILED: staged-fixture vocabulary survives in the"; \
	     echo "  image:$$bad"; \
	     echo "  M13C is the launcher. That wording belongs to the frozen M13B"; \
	     echo "  fixtures, where it is evidence, and nowhere near this one."; \
	     exit 1; \
	 fi; \
	 echo "no RUNG, STAGE or MILESTONE text and no staged-fixture phrasing"
	@echo "== ***** THE LAUNCHER SPEAKS THE OPERATOR'S LANGUAGE ***** =="
# THE POSITIVE HALF OF THE GATE ABOVE. Removing the old words is only half the
# task; the new ones have to actually be there, or a build that dropped every
# string would pass the negative check perfectly.
	@miss=""; \
	 for p in "LUAPORT GBA" "NO GBA ROMS FOUND" "SESSION COMPLETE" \
	          "SAVE UPDATED" "SAVE UNCHANGED" "BIOS NOT FOUND" \
	          "ROM COULD NOT BE OPENED" "ROM INVALID" "SAVE CORRUPT" \
	          "SAVE WRITE FAILED" "VIDEO INIT FAILED" \
	          "CONTROLLER NOT AVAILABLE" "AUDIO INIT FAILED" \
	          "TIMING INIT FAILED" "ROM MAPPING FAILED" "WATCHDOG EXIT" \
	          "CROSS LAUNCH" "CIRCLE EXIT" "ERROR " "STEP "; do \
	     grep -aq "$$p" $(M13CBUILD)/rodata.bin || miss="$$miss [$$p]"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M13C VERIFY FAILED: a required user-facing string is absent"; \
	     echo "  from the image:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "the title, the empty-library screen, the summary and all eleven"; \
	 echo "  error headings are present, with ERROR and STEP beneath them"
	@echo "== ***** NO AUTOMATIC SESSION-DURATION EXIT ***** =="
# M13B-5 ends its run on a WALL CLOCK after 30 seconds, because M13B-5 is a
# measurement. A launcher must not: hardware showed that window closing Metroid
# Fusion before a room could be played through. The terminator is DELETED rather
# than lengthened, and that is checked at the SOURCE -- a deleted code path
# leaves nothing behind in the binary for a symbol or string test to find.
	@if grep -Eq "M13C_SESSION_SECONDS|run_max_us|M13C_END_SESSION" \
	        apps/m13cgpsp/main.c; then \
	     echo "M13C VERIFY FAILED: a session-duration terminator survives in"; \
	     echo "  apps/m13cgpsp/main.c. Normal gameplay must end on the"; \
	     echo "  operator's chord, NEVER on elapsed time."; \
	     exit 1; \
	 fi; \
	 echo "no session-duration terminator: the frame loop cannot time out"
# THE WATCHDOG IS RETAINED, NOT REMOVED. Task 7A keeps it as an emergency
# backstop; its length is pinned by _Static_assert in the source, so this only
# has to prove it still exists and still bounds the loop.
	@if ! grep -q "iter < M13C_PLAY_MAX_ITERS" apps/m13cgpsp/main.c; then \
	     echo "M13C VERIFY FAILED: the emergency watchdog no longer bounds"; \
	     echo "  the frame loop. It must be RETAINED as a backstop."; \
	     exit 1; \
	 fi; \
	 echo "the emergency watchdog still bounds the loop (864,000 iters = 240 min)"
	@echo "== M13C VERIFY OK =="

# ---- the Lua payload --------------------------------------------------------
#
# ***** ITS OWN TEMPLATE, NOT A REUSE OF lua/m13b.lua.in. ***** That script
# carries a STAGE selector, seven stage names and three per-stage banners, and
# it is FROZEN as the driver for three hardware-passed images. M13C has one flow
# and no selector, so it needs no @@STAGE@@ substitution and this target has no
# sed step.
M13C_LUA_TEMPLATE := lua/m13c.lua.in
M13C_LUA_OUT      := $(M13CBUILD)/$(M13CTARGET).lua

m13c-lua: m13c-check $(M13CBIN) $(M13CELF)
	@$(PYTHON) tools/mklua.py $(M13C_LUA_TEMPLATE) $(M13CBIN) \
	     $(M13C_LUA_OUT) $(M13CELF)
	@echo "M13C LUA OK: $(M13C_LUA_OUT)"

# ---- m13c-send -- HARDWARE DELIVERY -----------------------------------------
#
# NOT AUTHORISED BY THE EXISTENCE OF THIS TARGET.
#
#   * m13c-verify IS A PREREQUISITE, and it in turn depends on m13b-equiv, so
#     the 283-check picker harness runs before anything is transmitted, always.
#   * ***** M13C IS OFFLINE-GREEN ONLY. ***** It has had NO hardware run. The
#     seven M13B passes are evidence for the MACHINERY it reuses, not for this
#     image's own flow.
#   * ***** THE FIRST HARDWARE TEST IS A TWO-CARTRIDGE, TWO-LAUNCH PROOF. *****
#     Two ROMs in /temp0; open the launcher; pick Metroid; play; exit with the
#     chord; read the summary; RELAUNCH THE HOST GAME; pick Tony Hawk; its
#     persisted save must restore and its commit must still be correct. A file
#     existing is NOT the pass criterion -- the GAME must recognise the save.
#   * CAPTURE THE UDP LOG ON 9027. M13C draws no diagnostics, so the log is
#     where every measurement now lives.
m13c-send: m13c-verify m13c-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m13c-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M13C_LUA_OUT) $(M13CBIN)

m13c-clean:
	rm -rf $(M13CBUILD)

.PHONY: m13c m13c-objs m13c-check m13c-link m13c-size m13c-verify \
        m13c-hostcc m13c-lua m13c-send m13c-clean

# =============================================================================
# M14A -- THE FRAME PROFILER
# =============================================================================
#
#   make m14diag-check      offline structural check, no compiler needed
#   make m14diag-objs
#   make m14diag-link
#   make m14diag
#   make m14diag-size
#   make m14diag-verify
#   make m14diag-lua
#   make m14diag-send PS5_IP=<addr>
#   make m14diag-clean
#
# ***** A FIFTH TARGET FAMILY, AND EVERY EXISTING ONE IS UNTOUCHED. *****
#
#   build/m13b/    UNTOUCHED.   m13b-verify   unmodified.
#   build/m13b4/   UNTOUCHED.   m13b4-verify  unmodified.
#   build/m13b5/   UNTOUCHED.   m13b5-verify  unmodified.
#   build/m13c/    UNTOUCHED.   m13c-verify   unmodified. THE LAUNCHER.
#   build/m14diag/ THE PROFILER. Its own directory, objects and verifier.
#
# ***** NOT ONE M13C LINE IS EDITED BY THIS MILESTONE. ***** $(M13COBJS),
# apps/m13cgpsp/main.c, lua/m13c.lua.in and m13c-verify are not touched, and
# m14diag-verify PROVES it rather than asserting it -- see the "M13C IS NOT
# MODIFIED" section below.
#
# ***** THE PICKER MODEL IS SHARED, NOT COPIED. ***** apps/m13bgpsp/
# m13b_picker.inc is BYTE-UNCHANGED and is reached through -Iapps/m13bgpsp, so
# tools/m13b_picker_equiv.c's 283 checks prove the picker that ships in ALL FIVE
# images. m14diag-verify depends on m13b-equiv for exactly that reason.
#
# ***** EVERY ADAPTER IS LINKED FROM BYTE-IDENTICAL, FROZEN SOURCE. ***** Not one
# line of gpsp/**, adapters/gba/** or runtime/** is modified. M14A IS A
# MEASUREMENT MILESTONE: the only new source file in the repository is
# apps/m14diag/main.c.

M14ATARGET := m14diag
M14ABUILD  := build/m14diag
M14AELF    := $(M14ABUILD)/$(M14ATARGET).elf
M14ABIN    := $(M14ABUILD)/$(M14ATARGET).bin

$(M14ABUILD):
	mkdir -p $(M14ABUILD)

# ---- flags ------------------------------------------------------------------
#
# ***** IDENTICAL TO $(M13CFLAGS) EXCEPT FOR THE APP INCLUDE PATH. ***** No new
# warning level, no new optimisation setting, no M14A define reaches any adapter.
# A profiler built with different flags from the thing it profiles would be
# measuring a different program.
M14A_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m14diag \
                -Iapps/m13bgpsp -Iadapters/gba

M14AFLAGS = $(BASEFLAGS) $(M14A_INCLUDES) -Wall -Wno-unused-function

# ***** THE gpSP-FACING FLAGS ARE BYTE-IDENTICAL TO M13C's. *****
# ***** -DROM_BUFFER_SIZE=2 IS NOT ALTERED. ***** M14A MEASURES the paging
# behaviour that this setting produces; changing it would destroy the very
# baseline the milestone exists to establish.
M14A_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M14A_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M14AHEADERS = $(HEADERS)

M14ALDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
              -Wl,--build-id=none -Wl,-z,norelro \
              -Wl,--orphan-handling=error \
              -Wl,-Map=$(M14ABUILD)/$(M14ATARGET).map

# ----------------------------------------------------------------- objects ---
#
# ***** M13C's OBJECT SET WITH m13cmain.o REPLACED BY m14amain.o AND
# gba_sramobs.o DROPPED. THAT IS THE WHOLE DIFFERENCE. *****
#
# ***** WHY gba_sramobs.o IS DELIBERATELY ABSENT. ***** It is M12C's commit-side
# SRAM capture: 10,432 bytes of .bss and fifteen numbers about gamepak_backup's
# upper half. It is pure forensics, a PROFILER has no use for it, and the whole
# object set is linked without --gc-sections -- so an unreferenced object would
# carry its .bss for nothing. M13C remains its proper home and keeps it. The
# absence is a GATE below, not a coincidence.
#
# STILL ABSENT, AND EVERY ABSENCE IS A GATE:
#   gba_m11save.o       M11's observer -- a 128-block dirty map nothing reads.
#   cpu_threaded.o      every dynarec emitter and stub. HAVE_DYNAREC stays
#                       undefined and dynarec_enable stays an UNDEFINED symbol
#                       so any leak is a LINK ERROR rather than a silent success.
#   libretro*.o         and the libretro VFS.
M14A_OWN_OBJS = $(M14ABUILD)/m14amain.o \
                $(M14ABUILD)/shim.o \
                $(M14ABUILD)/gfx.o \
                $(M14ABUILD)/platform.o \
                $(M14ABUILD)/audio.o \
                $(M14ABUILD)/savedata.o \
                $(M14ABUILD)/m13store.o \
                $(M14ABUILD)/gba_library.o \
                $(M14ABUILD)/gba_fixture.o \
                $(M14ABUILD)/gba_filestream.o \
                $(M14ABUILD)/gba_compat.o \
                $(M14ABUILD)/gba_probe.o \
                $(M14ABUILD)/gba_bios.o \
                $(M14ABUILD)/gba_rom.o \
                $(M14ABUILD)/gba_save.o \
                $(M14ABUILD)/gba_savefile.o \
                $(M14ABUILD)/gba_savehdr.o \
                $(M14ABUILD)/gba_restore.o \
                $(M14ABUILD)/gba_restorefile.o \
                $(M14ABUILD)/gba_m8map.o \
                $(M14ABUILD)/gba_m10map.o \
                $(M14ABUILD)/gba_exec.o \
                $(M14ABUILD)/gba_present.o \
                $(M14ABUILD)/gba_input.o \
                $(M14ABUILD)/gba_audio.o

M14A_GPSP_OBJS = $(M14ABUILD)/gpsp_cpu.o $(M14ABUILD)/gpsp_main.o \
                 $(M14ABUILD)/gpsp_gba_memory.o $(M14ABUILD)/gpsp_video.o \
                 $(M14ABUILD)/gpsp_sound.o $(M14ABUILD)/gpsp_serial.o \
                 $(M14ABUILD)/gpsp_serial_proto.o $(M14ABUILD)/gpsp_rfu.o \
                 $(M14ABUILD)/gpsp_gbp.o $(M14ABUILD)/gpsp_cheats.o \
                 $(M14ABUILD)/gpsp_savestate.o $(M14ABUILD)/gpsp_input.o

M14AOBJS = $(M14A_OWN_OBJS) $(M14A_GPSP_OBJS)

# Every capability the profiler advertises must be ON THE LINK LINE.
M14A_REQUIRED_OBJS = m14amain.o m13store.o gba_library.o gba_rom.o \
                     gba_filestream.o gba_compat.o gba_fixture.o gba_bios.o \
                     gba_probe.o gba_exec.o gba_present.o gfx.o platform.o \
                     gba_input.o gba_m8map.o audio.o gba_audio.o gba_m10map.o \
                     gba_save.o gba_restore.o gba_restorefile.o gba_savehdr.o \
                     savedata.o gba_savefile.o

# ---- offline structural check -----------------------------------------------
#
# ***** NO COMPILER IS NEEDED FOR ANY OF THIS, WHICH IS WHY THE LOOP GATES RUN
# HERE TOO. ***** The frame-loop order, the timestamp count, the absence of a HUD
# and the glyph coverage are all source properties, so they can be proved before
# a single object exists -- and on a workstation with no toolchain this is the
# only check that can run at all.
m14diag-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m14diag/main.c
	@$(PYTHON) tools/m8_syntax_check.py apps/m13bgpsp/m13b_picker.inc
	@$(PYTHON) tools/m14a_loop_check.py apps/m14diag/main.c

# ---- LUAport-owned objects, runtime side ------------------------------------
#
# ***** apps/m13bgpsp/m13b_picker.inc IS A PREREQUISITE OF m14amain.o. ***** It
# is textually included, so it is a dependency exactly like a header.
$(M14ABUILD)/m14amain.o: apps/m14diag/main.c \
                         apps/m13bgpsp/m13b_picker.inc \
                         adapters/gba/m13store.h \
                         adapters/gba/gba_library.h \
                         adapters/gba/gba_rom.h \
                         adapters/gba/gba_probe.h \
                         adapters/gba/gba_bios.h \
                         adapters/gba/gba_save.h \
                         adapters/gba/gba_savefile.h \
                         adapters/gba/gba_restore.h \
                         adapters/gba/gba_restorefile.h \
                         adapters/gba/gba_exec.h \
                         adapters/gba/gba_present.h \
                         adapters/gba/gba_input.h \
                         adapters/gba/gba_m8map.h \
                         adapters/gba/gba_m10map.h \
                         adapters/gba/gba_audio.h runtime/audio.h \
                         runtime/savedata.h \
                         $(M14AHEADERS) | $(M14ABUILD)
	$(CC) $(M14AFLAGS) -c $< -o $@

$(M14ABUILD)/shim.o: runtime/shim.c $(M14AHEADERS) | $(M14ABUILD)
	$(CC) $(M14AFLAGS) -c $< -o $@

$(M14ABUILD)/gfx.o: runtime/gfx.c $(M14AHEADERS) | $(M14ABUILD)
	$(CC) $(M14AFLAGS) -c $< -o $@

$(M14ABUILD)/platform.o: runtime/platform.c $(M14AHEADERS) | $(M14ABUILD)
	$(CC) $(M14AFLAGS) -c $< -o $@

$(M14ABUILD)/audio.o: runtime/audio.c runtime/audio.h $(M14AHEADERS) \
                      | $(M14ABUILD)
	$(CC) $(M14AFLAGS) -c $< -o $@

$(M14ABUILD)/savedata.o: runtime/savedata.c runtime/savedata.h \
                         $(M14AHEADERS) | $(M14ABUILD)
	$(CC) $(M14AFLAGS) -c $< -o $@

$(M14ABUILD)/m13store.o: adapters/gba/m13store.c \
                         adapters/gba/m13store.h runtime/core.h | $(M14ABUILD)
	$(CC) $(M14AFLAGS) -c $< -o $@

$(M14ABUILD)/gba_library.o: adapters/gba/gba_library.c \
                            adapters/gba/gba_library.h \
                            adapters/gba/m13store.h | $(M14ABUILD)
	$(CC) $(M14AFLAGS) -c $< -o $@

$(M14ABUILD)/gba_present.o: adapters/gba/gba_present.c \
                            adapters/gba/gba_present.h \
                            runtime/core.h | $(M14ABUILD)
	$(CC) $(M14AFLAGS) -c $< -o $@

# THE ONLY OBJECT IN THE IMAGE THAT MAY CALL fwrite.
$(M14ABUILD)/gba_savefile.o: adapters/gba/gba_savefile.c \
                             adapters/gba/gba_savefile.h \
                             adapters/gba/gba_save.h \
                             runtime/savedata.h | $(M14ABUILD)
	$(CC) $(M14AFLAGS) -c $< -o $@

# M12C's VALIDATOR. It opens both files "rb" and NEVER writes.
$(M14ABUILD)/gba_restorefile.o: adapters/gba/gba_restorefile.c \
                                adapters/gba/gba_restorefile.h \
                                adapters/gba/gba_restore.h \
                                adapters/gba/gba_savehdr.h \
                                adapters/gba/gba_save.h | $(M14ABUILD)
	$(CC) $(M14AFLAGS) -c $< -o $@

# ---- gpSP-facing adapters ---------------------------------------------------
#
# ***** THESE GET THE gpSP FLAGS AND NOTHING ELSE. ***** They include
# gpsp/common.h, and they MUST NOT get $(M14A_INCLUDES), whose -Iruntime would
# put runtime/core.h on the include path: gpsp/common.h:100 typedefs u64 as
# `unsigned long long` and runtime/core.h:30 as `unsigned long` -- DISTINCT
# types -- so a TU that saw both would not compile.
$(M14ABUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h \
                          | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h \
                         | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h \
                        | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gba_save.o: adapters/gba/gba_save.c adapters/gba/gba_save.h \
                         | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gba_savehdr.o: adapters/gba/gba_savehdr.c \
                            adapters/gba/gba_savehdr.h | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

# ***** M12C's RESTORE ADAPTER. THE SOLE WRITER OF gamepak_backup[]. *****
$(M14ABUILD)/gba_restore.o: adapters/gba/gba_restore.c \
                            adapters/gba/gba_restore.h | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gba_m8map.o: adapters/gba/gba_m8map.c adapters/gba/gba_m8map.h \
                          | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gba_m10map.o: adapters/gba/gba_m10map.c \
                           adapters/gba/gba_m10map.h | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h \
                         | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gba_input.o: adapters/gba/gba_input.c adapters/gba/gba_input.h \
                          | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

# NOTE THE ABSENT PREREQUISITE: runtime/audio.h is deliberately NOT listed,
# because gba_audio.c deliberately does NOT include it.
$(M14ABUILD)/gba_audio.o: adapters/gba/gba_audio.c adapters/gba/gba_audio.h \
                          | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects ------------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, IDENTICAL to M13C's.
# gpsp/** is never modified, its own Makefile is never invoked, AND NOT ONE gpSP
# OBJECT IS INSTRUMENTED -- the profiler measures from OUTSIDE the emulator.

$(M14ABUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M14ABUILD)
	$(CXX) $(M14A_GPSPCXXFLAGS) -c $< -o $@

$(M14ABUILD)/gpsp_video.o: gpsp/video.cc | $(M14ABUILD)
	$(CXX) $(M14A_GPSPCXXFLAGS) -c $< -o $@

$(M14ABUILD)/gpsp_main.o: gpsp/main.c | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gpsp_sound.o: gpsp/sound.c | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gpsp_serial.o: gpsp/serial.c | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

$(M14ABUILD)/gpsp_input.o: gpsp/input.c | $(M14ABUILD)
	$(CC) $(M14A_GPSPFLAGS) -c $< -o $@

m14diag-objs: $(M14AOBJS)

# ---- link -------------------------------------------------------------------

$(M14AELF): $(M14AOBJS) runtime/linker.ld | $(M14ABUILD)
	$(CC) $(BASEFLAGS) $(M14ALDFLAGS) -o $@ $(M14AOBJS)

m14diag-link: $(M14AOBJS) | $(M14ABUILD)
	@$(CC) $(BASEFLAGS) $(M14ALDFLAGS) -o $(M14AELF) $(M14AOBJS) \
	     > $(M14ABUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to .[^']+'" $(M14ABUILD)/link.log \
	            | sed -E "s/.*to .//; s/'$$//" | sort -u); \
	 if [ $$status -ne 0 ]; then \
	     cat $(M14ABUILD)/link.log; \
	     echo "M14A LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M14A LINK FAILED: undefined references reported despite exit 0"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "M14A LINK OK: linker exit 0, no undefined references"

$(M14ABIN): $(M14AELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M14AELF) $@ \
	   --map $(M14ABUILD)/$(M14ATARGET).map --budget $(JIT_LIMIT) --arena 4194304

m14diag: $(M14ABIN)

# THE TWO HARD CEILINGS, UNCHANGED AND UNRAISED. sizereport.py enforces the JIT
# budget ($(JIT_LIMIT) = 524,288) and tools/check_forbidden.py enforces the
# 2,097,152-byte .bss tripwire.
#
# ***** THE .bss PROJECTION. ***** M14A links M13C's object set MINUS
# gba_sramobs.o (which carries 10,432 bytes of .bss) and adds under 350 bytes of
# profile accumulators. It owns the same three large arrays M13C does -- the
# 518,400-byte UI surface, the 77,280-byte GBA screen and the ~16.7 KB library
# table -- so .bss should land BELOW M13C's 2,011,328. THE PROFILER DELIBERATELY
# HAS NO PER-FRAME HISTORY BUFFER: 864,000 frames of u32 would be 3.4 MB and
# would blow the tripwire on its own.
#
# ***** IF EITHER CEILING IS EXCEEDED, STOP AND REPORT. ***** Do not raise the
# tripwire and do not shrink unrelated code to fit. A profiler that required the
# budget to be relaxed would be changing the thing it measures.
m14diag-size: $(M14AELF) $(M14ABIN)
	@$(PYTHON) tools/sizereport.py $(M14AELF) $(M14ABIN) \
	   --map $(M14ABUILD)/$(M14ATARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification -----------------------------------------------------------
#
# ***** THIS VERIFIER PROVES THE PROFILER IS A PROFILER AND NOTHING ELSE. *****
#
# It reuses every structural gate M13C passes, and adds the gates that are
# specific to an instrument: that M13C is untouched, that gpSP is uninstrumented,
# that the frame loop is still M13C's frame loop, that nothing was optimised, and
# that the measurement is honestly reported (RAW, OVERHEAD and CORRECTED all
# present; RESIDUAL shown as a phase). No existing verifier is weakened by one
# character.
m14diag-verify: m13b-equiv $(M14AELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M14AELF)
	@echo "== relocations (NON-MASKING pipeline) =="
	@$(PYTHON) tools/verify_reloc.py $(M14AELF) $(M14AOBJS) \
	     > $(M14ABUILD)/reloc.log 2>&1; \
	 st=$$?; \
	 tail -12 $(M14ABUILD)/reloc.log; \
	 if [ $$st -ne 0 ]; then \
	     echo "M14A VERIFY FAILED: the relocation check exited $$st."; \
	     echo "  Full output: $(M14ABUILD)/reloc.log"; \
	     exit $$st; \
	 fi
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M14AOBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M14AOBJS)
	@echo "== forbidden symbols in the linked image (.bss tripwire included) =="
	@$(PYTHON) tools/check_forbidden.py $(M14AELF) $(M14AOBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M14AELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M14A VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== ***** EVERY ADVERTISED CAPABILITY IS LINKED ***** =="
	@missing=""; \
	 for want in $(M14A_REQUIRED_OBJS); do \
	     found=0; \
	     for o in $(M14AOBJS); do \
	         case "$$o" in */$$want) found=1;; esac; \
	     done; \
	     if [ $$found -eq 0 ]; then missing="$$missing $$want"; fi; \
	 done; \
	 if [ -n "$$missing" ]; then \
	     echo "M14A VERIFY FAILED: a required object is NOT on the link"; \
	     echo "  line:$$missing"; \
	     echo "  The profiler reproduces M13C's whole flow. An image missing"; \
	     echo "  one of these is not profiling that flow."; \
	     exit 1; \
	 fi; \
	 echo "all twenty-four required objects are PRESENT"
	@echo "== ***** M13C IS NOT MODIFIED BY THIS MILESTONE ***** =="
# The strongest claim this milestone makes is that it changed nothing. A claim
# that costs nothing to state is worth checking: if M14A had been implemented by
# editing the launcher instead of adding a fixture, these greps would catch it.
	@if grep -qE "M14A|m14a_|m14diag" apps/m13cgpsp/main.c; then \
	     echo "M14A VERIFY FAILED: apps/m13cgpsp/main.c mentions M14A. The"; \
	     echo "  launcher must be BYTE-UNCHANGED by this milestone -- the"; \
	     echo "  profiler is a SEPARATE fixture, which is the entire reason it"; \
	     echo "  was justified over instrumenting M13C directly."; \
	     exit 1; \
	 fi; \
	 echo "apps/m13cgpsp/main.c does not mention M14A"
	@bad=""; \
	 for d in gpsp adapters/gba runtime; do \
	     if grep -rqE "M14A|m14a_|m14diag" $$d 2>/dev/null; then \
	         bad="$$bad $$d"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M14A VERIFY FAILED: M14A names appear under:$$bad"; \
	     echo "  gpsp/**, adapters/gba/** and runtime/** are FROZEN for this"; \
	     echo "  milestone. Instrumentation lives in apps/m14diag/main.c only."; \
	     exit 1; \
	 fi; \
	 echo "gpsp/**, adapters/gba/** and runtime/** contain no M14A reference"
	@echo "== ***** gpSP IS NOT INSTRUMENTED ***** =="
# The profiler measures from OUTSIDE the emulator. If a clock read had been added
# inside gpSP the phase boundaries would no longer correspond to the launcher's
# steps, and the numbers would describe a program M13C does not ship.
	@if grep -rqE "plat_time_us|plat_time_init" gpsp 2>/dev/null; then \
	     echo "M14A VERIFY FAILED: gpsp/** reads the LUAport clock. The"; \
	     echo "  emulator core must remain uninstrumented."; \
	     exit 1; \
	 fi; \
	 echo "no gpsp/** source reads plat_time_us or plat_time_init"
	@echo "== ***** THE SRAM FORENSICS OBSERVER IS DELIBERATELY ABSENT ***** =="
	@case " $(M14AOBJS) " in \
	   *" $(M14ABUILD)/gba_sramobs.o "*) \
	      echo "M14A VERIFY FAILED: gba_sramobs.o is on the M14A link line."; \
	      echo "  It is 10,432 bytes of .bss of pure commit-side forensics and"; \
	      echo "  the profiler never calls it, so linking it would spend the"; \
	      echo "  .bss budget on an object that cannot be reached. M13C is its"; \
	      echo "  home."; \
	      exit 1;; \
	 esac; \
	 echo "gba_sramobs.o is NOT linked, and nothing references it"
	@if $(NM) -u $(M14ABUILD)/m14amain.o 2>/dev/null \
	      | grep -qE "m12c_sramobs"; then \
	     echo "M14A VERIFY FAILED: m14amain.o references the SRAM observer,"; \
	     echo "  whose object is not linked. That is a link error waiting to"; \
	     echo "  happen and a sign the fixture was copied without pruning."; \
	     exit 1; \
	 fi; \
	 echo "m14amain.o names no SRAM observer entry point"
	@echo "== ***** THE MEASURED PATH IS REALLY WIRED UP ***** =="
# Presence is necessary but not sufficient: a link line can carry an object while
# nothing calls into it, and the whole-object link model (no --gc-sections) would
# keep it there. The chain below is the one the PROFILE depends on -- if
# m14amain.o did not reference plat_time_us, every figure would be zero.
	@fail=0; \
	 for pair in "m14amain.o:gba_lib_scan" \
	             "m14amain.o:m4_rom_load_named" \
	             "m14amain.o:m5_exec_run" \
	             "gba_exec.o:execute_arm" \
	             "m14amain.o:gba_present_blit" \
	             "m14amain.o:plat_video_present" \
	             "m14amain.o:m8_input_apply" \
	             "m14amain.o:gba_audio_pull" \
	             "m14amain.o:gba_audio_resample" \
	             "m14amain.o:gba_audio_drain" \
	             "m14amain.o:plat_audio_init" \
	             "m14amain.o:m10map_paging_delta" \
	             "m14amain.o:gba_restorefile_run" \
	             "m14amain.o:gba_savefile_commit" \
	             "m14amain.o:plat_savedata_init" \
	             "m14amain.o:plat_time_init" \
	             "m14amain.o:plat_time_us"; do \
	     o=$${pair%%:*}; s=$${pair##*:}; \
	     if ! $(NM) -u $(M14ABUILD)/$$o 2>/dev/null | grep -qw "$$s"; then \
	         echo "  MISSING: $$o does not reference $$s"; \
	         fail=1; \
	     fi; \
	 done; \
	 if [ $$fail -ne 0 ]; then \
	     echo "M14A VERIFY FAILED: the measured path is not fully wired."; \
	     exit 1; \
	 fi; \
	 echo "every reference on the measured path is present"
	@echo "== ***** THE LOOP IS STILL M13C's LOOP ***** =="
# ALL TEN STEPS, IN M13C's ORDER. A profiler that reordered the loop -- most
# temptingly by moving the audio block above the flip -- would be measuring a
# different program and would silently re-pace the emulator.
	@miss=""; \
	 for s in plat_pad_read m8_input_from_pad m8_input_apply \
	          m8_input_irq_edge m5_exec_run gba_present_blit \
	          plat_video_present gba_audio_pull gba_audio_resample \
	          gba_audio_drain; do \
	     grep -q "$$s" apps/m14diag/main.c || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M14A VERIFY FAILED: a frame-loop step is absent from the"; \
	     echo "  profiler:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "all ten frame-loop steps are present in apps/m14diag/main.c"
# ***** THE ORDER, THE TIMESTAMP COUNT, THE ABSENCE OF A HUD AND THE GLYPH
# COVERAGE ARE ALL SOURCE PROPERTIES, SO THEY ARE CHECKED AT THE SOURCE. ***** A
# linked image can prove a string exists and a symbol is referenced; it cannot
# prove the ten steps are still in M13C's order or that the loop takes exactly
# nine readings. tools/m14a_loop_check.py is a separate script rather than a
# recipe heredoc because this Makefile does not set .ONESHELL -- every recipe
# line is its own shell, so a multi-line heredoc would silently not run, and for
# a NEGATIVE gate that means PASSING VACUOUSLY.
	@$(PYTHON) tools/m14a_loop_check.py apps/m14diag/main.c
	@echo "== ***** NOTHING WAS OPTIMISED AND NOTHING IS SKIPPED ***** =="
# M14A produces numbers. M14B and beyond may act on them, one measured change at
# a time. These are the settings a profiler is most tempted to "improve".
	@if ! grep -q "ROM_BUFFER_SIZE=2" Makefile; then \
	     echo "M14A VERIFY FAILED: -DROM_BUFFER_SIZE=2 is gone."; \
	     exit 1; \
	 fi; \
	 echo "ROM_BUFFER_SIZE is still 2"
# ***** THE "PROFILER NEVER WRITES skip_next_frame" GATE MOVED INTO
# tools/m14a_loop_check.py (RUN ABOVE). ***** It used to be a grep here:
#
#     ^[^/*]*skip_next_frame[ \t]*=[^=]
#
# which was wrong in both directions. grep is line-oriented, so `[^/*]*` was
# only a GUESS at "this line is code" -- it false-positived on a block-comment
# continuation line that quoted `u32 skip_next_frame = 0;` from
# adapters/gba/gba_fixture.c:70, because the '/*' that opened the comment was on
# the previous line. It also MISSED every compound assignment, both increment
# forms and `&skip_next_frame`, since it demanded '=' immediately after the
# identifier. The replacement strips comments AND string literals first, catches
# all of those forms, and proves itself against its mutation and legal-read
# controls before it will report on the file -- run `m14a_loop_check.py
# --self-test` to see those controls exercised by name. The counts live in the
# script so they cannot go stale here. Do not re-add an approximation.
	@if ! grep -q "skip_next_frame != 0u" apps/m14diag/main.c; then \
	     echo "M14A VERIFY FAILED: the frameskip gate is not asserted."; \
	     exit 1; \
	 fi; \
	 echo "the frameskip gate is re-asserted every frame"
	@if grep -qE "\[M14A_PLAY_MAX_ITERS\]|\[864000\]" apps/m14diag/main.c; then \
	     echo "M14A VERIFY FAILED: a per-frame history array exists. 864,000"; \
	     echo "  frames of u32 is 3.4 MB and would blow the .bss tripwire on"; \
	     echo "  its own. Percentiles come from the nine-bucket histogram."; \
	     exit 1; \
	 fi; \
	 echo "no per-frame history buffer: percentiles come from the histogram"
	@if grep -qE "M14A_SESSION_SECONDS|run_max_us|M14A_END_SESSION" \
	        apps/m14diag/main.c; then \
	     echo "M14A VERIFY FAILED: a session-duration terminator exists in"; \
	     echo "  apps/m14diag/main.c. The profiler reads the clock nine times"; \
	     echo "  per frame and must COMPARE IT TO NOTHING -- measuring time is"; \
	     echo "  not the same as being governed by it."; \
	     exit 1; \
	 fi; \
	 echo "no session-duration terminator: the frame loop cannot time out"
	@if ! grep -q "iter < M14A_PLAY_MAX_ITERS" apps/m14diag/main.c; then \
	     echo "M14A VERIFY FAILED: the emergency watchdog no longer bounds"; \
	     echo "  the frame loop. It must be RETAINED as a backstop."; \
	     exit 1; \
	 fi; \
	 echo "the emergency watchdog still bounds the loop (864,000 iters = 240 min)"
	@echo "== rodata carve =="
	@$(OBJCOPY) -O binary --only-section=.text $(M14AELF) $(M14ABUILD)/text.bin
	@tx=$$($(NM) $(M14AELF) | awk '$$NF == "__text_start"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 ra=$$($(NM) $(M14AELF) | awk '$$NF == "__rodata_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 rz=$$($(NM) $(M14AELF) | awk '$$NF == "__rodata_end"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 if [ -z "$$tx" ] || [ -z "$$ra" ] || [ -z "$$rz" ]; then \
	     echo "M14A VERIFY FAILED: linker marker symbol missing."; \
	     exit 1; \
	 fi; \
	 t=$$((0x$$tx)); a=$$((0x$$ra)); z=$$((0x$$rz)); n=$$((z - a)); \
	 if [ "$$t" -ne 0 ]; then \
	     echo "M14A VERIFY FAILED: .text does not start at vaddr 0."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M14A VERIFY FAILED: empty rodata window."; \
	     exit 1; \
	 fi; \
	 have=$$(wc -c < $(M14ABUILD)/text.bin | tr -d " "); \
	 if [ "$$have" -lt "$$z" ]; then \
	     echo "M14A VERIFY FAILED: .text extraction is short."; \
	     exit 1; \
	 fi; \
	 dd if=$(M14ABUILD)/text.bin of=$(M14ABUILD)/rodata.bin bs=1 skip=$$a count=$$n \
	    2>/dev/null || { \
	     echo "M14A VERIFY FAILED: dd could not carve the rodata window."; exit 1; \
	 }; \
	 if [ ! -s $(M14ABUILD)/rodata.bin ]; then \
	     echo "M14A VERIFY FAILED: rodata extraction produced 0 bytes -- every"; \
	     echo "  check below would pass vacuously."; \
	     exit 1; \
	 fi; \
	 got=$$(wc -c < $(M14ABUILD)/rodata.bin | tr -d " "); \
	 if [ "$$got" -ne "$$n" ]; then \
	     echo "M14A VERIFY FAILED: rodata.bin is $$got bytes, expected $$n."; \
	     exit 1; \
	 fi; \
	 echo "rodata carved from .text by linker markers: [$$a,$$z) = $$n bytes"
	@echo "== ***** THE MEASUREMENT IS HONESTLY REPORTED ***** =="
# ***** THE THREE P0 COLUMNS MUST ALL BE ON THE SCREEN. ***** The whole M9-D
# discipline is that the timer's own cost is REPORTED rather than assumed, and
# that the corrected figure never REPLACES the raw one. An image carrying only
# CORRECTED would be silently subtracting.
	@miss=""; \
	 for p in "RAW" "OVERHEAD" "CORRECTED" "RESIDUAL" "SAMPLED" "SKIPPED" \
	          "NATIVE GBA LATE" "HOST VSYNC LATE" "SEVERE" \
	          "WORST" "TIMER COST" "MEASURED"; do \
	     grep -aq "$$p" $(M14ABUILD)/rodata.bin || miss="$$miss [$$p]"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M14A VERIFY FAILED: a required reporting string is absent from"; \
	     echo "  the image:$$miss"; \
	     echo "  RAW, OVERHEAD and CORRECTED must ALL be present: the corrected"; \
	     echo "  figure may never replace the raw one, and RESIDUAL may never"; \
	     echo "  be hidden."; \
	     exit 1; \
	 fi; \
	 echo "RAW, OVERHEAD, CORRECTED and RESIDUAL are all reported, and the"; \
	 echo "  three deadlines are named separately"
	@echo "== ***** THE RANKING IS BY MEASURED TIME, NOT BY OBJECT SIZE ***** =="
	@if ! grep -aq "NOT SORTED BY OBJECT SIZE" $(M14ABUILD)/rodata.bin; then \
	     echo "M14A VERIFY FAILED: the ranking disclaimer is absent. The plan"; \
	     echo "  phase could only rank by inference and got the direction wrong"; \
	     echo "  at least once; the whole point of M14A is that this ranking is"; \
	     echo "  sorted by accumulated microseconds."; \
	     exit 1; \
	 fi; \
	 echo "the result screen states that the ranking is by measured time"
	@echo "== M14A VERIFY OK =="

# ---- the Lua payload --------------------------------------------------------
#
# ***** ITS OWN TEMPLATE. lua/m13c.lua.in IS NOT EDITED AND NOT REUSED IN
# PLACE. ***** The delivery machinery it contains is hardware-proven from M0
# through M13C and is therefore reproduced rather than re-derived; only the
# labels and the dbg[] documentation differ, because M14A packs the profile into
# dbg[5] and dbg[6] where M13C packs the two save hashes.
M14A_LUA_TEMPLATE := lua/m14diag.lua.in
M14A_LUA_OUT      := $(M14ABUILD)/$(M14ATARGET).lua

m14diag-lua: m14diag-check $(M14ABIN) $(M14AELF)
	@$(PYTHON) tools/mklua.py $(M14A_LUA_TEMPLATE) $(M14ABIN) \
	     $(M14A_LUA_OUT) $(M14AELF)
	@echo "M14A LUA OK: $(M14A_LUA_OUT)"

# ---- m14diag-send -- HARDWARE DELIVERY --------------------------------------
#
# NOT AUTHORISED BY THE EXISTENCE OF THIS TARGET.
#
#   * m14diag-verify IS A PREREQUISITE, and it in turn depends on m13b-equiv, so
#     the 283-check picker harness runs before anything is transmitted, always.
#   * ***** M14A IS OFFLINE-GREEN ONLY. ***** It has had NO hardware run.
#   * ***** THE FIRST HARDWARE TEST IS A TEN-MINUTE BASELINE. ***** One ROM in
#     /temp0; open the profiler; pick it; play for about ten minutes WITHOUT
#     touching the exit chord; then hold L1+R1+L2+R2. PHOTOGRAPH THE RESULT
#     SCREEN -- it is the entire product of the run and the UDP log is not a
#     guaranteed channel.
#   * ***** READ P0 FIRST. ***** If nine plat_time_us() calls are a material
#     fraction of 16,667 us then the late-frame counts describe the PROFILER and
#     not the launcher, and the run must be read that way.
#   * CAPTURE THE UDP LOG ON 9027.
m14diag-send: m14diag-verify m14diag-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m14diag-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M14A_LUA_OUT) $(M14ABIN)

m14diag-clean:
	rm -rf $(M14ABUILD)

.PHONY: m14diag m14diag-objs m14diag-check m14diag-link m14diag-size \
        m14diag-verify m14diag-lua m14diag-send m14diag-clean

# =============================================================================
# M14B -- THE FRAME-BUDGET ATTRIBUTION FIXTURE
# =============================================================================
#
#   make m14bdiag-check      offline structural check, no compiler needed
#   make m14bdiag-objs
#   make m14bdiag-link
#   make m14bdiag
#   make m14bdiag-size
#   make m14bdiag-verify
#   make m14bdiag-lua
#   make m14bdiag-send PS5_IP=<addr>
#   make m14bdiag-clean
#
# ***** A SIXTH TARGET FAMILY, AND EVERY EXISTING ONE IS UNTOUCHED. *****
#
#   build/m13b/     UNTOUCHED.  m13b-verify    unmodified.
#   build/m13b4/    UNTOUCHED.  m13b4-verify   unmodified.
#   build/m13b5/    UNTOUCHED.  m13b5-verify   unmodified.
#   build/m13c/     UNTOUCHED.  m13c-verify    unmodified. THE LAUNCHER.
#   build/m14diag/  UNTOUCHED.  m14diag-verify unmodified. THE BASELINE.
#   build/m14bdiag/ THE ATTRIBUTION FIXTURE. Its own directory and verifier.
#
# ***** M14A IS NOW FROZEN EVIDENCE AND M14B PROVES IT RATHER THAN ASSERTING
# IT. ***** apps/m14diag/main.c holds the baseline this milestone is trying to
# explain. If M14B had been implemented by editing that file, the baseline and
# the explanation would no longer be independent and the comparison would be
# worthless. The "M14A IS NOT MODIFIED" gate below is the check.
#
# ***** M14B ADDS NO CLOCK READ. ***** It takes THE SAME NINE plat_time_us()
# readings at THE SAME NINE PLACES and only cross-tabulates the spans M14A
# already computed. tools/m14a_loop_check.py enforces the count and the
# ordering for BOTH fixtures from the same gate set -- see --app below.
#
# ***** EVERY ADAPTER IS STILL LINKED FROM BYTE-IDENTICAL, FROZEN SOURCE. *****
# Not one line of gpsp/**, adapters/gba/** or runtime/** is modified. The only
# new source file in the repository is apps/m14bdiag/main.c.

M14BTARGET := m14bdiag
M14BBUILD  := build/m14bdiag
M14BELF    := $(M14BBUILD)/$(M14BTARGET).elf
M14BBIN    := $(M14BBUILD)/$(M14BTARGET).bin

$(M14BBUILD):
	mkdir -p $(M14BBUILD)

# ---- flags ------------------------------------------------------------------
#
# ***** IDENTICAL TO $(M14AFLAGS) EXCEPT FOR THE APP INCLUDE PATH. ***** M14B
# must measure the SAME PROGRAM M14A measured. A different warning level, a
# different optimisation setting or a new -D reaching an adapter would make the
# two sets of numbers incomparable, which would defeat the entire milestone.
M14B_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m14bdiag \
                -Iapps/m13bgpsp -Iadapters/gba

M14BFLAGS = $(BASEFLAGS) $(M14B_INCLUDES) -Wall -Wno-unused-function

# ***** -DROM_BUFFER_SIZE=2 IS NOT ALTERED. ***** M14B is DIAGNOSTIC-ONLY. It
# exists to find out where the frame budget goes BEFORE anything is changed;
# changing the paging configuration here would be optimising ahead of the
# measurement and would invalidate the comparison with M14A's baseline.
M14B_GPSPFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M14B_GPSPCXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M14BHEADERS = $(HEADERS)

M14BLDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
              -Wl,--build-id=none -Wl,-z,norelro \
              -Wl,--orphan-handling=error \
              -Wl,-Map=$(M14BBUILD)/$(M14BTARGET).map

# ----------------------------------------------------------------- objects ---
#
# ***** M14A's OBJECT SET WITH m14amain.o REPLACED BY m14bmain.o. THAT IS THE
# WHOLE DIFFERENCE. ***** gba_sramobs.o stays absent for M14A's reason, and
# every other absence below is still a GATE:
#   gba_m11save.o       M11's observer -- a 128-block dirty map nothing reads.
#   cpu_threaded.o      every dynarec emitter and stub. HAVE_DYNAREC stays
#                       undefined and dynarec_enable stays an UNDEFINED symbol
#                       so any leak is a LINK ERROR rather than a silent success.
#   libretro*.o         and the libretro VFS.
M14B_OWN_OBJS = $(M14BBUILD)/m14bmain.o \
                $(M14BBUILD)/shim.o \
                $(M14BBUILD)/gfx.o \
                $(M14BBUILD)/platform.o \
                $(M14BBUILD)/audio.o \
                $(M14BBUILD)/savedata.o \
                $(M14BBUILD)/m13store.o \
                $(M14BBUILD)/gba_library.o \
                $(M14BBUILD)/gba_fixture.o \
                $(M14BBUILD)/gba_filestream.o \
                $(M14BBUILD)/gba_compat.o \
                $(M14BBUILD)/gba_probe.o \
                $(M14BBUILD)/gba_bios.o \
                $(M14BBUILD)/gba_rom.o \
                $(M14BBUILD)/gba_save.o \
                $(M14BBUILD)/gba_savefile.o \
                $(M14BBUILD)/gba_savehdr.o \
                $(M14BBUILD)/gba_restore.o \
                $(M14BBUILD)/gba_restorefile.o \
                $(M14BBUILD)/gba_m8map.o \
                $(M14BBUILD)/gba_m10map.o \
                $(M14BBUILD)/gba_exec.o \
                $(M14BBUILD)/gba_present.o \
                $(M14BBUILD)/gba_input.o \
                $(M14BBUILD)/gba_audio.o

M14B_GPSP_OBJS = $(M14BBUILD)/gpsp_cpu.o $(M14BBUILD)/gpsp_main.o \
                 $(M14BBUILD)/gpsp_gba_memory.o $(M14BBUILD)/gpsp_video.o \
                 $(M14BBUILD)/gpsp_sound.o $(M14BBUILD)/gpsp_serial.o \
                 $(M14BBUILD)/gpsp_serial_proto.o $(M14BBUILD)/gpsp_rfu.o \
                 $(M14BBUILD)/gpsp_gbp.o $(M14BBUILD)/gpsp_cheats.o \
                 $(M14BBUILD)/gpsp_savestate.o $(M14BBUILD)/gpsp_input.o

M14BOBJS = $(M14B_OWN_OBJS) $(M14B_GPSP_OBJS)

# Every capability the fixture advertises must be ON THE LINK LINE.
M14B_REQUIRED_OBJS = m14bmain.o m13store.o gba_library.o gba_rom.o \
                     gba_filestream.o gba_compat.o gba_fixture.o gba_bios.o \
                     gba_probe.o gba_exec.o gba_present.o gfx.o platform.o \
                     gba_input.o gba_m8map.o audio.o gba_audio.o gba_m10map.o \
                     gba_save.o gba_restore.o gba_restorefile.o gba_savehdr.o \
                     savedata.o gba_savefile.o

# ---- offline structural check -----------------------------------------------
#
# ***** THIS IS THE ONLY CHECK THAT RUNS ON A WORKSTATION WITH NO TOOLCHAIN,
# WHICH IS WHY THE M14B GATES LIVE IN IT. ***** --app m14b turns on the gates
# that are specific to an attribution fixture: no host-wait figure may be
# produced by subtraction, no covariance may be computed in the overflowing
# n*sum_ps form, the UNKNOWN rows must be present, and the residual clamp must
# be both preserved and witnessed.
m14bdiag-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m14bdiag/main.c
	@$(PYTHON) tools/m8_syntax_check.py apps/m13bgpsp/m13b_picker.inc
	@$(PYTHON) tools/m14a_loop_check.py --app m14b apps/m14bdiag/main.c

# ---- LUAport-owned objects, runtime side ------------------------------------
#
# ***** apps/m13bgpsp/m13b_picker.inc IS A PREREQUISITE OF m14bmain.o. ***** It
# is textually included, so it is a dependency exactly like a header.
$(M14BBUILD)/m14bmain.o: apps/m14bdiag/main.c \
                         apps/m13bgpsp/m13b_picker.inc \
                         adapters/gba/m13store.h \
                         adapters/gba/gba_library.h \
                         adapters/gba/gba_rom.h \
                         adapters/gba/gba_probe.h \
                         adapters/gba/gba_bios.h \
                         adapters/gba/gba_save.h \
                         adapters/gba/gba_savefile.h \
                         adapters/gba/gba_restore.h \
                         adapters/gba/gba_restorefile.h \
                         adapters/gba/gba_exec.h \
                         adapters/gba/gba_present.h \
                         adapters/gba/gba_input.h \
                         adapters/gba/gba_m8map.h \
                         adapters/gba/gba_m10map.h \
                         adapters/gba/gba_audio.h runtime/audio.h \
                         runtime/savedata.h \
                         $(M14BHEADERS) | $(M14BBUILD)
	$(CC) $(M14BFLAGS) -c $< -o $@

$(M14BBUILD)/shim.o: runtime/shim.c $(M14BHEADERS) | $(M14BBUILD)
	$(CC) $(M14BFLAGS) -c $< -o $@

$(M14BBUILD)/gfx.o: runtime/gfx.c $(M14BHEADERS) | $(M14BBUILD)
	$(CC) $(M14BFLAGS) -c $< -o $@

$(M14BBUILD)/platform.o: runtime/platform.c $(M14BHEADERS) | $(M14BBUILD)
	$(CC) $(M14BFLAGS) -c $< -o $@

$(M14BBUILD)/audio.o: runtime/audio.c runtime/audio.h $(M14BHEADERS) \
                      | $(M14BBUILD)
	$(CC) $(M14BFLAGS) -c $< -o $@

$(M14BBUILD)/savedata.o: runtime/savedata.c runtime/savedata.h \
                         $(M14BHEADERS) | $(M14BBUILD)
	$(CC) $(M14BFLAGS) -c $< -o $@

$(M14BBUILD)/m13store.o: adapters/gba/m13store.c \
                         adapters/gba/m13store.h runtime/core.h | $(M14BBUILD)
	$(CC) $(M14BFLAGS) -c $< -o $@

$(M14BBUILD)/gba_library.o: adapters/gba/gba_library.c \
                            adapters/gba/gba_library.h \
                            adapters/gba/m13store.h | $(M14BBUILD)
	$(CC) $(M14BFLAGS) -c $< -o $@

$(M14BBUILD)/gba_present.o: adapters/gba/gba_present.c \
                            adapters/gba/gba_present.h \
                            runtime/core.h | $(M14BBUILD)
	$(CC) $(M14BFLAGS) -c $< -o $@

# THE ONLY OBJECT IN THE IMAGE THAT MAY CALL fwrite.
$(M14BBUILD)/gba_savefile.o: adapters/gba/gba_savefile.c \
                             adapters/gba/gba_savefile.h \
                             adapters/gba/gba_save.h \
                             runtime/savedata.h | $(M14BBUILD)
	$(CC) $(M14BFLAGS) -c $< -o $@

# M12C's VALIDATOR. It opens both files "rb" and NEVER writes.
$(M14BBUILD)/gba_restorefile.o: adapters/gba/gba_restorefile.c \
                                adapters/gba/gba_restorefile.h \
                                adapters/gba/gba_restore.h \
                                adapters/gba/gba_savehdr.h \
                                adapters/gba/gba_save.h | $(M14BBUILD)
	$(CC) $(M14BFLAGS) -c $< -o $@

# ---- gpSP-facing adapters ---------------------------------------------------
#
# ***** THESE GET THE gpSP FLAGS AND NOTHING ELSE. ***** They include
# gpsp/common.h, and they MUST NOT get $(M14B_INCLUDES), whose -Iruntime would
# put runtime/core.h on the include path: gpsp/common.h:100 typedefs u64 as
# `unsigned long long` and runtime/core.h:30 as `unsigned long` -- DISTINCT
# types -- so a TU that saw both would not compile.
$(M14BBUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h \
                          | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h \
                         | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h \
                        | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gba_save.o: adapters/gba/gba_save.c adapters/gba/gba_save.h \
                         | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gba_savehdr.o: adapters/gba/gba_savehdr.c \
                            adapters/gba/gba_savehdr.h | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

# ***** M12C's RESTORE ADAPTER. THE SOLE WRITER OF gamepak_backup[]. *****
$(M14BBUILD)/gba_restore.o: adapters/gba/gba_restore.c \
                            adapters/gba/gba_restore.h | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gba_m8map.o: adapters/gba/gba_m8map.c adapters/gba/gba_m8map.h \
                          | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gba_m10map.o: adapters/gba/gba_m10map.c \
                           adapters/gba/gba_m10map.h | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h \
                         | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gba_input.o: adapters/gba/gba_input.c adapters/gba/gba_input.h \
                          | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

# NOTE THE ABSENT PREREQUISITE: runtime/audio.h is deliberately NOT listed,
# because gba_audio.c deliberately does NOT include it.
$(M14BBUILD)/gba_audio.o: adapters/gba/gba_audio.c adapters/gba/gba_audio.h \
                          | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects ------------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, IDENTICAL to M14A's.
# gpsp/** is never modified, its own Makefile is never invoked, AND NOT ONE gpSP
# OBJECT IS INSTRUMENTED -- the fixture measures from OUTSIDE the emulator.

$(M14BBUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M14BBUILD)
	$(CXX) $(M14B_GPSPCXXFLAGS) -c $< -o $@

$(M14BBUILD)/gpsp_video.o: gpsp/video.cc | $(M14BBUILD)
	$(CXX) $(M14B_GPSPCXXFLAGS) -c $< -o $@

$(M14BBUILD)/gpsp_main.o: gpsp/main.c | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gpsp_sound.o: gpsp/sound.c | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gpsp_serial.o: gpsp/serial.c | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

$(M14BBUILD)/gpsp_input.o: gpsp/input.c | $(M14BBUILD)
	$(CC) $(M14B_GPSPFLAGS) -c $< -o $@

m14bdiag-objs: $(M14BOBJS)

# ---- link -------------------------------------------------------------------

$(M14BELF): $(M14BOBJS) runtime/linker.ld | $(M14BBUILD)
	$(CC) $(BASEFLAGS) $(M14BLDFLAGS) -o $@ $(M14BOBJS)

m14bdiag-link: $(M14BOBJS) | $(M14BBUILD)
	@$(CC) $(BASEFLAGS) $(M14BLDFLAGS) -o $(M14BELF) $(M14BOBJS) \
	     > $(M14BBUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to .[^']+'" $(M14BBUILD)/link.log \
	            | sed -E "s/.*to .//; s/'$$//" | sort -u); \
	 if [ $$status -ne 0 ]; then \
	     cat $(M14BBUILD)/link.log; \
	     echo "M14B LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M14B LINK FAILED: undefined references reported despite exit 0"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "M14B LINK OK: linker exit 0, no undefined references"

$(M14BBIN): $(M14BELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M14BELF) $@ \
	   --map $(M14BBUILD)/$(M14BTARGET).map --budget $(JIT_LIMIT) --arena 4194304

m14bdiag: $(M14BBIN)

# THE TWO HARD CEILINGS, UNCHANGED AND UNRAISED. sizereport.py enforces the JIT
# budget ($(JIT_LIMIT) = 524,288) and tools/check_forbidden.py enforces the
# 2,097,152-byte .bss tripwire.
#
# ***** THE .bss PROJECTION. ***** M14B links M14A's object set and adds the
# attribution accumulators: five u64 + five unsigned grain slots, five unsigned
# grain histogram and late-by-grain slots, four u64 + four unsigned PRESENT-bin
# slots, three u64 second moments, two u64/unsigned witnesses and two extra
# struct m14a_phase entries. That is UNDER 300 BYTES on top of M14A. THE FIXTURE
# STILL HAS NO PER-FRAME HISTORY BUFFER: 864,000 frames of u32 would be 3.4 MB
# and would blow the tripwire on its own.
#
# ***** IF EITHER CEILING IS EXCEEDED, STOP AND REPORT. ***** Do not raise the
# tripwire and do not shrink unrelated code to fit. A fixture that required the
# budget to be relaxed would be changing the thing it measures.
m14bdiag-size: $(M14BELF) $(M14BBIN)
	@$(PYTHON) tools/sizereport.py $(M14BELF) $(M14BBIN) \
	   --map $(M14BBUILD)/$(M14BTARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification -----------------------------------------------------------
#
# ***** THIS VERIFIER PROVES THE FIXTURE MEASURES THE SAME PROGRAM M14A
# MEASURED, AND REPORTS WHAT IT CANNOT KNOW. *****
#
# It reuses every structural gate M14A passes and adds the gates specific to an
# ATTRIBUTION fixture: that M14A itself is untouched, that no new clock read was
# introduced, that the host-wait split is reported as UNKNOWN rather than
# guessed, and that the residual clamp is witnessed. No existing verifier is
# weakened by one character.
m14bdiag-verify: m13b-equiv $(M14BELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M14BELF)
	@echo "== relocations (NON-MASKING pipeline) =="
	@$(PYTHON) tools/verify_reloc.py $(M14BELF) $(M14BOBJS) \
	     > $(M14BBUILD)/reloc.log 2>&1; \
	 st=$$?; \
	 tail -12 $(M14BBUILD)/reloc.log; \
	 if [ $$st -ne 0 ]; then \
	     echo "M14B VERIFY FAILED: the relocation check exited $$st."; \
	     echo "  Full output: $(M14BBUILD)/reloc.log"; \
	     exit $$st; \
	 fi
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M14BOBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M14BOBJS)
	@echo "== forbidden symbols in the linked image (.bss tripwire included) =="
	@$(PYTHON) tools/check_forbidden.py $(M14BELF) $(M14BOBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M14BELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M14B VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== ***** EVERY ADVERTISED CAPABILITY IS LINKED ***** =="
	@missing=""; \
	 for want in $(M14B_REQUIRED_OBJS); do \
	     found=0; \
	     for o in $(M14BOBJS); do \
	         case "$$o" in */$$want) found=1;; esac; \
	     done; \
	     if [ $$found -eq 0 ]; then missing="$$missing $$want"; fi; \
	 done; \
	 if [ -n "$$missing" ]; then \
	     echo "M14B VERIFY FAILED: a required object is NOT on the link"; \
	     echo "  line:$$missing"; \
	     echo "  The fixture reproduces M13C's whole flow. An image missing"; \
	     echo "  one of these is not measuring that flow."; \
	     exit 1; \
	 fi; \
	 echo "all twenty-four required objects are PRESENT"
	@echo "== ***** M14A IS NOT MODIFIED BY THIS MILESTONE ***** =="
# M14A holds the baseline M14B exists to explain. If the two were not
# independent the comparison would be worthless, so the independence is CHECKED
# rather than asserted.
#
# ***** THE PATTERN IS `m14b_|m14bdiag`, NOT THE BARE WORD `M14B`, AND THAT IS
# DELIBERATE. ***** M14A's own prose already said "M14A produces numbers; M14B
# and beyond may act on them" long before this milestone existed -- at
# apps/m14diag/main.c:121 and :3804, and lua/m14diag.lua.in:34. Matching the
# bare word would fail this gate against THREE PRE-EXISTING COMMENTS and would
# have to be "fixed" by editing the very file the gate is protecting, which is
# precisely backwards. What must not appear is a CODE reference: the m14b_
# identifier prefix or the m14bdiag path.
	@if grep -qE "m14b_|m14bdiag" apps/m14diag/main.c; then \
	     echo "M14B VERIFY FAILED: apps/m14diag/main.c carries an M14B code"; \
	     echo "  reference. The baseline fixture must be BYTE-UNCHANGED by this"; \
	     echo "  milestone -- M14B is a SEPARATE fixture precisely so the"; \
	     echo "  baseline and the explanation of it stay independent."; \
	     exit 1; \
	 fi; \
	 echo "apps/m14diag/main.c carries no M14B code reference"
	@if grep -qE "m14b_|m14bdiag" lua/m14diag.lua.in; then \
	     echo "M14B VERIFY FAILED: lua/m14diag.lua.in carries an M14B code"; \
	     echo "  reference. M14A's payload template is frozen evidence and M14B"; \
	     echo "  has its own loader."; \
	     exit 1; \
	 fi; \
	 echo "lua/m14diag.lua.in carries no M14B code reference"
	@echo "== ***** M13C IS NOT MODIFIED BY THIS MILESTONE ***** =="
	@if grep -qE "m14b_|m14bdiag" apps/m13cgpsp/main.c; then \
	     echo "M14B VERIFY FAILED: apps/m13cgpsp/main.c carries an M14B code"; \
	     echo "  reference. The launcher must be BYTE-UNCHANGED."; \
	     exit 1; \
	 fi; \
	 echo "apps/m13cgpsp/main.c carries no M14B code reference"
	@bad=""; \
	 for d in gpsp adapters/gba runtime; do \
	     if grep -rqE "m14b_|m14bdiag" $$d 2>/dev/null; then \
	         bad="$$bad $$d"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M14B VERIFY FAILED: M14B names appear under:$$bad"; \
	     echo "  gpsp/**, adapters/gba/** and runtime/** are FROZEN for this"; \
	     echo "  milestone. Instrumentation lives in apps/m14bdiag/main.c only."; \
	     echo "  ***** THIS IS THE GATE THAT KEEPS THE HOST WAIT SPLIT"; \
	     echo "  HONEST. ***** Splitting HOST WAIT from NONWAIT would need a"; \
	     echo "  timestamp inside runtime/platform.c or gba_audio.c. M14B"; \
	     echo "  reports UNKNOWN instead, and this gate is why it must."; \
	     exit 1; \
	 fi; \
	 echo "gpsp/**, adapters/gba/** and runtime/** contain no M14B reference"
	@echo "== ***** gpSP IS NOT INSTRUMENTED ***** =="
	@if grep -rqE "plat_time_us|plat_time_init" gpsp 2>/dev/null; then \
	     echo "M14B VERIFY FAILED: gpsp/** reads the LUAport clock. The"; \
	     echo "  emulator core must remain uninstrumented."; \
	     exit 1; \
	 fi; \
	 echo "no gpsp/** source reads plat_time_us or plat_time_init"
	@echo "== ***** THE SRAM FORENSICS OBSERVER IS DELIBERATELY ABSENT ***** =="
	@case " $(M14BOBJS) " in \
	   *" $(M14BBUILD)/gba_sramobs.o "*) \
	      echo "M14B VERIFY FAILED: gba_sramobs.o is on the M14B link line."; \
	      echo "  It is 10,432 bytes of .bss of pure commit-side forensics and"; \
	      echo "  the fixture never calls it. M13C is its home."; \
	      exit 1;; \
	 esac; \
	 echo "gba_sramobs.o is NOT linked, and nothing references it"
	@if $(NM) -u $(M14BBUILD)/m14bmain.o 2>/dev/null \
	      | grep -qE "m12c_sramobs"; then \
	     echo "M14B VERIFY FAILED: m14bmain.o references the SRAM observer,"; \
	     echo "  whose object is not linked."; \
	     exit 1; \
	 fi; \
	 echo "m14bmain.o names no SRAM observer entry point"
	@echo "== ***** THE MEASURED PATH IS REALLY WIRED UP ***** =="
	@fail=0; \
	 for pair in "m14bmain.o:gba_lib_scan" \
	             "m14bmain.o:m4_rom_load_named" \
	             "m14bmain.o:m5_exec_run" \
	             "gba_exec.o:execute_arm" \
	             "m14bmain.o:gba_present_blit" \
	             "m14bmain.o:plat_video_present" \
	             "m14bmain.o:m8_input_apply" \
	             "m14bmain.o:gba_audio_pull" \
	             "m14bmain.o:gba_audio_resample" \
	             "m14bmain.o:gba_audio_drain" \
	             "m14bmain.o:plat_audio_init" \
	             "m14bmain.o:m10map_paging_delta" \
	             "m14bmain.o:gba_restorefile_run" \
	             "m14bmain.o:gba_savefile_commit" \
	             "m14bmain.o:plat_savedata_init" \
	             "m14bmain.o:plat_time_init" \
	             "m14bmain.o:plat_time_us"; do \
	     o=$${pair%%:*}; s=$${pair##*:}; \
	     if ! $(NM) -u $(M14BBUILD)/$$o 2>/dev/null | grep -qw "$$s"; then \
	         echo "  MISSING: $$o does not reference $$s"; \
	         fail=1; \
	     fi; \
	 done; \
	 if [ $$fail -ne 0 ]; then \
	     echo "M14B VERIFY FAILED: the measured path is not fully wired."; \
	     exit 1; \
	 fi; \
	 echo "every reference on the measured path is present"
	@echo "== ***** THE LOOP IS STILL M13C's LOOP ***** =="
	@miss=""; \
	 for s in plat_pad_read m8_input_from_pad m8_input_apply \
	          m8_input_irq_edge m5_exec_run gba_present_blit \
	          plat_video_present gba_audio_pull gba_audio_resample \
	          gba_audio_drain; do \
	     grep -q "$$s" apps/m14bdiag/main.c || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M14B VERIFY FAILED: a frame-loop step is absent from the"; \
	     echo "  fixture:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "all ten frame-loop steps are present in apps/m14bdiag/main.c"
# ***** THE ORDER, THE TIMESTAMP COUNT, THE ABSENCE OF A HUD, THE GLYPH
# COVERAGE AND EVERY M14B-SPECIFIC GATE ARE SOURCE PROPERTIES, SO THEY ARE
# CHECKED AT THE SOURCE. ***** --app m14b additionally rejects a host-wait
# figure derived by subtraction and the overflowing n*sum_ps covariance form.
	@$(PYTHON) tools/m14a_loop_check.py --app m14b apps/m14bdiag/main.c
	@echo "== ***** NOTHING WAS OPTIMISED AND NOTHING IS SKIPPED ***** =="
# ***** M14B IS DIAGNOSTIC-ONLY. ***** It exists to decide where the frame
# budget goes BEFORE anything is changed. These are the settings it is most
# tempted to "improve" while it has the numbers in front of it.
	@if ! grep -q "ROM_BUFFER_SIZE=2" Makefile; then \
	     echo "M14B VERIFY FAILED: -DROM_BUFFER_SIZE=2 is gone."; \
	     exit 1; \
	 fi; \
	 echo "ROM_BUFFER_SIZE is still 2"
	@if ! grep -q "skip_next_frame != 0u" apps/m14bdiag/main.c; then \
	     echo "M14B VERIFY FAILED: the frameskip gate is not asserted."; \
	     exit 1; \
	 fi; \
	 echo "the frameskip gate is re-asserted every frame"
	@if grep -qE "\[M14A_PLAY_MAX_ITERS\]|\[864000\]" apps/m14bdiag/main.c; then \
	     echo "M14B VERIFY FAILED: a per-frame history array exists. 864,000"; \
	     echo "  frames of u32 is 3.4 MB and would blow the .bss tripwire on"; \
	     echo "  its own. The cross-tabulations are FIXED-SIZE BINS for exactly"; \
	     echo "  this reason."; \
	     exit 1; \
	 fi; \
	 echo "no per-frame history buffer: every M14B table is a fixed-size bin"
	@if grep -qE "M14A_SESSION_SECONDS|run_max_us|M14A_END_SESSION" \
	        apps/m14bdiag/main.c; then \
	     echo "M14B VERIFY FAILED: a session-duration terminator exists in"; \
	     echo "  apps/m14bdiag/main.c. The fixture reads the clock nine times"; \
	     echo "  per frame and must COMPARE IT TO NOTHING."; \
	     exit 1; \
	 fi; \
	 echo "no session-duration terminator: the frame loop cannot time out"
	@if ! grep -q "iter < M14A_PLAY_MAX_ITERS" apps/m14bdiag/main.c; then \
	     echo "M14B VERIFY FAILED: the emergency watchdog no longer bounds"; \
	     echo "  the frame loop. It must be RETAINED as a backstop."; \
	     exit 1; \
	 fi; \
	 echo "the emergency watchdog still bounds the loop (864,000 iters = 240 min)"
	@echo "== rodata carve =="
	@$(OBJCOPY) -O binary --only-section=.text $(M14BELF) $(M14BBUILD)/text.bin
	@tx=$$($(NM) $(M14BELF) | awk '$$NF == "__text_start"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 ra=$$($(NM) $(M14BELF) | awk '$$NF == "__rodata_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 rz=$$($(NM) $(M14BELF) | awk '$$NF == "__rodata_end"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 if [ -z "$$tx" ] || [ -z "$$ra" ] || [ -z "$$rz" ]; then \
	     echo "M14B VERIFY FAILED: linker marker symbol missing."; \
	     exit 1; \
	 fi; \
	 t=$$((0x$$tx)); a=$$((0x$$ra)); z=$$((0x$$rz)); n=$$((z - a)); \
	 if [ "$$t" -ne 0 ]; then \
	     echo "M14B VERIFY FAILED: .text does not start at vaddr 0."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M14B VERIFY FAILED: empty rodata window."; \
	     exit 1; \
	 fi; \
	 have=$$(wc -c < $(M14BBUILD)/text.bin | tr -d " "); \
	 if [ "$$have" -lt "$$z" ]; then \
	     echo "M14B VERIFY FAILED: .text extraction is short."; \
	     exit 1; \
	 fi; \
	 dd if=$(M14BBUILD)/text.bin of=$(M14BBUILD)/rodata.bin bs=1 skip=$$a count=$$n \
	    2>/dev/null || { \
	     echo "M14B VERIFY FAILED: dd could not carve the rodata window."; exit 1; \
	 }; \
	 if [ ! -s $(M14BBUILD)/rodata.bin ]; then \
	     echo "M14B VERIFY FAILED: rodata extraction produced 0 bytes -- every"; \
	     echo "  check below would pass vacuously."; \
	     exit 1; \
	 fi; \
	 got=$$(wc -c < $(M14BBUILD)/rodata.bin | tr -d " "); \
	 if [ "$$got" -ne "$$n" ]; then \
	     echo "M14B VERIFY FAILED: rodata.bin is $$got bytes, expected $$n."; \
	     exit 1; \
	 fi; \
	 echo "rodata carved from .text by linker markers: [$$a,$$z) = $$n bytes"
	@echo "== ***** THE MEASUREMENT IS HONESTLY REPORTED ***** =="
# M14A's twelve reporting strings are still required: M14B inherits the whole
# result screen and may not quietly drop a column while adding its own page.
	@miss=""; \
	 for p in "RAW" "OVERHEAD" "CORRECTED" "RESIDUAL" "SAMPLED" "SKIPPED" \
	          "NATIVE GBA LATE" "HOST VSYNC LATE" "SEVERE" \
	          "WORST" "TIMER COST" "MEASURED"; do \
	     grep -aq "$$p" $(M14BBUILD)/rodata.bin || miss="$$miss [$$p]"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M14B VERIFY FAILED: a required reporting string inherited from"; \
	     echo "  M14A is absent from the image:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "every M14A reporting string survives in M14B"
	@echo "== ***** WHAT CANNOT BE KNOWN IS PRINTED AS UNKNOWN ***** =="
# ***** THIS IS THE CENTRAL HONESTY GATE OF THE MILESTONE. ***** The HOST WAIT /
# NONWAIT split needs a timestamp inside frozen code. M14B therefore prints
# UNKNOWN and NOT SPLITTABLE on those four rows. An image that had replaced
# them with a number would have GUESSED, and the guess would look exactly like
# a measurement on a photograph.
	@miss=""; \
	 for p in "UNKNOWN" "NOT SPLITTABLE" "AUDIO SUBMIT HOST WAIT" \
	          "AUDIO SUBMIT NONWAIT" "PRESENT HOST WAIT" "PRESENT NONWAIT" \
	          "PROVENANCE" "DERIVED EXACT" "PACING" "NONPACING" \
	          "RATIO PCT" "RESID CLAMP" "REQUIRE 0"; do \
	     grep -aq "$$p" $(M14BBUILD)/rodata.bin || miss="$$miss [$$p]"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M14B VERIFY FAILED: an attribution string is absent from the"; \
	     echo "  image:$$miss"; \
	     echo "  The four HOST WAIT / NONWAIT rows must read UNKNOWN, every"; \
	     echo "  figure must carry a PROVENANCE label, and the residual clamp"; \
	     echo "  witness must be on the screen under REQUIRE 0."; \
	     exit 1; \
	 fi; \
	 echo "the four unsplittable rows read UNKNOWN and carry a provenance label"
	@echo "== ***** THE RANKING IS BY MEASURED TIME, NOT BY OBJECT SIZE ***** =="
	@if ! grep -aq "NOT SORTED BY OBJECT SIZE" $(M14BBUILD)/rodata.bin; then \
	     echo "M14B VERIFY FAILED: the ranking disclaimer is absent."; \
	     exit 1; \
	 fi; \
	 echo "the result screen states that the ranking is by measured time"
	@echo "== M14B VERIFY OK =="

# ---- the Lua payload --------------------------------------------------------
#
# ***** ITS OWN TEMPLATE. lua/m14diag.lua.in IS NOT EDITED AND NOT REUSED IN
# PLACE. ***** The delivery machinery it contains is hardware-proven from M0
# through M14A and is therefore reproduced rather than re-derived; only the
# labels differ.
M14B_LUA_TEMPLATE := lua/m14bdiag.lua.in
M14B_LUA_OUT      := $(M14BBUILD)/$(M14BTARGET).lua

m14bdiag-lua: m14bdiag-check $(M14BBIN) $(M14BELF)
	@$(PYTHON) tools/mklua.py $(M14B_LUA_TEMPLATE) $(M14BBIN) \
	     $(M14B_LUA_OUT) $(M14BELF)
	@echo "M14B LUA OK: $(M14B_LUA_OUT)"

# ---- m14bdiag-send -- HARDWARE DELIVERY -------------------------------------
#
# NOT AUTHORISED BY THE EXISTENCE OF THIS TARGET.
#
#   * m14bdiag-verify IS A PREREQUISITE, and it in turn depends on m13b-equiv,
#     so the 283-check picker harness runs before anything is transmitted.
#   * ***** M14B IS OFFLINE-GREEN ONLY. ***** It has had NO hardware run.
#   * ***** PHOTOGRAPH BOTH SCREENS. ***** M14B draws the inherited result
#     screen AND the attribution page after it. The second one is the product
#     of this milestone and CIRCLE advances from the first to it.
#   * ***** READ R3 RESID CLAMP N FIRST. ***** It must be 0. A non-zero value
#     means the phase decomposition is not additive and NO OTHER FIGURE ON THE
#     PAGE MAY BE BELIEVED.
#   * ***** THE FOUR UNKNOWN ROWS ARE THE EXPECTED RESULT, NOT A FAILURE. *****
#   * CAPTURE THE UDP LOG ON 9027.
m14bdiag-send: m14bdiag-verify m14bdiag-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m14bdiag-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M14B_LUA_OUT) $(M14BBIN)

m14bdiag-clean:
	rm -rf $(M14BBUILD)

.PHONY: m14bdiag m14bdiag-objs m14bdiag-check m14bdiag-link m14bdiag-size \
        m14bdiag-verify m14bdiag-lua m14bdiag-send m14bdiag-clean

# =============================================================================
# M16-0 -- THE RELAUNCH DIAGNOSTIC
# =============================================================================
#
#   make m16diag-check      offline structural check, no compiler needed
#   make m16diag-objs
#   make m16diag-link
#   make m16diag
#   make m16diag-size
#   make m16diag-verify
#   make m16diag-lua
#   make m16diag-send PS5_IP=<addr>
#   make m16diag-clean
#
# ***** AN EIGHTH TARGET FAMILY, AND EVERY EARLIER ONE IS UNTOUCHED. *****
# build/m13c/ IS FROZEN REGRESSION EVIDENCE: 308,784 bytes, SHA-256
# e9925161d48f65660bbc89cdd9bd9fb6fb98f6b48431a80b44c500395d6d824a. Neither
# $(M13COBJS) nor any M13C* variable nor apps/m13cgpsp/** is touched here.
#
# ***** THE QUESTION THIS IMAGE ANSWERS. ***** Can ONE payload lifetime run
# ROM A -> session reset -> ROM A -> session reset -> ROM A, with every
# once-per-process latch returning to its pre-session value each time? M13C
# cannot answer it: M13C exits after one cartridge, so every latch it sets is
# set exactly once and never has to come back.
#
# ***** THE SAME ROM THREE TIMES, DELIBERATELY. ***** Cartridge SWITCHING adds
# a second variable. This rung isolates the FIRST one -- does the reset work at
# all -- so a failure cannot be blamed on the new cartridge.
#
# =============================================================================
# HOW THE M13C FREEZE IS PROTECTED, MECHANICALLY
# =============================================================================
#
# M16-0 adds code to FIVE SHARED translation units -- runtime/shim.c,
# adapters/gba/gba_save.c, adapters/gba/gba_restore.c,
# adapters/gba/gba_filestream.c -- plus one wholly new pair,
# adapters/gba/gba_session.c|h.
#
# ***** EVERY ADDITION TO A SHARED FILE IS INSIDE #ifdef LUAPORT_SESSION_REUSE,
# AND THAT MACRO IS DEFINED ON M16 TARGETS AND NOWHERE ELSE. ***** M13C
# preprocesses all four shared files with the macro UNDEFINED, so the
# preprocessed token stream M13C compiles is byte-identical to what it was
# before M16-0 existed, and $(M13CBIN) must still hash to the frozen value.
# m16diag-verify CHECKS that the macro never reaches an M13C flag variable
# rather than trusting it.
#
# ***** THE DEFINE IS NOT GIVEN TO gpsp/**. ***** No file under gpsp/ mentions
# LUAPORT_SESSION_REUSE, so passing it there would change nothing while making
# the gpSP core objects look milestone-specific. $(M16_COREFLAGS) therefore
# omits it and only the ADAPTERS get $(M16_GPSPFLAGS).

M16TARGET := m16diag
M16BUILD  := build/m16diag
M16ELF    := $(M16BUILD)/$(M16TARGET).elf
M16BIN    := $(M16BUILD)/$(M16TARGET).bin

$(M16BUILD):
	mkdir -p $(M16BUILD)

# ---- flags ------------------------------------------------------------------
#
# ***** THE ONE DEFINE THAT SEPARATES M16 FROM EVERY FROZEN MILESTONE. *****
# It appears in exactly two flag variables below and must never appear in an
# M13C*, M14*, M12* or M1* one.
M16_SESSION_DEFINE = -DLUAPORT_SESSION_REUSE

# ***** NO -Iapps/m13bgpsp. ***** M16-0 HAS NO PICKER. The three sessions load
# the SAME ROM from a fixed path so that no operator choice can make two
# sessions differ, and m13b_picker.inc is therefore not included, not compiled
# and not on the dependency line.
M16_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m16diag \
               -Iadapters/gba

M16FLAGS = $(BASEFLAGS) $(M16_INCLUDES) -Wall -Wno-unused-function \
           $(M16_SESSION_DEFINE)

# ***** -DROM_BUFFER_SIZE=2 IS NOT ALTERED. ***** Two 1 MB buffers is what makes
# a cartridge over 2 MB demand paged, and the demand-paging file handle is the
# ONE RFILE pool slot per launch that this diagnostic exists to prove is
# released. Raising it would delete the very leak being measured.
M16_GPSPFLAGS = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2 $(M16_SESSION_DEFINE)

# gpsp/** gets the SAME flags M13C's core objects get, WITHOUT the session
# define. See the note at the top of this section.
M16_COREFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M16_CORECXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M16HEADERS = $(HEADERS)

M16LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
             -Wl,--build-id=none -Wl,-z,norelro \
             -Wl,--orphan-handling=error \
             -Wl,-Map=$(M16BUILD)/$(M16TARGET).map

# ----------------------------------------------------------------- objects ---
#
# ***** M13C's SET, MINUS THE PICKER AND MINUS EVERY WRITE PATH, PLUS ONE NEW
# ADAPTER. ***** Each absence below is a STRUCTURAL GATE, not an oversight.
#
# gba_session.o IS THE ONLY NEW OBJECT IN M16-0. It orders existing, proven
# resets; see adapters/gba/gba_session.c for why it adds no new mechanism.
#
# ***** THE FOUR SAVE-WRITER OBJECTS ARE ABSENT, AND THAT IS THE STRONGEST
# CLAIM THIS MAKEFILE MAKES. *****
#   savedata.o          the runtime savedata layer
#   gba_savefile.o      the ONLY writer of /savedata0/gba/<ID>.sav
#   gba_savehdr.o       the .hdr format
#   gba_restorefile.o   the save-file reader
# A diagnostic that runs the SAME cartridge three times and is still learning
# whether gba_save_unlatch() works MUST NOT be able to write a save file: if
# the unlatch were broken, session 2 would commit into session 1's identity.
# Omitting the objects makes that physically impossible rather than merely
# forbidden -- the write path is not in the image to be called. Verified
# link-clean: gba_save.c names gba_savefile_commit only in a COMMENT (:423) and
# gba_restore.c includes only common.h and gba_restore.h.
#
# ***** gba_m8map.o IS ABSENT: M16-0 MAKES NO CONTROLLER-MAPPING CHANGE. *****
# The bounded run calls m8_input_apply(0u) -- neutral, every frame -- so that
# three sessions execute identical instructions and a difference between them
# can only come from the teardown. No pad-to-GBA mapping is performed, so the
# mapping object is not linked.
#
# STILL ABSENT, FOR THEIR ORIGINAL REASONS:
#   gba_m11save.o       M11's 128-block dirty map that nothing here reads
#   cpu_threaded.o      every dynarec emitter. HAVE_DYNAREC stays undefined and
#                       dynarec_enable stays UNDEFINED so a leak is a LINK ERROR
#   libretro*.o         and the libretro VFS
M16_OWN_OBJS = $(M16BUILD)/m16main.o \
               $(M16BUILD)/shim.o \
               $(M16BUILD)/gfx.o \
               $(M16BUILD)/platform.o \
               $(M16BUILD)/audio.o \
               $(M16BUILD)/m13store.o \
               $(M16BUILD)/gba_library.o \
               $(M16BUILD)/gba_fixture.o \
               $(M16BUILD)/gba_filestream.o \
               $(M16BUILD)/gba_compat.o \
               $(M16BUILD)/gba_probe.o \
               $(M16BUILD)/gba_bios.o \
               $(M16BUILD)/gba_rom.o \
               $(M16BUILD)/gba_save.o \
               $(M16BUILD)/gba_restore.o \
               $(M16BUILD)/gba_sramobs.o \
               $(M16BUILD)/gba_m10map.o \
               $(M16BUILD)/gba_exec.o \
               $(M16BUILD)/gba_present.o \
               $(M16BUILD)/gba_input.o \
               $(M16BUILD)/gba_audio.o \
               $(M16BUILD)/gba_session.o

M16_GPSP_OBJS = $(M16BUILD)/gpsp_cpu.o $(M16BUILD)/gpsp_main.o \
                $(M16BUILD)/gpsp_gba_memory.o $(M16BUILD)/gpsp_video.o \
                $(M16BUILD)/gpsp_sound.o $(M16BUILD)/gpsp_serial.o \
                $(M16BUILD)/gpsp_serial_proto.o $(M16BUILD)/gpsp_rfu.o \
                $(M16BUILD)/gpsp_gbp.o $(M16BUILD)/gpsp_cheats.o \
                $(M16BUILD)/gpsp_savestate.o $(M16BUILD)/gpsp_input.o

M16OBJS = $(M16_OWN_OBJS) $(M16_GPSP_OBJS)

# Every capability the diagnostic advertises must be ON THE LINK LINE.
M16_REQUIRED_OBJS = m16main.o gba_session.o shim.o m13store.o gba_library.o \
                    gba_rom.o gba_filestream.o gba_compat.o gba_fixture.o \
                    gba_bios.o gba_probe.o gba_exec.o gba_present.o gfx.o \
                    platform.o gba_input.o audio.o gba_audio.o gba_m10map.o \
                    gba_save.o gba_restore.o gba_sramobs.o

# ***** THE OBJECTS THAT MUST NOT BE ON THE LINK LINE. ***** See the object
# commentary above: these four are the entire save-write path, and gba_m8map.o
# is the controller mapping M16-0 does not perform.
M16_FORBIDDEN_OBJS = savedata.o gba_savefile.o gba_savehdr.o \
                     gba_restorefile.o gba_m8map.o

# ---- offline structural check -----------------------------------------------
#
# Runs on a workstation with NO toolchain at all. m13b_picker.inc is NOT checked
# here because M16-0 does not include it.
m16diag-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m16diag/main.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_session.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_save.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_restore.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_filestream.c
	@$(PYTHON) tools/m8_syntax_check.py runtime/shim.c

# ---- LUAport-owned objects, runtime side ------------------------------------
#
# ***** m16main.o IS COMPILED WITH $(M16FLAGS), WHICH HAS NO -Igpsp. ***** The
# app names exactly one gpSP symbol -- skip_next_frame -- and hand-declares it
# at apps/m16diag/main.c:93 as `extern unsigned int`, sourced from
# adapters/gba/gba_fixture.c:70. Nothing else crosses the type boundary.
$(M16BUILD)/m16main.o: apps/m16diag/main.c \
                       adapters/gba/m13store.h \
                       adapters/gba/gba_library.h \
                       adapters/gba/gba_rom.h \
                       adapters/gba/gba_probe.h \
                       adapters/gba/gba_bios.h \
                       adapters/gba/gba_save.h \
                       adapters/gba/gba_restore.h \
                       adapters/gba/gba_exec.h \
                       adapters/gba/gba_present.h \
                       adapters/gba/gba_input.h \
                       adapters/gba/gba_m10map.h \
                       adapters/gba/gba_sramobs.h \
                       adapters/gba/gba_session.h \
                       adapters/gba/gba_audio.h runtime/audio.h \
                       $(M16HEADERS) | $(M16BUILD)
	$(CC) $(M16FLAGS) -c $< -o $@

# ***** shim.o CARRIES arena_mark() AND arena_rewind() AND IS THEREFORE THE ONE
# RUNTIME OBJECT THAT DIFFERS FROM M13C's. ***** Both live inside
# #ifdef LUAPORT_SESSION_REUSE (runtime/shim.c:41-74), so M13C's shim.o is
# unchanged. runtime/shim.h is an explicit prerequisite because the guarded
# declarations live there too.
$(M16BUILD)/shim.o: runtime/shim.c runtime/shim.h $(M16HEADERS) | $(M16BUILD)
	$(CC) $(M16FLAGS) -c $< -o $@

$(M16BUILD)/gfx.o: runtime/gfx.c $(M16HEADERS) | $(M16BUILD)
	$(CC) $(M16FLAGS) -c $< -o $@

$(M16BUILD)/platform.o: runtime/platform.c $(M16HEADERS) | $(M16BUILD)
	$(CC) $(M16FLAGS) -c $< -o $@

$(M16BUILD)/audio.o: runtime/audio.c runtime/audio.h $(M16HEADERS) \
                     | $(M16BUILD)
	$(CC) $(M16FLAGS) -c $< -o $@

$(M16BUILD)/m13store.o: adapters/gba/m13store.c \
                        adapters/gba/m13store.h runtime/core.h | $(M16BUILD)
	$(CC) $(M16FLAGS) -c $< -o $@

$(M16BUILD)/gba_library.o: adapters/gba/gba_library.c \
                           adapters/gba/gba_library.h \
                           adapters/gba/m13store.h | $(M16BUILD)
	$(CC) $(M16FLAGS) -c $< -o $@

$(M16BUILD)/gba_present.o: adapters/gba/gba_present.c \
                           adapters/gba/gba_present.h \
                           runtime/core.h | $(M16BUILD)
	$(CC) $(M16FLAGS) -c $< -o $@

# ---- gpSP-facing adapters ---------------------------------------------------
#
# These see gpSP's type universe through -Igpsp and MUST NOT get $(M16FLAGS).
# They DO get $(M16_SESSION_DEFINE): four of them carry guarded M16 additions
# and the rest are unaffected because they never name the macro.
$(M16BUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M16BUILD)
	$(CC) $(M16_GPSPFLAGS) -c $< -o $@

# GUARDED ADDITION: gba_rfile_inuse() and gba_rfile_slots(), the
# read-only pool census at :230-272. Neither opens, closes, seeks, reads or
# writes anything.
$(M16BUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M16BUILD)
	$(CC) $(M16_GPSPFLAGS) -c $< -o $@

$(M16BUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M16BUILD)
	$(CC) $(M16_GPSPFLAGS) -c $< -o $@

$(M16BUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h \
                         | $(M16BUILD)
	$(CC) $(M16_GPSPFLAGS) -c $< -o $@

$(M16BUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h \
                        | $(M16BUILD)
	$(CC) $(M16_GPSPFLAGS) -c $< -o $@

$(M16BUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h \
                       | $(M16BUILD)
	$(CC) $(M16_GPSPFLAGS) -c $< -o $@

# GUARDED ADDITION: gba_save_unlatch() at :397-467. It clears gs_latched, gs_id,
# gs_rom_hash and the gs_lat_* fields and does NOTHING else -- no change to the
# save format, to compatibility, to commit policy or to dirty detection.
$(M16BUILD)/gba_save.o: adapters/gba/gba_save.c adapters/gba/gba_save.h \
                        | $(M16BUILD)
	$(CC) $(M16_GPSPFLAGS) -c $< -o $@

# GUARDED ADDITION: gba_restore_disarm() at :177-221. It clears gr_armed,
# gr_applied, gr_rollbacks and the gr_base_*/gr_exit_* measurements. No
# validation policy changes.
$(M16BUILD)/gba_restore.o: adapters/gba/gba_restore.c \
                           adapters/gba/gba_restore.h | $(M16BUILD)
	$(CC) $(M16_GPSPFLAGS) -c $< -o $@

$(M16BUILD)/gba_sramobs.o: adapters/gba/gba_sramobs.c \
                           adapters/gba/gba_sramobs.h | $(M16BUILD)
	$(CC) $(M16_GPSPFLAGS) -c $< -o $@

$(M16BUILD)/gba_m10map.o: adapters/gba/gba_m10map.c \
                          adapters/gba/gba_m10map.h | $(M16BUILD)
	$(CC) $(M16_GPSPFLAGS) -c $< -o $@

$(M16BUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h \
                        | $(M16BUILD)
	$(CC) $(M16_GPSPFLAGS) -c $< -o $@

$(M16BUILD)/gba_input.o: adapters/gba/gba_input.c adapters/gba/gba_input.h \
                         | $(M16BUILD)
	$(CC) $(M16_GPSPFLAGS) -c $< -o $@

# runtime/audio.h is deliberately NOT a prerequisite: gba_audio.c does not
# include it.
$(M16BUILD)/gba_audio.o: adapters/gba/gba_audio.c adapters/gba/gba_audio.h \
                         | $(M16BUILD)
	$(CC) $(M16_GPSPFLAGS) -c $< -o $@

# ***** THE ONE NEW ADAPTER IN M16-0. ***** gba_session.h refuses to compile
# without $(M16_SESSION_DEFINE) -- it has an #error at :22-23 -- so this object
# cannot be built into a frozen milestone even by accident.
$(M16BUILD)/gba_session.o: adapters/gba/gba_session.c \
                           adapters/gba/gba_session.h \
                           adapters/gba/gba_save.h \
                           adapters/gba/gba_restore.h \
                           adapters/gba/gba_sramobs.h \
                           adapters/gba/gba_input.h \
                           adapters/gba/gba_audio.h \
                           adapters/gba/gba_m10map.h \
                           adapters/gba/gba_rom.h \
                           adapters/gba/gba_probe.h \
                           adapters/gba/gba_bios.h | $(M16BUILD)
	$(CC) $(M16_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects ------------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags. gpsp/** is never
# modified, its own Makefile is never invoked, and it does NOT receive
# $(M16_SESSION_DEFINE).
#
# ***** gpsp_gba_memory.o CARRIES memory_term(), THE PRIMITIVE THIS WHOLE
# MILESTONE TURNS ON. ***** It is defined at gpsp/gba_memory.c:2456, it is
# already linked into the FROZEN M13C image (build/m13c/m13cgpsp.map:531), and
# it has simply never been called. M16-0 calls it. NOT ONE LINE OF gpsp/ IS
# EDITED TO MAKE THAT HAPPEN.

$(M16BUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M16BUILD)
	$(CXX) $(M16_CORECXXFLAGS) -c $< -o $@

$(M16BUILD)/gpsp_video.o: gpsp/video.cc | $(M16BUILD)
	$(CXX) $(M16_CORECXXFLAGS) -c $< -o $@

$(M16BUILD)/gpsp_main.o: gpsp/main.c | $(M16BUILD)
	$(CC) $(M16_COREFLAGS) -c $< -o $@

$(M16BUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M16BUILD)
	$(CC) $(M16_COREFLAGS) -c $< -o $@

$(M16BUILD)/gpsp_sound.o: gpsp/sound.c | $(M16BUILD)
	$(CC) $(M16_COREFLAGS) -c $< -o $@

$(M16BUILD)/gpsp_serial.o: gpsp/serial.c | $(M16BUILD)
	$(CC) $(M16_COREFLAGS) -c $< -o $@

$(M16BUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M16BUILD)
	$(CC) $(M16_COREFLAGS) -c $< -o $@

$(M16BUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M16BUILD)
	$(CC) $(M16_COREFLAGS) -c $< -o $@

$(M16BUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M16BUILD)
	$(CC) $(M16_COREFLAGS) -c $< -o $@

$(M16BUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M16BUILD)
	$(CC) $(M16_COREFLAGS) -c $< -o $@

$(M16BUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M16BUILD)
	$(CC) $(M16_COREFLAGS) -c $< -o $@

$(M16BUILD)/gpsp_input.o: gpsp/input.c | $(M16BUILD)
	$(CC) $(M16_COREFLAGS) -c $< -o $@

m16diag-objs: $(M16OBJS)

# ---- link -------------------------------------------------------------------

$(M16ELF): $(M16OBJS) runtime/linker.ld | $(M16BUILD)
	$(CC) $(BASEFLAGS) $(M16LDFLAGS) -o $@ $(M16OBJS)

m16diag-link: $(M16OBJS) | $(M16BUILD)
	@$(CC) $(BASEFLAGS) $(M16LDFLAGS) -o $(M16ELF) $(M16OBJS) \
	     > $(M16BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to .[^']+'" $(M16BUILD)/link.log \
	            | sed -E "s/.*to .//; s/'$$//" | sort -u); \
	 if [ $$status -ne 0 ]; then \
	     cat $(M16BUILD)/link.log; \
	     echo "M16 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M16 LINK FAILED: undefined references reported despite exit 0"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "M16 LINK OK: linker exit 0, no undefined references"

$(M16BIN): $(M16ELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M16ELF) $@ \
	   --map $(M16BUILD)/$(M16TARGET).map --budget $(JIT_LIMIT) --arena 4194304

m16diag: $(M16BIN)

# ***** THE BUDGETS ARE NOT RAISED BY THIS MILESTONE. ***** $(JIT_LIMIT) is
# still 524,288, the .bss tripwire is still 2,097,152 and the arena is still
# 4,194,304. M16-0's whole thesis is that a second session must fit in the
# SAME arena the first one used, so growing the arena would delete the finding.
m16diag-size: $(M16ELF) $(M16BIN)
	@$(PYTHON) tools/sizereport.py $(M16ELF) $(M16BIN) \
	   --map $(M16BUILD)/$(M16TARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification -----------------------------------------------------------

m16diag-verify: $(M16ELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M16ELF)
	@echo "== relocations (NON-MASKING pipeline) =="
	@$(PYTHON) tools/verify_reloc.py $(M16ELF) $(M16OBJS) \
	     > $(M16BUILD)/reloc.log 2>&1; \
	 st=$$?; \
	 tail -12 $(M16BUILD)/reloc.log; \
	 if [ $$st -ne 0 ]; then \
	     echo "M16 VERIFY FAILED: the relocation check exited $$st."; \
	     echo "  Full output: $(M16BUILD)/reloc.log"; \
	     exit $$st; \
	 fi
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M16OBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M16OBJS)
	@echo "== forbidden symbols in the linked image (.bss tripwire included) =="
	@$(PYTHON) tools/check_forbidden.py $(M16ELF) $(M16OBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M16ELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M16 VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== ***** EVERY ADVERTISED CAPABILITY IS LINKED ***** =="
	@missing=""; \
	 for want in $(M16_REQUIRED_OBJS); do \
	     found=0; \
	     for o in $(M16OBJS); do \
	         case "$$o" in */$$want) found=1;; esac; \
	     done; \
	     if [ $$found -eq 0 ]; then missing="$$missing $$want"; fi; \
	 done; \
	 if [ -n "$$missing" ]; then \
	     echo "M16 VERIFY FAILED: a required object is NOT on the link"; \
	     echo "  line:$$missing"; \
	     exit 1; \
	 fi; \
	 echo "all twenty-two required objects are PRESENT"
	@echo "== ***** NO SAVE CAN BE WRITTEN: THE WRITE PATH IS NOT LINKED ***** =="
# A diagnostic that is still LEARNING whether gba_save_unlatch() works must not
# be able to commit. If the unlatch were broken, session 2 would write into
# session 1's identity and destroy a real save. The write path is therefore
# ABSENT FROM THE IMAGE rather than merely unused.
	@present=""; \
	 for bad in $(M16_FORBIDDEN_OBJS); do \
	     for o in $(M16OBJS); do \
	         case "$$o" in */$$bad) present="$$present $$bad";; esac; \
	     done; \
	 done; \
	 if [ -n "$$present" ]; then \
	     echo "M16 VERIFY FAILED: a forbidden object is on the link line:"; \
	     echo " $$present"; \
	     echo "  The four save-writer objects and the controller mapping are"; \
	     echo "  EXCLUDED BY DESIGN. M16-0 commits nothing and remaps nothing."; \
	     exit 1; \
	 fi; \
	 echo "no save-writer object and no controller mapping is linked"
	@sym=$$($(NM) $(M16ELF) 2>/dev/null \
	          | grep -cE " T (gba_savefile_commit|gba_savehdr_parse|plat_savedata_init)$$"); \
	 if [ "$$sym" != "0" ]; then \
	     echo "M16 VERIFY FAILED: a save-write entry point is DEFINED in the"; \
	     echo "  image even though its object was excluded."; \
	     exit 1; \
	 fi; \
	 echo "no save-write entry point is defined anywhere in the image"
	@echo "== ***** memory_term IS LINKED AND REACHED ***** =="
# The primitive the whole milestone turns on. It has been in every image since
# M1 and has never been called; if it were absent or unreferenced here, the
# teardown would be doing nothing and every session would still "pass".
	@if ! $(NM) $(M16ELF) 2>/dev/null | grep -qE " [Tt] memory_term$$"; then \
	     echo "M16 VERIFY FAILED: memory_term is not defined in the image."; \
	     exit 1; \
	 fi; \
	 echo "memory_term is DEFINED in the image"
	@if ! $(NM) $(M16BUILD)/gba_session.o 2>/dev/null \
	        | grep -qE " U memory_term$$"; then \
	     echo "M16 VERIFY FAILED: gba_session.o does not REFERENCE"; \
	     echo "  memory_term. The teardown would close no file and free no"; \
	     echo "  buffer, and all three sessions would pass vacuously."; \
	     exit 1; \
	 fi; \
	 echo "gba_session.o REFERENCES memory_term (the ROM file is really closed)"
	@echo "== ***** THE NEW RESET PRIMITIVES EXIST ***** =="
	@miss=""; \
	 for s in arena_mark arena_rewind gba_save_unlatch gba_restore_disarm \
	          gba_rfile_inuse gba_session_end gba_session_probe_relaunch; do \
	     $(NM) $(M16ELF) 2>/dev/null | grep -qE " [Tt] $$s$$" \
	         || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M16 VERIFY FAILED: a reset primitive is missing:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "all seven M16 reset primitives are defined"
	@echo "== ***** THE FROZEN M13C BASELINE IS NOT TOUCHED ***** =="
# ***** THIS IS THE CENTRAL ISOLATION GATE OF THE MILESTONE. *****
# M16-0 adds code to four SHARED translation units. The freeze survives only
# because every addition sits behind #ifdef LUAPORT_SESSION_REUSE and that
# macro never reaches an M13C compile. Both halves are CHECKED, not trusted.
	@if $(MAKE) --no-print-directory -pn m13c 2>/dev/null \
	     | grep -E "^M13C[A-Z_]* *[:+]?=" \
	     | grep -q "LUAPORT_SESSION_REUSE"; then \
	     echo "M16 VERIFY FAILED: LUAPORT_SESSION_REUSE reaches an M13C flag"; \
	     echo "  variable. M13C would preprocess the shared files differently"; \
	     echo "  and build/m13c/m13cgpsp.bin would no longer reproduce"; \
	     echo "  e9925161d48f65660bbc89cdd9bd9fb6fb98f6b48431a80b44c500395d6d824a."; \
	     echo "  STOP -- do not update the frozen hash."; \
	     exit 1; \
	 fi; \
	 echo "no M13C flag variable carries LUAPORT_SESSION_REUSE"
	@if grep -qE "m16_|m16diag|LUAPORT_SESSION_REUSE" apps/m13cgpsp/main.c; then \
	     echo "M16 VERIFY FAILED: apps/m13cgpsp/main.c carries an M16 code"; \
	     echo "  reference. The launcher is FROZEN EVIDENCE and M16-0 is a"; \
	     echo "  SEPARATE payload precisely so the two stay independent."; \
	     exit 1; \
	 fi; \
	 echo "apps/m13cgpsp/main.c carries no M16 code reference"
	@if grep -qE "m16_|m16diag" lua/m13c.lua.in; then \
	     echo "M16 VERIFY FAILED: lua/m13c.lua.in carries an M16 reference."; \
	     exit 1; \
	 fi; \
	 echo "lua/m13c.lua.in carries no M16 reference"
# ***** EVERY M16 ADDITION TO A SHARED FILE IS GUARDED. ***** Counted
# mechanically: each of the four shared units must contain a balanced
# #ifdef LUAPORT_SESSION_REUSE / #endif pair. A new unguarded function would
# change M13C's preprocessed output and break the freeze silently.
	@for f in runtime/shim.c adapters/gba/gba_save.c \
	          adapters/gba/gba_restore.c adapters/gba/gba_filestream.c; do \
	     n=$$(grep -c "^#ifdef LUAPORT_SESSION_REUSE" $$f); \
	     if [ "$$n" -lt 1 ]; then \
	         echo "M16 VERIFY FAILED: $$f has no LUAPORT_SESSION_REUSE guard."; \
	         echo "  Either the M16 addition was removed, or it was added"; \
	         echo "  UNGUARDED and the M13C freeze is already broken."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all four shared translation units carry a session-reuse guard"
	@echo "== ***** THE BUDGETS ARE NOT RAISED ***** =="
	@if ! grep -q "ROM_BUFFER_SIZE=2" Makefile; then \
	     echo "M16 VERIFY FAILED: ROM_BUFFER_SIZE is no longer 2."; \
	     exit 1; \
	 fi; \
	 echo "ROM_BUFFER_SIZE is still 2"
	@if grep -qE "M16_SESSIONS +[4-9]" apps/m16diag/main.c; then \
	     echo "M16 VERIFY FAILED: the session count was raised. THREE is the"; \
	     echo "  claim -- A, reset, A, reset, A -- and the arena headroom is"; \
	     echo "  sized for it."; \
	     exit 1; \
	 fi; \
	 echo "the diagnostic still runs exactly three sessions"
	@echo "== ***** THE DIAGNOSTIC CANNOT SILENTLY PASS ***** =="
# The failure statuses that ARE the product of this image. If a future edit
# deleted one, a broken teardown would report success.
	@miss=""; \
	 for s in M16_TEARDOWN_DIRTY M16_ARENA_LEAK M16_ARENA_REWIND_BAD \
	          M16_IDENTITY_STALE M16_PRE_UNEXPECTED; do \
	     grep -q "$$s" apps/m16diag/main.c || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M16 VERIFY FAILED: a failure status was removed:$$miss"; \
	     echo "  Each one names a blocker from the architecture study. An"; \
	     echo "  image that cannot report them cannot fail honestly."; \
	     exit 1; \
	 fi; \
	 echo "all five M16-specific failure statuses are present"
	@if ! grep -q "gba_session_probe_relaunch" apps/m16diag/main.c; then \
	     echo "M16 VERIFY FAILED: the relaunch invariant probe is not called."; \
	     exit 1; \
	 fi; \
	 echo "the relaunch invariant probe is called at every session boundary"
	@echo "== M16 VERIFY OK =="

# ---- the Lua payload --------------------------------------------------------
#
# ***** ITS OWN TEMPLATE. lua/m13c.lua.in IS NOT EDITED AND NOT REUSED IN
# PLACE. ***** The delivery machinery is hardware-proven from M0 through M14B
# and is reproduced rather than re-derived; only the labels and the decode
# differ.
M16_LUA_TEMPLATE := lua/m16diag.lua.in
M16_LUA_OUT      := $(M16BUILD)/$(M16TARGET).lua

m16diag-lua: m16diag-check $(M16BIN) $(M16ELF)
	@$(PYTHON) tools/mklua.py $(M16_LUA_TEMPLATE) $(M16BIN) \
	     $(M16_LUA_OUT) $(M16ELF)
	@echo "M16 LUA OK: $(M16_LUA_OUT)"

# ---- m16diag-send -- HARDWARE DELIVERY --------------------------------------
#
# NOT AUTHORISED BY THE EXISTENCE OF THIS TARGET.
#
#   * ***** M16-0 HAS HAD NO HARDWARE RUN. ***** Every claim it makes is a
#     HYPOTHESIS until a PS5 says otherwise.
#   * ***** IT WRITES NO SAVE AND CANNOT. ***** The write path is not linked;
#     m16diag-verify proves it.
#   * ***** PUT A SMALL ROM IN THE LIBRARY. ***** All three sessions load the
#     SAME cartridge. A demand-paged one (over 2 MB) exercises the RFILE handle
#     that memory_term() must close, which is the more interesting case.
#   * ***** READ THE RELAUNCH MASK FIRST. ***** A non-zero mask on session 2 or
#     3 means a latch did not reset, and NO OTHER FIGURE MATTERS until it does.
#   * ***** A -518 IDENTITY STALE IS THE DATA-LOSS FINDING. ***** It means
#     gba_save_unlatch() did not clear the save identity and a real launcher
#     would have committed cartridge B's memory into cartridge A's file.
#   * CAPTURE THE UDP LOG ON 9027 -- the per-session table is only there in full.
m16diag-send: m16diag-verify m16diag-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m16diag-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M16_LUA_OUT) $(M16BIN)

m16diag-clean:
	rm -rf $(M16BUILD)

.PHONY: m16diag m16diag-objs m16diag-check m16diag-link m16diag-size \
        m16diag-verify m16diag-lua m16diag-send m16diag-clean

# =============================================================================
# M16-1 -- THE A -> B -> A CARTRIDGE ISOLATION DIAGNOSTIC
# =============================================================================
#
#   make m16bdiag-check      offline structural check, no compiler needed
#   make m16bdiag-objs
#   make m16bdiag-link
#   make m16bdiag
#   make m16bdiag-size
#   make m16bdiag-verify
#   make m16bdiag-lua
#   make m16bdiag-send PS5_IP=<addr>
#   make m16bdiag-clean
#
# ***** A NINTH TARGET FAMILY, AND EVERY EARLIER ONE IS UNTOUCHED. *****
#
# build/m13c/ IS FROZEN REGRESSION EVIDENCE: 308,784 bytes, SHA-256
# e9925161d48f65660bbc89cdd9bd9fb6fb98f6b48431a80b44c500395d6d824a. Neither
# $(M13COBJS) nor any M13C* variable nor apps/m13cgpsp/** is touched here.
#
# ***** THE M16-0 FAMILY IS ALSO UNTOUCHED, AND FOR THE SAME REASON. *****
# M16-0 has a HARDWARE PASS on a real PS5: three sessions, 600 frames each,
# arena watermark back to the same value every time, buffer count 0, RFILE
# count 0, save latch CLEAR after every teardown, on screen. $(M16TARGET),
# $(M16OBJS), $(M16BUILD), apps/m16diag/** and lua/m16diag.lua.in are therefore
# EVIDENCE, and evidence is not edited to grow a feature. M16-1 is a SEPARATE
# app, a SEPARATE object set and a SEPARATE payload -- exactly the relationship
# m14bdiag already has with m14diag.
#
# ***** THE QUESTION THIS IMAGE ANSWERS. ***** M16-0 proved the session reset
# works with ONE cartridge. It could not prove cartridge SWITCHING, and said so.
# This image runs library entry 0, then library entry 1, then library entry 0
# again inside ONE payload lifetime and asks whether cartridge identity, save
# identity, paging shape, the demand-paging file handle and every session
# resource follow the cartridge instead of leaking across the boundary.
#
# ***** NO NEW MECHANISM. ***** adapters/gba/gba_session.c|h are used EXACTLY as
# M16-0 left them. gpsp/** is untouched. runtime/shim.c, gba_save.c,
# gba_restore.c and gba_filestream.c are untouched by this rung -- their M16
# additions are already there, already guarded, and already hardware-proven.
# apps/m16bdiag/main.c and lua/m16bdiag.lua.in are the only new sources.

M16BTARGET := m16bdiag
M16BBUILD  := build/m16bdiag
M16BELF    := $(M16BBUILD)/$(M16BTARGET).elf
M16BBIN    := $(M16BBUILD)/$(M16BTARGET).bin

$(M16BBUILD):
	mkdir -p $(M16BBUILD)

# ---- flags ------------------------------------------------------------------
#
# ***** ITS OWN COPY OF THE ONE DEFINE THAT SEPARATES M16 FROM EVERY FROZEN
# MILESTONE. ***** Declared separately from $(M16_SESSION_DEFINE) so that the
# two M16 families have no shared variable at all and neither can be changed by
# editing the other. It must never appear in an M13C*, M14*, M12* or M1* flag
# variable; m16bdiag-verify checks that mechanically.
M16B_SESSION_DEFINE = -DLUAPORT_SESSION_REUSE

# ***** NO -Iapps/m13bgpsp. ***** M16-1 HAS NO PICKER. A is library entry 0 and
# B is library entry 1, fixed at compile time, so m13b_picker.inc is not
# included, not compiled and not on the dependency line.
M16B_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m16bdiag \
                -Iadapters/gba

M16BFLAGS = $(BASEFLAGS) $(M16B_INCLUDES) -Wall -Wno-unused-function \
            $(M16B_SESSION_DEFINE)

# ***** -DROM_BUFFER_SIZE=2 IS NOT ALTERED. ***** Two 1 MB buffers is what makes
# a cartridge over 2 MB demand paged, and the demand-paging file handle is the
# ONE RFILE pool slot per launch this family exists to prove is released ACROSS
# A CARTRIDGE CHANGE. Raising it would delete the very leak being measured.
M16B_GPSPFLAGS = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2 $(M16B_SESSION_DEFINE)

# gpsp/** gets the SAME flags M13C's and M16-0's core objects get, WITHOUT the
# session define: no file under gpsp/ mentions LUAPORT_SESSION_REUSE.
M16B_COREFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M16B_CORECXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M16BHEADERS = $(HEADERS)

M16BLDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
              -Wl,--build-id=none -Wl,-z,norelro \
              -Wl,--orphan-handling=error \
              -Wl,-Map=$(M16BBUILD)/$(M16BTARGET).map

# ----------------------------------------------------------------- objects ---
#
# ***** M16-0's SET EXACTLY, WITH m16main.o REPLACED BY m16bmain.o. ***** Not
# one object is added and not one is removed. That is itself a claim: A -> B -> A
# needs NO new capability, because everything gba_session_end() releases is
# session-scoped rather than cartridge-scoped.
#
# ***** THE FOUR SAVE-WRITER OBJECTS ARE ABSENT, AND HERE THE STAKES ARE HIGHER
# THAN THEY WERE IN M16-0. *****
#   savedata.o          the runtime savedata layer
#   gba_savefile.o      the ONLY writer of /savedata0/gba/<ID>.sav
#   gba_savehdr.o       the .hdr format
#   gba_restorefile.o   the save-file reader
# M16-0 ran ONE cartridge, so a broken unlatch could only have overwritten a
# save with its own data. THIS image runs TWO DIFFERENT cartridges, so a broken
# unlatch would commit cartridge B's memory into cartridge A's save file and
# destroy it. A diagnostic that is still LEARNING whether that happens MUST NOT
# be able to perform the write. Omitting the objects makes it physically
# impossible rather than merely forbidden.
#
# ***** gba_m8map.o IS ABSENT: M16-1 MAKES NO CONTROLLER-MAPPING CHANGE. *****
# The bounded run calls m8_input_apply(0u) -- neutral, every frame, both
# cartridges -- so a difference between sessions can only come from the
# cartridge or from the teardown.
#
# STILL ABSENT, FOR THEIR ORIGINAL REASONS:
#   gba_m11save.o       M11's 128-block dirty map that nothing here reads
#   cpu_threaded.o      every dynarec emitter. HAVE_DYNAREC stays undefined and
#                       dynarec_enable stays UNDEFINED so a leak is a LINK ERROR
#   libretro*.o         and the libretro VFS
M16B_OWN_OBJS = $(M16BBUILD)/m16bmain.o \
                $(M16BBUILD)/shim.o \
                $(M16BBUILD)/gfx.o \
                $(M16BBUILD)/platform.o \
                $(M16BBUILD)/audio.o \
                $(M16BBUILD)/m13store.o \
                $(M16BBUILD)/gba_library.o \
                $(M16BBUILD)/gba_fixture.o \
                $(M16BBUILD)/gba_filestream.o \
                $(M16BBUILD)/gba_compat.o \
                $(M16BBUILD)/gba_probe.o \
                $(M16BBUILD)/gba_bios.o \
                $(M16BBUILD)/gba_rom.o \
                $(M16BBUILD)/gba_save.o \
                $(M16BBUILD)/gba_restore.o \
                $(M16BBUILD)/gba_sramobs.o \
                $(M16BBUILD)/gba_m10map.o \
                $(M16BBUILD)/gba_exec.o \
                $(M16BBUILD)/gba_present.o \
                $(M16BBUILD)/gba_input.o \
                $(M16BBUILD)/gba_audio.o \
                $(M16BBUILD)/gba_session.o

M16B_GPSP_OBJS = $(M16BBUILD)/gpsp_cpu.o $(M16BBUILD)/gpsp_main.o \
                 $(M16BBUILD)/gpsp_gba_memory.o $(M16BBUILD)/gpsp_video.o \
                 $(M16BBUILD)/gpsp_sound.o $(M16BBUILD)/gpsp_serial.o \
                 $(M16BBUILD)/gpsp_serial_proto.o $(M16BBUILD)/gpsp_rfu.o \
                 $(M16BBUILD)/gpsp_gbp.o $(M16BBUILD)/gpsp_cheats.o \
                 $(M16BBUILD)/gpsp_savestate.o $(M16BBUILD)/gpsp_input.o

M16BOBJS = $(M16B_OWN_OBJS) $(M16B_GPSP_OBJS)

# Every capability the diagnostic advertises must be ON THE LINK LINE.
M16B_REQUIRED_OBJS = m16bmain.o gba_session.o shim.o m13store.o gba_library.o \
                     gba_rom.o gba_filestream.o gba_compat.o gba_fixture.o \
                     gba_bios.o gba_probe.o gba_exec.o gba_present.o gfx.o \
                     platform.o gba_input.o audio.o gba_audio.o gba_m10map.o \
                     gba_save.o gba_restore.o gba_sramobs.o

# ***** THE OBJECTS THAT MUST NOT BE ON THE LINK LINE. *****
M16B_FORBIDDEN_OBJS = savedata.o gba_savefile.o gba_savehdr.o \
                      gba_restorefile.o gba_m8map.o

# ---- offline structural check -----------------------------------------------
#
# Runs on a workstation with NO toolchain at all. Only the NEW source is checked
# here plus the shared units M16 touched, exactly as m16diag-check does;
# apps/m16diag/main.c is deliberately NOT checked by this target, because this
# family does not own it.
m16bdiag-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m16bdiag/main.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_session.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_save.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_restore.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_filestream.c
	@$(PYTHON) tools/m8_syntax_check.py runtime/shim.c

# ---- LUAport-owned objects, runtime side ------------------------------------
#
# ***** m16bmain.o IS COMPILED WITH $(M16BFLAGS), WHICH HAS NO -Igpsp. ***** The
# app names exactly one gpSP symbol -- skip_next_frame -- and hand-declares it
# as `extern unsigned int`, sourced from adapters/gba/gba_fixture.c:70. Nothing
# else crosses the type boundary.
$(M16BBUILD)/m16bmain.o: apps/m16bdiag/main.c \
                         adapters/gba/m13store.h \
                         adapters/gba/gba_library.h \
                         adapters/gba/gba_rom.h \
                         adapters/gba/gba_probe.h \
                         adapters/gba/gba_bios.h \
                         adapters/gba/gba_save.h \
                         adapters/gba/gba_restore.h \
                         adapters/gba/gba_exec.h \
                         adapters/gba/gba_present.h \
                         adapters/gba/gba_input.h \
                         adapters/gba/gba_m10map.h \
                         adapters/gba/gba_sramobs.h \
                         adapters/gba/gba_session.h \
                         adapters/gba/gba_audio.h runtime/audio.h \
                         $(M16BHEADERS) | $(M16BBUILD)
	$(CC) $(M16BFLAGS) -c $< -o $@

# shim.o carries arena_mark() and arena_rewind(), both inside
# #ifdef LUAPORT_SESSION_REUSE (runtime/shim.c:41-74), so M13C's shim.o is
# unchanged. runtime/shim.h is an explicit prerequisite because the guarded
# declarations live there too.
$(M16BBUILD)/shim.o: runtime/shim.c runtime/shim.h $(M16BHEADERS) \
                     | $(M16BBUILD)
	$(CC) $(M16BFLAGS) -c $< -o $@

$(M16BBUILD)/gfx.o: runtime/gfx.c $(M16BHEADERS) | $(M16BBUILD)
	$(CC) $(M16BFLAGS) -c $< -o $@

$(M16BBUILD)/platform.o: runtime/platform.c $(M16BHEADERS) | $(M16BBUILD)
	$(CC) $(M16BFLAGS) -c $< -o $@

$(M16BBUILD)/audio.o: runtime/audio.c runtime/audio.h $(M16BHEADERS) \
                      | $(M16BBUILD)
	$(CC) $(M16BFLAGS) -c $< -o $@

$(M16BBUILD)/m13store.o: adapters/gba/m13store.c \
                         adapters/gba/m13store.h runtime/core.h | $(M16BBUILD)
	$(CC) $(M16BFLAGS) -c $< -o $@

$(M16BBUILD)/gba_library.o: adapters/gba/gba_library.c \
                            adapters/gba/gba_library.h \
                            adapters/gba/m13store.h | $(M16BBUILD)
	$(CC) $(M16BFLAGS) -c $< -o $@

$(M16BBUILD)/gba_present.o: adapters/gba/gba_present.c \
                            adapters/gba/gba_present.h \
                            runtime/core.h | $(M16BBUILD)
	$(CC) $(M16BFLAGS) -c $< -o $@

# ---- gpSP-facing adapters ---------------------------------------------------
#
# These see gpSP's type universe through -Igpsp and MUST NOT get $(M16BFLAGS).
$(M16BBUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M16BBUILD)
	$(CC) $(M16B_GPSPFLAGS) -c $< -o $@

$(M16BBUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M16BBUILD)
	$(CC) $(M16B_GPSPFLAGS) -c $< -o $@

$(M16BBUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M16BBUILD)
	$(CC) $(M16B_GPSPFLAGS) -c $< -o $@

$(M16BBUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h \
                          | $(M16BBUILD)
	$(CC) $(M16B_GPSPFLAGS) -c $< -o $@

$(M16BBUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h \
                         | $(M16BBUILD)
	$(CC) $(M16B_GPSPFLAGS) -c $< -o $@

$(M16BBUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h \
                        | $(M16BBUILD)
	$(CC) $(M16B_GPSPFLAGS) -c $< -o $@

$(M16BBUILD)/gba_save.o: adapters/gba/gba_save.c adapters/gba/gba_save.h \
                         | $(M16BBUILD)
	$(CC) $(M16B_GPSPFLAGS) -c $< -o $@

$(M16BBUILD)/gba_restore.o: adapters/gba/gba_restore.c \
                            adapters/gba/gba_restore.h | $(M16BBUILD)
	$(CC) $(M16B_GPSPFLAGS) -c $< -o $@

$(M16BBUILD)/gba_sramobs.o: adapters/gba/gba_sramobs.c \
                            adapters/gba/gba_sramobs.h | $(M16BBUILD)
	$(CC) $(M16B_GPSPFLAGS) -c $< -o $@

$(M16BBUILD)/gba_m10map.o: adapters/gba/gba_m10map.c \
                           adapters/gba/gba_m10map.h | $(M16BBUILD)
	$(CC) $(M16B_GPSPFLAGS) -c $< -o $@

$(M16BBUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h \
                         | $(M16BBUILD)
	$(CC) $(M16B_GPSPFLAGS) -c $< -o $@

$(M16BBUILD)/gba_input.o: adapters/gba/gba_input.c adapters/gba/gba_input.h \
                          | $(M16BBUILD)
	$(CC) $(M16B_GPSPFLAGS) -c $< -o $@

# runtime/audio.h is deliberately NOT a prerequisite: gba_audio.c does not
# include it.
$(M16BBUILD)/gba_audio.o: adapters/gba/gba_audio.c adapters/gba/gba_audio.h \
                          | $(M16BBUILD)
	$(CC) $(M16B_GPSPFLAGS) -c $< -o $@

# ***** THE SESSION ADAPTER, UNCHANGED FROM M16-0. ***** gba_session.h refuses
# to compile without the session define -- it has an #error at :22-23 -- so this
# object cannot be built into a frozen milestone even by accident.
$(M16BBUILD)/gba_session.o: adapters/gba/gba_session.c \
                            adapters/gba/gba_session.h \
                            adapters/gba/gba_save.h \
                            adapters/gba/gba_restore.h \
                            adapters/gba/gba_sramobs.h \
                            adapters/gba/gba_input.h \
                            adapters/gba/gba_audio.h \
                            adapters/gba/gba_m10map.h \
                            adapters/gba/gba_rom.h \
                            adapters/gba/gba_probe.h \
                            adapters/gba/gba_bios.h | $(M16BBUILD)
	$(CC) $(M16B_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects ------------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags. gpsp/** is never
# modified, its own Makefile is never invoked, and it does NOT receive
# $(M16B_SESSION_DEFINE).
#
# gpsp_gba_memory.o carries memory_term(), load_gamepak_raw() and
# init_gamepak_buffer() -- the three functions that make a SECOND cartridge
# safe. load_gamepak_raw() map_null()s 0x8000000-0xD000000 before mapping
# anything and recomputes every size global; init_gamepak_buffer() rebuilds all
# 1024 LRU entries with phy_rom = -1. NOT ONE LINE OF gpsp/ IS EDITED.

$(M16BBUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M16BBUILD)
	$(CXX) $(M16B_CORECXXFLAGS) -c $< -o $@

$(M16BBUILD)/gpsp_video.o: gpsp/video.cc | $(M16BBUILD)
	$(CXX) $(M16B_CORECXXFLAGS) -c $< -o $@

$(M16BBUILD)/gpsp_main.o: gpsp/main.c | $(M16BBUILD)
	$(CC) $(M16B_COREFLAGS) -c $< -o $@

$(M16BBUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M16BBUILD)
	$(CC) $(M16B_COREFLAGS) -c $< -o $@

$(M16BBUILD)/gpsp_sound.o: gpsp/sound.c | $(M16BBUILD)
	$(CC) $(M16B_COREFLAGS) -c $< -o $@

$(M16BBUILD)/gpsp_serial.o: gpsp/serial.c | $(M16BBUILD)
	$(CC) $(M16B_COREFLAGS) -c $< -o $@

$(M16BBUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M16BBUILD)
	$(CC) $(M16B_COREFLAGS) -c $< -o $@

$(M16BBUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M16BBUILD)
	$(CC) $(M16B_COREFLAGS) -c $< -o $@

$(M16BBUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M16BBUILD)
	$(CC) $(M16B_COREFLAGS) -c $< -o $@

$(M16BBUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M16BBUILD)
	$(CC) $(M16B_COREFLAGS) -c $< -o $@

$(M16BBUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M16BBUILD)
	$(CC) $(M16B_COREFLAGS) -c $< -o $@

$(M16BBUILD)/gpsp_input.o: gpsp/input.c | $(M16BBUILD)
	$(CC) $(M16B_COREFLAGS) -c $< -o $@

m16bdiag-objs: $(M16BOBJS)

# ---- link -------------------------------------------------------------------

$(M16BELF): $(M16BOBJS) runtime/linker.ld | $(M16BBUILD)
	$(CC) $(BASEFLAGS) $(M16BLDFLAGS) -o $@ $(M16BOBJS)

m16bdiag-link: $(M16BOBJS) | $(M16BBUILD)
	@$(CC) $(BASEFLAGS) $(M16BLDFLAGS) -o $(M16BELF) $(M16BOBJS) \
	     > $(M16BBUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to .[^']+'" $(M16BBUILD)/link.log \
	            | sed -E "s/.*to .//; s/'$$//" | sort -u); \
	 if [ $$status -ne 0 ]; then \
	     cat $(M16BBUILD)/link.log; \
	     echo "M16B LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M16B LINK FAILED: undefined references reported despite exit 0"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "M16B LINK OK: linker exit 0, no undefined references"

$(M16BBIN): $(M16BELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M16BELF) $@ \
	   --map $(M16BBUILD)/$(M16BTARGET).map --budget $(JIT_LIMIT) --arena 4194304

m16bdiag: $(M16BBIN)

# ***** THE BUDGETS ARE NOT RAISED BY THIS MILESTONE EITHER. ***** $(JIT_LIMIT)
# is still 524,288, the .bss tripwire is still 2,097,152 and the arena is still
# 4,194,304. The thesis is that a SECOND, DIFFERENT cartridge fits in the SAME
# arena the first one used, so growing the arena would delete the finding.
m16bdiag-size: $(M16BELF) $(M16BBIN)
	@$(PYTHON) tools/sizereport.py $(M16BELF) $(M16BBIN) \
	   --map $(M16BBUILD)/$(M16BTARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification -----------------------------------------------------------

m16bdiag-verify: $(M16BELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M16BELF)
	@echo "== relocations (NON-MASKING pipeline) =="
	@$(PYTHON) tools/verify_reloc.py $(M16BELF) $(M16BOBJS) \
	     > $(M16BBUILD)/reloc.log 2>&1; \
	 st=$$?; \
	 tail -12 $(M16BBUILD)/reloc.log; \
	 if [ $$st -ne 0 ]; then \
	     echo "M16B VERIFY FAILED: the relocation check exited $$st."; \
	     echo "  Full output: $(M16BBUILD)/reloc.log"; \
	     exit $$st; \
	 fi
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M16BOBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M16BOBJS)
	@echo "== forbidden symbols in the linked image (.bss tripwire included) =="
	@$(PYTHON) tools/check_forbidden.py $(M16BELF) $(M16BOBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M16BELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M16B VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== ***** EVERY ADVERTISED CAPABILITY IS LINKED ***** =="
	@missing=""; \
	 for want in $(M16B_REQUIRED_OBJS); do \
	     found=0; \
	     for o in $(M16BOBJS); do \
	         case "$$o" in */$$want) found=1;; esac; \
	     done; \
	     if [ $$found -eq 0 ]; then missing="$$missing $$want"; fi; \
	 done; \
	 if [ -n "$$missing" ]; then \
	     echo "M16B VERIFY FAILED: a required object is NOT on the link"; \
	     echo "  line:$$missing"; \
	     exit 1; \
	 fi; \
	 echo "all twenty-two required objects are PRESENT"
	@echo "== ***** NO SAVE CAN BE WRITTEN: THE WRITE PATH IS NOT LINKED ***** =="
# M16-0 ran one cartridge, so a broken unlatch could only have overwritten a
# save with its own data. THIS image runs TWO DIFFERENT cartridges: a broken
# unlatch would commit cartridge B's memory into cartridge A's save file. The
# write path is therefore ABSENT FROM THE IMAGE rather than merely unused.
	@present=""; \
	 for bad in $(M16B_FORBIDDEN_OBJS); do \
	     for o in $(M16BOBJS); do \
	         case "$$o" in */$$bad) present="$$present $$bad";; esac; \
	     done; \
	 done; \
	 if [ -n "$$present" ]; then \
	     echo "M16B VERIFY FAILED: a forbidden object is on the link line:"; \
	     echo " $$present"; \
	     echo "  The four save-writer objects and the controller mapping are"; \
	     echo "  EXCLUDED BY DESIGN. M16-1 commits nothing and remaps nothing."; \
	     exit 1; \
	 fi; \
	 echo "no save-writer object and no controller mapping is linked"
	@sym=$$($(NM) $(M16BELF) 2>/dev/null \
	          | grep -cE " T (gba_savefile_commit|gba_savehdr_parse|plat_savedata_init)$$"); \
	 if [ "$$sym" != "0" ]; then \
	     echo "M16B VERIFY FAILED: a save-write entry point is DEFINED in the"; \
	     echo "  image even though its object was excluded."; \
	     exit 1; \
	 fi; \
	 echo "no save-write entry point is defined anywhere in the image"
	@echo "== ***** memory_term IS LINKED AND REACHED ***** =="
	@if ! $(NM) $(M16BELF) 2>/dev/null | grep -qE " [Tt] memory_term$$"; then \
	     echo "M16B VERIFY FAILED: memory_term is not defined in the image."; \
	     exit 1; \
	 fi; \
	 echo "memory_term is DEFINED in the image"
	@if ! $(NM) $(M16BBUILD)/gba_session.o 2>/dev/null \
	        | grep -qE " U memory_term$$"; then \
	     echo "M16B VERIFY FAILED: gba_session.o does not REFERENCE"; \
	     echo "  memory_term. The teardown would close no file and free no"; \
	     echo "  buffer, and all three sessions would pass vacuously."; \
	     exit 1; \
	 fi; \
	 echo "gba_session.o REFERENCES memory_term (the ROM file is really closed)"
	@echo "== ***** THE RESET PRIMITIVES EXIST ***** =="
	@miss=""; \
	 for s in arena_mark arena_rewind gba_save_unlatch gba_restore_disarm \
	          gba_rfile_inuse gba_session_end gba_session_probe_relaunch; do \
	     $(NM) $(M16BELF) 2>/dev/null | grep -qE " [Tt] $$s$$" \
	         || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M16B VERIFY FAILED: a reset primitive is missing:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "all seven M16 reset primitives are defined"
	@echo "== ***** THE CARTRIDGE IDENTITY EVIDENCE IS REALLY READ ***** =="
# The whole rung rests on CONTENT-derived identity. If any of these went
# missing, the image would be comparing something else -- or nothing -- and
# would still report a PASS.
	@miss=""; \
	 for s in m4_rom_fnv1a m4_rom_code_copy gba_save_rom_hash \
	          gba_save_id_copy gba_save_latched; do \
	     grep -q "$$s" apps/m16bdiag/main.c || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M16B VERIFY FAILED: the diagnostic no longer reads its own"; \
	     echo "  cartridge identity evidence:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "all five content-derived identity readers are called"
	@echo "== ***** THE PLAN IS A -> B -> A ***** =="
	@if ! grep -qE "M16B_SLOT_A, *M16B_SLOT_B, *M16B_SLOT_A" \
	        apps/m16bdiag/main.c; then \
	     echo "M16B VERIFY FAILED: the A -> B -> A plan table is gone. Without"; \
	     echo "  it this image is M16-0 under a different name, and M16-0 has"; \
	     echo "  already passed -- it would report an isolation PASS that was"; \
	     echo "  never measured."; \
	     exit 1; \
	 fi; \
	 echo "the session plan is library entry 0 -> entry 1 -> entry 0"
	@if grep -qE "M16B_SESSIONS +[4-9]" apps/m16bdiag/main.c; then \
	     echo "M16B VERIFY FAILED: the session count was raised. THREE is the"; \
	     echo "  claim -- A, reset, B, reset, A -- and only the third session"; \
	     echo "  can prove A is RECOVERED rather than merely replaced."; \
	     exit 1; \
	 fi; \
	 echo "the diagnostic still runs exactly three sessions"
	@echo "== ***** THE DIAGNOSTIC CANNOT SILENTLY PASS ***** =="
# The eight cross-cartridge statuses are the product of this image, and the five
# inherited from M16-0 still guard the resource half. If a future edit deleted
# one, a broken teardown or a stale identity would report success.
	@miss=""; \
	 for s in M16B_NEED_TWO_ROMS M16B_AB_SAME M16B_ID_EMPTY \
	          M16B_B_INHERITED_A M16B_A3_LOST M16B_ROM_B_BAD \
	          M16B_PAGING_DIRTY M16B_CROSS_DRIFT \
	          M16B_TEARDOWN_DIRTY M16B_ARENA_LEAK M16B_ARENA_REWIND_BAD \
	          M16B_IDENTITY_STALE M16B_PRE_UNEXPECTED; do \
	     grep -q "$$s" apps/m16bdiag/main.c || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M16B VERIFY FAILED: a failure status was removed:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "all thirteen M16-1 failure statuses are present"
	@if ! grep -q "gba_session_probe_relaunch" apps/m16bdiag/main.c; then \
	     echo "M16B VERIFY FAILED: the relaunch invariant probe is not called."; \
	     exit 1; \
	 fi; \
	 echo "the relaunch invariant probe is called at every session boundary"
	@echo "== ***** THE M16-0 HARDWARE EVIDENCE IS NOT TOUCHED ***** =="
# M16-0 passed on a real PS5. Its sources are evidence, and this family must be
# able to change without any risk to them.
	@if grep -qE "m16b|M16B_|m16bdiag" apps/m16diag/main.c lua/m16diag.lua.in; then \
	     echo "M16B VERIFY FAILED: an M16-0 source carries an M16-1 reference."; \
	     echo "  apps/m16diag/** and lua/m16diag.lua.in are HARDWARE PASS"; \
	     echo "  EVIDENCE and M16-1 is a SEPARATE payload precisely so the two"; \
	     echo "  stay independent."; \
	     exit 1; \
	 fi; \
	 echo "apps/m16diag/main.c and lua/m16diag.lua.in carry no M16-1 reference"
	@echo "== ***** THE FROZEN M13C BASELINE IS NOT TOUCHED ***** =="
# ***** THE CENTRAL ISOLATION GATE, REPRODUCED RATHER THAN SHARED. *****
# M16 adds code to four SHARED translation units. The freeze survives only
# because every addition sits behind #ifdef LUAPORT_SESSION_REUSE and that macro
# never reaches an M13C compile. Both halves are CHECKED, not trusted.
	@if $(MAKE) --no-print-directory -pn m13c 2>/dev/null \
	     | grep -E "^M13C[A-Z_]* *[:+]?=" \
	     | grep -q "LUAPORT_SESSION_REUSE"; then \
	     echo "M16B VERIFY FAILED: LUAPORT_SESSION_REUSE reaches an M13C flag"; \
	     echo "  variable. M13C would preprocess the shared files differently"; \
	     echo "  and build/m13c/m13cgpsp.bin would no longer reproduce"; \
	     echo "  e9925161d48f65660bbc89cdd9bd9fb6fb98f6b48431a80b44c500395d6d824a."; \
	     echo "  STOP -- do not update the frozen hash."; \
	     exit 1; \
	 fi; \
	 echo "no M13C flag variable carries LUAPORT_SESSION_REUSE"
	@if grep -qE "m16_|m16b|m16diag|m16bdiag|LUAPORT_SESSION_REUSE" \
	        apps/m13cgpsp/main.c; then \
	     echo "M16B VERIFY FAILED: apps/m13cgpsp/main.c carries an M16 code"; \
	     echo "  reference. The launcher is FROZEN EVIDENCE."; \
	     exit 1; \
	 fi; \
	 echo "apps/m13cgpsp/main.c carries no M16 code reference"
	@if grep -qE "m16_|m16b|m16diag|m16bdiag" lua/m13c.lua.in; then \
	     echo "M16B VERIFY FAILED: lua/m13c.lua.in carries an M16 reference."; \
	     exit 1; \
	 fi; \
	 echo "lua/m13c.lua.in carries no M16 reference"
	@for f in runtime/shim.c adapters/gba/gba_save.c \
	          adapters/gba/gba_restore.c adapters/gba/gba_filestream.c; do \
	     n=$$(grep -c "^#ifdef LUAPORT_SESSION_REUSE" $$f); \
	     if [ "$$n" -lt 1 ]; then \
	         echo "M16B VERIFY FAILED: $$f has no LUAPORT_SESSION_REUSE guard."; \
	         echo "  Either the M16 addition was removed, or it was added"; \
	         echo "  UNGUARDED and the M13C freeze is already broken."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all four shared translation units carry a session-reuse guard"
	@echo "== ***** THE BUDGETS ARE NOT RAISED ***** =="
	@if ! grep -q "ROM_BUFFER_SIZE=2" Makefile; then \
	     echo "M16B VERIFY FAILED: ROM_BUFFER_SIZE is no longer 2."; \
	     exit 1; \
	 fi; \
	 echo "ROM_BUFFER_SIZE is still 2"
	@if ! grep -q "GBA_LIB_MAX_ENTRIES 64" adapters/gba/gba_library.h; then \
	     echo "M16B VERIFY FAILED: the picker capacity is no longer 64."; \
	     exit 1; \
	 fi; \
	 echo "the library capacity is still 64"
	@echo "== M16B VERIFY OK =="

# ---- the Lua payload --------------------------------------------------------
#
# ***** ITS OWN TEMPLATE. lua/m13c.lua.in AND lua/m16diag.lua.in ARE NOT EDITED
# AND NOT REUSED IN PLACE. ***** The delivery machinery is hardware-proven from
# M0 through M16-0 and is reproduced rather than re-derived; only the labels and
# the decode differ.
M16B_LUA_TEMPLATE := lua/m16bdiag.lua.in
M16B_LUA_OUT      := $(M16BBUILD)/$(M16BTARGET).lua

m16bdiag-lua: m16bdiag-check $(M16BBIN) $(M16BELF)
	@$(PYTHON) tools/mklua.py $(M16B_LUA_TEMPLATE) $(M16BBIN) \
	     $(M16B_LUA_OUT) $(M16BELF)
	@echo "M16B LUA OK: $(M16B_LUA_OUT)"

# ---- m16bdiag-send -- HARDWARE DELIVERY --------------------------------------
#
# NOT AUTHORISED BY THE EXISTENCE OF THIS TARGET.
#
#   * ***** M16-1 HAS HAD NO HARDWARE RUN. ***** M16-0 passed; that is a
#     DIFFERENT image answering a DIFFERENT question. Every claim this one makes
#     is a HYPOTHESIS until a PS5 says otherwise.
#   * ***** PUT TWO GENUINELY DIFFERENT .gba FILES IN THE LIBRARY. ***** A is
#     entry 0 and B is entry 1. Two copies of one cartridge under two names is
#     detected and is FATAL (-522): the run would prove nothing. Ideally make
#     one of them larger than 2 MB so the demand-paging RFILE handle is
#     exercised across the cartridge change.
#   * ***** IT WRITES NO SAVE AND CANNOT. ***** The write path is not linked;
#     m16bdiag-verify proves it.
#   * ***** A -524 B INHERITED A IS THE DATA-LOSS FINDING. ***** It means the
#     save identity did not follow the cartridge, and a real launcher would have
#     committed cartridge B's memory into cartridge A's save file.
#   * ***** A -525 A3 LOST IS THE SAME FINDING IN THE OTHER DIRECTION. *****
#   * THE SCREEN IS THE EVIDENCE. Photograph the final report: three rows, row 1
#     and row 3 the SAME identity, row 2 a DIFFERENT one, all three CLEAR, and
#     the four conclusion lines YES. The UDP log on 9027 carries the detail but
#     is not required for the PASS.
m16bdiag-send: m16bdiag-verify m16bdiag-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m16bdiag-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M16B_LUA_OUT) $(M16BBIN)

m16bdiag-clean:
	rm -rf $(M16BBUILD)

.PHONY: m16bdiag m16bdiag-objs m16bdiag-check m16bdiag-link m16bdiag-size \
        m16bdiag-verify m16bdiag-lua m16bdiag-send m16bdiag-clean

# =============================================================================
# M16-2 -- THE LAUNCHER THAT COMES BACK
# =============================================================================
#
#   make m16c-check      offline structural check, no compiler needed
#   make m16c-objs
#   make m16c-link
#   make m16c
#   make m16c-size
#   make m16c-verify
#   make m16c-lua
#   make m16c-send PS5_IP=<addr>
#   make m16c-clean
#
# ***** THE M13C FAMILY IS THE TEMPLATE. THE M16 DIAGNOSTIC FAMILIES ARE NOT.
# ***** M16-0 and M16-1 are DIAGNOSTICS, and both of them deliberately OMIT the
# save writer -- savedata.o, gba_savefile.o, gba_savehdr.o and gba_restorefile.o
# are absent from their link lines and their verifiers assert that absence. That
# restriction existed because those images were still LEARNING whether the save
# identity follows the cartridge across a session boundary; a diagnostic that
# does not yet know must not be able to perform the write.
#
# ***** BOTH HAVE NOW PASSED ON HARDWARE, SO M16-2 INHERITS THE OPPOSITE RULE.
# ***** M16C is a LAUNCHER, and a launcher that cannot persist a save is not a
# launcher. It therefore links the REAL M13C persistent save/restore stack, and
# m16c-verify asserts the PRESENCE of exactly the four objects m16bdiag-verify
# asserts the ABSENCE of. Reusing the M16B block here would have silently
# inherited a no-save-writer image and every session would have been vacuous.
#
# ***** WHAT THIS MILESTONE DOES NOT TOUCH. *****
#   apps/m13cgpsp/**            THE FROZEN LAUNCHER. Not one byte.
#   lua/m13c.lua.in             frozen
#   apps/m16diag/**             M16-0 HARDWARE EVIDENCE. Not one byte.
#   apps/m16bdiag/**            M16-1 HARDWARE EVIDENCE. Not one byte.
#   lua/m16diag.lua.in          evidence
#   lua/m16bdiag.lua.in         evidence
#   adapters/gba/gba_session.c  used EXACTLY as M16-0 left it
#   adapters/gba/gba_session.h  used EXACTLY as M16-0 left it
#   gpsp/**                     not one line
#   $(M13COBJS), $(M16OBJS), $(M16BOBJS) and all three of their verifiers
#
# ***** THE PICKER MODEL IS SHARED, NOT COPIED. ***** apps/m13bgpsp/
# m13b_picker.inc is BYTE-UNCHANGED and is reached from apps/m16cgpsp/main.c
# through -Iapps/m13bgpsp, exactly as M13B and M13C reach it, so
# tools/m13b_picker_equiv.c's 283 checks prove the picker that ships HERE too.
# m16c-verify depends on m13b-equiv for exactly that reason.

M16CTARGET := m16cgpsp
M16CBUILD  := build/m16c
M16CELF    := $(M16CBUILD)/$(M16CTARGET).elf
M16CBIN    := $(M16CBUILD)/$(M16CTARGET).bin

$(M16CBUILD):
	mkdir -p $(M16CBUILD)

# ---- flags ------------------------------------------------------------------
#
# ***** ITS OWN COPY OF THE ONE DEFINE THAT SEPARATES M16 FROM EVERY FROZEN
# MILESTONE. ***** Declared separately from $(M16_SESSION_DEFINE) and
# $(M16B_SESSION_DEFINE) so that the three M16 families share NO flag variable
# at all and none of them can be changed by editing another. It must never
# appear in an M13C*, M14*, M12* or M1* flag variable; m16c-verify checks that
# mechanically, against the frozen M13C hash by name.
M16C_SESSION_DEFINE = -DLUAPORT_SESSION_REUSE

# ***** -Iapps/m13bgpsp IS PRESENT, UNLIKE M16-1's. ***** M16-1 has no picker --
# its A and B are library entries fixed at compile time. M16-2 IS the picker, so
# m13b_picker.inc is included exactly as M13C includes it.
M16C_INCLUDES = $(M1_SHIMINC) -Iruntime/libc -Iruntime -Iapps/m16cgpsp \
                -Iapps/m13bgpsp -Iadapters/gba

# shim.o carries arena_mark()/arena_rewind() behind #ifdef LUAPORT_SESSION_REUSE
# (runtime/shim.c:41-74), so the runtime-side flags MUST carry the define or the
# boundary would not link.
M16CFLAGS = $(BASEFLAGS) $(M16C_INCLUDES) -Wall -Wno-unused-function \
            $(M16C_SESSION_DEFINE)

# ***** -DROM_BUFFER_SIZE=2 IS NOT ALTERED. ***** Two 1 MB buffers is what makes
# a cartridge over 2 MB demand paged, and the demand-paging file handle is the
# one RFILE pool slot per session that the boundary must release before the
# picker comes back. Raising it would delete the very leak being measured.
#
# The gpSP-FACING ADAPTERS get the session define because three of them --
# gba_save.c, gba_restore.c and gba_filestream.c -- carry guarded M16 additions.
M16C_GPSPFLAGS = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2 $(M16C_SESSION_DEFINE)

# gpsp/** gets the SAME flags M13C's, M16-0's and M16-1's core objects get,
# WITHOUT the session define: no file under gpsp/ mentions LUAPORT_SESSION_REUSE,
# so passing it would change nothing while making these objects differ from
# M13C's for no reason -- and would weaken the claim that they are unmodified.
M16C_COREFLAGS    = $(GPSPFLAGS) -DROM_BUFFER_SIZE=2
M16C_CORECXXFLAGS = $(GPSPCXXFLAGS) -DROM_BUFFER_SIZE=2

M16CHEADERS = $(HEADERS)

M16CLDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
              -Wl,--build-id=none -Wl,-z,norelro \
              -Wl,--orphan-handling=error \
              -Wl,-Map=$(M16CBUILD)/$(M16CTARGET).map

# ----------------------------------------------------------------- objects ---
#
# ***** M13C's TWENTY-SIX + TWELVE, WITH m13cmain.o REPLACED BY m16cmain.o AND
# gba_session.o ADDED -- PLUS gba_r7input.o AT R7, SO TWENTY-EIGHT + TWELVE.
# ***** Coming back to the picker needed ONE new adapter, because everything
# gba_session_end() releases is session-scoped rather than payload-scoped; the
# gameplay button aliases needed ONE more, for a reason that is recorded rather
# than assumed -- see the gba_r7input.o note below.
#
#   picker / library    m13store.o gba_library.o
#   named-ROM loader    gba_rom.o gba_filestream.o gba_compat.o gba_fixture.o
#   BIOS                gba_bios.o          loaded ONCE, per payload
#   probes              gba_probe.o
#   execution           gba_exec.o          THE ONLY execute_arm CALLER
#   presentation        gba_present.o gfx.o platform.o
#   input               gba_input.o gba_m8map.o
#   gameplay aliases    gba_r7input.o   ***** THE R7 OBJECT *****
#   audio               audio.o gba_audio.o
#   paging observer     gba_m10map.o
#   save identity       gba_save.o
#   save restore        gba_restore.o gba_restorefile.o gba_savehdr.o
#   save commit         savedata.o gba_savefile.o
#   SRAM observation    gba_sramobs.o   READ-ONLY, COMMIT-SIDE, DIAGNOSTIC
#   THE SESSION BOUNDARY gba_session.o  ***** THE ONE NEW OBJECT *****
#
# ***** gba_m8map.o IS PRESENT, UNLIKE M16-1's. ***** M16-1 dropped it because a
# bounded diagnostic calls m8_input_apply(0u) every frame and a mapping change
# would have been a second variable. M16C is the launcher and the operator's
# controller mapping is part of what it delivers.
#
# ***** gba_r7input.o IS M16C-ONLY, AND THAT IS THE WHOLE POINT. ***** R7 gives
# gameplay two intentional button aliases -- { CROSS, CIRCLE } -> GBA A and
# { SQUARE, TRIANGLE } -> GBA B. The obvious implementation, editing the table
# in adapters/gba/gba_input.c, IS NOT AVAILABLE: that source is compiled
# separately into ELEVEN images, including the FROZEN M13C (:11425, :11607),
# whose SHA-256 is an acceptance criterion. One table edit would move six
# hardware-verified baselines at once.
#
# So gba_input.{c,h} ARE NOT EDITED BY R7 -- not one byte -- and the alias layer
# is an ADDITIVE unit linked into M16C alone, which canonicalises the PAD WORD
# and then delegates to the frozen translator. m8_input_selftest()'s
# M8_ST_UNIQUE and M8_ST_UNMAPPED therefore still pass VERBATIM, and
# `make r7-input-equiv` re-proves them before proving the aliases on top. This
# is adapters/gba/gba_m8map.h:20-46's precedent applied a second time: additive,
# milestone-local, and strictly stronger than what it sits above.
#
# STILL ABSENT, AND EVERY ABSENCE IS A GATE:
#   gba_m11save.o       M11's observer. M16C owns its own dirty accounting
#                       (M12C's post-restore baseline) and would gain a
#                       128-block dirty map nothing reads.
#   cpu_threaded.o      every dynarec emitter and stub. HAVE_DYNAREC stays
#                       undefined and dynarec_enable stays an UNDEFINED symbol
#                       so any leak is a LINK ERROR rather than a silent success.
#   libretro*.o         and the libretro VFS.
M16C_OWN_OBJS = $(M16CBUILD)/m16cmain.o \
                $(M16CBUILD)/shim.o \
                $(M16CBUILD)/gfx.o \
                $(M16CBUILD)/platform.o \
                $(M16CBUILD)/audio.o \
                $(M16CBUILD)/savedata.o \
                $(M16CBUILD)/m13store.o \
                $(M16CBUILD)/gba_library.o \
                $(M16CBUILD)/gba_fixture.o \
                $(M16CBUILD)/gba_filestream.o \
                $(M16CBUILD)/gba_compat.o \
                $(M16CBUILD)/gba_probe.o \
                $(M16CBUILD)/gba_bios.o \
                $(M16CBUILD)/gba_rom.o \
                $(M16CBUILD)/gba_save.o \
                $(M16CBUILD)/gba_savefile.o \
                $(M16CBUILD)/gba_savehdr.o \
                $(M16CBUILD)/gba_restore.o \
                $(M16CBUILD)/gba_restorefile.o \
                $(M16CBUILD)/gba_sramobs.o \
                $(M16CBUILD)/gba_m8map.o \
                $(M16CBUILD)/gba_m10map.o \
                $(M16CBUILD)/gba_exec.o \
                $(M16CBUILD)/gba_present.o \
                $(M16CBUILD)/gba_input.o \
                $(M16CBUILD)/gba_r7input.o \
                $(M16CBUILD)/gba_audio.o \
                $(M16CBUILD)/gba_session.o

M16C_GPSP_OBJS = $(M16CBUILD)/gpsp_cpu.o $(M16CBUILD)/gpsp_main.o \
                 $(M16CBUILD)/gpsp_gba_memory.o $(M16CBUILD)/gpsp_video.o \
                 $(M16CBUILD)/gpsp_sound.o $(M16CBUILD)/gpsp_serial.o \
                 $(M16CBUILD)/gpsp_serial_proto.o $(M16CBUILD)/gpsp_rfu.o \
                 $(M16CBUILD)/gpsp_gbp.o $(M16CBUILD)/gpsp_cheats.o \
                 $(M16CBUILD)/gpsp_savestate.o $(M16CBUILD)/gpsp_input.o

M16COBJS = $(M16C_OWN_OBJS) $(M16C_GPSP_OBJS)

# ---- offline structural check -----------------------------------------------
#
# M13C's three sources plus the five units M16 touched. Runs on a workstation
# with NO toolchain at all.
m16c-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m16cgpsp/main.c
	@$(PYTHON) tools/m8_syntax_check.py apps/m13bgpsp/m13b_picker.inc
	@$(PYTHON) tools/m8_syntax_check.py apps/m16cgpsp/m16c_splash_art.inc
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_sramobs.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_restore.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_savehdr.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_restorefile.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_session.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_save.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_filestream.c
	@$(PYTHON) tools/m8_syntax_check.py adapters/gba/gba_r7input.c
	@$(PYTHON) tools/m8_syntax_check.py runtime/shim.c

# ---- THE HOST-ONLY R7 ALIAS PROOF -------------------------------------------
#
# ***** THE ALIASES ARE PROVED EXHAUSTIVELY, AND NOT ONE BYTE OF IT SHIPS.
# ***** R7's margin above the 2,000-byte floor is small, so no self-test table
# is linked into the PS5 image. tools/r7_input_equiv.c instead sweeps ALL
# 2,097,152 reportable pad words on a workstation and compares the shipping
# function against a specification written by hand from the R7 requirement --
# which is a test, whereas comparing it against the same source would be
# circular.
#
# ***** IT RE-PROVES THE FROZEN INVARIANT BEFORE IT PROVES THE ALIASES. *****
# Section 1 asserts m8_input_selftest() == 0, so M8_ST_UNIQUE (the mapping table
# is injective) and M8_ST_UNMAPPED (SQUARE/TRIANGLE/L2/R2/L3/R3 map to nothing)
# must still hold VERBATIM. Nothing was deleted, disabled or weakened to let the
# aliases through; had R7 edited the table, this target fails first and loudest.
# Section 8 then adds what M8_ST_UNIQUE cannot express once aliasing is allowed
# at all: the PERMITTED equivalence classes, encoded as data, with every other
# duplicate relationship REJECTED.
#
# The harness #includes the production .c files verbatim, so the code under test
# is the code that ships. It pre-defines COMMON_H to neutralise gpsp/common.h,
# which is why -I gpsp is passed: the preprocessor must FIND the header in order
# to skip it. THAT IS A BUILD RULE ONLY -- no shipping source is modified to
# suit the harness and no test-only #ifdef exists in any production file.
# r7_input_equiv appears in NO object list and no payload rule builds it.
r7-input-equiv: | $(M16CBUILD)
	@$(HOSTCC) -std=c11 -O2 -Wall -Wextra -I adapters/gba -I gpsp \
	    -o $(M16CBUILD)/r7_input_equiv tools/r7_input_equiv.c
	@$(M16CBUILD)/r7_input_equiv

# ---- THE SAVE/RESTORE GATE, IN M16C's OWN CONFIGURATION ---------------------
#
# ***** THE SAME HARNESS SOURCE AS m12c-equiv, COMPILED THE WAY M16C COMPILES
# ***** THE ADAPTERS IT TESTS.
#
# WHY A SECOND TARGET RATHER THAN A CHANGE TO THE FIRST. m12c-equiv builds
# tools/gba_restore_equiv.c with NO session define, which is exactly how M13C,
# M12C, M12B and M11 build gba_savehdr.o, gba_restore.o and gba_restorefile.o.
# Its verdicts are therefore the FROZEN-MILESTONE verdicts and they must never
# move -- including the one that refuses a FLASH64 save under a live UNKNOWN.
# Editing m12c-equiv to expect the new behaviour would have destroyed the only
# gate that can detect the M16 rule leaking into a frozen image.
#
# M16C IS DIFFERENT AND THE DIFFERENCE IS THE POINT. $(M16CFLAGS) and
# $(M16C_GPSPFLAGS) both carry $(M16C_SESSION_DEFINE), so the five shared
# adapters are compiled for M16C with LUAPORT_SESSION_REUSE DEFINED. Everything
# guarded by that macro ships in M16C and in NO other image. Until this target
# existed, that guarded code had NO regression gate at all: m12c-equiv compiles
# it out by construction and cannot see it.
#
# ***** THE TWO TARGETS TOGETHER ARE THE GUARD. ***** One proves the rule is
# PRESENT where it belongs, the other proves it is ABSENT everywhere else. A
# change that satisfied only one of them would be exactly the defect each exists
# to catch.
#
# ***** NO PRODUCTION LOGIC IS DUPLICATED IN THE TEST. ***** The harness
# #includes the shipping .c files verbatim, so this runs the code that ships;
# the #ifdefs inside the harness select EXPECTATIONS, not behaviour, and no
# test-only #ifdef exists in any production source.
#
# -I gpsp IS THE SAME BUILD-RULE-ONLY DEVICE m12c-equiv uses: the harness
# pre-defines COMMON_H so gpsp/common.h expands to nothing, but the preprocessor
# must still FIND the header in order to skip it. Nothing is copied out of gpsp/
# and no gpSP source is compiled here.
#
# A SEPARATE OUTPUT NAME, IN M16C's OWN BUILD DIRECTORY. The two binaries are
# built from one source with different flags and must never overwrite one
# another -- running the wrong one would silently report the wrong
# configuration's verdicts. gba_restore_equiv_sr appears in NO object list and
# no payload rule builds it.
m16c-save-equiv: | $(M16CBUILD)
	@$(HOSTCC) -std=c11 -O2 -Wall -Wextra -I adapters/gba -I gpsp \
	    $(M16C_SESSION_DEFINE) \
	    -o $(M16CBUILD)/gba_restore_equiv_sr tools/gba_restore_equiv.c
	@$(M16CBUILD)/gba_restore_equiv_sr

# ---- LUAport-owned objects, runtime side ------------------------------------
#
# ***** apps/m13bgpsp/m13b_picker.inc IS A PREREQUISITE OF m16cmain.o. *****
# It is textually included, so it is a dependency exactly like a header. Without
# it here, a change to the picker math would leave a stale m16cmain.o in place
# while m13b-equiv reported the new behaviour green -- the two would silently
# describe different code.
#
# apps/m16cgpsp/m16c_splash_art.inc is named for exactly the same reason. It is
# generated by tools/mkart.py and COMMITTED, so `make m16c` never needs a PNG
# decoder; but regenerating the portrait must not leave a stale object behind.
$(M16CBUILD)/m16cmain.o: apps/m16cgpsp/main.c \
                         apps/m13bgpsp/m13b_picker.inc \
                         apps/m16cgpsp/m16c_splash_art.inc \
                         adapters/gba/m13store.h \
                         adapters/gba/gba_library.h \
                         adapters/gba/gba_rom.h \
                         adapters/gba/gba_probe.h \
                         adapters/gba/gba_bios.h \
                         adapters/gba/gba_save.h \
                         adapters/gba/gba_savefile.h \
                         adapters/gba/gba_restore.h \
                         adapters/gba/gba_restorefile.h \
                         adapters/gba/gba_sramobs.h \
                         adapters/gba/gba_session.h \
                         adapters/gba/gba_exec.h \
                         adapters/gba/gba_present.h \
                         adapters/gba/gba_input.h \
                         adapters/gba/gba_r7input.h \
                         adapters/gba/gba_m8map.h \
                         adapters/gba/gba_m10map.h \
                         adapters/gba/gba_audio.h runtime/audio.h \
                         runtime/savedata.h \
                         $(M16CHEADERS) | $(M16CBUILD)
	$(CC) $(M16CFLAGS) -c $< -o $@

# runtime/shim.h is an EXPLICIT prerequisite because the guarded arena_mark()
# and arena_rewind() declarations live there, not only in the .c.
$(M16CBUILD)/shim.o: runtime/shim.c runtime/shim.h $(M16CHEADERS) \
                     | $(M16CBUILD)
	$(CC) $(M16CFLAGS) -c $< -o $@

$(M16CBUILD)/gfx.o: runtime/gfx.c $(M16CHEADERS) | $(M16CBUILD)
	$(CC) $(M16CFLAGS) -c $< -o $@

$(M16CBUILD)/platform.o: runtime/platform.c $(M16CHEADERS) | $(M16CBUILD)
	$(CC) $(M16CFLAGS) -c $< -o $@

$(M16CBUILD)/audio.o: runtime/audio.c runtime/audio.h $(M16CHEADERS) \
                      | $(M16CBUILD)
	$(CC) $(M16CFLAGS) -c $< -o $@

# ***** M12A's SAVEDATA LAYER, FROM UNMODIFIED, FROZEN SOURCE. *****
# It is the ONLY object that names sceSaveData*, and m16c-verify asserts that.
# ***** ITS PRESENCE IS THE POINT OF THIS MILESTONE. ***** m16bdiag-verify
# asserts this object is ABSENT. m16c-verify asserts it is PRESENT.
$(M16CBUILD)/savedata.o: runtime/savedata.c runtime/savedata.h \
                         $(M16CHEADERS) | $(M16CBUILD)
	$(CC) $(M16CFLAGS) -c $< -o $@

$(M16CBUILD)/m13store.o: adapters/gba/m13store.c \
                         adapters/gba/m13store.h runtime/core.h | $(M16CBUILD)
	$(CC) $(M16CFLAGS) -c $< -o $@

$(M16CBUILD)/gba_library.o: adapters/gba/gba_library.c \
                            adapters/gba/gba_library.h \
                            adapters/gba/m13store.h | $(M16CBUILD)
	$(CC) $(M16CFLAGS) -c $< -o $@

$(M16CBUILD)/gba_present.o: adapters/gba/gba_present.c \
                            adapters/gba/gba_present.h \
                            runtime/core.h | $(M16CBUILD)
	$(CC) $(M16CFLAGS) -c $< -o $@

# ***** M12B's WRITER. THE RUNTIME HALF, SO IT GETS THE RUNTIME FLAGS. *****
# THIS IS THE ONLY OBJECT IN THE IMAGE THAT MAY CALL fwrite.
$(M16CBUILD)/gba_savefile.o: adapters/gba/gba_savefile.c \
                             adapters/gba/gba_savefile.h \
                             adapters/gba/gba_save.h \
                             runtime/savedata.h | $(M16CBUILD)
	$(CC) $(M16CFLAGS) -c $< -o $@

# ***** M12C's VALIDATOR. ALSO THE RUNTIME HALF. *****
# It opens both files "rb" and NEVER writes.
$(M16CBUILD)/gba_restorefile.o: adapters/gba/gba_restorefile.c \
                                adapters/gba/gba_restorefile.h \
                                adapters/gba/gba_restore.h \
                                adapters/gba/gba_savehdr.h \
                                adapters/gba/gba_save.h | $(M16CBUILD)
	$(CC) $(M16CFLAGS) -c $< -o $@

# ---- gpSP-facing adapters ---------------------------------------------------
#
# ***** THESE GET THE gpSP FLAGS AND NOTHING ELSE. ***** They include
# gpsp/common.h, so they must see the same gpSP configuration every other gpSP
# object sees. And they MUST NOT get $(M16C_INCLUDES), whose -Iruntime would put
# runtime/core.h on the include path: gpsp/common.h:100 typedefs u64 as
# `unsigned long long` and runtime/core.h:30 as `unsigned long` -- DISTINCT
# types -- so a TU that saw both would not compile.
$(M16CBUILD)/gba_fixture.o: adapters/gba/gba_fixture.c | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

$(M16CBUILD)/gba_filestream.o: adapters/gba/gba_filestream.c | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

$(M16CBUILD)/gba_compat.o: adapters/gba/gba_compat.c | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

$(M16CBUILD)/gba_probe.o: adapters/gba/gba_probe.c adapters/gba/gba_probe.h \
                          | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

$(M16CBUILD)/gba_bios.o: adapters/gba/gba_bios.c adapters/gba/gba_bios.h \
                         | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

$(M16CBUILD)/gba_rom.o: adapters/gba/gba_rom.c adapters/gba/gba_rom.h \
                        | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

# M12B's SAVE ADAPTER. It owns the identity, the class, the size policy, the
# hashes and the byte tap. Its M16 addition -- gba_save_unlatch() -- is behind
# #ifdef LUAPORT_SESSION_REUSE, which is why it is built with the define here
# and without it in every frozen milestone.
$(M16CBUILD)/gba_save.o: adapters/gba/gba_save.c adapters/gba/gba_save.h \
                         | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

# M12C's HEADER READER. Pure arithmetic over an unsigned char array.
$(M16CBUILD)/gba_savehdr.o: adapters/gba/gba_savehdr.c \
                            adapters/gba/gba_savehdr.h | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

# ***** M12C's RESTORE ADAPTER. THE SOLE WRITER OF gamepak_backup[]. *****
$(M16CBUILD)/gba_restore.o: adapters/gba/gba_restore.c \
                            adapters/gba/gba_restore.h | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

# ***** M12C's SRAM OBSERVER. A READER, AND ONLY A READER. *****
$(M16CBUILD)/gba_sramobs.o: adapters/gba/gba_sramobs.c \
                            adapters/gba/gba_sramobs.h | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

$(M16CBUILD)/gba_m8map.o: adapters/gba/gba_m8map.c adapters/gba/gba_m8map.h \
                          | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

$(M16CBUILD)/gba_m10map.o: adapters/gba/gba_m10map.c \
                           adapters/gba/gba_m10map.h | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

$(M16CBUILD)/gba_exec.o: adapters/gba/gba_exec.c adapters/gba/gba_exec.h \
                         | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

$(M16CBUILD)/gba_input.o: adapters/gba/gba_input.c adapters/gba/gba_input.h \
                          | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

# ***** R7's GAMEPLAY BUTTON-ALIAS LAYER, AND IT IS M16C-ONLY. *****
#
# NOTE THE FLAGS: $(M16CFLAGS), **NOT** $(M16C_GPSPFLAGS). gba_input.c needs the
# gpSP flags because it opens with #include "common.h" and writes REG_P1. This
# unit includes NO gpSP header, writes nothing and touches no emulator state --
# it rewrites a pad word and calls m8_input_from_pad(). Compiling it on the
# LUAport-side flags is therefore not a shortcut but a STATEMENT, and the
# closure and relocation gates in m16c-verify check it like every other object:
# the alias layer cannot reach the gpSP surface even by accident.
#
# adapters/gba/gba_input.h IS A PREREQUISITE AND adapters/gba/gba_input.c IS
# NOT. This object uses the frozen header's bit names and calls into the frozen
# object; it does not compile that source, and R7 does not edit it.
$(M16CBUILD)/gba_r7input.o: adapters/gba/gba_r7input.c \
                            adapters/gba/gba_r7input.h \
                            adapters/gba/gba_input.h | $(M16CBUILD)
	$(CC) $(M16CFLAGS) -c $< -o $@

# NOTE THE ABSENT PREREQUISITE: runtime/audio.h is deliberately NOT listed,
# because gba_audio.c deliberately does NOT include it.
$(M16CBUILD)/gba_audio.o: adapters/gba/gba_audio.c adapters/gba/gba_audio.h \
                          | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

# ***** THE SESSION ADAPTER, UNCHANGED FROM M16-0 AND M16-1. *****
# gba_session.h refuses to compile without the session define -- it has an
# #error at :22-25 -- so this object cannot be built into a frozen milestone
# even by accident.
$(M16CBUILD)/gba_session.o: adapters/gba/gba_session.c \
                            adapters/gba/gba_session.h \
                            adapters/gba/gba_save.h \
                            adapters/gba/gba_restore.h \
                            adapters/gba/gba_sramobs.h \
                            adapters/gba/gba_input.h \
                            adapters/gba/gba_audio.h \
                            adapters/gba/gba_m10map.h \
                            adapters/gba/gba_rom.h \
                            adapters/gba/gba_probe.h \
                            adapters/gba/gba_bios.h | $(M16CBUILD)
	$(CC) $(M16C_GPSPFLAGS) -c $< -o $@

# ---- gpSP core objects ------------------------------------------------------
#
# Compiled IN PLACE from gpsp/ with LUAport-owned flags, IDENTICAL to M13C's.
# gpsp/** is never modified, its own Makefile is never invoked, and it does NOT
# receive $(M16C_SESSION_DEFINE).

$(M16CBUILD)/gpsp_cpu.o: gpsp/cpu.cc | $(M16CBUILD)
	$(CXX) $(M16C_CORECXXFLAGS) -c $< -o $@

$(M16CBUILD)/gpsp_video.o: gpsp/video.cc | $(M16CBUILD)
	$(CXX) $(M16C_CORECXXFLAGS) -c $< -o $@

$(M16CBUILD)/gpsp_main.o: gpsp/main.c | $(M16CBUILD)
	$(CC) $(M16C_COREFLAGS) -c $< -o $@

$(M16CBUILD)/gpsp_gba_memory.o: gpsp/gba_memory.c | $(M16CBUILD)
	$(CC) $(M16C_COREFLAGS) -c $< -o $@

$(M16CBUILD)/gpsp_sound.o: gpsp/sound.c | $(M16CBUILD)
	$(CC) $(M16C_COREFLAGS) -c $< -o $@

$(M16CBUILD)/gpsp_serial.o: gpsp/serial.c | $(M16CBUILD)
	$(CC) $(M16C_COREFLAGS) -c $< -o $@

$(M16CBUILD)/gpsp_serial_proto.o: gpsp/serial_proto.c | $(M16CBUILD)
	$(CC) $(M16C_COREFLAGS) -c $< -o $@

$(M16CBUILD)/gpsp_rfu.o: gpsp/rfu.c | $(M16CBUILD)
	$(CC) $(M16C_COREFLAGS) -c $< -o $@

$(M16CBUILD)/gpsp_gbp.o: gpsp/gbp.c | $(M16CBUILD)
	$(CC) $(M16C_COREFLAGS) -c $< -o $@

$(M16CBUILD)/gpsp_cheats.o: gpsp/cheats.c | $(M16CBUILD)
	$(CC) $(M16C_COREFLAGS) -c $< -o $@

$(M16CBUILD)/gpsp_savestate.o: gpsp/savestate.c | $(M16CBUILD)
	$(CC) $(M16C_COREFLAGS) -c $< -o $@

$(M16CBUILD)/gpsp_input.o: gpsp/input.c | $(M16CBUILD)
	$(CC) $(M16C_COREFLAGS) -c $< -o $@

m16c-objs: $(M16COBJS)

# ---- link -------------------------------------------------------------------

$(M16CELF): $(M16COBJS) runtime/linker.ld | $(M16CBUILD)
	$(CC) $(BASEFLAGS) $(M16CLDFLAGS) -o $@ $(M16COBJS)

m16c-link: $(M16COBJS) | $(M16CBUILD)
	@$(CC) $(BASEFLAGS) $(M16CLDFLAGS) -o $(M16CELF) $(M16COBJS) \
	     > $(M16CBUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to .[^']+'" $(M16CBUILD)/link.log \
	            | sed -E "s/.*to .//; s/'$$//" | sort -u); \
	 if [ $$status -ne 0 ]; then \
	     cat $(M16CBUILD)/link.log; \
	     echo "M16C LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M16C LINK FAILED: undefined references reported despite exit 0"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "M16C LINK OK: linker exit 0, no undefined references"

$(M16CBIN): $(M16CELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M16CELF) $@ \
	   --map $(M16CBUILD)/$(M16CTARGET).map --budget $(JIT_LIMIT) --arena 4194304

m16c: $(M16CBIN)

# THE TWO HARD CEILINGS, UNCHANGED AND UNRAISED. sizereport.py enforces the JIT
# budget ($(JIT_LIMIT) = 524,288) and tools/check_forbidden.py enforces the
# 2,097,152-byte .bss tripwire.
#
# ***** THE .bss PROJECTION. ***** M16C links M13C's object set with m13cmain.o
# replaced by m16cmain.o and gba_session.o added. gba_session.c holds no large
# array, and the app owns the same three that M13C's did: the 518,400 byte UI
# surface, the 77,280 byte GBA screen and the ~16.7 KB library table. The
# difference is in SCALARS and in rodata. If the measured figure moves
# materially, STOP and read the map -- DO NOT RAISE THE TRIPWIRE.
m16c-size: $(M16CELF) $(M16CBIN)
	@$(PYTHON) tools/sizereport.py $(M16CELF) $(M16CBIN) \
	   --map $(M16CBUILD)/$(M16CTARGET).map --budget $(JIT_LIMIT) --arena 4194304

# ---- verification -----------------------------------------------------------
#
# ***** THIS VERIFIER PROVES M13C's CLAIMS, PLUS THE LIFECYCLE INVARIANTS THAT
# ONLY EXIST BECAUSE THE PICKER COMES BACK, AND WEAKENS NOTHING. *****
#
# m13c-verify, m16diag-verify and m16bdiag-verify are NOT MODIFIED and still run
# and still prove their own images.

# The dynarec entry point stays forbidden. execute_arm is NOT forbidden -- it is
# gpSP's own interpreter and has been in gpsp_cpu.o since M1. It is CONFINED.
M16C_FORBIDDEN_DYNAREC = execute_arm_translate dynarec_enable

# Every capability the launcher claims, as an object that must be on the line.
# ***** THE LAST FIVE ARE THE ONES M16-1 FORBIDS. ***** Listing them here is the
# mechanical statement that M16-2 does not inherit the diagnostic restriction.
M16C_REQUIRED_OBJS = m16cmain.o gba_session.o shim.o m13store.o gba_library.o \
                     gba_rom.o gba_filestream.o gba_compat.o gba_fixture.o \
                     gba_bios.o gba_probe.o gba_exec.o gba_m8map.o \
                     gba_m10map.o gba_present.o gba_input.o gba_r7input.o \
                     gba_audio.o \
                     audio.o gfx.o platform.o gba_save.o gba_restore.o \
                     gba_sramobs.o \
                     savedata.o gba_savefile.o gba_savehdr.o gba_restorefile.o

# The save/restore stack, called out separately so the PRESENCE gate can name
# the exact four objects m16bdiag-verify asserts are ABSENT.
M16C_SAVE_OBJS = savedata.o gba_savefile.o gba_savehdr.o gba_restorefile.o

# ***** WHO MAY NAME gamepak_backup, BY ROLE. ***** M13C's three role-named
# lists, unchanged. gba_session.o is NOT among them: gba_session_end() reaches
# the save state through gba_save_unlatch() and gba_restore_disarm(), never
# through the array itself, and this gate is what holds that to be true.
M16C_BACKUP_INIT_WRITERS   = gba_rom.o
M16C_BACKUP_RESTORE_WRITER = gba_restore.o
M16C_BACKUP_READERS        = gba_save.o gba_sramobs.o
M16C_BACKUP_NAMERS = $(M16C_BACKUP_INIT_WRITERS) \
                     $(M16C_BACKUP_RESTORE_WRITER) \
                     $(M16C_BACKUP_READERS)

# ***** THE WRITE CAPABILITY IS ALLOWED THROUGH EXACTLY ONE OBJECT. *****
M16C_FORBIDDEN_FWRITE = fwrite fputc fputs fprintf
M16C_WRITER_OBJ       = gba_savefile.o

# The savedata layer must really have reached the image, or a clean session
# would be VACUOUS.
M16C_REQUIRED_NAMES = libSceSaveData.sprx sceSaveDataUmount sceKernelMkdir \
                      sceKernelStat sceKernelUsleep SLUS-20268 SLES-50366

# ***** THE VOCABULARY THAT MAY NOT SURVIVE INTO THE LAUNCHER. ***** M13C's
# gate, inherited verbatim: M16C is a product, not a staged fixture.
M16C_FORBIDDEN_WORDS = RUNG STAGE MILESTONE

# The seven reset primitives the boundary is built from.
M16C_RESET_PRIMITIVES = arena_mark arena_rewind gba_save_unlatch \
                        gba_restore_disarm gba_rfile_inuse gba_session_end \
                        gba_session_probe_relaunch

# ***** HOST COMPILER PRESENCE, CHECKED BEFORE ANY HARNESS IS BUILT. *****
# m12c-equiv is the ONLY gate in the shipping chain that exercises save and
# restore, and it needs a HOST compiler. IT MUST NEVER DEGRADE TO A SKIP.
m16c-hostcc:
	@if command -v $(firstword $(HOSTCC)) >/dev/null 2>&1; then \
	    echo "HOSTCC = $(HOSTCC) (present)"; \
	 else \
	    echo "M16C VERIFY FAILED: no host compiler -- HOSTCC=$(HOSTCC) is not executable."; \
	    echo "  m12c-equiv is the save/restore regression gate and is MANDATORY."; \
	    echo "  M16-2 SHIPS THE REAL WRITER, so this gate matters MORE here than"; \
	    echo "  it did in the diagnostics, which could not write at all."; \
	    echo "  Re-run with an available compiler, for example:"; \
	    echo "      make m16c-verify HOSTCC=gcc"; \
	    echo "      make m16c-verify HOSTCC=\"zig cc\""; \
	    exit 1; \
	 fi

# ***** BOTH SAVE GATES ARE PREREQUISITES, AND NEITHER SUBSTITUTES FOR THE
# ***** OTHER. m12c-equiv proves the frozen-milestone behaviour is untouched;
# m16c-save-equiv proves M16C's own session-reuse configuration behaves. The
# guarded FLASH64 rule is invisible to the first by construction, so without the
# second it would ship with no host coverage whatsoever.
m16c-verify: m16c-hostcc m13b-equiv m12c-equiv m16c-save-equiv r7-input-equiv \
             $(M16CELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M16CELF)
	@echo "== relocations (NON-MASKING pipeline) =="
	@$(PYTHON) tools/verify_reloc.py $(M16CELF) $(M16COBJS) \
	     > $(M16CBUILD)/reloc.log 2>&1; \
	 st=$$?; \
	 tail -12 $(M16CBUILD)/reloc.log; \
	 if [ $$st -ne 0 ]; then \
	     echo "M16C VERIFY FAILED: the relocation check exited $$st."; \
	     echo "  Full output: $(M16CBUILD)/reloc.log"; \
	     exit $$st; \
	 fi
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M16COBJS) | tail -4
	@echo "== closure, dynarec and frontend exclusion =="
	@$(PYTHON) tools/check_closure.py --core gpsp $(M16COBJS)
	@echo "== forbidden symbols in the linked image (.bss tripwire included) =="
# ***** THE FORBIDDEN-SYMBOL CHECKER IS INVOKED UNCHANGED. ***** Same script,
# same two arguments, same semantics as M13C, M16-0 and M16-1. It is not
# wrapped, not filtered and not given an exception list.
	@$(PYTHON) tools/check_forbidden.py $(M16CELF) $(M16COBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M16CELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M16C VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== ***** EVERY ADVERTISED CAPABILITY IS LINKED ***** =="
	@missing=""; \
	 for want in $(M16C_REQUIRED_OBJS); do \
	     found=0; \
	     for o in $(M16COBJS); do \
	         case "$$o" in */$$want) found=1;; esac; \
	     done; \
	     if [ $$found -eq 0 ]; then missing="$$missing $$want"; fi; \
	 done; \
	 if [ -n "$$missing" ]; then \
	     echo "M16C VERIFY FAILED: a required object is NOT on the link"; \
	     echo "  line:$$missing"; \
	     exit 1; \
	 fi; \
	 echo "all twenty-eight required objects are PRESENT"
	@echo "== ***** THE REAL SAVE/RESTORE STACK IS LINKED ***** =="
# ***** THIS IS THE GATE THAT SEPARATES THE LAUNCHER FROM THE DIAGNOSTICS.
# ***** m16bdiag-verify asserts these four objects are ABSENT, because M16-1 was
# still learning whether the save identity follows the cartridge and must not
# have been able to write. Both M16-0 and M16-1 have PASSED, so M16-2 carries
# the writer -- and an M16C built by copying the M16B block would link a
# launcher that cannot persist anything while every other gate stayed green.
	@miss=""; \
	 for want in $(M16C_SAVE_OBJS); do \
	     found=0; \
	     for o in $(M16COBJS); do \
	         case "$$o" in */$$want) found=1;; esac; \
	     done; \
	     if [ $$found -eq 0 ]; then miss="$$miss $$want"; fi; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M16C VERIFY FAILED: the persistent save stack is INCOMPLETE:"; \
	     echo " $$miss"; \
	     echo "  M16-2 is the LAUNCHER. It must link the M13C save/restore"; \
	     echo "  stack in full. Do NOT inherit M16-1's no-save-writer rule --"; \
	     echo "  that restriction belongs to a diagnostic that was still"; \
	     echo "  learning, and both M16 diagnostics have now passed."; \
	     exit 1; \
	 fi; \
	 echo "savedata.o, gba_savefile.o, gba_savehdr.o and gba_restorefile.o are"; \
	 echo "  all on the link line -- M16-1's restriction is NOT inherited"
	@miss=""; \
	 for s in gba_savefile_commit gba_savehdr_parse plat_savedata_init \
	          gba_restorefile_run; do \
	     $(NM) $(M16CELF) 2>/dev/null | grep -qw "$$s" || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M16C VERIFY FAILED: a save entry point is not defined in the"; \
	     echo "  image:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "the commit, the header parser, the savedata layer and the restore"; \
	 echo "  reader are all DEFINED in the image"
	@echo "== ***** gba_session.o IS LINKED AND REALLY TEARS DOWN ***** =="
	@case " $(M16COBJS) " in \
	   *" $(M16CBUILD)/gba_session.o "*) ;; \
	   *) echo "M16C VERIFY FAILED: gba_session.o is not on the link line."; \
	      echo "  Without it there is no session boundary and the picker"; \
	      echo "  could not come back."; \
	      exit 1;; \
	 esac; \
	 echo "gba_session.o is linked into M16C"
	@if ! $(NM) $(M16CELF) 2>/dev/null | grep -qE " [Tt] memory_term$$"; then \
	     echo "M16C VERIFY FAILED: memory_term is not defined in the image."; \
	     exit 1; \
	 fi; \
	 echo "memory_term is DEFINED in the image"
	@if ! $(NM) -u $(M16CBUILD)/gba_session.o 2>/dev/null \
	        | grep -qw "memory_term"; then \
	     echo "M16C VERIFY FAILED: gba_session.o does not REFERENCE"; \
	     echo "  memory_term. The teardown would close no file and free no"; \
	     echo "  buffer, and every session after the first would inherit the"; \
	     echo "  previous cartridge's demand-paging handle."; \
	     exit 1; \
	 fi; \
	 echo "gba_session.o REFERENCES memory_term (the ROM file is really closed)"
	@echo "== ***** THE SEVEN RESET PRIMITIVES EXIST ***** =="
	@miss=""; \
	 for s in $(M16C_RESET_PRIMITIVES); do \
	     $(NM) $(M16CELF) 2>/dev/null | grep -qE " [Tt] $$s$$" \
	         || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M16C VERIFY FAILED: a reset primitive is missing:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "all seven M16 reset primitives are defined"
	@if ! $(NM) -u $(M16CBUILD)/m16cmain.o 2>/dev/null \
	        | grep -qw "gba_session_probe_relaunch"; then \
	     echo "M16C VERIFY FAILED: the relaunch invariant probe is never"; \
	     echo "  called. Every boundary would pass VACUOUSLY."; \
	     exit 1; \
	 fi; \
	 echo "m16cmain.o drives the relaunch invariant probe"
	@echo "== ***** THE SAVE COMMIT PRECEDES gba_session_end() ***** =="
# ***** THE SINGLE MOST IMPORTANT ORDERING CLAIM IN THIS MILESTONE. *****
# Step 4 of gba_session_end() is gba_save_unlatch(), and gba_session.c:115-126 is
# explicit: the commit path builds /savedata0/gba/<ID>.sav from the identity the
# unlatch destroys. An out-of-order controller gets GBA_SAVEFILE_ENOID -- a
# refusal rather than a misfile, but THE SAVE IS STILL LOST.
#
# ***** IT IS CHECKED AS A CALL-GRAPH FACT, NOT AS A LINE-ORDER ONE. *****
# gba_session_end() is called from inside m16c_boundary(), which is DEFINED
# ABOVE _start(), so comparing raw line numbers of the two calls would compare
# a definition against a use and would fail on a correct file. What is actually
# load-bearing is: gba_session_end() has exactly ONE call site, it is inside
# m16c_boundary(), and every call to m16c_boundary() from the session body comes
# AFTER the commit.
	@n=$$(grep -c "^[[:space:]]*gba_session_end();" apps/m16cgpsp/main.c); \
	 if [ "$$n" -ne 1 ]; then \
	     echo "M16C VERIFY FAILED: gba_session_end() has $$n call sites."; \
	     echo "  It must have EXACTLY ONE, inside m16c_boundary(), or the"; \
	     echo "  commit-before-unlatch ordering cannot be reasoned about at"; \
	     echo "  all."; \
	     exit 1; \
	 fi; \
	 bdef=$$(grep -n "^static int m16c_boundary" apps/m16cgpsp/main.c \
	           | head -1 | cut -d: -f1); \
	 send=$$(grep -n "^[[:space:]]*gba_session_end();" apps/m16cgpsp/main.c \
	           | head -1 | cut -d: -f1); \
	 if [ -z "$$bdef" ] || [ "$$send" -le "$$bdef" ]; then \
	     echo "M16C VERIFY FAILED: the single gba_session_end() call is not"; \
	     echo "  inside m16c_boundary()."; \
	     exit 1; \
	 fi; \
	 echo "  gba_session_end() has exactly one call site, inside m16c_boundary()"; \
	 commit=$$(grep -n "= gba_savefile_commit();" apps/m16cgpsp/main.c \
	             | head -1 | cut -d: -f1); \
	 lastb=$$(grep -n "m16c_boundary(ext," apps/m16cgpsp/main.c \
	            | tail -1 | cut -d: -f1); \
	 if [ -z "$$commit" ]; then \
	     echo "M16C VERIFY FAILED: the image never calls gba_savefile_commit()."; \
	     echo "  A launcher that cannot commit is VACUOUS."; \
	     exit 1; \
	 fi; \
	 if [ "$$lastb" -le "$$commit" ]; then \
	     echo "M16C VERIFY FAILED: the session-body boundary call at line"; \
	     echo "  $$lastb does NOT follow the commit at line $$commit."; \
	     echo "  gba_save_unlatch() would destroy the identity the commit"; \
	     echo "  needs and the save would be LOST."; \
	     exit 1; \
	 fi; \
	 echo "  the commit (line $$commit) precedes the session boundary call"; \
	 echo "  (line $$lastb) -- the unlatch can never run first"
	@echo "== ***** gba_session_end() PRECEDES arena_rewind() ***** =="
# arena_rewind() must come AFTER memory_term(), because memory_term() is what
# releases the gamepak buffers the rewind then reclaims. Both calls live in
# m16c_boundary(), so here line order IS the control-flow order.
	@send=$$(grep -n "^[[:space:]]*gba_session_end();" apps/m16cgpsp/main.c \
	           | head -1 | cut -d: -f1); \
	 arw=$$(grep -n "if (!arena_rewind(mark))" apps/m16cgpsp/main.c \
	          | head -1 | cut -d: -f1); \
	 if [ -z "$$arw" ]; then \
	     echo "M16C VERIFY FAILED: the boundary never calls arena_rewind()."; \
	     echo "  The arena would grow by one session's worth of buffers on"; \
	     echo "  every game and the launcher would die of exhaustion."; \
	     exit 1; \
	 fi; \
	 if [ "$$arw" -le "$$send" ]; then \
	     echo "M16C VERIFY FAILED: arena_rewind() at line $$arw does not"; \
	     echo "  follow gba_session_end() at line $$send."; \
	     exit 1; \
	 fi; \
	 echo "gba_session_end() (line $$send) precedes arena_rewind() (line $$arw)"
	@echo "== ***** THE RUNTIME TEARDOWN IS NOT THE SESSION RETURN ***** =="
# ***** THE WHOLE POINT OF M16-2, STATED AS A GATE. ***** M13C ends a game by
# tearing the runtime down and returning; that is why ending a game ends
# LUAport. M16C must NOT do that on a normal session return: video, the pad and
# the audio port stay up across every boundary, exactly as gba_session.h:98-101
# requires. m16c_teardown() may therefore appear ONLY on payload-exit paths and
# inside m16c_runtime_fatal() -- never anywhere in the session body.
	@n=$$(grep -c "^[[:space:]]*m16c_teardown();" apps/m16cgpsp/main.c); \
	 if [ "$$n" -ne 3 ]; then \
	     echo "M16C VERIFY FAILED: m16c_teardown() has $$n call sites,"; \
	     echo "  expected exactly 3: m16c_runtime_fatal(), the empty-library"; \
	     echo "  payload exit, and the step 459 payload exit."; \
	     exit 1; \
	 fi; \
	 s451=$$(grep -n "ext->step = 451;" apps/m16cgpsp/main.c \
	           | head -1 | cut -d: -f1); \
	 s459=$$(grep -n "ext->step = 459;" apps/m16cgpsp/main.c \
	           | head -1 | cut -d: -f1); \
	 if [ -z "$$s451" ] || [ -z "$$s459" ]; then \
	     echo "M16C VERIFY FAILED: step 451 or step 459 is missing, so the"; \
	     echo "  session body cannot be bounded."; \
	     exit 1; \
	 fi; \
	 bad=""; \
	 for l in $$(grep -n "^[[:space:]]*m16c_teardown();" apps/m16cgpsp/main.c \
	               | cut -d: -f1); do \
	     if [ "$$l" -ge "$$s451" ] && [ "$$l" -le "$$s459" ]; then \
	         bad="$$bad $$l"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M16C VERIFY FAILED: m16c_teardown() is called INSIDE the"; \
	     echo "  session body, at line(s):$$bad"; \
	     echo "  A normal session return must run the SESSION boundary"; \
	     echo "  (gba_session_end + arena_rewind), NOT the runtime teardown."; \
	     echo "  Tearing video, the pad and the audio port down to show a"; \
	     echo "  menu is exactly what M16-2 exists to stop doing."; \
	     exit 1; \
	 fi; \
	 echo "m16c_teardown() has 3 call sites and NONE is in the session body"; \
	 echo "  (lines $$s451..$$s459) -- normal session return uses the boundary"
	@echo "== ***** end_reason IS INITIALISED PER SESSION ***** =="
# ***** A STALE end_reason IS A SILENT DATA-LOSS BUG. ***** end_reason is the
# first of the seven clean terms. If session N+1 inherited session N's OPERATOR
# value, a session that died in the frame loop could still read `clean` and
# commit bytes produced by a machine that was already misbehaving. The record is
# zeroed and RE-SEEDED to WATCHDOG at the top of every session, verified fresh,
# and then the local is seeded FROM the verified record rather than from the
# literal -- so the two can never drift apart.
	@miss=""; \
	 for s in "m16c_session_reset();" "m16c_session_is_fresh()" \
	          "unsigned end_reason = m16c_rec.end_reason;" \
	          "m16c_rec.end_reason = M16C_END_WATCHDOG;"; do \
	     grep -qF "$$s" apps/m16cgpsp/main.c || miss="$$miss [$$s]"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M16C VERIFY FAILED: the per-session end_reason initialisation"; \
	     echo "  is incomplete:$$miss"; \
	     echo "  Session N+1 could inherit session N's ending and commit on"; \
	     echo "  the strength of it."; \
	     exit 1; \
	 fi; \
	 echo "the session record is reset, re-seeded to WATCHDOG, verified fresh"; \
	 echo "  and end_reason is seeded FROM the verified record"
	@echo "== ***** THE SEVEN-TERM CLEAN VERDICT IS INTACT ***** =="
# ***** REMOVING A TERM IS HOW A LAUNCHER LEARNS TO COMMIT GARBAGE. *****
# Each of these answers a different question and any one failing makes the
# others unsafe to believe, which is why they are ANDed. SPEED IS NOT A TERM and
# returning to the picker is NOT a term -- returning to the picker is not
# evidence about the session that just ended.
	@miss=""; \
	 for t in "end_reason == M16C_END_OPERATOR" \
	          "elapsed_us > 0u" \
	          "frames_emulated == frames_presented" \
	          "frames_emulated > 0u" \
	          "aud.overruns == 0u" \
	          "skip_seen == 0u" \
	          "arena_end == arena_after_warm"; do \
	     grep -qF "$$t" apps/m16cgpsp/main.c || miss="$$miss [$$t]"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M16C VERIFY FAILED: a clean-verdict term was removed:$$miss"; \
	     echo "  The verdict is M13C's SEVEN terms, verbatim. A launcher with"; \
	     echo "  six of them can commit a save from a session it cannot"; \
	     echo "  vouch for."; \
	     exit 1; \
	 fi; \
	 n=$$(sed -n "/clean = (end_reason == M16C_END_OPERATOR/,/arena_end == arena_after_warm);/p" \
	          apps/m16cgpsp/main.c | grep -c "&&"); \
	 if [ "$$n" -ne 6 ]; then \
	     echo "M16C VERIFY FAILED: the clean conjunction has $$n '&&'"; \
	     echo "  operators, expected 6 (seven ANDed terms)."; \
	     exit 1; \
	 fi; \
	 echo "all seven clean-verdict terms are present and ANDed (6 operators)"
	@echo "== ***** THE COMMIT IS GATED ON clean AND modified ***** =="
	@if ! grep -qF "if (clean && modified)" apps/m16cgpsp/main.c; then \
	     echo "M16C VERIFY FAILED: the commit is no longer gated on"; \
	     echo "  (clean && modified). An unclean session would write, or a"; \
	     echo "  session that changed nothing would open a write window for"; \
	     echo "  no reason."; \
	     exit 1; \
	 fi; \
	 echo "the commit is gated on (clean && modified)"
	@echo "== ***** NO AUTOMATIC SESSION-DURATION EXIT ***** =="
# M13B-5 ends its run on a WALL CLOCK after 30 seconds, because M13B-5 is a
# measurement. A launcher must not: hardware showed that window closing Metroid
# Fusion before a room could be played through. Checked at the SOURCE -- a
# deleted code path leaves nothing behind for a symbol or string test to find.
	@if grep -Eq "M16C_SESSION_SECONDS|run_max_us|M16C_END_SESSION" \
	        apps/m16cgpsp/main.c; then \
	     echo "M16C VERIFY FAILED: a session-duration terminator survives in"; \
	     echo "  apps/m16cgpsp/main.c. Normal gameplay must end on the"; \
	     echo "  operator's chord, NEVER on elapsed time."; \
	     exit 1; \
	 fi; \
	 echo "no session-duration terminator: the frame loop cannot time out"
	@echo "== ***** THE WATCHDOG IS 864,000 ITERATIONS ***** =="
# RETAINED, NOT REMOVED, NOT RESIZED. Its length is pinned by _Static_assert in
# the source, so a drift is a COMPILE error; this proves the assert itself is
# still there and still bounds the loop.
	@if ! grep -q "iter < M16C_PLAY_MAX_ITERS" apps/m16cgpsp/main.c; then \
	     echo "M16C VERIFY FAILED: the emergency watchdog no longer bounds"; \
	     echo "  the frame loop. It must be RETAINED as a backstop."; \
	     exit 1; \
	 fi
	@if ! grep -qF "M16C_PLAY_MAX_ITERS == 864000u" apps/m16cgpsp/main.c; then \
	     echo "M16C VERIFY FAILED: the 864,000-iteration static assert is"; \
	     echo "  gone. 240 minutes at a 60 Hz ceiling is the figure and it is"; \
	     echo "  not this milestone's to change."; \
	     exit 1; \
	 fi; \
	 echo "the watchdog still bounds the loop at 864,000 iterations (240 min)"
	@if ! grep -qE "define[[:space:]]+M16C_EXIT_HOLD[[:space:]]+15u" \
	        apps/m16cgpsp/main.c; then \
	     echo "M16C VERIFY FAILED: the return chord hold is no longer 15"; \
	     echo "  frames. The hold is what stops a brush against the pad from"; \
	     echo "  ending a session, and it is M13C's figure, unchanged."; \
	     exit 1; \
	 fi; \
	 echo "the return chord is still all four shoulders HELD for 15 frames"
	@echo "== ***** R7's ALIAS LAYER IS REALLY IN THE GAMEPLAY PATH ***** =="
# GATE 1 of 5: the launcher must CALL it. Without this the object could be
# linked, pass every structural check, and translate nothing -- the aliases
# would be green and absent at the same time.
	@if ! $(NM) -u $(M16CBUILD)/m16cmain.o 2>/dev/null \
	        | grep -qw "r7_input_from_pad"; then \
	     echo "M16C VERIFY FAILED: m16cmain.o does NOT reference"; \
	     echo "  r7_input_from_pad. The gameplay loop is not going through"; \
	     echo "  the alias layer, so X/O -> A and SQUARE/TRIANGLE -> B are"; \
	     echo "  NOT in effect no matter what the host test proved."; \
	     exit 1; \
	 fi; \
	 echo "m16cmain.o REFERENCES r7_input_from_pad (step 2 is the alias layer)"
# GATE 2 of 5: and it must not ALSO keep the un-aliased path. Two translators
# reachable from the same loop is exactly how a half-applied mapping ships.
	@if $(NM) -u $(M16CBUILD)/m16cmain.o 2>/dev/null \
	        | grep -qw "m8_input_from_pad"; then \
	     echo "M16C VERIFY FAILED: m16cmain.o still references"; \
	     echo "  m8_input_from_pad DIRECTLY. Gameplay input must reach the"; \
	     echo "  frozen translator only THROUGH r7_input_from_pad, or some"; \
	     echo "  frames would be aliased and others not."; \
	     exit 1; \
	 fi; \
	 echo "m16cmain.o does NOT call m8_input_from_pad directly -- one path only"
# GATE 3 of 5: the alias layer must DELEGATE. This is what proves it is a
# canonicalisation and not a second, drifting copy of the mapping policy.
	@if ! $(NM) -u $(M16CBUILD)/gba_r7input.o 2>/dev/null \
	        | grep -qw "m8_input_from_pad"; then \
	     echo "M16C VERIFY FAILED: gba_r7input.o does NOT reference"; \
	     echo "  m8_input_from_pad. The alias layer must DELEGATE to the"; \
	     echo "  frozen table, never restate it."; \
	     exit 1; \
	 fi; \
	 echo "gba_r7input.o REFERENCES m8_input_from_pad (the frozen table still"; \
	 echo "  decides what every button MEANS)"
# GATE 4 of 5: no second mapping table. The frozen arrays are named m8_map_pad
# and m8_map_key; if either name appears in the R7 source, policy has been
# duplicated and the two copies can drift.
	@if grep -q "m8_map_" adapters/gba/gba_r7input.c; then \
	     echo "M16C VERIFY FAILED: adapters/gba/gba_r7input.c names the"; \
	     echo "  frozen mapping arrays. R7 must rewrite the PAD WORD, not"; \
	     echo "  restate or shadow the table."; \
	     exit 1; \
	 fi; \
	 echo "gba_r7input.c contains no second mapping table"
# GATE 5 of 5: the same identity discipline gba_input.o has carried since M8.
	@bad=$$($(NM) -u $(M16CBUILD)/gba_r7input.o 2>/dev/null \
	        | grep -oE "plat_[A-Za-z0-9_]*|scePad[A-Za-z0-9_]*|sceVideoOut[A-Za-z0-9_]*"); \
	 if [ -n "$$bad" ]; then \
	     echo "M16C VERIFY FAILED: gba_r7input.o carries a platform"; \
	     echo "  relocation:$$bad"; \
	     echo "  The alias layer sits between the pad word and the frozen"; \
	     echo "  adapter and must reach neither end of the boundary."; \
	     exit 1; \
	 fi; \
	 echo "gba_r7input.o carries NO plat_*/scePad*/sceVideoOut* relocation"
	@echo "== ***** THE RETURN CHORD STILL READS THE RAW PAD WORD ***** =="
# CHECKED AT THE SOURCE, because this is the one way the aliases could silently
# break the chord: feeding the canonicalised word to the chord test would let
# CROSS/CIRCLE and SQUARE/TRIANGLE perturb the bits L1+R1+L2+R2 are read from.
# raw_pad must remain the UNTOUCHED physical word.
	@if ! grep -q "raw_pad & M16C_EXIT_CHORD" apps/m16cgpsp/main.c; then \
	     echo "M16C VERIFY FAILED: the return chord no longer tests"; \
	     echo "  raw_pad against M16C_EXIT_CHORD. The chord must inspect the"; \
	     echo "  ORIGINAL pad word, never an aliased or canonicalised one."; \
	     exit 1; \
	 fi; \
	 echo "the chord still tests raw_pad -- the alias layer cannot reach it"
	@if ! grep -q "r7_input_from_pad(raw_pad, pad_conn)" apps/m16cgpsp/main.c; then \
	     echo "M16C VERIFY FAILED: the gameplay call site no longer passes"; \
	     echo "  raw_pad to r7_input_from_pad. Aliasing must happen INSIDE"; \
	     echo "  the call; raw_pad itself must never be rewritten."; \
	     exit 1; \
	 fi; \
	 echo "aliasing happens inside the call -- raw_pad is never rewritten"
	@echo "== ***** execute_arm IS REACHABLE ONLY THROUGH gba_exec.o ***** =="
	@bad=""; \
	 for o in $(M16COBJS); do \
	     case "$$o" in */gba_exec.o) continue ;; esac; \
	     if $(NM) -u $$o 2>/dev/null | grep -qw "execute_arm"; then \
	         bad="$$bad $$o"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M16C VERIFY FAILED: an object outside gba_exec.o REFERENCES"; \
	     echo "  execute_arm:$$bad"; \
	     exit 1; \
	 fi; \
	 defs=""; \
	 for o in $(M16COBJS); do \
	     if $(NM) --defined-only $$o 2>/dev/null | grep -qw "execute_arm"; then \
	         defs="$$defs $$o"; \
	     fi; \
	 done; \
	 case "$$defs" in \
	   " $(M16CBUILD)/gpsp_cpu.o") ;; \
	   *) echo "M16C VERIFY FAILED: execute_arm is defined by:$$defs"; \
	      echo "  It must be defined by gpsp_cpu.o and by nothing else."; \
	      exit 1;; \
	 esac; \
	 echo "gba_exec.o is the ONLY caller and gpsp_cpu.o the ONLY definer"
	@echo "== ***** THE LAUNCHER TOUCHES NO gpSP SAVE GLOBAL ***** =="
	@bad=""; \
	 for s in backup_type gamepak_backup flash_bank_cnt eeprom_size; do \
	     if $(NM) -u $(M16CBUILD)/m16cmain.o 2>/dev/null | grep -qw "$$s"; then \
	         bad="$$bad $$s"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M16C VERIFY FAILED: m16cmain.o names a gpSP save global:$$bad"; \
	     echo "  The launcher must reach save state only through the adapters."; \
	     exit 1; \
	 fi; \
	 echo "m16cmain.o names no gpSP save global -- it goes through the adapters"
	@bad=""; \
	 for o in $(M16COBJS); do \
	     case "$$o" in \
	       */gpsp_*.o) continue ;; \
	     esac; \
	     ok=""; \
	     for a in $(M16C_BACKUP_NAMERS); do \
	         if [ "$${o##*/}" = "$$a" ]; then ok=1; fi; \
	     done; \
	     if [ -n "$$ok" ]; then continue; fi; \
	     if $(NM) -u $$o 2>/dev/null | grep -qw "gamepak_backup"; then \
	         bad="$$bad $$o"; \
	     fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M16C VERIFY FAILED: an unexpected object reaches"; \
	     echo "  gamepak_backup:$$bad"; \
	     echo "  The permitted set, BY ROLE, is:"; \
	     echo "    gba_rom.o      INITIALIZATION WRITER (0xFF pre-load fill)"; \
	     echo "    gba_restore.o  RESTORE WRITER (sole writer of restored bytes)"; \
	     echo "    gba_save.o     READ-ONLY (identity/region hashes)"; \
	     echo "    gba_sramobs.o  READ-ONLY (commit-side capture)"; \
	     echo "  NOTE gba_session.o IS NOT IN THE SET: the boundary reaches"; \
	     echo "  save state through gba_save_unlatch() and"; \
	     echo "  gba_restore_disarm(), never through the array."; \
	     exit 1; \
	 fi; \
	 echo "gamepak_backup is reached by exactly the four permitted objects,"; \
	 echo "  and gba_session.o is not one of them"
	@if ! $(NM) -u $(M16CBUILD)/gba_rom.o 2>/dev/null \
	      | grep -qw "gamepak_backup"; then \
	     echo "M16C VERIFY FAILED: gba_rom.o does NOT reference gamepak_backup."; \
	     echo "  m4_rom_backup_init() must still memset the array to 0xFF"; \
	     echo "  before load_gamepak. Without it cartridges boot with the"; \
	     echo "  wrong blank-save state -- and in a LOOPING launcher that"; \
	     echo "  would mean cartridge N+1 booting on cartridge N's bytes."; \
	     exit 1; \
	 fi; \
	 echo "gba_rom.o still reaches gamepak_backup -- the 0xFF pre-load fill is"; \
	 echo "  present, and it runs per session BEFORE each load"
	@echo "== ***** THE DYNAREC IS ABSENT ***** =="
	@for s in $(M16C_FORBIDDEN_DYNAREC); do \
	     if $(READELF) -sW $(M16CELF) 2>/dev/null \
	          | awk '$$7 != "UND" { print $$8 }' | grep -qx "$$s"; then \
	         echo "M16C VERIFY FAILED: dynarec symbol '$$s' is DEFINED in the"; \
	         echo "  linked image. HAVE_DYNAREC must stay undefined and"; \
	         echo "  cpu_threaded.c must stay uncompiled."; \
	         exit 1; \
	     fi; \
	 done; \
	 bad=""; \
	 for o in $(M16COBJS); do \
	     for s in $(M16C_FORBIDDEN_DYNAREC); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M16C VERIFY FAILED: an object REFERENCES a dynarec entry"; \
	     echo "  point:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "execute_arm_translate and dynarec_enable are absent, defined and"; \
	 echo "  referenced alike -- the interpreter is the only execution path"
	@for o in $(M16COBJS); do \
	     case "$$o" in \
	       */cpu_threaded.o|*/dynarec*.o|*/libretro*.o) \
	           echo "M16C VERIFY FAILED: a dynarec or libretro object is"; \
	           echo "  linked: $$o"; \
	           exit 1;; \
	     esac; \
	 done; \
	 echo "no cpu_threaded, dynarec or libretro object is on the link line"
	@echo "== ***** M11's OBSERVER IS NOT LINKED ***** =="
	@for o in $(M16COBJS); do \
	     case "$$o" in \
	       */gba_m11save.o) \
	           echo "M16C VERIFY FAILED: M11's observer is linked: $$o"; \
	           exit 1;; \
	     esac; \
	 done; \
	 echo "gba_m11save.o is absent -- the dirty decision is M12C's baseline"
	@echo "== ***** THE WRITE CAPABILITY IS CONFINED TO ONE OBJECT ***** =="
# GATE 1 of 3: no object BUT gba_savefile.o may REFERENCE a write entry point.
	@bad=""; \
	 for o in $(M16COBJS); do \
	     case "$$o" in */$(M16C_WRITER_OBJ)) continue ;; esac; \
	     for s in $(M16C_FORBIDDEN_FWRITE); do \
	         if $(NM) -u $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M16C VERIFY FAILED: an object outside the writer REFERENCES a"; \
	     echo "  file-write entry point:$$bad"; \
	     echo "  Only $(M16C_WRITER_OBJ) may write."; \
	     exit 1; \
	 fi; \
	 echo "no object but $(M16C_WRITER_OBJ) references a write entry point"
# GATE 2 of 3: the writer MUST reference fwrite, or the gate above would pass
# VACUOUSLY on a stubbed-out writer.
	@if ! $(NM) -u $(M16CBUILD)/$(M16C_WRITER_OBJ) 2>/dev/null \
	        | grep -qw "fwrite"; then \
	     echo "M16C VERIFY FAILED: $(M16C_WRITER_OBJ) does NOT reference"; \
	     echo "  fwrite. The launcher WRITES a .sav; if the writer cannot"; \
	     echo "  write, a clean session would be VACUOUS."; \
	     exit 1; \
	 fi; \
	 echo "$(M16C_WRITER_OBJ) REFERENCES fwrite -- the write path is real"
# GATE 3 of 3: no object BUT the frozen shim may DEFINE a write entry point.
	@bad=""; \
	 for o in $(M16COBJS); do \
	     case "$$o" in *shim.o) continue ;; esac; \
	     for s in $(M16C_FORBIDDEN_FWRITE); do \
	         if $(NM) --defined-only $$o 2>/dev/null | grep -qw "$$s"; then \
	             bad="$$bad $$o:$$s"; \
	         fi; \
	     done; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M16C VERIFY FAILED: a write entry point is DEFINED outside the"; \
	     echo "  frozen shim, by:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "no object but the frozen shim defines a write entry point"
	@echo "== ***** SD_CREATE IS NEVER DEFINED (M12A's gate, unchanged) ***** =="
	@if grep -nE "^[[:space:]]*#[[:space:]]*define[[:space:]]+SD_CREATE" \
	        runtime/savedata.c runtime/savedata.h \
	        adapters/gba/gba_savefile.c adapters/gba/gba_savefile.h; then \
	     echo "M16C VERIFY FAILED: SD_CREATE is DEFINED."; \
	     exit 1; \
	 fi; \
	 echo "SD_CREATE is not defined in the savedata layer or the save adapter"
	@echo "== rodata carve (marker-based, as M7-M13C) =="
	@$(OBJCOPY) -O binary --only-section=.text $(M16CELF) $(M16CBUILD)/text.bin
	@tx=$$($(NM) $(M16CELF) | awk '$$NF == "__text_start"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 ra=$$($(NM) $(M16CELF) | awk '$$NF == "__rodata_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 rz=$$($(NM) $(M16CELF) | awk '$$NF == "__rodata_end"   && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' | head -1); \
	 if [ -z "$$tx" ] || [ -z "$$ra" ] || [ -z "$$rz" ]; then \
	     echo "M16C VERIFY FAILED: linker marker symbol missing."; \
	     exit 1; \
	 fi; \
	 t=$$((0x$$tx)); a=$$((0x$$ra)); z=$$((0x$$rz)); n=$$((z - a)); \
	 if [ "$$t" -ne 0 ]; then \
	     echo "M16C VERIFY FAILED: .text does not start at vaddr 0."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M16C VERIFY FAILED: empty rodata window."; \
	     exit 1; \
	 fi; \
	 have=$$(wc -c < $(M16CBUILD)/text.bin | tr -d " "); \
	 if [ "$$have" -lt "$$z" ]; then \
	     echo "M16C VERIFY FAILED: .text extraction is short."; \
	     exit 1; \
	 fi; \
	 dd if=$(M16CBUILD)/text.bin of=$(M16CBUILD)/rodata.bin bs=1 skip=$$a count=$$n \
	    2>/dev/null || { \
	     echo "M16C VERIFY FAILED: dd could not carve the rodata window."; exit 1; \
	 }; \
	 if [ ! -s $(M16CBUILD)/rodata.bin ]; then \
	     echo "M16C VERIFY FAILED: rodata extraction produced 0 bytes."; \
	     exit 1; \
	 fi; \
	 got=$$(wc -c < $(M16CBUILD)/rodata.bin | tr -d " "); \
	 if [ "$$got" -ne "$$n" ]; then \
	     echo "M16C VERIFY FAILED: rodata.bin is $$got bytes, expected $$n."; \
	     exit 1; \
	 fi; \
	 echo "rodata carved from .text by linker markers: [$$a,$$z) = $$n bytes"
	@echo "== ***** THE SAVEDATA LAYER REALLY REACHED THE IMAGE ***** =="
	@for s in $(M16C_REQUIRED_NAMES); do \
	     if ! grep -aq "$$s" $(M16CBUILD)/rodata.bin; then \
	         echo "M16C VERIFY FAILED: required savedata name '$$s' is NOT in"; \
	         echo "  the image. A launcher that cannot name the module it"; \
	         echo "  mounts is not mounting anything."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "libSceSaveData.sprx, sceSaveDataUmount, both region dirNames and the"; \
	 echo "  three kernel entry points are all present"
	@echo "== ***** THE LAUNCHER NAMES THE FILES IT WRITES ***** =="
	@miss=""; \
	 for s in "/savedata0/gba" ".sav" ".hdr"; do \
	     grep -aq "$$s" $(M16CBUILD)/rodata.bin || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M16C VERIFY FAILED: the image does not contain:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "/savedata0/gba, .sav and .hdr are all present in rodata"
	@echo "== ***** NO DIRECT USB, DEVICE OR MOUNT LITERAL ***** =="
	@if grep -aqiE "/mnt|/usb|/dev/|ugen|usbctl" $(M16CBUILD)/rodata.bin; then \
	     echo "M16C VERIFY FAILED: the image contains a USB, device or mount"; \
	     echo "  literal. M16C reads /temp0 and /savedata0 and nothing else."; \
	     exit 1; \
	 fi; \
	 echo "no /mnt, /usb, /dev, ugen or usbctl literal is present"
	@echo "== ***** NO MILESTONE VOCABULARY IN THE USER-FACING IMAGE ***** =="
# A comment cannot reach rodata, so this can only fire on a real string. M16C is
# the launcher; the staged-fixture wording belongs to the frozen M13B images.
	@bad=""; \
	 for w in $(M16C_FORBIDDEN_WORDS); do \
	     if grep -aq "$$w" $(M16CBUILD)/rodata.bin; then bad="$$bad $$w"; fi; \
	 done; \
	 for p in "RE-RUN WITH" "NO GBA CODE EXECUTED" "NOTHING IS WRITTEN" \
	          "NO GAME RUNS AT THIS" "NO ROM WAS LOADED"; do \
	     if grep -aq "$$p" $(M16CBUILD)/rodata.bin; then bad="$$bad [$$p]"; fi; \
	 done; \
	 if [ -n "$$bad" ]; then \
	     echo "M16C VERIFY FAILED: staged-fixture vocabulary survives in the"; \
	     echo "  image:$$bad"; \
	     exit 1; \
	 fi; \
	 echo "no RUNG, STAGE or MILESTONE text and no staged-fixture phrasing"
	@echo "== ***** THE LAUNCHER SPEAKS THE OPERATOR'S LANGUAGE ***** =="
	@miss=""; \
	 for p in "LUAPORT GBA" "NO GBA ROMS FOUND" "SESSION COMPLETE" \
	          "BIOS NOT FOUND" "ROM COULD NOT BE OPENED" "ROM INVALID" \
	          "SAVE CORRUPT" "SAVE WRITE FAILED" "VIDEO INIT FAILED" \
	          "CONTROLLER NOT AVAILABLE" "AUDIO INIT FAILED" \
	          "TIMING INIT FAILED" "ROM MAPPING FAILED" "WATCHDOG EXIT" \
	          "SESSION DID NOT RELEASE" \
	          "ERROR " "STEP "; do \
	     grep -aq "$$p" $(M16CBUILD)/rodata.bin || miss="$$miss [$$p]"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M16C VERIFY FAILED: a required user-facing string is absent"; \
	     echo "  from the image:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "the title, the empty-library screen, the summary and every error"; \
	 echo "  heading are present, with ERROR and STEP beneath them"
	@echo "== ***** THE FROZEN M13C BASELINE IS NOT TOUCHED ***** =="
# ***** THE CENTRAL ISOLATION GATE, REPRODUCED RATHER THAN SHARED. *****
# M16 adds code to six SHARED translation units -- gba_savehdr.c and
# gba_restorefile.c joined the list with the M16-2 FLASH64 rule. The freeze
# survives only because every addition sits behind #ifdef LUAPORT_SESSION_REUSE
# and that macro never reaches an M13C compile. Both halves are CHECKED, not
# trusted.
	@if $(MAKE) --no-print-directory -pn m13c 2>/dev/null \
	     | grep -E "^M13C[A-Z_]* *[:+]?=" \
	     | grep -q "LUAPORT_SESSION_REUSE"; then \
	     echo "M16C VERIFY FAILED: LUAPORT_SESSION_REUSE reaches an M13C flag"; \
	     echo "  variable. M13C would preprocess the shared files differently"; \
	     echo "  and build/m13c/m13cgpsp.bin would no longer reproduce"; \
	     echo "  e9925161d48f65660bbc89cdd9bd9fb6fb98f6b48431a80b44c500395d6d824a."; \
	     echo "  STOP -- do not update the frozen hash."; \
	     exit 1; \
	 fi; \
	 echo "no M13C flag variable carries LUAPORT_SESSION_REUSE"
# ***** AND THE POSITIVE HALF: M16C ITSELF MUST CARRY IT. ***** gba_session.h
# has an #error without it, so a missing define is a compile failure rather than
# a silent one -- but asserting it here means the FLAG VARIABLE is the thing
# checked, not the accident of a header.
	@if ! $(MAKE) --no-print-directory -pn m16c 2>/dev/null \
	     | grep -E "^M16C[A-Z_]* *[:+]?=" \
	     | grep -q "LUAPORT_SESSION_REUSE"; then \
	     echo "M16C VERIFY FAILED: no M16C flag variable carries"; \
	     echo "  LUAPORT_SESSION_REUSE. The session boundary would not link."; \
	     exit 1; \
	 fi; \
	 echo "M16C's own flags carry LUAPORT_SESSION_REUSE"
	@if grep -qE "m16_|m16b|m16c|m16diag|m16bdiag|m16cgpsp|LUAPORT_SESSION_REUSE" \
	        apps/m13cgpsp/main.c; then \
	     echo "M16C VERIFY FAILED: apps/m13cgpsp/main.c carries an M16 code"; \
	     echo "  reference. The launcher is FROZEN EVIDENCE."; \
	     exit 1; \
	 fi; \
	 echo "apps/m13cgpsp/main.c carries no M16 code reference"
	@if grep -qE "m16_|m16b|m16c|m16diag|m16bdiag|m16cgpsp" lua/m13c.lua.in; then \
	     echo "M16C VERIFY FAILED: lua/m13c.lua.in carries an M16 reference."; \
	     exit 1; \
	 fi; \
	 echo "lua/m13c.lua.in carries no M16 reference"
	@for f in runtime/shim.c adapters/gba/gba_save.c \
	          adapters/gba/gba_restore.c adapters/gba/gba_filestream.c \
	          adapters/gba/gba_savehdr.c adapters/gba/gba_restorefile.c; do \
	     n=$$(grep -c "^#ifdef LUAPORT_SESSION_REUSE" $$f); \
	     if [ "$$n" -lt 1 ]; then \
	         echo "M16C VERIFY FAILED: $$f has no LUAPORT_SESSION_REUSE guard."; \
	         echo "  Either the M16 addition was removed, or it was added"; \
	         echo "  UNGUARDED and the M13C freeze is already broken."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "all six shared translation units carry a session-reuse guard"
	@echo "== ***** M16-0 AND M16-1 HARDWARE EVIDENCE IS NOT TOUCHED ***** =="
# Both diagnostics passed on a real PS5. Their sources are evidence, and M16-2
# is a SEPARATE payload precisely so all three stay independent.
	@if grep -qE "m16c|M16C_|m16cgpsp" apps/m16diag/main.c lua/m16diag.lua.in \
	        apps/m16bdiag/main.c lua/m16bdiag.lua.in; then \
	     echo "M16C VERIFY FAILED: an M16-0 or M16-1 source carries an M16-2"; \
	     echo "  reference. apps/m16diag/**, apps/m16bdiag/** and their two"; \
	     echo "  Lua templates are HARDWARE PASS EVIDENCE."; \
	     exit 1; \
	 fi; \
	 echo "no M16-0 or M16-1 source carries an M16-2 reference"
	@for v in M16OBJS M16BOBJS M13COBJS; do \
	     if $(MAKE) --no-print-directory -pn m16c 2>/dev/null \
	          | grep -qE "^$$v *[:+]?=.*m16cmain\.o"; then \
	         echo "M16C VERIFY FAILED: $$v was altered to carry m16cmain.o."; \
	         exit 1; \
	     fi; \
	 done; \
	 echo "M16OBJS, M16BOBJS and M13COBJS are unaltered by this family"
	@echo "== ***** THE BUDGETS ARE NOT RAISED ***** =="
	@if ! grep -q "ROM_BUFFER_SIZE=2" Makefile; then \
	     echo "M16C VERIFY FAILED: ROM_BUFFER_SIZE is no longer 2."; \
	     exit 1; \
	 fi; \
	 echo "ROM_BUFFER_SIZE is still 2"
	@if ! $(MAKE) --no-print-directory -pn m16c 2>/dev/null \
	     | grep -qE "^M16C_GPSPFLAGS *[:+]?=.*ROM_BUFFER_SIZE=2"; then \
	     echo "M16C VERIFY FAILED: M16C's own gpSP flags no longer carry"; \
	     echo "  -DROM_BUFFER_SIZE=2, so this image is not demand paging."; \
	     exit 1; \
	 fi; \
	 echo "M16C's gpSP flags carry -DROM_BUFFER_SIZE=2"
	@if ! grep -q "GBA_LIB_MAX_ENTRIES 64" adapters/gba/gba_library.h; then \
	     echo "M16C VERIFY FAILED: the picker capacity is no longer 64."; \
	     exit 1; \
	 fi; \
	 echo "the library capacity is still 64"
	@if ! grep -qE "^BSS_LIMIT = 2 \* 1024 \* 1024" tools/check_forbidden.py; then \
	     echo "M16C VERIFY FAILED: the .bss tripwire is no longer 2,097,152."; \
	     echo "  M16C links one more object than M13C. If it does not fit,"; \
	     echo "  READ THE MAP -- do not raise the ceiling."; \
	     exit 1; \
	 fi; \
	 echo "the .bss tripwire is still 2,097,152 bytes"
	@if ! grep -qE "^JIT_LIMIT := 524288" Makefile; then \
	     echo "M16C VERIFY FAILED: the JIT budget is no longer 524,288."; \
	     exit 1; \
	 fi; \
	 echo "the JIT budget is still 524,288 bytes"
	@echo "== M16C VERIFY OK =="

# ---- the Lua payload --------------------------------------------------------
#
# ***** ITS OWN TEMPLATE. lua/m13c.lua.in IS NOT EDITED AND NOT REUSED IN PLACE.
# ***** The transport is byte-for-byte M0's through M16-1's and is reproduced
# rather than re-derived; only the step names, the status names and the verdict
# decode differ, because M16C's bands are 440-459 and -601..-635.
M16C_LUA_TEMPLATE := lua/m16c.lua.in
M16C_LUA_OUT      := $(M16CBUILD)/$(M16CTARGET).lua

m16c-lua: m16c-check $(M16CBIN) $(M16CELF)
	@$(PYTHON) tools/mklua.py $(M16C_LUA_TEMPLATE) $(M16CBIN) \
	     $(M16C_LUA_OUT) $(M16CELF)
	@echo "M16C LUA OK: $(M16C_LUA_OUT)"

# ---- m16c-send -- HARDWARE DELIVERY -----------------------------------------
#
# NOT AUTHORISED BY THE EXISTENCE OF THIS TARGET.
#
#   * m16c-verify IS A PREREQUISITE, and it in turn depends on m13b-equiv and
#     m12c-equiv, so the picker harness and the save/restore regression gate
#     both run before anything is transmitted, always.
#   * ***** M16-2 HAS HAD NO HARDWARE RUN. ***** M16-0 and M16-1 passed; those
#     are DIFFERENT images answering DIFFERENT questions, and neither of them
#     could write a save at all.
#   * ***** THIS IS THE FIRST M16 IMAGE THAT CAN WRITE. ***** The two
#     diagnostics were deliberately incapable of it. Every earlier M16 pass is
#     evidence about the BOUNDARY, not about a commit that crosses one.
#   * ***** THE FIRST HARDWARE TEST IS A TWO-CARTRIDGE, TWO-SESSION, ONE-LAUNCH
#     PROOF. ***** Two ROMs in /temp0; open the launcher; pick A; play; return
#     with the chord; the PICKER MUST COME BACK; pick B; play; return; pick A
#     again. A's save must restore correctly on the third session. The payload
#     must never have restarted -- check that the frame ids in the log climb
#     monotonically through all three.
#   * CAPTURE THE UDP LOG ON 9027. M16C draws no diagnostics, so the log is
#     where every measurement lives.
m16c-send: m16c-verify m16c-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m16c-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M16C_LUA_OUT) $(M16CBIN)

m16c-clean:
	rm -rf $(M16CBUILD)

.PHONY: m16c m16c-objs m16c-check m16c-link m16c-size m16c-verify \
        m16c-hostcc m16c-save-equiv m16c-lua m16c-send m16c-clean

# =============================================================================
# @@M18_BLOCK_BEGIN@@
# M18-0 -- THE NATIVE UDP CAPABILITY DIAGNOSTIC
# =============================================================================
#
# The sentinel on the line above, together with the matching END sentinel at the
# very bottom of this file, brackets the M18 block STRUCTURALLY. m18diag-verify
# uses that pair to decide which lines of this Makefile are M18 and which are
# pre-existing, so its contamination gate can search ONLY the pre-existing
# region. DO NOT REMOVE, REORDER OR DUPLICATE THE PAIR: the gate is written to
# fail loudly rather than pass vacuously if the first BEGIN / last END pair is
# missing or out of order.
#
#   make m18diag-check      offline structural check, no compiler needed
#   make m18diag-objs
#   make m18diag-link
#   make m18diag
#   make m18diag-size
#   make m18diag-verify
#   make m18diag-lua
#   make m18diag-send PS5_IP=<addr>
#   make m18diag-clean
#
# ***** THIS BLOCK IS PURELY ADDITIVE. ***** It defines new M18* variables and
# new m18diag-* targets and MODIFIES NOTHING. No existing variable is reassigned
# and no existing rule is edited. $(BASEFLAGS), $(HEADERS), $(JIT_LIMIT),
# $(PYTHON), $(CC), $(NM), $(READELF), $(OBJDUMP) and $(OBJCOPY) are READ, which
# is exactly what every other milestone block does.
#
# ***** THE FROZEN ARTIFACTS CANNOT BE AFFECTED. ***** M13C's image is
# determined by $(M13C_OWN_OBJS) plus its gpSP objects. This block adds only NEW
# files and NEW targets; runtime/net.o appears in $(M18_OWN_OBJS) and in no
# other object list in this file, and m18diag-verify PROVES that rather than
# asserting it. M13C must still reproduce at 308,784 bytes / SHA-256
# e9925161d48f65660bbc89cdd9bd9fb6fb98f6b48431a80b44c500395d6d824a.
#
# =============================================================================
# WHAT M18-0 IS, IN ONE PARAGRAPH
# =============================================================================
# ONE console. NO peer. NO emulator. It answers two questions and stops:
#   U1  can the NATIVE payload resolve and call the BSD socket primitives?
#   U3  can it create and bind a UDP socket with NO libSceNet.sprx load?
#
# Everything else the M18 plan describes -- the LNP1 header, peers,
# generations, sequence numbers, reliability, the gpSP netpacket seam -- is
# ABSENT BY DESIGN and belongs to M18-1 and later.

M18TARGET := m18diag
M18BUILD  := build/m18diag
M18ELF    := $(M18BUILD)/$(M18TARGET).elf
M18BIN    := $(M18BUILD)/$(M18TARGET).bin

# ---- the CODE-ONLY VIEW of runtime/net.c ------------------------------------
#
# runtime/net.c with every comment body and every string/char literal body
# replaced by spaces: ONE LINE IN, ONE LINE OUT, COLUMN FOR COLUMN. A grep -n
# on it therefore reports TRUE runtime/net.c line numbers, which is what lets
# m18diag-verify keep comparing line numbers.
#
# The structural gates at the end of m18diag-verify grep THIS instead of the
# source, because every invariant they state is about EXECUTABLE CODE and a raw
# line cannot tell code from commentary. See tools/m18_code_only.py, and the
# note above the fcntl gate block for the host build this repaired.
#
# ***** IT IS A BUILD ARTIFACT AND IS NEVER COMPILED. ***** Nothing in
# $(M18OBJS) depends on it, no rule hands it to $(CC), and it carries no
# information the source does not already carry. It exists only so a gate can
# be asked a question about code and get an answer about code.
M18CODE   := $(M18BUILD)/net.code-only.c

$(M18BUILD):
	mkdir -p $(M18BUILD)

# ---- flags ------------------------------------------------------------------
#
# ***** NO -DLUAPORT_SESSION_REUSE. ***** That define is M16's alone. M18-0 runs
# ONE session, allocates nothing from the arena and never rewinds it, so shim.o
# here is the plain M0-shaped object.
#
# ***** NO -Igpsp AND NO -Iadapters/gba. ***** There is no emulator in this
# image. A gpSP or GBA dependency that survived into these sources becomes a
# COMPILE ERROR here rather than a surprise on the link line -- the same
# structural argument runtime/platform.h and the M0 INCLUDES note record.
M18_INCLUDES = -Iruntime/libc -Iruntime -Iapps/m18diag

M18FLAGS = $(BASEFLAGS) $(M18_INCLUDES) -Wall -Wno-unused-function

# runtime/net.h is the ONE header this milestone adds. boot.inc and fault.inc
# are #included into main.c and are already carried by $(HEADERS), which is why
# they are dependencies exactly like a header -- see the note at the M0 HEADERS
# definition for the bug that omission once caused.
M18HEADERS = $(HEADERS) runtime/net.h

M18LDFLAGS = -T runtime/linker.ld -nostdlib -nostartfiles -static-pie \
             -Wl,--build-id=none -Wl,-z,norelro \
             -Wl,--orphan-handling=error \
             -Wl,-Map=$(M18BUILD)/$(M18TARGET).map

# ----------------------------------------------------------------- objects ---
#
# ***** FIVE OBJECTS. THE SMALLEST LINK LINE IN THIS MAKEFILE SINCE M0. *****
# Each ABSENCE below is a STRUCTURAL GATE, not an oversight:
#
#   NO gpsp_*.o          there is no emulator. The entire gpSP core, the serial
#                        objects included, is absent from the image.
#   NO gba_*.o           no GBA adapter of any kind. M18-0 has no link cable
#                        concept, no netpacket seam and no session.
#   NO audio.o           M18-0 opens no audio port. Keeping it off the line also
#                        keeps every sceAudioOut* string out of this image.
#   NO savedata.o, gba_savefile.o, gba_savehdr.o, gba_restorefile.o
#                        a network capability probe MUST NOT be able to write a
#                        save file. Omitting the objects makes that physically
#                        impossible rather than merely forbidden.
#   NO m13store.o, gba_library.o
#                        no ROM picker and no paging.
#   NO cpu_threaded.o    every dynarec emitter. HAVE_DYNAREC stays undefined.
#   NO libretro*.o       and no libretro VFS.
M18_OWN_OBJS = $(M18BUILD)/m18main.o \
               $(M18BUILD)/net.o \
               $(M18BUILD)/shim.o \
               $(M18BUILD)/gfx.o \
               $(M18BUILD)/platform.o

M18OBJS = $(M18_OWN_OBJS)

# Every capability the diagnostic advertises must be ON the link line.
M18_REQUIRED_OBJS = m18main.o net.o shim.o gfx.o platform.o

# ***** THE OBJECTS THAT MUST NOT BE ON THE LINK LINE. ***** See the commentary
# above: this is the entire emulator, the entire save-write path, the picker,
# the paging store and the audio layer.
M18_FORBIDDEN_OBJS = audio.o savedata.o m13store.o \
                     gba_library.o gba_fixture.o gba_filestream.o \
                     gba_compat.o gba_probe.o gba_bios.o gba_rom.o \
                     gba_save.o gba_restore.o gba_sramobs.o gba_m10map.o \
                     gba_m8map.o gba_m11save.o gba_exec.o gba_present.o \
                     gba_input.o gba_audio.o gba_session.o gba_savefile.o \
                     gba_savehdr.o gba_restorefile.o gba_link.o \
                     gpsp_cpu.o gpsp_main.o gpsp_gba_memory.o gpsp_video.o \
                     gpsp_sound.o gpsp_serial.o gpsp_serial_proto.o \
                     gpsp_rfu.o gpsp_gbp.o gpsp_cheats.o gpsp_savestate.o \
                     gpsp_input.o cpu_threaded.o libretro.o

# ---- offline structural check -----------------------------------------------
#
# Runs on a workstation with NO toolchain at all -- which is the situation this
# milestone was authored in.
m18diag-check:
	@$(PYTHON) tools/m8_syntax_check.py apps/m18diag/main.c
	@$(PYTHON) tools/m8_syntax_check.py runtime/net.c
	@echo "M18 CHECK OK: apps/m18diag/main.c and runtime/net.c are structurally sound"

# ---- compilation ------------------------------------------------------------

$(M18BUILD)/m18main.o: apps/m18diag/main.c $(M18HEADERS) | $(M18BUILD)
	$(CC) $(M18FLAGS) -c $< -o $@

# ***** THE ONLY OBJECT IN THIS TREE THAT CALLS socket(), bind() OR
# recvfrom(). ***** It is its own translation unit for the same reason
# runtime/audio.c is: every image links each named object IN ITS ENTIRETY with
# no --gc-sections, so putting socket code into a SHARED object would carry it
# into every frozen milestone that links that object.
$(M18BUILD)/net.o: runtime/net.c $(M18HEADERS) | $(M18BUILD)
	$(CC) $(M18FLAGS) -c $< -o $@

$(M18BUILD)/shim.o: runtime/shim.c $(M18HEADERS) | $(M18BUILD)
	$(CC) $(M18FLAGS) -c $< -o $@

$(M18BUILD)/gfx.o: runtime/gfx.c $(M18HEADERS) | $(M18BUILD)
	$(CC) $(M18FLAGS) -c $< -o $@

$(M18BUILD)/platform.o: runtime/platform.c $(M18HEADERS) | $(M18BUILD)
	$(CC) $(M18FLAGS) -c $< -o $@

m18diag-objs: $(M18OBJS)

# ---- link -------------------------------------------------------------------

$(M18ELF): $(M18OBJS) runtime/linker.ld | $(M18BUILD)
	$(CC) $(BASEFLAGS) $(M18LDFLAGS) -o $@ $(M18OBJS)

# Link-only. The linker's exit status is captured BEFORE anything is piped, for
# the reason the M0 `link` target records: piping into grep|sed|sort made the
# recipe's status sort's, and sort exits 0 on empty input, so success and
# failure were indistinguishable.
m18diag-link: $(M18OBJS) | $(M18BUILD)
	@$(CC) $(BASEFLAGS) $(M18LDFLAGS) -o $(M18ELF) $(M18OBJS) \
	     > $(M18BUILD)/link.log 2>&1; \
	 status=$$?; \
	 undef=$$(grep -oE "undefined reference to .[^']+'" $(M18BUILD)/link.log \
	            | sed -E "s/.*to .//; s/'$$//" | sort -u); \
	 if [ $$status -ne 0 ]; then \
	     cat $(M18BUILD)/link.log; \
	     echo "M18 LINK FAILED: linker exit $$status"; \
	     exit $$status; \
	 fi; \
	 if [ -n "$$undef" ]; then \
	     echo "M18 LINK FAILED: undefined references reported despite exit 0"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "M18 LINK OK: linker exit 0, no undefined references"

$(M18BIN): $(M18ELF)
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $<
	$(OBJCOPY) -O binary $< $@
	@$(PYTHON) tools/sizereport.py $(M18ELF) $@ \
	   --map $(M18BUILD)/$(M18TARGET).map --budget $(JIT_LIMIT)

m18diag: $(M18BIN)

# ***** THE BUDGETS ARE NOT RAISED BY THIS MILESTONE. ***** $(JIT_LIMIT) is
# still 524,288 and the .bss tripwire is still 2,097,152. M18-0 adds five .bss
# objects at most -- the resolved pointers, the status block and one 256-byte
# staging buffer -- and allocates nothing dynamically at all.
m18diag-size: $(M18ELF) $(M18BIN)
	@$(PYTHON) tools/sizereport.py $(M18ELF) $(M18BIN) \
	   --map $(M18BUILD)/$(M18TARGET).map --budget $(JIT_LIMIT)

# ---- verification -----------------------------------------------------------

m18diag-verify: $(M18ELF)
	@echo "== image position independence =="
	@READELF="$(READELF)" OBJDUMP="$(OBJDUMP)" sh tools/check_image.sh $(M18ELF)
	@echo "== relocations (NON-MASKING pipeline) =="
	@$(PYTHON) tools/verify_reloc.py $(M18ELF) $(M18OBJS) \
	     > $(M18BUILD)/reloc.log 2>&1; \
	 st=$$?; \
	 tail -12 $(M18BUILD)/reloc.log; \
	 if [ $$st -ne 0 ]; then \
	     echo "M18 VERIFY FAILED: the relocation check exited $$st."; \
	     echo "  Full output: $(M18BUILD)/reloc.log"; \
	     exit $$st; \
	 fi
	@echo "== symbol kinds =="
	@$(PYTHON) tools/check_symtypes.py $(M18OBJS) | tail -4
	@echo "== closure: NO EMULATOR CORE AT ALL =="
# --core defaults to `none`, which is the correct setting here for the same
# reason it is correct for M0: there is no emulator in this image, so ANY core
# symbol is a leak rather than an expected member of the closure.
	@$(PYTHON) tools/check_closure.py $(M18OBJS)
	@echo "== forbidden symbols in the linked image (.bss tripwire included) =="
	@$(PYTHON) tools/check_forbidden.py $(M18ELF) $(M18OBJS)
	@echo "== no undefined symbols =="
	@undef=$$($(READELF) -sW $(M18ELF) 2>/dev/null \
	            | awk '$$7 == "UND" && $$8 != "" { print $$8 }' | sort -u); \
	 if [ -n "$$undef" ]; then \
	     echo "M18 VERIFY FAILED: the image has undefined symbols:"; \
	     echo "$$undef" | sed "s/^/  /"; \
	     exit 1; \
	 fi; \
	 echo "zero undefined symbols (expected: everything goes through dlsym)"
	@echo "== ***** THE STANDALONE OBJECT SET ***** =="
	@missing=""; \
	 for want in $(M18_REQUIRED_OBJS); do \
	     found=0; \
	     for o in $(M18OBJS); do \
	         case "$$o" in */$$want) found=1;; esac; \
	     done; \
	     if [ $$found -eq 0 ]; then missing="$$missing $$want"; fi; \
	 done; \
	 if [ -n "$$missing" ]; then \
	     echo "M18 VERIFY FAILED: a required object is NOT on the link"; \
	     echo "  line:$$missing"; \
	     exit 1; \
	 fi; \
	 n=0; for o in $(M18OBJS); do n=$$((n+1)); done; \
	 if [ "$$n" -ne 5 ]; then \
	     echo "M18 VERIFY FAILED: the link line has $$n objects, expected"; \
	     echo "  exactly 5. M18-0 is a STANDALONE diagnostic; anything extra"; \
	     echo "  means an emulator or a write path has arrived."; \
	     exit 1; \
	 fi; \
	 echo "exactly five objects, all five required ones present"
	@echo "== ***** NO EMULATOR, NO GBA ADAPTER, NO AUDIO, NO SAVE, NO PAGING ***** =="
	@present=""; \
	 for bad in $(M18_FORBIDDEN_OBJS); do \
	     for o in $(M18OBJS); do \
	         case "$$o" in */$$bad) present="$$present $$bad";; esac; \
	     done; \
	 done; \
	 if [ -n "$$present" ]; then \
	     echo "M18 VERIFY FAILED: a forbidden object is on the link line:"; \
	     echo " $$present"; \
	     exit 1; \
	 fi; \
	 echo "no gpSP, GBA adapter, audio, save-writer or paging object is linked"
# A NAME check on the object list is an argument about this Makefile. This is
# the check on the ARTIFACT: no symbol belonging to an emulator, an adapter, the
# audio layer or the save layer may be DEFINED or REFERENCED anywhere in the
# image. A reference is just as much a failure as a definition, because the link
# would have to be satisfied from somewhere.
	@sym=$$($(NM) $(M18ELF) 2>/dev/null \
	          | grep -cE " [A-Za-z] (gba_|gpsp_|m13|m16|plat_audio_|plat_savedata_|netpacket_|netplay_|rfu_|serial)" ); \
	 if [ "$$sym" != "0" ]; then \
	     echo "M18 VERIFY FAILED: $$sym emulator/adapter/audio/save symbol(s)"; \
	     echo "  are present in the image. M18-0 links none of those layers,"; \
	     echo "  and it must NOT carry the gpSP netpacket seam either."; \
	     $(NM) $(M18ELF) | grep -E " [A-Za-z] (gba_|gpsp_|m13|m16|plat_audio_|plat_savedata_|netpacket_|netplay_|rfu_|serial)" | sed "s/^/    /"; \
	     exit 1; \
	 fi; \
	 echo "no gba_*, gpsp_*, netpacket_*, netplay_*, audio or savedata symbol"
	@echo "== ***** THE NETWORK LAYER IS LINKED AND REACHED ***** =="
# The primitive the whole milestone turns on. If plat_net_open were absent or
# unreferenced, the diagnostic would measure nothing and still "pass".
	@miss=""; \
	 for s in plat_net_init plat_net_open plat_net_send plat_net_recv \
	          plat_net_shutdown plat_net_status plat_net_module_loaded; do \
	     $(NM) $(M18ELF) 2>/dev/null | grep -qE " [Tt] $$s$$" \
	         || miss="$$miss $$s"; \
	 done; \
	 if [ -n "$$miss" ]; then \
	     echo "M18 VERIFY FAILED: a network entry point is missing:$$miss"; \
	     exit 1; \
	 fi; \
	 echo "every plat_net_* entry point is DEFINED in the image"
	@if ! $(NM) $(M18BUILD)/m18main.o 2>/dev/null \
	        | grep -qE " U plat_net_open$$"; then \
	     echo "M18 VERIFY FAILED: m18main.o does not REFERENCE plat_net_open."; \
	     echo "  The diagnostic would open no socket and would pass vacuously."; \
	     exit 1; \
	 fi; \
	 echo "m18main.o REFERENCES plat_net_open (a socket is really created)"
	@echo "== ***** libSceNet IS NOT NAMED ANYWHERE IN THE IMAGE ***** =="
# The U3 claim, checked on the artefact. A module-load fallback added later
# would put this string into the image and this gate would fail -- which is
# exactly the protection the ACT asked for: the hardware result must be able to
# distinguish the native path from a module-assisted one.
#
# ***** THE WHOLE IMAGE IS SEARCHED, NOT `--only-section=.rodata`. *****
# THERE IS NO OUTPUT SECTION NAMED .rodata. runtime/linker.ld:64-67 folds
# *(.rodata) and *(.rodata.*) INTO the single .text output section so the
# strings stay inside the read-only JIT-backed mapping. `objcopy
# --only-section=.rodata` therefore produces an EMPTY file and every string
# check against it PASSES VACUOUSLY -- the precise bug Makefile:2658 records
# paying for once already. Searching the flat binary covers .text, the folded
# rodata and .data's initial image in one pass, and cannot silently degrade.
	@$(OBJCOPY) -O binary $(M18ELF) $(M18BUILD)/image.bin
	@if [ ! -s $(M18BUILD)/image.bin ]; then \
	     echo "M18 VERIFY FAILED: the image extraction produced 0 bytes, so"; \
	     echo "  every string check below would pass vacuously."; \
	     exit 1; \
	 fi
# ***** THE PAYLOAD MUST NAME NO MODULE, IN ANY CASE. *****
# Host verification failed here once, and the cause was the diagnostic itself:
# apps/m18diag/main.c logged "... and bound, with NO libSceNet.sprx load" -- a
# literal ASSERTING the module's ABSENCE. To a binary string search that is
# indistinguishable from a real module load, so the diagnostic and the verifier
# contradicted each other. It was the ONLY case-sensitive hit in the image
# (offset 18959, from m18main.o); the two on-screen labels were uppercase
# LIBSCENET and survived only by casing luck.
#
# The fix removed the name from the payload -- the screen now reads
# "MODULE LOAD  NO" -- rather than relaxing this gate. Because no form of the
# name remains, the search is now case-INSENSITIVE too: a real loader could pass
# any casing, and casing must never be the thing that saves us.
	@if grep -aiq "libscenet" $(M18BUILD)/image.bin; then \
	     echo "M18 VERIFY FAILED: the module name libSceNet appears in the image."; \
	     echo "  M18-0 must prove the NATIVE path works WITHOUT that module;"; \
	     echo "  a hidden fallback makes the hardware result uninterpretable."; \
	     echo "  If this is only diagnostic TEXT, do NOT relax this gate --"; \
	     echo "  reword the literal. Module names belong in comments only,"; \
	     echo "  because comments do not reach the binary."; \
	     exit 1; \
	 fi; \
	 echo "no libSceNet reference anywhere in the image (any casing)"
# Positive control: the extraction really can see strings this image DOES
# contain. Without this, a broken carve and a clean result look identical.
	@if ! grep -aq "NET: resolving BSD socket primitives" $(M18BUILD)/image.bin; then \
	     echo "M18 VERIFY FAILED: a string known to be in runtime/net.c was NOT"; \
	     echo "  found in the extracted image, so the libSceNet check above is"; \
	     echo "  not trustworthy."; \
	     exit 1; \
	 fi; \
	 echo "positive control: a known net.c string IS present, so the search works"
# A genuine DEPENDENCY check, and the reason the gate above can stay strict
# without being the only evidence. The string gate proves no module NAME is in
# the image; this proves the network objects contain no CALL to the loader.
# Measured on the failing build: sceKernelLoadStartModule appears ONLY in
# platform.o (three references -- libScePad.sprx for the exit button), while
# net.o and m18main.o carry zero. That is what must stay true.
	@if $(NM) $(M18BUILD)/net.o 2>/dev/null | grep -qE "sceKernelLoadStartModule"; then \
	     echo "M18 VERIFY FAILED: runtime/net.o references sceKernelLoadStartModule."; \
	     echo "  The network layer must load NO module on ANY path, including as"; \
	     echo "  a fallback, or a PASS on hardware is uninterpretable."; \
	     exit 1; \
	 fi; \
	 echo "runtime/net.o does NOT reference sceKernelLoadStartModule"
	@if $(NM) $(M18BUILD)/m18main.o 2>/dev/null | grep -qE "sceKernelLoadStartModule"; then \
	     echo "M18 VERIFY FAILED: m18main.o references sceKernelLoadStartModule."; \
	     echo "  The diagnostic must not load a module on the app side either."; \
	     exit 1; \
	 fi; \
	 echo "m18main.o does NOT reference sceKernelLoadStartModule"
	@if $(NM) $(M18ELF) 2>/dev/null | grep -qE "sceKernelLoadStartModule"; then \
	     echo "M18 VERIFY NOTE: sceKernelLoadStartModule is referenced in the image"; \
	     echo "  -- that is runtime/platform.c loading libScePad.sprx for the exit"; \
	     echo "  button, NOT the network layer. The two per-object gates above are"; \
	     echo "  what prove the network path never loads a module."; \
	 fi
	@echo "== ***** M18 DOES NOT ENTER ANY EXISTING OBJECT LIST ***** =="
# THE QUESTION THIS GATE ASKS. It is NOT "does the name m18main.o appear in this
# Makefile" -- inside the M18 block that name appears constantly: in
# $(M18_REQUIRED_OBJS), in the compile rules, in comments, and in this gate's OWN
# failure messages. The real question is whether an M18 object entered a
# PRE-EXISTING, NON-M18 object list or target, because ONLY that could pull a new
# object into a frozen image and change it.
#
# The previous form asked the wrong question and self-matched: it grepped the
# whole file and filtered only lines containing the literal "M18BUILD", so every
# ordinary mention inside this very block -- the bare-name list, the echoes, the
# comments, and the gate's own two failure messages -- was reported as
# contamination. It could never pass, and it never tested anything real.
#
# The search region is now bounded STRUCTURALLY by the sentinel comments that
# bracket this block, and ONLY the lines outside that range are searched. The
# FIRST @@M18_BLOCK_BEGIN@@ and the LAST @@M18_BLOCK_END@@ in the file are the
# boundaries; the copies that appear inside these recipe lines lie between those
# two, so they can never act as boundaries themselves. No exclusion list is
# maintained, and adding prose or echoes to the M18 block can never trip it.
#
# The name test is also STRICTER than before, not weaker. The old pattern
# required a "$(...BUILD)/" prefix, so a pre-existing list written with bare
# names -- exactly the style of $(M18_REQUIRED_OBJS) above -- would have slipped
# through unseen. The region is now matched on the object name itself, guarded on
# BOTH sides: the left guard stops ethernet.o, subnet.o, telnet.o and sceNet.o
# from masquerading as net.o, and the right guard stops net.obj from doing so.
# The guards admit '/' and '(' and ')' so $(M0BUILD)/net.o is still caught.
	@begin=$$(grep -n '@@M18_BLOCK_BEGIN@@' Makefile | head -1 | cut -d: -f1); \
	 end=$$(grep -n '@@M18_BLOCK_END@@' Makefile | tail -1 | cut -d: -f1); \
	 if [ -z "$$begin" ] || [ -z "$$end" ] || [ "$$begin" -lt 2 ] \
	    || [ "$$begin" -ge "$$end" ]; then \
	     echo "M18 VERIFY FAILED: the M18 block sentinels are missing or out of"; \
	     echo "  order (begin='$$begin' end='$$end'). This gate will NOT pass"; \
	     echo "  vacuously -- restore the sentinel comments before rerunning."; \
	     exit 1; \
	 fi; \
	 outside=$$(grep -n '' Makefile \
	              | sed -n "1,$$(($$begin-1))p;$$(($$end+1)),\$$p"); \
	 if ! printf '%s\n' "$$outside" | grep -q 'M13C_OWN_OBJS'; then \
	     echo "M18 VERIFY FAILED: positive control -- M13C_OWN_OBJS is NOT in the"; \
	     echo "  region being searched, so the region is wrong and a real"; \
	     echo "  contamination could go unnoticed."; \
	     exit 1; \
	 fi; \
	 if ! printf '%s\n' "$$outside" | grep -q 'M16C_OWN_OBJS'; then \
	     echo "M18 VERIFY FAILED: positive control -- M16C_OWN_OBJS is NOT in the"; \
	     echo "  region being searched, so the region is wrong and a real"; \
	     echo "  contamination could go unnoticed."; \
	     exit 1; \
	 fi; \
	 echo "positive control: the pre-M18 object lists ARE inside the search region"; \
	 bad=$$(printf '%s\n' "$$outside" \
	          | grep -E '(^|[^A-Za-z0-9_.])net\.o([^A-Za-z0-9_]|$$)' || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M18 VERIFY FAILED: runtime/net.o is referenced OUTSIDE the M18"; \
	     echo "  block. That would change a frozen artifact:"; \
	     echo "$$bad" | sed "s/^/    /"; \
	     exit 1; \
	 fi; \
	 echo "runtime/net.o entered NO non-M18 object list or target"; \
	 bad=$$(printf '%s\n' "$$outside" \
	          | grep -E '(^|[^A-Za-z0-9_.])m18main\.o([^A-Za-z0-9_]|$$)' || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M18 VERIFY FAILED: m18main.o is referenced OUTSIDE the M18 block:"; \
	     echo "$$bad" | sed "s/^/    /"; \
	     exit 1; \
	 fi; \
	 echo "m18main.o entered NO non-M18 object list or target"
	@echo "== ***** THE BUDGETS ARE NOT RAISED ***** =="
	@if [ "$(JIT_LIMIT)" != "524288" ]; then \
	     echo "M18 VERIFY FAILED: JIT_LIMIT is $(JIT_LIMIT), expected 524288."; \
	     exit 1; \
	 fi; \
	 echo "the JIT budget is still 524,288 bytes"
# .bss is measured from the LINKER MARKERS, not from `readelf | awk strtonum`.
# strtonum is a gawk extension: on a host where awk is mawk or busybox awk it
# is not merely slower, it is a SYNTAX ERROR, and the surrounding `|| bss=0`
# style would then report a .bss of zero and PASS. The marker pair is the same
# nm+awk idiom Makefile:2700-2701 already uses for __rodata_start/__rodata_end,
# and POSIX shell arithmetic understands a 0x prefix directly.
	@bs=$$($(NM) $(M18ELF) 2>/dev/null \
	         | awk '$$NF == "__bss_start" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' \
	         | head -1); \
	 be=$$($(NM) $(M18ELF) 2>/dev/null \
	         | awk '$$NF == "__bss_end" && $$1 ~ /^[0-9a-fA-F]+$$/ { print $$1 }' \
	         | head -1); \
	 if [ -z "$$bs" ] || [ -z "$$be" ]; then \
	     echo "M18 VERIFY FAILED: __bss_start/__bss_end were not found in"; \
	     echo "  $(M18ELF), so the .bss budget CANNOT be checked. Refusing to"; \
	     echo "  report a size this rule did not measure."; \
	     exit 1; \
	 fi; \
	 n=$$(( 0x$$be - 0x$$bs )); \
	 echo ".bss is $$n bytes (__bss_start=$$bs __bss_end=$$be)"; \
	 if [ "$$n" -le 0 ]; then \
	     echo "M18 VERIFY FAILED: computed a non-positive .bss size."; \
	     exit 1; \
	 fi; \
	 if [ "$$n" -gt 2097152 ]; then \
	     echo "M18 VERIFY FAILED: .bss exceeds the 2,097,152 tripwire."; \
	     exit 1; \
	 fi; \
	 echo ".bss is within the standing 2,097,152 tripwire"
# ---------------------------------------------------------------------------
# THE fcntl PROBES ARE MEASUREMENTS, NOT CORRECTIONS.
#
# This block is ADDITIVE -- it weakens nothing above it. It exists because the
# characterization run has a failure mode that no other gate can see: somebody
# "helpfully" turning a probe into the fix, by feeding the flags F_GETFL
# returned into the F_SETFL beside it, or by letting the M18-0b probe matrix
# quietly keep a socket that one of its own probes managed to make non-blocking.
# Either would change two things at once and make the hardware result
# uninterpretable, because a PASS could no longer be attributed to one change.
#
# M18-0b ADDED FOUR fcntl CALLS INSIDE THE NONBLOCK FAILURE BRANCH. A textual
# "does net.c branch on the probe?" scan is no longer a sufficient description
# of the invariant, so the scan is KEPT and JOINED BY STRUCTURAL CHECKS that
# compare LINE NUMBERS. The six things proven below are exactly the six the
# M18-0b ACT requires:
#
#   1. the operative F_SETFL call is source-identical to the previous hardware
#      runs -- and now also UNIQUE, so a probe cannot impersonate it;
#   2. no probe result controls network behaviour in runtime/net.c;
#   3. every probe lives strictly INSIDE the already-doomed failure branch,
#      between the branch guard and the close;
#   4. bind remains reachable only after the ORIGINAL operative F_SETFL
#      succeeds -- the bind call site is beyond the failure return;
#   5. the nonblock failure still closes the descriptor -- the close precedes
#      that return;
#   6. no probe can turn FAIL NONBLOCK into success -- the branch has exactly
#      ONE exit, it is the PLAT_NET_ENONBLOCK return, there is no goto, and
#      nothing in the branch marks the socket live again.
#
# Only runtime/net.c is scanned. apps/m18diag/main.c DOES branch on the probe
# results, and must: that is the screen and the log REPORTING the measurement,
# which is the entire point. Reporting is not acting.
#
# ---------------------------------------------------------------------------
# EVERY CHECK BELOW GREPS $(M18CODE), NOT runtime/net.c.
#
# THIS IS NOT A REFINEMENT. IT REPAIRS A GATE THAT STOPPED A HOST BUILD FOR NO
# REASON, AND IT REPAIRS THE WHOLE CLASS, NOT THE INSTANCE.
#
# The first version of these checks read RAW LINES. A raw line cannot tell code
# from commentary, so "the nonblock failure branch has exactly one exit" was
# implemented as "no line between the guard and the return matches \<return\>".
# On the M18-0b host run that found TWO lines and failed the build:
#
#     net.c:435      if (rc < 0) {
#     net.c:442          * cleared fd, the cleared opened flag and the
#                          PLAT_NET_ENONBLOCK return        <-- PROSE
#     net.c:534          return PLAT_NET_ENONBLOCK;         <-- the real exit
#
# Line 442 is the fourth line of a sentence inside the block comment opened at
# 436 and closed at 492. It emits no instructions. The INVARIANT held; the
# PREDICATE was wrong, because it was a claim about line text wearing the
# costume of a claim about control flow.
#
# Note which line did NOT trip it: 445, which says in capitals that there is no
# RETURN in the block, escaped only because it is uppercase. The gate was one
# comment reflow away from failing whatever the code did. runtime/net.c carries
# the word "return" in prose at lines 9, 94, 168 and 174 as well.
#
# THE FIX IS NOT TO EXCLUDE LINE 442, and it is not to reword the comment.
# Either would leave the predicate wrong and merely move the trip wire. The fix
# is to stop handing commentary to checks whose invariant is about executable
# code -- so the comment at 442 may say whatever is true, and the gate keeps
# measuring control flow.
#
# WHAT THIS RETIRED. Two "EDITOR BEWARE" traps used to live here: a standing
# rule that prose in net.c must say "gfl" rather than "getfl", and a note that
# the scan keywords are word-anchored to stop "before" matching "for". Both
# were coping strategies for the raw-line defect. The first is now DELETED --
# net.c may write "getfl" in a sentence. The word anchoring is KEPT, because it
# costs nothing and is still correct for code, but it is no longer load-bearing.
#
# WHAT IS DELIBERATELY *NOT* ROUTED THROUGH THE CODE-ONLY VIEW: the image and
# object checks earlier in this target. "no libSceNet string in image.bin", its
# positive control, and the $(NM) dependency scans are about LITERAL BYTES in a
# BUILT ARTIFACT, not about C code -- a module name in a comment genuinely is
# harmless there, which is exactly why those gates search the image instead of
# the source. Routing them would weaken them.
#
# NON-VACUITY. The filter is proved to work before it is trusted, in three
# independent ways, all of which abort verification on failure:
#
#   * tools/m18_code_only.py --selftest runs a synthetic corpus that asserts
#     BOTH directions -- executable returns survive, commented and quoted ones
#     do not. A filter that blanked everything would satisfy one direction
#     alone and prove nothing, so both are required.
#   * the code-only view must have the SAME LINE COUNT as the source, checked
#     here as well as inside the script. If it ever differed, every line number
#     compared below would be naming the wrong line -- silently.
#   * the filter must be shown to have REMOVED at least one \<return\> while
#     KEEPING the operative one. That is the two-sided positive control: it
#     proves the stage is neither inert nor overzealous, without naming a line
#     number that a future edit would invalidate.
#
# The anchor checks are anti-vacuous too: an anchor that cannot be found, is
# not UNIQUE, or is out of order FAILS LOUDLY rather than silently proving
# nothing about whichever candidate head -1 happened to pick.
# ---------------------------------------------------------------------------
	@echo "== ***** THE fcntl PROBES MEASURE, THEY DO NOT CORRECT ***** =="
# The filter is proved BEFORE it is used. If the self-test fails, verification
# stops here rather than running structural gates through a stage that cannot
# separate code from commentary -- a broken filter must never produce a GREEN
# gate, only a stopped build.
	@$(PYTHON) tools/m18_code_only.py --selftest
	@$(PYTHON) tools/m18_code_only.py runtime/net.c $(M18CODE)
# HARD ANTI-VACUITY CHECK, independent of the script's own. Every gate below
# compares LINE NUMBERS between the code-only view and runtime/net.c, so the
# two must agree line for line. A filter that dropped or added a single line
# would leave every comparison below quietly measuring the wrong lines.
	@a=$$(wc -l < runtime/net.c | tr -d '[:space:]'); \
	 b=$$(wc -l < $(M18CODE) | tr -d '[:space:]'); \
	 if [ "$$a" != "$$b" ]; then \
	     echo "M18 VERIFY FAILED: the code-only view of runtime/net.c has $$b"; \
	     echo "  lines but the source has $$a. Every line-number invariant"; \
	     echo "  below would be naming the wrong line. Refusing to continue."; \
	     exit 1; \
	 fi; \
	 echo "code-only view is line-for-line with runtime/net.c ($$a lines)"
# TWO-SIDED POSITIVE CONTROL. The filter must have removed at least one
# \<return\> -- runtime/net.c demonstrably discusses returns in prose -- and
# must still contain exactly one operative PLAT_NET_ENONBLOCK return. An inert
# filter fails the first half; an overzealous one that ate code fails the
# second. Neither half names a line number, so reflowing a comment cannot
# invalidate this control.
	@s=$$(grep -c '\<return\>' runtime/net.c); \
	 f=$$(grep -c '\<return\>' $(M18CODE)); \
	 k=$$(grep -c 'return PLAT_NET_ENONBLOCK;' $(M18CODE)); \
	 if [ "$$f" -ge "$$s" ]; then \
	     echo "M18 VERIFY FAILED: the code-only filter removed NOTHING"; \
	     echo "  ($$s matches for \\<return\\> in the source, $$f in the"; \
	     echo "  filtered view). runtime/net.c is known to use the word in"; \
	     echo "  prose, so the stage is inert and the gates below would be"; \
	     echo "  reading raw lines again without saying so."; \
	     exit 1; \
	 fi; \
	 if [ "$$k" -ne 1 ]; then \
	     echo "M18 VERIFY FAILED: the code-only view contains $$k operative"; \
	     echo "  PLAT_NET_ENONBLOCK returns, expected exactly 1. The filter is"; \
	     echo "  eating EXECUTABLE CODE, which would make every gate below"; \
	     echo "  pass vacuously."; \
	     exit 1; \
	 fi; \
	 echo "filter control: \\<return\\> $$s in source -> $$f in code-only view, operative return kept"
	@if ! grep -qE '^#define[[:space:]]+NET_F_GETFL[[:space:]]+3[[:space:]]*$$' $(M18CODE); then \
	     echo "M18 VERIFY FAILED: NET_F_GETFL is not defined as 3 in"; \
	     echo "  runtime/net.c. The screen and the log both PRINT the command"; \
	     echo "  number they used, so the source and the report must not be"; \
	     echo "  able to drift apart."; \
	     exit 1; \
	 fi; \
	 if ! grep -qE '^#define[[:space:]]+NET_F_GETFD[[:space:]]+1[[:space:]]*$$' $(M18CODE); then \
	     echo "M18 VERIFY FAILED: NET_F_GETFD is not defined as 1."; \
	     exit 1; \
	 fi; \
	 if ! grep -qE '^#define[[:space:]]+NET_F_SETFD[[:space:]]+2[[:space:]]*$$' $(M18CODE); then \
	     echo "M18 VERIFY FAILED: NET_F_SETFD is not defined as 2."; \
	     exit 1; \
	 fi; \
	 echo "fcntl commands 3/1/2 match the numbers the screen and the log print"
# Read from the code-only view, so a COMMENTED-OUT or merely DISCUSSED #define
# cannot satisfy these. The trailing [[:space:]]*$$ is what makes that work and
# is also stricter than the old form: the value must be the last thing on the
# line, because these three defines carry trailing comments in the source and
# the filter leaves their columns behind as spaces.
	@if ! grep -qE '^#define[[:space:]]+NET_PROBE_SETFL_A[[:space:]]+2[[:space:]]*$$' $(M18CODE) \
	   || ! grep -qE '^#define[[:space:]]+NET_PROBE_SETFL_B[[:space:]]+6[[:space:]]*$$' $(M18CODE) \
	   || ! grep -qE '^#define[[:space:]]+NET_PROBE_SETFD_ARG[[:space:]]+0[[:space:]]*$$' $(M18CODE); then \
	     echo "M18 VERIFY FAILED: the probe arguments are not the frozen"; \
	     echo "  literals 2 / 6 / 0. They must NOT be derived from getfl_rc at"; \
	     echo "  runtime: a probe argument computed from this run's measurement"; \
	     echo "  is a different experiment from run to run and cannot be"; \
	     echo "  compared with the last one."; \
	     exit 1; \
	 fi; \
	 echo "probe arguments are frozen literals (2, 6, 0), not derived from any measurement"
# Counted in the code-only view. That matters in BOTH directions here: a
# comment QUOTING this call string would inflate the count to 2 and block the
# build, and a commented-out copy must not be able to stand in for the real one.
	@n=$$(grep -c '(u64)(s64)fd, (u64)NET_F_SETFL, (u64)NET_O_NONBLOCK, 0, 0, 0);' $(M18CODE)); \
	 if [ "$$n" -ne 1 ]; then \
	     echo "M18 VERIFY FAILED: runtime/net.c must contain EXACTLY ONE"; \
	     echo "  unmodified operative F_SETFL call; found $$n."; \
	     echo "  This build is a CHARACTERIZATION. F_SETFL must stay EXACTLY as"; \
	     echo "  the last hardware run left it, or the -1 that run reported"; \
	     echo "  cannot be compared with whatever this one reports. Requiring"; \
	     echo "  exactly one also stops a PROBE from impersonating it."; \
	     exit 1; \
	 fi; \
	 echo "the operative F_SETFL call is source-identical to the previous hardware runs, and unique"
# THE ORIGINAL TRAP, NOW DISARMED. This is the scan that used to force net.c's
# prose to say "gfl" instead of "getfl": a sentence pairing either token with
# the word "if" was indistinguishable from a branch. Reading the code-only view
# removes that coupling entirely -- comments may now discuss getfl and probe_
# freely, and only real control flow can trip this. The line numbers it prints
# are true runtime/net.c line numbers.
	@bad=$$(grep -nE '(\<(if|while|for|switch)\>|\?|&&|\|\|)[^;]*(getfl|probe_)' \
	          $(M18CODE) || true); \
	 if [ -n "$$bad" ]; then \
	     echo "M18 VERIFY FAILED: runtime/net.c has control flow that depends"; \
	     echo "  on an fcntl probe result:"; \
	     echo "$$bad" | sed "s/^/    /"; \
	     echo "  The probes are measurements: recorded, printed, then ignored."; \
	     echo "  Acting on one would change networking behaviour in the very"; \
	     echo "  run that is supposed to characterise it."; \
	     exit 1; \
	 fi; \
	 echo "no control flow in runtime/net.c depends on any probe result"
# ANCHORS MUST BE UNAMBIGUOUS, NOT MERELY PRESENT. These used to be located
# with head -1, which silently picks the first of however many candidates
# exist. If a second "if (rc < 0) {" were ever added ABOVE this one, head -1
# would retarget every comparison below to the wrong branch and still print a
# confident PASS. Four of the five anchors are therefore asserted UNIQUE.
#
# net_close_fd(fd) is the exception and cannot be unique: runtime/net.c closes
# the descriptor on the bind failure path too. Its invariant is a RANGE
# invariant -- exactly one close strictly between the guard and the return --
# which is the property actually wanted, and is stronger than head -1 was.
#
# SAID PLAINLY so the printed sentence is not over-read: because `clo` is
# SELECTED from the open interval (guard, ret), the "guard < clo" and
# "clo < ret" terms of the ordering test below cannot fire. They are a
# restatement, not a second measurement. The thing that actually enforces
# "the failure path closes the fd before it returns" is the nclo -ne 1 test,
# which fires when that interval contains no close, or more than one.
	@nsetfl=$$(grep -c 'N\.st\.nonblock_rc = rc;' $(M18CODE)); \
	 nguard=$$(grep -c 'if (rc < 0) {' $(M18CODE)); \
	 nret=$$(grep -c 'return PLAT_NET_ENONBLOCK;' $(M18CODE)); \
	 nbnd=$$(grep -c 'N\.gadget, N\.fn_bind' $(M18CODE)); \
	 if [ "$$nsetfl" -ne 1 ] || [ "$$nguard" -ne 1 ] \
	    || [ "$$nret" -ne 1 ] || [ "$$nbnd" -ne 1 ]; then \
	     echo "M18 VERIFY FAILED: a structural anchor in runtime/net.c is not"; \
	     echo "  UNIQUE, so the line-number comparisons below would be about"; \
	     echo "  whichever candidate happened to come first:"; \
	     echo "    N.st.nonblock_rc = rc;      $$nsetfl (want 1)"; \
	     echo "    if (rc < 0) {               $$nguard (want 1)"; \
	     echo "    return PLAT_NET_ENONBLOCK;  $$nret (want 1)"; \
	     echo "    N.gadget, N.fn_bind         $$nbnd (want 1)"; \
	     echo "  Refusing to report an invariant this rule did not measure."; \
	     exit 1; \
	 fi; \
	 setfl=$$(grep -n 'N\.st\.nonblock_rc = rc;' $(M18CODE) | cut -d: -f1); \
	 guard=$$(grep -n 'if (rc < 0) {' $(M18CODE) | cut -d: -f1); \
	 ret=$$(grep -n 'return PLAT_NET_ENONBLOCK;' $(M18CODE) | cut -d: -f1); \
	 bnd=$$(grep -n 'N\.gadget, N\.fn_bind' $(M18CODE) | cut -d: -f1); \
	 clo=$$(grep -n 'net_close_fd(fd);' $(M18CODE) | cut -d: -f1 \
	        | awk -v g=$$guard -v r=$$ret '$$1 > g && $$1 < r'); \
	 nclo=$$(printf '%s\n' "$$clo" | grep -c '[0-9]'); \
	 if [ "$$nclo" -ne 1 ]; then \
	     echo "M18 VERIFY FAILED: the nonblock failure branch must contain"; \
	     echo "  EXACTLY ONE net_close_fd(fd) strictly between the guard"; \
	     echo "  ($$guard) and the return ($$ret); found $$nclo ('$$clo')."; \
	     echo "  A failure path that does not close the descriptor hands back a"; \
	     echo "  BLOCKING socket, which is the watchdog hazard this rung exists"; \
	     echo "  to avoid."; \
	     exit 1; \
	 fi; \
	 if [ "$$setfl" -ge "$$guard" ] || [ "$$guard" -ge "$$clo" ] \
	    || [ "$$clo" -ge "$$ret" ] || [ "$$ret" -ge "$$bnd" ]; then \
	     echo "M18 VERIFY FAILED: plat_net_open() no longer runs in the order"; \
	     echo "  F_SETFL -> failure branch -> close -> return -> bind."; \
	     echo "  measured: setfl=$$setfl guard=$$guard close=$$clo"; \
	     echo "            return=$$ret bind=$$bnd"; \
	     echo "  bind must be reachable ONLY after the operative F_SETFL"; \
	     echo "  succeeds, and every nonblock failure must close the fd first."; \
	     exit 1; \
	 fi; \
	 echo "fail-closed order holds: F_SETFL($$setfl) < branch($$guard) < close($$clo) < return($$ret) < bind($$bnd)"
	@guard=$$(grep -n 'if (rc < 0) {' $(M18CODE) | cut -d: -f1); \
	 ret=$$(grep -n 'return PLAT_NET_ENONBLOCK;' $(M18CODE) | cut -d: -f1); \
	 if [ -z "$$guard" ] || [ -z "$$ret" ]; then \
	     echo "M18 VERIFY FAILED: cannot locate the nonblock failure branch."; \
	     exit 1; \
	 fi; \
	 clo=$$(grep -n 'net_close_fd(fd);' $(M18CODE) | cut -d: -f1 \
	        | awk -v g=$$guard -v r=$$ret '$$1 > g && $$1 < r'); \
	 if [ -z "$$clo" ]; then \
	     echo "M18 VERIFY FAILED: the nonblock failure branch closes no"; \
	     echo "  descriptor between the guard ($$guard) and the return ($$ret)."; \
	     exit 1; \
	 fi; \
	 ncall=$$(grep -c 'N\.st\.probe_[a-z0-9]*_rc = (s32)NC(' $(M18CODE)); \
	 if [ "$$ncall" -ne 4 ]; then \
	     echo "M18 VERIFY FAILED: expected EXACTLY 4 fcntl probe calls in"; \
	     echo "  runtime/net.c (F_GETFD, F_SETFD 0, F_SETFL 2, F_SETFL 6);"; \
	     echo "  found $$ncall."; \
	     exit 1; \
	 fi; \
	 out=$$(grep -n 'N\.st\.probe_' $(M18CODE) | cut -d: -f1 \
	        | awk -v g=$$guard -v c=$$clo '$$1 <= g || $$1 >= c'); \
	 if [ -n "$$out" ]; then \
	     echo "M18 VERIFY FAILED: a probe in runtime/net.c sits OUTSIDE the"; \
	     echo "  already-doomed nonblock failure branch (lines $$guard..$$clo)."; \
	     echo "  Offending lines:"; \
	     echo "$$out" | sed "s/^/    line /"; \
	     echo "  A probe outside that branch would run on a LIVE descriptor and"; \
	     echo "  would no longer be measurement-only."; \
	     exit 1; \
	 fi; \
	 rl=$$(grep -n '\<return\>' $(M18CODE) \
	       | awk -F: -v g=$$guard -v r=$$ret '$$1 > g && $$1 <= r {print $$1}'); \
	 if [ "$$rl" != "$$ret" ]; then \
	     echo "M18 VERIFY FAILED: the nonblock failure branch does not have"; \
	     echo "  exactly one EXECUTABLE exit. return lines found in it: '$$rl';"; \
	     echo "  expected only $$ret, the PLAT_NET_ENONBLOCK return."; \
	     echo "  A second exit could let a successful probe escape the branch."; \
	     echo "  These line numbers come from the CODE-ONLY view, so a comment"; \
	     echo "  or a string containing the word return is NOT what tripped"; \
	     echo "  this -- the lines above carry real control flow."; \
	     exit 1; \
	 fi; \
	 ng=$$(grep -n '\<goto\>' $(M18CODE) \
	       | awk -F: -v g=$$guard -v r=$$ret '$$1 > g && $$1 < r' \
	       | wc -l | tr -d '[:space:]'); \
	 nl=$$(grep -n 'N\.st\.opened = 1' $(M18CODE) \
	       | awk -F: -v g=$$guard -v r=$$ret '$$1 > g && $$1 < r' \
	       | wc -l | tr -d '[:space:]'); \
	 if [ "$$ng" -ne 0 ] || [ "$$nl" -ne 0 ]; then \
	     echo "M18 VERIFY FAILED: the nonblock failure branch contains a goto"; \
	     echo "  ($$ng) or re-marks the socket live ($$nl). No probe may turn"; \
	     echo "  FAIL NONBLOCK into success -- not even F_SETFL 6 returning 0."; \
	     exit 1; \
	 fi; \
	 echo "all 4 probes are inside the doomed branch; one exit ($$ret), no goto, socket never re-opened"
	@echo "== M18 VERIFY OK =="

# ---- the Lua payload --------------------------------------------------------
#
# ***** ITS OWN TEMPLATE. lua/m0.lua.in, lua/m13c.lua.in AND lua/m16c.lua.in ARE
# NOT EDITED AND NOT REUSED IN PLACE. ***** The delivery machinery is
# hardware-proven from M0 through M17 and is reproduced rather than re-derived;
# only the labels and the status decode differ, because M18-0's band is -50..-54.
M18_LUA_TEMPLATE := lua/m18diag.lua.in
M18_LUA_OUT      := $(M18BUILD)/$(M18TARGET).lua

m18diag-lua: m18diag-check $(M18BIN) $(M18ELF)
	@$(PYTHON) tools/mklua.py $(M18_LUA_TEMPLATE) $(M18BIN) \
	     $(M18_LUA_OUT) $(M18ELF)
	@echo "M18 LUA OK: $(M18_LUA_OUT)"

# ---- m18diag-send -- HARDWARE DELIVERY --------------------------------------
#
# NOT AUTHORISED BY THE EXISTENCE OF THIS TARGET.
#
#   * ***** M18-0 HAS HAD NO HARDWARE RUN. ***** Every claim it makes is a
#     HYPOTHESIS until a PS5 says otherwise. U1 and U3 are OPEN QUESTIONS.
#   * ***** ONE CONSOLE. ***** M18-0 has no peer and no peer address. Inbound
#     PS5-to-PS5 UDP is M18-1 and is NOT tested here.
#   * ***** IT WRITES NO SAVE AND CANNOT. ***** The save path is not linked.
#   * ***** IT RUNS NO EMULATOR. ***** No ROM is needed and none is read.
#   * ***** READ THE SYMBOLS BLOCK FIRST. ***** If any primitive shows NULL,
#     U1 is answered NO and NO OTHER FIGURE MATTERS. The fallback is to create
#     the socket in Lua and pass the fd through ext, exactly as log_fd already
#     travels -- but that is a DIFFERENT rung and must not be done silently.
#   * ***** A FAILED LOOPBACK IS NOT A FAILURE. ***** SELF RX is evidence, not
#     a pass criterion, and is drawn neutral for that reason.
#   * PHOTOGRAPH THE SCREEN. The UDP log is a bonus here and cannot be the
#     instrument, because the transport is part of what is under test.
m18diag-send: m18diag-verify m18diag-lua
ifndef PS5_IP
	$(error PS5_IP is not set -- use: make m18diag-send PS5_IP=<console address>)
endif
	$(PYTHON) tools/send.py $(PS5_IP) $(M18_LUA_OUT) $(M18BIN)

m18diag-clean:
	rm -rf $(M18BUILD)

.PHONY: m18diag m18diag-objs m18diag-check m18diag-link m18diag-size \
        m18diag-verify m18diag-lua m18diag-send m18diag-clean

# =============================================================================
# End of the M18-0 block.
#
# m18diag-verify treats the LAST occurrence of the sentinel below as the closing
# boundary of the M18 region: every line ABOVE the BEGIN sentinel and every line
# BELOW this sentinel is searched for M18 object contamination.
#
# Append any future milestone block BELOW the sentinel. That is deliberate: a
# later milestone that pulled an M18 object into its own object list would then
# be caught as the contamination it would be.
#
# NOTE FOR WHOEVER EDITS THIS NEXT: keep the sentinel on the LAST line of the
# file, and do not write M18 object names into any comment placed below it. Text
# below the sentinel is inside the searched region, so a mere prose mention of
# those names down there is reported as contamination -- the explanation has to
# live above the marker, as it does here.
# =============================================================================
# @@M18_BLOCK_END@@
