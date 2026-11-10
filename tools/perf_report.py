"""Development aid: reads the XBE_PERF lines of a run's batman.log.

Lists every second under a frame rate with its breakdown, and says whether building shaders and pipelines explains it.
With two runs it puts them side by side. All times per frame in ms, as the log gives them.
"""
from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

FRAME_MS = 1000.0 / 30.0  # the rate the level asks for
BLAME_SHARE = 0.5  # share of the lost time that building must explain

FIRST = re.compile(
    r"\[\s*(?P<at>[\d.]+)\] perf: (?P<fps>[\d.]+) fps (?P<ms>[\d.]+) ms/frame \| host (?P<host>[\d.]+) "
    r"\(draw (?P<draw>[\d.]+): assemble (?P<assemble>[\d.]+) bind (?P<bind>[\d.]+) "
    r"\[check [\d.]+, upload (?P<upload>[\d.]+)\] compile (?P<compile>[\d.]+)(?: pso (?P<pso>[\d.]+))?; "
    r"clear (?P<clear>[\d.]+) copy (?P<copy>[\d.]+) sync (?P<sync>[\d.]+) present (?P<present>[\d.]+) "
    r"\[swap [\d.]+ wait [\d.]+\]\) other (?P<other>[\d.]+)")
THREAD = re.compile(r"perf: title thread per frame: recording (?P<recording>[\d.]+) ms, waiting (?P<waiting>[\d.]+) ms")
COUNTS = re.compile(r"perf: per frame .*\| (?P<shaders>\d+) shaders (?P<psos>\d+) psos this second")


@dataclass
class Second:
    at: float
    fps: float
    ms: float
    host: float
    draw: float
    assemble: float
    bind: float
    upload: float
    compile: float
    pso: float | None
    clear: float
    copy: float
    sync: float
    present: float
    other: float
    recording: float = 0.0
    waiting: float = 0.0
    shaders: int = 0
    psos: int = 0


def parse_log(text: str) -> list[Second]:
    rows: list[Second] = []
    for line in text.splitlines():
        first = FIRST.search(line)
        if first:
            values = first.groupdict()
            pso = values.pop("pso")
            rows.append(Second(pso=None if pso is None else float(pso), **{k: float(v) for k, v in values.items()}))
            continue
        thread = THREAD.search(line)
        if thread and rows:
            rows[-1].recording, rows[-1].waiting = float(thread["recording"]), float(thread["waiting"])
            continue
        counts = COUNTS.search(line)
        if counts and rows:
            rows[-1].shaders, rows[-1].psos = int(counts["shaders"]), int(counts["psos"])
    return rows


def slow(rows: list[Second], below: float) -> list[Second]:
    return [row for row in rows if row.fps < below]


def building_ms(row: Second) -> float:
    """Time spent compiling shaders and creating pipelines in that second."""
    return (row.compile + (row.pso or 0.0)) * row.fps


def blame(row: Second) -> str:
    lost = 1000.0 - row.fps * FRAME_MS
    return "build" if building_ms(row) >= lost * BLAME_SHARE else "other"


def table(rows: list[Second]) -> list[str]:
    lines = ["     at    fps ms/frame compile     pso    draw    bind  upload present    sync   other    rec   wait"
             " shaders  psos blame"]
    for row in rows:
        pso = "-" if row.pso is None else f"{row.pso:.2f}"
        lines.append(f"{row.at:7.1f} {row.fps:6.1f} {row.ms:8.2f} {row.compile:7.2f} {pso:>7} {row.draw:7.2f} "
                     f"{row.bind:7.2f} {row.upload:7.2f} {row.present:7.2f} {row.sync:7.2f} {row.other:7.2f} "
                     f"{row.recording:6.2f} {row.waiting:6.2f} {row.shaders:7d} {row.psos:5d} {blame(row)}")
    return lines


def summary(rows: list[Second], below: float) -> dict:
    under = slow(rows, below)
    return {
        "seconds": len(rows),
        "slow": len(under),
        "slow_build": sum(blame(row) == "build" for row in under),
        "shaders": sum(row.shaders for row in rows),
        "psos": sum(row.psos for row in rows),
        "build_ms": round(sum(building_ms(row) for row in rows), 1),
        "lowest_fps": min((row.fps for row in rows), default=0.0),
    }


def _cell(row: Second | None) -> str:
    if row is None:
        return f"{'-':>7} {'-':>6} {'-':>7} {'-':>7} {'-':>4} {'-':>4} {'-':>5}"
    pso = "-" if row.pso is None else f"{row.pso:.2f}"
    return (f"{row.at:7.1f} {row.fps:6.1f} {row.compile:7.2f} {pso:>7} {row.shaders:4d} {row.psos:4d} "
            f"{blame(row) if row.fps < 29 else '':>5}")


def side_by_side(first: list[Second], second: list[Second], below: float, window: float = 1.5) -> list[str]:
    """Each second slow in either run, beside the other run's closest second within the window."""
    def nearest(rows: list[Second], at: float) -> Second | None:
        best = min(rows, key=lambda row: abs(row.at - at), default=None)
        return best if best is not None and abs(best.at - at) <= window else None

    pairs = [(row, nearest(second, row.at)) for row in slow(first, below)]
    paired = {id(other) for _, other in pairs if other is not None}
    pairs += [(nearest(first, row.at), row) for row in slow(second, below) if id(row) not in paired]
    pairs.sort(key=lambda pair: (pair[0] or pair[1]).at)
    header = f"{'at':>7} {'fps':>6} {'compile':>7} {'pso':>7} {'shdr':>4} {'pso#':>4} {'blame':>5}"
    return [f"{header} | {header}"] + [f"{_cell(a)} | {_cell(b)}" for a, b in pairs]


def _read(path: Path) -> list[Second]:
    log = path / "batman.log" if path.is_dir() else path
    return parse_log(log.read_text(encoding="utf-8", errors="replace"))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runs", type=Path, nargs="+", help="a run folder or log; two or more are shown side by side")
    parser.add_argument("--below", type=float, default=29.0)
    parser.add_argument("--from", dest="start", type=float, default=0.0, help="ignore seconds before this time")
    parser.add_argument("--side-by-side", action="store_true", help="two runs, each slow second beside the other's")
    args = parser.parse_args()
    if args.side_by_side:
        first, second = ([row for row in _read(path) if row.at >= args.start] for path in args.runs)
        print(f"== {args.runs[0]} | {args.runs[1]}")
        print("\n".join(side_by_side(first, second, args.below)))
        print(summary(first, args.below))
        print(summary(second, args.below))
        return
    for path in args.runs:
        rows = [row for row in _read(path) if row.at >= args.start]
        print(f"== {path}")
        print("\n".join(table(slow(rows, args.below))))
        print(summary(rows, args.below))


if __name__ == "__main__":
    main()
