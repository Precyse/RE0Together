"""Live tails of the adapter log and the launchers' stdout files, the crash dump watch, and `wait`."""
import re
import threading
import time
from datetime import datetime
from pathlib import Path

POLL_S = 0.1
STAMP = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) (.*)$")
STAMP_FORMAT = "%Y-%m-%d %H:%M:%S.%f"
SOURCE_ADAPTER, SOURCE_HOST, SOURCE_JOIN, SOURCE_TOOL = "adapter", "host_launcher", "join_launcher", "tool"
DUMP_PATTERN = "crash-*.dmp"


class LogLine:
    __slots__ = ("t", "source", "text")

    def __init__(self, t, source, text):
        self.t, self.source, self.text = t, source, text

    def __repr__(self):
        return f"{self.source}: {self.text}"


def parse_stamp(line):
    """(epoch ms, text) of an adapter log line, or (None, line) when it carries no stamp."""
    found = STAMP.match(line)
    if not found:
        return None, line
    return datetime.strptime(found.group(1), STAMP_FORMAT).timestamp() * 1000.0, found.group(2)


class LogStore:
    """Every line seen so far, from all sources, in arrival order."""

    def __init__(self):
        self.lines = []
        self.listeners = []  # called with each new LogLine
        self._changed = threading.Condition()

    def add(self, source, text, t=None):
        line = LogLine(time.time() * 1000.0 if t is None else t, source, text)
        with self._changed:
            self.lines.append(line)
            self._changed.notify_all()
        for listener in self.listeners:
            listener(line)

    def search(self, pattern, source=None, since=0.0, until=None):
        regex = re.compile(pattern) if isinstance(pattern, str) else pattern
        with self._changed:
            snapshot = list(self.lines)
        hits = []
        for line in snapshot:
            if (source is None or line.source == source) and line.t >= since and (until is None or line.t <= until):
                found = regex.search(line.text)
                if found:
                    hits.append((line, found))
        return hits

    def wait(self, pattern, timeout, source=None, since=0.0):
        """The first (line, match) at or after `since`, waiting up to `timeout` seconds; None when it never came."""
        deadline = time.monotonic() + timeout
        while True:
            hits = self.search(pattern, source, since)
            if hits:
                return hits[0]
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            with self._changed:
                self._changed.wait(min(remaining, POLL_S))


class FileTail:
    """Reads a growing text file into the store. `from_end` skips what is already there (the adapter log of an earlier run);
    a file that shrinks is a restarted game and is read again from its start."""

    def __init__(self, path, source, store, stamped=False, from_end=False):
        self.path, self.source, self.store, self.stamped = Path(path), source, store, stamped
        self._offset = self._size() if from_end else 0
        self._partial = b""
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def _size(self):
        try:
            return self.path.stat().st_size
        except OSError:
            return 0

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()

    def poll(self):
        """Reads whatever was appended since the last call."""
        size = self._size()
        if size < self._offset:
            self.store.add(SOURCE_TOOL, f"{self.source} log restarted")
            self._offset, self._partial = 0, b""
        if size == self._offset:
            return
        try:
            with open(self.path, "rb") as handle:
                handle.seek(self._offset)
                data = handle.read(size - self._offset)
        except OSError:
            return
        self._offset += len(data)
        *complete, self._partial = (self._partial + data).split(b"\n")
        for raw in complete:
            self._add_line(raw.decode("utf-8", "replace").rstrip("\r"))

    def _add_line(self, line):
        if not line:
            return
        t, text = parse_stamp(line) if self.stamped else (None, line)
        self.store.add(self.source, text, t)

    def _run(self):
        while not self._stop.is_set():
            self.poll()
            time.sleep(POLL_S)


class DumpWatch:
    """New crash dumps next to the adapter log since the run started."""

    def __init__(self, directory):
        self.directory = Path(directory)
        self._before = self._current()

    def _current(self):
        return set(self.directory.glob(DUMP_PATTERN)) if self.directory.exists() else set()

    def new(self):
        return sorted(self._current() - self._before)
