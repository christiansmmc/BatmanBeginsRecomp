import hashlib
import hmac
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import save_sign  # noqa: E402

KEY = bytes(range(16))
SAVES = ROOT / ".local" / "saves" / "reference"  # real saves from the port, never in Git


def make_save(key: bytes = bytes(16)) -> bytes:
    body = bytes(i % 251 for i in range(0x800))
    return body + hmac.new(key, body, hashlib.sha1).digest() + bytes(0xA00 - 0x814)


def test_a_save_signed_with_the_key_checks_only_with_that_key():
    save = make_save(KEY)
    assert save_sign.is_signed_with(save, KEY)
    assert not save_sign.is_signed_with(save, bytes(16))


def test_resigning_changes_the_signature_and_nothing_else():
    save = make_save()
    new = save_sign.resign(save, KEY)
    assert save_sign.is_signed_with(new, KEY)
    assert not save_sign.is_signed_with(new, bytes(16))
    assert new[:0x800] == save[:0x800]
    assert new[0x814:] == save[0x814:]
    assert len(new) == len(save)


def test_refuses_a_file_too_short_to_be_a_save():
    with pytest.raises(ValueError):
        save_sign.resign(bytes(100), KEY)


def save_with_entry(offset: int, value: int) -> bytes:
    body = bytearray(0xA00)
    body[offset:offset + 4] = bytes([0x3E, 0, value, 0])
    return bytes(body)


def test_the_widescreen_entry_is_read_wherever_it_sits():
    assert save_sign.widescreen_of(save_with_entry(0x84, 1)) == 1
    assert save_sign.widescreen_of(save_with_entry(0x80, 0)) == 0


def test_setting_the_widescreen_entry_changes_only_its_value():
    save = save_with_entry(0x84, 1)
    new = save_sign.set_widescreen(save, 0)
    assert new[0x86] == 0
    assert new[:0x86] == save[:0x86]
    assert new[0x88:] == save[0x88:]


def test_refuses_a_save_where_the_entry_is_not_found_once():
    with pytest.raises(ValueError):
        save_sign.widescreen_of(bytes(0xA00))
    twice = bytearray(save_with_entry(0x84, 1))
    twice[0x90:0x94] = bytes([0x3E, 0, 1, 0])
    with pytest.raises(ValueError):
        save_sign.widescreen_of(bytes(twice))


@pytest.mark.skipif(not SAVES.is_dir(), reason="the user's saves are not in Git")
def test_the_ports_saves_are_signed_with_a_zero_key():
    for name in ("13AD93548315/BATMAN A", "13AD93548316/BATMAN B"):
        assert save_sign.is_signed_with((SAVES / name).read_bytes(), bytes(16))


@pytest.mark.skipif(not SAVES.is_dir(), reason="the user's saves are not in Git")
def test_the_ports_saves_keep_the_widescreen_option_on():
    for name in ("13AD93548315/BATMAN A", "13AD93548316/BATMAN B"):
        assert save_sign.widescreen_of((SAVES / name).read_bytes()) == 1
