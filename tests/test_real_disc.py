"""Checks against the user's own ISO; skipped where it is absent."""
import hashlib
from pathlib import Path

import pytest

import xiso

ISO = Path(__file__).resolve().parent.parent / "Batman Begins (USA).iso"
XBE_SHA1 = "9B5E83CD2B0813C77CDD83C05D608E3986C6295E"

pytestmark = pytest.mark.skipif(not ISO.exists(), reason="the game's ISO is not here")


@pytest.fixture(scope="module")
def entries():
    with ISO.open("rb") as f:
        return xiso.list_files(f, xiso.find_partition(f))


def test_partition_is_redump_layout():
    with ISO.open("rb") as f:
        assert xiso.find_partition(f) == 0x18300000


def test_disc_has_the_expected_files(entries):
    assert len(entries) == 41
    assert sum(e.path.lower().endswith(".xmv") for e in entries) == 30


def test_xbe_is_the_supported_release(entries):
    xbe = next(e for e in entries if e.path == "/default.xbe")
    assert xbe.size == 4108288
    with ISO.open("rb") as f:
        f.seek(0x18300000 + xbe.sector * 2048)
        assert hashlib.sha1(f.read(xbe.size)).hexdigest().upper() == XBE_SHA1
