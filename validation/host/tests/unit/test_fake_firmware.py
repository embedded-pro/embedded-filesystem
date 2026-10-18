from __future__ import annotations

import pytest
from ad3_waveforms_bench.terminal import FirmwareError, FirmwareTerminal

from emfs_validation import convert
from emfs_validation.board import load_board
from emfs_validation.fake_firmware import FakeFirmware


@pytest.fixture
def board():
    return load_board("stm32h757i_eval")


@pytest.fixture
def terminal(board):
    terminal = FirmwareTerminal(serial=FakeFirmware(board), timeout=0.5)
    terminal.sync()
    return terminal


@pytest.fixture
def disco():
    terminal = FirmwareTerminal(serial=FakeFirmware(load_board("stm32h745i_disco")), timeout=0.5)
    terminal.sync()
    return terminal


def test_boots_with_the_identity_of_the_board(board):
    terminal = FirmwareTerminal(serial=FakeFirmware(board), timeout=0.5)

    boot = terminal.wait_boot(0.5)

    assert boot.values == {"board": "stm32h757i-eval", "family": "stm32h7", "sysclk": "400000000", "reset": "pin"}


def test_answers_ping(terminal):
    assert terminal.command("ping").ok


def test_answers_info_with_the_96_bit_unique_id_and_the_cause_of_the_last_reset(terminal):
    values = terminal.command("info").values

    assert values["board"] == "stm32h757i-eval"
    assert values["reset"] == "pin"
    assert values["uid"] == "00112233445566778899aabb"


def test_resets_with_a_software_reset_cause(terminal):
    terminal.drain_events("boot")
    terminal.send_nowait("reset")

    assert terminal.wait_boot(0.5).values["reset"] == "sw"
    terminal.sync()
    assert terminal.command("info").values["reset"] == "sw"


def test_answers_firmware_info_with_its_commit_and_a_parked_cortex_m4(terminal):
    assert terminal.command("firmware.info").values == {"commit": "fake", "cm4": "stopped"}


def test_reports_a_cortex_m4_that_did_not_stop(board):
    terminal = FirmwareTerminal(serial=FakeFirmware(board, commit="0123abc", cortex_m4_stopped=False), timeout=0.5)
    terminal.sync()

    assert terminal.command("firmware.info").values == {"commit": "0123abc", "cm4": "running"}


def test_answers_board_pins_without_aliases(terminal):
    assert terminal.command("board.pins").values == {}


@pytest.mark.parametrize("line, reason", [("delay 10", None), ("delay 600000", None), ("delay 600001", "range"), ("delay x", "usage")])
def test_answers_delay_within_its_range(terminal, line, reason):
    if reason is None:
        assert terminal.command(line).ok
    else:
        with pytest.raises(FirmwareError) as error:
            terminal.command(line)
        assert error.value.reason == reason


@pytest.mark.parametrize("line", ["ping 1", "dev.list qspi", "delay"])
def test_answers_usage_to_what_it_does_not_know(terminal, line):
    with pytest.raises(FirmwareError) as error:
        terminal.command(line)

    assert error.value.reason == "usage"


def test_closes(board):
    firmware = FakeFirmware(board)
    FirmwareTerminal(serial=firmware).close()

    assert firmware.closed


F2_OPERATIONS = ["format", "mount", "open", "list", "read", "overwrite", "create", "append", "write", "rename", "remove", "unmount"]


@pytest.mark.parametrize(
    ("subject", "device"),
    [
        ("embedded-filesystem/fat", "ramblock"),
        ("embedded-filesystem/nor", "ramflash"),
        ("fatfs/tuned", "ramblock"),
        ("littlefs/ram-matched", "ramflash"),
    ],
)
def test_cycles_run_prints_the_ram_device_and_a_record_per_operation_of_f2(terminal, subject, device):
    assert terminal.command(f"cycles.run {subject} n=5").ok

    devices = terminal.drain_events("device")
    cycles = terminal.drain_events("cycles")
    assert [event.values["device"] for event in devices] == [device]
    assert [event.values["op"] for event in cycles] == F2_OPERATIONS
    for event in cycles:
        assert event.values["subject"] == subject
        assert event.values["device"] == device
        assert event.values["n"] == "5"
        assert int(event.values["minCycles"]) <= int(event.values["medianCycles"]) <= int(event.values["maxCycles"])


def test_cycles_run_numbers_its_records_from_the_boot(terminal):
    terminal.command("cycles.run fatfs/tuned")
    terminal.command("cycles.run fatfs/tuned")

    ids = [int(event.values["id"]) for event in terminal.drain_events() if event.kind in ("device", "cycles")]
    assert ids == list(range(1, 27))


def test_cycles_run_describes_the_ram_devices(terminal):
    terminal.command("cycles.run embedded-filesystem/nor")
    terminal.command("cycles.run embedded-filesystem/fat")

    devices = [event.values for event in terminal.drain_events("device")]
    assert {key: value for key, value in devices[0].items() if key != "id"} == {
        "device": "ramflash",
        "kind": "nor",
        "sectors": "1024",
        "sectorSize": "4096",
        "memory": "sdram",
    }
    assert {key: value for key, value in devices[1].items() if key != "id"} == {
        "device": "ramblock",
        "kind": "block",
        "blocks": "4096",
        "blockSize": "512",
        "memory": "sdram",
    }


@pytest.mark.parametrize(
    ("line", "reason"),
    [
        ("cycles.run", "usage"),
        ("cycles.run other", "unsupported"),
        ("cycles.run fatfs/tuned n=0", "range"),
        ("cycles.run fatfs/tuned n=201", "range"),
        ("cycles.run fatfs/tuned m=1", "usage"),
    ],
)
def test_cycles_run_rejects_what_the_firmware_rejects(terminal, line, reason):
    with pytest.raises(FirmwareError) as error:
        terminal.command(line)

    assert error.value.reason == reason


def test_dev_list_names_the_nor_and_block_devices_of_the_board_and_the_ram_devices(terminal):
    assert terminal.command("dev.list").values == {"devices": "qspi,fmc,sd,ramflash,ramblock"}


def test_dev_list_leaves_out_what_the_board_does_not_have(disco):
    assert disco.command("dev.list").values == {"devices": "qspi,emmc,ramflash,ramblock"}


def test_dev_info_describes_a_nor_device(terminal):
    assert terminal.command("dev.info fmc").ok

    (record,) = terminal.drain_events("device")
    assert {key: value for key, value in record.values.items() if key != "id"} == {
        "device": "fmc",
        "kind": "nor",
        "sectors": "128",
        "sectorSize": "131072",
        "pageSize": "64",
        "chip": "0089227e",
        "clockHz": "200000000",
    }


def test_dev_info_describes_a_block_device(terminal):
    assert terminal.command("dev.info sd").ok

    (record,) = terminal.drain_events("device")
    assert {key: value for key, value in record.values.items() if key != "id"} == {
        "device": "sd",
        "kind": "block",
        "blocks": "524288",
        "blockSize": "512",
        "unitBlocks": "8192",
        "chip": "035344534431364780",
        "clockHz": "25000000",
    }


def test_dev_info_describes_the_cache_of_the_emmc(disco):
    assert disco.command("dev.info emmc").ok

    assert disco.drain_events("device")[0].values["cache"] == "on"


def test_dev_info_describes_a_ram_device(terminal):
    assert terminal.command("dev.info ramblock").ok

    assert terminal.drain_events("device")[0].values["memory"] == "sdram"


@pytest.mark.parametrize(("line", "reason"), [("dev.info", "usage"), ("dev.info qspi fmc", "usage"), ("dev.info emmc", "unsupported")])
def test_dev_info_takes_one_device_of_the_board(terminal, line, reason):
    with pytest.raises(FirmwareError) as error:
        terminal.command(line)

    assert error.value.reason == reason


@pytest.mark.parametrize(
    ("line", "op", "size"),
    [
        ("bench.program qspi size=16 n=4", "program", "16"),
        ("bench.erase fmc n=2", "erase", "131072"),
        ("bench.read qspi size=4096 n=8", "read", "4096"),
    ],
)
def test_bench_prints_a_bench_record(terminal, line, op, size):
    assert terminal.command(line).ok

    (record,) = terminal.drain_events("bench")
    values = record.values
    assert (values["op"], values["size"]) == (op, size)
    assert int(values["minNs"]) <= int(values["medianNs"]) <= int(values["p99Ns"]) <= int(values["maxNs"])


@pytest.mark.parametrize(
    ("line", "reason"),
    [
        ("bench.program qspi size=16", "usage"),
        ("bench.program qspi n=1", "usage"),
        ("bench.erase qspi n=1 size=4", "usage"),
        ("bench.read qspi fmc size=4 n=1", "usage"),
        ("bench.read qspi size=x n=1", "usage"),
        ("bench.read qspi size=4 n=0", "range"),
        ("bench.read qspi size=4 n=1025", "range"),
        ("bench.read sd size=4 n=1", "unsupported"),
        ("bench.read ramflash size=4 n=1", "unsupported"),
        ("bench.program qspi size=4097 n=1", "range"),
        ("bench.read qspi size=x n=0", "range"),
        ("bench.read qspi size=0x100 n=1", None),
        ("bench.read qspi size=99999999999 n=1", "usage"),
        ("bench.read qspi size=4 n=1 n=2000", None),
        ("bench.program qspi size=256 n=1024", None),
        ("bench.erase fmc n=129", "range"),
        ("bench.read qspi size=65537 n=1", "range"),
        ("bench.read qspi size=0 n=1", "range"),
    ],
)
def test_bench_checks_its_arguments_as_the_firmware_does(terminal, line, reason):
    if reason is None:
        assert terminal.command(line).ok
        return
    with pytest.raises(FirmwareError) as error:
        terminal.command(line)

    assert error.value.reason == reason


def test_bench_on_a_device_the_board_does_not_have_is_unsupported():
    terminal = FirmwareTerminal(serial=FakeFirmware(load_board("stm32h745i_disco")), timeout=0.5)
    terminal.sync()

    with pytest.raises(FirmwareError) as error:
        terminal.command("bench.erase fmc n=1")

    assert error.value.reason == "unsupported"


@pytest.mark.parametrize(
    ("line", "reason"),
    [("dev.check qspi", None), ("dev.check", "usage"), ("dev.check qspi fmc", "usage"), ("dev.check ramflash", "unsupported")],
)
def test_dev_check_checks_a_nor_device_of_the_board(terminal, line, reason):
    if reason is None:
        assert terminal.command(line).ok
        return
    with pytest.raises(FirmwareError) as error:
        terminal.command(line)

    assert error.value.reason == reason


@pytest.mark.parametrize(
    ("line", "reason"),
    [
        ("fs.format qspi", None),
        ("fs.format sd", None),
        ("fs.format", "usage"),
        ("fs.format qspi fmc", "usage"),
        ("fs.format ramflash", "unsupported"),
        ("fs.format emmc", "unsupported"),
    ],
)
def test_fs_format_formats_a_nor_or_block_device_of_the_board(terminal, line, reason):
    if reason is None:
        assert terminal.command(line).ok
        return
    with pytest.raises(FirmwareError) as error:
        terminal.command(line)

    assert error.value.reason == reason


@pytest.mark.parametrize(
    ("line", "reason"),
    [
        ("wl.run qspi", "usage"),
        ("wl.run qspi L2 other", "usage"),
        ("wl.run qspi L2 n=1", "usage"),
        ("wl.run qspi L2 fill=x", "usage"),
        ("wl.run qspi L2 fill=100", "range"),
        ("wl.run qspi L2 scale=0", "range"),
        ("wl.run qspi L2 scale=1025", "range"),
        ("wl.run qspi L1 fill=10", "range"),
        ("wl.run ramflash L2", "unsupported"),
        ("wl.run qspi L5", "unsupported"),
    ],
)
def test_wl_run_rejects_what_the_firmware_rejects(terminal, line, reason):
    with pytest.raises(FirmwareError) as error:
        terminal.command(line)

    assert error.value.reason == reason


@pytest.mark.parametrize("workload", ["L1", "L2", "L3", "L4", "L6", "L7"])
def test_wl_run_announces_the_workload_and_prints_its_phases(terminal, workload):
    terminal.drain_events("workload")
    terminal.drain_events("phase")

    assert terminal.command(f"wl.run qspi {workload} scale=16").ok

    (start,) = terminal.drain_events("workload")
    assert start.values["workload"] == workload
    assert start.values["scale"] == "16"
    phases = [event.values["phase"] for event in terminal.drain_events("phase")]
    assert phases[0] == f"{workload},prepare"
    assert len(phases) > 1


def test_wl_run_at_a_fill_level_fills_the_volume_and_the_data_logger_prints_its_windows(terminal):
    terminal.drain_events("phase")

    assert terminal.command("wl.run qspi L2 fill=50").ok

    assert [event.values["phase"] for event in terminal.drain_events("phase")] == ["L2,prepare", "fill", "L2", "L2,second,log"]
    assert [event.values["log"] for event in terminal.drain_events("window")] == ["first", "second"]


def test_the_records_of_wl_run_convert_into_the_metrics_of_the_workloads(board, tmp_path):
    firmware = FakeFirmware(board)
    terminal = FirmwareTerminal(serial=firmware, timeout=0.5)
    terminal.sync()
    terminal.command("dev.info qspi")
    for workload in ("L1", "L2 fill=10", "L2 fill=50", "L2 fill=90", "L3 fill=50", "L6", "L7"):
        terminal.command(f"wl.run qspi {workload}")
    kinds = ("device", "workload", "phase", "window", "workload.done")
    events = [event.raw for kind in kinds for event in terminal.drain_events(kind)]
    run = tmp_path / "2026_10_20"
    run.mkdir()
    (run / "transcript.log").write_text("\n".join(events) + "\n", encoding="utf-8")
    (run / "run.json").write_text('{"board": "stm32h757i-eval"}', encoding="utf-8")

    fragment = convert.convert_run(
        run,
        {
            "stm32h757i-eval/qspi": {
                "typical.pageProgramNs": "400000",
                "typical.sectorEraseNs": "45000000",
                "typical.readBytesPerSecond": "50000000",
                "w6BoundMs": "974",
            }
        },
    )

    metrics = {entry["metric"] for entry in fragment["results"]}
    assert metrics == {"W-1", "W-2", "W-3", "W-4", "W-5", "W-6", "R-1", "R-2", "R-3", "R-4", "R-5", "C-1", "C-2"}


@pytest.mark.parametrize(
    ("line", "op", "size", "samples"),
    [
        ("bench.blocks sd read blocks=8 n=4", "read", "4096", "4"),
        ("bench.blocks sd write blocks=128 n=2", "write", "65536", "2"),
        ("bench.units sd units=3 n=4", "units", "512", "12"),
        ("bench.flush sd n=8", "flush", "512", "8"),
        ("bench.discard sd blocks=8192 n=2", "discard", "4194304", "2"),
    ],
)
def test_block_benchmarks_print_a_bench_record(terminal, line, op, size, samples):
    assert terminal.command(line).ok

    (record,) = terminal.drain_events("bench")
    values = record.values
    assert (values["device"], values["op"], values["size"], values["n"]) == ("sd", op, size, samples)
    assert int(values["minNs"]) <= int(values["medianNs"]) <= int(values["p99Ns"]) <= int(values["maxNs"])


def test_the_benchmark_of_allocation_units_names_its_units(terminal):
    assert terminal.command("bench.units sd units=5 n=1").ok

    assert terminal.drain_events("bench")[0].values["units"] == "5"


@pytest.mark.parametrize(
    ("line", "reason"),
    [
        ("bench.blocks sd read blocks=8", "usage"),
        ("bench.blocks sd blocks=8 n=1", "usage"),
        ("bench.blocks sd read n=1", "usage"),
        ("bench.blocks sd read blocks=8 n=1 size=4", "usage"),
        ("bench.blocks sd erase blocks=8 n=1", "usage"),
        ("bench.blocks sd read blocks=x n=1", "usage"),
        ("bench.blocks sd read blocks=8 n=0", "range"),
        ("bench.blocks sd read blocks=8 n=1025", "range"),
        ("bench.blocks sd read blocks=0 n=1", "range"),
        ("bench.blocks sd read blocks=129 n=1", "range"),
        ("bench.blocks sd write blocks=128 n=1024", None),
        ("bench.blocks qspi read blocks=1 n=1", "unsupported"),
        ("bench.blocks emmc read blocks=1 n=1", "unsupported"),
        ("bench.blocks ramblock read blocks=1 n=1", "unsupported"),
        ("bench.units sd n=1", "usage"),
        ("bench.units sd sd units=1 n=1", "usage"),
        ("bench.units sd units=0 n=1", "range"),
        ("bench.units sd units=9 n=1", "range"),
        ("bench.units sd units=8 n=129", "range"),
        ("bench.units sd units=8 n=128", None),
        ("bench.flush sd", "usage"),
        ("bench.flush sd n=1 blocks=1", "usage"),
        ("bench.flush sd n=1025", "range"),
        ("bench.flush fmc n=1", "unsupported"),
        ("bench.discard sd n=1", "usage"),
        ("bench.discard sd blocks=0 n=1", "range"),
        ("bench.discard sd blocks=524289 n=1", "range"),
        ("bench.discard sd blocks=524288 n=1", None),
        ("bench.read sd size=512 n=1", "unsupported"),
        ("dev.check sd", "unsupported"),
    ],
)
def test_block_benchmarks_check_their_arguments_as_the_firmware_does(terminal, line, reason):
    if reason is None:
        assert terminal.command(line).ok
        return
    with pytest.raises(FirmwareError) as error:
        terminal.command(line)

    assert error.value.reason == reason


def test_block_benchmarks_run_on_the_emmc_of_the_disco(disco):
    assert disco.command("bench.flush emmc n=1").ok

    assert disco.drain_events("bench")[0].values["device"] == "emmc"


def test_the_records_of_block_benchmarks_convert_into_the_parameters_of_a_model(terminal, tmp_path):
    lines = [
        "dev.info sd",
        "bench.blocks sd read blocks=1 n=8",
        "bench.blocks sd read blocks=128 n=8",
        "bench.blocks sd write blocks=1 n=8",
    ]
    lines += ["bench.blocks sd write blocks=128 n=8", "bench.flush sd n=8", "bench.discard sd blocks=8192 n=2"]
    lines += [f"bench.units sd units={units} n=8" for units in range(1, 9)]
    transcript = []
    for line in lines:
        assert terminal.command(line).ok
        transcript += [event.raw for kind in ("device", "bench") for event in terminal.drain_events(kind)]
    run = tmp_path / "2026_10_20"
    run.mkdir()
    (run / "run.json").write_text('{"board": "stm32h757i-eval"}', encoding="utf-8")
    (run / "transcript.log").write_text("\n".join(transcript) + "\n", encoding="utf-8")

    parameters = convert.convert_run(run, {})["devices"]["stm32h757i-eval/sd"]

    assert {key: value for key, value in parameters.items() if key.startswith("measured.")} == {
        "measured.blockReadNs": "20000",
        "measured.readCommandNs": "280000",
        "measured.blockWrittenNs": "40000",
        "measured.writeCommandNs": "460000",
        "measured.recentAllocationUnits": "4",
        "measured.allocationUnitOpeningNs": "8000000",
        "measured.flushNs": "1200000",
        "measured.discardNs": "3000000",
    }


def test_wl_run_on_a_block_device_runs_the_fat_subject_and_prints_the_counts_of_blocks(terminal):
    terminal.drain_events("workload")
    terminal.drain_events("phase")

    assert terminal.command("wl.run sd L1 scale=16").ok

    (workload,) = terminal.drain_events("workload")
    assert workload.values["subject"] == "embedded-filesystem/fat"
    phases = [event.values for event in terminal.drain_events("phase") if "phase" in event.values]
    assert all(phase["subject"] == "embedded-filesystem/fat" for phase in phases)
    assert all("blocksWritten" in phase and "programNs" not in phase for phase in phases)


def test_wl_run_of_the_data_logger_on_a_block_device_prints_windows_of_appends_without_volume_counts(disco):
    disco.drain_events("window")

    assert disco.command("wl.run emmc L2 fill=50").ok

    windows = [event.values for event in disco.drain_events("window") if "log" in event.values]
    assert [(window["log"], window["appends"]) for window in windows] == [("first", "64"), ("second", "64")]
    assert all("checkpoints" not in window for window in windows)


def test_the_records_of_wl_run_on_a_block_device_convert_into_the_metrics_of_the_workloads(board, tmp_path):
    terminal = FirmwareTerminal(serial=FakeFirmware(board), timeout=0.5)
    terminal.sync()
    terminal.command("dev.info sd")
    for workload in ("L1", "L2 fill=10", "L2 fill=50", "L2 fill=90", "L3 fill=50", "L4 fill=50", "L6", "L7"):
        terminal.command(f"wl.run sd {workload}")
    kinds = ("device", "workload", "phase", "window", "workload.done")
    events = [event.raw for kind in kinds for event in terminal.drain_events(kind)]
    run = tmp_path / "2026_10_20"
    run.mkdir()
    (run / "transcript.log").write_text("\n".join(events) + "\n", encoding="utf-8")
    (run / "run.json").write_text('{"board": "stm32h757i-eval"}', encoding="utf-8")
    model = {
        "model": "sd-card-256m",
        "blocksPerAllocationUnit": "8192",
        "recentAllocationUnits": "2",
        "typical.commandNs": "100000",
        "typical.blockReadNs": "25000",
        "typical.blockWrittenNs": "50000",
        "typical.writeBusyNs": "0",
        "typical.allocationUnitOpeningNs": "10000000",
        "w6BoundMs": "1021.025",
    }

    fragment = convert.convert_run(run, {"stm32h757i-eval/sd": model})

    metrics = {entry["metric"] for entry in fragment["results"]}
    expected = {"W-1", "W-2", "W-3", "W-4", "W-5", "W-6", "R-1", "R-2", "R-3", "R-4", "R-5", "M-1", "M-2", "M-3", "C-1", "C-2"}
    assert metrics == expected
    assert all(entry["subject"] == "embedded-filesystem/fat" for entry in fragment["results"])
