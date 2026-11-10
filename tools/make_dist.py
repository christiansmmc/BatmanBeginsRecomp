"""Puts together the folder the player gets, dist/Batman Begins, from a finished build and the user's own ISO.

A new version can be built over an old folder: the player's saves, logs, settings and shaders stay as they are.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BUILD = ROOT / "out" / "build" / "release"
DEFAULT_ISO = ROOT / "Batman Begins (USA).iso"
DEFAULT_OUT = ROOT / "dist" / "Batman Begins"
# an existing folder's cache, which play has grown, seeds a new folder; read only
DEFAULT_CACHES = (DEFAULT_OUT / "shaders.cache",)

EXE, DLL = "BatmanBegins.exe", "BatmanBegins.dll"
FONT_LICENCES = ("OFL-Michroma.txt", "OFL-BarlowSemiCondensed.txt")
RUNTIME = ("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll")  # what the exe imports beyond Windows itself
THIRD_PARTY = ROOT / "sdk" / "THIRD_PARTY.txt"
PLAYER_README = ROOT / "docs" / "player-readme.txt"  # README.txt in the folder
FPS_POLICY = ROOT / "src" / "common" / "fps_policy.h"
CACHE = "shaders.cache"
SETTINGS = "settings.toml"

HASH_BLOCK = 1 << 24
CACHE_MAGIC = b"XBESHADR"
CACHE_HEADER = struct.Struct("<8sII")
CACHE_RECORD = struct.Struct("<QIIQ")
WORD = (1 << 64) - 1
HASH_SEED, HASH_K0, HASH_K1 = 1469598103934665603, 0x9E3779B97F4A7C15, 0xE7037ED1A0B428DB


def runtime_dir() -> Path:
    """The Visual C++ runtime that can be shipped, from the installed Visual Studio, as release.bat finds it."""
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        raise FileNotFoundError(vswhere)
    found = subprocess.run([str(vswhere), "-latest", "-products", "*", "-property", "installationPath"],
                           capture_output=True, text=True, check=True).stdout.strip()
    folders = sorted((Path(found) / "VC" / "Redist" / "MSVC").glob("1*/x64/Microsoft.VC*.CRT"))
    if not folders:
        raise FileNotFoundError(f"no Visual C++ runtime under {found}")
    return folders[-1]


def manifest(build_dir: Path) -> list[Path]:
    """The files of a build that go to the player, and nothing else from that folder."""
    build_dir = Path(build_dir)
    files = [build_dir / name for name in (EXE, DLL, *FONT_LICENCES)]
    files += [runtime_dir() / name for name in RUNTIME]
    for path in files:
        if not path.is_file():
            raise FileNotFoundError(path)
    return files


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        while block := f.read(HASH_BLOCK):
            digest.update(block)
    return digest.hexdigest()


def _copy_file(source: Path, target: Path) -> None:
    shutil.copyfile(source, target)


def copy_iso(iso: Path, assets: Path) -> None:
    """Copies the disc image and compares it with the original. A copy already there and right is left alone."""
    assets.mkdir(parents=True, exist_ok=True)
    target = assets / iso.name
    expected = sha256(iso)
    if target.exists() and target.stat().st_size == iso.stat().st_size and sha256(target) == expected:
        return
    partial = target.with_name(target.name + ".part")  # the game only looks at .iso files
    try:
        _copy_file(iso, partial)
        if sha256(partial) != expected:
            raise ValueError(f"the copy of {iso.name} differs from the original")
        os.replace(partial, target)
    finally:
        partial.unlink(missing_ok=True)


def _mix(a: int, b: int) -> int:
    product = a * b
    return (product & WORD) ^ (product >> 64)


def _hash(data: bytes, seed: int = HASH_SEED) -> int:
    """Hash in sdk/runtime/gpu/shaders.cpp."""
    state, size, at = seed ^ HASH_K0, len(data), 0
    while at + 16 <= size:
        x, y = struct.unpack_from("<QQ", data, at)
        state = _mix(x ^ HASH_K1, y ^ state)
        at += 16
    left = size - at
    if left >= 8:
        x, y = struct.unpack_from("<Q", data, at)[0], struct.unpack_from("<Q", data, size - 8)[0]
        state = _mix(x ^ HASH_K1, y ^ state ^ left)
    elif left:
        state = _mix(int.from_bytes(data[at:], "little") ^ HASH_K1, state ^ left)
    return _mix(state ^ HASH_K0, size ^ HASH_K1)


def record_check(key: int, code: bytes) -> int:
    """RecordCheck in sdk/runtime/gpu/shader_cache.cpp."""
    seed = _hash(struct.pack("<Q", key))
    seed = _hash(struct.pack("<Q", len(code)), seed)
    return _hash(code, seed)


def read_cache(path: Path) -> tuple[int, dict[int, bytes]]:
    """The file's version and its records up to the first bad one, as the game reads it. Raises ValueError for no cache."""
    data = path.read_bytes()
    if len(data) < CACHE_HEADER.size:
        raise ValueError(f"{path} is too short")
    magic, version, zero = CACHE_HEADER.unpack_from(data)
    if magic != CACHE_MAGIC or zero:
        raise ValueError(f"{path} is not a shader cache")
    records, at = {}, CACHE_HEADER.size
    while len(data) - at >= CACHE_RECORD.size:
        key, size, zero, check = CACHE_RECORD.unpack_from(data, at)
        body = at + CACHE_RECORD.size
        if zero or size > len(data) - body:
            break
        code = data[body:body + size]
        if record_check(key, code) != check:
            break
        records.setdefault(key, code)
        at = body + size
    return version, records


def merge_caches(paths: list[Path]) -> bytes:
    """One cache of every shader in the files, each once. The first readable file sets the version; b"" if there is none."""
    version, records = None, {}
    for path in map(Path, paths):
        try:
            file_version, file_records = read_cache(path)
        except (OSError, ValueError):
            continue
        version = file_version if version is None else version
        if file_version == version:
            for key, code in file_records.items():
                records.setdefault(key, code)
    if version is None:
        return b""
    out = bytearray(CACHE_HEADER.pack(CACHE_MAGIC, version, 0))
    for key, code in records.items():
        out += CACHE_RECORD.pack(key, len(code), 0, record_check(key, code)) + code
    return bytes(out)


def default_settings() -> str:
    """Borderless at the desktop's size, the frame rate cap the game may use, vsync on."""
    cap = re.search(r"kMaxValidatedFps = (\d+)", FPS_POLICY.read_text(encoding="utf-8")).group(1)
    return f'display_mode = "borderless"\nwidth = 0\nheight = 0\nfps_cap = {cap}\nvsync = true\n'


def _windows_text(source: Path, target: Path) -> None:
    text = source.read_text(encoding="utf-8").replace("\r\n", "\n")
    target.write_bytes(b"\xef\xbb\xbf" + text.replace("\n", "\r\n").encode("utf-8"))


def build(build_dir: Path, iso: Path, out: Path, caches: list[Path] = ()) -> None:
    """Makes the folder, or brings one up to date. The cache of a new folder is every shader of the build and of caches."""
    build_dir, iso, out = Path(build_dir), Path(iso), Path(out)
    files = manifest(build_dir)
    if (build_dir / DLL).stat().st_mtime_ns <= (build_dir / EXE).stat().st_mtime_ns:
        raise ValueError(f"{DLL} is older than {EXE}: the program would try to rebuild it, and the folder has no compiler")
    out.mkdir(parents=True, exist_ok=True)
    for path in files:
        shutil.copy2(path, out / path.name)  # keeps the times, and so the DLL newer than the exe
    shutil.copy2(THIRD_PARTY, out / THIRD_PARTY.name)
    _windows_text(PLAYER_README, out / "README.txt")
    if not (out / SETTINGS).exists():
        (out / SETTINGS).write_text(default_settings(), encoding="utf-8")
    if not (out / CACHE).exists():
        merged = merge_caches([build_dir / CACHE, *caches])
        if merged:
            (out / CACHE).write_bytes(merged)
    copy_iso(iso, out / "assets")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--iso", type=Path, default=DEFAULT_ISO)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--cache", type=Path, action="append", default=None, help="more shader caches for a new folder")
    args = parser.parse_args(argv)
    build(args.build_dir, args.iso, args.out, DEFAULT_CACHES if args.cache is None else args.cache)
    print(args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
