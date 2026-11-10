"""The public release's exe carries the project's own icon, never the one the build makes from the game's title image."""
import shutil
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import release_icon  # noqa: E402

BUILT_EXE = ROOT / "out" / "build" / "release" / "BatmanBegins.exe"
needs_build = pytest.mark.skipif(not BUILT_EXE.exists(), reason="needs a build, which has the game's icon")


def test_the_icon_has_every_size_windows_asks_for():
    ico = release_icon.draw_icon()
    reserved, kind, count = struct.unpack_from("<HHH", ico)
    assert (reserved, kind) == (0, 1)
    assert release_icon.ico_sizes(ico) == [256, 128, 64, 48, 32, 24, 16]
    assert len(release_icon.ico_images(ico)) == count == 7


def test_the_icon_is_the_same_each_time():
    assert release_icon.draw_icon() == release_icon.draw_icon()


@needs_build
def test_the_copy_has_only_the_new_icon(tmp_path):
    exe = tmp_path / "BatmanBegins.exe"
    shutil.copy2(BUILT_EXE, exe)
    game_icon = release_icon.icon_images(exe)
    assert game_icon, "the build's exe should carry the game's icon"

    ico = release_icon.draw_icon()
    release_icon.replace_icon(exe, ico)

    after = release_icon.icon_images(exe)
    assert sorted(after) == sorted(release_icon.ico_images(ico))
    assert not set(after) & set(game_icon)
    assert release_icon.group_count(exe) == 1


@needs_build
def test_the_build_keeps_its_own_icon(tmp_path):
    before = release_icon.icon_images(BUILT_EXE)
    exe = tmp_path / "BatmanBegins.exe"
    shutil.copy2(BUILT_EXE, exe)
    release_icon.replace_icon(exe, release_icon.draw_icon())
    assert release_icon.icon_images(BUILT_EXE) == before
