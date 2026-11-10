import hashlib
import io
import struct

import pytest

import xiso
from xiso_fixture import MAGIC, OffsetFile, build_image, write_image

XBE = b"XBEH" + b"\x11" * 300
A = b"alpha" * 1000
B = b"beta" * 700
FILES = {"default.xbe": XBE, "Data/a.bin": A, "Data/b.bin": B}
XBE_SHA1 = hashlib.sha1(XBE).hexdigest()


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test_finds_raw_xiso_partition():
    assert xiso.find_partition(io.BytesIO(build_image(FILES))) == 0


def test_finds_redump_partition():
    assert xiso.find_partition(OffsetFile(build_image(FILES), 0x18300000)) == 0x18300000


def test_rejects_non_xbox_image():
    with pytest.raises(ValueError):
        xiso.find_partition(io.BytesIO(b"\0" * 0x20000))


def test_lists_nested_files():
    entries = xiso.list_files(io.BytesIO(build_image(FILES)), 0)
    assert {e.path for e in entries} == {"/default.xbe", "/Data/a.bin", "/Data/b.bin"}
    assert {e.path: e.size for e in entries}["/Data/a.bin"] == len(A)


def test_lists_files_of_redump_layout():
    entries = xiso.list_files(OffsetFile(build_image(FILES), 0x18300000), 0x18300000)
    assert {e.path for e in entries} == {"/default.xbe", "/Data/a.bin", "/Data/b.bin"}


def test_extract_copies_exact_bytes(tmp_path):
    iso = write_image(tmp_path / "game.iso", FILES)
    out = tmp_path / "assets"
    xiso.extract(iso, out, XBE_SHA1)
    assert (out / "Data/a.bin").read_bytes() == A
    assert (out / "Data/b.bin").read_bytes() == B
    assert (out / "default.xbe").read_bytes() == XBE


def test_extract_rejects_other_xbe(tmp_path):
    iso = write_image(tmp_path / "game.iso", FILES)
    out = tmp_path / "assets"
    with pytest.raises(ValueError):
        xiso.extract(iso, out, "0" * 40)
    assert not out.exists()


def test_extract_accepts_uppercase_sha1(tmp_path):
    iso = write_image(tmp_path / "game.iso", FILES)
    xiso.extract(iso, tmp_path / "assets", XBE_SHA1.upper())
    assert (tmp_path / "assets/default.xbe").exists()


def test_extract_refuses_non_empty_output(tmp_path):
    iso = write_image(tmp_path / "game.iso", FILES)
    out = tmp_path / "assets"
    out.mkdir()
    (out / "keep.txt").write_text("mine")
    with pytest.raises(FileExistsError):
        xiso.extract(iso, out, None)
    assert (out / "keep.txt").read_text() == "mine"


def test_extract_never_writes_to_iso(tmp_path):
    iso = write_image(tmp_path / "game.iso", FILES)
    before = sha256(iso)
    xiso.extract(iso, tmp_path / "assets", None)
    assert sha256(iso) == before


def test_rejects_entry_name_that_escapes_the_folder():
    image = bytearray(build_image({"default.xbe": XBE}))
    descriptor = 32 * 2048
    assert image[descriptor:descriptor + 20] == MAGIC
    root_sector = struct.unpack_from("<I", image, descriptor + 0x14)[0]
    entry = root_sector * 2048
    name = b"..\\evil.xbe"
    assert image[entry + 13] == len(b"default.xbe") == len(name)
    image[entry + 14:entry + 14 + len(name)] = name
    with pytest.raises(ValueError):
        xiso.list_files(io.BytesIO(bytes(image)), 0)


def test_export_partition_is_readable_as_raw_xiso(tmp_path):
    out = tmp_path / "game.xiso"
    xiso.export_partition(OffsetFile(build_image(FILES), 0x18300000), out)
    with out.open("rb") as f:
        assert xiso.find_partition(f) == 0
        entries = {e.path: e for e in xiso.list_files(f, 0)}
        assert set(entries) == {"/default.xbe", "/Data/a.bin", "/Data/b.bin"}
        f.seek(entries["/Data/a.bin"].sector * 2048)
        assert f.read(len(A)) == A


def test_export_partition_refuses_existing_output(tmp_path):
    out = tmp_path / "game.xiso"
    out.write_bytes(b"mine")
    with pytest.raises(FileExistsError):
        xiso.export_partition(OffsetFile(build_image(FILES), 0x18300000), out)
    assert out.read_bytes() == b"mine"
