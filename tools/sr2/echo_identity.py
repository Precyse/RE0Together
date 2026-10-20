"""Which process is on the other end of a loopback connection, and which executables are running (Windows, ctypes only):
the echo hub tells the game instances apart by the folder their SR2_pc.exe was started from."""
import ctypes
import os
import socket
from ctypes import wintypes

AF_INET = 2
TCP_TABLE_OWNER_PID_ALL = 5
ERROR_INSUFFICIENT_BUFFER = 122
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
PATH_CHARS = 1024
PID_SLOTS = 4096
PORT_MASK = 0xFFFF
LOOPBACK_NETWORK_ORDER = 0x0100007F

iphlpapi = ctypes.WinDLL("iphlpapi", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)

kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
psapi.EnumProcesses.argtypes = [ctypes.POINTER(wintypes.DWORD), wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]


class TcpRow(ctypes.Structure):
    """MIB_TCPROW_OWNER_PID; the ports are in network byte order in the low 16 bits."""
    _fields_ = [("state", wintypes.DWORD), ("local_address", wintypes.DWORD), ("local_port", wintypes.DWORD),
                ("remote_address", wintypes.DWORD), ("remote_port", wintypes.DWORD), ("pid", wintypes.DWORD)]


def tcp_rows():
    """The machine's IPv4 TCP table."""
    size = wintypes.DWORD(0)
    result = iphlpapi.GetExtendedTcpTable(None, ctypes.byref(size), False, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0)
    while result == ERROR_INSUFFICIENT_BUFFER:
        buffer = ctypes.create_string_buffer(size.value)
        result = iphlpapi.GetExtendedTcpTable(buffer, ctypes.byref(size), False, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0)
    if result != 0:
        raise OSError(f"GetExtendedTcpTable failed with {result}")
    entries = ctypes.c_uint32.from_buffer(buffer).value
    return (TcpRow * entries).from_buffer(buffer, ctypes.sizeof(ctypes.c_uint32))


def host_port(network_order_port):
    return socket.ntohs(network_order_port & PORT_MASK)


def client_pid(client_port, listener_port):
    """The PID that owns the loopback connection whose local port is client_port and whose remote port is listener_port, or None."""
    for row in tcp_rows():
        if (host_port(row.local_port) == client_port and host_port(row.remote_port) == listener_port
                and row.local_address == LOOPBACK_NETWORK_ORDER):
            return row.pid
    return None


def exe_path(pid):
    """The full image path of a process, or None when it cannot be queried (gone, or not ours)."""
    handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return None
    try:
        buffer = ctypes.create_unicode_buffer(PATH_CHARS)
        length = wintypes.DWORD(PATH_CHARS)
        return buffer.value if kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(length)) else None
    finally:
        kernel32.CloseHandle(handle)


def running_exe_paths():
    """The normalised image paths of every process this account can query."""
    pids = (wintypes.DWORD * PID_SLOTS)()
    returned = wintypes.DWORD(0)
    if not psapi.EnumProcesses(pids, ctypes.sizeof(pids), ctypes.byref(returned)):
        raise OSError("EnumProcesses failed")
    count = returned.value // ctypes.sizeof(wintypes.DWORD)
    paths = (exe_path(pid) for pid in pids[:count] if pid)
    return {normal_path(path) for path in paths if path}


def normal_path(path):
    return os.path.normcase(os.path.abspath(path))
