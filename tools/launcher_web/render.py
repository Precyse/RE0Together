"""Headless screenshots of the launcher page without the launcher.

python tools/launcher_web/render.py <key...> --out <dir> [--size 840x540] [--frames 0,62,125,187,250 | 0%,25%,50%,75%,100%]
       [--reduced] [--smoke <name>] [--dump-dom] [--prepare-art]

A key is a fixture (launcher/web/dev/fixtures/<key>.js) or a sequence (dev/sequences/<key>.js). The page is served from 127.0.0.1 on a free port by
a throwaway server, drawn by headless Edge with a new empty profile folder; both are stopped when the run ends. Never starts the launcher.
"""
import argparse
import functools
import http.server
import json
import os
import re
import shutil
import subprocess
import tempfile
import threading
import winreg

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WEB = os.path.join(REPO, "launcher", "web")
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
VIRTUAL_TIME_MS = 3000
RUN_TIMEOUT_S = 90
STEAM_DEFAULT = r"C:\Program Files (x86)\Steam"
APP_IDS = [339340, 9480, 3280350]
ART_FILES = {"capsule.jpg": ["library_600x900.jpg", "library_capsule.jpg"], "hero.jpg": ["library_hero.jpg"], "logo.png": ["logo.png"]}
OUTFIT_SOURCE = os.path.join(os.environ.get("APPDATA", ""), "CoopLauncher", "art", "re0")
SMOKE_DUMP = re.compile(r'<pre id="smoke"[^>]*>(.*?)</pre>', re.S)


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".js": "text/javascript", ".mjs": "text/javascript"}

    def log_message(self, *args):
        pass


def serve():
    handler = functools.partial(Handler, directory=WEB)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def steam_root():
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
            return winreg.QueryValueEx(key, "SteamPath")[0].replace("/", "\\")
    except OSError:
        return STEAM_DEFAULT


def find_file(folder, names):
    for name in names:
        for root, _, files in os.walk(folder):
            if name in files:
                return os.path.join(root, name)
    return None


def prepare_art():
    art = os.path.join(WEB, "dev", "art")
    for app in APP_IDS:
        source = os.path.join(steam_root(), "appcache", "librarycache", str(app))
        os.makedirs(os.path.join(art, str(app)), exist_ok=True)
        for target, names in ART_FILES.items():
            found = find_file(source, names) if os.path.isdir(source) else None
            if found:
                shutil.copyfile(found, os.path.join(art, str(app), target))
    figures = os.path.join(art, "figure")
    os.makedirs(figures, exist_ok=True)
    if os.path.isdir(OUTFIT_SOURCE):
        for name in os.listdir(OUTFIT_SOURCE):
            if name.endswith(".png"):
                shutil.copyfile(os.path.join(OUTFIT_SOURCE, name), os.path.join(figures, name))


def sequence_ms(key):
    path = os.path.join(WEB, "dev", "sequences", f"{key}.js")
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf8") as handle:
        return json.loads(re.sub(r"^export default |;\s*$", "", handle.read().strip()))["ms"]


def frame_list(spec, key):
    out = []
    for part in spec.split(","):
        if part.endswith("%"):
            total = sequence_ms(key)
            if total is None:
                raise SystemExit(f"{key} is not a sequence: percent frames need dev/sequences/{key}.js")
            out.append(round(total * float(part[:-1]) / 100))
        else:
            out.append(int(part))
    return out


def run_edge(url, size, png, dump):
    profile = tempfile.mkdtemp(prefix="coop-web-edge-")
    command = [EDGE, "--headless=new", f"--user-data-dir={profile}", "--force-device-scale-factor=1", "--hide-scrollbars",
               f"--window-size={size[0]},{size[1]}", f"--virtual-time-budget={VIRTUAL_TIME_MS}", "--disable-extensions",
               "--no-first-run", "--no-default-browser-check", ("--dump-dom" if dump else f"--screenshot={png}"), url]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, encoding="utf8")
    try:
        output, _ = process.communicate(timeout=RUN_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)], capture_output=True)
        raise SystemExit(f"Edge timed out on {url}")
    finally:
        shutil.rmtree(profile, ignore_errors=True)
    return output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("keys", nargs="*")
    parser.add_argument("--out", default=".")
    parser.add_argument("--size", default="840x540")
    parser.add_argument("--frames", default="")
    parser.add_argument("--reduced", action="store_true")
    parser.add_argument("--smoke", default="")
    parser.add_argument("--dump-dom", action="store_true")
    parser.add_argument("--prepare-art", action="store_true")
    args = parser.parse_args()
    if args.prepare_art:
        prepare_art()
    if not args.keys:
        return
    size = tuple(int(n) for n in args.size.split("x"))
    os.makedirs(args.out, exist_ok=True)
    server = serve()
    try:
        for key in args.keys:
            page = "mini.html" if key.startswith("mini_") else "index.html"
            kind = "sequence" if sequence_ms(key) is not None else "fixture"
            frames = frame_list(args.frames, key) if args.frames else [None]
            for frame in frames:
                query = f"{kind}={key}" + (f"&frame={frame}" if frame is not None else "") + ("&reduced=1" if args.reduced else "") \
                    + (f"&smoke={args.smoke}" if args.smoke else "")
                url = f"http://127.0.0.1:{server.server_port}/{page}?{query}"
                name = key + (f"_{frame}" if frame is not None else "") + ("_reduced" if args.reduced else "") + (f"_{args.smoke}" if args.smoke else "")
                png = os.path.join(args.out, name + ".png")
                if args.dump_dom or args.smoke:
                    dom = run_edge(url, size, None, True)
                    found = SMOKE_DUMP.search(dom) if args.smoke else None
                    if found:
                        with open(os.path.join(args.out, f"{name}_smoke.txt"), "w", encoding="utf8") as handle:
                            handle.write(found.group(1))
                        print(f"{name} smoke:")
                        print(found.group(1))
                    if args.dump_dom:
                        with open(os.path.join(args.out, name + ".html"), "w", encoding="utf8") as handle:
                            handle.write(dom)
                run_edge(url, size, png, False)
                print(png)
    finally:
        server.shutdown()


if __name__ == "__main__":
    main()
