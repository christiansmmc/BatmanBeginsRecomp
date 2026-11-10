"""Development aid: whether sound stayed with picture between two filmed runs of the port, each with --audio.

Each run has two clocks, the film and the sound file, started at unrelated moments. The opening movie is a clapper: its
picture and sound are locked by the file, so the delay of run B against run A measured on the movie tells how the two
clocks of B sit against those of A. Then the same is measured on the real-time scene. If the scene's picture and sound
moved by different amounts from the movie's, the sound moved against the picture by that difference.

  voice_sync.py <run A> <run B> --movie 10,40 --scene 45,75 [--guess -6.6] [--sound-a 69.5]
      times are seconds in film A; --sound-a says how long after sound A film A starts, which only needs to be near.
"""
from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

import numpy as np

from frame_timeline import SIZE, read_frames

RATE = 8000
FILM_RATE = 30.0
SOUND_SECONDS = 3.0
SOUND_REACH = 5.0
PICTURE_REACH = 45  # film frames
QUIET = 1e-5


def read_sound(path: Path) -> np.ndarray:
    raw = subprocess.run(["ffmpeg", "-v", "quiet", "-i", str(path), "-ac", "1", "-ar", str(RATE), "-f", "f32le", "-"],
                         check=True, capture_output=True).stdout
    return np.frombuffer(raw, np.float32)


def sound_delay(a: np.ndarray, b: np.ndarray, at: float, seconds: float = SOUND_SECONDS,
                reach: float = SOUND_REACH) -> tuple[float, float]:
    """Where the stretch of a from second `at` lies in b, in seconds later than in a, and how well it matches (0 to 1)."""
    start, length = int(at * RATE), int(seconds * RATE)
    piece = a[start:start + length].astype(np.float64)
    if len(piece) < length or (piece * piece).mean() < QUIET:
        return 0.0, 0.0
    low = max(0, start - int(reach * RATE))
    other = b[low:start + length + int(reach * RATE)].astype(np.float64)
    size = len(piece) + len(other)
    product = np.fft.irfft(np.fft.rfft(other, size) * np.conj(np.fft.rfft(piece - piece.mean(), size)), size)[:len(other)]
    index = int(np.argmax(np.abs(product)))
    match = other[index:index + length]
    if len(match) < length or (match * match).sum() == 0:
        return 0.0, 0.0
    score = float(np.abs(product[index]) / np.sqrt((piece * piece).sum() * (match * match).sum()))
    return (low + index - start) / RATE, score


def sync_shift(movie_picture: float, movie_sound: float, scene_picture: float, scene_sound: float) -> float:
    """How much later the picture is against the sound in the scene than the two runs' clocks explain."""
    return (scene_picture - movie_picture) - (scene_sound - movie_sound)


def picture_delay(a: np.ndarray, b: np.ndarray, start: int, count: int, reach: int = PICTURE_REACH,
                  step: int = 3) -> float:
    """Seconds by which b shows the frames of a later, the median over a stretch, each refined to a fraction of a frame."""
    a, b = a.astype(np.float32), b.astype(np.float32)
    delays = []
    for index in range(start, min(start + count, len(a)), step):
        low, high = max(0, index - reach), min(len(b), index + reach)
        errors = ((b[low:high] - a[index]) ** 2).mean(axis=(1, 2))
        best = int(errors.argmin())
        fraction = 0.0
        if 0 < best < len(errors) - 1:
            before, here, after = errors[best - 1], errors[best], errors[best + 1]
            if before - 2 * here + after > 0:
                fraction = float(before - after) / float(2 * (before - 2 * here + after))
        delays.append(low + best + fraction - index)
    return float(np.median(delays)) / FILM_RATE


def _pair(text: str) -> tuple[float, float]:
    first, second = text.split(",")
    return float(first), float(second)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run_a", type=Path)
    parser.add_argument("run_b", type=Path)
    parser.add_argument("--movie", type=_pair, required=True)
    parser.add_argument("--scene", type=_pair, required=True)
    parser.add_argument("--guess", type=float, default=0.0, help="film B later than film A by about this")
    parser.add_argument("--sound-a", type=float, default=0.0, help="film A starts this long after sound A")
    parser.add_argument("--window", type=float, default=5.0)
    args = parser.parse_args()

    sound_a, sound_b = read_sound(args.run_a / "audio.wav"), read_sound(args.run_b / "audio.wav")
    first, last = min(args.movie[0], args.scene[0]), max(args.movie[1], args.scene[1])
    film_a = read_frames(args.run_a / "capture.mkv", first, last, size=SIZE)
    film_b = read_frames(args.run_b / "capture.mkv", first + args.guess, last + args.guess, size=SIZE)

    def measure(start: float, end: float) -> tuple[float, float, float]:
        picture = picture_delay(film_a, film_b, int((start - first) * FILM_RATE), int((end - start) * FILM_RATE))
        delays, scores = [], []
        for at in np.arange(start, end - SOUND_SECONDS, SOUND_SECONDS):
            delay, score = sound_delay(sound_a, sound_b, at + args.sound_a, reach=SOUND_REACH + abs(args.guess))
            if score > 0.5:
                delays.append(delay)
                scores.append(score)
        sound = float(np.median(delays)) if delays else float("nan")
        return picture + args.guess, sound, float(np.median(scores)) if scores else 0.0

    movie_picture, movie_sound, movie_score = measure(*args.movie)
    print(f"movie {args.movie}: picture {movie_picture:+.3f} s, sound {movie_sound:+.3f} s, match {movie_score:.2f}")
    for start in np.arange(args.scene[0], args.scene[1], args.window):
        end = min(start + args.window, args.scene[1])
        picture, sound, score = measure(start, end)
        shift = sync_shift(movie_picture, movie_sound, picture, sound)
        print(f"scene {start:6.1f} to {end:6.1f}: picture {picture:+.3f} s, sound {sound:+.3f} s, match {score:.2f}, "
              f"picture against sound {shift:+.3f} s")


if __name__ == "__main__":
    main()
