from __future__ import annotations

import json
from pathlib import Path

import pytest

from emfs_validation.convert import convert_run
from emfs_validation.workloads import DeviceRuns, Operation, Totals, WorkloadRun, bucket_upper_bound, percentile_ns, workload_results

BOARD = "stm32h757i-eval"
DEVICE = "EVT device id=1 device=qspi kind=nor sectors=4096 sectorSize=4096 pageSize=256"
MODEL = {
    "model": "nor-qspi-16m",
    "typical.pageProgramNs": "400000",
    "typical.sectorEraseNs": "45000000",
    "typical.readBytesPerSecond": "50000000",
    "w6BoundMs": "974.5",
}


def totals(**fields: str | int) -> Totals:
    return Totals.parse({key.replace("_", "."): str(value) for key, value in fields.items()})


def device_runs(runs: dict[tuple[str, int], WorkloadRun], model: dict[str, str] | None = None) -> DeviceRuns:
    return DeviceRuns("qspi", "embedded-filesystem/nor", runs, model or {})


def run_of(phases: dict[str, Totals], windows: dict[str, Totals] | None = None, failed: bool = False) -> WorkloadRun:
    return WorkloadRun(ram_bytes=17764, phases=phases, windows=windows or {}, failed=failed)


def values(entries: list[dict[str, object]]) -> dict[tuple[object, ...], object]:
    return {(entry["metric"], entry["workload"], entry.get("fill")): entry["value"] for entry in entries}


def write_run(directory: Path, lines: list[str]) -> Path:
    directory.mkdir(parents=True)
    (directory / "transcript.log").write_text("".join(line + "\n" for line in lines), encoding="utf-8")
    (directory / "run.json").write_text(json.dumps({"board": BOARD}), encoding="utf-8")
    return directory


def phase_line(record_id: int, workload: str, phase: str, fill: int = 0, rest: str = "") -> str:
    subject = "device=qspi subject=embedded-filesystem/nor"
    return f"EVT phase id={record_id} {subject} workload={workload} phase={phase} fill={fill} ramBytes=17764 {rest}"


def test_the_upper_bound_of_a_bucket_is_the_longest_duration_it_holds():
    assert [bucket_upper_bound(bucket) for bucket in (0, 3, 4, 8, 11, 35, 84)] == [0, 3, 4, 9, 15, 1023, 5242879]


def test_totals_hold_the_counts_and_the_statistics_of_each_operation():
    parsed = totals(
        readNs=3, programNs=4, eraseNs=5, write_calls=2, write_userBytes=128, write_latencyNs=10, write_maxNs=7, write_histogram="35,1,41,1"
    )

    assert parsed.device_ns() == 12
    assert parsed.calls("write") == 2
    assert parsed.user_bytes("write") == 128
    assert parsed.operations["write"] == Operation(2, 128, 10, 7, {35: 1, 41: 1})
    assert parsed.calls("read") == 0
    assert parsed.user_bytes("read") == 0


def test_an_empty_histogram_has_no_buckets():
    assert totals(write_calls=0, write_histogram="").operations["write"].histogram == {}


def test_totals_of_the_same_phase_add_up_with_the_longest_call():
    first = totals(readNs=3, write_calls=1, write_maxNs=7, write_histogram="35,1")

    first.merge(totals(readNs=4, write_calls=2, write_maxNs=5, write_histogram="35,2,41,1"))

    assert first.count("readNs") == 7
    assert first.operations["write"] == Operation(3, 0, 0, 7, {35: 3, 41: 1})


def test_the_percentile_is_the_end_of_the_bucket_of_its_rank_and_at_most_the_longest_call():
    operations = [Operation(99, histogram={35: 99}, max_ns=1000), Operation(1, histogram={84: 1}, max_ns=5_000_000)]

    assert percentile_ns(operations, 0.99) == 1023
    assert percentile_ns(operations, 1.0) == 5_000_000
    assert percentile_ns([], 0.99) == 0


def test_l1_gives_the_throughput_with_erases_charged_at_allocation_and_the_amplification():
    prepare = totals(sectorsErased=10, eraseNs=500_000_000)
    write = totals(programNs=1_000_000, eraseNs=7, allocatedSectors=1, bytesProgrammed=8192, write_calls=64, write_userBytes=4096)
    runs = device_runs({("L1", 0): run_of({"L1 prepare": prepare, "L1 4096 64": write})})

    results = values(workload_results(runs))

    assert results[("W-1", "L1 4096 64", None)] == pytest.approx(4096 / 0.051)
    assert results[("W-4", "L1 4096 64", None)] == 2


def test_l2_compares_an_append_into_the_full_log_with_one_into_the_second_log():
    first = totals(appends=4, programNs=8_000_000, allocatedSectors=0)
    second = totals(appends=4, programNs=4_000_000, allocatedSectors=0)
    log = totals(bytesProgrammed=640, write_calls=10, write_userBytes=640)
    runs = device_runs({("L2", 10): run_of({"L2": log}, {"first": first, "second": second})})

    results = values(workload_results(runs))

    assert results[("W-2", "L2", 10)] == 2
    assert results[("W-4", "L2", 10)] == 1


def test_l2_without_both_windows_has_no_w2():
    runs = device_runs({("L2", 10): run_of({"L2": totals(write_calls=1, write_userBytes=64)}, {"first": totals(appends=4)})})

    assert ("W-2", "L2", 10) not in values(workload_results(runs))


def test_l2_compares_an_append_on_a_full_volume_with_one_on_an_empty_one():
    runs = device_runs(
        {
            ("L2", 10): run_of({"L2": totals(programNs=1_000_000, write_calls=10)}),
            ("L2", 90): run_of({"L2": totals(programNs=3_000_000, write_calls=10)}),
        }
    )

    assert values(workload_results(runs))[("W-3", "L2", None)] == 3


def test_l2_at_half_fill_gives_the_latencies_of_both_logs_and_their_bound():
    log = totals(write_calls=2, write_maxNs=3_000_000, write_histogram="81,2", idle_calls=2, idle_maxNs=100_000_000, idle_histogram="100,2")
    second = totals(sync_calls=1, sync_maxNs=4_000_000, sync_histogram="83,1")
    runs = device_runs({("L2", 50): run_of({"L2": log, "L2 second log": second})}, MODEL)

    results = values(workload_results(runs))

    assert results[("W-6", "L2 p99", 50)] == pytest.approx(4.0)
    assert results[("W-6", "L2 maximum", 50)] == pytest.approx(4.0)
    assert results[("C-2", "L2", 50)] == pytest.approx(4.0 / 974.5)


def test_l3_compares_an_overwrite_in_the_largest_file_with_one_in_the_smallest_and_gives_its_latencies():
    small = totals(programNs=1_000_000, write_calls=64, write_maxNs=2_000_000, write_histogram="79,64")
    large = totals(programNs=3_000_000, write_calls=64, write_maxNs=2_000_000, write_histogram="79,64")
    runs = device_runs({("L3", 50): run_of({"L3 create 512": totals(), "L3 512": small, "L3 16384": large})})

    results = values(workload_results(runs))

    assert results[("W-5", "L3", 50)] == 3
    assert results[("W-6", "L3 maximum", 50)] == pytest.approx(2.0)


def test_l3_with_one_file_has_no_w5():
    runs = device_runs({("L3", 50): run_of({"L3 512": totals(write_calls=64)})})

    assert ("W-5", "L3", 50) not in values(workload_results(runs))


def test_l6_gives_the_read_throughput_and_amplification_the_reads_of_a_random_read_and_the_lookups():
    sequential = totals(readNs=1_000_000, bytesRead=8192, read_calls=2, read_userBytes=4096)
    phases = {
        "L6 sequential": sequential,
        "L6 random 512": totals(readRequests=64, read_calls=64),
        "L6 random 16384": totals(readRequests=128, read_calls=64),
        "L6 lookup 10": totals(readNs=3_200_000, open_calls=32),
    }
    runs = device_runs({("L6", 0): run_of(phases)})

    results = values(workload_results(runs))

    assert results[("R-1", "L6 sequential", None)] == pytest.approx(4_096_000)
    assert results[("R-5", "L6 sequential", None)] == 2
    assert results[("R-2", "L6 random 512", None)] == 1
    assert results[("R-2", "L6 random ratio", None)] == 2
    assert results[("R-3", "L6 lookup 10", None)] == pytest.approx(0.1)


def test_l7_gives_the_time_of_a_mount_at_each_fill_level():
    phases = {"L7 prepare 10": totals(eraseNs=1), "L7 10": totals(readNs=2_000_000), "L7 90": totals(readNs=3_000_000)}
    runs = device_runs({("L7", 0): run_of(phases)})

    results = values(workload_results(runs))

    assert results[("R-4", "L7", 10)] == pytest.approx(2.0)
    assert results[("R-4", "L7", 90)] == pytest.approx(3.0)


def test_c1_compares_the_time_of_the_model_for_the_requests_of_each_phase_with_the_time_they_took():
    phase = totals(readNs=1_000_000, programNs=4_000_000, eraseNs=45_000_000, pagesProgrammed=10, sectorsErased=1, bytesRead=100_000)
    idle = totals(write_calls=1)
    runs = device_runs({("L4", 10): run_of({"L4": phase, "L4 idle": idle}), ("L6", 0): run_of({"L6 create": phase})}, MODEL)

    results = values(workload_results(runs))

    model_ns = 10 * 400_000 + 45_000_000 + 100_000 * 1e9 / 50_000_000
    assert results[("C-1", "L4", 10)] == pytest.approx(abs(model_ns - 50_000_000) / 50_000_000)
    assert results[("C-1", "L6 create", None)] == pytest.approx(abs(model_ns - 50_000_000) / 50_000_000)
    assert ("C-1", "L4 idle", 10) not in results


def test_a_device_without_a_model_has_no_c1_and_no_c2():
    runs = device_runs({("L2", 50): run_of({"L2": totals(readNs=1, write_calls=1, write_maxNs=1, write_histogram="1,1")})})

    metrics = {metric for metric, _, _ in values(workload_results(runs))}

    assert "C-1" not in metrics
    assert "C-2" not in metrics


def test_a_run_that_failed_has_no_results_and_no_part_in_the_erase_of_a_sector():
    phase = totals(write_calls=1, write_userBytes=64, sectorsErased=1, eraseNs=9)
    runs = device_runs({("L1", 0): run_of({"L1 4096 64": phase}, failed=True)})

    assert workload_results(runs) == []
    assert runs.sector_erase_ns() == 0


def test_an_infinite_value_is_written_as_null():
    runs = device_runs({("L1", 0): run_of({"L1 4096 64": totals(write_calls=1, write_userBytes=0, bytesProgrammed=10)})})

    assert values(workload_results(runs))[("W-4", "L1 4096 64", None)] is None


def test_a_transcript_of_a_workload_becomes_its_metrics(tmp_path: Path):
    lines = [
        DEVICE,
        "EVT workload id=2 device=qspi subject=embedded-filesystem/nor workload=L1 fill=0 scale=16 ramBytes=17764",
        phase_line(3, "L1", "L1,4096,64", rest="programNs=1000000 bytesProgrammed=4096 pagesProgrammed=16"),
        "EVT phase id=3 write.calls=64 write.userBytes=4096",
    ]

    fragment = convert_run(write_run(tmp_path / "2026_10_20", lines), {f"{BOARD}/qspi": MODEL})

    results = {(entry["metric"], entry["workload"]): entry for entry in fragment["results"]}
    assert results[("W-1", "L1 4096 64")]["value"] == pytest.approx(4_096_000)
    assert results[("W-1", "L1 4096 64")]["device"] == f"{BOARD}/qspi"
    assert results[("W-1", "L1 4096 64")]["ramBytes"] == 17764
    assert "fill" not in results[("W-1", "L1 4096 64")]
    assert results[("C-1", "L1 4096 64")]["value"] == pytest.approx(abs(16 * 400_000 - 1_000_000) / 1_000_000)


def test_the_last_run_of_a_workload_replaces_earlier_ones_and_phases_of_the_same_name_add_up(tmp_path: Path):
    start = "EVT workload id={} device=qspi subject=embedded-filesystem/nor workload=L7 fill=0 scale=16 ramBytes=17764"
    lines = [
        DEVICE,
        start.format(2),
        phase_line(3, "L7", "L7,10", rest="readNs=9000000"),
        start.format(4),
        phase_line(5, "L7", "L7,10", rest="readNs=1000000"),
        phase_line(6, "L7", "L7,10", rest="readNs=2000000"),
    ]

    fragment = convert_run(write_run(tmp_path / "2026_10_20", lines), {})

    assert [entry["value"] for entry in fragment["results"] if entry["metric"] == "R-4"] == [pytest.approx(3.0)]


def test_a_workload_that_failed_leaves_no_results(tmp_path: Path):
    lines = [
        DEVICE,
        phase_line(2, "L7", "L7,10", rest="readNs=9000000"),
        "EVT workload.failed id=3 device=qspi subject=embedded-filesystem/nor workload=L7 fill=0 phase=L7,10 op=mount result=timeout",
    ]

    assert convert_run(write_run(tmp_path / "2026_10_20", lines), {})["results"] == []


def test_windows_of_a_transcript_give_w2(tmp_path: Path):
    window = "EVT window id={} device=qspi subject=embedded-filesystem/nor workload=L2 fill=50 log={} appends=4 programNs={}"
    lines = [DEVICE, window.format(2, "first", 8_000_000), window.format(3, "second", 4_000_000)]

    fragment = convert_run(write_run(tmp_path / "2026_10_20", lines), {})

    assert [(entry["metric"], entry["fill"], entry["value"]) for entry in fragment["results"]] == [("W-2", 50, 2)]
