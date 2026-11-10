"""Runs the game for a while and keeps the evidence: screenshots, logs, crash dumps and a result.json."""
from __future__ import annotations

import argparse
import array
import ctypes
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import time
from ctypes import wintypes
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_EXE = ROOT / "out" / "build" / "release" / "BatmanBegins.exe"
LOG_TAIL_LINES = 40
POLL_SECONDS = 0.02
WINDOWED_SETTINGS = 'display_mode = "windowed"\nwidth = 1280\nheight = 720\n'
NO_ACTIVATE_ENV = "XBE_NO_ACTIVATE"  # the port's window opens without taking the keyboard from what the user is doing

USER_GAME_WAIT_SECONDS = 20
USER_GAME_CHECK_SECONDS = 1.0
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
MAX_PROCESS_IDS = 4096
ROTATED_LOG = re.compile(r"\.\d+\.log$", re.IGNORECASE)  # an older session's, batman.1.log, moved along by the game

WM_SETFOCUS, WM_KILLFOCUS, WM_ACTIVATE, WM_ACTIVATEAPP = 0x0007, 0x0008, 0x0006, 0x001C
WM_KEYDOWN, WM_KEYUP = 0x0100, 0x0101
WM_CLOSE = 0x0010
CLOSE_WAIT_SECONDS = 5
GRAB_TIMEOUT_SECONDS = 10
DEFAULT_KEY_HOLD = 0.15
EXTENDED_KEY_BIT = 1 << 24
EXTENDED_KEYS = {"UP", "DOWN", "LEFT", "RIGHT"}
# virtual key and scan code; the scan code is what SDL reads
KEYS = {
    "RETURN": (0x0D, 0x1C), "BACKSPACE": (0x08, 0x0E), "ESCAPE": (0x1B, 0x01), "SPACE": (0x20, 0x39),
    "UP": (0x26, 0x48), "DOWN": (0x28, 0x50), "LEFT": (0x25, 0x4B), "RIGHT": (0x27, 0x4D),
    "A": (0x41, 0x1E), "B": (0x42, 0x30), "X": (0x58, 0x2D), "Y": (0x59, 0x15),
    "W": (0x57, 0x11), "S": (0x53, 0x1F), "D": (0x44, 0x20),
    "C": (0x43, 0x2E), "E": (0x45, 0x12), "Q": (0x51, 0x10), "R": (0x52, 0x13), "Z": (0x5A, 0x2C), "V": (0x56, 0x2F),
    "I": (0x49, 0x17), "J": (0x4A, 0x24), "K": (0x4B, 0x25), "L": (0x4C, 0x26),
    "TAB": (0x09, 0x0F), "LSHIFT": (0x10, 0x2A), "LCTRL": (0x11, 0x1D),
    "F": (0x46, 0x21), "F3": (0x72, 0x3D),
}


@dataclass
class RunConfig:
    name: str
    seconds: float
    command: list[str]
    cwd: Path
    runs_dir: Path = ROOT / "runs"
    script: Path | None = None
    shots: list[float] = field(default_factory=list)
    env: dict[str, str] = field(default_factory=dict)
    focus_away: tuple[float, float] | None = None
    audio: bool = False
    keys: list[tuple[float, str, float]] = field(default_factory=list)
    fresh_saves: bool = False
    audio_window: tuple[float, float] | None = None
    record: tuple[float, float] | None = None
    player_env: bool = False


def parse_keys(text: str) -> list[tuple[float, str, float]]:
    """Comma separated "seconds:KEY" or "seconds:KEY:seconds held"."""
    presses = []
    for item in filter(None, text.split(",")):
        at, key, *held = item.split(":")
        if key not in KEYS:
            raise ValueError(f"unknown key: {key}")
        presses.append((float(at), key, float(held[0]) if held else DEFAULT_KEY_HOLD))
    return presses


def build_timeline(shots: list[float], focus_away: tuple[float, float] | None,
                   keys: list[tuple[float, str, float]] = (),
                   record: tuple[float, float] | None = None) -> list[tuple[float, str]]:
    timeline = [(at, "shot") for at in shots]
    if record:
        timeline += [(record[0], "record_on"), (record[1], "record_off")]
    if focus_away:
        timeline += [(focus_away[0], "focus_off"), (focus_away[1], "focus_on")]
    for at, key, held in keys:
        timeline += [(at, f"key_down:{key}"), (at + held, f"key_up:{key}")]
    return sorted(timeline)


def succeeded(result: dict) -> bool:
    return not result["crashed"] and (result["alive_at_end"] or result["exit_code"] == 0)


def run_dir_of(result: dict) -> Path:
    return Path(result["run_dir"])


def ensure_windowed_settings(settings: Path) -> None:
    """Tests must not take over the user's screen; settings already there are left alone."""
    if not settings.exists():
        settings.write_text(WINDOWED_SETTINGS, encoding="utf-8")


def wav_rms_dbfs(path: Path, start: float | None = None, end: float | None = None) -> float:
    """RMS level of a 16-bit or float WAV, or of the stretch from start to end in seconds.

    A killed game leaves its header unfinished: the data then runs to the end."""
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path} is not a WAV file")
    position, format_tag, bits, frame_rate, channels = 12, None, None, 0, 1
    samples = b""
    while position + 8 <= len(data):
        chunk, size = data[position:position + 4], struct.unpack_from("<I", data, position + 4)[0]
        body = position + 8
        if chunk == b"fmt ":
            format_tag, bits = struct.unpack_from("<H", data, body)[0], struct.unpack_from("<H", data, body + 14)[0]
            channels, frame_rate = struct.unpack_from("<HI", data, body + 2)
        elif chunk == b"data":
            available = len(data) - body
            samples = data[body:body + size] if 0 < size <= available else data[body:]
            break
        position = body + size + (size & 1)
    if format_tag == 3 and bits == 32:
        values, scale = array.array("f", samples[:len(samples) // 4 * 4]), 1.0
    elif format_tag == 1 and bits == 16:
        values, scale = array.array("h", samples[:len(samples) // 2 * 2]), 32768.0
    else:
        raise ValueError(f"{path}: unsupported WAV format {format_tag}/{bits}")
    if start is not None:
        values = values[int(start * frame_rate) * channels:int(end * frame_rate) * channels]
    if not values:
        return -math.inf
    power = sum(v * v for v in values) / len(values) / (scale * scale)
    return 10 * math.log10(power) if power > 0 else -math.inf


def mean_brightness(path: Path) -> float:
    from PIL import Image, ImageStat

    with Image.open(path) as image:
        return ImageStat.Stat(image.convert("L")).mean[0]


def is_user_game(path: str) -> bool:
    """The user plays from a copy under dist; a window opened here would take the keyboard away from them."""
    folders = path.lower().replace("/", "\\").split("\\")
    return folders[-1] == "batmanbegins.exe" and "dist" in folders[:-1]


def player_environment(environ: dict[str, str]) -> dict[str, str]:
    """What a player's double click gives the game: the system's PATH, and no test or build switches."""
    env = {k: v for k, v in environ.items()
           if k.upper() not in ("PATH", "CI") and not k.upper().startswith(("BB_", "XBE_"))}
    root = environ.get("SystemRoot", r"C:\Windows")
    env["PATH"] = rf"{root}\System32;{root};{root}\System32\Wbem"
    return env


def _processes() -> list[tuple[int, str]]:
    kernel32, psapi = ctypes.windll.kernel32, ctypes.windll.psapi
    kernel32.OpenProcess.restype = wintypes.HANDLE
    ids = (wintypes.DWORD * MAX_PROCESS_IDS)()
    used = wintypes.DWORD()
    if not psapi.EnumProcesses(ids, ctypes.sizeof(ids), ctypes.byref(used)):
        raise ctypes.WinError()
    found = []
    for pid in ids[:used.value // ctypes.sizeof(wintypes.DWORD)]:
        handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not handle:
            continue
        buffer = ctypes.create_unicode_buffer(wintypes.MAX_PATH * 4)
        size = wintypes.DWORD(len(buffer))
        if kernel32.QueryFullProcessImageNameW(wintypes.HANDLE(handle), 0, buffer, ctypes.byref(size)):
            found.append((pid, buffer.value))
        kernel32.CloseHandle(wintypes.HANDLE(handle))
    return found


def user_game_running(ignore_pid: int | None = None) -> bool:
    """ignore_pid: a run launched from under dist is not the user's game."""
    return any(is_user_game(path) for pid, path in _processes() if pid != ignore_pid)


def wait_for_user_game() -> None:
    """Blocks until the user's copy of the game has closed, however long that takes."""
    announced = False
    while user_game_running():
        if not announced:
            print("waiting: the user's game is open", flush=True)
            announced = True
        time.sleep(USER_GAME_WAIT_SECONDS)


def foreground_window() -> int:
    return ctypes.windll.user32.GetForegroundWindow()


def owns_foreground(pid: int) -> bool:
    owner = wintypes.DWORD()
    ctypes.windll.user32.GetWindowThreadProcessId(foreground_window(), ctypes.byref(owner))
    return owner.value == pid


def find_window(pid: int) -> int | None:
    """The process's visible top-level window."""
    user32 = ctypes.windll.user32
    found: list[int] = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
            return False
        return True

    user32.EnumWindows(visit, 0)
    return found[0] if found else None


def capture_window(hwnd: int, path: Path) -> bool:
    """Grabs the window itself, whatever is on top of it."""
    source = f"gfxcapture=hwnd={hwnd}:capture_cursor=0,hwdownload,format=bgra"
    try:
        grab = subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-filter_complex", source, "-frames:v", "1",
                               "-pix_fmt", "rgb24", str(path)], capture_output=True, timeout=GRAB_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        return False
    return grab.returncode == 0 and path.exists()


def start_recording(hwnd: int, path: Path) -> subprocess.Popen:
    """Films the window itself for timing, whatever is on top of it."""
    source = f"gfxcapture=hwnd={hwnd}:capture_cursor=0:max_framerate=30,hwdownload,format=bgra,fps=30"
    return subprocess.Popen(
        ["ffmpeg", "-loglevel", "error", "-y", "-filter_complex", source,
         "-c:v", "libx264", "-preset", "ultrafast", "-qp", "24", "-pix_fmt", "yuv420p", str(path)],
        stdin=subprocess.PIPE)


def stop_recording(recorder: subprocess.Popen) -> None:
    recorder.communicate(b"q", timeout=30)


def post_focus(hwnd: int, active: bool) -> None:
    """Tells the window it lost or got focus, without disturbing whatever the user has open."""
    post = ctypes.windll.user32.PostMessageW
    post(hwnd, WM_ACTIVATE, 1 if active else 0, 0)
    post(hwnd, WM_ACTIVATEAPP, 1 if active else 0, 0)
    post(hwnd, WM_SETFOCUS if active else WM_KILLFOCUS, 0, 0)


def key_lparam(key: str, down: bool) -> int:
    """The message's flags. Without the extended bit SDL reads an arrow's scan code as the numeric keypad's."""
    state = 0x00000001 if down else 0xC0000001
    extended = EXTENDED_KEY_BIT if key in EXTENDED_KEYS else 0
    return state | extended | (KEYS[key][1] << 16)


def post_key(hwnd: int, key: str, down: bool) -> None:
    """Sends a key to that window alone, so the rest of the desktop keeps working."""
    ctypes.windll.user32.PostMessageW(hwnd, WM_KEYDOWN if down else WM_KEYUP, KEYS[key][0], key_lparam(key, down))


def _new_run_dir(runs_dir: Path, name: str) -> Path:
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    candidate = runs_dir / f"{name}-{stamp}"
    counter = 1
    while candidate.exists():
        counter += 1
        candidate = runs_dir / f"{name}-{stamp}-{counter}"
    candidate.mkdir(parents=True)
    return candidate


def _snapshot(folder: Path) -> dict[str, tuple[int, int]]:
    if not folder.is_dir():
        return {}
    return {p.name: (p.stat().st_mtime_ns, p.stat().st_size) for p in folder.iterdir() if p.is_file()}


def _do_shot(pid: int, run_dir: Path, at: float) -> dict:
    hwnd = find_window(pid)
    shot = run_dir / f"shot_{at:g}.png"
    if hwnd is None or not capture_window(hwnd, shot):
        return {"at": at, "file": None}
    return {"at": at, "file": shot.name, "mean": round(mean_brightness(shot), 2)}


def _stop(process: subprocess.Popen) -> None:
    """Closes the window first, which lets the game flush its log; kills it if that does not end it."""
    hwnd = find_window(process.pid)
    if hwnd is not None:
        ctypes.windll.user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
        try:
            process.wait(CLOSE_WAIT_SECONDS)
            return
        except subprocess.TimeoutExpired:
            pass
    process.kill()
    process.wait()


def run(config: RunConfig) -> dict:
    wait_for_user_game()
    run_dir = _new_run_dir(config.runs_dir, config.name)
    if config.fresh_saves:
        shutil.rmtree(config.cwd / "hdd" / "E" / "UDATA", ignore_errors=True)
    logs, dumps = config.cwd / "logs", config.cwd / "CrashDumps"
    logs_before, dumps_before = _snapshot(logs), _snapshot(dumps)

    env = {**(player_environment(os.environ) if config.player_env else os.environ), **config.env}
    if config.script:
        env["BB_PAD_SCRIPT"] = str(Path(config.script).resolve())
    audio_path = run_dir / "audio.wav"
    if config.audio:
        env["BB_AUDIO_DUMP"] = str(audio_path)

    foreground_before = foreground_window()
    process = subprocess.Popen(config.command, cwd=config.cwd, env=env)
    start = time.monotonic()
    pending = build_timeline(config.shots, config.focus_away, config.keys, config.record)
    shots: list[dict] = []
    recorder = None
    user_game_seen = False
    took_foreground = False
    next_user_check = start
    while time.monotonic() - start < config.seconds and process.poll() is None:
        took_foreground = took_foreground or owns_foreground(process.pid)
        if time.monotonic() >= next_user_check:
            user_game_seen = user_game_seen or user_game_running(process.pid)
            next_user_check = time.monotonic() + USER_GAME_CHECK_SECONDS
        while pending and time.monotonic() - start >= pending[0][0]:
            at, action = pending.pop(0)
            if action == "shot":
                shots.append(_do_shot(process.pid, run_dir, at))
            else:
                hwnd = find_window(process.pid)
                if hwnd is None:
                    continue
                if action == "record_on":
                    recorder = start_recording(hwnd, run_dir / "capture.mkv")
                elif action == "record_off":
                    if recorder:
                        stop_recording(recorder)
                        recorder = None
                elif action.startswith("key_"):
                    kind, key = action.split(":")
                    post_key(hwnd, key, kind == "key_down")
                else:
                    post_focus(hwnd, action == "focus_on")
        time.sleep(POLL_SECONDS)
    shots += [{"at": at, "file": None} for at, action in pending if action == "shot"]
    if recorder:
        stop_recording(recorder)

    elapsed = round(time.monotonic() - start, 2)
    foreground_kept = foreground_window() == foreground_before
    alive = process.poll() is None
    if alive:
        _stop(process)
    exit_code = None if alive else process.returncode

    dumps_after = _snapshot(dumps)
    new_dumps = sorted(n for n, s in dumps_after.items() if dumps_before.get(n) != s)
    for name in new_dumps:
        shutil.copy2(dumps / name, run_dir / name)
    logs_after = _snapshot(logs)
    new_logs = sorted((n for n, s in logs_after.items() if logs_before.get(n) != s and not ROTATED_LOG.search(n)),
                      key=lambda n: logs_after[n][0])
    for name in new_logs:
        shutil.copy2(logs / name, run_dir / name)
    tail: list[str] = []
    if new_logs:
        tail = (logs / new_logs[-1]).read_text(encoding="utf-8", errors="replace").splitlines()[-LOG_TAIL_LINES:]

    result = {
        "name": config.name,
        "run_dir": str(run_dir),
        "exit_code": exit_code,
        "alive_at_end": alive,
        "crashed": bool(new_dumps) or (not alive and exit_code != 0),
        "seconds": elapsed,
        "shots": shots,
        "new_crash_dumps": new_dumps,
        "user_game_seen": user_game_seen,
        "took_foreground": took_foreground,
        "foreground_kept": foreground_kept,
        "log_tail": tail,
    }
    if config.audio:
        result["audio_rms_dbfs"] = wav_rms_dbfs(audio_path) if audio_path.exists() else None
        if config.audio_window and audio_path.exists():
            result["audio_window_rms_dbfs"] = wav_rms_dbfs(audio_path, *config.audio_window)
    (run_dir / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    return result


def _pair(text: str) -> tuple[float, float]:
    first, second = text.split(",")
    return float(first), float(second)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--seconds", type=float, required=True)
    parser.add_argument("--script", type=Path)
    parser.add_argument("--shots", default="", help="seconds, comma separated")
    parser.add_argument("--env", action="append", default=[], metavar="K=V")
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    parser.add_argument("--arg", action="append", default=[], help="argument for the program, as --arg=value")
    parser.add_argument("--focus-away", type=_pair, metavar="FROM,TO")
    parser.add_argument("--audio", action="store_true", help="dump the mixer to audio.wav and report its level")
    parser.add_argument("--record", type=_pair, metavar="FROM,TO", help="film the window to capture.mkv for timing")
    parser.add_argument("--audio-window", type=_pair, metavar="FROM,TO", help="also report the level of this stretch")
    parser.add_argument("--fresh-saves", action="store_true", help="delete the game's saves before it starts")
    parser.add_argument("--player-env", action="store_true",
                        help="the game gets the system's PATH and no CI, BB_ or XBE_ variables, only the no-focus switch")
    parser.add_argument("--keys", type=parse_keys, default=[], help='"seconds:KEY[:seconds held]", comma separated')
    return parser.parse_args(argv)


def build_config(args: argparse.Namespace) -> RunConfig:
    exe = args.exe.resolve()
    env = {} if args.player_env else {"XBE_PERF": "1"}
    if exe.name == DEFAULT_EXE.name:
        ensure_windowed_settings(exe.parent / "settings.toml")
        env[NO_ACTIVATE_ENV] = "1"
    return RunConfig(
        name=args.name,
        seconds=args.seconds,
        command=[str(exe), *args.arg],
        cwd=exe.parent,
        script=args.script,
        shots=[float(s) for s in args.shots.split(",") if s],
        env={**env, **dict(item.split("=", 1) for item in args.env)},
        focus_away=args.focus_away,
        audio=args.audio,
        keys=args.keys,
        fresh_saves=args.fresh_saves,
        audio_window=args.audio_window,
        record=args.record,
        player_env=args.player_env,
    )


def main(argv: list[str]) -> int:
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except OSError:
        pass
    result = run(build_config(parse_args(argv)))
    print(json.dumps({k: v for k, v in result.items() if k != "log_tail"}, indent=2))
    print("\n".join(result["log_tail"][-15:]))
    return 0 if succeeded(result) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
