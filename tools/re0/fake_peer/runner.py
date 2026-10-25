"""The scenario context and the run: builds the fake machine (link, peer, modules), starts launchers and log tails, hands the
scenario a small blocking API, and ends with the gates, the report and the run folder. Network work runs on threads owned
here; scenario code never touches sockets."""
import random
import re
import shutil
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

from fake_peer import gamedir
from fake_peer import gates as gate_engine
from fake_peer import library as library_module
from fake_peer import wire
from fake_peer.authority import RoomAuthority
from fake_peer.boss import Boss
from fake_peer.contact import Contact
from fake_peer.enemies import Enemies
from fake_peer.launchers import Launchers, Ports, find_launcher
from fake_peer.library import FILLED_BY, Journal, Library, wall_ms
from fake_peer.impair import make_params
from fake_peer.link import BridgeLink, Dispatcher, StandinLink, GAME_PORT
from fake_peer.logwatch import DumpWatch, FileTail, LogStore, SOURCE_ADAPTER, SOURCE_HOST, SOURCE_JOIN
from fake_peer.peer import Peer, RemoteView
from fake_peer.rooms import Rooms
from fake_peer.session import SessionFlows
from fake_peer.shaper import OUT, Shaper
from fake_peer.sources import Sources
from fake_peer.story import Story
from fake_peer.world import Digest, Flags, Items, Props

TICK_S = 0.005
SESSION_TIMEOUT_S = 60.0
GAME_START_TIMEOUT_S = 1800.0  # the stand-in is ready when the game starts, which the user does by hand
DEFAULT_WAIT_FRAME_S = 10.0
RUN_FOLDER_FORMAT = "%Y%m%d-%H%M%S"
ADAPTER_LOG = Path("coop") / "adapter.log"
DEV_COMMAND_FILE = Path("coop") / "cmd.txt"
FAKE_PEER_DIR = Path("coop") / "fake_peer"
MODE_LINE = re.compile(r"^config: partner_mode=(input|state)")
SEATS = ("guest", "host")
SEED_LIMIT = 1 << 32
CLI_LINK_SETTINGS = {"delay_ms": "latency_ms", "jitter_ms": "jitter_ms", "loss": "loss"}
TRANSPORTS = ("launchers", "standin", "bridge")
DEFAULT_TRANSPORT = {"guest": "launchers", "host": "standin"}


class Stopped(Exception):
    """Ctrl+C or a dead link: the scenario ends, the report is still written."""


class Options:
    """What the command line says (or a test sets); anything unset has a default."""

    def __init__(self, **values):
        self.scenario = values.get("scenario")
        self.seat = values.get("seat", "guest")
        self.transport = values.get("transport")
        self.bridge_port = values.get("bridge_port")
        self.game_dir = values.get("game_dir")
        self.launcher = values.get("launcher")
        self.save_source = values.get("save_source")
        self.out = values.get("out")
        self.scratch = values.get("scratch", False)
        self.no_game = values.get("no_game", False)
        self.fake_character = values.get("fake_character")
        self.delay = values.get("delay", 1.0)
        self.offset_x = values.get("offset_x", 150.0)
        self.link = values.get("link")
        self.seed = values["seed"] if values.get("seed") is not None else random.SystemRandom().randrange(SEED_LIMIT)
        overrides = {setting: values[key] for key, setting in CLI_LINK_SETTINGS.items() if values.get(key) is not None}
        self.link_params = make_params(self.link, **overrides)
        self.ports = values.get("ports") or Ports()
        self.status = values.get("status", True)
        self.input_mode = values.get("input_mode", False)
        self.seconds = values.get("seconds")
        self.round = values.get("round")
        self.dev_commands = values.get("dev_commands", False)  # the game runs with adapter.ini dev_commands=1


def check_needs(scenario, options):
    """Problems that stop the run before anything starts."""
    problems = []
    if options.seat not in scenario.SEATS:
        problems.append(f"{scenario.ID} runs as {' or '.join(scenario.SEATS)}, not {options.seat}")
    if options.seat in scenario.SCRATCH_SEATS and not options.scratch:
        problems.append(f"{scenario.ID} changes the save's state: run it on a scratch save with --scratch")
    return problems


def game_folder(options):
    """The game folder: --game-dir, else the Steam library that holds the app."""
    return Path(options.game_dir) if options.game_dir else gamedir.find_game_dir()


def output_root(options):
    """Where run folders and the library live: --out, else coop/fake_peer in the game folder; None without a game folder."""
    if options.out:
        return Path(options.out)
    game = game_folder(options)
    return game / FAKE_PEER_DIR if game else None


class Context:
    """One fake machine: the scenario's handle on the link, the modules, the log, the marks and the gates."""

    def __init__(self, options, run_dir=None):
        self.options = options
        self.seat = options.seat
        self.lock = threading.RLock()
        self.run_dir = Path(run_dir) if run_dir else None
        if self.run_dir:
            self.run_dir.mkdir(parents=True, exist_ok=True)
        self.journal = Journal(self.run_dir / library_module.FRAMES_FILE if self.run_dir else None)
        self.store = LogStore()
        self.shaper = Shaper(options.link_params, options.seed)
        self.marks = {}
        self.input_mode = options.input_mode
        self.started_ms = wall_ms()
        self.stopped = threading.Event()
        self.launchers = None
        self.tails = []
        self.dumps = None
        self.gates = []
        self.data = {}  # values a scenario keeps for its own gates
        self.skipped = {}
        self.manual_questions = []
        self.waits_missed = []
        self._steps = 0
        self.store.listeners.append(self._watch_mode)
        self.library = self._load_library()
        self.dispatcher = Dispatcher(self.journal, self.lock)
        self.link = self._make_link()
        self.link.guard = self._allow_send
        character = self._character()
        self.peer = Peer(self, character, status=options.status)
        self.remote = RemoteView(self)
        self.sources = Sources(self)
        self.rooms = Rooms(self)
        self.authority = RoomAuthority(self)
        self.flows = SessionFlows(self)
        self.enemies = Enemies(self)
        self.contact = Contact(self)
        self.items = Items(self)
        self.flags = Flags(self)
        self.digest = Digest(self)
        self.props = Props(self)
        self.boss = Boss(self)
        self.story = Story(self)
        self.modules = [self.peer, self.remote, self.rooms, self.authority, self.sources, self.flows, self.enemies, self.contact,
                        self.items, self.flags, self.digest, self.props, self.boss, self.story]
        for module in self.modules:
            self.dispatcher.register(module)
        self._ticker = threading.Thread(target=self._tick_loop, daemon=True)

    # ---- construction --------------------------------------------------------------------------------------------

    def _character(self):
        if self.options.fake_character:
            return wire.CHARACTER_NAMES.index(self.options.fake_character)
        return wire.BILLY if self.seat == "guest" else wire.REBECCA

    def _load_library(self):
        root = output_root(self.options)
        if root and root.exists():
            return library_module.build(root)
        return Library()

    def _make_link(self):
        o = self.options
        transport = o.transport or DEFAULT_TRANSPORT[self.seat]
        if transport == "standin":
            if self.seat != "host":
                raise ValueError("standin is the game's own launcher: the game is the guest and the fake the host")
            return StandinLink(GAME_PORT, self.journal, self.dispatcher.dispatch, self.shaper)
        port = o.bridge_port if transport == "bridge" else o.ports.fake_bridge
        if port is None:
            raise ValueError("bridge transport needs --bridge-port")
        return BridgeLink(port, self.journal, self.dispatcher.dispatch, self.shaper)

    def _allow_send(self, msg_type):
        return not (self.input_mode and wire.is_state_type(msg_type))

    def _watch_mode(self, line):
        found = MODE_LINE.match(line.text) if line.source == SOURCE_ADAPTER else None
        if found:
            self.input_mode = found.group(1) == "input"

    # ---- lifecycle -----------------------------------------------------------------------------------------------

    def start(self):
        o = self.options
        transport = o.transport or DEFAULT_TRANSPORT[self.seat]
        game = None if o.no_game else game_folder(o)
        if transport == "launchers":
            self.launchers = Launchers(find_launcher(o.launcher), self.run_dir, o.ports, self.seat, o.game_dir, o.save_source)
            self.launchers.start()
            self.tails += [FileTail(self.launchers.logs["host"], SOURCE_HOST, self.store),
                           FileTail(self.launchers.logs["join"], SOURCE_JOIN, self.store)]
        if game and not o.no_game:
            self.tails.append(FileTail(game / ADAPTER_LOG, SOURCE_ADAPTER, self.store, stamped=True, from_end=True))
            self.dumps = DumpWatch(game / "coop")
        for tail in self.tails:
            tail.start()
        self.link.abort = self._launcher_died
        self.link.start()
        self._ticker.start()

    def _launcher_died(self):
        return self.launchers is not None and not self.launchers.alive()

    def wait_ready(self):
        """Waits for the session: both ends welcomed (launchers), or the game's adapter linked (stand-in)."""
        s = self.link.session
        standin = isinstance(self.link, StandinLink)
        deadline = time.monotonic() + (GAME_START_TIMEOUT_S if standin else SESSION_TIMEOUT_S)
        while not (s.ready and self.link.connected.is_set()):
            self._check_stop()
            if self._launcher_died():
                raise ConnectionError(f"a launcher exited: see {self.run_dir}")
            if time.monotonic() > deadline:
                raise TimeoutError("the session did not form")
            time.sleep(0.05)

    def _tick_loop(self):
        while not self.stopped.is_set():
            now = time.monotonic()
            with self.lock:
                for module in self.modules:
                    try:
                        module.tick(now)
                    except Exception as error:  # a module fault must not end the run
                        self.journal.note(f"fault in {type(module).__name__}.tick: {type(error).__name__}: {error}")
            time.sleep(TICK_S)

    def stop(self):
        self.stopped.set()
        self.sources.use("silent")
        self.link.close()
        for tail in self.tails:
            tail.poll()
            tail.stop()
        if self.launchers:
            self.launchers.stop()
        self.journal.close()

    def _check_stop(self):
        if self.stopped.is_set():
            raise Stopped()

    # ---- the scenario's API --------------------------------------------------------------------------------------

    def step(self, text):
        """A numbered, terse instruction for the user."""
        self._steps += 1
        print(f"{self._steps}. {text}", flush=True)

    def duration(self, default):
        """How long a timed phase runs: the scenario's own length unless --seconds shortens or lengthens it."""
        return self.options.seconds if self.options.seconds else default

    def say(self, text):
        print(text, flush=True)

    def link_note(self):
        """The run's link model: repeat a run with the same `--link` and `--seed`."""
        return f"link profile {self.options.link or 'none'} seed {self.options.seed}"

    def wait(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self._check_stop()
            time.sleep(min(0.05, max(deadline - time.monotonic(), 0)))

    def wait_until(self, condition, timeout, label="condition"):
        """True once `condition()` holds (checked under the lock), False when `timeout` seconds pass first."""
        deadline = time.monotonic() + timeout
        while True:
            with self.lock:
                if condition():
                    return True
            if time.monotonic() > deadline:
                self.waits_missed.append(label)
                return False
            self._check_stop()
            time.sleep(0.02)

    def wait_log(self, pattern, timeout, source=SOURCE_ADAPTER, since=None):
        """The first (line, match) after `since` (default: the run start), or None, recorded as missing."""
        deadline = time.monotonic() + timeout
        start = self.started_ms if since is None else since
        while True:
            hit = self.store.wait(pattern, min(0.5, max(deadline - time.monotonic(), 0.01)), source, start)
            if hit:
                return hit
            self._check_stop()
            if time.monotonic() > deadline:
                self.waits_missed.append(pattern)
                return None

    def wait_frame(self, msg_type, predicate=None, timeout=DEFAULT_WAIT_FRAME_S, since=None):
        """The first frame from the game of this type (and passing `predicate(entry)`) after `since` (default: now)."""
        start = wall_ms() if since is None else since
        found = []

        def check():
            for entry in self.journal.frames(msg_type, library_module.IN, start):
                if predicate is None or predicate(entry):
                    found.append(entry)
                    return True
            return False

        return found[0] if self.wait_until(check, timeout, f"frame {wire.type_name(msg_type)}") else None

    def send(self, msg_type, payload=b"", reliable=True):
        with self.lock:
            return self.link.send(msg_type, payload, wire.FLAG_RELIABLE if reliable else 0)

    def source(self, name, **opts):
        with self.lock:
            self.sources.use(name, **opts)

    def shape(self, cut=None, stall=None, direction=OUT, **config):
        """Link shaper: `cut=S` or `stall=S` seconds, or model settings (latency_ms, jitter_ms, loss, ...) for the rest of
        the run, on the fake's outgoing frames, the game's incoming ones (`direction="in"`) or both (`"both"`)."""
        if cut:
            self.shaper.cut(cut, direction)
        if stall:
            self.shaper.stall(stall, direction)
        if config:
            self.shaper.configure(direction, **config)

    def drop(self, seconds):
        """Stand-in only: the heartbeats stop too, so the adapter's own 5 s link timeout fires."""
        self.link.stop_heartbeats(seconds)
        self.shaper.stall(seconds)

    def vanish(self):
        """The fake's machine goes away: PEER_DOWN (stand-in) or its launcher stopped (launchers)."""
        if self.launchers:
            self.launchers.stop_join()
        elif hasattr(self.link, "peer_down"):
            self.link.peer_down()

    def mark(self, name):
        self.marks[name] = wall_ms()
        self.journal.note(f"mark {name}")

    def mark_clip(self, character, name, edge):
        self.mark(f"{library_module.CLIP_MARK}:{character}:{name}:{edge}")

    def add_gates(self, gate_list):
        self.gates.extend(gate_list)

    def dev_command(self, text):
        """Writes one developer command into the game's coop\\cmd.txt (dev_command_rule.h) for the adapter to take within a
        second. False, and nothing written, unless the run says the game polls it (--dev-commands) and has a game folder."""
        game = None if self.options.no_game or not self.options.dev_commands else game_folder(self.options)
        if game is None:
            return False
        (game / DEV_COMMAND_FILE).write_text(text + "\n", encoding="utf-8")
        return True

    def skip(self, gate_id, reason):
        self.skipped[gate_id] = reason

    def require(self, kind, *gate_ids):
        """True when the library holds `kind`; otherwise skips the gates, naming the run that would fill it."""
        if self.library.has(kind):
            return True
        for gate_id in gate_ids:
            self.skip(gate_id, f"needs a {kind} recording: run {FILLED_BY[kind]}")
        return False

    def manual(self, gate_id, question):
        self.manual_questions.append((gate_id, question))

    def wait_enter(self, prompt):
        """The record scenario's marks: waits for Enter in the console."""
        print(prompt, flush=True)
        if sys.stdin.isatty():
            try:
                input()
            except EOFError:
                self.stopped.set()
                raise Stopped() from None

    # ---- the end -------------------------------------------------------------------------------------------------

    def evidence(self):
        mirrors = {"authority": self.authority.table, "rooms": self.rooms, "enemies": self.enemies, "contact": self.contact,
                   "flags": self.flags, "digest": self.digest, "items": self.items, "props": self.props, "boss": self.boss,
                   "flows": self.flows, "story": self.story, "remote": self.remote, "peer": self.peer}
        return gate_engine.Evidence(self.store, self.journal, self.marks, mirrors,
                                    self.dumps.new() if self.dumps else ())

    def build_report(self):
        report = gate_engine.Report()
        evidence = self.evidence()
        with self.lock:
            for gate in self.gates:
                if gate.id in self.skipped:
                    report.add(gate.id, gate_engine.SKIP, self.skipped[gate.id])
                else:
                    result = gate.judge(evidence)
                    report.add(gate.id, result.verdict, f"{gate.title}: {result.detail}" if result.detail else gate.title)
            result = gate_engine.judge_global(evidence)
            report.add(gate_engine.GLOBAL_GATE_ID, result.verdict, result.detail)
        for pattern in self.waits_missed:
            report.add("wait", gate_engine.MISSING, pattern)
        for gate_id, question in self.manual_questions:
            report.add(gate_id, gate_engine.MANUAL, f"{question}: {self._ask(question)}")
        if self.dispatcher.malformed or self.dispatcher.faults:
            report.notes.append(f"malformed frames {dict(self.dispatcher.malformed)} faults {dict(self.dispatcher.faults)}")
        report.notes.append(self.link_note())
        return report

    @staticmethod
    def _ask(question):
        if not sys.stdin.isatty():
            return "unanswered"
        try:
            answer = input(f"{question} (y/n) ").strip().lower()
        except EOFError:
            return "unanswered"
        return "yes" if answer.startswith("y") else "no"

    def save_artifacts(self):
        """The adapter log segment of the run and the crash dumps that appeared, next to frames.jsonl."""
        if not self.run_dir:
            return
        lines = [f"{line.text}" for line in self.store.lines if line.source == SOURCE_ADAPTER]
        (self.run_dir / "adapter_segment.log").write_text("\n".join(lines) + "\n", encoding="utf-8")
        for dump in (self.dumps.new() if self.dumps else ()):
            shutil.copy2(dump, self.run_dir / dump.name)


def run_folder(root, scenario_id):
    return Path(root) / f"{datetime.now().strftime(RUN_FOLDER_FORMAT)}-{scenario_id}"


def run_scenario(scenario, options):
    """Runs a scenario against a game: needs check, session, scenario code, gates, report. Returns the exit code."""
    problems = check_needs(scenario, options)
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 2
    folder = run_folder(output_root(options) or Path("."), scenario.ID)
    ctx = Context(options, folder)
    ctx.input_mode = ctx.input_mode or getattr(scenario, "MODE", None) == "input"
    ctx.add_gates(scenario.gates(options.seat))
    if options.seat in scenario.SCRATCH_SEATS:
        ctx.say("scratch: this run changes the save's state")
    try:
        ctx.say(ctx.link_note())
        ctx.start()
        ctx.wait_ready()
        ctx.say("ready")
        scenario.run(ctx)
    except (KeyboardInterrupt, Stopped):
        ctx.say("stopped")
    except (TimeoutError, OSError, ValueError) as error:
        print(f"{type(error).__name__}: {error}", file=sys.stderr)
    finally:
        ctx.stop()
    report = ctx.build_report()
    ctx.save_artifacts()
    if ctx.run_dir:
        report.write(ctx.run_dir)
    print(report.text(ctx.run_dir))
    return 1 if report.failed else 0
