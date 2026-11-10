import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

import camera_probe  # noqa: E402


def matrix(xs, ys):
    return (xs, 0.0, 0.0, 0.0, 0.0, ys, 0.0, 0.0, 0.0, 0.0, 1.001, 1.0, 0.0, 0.0, -0.2, 0.0)


class FakeMemory:
    """Guest memory as a dict of address to bytes; reads return what was stored at that exact address."""

    def __init__(self, values):
        self.values = values

    def read(self, address, size):
        return self.values[address][:size]


def test_the_ratio_of_a_16_9_frame():
    assert camera_probe.scale_ratio(matrix(1.8107, 3.219)) == pytest.approx(0.5625, abs=1e-4)


def test_the_ratio_of_a_matrix_with_no_y_scale_is_zero():
    assert camera_probe.scale_ratio(matrix(1.0, 0.0)) == 0.0


def test_samples_of_one_camera_are_counted_together():
    samples = [matrix(1.8107, 3.219)] * 3 + [matrix(80.0, 80.0)] * 5 + [matrix(1.8107, 3.219)]
    found = camera_probe.distinct_projections(samples)
    assert [(p.xs, p.ys, p.count) for p in found] == [(80.0, 80.0, 5), (1.8107, 3.219, 4)]


def test_a_main_camera_is_one_whose_view_is_not_a_square_or_a_slit():
    found = camera_probe.distinct_projections(
        [matrix(1.8107, 3.219)] * 2 + [matrix(80.0, 80.0)] * 9 + [matrix(5.03, 5.03)] * 2 + [matrix(1.5, 2.0)] * 3)
    assert [p.ratio for p in camera_probe.main_cameras(found)] == [0.75, pytest.approx(0.5625, abs=1e-4)]


def test_the_flags_are_read_from_the_game_memory():
    memory = FakeMemory({camera_probe.WIDESCREEN: b"\x01", camera_probe.CONSOLE_WIDESCREEN: b"\x00"})
    assert camera_probe.read_flags(memory) == {"widescreen": 1, "console_widescreen": 0}


def test_a_hit_of_the_camera_function_reads_the_camera_it_ran_for():
    camera = 0x1000
    rect = struct.pack("<4f", 0.0, 0.0, 640.0, 480.0)
    memory = FakeMemory({
        camera_probe.PROJECTION: struct.pack("<16f", *matrix(1.8107, 3.219)),
        camera_probe.WIDESCREEN: b"\x00",
        camera_probe.CONSOLE_WIDESCREEN: b"\x00",
        camera + camera_probe.CAMERA_RECT: rect,
        camera + camera_probe.CAMERA_FOV: struct.pack("<f", 0.6),
        camera + camera_probe.CAMERA_FLAG: b"\x01",
        camera + camera_probe.CAMERA_MATRIX: struct.pack("<16f", *matrix(2.4142, 3.219)),
    })
    hit = camera_probe.read_camera_hit(memory, camera)
    assert hit["fov"] == pytest.approx(0.6)
    assert hit["flag"] == 1
    assert hit["rect"] == [0.0, 0.0, 640.0, 480.0]
    assert hit["projection_ratio"] == pytest.approx(0.5625, abs=1e-4)
    assert hit["camera_matrix_ratio"] == pytest.approx(0.75, abs=1e-4)


def test_only_a_stop_at_the_end_of_the_camera_function_counts():
    assert camera_probe.is_camera_hit({"eip": camera_probe.CAMERA_END})
    assert not camera_probe.is_camera_hit({"eip": camera_probe.CAMERA_END + 1})
