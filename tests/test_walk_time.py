import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

import walk_time  # noqa: E402

FPS = 30.0
SIZE = (48, 64)


def film(idle: int, walking: int, seed: int = 1) -> np.ndarray:
    """A still picture, then a slow drift as the walk starts, then one large change, the camera's cut at the trigger."""
    rng = np.random.default_rng(seed)
    still = rng.integers(40, 80, SIZE).astype(np.float32)
    frames = [still + rng.normal(0, 0.2, SIZE) for _ in range(idle)]
    for step in range(walking):
        frames.append(still + 12.0 * np.sin(np.linspace(0, 6, SIZE[1]) + step * 0.2))
    after = rng.integers(120, 200, SIZE).astype(np.float32)
    frames += [after + rng.normal(0, 0.2, SIZE) for _ in range(20)]
    return np.array(frames)


def test_finds_when_the_picture_starts_moving_and_where_it_cuts():
    start, cut = walk_time.walk_frames(film(idle=40, walking=300))
    assert abs(start - 40) <= 2
    assert cut == 340


def test_the_same_walk_in_two_films_gives_the_same_length():
    first = walk_time.walk_frames(film(idle=40, walking=300))
    second = walk_time.walk_frames(film(idle=70, walking=300, seed=2))
    assert abs((first[1] - first[0]) - (second[1] - second[0])) <= 2


def test_seconds_uses_the_films_rate():
    assert walk_time.seconds((40, 340)) == 300 / FPS


def moving_film(times: np.ndarray) -> np.ndarray:
    """A picture that changes smoothly with time, so each moment has its own frame."""
    rows = np.linspace(0, 6, 72)[:, None]
    columns = np.linspace(0, 6, 96)[None, :]
    return np.array([100 + 50 * np.sin(rows + columns * 2 + time * 1.3) + 40 * np.cos(columns - time * 0.7)
                     for time in times], dtype=np.float32)


def test_compare_finds_the_speed_and_the_offset_between_two_films():
    reference = moving_film(np.arange(-1, 12, 1 / FPS))
    other = moving_film(np.arange(-1, 12, 1 / FPS) / 1.01 - 0.1)  # the same moments 1% later, 0.1 s behind
    rows = walk_time.match_frames(other, reference, seconds_after_start=11.0)
    fit = walk_time.fit_stretch(rows, 0.3, 10.3)
    assert abs(fit.slope - 1.01) < 0.002
    assert abs(fit.offset - 0.101) < 0.02
    assert fit.kept == fit.total == 100
    assert fit.residual < 0.01


def test_compare_leaves_out_moments_that_do_not_match():
    reference = moving_film(np.arange(-1, 12, 1 / FPS))
    other = reference.copy()
    other[150:165] = reference[0]  # half a second of something else
    rows = walk_time.match_frames(other, reference, seconds_after_start=11.0)
    fit = walk_time.fit_stretch(rows, 0.3, 10.3)
    assert abs(fit.slope - 1.0) < 0.002
    assert fit.kept < fit.total


def test_compare_ignores_the_brightness_of_each_film():
    reference = moving_film(np.arange(-1, 12, 1 / FPS))
    rows = walk_time.match_frames(reference * 0.5 + 20, reference, seconds_after_start=11.0)
    fit = walk_time.fit_stretch(rows, 0.3, 10.3)
    assert abs(fit.slope - 1.0) < 0.002
    assert abs(fit.offset) < 0.02


def test_a_stretch_with_too_few_moments_has_no_fit():
    reference = moving_film(np.arange(-1, 12, 1 / FPS))
    rows = walk_time.match_frames(reference, reference, seconds_after_start=11.0)
    assert walk_time.fit_stretch(rows, 5.0, 5.2) is None
