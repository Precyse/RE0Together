"""Turns an assembled package into the files of a release channel: Coop.zip, launcher.zip, mod-<id>.zip per game and manifest.json.

usage: python tools/release_manifest.py PACKAGE OUT --repo OWNER/REPO --channel next --build N --commit SHA [--legacy-zip NAME]

PACKAGE holds launcher/app (the published launcher), launcher/games/<id>.json, adapters/<id>/** per game and README.txt. The
script writes launcher/app/update.json ({repo, channel}) and launcher/installed.json ({build, channel, commit}) into it, then
zips (entries sorted, fixed timestamps, forward slashes, so the same package gives the same zips):
  Coop.zip      the whole package, for first installs
  launcher.zip  launcher/app/**
  mod-<id>.zip  launcher/games/<id>.json and adapters/<id>/**
manifest.json lists every asset with its SHA-256 and size, and every component's files with theirs (the launcher reads it,
see launcher/ReleaseManifest.cs). installed.json is not in any component: the launcher writes it last when an update is done.
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


def build_release(package, out, repo, channel, build, commit, legacy_zip=None):
    """Writes the zips and manifest.json into out and returns the manifest."""
    write_json(package / UPDATE_FILE, {"repo": repo, "channel": channel})
    write_json(package / INSTALLED_FILE, {"build": build, "channel": channel, "commit": commit})
    parts = components(package)
    check_covered(package, parts)
    everything = package_files(package, ".")
    write_zip(package, everything, out / FULL_ASSET)
    entries = []
    for part, paths in parts.items():
        zip_path = out / (f"{part}.zip" if part == LAUNCHER_ID else f"mod-{part}.zip")
        write_zip(package, paths, zip_path)
        entries.append({"id": part, **asset_entry(zip_path), "files": [file_entry(package, path) for path in paths]})
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
    parser.add_argument("--legacy-zip")
    args = parser.parse_args(argv)
    try:
        manifest = build_release(args.package, args.out, args.repo, args.channel, args.build, args.commit, args.legacy_zip)
    except PackageError as error:
        print(f"release_manifest: {error}", file=sys.stderr)
        return 1
    print(f"release_manifest: build {manifest['build']} on {manifest['channel']}: " + ", ".join(
        f"{part['asset']} {part['size']}" for part in [manifest["full"], *manifest["components"]]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
