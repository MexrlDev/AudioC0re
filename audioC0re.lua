-- SPDX-License-Identifier: MIT
--[[
  audioC0re.lua — LuaC0re payload for AudioC0re.
]]

local PC_IP        = "__PC_IP__"
local LOG_PORT     = 9027
local SC_PORT_BASE = 5001
local SC_PORT_MAX  = 5020
local LIB_PORT_BASE= 5100
local LIB_PORT_MAX = 5120
local SC_MIN_BYTES = 0x1000

local HAVE_LOGS = (PC_IP:match("^%d+%.%d+%.%d+%.%d+$") ~= nil)

init_dlsym()
sceMsgDialogTerminate()

local function htons(p) return ((p << 8) | (p >> 8)) & 0xFFFF end
local function inet_addr(s)
    local a,b,c,d = s:match("(%d+)%.(%d+)%.(%d+)%.(%d+)")
    return (d << 24) | (c << 16) | (b << 8) | a
end
local function make_sockaddr_in(port, ip)
    local sa = malloc(16)
    for i = 0,15 do write8(sa + i, 0) end
    write8(sa + 0, 16); write8(sa + 1, 2)
    write16(sa + 2, htons(port))
    if ip then write32(sa + 4, inet_addr(ip)) end
    return sa
end

local log_sock = -1
local log_sa   = nil
if HAVE_LOGS then
    log_sock = create_socket(AF_INET, SOCK_DGRAM, 0)
    log_sa   = make_sockaddr_in(LOG_PORT, PC_IP)
end

local function ulog(m)
    if HAVE_LOGS and log_sock >= 0 and log_sa then
        syscall.sendto(log_sock, m .. "\n", #m + 1, 0, log_sa, 16)
    end
end

ulog("lua: start")

local rw, rx, SC_SIZE = 0, 0, 0

-- ============================================================
-- Step A: plain mmap(PROT_RWX), sizes from big to small
-- ============================================================
ulog("lua: probing address space")
do
    local probe = syscall.mmap(0, 0x1000, 0x3, 0x1002, -1, 0)
    ulog("lua: probe mmap 4KB -> 0x" .. string.format("%x", probe or 0))
    if probe and probe > 0x10000 then
        syscall.munmap(probe, 0x1000)
        ulog("lua: probe ok, VA is available")
    else
        ulog("lua: probe FAILED — address space is exhausted")
    end
end

local MMAP_SIZES = {
    0x180000, 0x100000, 0x80000, 0x40000, 0x20000, 0x10000, 0x8000
}
for _, size in ipairs(MMAP_SIZES) do
    local m = syscall.mmap(0, size, 0x7, 0x1002, -1, 0)
    ulog("lua: mmap RWX " .. string.format("%x", size) ..
         " -> 0x" .. string.format("%x", m or 0))
    if m and m > 0x10000 then
        rw, rx, SC_SIZE = m, m, size
        ulog("lua: using mmap RWX at 0x" .. string.format("%x", m))
        break
    end
end

-- ============================================================
-- Step B: split mappings — RW at some address, RX at the same
-- address via jit_send_recv_fd.  Some firmwares allow RW+X but
-- refuse RWX in a single mapping.
-- ============================================================
if rw == 0 then
    ulog("lua: RWX refused, trying split RW/RX")
    for _, size in ipairs(MMAP_SIZES) do
        local m = syscall.mmap(0, size, 0x3, 0x1002, -1, 0)   -- PROT_RW
        if m and m > 0x10000 then
            ulog("lua: mmap RW " .. string.format("%x", size) ..
                 " -> 0x" .. string.format("%x", m))

            local rxa = malloc(8)
            local mfd = jit_send_recv_fd(jit_malloc(8), NEW_JIT_SOCK, NEW_MAIN_SOCK)
            -- Try aliasing this fd as executable at the same addr.
            local ok = pcall(function()
                sceKernelJitMapSharedMemory(mfd, PROT_READ|PROT_EXECUTE, rxa)
            end)
            local rxtry = read64(rxa)
            if ok and rxtry and rxtry ~= 0 then
                rw, rx, SC_SIZE = m, m, size
                ulog("lua: split RW/RX ok at 0x" .. string.format("%x", m))
                break
            else
                ulog("lua: split RW/RX alias failed")
                syscall.munmap(m, size)
            end
        end
    end
end

-- ============================================================
-- Step C: JIT shared memory (last resort)
-- ============================================================
if rw == 0 then
    ulog("lua: mmap and split both failed, trying JIT")

    local function jit_alloc(size)
        local bfd  = jit_malloc(8)
        local rwfd = jit_malloc(8)
        local rxfd = jit_malloc(8)
        local rwa  = jit_malloc(8)
        local rxa  = malloc(8)
        local nm   = jit_malloc(8)
        if bfd == 0 or rwfd == 0 or rxfd == 0 or rwa == 0 or nm == 0 then
            return 0, 0
        end
        jit_write_buffer(nm, "ac0r")

        jit_sceKernelJitCreateSharedMemory(nm, size, 7, bfd)
        local h = jit_read32(bfd)
        if h == 0 then return 0, 0 end

        jit_sceKernelJitCreateAliasOfSharedMemory(h, PROT_READ|PROT_WRITE, rwfd)
        jit_sceKernelJitCreateAliasOfSharedMemory(h, PROT_READ|PROT_EXECUTE, rxfd)
        jit_sceKernelJitMapSharedMemory(jit_read32(rwfd), PROT_READ|PROT_WRITE, rwa)
        local r = jit_read64(rwa)
        if r == 0 then return 0, 0 end

        local mfd = jit_send_recv_fd(jit_read32(rxfd), NEW_JIT_SOCK, NEW_MAIN_SOCK)
        sceKernelJitMapSharedMemory(mfd, PROT_READ|PROT_EXECUTE, rxa)
        return r, read64(rxa)
    end

    local JIT_SIZES = { 0x80000, 0x40000, 0x20000 }
    for _, size in ipairs(JIT_SIZES) do
        local r, x = jit_alloc(size)
        if r ~= 0 then
            rw, rx, SC_SIZE = r, x, size
            ulog("lua: JIT ok " .. string.format("%x", size))
            break
        end
    end
end

if rw == 0 then
    ulog("lua: FATAL no RWX memory. Reboot the console.")
    error("no RWX memory for shellcode — reboot the PS5, " ..
          "relaunch the game, then try again")
end

ulog("lua: dest rw=0x" .. string.format("%x", rw) ..
     " size=0x" .. string.format("%x", SC_SIZE))

-- ============================================================
-- Library listener
-- ============================================================
ulog("lua: binding LIBPORT")
local lib_srv, lib_port = -1, 0
for p = LIB_PORT_BASE, LIB_PORT_MAX do
    local s = create_socket(AF_INET, SOCK_STREAM, 0)
    if s >= 0 then
        local en = malloc(4); write32(en, 1)
        syscall.setsockopt(s, 0xFFFF, 0x0004, en, 4)
        local sa2 = make_sockaddr_in(p)
        if syscall.bind(s, sa2, 16) == 0 and syscall.listen(s, 1) == 0 then
            lib_srv, lib_port = s, p
            break
        end
        syscall.close(s)
    end
end
ulog("LIBPORT " .. tostring(lib_port))

-- ============================================================
-- Shellcode listener
-- ============================================================
ulog("lua: binding SCPORT")
local srv, sc_port = -1, 0
for p = SC_PORT_BASE, SC_PORT_MAX do
    local s = create_socket(AF_INET, SOCK_STREAM, 0)
    if s >= 0 then
        local en = malloc(4); write32(en, 1)
        syscall.setsockopt(s, 0xFFFF, 0x0004, en, 4)
        local sa = make_sockaddr_in(p)
        if syscall.bind(s, sa, 16) == 0 and syscall.listen(s, 1) == 0 then
            srv, sc_port = s, p
            break
        end
        syscall.close(s)
    end
end
if srv < 0 then
    ulog("lua: no free shellcode port")
    error("no shellcode port")
end
ulog("SCPORT " .. tostring(sc_port))

-- ============================================================
-- Receive shellcode
-- ============================================================
local sa = make_sockaddr_in(sc_port)
local alen = malloc(8); write32(alen, 16)
ulog("lua: awaiting shellcode")
local cfd = syscall.accept(srv, sa, alen)
if cfd < 0 then error("accept failed") end

local total = 0
while total < SC_SIZE do
    local n = syscall.read(cfd, rw + total, SC_SIZE - total)
    if n == 0 then break end
    if n < 0 then error("read error") end
    total = total + n
end
syscall.close(cfd)
syscall.close(srv)
ulog("lua: shellcode received " .. total)
if total < SC_MIN_BYTES then
    error("short receive: got " .. tostring(total))
end

-- ============================================================
-- ext_args
-- ============================================================
local ext = malloc(0x80)
memset(ext, 0, 0x80)
write64(ext + 0x00, 0xDEAD)
write32(ext + 0x18, log_sock)
write32(ext + 0x1C, -1)
if log_sa then
    for i = 0, 15 do write8(ext + 0x20 + i, read8(log_sa + i)) end
end
write64(ext + 0x30, 0)
write64(ext + 0x38, lib_srv)

ulog("lua: entering shellcode at 0x" .. string.format("%x", rx))
func_wrap(rx)(EBOOT_BASE, SCE_KERNEL_DLSYM, ext)
ulog("lua: returned from shellcode")
