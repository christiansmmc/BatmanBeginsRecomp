"""Game data must never reach Git."""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FORBIDDEN_SUFFIXES = (".iso", ".xiso", ".xbe", ".rvz", ".qcow2", ".bin", ".xmv", ".edb", ".sfx")
FORBIDDEN_PREFIXES = ("generated/", "assets/", "reference/", "dist/", "runs/")


def find_forbidden(paths: list[str]) -> list[str]:
    return [
        path for path in paths
        if not path.startswith("sdk/")
        and (path.lower().endswith(FORBIDDEN_SUFFIXES) or path.startswith(FORBIDDEN_PREFIXES))
    ]


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=True).stdout


def test_find_forbidden_flags_game_data():
    paths = ["game.iso", "assets/x.txt", "reference/bios", "sdk/data.bin", "tools/xiso.py", "a/default.XBE"]
    assert find_forbidden(paths) == ["game.iso", "assets/x.txt", "reference/bios", "a/default.XBE"]


def test_no_game_data_is_tracked():
    assert find_forbidden(git("ls-files").splitlines()) == []


def test_game_paths_are_ignored():
    for path in ("Batman Begins (USA).iso", "assets/default.xbe", "reference/mcpx_1.0.bin",
                 "generated/game/x.cpp", "dist/Batman Begins/BatmanBegins.exe", "runs/x/result.json",
                 "BatmanBegins.dll", "x.rvz", "x.qcow2"):
        result = subprocess.run(["git", "check-ignore", "-q", path], cwd=ROOT)
        assert result.returncode == 0, path


def test_movie_skip_shortcut_is_gone():
    name = "BB_DEV_" + "SKIP_MOVIES"  # spelled apart so this file does not count as a use
    for folder in ("src", "sdk/runtime", "tools"):
        for path in (ROOT / folder).rglob("*"):
            if path.is_file() and path.suffix in {".cpp", ".h", ".py"}:
                assert name not in path.read_text(encoding="utf-8", errors="replace"), path
