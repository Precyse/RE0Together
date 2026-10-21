"""Frame, long-task and state-apply numbers of the launcher page, measured in real time (needs the `websockets` package).

python tools/launcher_web/perf.py <fixture|sequence> [--size 840x540] [--out file.txt]

Starts the throwaway static server and headless Edge (new empty profile, remote debugging on a free port, no user profile), loads the page
with ?perf=1, waits for the mock's html[data-ready] and prints the text of the perf overlay (js/perf.js). Both are stopped by PID at the end.
"""
import argparse
import asyncio
import json
import os
import shutil
import subprocess
import tempfile
import time

import websockets

import render

READY_TIMEOUT_S = 30
PORT_FILE_TIMEOUT_S = 20
SETTLE_AFTER_READY_S = 1.0
POLL_S = 0.1
READY_EXPRESSION = "document.documentElement.hasAttribute('data-ready')"
PERF_EXPRESSION = "document.getElementById('perf').textContent"


class Session:
    def __init__(self, socket):
        self.socket = socket
        self.next_id = 0

    async def call(self, method, params=None, session=None):
        self.next_id += 1
        message = {"id": self.next_id, "method": method, "params": params or {}}
        if session:
            message["sessionId"] = session
        await self.socket.send(json.dumps(message))
        while True:
            reply = json.loads(await self.socket.recv())
            if reply.get("id") == self.next_id:
                return reply.get("result", {})


async def measure(port, url):
    async with websockets.connect(f"ws://127.0.0.1:{port}/devtools/browser/{browser_id(port)}", max_size=None) as socket:
        cdp = Session(socket)
        target = (await cdp.call("Target.createTarget", {"url": "about:blank"}))["targetId"]
        session = (await cdp.call("Target.attachToTarget", {"targetId": target, "flatten": True}))["sessionId"]
        await cdp.call("Page.navigate", {"url": url}, session)
        deadline = time.time() + READY_TIMEOUT_S
        while time.time() < deadline:
            ready = await cdp.call("Runtime.evaluate", {"expression": READY_EXPRESSION, "returnByValue": True}, session)
            if ready["result"].get("value"):
                break
            await asyncio.sleep(POLL_S)
        await asyncio.sleep(SETTLE_AFTER_READY_S)
        text = await cdp.call("Runtime.evaluate", {"expression": PERF_EXPRESSION, "returnByValue": True}, session)
        return text["result"].get("value", "")


def browser_id(port):
    return open(PORT_FILE, encoding="utf8").read().split("\n")[1].rsplit("/", 1)[-1]


PORT_FILE = ""


def main():
    global PORT_FILE
    parser = argparse.ArgumentParser()
    parser.add_argument("key")
    parser.add_argument("--size", default="840x540")
    parser.add_argument("--out", default="")
    args = parser.parse_args()
    width, height = args.size.split("x")
    server = render.serve()
    profile = tempfile.mkdtemp(prefix="coop-web-edge-")
    kind = "sequence" if render.sequence_ms(args.key) is not None else "fixture"
    url = f"http://127.0.0.1:{server.server_port}/index.html?{kind}={args.key}&perf=1"
    edge = subprocess.Popen([render.EDGE, "--headless=new", f"--user-data-dir={profile}", "--force-device-scale-factor=1", "--hide-scrollbars",
                             f"--window-size={width},{height}", "--remote-debugging-port=0", "--no-first-run", "--disable-extensions", "about:blank"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        PORT_FILE = os.path.join(profile, "DevToolsActivePort")
        deadline = time.time() + PORT_FILE_TIMEOUT_S
        while not os.path.isfile(PORT_FILE) and time.time() < deadline:
            time.sleep(POLL_S)
        port = int(open(PORT_FILE, encoding="utf8").read().split("\n")[0])
        text = asyncio.run(measure(port, url))
        print(text)
        if args.out:
            with open(args.out, "w", encoding="utf8") as handle:
                handle.write(text)
    finally:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(edge.pid)], capture_output=True)
        shutil.rmtree(profile, ignore_errors=True)
        server.shutdown()


if __name__ == "__main__":
    main()
