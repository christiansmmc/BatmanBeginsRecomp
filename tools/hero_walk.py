"""Development aid: samples the hero's place while the game runs, in the port or in xemu, from outside the program.

Speed from the game's own numbers does not depend on the camera or on two runs taking the same path, which films do.
[0x3EF3C4] is the player item (gpPlayerItem in the GameCube's names), its place three floats at +0x38C. See docs/TECHNICAL.md, "Frame rate".

  hero_walk.py port <csv> --after 135 --seconds 60     waits for the port, then samples it
  hero_walk.py xemu <csv> --after 150 --seconds 60     the same in xemu
  hero_walk.py speed <csv> [<csv> ...] [--start s --end s]    the running speed of each, the typical half second
"""
from __future__ import annotations

import argparse
import ctypes
import struct
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

import numpy as np

from camera_probe import PortMemory, port_pid

PLAYER_ITEM = 0x3EF3C4
PLACE = 0x38C
FRAME_LENGTH = 0x30FF74  # EXBaseApp::m_FrameLength, 1 / the rate the game steps at
SAMPLE_SECONDS = 0.005
WINDOW_SECONDS = 0.5
MOVING = 0.3  # a window slower than this share of the fastest ones is a stop, a turn or a wall
MIN_WINDOWS = 3

MEM_COMMIT, READABLE = 0x1000, {0x02, 0x04, 0x08, 0x20, 0x40, 0x80}
IMAGE_MARK = bytes.fromhex("A1 18 04 40 00 8B 88 88 09 00 00 41")  # the vertical blank callback's first instructions
IMAGE_MARK_ADDRESS = 0x1FA550
XBOX_RAM_SIZES = {64 * 1024 * 1024, 128 * 1024 * 1024}


class MemoryBasicInformation(ctypes.Structure):
    _fields_ = [("base", ctypes.c_void_p), ("allocation_base", ctypes.c_void_p), ("allocation_protect", wintypes.DWORD),
                ("partition", wintypes.WORD), ("size", ctypes.c_size_t), ("state", wintypes.DWORD),
                ("protect", wintypes.DWORD), ("type", wintypes.DWORD)]


class XemuMemory:
    """Reads the guest memory of xemu from outside: low addresses are the image, 0x80000000 and up the RAM itself."""

    def __init__(self, pid: int):
        self.kernel32 = ctypes.windll.kernel32
        self.kernel32.OpenProcess.restype = wintypes.HANDLE
        self.kernel32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                                    ctypes.POINTER(ctypes.c_size_t)]
        self.kernel32.VirtualQueryEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p,
                                                 ctypes.POINTER(MemoryBasicInformation), ctypes.c_size_t]
        self.kernel32.VirtualQueryEx.restype = ctypes.c_size_t
        self.handle = self.kernel32.OpenProcess(0x0410, False, pid)  # query information, read
        if not self.handle:
            raise ctypes.WinError()
        self.ram, self.image = self._find_ram()

    def _raw(self, address: int, size: int) -> bytes | None:
        buffer = ctypes.create_string_buffer(size)
        done = ctypes.c_size_t()
        ok = self.kernel32.ReadProcessMemory(self.handle, address, buffer, size, ctypes.byref(done))
        return buffer.raw if ok else None

    def _find_ram(self) -> tuple[int, int]:
        """Guest RAM and where the image lies in it: the kernel does not load it at its virtual address."""
        info = MemoryBasicInformation()
        address = 0
        while self.kernel32.VirtualQueryEx(self.handle, address, ctypes.byref(info), ctypes.sizeof(info)):
            base, size = info.base or 0, info.size
            if info.state == MEM_COMMIT and info.protect in READABLE and size in XBOX_RAM_SIZES:
                data = self._raw(base, size)
                at = data.find(IMAGE_MARK) if data else -1
                if at >= 0:
                    return base, at - IMAGE_MARK_ADDRESS
            address = base + size
        raise SystemExit("no guest RAM with the game's code in xemu")

    def read(self, address: int, size: int) -> bytes:
        offset = address - 0x80000000 if address >= 0x80000000 else address + self.image
        data = self._raw(self.ram + offset, size)
        if data is None:
            raise ctypes.WinError()
        return data


def run_speed(rows: np.ndarray, start: float | None = None, end: float | None = None) -> float:
    """The typical speed while running: distance over each half second, among the windows near the fastest."""
    first = rows[0, 0] if start is None else start
    last = rows[-1, 0] if end is None else end
    speeds = []
    for edge in np.arange(first, last - WINDOW_SECONDS + 1e-9, WINDOW_SECONDS):
        inside = rows[(rows[:, 0] >= edge) & (rows[:, 0] < edge + WINDOW_SECONDS)]
        if len(inside) >= 2 and inside[-1, 0] > inside[0, 0]:
            speeds.append(np.linalg.norm(inside[-1, 1:4] - inside[0, 1:4]) / (inside[-1, 0] - inside[0, 0]))
    if len(speeds) < MIN_WINDOWS:
        raise ValueError(f"only {len(speeds)} half seconds to measure")
    speeds = np.array(speeds)
    return float(np.median(speeds[speeds >= speeds.max() * MOVING]))


def write_rows(path: Path, rows: np.ndarray) -> None:
    np.savetxt(path, rows, delimiter=",", fmt="%.6f")


def read_rows(path: Path) -> np.ndarray:
    return np.loadtxt(path, delimiter=",", ndmin=2)


def _xemu_pid() -> int:
    lines = subprocess.run(["tasklist", "/FI", "IMAGENAME eq xemu.exe", "/FO", "CSV", "/NH"],
                           capture_output=True, text=True, check=True).stdout.splitlines()
    for line in lines:
        if "xemu" in line:
            return int(line.split(",")[1].strip('"'))
    raise SystemExit("xemu is not running")


def _wait_for(find_pid, after: float) -> int:
    while True:
        try:
            pid = find_pid()
            break
        except SystemExit:
            time.sleep(0.1)
    time.sleep(after)
    return pid


def sample(memory, seconds: float) -> np.ndarray:
    rows = []
    start = time.perf_counter()
    while (now := time.perf_counter() - start) < seconds:
        item = struct.unpack("<I", memory.read(PLAYER_ITEM, 4))[0]
        if item:
            try:
                place = struct.unpack("<3f", memory.read(item + PLACE, 12))
                rows.append((now, *place, struct.unpack("<f", memory.read(FRAME_LENGTH, 4))[0]))
            except OSError:
                pass
        time.sleep(SAMPLE_SECONDS)
    return np.array(rows)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("port", "xemu"):
        command = commands.add_parser(name)
        command.add_argument("csv", type=Path)
        command.add_argument("--after", type=float, default=0.0)
        command.add_argument("--seconds", type=float, default=30.0)
    command = commands.add_parser("speed")
    command.add_argument("csv", type=Path, nargs="+")
    command.add_argument("--start", type=float)
    command.add_argument("--end", type=float)
    args = parser.parse_args()
    if args.command == "speed":
        for path in args.csv:
            rows = read_rows(path)
            print(f"{path}: {run_speed(rows, args.start, args.end):.4f} units/s, {len(rows)} samples")
        return
    if args.command == "port":
        memory = PortMemory(_wait_for(port_pid, args.after))
    else:
        memory = XemuMemory(_wait_for(_xemu_pid, args.after))
    rows = sample(memory, args.seconds)
    write_rows(args.csv, rows)
    print(f"{len(rows)} samples in {args.csv}, frame length {rows[-1, 4]:.5f}")


if __name__ == "__main__":
    main()
