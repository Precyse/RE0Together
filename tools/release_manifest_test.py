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
PARTS = ["launcher", "re0", "sr2"]
RE0_CHANGED = {**FILES, "adapters/re0/dinput8.dll": b"re0 dll changed"}
ALL_CHANGED = {**FILES, "launcher/app/coop-launcher.exe": b"exe changed", "adapters/re0/dinput8.dll": b"re0 dll changed",
               "adapters/sr2/dinput8.dll": b"sr2 dll changed"}
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


def release(work, name="out", build=7, files=FILES, previous=None, channel="next"):
    package = make_package(work / ("pkg-" + name), files)
    out = work / name
    manifest = rm.build_release(package, out, "Owner/Repo", channel, build, "abc123", previous)
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
    check(all(list(part)[:2] == ["id", "version"] for part in on_disk["components"]), "a component starts with its id and version")
    check([part["version"] for part in on_disk["components"]] == [1, 1, 1], "with no previous release every component is version 1")
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
    check(json.loads((package / "launcher/installed.json").read_text()) ==
          {"build": 7, "channel": "next", "commit": "abc123", "components": {"launcher": 1, "re0": 1, "sr2": 1}},
          "installed.json carries build, channel, commit and every component version")


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

    refused({**FILES, "adapters/sr2/scripts/a.lua": b"x"}, "generated SR2 scripts")
    refused({**FILES, "adapters/sr2/loose/extra.bin": b"x", "stray.txt": b"x"}, "a file in no component")
    refused({key: value for key, value in FILES.items() if not key.startswith("adapters/re0")}, "a profile without adapter files")
    refused({key: value for key, value in FILES.items() if not key.startswith("launcher/app")}, "no launcher exe")
    refused({key: value for key, value in FILES.items() if not key.startswith("launcher/games")}, "no game profile")


def component(manifest, part):
    return next(entry for entry in manifest["components"] if entry["id"] == part)


def zip_bytes(out, part):
    return (out / rm.asset_name(part)).read_bytes()


def test_identical_output_is_reused(work):
    _, first, first_manifest = release(work, "reuse-1", build=7)
    package, second, manifest = release(work, "reuse-2", build=8, previous=first)
    check(all(zip_bytes(first, part) == zip_bytes(second, part) for part in PARTS), "identical output: the previous zips ship again byte for byte")
    check(all(component(manifest, part) == component(first_manifest, part) for part in PARTS),
          "and their manifest entries (version, hashes, files) are the previous ones")
    check(manifest["build"] == 8 and json.loads((package / "launcher/installed.json").read_text())["components"] == {"launcher": 1, "re0": 1, "sr2": 1},
          "the release build moves on, the component versions stay")
    check((second / "Coop.zip").read_bytes() != (first / "Coop.zip").read_bytes(), "Coop.zip still differs: installed.json names the new build")


def test_changed_output_is_bumped(work):
    _, first, _ = release(work, "bump-1", build=7)
    _, second, manifest = release(work, "bump-2", build=8, files=ALL_CHANGED, previous=first)
    check([component(manifest, part)["version"] for part in PARTS] == [2, 2, 2], "every component whose files changed goes up by one")
    check(all(zip_bytes(second, part) != zip_bytes(first, part) for part in PARTS), "and ships the new build")
    with zipfile.ZipFile(second / "mod-sr2.zip") as archive:
        check(archive.read("adapters/sr2/dinput8.dll") == ALL_CHANGED["adapters/sr2/dinput8.dll"], "with the new build's files")
    _, third, again = release(work, "bump-3", build=9, files=ALL_CHANGED, previous=second)
    check([component(again, part)["version"] for part in PARTS] == [2, 2, 2] and zip_bytes(third, "sr2") == zip_bytes(second, "sr2"),
          "output that stayed the same since the last release is reused again")


def test_one_mod_changed_leaves_the_others(work):
    """An RE0-only source change: the SR2 and launcher bytes are what they were, so only RE0 is an update."""
    _, first, _ = release(work, "only-1", build=7)
    package, second, manifest = release(work, "only-2", build=8, files=RE0_CHANGED, previous=first)
    check([component(manifest, part)["version"] for part in PARTS] == [1, 2, 1], "only the mod with different bytes goes up")
    check(zip_bytes(second, "sr2") == zip_bytes(first, "sr2") and zip_bytes(second, "launcher") == zip_bytes(first, "launcher"),
          "the others are the previous zips")
    check(json.loads((package / "launcher/installed.json").read_text())["components"] == {"launcher": 1, "re0": 2, "sr2": 1},
          "installed.json carries the kept and the bumped versions")


def test_file_set_change_is_bumped(work):
    _, first, _ = release(work, "set-1", build=7)
    _, _, added = release(work, "set-2", build=8, files={**FILES, "adapters/sr2/extra.bin": b"x"}, previous=first)
    check([component(added, part)["version"] for part in PARTS] == [1, 1, 2], "an added file is a change")
    moved = {key: value for key, value in FILES.items() if key != "adapters/sr2/scripts.delta.json"}
    moved["adapters/sr2/scripts.json"] = FILES["adapters/sr2/scripts.delta.json"]
    _, _, renamed = release(work, "set-3", build=8, files=moved, previous=first)
    check([component(renamed, part)["version"] for part in PARTS] == [1, 1, 2], "the same bytes under another path is a change")


def versions_with(work, name, previous, channel="next"):
    manifest = release(work, name, build=8, previous=previous, channel=channel)[2]
    return [component(manifest, part)["version"] for part in PARTS]


def test_nothing_to_reuse_starts_at_one(work):
    _, first, _ = release(work, "start-1", build=7)
    check(versions_with(work, "start-none", None) == [1, 1, 1], "no previous release: version 1")
    check(versions_with(work, "start-missing", work / "does-not-exist") == [1, 1, 1], "a previous folder without a manifest: version 1")
    check(versions_with(work, "start-channel", first, channel="latest") == [1, 1, 1], "another channel manifest is not used")
    legacy = work / "legacy-previous"
    legacy.mkdir()
    old = json.loads((first / "manifest.json").read_text())
    for entry in old["components"]:
        del entry["version"]
    (legacy / "manifest.json").write_text(json.dumps(old))
    check(versions_with(work, "start-legacy", legacy) == [1, 1, 1], "a previous manifest from before component versions: version 1")


def rewrite_manifest(out, change):
    manifest = json.loads((out / "manifest.json").read_text())
    change(component(manifest, "re0"))
    (out / "manifest.json").write_text(json.dumps(manifest))


def test_unusable_previous_zip_is_rebuilt(work):
    def rebuilt_after(damage, name):
        _, first, _ = release(work, name + "-1", build=7)
        damage(first)
        _, _, manifest = release(work, name + "-2", build=8, previous=first)
        return component(manifest, "re0")["version"] == 2 and component(manifest, "sr2")["version"] == 1

    check(rebuilt_after(lambda out: (out / "mod-re0.zip").unlink(), "gone"), "a previous zip that is missing: that component is bumped, the others still reused")
    check(rebuilt_after(lambda out: (out / "mod-re0.zip").write_bytes(b"damaged"), "damaged"), "a previous zip that does not match its hash is bumped")
    check(rebuilt_after(lambda out: rewrite_manifest(out, lambda entry: entry["files"].pop()), "files"),
          "a previous entry whose files are not the package files is bumped")


def test_command_line(work):
    make_package(work / "cli-pkg")
    command = [str(work / "cli-pkg"), str(work / "cli-out"), "--repo", "O/R", "--channel", "latest", "--build", "3", "--commit", "c"]
    check(rm.main(command) == 0 and json.loads((work / "cli-out/manifest.json").read_text())["channel"] == "latest", "the command line builds a release")
    check(rm.main([str(work / "missing-pkg"), *command[1:]]) == 1, "a missing package is an error, not a crash")


def main():
    with tempfile.TemporaryDirectory(prefix="cf_release_manifest_") as directory:
        work = Path(directory)
        package, out, manifest = test_assets_and_manifest(work)
        test_hashes(out, manifest)
        test_written_files(package)
        test_same_package_same_zips(work)
        test_identical_output_is_reused(work)
        test_changed_output_is_bumped(work)
        test_one_mod_changed_leaves_the_others(work)
        test_file_set_change_is_bumped(work)
        test_nothing_to_reuse_starts_at_one(work)
        test_unusable_previous_zip_is_rebuilt(work)
        test_legacy_zip(work)
        test_layout_errors(work)
        test_command_line(work)
    print("release_manifest_test: " + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
