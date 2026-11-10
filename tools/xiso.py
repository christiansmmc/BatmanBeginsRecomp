"""Reads original Xbox disc images (XDVDFS). Opens them read-only and never writes to them."""
from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# where the game partition starts: redump dump of a retail disc, raw xiso, then the later disc layouts
PARTITION_OFFSETS = (0x18300000, 0, 0x0FD90000, 0x02080000)
DESCRIPTOR_SECTOR = 32
ATTR_DIRECTORY = 0x10
NO_CHILD = 0
PADDING = 0xFFFF
ENTRY_HEADER = struct.Struct("<HHIIBB")
CHUNK = 1024 * 1024


@dataclass(frozen=True)
class Entry:
    path: str
    sector: int
    size: int


def find_partition(f: BinaryIO) -> int:
    for offset in PARTITION_OFFSETS:
        f.seek(offset + DESCRIPTOR_SECTOR * SECTOR)
        if f.read(len(MAGIC)) == MAGIC:
            return offset
    raise ValueError("this is not an Xbox disc image: no XDVDFS volume at any known offset")


def _safe_name(raw: bytes) -> str:
    name = raw.decode("latin-1")
    if not name or name in (".", "..") or any(c in name for c in '/\\:'):
        raise ValueError(f"unsafe file name on the disc: {name!r}")
    return name


def _read_directory(f: BinaryIO, base: int, sector: int, size: int, prefix: str, seen: set[int]) -> list[Entry]:
    if sector in seen:
        raise ValueError(f"directory loop on the disc at sector {sector}")
    seen.add(sector)
    f.seek(base + sector * SECTOR)
    table = f.read(size)
    entries: list[Entry] = []
    visited: set[int] = set()
    pending = [0]
    while pending:
        offset = pending.pop()
        if offset in visited or offset + ENTRY_HEADER.size > len(table):
            continue
        visited.add(offset)
        left, right, start, length, attributes, name_length = ENTRY_HEADER.unpack_from(table, offset)
        if left == PADDING and right == PADDING:
            continue
        name_start = offset + ENTRY_HEADER.size
        path = f"{prefix}/{_safe_name(table[name_start:name_start + name_length])}"
        for child in (left, right):
            if child not in (NO_CHILD, PADDING):
                pending.append(child * 4)
        if attributes & ATTR_DIRECTORY:
            if length:
                entries.extend(_read_directory(f, base, start, length, path, seen))
        else:
            entries.append(Entry(path, start, length))
    return entries


def list_files(f: BinaryIO, base: int) -> list[Entry]:
    f.seek(base + DESCRIPTOR_SECTOR * SECTOR)
    descriptor = f.read(0x1C)
    if descriptor[:len(MAGIC)] != MAGIC:
        raise ValueError(f"no XDVDFS volume at offset {base:#x}")
    root_sector, root_size = struct.unpack_from("<II", descriptor, 0x14)
    return _read_directory(f, base, root_sector, root_size, "", set())


def _copy_range(f: BinaryIO, start: int, size: int, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    f.seek(start)
    with target.open("wb") as out:
        remaining = size
        while remaining:
            data = f.read(min(CHUNK, remaining))
            if not data:
                raise ValueError(f"the image ends before the end of {target.name}")
            out.write(data)
            remaining -= len(data)


def extract(iso: Path, out: Path, expect_xbe_sha1: str | None = None) -> list[Entry]:
    """Extracts every file. `out` only appears once all of them are there and the XBE was accepted."""
    if out.exists() and any(out.iterdir()):
        raise FileExistsError(f"{out} already has files")
    with iso.open("rb") as f:
        base = find_partition(f)
        entries = list_files(f, base)
        if expect_xbe_sha1 is not None:
            xbe = next((e for e in entries if e.path.lower() == "/default.xbe"), None)
            if xbe is None:
                raise ValueError("the disc has no default.xbe")
            f.seek(base + xbe.sector * SECTOR)
            actual = hashlib.sha1(f.read(xbe.size)).hexdigest()
            if actual.lower() != expect_xbe_sha1.lower():
                raise ValueError(f"default.xbe is another release: SHA-1 {actual}")
        partial = out.with_name(out.name + ".partial")
        if partial.exists():
            shutil.rmtree(partial)
        try:
            for entry in entries:
                _copy_range(f, base + entry.sector * SECTOR, entry.size, partial / entry.path.lstrip("/"))
            if out.exists():
                out.rmdir()
            partial.rename(out)
        except BaseException:
            shutil.rmtree(partial, ignore_errors=True)
            raise
    return entries


def export_partition(f: BinaryIO, out: Path) -> None:
    """Writes the game partition alone, the layout xemu mounts. The source is left untouched."""
    if out.exists():
        raise FileExistsError(f"{out} already exists")
    f.seek(find_partition(f))
    partial = out.with_name(out.name + ".partial")
    try:
        with partial.open("wb") as target:
            while data := f.read(CHUNK):
                target.write(data)
        partial.rename(out)
    except BaseException:
        partial.unlink(missing_ok=True)
        raise


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    extract_command = commands.add_parser("extract", help="extract every file of the disc")
    extract_command.add_argument("iso", type=Path)
    extract_command.add_argument("out", type=Path)
    extract_command.add_argument("--expect-xbe-sha1")
    partition_command = commands.add_parser("partition", help="write the game partition alone")
    partition_command.add_argument("iso", type=Path)
    partition_command.add_argument("out", type=Path)
    args = parser.parse_args(argv)
    if args.command == "partition":
        with args.iso.open("rb") as f:
            export_partition(f, args.out)
        print(f"wrote {args.out}, {args.out.stat().st_size} bytes")
        return 0
    entries = extract(args.iso, args.out, args.expect_xbe_sha1)
    print(f"extracted {len(entries)} files, {sum(e.size for e in entries)} bytes, into {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
