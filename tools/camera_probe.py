"""Development aid: reads the game's camera projection while it plays, in the port or in xemu.

The camera function at 0x2010F0 builds the projection the game uses at 0x400000 and a second matrix inside the camera.
xScale / yScale tells which picture the frame is made for: 0.75 for 4:3, 0.5625 for 16:9. See docs/TECHNICAL.md, "Aspect ratio".

  camera_probe.py port [seconds]
      samples the running port from outside, as fast as it can, and lists each projection seen
  camera_probe.py xemu [hits]
      stops xemu (--arg=-s) at the end of the camera function for that many calls and lists each call
"""
from __future__ import annotations

import argparse
import collections
import ctypes
import json
import struct
import subprocess
import time
from ctypes import wintypes
from dataclasses import dataclass

from gdb_stub import GdbStub

PROJECTION = 0x400000  # 16 floats, written by the camera function for the last camera it ran for
WIDESCREEN = 0x4006A4  # byte the camera function reads; the console's flag at 0x1FAC0C, then the setting 0x4200003E
CONSOLE_WIDESCREEN = 0x3EE4CC  # byte: what the console said, the default of that setting at new game
CAMERA_END = 0x201F77  # the last call in the camera function, with ebp holding the camera
CAMERA_RECT, CAMERA_FOV, CAMERA_FLAG, CAMERA_MATRIX = 0x4, 0x18, 0x6D, 0x70
PORT_GUEST_BASE = 0x100000000  # sdk/runtime/kernel/memory.cpp, logged as "guest space at"
MATRIX_FLOATS = 16
ROUND_DIGITS = 4
# the shadow camera sees through a slit, and the two D3DX cameras are square: the player's has neither
MAIN_MIN_Y_SCALE, MAIN_MAX_Y_SCALE = 1.0, 20.0


@dataclass
class Projection:
    xs: float
    ys: float
    count: int

    @property
    def ratio(self) -> float:
        return self.xs / self.ys if self.ys else 0.0


def scale_ratio(matrix: tuple[float, ...]) -> float:
    return matrix[0] / matrix[5] if matrix[5] else 0.0


def distinct_projections(samples: list[tuple[float, ...]]) -> list[Projection]:
    """Each projection seen, the most seen first."""
    counts = collections.Counter((round(m[0], ROUND_DIGITS), round(m[5], ROUND_DIGITS)) for m in samples)
    return [Projection(xs, ys, count) for (xs, ys), count in counts.most_common()]


def main_cameras(projections: list[Projection]) -> list[Projection]:
    """The player's camera among them, by the shape of its view: not as wide as tall, not a slit."""
    return [p for p in projections if MAIN_MIN_Y_SCALE < p.ys < MAIN_MAX_Y_SCALE and p.xs != p.ys]


def read_flags(memory) -> dict[str, int]:
    return {"widescreen": memory.read(WIDESCREEN, 1)[0], "console_widescreen": memory.read(CONSOLE_WIDESCREEN, 1)[0]}


def _matrix(memory, address: int) -> tuple[float, ...]:
    return struct.unpack(f"<{MATRIX_FLOATS}f", memory.read(address, MATRIX_FLOATS * 4))


def is_camera_hit(registers: dict[str, int]) -> bool:
    return registers["eip"] == CAMERA_END


def read_camera_hit(memory, camera: int) -> dict:
    """What the camera function left for this camera. The guest must be stopped at CAMERA_END."""
    projection, inner = _matrix(memory, PROJECTION), _matrix(memory, camera + CAMERA_MATRIX)
    return {
        **read_flags(memory),
        "camera": camera,
        "fov": struct.unpack("<f", memory.read(camera + CAMERA_FOV, 4))[0],
        "flag": memory.read(camera + CAMERA_FLAG, 1)[0],
        "rect": list(struct.unpack("<4f", memory.read(camera + CAMERA_RECT, 16))),
        "xs": projection[0],
        "ys": projection[5],
        "projection_ratio": scale_ratio(projection),
        "camera_matrix_ratio": scale_ratio(inner),
    }


class PortMemory:
    """Reads the running port from outside it, so nothing in it has to change."""

    def __init__(self, pid: int):
        self.kernel32 = ctypes.windll.kernel32
        self.kernel32.OpenProcess.restype = wintypes.HANDLE
        self.kernel32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                                    ctypes.POINTER(ctypes.c_size_t)]
        process_vm_read = 0x0010
        self.handle = self.kernel32.OpenProcess(process_vm_read, False, pid)
        if not self.handle:
            raise ctypes.WinError()

    def read(self, address: int, size: int) -> bytes:
        buffer = ctypes.create_string_buffer(size)
        done = ctypes.c_size_t()
        if not self.kernel32.ReadProcessMemory(self.handle, PORT_GUEST_BASE + address, buffer, size, ctypes.byref(done)):
            raise ctypes.WinError()
        return buffer.raw


def port_pid() -> int:
    lines = subprocess.run(["tasklist", "/FI", "IMAGENAME eq BatmanBegins.exe", "/FO", "CSV", "/NH"],
                           capture_output=True, text=True, check=True).stdout.splitlines()
    for line in lines:
        if "BatmanBegins" in line:
            return int(line.split(",")[1].strip('"'))
    raise SystemExit("the port is not running")


def probe_port(seconds: float) -> None:
    memory = PortMemory(port_pid())
    samples, flags = [], collections.Counter()
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        samples.append(_matrix(memory, PROJECTION))
        flags[memory.read(WIDESCREEN, 1)[0]] += 1
    print(f"{len(samples)} samples, widescreen byte {dict(flags)}, console says {memory.read(CONSOLE_WIDESCREEN, 1)[0]}")
    projections = distinct_projections(samples)
    main = main_cameras(projections)
    for p in projections[:12]:
        print(f"xs {p.xs:9.4f} ys {p.ys:9.4f} ratio {p.ratio:.5f} seen {p.count}{'  main' if p in main else ''}")
    if main:
        total = sum(p.count for p in main)
        print(f"main camera: {len(main)} projections, ratio from {min(p.ratio for p in main):.5f} "
              f"to {max(p.ratio for p in main):.5f}, weighted {sum(p.ratio * p.count for p in main) / total:.5f}")


def probe_xemu(hits: int) -> None:
    stub = GdbStub()  # the guest stops when the stub connects
    stub.set_breakpoint(CAMERA_END)
    try:
        for _ in range(hits):
            stub.run_until_stop()
            registers = stub.registers()
            if is_camera_hit(registers):  # the first stop can be the one from connecting
                print(json.dumps(read_camera_hit(stub, registers["ebp"])), flush=True)
    finally:
        stub.remove_breakpoint(CAMERA_END)
        stub.resume()
        stub.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(required=True)
    command = commands.add_parser("port")
    command.add_argument("seconds", type=float, nargs="?", default=3.0)
    command.set_defaults(run=lambda args: probe_port(args.seconds))
    command = commands.add_parser("xemu")
    command.add_argument("hits", type=int, nargs="?", default=30)
    command.set_defaults(run=lambda args: probe_xemu(args.hits))
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
