"""Which repo files each release component is built from, and the fingerprint of them at a commit.

usage: python tools/release_inputs.py COMMIT OUT.json        (from the repo root)

A component whose fingerprint equals the one in the previous release's manifest.json is not rebuilt in the release: tools/release_manifest.py
reuses the previous zip unchanged (same bytes, same version), so a launcher only downloads what really changed. The fingerprint is the SHA-256
over the git blob hash of every tracked file of the component's inputs at COMMIT (git ls-tree), so it moves exactly when one of those files does.

Inputs per component, read from the adapters' CMakeLists.txt and the launcher project:
  launcher  launcher/** without launcher/games (the profiles belong to the mods) and launcher/package (README.txt ships only in Coop.zip)
  re0       adapters/re0, the DS2 authority sources it compiles (adapters/ds2/src), launcher/games/re0.json
  sr2       adapters/sr2, the RE0 net/log/hook sources, MinHook and dinput8.def it compiles, launcher/games/sr2.json
  ds2       adapters/ds2, the RE0 sources and third_party (MinHook, ImGui) it compiles, launcher/games/ds2.json
Tests, notes and research tools of an adapter are no input: they never reach the DLL. Every component also depends on the packaging
(this file, release_manifest.py and the workflow), since those decide what a zip holds and how the adapters are built.
"""
import hashlib
import json
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

LAUNCHER_ID = "launcher"
PACKAGING = ("tools/release_inputs.py", "tools/release_manifest.py", ".github/workflows/release.yml")
RE0_SOURCES = ("adapters/re0/src", "adapters/re0/third_party", "adapters/re0/dinput8.def")


@dataclass(frozen=True)
class Inputs:
    """Tracked paths (files or folders) a component is built from, minus the paths inside them that it is not."""
    include: tuple
    exclude: tuple = field(default=())


def adapter_inputs(game, shared, ignored=("tests", "tools", "README.md", "test.cmd", "rebuild.cmd")):
    own_ignored = tuple(f"adapters/{game}/{name}" for name in ignored)
    return Inputs((f"adapters/{game}", f"launcher/games/{game}.json", *shared), own_ignored)


COMPONENT_INPUTS = {
    LAUNCHER_ID: Inputs(("launcher",), ("launcher/games", "launcher/package")),
    "re0": adapter_inputs("re0", ("adapters/ds2/src",)),
    "sr2": adapter_inputs("sr2", ("adapters/re0/src", "adapters/re0/third_party/minhook", "adapters/re0/dinput8.def")),
    "ds2": adapter_inputs("ds2", ("adapters/re0/src", "adapters/re0/third_party")),
}


class InputsError(Exception):
    """An input path of a component is not in the commit (a typo or a moved folder would otherwise leave it out of the fingerprint)."""


def _covers(prefix, path):
    return path == prefix or path.startswith(prefix + "/")


def tracked_files(root, commit, paths):
    """{path: blob hash} of the files below paths at commit."""
    listing = subprocess.run(["git", "-C", str(root), "ls-tree", "-r", "-z", "--full-tree", commit, "--", *paths],
                             capture_output=True, text=True, check=True).stdout
    files = {}
    for entry in filter(None, listing.split("\0")):
        meta, path = entry.split("\t", 1)
        files[path] = meta.split()[2]
    return files


def fingerprint(root, commit, inputs):
    """SHA-256 hex over the sorted (path, blob hash) of the inputs plus the packaging files."""
    packaging = tracked_files(root, commit, PACKAGING)
    missing = [path for path in PACKAGING if path not in packaging]
    if missing:
        raise InputsError("packaging file not in " + commit[:12] + ": " + ", ".join(missing))
    selected = {}
    for prefix in inputs.include:
        found = {path: blob for path, blob in tracked_files(root, commit, [prefix]).items()
                 if not any(_covers(skip, path) for skip in inputs.exclude)}
        if not found:
            raise InputsError(f"input {prefix} has no files in {commit[:12]}")
        selected.update(found)
    digest = hashlib.sha256()
    for path, blob in sorted({**selected, **packaging}.items()):
        digest.update(f"{path}\0{blob}\n".encode("utf-8"))
    return digest.hexdigest()


def fingerprints(root, commit, table=COMPONENT_INPUTS):
    """{component id: fingerprint} for every component of the table."""
    return {component: fingerprint(root, commit, inputs) for component, inputs in table.items()}


def main(argv):
    if len(argv) != 2:
        print(__doc__.split("\n")[2], file=sys.stderr)
        return 2
    try:
        found = fingerprints(Path("."), argv[0])
    except (InputsError, subprocess.CalledProcessError) as error:
        print(f"release_inputs: {error}", file=sys.stderr)
        return 1
    Path(argv[1]).write_text(json.dumps(found, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
