from __future__ import annotations

import pytest

from emfs_validation.board import BOARDS_DIR, BoardError, board_path, load_board, parse_board

VALID = {
    "board": "stm32h757i-eval",
    "family": "stm32h7",
    "sysclk": 400000000,
    "baud": 115200,
    "command_timeout": 2.0,
    "scale": 16,
    "devices": ["qspi"],
}


@pytest.mark.parametrize("name, firmware_name", [("stm32h757i_eval", "stm32h757i-eval"), ("stm32h745i_disco", "stm32h745i-disco")])
def test_every_board_file_loads(name, firmware_name):
    board = load_board(name)

    assert board.name == firmware_name
    assert board.family == "stm32h7"
    assert board.sysclk == 400000000
    assert board.baud == 115200
    assert board.scale == 16


def test_a_name_is_a_file_of_the_boards_directory_and_a_path_is_itself(tmp_path):
    assert board_path("stm32h757i_eval") == BOARDS_DIR / "stm32h757i_eval.yaml"
    assert board_path(str(tmp_path / "custom.yaml")) == tmp_path / "custom.yaml"


def test_parse_takes_every_field():
    board = parse_board(VALID, "test")

    assert board.devices == ("qspi",)
    assert board.command_timeout == 2.0


@pytest.mark.parametrize(
    "data",
    [
        [],
        {key: value for key, value in VALID.items() if key != "sysclk"},
        {**VALID, "unknown": 1},
        {**VALID, "devices": "qspi"},
    ],
)
def test_parse_rejects_a_malformed_board(data):
    with pytest.raises(BoardError):
        parse_board(data, "test")


def test_load_rejects_a_missing_file(tmp_path):
    with pytest.raises(BoardError):
        load_board(str(tmp_path / "missing.yaml"))
