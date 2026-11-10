"""Development aid: reads the files of an Xbox disk image in qcow2, and overwrites them in place.

No qemu-img needed. Writing never allocates: it replaces bytes of clusters a file already has, so the image's refcounts
and the FATX tables stay as they are. A file that does not exist yet has to be created by the game running in xemu.

  ls  <image> E:/UDATA/45410389
  get <image> "E:/UDATA/45410389/13AD93548315/BATMAN A" out.bin
  put <image> "E:/UDATA/45410389/13AD93548315/BATMAN A" in.bin
"""
from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path

# the retail layout
PARTITIONS = {
    "X": (0x00080000, 0x2EE00000),
    "Y": (0x2EE80000, 0x2EE00000),
    "Z": (0x5DC80000, 0x2EE00000),
    "C": (0x8CA80000, 0x1F400000),
    "E": (0xABE80000, 0x1312D6000),
}

L_OFFSET_MASK = 0x00FFFFFFFFFFFE00
COMPRESSED = 1 << 62
ALL_ZERO = 1
SECTOR = 512
FAT_START = 0x1000
FAT_ALIGN = 0x1000
DIRENT = 64
NAME_MAX = 42
DELETED = 0xE5
ATTR_DIR = 0x10


class Qcow2:
    def __init__(self, path: Path, writable: bool = False):
        self.file = open(path, "r+b" if writable else "rb")
        self.writable = writable
        header = self.file.read(72)
        magic, version, _, _, cluster_bits, self.size, crypt, l1_size, l1_offset = struct.unpack(">4sIQIIQIIQ", header[:48])
        if magic != b"QFI\xfb" or version not in (2, 3):
            raise ValueError(f"{path} is not a qcow2 image")
        if crypt:
            raise ValueError(f"{path} is encrypted")
        self.cluster = 1 << cluster_bits
        self.l2_entries = self.cluster // 8
        self.file.seek(l1_offset)
        self.l1 = struct.unpack(f">{l1_size}Q", self.file.read(l1_size * 8))

    def _host_offset(self, offset: int) -> int | None:
        """Where that guest byte lives in the file; None for a hole, which reads as zeros."""
        cluster_index = offset // self.cluster
        l1_entry = self.l1[cluster_index // self.l2_entries] & L_OFFSET_MASK
        if not l1_entry:
            return None
        self.file.seek(l1_entry + (cluster_index % self.l2_entries) * 8)
        entry = struct.unpack(">Q", self.file.read(8))[0]
        if entry & COMPRESSED:
            raise ValueError(f"compressed cluster at {offset:#x} is not handled")
        host = entry & L_OFFSET_MASK
        if not host or entry & ALL_ZERO:
            return None
        return host + offset % self.cluster

    def read(self, offset: int, size: int) -> bytes:
        out = bytearray()
        while size > 0:
            take = min(size, self.cluster - offset % self.cluster)
            host = self._host_offset(offset)
            if host is None:
                out += bytes(take)
            else:
                self.file.seek(host)
                out += self.file.read(take)
            offset += take
            size -= take
        return bytes(out)

    def write(self, offset: int, data: bytes) -> None:
        """Only over clusters the image already holds."""
        if not self.writable:
            raise PermissionError("opened read-only")
        position = 0
        while position < len(data):
            take = min(len(data) - position, self.cluster - offset % self.cluster)
            host = self._host_offset(offset)
            if host is None:
                raise ValueError(f"guest offset {offset:#x} has no cluster in the image")
            self.file.seek(host)
            self.file.write(data[position:position + take])
            offset += take
            position += take
        self.file.flush()

    def close(self) -> None:
        self.file.close()


@dataclass
class Entry:
    name: str
    is_dir: bool
    cluster: int
    size: int
    where: int  # the entry's position on the disk


class Fatx:
    def __init__(self, disk: Qcow2, offset: int, size: int):
        self.disk = disk
        self.offset = offset
        magic, _, sectors_per_cluster, self.root = struct.unpack("<4sIII", disk.read(offset, 16))
        if magic != b"FATX":
            raise ValueError(f"no FATX volume at {offset:#x}")
        self.cluster_size = sectors_per_cluster * SECTOR
        clusters = size // self.cluster_size
        self.entry_size = 2 if clusters < 0xFFF0 else 4
        fat_bytes = (clusters + 1) * self.entry_size
        fat_bytes = (fat_bytes + FAT_ALIGN - 1) // FAT_ALIGN * FAT_ALIGN
        self.data = offset + FAT_START + fat_bytes

    def _next(self, cluster: int) -> int | None:
        raw = self.disk.read(self.offset + FAT_START + cluster * self.entry_size, self.entry_size)
        value = int.from_bytes(raw, "little")
        end = 0xFFF8 if self.entry_size == 2 else 0xFFFFFFF8
        return None if value >= end or value == 0 else value

    def _chain(self, cluster: int) -> list[int]:
        chain = []
        while cluster is not None and len(chain) < 1 << 24:
            chain.append(cluster)
            cluster = self._next(cluster)
        return chain

    def _cluster_offset(self, cluster: int) -> int:
        return self.data + (cluster - 1) * self.cluster_size

    def _entries(self, cluster: int) -> list[Entry]:
        entries = []
        for number in self._chain(cluster):
            base = self._cluster_offset(number)
            block = self.disk.read(base, self.cluster_size)
            for at in range(0, self.cluster_size, DIRENT):
                length = block[at]
                if length in (0x00, 0xFF):
                    return entries
                if length == DELETED or length > NAME_MAX:
                    continue
                name = block[at + 2:at + 2 + length].decode("latin-1")
                first, size = struct.unpack_from("<II", block, at + 44)
                entries.append(Entry(name, bool(block[at + 1] & ATTR_DIR), first, size, base + at))
        return entries

    def _find(self, path: str) -> Entry | None:
        """None is the root."""
        cluster, found = self.root, None
        for part in path.replace("\\", "/").strip("/").split("/"):
            if not part:
                continue
            found = next((e for e in self._entries(cluster) if e.name.lower() == part.lower()), None)
            if found is None:
                raise FileNotFoundError(path)
            cluster = found.cluster
        return found

    def listdir(self, path: str) -> list[Entry]:
        entry = self._find(path)
        return self._entries(entry.cluster if entry else self.root)

    def read_file(self, path: str) -> bytes:
        entry = self._find(path)
        if entry is None or entry.is_dir:
            raise IsADirectoryError(path)
        out = bytearray()
        for cluster in self._chain(entry.cluster):
            out += self.disk.read(self._cluster_offset(cluster), self.cluster_size)
            if len(out) >= entry.size:
                break
        return bytes(out[:entry.size])

    def overwrite_file(self, path: str, data: bytes) -> None:
        entry = self._find(path)
        if entry is None or entry.is_dir:
            raise IsADirectoryError(path)
        chain = self._chain(entry.cluster)
        if len(data) > len(chain) * self.cluster_size:
            raise ValueError(f"{len(data)} bytes do not fit the {len(chain)} clusters of {path}")
        if not self.disk.writable:
            raise PermissionError("opened read-only")
        for index, cluster in enumerate(chain):
            piece = data[index * self.cluster_size:(index + 1) * self.cluster_size]
            if piece:
                self.disk.write(self._cluster_offset(cluster), piece)
        if len(data) != entry.size:
            self.disk.write(entry.where + 48, struct.pack("<I", len(data)))


def _open(image: Path, path: str, writable: bool = False) -> tuple[Fatx, str]:
    letter, _, rest = path.partition(":")
    offset, size = PARTITIONS[letter.upper()]
    return Fatx(Qcow2(image, writable), offset, size), rest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("ls", "get", "put"):
        command = commands.add_parser(name)
        command.add_argument("image", type=Path)
        command.add_argument("path", help="with its drive letter, E:/UDATA/...")
        if name != "ls":
            command.add_argument("file", type=Path)
    args = parser.parse_args()
    volume, inner = _open(args.image, args.path, args.command == "put")
    if args.command == "ls":
        for entry in volume.listdir(inner):
            print(f"{'<dir>' if entry.is_dir else entry.size:>10}  {entry.name}")
    elif args.command == "get":
        args.file.write_bytes(volume.read_file(inner))
    else:
        volume.overwrite_file(inner, args.file.read_bytes())


if __name__ == "__main__":
    main()
