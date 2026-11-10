"""The folder the player gets: what goes in it, and what a new version must leave alone."""
import hashlib
import os
import re
import struct
import tomllib
from pathlib import Path

import pytest

import make_dist

ROOT = Path(__file__).resolve().parent.parent
RUNTIME = ("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll")
LICENCES = ("OFL-Michroma.txt", "OFL-BarlowSemiCondensed.txt")
VERSION = 1 << 24
# what the game itself wrote for these records, from tests/native/shader_cache_test.cpp
FIRST = (0x1111222233334444, bytes(range(1, 10)), 0x4C016E5356DE53C2)
SECOND = (0x5555666677778888, bytes((200, 201, 202)), 0x46F4F150D4F87A58)
THIRD = (0x7777, b"third record code", None)


def cache_bytes(*records, version=VERSION) -> bytes:
    data = b"XBESHADR" + struct.pack("<II", version, 0)
    for key, code, check in records:
        check = make_dist.record_check(key, code) if check is None else check
        data += struct.pack("<QIIQ", key, len(code), 0, check) + code
    return data


@pytest.fixture
def build_dir(tmp_path, monkeypatch):
    """A build folder with its leftovers, and a runtime folder beside it."""
    folder = tmp_path / "build"
    (folder / "generated").mkdir(parents=True)
    (folder / "compiler").mkdir()
    for name in ("BatmanBegins.exe", "BatmanBegins.dll", *LICENCES, "BatmanBegins.pdb", "x.obj", "CMakeCache.txt",
                 "fps_policy_test.exe", "generated/game.cpp", "compiler/clang.exe"):
        (folder / name).write_text(name)
    exe = (folder / "BatmanBegins.exe").stat().st_mtime
    os.utime(folder / "BatmanBegins.dll", (exe + 1, exe + 1))
    (folder / "shaders.cache").write_bytes(cache_bytes(FIRST))
    runtime = tmp_path / "crt"
    runtime.mkdir()
    for name in RUNTIME:
        (runtime / name).write_text(name)
    monkeypatch.setattr(make_dist, "runtime_dir", lambda: runtime)
    return folder


@pytest.fixture
def iso(tmp_path):
    path = tmp_path / "game.iso"
    path.write_bytes(b"disc image " * 1000)
    return path


def names_of(build_dir) -> set[str]:
    return {path.name for path in make_dist.manifest(build_dir)}


def test_has_exe_dll_and_runtime(build_dir):
    assert {"BatmanBegins.exe", "BatmanBegins.dll", *RUNTIME} <= names_of(build_dir)


def test_has_font_licences(build_dir):
    assert set(LICENCES) <= names_of(build_dir)


def test_excludes_build_leftovers(build_dir):
    names = names_of(build_dir)
    assert not any(n.endswith((".pdb", ".obj")) or "generated" in n or "compiler" in n for n in names)
    assert "CMakeCache.txt" not in names
    assert "fps_policy_test.exe" not in names


def test_missing_dll_fails(build_dir):
    (build_dir / "BatmanBegins.dll").unlink()
    with pytest.raises(FileNotFoundError):
        make_dist.manifest(build_dir)


def test_missing_licence_fails(build_dir):
    (build_dir / LICENCES[0]).unlink()
    with pytest.raises(FileNotFoundError):
        make_dist.manifest(build_dir)


def test_missing_runtime_fails(build_dir, monkeypatch, tmp_path):
    monkeypatch.setattr(make_dist, "runtime_dir", lambda: tmp_path / "nowhere")
    with pytest.raises(FileNotFoundError):
        make_dist.manifest(build_dir)


def test_build_has_everything_the_player_needs(build_dir, iso, tmp_path):
    out = tmp_path / "out folder"
    make_dist.build(build_dir, iso, out)
    expected = {"BatmanBegins.exe", "BatmanBegins.dll", *RUNTIME, *LICENCES,
                "THIRD_PARTY.txt", "README.txt", "settings.toml", "shaders.cache", "assets"}
    assert {path.name for path in out.iterdir()} == expected
    assert (out / "assets" / iso.name).read_bytes() == iso.read_bytes()


def test_dll_stays_newer_than_the_exe(build_dir, iso, tmp_path):
    """The program rebuilds a DLL older than itself, and this folder has no compiler."""
    out = tmp_path / "out"
    make_dist.build(build_dir, iso, out)
    assert (out / "BatmanBegins.dll").stat().st_mtime_ns > (out / "BatmanBegins.exe").stat().st_mtime_ns


def test_dll_older_than_exe_fails(build_dir, iso, tmp_path):
    exe = (build_dir / "BatmanBegins.exe").stat().st_mtime
    os.utime(build_dir / "BatmanBegins.dll", (exe - 10, exe - 10))
    with pytest.raises(ValueError):
        make_dist.build(build_dir, iso, tmp_path / "out")


def test_iso_copy_is_verified(build_dir, iso, tmp_path, monkeypatch):
    def bad_copy(source, target):
        Path(target).write_bytes(b"not the same bytes")

    monkeypatch.setattr(make_dist, "_copy_file", bad_copy)
    out = tmp_path / "out"
    with pytest.raises(ValueError):
        make_dist.build(build_dir, iso, out)
    assert list((out / "assets").iterdir()) == []


def test_good_iso_is_not_copied_again(build_dir, iso, tmp_path, monkeypatch):
    out = tmp_path / "out"
    make_dist.build(build_dir, iso, out)

    def no_copy(source, target):
        raise AssertionError("copied again")

    monkeypatch.setattr(make_dist, "_copy_file", no_copy)
    make_dist.build(build_dir, iso, out)
    assert make_dist.sha256(out / "assets" / iso.name) == hashlib.sha256(iso.read_bytes()).hexdigest()


def test_damaged_iso_is_replaced(build_dir, iso, tmp_path):
    out = tmp_path / "out"
    make_dist.build(build_dir, iso, out)
    (out / "assets" / iso.name).write_bytes(b"damaged")
    make_dist.build(build_dir, iso, out)
    assert (out / "assets" / iso.name).read_bytes() == iso.read_bytes()


def test_rebuild_keeps_player_files(build_dir, iso, tmp_path):
    out = tmp_path / "out"
    make_dist.build(build_dir, iso, out)
    player = {
        "hdd/E/UDATA/45410389/13AD93548315/BATMAN A": "a save",
        "logs/batman.log": "last session",
        "logs/batman.1.log": "the one before",
        "CrashDumps/x.dmp": "a dump",
        "settings.toml": 'display_mode = "windowed"\n',
        "shaders.cache": "the player's shaders",
    }
    for name, text in player.items():
        (out / name).parent.mkdir(parents=True, exist_ok=True)
        (out / name).write_text(text)
    stamps = {name: (out / name).stat().st_mtime_ns for name in player}

    (build_dir / "BatmanBegins.exe").write_text("new exe")
    newer = (build_dir / "BatmanBegins.exe").stat().st_mtime + 1
    os.utime(build_dir / "BatmanBegins.dll", (newer, newer))
    make_dist.build(build_dir, iso, out)

    assert (out / "BatmanBegins.exe").read_text() == "new exe"
    for name, text in player.items():
        assert (out / name).read_text() == text, name
        assert (out / name).stat().st_mtime_ns == stamps[name], name


def test_settings_defaults(build_dir, iso, tmp_path):
    out = tmp_path / "out"
    make_dist.build(build_dir, iso, out)
    settings = tomllib.loads((out / "settings.toml").read_text(encoding="utf-8"))
    header = (ROOT / "src" / "common" / "fps_policy.h").read_text(encoding="utf-8")
    assert settings == {
        "display_mode": "borderless",
        "width": 0,
        "height": 0,
        "fps_cap": int(re.search(r"kMaxValidatedFps = (\d+)", header).group(1)),
        "vsync": True,
    }


def test_readme_opens_as_windows_text(build_dir, iso, tmp_path):
    out = tmp_path / "out"
    make_dist.build(build_dir, iso, out)
    data = (out / "README.txt").read_bytes()
    assert data.startswith(b"\xef\xbb\xbf")
    assert data.count(b"\n") == data.count(b"\r\n") > 10


def test_readme_tells_what_was_asked(build_dir, iso, tmp_path):
    out = tmp_path / "out"
    make_dist.build(build_dir, iso, out)
    text = (out / "README.txt").read_text(encoding="utf-8-sig")
    for words in ("BatmanBegins.exe", "Esc", "Back + Start", "Space", "hdd\\E\\UDATA", "logs", "30 FPS", "33%", "4:3"):
        assert words in text, words
    assert "experimental_play_fps" not in text


def test_record_check_matches_the_game():
    for key, code, check in (FIRST, SECOND):
        assert make_dist.record_check(key, code) == check


def test_merged_cache_has_each_shader_once(tmp_path):
    (tmp_path / "a").write_bytes(cache_bytes(FIRST, SECOND))
    (tmp_path / "b").write_bytes(cache_bytes(SECOND, THIRD))
    merged = make_dist.merge_caches([tmp_path / "a", tmp_path / "b"])
    assert merged == cache_bytes(FIRST, SECOND, THIRD)


def test_merge_stops_at_a_damaged_record(tmp_path):
    data = bytearray(cache_bytes(FIRST, SECOND))
    data[-1] ^= 0xFF
    (tmp_path / "a").write_bytes(data)
    (tmp_path / "b").write_bytes(cache_bytes(THIRD))
    assert make_dist.merge_caches([tmp_path / "a", tmp_path / "b"]) == cache_bytes(FIRST, THIRD)


def test_merge_takes_the_first_files_version(tmp_path):
    """A cache of another version holds code compiled with other flags: the game would throw the whole file away."""
    (tmp_path / "a").write_bytes(cache_bytes(FIRST))
    (tmp_path / "b").write_bytes(cache_bytes(SECOND, version=VERSION + 1))
    (tmp_path / "c").write_bytes(b"not a cache")
    missing = tmp_path / "missing"
    merged = make_dist.merge_caches([missing, tmp_path / "a", tmp_path / "b", tmp_path / "c"])
    assert merged == cache_bytes(FIRST)


def test_merge_of_nothing_is_empty(tmp_path):
    assert make_dist.merge_caches([tmp_path / "missing"]) == b""


def test_new_folder_gets_the_merged_cache(build_dir, iso, tmp_path):
    extra = tmp_path / "extra.cache"
    extra.write_bytes(cache_bytes(FIRST, THIRD))
    out = tmp_path / "out"
    make_dist.build(build_dir, iso, out, caches=[extra])
    assert (out / "shaders.cache").read_bytes() == cache_bytes(FIRST, THIRD)


def test_build_without_any_cache_leaves_it_to_the_game(build_dir, iso, tmp_path):
    (build_dir / "shaders.cache").unlink()
    out = tmp_path / "out"
    make_dist.build(build_dir, iso, out)
    assert not (out / "shaders.cache").exists()


def test_real_build_has_a_full_manifest():
    build = ROOT / "out" / "build" / "release"
    if not (build / "BatmanBegins.exe").exists():
        pytest.skip("not built here")
    assert {"BatmanBegins.exe", "BatmanBegins.dll", *RUNTIME, *LICENCES} <= names_of(build)


def test_real_cache_passes_the_check():
    cache = ROOT / "out" / "build" / "release" / "shaders.cache"
    if not cache.exists():
        pytest.skip("no shaders here yet")
    _, records = make_dist.read_cache(cache)
    assert sum(len(code) + 24 for code in records.values()) + 16 == cache.stat().st_size
