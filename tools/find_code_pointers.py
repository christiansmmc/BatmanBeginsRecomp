"""Finds code the recompiler missed: pointers in the data sections that aim at the start of a function it does not know.
Prints [functions] entries for config/game.toml. A pointer counts when the bytes before its target end a function
and the target is not inside a known function's instructions at a matching boundary."""
from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA_SECTIONS = (".rdata", ".data")
CODE_SECTION = ".text"
PADDING_BYTE = 0xCC
FUNCTION_END_BYTES = {0xC3, 0x90}
PROLOGUE_STARTS = (b"\x55\x8B\xEC", b"\x56", b"\x53", b"\x57", b"\x51", b"\x83\xEC", b"\x8B\x44\x24", b"\x8B\x4C\x24",
                   b"\xB8", b"\x6A", b"\x68", b"\xD9", b"\xE9", b"\x8B\x01", b"\x8B\x41", b"\x8B\x81", b"\x81\xEC",
                   b"\xC3", b"\xC2", b"\x33\xC0", b"\x32\xC0", b"\xB0")


def load_sections(xbe: bytes) -> dict[str, tuple[int, int, int]]:
    """name -> (virtual address, raw offset, raw size)"""
    u32 = lambda offset: struct.unpack_from("<I", xbe, offset)[0]
    base = u32(0x104)
    sections = {}
    for index in range(u32(0x11C)):
        header = u32(0x120) - base + index * 56
        _, va, _, raw, rawsize, name_va = struct.unpack_from("<IIIIII", xbe, header)
        name_offset = name_va - base
        sections[xbe[name_offset:xbe.index(b"\0", name_offset)].decode()] = (va, raw, rawsize)
    return sections


def known_functions(mapping_source: str) -> set[int]:
    """The generated x86_func_mapping.cpp lists each function as sub_XXXXXXXX."""
    return {int(found, 16) for found in re.findall(r"sub_([0-9A-Fa-f]{8})", mapping_source)}


def looks_like_function_start(code: bytes, offset: int) -> bool:
    if offset == 0 or offset + 3 > len(code):
        return False
    previous = code[offset - 1]
    if previous == PADDING_BYTE:  # compilers pad between functions, so what follows padding starts one
        return True
    ends_before = previous in FUNCTION_END_BYTES or (offset >= 3 and code[offset - 3] == 0xC2)
    return ends_before and code[offset:].startswith(PROLOGUE_STARTS)


def missed_code_pointers(data: bytes, code: bytes, code_va: int, known: set[int]) -> list[int]:
    """Dword-aligned values in data that aim at an unknown function start in code."""
    found = set()
    for offset in range(0, len(data) - 3, 4):
        value = struct.unpack_from("<I", data, offset)[0]
        if value in known or not code_va <= value < code_va + len(code):
            continue
        if looks_like_function_start(code, value - code_va):
            found.add(value)
    return sorted(found)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xbe", type=Path, default=ROOT / "assets" / "default.xbe")
    parser.add_argument("--mapping", type=Path, default=ROOT / "generated" / "game" / "x86_func_mapping.cpp")
    args = parser.parse_args()

    xbe = args.xbe.read_bytes()
    sections = load_sections(xbe)
    code_va, code_raw, code_size = sections[CODE_SECTION]
    code = xbe[code_raw:code_raw + code_size]
    known = known_functions(args.mapping.read_text(encoding="utf-8"))
    print(f"# known functions: {len(known)}")
    missed: set[int] = set()
    for name in DATA_SECTIONS:
        _, raw, size = sections[name]
        missed.update(missed_code_pointers(xbe[raw:raw + size], code, code_va, known))
    print(f"# missed code pointers: {len(missed)}")
    for address in sorted(missed):
        print(f"0x{address:08X}")


if __name__ == "__main__":
    main()
