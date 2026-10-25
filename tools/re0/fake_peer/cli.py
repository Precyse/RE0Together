"""The command line: list, run, gates, library build, selftest, golden."""
import argparse
import sys

from fake_peer import golden, library, selftest
from fake_peer.impair import load_profiles
from fake_peer.launchers import Ports
from fake_peer.runner import SEATS, TRANSPORTS, Options, output_root, run_scenario
from fake_peer.scenarios import ORDER, REGISTRY
from fake_peer.wire import CHARACTER_NAMES


def build_parser():
    parser = argparse.ArgumentParser(prog="fake_peer", description="One-PC fake peer for RE0 state mode.")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list", help="scenarios with their test plan rows and seats")
    run = commands.add_parser("run", help="run a scenario against the game")
    run.add_argument("scenario", choices=sorted(REGISTRY))
    run.add_argument("--seat", choices=SEATS, default="guest", help="the seat the fake plays (the game plays the other)")
    run.add_argument("--transport", choices=TRANSPORTS)
    run.add_argument("--bridge-port", type=int, help="bridge transport: the launcher bridge to join")
    run.add_argument("--game-dir")
    run.add_argument("--launcher")
    run.add_argument("--save-source", help="launchers: the host launcher's save folder when Steam does not give one")
    run.add_argument("--out", help="folder for the run folders and the library")
    run.add_argument("--scratch", action="store_true", help="the save may be changed (flags, items, pushes, deaths)")
    run.add_argument("--fake-character", choices=CHARACTER_NAMES)
    run.add_argument("--delay", type=float, default=1.0, help="echo delay in seconds")
    run.add_argument("--offset-x", type=float, default=150.0, help="echo offset in game units")
    run.add_argument("--link", choices=sorted(load_profiles()), help="link profile for both directions (impair.py, profiles.json)")
    run.add_argument("--seed", type=int, help="seed of the link model; a run without one picks a seed and prints it")
    run.add_argument("--delay-ms", type=float, help="one-way delay on top of the profile")
    run.add_argument("--jitter-ms", type=float)
    run.add_argument("--loss", type=float)
    run.add_argument("--seconds", type=float, help="length of the scenario's timed phases")
    run.add_argument("--round", choices=("door", "vanish"), help="t11_boss as the host: which round to play")
    run.add_argument("--dev-commands", action="store_true", help="the game runs with adapter.ini dev_commands=1: steps may write coop\\cmd.txt")
    gates = commands.add_parser("gates", help="print the gate table of a scenario")
    gates.add_argument("scenario", choices=sorted(REGISTRY))
    gates.add_argument("--seat", choices=SEATS, default="guest")
    lib = commands.add_parser("library", help="index the recordings of every run")
    lib.add_argument("action", choices=("build",))
    lib.add_argument("--out")
    lib.add_argument("--game-dir")
    test = commands.add_parser("selftest", help="wire check, golden frames, anchors, unit tests")
    test.add_argument("--smoke", action="store_true", help="also run two fakes against each other through real launchers")
    commands.add_parser("golden", help="write tests/golden_frames.bin")
    return parser


def list_scenarios():
    for module in ORDER:
        scratch = f" scratch on {'/'.join(module.SCRATCH_SEATS)}" if module.SCRATCH_SEATS else ""
        print(f"{module.ID:16} seats {'/'.join(module.SEATS)}{scratch}   {module.ROWS}")
        print(f"{'':16} {module.SUMMARY}")


def print_gates(scenario, seat):
    if seat not in scenario.SEATS:
        print(f"{scenario.ID} runs as {' or '.join(scenario.SEATS)}", file=sys.stderr)
        return 2
    for gate in scenario.gates(seat):
        print("\n".join(gate.table()))
    return 0


def build_library(args):
    root = output_root(Options(out=args.out, game_dir=args.game_dir))
    if root is None:
        print("no game folder: pass --game-dir or --out", file=sys.stderr)
        return 2
    print(library.build(root).summary())
    return 0


def options_from(args):
    return Options(scenario=args.scenario, seat=args.seat, transport=args.transport, bridge_port=args.bridge_port,
                   game_dir=args.game_dir, launcher=args.launcher, save_source=args.save_source, out=args.out, scratch=args.scratch,
                   fake_character=args.fake_character, delay=args.delay, offset_x=args.offset_x, delay_ms=args.delay_ms,
                   jitter_ms=args.jitter_ms, loss=args.loss, link=args.link, seed=args.seed, seconds=args.seconds,
                   round=args.round, dev_commands=args.dev_commands, ports=Ports())


def main(argv=None):
    args = build_parser().parse_args(argv)
    if args.command == "list":
        list_scenarios()
        return 0
    if args.command == "run":
        return run_scenario(REGISTRY[args.scenario], options_from(args))
    if args.command == "gates":
        return print_gates(REGISTRY[args.scenario], args.seat)
    if args.command == "library":
        return build_library(args)
    if args.command == "golden":
        print(f"{golden.write()} golden frames written to {golden.GOLDEN_PATH}")
        return 0
    return selftest.run(smoke=args.smoke)
