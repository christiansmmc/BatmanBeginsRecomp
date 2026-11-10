"""Development aid: times filmed runs, the capture.mkv of run_game.py --record.

cuts: when the picture goes black, comes back or changes all at once.
align: matches the frames of a stretch of one film against another and fits a line through the matches. A slope of 1
means both played that stretch at the same speed. The port's wider picture is cropped to the middle 4:3 first, which is
right against an xemu on a new game; with a port save in xemu both make the same picture, so use --same-framing.
period: the time a stretch takes to repeat, the stride while walking with the camera following. Frames are films' own,
so a filmed rate under the game's only costs precision.
"""
from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

import numpy as np

FPS = 30.0
SIZE = 64, 48


def read_frames(path: Path, start: float, end: float | None, crop_4_3: bool = False, size=SIZE) -> np.ndarray:
    crop = "crop=ih*4/3:ih," if crop_4_3 else ""
    command = ["ffmpeg", "-loglevel", "error", "-ss", str(start)]
    if end is not None:
        command += ["-to", str(end)]
    command += ["-i", str(path), "-vf", f"{crop}scale={size[0]}:{size[1]},format=gray", "-f", "rawvideo", "-"]
    raw = subprocess.run(command, check=True, capture_output=True).stdout
    return np.frombuffer(raw, np.uint8).reshape(-1, size[1], size[0])


def fit_line(xs: np.ndarray, ys: np.ndarray) -> tuple[float, float]:
    slope, offset = np.polyfit(xs, ys, 1)
    return float(slope), float(offset)


def cuts(args: argparse.Namespace) -> None:
    frames = read_frames(args.film, args.start, args.end)
    mean = frames.mean(axis=(1, 2))
    change = np.abs(frames[1:].astype(np.int16) - frames[:-1]).mean(axis=(1, 2))
    print(f"{len(frames)} frames, {len(frames) / FPS:.2f} s")
    state = None
    for index, value in enumerate(mean):
        now = "black" if value < args.black else "picture"
        if now != state:
            print(f"{args.start + index / FPS:8.2f} s  {now}  (mean {value:.1f})")
            state = now
    for index, value in enumerate(change):
        if value >= args.cut:
            print(f"{args.start + (index + 1) / FPS:8.2f} s  cut  (change {value:.1f})")


def align(args: argparse.Namespace) -> None:
    ref = read_frames(args.reference, args.ref_start, args.ref_end).astype(np.float32)
    other = read_frames(args.other, args.other_start, args.other_end, crop_4_3=not args.same_framing).astype(np.float32)
    xs, ys, errors_of = [], [], []
    for index in range(0, len(ref), args.step):
        errors = ((other - ref[index]) ** 2).mean(axis=(1, 2))
        best = int(errors.argmin())
        xs.append(index / FPS + args.ref_start)
        ys.append(best / FPS + args.other_start)
        errors_of.append(float(errors[best]))
        print(f"{xs[-1]:8.2f} -> {ys[-1]:8.2f}  error {errors[best]:7.1f}")
    xs, ys = np.array(xs), np.array(ys)
    slope, offset = fit_line(xs, ys)
    keep = np.abs(ys - (slope * xs + offset)) < 0.5  # a repeated picture matches anywhere, the line ignores those
    slope, offset = fit_line(xs[keep], ys[keep])
    print(f"slope {slope:.4f} from {keep.sum()} of {len(xs)} frames, offset {offset:.2f} s")
    kept = np.array(errors_of)[keep]
    print(f"mean error {kept.mean():.1f}, worst {kept.max():.1f}")


def period(args: argparse.Namespace) -> None:
    frames = read_frames(args.film, args.start, args.end, crop_4_3=args.crop_4_3).astype(np.float32)
    height, width = frames.shape[1:]
    frames = frames[:, height // 4:height * 3 // 4, width // 4:width * 3 // 4]  # the character, with the camera following
    # the scene drifting past is slow: only what changes faster than a stride is kept
    window = 15
    kernel = np.ones(window) / window
    slow = np.apply_along_axis(lambda line: np.convolve(line, kernel, mode="same"), 0, frames)
    frames = (frames - slow)[window:-window]
    lags = range(args.min_lag, min(args.max_lag, len(frames) // 2))
    scores = []
    for lag in lags:
        a, b = frames[:-lag].ravel(), frames[lag:].ravel()
        scores.append(float(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b))))
    peaks = [(lags[i], scores[i]) for i in range(1, len(scores) - 1) if scores[i - 1] < scores[i] >= scores[i + 1]]
    for lag, score in sorted(peaks, key=lambda item: -item[1])[:6]:
        print(f"lag {lag:3d} frames = {lag / FPS:6.3f} s, correlation {score:.3f}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(required=True)

    command = commands.add_parser("cuts")
    command.add_argument("film", type=Path)
    command.add_argument("--start", type=float, default=0.0)
    command.add_argument("--end", type=float)
    command.add_argument("--cut", type=float, default=25.0, help="mean change from the frame before that counts as a cut")
    command.add_argument("--black", type=float, default=6.0, help="mean brightness below this is black")
    command.set_defaults(run=cuts)

    command = commands.add_parser("align")
    command.add_argument("reference", type=Path)
    command.add_argument("other", type=Path)
    command.add_argument("--ref-start", type=float, default=0.0)
    command.add_argument("--ref-end", type=float)
    command.add_argument("--other-start", type=float, default=0.0)
    command.add_argument("--other-end", type=float)
    command.add_argument("--step", type=int, default=15, help="reference frames between matches")
    command.add_argument("--same-framing", action="store_true",
                         help="both films have the same widescreen setting: no 4:3 crop")
    command.set_defaults(run=align)

    command = commands.add_parser("period")
    command.add_argument("film", type=Path)
    command.add_argument("--start", type=float, default=0.0)
    command.add_argument("--end", type=float)
    command.add_argument("--min-lag", type=int, default=6)
    command.add_argument("--max-lag", type=int, default=60)
    command.add_argument("--crop-4-3", action="store_true", help="the port's film: use the middle 4:3")
    command.set_defaults(run=period)

    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
