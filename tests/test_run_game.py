import json
import math
import os
import shutil
import struct
import subprocess
import sys
import textwrap
import time
import wave
from pathlib import Path

import pytest

import run_game

REAL_USER_GAME_RUNNING = run_game.user_game_running


@pytest.fixture(autouse=True)
def no_user_game(monkeypatch):
    """These runs use a fake program, so they never compete with a copy the user has open."""
    monkeypatch.setattr(run_game, "user_game_running", lambda ignore_pid=None: False)


def fake_program(tmp_path, body):
    """A small program standing in for the game, run with its folder as the current directory."""
    script = tmp_path / "fake.py"
    script.write_text(textwrap.dedent(body), encoding="utf-8")
    return [sys.executable, "-I", str(script)]


def run(tmp_path, body, seconds=0.5, **options):
    config = run_game.RunConfig(
        name="test",
        seconds=seconds,
        command=fake_program(tmp_path, body),
        cwd=tmp_path,
        runs_dir=tmp_path / "runs",
        **options,
    )
    return run_game.run(config)


def test_reports_crash(tmp_path):
    result = run(tmp_path, "raise SystemExit(3)")
    assert result["crashed"] is True
    assert result["exit_code"] == 3
    assert result["alive_at_end"] is False


def test_kills_process_alive_at_end(tmp_path):
    marker = tmp_path / "alive.txt"
    result = run(tmp_path, f"""
        import time
        while True:
            open(r"{marker}", "w").write(str(time.time()))
            time.sleep(0.02)
    """, seconds=0.8)
    assert result["alive_at_end"] is True
    assert result["crashed"] is False
    time.sleep(0.2)
    first = marker.read_text()
    time.sleep(0.2)
    assert marker.read_text() == first


def test_exit_zero_is_success(tmp_path):
    result = run(tmp_path, "pass")
    assert result["crashed"] is False
    assert result["exit_code"] == 0
    assert run_game.succeeded(result)


def test_nonzero_exit_is_not_success(tmp_path):
    assert not run_game.succeeded(run(tmp_path, "raise SystemExit(1)"))


def test_collects_new_crash_dump(tmp_path):
    (tmp_path / "CrashDumps").mkdir()
    (tmp_path / "CrashDumps" / "old.dmp").write_bytes(b"old")
    result = run(tmp_path, """
        open("CrashDumps/x.dmp", "wb").write(b"dump")
    """)
    assert result["new_crash_dumps"] == ["x.dmp"]
    assert result["crashed"] is True
    assert not run_game.succeeded(result)
    assert (run_game.run_dir_of(result) / "x.dmp").read_bytes() == b"dump"


def test_writes_result_json_and_log_tail(tmp_path):
    (tmp_path / "logs").mkdir()
    (tmp_path / "logs" / "old.log").write_text("old line\n")
    result = run(tmp_path, """
        import time
        time.sleep(0.05)
        open("logs/batman_1.log", "w").write("first\\nsecond\\n")
    """)
    assert result["log_tail"] == ["first", "second"]
    saved = json.loads((run_game.run_dir_of(result) / "result.json").read_text(encoding="utf-8"))
    assert saved["exit_code"] == 0
    assert (run_game.run_dir_of(result) / "batman_1.log").exists()


def test_passes_environment_and_script(tmp_path):
    pad = tmp_path / "pad.txt"
    pad.write_text("10 A\n")
    result = run(tmp_path, """
        import os
        raise SystemExit(0 if os.environ["BB_PAD_SCRIPT"].endswith("pad.txt") and os.environ["K"] == "V" else 9)
    """, script=pad, env={"K": "V"})
    assert result["exit_code"] == 0


def test_audio_option_reports_level_of_what_the_game_wrote(tmp_path):
    result = run(tmp_path, """
        import math, os, struct, wave
        with wave.open(os.environ["BB_AUDIO_DUMP"], "wb") as w:
            w.setnchannels(2); w.setsampwidth(2); w.setframerate(48000)
            w.writeframes(b"".join(struct.pack("<hh", 8000, 8000) for _ in range(4800)))
    """, audio=True)
    assert result["audio_rms_dbfs"] == pytest.approx(-12.3, abs=0.2)


def test_missing_audio_is_reported_as_none(tmp_path):
    assert run(tmp_path, "pass", audio=True)["audio_rms_dbfs"] is None


def test_timeline_orders_shots_and_focus_changes():
    assert run_game.build_timeline([30, 60], (40, 50)) == [
        (30, "shot"), (40, "focus_off"), (50, "focus_on"), (60, "shot"),
    ]


def test_shot_without_window_is_recorded_as_none(tmp_path):
    result = run(tmp_path, "import time; time.sleep(0.6)", seconds=0.5, shots=[0.2])
    assert result["shots"] == [{"at": 0.2, "file": None}]


def test_writes_windowed_settings_only_when_missing(tmp_path):
    settings = tmp_path / "settings.toml"
    run_game.ensure_windowed_settings(settings)
    assert 'display_mode = "windowed"' in settings.read_text(encoding="utf-8")
    settings.write_text('display_mode = "fullscreen"\n', encoding="utf-8")
    run_game.ensure_windowed_settings(settings)
    assert settings.read_text(encoding="utf-8") == 'display_mode = "fullscreen"\n'


def write_wav(path, samples):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(48000)
        w.writeframes(b"".join(struct.pack("<hh", s, s) for s in samples))


def test_rms_of_full_scale_sine_is_about_minus_3_dbfs(tmp_path):
    path = tmp_path / "a.wav"
    write_wav(path, [int(32767 * math.sin(2 * math.pi * i / 100)) for i in range(4800)])
    assert run_game.wav_rms_dbfs(path) == pytest.approx(-3.01, abs=0.05)


def test_rms_of_silence_is_minus_infinity(tmp_path):
    path = tmp_path / "a.wav"
    write_wav(path, [0] * 4800)
    assert run_game.wav_rms_dbfs(path) == -math.inf


def test_rms_reads_float_wav_written_by_the_game(tmp_path):
    path = tmp_path / "a.wav"
    frames = struct.pack("<" + "f" * 9600, *([0.5] * 9600))
    header = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(frames), b"WAVE", b"fmt ", 16, 3, 2, 48000,
                         48000 * 8, 8, 32, b"data", len(frames))
    path.write_bytes(header + frames)
    assert run_game.wav_rms_dbfs(path) == pytest.approx(-6.02, abs=0.05)


def test_rms_reads_wav_left_open_by_a_killed_game(tmp_path):
    path = tmp_path / "a.wav"
    frames = struct.pack("<" + "f" * 9600, *([0.5] * 9600))
    header = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 0, b"WAVE", b"fmt ", 16, 3, 2, 48000,
                         48000 * 8, 8, 32, b"data", 0)
    path.write_bytes(header + frames)
    assert run_game.wav_rms_dbfs(path) == pytest.approx(-6.02, abs=0.05)


def test_mean_brightness_tells_black_from_a_picture(tmp_path):
    from PIL import Image

    black, white = tmp_path / "b.png", tmp_path / "w.png"
    Image.new("RGB", (8, 8), (0, 0, 0)).save(black)
    Image.new("RGB", (8, 8), (255, 255, 255)).save(white)
    assert run_game.mean_brightness(black) == 0
    assert run_game.mean_brightness(white) == 255


def test_cli_builds_command_with_arguments_for_another_program(tmp_path):
    exe = tmp_path / "other.exe"
    args = run_game.parse_args(["--name", "x", "--seconds", "1", "--exe", str(exe), "--arg=-config_path", "--arg=a.toml"])
    config = run_game.build_config(args)
    assert config.command == [str(exe.resolve()), "-config_path", "a.toml"]
    assert config.cwd == exe.resolve().parent
    assert not (tmp_path / "settings.toml").exists()


def test_cli_writes_windowed_settings_for_the_game(tmp_path):
    exe = tmp_path / "BatmanBegins.exe"
    run_game.build_config(run_game.parse_args(["--name", "x", "--seconds", "1", "--exe", str(exe)]))
    assert 'display_mode = "windowed"' in (tmp_path / "settings.toml").read_text(encoding="utf-8")


def test_parse_keys_reads_time_key_and_optional_hold():
    assert run_game.parse_keys("30:RETURN,35.5:DOWN:0.5") == [(30.0, "RETURN", 0.15), (35.5, "DOWN", 0.5)]


def test_parse_keys_rejects_unknown_key():
    with pytest.raises(ValueError):
        run_game.parse_keys("3:NOPE")


def test_timeline_includes_key_presses_and_releases():
    assert run_game.build_timeline([5], None, [(3.0, "RETURN", 0.5)]) == [
        (3.0, "key_down:RETURN"), (3.5, "key_up:RETURN"), (5, "shot"),
    ]


def test_fresh_saves_empties_the_save_folder_before_the_game_starts(tmp_path):
    saves = tmp_path / "hdd" / "E" / "UDATA" / "45410389" / "13AD93548315"
    saves.mkdir(parents=True)
    (saves / "BATMAN A").write_text("old", encoding="utf-8")
    result = run(tmp_path, """
        import pathlib, sys
        sys.exit(1 if pathlib.Path("hdd/E/UDATA").exists() else 0)
    """, fresh_saves=True)
    assert result["exit_code"] == 0


def test_saves_are_kept_by_default(tmp_path):
    saves = tmp_path / "hdd" / "E" / "UDATA"
    saves.mkdir(parents=True)
    result = run(tmp_path, """
        import pathlib, sys
        sys.exit(0 if pathlib.Path("hdd/E/UDATA").exists() else 1)
    """)
    assert result["exit_code"] == 0


def test_rms_of_a_stretch_of_the_wav(tmp_path):
    path = tmp_path / "a.wav"
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(1000)
        out.writeframes(struct.pack("<2000h", *([0] * 2000)))
        out.writeframes(struct.pack("<2000h", *([16384] * 2000)))
    assert run_game.wav_rms_dbfs(path, 1.0, 2.0) == pytest.approx(-6.02, abs=0.05)
    assert run_game.wav_rms_dbfs(path, 0.0, 1.0) == -math.inf


def test_audio_window_is_reported_apart(tmp_path):
    result = run(tmp_path, f"""
        import os, struct, wave
        with wave.open(os.environ["BB_AUDIO_DUMP"], "wb") as out:
            out.setnchannels(1)
            out.setsampwidth(2)
            out.setframerate(1000)
            out.writeframes(struct.pack("<1000h", *([0] * 1000)))
            out.writeframes(struct.pack("<1000h", *([16384] * 1000)))
    """, audio=True, audio_window=(1.0, 2.0))
    assert result["audio_window_rms_dbfs"] == pytest.approx(-6.02, abs=0.05)


def test_timeline_includes_the_recording():
    timeline = run_game.build_timeline([5.0], None, [], (2.0, 8.0))
    assert timeline == [(2.0, "record_on"), (5.0, "shot"), (8.0, "record_off")]


def test_arrow_keys_are_sent_as_extended_keys():
    extended = 1 << 24
    for key in ("UP", "DOWN", "LEFT", "RIGHT"):
        assert run_game.key_lparam(key, True) & extended, key
        assert run_game.key_lparam(key, False) & extended, key
    assert not run_game.key_lparam("RETURN", True) & extended
    assert (run_game.key_lparam("DOWN", True) >> 16) & 0xFF == 0x50
    assert run_game.key_lparam("A", False) & 0xC0000000 == 0xC0000000
    assert run_game.key_lparam("A", True) & 0xC0000000 == 0


def test_recognises_the_copies_the_user_plays():
    assert run_game.is_user_game(r"C:\Users\x\projects\BatmanBeginsRecomp\dist\test copy\BatmanBegins.exe")
    assert run_game.is_user_game(r"c:\x\DIST\Batman Begins\batmanbegins.EXE")
    assert not run_game.is_user_game(r"C:\x\out\build\release\BatmanBegins.exe")
    assert not run_game.is_user_game(r"C:\x\dist\test copy\other.exe")


def test_sees_a_game_opened_from_the_final_folder(tmp_path):
    """A real process at that path, spaces and all, and not only the shape of the path."""
    exe = tmp_path / "dist" / "Batman Begins" / "BatmanBegins.exe"
    exe.parent.mkdir(parents=True)
    shutil.copy(Path(os.environ["SystemRoot"]) / "System32" / "PING.EXE", exe)
    process = subprocess.Popen([str(exe), "-n", "30", "127.0.0.1"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        assert (process.pid, str(exe)) in run_game._processes()
        assert REAL_USER_GAME_RUNNING() is True
    finally:
        process.kill()
        process.wait()


def test_the_run_does_not_take_its_own_game_for_the_users(monkeypatch):
    user_copy = r"C:\x\dist\Batman Begins\BatmanBegins.exe"
    monkeypatch.setattr(run_game, "_processes", lambda: [(10, user_copy), (11, r"C:\x\other.exe")])
    assert REAL_USER_GAME_RUNNING() is True
    assert REAL_USER_GAME_RUNNING(ignore_pid=11) is True
    assert REAL_USER_GAME_RUNNING(ignore_pid=10) is False


def test_a_run_from_the_final_folder_does_not_flag_itself(tmp_path, monkeypatch):
    seen_with = []

    def running(ignore_pid=None):
        seen_with.append(ignore_pid)
        return False

    monkeypatch.setattr(run_game, "user_game_running", running)
    monkeypatch.setattr(run_game, "USER_GAME_CHECK_SECONDS", 0.01)
    run(tmp_path, """
        import time
        time.sleep(1)
    """, seconds=0.3)
    assert seen_with[0] is None  # before the launch, anything open counts
    assert seen_with[-1] is not None  # after it, its own process does not


def test_player_environment_is_what_a_player_has():
    env = run_game.player_environment({
        "PATH": r"C:\tools;C:\scoop\shims;C:\Windows\System32", "SystemRoot": r"C:\Windows", "CI": "1",
        "BB_PAD_SCRIPT": "x", "bb_dev_movie": "y", "XBE_PERF": "1", "XBE_NO_ACTIVATE": "1", "TEMP": r"C:\t",
    })
    assert env.pop("PATH") == r"C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem"
    assert env == {"SystemRoot": r"C:\Windows", "TEMP": r"C:\t"}


def test_a_player_run_has_no_test_switches_but_keeps_the_foreground_rule(tmp_path):
    exe = tmp_path / "BatmanBegins.exe"
    args = ["--name", "x", "--seconds", "1", "--exe", str(exe), "--player-env"]
    config = run_game.build_config(run_game.parse_args(args))
    assert config.player_env is True
    assert config.env == {run_game.NO_ACTIVATE_ENV: "1"}


def test_a_player_run_reaches_the_program(tmp_path, monkeypatch):
    monkeypatch.setenv("BB_SOMETHING", "1")
    monkeypatch.setenv("CI", "1")
    result = run(tmp_path, """
        import json, os
        open("env.json", "w").write(json.dumps(dict(os.environ)))
    """, player_env=True, env={"XBE_NO_ACTIVATE": "1"})
    assert result["exit_code"] == 0
    env = {k.upper(): v for k, v in json.loads((tmp_path / "env.json").read_text()).items()}
    assert "BB_SOMETHING" not in env and "CI" not in env
    assert env["XBE_NO_ACTIVATE"] == "1"
    assert "scoop" not in env["PATH"].lower()


def test_keeps_only_this_sessions_log(tmp_path):
    """The game moves the older sessions along at each start; they are not this run's."""
    (tmp_path / "logs").mkdir()
    (tmp_path / "logs" / "batman.log").write_text("last session\n")
    (tmp_path / "logs" / "batman.1.log").write_text("the one before\n")
    result = run(tmp_path, r"""
        import os, time
        time.sleep(0.05)
        os.replace("logs/batman.1.log", "logs/batman.2.log")
        os.replace("logs/batman.log", "logs/batman.1.log")
        open("logs/batman.log", "w").write("this session\n")
    """)
    assert result["log_tail"] == ["this session"]
    assert sorted(p.name for p in run_game.run_dir_of(result).glob("*.log")) == ["batman.log"]


def test_waits_for_the_users_game_before_launching(tmp_path, monkeypatch):
    marker = tmp_path / "started.txt"
    answers = iter([True, True, False])
    checks_before_start = []

    def running(ignore_pid=None):
        checks_before_start.append(marker.exists())
        return next(answers, False)

    monkeypatch.setattr(run_game, "user_game_running", running)
    monkeypatch.setattr(run_game, "USER_GAME_WAIT_SECONDS", 0.01)
    result = run(tmp_path, f"""
        open(r"{marker}", "w").write("x")
    """, seconds=0.3)
    assert checks_before_start[:3] == [False, False, False]
    assert marker.exists()
    assert result["user_game_seen"] is False


def test_flags_a_run_the_users_game_opened_during(tmp_path, monkeypatch):
    answers = iter([False])
    monkeypatch.setattr(run_game, "user_game_running", lambda ignore_pid=None: next(answers, True))
    monkeypatch.setattr(run_game, "USER_GAME_CHECK_SECONDS", 0.01)
    result = run(tmp_path, """
        import time
        time.sleep(1)
    """, seconds=0.3)
    assert result["user_game_seen"] is True


def test_the_port_opens_without_taking_the_foreground(tmp_path):
    exe = tmp_path / "BatmanBegins.exe"
    config = run_game.build_config(run_game.parse_args(["--name", "x", "--seconds", "1", "--exe", str(exe)]))
    assert config.env[run_game.NO_ACTIVATE_ENV] == "1"


def test_a_run_can_ask_for_the_old_window_behaviour(tmp_path):
    exe = tmp_path / "BatmanBegins.exe"
    args = ["--name", "x", "--seconds", "1", "--exe", str(exe), f"--env={run_game.NO_ACTIVATE_ENV}=0"]
    assert run_game.build_config(run_game.parse_args(args)).env[run_game.NO_ACTIVATE_ENV] == "0"


def test_other_programs_do_not_get_the_switch(tmp_path):
    exe = tmp_path / "xemu.exe"
    config = run_game.build_config(run_game.parse_args(["--name", "x", "--seconds", "1", "--exe", str(exe)]))
    assert run_game.NO_ACTIVATE_ENV not in config.env


def test_result_says_when_the_program_took_the_foreground(tmp_path, monkeypatch):
    monkeypatch.setattr(run_game, "owns_foreground", lambda pid: True)
    result = run(tmp_path, "import time; time.sleep(5)", seconds=0.2)
    assert result["took_foreground"] is True


def test_result_says_when_the_foreground_stayed_with_someone_else(tmp_path, monkeypatch):
    monkeypatch.setattr(run_game, "owns_foreground", lambda pid: False)
    monkeypatch.setattr(run_game, "foreground_window", lambda: 1234)
    result = run(tmp_path, "import time; time.sleep(5)", seconds=0.2)
    assert result["took_foreground"] is False
    assert result["foreground_kept"] is True


def test_result_says_when_the_foreground_window_changed(tmp_path, monkeypatch):
    monkeypatch.setattr(run_game, "owns_foreground", lambda pid: False)
    windows = iter([1, 2])
    monkeypatch.setattr(run_game, "foreground_window", lambda: next(windows))
    result = run(tmp_path, "import time; time.sleep(5)", seconds=0.2)
    assert result["foreground_kept"] is False
