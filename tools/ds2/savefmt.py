"""DS2 save container (decrypt only, offline, works on copies). Layout, from the loader at DS2.exe 0x14215de20:
32-byte header (magic 49f60ec12db5fba9, zeros, 1ef051da4dd38bf0, u32 seed, zeros), then everything is XORed in 16-byte
blocks with one 16-byte key = MurmurHash3-x64-128-like hash (seed 42) of the 16-byte block {u32 seed, constant[4:16]}
(the constant is the 16 bytes at DS2.exe 0x143466d50). Decrypted: a 16-byte index {u16 0, u16 count, u32 extra, ...},
count u32 chunk sizes, then the chunks."""
import struct

HEADER_BYTES = 32
SEED_OFFSET = 24
KEY_CONSTANT = bytes.fromhex("463fbbbc7b6f57ed9b417d36e32612bc")
MURMUR_SEED = 42
MASK = (1 << 64) - 1
C1, C2 = 0x87C37B91114253D5, 0x4CF5AD432745937F
BLOCK = 16


def _rol(x, r):
    return ((x << r) | (x >> (64 - r))) & MASK


def _fmix(k):
    k ^= k >> 33
    k = k * 0xFF51AFD7ED558CCD & MASK
    k ^= k >> 33
    k = k * 0xC4CEB9FE1A85EC53 & MASK
    return k ^ (k >> 33)


def murmur128(data, seed=MURMUR_SEED):
    h1 = h2 = seed
    for i in range(0, len(data) - BLOCK + 1, BLOCK):
        k1, k2 = struct.unpack_from("<QQ", data, i)
        h1 = ((_rol(h1 ^ (_rol(k1 * C1 & MASK, 31) * C2 & MASK), 27) + h2) * 5 + 0x52DCE729) & MASK
        h2 = ((_rol(h2 ^ (_rol(k2 * C2 & MASK, 33) * C1 & MASK), 31) + h1) * 5 + 0x38495AB5) & MASK
    h1 ^= len(data)
    h2 ^= len(data)
    h1 = (h1 + h2) & MASK
    h2 = (h2 + h1) & MASK
    h1, h2 = _fmix(h1), _fmix(h2)
    h1 = (h1 + h2) & MASK
    h2 = (h2 + h1) & MASK
    return struct.pack("<QQ", h1, h2)


def key_for(seed):
    return murmur128(struct.pack("<I", seed) + KEY_CONSTANT[4:])


def xor_cycled(data, key):
    """XOR with the key repeated from the start of `data`, a trailing partial block with the key's prefix (the game
    decrypts each segment, rounded up to whole blocks, from its own start)."""
    stream = (key * (len(data) // BLOCK + 1))[:len(data)]
    return bytes(a ^ b for a, b in zip(data, stream))


def seed_of(save):
    return struct.unpack_from("<I", save, SEED_OFFSET)[0]


def segments(save):
    """The save's decrypted segments: the 16-byte index, the size table (count u32), then each chunk (still compressed)."""
    key = key_for(seed_of(save))
    body = save[HEADER_BYTES:]
    index = xor_cycled(body[:BLOCK], key)
    count = struct.unpack_from("<H", index, 2)[0]
    table_end = BLOCK + 4 * count
    sizes = struct.unpack_from(f"<{count}I", xor_cycled(body[BLOCK:table_end], key))
    chunks, at = [], table_end
    for size in sizes:
        chunks.append(xor_cycled(body[at:at + size], key))
        at += size
    return index, sizes, chunks


def assemble(header, index, sizes, chunks):
    """The inverse of `segments`: the save file from its 32-byte header and decrypted segments (encrypt is the same XOR)."""
    key = key_for(struct.unpack_from("<I", header, SEED_OFFSET)[0])
    table = struct.pack(f"<{len(sizes)}I", *sizes)
    return header + b"".join(xor_cycled(part, key) for part in (index, table, *chunks))


def lz4_block(src):
    """Decodes one LZ4 block (the game's decompressor at DS2.exe 0x14206b9b0 is LZ4_decompress_safe); the output size is
    not stored, so it runs to the end of `src`. Raises ValueError when the stream is malformed."""
    out = bytearray()
    i = 0
    while i < len(src):
        token = src[i]
        i += 1
        literals = token >> 4
        if literals == 15:
            while True:
                extra = src[i]
                i += 1
                literals += extra
                if extra != 255:
                    break
        out += src[i:i + literals]
        i += literals
        if i >= len(src):
            break
        offset = src[i] | src[i + 1] << 8
        i += 2
        length = token & 15
        if length == 15:
            while True:
                extra = src[i]
                i += 1
                length += extra
                if extra != 255:
                    break
        length += 4
        if offset == 0 or offset > len(out):
            raise ValueError("bad match offset")
        for _ in range(length):
            out.append(out[-offset])
    return bytes(out)
