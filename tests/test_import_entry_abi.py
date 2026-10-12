#!/usr/bin/env python3
"""Check actual AArch64 veneer object bytes, relocations and unwind rows.

This is a deliberately small instruction/CFI interpreter, not an AArch64 CPU or
a libc/Vulkan forwarding test. No Frame files or native processes are opened.
The dispatcher is an adversarial AAPCS64 model; actual runtime dispatchers,
provider forwarding, cancellation and native execution remain unverified.
"""
import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import random
import shlex
import shutil
import struct
import subprocess
import tempfile


class VerificationError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise VerificationError(message)


def bounded(data, at, size):
    require(at >= 0 and size >= 0 and at <= len(data) and size <= len(data) - at,
            "ELF/CFI field outside its owned bytes")
    return data[at:at + size]


def unpack(fmt, data, at):
    return struct.unpack(fmt, bounded(data, at, struct.calcsize(fmt)))


def string(data, at):
    require(0 <= at < len(data), "string offset outside table")
    end = data.find(b"\0", at)
    require(end >= 0, "unterminated ELF string")
    return data[at:end].decode("ascii")


@dataclass(frozen=True)
class Section:
    index: int
    name: str
    kind: int
    flags: int
    offset: int
    size: int
    link: int
    info: int
    entry_size: int


@dataclass(frozen=True)
class Symbol:
    index: int
    name: str
    binding: int
    kind: int
    visibility: int
    section: int
    value: int
    size: int
    table_offset: int


@dataclass(frozen=True)
class Relocation:
    offset: int
    kind: int
    symbol: int
    addend: int
    table_offset: int


class Object:
    def __init__(self, data):
        self.data = data
        header = unpack("<16sHHIQQQIHHHHHH", data, 0)
        ident, kind, machine, version = header[:4]
        require(ident[:7] == b"\x7fELF\x02\x01\x01" and kind == 1 and
                machine == 183 and version == 1, "not an ELF64 LE AArch64 object")
        section_at, section_size, count, names_index = header[6], header[11], header[12], header[13]
        require(section_size == 64 and 0 < count < 4096 and names_index < count,
                "unsupported ELF section table")
        rows = [unpack("<IIQQQQIIQQ", data, section_at + 64 * i) for i in range(count)]
        names = bounded(data, rows[names_index][4], rows[names_index][5])
        self.sections = [Section(i, string(names, row[0]), row[1], row[2], row[4],
                                 row[5], row[6], row[7], row[9]) for i, row in enumerate(rows)]
        table = self.section(".symtab")
        require(table.kind == 2 and table.entry_size == 24 and table.size % 24 == 0,
                "unsupported symbol table")
        strings = self.section_data(self.sections[table.link])
        self.symbols = []
        for i in range(table.size // 24):
            at = table.offset + i * 24
            name, info, other, section, value, size = unpack("<IBBHQQ", data, at)
            self.symbols.append(Symbol(i, string(strings, name), info >> 4, info & 15,
                                       other & 3, section, value, size, at))

    def section(self, name):
        found = [section for section in self.sections if section.name == name]
        require(len(found) == 1, "missing/duplicate " + name)
        return found[0]

    def section_data(self, section):
        return bounded(self.data, section.offset, section.size)

    def relocations(self, section):
        tables = [table for table in self.sections if table.kind == 4 and table.info == section.index]
        require(len(tables) == 1, "missing/duplicate relocations for " + section.name)
        table = tables[0]
        require(table.entry_size == 24 and table.size % 24 == 0 and
                table.link == self.section(".symtab").index, "invalid relocation table")
        result = []
        for i in range(table.size // 24):
            at = table.offset + 24 * i
            offset, info, addend = unpack("<QQq", self.data, at)
            require(info >> 32 < len(self.symbols), "invalid relocation symbol")
            result.append(Relocation(offset, info & 0xffffffff, info >> 32, addend, at))
        return result


ENTRIES = ("lock", "unlock", "write", "image", "bind", "view", "init", "destroy", "close", "read", "connect")
PUBLIC_ALIASES = {}
MASK64 = (1 << 64) - 1
CALLEE = tuple(range(19, 31))


def leb(data, at, signed=False):
    value, shift = 0, 0
    for _ in range(10):
        byte = bounded(data, at, 1)[0]
        at += 1
        value |= (byte & 0x7f) << shift
        shift += 7
        if not byte & 0x80:
            if signed and byte & 0x40:
                value -= 1 << shift
            return value, at
    raise VerificationError("unbounded CFI LEB integer")


@dataclass(frozen=True)
class Rules:
    cfa_register: int
    cfa_offset: int
    registers: dict


def cfi_rows(data, start, code_align, data_align, initial):
    """Interpret only the bounded CFI language emitted by these veneers."""
    pc, at = start, 0
    register, offset, rules = initial.cfa_register, initial.cfa_offset, dict(initial.registers)
    rows = [(pc, Rules(register, offset, dict(rules)))]
    while at < len(data):
        op = data[at]
        at += 1
        primary = op & 0xc0
        if primary == 0x40:
            pc += (op & 0x3f) * code_align
        elif primary == 0x80:
            amount, at = leb(data, at)
            rules[op & 0x3f] = ("offset", amount * data_align)
        elif primary == 0xc0:
            restored = op & 0x3f
            if restored in initial.registers:
                rules[restored] = initial.registers[restored]
            else:
                rules.pop(restored, None)
        elif op == 0:
            continue
        elif op in (2, 3, 4):
            size = {2: 1, 3: 2, 4: 4}[op]
            pc += int.from_bytes(bounded(data, at, size), "little") * code_align
            at += size
        elif op == 0x0c:
            register, at = leb(data, at)
            offset, at = leb(data, at)
        elif op == 0x0d:
            register, at = leb(data, at)
        elif op == 0x0e:
            offset, at = leb(data, at)
        elif op in (0x06, 0x07, 0x08):
            target, at = leb(data, at)
            if op == 0x06:
                if target in initial.registers:
                    rules[target] = initial.registers[target]
                else:
                    rules.pop(target, None)
            else:
                rules[target] = ("undefined" if op == 7 else "same", 0)
        else:
            raise VerificationError(f"unsupported CFI opcode {op:#x}")
        rows.append((pc, Rules(register, offset, dict(rules))))
    return rows


def unwind_tables(obj):
    section = obj.section(".eh_frame")
    data = obj.section_data(section)
    relocs = {entry.offset: entry for entry in obj.relocations(section)}
    require(len(relocs) == len(ENTRIES), "unexpected unwind relocations")
    at, cies, frames = 0, {}, {}
    while at < len(data):
        length = unpack("<I", data, at)[0]
        if length == 0:
            require(not any(data[at:]), "nonzero bytes after CFI terminator")
            break
        end = at + 4 + length
        require(length < 4096 and end <= len(data), "invalid CFI record")
        ident = unpack("<I", data, at + 4)[0]
        body = at + 8
        if ident == 0:
            require(data[body] == 1, "unsupported CIE version")
            body += 1
            augmentation = string(data, body)
            body += len(augmentation) + 1
            require(augmentation == "zR", "unsupported CIE augmentation")
            code_align, body = leb(data, body)
            data_align, body = leb(data, body, signed=True)
            return_register, body = leb(data, body)
            augmentation_size, body = leb(data, body)
            require(code_align == 4 and data_align == -8 and return_register == 30 and
                    augmentation_size == 1 and data[body] == 0x1b,
                    "unexpected AArch64 CIE contract")
            body += 1
            rows = cfi_rows(data[body:end], 0, code_align, data_align, Rules(31, 0, {}))
            require(all(pc == 0 for pc, _ in rows), "CIE unexpectedly changes location")
            cies[at] = (code_align, data_align, rows[-1][1])
        else:
            cie_at = at + 4 - ident
            require(cie_at in cies and body in relocs, "FDE missing CIE/PC relocation")
            relocation = relocs[body]
            symbol = obj.symbols[relocation.symbol]
            require(relocation.kind == 261 and symbol.section == obj.section(".text").index,
                    "unexpected FDE relocation")
            start = symbol.value + relocation.addend
            require(unpack("<i", data, body)[0] == 0, "unexpected FDE PC addend")
            size = unpack("<I", data, body + 4)[0]
            body += 8
            augmentation_size, body = leb(data, body)
            require(augmentation_size == 0 and start not in frames, "unsupported/duplicate FDE")
            code_align, data_align, initial = cies[cie_at]
            rows = cfi_rows(data[body:end], start, code_align, data_align, initial)
            require(all(start <= pc <= start + size for pc, _ in rows), "CFI location outside function")
            frames[start] = (size, rows)
        at = end
    require(len(cies) == 1 and len(frames) == len(ENTRIES), "incomplete unwind coverage")
    return frames


def instructions(data):
    """Decode machine bytes, refusing instructions outside this small veneer ISA."""
    require(len(data) % 4 == 0, "unaligned instruction stream")
    result = []
    for at in range(0, len(data), 4):
        word = unpack("<I", data, at)[0]
        # ADD/SUB immediate, 64-bit only, no flags/shift. Register 31 is SP.
        base = word & 0xffc00000
        if base in (0x91000000, 0xd1000000):
            result.append(("add" if base == 0x91000000 else "sub",
                           word & 31, (word >> 5) & 31, (word >> 10) & 4095))
        # STP/LDP 64-bit signed-offset addressing; reject writeback modes.
        elif word & 0xffc00000 in (0xa9000000, 0xa9400000):
            offset = (word >> 15) & 127
            if offset & 64:
                offset -= 128
            result.append(("ldp" if word & 0x00400000 else "stp", word & 31,
                           (word >> 10) & 31, (word >> 5) & 31, offset * 8))
        # STR/LDR 64-bit unsigned-offset addressing, with no writeback.
        elif word & 0xffc00000 in (0xf9000000, 0xf9400000):
            result.append(("ldr" if word & 0x00400000 else "str", word & 31,
                           (word >> 5) & 31, ((word >> 10) & 4095) * 8))
        elif word & 0xfc000000 == 0x94000000:
            require(word == 0x94000000, "unexpected resolved branch/call addend")
            result.append(("bl",))
        elif word == 0xd65f03c0:
            result.append(("ret",))
        else:
            raise VerificationError(f"instruction {word:#010x} outside audited subset at +{at:#x}")
    return result


class Machine:
    def __init__(self, rng, native_result=None):
        self.original = [rng.getrandbits(64) for _ in range(31)]
        self.x = list(self.original)
        self.original_sp = (0x100000 + rng.randrange(1, 4096) * 16)
        self.sp = self.original_sp
        self.memory = {}
        self.v = [rng.getrandbits(128) for _ in range(32)]
        self.original_v = list(self.v)
        self.native_result = rng.getrandbits(64) if native_result is None else native_result
        self.calls = 0

    def get(self, reg):
        return self.sp if reg == 31 else self.x[reg]

    def put(self, reg, value):
        if reg == 31:
            self.sp = value & MASK64
            require(self.sp % 16 == 0, "SP alignment changed")
        else:
            self.x[reg] = value & MASK64

    def store(self, at, value):
        require(at % 8 == 0 and self.sp <= at and at + 8 <= self.original_sp,
                "store outside owned active stack")
        self.memory[at] = value

    def load(self, at):
        require(at % 8 == 0 and self.sp <= at and at + 8 <= self.original_sp and
                at in self.memory, "read outside initialized active stack")
        return self.memory[at]

    def check_cfi(self, rules):
        cfa = self.get(rules.cfa_register) + rules.cfa_offset
        require(cfa == self.original_sp, "CFI computes wrong caller SP")
        for reg in CALLEE:
            method, offset = rules.registers.get(reg, ("same", 0))
            require(method != "undefined", f"CFI discards x{reg}")
            value = self.load(cfa + offset) if method == "offset" else self.x[reg]
            require(value == self.original[reg], f"CFI recovers wrong x{reg}")

    def dispatch(self, rng, return_pc):
        require(self.sp == self.original_sp - 256 and self.x[0] == self.sp,
                "dispatcher did not receive true Entry pointer/aligned frame")
        captured = [self.load(self.x[0] + i * 8) for i in range(32)]
        require(captured == self.original + [self.original_sp],
                "Entry differs from true x0..x30/SP before dispatcher prologue")
        require(self.calls == 0, "multiple dispatcher calls")
        self.calls += 1
        # Linux AAPCS64 permits x18 to be caller-saved; the veneer promises to
        # restore the original platform register nevertheless. x30 is the BL LR.
        for reg in range(19):
            self.x[reg] = rng.getrandbits(64)
        self.x[30] = return_pc
        for reg in range(32):
            low = self.v[reg] & MASK64
            self.v[reg] = rng.getrandbits(128)
            if 8 <= reg <= 15:
                self.v[reg] = (self.v[reg] & ~MASK64) | low
        self.x[0] = self.native_result

    def finish(self):
        require(self.calls == 1 and self.sp == self.original_sp,
                "dispatcher count/caller SP changed")
        require(self.x[0] == self.native_result, "native integer result overwritten")
        require(self.x[1:] == self.original[1:], "saved integer register/return changed")
        require(all((self.v[i] & MASK64) == (self.original_v[i] & MASK64) for i in range(8, 16)),
                "AAPCS64 callee-saved vector lanes changed")


def run_function(code, start, rows, rng, native_result=None):
    ops = instructions(code)
    machine = Machine(rng, native_result)
    row = 0
    for i, op in enumerate(ops):
        pc = start + i * 4
        while row + 1 < len(rows) and rows[row + 1][0] <= pc:
            row += 1
        machine.check_cfi(rows[row][1])
        if op[0] in ("add", "sub"):
            value = machine.get(op[2]) + (op[3] if op[0] == "add" else -op[3])
            machine.put(op[1], value)
        elif op[0] in ("stp", "ldp"):
            _, first, second, base, offset = op
            require(base == 31 and first != 31 and second != 31, "pair uses unexpected registers")
            at = machine.get(base) + offset
            if op[0] == "stp":
                machine.store(at, machine.get(first))
                machine.store(at + 8, machine.get(second))
            else:
                machine.put(first, machine.load(at))
                machine.put(second, machine.load(at + 8))
        elif op[0] in ("str", "ldr"):
            _, reg, base, offset = op
            require(base == 31 and reg != 31, "scalar uses unexpected registers")
            at = machine.get(base) + offset
            if op[0] == "str":
                machine.store(at, machine.get(reg))
            else:
                machine.put(reg, machine.load(at))
        elif op[0] == "bl":
            machine.dispatch(rng, pc + 4)
        else:
            require(i + 1 == len(ops), "return before end of function")
            machine.finish()
    require(ops[-1] == ("ret",), "missing final return")


def verify(data, iterations=256, seed=0x4652414d45):
    obj = Object(data)
    text = obj.section(".text")
    require(text.flags == 6, "text section is not read-only executable code")
    stack = obj.section(".note.GNU-stack")
    require(stack.flags == 0 and stack.size == 0, "executable/nonempty GNU stack note")
    code = obj.section_data(text)
    symbols = {symbol.name: symbol for symbol in obj.symbols if symbol.binding != 0}
    wanted = {f"frame_import_{name}_{suffix}" for name in ENTRIES for suffix in ("entry", "dispatch")}
    wanted.update(PUBLIC_ALIASES)
    require(set(symbols) == wanted, "unexpected externally bound API/hook symbol")
    for public, hidden in PUBLIC_ALIASES.items():
        alias, original = symbols[public], symbols[hidden]
        require(alias.binding == 1 and alias.kind == 2 and alias.visibility == 0 and
                (alias.section, alias.value, alias.size) == (original.section, original.value, original.size),
                "public mutex alias does not share the exact true-entry PC/extent")
    relocations = obj.relocations(text)
    require(len(relocations) == len(ENTRIES), "unexpected text relocation count")
    unwind = unwind_tables(obj)
    covered = 0
    rng = random.Random(seed)
    for name in ENTRIES:
        entry = symbols[f"frame_import_{name}_entry"]
        dispatch = symbols[f"frame_import_{name}_dispatch"]
        require(entry.binding == 1 and entry.kind == 2 and entry.visibility == 2 and
                entry.section == text.index and entry.value == covered and 0 < entry.size < 1024,
                "entry symbol is not the expected hidden function")
        require(dispatch.binding == 1 and dispatch.section == 0 and dispatch.size == 0,
                "dispatcher unexpectedly defined/exported")
        function = bounded(code, entry.value, entry.size)
        ops = instructions(function)
        calls = [entry.value + i * 4 for i, op in enumerate(ops) if op == ("bl",)]
        matching = [reloc for reloc in relocations if entry.value <= reloc.offset < entry.value + entry.size]
        require(len(calls) == 1 and len(matching) == 1 and matching[0].kind == 283 and
                matching[0].offset == calls[0] and matching[0].addend == 0 and
                matching[0].symbol == dispatch.index, "call targets wrong dispatcher/relocation")
        require(entry.value in unwind and unwind[entry.value][0] == entry.size,
                "FDE differs from function extent")
        # Include native ssize_t/VkResult boundary representations rather than
        # relying on random values alone to cover zero and negative results.
        results = (0, 1, MASK64, 0xffffffff, 0x80000000, 1 << 63)
        for iteration in range(iterations):
            result = results[iteration] if iteration < len(results) else None
            run_function(function, entry.value, unwind[entry.value][1], rng, result)
        covered += entry.size
    require(covered == len(code), "unaudited text bytes outside veneers")
    return obj


def negative_controls(data):
    obj = Object(data)
    text = obj.section(".text")
    controls = []

    def instruction(label, relative, word):
        changed = bytearray(data)
        struct.pack_into("<I", changed, text.offset + relative, word)
        controls.append((label, changed))

    # Patches modify actual assembled instructions, not the source text. Decode
    # offsets are found from their bytes so no source-layout regex can pass them.
    code = obj.section_data(text)
    first_name = "frame_import_" + ENTRIES[0] + "_entry"
    ops = instructions(code[:next(s.size for s in obj.symbols if s.name == first_name)])
    at = lambda target: next(i * 4 for i, op in enumerate(ops) if op == target)
    instruction("frame allocation 240 instead of 256", 0, 0xd103c3ff)
    first_store = at(("stp", 0, 1, 31, 0))
    instruction("wrong captured register slot", first_store,
                unpack("<I", code, first_store)[0] | (1 << 15))
    instruction("entry clobbered before capture", first_store, 0x91000400)
    result_load = at(("ldr", 1, 31, 8))
    instruction("native result restored from wrong saved slot", result_load,
                unpack("<I", code, result_load)[0] & ~31)
    platform_load = at(("ldp", 18, 19, 31, 144))
    instruction("platform register not restored", platform_load,
                (unpack("<I", code, platform_load)[0] & ~31) | 17)
    lr_load = at(("ldr", 30, 31, 240))
    instruction("return LR loaded from wrong slot", lr_load,
                unpack("<I", code, lr_load)[0] - (1 << 10))
    sp_save = at(("add", 9, 31, 256))
    instruction("wrong captured original SP", sp_save,
                unpack("<I", code, sp_save)[0] - (16 << 10))
    call = at(("bl",))
    changed = bytearray(data)
    a, b = text.offset + call, text.offset + call - 4
    changed[a:a + 4], changed[b:b + 4] = changed[b:b + 4], changed[a:a + 4]
    reloc = next(entry for entry in obj.relocations(text) if entry.offset == call)
    # Preserve a syntactically valid CALL26 relocation at the moved instruction;
    # the machine interpreter must catch the premature callback itself.
    struct.pack_into("<Q", changed, reloc.table_offset, call - 4)
    controls.append(("dispatcher invoked before Entry argument", changed))
    changed = bytearray(data)
    wrong = next(symbol for symbol in obj.symbols if symbol.name == "frame_import_" + ENTRIES[-1] + "_dispatch")
    struct.pack_into("<Q", changed, reloc.table_offset + 8, (wrong.index << 32) | reloc.kind)
    controls.append(("wrong dispatcher relocation", changed))
    changed = bytearray(data)
    symbol = next(symbol for symbol in obj.symbols if symbol.name == first_name)
    changed[symbol.table_offset + 5] = 0
    controls.append(("public entry export", changed))
    for public in PUBLIC_ALIASES:
        alias = next(symbol for symbol in obj.symbols if symbol.name == public)
        changed = bytearray(data)
        struct.pack_into("<Q", changed, alias.table_offset + 8, alias.value + 4)
        controls.append((public + " alias changes source entry PC", changed))
        changed = bytearray(data)
        changed[alias.table_offset + 5] = 2
        controls.append((public + " alias unexpectedly hidden", changed))
    cfi = obj.section(".eh_frame")
    cfi_data = obj.section_data(cfi)
    position = cfi_data.find(bytes((0x80 | 19, 13)))
    require(position >= 0, "cannot locate actual x19 CFI offset for negative control")
    changed = bytearray(data)
    changed[cfi.offset + position + 1] = 12
    controls.append(("incorrect x19 unwind offset", changed))
    # Retiring the CFA while saved-register rules still refer below SP is a real
    # bug even when those bytes happen to retain the original value. AArch64 does
    # not promise a red zone below SP. Poisoning any restore tests this boundary.
    restores = [i for i, byte in enumerate(cfi_data) if byte == (0xc0 | 30)]
    if restores:
        changed = bytearray(data)
        changed[cfi.offset + restores[0]] = 0
        controls.append(("LR unwind restore omitted before frame retirement", changed))
    else:
        # Accept an explicit DW_CFA_same_value alternative to .cfi_restore.
        position = cfi_data.find(bytes((0x08, 30)))
        require(position >= 0, "no LR unwind retirement rule available for negative control")
        changed = bytearray(data)
        changed[cfi.offset + position:cfi.offset + position + 2] = b"\0\0"
        controls.append(("LR same-value unwind retirement omitted", changed))
    results = []
    for label, changed in controls:
        try:
            verify(bytes(changed), iterations=4)
        except VerificationError as failure:
            results.append({"mutation": label, "rejected": True, "reason": str(failure)})
        else:
            raise VerificationError("negative control accepted: " + label)
    return results


def main():
    global ENTRIES, PUBLIC_ALIASES
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path,
                        default=Path(__file__).resolve().parents[1] / "import/native_import_entry.S")
    parser.add_argument("--object", type=Path, help="verify an already assembled AArch64 ELF object")
    parser.add_argument("--compiler", help="assembler driver command; default AARCH64_CC/aarch64-linux-gnu-gcc")
    parser.add_argument("--iterations", type=int, default=256)
    parser.add_argument("--record", type=Path, help="retain a small JSON verification record")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--startup", action="store_true",
                        help="check the two startup-only entries from the same shared assembler macro")
    modes.add_argument("--mutex", action="store_true",
                       help="check the two mutex entries and exact public aliases from the same macro")
    args = parser.parse_args()
    if args.startup:
        ENTRIES = ("vr_init", "vr_shutdown")
    if args.mutex:
        ENTRIES = ("init", "destroy")
        PUBLIC_ALIASES = {"pthread_mutex_init": "frame_import_init_entry",
                          "pthread_mutex_destroy": "frame_import_destroy_entry"}
    require(1 <= args.iterations <= 4096, "iterations outside bounded fixture range")
    command = shlex.split(args.compiler or os.environ.get("AARCH64_CC", "aarch64-linux-gnu-gcc"))
    direct_as = bool(command) and Path(command[0]).name.endswith("-as")
    if not args.compiler and not os.environ.get("AARCH64_CC") and not shutil.which(command[0]):
        # The fixture contains GNU assembler macros, not C/C++ input. CI needs
        # only the small cross-binutils package; the architecture guard is the
        # sole CPP directive and is checked before direct assembly below.
        if shutil.which("aarch64-linux-gnu-as"):
            command = ["aarch64-linux-gnu-as"]
            direct_as = True
    if not args.object and (not command or not shutil.which(command[0])):
        print("AArch64 entry ABI: SKIP; cross assembler unavailable, no native-execution claim")
        return 77
    with tempfile.TemporaryDirectory(prefix="frame-import-entry-abi-") as directory:
        private = Path(directory)
        private.chmod(0o700)
        source_sha = None
        if args.object:
            obj_data = args.object.read_bytes()
        else:
            source_sha = hashlib.sha256(args.source.read_bytes()).hexdigest()
            output = private / "entry.o"
            if direct_as:
                directives = [line.strip() for line in args.source.read_text().splitlines()
                              if line.lstrip().startswith("#")]
                require(directives == ['#if !defined(__aarch64__)',
                                       '#error "This entry ABI is AArch64-only"', '#endif'],
                        "direct assembly requires an otherwise CPP-free source")
            options = ["-g"] if direct_as else ["-c", "-g"]
            if args.startup or args.mutex:
                define = "FRAME_STARTUP_ENTRY_BUILD=1" if args.startup else "FRAME_MUTEX_ENTRY_BUILD=1"
                options += (["--defsym", define] if direct_as else ["-Wa,--defsym," + define])
            subprocess.run(command + options + [str(args.source), "-o", str(output)],
                           check=True, timeout=30, capture_output=True)
            obj_data = output.read_bytes()
        verify(obj_data, iterations=args.iterations)
        controls = negative_controls(obj_data)
        record = {
            "source_sha256": source_sha,
            "object_sha256": hashlib.sha256(obj_data).hexdigest(),
            "entry_functions": len(ENTRIES),
            "randomized_machine_byte_executions": len(ENTRIES) * args.iterations,
            "negative_controls": controls,
            "elf_hidden_entries": True,
            "public_mutex_aliases": sorted(PUBLIC_ALIASES),
            "dispatch_relocations": True,
            "active_stack_cfi_each_instruction": True,
            "dispatcher_model": "AAPCS64 legal caller-saved clobbers; low d8..d15 preserved",
            "real_aarch64_execution": False,
            "real_dispatcher_provider_forwarding": False,
            "native_cancellation_forced_unwind_tested": False,
            "hardware_tested": False,
        }
        if args.record:
            args.record.parent.mkdir(parents=True, exist_ok=True)
            args.record.write_text(json.dumps(record, indent=2) + "\n")
            args.record.chmod(0o600)
        print(f"AArch64 entry ABI: {len(ENTRIES) * args.iterations} machine-byte model executions, "
              f"{len(controls)} rejected object mutations; CFI checked at each instruction; "
              "no real AArch64/forwarding/cancellation/hardware execution")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except VerificationError as failure:
        raise SystemExit("AArch64 entry ABI verification failed: " + str(failure))
