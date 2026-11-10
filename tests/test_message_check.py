import ctypes
import json
import subprocess
import sys
import textwrap
import time
from pathlib import Path

import pytest

import message_check
import run_game

TOOLS = Path(__file__).resolve().parent.parent / "tools"
PROCESS_TERMINATE = 0x0001

# A real dialog of the class message_check looks for, with a text and an OK button. Never MessageBoxW in these programs: it
# plays the system's sound whichever desktop it is on, and the user hears every run of the tests.
SILENT_BOX = r"""
    import ctypes, struct
    from ctypes import wintypes

    def silent_box(title, text):
        def words(value):
            return value.encode("utf-16-le") + b"\0\0"

        def item(style, box, item_id, atom, label):
            data = struct.pack("<IIhhhhHHH", 0x50000000 | style, 0, *box, item_id, 0xFFFF, atom) + words(label) + b"\0\0"
            return data + b"\0" * (-len(data) % 4)

        head = struct.pack("<IIHhhhhHH", 0x90C00080, 0, 2, 0, 0, 220, 70, 0, 0) + words(title)
        head += b"\0" * (-len(head) % 4)
        data = head + item(0, (10, 10, 200, 24), 100, 0x82, text) + item(1, (85, 45, 50, 14), 1, 0x80, "OK")
        template = (ctypes.c_uint32 * (len(data) // 4)).from_buffer_copy(data)
        user32 = ctypes.WinDLL("user32")
        handler = ctypes.WINFUNCTYPE(ctypes.c_ssize_t, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)
        user32.EndDialog.argtypes = [wintypes.HWND, ctypes.c_ssize_t]
        user32.DialogBoxIndirectParamW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.HWND, handler, wintypes.LPARAM]
        user32.DialogBoxIndirectParamW.restype = ctypes.c_ssize_t

        @handler
        def on_message(hwnd, message, wparam, lparam):
            if message == 0x0111 and wparam & 0xFFFF == 1:  # WM_COMMAND from the OK button
                user32.EndDialog(hwnd, 1)
                return 1
            return 1 if message == 0x0110 else 0  # WM_INITDIALOG

        user32.DialogBoxIndirectParamW(None, template, None, on_message, 0)
"""


def running(pids) -> list[int]:
    return [pid for pid, _ in run_game._processes() if pid in pids]


def end_process(pid: int) -> None:
    kernel32 = ctypes.WinDLL("kernel32")
    kernel32.OpenProcess.restype = ctypes.c_void_p
    kernel32.TerminateProcess.argtypes = [ctypes.c_void_p, ctypes.c_uint]
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel32.OpenProcess(PROCESS_TERMINATE, False, pid)
    if handle:
        kernel32.TerminateProcess(handle, 1)
        kernel32.CloseHandle(handle)


@pytest.fixture(autouse=True)
def started(monkeypatch):
    """Every program a test starts; none may outlive its test, whatever the test did."""
    monkeypatch.setattr(run_game, "user_game_running", lambda ignore_pid=None: False)
    pids = []
    real_start = message_check._start

    def start(*args):
        process = real_start(*args)
        pids.append(process.dwProcessId)
        return process

    monkeypatch.setattr(message_check, "_start", start)
    yield pids
    left = running(pids)
    for pid in left:
        end_process(pid)
    assert left == []


def program(tmp_path, body):
    script = tmp_path / "fake.py"
    script.write_text(textwrap.dedent(SILENT_BOX) + textwrap.dedent(body), encoding="utf-8")
    return [getattr(sys, "_base_executable", sys.executable), "-I", str(script)]


def check(tmp_path, body, **options):
    return message_check.check(program(tmp_path, body), tmp_path, "test", runs_dir=tmp_path / "runs", **options)


def test_records_the_box_and_confirms_it(tmp_path):
    result = check(tmp_path, """
        import os, sys
        os.makedirs("logs", exist_ok=True)
        open("logs/batman.log", "w").write("a line\\n")
        silent_box("Títle", "Put the ISO in the folder, café")
        sys.exit(1)
    """)
    assert result["box"] == "#32770"
    assert result["title"] == "Títle"
    assert result["message"] == "Put the ISO in the folder, café"
    assert result["buttons"] == ["OK"]
    assert result["alive_while_shown"] is True
    assert result["closed_after_confirming"] is True
    assert result["exit_code"] == 1
    assert result["new_crash_dumps"] == []
    run_dir = run_game.run_dir_of(result)
    assert (run_dir / "message.png").stat().st_size > 0
    assert (run_dir / "batman.log").exists()
    assert json.loads((run_dir / "result.json").read_text(encoding="utf-8"))["message"] == result["message"]


def test_a_program_that_shows_nothing_is_said_so(tmp_path):
    result = check(tmp_path, """
        import sys
        sys.exit(0)
    """, wait_seconds=5)
    assert result["box"] is None
    assert result["message"] is None
    assert result["closed_after_confirming"] is False
    assert result["exit_code"] == 0


def test_a_program_that_never_stops_is_ended(tmp_path):
    result = check(tmp_path, """
        import time
        time.sleep(60)
    """, wait_seconds=0.5)
    assert result["box"] is None
    assert result["killed"] is True


def test_a_crash_dump_is_kept(tmp_path):
    result = check(tmp_path, """
        import os
        os.makedirs("CrashDumps", exist_ok=True)
        open("CrashDumps/x.dmp", "w").write("dump")
        silent_box("y", "x")
    """)
    assert result["new_crash_dumps"] == ["x.dmp"]


def test_a_box_that_never_closes_is_ended(tmp_path):
    """On a desktop of its own nobody could see that box or close it."""
    result = check(tmp_path, """
        import time
        silent_box("y", "x")
        time.sleep(60)
    """)
    assert result["closed_after_confirming"] is False
    assert result["killed"] is True


def test_a_failure_in_the_tool_ends_the_program(tmp_path, monkeypatch, started):
    def broken(*_):
        raise ValueError("cannot write empty image")

    monkeypatch.setattr(message_check, "_picture", broken)
    with pytest.raises(ValueError):
        check(tmp_path, """
            silent_box("y", "x")
        """)
    assert len(started) == 1
    assert running(started) == []


def test_the_program_dies_with_the_tool(tmp_path):
    """Nothing else could end it if the tool itself is killed in the middle of a run."""
    pid_file = tmp_path / "pid.txt"
    command = program(tmp_path, f"""
        import os, time
        open(r"{pid_file}", "w").write(str(os.getpid()))
        time.sleep(60)
    """)
    tool = textwrap.dedent(f"""
        import sys
        sys.path.insert(0, r"{TOOLS}")
        from pathlib import Path
        import message_check, run_game
        run_game.user_game_running = lambda ignore_pid=None: False
        message_check.check({command!r}, Path(r"{tmp_path}"), "test", runs_dir=Path(r"{tmp_path}") / "runs", wait_seconds=60)
    """)
    runner = subprocess.Popen([getattr(sys, "_base_executable", sys.executable), "-I", "-c", tool])
    deadline = time.monotonic() + 15
    try:
        while not pid_file.exists() or not pid_file.read_text():
            assert time.monotonic() < deadline, "the program never started"
            time.sleep(0.05)
        pid = int(pid_file.read_text())
        assert running([pid]) == [pid]
    finally:
        runner.kill()
        runner.wait()
    try:
        while running([pid]) and time.monotonic() < deadline:
            time.sleep(0.05)
        assert running([pid]) == []
    finally:
        end_process(pid)
