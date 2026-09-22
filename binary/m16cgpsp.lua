-- ==========================================================================
-- LUAport M16-2 -- lua/m16c.lua.in
-- THE LAUNCHER LOADER, FOR THE LAUNCHER THAT COMES BACK.
-- ==========================================================================
--
-- ***** THIS SCRIPT DRIVES EXACTLY ONE BINARY: build/m16c/m16cgpsp.bin. *****
--
-- lua/m13c.lua.in drives the FROZEN launcher and is NOT touched by this
-- milestone -- `make m13c-lua` still works and still produces the same payload
-- from the same bytes. lua/m16diag.lua.in and lua/m16bdiag.lua.in are HARDWARE
-- PASS EVIDENCE for M16-0 and M16-1 and are likewise untouched.
--
-- ***** THE ONE BEHAVIOURAL DIFFERENCE FROM M13C, STATED PLAINLY. *****
--
--   M13C   scan -> picker -> load -> play -> commit -> THE PAYLOAD EXITS
--   M16C   scan -> picker -> load -> play -> commit -> BACK TO THE PICKER
--
-- M13C's _start() is a straight line, so ending a game ends LUAport. M16C adds
-- ONE BACKWARD BRANCH: the four-shoulder chord now returns to the picker, and
-- the ONLY payload exit is CIRCLE at the picker. That is why this script's
-- verdict section reports a PAYLOAD LEDGER -- how many sessions ran, how many
-- were clean, how many frames the process presented in total -- in addition to
-- the last session's own numbers.
--
-- ***** WHY THE FRAME TOTAL IS THE STRONGEST NUMBER IN THE LOG. ***** It is
-- monotonic across the whole process, so a capture whose frame ids climb
-- through three games cannot have come from three separate launches. That is
-- the claim M16-0 and M16-1 could not make, and it is what M16-2 exists to
-- demonstrate.
--
-- ==========================================================================
-- TRANSPORT IS BYTE-FOR-BYTE M0's..M16-1's, AND DELIBERATELY SO
-- ==========================================================================
-- Same ports (9026 script, 9027 UDP log, 9028-9045 blob), same 'K' ACK
-- handshake, same port-walk, same JIT mapping loop, same 0x80 ext_args layout
-- and offsets, same dlsym fix, same auto-IP, same userId block. Every milestone
-- from M0 to M16-1 re-proved all of it on hardware, so M16C changes NONE of it.
-- Any M16C failure is therefore a launcher finding, not a delivery failure.
--
-- ==========================================================================
-- PS5 runtime pattern: EmuC0re by egycnq / EgyDevTeam.
-- Delivery substrate: Luac0re by Gezine.
-- ==========================================================================

-- Where the UDP debug log is sent. Listen with:  nc -u -l -p 9027
--
-- ***** THE LOG IS WHERE EVERY NUMBER WENT. ***** M16C draws no diagnostics on
-- screen -- no frame rate, no hashes, no page counters, no probe masks. All of
-- it is still MEASURED and all of it is printed here, in full, and in a looping
-- launcher that matters MORE than it did in M13C: the on-screen summary is
-- replaced by the next picker the moment the operator moves on, so the log is
-- the only place a three-session run survives in full.
local PC_IP    = "auto"
local LOG_PORT = 9027
local LOG_BOOT = "255.255.255.255"

local function htons(p) return ((p << 8) | (p >> 8)) & 0xFFFF end

local function inet_addr(s)
    local a, b, c, d = s:match("(%d+)%.(%d+)%.(%d+)%.(%d+)")
    return (d << 24) | (c << 16) | (b << 8) | a
end

local function make_sockaddr_in(port, ip)
    local sa = malloc(16)
    for i = 0, 15 do write8(sa + i, 0) end
    write8(sa + 0, 16)
    write8(sa + 1, 2)
    write16(sa + 2, htons(port))
    if ip then write32(sa + 4, inet_addr(ip)) end
    return sa
end

-- CONVERSION RULE -- learned on hardware, do not "tidy" this back.
-- This Luac0re tonumber() accepts STRINGS ONLY. Handing it a number raises
--     bad argument #1 to 'tonumber' (string expected, got number)
-- create_socket() and the syscall.* wrappers already return numbers, so raw
-- returns are kept VERBATIM and never converted.
local log_sock = create_socket(AF_INET, SOCK_DGRAM, 0)

local tx_live  = log_sock >= 0
local tx_dest  = LOG_BOOT
local tx_first = nil
local tx_bufrc = nil
local tx_calls = 0
local tx_fail  = 0

-- Sending to a broadcast address is refused with EACCES unless SO_BROADCAST is
-- set. BSD values: SOL_SOCKET 0xffff, SO_BROADCAST 0x0020 -- the PS5 kernel is
-- FreeBSD-derived, and the Linux numbers are different.
if tx_live then
    local en = malloc(4)
    write32(en, 1)
    syscall.setsockopt(log_sock, 0xffff, 0x0020, en, 4)
end

local log_sa = make_sockaddr_in(LOG_PORT, LOG_BOOT)

local function ulog(m)
    if not tx_live then return nil end
    local rc = syscall.sendto(log_sock, m .. "\n", #m + 1, 0, log_sa, 16)
    tx_calls = tx_calls + 1
    -- type() guard rather than tonumber(): ulog() runs long before blob
    -- bring-up, so anything it can throw takes the ENTIRE run down with it.
    if type(rc) ~= "number" or rc < 0 then tx_fail = tx_fail + 1 end
    return rc
end

-- The SAME datagram as ulog(), differing in ONE respect: the buffer is an
-- explicitly malloc'd native block instead of a Lua string. Sending one probe
-- both ways separates a bad string-to-pointer conversion from a genuine
-- network fault in a single hardware run.
local function ulog_buf(m)
    if not tx_live then return nil end
    local n   = #m + 1
    local buf = malloc(n)
    for i = 1, #m do write8(buf + i - 1, m:byte(i)) end
    write8(buf + #m, 10)                                    -- '\n'
    local rc = syscall.sendto(log_sock, buf, n, 0, log_sa, 16)
    tx_calls = tx_calls + 1
    if type(rc) ~= "number" or rc < 0 then tx_fail = tx_fail + 1 end
    return rc
end

-- ONE short line, appended to EVERY notification this script raises. The
-- notification is the only channel proven to work on this console, so the
-- transport's health has to ride on it rather than on itself.
local function tx_report()
    if not tx_live then
        return "UDP DEAD: create_socket=" .. tostring(log_sock)
    end
    return string.format("UDP fd=%s %s str=%s buf=%s e=%s",
                         tostring(log_sock), tostring(tx_dest),
                         tostring(tx_first), tostring(tx_bufrc), tostring(tx_fail))
end

-- Single choke point so no verdict branch can forget the transport line. The
-- pcall is belt and braces: a diagnostic added to explain a dead channel must
-- not be able to kill the live one.
local function notify(body)
    local ok, line = pcall(function() return tx_report() end)
    send_notification(body .. "\n" .. (ok and line or "UDP report failed"))
end

ulog("=== LUAport GBA launcher (M16C) -- THE PICKER COMES BACK")
ulog("=== ONE payload, ONE process, ONE arena: it brings the runtime up once,")
ulog("=== scans the library once, loads the BIOS once, and then runs every")
ulog("=== cartridge the operator chooses, one after another")
ulog("=== the four-shoulder chord RETURNS TO THE PICKER; it no longer exits")
ulog("=== CIRCLE at the picker is the ONLY payload exit")
ulog("=== it may write /savedata0/gba/<ID>.sav and <ID>.hdr -- NOTHING else")
ulog("=== the identity is derived from CARTRIDGE CONTENT, not the filename")
ulog("=== no save is written unless a session ends cleanly AND the game")
ulog("=== actually changed its save -- returning to the picker is NOT")
ulog("=== save permission on its own")

-- Resolve sceKernelDlsym before Luac0re's own guess is used.
--
-- init_dlsym() derives it as a fixed distance back from
-- sceKernelGetModuleInfoFromAddr and, on PS5, never checks the answer:
-- 0x450 for firmware 10.00 and up, 0x480 below that. Measured across the 55
-- retail firmwares that could be parsed, the real distance is:
--
--     0x330   02.00 - 03.21        0x480   05.00 - 09.60
--     0x340   04.00 - 04.51        0x450   10.00 - 13.42
--
-- so the built-in constant is wrong for every 2.xx, 3.xx and 4.xx console.
-- Calling that address kills the Lua VM outright, so the loader prints its
-- banner and disappears. Ask the kernel instead.
pcall(function()
    if type(sceKernelDlsym) == "function" then return end

    local addr = nil

    if type(syscall) == "table" and type(syscall.dlsym) == "function" then
        local out = malloc(8)
        write64(out, 0)
        if syscall.dlsym(LIBKERNEL_HANDLE, "sceKernelDlsym", out) == 0 then
            local a = read64(out)
            if a and a ~= 0 then addr = a end
        end
    end

    if not addr then
        local major = tonumber(tostring(FW_VERSION or ""):match("^(%d+)"))
        local delta = nil
        if major then
            if major >= 10 then delta = 0x450
            elseif major >= 5 then delta = 0x480
            elseif major == 4 then delta = 0x340
            elseif major >= 2 then delta = 0x330
            end
        end
        if delta then
            addr = read64(LIBC_OFFSETS.sceKernelGetModuleInfoFromAddr) - delta
            DLSYM_FIX_MSG = string.format(
                "sys_dynlib_dlsym unavailable; using measured delta 0x%X", delta)
        end
    end

    if addr then
        SCE_KERNEL_DLSYM = addr
        sceKernelDlsym = func_wrap(addr)
        DLSYM_FIX_MSG = DLSYM_FIX_MSG
            or string.format("dlsym resolved by syscall at 0x%X", addr)
    end
end)

init_dlsym()
ulog(tostring(DLSYM_FIX_MSG or "dlsym: fix did not run (built-in path in use)"))

-- Resolve "auto" to this console's subnet broadcast address. get_current_ip()
-- is a Luac0re builtin, so the console can work this out for itself.
if PC_IP == "auto" then
    PC_IP = "255.255.255.255"
    local ok, own = pcall(function() return tostring(get_current_ip() or "") end)
    if ok then
        local a, b, c = own:match("(%d+)%.(%d+)%.(%d+)%.%d+")
        if a and tonumber(a) > 0 and tonumber(a) ~= 127 then
            PC_IP = a .. "." .. b .. "." .. c .. ".255"
        end
        AUTO_IP_MSG = "get_current_ip -> '" .. own .. "', logging to " .. PC_IP
    else
        AUTO_IP_MSG = "get_current_ip FAILED, logging to " .. PC_IP
    end
else
    AUTO_IP_MSG = "PC_IP fixed at build time (unicast) -> " .. PC_IP
end

local log_sa2 = make_sockaddr_in(LOG_PORT, PC_IP)
write32(log_sa + 4, read32(log_sa2 + 4))
tx_dest = PC_IP
ulog(tostring(AUTO_IP_MSG))

-- ***** THE DESTINATION IS FINAL FROM THIS POINT ON. *****
-- Expected on a healthy console: str=14 and buf=18. Two distinct, predictable
-- numbers make a partial success impossible to misread as a full one.
tx_first = ulog("M16C UDP TEST")
tx_bufrc = ulog_buf("M16C UDP TEST BUF")

local ok_rep, rep0 = pcall(function() return tx_report() end)
ulog(ok_rep and rep0 or "UDP report failed")

-- Deferred to here on purpose: it goes through dlsym, so it must not run
-- before the log exists to report a dlsym failure.
sceMsgDialogTerminate()
ulog("msgdialog terminated")

-- --------------------------------------------------------------- userId ---
-- CARRIED FORWARD FROM M8-M16-1 UNCHANGED.
--
-- THIS IS THE ONLY VALUE THE NATIVE SIDE CANNOT DERIVE FOR ITSELF.
-- sceUserServiceGetInitialUser lives in libSceUserService.sprx, and nothing in
-- the payload loads that module. It is handed across in ext.dbg[3] (+0x48).
--
-- ***** AT M16C IT HAS TWO JOBS: scePadGetHandle AND plat_savedata_init.
-- ***** The launcher opens a savedata container as well as a pad, and both are
-- opened ONCE for the whole payload rather than once per session -- so a userId
-- problem costs every game the operator would have played, not just the first.
-- The payload reports each as its own named failure.
--
-- THE 0xFF TRAP. 0xFF is SCE_USER_SERVICE_USER_ID_SYSTEM. It is the correct
-- argument to sceVideoOutOpen and the WRONG argument to scePadGetHandle, which
-- simply fails. NOTHING HERE INVENTS AN ID: on any failure userId stays 0, the
-- payload is TOLD 0, and reports it rather than passing a fabricated value
-- down.
local userId = 0
local uid_ok, uid_err = pcall(function()
    if not sceKernelLoadStartModule then
        sceKernelLoadStartModule = func_wrap(dlsym(LIBKERNEL_HANDLE, "sceKernelLoadStartModule"))
    end

    local libUser   = sceKernelLoadStartModule("libSceUserService.sprx", 0, 0, 0, 0, 0)
    local getUserId = dlsym(libUser, "sceUserServiceGetInitialUser")
    local uid_buf   = malloc(4)
    write32(uid_buf, 0)
    if getUserId then func_wrap(getUserId)(uid_buf) end
    userId = read32(uid_buf)
end)

if not uid_ok then
    ulog("WARNING: userId lookup raised: " .. tostring(uid_err))
end
ulog(string.format("userId=%d (0x%X) from sceUserServiceGetInitialUser",
                   userId, userId))
if userId == 0 then
    ulog("WARNING: userId is 0 -- no initial user. scePadGetHandle cannot "
         .. "succeed, so M16C will stop with -609 CONTROLLER NOT AVAILABLE. "
         .. "Sign a user in on the console and re-run.")
elseif userId == 0xFF then
    ulog("WARNING: userId is 0xFF -- that is the VIDEO convention "
         .. "(SCE_USER_SERVICE_USER_ID_SYSTEM), not a pad user id. "
         .. "M16C will refuse it rather than pass it on.")
end

-- The payload blob: code plus the initial contents of .data. It arrives over
-- TCP as raw binary rather than hex inside this script -- Luac0re's remote lua
-- loader caps scripts at 500KB and hex doubles the payload.

local BLOB_PORT_LO, BLOB_PORT_HI = 9028, 9045
local BLOB_PORT = 9028

local function recv_blob()
    local sa2 = malloc(16)
    local en2 = malloc(4)
    local function htons2(p) return ((p << 8) | (p >> 8)) & 0xFFFF end

    local srv = create_socket(AF_INET, SOCK_STREAM, 0)
    ulog("blob: socket fd=" .. tostring(srv))
    if srv < 0 then error("blob: create_socket failed") end

    write32(en2, 1)
    -- SO_REUSEADDR only. SO_REUSEPORT lets a leaked listener from an earlier
    -- payload share the port and steal the incoming connection, which hangs
    -- this accept() and wedges the loader.
    syscall.setsockopt(srv, SOL_SOCKET, 0x0004, en2, 4)   -- SO_REUSEADDR

    local bound = -1
    for p = BLOB_PORT_LO, BLOB_PORT_HI do
        write8(sa2 + 1, AF_INET)
        write16(sa2 + 2, htons2(p))
        write32(sa2 + 4, INADDR_ANY)
        if syscall.bind(srv, sa2, 16) == 0 then bound = p; break end
        ulog(string.format("blob: bind %d in use, trying next", p))
    end
    if bound < 0 then
        syscall.close(srv)
        error("blob: no free port -- relaunch to clear leaked listeners")
    end
    BLOB_PORT = bound
    ulog(string.format("blob: bind %d -> 0", BLOB_PORT))

    local lr = syscall.listen(srv, 1)
    ulog("blob: listen -> " .. tostring(lr))

    -- Timeout, never a bare accept(): a blocking accept that never fires wedges
    -- the Luac0re loader and costs a game relaunch.
    local F_SETFL, O_NONBLOCK = 4, 4
    syscall.fcntl(srv, F_SETFL, O_NONBLOCK)

    local ts = malloc(16)
    write64(ts, 0); write64(ts + 8, 50 * 1000 * 1000)   -- 50ms
    local function okfd(v) return v ~= nil and v >= 0 and v < 0x80000000 end

    ulog(string.format("blob: waiting on TCP %d (30s timeout)", BLOB_PORT))
    local cli = -1
    for _ = 1, 600 do
        cli = syscall.accept(srv, sa2, en2)
        if okfd(cli) then break end
        syscall.nanosleep(ts, 0)
    end
    if not okfd(cli) then
        syscall.close(srv)
        error("blob: no client within 30s")
    end
    syscall.fcntl(cli, F_SETFL, 0)

    -- A leaked listener accepts and buffers without complaint, then resets
    -- part-way through. Only a running script sends this byte back.
    local ackb = malloc(4)
    write8(ackb, 75)                    -- 'K'
    syscall.write(cli, ackb, 1)

    ulog("blob: accept fd=" .. tostring(cli))

    local maxsize, total = 0x400000, 0
    while total < maxsize do
        local n = syscall.read(cli, SHELLCODE_SCRATCH + total, maxsize - total)
        if n == 0 then break end
        if n < 0 then
            syscall.close(cli); syscall.close(srv); error("blob: read error")
        end
        total = total + n
    end
    syscall.close(cli)
    syscall.close(srv)
    if total == 0 then error("blob: nothing received") end
    ulog(string.format("blob: received %d bytes", total))
    return total
end

local blob_len = recv_blob()

-- Reservation covers code plus .data's initial image, not just the blob.
-- Substituted by tools/mklua.py from the LINKED ELF's __data_start symbol --
-- never hand-written.
--
-- NOTE: this has NOTHING to do with the launcher's 4 MB arena. That arena is a
-- separate anonymous mmap the payload makes at runtime and contributes ZERO
-- bytes to the blob, to this reservation and to the image. It is where the
-- gamepak buffers come from.
--
-- ***** AND IT IS STILL ONE ARENA, NOT ONE PER SESSION. ***** M16C takes an
-- arena mark before each cartridge and rewinds to it at the session boundary,
-- so session N+1 fits in the SAME 4 MB session N used. The arena is never
-- re-mapped and never grown. If it were, the payload would be restarting and
-- the whole milestone would be moot.
local JIT_SIZE = 0x60000

local bfd  = jit_malloc(8)
local rwfd = jit_malloc(8)
local rxfd = jit_malloc(8)
local rwa  = jit_malloc(8)
local rxa  = malloc(8)
local nm   = jit_malloc(8)

local function jitfail(what, extra)
    ulog(string.format("JIT FAIL at %s%s", what, extra and (" -- " .. extra) or ""))
    notify("LUAport GBA\nJIT setup failed at\n" .. what)
    return nil
end

jit_write_buffer(nm, "lup0")

-- Multi-mapping loader.
--
-- OPERATIONAL NOTE, unchanged from M1-M16-1 and still the most likely cause of
-- a spurious failure: the JIT pool DEPLETES within a game session. Every
-- payload sent leaves its mappings behind. A fresh launch yields ~3 mappings; a
-- session that has already run a payload may only yield 2. If
-- CreateSharedMemory fails, RELAUNCH THE GAME -- it is not an image defect.
--
-- ***** THIS IS ANOTHER REASON M16-2 MATTERS. ***** Under M13C an operator who
-- wanted to play a second cartridge had to send the payload again and spend
-- another set of JIT mappings. M16C sends once and plays as many cartridges as
-- the operator likes, so the pool is touched exactly once per launch.

local CHUNK = 0x40000

local nmaps = (JIT_SIZE + CHUNK - 1) // CHUNK

ulog(string.format("JIT: blob %d bytes, reserve 0x%X -> %d mapping(s) of 0x%X",
                   blob_len, JIT_SIZE, nmaps, CHUNK))

local maps = {}
for i = 1, nmaps do
    jit_write32(bfd, 0); jit_write32(rwfd, 0); jit_write32(rxfd, 0)
    jit_write64(rwa, 0); write64(rxa, 0)

    local rr = jit_sceKernelJitCreateSharedMemory(nm, CHUNK, 7, bfd)
    local base_fd = jit_read32(bfd)
    if base_fd == 0 or base_fd == 0xFFFFFFFF then
        return jitfail("CreateSharedMemory",
            string.format("mapping %d of %d, ret=0x%X -- JIT pool exhausted; relaunch the game",
                          i, nmaps, rr & 0xFFFFFFFF))
    end

    jit_sceKernelJitCreateAliasOfSharedMemory(base_fd, PROT_READ | PROT_WRITE, rwfd)
    jit_sceKernelJitCreateAliasOfSharedMemory(base_fd, PROT_READ | PROT_EXECUTE, rxfd)
    local rw_fd, rx_fd = jit_read32(rwfd), jit_read32(rxfd)
    if rw_fd == 0 or rx_fd == 0 or rw_fd == 0xFFFFFFFF or rx_fd == 0xFFFFFFFF then
        return jitfail("CreateAliasOfSharedMemory", string.format("mapping %d", i))
    end

    jit_sceKernelJitMapSharedMemory(rw_fd, PROT_READ | PROT_WRITE, rwa)
    local rw = jit_read64(rwa)
    if rw == 0 then return jitfail("JitMapSharedMemory(RW)", string.format("mapping %d", i)) end

    local mfd = jit_send_recv_fd(rx_fd, NEW_JIT_SOCK, NEW_MAIN_SOCK)
    if mfd < 0 then return jitfail("send_recv_fd", string.format("mapping %d", i)) end
    sceKernelJitMapSharedMemory(mfd, PROT_READ | PROT_EXECUTE, rxa)
    local rx = read64(rxa)
    if rx == 0 then return jitfail("JitMapSharedMemory(RX)", string.format("mapping %d", i)) end

    if i > 1 then
        local want = maps[i-1].rx + CHUNK
        if rx ~= want then
            return jitfail("non-adjacent mapping",
                string.format("mapping %d at 0x%X, expected 0x%X -- a flat image cannot span a gap",
                              i, rx, want))
        end
    end

    maps[i] = { rw = rw, rx = rx }
    ulog(string.format("JIT: mapping %d/%d rw=0x%X rx=0x%X", i, nmaps, rw, rx))
end

-- THE `len > 0` GUARD IS RETAINED FROM M1-M16-1 AND IS STILL LOAD-BEARING.
--
-- nmaps comes from JIT_SIZE (the reservation), while the slices come from
-- blob_len. The reservation runs to __data_start and is therefore LARGER than
-- the blob, so the final mapping can legitimately receive zero blob bytes. In
-- that case an unguarded loop computes a NEGATIVE length, which jit_memcpy
-- reinterprets as a huge unsigned size -- running off the end of
-- SHELLCODE_SCRATCH and taking the whole game process down.
for i = 1, nmaps do
    local off = (i - 1) * CHUNK
    local len = blob_len - off
    if len > CHUNK then len = CHUNK end
    if len > 0 then
        jit_memcpy(maps[i].rw, SHELLCODE_SCRATCH + off, len)
        ulog(string.format("JIT: mapping %d/%d <- %d bytes at blob offset %d",
                           i, nmaps, len, off))
    else
        ulog(string.format("JIT: mapping %d/%d reserved only (no blob bytes)",
                           i, nmaps))
    end
end

local rx = maps[1].rx
ulog(string.format("Code: %d bytes written across %d mapping(s), entry rx=0x%X",
                   blob_len, nmaps, rx))

-- ext_args, mirroring struct ext_args in runtime/core.h. UNCHANGED from
-- M0-M16-1.
--   +0x00 s64 status        +0x08 s64 step        +0x10 u32 frame_count
--   +0x18 s32 log_fd        +0x1C s32 pad_fd      +0x20 u8  log_addr[16]
--
-- ***** +0x10 IS WRITTEN AS 0 AND THE PAYLOAD NEVER READS IT. ***** M13B used
-- it as a STAGE SELECTOR. M16C, like M13C, has one flow, so there is nothing to
-- select and the slot is left at its memset value rather than repurposed --
-- repurposing a slot whose old meaning is still live in a sister image is
-- exactly how a captured log gets read against the wrong layout.
--
-- THE dbg[] SLOTS. Their meaning depends on how far the launcher got, which is
-- why the verdict below decodes them against `step` and never blindly.
--
-- ***** AND AT M16C THERE IS A SECOND REASON TO BE CAREFUL. ***** These are a
-- ROLLING RECORD OF THE LATEST SESSION, not a summary of the payload. A single
-- set of dbg fields cannot describe eight games. The CROSS-SESSION facts live
-- in dbg[5..7] at the payload exit, and in the UDP ledger, and nowhere else.
--
--   BEFORE THE LOAD (step < 451)
--     +0x30 dbg[0]  .gba entries accepted
--     +0x38 dbg[1]  directory entries seen
--     +0x40 dbg[2]  entries rejected, all reasons summed
--     +0x50 dbg[4]  entries that overflowed the 64-entry table, or the raw
--                   failure code of whatever refused
--     +0x58 dbg[5]  arena_used() after video + pad init -- THE PAYLOAD BASELINE
--     +0x60 dbg[6]  entries that passed the ROM gate
--     +0x68 dbg[7]  the chosen cartridge's measured size
--
--   DURING A SESSION (step >= 451) -- dbg[0..2] and dbg[4] are REUSED
--     +0x30 dbg[0]  the chosen cartridge's file size, then FRAMES EMULATED
--     +0x38 dbg[1]  the ROM FNV, then FRAMES PRESENTED
--     +0x40 dbg[2]  the SAVE IDENTITY hash, then ELAPSED MILLISECONDS
--     +0x50 dbg[4]  the mapping mask, then the END REASON, then a failure code
--     +0x58 dbg[5]  the START HASH
--     +0x60 dbg[6]  the END HASH
--     +0x68 dbg[7]  the gamepak buffer count, then PRESENT FPS x100
--
--   ***** AT THE PAYLOAD EXIT (step 459) dbg[5..7] BECOME THE LEDGER. *****
--     +0x58 dbg[5]  SESSIONS STARTED
--     +0x60 dbg[6]  SESSIONS CLEAN
--     +0x68 dbg[7]  TOTAL FRAMES PRESENTED BY THIS PAYLOAD -- monotonic across
--                   every session, and THE strongest single number in the run:
--                   it is what proves the process never restarted
--
--   +0x48 dbg[3]  ***** userId IN, THEN THE VERDICT OUT. ***** The payload
--                 consumes the id at step 446 and overwrites the slot at the
--                 end of each session. It is decoded ONLY on the paths that
--                 reach that far -- an echo that looks like an output is how
--                 the M8 bug survived review, and that lesson is not unlearned
--                 here.
--
--     [3:0]   restore  0 NONE       1 LOADED    2 REJECTED
--     [7:4]   commit   0 NOT NEEDED 1 UPDATED   2 FAILED
--     [8]     dirty
--     [11:9]  flow     0 EXITED (no session completed)  1 SESSION COMPLETE
--     [14:12] SRAM capture verdict  0 NOT TAKEN  1 TRUNCATED  2 REFUTED
--                                   3 FAMILY     4 INCONCLUSIVE
--     [15]    the capture saw non-0xFF bytes in the upper half
--     [39:16] restored bytes
--     [63:40] committed bytes
--
-- ***** AT step 459 THE FLOW NIBBLE IS ZEROED ONLY IF NO SESSION EVER
-- COMPLETED. ***** A payload that played three games and then exited KEEPS the
-- last session's packing, so the notification describes a real session while
-- the ledger describes the payload.
--
-- status starts at 0xDEAD so the readback can distinguish "the payload set a
-- status" from "the payload never wrote anything".
local ext = malloc(0x80)
memset(ext, 0, 0x80)
write64(ext + 0x00, 0xDEAD)
write32(ext + 0x10, 0)                -- no stage selector at M16C
write32(ext + 0x18, log_sock)
write32(ext + 0x1C, -1)
for i = 0, 15 do write8(ext + 0x20 + i, read8(log_sa + i)) end
write64(ext + 0x48, userId)           -- dbg[3] IN

ulog(string.format("calling the M16C launcher at 0x%X", rx))

func_wrap(rx)(EBOOT_BASE, SCE_KERNEL_DLSYM, ext)

local status = read64(ext + 0x00)
local step   = read64(ext + 0x08)

-- Recover a NEGATIVE C int from dbg[]. The payload stores an `unsigned` into a
-- u64, so a code such as -2 arrives as 0x00000000FFFFFFFE rather than as -2.
-- BYTE-IDENTICAL to lua/m12c.lua.in, lua/m13a.lua.in, lua/m13b.lua.in and
-- lua/m13c.lua.in.
--
-- `status` and `step` are NOT put through it: they are written as s64 by the
-- payload and read back already signed.
local function s32(v)
    v = v & 0xFFFFFFFF
    if v >= 0x80000000 then return v - 0x100000000 end
    return v
end

local d3 = read64(ext + 0x48)
local d0 = read64(ext + 0x30)
local d1 = read64(ext + 0x38)
local d2 = read64(ext + 0x40)
local d4 = read64(ext + 0x50)
local d5 = read64(ext + 0x58)
local d6 = read64(ext + 0x60)
local d7 = read64(ext + 0x68)

ulog(string.format("Done. status=%d step=%d", status, step))

-- ==========================================================================
-- THE STEP NAMES
-- ==========================================================================
-- 440-459, a band DISJOINT from M13C's 360-377 and from M16-0's and M16-1's
-- 400-409, so a number read off a photograph can never be attributed to the
-- wrong image.
--
-- ***** THE BAND IS CONTIGUOUS AND IN EXECUTION ORDER, AND 451-458 REPEAT.
-- ***** Steps 440-450 run ONCE per payload. Steps 451-458 are the SESSION and
-- run once per cartridge the operator plays, so a `step` of 455 means "in a
-- session" and says nothing about WHICH one -- that is what the ledger is for.
-- 459 is reached exactly once, at the payload exit.
local function stepname(s)
    if     s == 440 then return "native entry"
    elseif s == 441 then return "runtime bring-up"
    elseif s == 442 then return "image measurement"
    elseif s == 443 then return "symbol resolution"
    elseif s == 444 then return "savedata layer init (once, before the picker)"
    elseif s == 445 then return "scanning the ROM library (once)"
    elseif s == 446 then return "video, controller and the audio port (once)"
    elseif s == 447 then return "validating the library (once)"
    elseif s == 448 then return "the clock (once, hoisted out of the session)"
    elseif s == 449 then return "the picker"
    elseif s == 450 then return "selection latched"
    elseif s == 451 then return "session setup and cartridge load"
    elseif s == 452 then return "information screen and save probe"
    elseif s == 453 then return "session bring-up"
    elseif s == 454 then return "restoring the save"
    elseif s == 455 then return "the session"
    elseif s == 456 then return "committing the save"
    elseif s == 457 then return "the summary"
    elseif s == 458 then return "the session boundary (back to the picker)"
    elseif s == 459 then return "payload exit -- CIRCLE at the picker"
    else                 return "step " .. tostring(s)
    end
end

-- ==========================================================================
-- THE STATUS NAMES
-- ==========================================================================
-- ***** THESE ARE THE SAME WORDS THE ON-SCREEN HEADING USES. ***** The payload
-- draws a heading, the code and the step; this script names the same code the
-- same way. An operator comparing a photograph against a captured log must not
-- have to translate between two vocabularies.
--
-- -601..-635, DISJOINT from M13C's -401..-428 and from M16-0's and M16-1's
-- -501..-528.
local function statusname(st)
    if     st == 0    then return "OK"
    elseif st == -601 then return "SYSTEM MEMORY UNAVAILABLE (no mmap)"
    elseif st == -602 then return "SYSTEM MEMORY UNAVAILABLE (relocations)"
    elseif st == -603 then return "SYSTEM MEMORY UNAVAILABLE (arena)"
    elseif st == -604 then return "ROM LIBRARY UNREADABLE (getdents unresolved)"
    elseif st == -605 then return "SAVE STORAGE UNAVAILABLE"
    elseif st == -606 then return "ROM LIBRARY UNREADABLE"
    elseif st == -607 then return "NO GBA ROMS FOUND"
    elseif st == -608 then return "VIDEO INIT FAILED"
    elseif st == -609 then return "CONTROLLER NOT AVAILABLE"
    elseif st == -610 then return "CLOSED FROM THE PICKER"
    elseif st == -611 then return "EMULATOR NOT READY (nobody chose a cartridge)"
    elseif st == -612 then return "ROM INVALID -- every entry was rejected"
    elseif st == -613 then return "TIMING INIT FAILED (clock unresolved)"
    elseif st == -614 then return "TIMING INIT FAILED (clock read zero)"
    elseif st == -615 then return "AUDIO INIT FAILED"
    elseif st == -616 then return "BIOS NOT FOUND"
    elseif st == -617 then return "SAVE WRITE FAILED"
    elseif st == -618 then return "SAVE WRITTEN BUT STORAGE DID NOT CLOSE -- POWER CYCLE"
    elseif st == -619 then return "SESSION DID NOT RELEASE (boundary probe)"
    elseif st == -620 then return "SESSION DID NOT RELEASE (arena watermark)"
    elseif st == -621 then return "SESSION DID NOT RELEASE (arena rewind refused)"
    elseif st == -622 then return "SESSION DID NOT RELEASE (save identity still latched)"
    elseif st == -623 then return "EMULATOR NOT READY (session reset did not take)"
    elseif st == -624 then return "ROM COULD NOT BE OPENED (path too long)"
    elseif st == -625 then return "EMULATOR NOT READY (cartridge window)"
    elseif st == -626 then return "ROM COULD NOT BE OPENED"
    elseif st == -627 then return "ROM INVALID -- the loader refused it"
    elseif st == -628 then return "EMULATOR NOT READY (save identity)"
    elseif st == -629 then return "ROM INVALID -- a startup check failed"
    elseif st == -630 then return "ROM MAPPING FAILED (cartridge unmapped)"
    elseif st == -631 then return "EMULATOR NOT READY (identity drifted)"
    elseif st == -632 then return "EMULATOR NOT READY (cartridge window shrank)"
    elseif st == -633 then return "EMULATOR NOT READY (unsafe to start)"
    elseif st == -634 then return "SESSION ENDED UNEXPECTEDLY"
    elseif st == -635 then return "SESSION DID NOT RELEASE (unexpected relaunch state)"
    else                   return "status " .. tostring(st)
    end
end

ulog("verdict: " .. statusname(status) .. " at " .. stepname(step))

-- ==========================================================================
-- THE VERDICT NOTIFICATION
-- ==========================================================================
-- ***** THE SCREEN IS CLEAN; THE NOTIFICATION IS NOT. ***** M16C shows the
-- operator a short summary between games and nothing else. That is a UI
-- decision, not a decision to stop measuring: every figure is still produced,
-- and the ones that decide whether the run was correct go HERE, where they
-- survive a console that has just been power-cycled.
local body

-- ***** THE HEALTHY OUTCOME IS -610, NOT 0. ***** M13C returned 0 after its one
-- and only session. M16C never returns 0: a session that ends returns to the
-- picker rather than to the caller, so the only way the payload finishes is
-- CIRCLE at the picker. Reading -610 as a failure would mean reading every
-- successful run as a failure, which is why it is the FIRST branch here.
if status == -610 then
    local started = d5
    local cleanct = d6
    local frames  = d7
    local flow    = (d3 >> 9) & 0x7

    if flow == 1 then
        -- A session completed, so dbg[0..3] still describe the LAST game.
        local rstate = d3 & 0xF
        local cstate = (d3 >> 4) & 0xF
        local dirty  = (d3 >> 8) & 0x1
        local rbytes = (d3 >> 16) & 0xFFFFFF
        local cbytes = (d3 >> 40) & 0xFFFFFF

        local rword = ({ [0] = "NONE", [1] = "LOADED", [2] = "REJECTED" })[rstate]
                      or "?"
        local cword = ({ [0] = "UNCHANGED", [1] = "UPDATED", [2] = "FAILED" })[cstate]
                      or "?"

        body = string.format(
            "LUAport GBA  CLOSED FROM THE PICKER\n" ..
            "%d session(s) played, %d clean\n" ..
            "%d frames presented by this payload\n" ..
            "LAST GAME  %d frames in %d ms\n" ..
            "RESTORE %s  %d bytes\n" ..
            "DIRTY %s   SAVE %s  %d bytes",
            started, cleanct, frames,
            d0, d2,
            rword, rbytes,
            (dirty == 1) and "YES" or "NO", cword, cbytes)
    else
        -- ***** EXITING WITHOUT PLAYING IS A NORMAL OUTCOME AND IS WORDED AS
        -- ONE. ***** The operator opened the launcher, looked at the library and
        -- closed it. Nothing was opened, started or written.
        body = string.format(
            "LUAport GBA  CLOSED FROM THE PICKER\n" ..
            "no cartridge was started\n" ..
            "nothing was opened or written\n" ..
            "%d frames presented (picker only)",
            frames)
    end

elseif status == -607 then
    body = string.format(
        "LUAport GBA\nNO GBA ROMS FOUND\n" ..
        "the ROM folder listed %d entries and none\n" ..
        "of them was a .gba file\n" ..
        "upload one: make m13b-upload ROM=<file>",
        d1)

elseif status == -605 then
    body = string.format(
        "LUAport GBA\nSAVE STORAGE UNAVAILABLE\n" ..
        "plat_savedata_init returned %d\n" ..
        "no save could ever be committed, so the\n" ..
        "launcher was refused BEFORE the picker.\n" ..
        "Nothing was mounted, opened, played or written",
        s32(d4))

elseif status == -616 then
    body = "LUAport GBA\nBIOS NOT FOUND\n" ..
           "no valid GBA BIOS image is installed\n" ..
           "upload one: make m3-upload-bios"

elseif status == -609 then
    body = "LUAport GBA\nCONTROLLER NOT AVAILABLE\n" ..
           "scePadGetHandle needs a signed-in user.\n" ..
           "Sign a user in on the console and re-run"

elseif status == -615 then
    body = "LUAport GBA\nAUDIO INIT FAILED\n" ..
           "sceAudioOut would not open, so no session\n" ..
           "was started rather than run silent"

elseif status == -608 then
    body = "LUAport GBA\nVIDEO INIT FAILED\n" ..
           "the video port would not open"

-- ***** THE TWO SAVE FAILURES GET THEIR OWN SCREENS BECAUSE THEIR REMEDIES ARE
-- COMPLETELY DIFFERENT. ***** -617 means retry. -618 means power cycle. Folding
-- them together would send the operator to the wrong one half the time.
elseif status == -617 then
    body = string.format(
        "LUAport GBA\nSAVE WRITE FAILED\n" ..
        "the session was clean and the game saved,\n" ..
        "but the write did not complete (rc %d).\n" ..
        "%d frames in %d ms.\n" ..
        "THE PREVIOUS SAVE ON DISK IS STILL INTACT",
        s32(d4), d0, d2)

elseif status == -618 then
    body = string.format(
        "LUAport GBA\nSTORAGE DID NOT CLOSE\n" ..
        "THE SAVE WAS WRITTEN, but /savedata0 is\n" ..
        "still mounted read-write (rc %d).\n" ..
        "THE GAME MAY NOT CLOSE FROM THE PS MENU.\n" ..
        "POWER CYCLE THE CONSOLE",
        s32(d4))

elseif status == -634 then
    -- ***** THE WATCHDOG IS NAMED, NOT BLURRED INTO THE OTHERS. ***** END
    -- REASON 2 is the emergency backstop -- the session ran past its safety
    -- limit and was stopped. It is NOT a user exit and NOT a pass.
    --
    -- ***** AT M16C AN UNSTABLE SESSION IS SESSION-FATAL, NOT PAYLOAD-FATAL,
    -- SO REACHING THIS BRANCH MEANS SOMETHING ELSE ENDED THE PAYLOAD. ***** A
    -- single bad session draws its own screen and returns to the picker; only a
    -- failure the launcher cannot contain gets this far.
    if d4 == 2 then
        body = string.format(
            "LUAport GBA\nWATCHDOG EXIT\n" ..
            "%d emulated / %d presented in %d ms\n" ..
            "THE SESSION RAN PAST ITS SAFETY LIMIT AND\n" ..
            "WAS STOPPED. THIS IS NOT A USER EXIT.\n" ..
            "NOTHING WAS WRITTEN -- THE SAVE IS INTACT",
            d0, d1, d2)
    else
        body = string.format(
            "LUAport GBA\nSESSION ENDED UNEXPECTEDLY\n" ..
            "%d emulated / %d presented in %d ms\n" ..
            "END REASON %d\n" ..
            "(3 AUDIO 4 FRAMESKIP 5 MAP 6 MEMORY)\n" ..
            "NOTHING WAS WRITTEN -- THE SAVE IS INTACT",
            d0, d1, d2, d4)
    end

-- ***** THE FIVE BOUNDARY FAILURES SHARE A SCREEN BECAUSE THEY SHARE A
-- MEANING. ***** Each one says the session did not fully release something it
-- owned, so LUAport stopped itself rather than start another game on top of the
-- leftovers. That is the one failure class M16-2 introduces, and the safe
-- response to all five is identical: the save on disk is intact, relaunch.
elseif status == -619 or status == -620 or status == -621
    or status == -622 or status == -635 then
    body = string.format(
        "LUAport GBA\nSESSION DID NOT RELEASE\n" ..
        "%s\n" ..
        "LUAPORT STOPPED RATHER THAN START ANOTHER\n" ..
        "GAME ON TOP OF THE LAST ONE.\n" ..
        "detail 0x%X -- NOTHING WAS WRITTEN",
        statusname(status), d4)

elseif status == -611 then
    body = "LUAport GBA\nNO CARTRIDGE WAS CHOSEN\n" ..
           "the launcher closed itself rather than wait\n" ..
           "forever. Nothing was opened or written"

elseif status == -612 then
    body = string.format(
        "LUAport GBA\nROM INVALID\n" ..
        "%d files were listed and every one was\n" ..
        "rejected. The per-file reason is shown\n" ..
        "beside each row in the launcher",
        d0)

else
    body = string.format(
        "LUAport GBA\n%s\nat %s\nstatus %d step %d",
        statusname(status), stepname(step), status, step)
end

notify(body)
ulog("notification sent")
