from __future__ import annotations

import json
from pathlib import Path

import pytest

from emfs_validation import convert
from emfs_validation.convert import ConversionError, Fragment, Record, convert_run, latest_runs, main, parse_transcript

BOARD = "stm32h757i-eval"


def write_run(directory: Path, lines: list[str], board: str = BOARD) -> Path:
    directory.mkdir(parents=True)
    (directory / "transcript.log").write_text("".join(line + "\n" for line in lines), encoding="utf-8")
    (directory / "run.json").write_text(json.dumps({"board": board}), encoding="utf-8")
    return directory


def result(**fields: object) -> dict[str, object]:
    entry: dict[str, object] = {
        "metric": "C-1",
        "workload": "L1 65536 64",
        "subject": "embedded-filesystem/nor",
        "device": "qspi",
        "unit": "%",
        "value": 3.0,
        "ramBytes": 7680,
        "better": "lower",
    }
    entry.update(fields)
    return entry


def test_a_result_line_becomes_a_record_of_its_kind_and_id():
    records = parse_transcript(["EVT bench id=3 device=qspi op=erase n=16"])

    assert records == [Record("bench", 3, {"device": "qspi", "op": "erase", "n": "16"})]


def test_sent_lines_answers_and_events_without_an_id_are_not_records():
    records = parse_transcript(["> bench.erase qspi n=16", "EVT boot board=stm32h757i-eval reset=pin", "OK", "ERR usage", ""])

    assert records == []


def test_the_lines_of_a_record_are_joined():
    records = parse_transcript(["EVT phase id=2 device=qspi", "EVT phase id=2 elapsedNs=5"])

    assert records == [Record("phase", 2, {"device": "qspi", "elapsedNs": "5"})]


def test_a_list_continues_under_the_same_key():
    records = parse_transcript(["EVT phase id=2 histogram=1,2", "EVT phase id=2 histogram=3,4"])

    assert records[0].fields["histogram"] == "1,2,3,4"
    assert records[0].values("histogram") == [1, 2, 3, 4]


def test_an_empty_list_has_no_values():
    records = parse_transcript(["EVT phase id=2 histogram="])

    assert records[0].values("histogram") == []


def test_a_sent_line_does_not_end_a_record():
    records = parse_transcript(["EVT phase id=2 a=1", "> \x03", "EVT phase id=2 b=2"])

    assert records == [Record("phase", 2, {"a": "1", "b": "2"})]


def test_a_received_line_ends_a_record():
    records = parse_transcript(["EVT phase id=1 a=1", "OK", "EVT phase id=1 a=2"])

    assert records == [Record("phase", 1, {"a": "1"}), Record("phase", 1, {"a": "2"})]


def test_a_record_of_another_kind_or_id_starts_a_record():
    records = parse_transcript(["EVT phase id=1 a=1", "EVT window id=1 a=2", "EVT window id=2 a=3"])

    assert [(record.kind, record.id) for record in records] == [("phase", 1), ("window", 1), ("window", 2)]


def test_a_pair_without_a_value_is_an_error():
    with pytest.raises(ConversionError, match="line 2"):
        parse_transcript(["OK", "EVT bench id=1 device"])


def test_a_record_reports_a_missing_key():
    record = Record("bench", 1, {})

    with pytest.raises(ConversionError, match="bench id=1 has no 'device'"):
        record.field("device")


def test_a_record_reports_a_value_that_is_not_a_number():
    record = Record("bench", 1, {"n": "many"})

    with pytest.raises(ConversionError, match="'n' of bench id=1 is not a number"):
        record.number("n")


def test_a_number_of_a_record():
    assert Record("bench", 1, {"n": "16"}).number("n") == 16


def test_a_device_is_named_by_the_board_and_carries_the_run():
    fragment = Fragment(BOARD, "2026_10_20", {})

    name = fragment.device("qspi", {"kind": "nor", "sectors": "4096"})

    assert name == "stm32h757i-eval/qspi"
    assert fragment.devices == {name: {"kind": "nor", "sectors": "4096", "run": "2026_10_20"}}


def test_a_device_carries_the_model_it_is_converted_with():
    models = {"stm32h757i-eval/qspi": {"model": "nor-qspi-16m", "sectorErase": "45000000"}}
    fragment = Fragment(BOARD, "2026_10_20", models)

    fragment.device("qspi", {"kind": "nor"})

    assert fragment.devices["stm32h757i-eval/qspi"] == {
        "kind": "nor",
        "run": "2026_10_20",
        "model": "nor-qspi-16m",
        "model.sectorErase": "45000000",
    }


def test_a_device_reported_twice_alike_is_one_device():
    fragment = Fragment(BOARD, "2026_10_20", {})
    fragment.device("qspi", {"kind": "nor"})

    fragment.device("qspi", {"kind": "nor"})

    assert list(fragment.devices) == ["stm32h757i-eval/qspi"]


def test_a_device_reported_twice_differently_is_an_error():
    fragment = Fragment(BOARD, "2026_10_20", {})
    fragment.device("qspi", {"kind": "nor"})

    with pytest.raises(ConversionError, match="device 'qspi' is reported with different parameters"):
        fragment.device("qspi", {"kind": "block"})


def test_a_result_is_on_a_device_of_the_board():
    fragment = Fragment(BOARD, "2026_10_20", {})
    fragment.device("qspi", {"kind": "nor"})

    fragment.result(result())

    assert fragment.results == [result(device="stm32h757i-eval/qspi")]


def test_a_result_on_an_unknown_device_is_an_error():
    fragment = Fragment(BOARD, "2026_10_20", {})

    with pytest.raises(ConversionError, match="unknown device 'qspi'"):
        fragment.result(result())


@pytest.mark.parametrize("key", ["metric", "workload", "subject", "device", "unit", "value", "ramBytes", "better"])
def test_a_result_needs_every_key_of_a_fragment(key):
    fragment = Fragment(BOARD, "2026_10_20", {})
    fragment.device("qspi", {"kind": "nor"})
    incomplete = result()
    del incomplete[key]

    with pytest.raises(ConversionError, match=f"has no '{key}'"):
        fragment.result(incomplete)


def test_a_device_record_becomes_a_device_without_its_id_and_name(tmp_path):
    run = write_run(tmp_path / "2026_10_20", ["EVT device id=1 device=qspi kind=nor sectors=4096 chip=20ba19"])

    fragment = convert_run(run, {})

    assert fragment == {
        "devices": {"stm32h757i-eval/qspi": {"kind": "nor", "sectors": "4096", "chip": "20ba19", "run": "2026_10_20"}},
        "results": [],
    }


@pytest.mark.parametrize("key", ["run", "model", "model.sectorErase"])
def test_a_device_record_with_a_key_of_the_conversion_is_an_error(tmp_path, key):
    run = write_run(tmp_path / "2026_10_20", [f"EVT device id=1 device=qspi {key}=1"])

    with pytest.raises(ConversionError, match=f"device id=1 has '{key}'"):
        convert_run(run, {})


def test_records_of_unknown_kinds_are_ignored(tmp_path):
    run = write_run(tmp_path / "2026_10_20", ["EVT later id=1 device=qspi"])

    assert convert_run(run, {}) == {"devices": {}, "results": []}


def test_each_kind_has_its_converter(tmp_path, monkeypatch):
    def convert_bench(fragment, record):
        fragment.result(result(metric="bench", value=record.number("medianNs")))

    monkeypatch.setitem(convert.CONVERTERS, "bench", convert_bench)
    run = write_run(tmp_path / "2026_10_20", ["EVT device id=1 device=qspi kind=nor", "EVT bench id=2 device=qspi medianNs=900"])

    fragment = convert_run(run, {})

    assert fragment["results"] == [result(metric="bench", value=900, device="stm32h757i-eval/qspi")]


def test_a_run_without_a_board_is_an_error(tmp_path):
    run = write_run(tmp_path / "2026_10_20", [])
    (run / "run.json").write_text("{}", encoding="utf-8")

    with pytest.raises(ConversionError, match="names no board"):
        convert_run(run, {})


def test_the_latest_run_of_every_board_is_converted(tmp_path):
    for run in ["stm32h757i_eval/2026_10_20", "stm32h757i_eval/2026_11_02", "stm32h745i_disco/2026_10_21"]:
        (tmp_path / run).mkdir(parents=True)
    (tmp_path / "README.md").write_text("", encoding="utf-8")
    (tmp_path / "stm32h757i_eval" / "notes.md").write_text("", encoding="utf-8")

    assert latest_runs(tmp_path) == [tmp_path / "stm32h745i_disco/2026_10_21", tmp_path / "stm32h757i_eval/2026_11_02"]


def test_runs_of_a_day_are_ordered_by_their_number(tmp_path):
    for run in ["2026_10_20", "2026_10_20_2", "2026_10_20_10", "2026_10_19_11"]:
        (tmp_path / "stm32h757i_eval" / run).mkdir(parents=True)

    assert latest_runs(tmp_path) == [tmp_path / "stm32h757i_eval/2026_10_20_10"]


def test_directories_not_named_by_a_date_are_not_runs(tmp_path):
    for run in ["2026_10_20", "notes", "2026_10_20_x"]:
        (tmp_path / "stm32h757i_eval" / run).mkdir(parents=True)

    assert latest_runs(tmp_path) == [tmp_path / "stm32h757i_eval/2026_10_20"]


def test_a_board_without_runs_is_skipped(tmp_path):
    (tmp_path / "stm32h757i_eval").mkdir()

    assert latest_runs(tmp_path) == []


def test_missing_results_are_an_error(tmp_path):
    with pytest.raises(ConversionError, match="no directory"):
        latest_runs(tmp_path / "results")


def test_the_command_writes_a_fragment_per_board(tmp_path, capsys):
    results = tmp_path / "results"
    write_run(results / "stm32h757i_eval" / "2026_10_20", ["EVT device id=1 device=qspi kind=nor"])
    write_run(results / "stm32h745i_disco" / "2026_10_21", ["EVT device id=1 device=qspi kind=nor"], board="stm32h745i-disco")
    output = tmp_path / "fragments"

    assert main(["--results", str(results), "--output-directory", str(output)]) == 0

    assert sorted(path.name for path in output.iterdir()) == ["stm32h745i_disco.json", "stm32h757i_eval.json"]
    fragment = json.loads((output / "stm32h757i_eval.json").read_text(encoding="utf-8"))
    assert list(fragment["devices"]) == ["stm32h757i-eval/qspi"]
    assert "stm32h757i_eval/2026_10_20: 1 devices, 0 results" in capsys.readouterr().out


def test_the_command_converts_a_run_given_by_its_directory(tmp_path):
    run = write_run(tmp_path / "2026_10_22", ["EVT device id=1 device=qspi kind=nor"])
    output = tmp_path / "fragments"

    assert main(["--run", str(run), "--output-directory", str(output)]) == 0

    assert [path.name for path in output.iterdir()] == ["2026_10_22.json"]


def test_the_command_removes_the_fragments_of_an_earlier_conversion(tmp_path):
    results = tmp_path / "results"
    results.mkdir()
    output = tmp_path / "fragments"
    output.mkdir()
    (output / "stm32h757i_eval.json").write_text("{}", encoding="utf-8")
    (output / "keep.txt").write_text("", encoding="utf-8")

    assert main(["--results", str(results), "--output-directory", str(output)]) == 0

    assert [path.name for path in output.iterdir()] == ["keep.txt"]


def test_the_command_takes_the_parameters_of_the_models(tmp_path):
    run = write_run(tmp_path / "2026_10_20", ["EVT device id=1 device=qspi kind=nor"])
    models = tmp_path / "models.json"
    models.write_text(json.dumps({"stm32h757i-eval/qspi": {"model": "nor-qspi-16m"}}), encoding="utf-8")
    output = tmp_path / "fragments"

    assert main(["--run", str(run), "--models", str(models), "--output-directory", str(output)]) == 0

    fragment = json.loads((output / "2026_10_20.json").read_text(encoding="utf-8"))
    assert fragment["devices"]["stm32h757i-eval/qspi"]["model"] == "nor-qspi-16m"


def test_the_command_reports_a_conversion_error(tmp_path, capsys):
    run = write_run(tmp_path / "2026_10_20", ["EVT bench id=1 device"])

    assert main(["--run", str(run), "--output-directory", str(tmp_path / "fragments")]) == 2

    assert "convert: " in capsys.readouterr().err


def test_an_id_that_is_not_a_number_is_an_error():
    with pytest.raises(ConversionError, match="line 1: id 'x' is not a number"):
        parse_transcript(["EVT bench id=x"])


def test_a_record_reports_a_list_that_is_not_of_numbers():
    with pytest.raises(ConversionError, match="'histogram' of phase id=1 is not a list of numbers"):
        Record("phase", 1, {"histogram": "1,x"}).values("histogram")


def test_a_run_json_that_is_not_an_object_is_an_error(tmp_path):
    run = write_run(tmp_path / "2026_10_20", [])
    (run / "run.json").write_text("[]", encoding="utf-8")

    with pytest.raises(ConversionError, match="run.json is not an object"):
        convert_run(run, {})


def test_a_run_without_run_json_is_an_error(tmp_path):
    run = write_run(tmp_path / "2026_10_20", [])
    (run / "run.json").unlink()

    with pytest.raises(ConversionError, match="cannot read run.json"):
        convert_run(run, {})


def test_a_run_without_a_transcript_is_an_error(tmp_path):
    run = write_run(tmp_path / "2026_10_20", [])
    (run / "transcript.log").unlink()

    with pytest.raises(ConversionError, match="cannot read transcript.log"):
        convert_run(run, {})


def test_unreadable_models_are_an_error(tmp_path, capsys):
    run = write_run(tmp_path / "2026_10_20", [])

    assert main(["--run", str(run), "--models", str(tmp_path / "models.json"), "--output-directory", str(tmp_path / "fragments")]) == 2

    assert "cannot read the models" in capsys.readouterr().err


@pytest.mark.parametrize("models", ["[]", '{"stm32h757i-eval/qspi": "nor-qspi-16m"}'])
def test_models_that_are_not_objects_by_device_are_an_error(tmp_path, capsys, models):
    run = write_run(tmp_path / "2026_10_20", [])
    path = tmp_path / "models.json"
    path.write_text(models, encoding="utf-8")

    assert main(["--run", str(run), "--models", str(path), "--output-directory", str(tmp_path / "fragments")]) == 2

    assert "not an object of parameters by device" in capsys.readouterr().err


def test_runs_that_would_write_the_same_fragment_are_an_error(tmp_path, capsys):
    first = write_run(tmp_path / "a" / "2026_10_20", [])
    second = write_run(tmp_path / "b" / "2026_10_20", [])

    assert main(["--run", str(first), "--run", str(second), "--output-directory", str(tmp_path / "fragments")]) == 2

    assert "both write 2026_10_20.json" in capsys.readouterr().err


RAM_FLASH = "EVT device id=1 device=ramflash kind=nor sectors=1024 sectorSize=4096 memory=sdram"
CYCLES = (
    "EVT cycles id=2 subject=embedded-filesystem/nor device=ramflash op=mount n=100 ramBytes=13504 "
    "minCycles=900 medianCycles=1000 maxCycles=1500 settledCycles=4000"
)


def f2(workload: str, value: int) -> dict[str, object]:
    return {
        "metric": "F-2",
        "workload": workload,
        "subject": "embedded-filesystem/nor",
        "device": f"{BOARD}/ramflash",
        "unit": "cycles",
        "value": value,
        "ramBytes": 13504,
        "better": "lower",
    }


def test_a_cycles_record_becomes_f2_until_the_completion_and_until_the_volume_is_idle(tmp_path):
    fragment = convert_run(write_run(tmp_path / "2026_10_20", [RAM_FLASH, CYCLES]), {})

    assert fragment["devices"][f"{BOARD}/ramflash"] == {
        "kind": "nor",
        "sectors": "1024",
        "sectorSize": "4096",
        "memory": "sdram",
        "run": "2026_10_20",
    }
    assert fragment["results"] == [f2("mount", 1000), f2("mount settled", 4000)]


def test_a_cycles_record_on_a_device_that_the_run_did_not_report_is_an_error(tmp_path):
    with pytest.raises(ConversionError, match="unknown device 'ramflash'"):
        convert_run(write_run(tmp_path / "2026_10_20", [CYCLES]), {})


@pytest.mark.parametrize("key", ["subject", "device", "op", "ramBytes", "medianCycles", "settledCycles"])
def test_a_cycles_record_needs_its_keys(tmp_path, key):
    line = " ".join(token for token in CYCLES.split() if not token.startswith(f"{key}="))

    with pytest.raises(ConversionError, match=f"has no '{key}'"):
        convert_run(write_run(tmp_path / "2026_10_20", [RAM_FLASH, line]), {})


def test_a_failed_run_of_f2_is_not_converted(tmp_path):
    lines = [RAM_FLASH, CYCLES, "EVT cycles.failed id=3 subject=embedded-filesystem/nor op=open run=0 status=-28"]

    assert convert_run(write_run(tmp_path / "2026_10_20", lines), {})["results"] == []


def test_a_failed_run_of_f2_leaves_the_other_subjects(tmp_path):
    lines = [RAM_FLASH, CYCLES, "EVT cycles.failed id=3 subject=littlefs/ram-matched op=open run=0 status=-28"]

    assert convert_run(write_run(tmp_path / "2026_10_20", lines), {})["results"] == [f2("mount", 1000), f2("mount settled", 4000)]


def test_the_last_run_of_a_subject_replaces_its_earlier_runs(tmp_path):
    again = CYCLES.replace("id=2", "id=3").replace("medianCycles=1000", "medianCycles=1100")
    again = again.replace("settledCycles=4000", "settledCycles=4400")

    fragment = convert_run(write_run(tmp_path / "2026_10_20", [RAM_FLASH, CYCLES, again]), {})

    assert fragment["results"] == [f2("mount", 1100), f2("mount settled", 4400)]


QSPI = "EVT device id=1 device=qspi kind=nor sectors=4096 sectorSize=4096 pageSize=256 chip=20ba20 clockHz=50000000"


def bench(record_id: int, op: str, size: int, median: int) -> str:
    return (
        f"EVT bench id={record_id} device=qspi op={op} size={size} n=64 minNs={median - 10} medianNs={median} "
        f"p99Ns={median + 20} maxNs={median + 30} meanNs={median + 5}"
    )


def test_a_bench_record_becomes_parameters_of_its_device(tmp_path):
    fragment = convert_run(write_run(tmp_path / "2026_10_20", [QSPI, bench(2, "program", 16, 300000)]), {})

    parameters = fragment["devices"][f"{BOARD}/qspi"]
    assert {key: value for key, value in parameters.items() if key.startswith("bench.")} == {
        "bench.program.16.n": "64",
        "bench.program.16.minNs": "299990",
        "bench.program.16.medianNs": "300000",
        "bench.program.16.p99Ns": "300020",
        "bench.program.16.maxNs": "300030",
        "bench.program.16.meanNs": "300005",
    }
    assert fragment["results"] == []


def test_the_benchmarks_give_the_parameters_of_a_nor_model(tmp_path):
    lines = [
        QSPI,
        bench(2, "program", 256, 450000),
        bench(3, "erase", 4096, 45000000),
        bench(4, "read", 256, 7120),
        bench(5, "read", 65536, 1312720),
    ]

    parameters = convert_run(write_run(tmp_path / "2026_10_20", lines), {})["devices"][f"{BOARD}/qspi"]

    assert parameters["measured.pageProgramNs"] == "450000"
    assert parameters["measured.sectorEraseNs"] == "45000000"
    assert parameters["measured.readBytesPerSecond"] == "50000000"
    assert parameters["measured.readFixedNs"] == "2000"


def test_a_model_parameter_waits_for_its_benchmark(tmp_path):
    lines = [QSPI, bench(2, "program", 16, 300000), bench(3, "read", 256, 7120)]

    parameters = convert_run(write_run(tmp_path / "2026_10_20", lines), {})["devices"][f"{BOARD}/qspi"]

    assert not [key for key in parameters if key.startswith("measured.")]


def test_reads_that_take_no_longer_with_their_size_give_no_rate(tmp_path):
    lines = [QSPI, bench(2, "read", 256, 7000), bench(3, "read", 65536, 7000)]

    parameters = convert_run(write_run(tmp_path / "2026_10_20", lines), {})["devices"][f"{BOARD}/qspi"]

    assert "measured.readBytesPerSecond" not in parameters


def test_a_bench_record_on_a_device_that_the_run_did_not_report_is_an_error(tmp_path):
    with pytest.raises(ConversionError, match="benchmark is on an unknown device 'qspi'"):
        convert_run(write_run(tmp_path / "2026_10_20", [bench(1, "read", 256, 7000)]), {})


@pytest.mark.parametrize("key", ["bench.read.256.medianNs", "measured.pageProgramNs"])
def test_a_device_record_with_a_key_of_the_benchmarks_is_an_error(tmp_path, key):
    with pytest.raises(ConversionError, match=f"has '{key}'"):
        convert_run(write_run(tmp_path / "2026_10_20", [f"{QSPI} {key}=1"]), {})


def test_a_device_reported_again_after_its_benchmarks_keeps_them(tmp_path):
    lines = [QSPI, bench(2, "erase", 4096, 45000000), QSPI.replace("id=1", "id=3")]

    parameters = convert_run(write_run(tmp_path / "2026_10_20", lines), {})["devices"][f"{BOARD}/qspi"]

    assert parameters["measured.sectorEraseNs"] == "45000000"


def test_a_read_faster_than_its_rate_has_no_fixed_cost(tmp_path):
    lines = [QSPI, bench(2, "read", 256, 1000), bench(3, "read", 65536, 1306600)]

    parameters = convert_run(write_run(tmp_path / "2026_10_20", lines), {})["devices"][f"{BOARD}/qspi"]

    assert parameters["measured.readBytesPerSecond"] == "50000000"
    assert parameters["measured.readFixedNs"] == "0"


def test_reads_slower_than_a_byte_per_second_give_no_rate(tmp_path):
    lines = [QSPI, bench(2, "read", 1, 1000), bench(3, "read", 2, 2_000_000_000)]

    parameters = convert_run(write_run(tmp_path / "2026_10_20", lines), {})["devices"][f"{BOARD}/qspi"]

    assert "measured.readBytesPerSecond" not in parameters
