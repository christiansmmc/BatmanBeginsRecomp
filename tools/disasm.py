"""Development aid: disassembles a range of default.xbe by virtual address, with XDK names from config/game.toml."""
from __future__ import annotations

import argparse
import struct
import tomllib
from pathlib import Path

import capstone

ROOT = Path(__file__).resolve().parent.parent


def load_sections(xbe: bytes) -> list[tuple[str, int, int, int, int]]:
    u32 = lambda offset: struct.unpack_from("<I", xbe, offset)[0]
    base = u32(0x104)
    sections = []
    for index in range(u32(0x11C)):
        header = u32(0x120) - base + index * 56
        _, va, vsize, raw, rawsize, name_va = struct.unpack_from("<IIIIII", xbe, header)
        name_offset = name_va - base
        sections.append((xbe[name_offset:xbe.index(b"\0", name_offset)].decode(), va, vsize, raw, rawsize))
    return sections


def read_va(xbe: bytes, sections: list, va: int, size: int) -> bytes:
    for _, start, _, raw, rawsize in sections:
        if start <= va < start + rawsize:
            return xbe[raw + va - start:raw + va - start + size]
    raise ValueError(f"{va:#x} is not in the image")


def names_from_config(path: Path) -> dict[int, str]:
    config = tomllib.loads(path.read_text(encoding="utf-8"))
    names: dict[int, str] = {}
    for table in ("symbols", "data"):
        for name, value in config.get(table, {}).items():
            if isinstance(value, int) and value:
                names[value] = name
    return names


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", type=lambda text: int(text, 16))
    parser.add_argument("size", type=lambda text: int(text, 0), nargs="?", default=0x60)
    parser.add_argument("--xbe", type=Path, default=ROOT / "assets" / "default.xbe")
    parser.add_argument("--config", type=Path, default=ROOT / "config" / "game.toml")
    args = parser.parse_args()

    xbe = args.xbe.read_bytes()
    sections = load_sections(xbe)
    names = names_from_config(args.config)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for instruction in decoder.disasm(read_va(xbe, sections, args.address, args.size), args.address):
        text = f"{instruction.mnemonic} {instruction.op_str}"
        for address, name in names.items():
            if f"{address:#x}" in text:
                text += f"    ; {name}"
        label = f"{names[instruction.address]}:\n" if instruction.address in names else ""
        print(f"{label}{instruction.address:08X}  {text}")


if __name__ == "__main__":
    main()
