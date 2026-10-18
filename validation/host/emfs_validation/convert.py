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

from emfs_validation.workloads import DeviceRuns, Totals, WorkloadRun, workload_results

TRANSCRIPT = "transcript.log"
RUN = "run.json"
RESULT_KEYS = ("metric", "workload", "subject", "device", "unit", "value", "ramBytes", "better")
RESULT_IDENTITY = ("metric", "workload", "subject", "device", "fill")
MEASURED_PREFIXES = ("bench.", "measured.")
BENCH_STATISTICS = ("minNs", "medianNs", "p99Ns", "maxNs", "meanNs")
RUN_NAME = re.compile(r"^(\d{4})_(\d{2})_(\d{2})(?:_(\d+))?$")
# A write into one more allocation unit in turn that takes more than this many times the write into one opens a unit
UNIT_OPENING_FACTOR = 2


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
        self.workloads: dict[tuple[str, str], dict[tuple[str, int], WorkloadRun]] = {}

    def device(self, local: str, parameters: dict[str, str]) -> str:
        """Adds a device of the board, with the run and the parameters of the model the run is converted with."""
        name = f"{self.board}/{local}"
        model = self.models.get(name, {})
        complete = {**parameters, "run": self.run}
        complete.update({"model" if key == "model" else f"model.{key}": str(value) for key, value in model.items()})
        if name in self.devices:
            if self.reported(name) != complete:
                raise ConversionError(f"device '{local}' is reported with different parameters")
        else:
            self.devices[name] = complete
        return name

    def reported(self, name: str) -> dict[str, str]:
        """The parameters of a device as its device record and the conversion give them, without what its benchmarks measured."""
        return {key: value for key, value in self.devices[name].items() if not key.startswith(MEASURED_PREFIXES)}

    def measured(self, local: str) -> dict[str, str]:
        """The parameters of a device of the board that its benchmarks add to."""
        name = f"{self.board}/{local}"
        if name not in self.devices:
            raise ConversionError(f"a benchmark is on an unknown device '{local}'")
        return self.devices[name]

    def result(self, entry: dict[str, Any]) -> None:
        """Adds a result, in place of an earlier one of the same metric, workload, subject and device."""
        for key in RESULT_KEYS:
            if key not in entry:
                raise ConversionError(f"a result of {entry.get('metric')} has no '{key}'")
        name = f"{self.board}/{entry['device']}"
        if name not in self.devices:
            raise ConversionError(f"a result of {entry['metric']} is on an unknown device '{entry['device']}'")
        entry = {**entry, "device": name}
        self.discard(lambda earlier: all(earlier.get(key) == entry.get(key) for key in RESULT_IDENTITY))
        self.results.append(entry)

    def workload_run(self, record: Record, restart: bool = False) -> WorkloadRun:
        """The run of the workload of a record; a `workload` record restarts it, so that the last run replaces earlier ones."""
        runs = self.workloads.setdefault((record.field("device"), record.field("subject")), {})
        key = (record.field("workload"), record.number("fill"))
        if restart or key not in runs:
            runs[key] = WorkloadRun()
        return runs[key]

    def workload_results(self) -> None:
        """The metrics of the workloads, once every record of the run is read."""
        for (device, subject), runs in self.workloads.items():
            model = self.models.get(f"{self.board}/{device}", {})
            for entry in workload_results(DeviceRuns(device, subject, runs, model)):
                self.result(entry)

    def discard(self, matches: Callable[[dict[str, Any]], bool]) -> None:
        self.results = [result for result in self.results if not matches(result)]

    def json(self) -> dict[str, Any]:
        return {"devices": self.devices, "results": self.results}


def convert_device(fragment: Fragment, record: Record) -> None:
    for key in record.fields:
        if key in ("run", "model") or key.startswith(("model.", *MEASURED_PREFIXES)):
            raise ConversionError(f"{record} has '{key}', which the conversion adds")
    parameters = {key: value for key, value in record.fields.items() if key != "device"}
    fragment.device(record.field("device"), parameters)


def convert_cycles(fragment: Fragment, record: Record) -> None:
    """F-2: the median cycles of an operation until its completion, and until the volume is idle again (`<operation> settled`)."""
    common = {
        "metric": "F-2",
        "subject": record.field("subject"),
        "device": record.field("device"),
        "unit": "cycles",
        "ramBytes": record.number("ramBytes"),
        "better": "lower",
    }
    operation = record.field("op")
    fragment.result({**common, "workload": operation, "value": record.number("medianCycles")})
    fragment.result({**common, "workload": f"{operation} settled", "value": record.number("settledCycles")})


def convert_bench(fragment: Fragment, record: Record) -> None:
    """A benchmark: its statistics among the parameters of the device, as `bench.<op>.<size>.<statistic>`, or
    `bench.units.<units>.<statistic>` for the allocation units written in turn, and the parameters a model takes from the
    benchmarks of the device so far, as `measured.<parameter>`."""
    parameters = fragment.measured(record.field("device"))
    operation = record.field("op")
    prefix = f"bench.{operation}.{record.number('units' if operation == 'units' else 'size')}"
    parameters[f"{prefix}.n"] = str(record.number("n"))
    for statistic in BENCH_STATISTICS:
        parameters[f"{prefix}.{statistic}"] = str(record.number(statistic))
    parameters.update(model_parameters(parameters))


def model_parameters(parameters: dict[str, str]) -> dict[str, str]:
    medians = {
        key[: -len(".medianNs")]: int(value) for key, value in parameters.items() if key.startswith("bench.") and key.endswith(".medianNs")
    }
    if parameters.get("kind") == "block":
        return block_model_parameters(parameters, medians)
    return nor_model_parameters(parameters, medians)


def bench_medians(medians: dict[str, int], operation: str) -> list[tuple[int, int]]:
    """The medians of the benchmarks of an operation, by their size, or by their units, smallest first."""
    return sorted((int(key.split(".")[2]), median) for key, median in medians.items() if key.startswith(f"bench.{operation}."))


def nor_model_parameters(parameters: dict[str, str], medians: dict[str, int]) -> dict[str, str]:
    """The parameters of a NOR model from the medians of the benchmarks: a program of a page, an erase of a sector, and
    the rate and the fixed cost of a read, from the smallest and the largest read."""
    model: dict[str, str] = {}
    page = f"bench.program.{parameters.get('pageSize')}"
    if page in medians:
        model["measured.pageProgramNs"] = str(medians[page])
    sector = f"bench.erase.{parameters.get('sectorSize')}"
    if sector in medians:
        model["measured.sectorEraseNs"] = str(medians[sector])
    reads = bench_medians(medians, "read")
    if len(reads) >= 2 and reads[-1][1] > reads[0][1]:
        (smallest, smallest_ns), (largest, largest_ns) = reads[0], reads[-1]
        rate = (largest - smallest) * 1_000_000_000 // (largest_ns - smallest_ns)
        if rate > 0:
            model["measured.readBytesPerSecond"] = str(rate)
            model["measured.readFixedNs"] = str(max(0, smallest_ns - smallest * 1_000_000_000 // rate))
    return model


def block_model_parameters(parameters: dict[str, str], medians: dict[str, int]) -> dict[str, str]:
    """The parameters of a block device model from the medians of the benchmarks: the command and the time per block of a
    read and of a write, from the fewest and the most blocks; the allocation units the device keeps open and the opening of
    one, from the writes into units in turn; and a flush, and a discard of one allocation unit."""
    model: dict[str, str] = {}
    block_size = int(parameters.get("blockSize", "512"))
    for operation, per_block in (("read", "blockRead"), ("write", "blockWritten")):
        requests = [(size // block_size, median) for size, median in bench_medians(medians, operation)]
        if len(requests) >= 2 and requests[-1][1] > requests[0][1]:
            (fewest, fewest_ns), (most, most_ns) = requests[0], requests[-1]
            block_ns = (most_ns - fewest_ns) // (most - fewest)
            model[f"measured.{per_block}Ns"] = str(block_ns)
            model[f"measured.{operation}CommandNs"] = str(max(0, fewest_ns - fewest * block_ns))
    model.update(opening_parameters(dict(bench_medians(medians, "units"))))
    if f"bench.flush.{block_size}" in medians:
        model["measured.flushNs"] = str(medians[f"bench.flush.{block_size}"])
    discard = f"bench.discard.{int(parameters.get('unitBlocks', '0')) * block_size}"
    if discard in medians:
        model["measured.discardNs"] = str(medians[discard])
    return model


def opening_parameters(units: dict[int, int]) -> dict[str, str]:
    """The allocation units that stay open are those below the first number of units written in turn whose write is
    slower than UNIT_OPENING_FACTOR writes into one unit, and that write opens a unit."""
    if 1 not in units:
        return {}
    opening = next((count for count in sorted(units) if units[count] > UNIT_OPENING_FACTOR * units[1]), None)
    if opening is None:
        return {}
    return {
        "measured.recentAllocationUnits": str(max(count for count in units if count < opening)),
        "measured.allocationUnitOpeningNs": str(units[opening] - units[1]),
    }


def convert_cycles_failed(fragment: Fragment, record: Record) -> None:
    """A run of F-2 that failed leaves no F-2 for its subject, not even for the operations it completed."""
    subject = record.field("subject")
    fragment.discard(lambda result: result["metric"] == "F-2" and result["subject"] == subject)


def convert_workload(fragment: Fragment, record: Record) -> None:
    """The start of a run of a workload, which replaces an earlier run of the workload at its fill level on its device."""
    fragment.workload_run(record, restart=True).ram_bytes = record.number("ramBytes")


def convert_phase(fragment: Fragment, record: Record) -> None:
    """A phase of a workload, named as on the host; phases of a run with the same name are added up."""
    run = fragment.workload_run(record)
    run.ram_bytes = record.number("ramBytes")
    run.add_phase(record.field("phase").replace(",", " "), Totals.parse(record.fields))


def convert_window(fragment: Fragment, record: Record) -> None:
    fragment.workload_run(record).windows[record.field("log")] = Totals.parse(record.fields)


def convert_workload_failed(fragment: Fragment, record: Record) -> None:
    """A run of a workload with a call that failed, or that timed out, leaves no results."""
    fragment.workload_run(record).failed = True


def convert_workload_done(fragment: Fragment, record: Record) -> None:
    """The end of a run of a workload; a run without one, cut off by a reset or by the end of the transcript, leaves no results."""
    fragment.workload_run(record).done = True


CONVERTERS: dict[str, Callable[[Fragment, Record], None]] = {
    "device": convert_device,
    "bench": convert_bench,
    "cycles": convert_cycles,
    "cycles.failed": convert_cycles_failed,
    "workload": convert_workload,
    "phase": convert_phase,
    "window": convert_window,
    "workload.failed": convert_workload_failed,
    "workload.done": convert_workload_done,
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
        fragment.workload_results()
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
