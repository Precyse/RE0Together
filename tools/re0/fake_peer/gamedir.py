"""Finds RE0's folder the way the launcher does: Steam's libraryfolders.vdf names the library that holds the app, its
appmanifest names the folder. Reads two files; never sweeps drives."""
import re
from pathlib import Path

APP_ID = "339340"
DEFAULT_STEAM = Path(r"C:\Program Files (x86)\Steam")
LIBRARY_BLOCK = re.compile(r'"path"\s+"([^"]+)"(.*?)(?="path"|\Z)', re.S)
INSTALL_DIR = re.compile(r'"installdir"\s+"([^"]+)"')


def steam_path():
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
            return Path(winreg.QueryValueEx(key, "SteamPath")[0])
    except (ImportError, OSError):
        return DEFAULT_STEAM


def library_of_app(vdf_text, app_id=APP_ID):
    """The library path whose apps block lists `app_id`, or None."""
    for path, block in LIBRARY_BLOCK.findall(vdf_text):
        if re.search(rf'"{app_id}"\s+"', block):
            return Path(path.replace("\\\\", "\\"))
    return None


def find_game_dir(steam=None):
    """The RE0 folder, or None when Steam does not list the game."""
    steam = Path(steam) if steam else steam_path()
    vdf = steam / "steamapps" / "libraryfolders.vdf"
    if not vdf.exists():
        return None
    library = library_of_app(vdf.read_text(encoding="utf-8", errors="replace"))
    manifest = library / "steamapps" / f"appmanifest_{APP_ID}.acf" if library else None
    if not manifest or not manifest.exists():
        return None
    found = INSTALL_DIR.search(manifest.read_text(encoding="utf-8", errors="replace"))
    return library / "steamapps" / "common" / found.group(1) if found else None
