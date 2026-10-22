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
FINGERPRINTS = {"launcher": "fp-launcher", "re0": "fp-re0", "sr2": "fp-sr2"}
REBUILT = {**FILES, "launcher/app/coop-launcher.exe": b"exe rebuilt", "adapters/re0/dinput8.dll": b"re0 dll rebuilt",
           "adapters/sr2/dinput8.dll": b"sr2 dll rebuilt"}
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


def release(work, name="out", build=7, files=FILES, fingerprints=FINGERPRINTS, previous=None, channel="next"):
    package = make_package(work / ("pkg-" + name), files)
    out = work / name
    manifest = rm.build_release(package, out, "Owner/Repo", channel, build, "abc123", fingerprints, previous)
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
    check(all(list(part)[:3] == ["id", "version", "fingerprint"] for part in on_disk["components"]), "a component starts with its id, version and fingerprint")
    check([part["version"] for part in on_disk["components"]] == [1, 1, 1] and [part["fingerprint"] for part in on_disk["components"]] == list(FINGERPRINTS.values()),
          "with no previous release every component is version 1 and carries its fingerprint")
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
    rm.build_release(package, work / "legacy-out", "O/R", "latest", 41, "c", FINGERPRINTS, legacy_zip="RE0-Coop.zip")
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
    try:
        release(work, "no-fingerprint", fingerprints={"launcher": "x", "re0": "y"})
        check(False, "refused: a component without a fingerprint")
    except rm.PackageError:
        pass


def component(manifest, part):
    return next(entry for entry in manifest["components"] if entry["id"] == part)


def zip_bytes(out, part):
    return (out / rm.asset_name(part)).read_bytes()


def test_unchanged_components_are_reused(work):
    _, first, first_manifest = release(work, "reuse-1", build=7)
    package, second, manifest = release(work, "reuse-2", build=8, files=REBUILT, previous=first)
    check(all(zip_bytes(first, part) == zip_bytes(second, part) for part in FINGERPRINTS),
          "same fingerprints: the previous zips ship again byte for byte, whatever the new build produced")
    check(all(component(manifest, part) == component(first_manifest, part) for part in FINGERPRINTS),
          "and their manifest entries (version, hashes, files) are the previous ones")
    check(manifest["build"] == 8 and json.loads((package / "launcher/installed.json").read_text())["components"] == {"launcher": 1, "re0": 1, "sr2": 1},
          "the release build moves on, the component versions stay")
    with zipfile.ZipFile(second / "Coop.zip") as archive:
        check(archive.read("adapters/re0/dinput8.dll") == FILES["adapters/re0/dinput8.dll"]
              and archive.read("launcher/app/coop-launcher.exe") == FILES["launcher/app/coop-launcher.exe"],
              "Coop.zip holds the previous files of a reused component, not the new build's")
    check((package / "adapters/sr2/dinput8.dll").read_bytes() == FILES["adapters/sr2/dinput8.dll"], "the package files are the previous ones too")
    check((second / "Coop.zip").read_bytes() != (first / "Coop.zip").read_bytes(), "Coop.zip still differs: installed.json names the new build")


def test_changed_component_bumps_only_itself(work):
    _, first, _ = release(work, "bump-1", build=7)
    changed = {**FINGERPRINTS, "sr2": "fp-sr2-changed"}
    _, second, manifest = release(work, "bump-2", build=8, files=REBUILT, previous=first, fingerprints=changed)
    check([component(manifest, part)["version"] for part in FINGERPRINTS] == [1, 1, 2], "only the component with a new fingerprint goes up by one")
    check(zip_bytes(second, "launcher") == zip_bytes(first, "launcher") and zip_bytes(second, "re0") == zip_bytes(first, "re0"),
          "the others are the previous zips")
    check(zip_bytes(second, "sr2") != zip_bytes(first, "sr2") and component(manifest, "sr2")["fingerprint"] == "fp-sr2-changed",
          "the changed one is the new build with the new fingerprint")
    with zipfile.ZipFile(second / "mod-sr2.zip") as archive:
        check(archive.read("adapters/sr2/dinput8.dll") == REBUILT["adapters/sr2/dinput8.dll"], "and holds the new build's files")
    _, third, again = release(work, "bump-3", build=9, files=REBUILT, previous=second, fingerprints={**changed, "re0": "fp-re0-2"})
    check([component(again, part)["version"] for part in FINGERPRINTS] == [1, 2, 2], "versions count up per component across releases")
    check(zip_bytes(third, "sr2") == zip_bytes(second, "sr2"), "a component that stayed the same since the last release is reused again")


def versions_with(work, name, previous, channel="next", fingerprints=FINGERPRINTS):
    manifest = release(work, name, build=8, previous=previous, channel=channel, fingerprints=fingerprints)[2]
    return [component(manifest, part)["version"] for part in FINGERPRINTS]


def test_nothing_to_reuse_starts_at_one(work):
    _, first, _ = release(work, "start-1", build=7)
    check(versions_with(work, "start-none", None) == [1, 1, 1], "no previous release: version 1")
    check(versions_with(work, "start-missing", work / "does-not-exist") == [1, 1, 1], "a previous folder without a manifest: version 1")
    check(versions_with(work, "start-channel", first, channel="latest") == [1, 1, 1], "another channel manifest is not used")
    check(versions_with(work, "start-changed", first, fingerprints={part: "other-" + part for part in FINGERPRINTS}) == [2, 2, 2],
          "every fingerprint changed: every version goes up")
    legacy = work / "legacy-previous"
    legacy.mkdir()
    old = json.loads((first / "manifest.json").read_text())
    for entry in old["components"]:
        del entry["version"], entry["fingerprint"]
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
        _, second, manifest = release(work, name + "-2", build=8, files=REBUILT, previous=first)
        with zipfile.ZipFile(second / "mod-re0.zip") as archive:
            new_build = archive.read("adapters/re0/dinput8.dll") == REBUILT["adapters/re0/dinput8.dll"]
        return component(manifest, "re0")["version"] == 2 and new_build and component(manifest, "sr2")["version"] == 1

    check(rebuilt_after(lambda out: (out / "mod-re0.zip").unlink(), "gone"), "a previous zip that is missing: that component is rebuilt, the others still reused")
    check(rebuilt_after(lambda out: (out / "mod-re0.zip").write_bytes(b"damaged"), "damaged"), "a previous zip that does not match its hash is rebuilt")
    check(rebuilt_after(lambda out: rewrite_manifest(out, lambda entry: entry["files"].pop()), "files"),
          "a previous entry whose files are not the package files is rebuilt")


def test_command_line(work):
    make_package(work / "cli-pkg")
    (work / "fingerprints.json").write_text(json.dumps(FINGERPRINTS))
    command = [str(work / "cli-pkg"), str(work / "cli-out"), "--repo", "O/R", "--channel", "latest", "--build", "3", "--commit", "c",
               "--fingerprints", str(work / "fingerprints.json")]
    check(rm.main(command) == 0 and json.loads((work / "cli-out/manifest.json").read_text())["channel"] == "latest", "the command line builds a release")
    check(rm.main(command[:-1] + [str(work / "missing.json")]) == 1, "a missing fingerprints file is an error, not a crash")


def main():
    with tempfile.TemporaryDirectory(prefix="cf_release_manifest_") as directory:
        work = Path(directory)
        package, out, manifest = test_assets_and_manifest(work)
        test_hashes(out, manifest)
        test_written_files(package)
        test_same_package_same_zips(work)
        test_unchanged_components_are_reused(work)
        test_changed_component_bumps_only_itself(work)
        test_nothing_to_reuse_starts_at_one(work)
        test_unusable_previous_zip_is_rebuilt(work)
        test_legacy_zip(work)
        test_layout_errors(work)
        test_command_line(work)
    print("release_manifest_test: " + ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
