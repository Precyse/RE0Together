"""Turns an assembled package into the files of a release channel: Coop.zip, launcher.zip, mod-<id>.zip per game and manifest.json.

usage: python tools/release_manifest.py PACKAGE OUT --repo OWNER/REPO --channel next --build N --commit SHA [--previous DIR] [--legacy-zip NAME]

PACKAGE holds launcher/app (the published launcher), launcher/games/<id>.json, adapters/<id>/** per game and README.txt. The
script writes launcher/app/update.json ({repo, channel}) and launcher/installed.json ({build, channel, commit, components}) into it,
then zips (entries sorted, fixed timestamps, forward slashes, so the same package gives the same zips):
  Coop.zip      the whole package, for first installs
  launcher.zip  launcher/app/**
  mod-<id>.zip  launcher/games/<id>.json and adapters/<id>/**
manifest.json lists every asset with its SHA-256 and size, and every component's files with theirs (the launcher reads it,
see launcher/ReleaseManifest.cs). installed.json is not in any component: the launcher writes it last when an update is done.

Every component has its own `version`. DIR (--previous) holds the channel's previous manifest.json and component zips. A component
whose built files are exactly the previous entry's (the same paths and the same SHA-256 per file) keeps that zip, its version and its
entry unchanged: the zip is copied byte for byte, so players do not see an update for a mod that did not change. Reuse depends on the
output, never on the sources: the adapters build with /Brepro, the launcher publishes without the commit in its version, and nothing
in a component carries the build number (update.json holds repo and channel; the build number is in installed.json, which is no
component's file, and the launcher reads it from there), so the same sources give the same bytes and a change in shared sources that
leaves a DLL's bytes alone (an RE0-only edit and the SR2 DLL) is no update. A changed component gets the previous version + 1
(1 when there is no previous entry); `build` stays the CI run number (release title, and the launchers released before component
versions compare it).
--legacy-zip NAME also writes OUT/NAME: the full package plus launcher/app/version.txt (the build number), the file the launchers
released before installed.json check after they replace their package. It is what the `latest` channel serves as RE0-Coop.zip so
those launchers can still update once; it is in no component.
Fails when a file of the package belongs to no component (the layout and the components must not drift apart).
"""
import argparse
import hashlib
import json
import shutil
import sys
import zipfile
from pathlib import Path

LAUNCHER_ID = "launcher"
APP_DIR = "launcher/app"
LAUNCHER_EXE = "launcher/app/coop-launcher.exe"
GAMES_DIR = "launcher/games"
ADAPTERS_DIR = "adapters"
FULL_ASSET = "Coop.zip"
MANIFEST_FILE = "manifest.json"
README_FILE = "README.txt"
INSTALLED_FILE = "launcher/installed.json"
GENERATED_SCRIPTS_PREFIX = "adapters/sr2/scripts/"
LEGACY_VERSION_FILE = "launcher/app/version.txt"
UPDATE_FILE = "launcher/app/update.json"
ZIP_DATE = (1980, 1, 1, 0, 0, 0)
FILE_MODE = 0o644 << 16
CHUNK_BYTES = 1 << 20


class PackageError(Exception):
    """The package does not have the layout a release is built from."""


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(CHUNK_BYTES), b""):
            digest.update(chunk)
    return digest.hexdigest()


def package_files(package, subdir):
    """Relative paths (forward slashes, sorted) of the files below package/subdir."""
    base = package / subdir
    if not base.is_dir():
        return []
    return sorted(path.relative_to(package).as_posix() for path in base.rglob("*") if path.is_file())


def game_ids(package):
    ids = sorted(path.stem for path in (package / GAMES_DIR).glob("*.json"))
    if not ids:
        raise PackageError(f"no game profile in {GAMES_DIR}")
    return ids


def components(package):
    """{component id: relative paths}: the launcher, then one mod per game profile."""
    if not (package / LAUNCHER_EXE).is_file():
        raise PackageError(f"{LAUNCHER_EXE} is missing")
    parts = {LAUNCHER_ID: package_files(package, APP_DIR)}
    for game in game_ids(package):
        parts[game] = [f"{GAMES_DIR}/{game}.json", *package_files(package, f"{ADAPTERS_DIR}/{game}")]
        if len(parts[game]) < 2:
            raise PackageError(f"profile {game} has no files in {ADAPTERS_DIR}/{game}")
    return parts


def check_covered(package, parts):
    shipped = [path for paths in parts.values() for path in paths if path.startswith(GENERATED_SCRIPTS_PREFIX)]
    if shipped:
        raise PackageError(f"{GENERATED_SCRIPTS_PREFIX} is generated on the player's PC and never packaged (the delta is): {shipped[0]}")
    covered = {path for paths in parts.values() for path in paths} | {README_FILE, INSTALLED_FILE}
    stray = sorted(path.relative_to(package).as_posix() for path in package.rglob("*") if path.is_file()
                   and path.relative_to(package).as_posix() not in covered)
    if stray:
        raise PackageError("files in no component: " + ", ".join(stray))


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def write_zip(package, relative_paths, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for relative in sorted(relative_paths):
            info = zipfile.ZipInfo(relative, ZIP_DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = FILE_MODE
            with open(package / relative, "rb") as source, archive.open(info, "w") as target:
                shutil.copyfileobj(source, target, CHUNK_BYTES)


def write_legacy_zip(package, destination, build):
    version = package / LEGACY_VERSION_FILE
    version.write_text(str(build), encoding="utf-8")
    try:
        write_zip(package, package_files(package, "."), destination)
    finally:
        version.unlink()


def asset_entry(path):
    return {"asset": path.name, "sha256": sha256_of(path), "size": path.stat().st_size}


def file_entry(package, relative):
    path = package / relative
    return {"path": relative, "sha256": sha256_of(path), "size": path.stat().st_size}


def asset_name(part):
    return f"{part}.zip" if part == LAUNCHER_ID else f"mod-{part}.zip"


def load_previous(previous_dir, channel):
    """The previous release's manifest.json from previous_dir, or None: no folder, no readable manifest, or one of another channel."""
    if previous_dir is None:
        return None
    try:
        manifest = json.loads((previous_dir / MANIFEST_FILE).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    usable = isinstance(manifest, dict) and manifest.get("channel") == channel and isinstance(manifest.get("components"), list)
    return manifest if usable else None


def previous_entry(previous, part):
    """The previous manifest's entry of a component, or None."""
    entries = previous["components"] if previous else []
    return next((entry for entry in entries if isinstance(entry, dict) and entry.get("id") == part), None)


def previous_version(entry):
    """The version an entry recorded; 0 for no entry or one from before component versions."""
    version = entry.get("version") if entry else None
    return version if isinstance(version, int) and not isinstance(version, bool) and version > 0 else 0


def built_hashes(package, paths):
    """{path: SHA-256} of a component's freshly built files."""
    return {path: sha256_of(package / path) for path in paths}


def reusable_zip(entry, built, previous_dir):
    """The path of a component's previous zip when it can be shipped again, else None. It can when the previous entry lists exactly the
    built files with the built hashes, and the zip and every file in it are exactly what that entry says."""
    if previous_version(entry) == 0:
        return None
    try:
        listed = {file["path"]: file for file in entry["files"]}
        zip_path = previous_dir / entry["asset"]
        if Path(entry["asset"]).name != entry["asset"] or {path: file["sha256"] for path, file in listed.items()} != built or not zip_path.is_file():
            return None
        if zip_path.stat().st_size != entry["size"] or sha256_of(zip_path) != entry["sha256"]:
            return None
        with zipfile.ZipFile(zip_path) as archive:
            intact = set(archive.namelist()) == set(listed) and all(
                len(data := archive.read(path)) == file["size"] and hashlib.sha256(data).hexdigest() == file["sha256"] for path, file in listed.items())
    except (KeyError, TypeError, AttributeError, OSError, zipfile.BadZipFile):
        return None
    return zip_path if intact else None


def build_release(package, out, repo, channel, build, commit, previous_dir=None, legacy_zip=None):
    """Writes the zips and manifest.json into out and returns the manifest. A component whose built files equal the previous release's
    ships that release's zip again with its version; any other gets the previous version + 1 (see the module doc)."""
    write_json(package / UPDATE_FILE, {"repo": repo, "channel": channel})
    parts = components(package)
    check_covered(package, parts)
    previous = load_previous(previous_dir, channel)
    reused = {}
    versions = {}
    for part, paths in parts.items():
        entry = previous_entry(previous, part)
        reuse = reusable_zip(entry, built_hashes(package, paths), previous_dir)
        if reuse:
            reused[part] = reuse
        versions[part] = previous_version(entry) + (0 if reuse else 1)
    write_json(package / INSTALLED_FILE, {"build": build, "channel": channel, "commit": commit, "components": versions})
    write_zip(package, package_files(package, "."), out / FULL_ASSET)
    entries = []
    for part, paths in parts.items():
        zip_path = out / asset_name(part)
        if part in reused:
            shutil.copyfile(reused[part], zip_path)
        else:
            write_zip(package, paths, zip_path)
        entries.append({"id": part, "version": versions[part], **asset_entry(zip_path), "files": [file_entry(package, path) for path in paths]})
    manifest = {"build": build, "channel": channel, "commit": commit, "full": asset_entry(out / FULL_ASSET), "components": entries}
    write_json(out / MANIFEST_FILE, manifest)
    if legacy_zip:
        write_legacy_zip(package, out / legacy_zip, build)
    return manifest


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("package", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--channel", required=True)
    parser.add_argument("--build", required=True, type=int)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--previous", type=Path)
    parser.add_argument("--legacy-zip")
    args = parser.parse_args(argv)
    try:
        manifest = build_release(args.package, args.out, args.repo, args.channel, args.build, args.commit, args.previous, args.legacy_zip)
    except (PackageError, OSError, ValueError) as error:
        print(f"release_manifest: {error}", file=sys.stderr)
        return 1
    previous = load_previous(args.previous, args.channel)
    print(f"release_manifest: build {manifest['build']} on {manifest['channel']}: " + ", ".join(
        f"{part['asset']} {part['size']}" for part in [manifest["full"], *manifest["components"]]))
    print("release_manifest: versions " + ", ".join(
        f"{part['id']} v{part['version']} " + ("kept" if previous_version(previous_entry(previous, part["id"])) == part["version"] else "new")
        for part in manifest["components"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
