"""Builds small synthetic XDVDFS images for tests. Holds no game data."""
import io
import struct
from pathlib import Path

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
ATTR_DIR = 0x10
ATTR_FILE = 0x20


def _dir_table(children: list[tuple[str, int, int, int]]) -> bytes:
    """A balanced binary tree of (name, start sector, size, attributes) stored in pre-order, so its root is first."""
    children = sorted(children, key=lambda c: c[0].upper())
    nodes: list[tuple[int, int | None, int | None]] = []

    def visit(low: int, high: int) -> int | None:
        if low >= high:
            return None
        middle = (low + high) // 2
        slot = len(nodes)
        nodes.append((middle, None, None))
        left = visit(low, middle)
        right = visit(middle + 1, high)
        nodes[slot] = (middle, left, right)
        return middle

    visit(0, len(children))
    offsets: dict[int, int] = {}
    position = 0
    for index, _, _ in nodes:
        offsets[index] = position
        position += (14 + len(children[index][0]) + 3) & ~3

    table = bytearray()
    for index, left, right in nodes:
        name, start, length, attributes = children[index]
        encoded = name.encode("latin-1")
        left_link = offsets[left] // 4 if left is not None else 0
        right_link = offsets[right] // 4 if right is not None else 0
        entry = struct.pack("<HHIIBB", left_link, right_link, start, length, attributes, len(encoded)) + encoded
        table += entry + b"\xff" * (-len(entry) % 4)
    table += b"\xff" * (-len(table) % SECTOR)
    return bytes(table)


def build_image(files: dict[str, bytes]) -> bytes:
    """An XDVDFS image of the given files, keyed by "dir/name" with one level of directories at most."""
    roots: dict[str, bytes | dict[str, bytes]] = {}
    for path, data in files.items():
        parts = path.strip("/").split("/")
        if len(parts) == 1:
            roots[parts[0]] = data
        else:
            roots.setdefault(parts[0], {})[parts[1]] = data  # type: ignore[index]

    image = bytearray(34 * SECTOR)

    def append(data: bytes) -> int:
        sector = len(image) // SECTOR
        image.extend(data)
        image.extend(b"\0" * (-len(image) % SECTOR))
        return sector

    def entries_of(mapping: dict) -> list[tuple[str, int, int, int]]:
        result = []
        for name, value in mapping.items():
            if isinstance(value, dict):
                table = _dir_table(entries_of(value))
                result.append((name, append(table), len(table), ATTR_DIR))
            else:
                result.append((name, append(value), len(value), ATTR_FILE))
        return result

    root = _dir_table(entries_of(roots))
    root_sector = append(root)
    descriptor = 32 * SECTOR
    image[descriptor:descriptor + 20] = MAGIC
    image[descriptor + 0x14:descriptor + 0x1C] = struct.pack("<II", root_sector, len(root))
    image[descriptor + 0x7EC:descriptor + 0x800] = MAGIC
    return bytes(image)


def write_image(path: Path, files: dict[str, bytes]) -> Path:
    path.write_bytes(build_image(files))
    return path


class OffsetFile(io.RawIOBase):
    """Shows a small image as if it began `base` bytes into the file, without allocating the gap."""

    def __init__(self, image: bytes, base: int):
        super().__init__()
        self._image = image
        self._base = base
        self._position = 0

    def readable(self) -> bool:
        return True

    def seekable(self) -> bool:
        return True

    def seek(self, offset: int, whence: int = io.SEEK_SET) -> int:
        if whence == io.SEEK_SET:
            self._position = offset
        elif whence == io.SEEK_CUR:
            self._position += offset
        else:
            self._position = self._base + len(self._image) + offset
        return self._position

    def tell(self) -> int:
        return self._position

    def read(self, size: int = -1) -> bytes:
        inner = self._position - self._base
        if inner < 0:
            gap = min(-inner, size if size >= 0 else -inner)
            self._position += gap
            return b"\0" * gap
        data = self._image[inner:] if size < 0 else self._image[inner:inner + size]
        self._position += len(data)
        return data
