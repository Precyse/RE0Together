"""Guest diagnostics reach the host on local transport: new adapter.log lines arrive in coop\\peer_<id>.log and a
crash dump written during the session arrives byte-identical as coop\\peer_<id>_<name>.dmp.
Usage: python tools/diagnostics_test.py   (COOP_LAUNCHER overrides the exe, as in save_sync_test.py)"""
import os
import signal
import sys
import tempfile
from pathlib import Path

from save_sync_test import EXIT_TIMEOUT_S, TRANSFER_TIMEOUT_S, launcher, sha, wait_for

PORTS = (27983, 27984)
BRIDGE_PORTS = (27992, 27993)
DUMP_NAME = "crash-20260930-120000.dmp"
DUMP_BYTES = 300_000
LOG_LINE = "diagnostics test line\n"
SETTLE_S = 3


def main():
    work = Path(tempfile.mkdtemp(prefix="cf_diagnostics_"))
    source, host_game, guest_game = work / "source", work / "host_game", work / "guest_game"
    for d in (source, host_game / "coop", guest_game / "coop"):
        d.mkdir(parents=True)
    (source / "data0.bin").write_bytes(os.urandom(1000))
    (guest_game / "coop" / "adapter.log").write_text("old line before the session\n")

    common = ["--transport", "local", "--no-launch"]
    host = launcher(["host", "re0", *common, "--local-port", str(PORTS[0]), "--peer-port", str(PORTS[1]),
                     "--bridge-port", str(BRIDGE_PORTS[0]), "--save-source", str(source), "--game-dir", str(host_game)],
                    work / "host.log")
    guest = launcher(["join", "0", "--game", "re0", *common, "--local-port", str(PORTS[1]), "--peer-port", str(PORTS[0]),
                      "--bridge-port", str(BRIDGE_PORTS[1]), "--game-dir", str(guest_game)], work / "guest.log")
    failures = []
    try:
        wait_for(lambda: False, SETTLE_S)
        with open(guest_game / "coop" / "adapter.log", "a") as log:
            log.write(LOG_LINE)
        dump = guest_game / "coop" / DUMP_NAME
        dump.write_bytes(os.urandom(DUMP_BYTES))

        def peer_file(suffix):
            return next((p for p in (host_game / "coop").glob(f"peer_*{suffix}")), None)

        if not wait_for(lambda: peer_file(".log") and LOG_LINE in peer_file(".log").read_text(), TRANSFER_TIMEOUT_S):
            failures.append("log line never reached the host")
        elif "old line" in peer_file(".log").read_text():
            failures.append("lines from before the session were sent")
        received = lambda: peer_file("_" + DUMP_NAME)
        if not wait_for(lambda: received() and received().stat().st_size == DUMP_BYTES, TRANSFER_TIMEOUT_S):
            failures.append("crash dump never arrived complete")
        elif sha(received()) != sha(dump):
            failures.append("crash dump differs")
    finally:
        for proc in (guest, host):
            proc.send_signal(signal.CTRL_BREAK_EVENT)
        for proc in (guest, host):
            try:
                proc.wait(EXIT_TIMEOUT_S)
            except Exception:
                proc.kill()

    print("FAIL: " + "; ".join(failures) if failures else "diagnostics_test OK")
    print(f"logs in {work}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
