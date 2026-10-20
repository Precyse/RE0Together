"""Spawns and stops the two CLI launchers of a one-PC session (local transport) and finds the launcher build. Only the
processes started here are ever stopped."""
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
LAUNCHER_CANDIDATES = (
    REPO / "launcher" / "test" / "coop-launcher.exe",
    REPO / "launcher" / "bin" / "Release" / "net8.0-windows" / "coop-launcher.exe",
)
HOST_LOCAL_PORT, JOIN_LOCAL_PORT = 27961, 27962
FAKE_BRIDGE_PORT = 27970
STOP_WAIT_S = 5.0
GAME_ID = "re0"
JOIN_TARGET = "0"


def find_launcher(explicit=None):
    candidates = [Path(explicit)] if explicit else list(LAUNCHER_CANDIDATES)
    for path in candidates:
        if path.exists():
            return path
    raise FileNotFoundError("no launcher build: " + ", ".join(str(c) for c in candidates))


class Ports:
    """The loopback ports of one session; the defaults leave the game's own port 27960 to the game."""

    def __init__(self, host_local=HOST_LOCAL_PORT, join_local=JOIN_LOCAL_PORT, fake_bridge=FAKE_BRIDGE_PORT, game_bridge=None):
        self.host_local, self.join_local, self.fake_bridge, self.game_bridge = host_local, join_local, fake_bridge, game_bridge


class Launchers:
    """The host and join launchers of a session. `fake_seat` is the seat the fake plays: its launcher gets the fake's bridge
    port, the other one the game's (the profile port unless `ports.game_bridge` is given)."""

    def __init__(self, launcher, run_dir, ports=None, fake_seat="guest", game_dir=None, save_source=None):
        self.launcher, self.run_dir = Path(launcher), Path(run_dir)
        self.game_dir, self.save_source = game_dir, save_source
        self.ports = ports or Ports()
        self.fake_seat = fake_seat
        self.procs = {}
        self.logs = {"host": self.run_dir / "host_launcher.log", "join": self.run_dir / "join_launcher.log"}
        self._files = []

    def commands(self):
        p = self.ports
        host_bridge = p.fake_bridge if self.fake_seat == "host" else p.game_bridge
        join_bridge = p.fake_bridge if self.fake_seat == "guest" else p.game_bridge
        return {
            "host": self._command("host", GAME_ID, p.host_local, p.join_local, host_bridge, host=True),
            "join": self._command("join", JOIN_TARGET, p.join_local, p.host_local, join_bridge),
        }

    def _command(self, verb, target, local, peer, bridge, host=False):
        args = [str(self.launcher), verb, target, "--transport", "local", "--local-port", str(local), "--peer-port", str(peer),
                "--game", GAME_ID, "--no-launch"]
        args += ["--bridge-port", str(bridge)] if bridge else []
        args += ["--game-dir", str(self.game_dir)] if self.game_dir else []
        return args + (["--save-source", str(self.save_source)] if host and self.save_source else [])

    def start(self):
        self.run_dir.mkdir(parents=True, exist_ok=True)
        for role, args in self.commands().items():
            log = open(self.logs[role], "wb")
            self._files.append(log)
            self.procs[role] = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)

    def alive(self):
        return all(proc.poll() is None for proc in self.procs.values())

    def stop(self):
        for proc in self.procs.values():
            if proc.poll() is None:
                proc.terminate()
        for proc in self.procs.values():
            try:
                proc.wait(STOP_WAIT_S)
            except subprocess.TimeoutExpired:
                proc.kill()
        for handle in self._files:
            handle.close()

    def stop_join(self):
        """Stops only the join launcher (the guest machine vanishes)."""
        proc = self.procs.get("join")
        if proc and proc.poll() is None:
            proc.terminate()
