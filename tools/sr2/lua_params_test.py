"""Checks the parameter rule of the Lua overrides (tools/sr2/lua_params.py) on made-up scripts.

usage: python tools/sr2/lua_params_test.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import lua_params  # noqa: E402

failures = 0
WRAPPED = {"teleport", "get_dist", "character_is_in_vehicle"}


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def rewritten(text):
    return lua_params.rewrite(text, WRAPPED)


def test_rewrite():
    text, count = rewritten('function cb(who)\n\tteleport(who, "nav")\nend\n')
    check(count == 1 and 'coop_native(teleport)(who, "nav")' in text, "a parameter in the arguments: the call is made on the game's function")
    text, count = rewritten('function cb(who)\n\tteleport(REMOTE_PLAYER, "nav")\nend\n')
    check(count == 0 and 'teleport(REMOTE_PLAYER, "nav")' in text, "the literal REMOTE_PLAYER keeps the widening call")
    text, count = rewritten("function cb(who)\n\tteleport(who, REMOTE_PLAYER)\nend\n")
    check(count == 0, "a parameter beside the literal keeps the widening call")
    text, count = rewritten('teleport(who, "nav")\nlocal function late(x) end\n')
    check(count == 0, "outside any function nothing is rewritten")
    text, count = rewritten('function cb(who)\n\tteleport(other, "nav")\nend\n')
    check(count == 0, "a name that is not a parameter is kept")
    text, count = rewritten('function outer(who)\n\tlocal inner = function(a)\n\t\tteleport(who, a)\n\tend\nend\n')
    check(count == 1 and "coop_native(teleport)(who, a)" in text, "a parameter of an outer function used in a nested one")
    text, count = rewritten('function outer(who)\n\tif x then\n\t\tfor i = 1, 2 do\n\t\t\twhile y do\n\t\t\t\tteleport(who)\n\t\t\tend\n\t\tend\n\tend\nend\n')
    check(count == 1, "blocks (if, for, while) do not end the function")
    text, count = rewritten("function first(a)\nend\nfunction second()\n\tteleport(a)\nend\n")
    check(count == 0, "a parameter of a function that has ended is not in scope")
    text, count = rewritten("function cb(who)\n\trepeat\n\t\tx = 1\n\tuntil x\n\tteleport(who)\nend\n")
    check(count == 1, "repeat until does not end the function")


def test_skipped_text():
    text, count = rewritten('function cb(who)\n\t-- teleport(who)\n\tx = "teleport(who)"\n\ty = \'teleport(who)\'\nend\n')
    check(count == 0 and text.count("coop_native") == 0, "calls in comments and strings are kept")
    text, count = rewritten("function cb(who)\n\t--[[ teleport(who)\n\tteleport(who) ]]\n\tz = [[teleport(who)]]\nend\n")
    check(count == 0, "calls in long comments and long strings are kept")
    text, count = rewritten('function cb(who)\n\tx = "it\\"s a teleport" teleport(who)\nend\n')
    check(count == 1, "an escaped quote does not end a string early")
    text, count = rewritten("function cb(who)\n\tCOOP_NATIVE.teleport(who)\n\tobj:teleport(who)\nend\n")
    check(count == 0, "a field call or method call is kept")
    text, count = rewritten("function cb(who)\n\tcoop_native(teleport)(who)\n\tcoop_any_remote(character_is_in_vehicle, who)\nend\n")
    check(count == 0 and text.count("coop_native(teleport)") == 1, "a call already on the game's function, or a function as a value, is kept")


def test_risky_calls():
    text = (
        "function helper(who)\n\tteleport(who)\nend\n"
        "function other(x)\n\tlocal a = x\nend\n"
        "helper(REMOTE_PLAYER)\n"
        "local p = REMOTE_PLAYER\n"
        "helper(p)\n"
        "helper(LOCAL_PLAYER)\n"
        "other(REMOTE_PLAYER)\n"
    )
    check(lua_params.risky_calls(text, WRAPPED) == [(7, "helper", "REMOTE_PLAYER"), (9, "helper", "p")],
          "risky calls: the literal and a variable assigned the literal passed for a rewritten parameter")
    check(lua_params.risky_calls("function helper(who)\n\tteleport(REMOTE_PLAYER)\nend\nhelper(REMOTE_PLAYER)\n", WRAPPED) == [],
          "risky calls: a helper whose parameter is not rewritten is fine")


def test_names():
    names = lua_params.wrapped_names('COOP_RULES = {\n\tfanout = { "teleport", "attack" },\n\tall = { "character_is_dead" },\n}\n')
    check(names == {"teleport", "attack", "character_is_dead"}, "wrapped names are the quoted names of coop_rules.lua")


def main():
    for test in (test_rewrite, test_skipped_text, test_risky_calls, test_names):
        test()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
