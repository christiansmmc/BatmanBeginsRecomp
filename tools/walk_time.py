"""Development aid: how long a walk takes in a filmed run, and how two films of the same walk compare.

A walk from the same checkpoint with the same input ends at the same trigger whatever the speed, so the difference in
length between two films is the difference in walking speed. Used on capture.mkv of run_game.py --record.

  walk_time.py length <film> <from seconds> <to seconds> [--crop-4-3]
      the first movement and the camera's cut at its end
  walk_time.py compare <other film> <reference film> <start in other> <start in reference> <seconds>
      matches moments of the two films, each counted from its own movement start, and fits a line through the matches
      in stretches of the walk. A slope of 1 means both played that stretch at the same speed. Each whole picture is
      scaled to the same size, which is the right framing only when both films have the same widescreen setting, as the
      port and xemu do with the same port save. On an xemu new game use frame_timeline.py align, which crops to 4:3.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from frame_timeline import FPS, read_frames

IDLE_FRAMES = 15  # the film starts this long or more before the key
MOVED_FACTOR = 3.0
CUT_CHANGE = 25.0

MATCH_SIZE = 96, 72
HUD_ROWS, HUD_COLUMNS = 12, 10  # of MATCH_SIZE: the top and the sides, where the HUD is
LEAD_SECONDS = 1.0  # frames read before each film's start, so that a late start still finds its match
STEP_SECONDS = 0.1
FIRST_MOMENT = 0.3  # the first frames are nearly still and match anything
LAST_MARGIN = 0.5
SEARCH_FRAMES = 30  # a match further away than this is not the same moment
OUTLIER_SECONDS = 0.15
MIN_MOMENTS = 5


def walk_frames(frames: np.ndarray, cut_change: float = CUT_CHANGE) -> tuple[int, int]:
    """The frame where the picture first leaves its idle state, and the first later frame that changes all at once."""
    frames = frames.astype(np.float32)
    idle = frames[:IDLE_FRAMES].mean(axis=0)
    away = np.abs(frames - idle).mean(axis=(1, 2))
    start = int(np.argmax(away > away[:IDLE_FRAMES].max() * MOVED_FACTOR + 1))
    steps = np.abs(frames[1:] - frames[:-1]).mean(axis=(1, 2))
    cut = int(np.argmax(steps[start:] > cut_change)) + start + 1
    return start, cut


def seconds(walk: tuple[int, int]) -> float:
    return (walk[1] - walk[0]) / FPS


def _normalized(frames: np.ndarray) -> np.ndarray:
    frames = frames.astype(np.float32)[:, HUD_ROWS:, HUD_COLUMNS:-HUD_COLUMNS]
    frames = frames - frames.mean(axis=(1, 2), keepdims=True)
    return frames / (frames.std(axis=(1, 2), keepdims=True) + 1)


def match_frames(other: np.ndarray, reference: np.ndarray, seconds_after_start: float) -> np.ndarray:
    """Rows of (moment in the reference, the same moment in the other, error), in seconds from each film's start.

    Both films begin LEAD_SECONDS before their start. The match is refined to a fraction of a frame by a parabola."""
    other, reference = _normalized(other), _normalized(reference)
    rows = []
    for moment in np.arange(FIRST_MOMENT, seconds_after_start - LAST_MARGIN, STEP_SECONDS):
        index = int(round((moment + LEAD_SECONDS) * FPS))
        low, high = max(0, index - SEARCH_FRAMES), min(len(other), index + SEARCH_FRAMES)
        errors = ((other[low:high] - reference[index]) ** 2).mean(axis=(1, 2))
        best = int(errors.argmin())
        fraction = 0.0
        if 0 < best < len(errors) - 1:
            before, here, after = errors[best - 1], errors[best], errors[best + 1]
            if before - 2 * here + after > 0:
                fraction = (before - after) / (2 * (before - 2 * here + after))
        rows.append((moment, (low + best + fraction) / FPS - LEAD_SECONDS, float(errors[best])))
    return np.array(rows)


@dataclass
class Stretch:
    slope: float
    offset: float
    kept: int
    total: int
    residual: float
    error: float


def fit_stretch(rows: np.ndarray, start: float, end: float) -> Stretch | None:
    """A line through the moments from start to end, leaving out those far from it. None when too few remain."""
    inside = rows[(rows[:, 0] >= start) & (rows[:, 0] <= end)]
    if len(inside) < MIN_MOMENTS:
        return None
    slope, offset = np.polyfit(inside[:, 0], inside[:, 1], 1)
    kept = inside[np.abs(inside[:, 1] - (slope * inside[:, 0] + offset)) < OUTLIER_SECONDS]
    if len(kept) < MIN_MOMENTS:
        return None
    slope, offset = np.polyfit(kept[:, 0], kept[:, 1], 1)
    residual = float((kept[:, 1] - (slope * kept[:, 0] + offset)).std())
    return Stretch(float(slope), float(offset), len(kept), len(inside), residual, float(inside[:, 2].mean()))


def length(args: argparse.Namespace) -> None:
    walk = walk_frames(read_frames(args.film, args.start, args.end, crop_4_3=args.crop_4_3))
    print(f"moves at {args.start + walk[0] / FPS:.3f} s, cut at {args.start + walk[1] / FPS:.3f} s, "
          f"{walk[1] - walk[0]} frames, {seconds(walk):.3f} s")


def compare(args: argparse.Namespace) -> None:
    other = read_frames(args.other, args.other_start - LEAD_SECONDS, args.other_start + args.seconds, size=MATCH_SIZE)
    reference = read_frames(args.reference, args.reference_start - LEAD_SECONDS, args.reference_start + args.seconds,
                            size=MATCH_SIZE)
    rows = match_frames(other, reference, args.seconds)
    last = args.seconds - LAST_MARGIN
    stretches = [(start, min(start + 5.0, last)) for start in np.arange(0.0, last, 5.0)]
    for start, end in [*stretches, (FIRST_MOMENT, FIRST_MOMENT + 10.0), (FIRST_MOMENT, last)]:
        fit = fit_stretch(rows, start, end)
        if fit:
            print(f"{start:4.1f} to {end:4.1f} s: slope {fit.slope:.4f}, offset {fit.offset:+.3f} s, "
                  f"{fit.kept} of {fit.total} moments, residual {fit.residual:.3f} s, error {fit.error:.3f}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(required=True)

    command = commands.add_parser("length")
    command.add_argument("film", type=Path)
    command.add_argument("start", type=float)
    command.add_argument("end", type=float)
    command.add_argument("--crop-4-3", action="store_true", help="the port's film: use the middle 4:3")
    command.set_defaults(run=length)

    command = commands.add_parser("compare")
    command.add_argument("other", type=Path)
    command.add_argument("reference", type=Path)
    command.add_argument("other_start", type=float)
    command.add_argument("reference_start", type=float)
    command.add_argument("seconds", type=float)
    command.set_defaults(run=compare)

    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
