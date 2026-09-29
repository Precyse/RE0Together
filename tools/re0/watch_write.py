"""Find the code that writes a memory address in running RE0, using a hardware write breakpoint.

Attaches as a debugger, arms DR0 (write, 1/2/4 bytes) on every thread, records the instruction
address of each hit, then detaches and leaves the game running.

usage: python watch_write.py <addr> [seconds=20] [size=2|x]
       size x arms an execute breakpoint instead: it reports every time the instruction at <addr> runs.

To end early, create the file named by STOP_FILE; the watcher then clears the breakpoints and detaches.
Never kill this process while attached: armed debug registers left behind crash the game on the next write.
"""
import ctypes
import ctypes.wintypes as wt
import os
import sys
import time

import probe

DEBUG_EVENT_TIMEOUT_MS = 100
EXCEPTION_DEBUG_EVENT = 1
CREATE_THREAD_DEBUG_EVENT = 2
EXCEPTION_SINGLE_STEP = 0x80000004
STATUS_WX86_SINGLE_STEP = 0x4000001E
EXCEPTION_BREAKPOINT = 0x80000003
STATUS_WX86_BREAKPOINT = 0x4000001F
DBG_CONTINUE = 0x00010002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001
THREAD_ALL = 0x001F03FF
TH32CS_SNAPTHREAD = 0x4
WOW64_CONTEXT_DEBUG_REGISTERS = 0x00010010
WOW64_CONTEXT_CONTROL = 0x00010001
DR7_LEN = {1: 0b00, 2: 0b01, 4: 0b11}
DR7_WRITE = 0b01
DR7_EXECUTE = 0b00
EXECUTE = "x"
EFLAGS_RESUME = 0x10000  # RF: continue past an execute breakpoint without re-triggering it
STACK_ARGS = 4
TEXT_START, TEXT_END = 0x401000, 0xCB088B
STACK_SCAN_BYTES = 0x400
CALL_WINDOW = 6
CALL_REL32 = 0xE8
CALL_INDIRECT = 0xFF
MAX_CALLERS = 10
DRAIN_SECONDS = 0.5
STOP_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "watch_write.stop")

k32 = ctypes.WinDLL("kernel32", use_last_error=True)


class WOW64_CONTEXT(ctypes.Structure):
    _fields_ = [("ContextFlags", wt.DWORD), ("Dr0", wt.DWORD), ("Dr1", wt.DWORD), ("Dr2", wt.DWORD),
                ("Dr3", wt.DWORD), ("Dr6", wt.DWORD), ("Dr7", wt.DWORD), ("FloatSave", ctypes.c_byte * 112),
                ("SegGs", wt.DWORD), ("SegFs", wt.DWORD), ("SegEs", wt.DWORD), ("SegDs", wt.DWORD),
                ("Edi", wt.DWORD), ("Esi", wt.DWORD), ("Ebx", wt.DWORD), ("Edx", wt.DWORD), ("Ecx", wt.DWORD),
                ("Eax", wt.DWORD), ("Ebp", wt.DWORD), ("Eip", wt.DWORD), ("SegCs", wt.DWORD),
                ("EFlags", wt.DWORD), ("Esp", wt.DWORD), ("SegSs", wt.DWORD), ("ExtendedRegisters", ctypes.c_byte * 512)]


class EXCEPTION_RECORD(ctypes.Structure):
    _fields_ = [("ExceptionCode", wt.DWORD), ("ExceptionFlags", wt.DWORD), ("ExceptionRecord", ctypes.c_void_p),
                ("ExceptionAddress", ctypes.c_void_p), ("NumberParameters", wt.DWORD),
                ("ExceptionInformation", ctypes.c_size_t * 15)]


class EXCEPTION_DEBUG_INFO(ctypes.Structure):
    _fields_ = [("ExceptionRecord", EXCEPTION_RECORD), ("dwFirstChance", wt.DWORD)]


class DEBUG_EVENT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("Exception", EXCEPTION_DEBUG_INFO), ("pad", ctypes.c_byte * 160)]
    _fields_ = [("dwDebugEventCode", wt.DWORD), ("dwProcessId", wt.DWORD), ("dwThreadId", wt.DWORD), ("u", _U)]


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
                ("th32OwnerProcessID", wt.DWORD), ("tpBasePri", wt.LONG), ("tpDeltaPri", wt.LONG), ("dwFlags", wt.DWORD)]


def thread_ids(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    entry = THREADENTRY32(dwSize=ctypes.sizeof(THREADENTRY32))
    ids = []
    ok = k32.Thread32First(snap, ctypes.byref(entry))
    while ok:
        if entry.th32OwnerProcessID == pid:
            ids.append(entry.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(entry))
    k32.CloseHandle(snap)
    return ids


def set_debug_regs(tid, addr, size, arm):
    h = k32.OpenThread(THREAD_ALL, False, tid)
    if not h:
        return
    ctx = WOW64_CONTEXT(ContextFlags=WOW64_CONTEXT_DEBUG_REGISTERS)
    k32.SuspendThread(h)
    if k32.Wow64GetThreadContext(h, ctypes.byref(ctx)):
        if arm:
            ctx.Dr0 = addr
            rw, length = (DR7_EXECUTE, 0) if size == EXECUTE else (DR7_WRITE, DR7_LEN[size])
            ctx.Dr7 = (ctx.Dr7 & ~0xF0003) | 0b1 | (rw << 16) | (length << 18)
        else:
            ctx.Dr0 = 0
            ctx.Dr7 &= ~0xF0003
        ctx.Dr6 = 0
        k32.Wow64SetThreadContext(h, ctypes.byref(ctx))
    k32.ResumeThread(h)
    k32.CloseHandle(h)


def return_addresses(proc, esp):
    """Likely return addresses on the stack: code pointers directly preceded by a call instruction."""
    found = []
    stack = proc.read(esp, STACK_SCAN_BYTES)
    for off in range(0, len(stack) - 3, 4):
        ret = int.from_bytes(stack[off:off + 4], "little")
        if not TEXT_START <= ret < TEXT_END:
            continue
        before = proc.read(ret - CALL_WINDOW, CALL_WINDOW)
        if len(before) == CALL_WINDOW and (before[-5] == CALL_REL32 or before[-6] == CALL_INDIRECT
                                           or before[-2] == CALL_INDIRECT or before[-3] == CALL_INDIRECT):
            found.append(ret)
        if len(found) == MAX_CALLERS:
            break
    return found


def drain_pending_events():
    """Hits already queued when the registers were cleared must be answered while still attached; one delivered
    after detach is an unhandled single-step exception and kills the game."""
    event = DEBUG_EVENT()
    deadline = time.time() + DRAIN_SECONDS
    while time.time() < deadline:
        if not k32.WaitForDebugEvent(ctypes.byref(event), DEBUG_EVENT_TIMEOUT_MS):
            continue
        status = DBG_CONTINUE
        if event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT:
            code = event.u.Exception.ExceptionRecord.ExceptionCode
            if code not in (EXCEPTION_SINGLE_STEP, STATUS_WX86_SINGLE_STEP, EXCEPTION_BREAKPOINT, STATUS_WX86_BREAKPOINT):
                status = DBG_EXCEPTION_NOT_HANDLED
        k32.ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status)


def main():
    addr = int(sys.argv[1], 16)
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 20
    size = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] == EXECUTE else int(sys.argv[3]) if len(sys.argv) > 3 else 2
    sys.argv = ["probe"]
    pid = probe.find_pid()
    proc = probe.Proc(pid)
    if not k32.DebugActiveProcess(pid):
        sys.exit(f"DebugActiveProcess failed: {ctypes.get_last_error()}")
    k32.DebugSetProcessKillOnExit(False)
    hits = {}
    armed = False
    event = DEBUG_EVENT()
    end = time.time() + seconds
    try:
        if os.path.exists(STOP_FILE):
            os.remove(STOP_FILE)
        while time.time() < end and not os.path.exists(STOP_FILE):
            if not k32.WaitForDebugEvent(ctypes.byref(event), DEBUG_EVENT_TIMEOUT_MS):
                continue
            status = DBG_CONTINUE
            if event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT:
                code = event.u.Exception.ExceptionRecord.ExceptionCode
                if code in (EXCEPTION_BREAKPOINT, STATUS_WX86_BREAKPOINT) and not armed:
                    for tid in thread_ids(pid):
                        set_debug_regs(tid, addr, size, True)
                    armed = True
                    print(f"armed on {addr:#x}" + ("" if size == EXECUTE else f", value now {proc.read(addr, size).hex()}"), flush=True)
                elif code in (EXCEPTION_SINGLE_STEP, STATUS_WX86_SINGLE_STEP):
                    h = k32.OpenThread(THREAD_ALL, False, event.dwThreadId)
                    ctx = WOW64_CONTEXT(ContextFlags=WOW64_CONTEXT_CONTROL | WOW64_CONTEXT_DEBUG_REGISTERS)
                    k32.Wow64GetThreadContext(h, ctypes.byref(ctx))
                    eip = ctx.Eip
                    ctx.Dr6 = 0
                    if size == EXECUTE:
                        ctx.EFlags |= EFLAGS_RESUME
                    k32.Wow64SetThreadContext(h, ctypes.byref(ctx))
                    k32.CloseHandle(h)
                    hits[eip] = hits.get(eip, 0) + 1
                    callers = [hex(r) for r in return_addresses(proc, ctx.Esp)]
                    if size == EXECUTE:
                        args = [hex(int.from_bytes(proc.read(ctx.Esp + 4 * i, 4), "little")) for i in range(STACK_ARGS)]
                        print(f"exec hit: eip {eip:#x} ecx {ctx.Ecx:#x} eax {ctx.Eax:#x} stack {args} callers {callers}", flush=True)
                    else:
                        print(f"write hit: next eip {eip:#x} value {proc.read(addr, size).hex()} "
                              f"ecx {ctx.Ecx:#x} callers {callers}", flush=True)
                elif code not in (EXCEPTION_BREAKPOINT, STATUS_WX86_BREAKPOINT):
                    status = DBG_EXCEPTION_NOT_HANDLED
            elif event.dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT and armed:
                set_debug_regs(event.dwThreadId, addr, size, True)
            k32.ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status)
    finally:
        for tid in thread_ids(pid):
            set_debug_regs(tid, addr, size, False)
        drain_pending_events()
        k32.DebugActiveProcessStop(pid)
    print("detached; hits (address after the writing instruction):", {hex(k): v for k, v in hits.items()})


if __name__ == "__main__":
    main()
