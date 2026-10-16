from __future__ import annotations

import io
import json
import subprocess

import pytest

from emfs_validation.board import load_board
from emfs_validation.fake_firmware import FakeFirmware
from emfs_validation.recording import RUN, RecordingTerminal, source_commit, write_run


def recording():
    transcript = io.StringIO()
    terminal = RecordingTerminal(transcript, serial=FakeFirmware(load_board("stm32h745i_disco")), timeout=0.5)
    return terminal, transcript


def test_records_what_it_receives_and_sends_in_order():
    terminal, transcript = recording()

    terminal.wait_boot(0.5)
    terminal.command("ping")

    assert transcript.getvalue().splitlines() == [
        "EVT boot board=stm32h745i-disco family=stm32h7 sysclk=400000000 reset=pin",
        "> ping",
        "OK",
    ]


def test_records_a_command_without_a_final_line():
    terminal, transcript = recording()
    terminal.wait_boot(0.5)

    terminal.send_nowait("reset")
    terminal.wait_boot(0.5)

    assert transcript.getvalue().splitlines()[1:] == ["> reset", "EVT boot board=stm32h745i-disco family=stm32h7 sysclk=400000000 reset=sw"]


def test_does_not_record_a_command_it_refuses():
    terminal, transcript = recording()
    terminal.wait_boot(0.5)

    with pytest.raises(ValueError):
        terminal.command("bad\tline")

    assert "> bad" not in transcript.getvalue()


def test_names_the_commit_of_a_checkout(tmp_path):
    subprocess.run(["git", "init", "-q", str(tmp_path)], check=True)
    subprocess.run(
        ["git", "-C", str(tmp_path), "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "--allow-empty", "-m", "c"], check=True
    )
    head = subprocess.run(["git", "-C", str(tmp_path), "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()

    assert source_commit(tmp_path) == head
    (tmp_path / "file").write_text("x")
    assert source_commit(tmp_path) == head + "-dirty"


def test_names_no_commit_outside_a_checkout(tmp_path):
    assert source_commit(tmp_path) == "unknown"


def test_writes_the_run(tmp_path):
    path = write_run(tmp_path, {"board": "stm32h757i-eval", "exitstatus": 0})

    assert path == tmp_path / RUN
    assert json.loads(path.read_text()) == {"board": "stm32h757i-eval", "exitstatus": 0}
