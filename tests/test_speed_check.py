import pytest

from speed_check import compare


def test_within_three_percent_passes():
    assert compare(10.0, 10.29)
    assert compare(10.0, 9.71)
    assert compare(10.0, 10.0)


def test_beyond_three_percent_fails():
    assert not compare(10.0, 10.31)
    assert not compare(10.0, 9.69)


def test_tolerance_can_be_given():
    assert compare(10.0, 10.09, tolerance=0.01)
    assert not compare(10.0, 10.11, tolerance=0.01)


def test_reference_must_be_positive():
    with pytest.raises(ValueError):
        compare(0.0, 1.0)
