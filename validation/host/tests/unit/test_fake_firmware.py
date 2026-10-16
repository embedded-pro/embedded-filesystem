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


@pytest.mark.parametrize("line", ["ping 1", "dev.list", "delay"])
def test_answers_usage_to_what_it_does_not_know(terminal, line):
    with pytest.raises(FirmwareError) as error:
        terminal.command(line)

    assert error.value.reason == "usage"


def test_closes(board):
    firmware = FakeFirmware(board)
    FirmwareTerminal(serial=firmware).close()

    assert firmware.closed
