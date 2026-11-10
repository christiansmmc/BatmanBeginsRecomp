"""Plays each XMV movie in the game through BB_DEV_MOVIE and measures its length, sound and picture against the file."""
from __future__ import annotations

import argparse
import csv
import ctypes
import json
import math
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import run_game

ROOT = Path(__file__).resolve().parent.parent
MOVIES = ROOT / "assets" / "Batman" / "Binary" / "_bin_xb" / "PSS_US"
RUNS = ROOT / "runs" / "movies"
REPORT = RUNS / "report.csv"
HOLD_REPORT = RUNS / "report_hold.csv"

TOLERANCE_FRACTION = 0.02
TOLERANCE_MIN_SECONDS = 0.5
# time from launch to the first movie's open, and the time a run may take beyond its movie before it counts as stuck
BOOT_SECONDS = 8.2
RUN_MARGIN_SECONDS = 25.0

SILENT_DB = -80.0  # a file this quiet has no sound to find
SOUND_MARGIN_DB = 6.0
SOUND_RATE = 8000
MIN_SOUND_MATCH = 0.5
MIN_MATCH = 0.5
FLAT_STD = 3.0
MEAN_MARGIN = 16.0
COMPARE_WIDTH, COMPARE_HEIGHT = 160, 120
# where the first movie's slot draws a 640x480 picture across a 16:9 window, measured on 01_001us.xmv; full height
PICTURE_LEFT, PICTURE_RIGHT = 0.086, 0.9125
SEARCH_SECONDS = 3.0
FLAT_SEARCH_SECONDS = 1.0
COMPARE_FPS = 60  # above every movie's rate, so no frame is skipped: a fast cut leaves few frames to match
SHOT_FRACTIONS = (0.25, 0.5, 0.75)
MAX_RUNS = 5

# --hold: the button goes down this long after the movie opens, and stays down past the skip
HOLD_AT_SECONDS = 2.0
FIRST_OPEN_SECONDS = 7.15  # measured: the first logo's slot opens 7.13 to 7.15 s after launch
HOLD_KEY_SECONDS = 1.5
HOLD_MIN_MOVIE_SECONDS = 4.0
HOLD_CLOSE_SECONDS = 1.0
HOLD_CLOSE_MARGIN = 0.25
# time the program stays up after the close, to hear whether the sound stopped; the next movie opens in about 0.4 s
HOLD_TAIL_SECONDS = 1.0
AFTER_CLOSE_SKIP_SECONDS = 0.1  # what the mixer had already handed to the output
# not silence: the game itself plays something in that gap, -42.5 to -47.2 dBFS after EA's natural end in runs/options-scene-none-*
AFTER_CLOSE_SILENT_DB = -40.0

HOLD_COLUMNS = ["file", "passed", "expected_s", "hold_at_s", "skipped_after_hold_s", "close_after_hold_s", "before_rms_db",
                "after_rms_db", "crashed", "user_game_seen", "run_dir"]

COLUMNS = ["file", "passed", "expected_s", "header_s", "played_s", "speed_ok", "source_rms_db", "port_rms_db",
           "sound_match", "sound_lag_s", "sound_ok", "shot", "shot_mean", "source_mean", "source_std", "match",
           "match_time_s", "shot_time_s", "worst_match", "worst_offset_s", "picture_ok", "crashed", "user_game_seen",
           "run_dir"]


def evaluate(expected_s: float, played_s: float) -> bool:
    return abs(played_s - expected_s) <= max(TOLERANCE_FRACTION * expected_s, TOLERANCE_MIN_SECONDS)


def find_movies() -> list[Path]:
    return sorted(MOVIES.glob("*.xmv"), key=lambda path: path.name.lower())


def _movie_times(log: str, event: str, name: str | None) -> list[tuple[str, float]]:
    found = re.findall(rf"movie {event} (\S+) ([\d.]+)", log)
    return [(movie, float(at)) for movie, at in found if name is None or movie == name]


def open_time(log: str, name: str | None = None) -> float | None:
    opened = _movie_times(log, "open", name)
    return opened[0][1] if opened else None


def played_seconds(log: str, name: str | None = None) -> float | None:
    """From the movie's open to its close in the game's log."""
    opened, closed = _movie_times(log, "open", name), _movie_times(log, "close", name)
    return closed[0][1] - opened[0][1] if opened and closed else None


def ffprobe_duration(path) -> float | None:
    probe = subprocess.run(["ffprobe", "-v", "quiet", "-show_entries", "format=duration", "-of", "json", str(path)],
                           capture_output=True, text=True, check=True)
    duration = json.loads(probe.stdout).get("format", {}).get("duration")
    return float(duration) if duration not in (None, "N/A") else None


def last_packet_time(path) -> float | None:
    probe = subprocess.run(["ffprobe", "-v", "quiet", "-show_entries", "packet=pts_time,dts_time", "-of", "csv=p=0",
                            str(path)], capture_output=True, text=True, check=True)
    times = re.findall(r"\d+\.\d+", probe.stdout)
    return max((float(at) for at in times), default=None)


def expected_duration(path) -> float:
    """The packets, not the header: ea_US.xmv's header says 6.408 s and its packets, which the game plays, end at 3.16 s."""
    last_packet = last_packet_time(path)
    return last_packet if last_packet is not None else ffprobe_duration(path)


def decode_audio(path, stream: str | None = None) -> np.ndarray:
    """A mono copy for comparing waveforms; empty when there is no audio to decode."""
    select = ["-map", stream] if stream else []
    decode = subprocess.run(["ffmpeg", "-v", "quiet", "-i", str(path), *select, "-ac", "1", "-ar", str(SOUND_RATE),
                             "-f", "f32le", "-"], capture_output=True)
    return np.frombuffer(decode.stdout, dtype=np.float32).astype(np.float64)


def source_rms_db(path) -> float | None:
    """The level of the file's own sound, None when it has no audio stream."""
    decode = subprocess.run(["ffmpeg", "-v", "quiet", "-i", str(path), "-map", "0:a:0", "-ac", "2", "-f", "f32le", "-"],
                            capture_output=True)
    if decode.returncode != 0 or not decode.stdout:
        return None
    samples = np.frombuffer(decode.stdout, dtype=np.float32)
    power = float(np.mean(np.square(samples, dtype=np.float64)))
    return 10 * math.log10(power) if power > 0 else -math.inf


def waveform_match(recorded: np.ndarray, source: np.ndarray) -> tuple[float, float]:
    """How well the source's waveform is found in a recording, and where. At 3% off speed this falls near 0."""
    recorded, source = recorded - recorded.mean(), source - source.mean()
    scale = math.sqrt(float(recorded @ recorded) * float(source @ source))
    if scale < 1e-9:
        return 0.0, 0.0
    size = len(recorded) + len(source)
    crossed = np.fft.irfft(np.fft.rfft(recorded, size) * np.conj(np.fft.rfft(source, size)), size)
    peak = int(np.argmax(np.abs(crossed)))
    lag = peak if peak < len(recorded) else peak - size
    return float(abs(crossed[peak])) / scale, lag / SOUND_RATE


def sound_ok(source_db: float | None, port_db: float | None, match: float | None) -> bool:
    if source_db is None or source_db < SILENT_DB:
        return True
    if port_db is None or match is None:
        return False
    return abs(port_db - source_db) <= SOUND_MARGIN_DB and match >= MIN_SOUND_MATCH


def source_frames(path, start_s: float, end_s: float) -> tuple[np.ndarray, list[float]]:
    """Grey frames of the file between two times, at a fixed rate, and their times."""
    start_s = max(start_s, 0.0)
    decode = subprocess.run(
        ["ffmpeg", "-v", "quiet", "-i", str(path), "-ss", f"{start_s:.3f}", "-t", f"{end_s - start_s:.3f}", "-an",
         "-vf", f"fps={COMPARE_FPS},scale={COMPARE_WIDTH}:{COMPARE_HEIGHT},format=gray", "-f", "rawvideo", "-"],
        capture_output=True, check=True)
    frames = np.frombuffer(decode.stdout, dtype=np.uint8)
    frames = frames[:len(frames) // (COMPARE_WIDTH * COMPARE_HEIGHT) * (COMPARE_WIDTH * COMPARE_HEIGHT)]
    frames = frames.reshape(-1, COMPARE_HEIGHT, COMPARE_WIDTH)
    return frames, [start_s + index / COMPARE_FPS for index in range(len(frames))]


def shot_grey(path) -> np.ndarray:
    with Image.open(path) as image:
        area = (int(image.width * PICTURE_LEFT), 0, int(image.width * PICTURE_RIGHT), image.height)
        return np.asarray(image.convert("L").crop(area).resize((COMPARE_WIDTH, COMPARE_HEIGHT)), dtype=np.float64)


def correlation(first: np.ndarray, second: np.ndarray) -> float:
    a = first.astype(np.float64).ravel()
    b = second.astype(np.float64).ravel()
    a, b = a - a.mean(), b - b.mean()
    scale = math.sqrt(float(a @ a) * float(b @ b))
    return float(a @ b) / scale if scale > 1e-6 else 0.0


def best_match(shot: np.ndarray, frames: np.ndarray) -> tuple[float, int]:
    scores = [correlation(shot, frame) for frame in frames]
    index = int(np.argmax(scores))
    return scores[index], index


def compare_shot(shot: np.ndarray, frames: np.ndarray, times: list[float], shot_time: float) -> dict:
    """The frame the shot best matches. A flat shot matches nothing, so the nearest frames must be flat like it."""
    if shot.std() < FLAT_STD:
        near = [index for index, at in enumerate(times) if abs(at - shot_time) <= FLAT_SEARCH_SECONDS]
        near = near or [int(np.argmin([abs(at - shot_time) for at in times]))]
        index = min(near, key=lambda i: abs(float(frames[i].mean()) - float(shot.mean())))
        match = 0.0
    else:
        match, index = best_match(shot, frames)
    return dict(shot_mean=float(shot.mean()), source_mean=float(frames[index].mean()),
                source_std=float(frames[index].std()), match=match, match_time_s=times[index], shot_time_s=shot_time)


def picture_ok(shot_mean: float, source_mean: float, source_std: float, match: float) -> bool:
    """A flat frame (a fade, a black gap) has nothing to correlate, so only its brightness can be compared."""
    same_brightness = abs(shot_mean - source_mean) <= MEAN_MARGIN
    return same_brightness if source_std < FLAT_STD else same_brightness and match >= MIN_MATCH


def _check_ok(check: dict) -> bool:
    return picture_ok(check["shot_mean"], check["source_mean"], check["source_std"], check["match"])


def finish_row(row: dict) -> dict:
    played = row["played_s"]
    speed = played is not None and evaluate(row["expected_s"], played)
    sound = sound_ok(row["source_rms_db"], row["port_rms_db"], row["sound_match"])
    checks = row["checks"]
    picture = bool(checks) and all(_check_ok(check) for check in checks)
    passed = speed and sound and picture and not row["crashed"]
    finished = {key: value for key, value in row.items() if key != "checks"}
    if checks:
        finished.update(checks[len(checks) // 2])
        finished["worst_match"] = min(check["match"] for check in checks)
        finished["worst_offset_s"] = max(abs(check["match_time_s"] - check["shot_time_s"]) for check in checks)
    return {**finished, "speed_ok": speed, "sound_ok": sound, "picture_ok": picture, "passed": passed}


def write_report(path: Path, rows: list[dict], columns: list[str] = COLUMNS) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, columns)
        writer.writeheader()
        writer.writerows(rows)


def run_until_undisturbed(config) -> dict:
    """A run the user's game opened during has suspect timing, so it is repeated."""
    for _ in range(MAX_RUNS):
        result = run_game.run(config)
        if not result["user_game_seen"]:
            return result
    return result


def measure_shots(path: Path, run_dir: Path, shots: list[dict], opened: float) -> list[dict]:
    checks = []
    for shot in shots:
        if not shot.get("file"):
            continue
        shot_time = shot["at"] - opened
        frames, times = source_frames(path, shot_time - SEARCH_SECONDS, shot_time + SEARCH_SECONDS)
        if not len(frames):
            continue
        check = compare_shot(shot_grey(run_dir / shot["file"]), frames, times, shot_time)
        checks.append({**check, "shot": str(run_dir / shot["file"])})
    return checks


def newest_run(stem: str) -> Path | None:
    runs = [path for path in RUNS.glob(f"{stem}-*") if re.fullmatch(rf"{re.escape(stem)}-\d{{8}}-\d{{6}}(-\d+)?", path.name)]
    return max(runs, key=lambda path: path.name, default=None)


def measure_movie(path: Path) -> dict:
    expected = expected_duration(path)
    exe = run_game.DEFAULT_EXE
    run_game.ensure_windowed_settings(exe.parent / "settings.toml")
    result = run_until_undisturbed(run_game.RunConfig(
        name=path.stem,
        seconds=expected + RUN_MARGIN_SECONDS,
        command=[str(exe)],
        cwd=exe.parent,
        runs_dir=RUNS,
        shots=[round(BOOT_SECONDS + expected * fraction, 3) for fraction in SHOT_FRACTIONS],
        env={"XBE_PERF": "1", run_game.NO_ACTIVATE_ENV: "1", "BB_DEV_MOVIE": path.name},
        audio=True,
    ))
    return measure_run(path, expected, result)


def recheck_movie(path: Path) -> dict:
    """Measures the newest run again without playing it again."""
    run_dir = newest_run(path.stem)
    return measure_run(path, expected_duration(path), json.loads((run_dir / "result.json").read_text(encoding="utf-8")))


def measure_run(path: Path, expected: float, result: dict) -> dict:
    run_dir = Path(result["run_dir"])
    log_file = run_dir / "batman.log"
    log = log_file.read_text(encoding="utf-8", errors="replace") if log_file.exists() else ""
    opened, played = open_time(log, path.name), played_seconds(log, path.name)
    source_db = source_rms_db(path)
    row = dict(file=path.name, expected_s=expected, header_s=ffprobe_duration(path), played_s=played,
               source_rms_db=source_db, port_rms_db=None, sound_match=None, sound_lag_s=None, checks=[],
               crashed=result["crashed"], user_game_seen=result["user_game_seen"], run_dir=str(run_dir))
    audio = run_dir / "audio.wav"
    if audio.exists() and opened is not None and played is not None:
        row["port_rms_db"] = run_game.wav_rms_dbfs(audio, opened, opened + played)
        if source_db is not None:
            row["sound_match"], row["sound_lag_s"] = waveform_match(decode_audio(audio), decode_audio(path, "0:a:0"))
    if opened is not None:
        row["checks"] = measure_shots(path, run_dir, result["shots"], opened)
    return finish_row(row)


def long_enough_to_hold(expected_s: float) -> bool:
    return expected_s > HOLD_MIN_MOVIE_SECONDS


def hold_times(log: str, name: str) -> dict:
    """When the movie opened, when the hold started to count, the skip, the close, and the next movie's open."""
    def first(event: str) -> float | None:
        found = _movie_times(log, event, name)
        return found[0][1] if found else None

    opened = first("open")
    later = [at for movie, at in _movie_times(log, "open", None) if opened is not None and at > opened]
    return dict(opened=opened, held=first("hold"), skipped=first("skipped"), closed=first("close"),
                next_open=min(later) if later else None)


def hold_ok(hold_at: float | None, close_after_hold: float | None, after_rms: float | None, crashed: bool) -> bool:
    if crashed or hold_at is None or close_after_hold is None:
        return False
    if abs(close_after_hold - HOLD_CLOSE_SECONDS) > HOLD_CLOSE_MARGIN:
        return False
    return after_rms is None or after_rms <= AFTER_CLOSE_SILENT_DB


def measure_hold(path: Path) -> dict:
    expected = expected_duration(path)
    exe = run_game.DEFAULT_EXE
    run_game.ensure_windowed_settings(exe.parent / "settings.toml")
    press_at = FIRST_OPEN_SECONDS + HOLD_AT_SECONDS
    result = run_until_undisturbed(run_game.RunConfig(
        name=f"hold-{path.stem}",
        seconds=press_at + HOLD_CLOSE_SECONDS + HOLD_TAIL_SECONDS + RUN_MARGIN_SECONDS,
        command=[str(exe)],
        cwd=exe.parent,
        runs_dir=RUNS,
        keys=[(press_at, "SPACE", HOLD_KEY_SECONDS)],
        env={"XBE_PERF": "1", run_game.NO_ACTIVATE_ENV: "1", "BB_DEV_MOVIE": path.name,
             "BB_DEV_MOVIE_TAIL_MS": str(int(HOLD_TAIL_SECONDS * 1000))},
        audio=True,
    ))
    run_dir = Path(result["run_dir"])
    log_file = run_dir / "batman.log"
    log = log_file.read_text(encoding="utf-8", errors="replace") if log_file.exists() else ""
    times = hold_times(log, path.name)
    row = dict(file=path.name, expected_s=expected, crashed=result["crashed"], user_game_seen=result["user_game_seen"],
               run_dir=str(run_dir), hold_at_s=None, skipped_after_hold_s=None, close_after_hold_s=None, before_rms_db=None,
               after_rms_db=None)
    audio = run_dir / "audio.wav"
    if times["held"] is not None and times["opened"] is not None:
        row["hold_at_s"] = times["held"] - times["opened"]
        if times["skipped"] is not None:
            row["skipped_after_hold_s"] = times["skipped"] - times["held"]
        if times["closed"] is not None:
            row["close_after_hold_s"] = times["closed"] - times["held"]
    if audio.exists() and times["closed"] is not None and times["opened"] is not None:
        row["before_rms_db"] = run_game.wav_rms_dbfs(audio, times["opened"], times["closed"])
        after_start = times["closed"] + AFTER_CLOSE_SKIP_SECONDS
        after_end = min(times["closed"] + HOLD_TAIL_SECONDS, times["next_open"] or math.inf)
        if after_end > after_start:
            row["after_rms_db"] = run_game.wav_rms_dbfs(audio, after_start, after_end)
    row["passed"] = hold_ok(row["hold_at_s"], row["close_after_hold_s"], row["after_rms_db"], result["crashed"])
    return row


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", action="append", default=[], help="a movie's file name; all of them without this")
    parser.add_argument("--report", type=Path, default=REPORT)
    parser.add_argument("--recheck", action="store_true", help="measure the newest runs again without playing them")
    parser.add_argument("--hold", action="store_true",
                        help="hold the skip button 2 s into each movie over 4 s long, and see it close a second later")
    return parser.parse_args(argv)


def sweep_holds(wanted: set[str], report: Path) -> int:
    rows = []
    for path in find_movies():
        if wanted and path.name.lower() not in wanted:
            continue
        if not long_enough_to_hold(expected_duration(path)):
            print(f"{path.name}: too short for the hold", flush=True)
            continue
        rows.append(measure_hold(path))
        write_report(report, rows, HOLD_COLUMNS)
        row = rows[-1]
        print(f"{row['file']}: {'PASS' if row['passed'] else 'FAIL'} hold at {row['hold_at_s']} closed after "
              f"{row['close_after_hold_s']} sound {row['before_rms_db']} then {row['after_rms_db']}", flush=True)
    passed = sum(bool(row["passed"]) for row in rows)
    print(f"{passed} of {len(rows)} passed; report in {report}", flush=True)
    return 0 if passed == len(rows) else 1


def main(argv: list[str]) -> int:
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except OSError:
        pass
    args = parse_args(argv)
    wanted = {name.lower() for name in args.only}
    rows = []
    if args.hold:
        return sweep_holds(wanted, args.report if args.report != REPORT else HOLD_REPORT)
    for path in find_movies():
        if wanted and path.name.lower() not in wanted:
            continue
        rows.append(recheck_movie(path) if args.recheck else measure_movie(path))
        write_report(args.report, rows)
        row = rows[-1]
        print(f"{row['file']}: {'PASS' if row['passed'] else 'FAIL'} expected {row['expected_s']:.2f} played "
              f"{row['played_s']} level {row['source_rms_db']} / {row['port_rms_db']} wave {row['sound_match']} "
              f"picture {row.get('worst_match')}", flush=True)
    passed = sum(bool(row["passed"]) for row in rows)
    print(f"{passed} of {len(rows)} passed; report in {args.report}", flush=True)
    return 0 if passed == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
