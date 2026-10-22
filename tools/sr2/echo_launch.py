"""Starting the game instances for the echo hub (`--launch`): SR2_pc.exe nologos with the instance's folder as its working
directory (a direct start, not steam://). An instance whose exe is already running from its folder is left alone. The hub never
kills or restarts a game."""
import os
import subprocess
import threading

from echo_identity import normal_path, running_exe_paths

GAME_EXE = "SR2_pc.exe"
GAME_ARGS = ("nologos",)
STEP_TIMEOUT_S = 20.0
DETACHED_FLAGS = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP


def exe_of(folder):
    return os.path.join(folder, GAME_EXE)


def instances_to_start(folders, running):
    """The instances, host first, whose exe is not in the set of running image paths."""
    return [k for k in sorted(folders) if normal_path(exe_of(folders[k])) not in running]


def start_game(folder):
    """The process is not waited for and keeps running when the hub quits."""
    return subprocess.Popen([exe_of(folder), *GAME_ARGS], cwd=folder, creationflags=DETACHED_FLAGS, close_fds=True,
                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def launch(folders, hello_events, out, start=start_game, running=running_exe_paths):
    """Starts what is not running, each instance after the previous one's HELLO or STEP_TIMEOUT_S. Runs on its own thread."""
    for k in instances_to_start(folders, running()):
        out(f"launching instance {k}: {exe_of(folders[k])}")
        try:
            start(folders[k])
        except OSError as error:
            out(f"cannot start instance {k}: {error}")
            continue
        if not hello_events[k].wait(STEP_TIMEOUT_S):
            out(f"instance {k} did not connect within {STEP_TIMEOUT_S:.0f} s, starting the next")


def launch_in_background(folders, hello_events, out):
    thread = threading.Thread(target=launch, args=(folders, hello_events, out), daemon=True)
    thread.start()
    return thread
