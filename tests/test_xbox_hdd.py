import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

import xbox_hdd  # noqa: E402

CLUSTER = 1 << 16
FATX_CLUSTER = 16 * 1024
PART_OFFSET = 0x80000
PART_SIZE = 64 * FATX_CLUSTER
FIRST_DATA = PART_OFFSET + 0x2000  # superblock, then a 4 KB FAT for the 65 FAT16 entries


def dirent(name: str, cluster: int, size: int, directory: bool = False) -> bytes:
    raw = name.encode("ascii")
    return (bytes([len(raw), 0x10 if directory else 0]) + raw.ljust(42, b"\xff") +
            struct.pack("<II", cluster, size) + b"\0" * 12)


def raw_disk() -> bytearray:
    """A small FATX partition holding UDATA\\GAME\\SAVE A, whose second cluster is the end of its chain."""
    disk = bytearray(PART_OFFSET + PART_SIZE)
    disk[PART_OFFSET:PART_OFFSET + 16] = b"FATX" + struct.pack("<III", 0x1234, 32, 1)
    fat = [0xFFF8, 0xFFFF, 0xFFFF, 4, 0xFFFF, 0xFFFF]  # 1 root, 2 UDATA, 3-4 SAVE A, 5 unused file
    disk[PART_OFFSET + 0x1000:PART_OFFSET + 0x1000 + len(fat) * 2] = struct.pack(f"<{len(fat)}H", *fat)

    def cluster_at(number: int) -> int:
        return FIRST_DATA + (number - 1) * FATX_CLUSTER

    root = dirent("deleted", 9, 0).replace(bytes([7]), bytes([0xE5]), 1) + dirent("UDATA", 2, 0, True) + b"\xff" * 64
    disk[cluster_at(1):cluster_at(1) + len(root)] = root
    folder = dirent("SAVE A", 3, 20000) + dirent("SMALL", 5, 10) + b"\xff" * 64
    disk[cluster_at(2):cluster_at(2) + len(folder)] = folder
    disk[cluster_at(3):cluster_at(3) + 20000] = b"\x11" * 20000
    disk[cluster_at(5):cluster_at(5) + 10] = b"\x22" * 10
    return disk


def write_qcow2(path: Path, disk: bytes, virtual_size: int) -> None:
    """Version 3: header, refcount table and block, L1, one L2, then the data clusters present in `disk`."""
    refcount_table, refcount_block, l1, l2 = 1, 2, 3, 4
    next_cluster = 5
    l2_table = [0] * (CLUSTER // 8)
    body = bytearray()
    for index in range(0, len(disk), CLUSTER):
        chunk = disk[index:index + CLUSTER]
        if any(chunk):
            l2_table[index // CLUSTER] = (next_cluster * CLUSTER) | (1 << 63)
            body += chunk.ljust(CLUSTER, b"\0")
            next_cluster += 1
    image = bytearray(next_cluster * CLUSTER)
    image[:104] = struct.pack(">4sIQIIQIIQQIIQQQQII", b"QFI\xfb", 3, 0, 0, 16, virtual_size, 0, 1, l1 * CLUSTER,
                              refcount_table * CLUSTER, 1, 0, 0, 0, 0, 0, 4, 104)
    image[refcount_table * CLUSTER:refcount_table * CLUSTER + 8] = struct.pack(">Q", refcount_block * CLUSTER)
    for index in range(next_cluster):
        image[refcount_block * CLUSTER + index * 2:refcount_block * CLUSTER + index * 2 + 2] = struct.pack(">H", 1)
    image[l1 * CLUSTER:l1 * CLUSTER + 8] = struct.pack(">Q", (l2 * CLUSTER) | (1 << 63))
    image[l2 * CLUSTER:(l2 + 1) * CLUSTER] = struct.pack(f">{len(l2_table)}Q", *l2_table)
    image[5 * CLUSTER:] = body
    path.write_bytes(image)


@pytest.fixture
def image(tmp_path) -> Path:
    path = tmp_path / "disk.qcow2"
    disk = raw_disk()
    write_qcow2(path, bytes(disk), len(disk) + 16 * CLUSTER)
    return path


def open_volume(path: Path, writable: bool = False) -> xbox_hdd.Fatx:
    return xbox_hdd.Fatx(xbox_hdd.Qcow2(path, writable), PART_OFFSET, PART_SIZE)


def test_reads_across_clusters_and_holes_read_as_zero(image):
    disk = raw_disk()
    qcow = xbox_hdd.Qcow2(image)
    assert qcow.size == len(disk) + 16 * CLUSTER
    assert qcow.read(0, 16) == bytes(16)
    assert qcow.read(PART_OFFSET - 4, 12) == bytes(disk[PART_OFFSET - 4:PART_OFFSET + 8])
    assert qcow.read(len(disk) + CLUSTER, 32) == bytes(32)


def test_lists_a_folder_and_skips_deleted_entries(image):
    volume = open_volume(image)
    assert [(e.name, e.is_dir) for e in volume.listdir("")] == [("UDATA", True)]
    assert [(e.name, e.size) for e in volume.listdir("UDATA")] == [("SAVE A", 20000), ("SMALL", 10)]


def test_reads_a_file_through_its_cluster_chain(image):
    assert open_volume(image).read_file("UDATA/SAVE A") == b"\x11" * 20000
    assert open_volume(image).read_file("UDATA\\SMALL") == b"\x22" * 10


def test_a_missing_path_says_so(image):
    with pytest.raises(FileNotFoundError):
        open_volume(image).read_file("UDATA/NOPE")


def test_overwrites_a_file_in_place_and_keeps_its_clusters(image):
    new = bytes(range(256)) * 78
    assert len(new) == 19968
    size_before = image.stat().st_size
    volume = open_volume(image, writable=True)
    volume.overwrite_file("UDATA/SAVE A", new)
    assert open_volume(image).read_file("UDATA/SAVE A") == new
    assert image.stat().st_size == size_before
    assert [(e.name, e.size) for e in open_volume(image).listdir("UDATA")] == [("SAVE A", len(new)), ("SMALL", 10)]


def test_refuses_data_past_the_clusters_the_file_has(image):
    volume = open_volume(image, writable=True)
    with pytest.raises(ValueError):
        volume.overwrite_file("UDATA/SMALL", b"\x33" * (FATX_CLUSTER + 1))


def test_a_read_only_disk_refuses_to_write(image):
    with pytest.raises(PermissionError):
        open_volume(image).overwrite_file("UDATA/SMALL", b"\x33" * 10)


def test_refuses_an_image_that_is_not_qcow2(tmp_path):
    path = tmp_path / "other.bin"
    path.write_bytes(b"\0" * 4096)
    with pytest.raises(ValueError):
        xbox_hdd.Qcow2(path)
