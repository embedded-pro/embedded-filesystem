from __future__ import annotations

import pytest
from ad3_waveforms_bench.terminal import FirmwareError, FirmwareTerminal

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


def test_dev_list_names_the_nor_devices_of_the_board_and_the_ram_devices(terminal):
    assert terminal.command("dev.list").values == {"devices": "qspi,fmc,ramflash,ramblock"}


def test_dev_list_leaves_out_what_the_board_does_not_have():
    terminal = FirmwareTerminal(serial=FakeFirmware(load_board("stm32h745i_disco")), timeout=0.5)
    terminal.sync()

    assert terminal.command("dev.list").values == {"devices": "qspi,ramflash,ramblock"}


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


def test_dev_info_describes_a_ram_device(terminal):
    assert terminal.command("dev.info ramblock").ok

    assert terminal.drain_events("device")[0].values["memory"] == "sdram"


@pytest.mark.parametrize(("line", "reason"), [("dev.info", "usage"), ("dev.info qspi fmc", "usage"), ("dev.info sd", "unsupported")])
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
        ("bench.program fmc size=1 n=263000", "range"),
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
