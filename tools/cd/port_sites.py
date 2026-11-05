r"""Ports addresses from one Crimson Desert build to another by masked byte signatures.

Usage: python -I port_sites.py OLD_EXE NEW_EXE OLD_EXPORT_DIR SITES_TSV > ported.tsv

SITES_TSV rows: name <tab> old address (hex). Each address is classified with the old export:
  function  a function start in names.tsv: its first bytes, with rip-relative displacements and branch targets masked,
            are searched in the new build's executable sections, growing until the match is unique; then the same with
            every displacement and immediate masked; then the body after the prologue, mapped back through .pdata; then
            the targets of its ported call sites (calls.tsv), by majority vote;
  site      inside a function: the function is ported and the instruction at the same index taken, else the signature
            that starts at the site itself;
  data      in data_xrefs.tsv: the referencing instructions of up to kDataVotes functions are ported by their own
            signatures and their rip-relative targets taken; the result is the majority vote.
Output rows: name, old, new (or -), kind, method, detail. The game folder is only read.
"""
import bisect
import collections
import re
import struct
import sys

import capstone
import pefile

kSignatureLengths = (24, 32, 48, 64, 96, 128, 192, 256)
kMaxFunctionBytes = 0x4000
kDataVotes = 6
kCallerVotes = 6
kFieldReach = 0x1000  # a reference this far below a data address is taken as a field access into it
kExecutable = 0x20000000
kExceptionDirectory = 3
kRuntimeFunctionSize = 12
kPrologueMnemonics = {"push", "sub", "mov", "lea", "vmovaps", "movaps", "vmovdqa", "movdqa"}


class Image:
    def __init__(self, path):
        self.pe = pefile.PE(path, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.data = self.pe.__data__
        self.code = [s for s in self.pe.sections if s.Characteristics & kExecutable and s.SizeOfRawData]

    def function_starts(self):
        """Sorted RUNTIME_FUNCTION begin addresses from .pdata."""
        if not hasattr(self, "_starts"):
            entry = self.pe.OPTIONAL_HEADER.DATA_DIRECTORY[kExceptionDirectory]
            table = self.pe.get_data(entry.VirtualAddress, entry.Size)
            self._starts = sorted(self.base + struct.unpack_from("<I", table, i)[0]
                                  for i in range(0, len(table) - kRuntimeFunctionSize + 1, kRuntimeFunctionSize))
        return self._starts

    def function_containing(self, va):
        starts = self.function_starts()
        i = bisect.bisect_right(starts, va) - 1
        return starts[i] if i >= 0 else None

    def read(self, va, size):
        return self.pe.get_data(va - self.base, size)

    def search(self, pattern):
        hits = []
        for s in self.code:
            start = s.PointerToRawData
            for m in pattern.finditer(self.data, start, start + s.SizeOfRawData):
                hits.append(self.base + s.VirtualAddress + m.start() - start)
                if len(hits) > 1:
                    return hits
        return hits


def disassembler():
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    cs.detail = True
    return cs


def is_relative_branch(insn):
    return insn.group(capstone.CS_GRP_JUMP) or insn.group(capstone.CS_GRP_CALL)


def masked(insn, loose):
    """The instruction's bytes with every build-dependent field set to None; loose also masks every displacement and
    immediate (stack frame offsets and constants that moved between builds)."""
    out = list(insn.bytes)
    rip_relative = any(op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP for op in insn.operands)
    if insn.disp_size and (loose or rip_relative):
        out[insn.disp_offset:insn.disp_offset + insn.disp_size] = [None] * insn.disp_size
    if insn.imm_size and (loose or is_relative_branch(insn) or insn.imm_size == 8):
        out[insn.imm_offset:insn.imm_offset + insn.imm_size] = [None] * insn.imm_size
    return out


def rip_target(insn):
    for op in insn.operands:
        if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
            return insn.address + insn.size + op.mem.disp
    return None


def is_prologue(insn):
    saves_frame = insn.mnemonic in ("push", "sub") or "rsp" in insn.op_str or "rax" in insn.op_str
    return insn.mnemonic in kPrologueMnemonics and saves_frame


def to_regex(chunks):
    return re.compile(b"".join(b"." if b is None else re.escape(bytes([b])) for b in chunks), re.DOTALL)


class Porter:
    def __init__(self, old, new, export_dir):
        self.old, self.new, self.cs = old, new, disassembler()
        self.starts, self.sizes = self._functions(export_dir + "/names.tsv")
        self.data_refs = self._data_refs(export_dir + "/data_xrefs.tsv")
        self.call_sites = self._call_sites(export_dir + "/calls.tsv")
        self.cache = {}

    @staticmethod
    def _functions(path):
        rows = []
        with open(path, encoding="utf-8") as f:
            next(f)
            for line in f:
                cols = line.rstrip("\n").split("\t")
                rows.append((int(cols[0], 16), int(cols[4] or 0)))
        rows.sort()
        return [r[0] for r in rows], [r[1] for r in rows]

    @staticmethod
    def _data_refs(path):
        refs = {}
        with open(path, encoding="utf-8") as f:
            next(f)
            for line in f:
                cols = line.rstrip("\n").split("\t")
                refs[int(cols[0], 16)] = [int(a, 16) for a in cols[5].split(",") if a]
        return refs

    @staticmethod
    def _call_sites(path):
        sites = collections.defaultdict(list)
        with open(path, encoding="utf-8") as f:
            next(f)
            for line in f:
                caller, callee, site = line.rstrip("\n").split("\t")
                if site != caller:
                    sites[int(callee, 16)].append(int(site, 16))
        return sites

    def containing(self, va):
        i = bisect.bisect_right(self.starts, va) - 1
        if i >= 0 and va < self.starts[i] + max(self.sizes[i], 1):
            return self.starts[i], self.sizes[i]
        return None

    def instructions(self, image, va, size):
        return list(self.cs.disasm(image.read(va, min(size, kMaxFunctionBytes)), va))

    def port_function(self, va, size):
        """(new address or None, detail): the strict signature first, then the loose one."""
        if va not in self.cache:
            insns = self.instructions(self.old, va, size or kMaxFunctionBytes)
            new, detail = self.signature_match(insns, loose=False)
            if new is None:
                new, loose_detail = self.signature_match(insns, loose=True)
                detail = f"{detail}; loose: {loose_detail}"
            if new is None:
                new, body_detail = self.body_match(insns)
                detail = f"{detail}; body: {body_detail}"
            if new is None:
                new, caller_detail = self.caller_match(va)
                detail = f"{detail}; callers: {caller_detail}"
            self.cache[va] = (new, detail)
        return self.cache[va]

    def body_match(self, insns):
        """For a changed prologue (other saved registers, frame size): the loose signature of the instructions after
        the prologue, mapped back to the start of the new function holding the match."""
        prologue = 0
        while prologue < len(insns) and is_prologue(insns[prologue]):
            prologue += 1
        if prologue == len(insns):
            return None, "no body"
        hit, detail = self.signature_match(insns[prologue:], loose=True)
        if hit is None:
            return None, detail
        start = self.new.function_containing(hit)
        return start, f"{detail}, {insns[prologue].address - insns[0].address} B into the old function"

    def signature_match(self, insns, loose):
        """Grows the masked signature through kSignatureLengths until it matches once in the new build."""
        chunks = []
        result = (None, "no instructions")
        lengths = iter(kSignatureLengths)
        next_length = next(lengths)
        for insn in insns:
            chunks += masked(insn, loose)
            if len(chunks) < next_length and insn != insns[-1]:
                continue
            next_length = next(lengths, kMaxFunctionBytes)
            hits = self.new.search(to_regex(chunks))
            if not hits:
                return None, f"no match at {len(chunks)} B"
            if len(hits) == 1:
                return hits[0], f"unique at {len(chunks)} B"
            result = (None, f"ambiguous at {len(chunks)} B")
            if len(chunks) >= kSignatureLengths[-1]:
                break
        return result

    def same_index(self, old_fn, size, old_va, new_fn):
        """The new instruction at the index of old_va in old_fn, or None when the instruction sequence differs before it."""
        old = self.instructions(self.old, old_fn, size or kMaxFunctionBytes)
        new = self.instructions(self.new, new_fn, size or kMaxFunctionBytes)
        for a, b in zip(old, new):
            if a.mnemonic != b.mnemonic or a.size != b.size:
                return None
            if a.address == old_va:
                return a, b
        return None

    def local_match(self, va):
        """The new instruction matching the loose signature that starts at the old instruction va, or None."""
        hit, _ = self.signature_match(self.instructions(self.old, va, kSignatureLengths[-1]), loose=True)
        if hit is None:
            return None
        return next(self.cs.disasm(self.new.read(hit, kSignatureLengths[0]), hit), None)

    def caller_match(self, va):
        votes = collections.Counter()
        for site in self.call_sites.get(va, [])[:kCallerVotes]:
            insn = self.local_match(site)
            if insn and insn.mnemonic == "call" and insn.operands and insn.operands[0].type == capstone.x86.X86_OP_IMM:
                votes[insn.operands[0].imm] += 1
            else:
                target = self.same_call_order(site)
                if target is not None:
                    votes[target] += 1
        if not votes:
            return None, f"none of {len(self.call_sites.get(va, []))} call sites ported"
        best, n = votes.most_common(1)[0]
        return best, f"{n}/{sum(votes.values())} call sites agree"

    def direct_calls(self, image, va, size):
        return [i for i in self.instructions(image, va, size or kMaxFunctionBytes)
                if i.mnemonic == "call" and i.operands and i.operands[0].type == capstone.x86.X86_OP_IMM]

    def same_call_order(self, site):
        """The target of the call at the same position in the ported caller, when both callers make as many direct calls
        (the caller is ported by signature only, so this never recurses into caller votes)."""
        fn = self.containing(site)
        if not fn:
            return None
        insns = self.instructions(self.old, fn[0], fn[1] or kMaxFunctionBytes)
        new_fn = self.signature_match(insns, loose=False)[0] or self.signature_match(insns, loose=True)[0] or \
            self.body_match(insns)[0]
        if new_fn is None:
            return None
        old_calls = self.direct_calls(self.old, fn[0], fn[1])
        new_calls = self.direct_calls(self.new, new_fn, fn[1])
        index = next((k for k, c in enumerate(old_calls) if c.address == site), None)
        if index is None or len(old_calls) != len(new_calls):
            return None
        return new_calls[index].operands[0].imm

    def port_site(self, va):
        fn = self.containing(va)
        new_fn, detail = self.port_function(*fn) if fn else (None, "not in a known function")
        pair = self.same_index(fn[0], fn[1], va, new_fn) if new_fn is not None else None
        if pair:
            return pair[1].address, "site", f"function {fn[0]:X} -> {new_fn:X}"
        insn = self.local_match(va)
        if insn:
            return insn.address, "site", "own signature"
        return None, "site", f"function: {detail}; own signature: no unique match"

    def port_data(self, va):
        """Exact references first: a field reach carries the old spacing between globals, which a build can change."""
        votes = self.data_votes(va, exact=True) or self.data_votes(va, exact=False)
        refs = self.data_refs.get(va, [])
        if not votes:
            return None, "data", f"no referencing instruction ported ({min(len(refs), kDataVotes)} functions tried)"
        best, n = votes.most_common(1)[0]
        return best, "data", f"{n}/{sum(votes.values())} votes"

    def data_votes(self, va, exact):
        votes = collections.Counter()
        for ref in self.data_refs.get(va, [])[:kDataVotes]:
            fn = self.containing(ref)
            if not fn:
                continue
            for old in self.instructions(self.old, fn[0], fn[1] or kMaxFunctionBytes):
                t = rip_target(old)
                if t is None or not (t == va if exact else t <= va < t + kFieldReach):
                    continue
                new = self.local_match(old.address)
                if new is not None and rip_target(new) is not None:
                    votes[rip_target(new) + (va - t)] += 1
                break
        return votes

    def port(self, va):
        if va in self.data_refs and not self.containing(va):
            return self.port_data(va)
        i = bisect.bisect_left(self.starts, va)
        if i < len(self.starts) and self.starts[i] == va:
            new, detail = self.port_function(va, self.sizes[i])
            return new, "function", detail
        return self.port_site(va)


def main(argv):
    old_exe, new_exe, export_dir, sites = argv[1:5]
    porter = Porter(Image(old_exe), Image(new_exe), export_dir)
    print("name\told\tnew\tkind\tdetail")
    with open(sites, encoding="utf-8") as f:
        for line in f:
            if not line.strip() or line.startswith("#"):
                continue
            name, old = line.rstrip("\n").split("\t")[:2]
            va = int(old, 16)
            new, kind, detail = porter.port(va)
            print(f"{name}\t0x{va:X}\t{'-' if new is None else f'0x{new:X}'}\t{kind}\t{detail}", flush=True)


if __name__ == "__main__":
    main(sys.argv)
