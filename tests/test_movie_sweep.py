import csv
import struct
import wave

import numpy as np
import pytest

import movie_sweep
from movie_sweep import evaluate


def test_tolerance_is_two_percent_or_half_a_second():
    assert evaluate(100, 101.9) and not evaluate(100, 103)
    assert evaluate(1, 1.4) and not evaluate(1, 1.6)


def test_too_short_fails_like_too_long():
    assert evaluate(100, 98.1) and not evaluate(100, 97)
    assert not evaluate(10, 0)


def test_played_seconds_are_from_open_to_close_of_that_movie():
    log = """
[    2.001] movie open int1_us.xmv 2.000
[    9.000] perf: 30 fps
[  230.500] movie close int1_us.xmv 230.500
"""
    assert movie_sweep.played_seconds(log) == pytest.approx(228.5)


def test_a_movie_that_never_closed_has_no_played_time():
    assert movie_sweep.played_seconds("[ 2.0] movie open int1_us.xmv 2.000\n") is None
    assert movie_sweep.played_seconds("nothing here\n") is None


def test_open_time_comes_from_the_log():
    assert movie_sweep.open_time("[ 2.0] movie open int1_us.xmv 2.250\n") == pytest.approx(2.25)
    assert movie_sweep.open_time("nothing\n") is None


def test_only_the_requested_movie_counts():
    log = "[1] movie open a.xmv 1.0\n[2] movie close a.xmv 2.0\n[3] movie open b.xmv 3.0\n[9] movie close b.xmv 9.0\n"
    assert movie_sweep.played_seconds(log, "b.xmv") == pytest.approx(6.0)


def test_expected_duration_is_the_last_packet_not_the_header(monkeypatch):
    # ea_US.xmv: the header says 6.408 s, the packets end at 3.16 s, and the game plays those 3.2 s
    monkeypatch.setattr(movie_sweep, "ffprobe_duration", lambda path: 6.408)
    monkeypatch.setattr(movie_sweep, "last_packet_time", lambda path: 3.16)
    assert movie_sweep.expected_duration("x.xmv") == 3.16


def test_expected_duration_falls_back_to_the_header_without_packets(monkeypatch):
    monkeypatch.setattr(movie_sweep, "ffprobe_duration", lambda path: 12.5)
    monkeypatch.setattr(movie_sweep, "last_packet_time", lambda path: None)
    assert movie_sweep.expected_duration("x.xmv") == 12.5


def test_sound_needs_the_files_level_and_waveform():
    assert movie_sweep.sound_ok(-20.0, -22.0, 0.9)
    assert not movie_sweep.sound_ok(-20.0, -60.0, 0.9)
    assert not movie_sweep.sound_ok(-20.0, -5.0, 0.9)
    assert not movie_sweep.sound_ok(-20.0, None, None)
    assert not movie_sweep.sound_ok(-20.0, -21.0, 0.1)


def test_a_file_without_audio_cannot_fail_on_sound():
    assert movie_sweep.sound_ok(None, None, None)
    assert movie_sweep.sound_ok(None, -90.0, None)


def test_silent_file_needs_no_sound():
    assert movie_sweep.sound_ok(-95.0, -120.0, 0.0)


def test_waveform_match_finds_the_same_sound_late_in_the_recording():
    rng = np.random.default_rng(2)
    source = rng.normal(size=4000)
    recorded = np.concatenate([np.zeros(1000), 0.5 * source, np.zeros(500)])
    match, lag = movie_sweep.waveform_match(recorded, source)
    assert match > 0.99
    assert lag == pytest.approx(1000 / movie_sweep.SOUND_RATE)


def test_waveform_match_drops_at_another_speed():
    time = np.arange(16000) / 8000
    source = np.sin(2 * np.pi * (200 + 300 * time) * time)
    fast = np.interp(np.arange(0, len(source) - 1, 1.03), np.arange(len(source)), source)
    assert movie_sweep.waveform_match(fast, source)[0] < 0.5


def test_best_match_finds_the_frame_and_its_time():
    rng = np.random.default_rng(1)
    frames = rng.integers(0, 255, (5, 12, 16), dtype=np.uint8)
    shot = frames[3].astype(float) * 0.8 + 10
    correlation, index = movie_sweep.best_match(shot, frames)
    assert index == 3
    assert correlation > 0.99


def test_shot_is_compared_only_where_the_game_draws_the_movie(tmp_path):
    from PIL import Image

    window = Image.new("L", (1280, 720), 0)
    window.paste(200, (int(1280 * movie_sweep.PICTURE_LEFT), 0, int(1280 * movie_sweep.PICTURE_RIGHT), 720))
    path = tmp_path / "shot.png"
    window.save(path)
    grey = movie_sweep.shot_grey(path)
    assert grey.shape == (movie_sweep.COMPARE_HEIGHT, movie_sweep.COMPARE_WIDTH)
    assert grey.min() >= 199


def test_correlation_of_flat_images_is_zero():
    flat = np.full((4, 4), 7, dtype=np.uint8)
    assert movie_sweep.correlation(flat, flat) == 0.0


def test_black_shot_of_a_moving_picture_fails():
    assert not movie_sweep.picture_ok(shot_mean=0.5, source_mean=80.0, source_std=40.0, match=0.0)


def test_matching_shot_passes():
    assert movie_sweep.picture_ok(shot_mean=70.0, source_mean=80.0, source_std=40.0, match=0.9)


def test_a_dark_scene_passes_when_it_matches():
    assert movie_sweep.picture_ok(shot_mean=4.6, source_mean=4.5, source_std=13.0, match=1.0)


def test_wrong_brightness_fails_even_with_the_right_shapes():
    assert not movie_sweep.picture_ok(shot_mean=70.0, source_mean=20.0, source_std=30.0, match=0.9)


def test_wrong_picture_fails():
    assert not movie_sweep.picture_ok(shot_mean=70.0, source_mean=80.0, source_std=40.0, match=0.1)


def test_flat_source_passes_on_brightness_alone():
    assert movie_sweep.picture_ok(shot_mean=1.0, source_mean=0.5, source_std=0.2, match=0.0)
    assert not movie_sweep.picture_ok(shot_mean=120.0, source_mean=0.5, source_std=0.2, match=0.0)


def shot_check(**changes):
    values = dict(shot="s.png", shot_mean=50.0, source_mean=52.0, source_std=30.0, match=0.9, match_time_s=5.0,
                  shot_time_s=5.1)
    values.update(changes)
    return values


def row(**changes):
    values = dict(file="a.xmv", expected_s=10.0, header_s=10.5, played_s=10.1, source_rms_db=-20.0, port_rms_db=-21.0,
                  sound_match=0.9, sound_lag_s=8.0, checks=[shot_check(), shot_check(), shot_check()],
                  crashed=False, user_game_seen=False, run_dir="r")
    values.update(changes)
    return values


def test_row_passes_only_with_speed_sound_and_picture():
    assert movie_sweep.finish_row(row())["passed"] is True
    assert movie_sweep.finish_row(row(played_s=20.0))["passed"] is False
    assert movie_sweep.finish_row(row(played_s=None))["passed"] is False
    assert movie_sweep.finish_row(row(port_rms_db=-70.0))["passed"] is False
    assert movie_sweep.finish_row(row(sound_match=0.1))["passed"] is False
    assert movie_sweep.finish_row(row(crashed=True))["passed"] is False


def test_every_shot_must_show_the_picture():
    bad = [shot_check(), shot_check(match=0.0), shot_check()]
    finished = movie_sweep.finish_row(row(checks=bad))
    assert finished["picture_ok"] is False
    assert finished["worst_match"] == 0.0
    assert movie_sweep.finish_row(row(checks=[]))["picture_ok"] is False


def test_row_reports_the_middle_shot_and_the_worst_offset():
    checks = [shot_check(shot="a.png", shot_time_s=2.0, match_time_s=1.0), shot_check(shot="b.png"),
              shot_check(shot="c.png")]
    finished = movie_sweep.finish_row(row(checks=checks))
    assert finished["shot"] == "b.png"
    assert finished["worst_offset_s"] == pytest.approx(1.0)


def test_a_flat_shot_is_compared_with_nearby_frames_only():
    frames = np.stack([np.full((2, 2), 0, dtype=np.uint8), np.full((2, 2), 90, dtype=np.uint8)])
    frames[1][0, 0] = 200
    times = [0.0, 5.0]
    shot = np.zeros((2, 2))
    near = movie_sweep.compare_shot(shot, frames, times, shot_time=4.9)
    assert near["source_mean"] > 50
    assert not movie_sweep.picture_ok(near["shot_mean"], near["source_mean"], near["source_std"], near["match"])


def test_report_is_a_csv_with_one_row_per_movie(tmp_path):
    report = tmp_path / "sub" / "report.csv"
    movie_sweep.write_report(report, [movie_sweep.finish_row(row()), movie_sweep.finish_row(row(file="b.xmv"))])
    with report.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    assert [r["file"] for r in rows] == ["a.xmv", "b.xmv"]
    assert rows[0]["passed"] == "True"


def test_recheck_uses_each_movies_newest_run(tmp_path, monkeypatch):
    monkeypatch.setattr(movie_sweep, "RUNS", tmp_path)
    for name in ("a-20261031-100000", "a-20261031-110000", "ab-20261031-120000"):
        (tmp_path / name).mkdir()
    assert movie_sweep.newest_run("a").name == "a-20261031-110000"
    assert movie_sweep.newest_run("missing") is None


def test_finds_all_movies_on_the_real_disc_files():
    movies = movie_sweep.find_movies()
    if not movies:
        pytest.skip("assets not extracted")
    assert len(movies) == 30


def test_sweep_repeats_a_run_the_users_game_interrupted(tmp_path, monkeypatch):
    results = iter([{"user_game_seen": True, "run_dir": "first"}, {"user_game_seen": False, "run_dir": "second"}])
    monkeypatch.setattr(movie_sweep.run_game, "run", lambda config: next(results))
    assert movie_sweep.run_until_undisturbed(object())["run_dir"] == "second"


HOLD_LOG = """
[    8.200] movie open tt_us.xmv 8.200
[   10.230] movie hold tt_us.xmv 10.230
[   11.235] movie skipped tt_us.xmv 11.235
[   11.260] movie close tt_us.xmv 11.260
[   11.700] movie open wb_US.xmv 11.700
"""


def test_hold_times_come_from_the_log():
    times = movie_sweep.hold_times(HOLD_LOG, "tt_us.xmv")
    assert times == dict(opened=8.2, held=10.23, skipped=11.235, closed=11.26, next_open=11.7)


def test_hold_times_without_a_skip_are_missing():
    log = "[ 1.0] movie open a.xmv 1.0\n[ 5.0] movie close a.xmv 5.0\n"
    times = movie_sweep.hold_times(log, "a.xmv")
    assert times["held"] is None and times["skipped"] is None and times["closed"] == 5.0 and times["next_open"] is None


def test_a_hold_passes_when_the_movie_closes_a_second_after_the_press():
    assert movie_sweep.hold_ok(hold_at=2.0, close_after_hold=1.03, after_rms=-90.0, crashed=False)
    assert not movie_sweep.hold_ok(hold_at=2.0, close_after_hold=1.5, after_rms=-90.0, crashed=False)
    assert not movie_sweep.hold_ok(hold_at=2.0, close_after_hold=0.5, after_rms=-90.0, crashed=False)
    assert not movie_sweep.hold_ok(hold_at=None, close_after_hold=None, after_rms=-90.0, crashed=False)


def test_a_hold_fails_if_the_sound_goes_on_or_the_game_crashes():
    assert not movie_sweep.hold_ok(hold_at=2.0, close_after_hold=1.0, after_rms=-20.0, crashed=False)
    assert not movie_sweep.hold_ok(hold_at=2.0, close_after_hold=1.0, after_rms=-90.0, crashed=True)
    assert movie_sweep.hold_ok(hold_at=2.0, close_after_hold=1.0, after_rms=None, crashed=False)  # next movie starts too soon
    assert movie_sweep.hold_ok(hold_at=2.0, close_after_hold=1.0, after_rms=-47.0, crashed=False)  # what a natural end leaves there


def test_only_movies_with_time_for_the_hold_are_tried():
    assert movie_sweep.long_enough_to_hold(4.97)
    assert not movie_sweep.long_enough_to_hold(3.16)


def test_the_hold_report_writes_every_column(tmp_path):
    report = tmp_path / "hold.csv"
    row = dict.fromkeys(movie_sweep.HOLD_COLUMNS, 1)
    movie_sweep.write_report(report, [row], movie_sweep.HOLD_COLUMNS)
    with report.open(encoding="utf-8") as handle:
        assert next(csv.DictReader(handle))["file"] == "1"
