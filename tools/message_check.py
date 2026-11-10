"""Opens a program that should stop at a message box, records what the box says, and confirms it.

The program runs on a desktop of its own with a player's environment, so the box takes nobody's keyboard and nobody sees it.
Nobody could close it there either, so it never outlives the check, even if this tool is killed. A message box still plays the
system's sound on that desktop: each run of this tool on a real box can be heard.
Everything goes to runs/<name>-<time>: result.json, message.png and the log.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
import shutil
import sys
import threading
import time
from ctypes import wintypes
from datetime import datetime
from pathlib import Path

import run_game

ROOT = Path(__file__).resolve().parent.parent
GENERIC_ALL = 0x10000000
CREATE_SUSPENDED, CREATE_UNICODE_ENVIRONMENT = 0x00000004, 0x00000400
JOB_OBJECT_EXTENDED_LIMIT_INFORMATION, JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 9, 0x00002000
WAIT_OBJECT_0, STILL_ACTIVE = 0, 259
BM_CLICK = 0x00F5
MESSAGE_BOX_CLASS = "#32770"
DIB_RGB_COLORS = 0
POLL_SECONDS = 0.05
PAINT_SECONDS = 0.5
CLOSE_SECONDS = 10

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
HWND, HANDLE, HDC = wintypes.HWND, wintypes.HANDLE, wintypes.HDC
ENUM_WINDOWS = ctypes.WINFUNCTYPE(wintypes.BOOL, HWND, wintypes.LPARAM)

user32.CreateDesktopW.restype = HANDLE
user32.CreateDesktopW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
user32.CloseDesktop.argtypes = [HANDLE]
user32.SetThreadDesktop.argtypes = [HANDLE]
user32.EnumDesktopWindows.argtypes = [HANDLE, ENUM_WINDOWS, wintypes.LPARAM]
user32.EnumChildWindows.argtypes = [HWND, ENUM_WINDOWS, wintypes.LPARAM]
user32.GetWindowThreadProcessId.argtypes = [HWND, ctypes.POINTER(wintypes.DWORD)]
user32.IsWindowVisible.argtypes = [HWND]
user32.GetWindowTextW.argtypes = [HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetClassNameW.argtypes = [HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowRect.argtypes = [HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetDC.argtypes = [HWND]
user32.GetDC.restype = HDC
user32.ReleaseDC.argtypes = [HWND, HDC]
user32.PrintWindow.argtypes = [HWND, HDC, wintypes.UINT]
user32.PostMessageW.argtypes = [HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
gdi32.CreateCompatibleDC.argtypes = [HDC]
gdi32.CreateCompatibleDC.restype = HDC
gdi32.CreateCompatibleBitmap.argtypes = [HDC, ctypes.c_int, ctypes.c_int]
gdi32.CreateCompatibleBitmap.restype = HANDLE
gdi32.SelectObject.argtypes = [HDC, HANDLE]
gdi32.SelectObject.restype = HANDLE
gdi32.GetDIBits.argtypes = [HDC, HANDLE, wintypes.UINT, wintypes.UINT, ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT]
gdi32.DeleteObject.argtypes = [HANDLE]
gdi32.DeleteDC.argtypes = [HDC]
kernel32.WaitForSingleObject.argtypes = [HANDLE, wintypes.DWORD]
kernel32.WaitForSingleObject.restype = wintypes.DWORD
kernel32.GetExitCodeProcess.argtypes = [HANDLE, ctypes.POINTER(wintypes.DWORD)]
kernel32.TerminateProcess.argtypes = [HANDLE, wintypes.UINT]
kernel32.CloseHandle.argtypes = [HANDLE]
kernel32.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
kernel32.CreateJobObjectW.restype = HANDLE
kernel32.SetInformationJobObject.argtypes = [HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
kernel32.AssignProcessToJobObject.argtypes = [HANDLE, HANDLE]
kernel32.ResumeThread.argtypes = [HANDLE]


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("lpReserved", wintypes.LPWSTR), ("lpDesktop", wintypes.LPWSTR),
                ("lpTitle", wintypes.LPWSTR), ("dwX", wintypes.DWORD), ("dwY", wintypes.DWORD),
                ("dwXSize", wintypes.DWORD), ("dwYSize", wintypes.DWORD), ("dwXCountChars", wintypes.DWORD),
                ("dwYCountChars", wintypes.DWORD), ("dwFillAttribute", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
                ("wShowWindow", wintypes.WORD), ("cbReserved2", wintypes.WORD), ("lpReserved2", ctypes.c_void_p),
                ("hStdInput", HANDLE), ("hStdOutput", HANDLE), ("hStdError", HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", HANDLE), ("hThread", HANDLE), ("dwProcessId", wintypes.DWORD), ("dwThreadId", wintypes.DWORD)]


class JOB_LIMITS(ctypes.Structure):
    """JOBOBJECT_EXTENDED_LIMIT_INFORMATION."""
    _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD), ("SchedulingClass", wintypes.DWORD),
                ("IoCounters", ctypes.c_uint64 * 6), ("ProcessMemoryLimit", ctypes.c_size_t),
                ("JobMemoryLimit", ctypes.c_size_t), ("PeakProcessMemoryUsed", ctypes.c_size_t),
                ("PeakJobMemoryUsed", ctypes.c_size_t)]


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG), ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
                ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD), ("biSizeImage", wintypes.DWORD),
                ("biXPelsPerMeter", wintypes.LONG), ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
                ("biClrImportant", wintypes.DWORD)]


kernel32.CreateProcessW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, wintypes.BOOL,
                                    wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(STARTUPINFOW),
                                    ctypes.POINTER(PROCESS_INFORMATION)]


def _text(hwnd, getter=user32.GetWindowTextW) -> str:
    buffer = ctypes.create_unicode_buffer(4096)
    getter(hwnd, buffer, len(buffer))
    return buffer.value


def _job_that_ends_its_processes() -> HANDLE:
    """Windows ends what is in this job when its last handle closes, which also happens when this tool dies."""
    job = kernel32.CreateJobObjectW(None, None)
    limits = JOB_LIMITS(LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)
    if not job or not kernel32.SetInformationJobObject(job, JOB_OBJECT_EXTENDED_LIMIT_INFORMATION, ctypes.byref(limits),
                                                       ctypes.sizeof(limits)):
        raise ctypes.WinError(ctypes.get_last_error())
    return job


def _start(command: list[str], cwd: Path, desktop: str, env: dict[str, str], job: HANDLE) -> PROCESS_INFORMATION:
    """Starts it held, so it runs no code outside the job."""
    startup = STARTUPINFOW(cb=ctypes.sizeof(STARTUPINFOW), lpDesktop=f"WinSta0\\{desktop}")
    process = PROCESS_INFORMATION()
    line = ctypes.create_unicode_buffer(" ".join(f'"{part}"' for part in command))
    block = ctypes.create_unicode_buffer("\0".join(f"{k}={v}" for k, v in env.items()) + "\0\0")
    if not kernel32.CreateProcessW(command[0], line, None, None, False, CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, block,
                                   str(cwd), ctypes.byref(startup), ctypes.byref(process)):
        raise ctypes.WinError(ctypes.get_last_error())
    if not kernel32.AssignProcessToJobObject(job, process.hProcess):
        error = ctypes.WinError(ctypes.get_last_error())
        kernel32.TerminateProcess(process.hProcess, 1)
        kernel32.CloseHandle(process.hThread)
        kernel32.CloseHandle(process.hProcess)
        raise error
    kernel32.ResumeThread(process.hThread)
    return process


def _find_box(desktop, pid: int) -> int | None:
    found: list[int] = []

    @ENUM_WINDOWS
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        # by its class: a console program also owns a visible window of no size, the pseudo console's
        if owner.value == pid and user32.IsWindowVisible(hwnd) and _text(hwnd, user32.GetClassNameW) == MESSAGE_BOX_CLASS:
            found.append(hwnd)
            return False
        return True

    user32.EnumDesktopWindows(desktop, visit, 0)
    return found[0] if found else None


def _children(hwnd) -> list[tuple[int, str, str]]:
    found: list[tuple[int, str, str]] = []

    @ENUM_WINDOWS
    def visit(child, _):
        found.append((child, _text(child, user32.GetClassNameW), _text(child)))
        return True

    user32.EnumChildWindows(hwnd, visit, 0)
    return found


def _picture(hwnd, path: Path) -> None:
    from PIL import Image

    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    width, height = rect.right - rect.left, rect.bottom - rect.top
    screen = user32.GetDC(hwnd)
    memory = gdi32.CreateCompatibleDC(screen)
    bitmap = gdi32.CreateCompatibleBitmap(screen, width, height)
    gdi32.SelectObject(memory, bitmap)
    user32.PrintWindow(hwnd, memory, 0)
    header = BITMAPINFOHEADER(biSize=ctypes.sizeof(BITMAPINFOHEADER), biWidth=width, biHeight=-height, biPlanes=1, biBitCount=32)
    pixels = ctypes.create_string_buffer(width * height * 4)
    gdi32.GetDIBits(memory, bitmap, 0, height, pixels, ctypes.byref(header), DIB_RGB_COLORS)
    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(memory)
    user32.ReleaseDC(hwnd, screen)
    Image.frombuffer("RGB", (width, height), pixels.raw, "raw", "BGRX", 0, 1).save(path)


def _exit_code(process: PROCESS_INFORMATION) -> int:
    code = wintypes.DWORD()
    kernel32.GetExitCodeProcess(process.hProcess, ctypes.byref(code))
    return code.value


def _end(process: PROCESS_INFORMATION) -> None:
    if _exit_code(process) == STILL_ACTIVE:
        kernel32.TerminateProcess(process.hProcess, 1)
        kernel32.WaitForSingleObject(process.hProcess, 5000)


def _read_and_confirm(process: PROCESS_INFORMATION, desktop, run_dir: Path, wait_seconds: float) -> dict:
    result = {"box": None, "title": None, "message": None, "buttons": [], "picture": None}
    started = time.monotonic()
    box = None
    while time.monotonic() - started < wait_seconds and _exit_code(process) == STILL_ACTIVE:
        box = _find_box(desktop, process.dwProcessId)
        if box:
            break
        time.sleep(POLL_SECONDS)
    if box:
        time.sleep(PAINT_SECONDS)
        children = _children(box)
        buttons = [c for c in children if c[1] == "Button"]
        result.update(box=_text(box, user32.GetClassNameW), title=_text(box), buttons=[c[2] for c in buttons],
                      message="\n".join(c[2] for c in children if c[1] == "Static" and c[2]),
                      seconds_to_box=round(time.monotonic() - started, 2))
        _picture(box, run_dir / "message.png")
        result["picture"] = "message.png"
        result["alive_while_shown"] = _exit_code(process) == STILL_ACTIVE
        if buttons:
            user32.PostMessageW(buttons[0][0], BM_CLICK, 0, 0)
    closed = kernel32.WaitForSingleObject(process.hProcess, int(CLOSE_SECONDS * 1000) if box else 0) == WAIT_OBJECT_0
    result["closed_after_confirming"] = bool(box) and closed
    if not closed:
        _end(process)
        result["killed"] = True
    result["exit_code"] = _exit_code(process)
    return result


def _watch(command: list[str], cwd: Path, run_dir: Path, wait_seconds: float, env: dict[str, str]) -> dict:
    name = f"message-check-{os.getpid()}-{threading.get_ident()}"
    desktop = user32.CreateDesktopW(name, None, None, 0, GENERIC_ALL, None)
    if not desktop:
        raise ctypes.WinError(ctypes.get_last_error())
    job = None
    try:
        if not user32.SetThreadDesktop(desktop):
            raise ctypes.WinError(ctypes.get_last_error())
        job = _job_that_ends_its_processes()
        process = _start(command, cwd, name, env, job)
        try:
            return _read_and_confirm(process, desktop, run_dir, wait_seconds)
        finally:
            _end(process)  # also when reading the box failed
            kernel32.CloseHandle(process.hThread)
            kernel32.CloseHandle(process.hProcess)
    finally:
        if job:
            kernel32.CloseHandle(job)
        user32.CloseDesktop(desktop)


def check(command: list[str], cwd: Path, name: str, runs_dir: Path = ROOT / "runs", wait_seconds: float = 20) -> dict:
    run_game.wait_for_user_game()
    run_dir = run_game._new_run_dir(runs_dir, name)
    logs, dumps = cwd / "logs", cwd / "CrashDumps"
    logs_before, dumps_before = run_game._snapshot(logs), run_game._snapshot(dumps)
    outcome: dict = {}
    failure: list[BaseException] = []

    def worker():  # a thread of its own: SetThreadDesktop refuses a thread that already has windows
        try:
            outcome.update(_watch(command, cwd, run_dir, wait_seconds, run_game.player_environment(os.environ)))
        except BaseException as error:  # handed to the caller below
            failure.append(error)

    thread = threading.Thread(target=worker)
    thread.start()
    thread.join()
    if failure:
        raise failure[0]
    new_dumps = sorted(n for n, s in run_game._snapshot(dumps).items() if dumps_before.get(n) != s)
    for dump in new_dumps:
        shutil.copy2(dumps / dump, run_dir / dump)
    new_logs = sorted(n for n, s in run_game._snapshot(logs).items()
                      if logs_before.get(n) != s and not run_game.ROTATED_LOG.search(n))
    for log in new_logs:
        shutil.copy2(logs / log, run_dir / log)
    result = {"name": name, "run_dir": str(run_dir), "time": datetime.now().isoformat(timespec="seconds"),
              "command": command, "new_crash_dumps": new_dumps, "new_logs": new_logs, **outcome}
    (run_dir / "result.json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    return result


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except OSError:
        pass
    exe = args.exe.resolve()
    print(json.dumps(check([str(exe)], exe.parent, args.name), indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
