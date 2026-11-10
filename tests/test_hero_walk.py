import numpy as np
import pytest

import hero_walk


def rows(speed, seconds=4.0, rate=200.0):
    times = np.arange(0.0, seconds, 1.0 / rate)
    return np.column_stack([times, speed * times, np.zeros_like(times), np.zeros_like(times)])


def test_steady_run_gives_its_speed():
    assert hero_walk.run_speed(rows(3.6)) == pytest.approx(3.6, rel=1e-3)


def test_standing_still_and_stops_do_not_count():
    run = rows(3.6, seconds=4.0)
    still = rows(0.0, seconds=2.0)
    still[:, 0] += 4.0
    still[:, 1] = run[-1, 1]
    assert hero_walk.run_speed(np.vstack([run, still])) == pytest.approx(3.6, rel=1e-3)


def test_window_limits_the_stretch():
    slow, fast = rows(1.0, seconds=4.0), rows(4.0, seconds=4.0)
    fast[:, 0] += 4.0
    fast[:, 1] += slow[-1, 1]
    joined = np.vstack([slow, fast])
    assert hero_walk.run_speed(joined, start=0.0, end=3.5) == pytest.approx(1.0, rel=1e-3)
    assert hero_walk.run_speed(joined, start=4.5, end=8.0) == pytest.approx(4.0, rel=1e-3)


def test_too_few_samples_is_an_error():
    with pytest.raises(ValueError):
        hero_walk.run_speed(rows(3.6, seconds=0.2))


def test_csv_round_trip(tmp_path):
    path = tmp_path / "walk.csv"
    original = rows(2.0, seconds=1.0)
    hero_walk.write_rows(path, original)
    assert hero_walk.read_rows(path) == pytest.approx(original, rel=1e-5)
