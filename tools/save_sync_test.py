"""Save sync end to end on local transport: host sends a random data0.bin, the guest ends with an identical file,
coop=1 in both adapter.ini files; after the host's adapter reports SAVE_CHANGED for a rewritten save the guest gets
the new file; both sides reset (coop=0, no session folder) after Ctrl+Break.
Usage: python tools/save_sync_test.py   (the launcher must be built in Release; COOP_LAUNCHER overrides the exe)"""
import hashlib
import os
import socket
import struct
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LAUNCHER = Path(os.environ.get("COOP_LAUNCHER", ROOT / "launcher" / "bin" / "Release" / "net8.0-windows" / "coop-launcher.exe"))
FILE_NAME = "data0.bin"
FILE_BYTES = 2_300_000
HOST_PORTS = (27981, 27982)
HOST_BRIDGE_PORT = 27990
PROTO = 1
MSG_HELLO = 0x0001
MSG_SAVE_CHANGED = 0x0060
FLAG_RELIABLE = 1
CONNECT_RETRY_S = 0.5
TRANSFER_TIMEOUT_S = 30
EXIT_TIMEOUT_S = 15
POLL_S = 0.2


def launcher(args, log_path, exe=LAUNCHER):
    log = open(log_path, "w")
    return subprocess.Popen([str(exe)] + args, stdout=log, stderr=subprocess.STDOUT,
                            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def frame(msg_type, payload=b""):
    return struct.pack("<IHBB", 4 + len(payload), msg_type, FLAG_RELIABLE, 0) + payload


def report_save_changed(port):
    """Connects as the host's adapter and reports that data0.bin was written."""
    deadline = time.time() + TRANSFER_TIMEOUT_S
    while True:
        try:
            sock = socket.create_connection(("127.0.0.1", port))
            break
        except OSError:
            if time.time() > deadline:
                raise
            time.sleep(CONNECT_RETRY_S)
    game = b"re0"
    sock.sendall(frame(MSG_HELLO, struct.pack("<HB", PROTO, len(game)) + game))
    time.sleep(CONNECT_RETRY_S)
    sock.sendall(frame(MSG_SAVE_CHANGED, FILE_NAME.encode("ascii")))
    return sock


def ini_values(path):
    lines = Path(path).read_text().splitlines() if Path(path).exists() else []
    return dict(line.split("=", 1) for line in lines if "=" in line)


def wait_for(condition, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if condition():
            return True
        time.sleep(POLL_S)
    return False


def main():
    work = Path(tempfile.mkdtemp(prefix="cf_save_sync_"))
    source, host_game, guest_game = work / "source", work / "host_game", work / "guest_game"
    for d in (source, host_game, guest_game / "coop" / "session"):
        d.mkdir(parents=True)
    (source / FILE_NAME).write_bytes(os.urandom(FILE_BYTES))
    (guest_game / "coop" / "adapter.ini").write_text("port=1234\ncoop=1\n")
    (guest_game / "coop" / "session" / "stale.bin").write_bytes(b"stale")

    common = ["--transport", "local", "--no-launch"]
    host = launcher(["host", "re0", *common, "--local-port", str(HOST_PORTS[0]), "--peer-port", str(HOST_PORTS[1]),
                     "--bridge-port", str(HOST_BRIDGE_PORT), "--save-source", str(source), "--game-dir", str(host_game)], work / "host.log")
    guest = launcher(["join", "0", "--game", "re0", *common, "--local-port", str(HOST_PORTS[1]), "--peer-port", str(HOST_PORTS[0]),
                      "--bridge-port", "27991", "--game-dir", str(guest_game)], work / "guest.log")
    failures = []
    try:
        received = guest_game / "coop" / "session" / FILE_NAME
        if not wait_for(lambda: received.exists() and ini_values(guest_game / "coop" / "adapter.ini").get("coop") == "1"
                        and not (guest_game / "coop" / "session" / "stale.bin").exists(), TRANSFER_TIMEOUT_S):
            failures.append("guest never got the file with coop=1")
        elif sha(received) != sha(source / FILE_NAME):
            failures.append("hash mismatch")
        if ini_values(guest_game / "coop" / "adapter.ini").get("port") != "1234":
            failures.append("guest ini lost its other keys")
        if ini_values(host_game / "coop" / "adapter.ini").get("coop") != "1":
            failures.append("host coop not 1")
        if (host_game / "coop" / "session").exists():
            failures.append("host has a session dir")
        (source / FILE_NAME).write_bytes(os.urandom(FILE_BYTES))
        adapter = report_save_changed(HOST_BRIDGE_PORT)
        if not wait_for(lambda: received.exists() and sha(received) == sha(source / FILE_NAME), TRANSFER_TIMEOUT_S):
            failures.append("guest never got the rewritten save after SAVE_CHANGED")
        adapter.close()
    finally:
        for proc in (guest, host):
            proc.send_signal(signal.CTRL_BREAK_EVENT)
        for proc in (guest, host):
            try:
                proc.wait(EXIT_TIMEOUT_S)
            except subprocess.TimeoutExpired:
                proc.kill()
                failures.append("launcher did not exit on Ctrl+Break")

    for name, game in (("guest", guest_game), ("host", host_game)):
        if ini_values(game / "coop" / "adapter.ini").get("coop") != "0":
            failures.append(f"{name} coop not reset to 0")
        if (game / "coop" / "session").exists():
            failures.append(f"{name} session dir not removed")

    print("FAIL: " + "; ".join(failures) if failures else "save_sync_test OK")
    print(f"logs in {work}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
