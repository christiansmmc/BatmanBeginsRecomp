import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

import perf_report  # noqa: E402

OLD_FIRST = ("[  332.600] perf: 14.5 fps 68.97 ms/frame | host 37.50 (draw 37.00: assemble 0.10 bind 0.20 "
             "[check 0.10, upload 0.00] compile 36.73; clear 0.05 copy 0.00 sync 0.00 present 0.40 "
             "[swap 0.20 wait 0.00]) other 31.47")
NEW_FIRST = ("[  100.500] perf: 25.0 fps 40.00 ms/frame | host 10.00 (draw 9.00: assemble 0.10 bind 0.20 "
             "[check 0.10, upload 0.00] compile 3.00 pso 5.00; clear 0.05 copy 0.00 sync 0.00 present 0.40 "
             "[swap 0.20 wait 0.00]) other 30.00")
THREAD = ("[  100.500] perf: title thread per frame: recording 1.00 ms, waiting 2.00 ms (1.0 waits: read-back 0.00 "
          "resource 0.00 fence 0.00 flush 0.00 copy 0.00, next frame 2.00) | read-backs waited for: 0.0 lock, "
          "0.0 NOOVERWRITE")
COUNTS = ("[  100.500] perf: per frame 500 draws (3 raw) 90k verts 1.0 MB upload 0.0 overflow | 100 checks 1.00 MB "
          "hashed | 0.0 tex uploads 0.00 MB, 0.0 new tex, 0.0 new targets, 0.0 target uploads, 0.0 shrinks, "
          "0.0 read-backs 0.0 late | 7 shaders 12 psos this second")
FAST = ("[  101.500] perf: 30.0 fps 33.33 ms/frame | host 2.00 (draw 1.00: assemble 0.10 bind 0.20 "
        "[check 0.10, upload 0.00] compile 0.00 pso 0.00; clear 0.05 copy 0.00 sync 0.00 present 0.40 "
        "[swap 0.20 wait 0.00]) other 31.33")


def test_reads_a_second_with_its_breakdown_and_counts():
    rows = perf_report.parse_log("\n".join(["noise", NEW_FIRST, THREAD, COUNTS, FAST]))
    assert len(rows) == 2
    first = rows[0]
    assert (first.at, first.fps, first.compile, first.pso, first.other) == (100.5, 25.0, 3.0, 5.0, 30.0)
    assert (first.shaders, first.psos) == (7, 12)
    assert first.recording == 1.0
    assert first.waiting == 2.0
    assert rows[1].shaders == 0


def test_a_log_from_before_the_split_has_no_pipeline_time():
    row = perf_report.parse_log(OLD_FIRST)[0]
    assert row.compile == 36.73
    assert row.pso is None


def test_slow_seconds_are_those_under_the_limit():
    rows = perf_report.parse_log("\n".join([NEW_FIRST, FAST]))
    assert [r.at for r in perf_report.slow(rows, 29.0)] == [100.5]


def test_spent_compiling_counts_shaders_and_pipelines_for_the_whole_second():
    row = perf_report.parse_log(NEW_FIRST)[0]
    # 25 frames at 3 + 5 ms a frame
    assert perf_report.building_ms(row) == 200.0


def test_blame_names_compilation_only_when_it_explains_the_lost_time():
    rows = perf_report.parse_log("\n".join([NEW_FIRST, FAST]))
    # 25 fps at 30 wanted loses 5 frames of 33.3 ms, 167 ms; 200 ms were spent building
    assert perf_report.blame(rows[0]) == "build"
    other = perf_report.parse_log(OLD_FIRST.replace("compile 36.73", "compile 0.10"))[0]
    assert perf_report.blame(other) == "other"


def test_table_has_a_line_per_slow_second():
    rows = perf_report.parse_log("\n".join([NEW_FIRST, FAST]))
    lines = perf_report.table(perf_report.slow(rows, 29.0))
    assert len(lines) == 2
    assert "100.5" in lines[1]
    assert "build" in lines[1]


def test_summary_counts_and_totals():
    rows = perf_report.parse_log("\n".join([NEW_FIRST, COUNTS, FAST]))
    summary = perf_report.summary(rows, 29.0)
    assert summary["seconds"] == 2
    assert summary["slow"] == 1
    assert summary["slow_build"] == 1
    assert summary["shaders"] == 7
    assert summary["psos"] == 12
    assert summary["build_ms"] == 200.0


def test_side_by_side_pairs_each_slow_second_with_the_other_run_at_that_time():
    first = perf_report.parse_log("\n".join([NEW_FIRST, COUNTS, FAST]))
    second = perf_report.parse_log(FAST.replace("[  101.500]", "[  100.900]"))
    lines = perf_report.side_by_side(first, second, 29.0)
    assert len(lines) == 2
    assert "100.5" in lines[1] and "25.0" in lines[1] and "100.9" in lines[1] and "30.0" in lines[1]


def test_side_by_side_also_lists_a_second_only_the_later_run_lost():
    first = perf_report.parse_log(FAST)
    second = perf_report.parse_log(NEW_FIRST.replace("[  100.500]", "[  300.000]"))
    lines = perf_report.side_by_side(first, second, 29.0)
    assert len(lines) == 2
    assert "300.0" in lines[1] and "25.0" in lines[1]
