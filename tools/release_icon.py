"""Gives a copy of the program the project's own icon, for the public release.

The build makes the program's icon from the title image inside the player's own copy of the game (tools/icon), which is
the game's art and must not be published. release.bat runs this on the copy that goes into the zip: it draws an original
icon, a gold B on a dark tile, and puts it in place of every icon the copy has. The build's own exe is left alone.

  release_icon.py <exe>
"""
from __future__ import annotations

import argparse
import ctypes
import io
import struct
import sys
from ctypes import wintypes
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SIZES = (256, 128, 64, 48, 32, 24, 16)
FONT = Path("C:/Windows/Fonts/segoeuib.ttf")
DARK, EDGE, GOLD = (20, 22, 26, 255), (70, 82, 76, 255), (226, 179, 60, 255)
BAND = (141, 114, 46, 255)  # the gold at 60% over the dark, opaque

RT_ICON, RT_GROUP_ICON = 3, 14
LOAD_LIBRARY_AS_DATAFILE, LOAD_LIBRARY_AS_IMAGE_RESOURCE = 0x2, 0x20
LANG_EN_US = 0x409


def _tile(size: int) -> Image.Image:
    """The icon at one size, drawn at 4x and reduced, so small sizes stay smooth."""
    big = size * 4
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    radius = big // 6
    draw.rounded_rectangle((0, 0, big - 1, big - 1), radius=radius, fill=DARK, outline=EDGE, width=max(4, big // 40))
    # a slanted gold band low across the tile, as on the options screen
    band = [(0, big * 0.70), (big, big * 0.58), (big, big * 0.66), (0, big * 0.78)]
    draw.polygon(band, fill=BAND)
    font = ImageFont.truetype(str(FONT), int(big * 0.74))
    left, top, right, bottom = draw.textbbox((0, 0), "B", font=font)
    x = (big - (right - left)) / 2 - left
    y = (big - (bottom - top)) / 2 - top - big * 0.04
    draw.text((x, y), "B", font=font, fill=GOLD, stroke_width=max(2, big // 64), stroke_fill=DARK)
    return image.resize((size, size), Image.LANCZOS)


def draw_icon() -> bytes:
    """An .ico with every size Windows asks for, largest first, each as a PNG."""
    images = []
    for size in SIZES:
        out = io.BytesIO()
        _tile(size).save(out, format="PNG", optimize=False)
        images.append(out.getvalue())
    header = struct.pack("<HHH", 0, 1, len(images))
    entries, offset = b"", 6 + 16 * len(images)
    for size, data in zip(SIZES, images):
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    return header + entries + b"".join(images)


def _ico_entries(ico: bytes) -> list[tuple[bytes, bytes]]:
    """Each image's 12 directory bytes before its offset, and its data."""
    count = struct.unpack_from("<H", ico, 4)[0]
    entries = []
    for i in range(count):
        entry = ico[6 + 16 * i: 6 + 16 * (i + 1)]
        length, offset = struct.unpack_from("<II", entry, 8)
        entries.append((entry[:12], ico[offset: offset + length]))
    return entries


def ico_images(ico: bytes) -> list[bytes]:
    return [data for _, data in _ico_entries(ico)]


def ico_sizes(ico: bytes) -> list[int]:
    return [entry[0] or 256 for entry, _ in _ico_entries(ico)]


# ---- resources, through the Windows API ----

_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
_ENUMNAMES = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)
_ENUMLANGS = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p, wintypes.WORD,
                                ctypes.c_void_p)
_kernel32.LoadLibraryExW.restype = wintypes.HMODULE
_kernel32.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
_kernel32.FreeLibrary.argtypes = [wintypes.HMODULE]
_kernel32.EnumResourceNamesW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, _ENUMNAMES, ctypes.c_void_p]
_kernel32.EnumResourceLanguagesW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p, _ENUMLANGS,
                                             ctypes.c_void_p]
_kernel32.FindResourceExW.restype = ctypes.c_void_p
_kernel32.FindResourceExW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p, wintypes.WORD]
_kernel32.SizeofResource.restype = wintypes.DWORD
_kernel32.SizeofResource.argtypes = [wintypes.HMODULE, ctypes.c_void_p]
_kernel32.LoadResource.restype = ctypes.c_void_p
_kernel32.LoadResource.argtypes = [wintypes.HMODULE, ctypes.c_void_p]
_kernel32.LockResource.restype = ctypes.c_void_p
_kernel32.LockResource.argtypes = [ctypes.c_void_p]
_kernel32.BeginUpdateResourceW.restype = wintypes.HANDLE
_kernel32.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
_kernel32.UpdateResourceW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, wintypes.WORD,
                                      ctypes.c_void_p, wintypes.DWORD]
_kernel32.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]


def _check(ok, what: str):
    if not ok:
        raise OSError(ctypes.get_last_error(), what)
    return ok


def _resources(exe: Path, kind: int) -> list[tuple[int, int, bytes]]:
    """(id, language, data) of each resource of a numbered type; names must be numbers, as rc.exe gives icons."""
    module = _check(_kernel32.LoadLibraryExW(str(exe), None, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE),
                    f"open {exe}")
    try:
        names: list[int] = []

        def on_name(_module, _kind, name, _param):
            if name is not None and name >> 16 == 0:  # IS_INTRESOURCE
                names.append(int(name))
            return True

        _kernel32.EnumResourceNamesW(module, kind, _ENUMNAMES(on_name), None)  # FALSE when there are none
        found = []
        for name in names:
            languages: list[int] = []

            def on_language(_module, _kind, _name, language, _param):
                languages.append(int(language))
                return True

            _kernel32.EnumResourceLanguagesW(module, kind, name, _ENUMLANGS(on_language), None)
            for language in languages:
                info = _check(_kernel32.FindResourceExW(module, kind, name, language), "find a resource")
                size = _kernel32.SizeofResource(module, info)
                pointer = _check(_kernel32.LockResource(_kernel32.LoadResource(module, info)), "load a resource")
                found.append((name, language, ctypes.string_at(pointer, size)))
        return found
    finally:
        _kernel32.FreeLibrary(module)


def icon_images(exe: Path) -> list[bytes]:
    return [data for _, _, data in _resources(exe, RT_ICON)]


def group_count(exe: Path) -> int:
    return len(_resources(exe, RT_GROUP_ICON))


def replace_icon(exe: Path, ico: bytes) -> None:
    """Removes every icon and icon group the exe has, then adds this icon as group 1 with images 1 to N."""
    old_icons, old_groups = _resources(exe, RT_ICON), _resources(exe, RT_GROUP_ICON)
    language = old_groups[0][1] if old_groups else LANG_EN_US
    entries = _ico_entries(ico)
    group = struct.pack("<HHH", 0, 1, len(entries))
    for number, (directory, _) in enumerate(entries, start=1):
        group += directory + struct.pack("<H", number)

    update = _check(_kernel32.BeginUpdateResourceW(str(exe), False), f"open {exe} for writing")
    try:
        for name, lang, _ in old_icons:
            _check(_kernel32.UpdateResourceW(update, RT_ICON, name, lang, None, 0), "remove an icon")
        for name, lang, _ in old_groups:
            _check(_kernel32.UpdateResourceW(update, RT_GROUP_ICON, name, lang, None, 0), "remove an icon group")
        for number, (_, data) in enumerate(entries, start=1):
            buffer = ctypes.create_string_buffer(data, len(data))
            _check(_kernel32.UpdateResourceW(update, RT_ICON, number, language, buffer, len(data)), "add an icon")
        buffer = ctypes.create_string_buffer(group, len(group))
        _check(_kernel32.UpdateResourceW(update, RT_GROUP_ICON, 1, language, buffer, len(group)), "add the icon group")
    except BaseException:
        _kernel32.EndUpdateResourceW(update, True)  # discard
        raise
    _check(_kernel32.EndUpdateResourceW(update, False), f"write {exe}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("exe", type=Path)
    args = parser.parse_args()
    replace_icon(args.exe, draw_icon())
    print(f"{args.exe}: the project's own icon, {len(SIZES)} sizes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
