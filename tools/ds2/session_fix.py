"""Makes the coop session folder's Continue load a known save (a DHV state) instead of the newest autosaves.

Every save has its save time as a FILETIME in its description chunk (chunk 0, uncompressed bytes inside the packed
stream) and profile.dat holds the newest save's time in its chunk 0 (docs/DS2_NOTES.md "Save container"). This tool
  1. copies the whole session saves folder to G:/coop-scratch/ds2/session-backup-<time>,
  2. builds the prepared folder: the chosen save becomes autosave<SLOT>.dat with its save time set to now, every other
     autosave is left out, and profile.dat gets the same time (equal-length edits, then re-encrypted),
  3. with --apply (the game must not run) replaces the session folder's files with the prepared ones.
usage: python session_fix.py SOURCE.dat [--apply]      (SOURCE: a save copy, e.g. G:/coop-scratch/ds2/savefmt/autosave4.dat)
Only ever touches coop/session (the adapter's redirect target), never the real Documents saves."""
import argparse
import datetime
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

import savefmt

GAME_DIR = Path(r"G:\SteamLibrary\steamapps\common\DEATH STRANDING 2 - ON THE BEACH")
SAVES = GAME_DIR / "coop" / "session" / "Documents" / "DEATH STRANDING 2 - ON THE BEACH" / "76561198071592961"
SCRATCH = Path(r"G:\coop-scratch\ds2")
SLOT = 4
FILETIME_EPOCH = datetime.datetime(1601, 1, 1, tzinfo=datetime.timezone.utc)
FILETIME_TICKS_PER_S = 10_000_000
FILETIME_MIN, FILETIME_MAX = 0x01DC000000000000, 0x01DF000000000000
PROFILE_TIME_OFFSET = 74  # in the decrypted chunk 0 of profile.dat
DESCRIPTION_CHUNK = 0
GAME_PROCESS = "DS2.exe"


def filetime_now():
    return int((datetime.datetime.now(datetime.timezone.utc) - FILETIME_EPOCH).total_seconds() * FILETIME_TICKS_PER_S)


def find_save_time(chunk):
    """The offset of the save-time FILETIME in a save's description chunk."""
    hits = [i for i in range(len(chunk) - 8) if FILETIME_MIN < struct.unpack_from("<Q", chunk, i)[0] < FILETIME_MAX]
    if len(hits) != 1:
        raise SystemExit(f"expected one FILETIME in the description chunk, found {len(hits)}")
    return hits[0]


def rewrite_time(save, offset_of, stamp):
    """The save with its time (in chunk 0) replaced; same length, so the chunk table stays valid."""
    index, sizes, chunks = savefmt.segments(save)
    chunk = bytearray(chunks[DESCRIPTION_CHUNK])
    chunk[offset_of(chunk):offset_of(chunk) + 8] = struct.pack("<Q", stamp)
    chunks[DESCRIPTION_CHUNK] = bytes(chunk)
    return savefmt.assemble(save[:savefmt.HEADER_BYTES], index, sizes, chunks)


def build(source, out):
    stamp = filetime_now()
    out.mkdir(parents=True)
    for keep in SAVES.glob("*.dat"):
        if not keep.name.startswith("autosave") and keep.name != "profile.dat":
            shutil.copy2(keep, out / keep.name)
    shutil.copy2(SAVES / "bindings.cfg", out / "bindings.cfg") if (SAVES / "bindings.cfg").exists() else None
    (out / f"autosave{SLOT}.dat").write_bytes(rewrite_time(source.read_bytes(), find_save_time, stamp))
    profile = (SAVES / "profile.dat").read_bytes()
    (out / "profile.dat").write_bytes(rewrite_time(profile, lambda _: PROFILE_TIME_OFFSET, stamp))
    now = time.time()
    for path in out.glob("*.dat"):
        if path.name.startswith("autosave") or path.name == "profile.dat":
            os.utime(path, (now, now))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source", type=Path)
    ap.add_argument("--apply", action="store_true")
    args = ap.parse_args()
    stamp = time.strftime("%H%M%S")
    backup = SCRATCH / f"session-backup-{stamp}"
    shutil.copytree(GAME_DIR / "coop" / "session", backup)
    prepared = SCRATCH / f"session-prep-{stamp}"
    build(args.source, prepared)
    print("backup:", backup, "\nprepared:", prepared)
    if not args.apply:
        return
    if GAME_PROCESS.lower() in subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower():
        raise SystemExit("the game is running: not applying")
    for path in SAVES.glob("*.dat"):
        path.unlink()
    for path in prepared.iterdir():
        shutil.copy2(path, SAVES / path.name)
    print("applied")


if __name__ == "__main__":
    main()
