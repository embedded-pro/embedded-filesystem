"""Shared pytest plumbing: the board file, the terminal of the board and the results of a run.

The scenarios in `tests/hil` run against a board with `--port`, or against `emfs_validation.fake_firmware` with
`--fake`, and skip otherwise. With `--results <directory>`, a run writes there the transcript of the terminal,
`run.json` and the JUnit report, which is what a run sends back (docs/Validation.md).

`--no-ad3` and `--fake` come from the pytest plugin of ad3-waveforms-bench.
"""

from __future__ import annotations

import datetime
import io
import os
from collections.abc import Iterator
from pathlib import Path
from typing import TextIO

import pytest

from emfs_validation.board import Board, load_board
from emfs_validation.fake_firmware import FakeFirmware
from emfs_validation.recording import JUNIT, RUN, TRANSCRIPT, RecordingTerminal, source_commit, write_run

HIL_DIR = Path(__file__).parent / "hil"
_BOARD_KEY = pytest.StashKey[Board]()
_STARTED_KEY = pytest.StashKey[str]()
_FIRMWARE_KEY = pytest.StashKey[dict[str, str]]()


def pytest_addoption(parser: pytest.Parser) -> None:
    group = parser.getgroup("embedded-filesystem validation")
    group.addoption("--board", default=os.environ.get("EMFS_BOARD", "stm32h757i_eval"), help="board YAML name or path")
    group.addoption(
        "--port", default=os.environ.get("EMFS_PORT"), help="serial port of the terminal; the scenarios skip without it or --fake"
    )
    group.addoption("--baud", type=int, default=None, help="baud rate of the terminal (default from the board file)")
    group.addoption(
        "--command-timeout", type=float, default=None, help="seconds to wait for the answer of a command (default from the board file)"
    )
    group.addoption("--results", default=os.environ.get("EMFS_RESULTS"), help="directory for the transcript, run.json and the JUnit report")


def board_config(config: pytest.Config) -> Board:
    if _BOARD_KEY not in config.stash:
        config.stash[_BOARD_KEY] = load_board(config.getoption("--board"))
    return config.stash[_BOARD_KEY]


def results_directory(config: pytest.Config) -> Path | None:
    results = config.getoption("--results")
    return Path(results) if results else None


def pytest_configure(config: pytest.Config) -> None:
    config.stash[_STARTED_KEY] = datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")
    directory = results_directory(config)
    if directory is not None:
        if any((directory / name).exists() for name in (TRANSCRIPT, RUN, JUNIT)):
            raise pytest.UsageError(f"--results {directory} already holds a run; give every run a directory of its own")
        directory.mkdir(parents=True, exist_ok=True)
        if not config.option.xmlpath:
            config.option.xmlpath = str(directory / JUNIT)


def pytest_collection_modifyitems(config: pytest.Config, items: list[pytest.Item]) -> None:
    runs = bool(config.getoption("--port")) or config.getoption("--fake")
    skip = pytest.mark.skip(reason="needs --port or --fake")
    for item in items:
        if HIL_DIR in item.path.parents:
            item.add_marker(pytest.mark.hil)
            if not runs:
                item.add_marker(skip)


def pytest_sessionfinish(session: pytest.Session, exitstatus: int) -> None:
    config = session.config
    directory = results_directory(config)
    if directory is None:
        return
    board = board_config(config)
    write_run(
        directory,
        {
            "board": board.name,
            "port": "fake" if config.getoption("--fake") else config.getoption("--port"),
            "host_commit": source_commit(Path(__file__).parent),
            "firmware": config.stash.get(_FIRMWARE_KEY, {}),
            "options": {
                "board_file": config.getoption("--board"),
                "baud": config.getoption("--baud") or board.baud,
                "command_timeout": config.getoption("--command-timeout") or board.command_timeout,
                "scale": board.scale,
                "keyword": config.getoption("keyword"),
                "markers": config.getoption("markexpr"),
            },
            "started": config.stash[_STARTED_KEY],
            "finished": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
            "exitstatus": int(exitstatus),
        },
    )


@pytest.fixture(scope="session")
def board(pytestconfig: pytest.Config) -> Board:
    return board_config(pytestconfig)


@pytest.fixture(scope="session")
def transcript(pytestconfig: pytest.Config) -> Iterator[TextIO]:
    directory = results_directory(pytestconfig)
    if directory is None:
        yield io.StringIO()
    else:
        with (directory / TRANSCRIPT).open("a", encoding="utf-8") as file:
            yield file


@pytest.fixture(scope="session")
def terminal(pytestconfig: pytest.Config, board: Board, transcript: TextIO) -> Iterator[RecordingTerminal]:
    timeout = pytestconfig.getoption("--command-timeout") or board.command_timeout
    if pytestconfig.getoption("--fake"):
        device = RecordingTerminal(transcript, serial=FakeFirmware(board), timeout=timeout)
    else:
        port = pytestconfig.getoption("--port")
        device = RecordingTerminal(transcript, port=port, baud=pytestconfig.getoption("--baud") or board.baud, timeout=timeout)
    try:
        device.sync()
    except Exception as error:
        device.close()
        pytest.exit(f"the board does not answer ping: {error}", returncode=3)
    pytestconfig.stash[_FIRMWARE_KEY] = dict(device.command("firmware.info").values)
    yield device
    device.close()
