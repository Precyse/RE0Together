"""python tools\re0\fake_peer <command>: puts tools\re0 on sys.path once (for this package, probe and fake_session)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from fake_peer.cli import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
