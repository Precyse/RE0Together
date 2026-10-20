"""The files the launcher and the SR2 adapter share, on the hub's side (launcher/NativeJoin.cs): each instance's
coop\\sr2_join.txt is written atomically and removed again, and its coop\\sr2_status.txt is polled."""
import os

JOIN_FILE = "sr2_join.txt"
STATUS_FILE = "sr2_status.txt"
COOP_DIR = "coop"
LOG_FILE = "sr2_adapter.log"
TOKEN = "76561198000000000"
PASSWORD = "123456789"
STATUS_POLL_S = 2.0
HOST_ROLE = "host"
GUEST_ROLE = "guest"


def coop_path(folder, name):
    return os.path.join(folder, COOP_DIR, name)


def join_text(role, name):
    """Steam mode ignores the token and the password, but join_file::parse requires both."""
    return f"role={role}\ntoken={TOKEN}\npassword={PASSWORD}\nname={name}\n"


class JoinFiles:
    """Writes and removes the join file of each instance. A write always replaces the file, so the adapter's watcher sees a
    new write time even when the content is the same (the launcher's Join pressed again)."""

    def __init__(self, folders):
        self.folders = folders
        self.written = set()

    def write(self, instance, name):
        role = HOST_ROLE if instance == 1 else GUEST_ROLE
        path = coop_path(self.folders[instance], JOIN_FILE)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        temp = path + ".tmp"
        with open(temp, "w", newline="\n") as out:
            out.write(join_text(role, name))
        os.replace(temp, path)
        self.written.add(instance)

    def remove(self, instance):
        try:
            os.remove(coop_path(self.folders[instance], JOIN_FILE))
        except FileNotFoundError:
            pass
        self.written.discard(instance)

    def remove_all(self):
        for instance in sorted(self.written):
            self.remove(instance)


def clear_status(folder):
    """The launcher removes the status a session left behind when it starts a new one."""
    try:
        os.remove(coop_path(folder, STATUS_FILE))
    except FileNotFoundError:
        pass


def read_status(folder):
    """The instance's current status line, or '' when there is none."""
    try:
        with open(coop_path(folder, STATUS_FILE), encoding="utf-8", errors="replace") as source:
            return source.read().strip()
    except OSError:
        return ""


class StatusHistory:
    """The status lines of one instance since the last mark; a mark keeps the current line as the first, because a status
    that has not changed since is still true."""

    def __init__(self):
        self.current = ""
        self.history = []

    def record(self, line):
        """True when the line is new."""
        if not line or line == self.current:
            return False
        self.current = line
        self.history.append(line)
        return True

    def mark(self):
        self.history = [self.current] if self.current else []

    def lines(self):
        return list(self.history)
