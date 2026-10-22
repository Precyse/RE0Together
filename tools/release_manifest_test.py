"""Test for tools/release_manifest.py on a synthetic package in a temp folder (no network, no build).
Usage: python tools/release_manifest_test.py   (exit 1 on a failure)"""
import hashlib
import json
import sys
import tempfile
import zipfile
from pathlib import Path

import release_manifest as rm

FILES = {
    "launcher/app/coop-launcher.exe": b"exe bytes",
    "launcher/app/steam_api64.dll": b"steam",
    "launcher/games/re0.json": b'{"id": "re0"}',
    "launcher/games/sr2.json": b'{"id": "sr2"}',
    "adapters/re0/dinput8.dll": b"re0 dll",
    "adapters/sr2/dinput8.dll": b"sr2 dll",
    "adapters/sr2/scripts.delta.json": b"{}",
    "README.txt": b"readme",
}
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)
        print(f"FAIL: {what}")


def make_package(root, files=FILES):
    for relative, data in files.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return root


def release(work, name="out", build=7, files=FILES):
    package = make_package(work / ("pkg-" + name), files)
    out = work / name
    manifest = rm.build_release(package, out, "Owner/Repo", "next", build, "abc123")
    return package, out, manifest


def sha(data):
    return hashlib.sha256(data).hexdigest()


def zip_names(path):
    with zipfile.ZipFile(path) as archive:
        return sorted(archive.namelist())


def test_assets_and_manifest(work):
    package, out, manifest = release(work)
    check(sorted(path.name for path in out.iterdir()) == ["Coop.zip", "launcher.zip", "manifest.json", "mod-re0.zip", "mod-sr2.zip"],
          "the release holds the full zip, the launcher, one mod per game and the manifest")
    check(zip_names(out / "launcher.zip") == ["launcher/app/coop-launcher.exe", "launcher/app/steam_api64.dll", "launcher/app/update.json"],
          "launcher.zip is launcher/app/**, update.json included")
    check(zip_names(out / "mod-sr2.zip") == ["adapters/sr2/dinput8.dll", "adapters/sr2/scripts.delta.json", "launcher/games/sr2.json"],
          "a mod zip holds the profile and the adapter files")
    check("launcher/installed.json" in zip_names(out / "Coop.zip") and "README.txt" in zip_names(out / "Coop.zip"),
          "Coop.zip holds the whole package with installed.json and the readme")
    check(all("launcher/installed.json" not in zip_names(out / name) for name in ("launcher.zip", "mod-re0.zip", "mod-sr2.zip")),
          "installed.json is in no component")
    on_disk = json.loads((out / "manifest.json").read_text())
    check(on_disk == manifest and list(on_disk) == ["build", "channel", "commit", "full", "components"], "manifest.json is the returned manifest, keys in order")
    check(on_disk["build"] == 7 and on_disk["channel"] == "next" and on_disk["commit"] == "abc123", "build, channel and commit")
    check([part["id"] for part in on_disk["components"]] == ["launcher", "re0", "sr2"], "components: the launcher, then the games in order")
    return package, out, on_disk


def test_hashes(out, manifest):
    for part in [manifest["full"], *manifest["components"]]:
        data = (out / part["asset"]).read_bytes()
        check(part["sha256"] == sha(data) and part["size"] == len(data), f"{part['asset']}: sha256 and size match the file")
    for part in manifest["components"]:
        with zipfile.ZipFile(out / part["asset"]) as archive:
            for entry in part["files"]:
                data = archive.read(entry["path"])
                check(entry["sha256"] == sha(data) and entry["size"] == len(data), f"{entry['path']}: sha256 and size match the zip entry")
            check(sorted(entry["path"] for entry in part["files"]) == sorted(archive.namelist()), f"{part['id']}: the manifest lists exactly the zip's files")
    paths = [entry["path"] for part in manifest["components"] for entry in part["files"]]
    check(len(paths) == len(set(paths)) and all("\\" not in path and not path.startswith("/") for path in paths),
          "file paths are unique, relative and use forward slashes")


def test_written_files(package):
    check(json.loads((package / "launcher/app/update.json").read_text()) == {"repo": "Owner/Repo", "channel": "next"}, "update.json carries repo and channel")
    check(json.loads((package / "launcher/installed.json").read_text()) == {"build": 7, "channel": "next", "commit": "abc123"},
          "installed.json carries build, channel and commit")


def test_same_package_same_zips(work):
    _, first, _ = release(work, "again-a")
    _, second, _ = release(work, "again-b")
    check(all((first / name).read_bytes() == (second / name).read_bytes() for name in ("Coop.zip", "launcher.zip", "mod-re0.zip")),
          "the same package gives byte-identical zips")


def test_legacy_zip(work):
    package = make_package(work / "legacy-pkg")
    rm.build_release(package, work / "legacy-out", "O/R", "latest", 41, "c", legacy_zip="RE0-Coop.zip")
    with zipfile.ZipFile(work / "legacy-out/RE0-Coop.zip") as archive:
        check(archive.read("launcher/app/version.txt") == b"41", "the legacy zip carries the build in version.txt")
        check(sorted(archive.namelist()) == sorted([*FILES, "launcher/app/update.json", "launcher/installed.json", "launcher/app/version.txt"]),
              "and is otherwise the whole package")
    check(not (package / "launcher/app/version.txt").exists(), "version.txt is not left in the package")
    manifest = json.loads((work / "legacy-out/manifest.json").read_text())
    check(all("version.txt" not in entry["path"] for part in manifest["components"] for entry in part["files"]), "version.txt is in no component")


def test_layout_errors(work):
    def refused(files, what):
        try:
            release(work, "bad-" + what.replace(" ", "-"), files=files)
            check(False, f"refused: {what}")
        except rm.PackageError:
            pass

    refused({**FILES, "adapters/sr2/loose/extra.bin": b"x", "stray.txt": b"x"}, "a file in no component")
    refused({key: value for key, value in FILES.items() if not key.startswith("adapters/re0")}, "a profile without adapter files")
    refused({key: value for key, value in FILES.items() if not key.startswith("launcher/app")}, "no launcher exe")
    refused({key: value for key, value in FILES.items() if not key.startswith("launcher/games")}, "no game profile")


def test_command_line(work):
    make_package(work / "cli-pkg")
    code = rm.main([str(work / "cli-pkg"), str(work / "cli-out"), "--repo", "O/R", "--channel", "latest", "--build", "3", "--commit", "c"])
    check(code == 0 and json.loads((work / "cli-out/manifest.json").read_text())["channel"] == "latest", "the command line builds a release")


def main():
    with tempfile.TemporaryDirectory(prefix="cf_release_manifest_") as directory:
        work = Path(directory)
        package, out, manifest = test_assets_and_manifest(work)
        test_hashes(out, manifest)
        test_written_files(package)
        test_same_package_same_zips(work)
        test_legacy_zip(work)
        test_layout_errors(work)
        test_command_line(work)
    print("release_manifest_test: " + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
