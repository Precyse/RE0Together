"""Test for tools/release_inputs.py: fingerprints on a throwaway git repo, and that the inputs listed for each adapter cover what its
CMakeLists.txt compiles from other folders (the real files; no network, no build).
Usage: python tools/release_inputs_test.py   (exit 1 on a failure)"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import release_inputs as ri

REPO_ROOT = Path(__file__).resolve().parent.parent
TABLE = {
    "app": ri.Inputs(("app",), ("app/docs",)),
    "mod": ri.Inputs(("mod", "shared/src")),
}
FILES = {
    "app/main.cs": "main", "app/docs/notes.md": "notes", "mod/dll.cpp": "dll", "shared/src/net.cpp": "net", "shared/other/x.cpp": "x",
    "tools/release_inputs.py": "inputs", "tools/release_manifest.py": "manifest", ".github/workflows/release.yml": "workflow",
}
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)
        print(f"FAIL: {what}")


def git(root, *args):
    return subprocess.run(["git", "-C", str(root), "-c", "user.name=t", "-c", "user.email=t@t", *args],
                          capture_output=True, text=True, check=True).stdout.strip()


def commit(root, changes):
    for relative, text in changes.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    git(root, "add", "-A")
    git(root, "commit", "-m", "change", "--allow-empty")
    return git(root, "rev-parse", "HEAD")


def prints(root, sha):
    return ri.fingerprints(root, sha, TABLE)


def test_fingerprints(root):
    base = commit(root, FILES)
    first = prints(root, base)
    check(first["app"] != first["mod"] and all(len(value) == 64 for value in first.values()), "each component has its own sha-256 fingerprint")
    check(prints(root, commit(root, {"unrelated/file.txt": "x", "shared/other/x.cpp": "changed", "app/docs/notes.md": "edited"})) == first,
          "files outside a component's inputs, and the excluded ones, do not move it")
    app_changed = prints(root, commit(root, {"app/main.cs": "main v2"}))
    check(app_changed["app"] != first["app"] and app_changed["mod"] == first["mod"], "a changed input moves only its component")
    shared_changed = prints(root, commit(root, {"shared/src/net.cpp": "net v2"}))
    check(shared_changed["mod"] != app_changed["mod"] and shared_changed["app"] == app_changed["app"], "a shared source moves the component that compiles it")
    packaging = prints(root, commit(root, {"tools/release_manifest.py": "manifest v2"}))
    check(all(packaging[name] != shared_changed[name] for name in TABLE), "the packaging files move every component")
    check(prints(root, commit(root, {})) == packaging, "a commit that changes nothing keeps every fingerprint")
    check(prints(root, git(root, "rev-parse", "HEAD~1")) == packaging, "the same tree gives the same fingerprint")


def test_missing_inputs(root):
    sha = git(root, "rev-parse", "HEAD")
    try:
        ri.fingerprints(root, sha, {"x": ri.Inputs(("nothing-here",))})
        check(False, "refused: an input path with no files")
    except ri.InputsError:
        pass
    git(root, "rm", "-q", "tools/release_inputs.py")
    gone = commit(root, {})
    try:
        ri.fingerprints(root, gone, TABLE)
        check(False, "refused: a packaging file missing from the commit")
    except ri.InputsError:
        pass


def cmake_folders(game):
    """The sibling adapter folders a game's CMakeLists.txt reaches into (../re0, ../ds2)."""
    text = (REPO_ROOT / "adapters" / game / "CMakeLists.txt").read_text(encoding="utf-8")
    return sorted(set(re.findall(r"/\.\./(\w+)", text)) - {"tools"})


def test_cmake_inputs_are_listed():
    for game in ("re0", "sr2", "ds2"):
        listed = ri.COMPONENT_INPUTS[game].include
        for folder in cmake_folders(game):
            check(any(path.startswith(f"adapters/{folder}/") for path in listed), f"{game}: the sources it compiles from adapters/{folder} are inputs")
        check(f"adapters/{game}" in listed and f"launcher/games/{game}.json" in listed, f"{game}: its folder and its profile are inputs")
    check(set(ri.COMPONENT_INPUTS) == {ri.LAUNCHER_ID, *(path.stem for path in (REPO_ROOT / "launcher/games").glob("*.json"))},
          "every game profile has inputs, and the launcher too")


def test_real_repo_resolves():
    head = git(REPO_ROOT, "rev-parse", "HEAD")
    found = ri.fingerprints(REPO_ROOT, head)
    check(set(found) == set(ri.COMPONENT_INPUTS) and len(set(found.values())) == len(found), "the real inputs all exist at HEAD and differ per component")


def main():
    with tempfile.TemporaryDirectory(prefix="cf_release_inputs_") as directory:
        root = Path(directory)
        git(root, "init", "-q")
        test_fingerprints(root)
        test_missing_inputs(root)
    test_cmake_inputs_are_listed()
    test_real_repo_resolves()
    print("release_inputs_test: " + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
