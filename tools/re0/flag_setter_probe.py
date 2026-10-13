"""Live Frida probe of RE0's story flag setter (run only in a session you intend to play).

usage: python flag_setter_probe.py

Attaches to re0hd.exe and logs every call to the generic flag setter 0x59bd50 (this, id, value), plus whether
this equals sFlagManager (the pointer at global 0xdcc014), and every call to the BGM refresh 0x643490 (its ecx
against the object at 0xdd20a0). Ctrl+C detaches. Addresses are image-base 0x400000 offsets.
"""
import json
import time

import frida

PROCESS_NAME = "re0hd.exe"
SCRIPT = """
const mod = Process.getModuleByName('re0hd.exe');
const rel = (a) => mod.base.add(a - 0x400000);
const manager = () => rel(0xdcc014).readPointer();

Interceptor.attach(rel(0x59bd50), {
    onEnter(args) {
        const self = this.context.ecx;
        send({
            fn: 'setter',
            self: self.toString(),
            managerMatches: self.equals(manager()),
            id: args[0].toUInt32() & 0xffff,
            value: args[1].toUInt32(),
        });
    },
});

Interceptor.attach(rel(0x643490), {
    onEnter() {
        send({
            fn: 'bgm_refresh',
            object: this.context.ecx.toString(),
            objectMatches: this.context.ecx.equals(rel(0xdd20a0).readPointer()),
        });
    },
});
"""


def on_message(message, _data):
    if message["type"] == "send":
        print(json.dumps(message["payload"]), flush=True)
    else:
        print(message, flush=True)


def main():
    session = frida.attach(PROCESS_NAME)
    script = session.create_script(SCRIPT)
    script.on("message", on_message)
    script.load()
    print("attached to %s; Ctrl+C to detach" % PROCESS_NAME, flush=True)
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        session.detach()


if __name__ == "__main__":
    main()
