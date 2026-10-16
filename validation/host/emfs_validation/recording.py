"""A run on a board: the transcript of its terminal and `run.json`, which the conversion reads (docs/Validation.md)."""

from __future__ import annotations

import json
import subprocess
from pathlib import Path
from typing import Any, TextIO

from ad3_waveforms_bench.terminal import FirmwareTerminal, PendingCommand

from emfs_validation.convert import RUN, TRANSCRIPT

__all__ = ["JUNIT", "RUN", "TRANSCRIPT", "RecordingTerminal", "source_commit", "write_run"]

JUNIT = "junit.xml"


class RecordingTerminal(FirmwareTerminal):
    """Writes every line it sends, as `> <line>`, and every line it receives except the echo of a command to the transcript."""

    def __init__(self, transcript: TextIO, **kwargs: Any) -> None:
        super().__init__(**kwargs)
        self._transcript = transcript
        self._echo: str | None = None

    def begin(self, line: str, timeout: float | None = None) -> PendingCommand:
        pending = super().begin(line, timeout)
        self._sent(line)
        return pending

    def send_nowait(self, line: str) -> None:
        self.pump()
        super().send_nowait(line)
        self._sent(line)

    def _dispatch(self, line: str) -> None:
        if line == self._echo:
            self._echo = None
        else:
            self._record(line)
        super()._dispatch(line)

    def _sent(self, line: str) -> None:
        self._echo = line
        self._record(f"> {line}")

    def _record(self, line: str) -> None:
        self._transcript.write(line + "\n")
        self._transcript.flush()


def source_commit(directory: Path) -> str:
    """The commit of the checkout the run comes from, with `-dirty` when it has local changes."""
    try:
        commit = subprocess.run(
            ["git", "-C", str(directory), "rev-parse", "HEAD"], capture_output=True, text=True, check=True
        ).stdout.strip()
        status = subprocess.run(["git", "-C", str(directory), "status", "--porcelain"], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return "unknown"
    return commit + ("-dirty" if status.strip() else "")


def write_run(directory: Path, run: dict[str, Any]) -> Path:
    path = directory / RUN
    path.write_text(json.dumps(run, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path
