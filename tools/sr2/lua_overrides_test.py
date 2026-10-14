"""Checks tools/sr2/lua_overrides.py against made-up scripts (no game files): usage `python tools/sr2/lua_overrides_test.py`."""
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import lua_overrides  # noqa: E402

failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def made_up_source(directory, line_ending):
    """A script per edited file that holds every anchor the expected number of times."""
    for name, edits in lua_overrides.EDITS.items():
        text = "-- made up\n" + "".join(old * count for old, _, count in edits) + "\nend"
        (directory / name).write_bytes(text.replace("\n", line_ending).encode("latin-1"))


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    for label, ending in (("LF", "\n"), ("CRLF", "\r\n")):
        source = root / ("source_" + label)
        out = root / ("out_" + label)
        source.mkdir()
        made_up_source(source, ending)
        written = lua_overrides.build(source, out)
        check(written == sorted(lua_overrides.EDITS), f"{label}: every edited script is written")
        ug_lib = (out / "ug_lib.lua").read_bytes().decode("latin-1")
        check("function coop_remote_players()" in ug_lib and "function coop_is_remote_player(name)" in ug_lib,
              f"{label}: ug_lib.lua gets the helpers")
        check(("\r\n" in ug_lib) == (ending == "\r\n") and (ending == "\n" or "\n" not in ug_lib.replace("\r\n", "")),
              f"{label}: line endings kept")
        airport = (out / "sh_bh_airport.lua").read_bytes().decode("latin-1")
        check("REMOTE_PLAYER" not in airport and "coop_is_remote_player(char)" in airport, f"{label}: the airport tests every remote")

    broken = root / "broken"
    broken.mkdir()
    made_up_source(broken, "\n")
    (broken / "sh_tss_caverns.lua").write_bytes(b"-- nothing to edit here\n")
    out = root / "out_broken"
    try:
        lua_overrides.build(broken, out)
        raised = False
    except ValueError:
        raised = True
    check(raised and not out.exists(), "a script whose text differs stops the build before anything is written")

sys.exit(1 if failures else 0)
