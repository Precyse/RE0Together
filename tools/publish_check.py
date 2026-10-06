"""Pre-push check for the public repo: nothing from a game, no dumps, no secrets.

usage: python tools/publish_check.py        (from the repo root; exit 1 lists every problem)

Scans every file git would publish (tracked plus staged). Rules come from the coop-re skill's safety notes:
never game files or dumps of game code or memory, never decompiled output, never keys or tokens.
"""
import re
import subprocess
import sys
from pathlib import Path

MAX_BYTES = 1_000_000
# Binaries we ship on purpose.
ALLOWED_BINARIES = {"launcher/native/steam_api64.dll"}
ALLOWED_PREFIXES = ("adapters/re0/third_party/",)
# Game-derived formats and captures: RE0 archives, memory/code dumps, crash dumps, logs, saves.
BLOCKED_SUFFIXES = {".arc", ".pak", ".dmp", ".bin", ".log", ".sav", ".exe", ".tex", ".mod", ".lmt"}
SECRET_PATTERNS = {
    "private key": re.compile(rb"-----BEGIN [A-Z ]*PRIVATE KEY-----"),
    "GitHub token": re.compile(rb"gh[pousr]_[A-Za-z0-9]{36}"),
    "AWS key": re.compile(rb"AKIA[0-9A-Z]{16}"),
    "API key assignment": re.compile(rb"(?i)(fal_key|api[_-]?key|secret|token)\s*[=:]\s*['\"]?[A-Za-z0-9_\-]{24,}"),
    "Steam Web API key": re.compile(rb"(?i)steam.{0,20}key.{0,5}[=:].{0,3}[0-9A-F]{32}"),
}
# Test-only adapter switches that must never ship enabled.
TEST_ONLY_INI = re.compile(rb"(?im)^\s*god_mode\s*=\s*1\b")
# Decompiler output pasted into source or notes.
# Split literals so this file does not match its own patterns.
DECOMPILER_MARKERS = re.compile(rb"undefined4 FUN" rb"_|/\* WARNING: Could not" rb" recover|// Decompiled" rb" with (ILSpy|JetBrains)")


def published_files():
    tracked = subprocess.run(["git", "ls-files"], capture_output=True, text=True, check=True).stdout.split()
    staged = subprocess.run(["git", "diff", "--cached", "--name-only", "--diff-filter=AM"], capture_output=True,
                            text=True, check=True).stdout.split()
    return sorted(set(tracked) | set(staged))


def allowed_binary(path):
    return path in ALLOWED_BINARIES or path.startswith(ALLOWED_PREFIXES)


def problems_in(path):
    file = Path(path)
    if not file.is_file():
        return []
    found = []
    if file.suffix.lower() in BLOCKED_SUFFIXES and not allowed_binary(path):
        found.append(f"{file.suffix} files are game data, dumps or logs")
    data = file.read_bytes()
    if len(data) > MAX_BYTES and not allowed_binary(path):
        found.append(f"{len(data)} bytes (over {MAX_BYTES})")
    for name, pattern in SECRET_PATTERNS.items():
        if pattern.search(data):
            found.append(f"looks like a {name}")
    if file.suffix.lower() == ".ini" and TEST_ONLY_INI.search(data):
        found.append("enables god_mode (test only)")
    if DECOMPILER_MARKERS.search(data):
        found.append("contains decompiler output")
    return found


def main():
    failures = 0
    for path in published_files():
        for problem in problems_in(path):
            print(f"{path}: {problem}")
            failures += 1
    print("publish check: clean" if failures == 0 else f"publish check: {failures} problem(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
