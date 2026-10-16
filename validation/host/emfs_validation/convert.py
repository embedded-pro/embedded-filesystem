"""Turns the transcript of a run on a board into a report fragment for merge_report.py (docs/Validation.md).

Only the standard library is imported, so that the metrics build runs `python3 -m emfs_validation.convert`
with `validation/host` on PYTHONPATH and without installing the package.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import re
import sys
from collections.abc import Callable, Iterable
from pathlib import Path
from typing import Any

TRANSCRIPT = "transcript.log"
RUN = "run.json"
RESULT_KEYS = ("metric", "workload", "subject", "device", "unit", "value", "ramBytes", "better")
RUN_NAME = re.compile(r"^(\d{4})_(\d{2})_(\d{2})(?:_(\d+))?$")


class ConversionError(Exception):
    """A run cannot be converted."""


@dataclasses.dataclass
class Record:
    """A result of the firmware: the `EVT <kind> id=<id>` lines of one record, joined."""

    kind: str
    id: int
    fields: dict[str, str] = dataclasses.field(default_factory=dict)

    def __str__(self) -> str:
        return f"{self.kind} id={self.id}"

    def field(self, key: str) -> str:
        if key not in self.fields:
            raise ConversionError(f"{self} has no '{key}'")
        return self.fields[key]

    def number(self, key: str) -> int:
        try:
            return int(self.field(key))
        except ValueError:
            raise ConversionError(f"'{key}' of {self} is not a number") from None

    def values(self, key: str) -> list[int]:
        text = self.field(key)
        try:
            return [int(value) for value in text.split(",")] if text else []
        except ValueError:
            raise ConversionError(f"'{key}' of {self} is not a list of numbers") from None


def parse_pairs(tokens: Iterable[str], line_number: int) -> list[tuple[str, str]]:
    pairs = []
    for token in tokens:
        key, separator, value = token.partition("=")
        if not separator or not key:
            raise ConversionError(f"line {line_number}: '{token}' is not a key=value pair")
        pairs.append((key, value))
    return pairs


def parse_transcript(lines: Iterable[str]) -> list[Record]:
    """The records of a transcript; a record continues on the next received line with the same kind and id."""
    records: list[Record] = []
    current: Record | None = None
    for line_number, line in enumerate(lines, start=1):
        if line.startswith("> "):
            continue
        tokens = line.split()
        if len(tokens) < 3 or tokens[0] != "EVT" or not tokens[2].startswith("id="):
            current = None
            continue
        pairs = parse_pairs(tokens[2:], line_number)
        try:
            record_id = int(pairs[0][1])
        except ValueError:
            raise ConversionError(f"line {line_number}: id '{pairs[0][1]}' is not a number") from None
        if current is None or current.kind != tokens[1] or current.id != record_id:
            current = Record(tokens[1], record_id)
            records.append(current)
        for key, value in pairs[1:]:
            current.fields[key] = current.fields[key] + "," + value if key in current.fields else value
    return records


class Fragment:
    """The devices and results of the run of one board, with the board in the name of every device."""

    def __init__(self, board: str, run: str, models: dict[str, dict[str, str]]) -> None:
        self.board = board
        self.run = run
        self.models = models
        self.devices: dict[str, dict[str, str]] = {}
        self.results: list[dict[str, Any]] = []

    def device(self, local: str, parameters: dict[str, str]) -> str:
        """Adds a device of the board, with the run and the parameters of the model the run is converted with."""
        name = f"{self.board}/{local}"
        model = self.models.get(name, {})
        complete = {**parameters, "run": self.run}
        complete.update({"model" if key == "model" else f"model.{key}": str(value) for key, value in model.items()})
        if name in self.devices and self.devices[name] != complete:
            raise ConversionError(f"device '{local}' is reported with different parameters")
        self.devices[name] = complete
        return name

    def result(self, entry: dict[str, Any]) -> None:
        for key in RESULT_KEYS:
            if key not in entry:
                raise ConversionError(f"a result of {entry.get('metric')} has no '{key}'")
        name = f"{self.board}/{entry['device']}"
        if name not in self.devices:
            raise ConversionError(f"a result of {entry['metric']} is on an unknown device '{entry['device']}'")
        self.results.append({**entry, "device": name})

    def json(self) -> dict[str, Any]:
        return {"devices": self.devices, "results": self.results}


def convert_device(fragment: Fragment, record: Record) -> None:
    for key in record.fields:
        if key in ("run", "model") or key.startswith("model."):
            raise ConversionError(f"{record} has '{key}', which the conversion adds")
    parameters = {key: value for key, value in record.fields.items() if key != "device"}
    fragment.device(record.field("device"), parameters)


CONVERTERS: dict[str, Callable[[Fragment, Record], None]] = {
    "device": convert_device,
}


def read_run(directory: Path) -> dict[str, Any]:
    try:
        run = json.loads((directory / RUN).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ConversionError(f"{directory}: cannot read {RUN}: {error}") from None
    if not isinstance(run, dict):
        raise ConversionError(f"{directory}: {RUN} is not an object")
    return run


def convert_run(directory: Path, models: dict[str, dict[str, str]]) -> dict[str, Any]:
    board = read_run(directory).get("board")
    if not board:
        raise ConversionError(f"{directory}: {RUN} names no board")
    try:
        lines = (directory / TRANSCRIPT).read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ConversionError(f"{directory}: cannot read {TRANSCRIPT}: {error}") from None
    fragment = Fragment(board, directory.name, models)
    try:
        for record in parse_transcript(lines):
            converter = CONVERTERS.get(record.kind)
            if converter is not None:
                converter(fragment, record)
    except ConversionError as error:
        raise ConversionError(f"{directory}: {error}") from None
    return fragment.json()


def run_order(run: Path) -> tuple[int, ...]:
    match = RUN_NAME.match(run.name)
    assert match is not None
    return tuple(int(part or 1) for part in match.groups())


def latest_runs(results: Path) -> list[Path]:
    """The latest run of every board: `<results>/<board>/<run>`, where a run is named by its date, and a later run of a day by a number."""
    if not results.is_dir():
        raise ConversionError(f"no directory {results}")
    latest = []
    for board in sorted(path for path in results.iterdir() if path.is_dir()):
        runs = [path for path in board.iterdir() if path.is_dir() and RUN_NAME.match(path.name)]
        if runs:
            latest.append(max(runs, key=run_order))
    return latest


def load_models(path: Path | None) -> dict[str, dict[str, str]]:
    if path is None:
        return {}
    try:
        models = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ConversionError(f"cannot read the models {path}: {error}") from None
    if not isinstance(models, dict) or not all(isinstance(parameters, dict) for parameters in models.values()):
        raise ConversionError(f"the models {path} are not an object of parameters by device")
    return models


def parse_arguments(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python3 -m emfs_validation.convert", description=__doc__.splitlines()[0])
    runs = parser.add_mutually_exclusive_group(required=True)
    runs.add_argument("--results", type=Path, help="committed runs, <results>/<board>/<run>; the latest run of every board is converted")
    runs.add_argument("--run", type=Path, action="append", help="the --results directory of a run on a board")
    parser.add_argument("--models", type=Path, help="parameters of the model of every device, by <board>/<device>")
    parser.add_argument("--output-directory", type=Path, required=True, help="directory for the fragments; earlier ones are removed")
    return parser.parse_args(argv)


def fragments(arguments: argparse.Namespace) -> list[tuple[str, Path]]:
    if arguments.results is not None:
        return [(run.parent.name, run) for run in latest_runs(arguments.results)]
    names: dict[str, Path] = {}
    for run in arguments.run:
        if run.name in names:
            raise ConversionError(f"{names[run.name]} and {run} both write {run.name}.json")
        names[run.name] = run
    return list(names.items())


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    output = arguments.output_directory
    output.mkdir(parents=True, exist_ok=True)
    for stale in output.glob("*.json"):
        stale.unlink()
    try:
        models = load_models(arguments.models)
        for name, run in fragments(arguments):
            fragment = convert_run(run, models)
            (output / f"{name}.json").write_text(json.dumps(fragment, indent=2) + "\n", encoding="utf-8")
            print(f"{run.parent.name}/{run.name}: {len(fragment['devices'])} devices, {len(fragment['results'])} results")
    except ConversionError as error:
        print(f"convert: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
