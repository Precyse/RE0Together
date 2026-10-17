"""Stand-in launcher for the SR2 steam tunnel (docs/SR2_NOTES.md, milestone 10): listens on the adapter port, answers
HELLO with WELCOME (with a local steam id) and one PEER_UP for a fake peer, and handles the game's GAME_DATAGRAMs.
The real launchers are not needed.

Usage: python tools/sr2/fake_launcher.py --role host|guest [--port 27962] [--record FILE] [--replay FILE] [--answer FILE]

--role host    the fake is the host: its slot is --host-slot (0), the game is a guest in the other slot
--role guest   the fake is a guest in the other slot, the game is the host in --host-slot
Every GAME_DATAGRAM the game sends is printed: destination slot, dst address, size, the byte at --message-offset
(the first message id; the offset is the packet header size, set it once a capture shows it) and the first bytes.

Record files hold datagrams as consecutive records: u32 length, then that many bytes of GAME_DATAGRAM payload
(u32 dst, little-endian, then the game's UDP bytes).
--record FILE   appends every datagram the game sends
--replay FILE   sends the file's datagrams to the game as the fake peer once the link is up, and again after each
                datagram the game sends (at most once per --replay-interval seconds)
--answer FILE   sends the file's datagrams when a 20-byte datagram (the LAN ping) arrives
"""
import argparse
import socket
import struct
import threading
import time

import wire

DEFAULT_PORT = 27962
DEFAULT_LOCAL_ID = 76561198000000001
DEFAULT_PEER_ID = 76561198000000002
DEFAULT_MAX_PLAYERS = 4
DEFAULT_EPOCH = 1
PEER_NAME = "fake-peer"
HEARTBEAT_S = 1.0
REPLAY_INTERVAL_S = 1.0
FIRST_BYTES = 12
RECORD_LEN = struct.Struct("<I")


def load_records(path):
    data = open(path, "rb").read()
    records, offset = [], 0
    while offset < len(data):
        (length,) = RECORD_LEN.unpack_from(data, offset)
        offset += RECORD_LEN.size
        records.append(data[offset:offset + length])
        offset += length
    return records


def append_record(path, payload):
    with open(path, "ab") as out:
        out.write(RECORD_LEN.pack(len(payload)) + payload)


class FakeLauncher:
    def __init__(self, args):
        self.args = args
        other_slot = 1 if args.host_slot == 0 else 0
        self.peer_slot = args.host_slot if args.role == "host" else other_slot
        self.local_slot = other_slot if args.role == "host" else args.host_slot
        self.replay = load_records(args.replay) if args.replay else []
        self.answer = load_records(args.answer) if args.answer else []
        self.last_replay = 0.0
        self.send_lock = threading.Lock()

    def send(self, sock, frame):
        with self.send_lock:
            sock.sendall(frame)

    def send_records(self, sock, records):
        for record in records:
            self.send(sock, wire.encode(wire.MSG_GAME_DATAGRAM, 0, self.peer_slot, record))

    def heartbeat_loop(self, sock):
        try:
            while True:
                self.send(sock, wire.encode(wire.MSG_HEARTBEAT, 0, 0))
                time.sleep(HEARTBEAT_S)
        except OSError:
            pass

    def on_hello(self, sock):
        a = self.args
        self.send(sock, wire.welcome(self.local_slot, a.host_slot, a.max_players, a.epoch, a.local_id))
        self.send(sock, wire.peer_up(self.peer_slot, a.peer_id, PEER_NAME))
        print(f"WELCOME local_slot={self.local_slot} host_slot={a.host_slot} local_id={a.local_id}; "
              f"PEER_UP slot={self.peer_slot} id={a.peer_id}", flush=True)
        threading.Thread(target=self.heartbeat_loop, args=(sock,), daemon=True).start()
        self.send_records(sock, self.replay)
        self.last_replay = time.time()

    def on_datagram(self, sock, to_slot, payload):
        dst, data = wire.split_datagram(payload)
        offset = self.args.message_offset
        message = data[offset] if offset < len(data) else None
        print(f"GAME_DATAGRAM to_slot=0x{to_slot:02X} dst={wire.dotted(dst)} size={len(data)} message={message} "
              f"first={data[:FIRST_BYTES].hex(' ')}", flush=True)
        if self.args.record:
            append_record(self.args.record, payload)
        if self.answer and len(data) == wire.PING_BYTES:
            self.send_records(sock, self.answer)
        if self.replay and time.time() - self.last_replay >= self.args.replay_interval:
            self.send_records(sock, self.replay)
            self.last_replay = time.time()

    def serve(self, sock):
        while True:
            msg_type, _flags, slot, payload = wire.read_frame(sock)
            if msg_type == wire.MSG_HELLO:
                self.on_hello(sock)
            elif msg_type == wire.MSG_GAME_DATAGRAM:
                self.on_datagram(sock, slot, payload)

    def run(self):
        listener = socket.socket()
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", self.args.port))
        listener.listen(1)
        print(f"fake launcher on 127.0.0.1:{self.args.port}, role {self.args.role}", flush=True)
        while True:
            sock, _ = listener.accept()
            print("adapter connected", flush=True)
            try:
                self.serve(sock)
            except (ConnectionError, OSError):
                print("adapter disconnected", flush=True)
            finally:
                sock.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--role", choices=("host", "guest"), required=True)
    parser.add_argument("--host-slot", type=int, default=0)
    parser.add_argument("--max-players", type=int, default=DEFAULT_MAX_PLAYERS)
    parser.add_argument("--epoch", type=int, default=DEFAULT_EPOCH)
    parser.add_argument("--local-id", type=int, default=DEFAULT_LOCAL_ID, help="the game's steam id in WELCOME")
    parser.add_argument("--peer-id", type=int, default=DEFAULT_PEER_ID, help="the fake peer's steam id in PEER_UP")
    parser.add_argument("--message-offset", type=int, default=0)
    parser.add_argument("--record")
    parser.add_argument("--replay")
    parser.add_argument("--answer")
    parser.add_argument("--replay-interval", type=float, default=REPLAY_INTERVAL_S)
    try:
        FakeLauncher(parser.parse_args()).run()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
