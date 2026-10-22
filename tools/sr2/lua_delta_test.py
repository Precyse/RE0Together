"""Checks tools/sr2/lua_delta.py and the committed adapters/sr2/scripts.delta.json.

usage: python tools/sr2/lua_delta_test.py

Always: made-up scripts (CRLF, latin-1 bytes, a replaced line, an appended library) round trip through the ops, and a source whose
hash or size differs is refused. With SR2_LUA_SOURCE (the extracted scripts) and SR2_LUA_FIXES (Juiced's TU3 Mission Fixes) set, the
committed delta applied to those files must equal lua_overrides.py's output byte for byte, all 50 scripts.
"""
import json
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import lua_delta  # noqa: E402
import lua_overrides  # noqa: E402

DELTA_FILE = Path(__file__).resolve().parents[2] / "adapters" / "sr2" / "scripts.delta.json"
REAL_SCRIPT_COUNT = 50
failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def raises(function, *arguments):
    try:
        function(*arguments)
    except ValueError:
        return True
    return False


def round_trip(source, target):
    record = lua_delta.file_record("t.lua", source, target)
    return lua_delta.rebuild(record, source) == target, record


def test_synthetic():
    source = "-- caf\xe9 \x93quoted\x94\r\nlocal a = 1\r\nif x == REMOTE_PLAYER then\r\n\tf(REMOTE_PLAYER, 2)\r\nend\r\nreturn a\r\n"
    target = source.replace("f(REMOTE_PLAYER, 2)", "coop_each_remote(f, REMOTE_PLAYER, 2)") + "-- library\r\nfunction lib() end\r\n"
    same, record = round_trip(source, target)
    check(same, "CRLF and latin-1 bytes round trip")
    check(any(op[0] == "c" for op in record["ops"]) and lua_delta.insert_bytes({"files": [record]}) < len(target), "unchanged text is copied, not inserted")
    check(round_trip(source, source)[0] and round_trip("", "new")[0] and round_trip(source, "")[0], "identical, empty source and empty target round trip")
    long_line = "x = " + "y + " * 40 + "z\n"
    check(round_trip(long_line + "tail\n", long_line.replace("y +", "w +", 1) + "tail\n")[0], "a changed long line round trips")
    inserted = lua_delta.insert_bytes({"files": [record]})
    check(inserted < 80, f"a replaced line inserts our edit and not the whole line ({inserted} bytes)")
    check(raises(lua_delta.rebuild, record, source.replace("local a", "local b")), "a source with another hash is refused")
    check(raises(lua_delta.rebuild, record, source + "x"), "a source with another size is refused")
    broken = dict(record, ops=[["c", 0, len(source) + 1]])
    check(raises(lua_delta.rebuild, broken, source), "a copy past the end of the source is refused")
    wrong_output = dict(record, sha256="0" * 64)
    check(raises(lua_delta.rebuild, wrong_output, source), "a result that is not the pinned script is refused")


def test_build_and_format():
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        for name in ("sources", "fixes", "generated"):
            (root / name).mkdir()
        (root / "sources" / "a.lua").write_bytes(b"one\r\ntwo\r\nthree\r\n")
        (root / "fixes" / "em01.lua").write_bytes(b"fixed\nscript\n")
        (root / "generated" / "a.lua").write_bytes(b"one\r\n2\r\nthree\r\n")
        (root / "generated" / "em01.lua").write_bytes(b"fixed\nscript\nmore\n")
        delta = lua_delta.build(root / "sources", root / "fixes", root / "generated")
        check([r["name"] for r in delta["files"]] == ["a.lua", "em01.lua"], "one record per generated script, sorted")
        check([r["source"] for r in delta["files"]] == ["patch.vpp_pc:a.lua", "Mods/JUICED - TU3 Mission Fixes/em01.lua"],
              "a script names its source: a pack entry, or a Juiced file for em01 and rn07")
        parsed = json.loads(lua_delta.to_json(delta))
        check(parsed == delta and lua_delta.to_json(delta) == lua_delta.to_json(parsed), "the file parses back and is written the same way again")


def test_committed_delta():
    delta = json.loads(DELTA_FILE.read_text(encoding="ascii"))
    names = [record["name"] for record in delta["files"]]
    check(delta["format"] == lua_delta.FORMAT and len(names) == REAL_SCRIPT_COUNT, f"the committed delta holds {REAL_SCRIPT_COUNT} scripts")
    check(names == sorted(names) and names == sorted(lua_overrides.PATCHES), "its scripts are the ones lua_overrides.py patches")
    source, fixes = os.environ.get("SR2_LUA_SOURCE"), os.environ.get("SR2_LUA_FIXES")
    if not (source and fixes):
        print("skip real scripts (set SR2_LUA_SOURCE and SR2_LUA_FIXES)")
        return
    with tempfile.TemporaryDirectory() as folder:
        lua_overrides.build(Path(source), Path(fixes), Path(folder))
        for record in delta["files"]:
            name = record["name"]
            original = lua_delta.read_latin1((Path(fixes) if name in lua_overrides.FIXED_FILES else Path(source)) / name)
            built = lua_delta.rebuild(record, original)
            check(built == lua_delta.read_latin1(Path(folder) / name), f"{name}: the committed delta applied to the sources equals lua_overrides.py's output")


test_synthetic()
test_build_and_format()
test_committed_delta()
print("lua_delta_test: " + ("all checks passed" if failures == 0 else f"{failures} failed"))
sys.exit(1 if failures else 0)
