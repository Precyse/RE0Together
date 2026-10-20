"""Echo hub: stands in for every instance's co-op launcher so up to four real SR2 instances play together on one PC
(docs/SR2_NOTES.md "Echo testing"). It listens on the adapter port 127.0.0.1:27962, tells the instances apart by the folder the
connecting process runs from, answers HELLO with the snapshot of that instance's view (echo_session.py, a port of the
launcher's Session) and relays GAME_DATAGRAM between them. Join and status files are the launcher's (echo_files.py), the
assertions are echo_checks.py.

Usage: python tools/sr2/echo_hub.py [--count 4] [--launch] [--delay-ms 0] [--max-players 4] [--game-dir DIR] [--clones-root DIR]
Console: leave k | back k | drop k | restore k | rejoin k | status | mark | check <name> [k] | quit"""
import argparse
import os
import socket
import struct
import sys
import threading
import time
from collections import Counter, deque
from pathlib import Path

import echo_checks
import echo_clones
import echo_files
import echo_identity
import echo_launch
import wire
from echo_check_table import CHECKS
from echo_session import DROP_REASONS, Views, steam_id

LISTEN_HOST = "127.0.0.1"
ADAPTER_PORT = 27962
DEFAULT_COUNT = 4
DEFAULT_MAX_PLAYERS = 4
SEND_TIMEOUT_MS = 2000
PUMP_S = 0.02
HEARTBEAT_S = 1.0
IDENTIFY_PID = "pid"
IDENTIFY_ORDER = "order"
PROMPT = "echo> "
FRAME_NAMES = {wire.MSG_HELLO: "HELLO", wire.MSG_HEARTBEAT: "HEARTBEAT", wire.MSG_GAME_DATAGRAM: "GAME_DATAGRAM"}
INSTANCE_COMMANDS = ("leave", "back", "drop", "restore", "rejoin")


class HubError(Exception):
    """A refusal the user can act on."""


class Link:
    """One game's connection to the hub."""

    def __init__(self, sock, instance, pid):
        self.sock = sock
        self.instance = instance
        self.pid = pid
        self.closed = False


def frames_of(events):
    """The wire frames of the events a view produced."""
    encoders = {"Welcome": lambda e: wire.welcome(e.local_slot, e.host_slot, e.max_players, e.epoch, e.local_id),
                "PeerUp": lambda e: wire.peer_up(e.slot, e.steam_id, e.name),
                "PeerDown": lambda e: wire.peer_down(e.slot)}
    return [encoders[type(event).__name__](event) for event in events]


class Hub:
    """The listener, the links and the lobby. `folders` maps each instance (1 = the host) to its game folder. One lock guards
    the lobby, the links and the counters; there is one reader thread per link, one pump thread and the console."""

    def __init__(self, folders, max_players=DEFAULT_MAX_PLAYERS, delay_ms=0, port=ADAPTER_PORT, identify=IDENTIFY_PID, out=print):
        self.folders = folders
        self.instances = sorted(folders)
        self.delay_s = delay_ms / 1000
        self.requested_port = port
        self.out = out
        self.views = Views(len(folders), max_players)
        self.lock = threading.RLock()
        self.links = {}
        self.delayed = {k: deque() for k in self.instances}
        self.frame_counts = Counter()
        self.join_files = echo_files.JoinFiles(folders)
        self.statuses = {k: echo_files.StatusHistory() for k in self.instances}
        self.tails = {k: echo_checks.LogTail(echo_files.coop_path(folders[k], echo_files.LOG_FILE)) for k in self.instances}
        self.hello_events = {k: threading.Event() for k in self.instances}
        self.stopped = threading.Event()
        self.listener = None
        self.port = port
        self.identify = self.identify_by_pid if identify == IDENTIFY_PID else self.identify_by_order

    def name_of(self, instance):
        return self.views.names[steam_id(instance)]

    def start(self):
        listener = socket.socket()
        try:
            listener.bind((LISTEN_HOST, self.requested_port))
        except OSError as error:
            listener.close()
            raise HubError(f"port {self.requested_port} is taken ({error}); close the co-op launcher first") from error
        listener.listen()
        self.listener = listener
        self.port = listener.getsockname()[1]
        for k in self.instances:
            echo_files.clear_status(self.folders[k])
            self.join_files.write(k, self.name_of(k))
        for target in (self.accept_loop, self.pump):
            threading.Thread(target=target, daemon=True).start()

    def stop(self):
        """Removes the join files the hub wrote and closes the links; a running game is left running."""
        self.stopped.set()
        with self.lock:
            self.join_files.remove_all()
            for link in list(self.links.values()):
                self.close_link(link)
        if self.listener:
            self.listener.close()

    def identify_by_pid(self, sock):
        """(instance, pid) of the process that owns the other end of the connection, from the folder of its exe."""
        pid = echo_identity.client_pid(sock.getpeername()[1], self.port)
        path = echo_identity.exe_path(pid) if pid else None
        if not path:
            return None, pid
        folder = echo_identity.normal_path(os.path.dirname(path))
        for k, game_folder in self.folders.items():
            if echo_identity.normal_path(game_folder) == folder:
                return k, pid
        return None, pid

    def identify_by_order(self, _sock):
        """For tests only: the lowest instance that has no link yet."""
        with self.lock:
            return next((k for k in self.instances if k not in self.links), None), None

    def accept_loop(self):
        while not self.stopped.is_set():
            try:
                sock, _address = self.listener.accept()
            except OSError:
                return
            threading.Thread(target=self.serve, args=(sock,), daemon=True).start()

    def serve(self, sock):
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDTIMEO, struct.pack("I", SEND_TIMEOUT_MS))
        instance, pid = self.identify(sock)
        if instance is None:
            self.out(f"connection from pid {pid} is not an instance of this rig, closed")
            sock.close()
            return
        link = Link(sock, instance, pid)
        with self.lock:
            previous = self.links.get(instance)
            if previous:
                self.close_link(previous)
            self.links[instance] = link
        self.out(f"instance {instance} connected (pid {pid})")
        try:
            while True:
                self.handle(link, wire.read_frame(sock))
        except (ConnectionError, OSError):
            pass
        finally:
            with self.lock:
                self.close_link(link)

    def close_link(self, link):
        if link.closed:
            return
        link.closed = True
        link.sock.close()
        if self.links.get(link.instance) is link:
            del self.links[link.instance]
            self.views.unlinked(link.instance)
            self.hello_events[link.instance].clear()
            self.out(f"instance {link.instance} disconnected")

    def send(self, instance, frame):
        link = self.links.get(instance)
        if link is None:
            return
        try:
            link.sock.sendall(frame)
        except OSError:
            self.close_link(link)

    def send_events(self, events_by_instance):
        for instance, events in events_by_instance.items():
            for frame in frames_of(events):
                self.send(instance, frame)

    def handle(self, link, frame):
        msg_type, _flags, slot, payload = frame
        with self.lock:
            self.frame_counts[(link.instance, FRAME_NAMES.get(msg_type, f"0x{msg_type:04X}"))] += 1
            if msg_type == wire.MSG_HELLO:
                self.send_events({link.instance: self.views.hello(link.instance)})
                self.hello_events[link.instance].set()
            elif msg_type == wire.MSG_GAME_DATAGRAM and len(payload) > wire.DST.size:
                self.relay(link.instance, slot, payload)

    def relay(self, sender, dst_slot, payload):
        route = self.views.route(sender, dst_slot)
        for receiver, slot in route.deliveries:
            frame = wire.encode(wire.MSG_GAME_DATAGRAM, 0, slot, payload)
            if self.delay_s:
                self.delayed[receiver].append((time.monotonic() + self.delay_s, frame))
            else:
                self.send(receiver, frame)

    def pump(self):
        """Delayed frames, the heartbeat of every link and the status files."""
        next_heartbeat = next_status = 0.0
        while not self.stopped.wait(PUMP_S):
            now = time.monotonic()
            with self.lock:
                for receiver, queue in self.delayed.items():
                    while queue and queue[0][0] <= now:
                        self.send(receiver, queue.popleft()[1])
                if now >= next_heartbeat:
                    next_heartbeat = now + HEARTBEAT_S
                    for instance in list(self.links):
                        self.send(instance, wire.encode(wire.MSG_HEARTBEAT, 0, 0))
                if now >= next_status:
                    next_status = now + echo_files.STATUS_POLL_S
                    self.poll_status()

    def poll_status(self):
        for k in self.instances:
            line = echo_files.read_status(self.folders[k])
            if self.statuses[k].record(line):
                self.out(f"status {echo_checks.label_of(k)}: {line}")

    def command(self, line):
        """Runs one console line and returns what it prints."""
        words = line.split()
        if not words:
            return ""
        name, args = words[0], words[1:]
        try:
            if name in INSTANCE_COMMANDS:
                return self.instance_command(name, self.instance_arg(args))
            if name == "status":
                return self.status()
            if name == "mark":
                return self.mark()
            if name == "check":
                return self.check(args)
            if name == "quit":
                self.stop()
                return "join files removed, games left running"
        except (HubError, ValueError) as error:
            return f"error: {error}"
        return f"unknown command {name!r}; commands: {', '.join(INSTANCE_COMMANDS)}, status, mark, check, quit"

    def instance_arg(self, args):
        if len(args) != 1 or not args[0].isdigit() or int(args[0]) not in self.instances:
            raise HubError(f"expected an instance number {self.instances[0]} to {self.instances[-1]}")
        return int(args[0])

    def instance_command(self, name, k):
        with self.lock:
            if name == "leave":
                self.send_events(self.views.leave(k))
                self.join_files.remove(k)
            elif name == "back":
                self.send_events(self.views.back(k))
                self.join_files.write(k, self.name_of(k))
            elif name == "drop":
                self.views.drop_link(k)
            elif name == "restore":
                self.views.restore_link(k)
            else:
                self.join_files.write(k, self.name_of(k))
        return f"{name} {k} done"

    def mark(self):
        with self.lock:
            for k in self.instances:
                self.tails[k].mark()
                self.statuses[k].mark()
            self.views.clear_counters()
        return "marked"

    def status(self):
        with self.lock:
            rows = [f"hub on {LISTEN_HOST}:{self.port}, delay {self.delay_s * 1000:.0f} ms"]
            for k in self.instances:
                view, link, traffic = self.views.views[k], self.links.get(k), self.views.traffic[k]
                slot = self.views.slot_of(k)
                rows.append(f"{echo_checks.label_of(k):7} {self.folders[k]}  pid {link.pid if link else '-'}  "
                            f"link {'up' if link else 'down'}{' (dropped)' if k in self.views.down else ''}  "
                            f"slot {'-' if slot is None else slot}  epoch {view.epoch}  status '{self.statuses[k].current}'  "
                            f"datagrams in {traffic.received} out {traffic.sent}")
            rows.append("drops " + " ".join(f"{reason}={self.views.drops[reason]}" for reason in DROP_REASONS))
            others = sorted((k, name, n) for (k, name), n in self.frame_counts.items() if name not in FRAME_NAMES.values())
            rows += [f"frames {echo_checks.label_of(k)} {name} x{n}" for k, name, n in others]
        return "\n".join(rows)

    def check(self, args):
        if not args or args[0] not in CHECKS:
            raise HubError(f"check <{'|'.join(CHECKS)}> [instance]")
        target = self.instance_arg(args[1:2]) if len(args) > 1 else None
        results = echo_checks.run_check(self, CHECKS[args[0]], CHECKS, target)
        failed = sum(not r.passed for r in results)
        return "\n".join([echo_checks.format_result(r) for r in results] + [f"{len(results) - failed} passed, {failed} failed"])

    # The context the checks read (echo_checks.run_check).
    def tail(self, instance, source):
        return self.statuses[instance] if source == echo_checks.SOURCE_STATUS else self.tails[instance]

    def slots(self):
        with self.lock:
            return {k: slot for k in self.instances if (slot := self.views.slot_of(k)) is not None}

    def last_slot(self, instance):
        with self.lock:
            return self.views.last_slot(instance)

    def address(self, instance):
        return wire.virtual_address(steam_id(instance))

    def linked(self):
        with self.lock:
            return set(self.links)

    def counters(self):
        with self.lock:
            return dict(self.views.drops)


def console(hub):
    """Reads commands until quit or the end of input."""
    while not hub.stopped.is_set():
        try:
            line = input(PROMPT)
        except (EOFError, KeyboardInterrupt):
            hub.command("quit")
            return
        result = hub.command(line)
        if result:
            print(result, flush=True)


def instance_folders(game_dir, root, count):
    folders = {1: str(game_dir)}
    folders.update({k: str(Path(root) / echo_clones.GUEST_NAME.format(k)) for k in range(2, count + 1)})
    return folders


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--count", type=int, default=DEFAULT_COUNT, help="instances, the host included (1 to 4)")
    parser.add_argument("--max-players", type=int, default=DEFAULT_MAX_PLAYERS)
    parser.add_argument("--delay-ms", type=int, default=0)
    parser.add_argument("--launch", action="store_true")
    parser.add_argument("--game-dir", default=echo_clones.DEFAULT_GAME_DIR)
    parser.add_argument("--clones-root", default=echo_clones.DEFAULT_ROOT)
    parser.add_argument("--ini-override", action="append", default=[], metavar="guestN:key=value")
    parser.add_argument("--port", type=int, default=ADAPTER_PORT)
    parser.add_argument("--identify", choices=(IDENTIFY_PID, IDENTIFY_ORDER), default=IDENTIFY_PID)
    args = parser.parse_args()
    try:
        _clones, errors = echo_clones.sync(args.game_dir, args.clones_root, args.count, args.ini_override)
        for error in errors:
            print(f"warning: {error}")
        hub = Hub(instance_folders(args.game_dir, args.clones_root, args.count), args.max_players, args.delay_ms, args.port,
                  args.identify)
        hub.start()
    except (echo_clones.CloneError, HubError, ValueError) as error:
        print(f"FAIL: {error}")
        return 1
    print(f"hub on {LISTEN_HOST}:{hub.port} for {args.count} instances")
    if args.launch:
        echo_launch.launch_in_background(hub.folders, hub.hello_events, print)
    console(hub)
    return 0


if __name__ == "__main__":
    sys.exit(main())
