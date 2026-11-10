import numpy as np
import pytest

import voice_sync

RATE = voice_sync.RATE


def noise(seconds, seed=1):
    return np.random.default_rng(seed).normal(0, 0.2, int(seconds * RATE)).astype(np.float32)


def test_finds_the_delay_of_a_stretch_of_sound():
    sound = noise(20.0)
    late = np.concatenate([np.zeros(int(1.25 * RATE), np.float32), sound])
    delay, score = voice_sync.sound_delay(sound, late, at=5.0, seconds=3.0, reach=3.0)
    assert delay == pytest.approx(1.25, abs=0.002)
    assert score > 0.9


def test_silence_has_no_delay_to_report():
    delay, score = voice_sync.sound_delay(np.zeros(RATE * 20, np.float32), noise(20.0), at=5.0, seconds=3.0, reach=3.0)
    assert score == 0.0


def test_shift_compares_each_delay_with_the_clapper():
    # both clocks start at different times; only a change from the movie's own delays means the sound moved against the picture
    assert voice_sync.sync_shift(movie_picture=0.5, movie_sound=0.2, scene_picture=0.5, scene_sound=0.2) == pytest.approx(0.0)
    assert voice_sync.sync_shift(movie_picture=0.5, movie_sound=0.2, scene_picture=0.6, scene_sound=0.2) == pytest.approx(0.1)
    assert voice_sync.sync_shift(movie_picture=0.5, movie_sound=0.2, scene_picture=0.5, scene_sound=0.3) == pytest.approx(-0.1)


def test_picture_delay_of_a_moving_film():
    rng = np.random.default_rng(3)
    base = rng.normal(0, 40, (300, 12, 16)).astype(np.float32)
    base = np.cumsum(base, axis=0)  # each frame close to the last, like a film
    delayed = np.concatenate([base[:6], base])
    delay = voice_sync.picture_delay(base, delayed, start=60, count=120, reach=30)
    assert delay == pytest.approx(6 / voice_sync.FILM_RATE, abs=0.002)
