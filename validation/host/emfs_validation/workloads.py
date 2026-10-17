"""The metrics of the workloads on a board (docs/Validation.md): W-1 to W-6, R-1 to R-5, C-1 and C-2.

The firmware totals the calls of every phase instead of keeping a record per call, so the metrics are computed from the
totals of the phases as metrics/suite/Measurements.cpp computes them from the records of the calls on the host. Only the
standard library is imported, as in convert.py.
"""

from __future__ import annotations

import dataclasses
import math
import re
from collections.abc import Iterable, Iterator
from typing import Any

OPERATION_STATISTICS = ("calls", "userBytes", "latencyNs", "maxNs", "histogram")
ATTRIBUTES = {"calls": "calls", "userBytes": "user_bytes", "latencyNs": "latency_ns", "maxNs": "max_ns"}
COUNTS = (
    "elapsedNs",
    "readRequests",
    "bytesRead",
    "readNs",
    "programRequests",
    "bytesProgrammed",
    "pagesProgrammed",
    "programNs",
    "eraseRequests",
    "sectorsErased",
    "eraseNs",
    "allocatedSectors",
    "erasedBlocks",
    "checkpoints",
    "movedBlocks",
)
LATENCY_FILL = 50
DATA_LOGGER_PHASES = ("L2", "L2 second log")
OVERWRITE_PHASE = re.compile(r"^L3 (\d+)$")
RANDOM_READ_PHASE = re.compile(r"^L6 random (\d+)$")
LOOKUP_PHASE = re.compile(r"^L6 lookup (\d+)$")
MOUNT_PHASE = re.compile(r"^L7 (\d+)$")
SEQUENTIAL_WRITE_PHASE = re.compile(r"^L1 \d+ \d+$")


def bucket_upper_bound(bucket: int) -> int:
    """The largest duration in nanoseconds of a bucket of validation::LatencyHistogram: four buckets per octave."""
    if bucket < 4:
        return bucket
    octave = bucket // 4 + 1
    return ((5 + bucket % 4) << (octave - 2)) - 1


@dataclasses.dataclass
class Operation:
    """The calls of one operation in a phase."""

    calls: int = 0
    user_bytes: int = 0
    latency_ns: int = 0
    max_ns: int = 0
    histogram: dict[int, int] = dataclasses.field(default_factory=dict)

    def merge(self, other: Operation) -> None:
        self.calls += other.calls
        self.user_bytes += other.user_bytes
        self.latency_ns += other.latency_ns
        self.max_ns = max(self.max_ns, other.max_ns)
        for bucket, count in other.histogram.items():
            self.histogram[bucket] = self.histogram.get(bucket, 0) + count


@dataclasses.dataclass
class Totals:
    """The totals of a phase, or of a window of W-2: what the device and the volume counted, and the calls per operation."""

    counts: dict[str, int] = dataclasses.field(default_factory=dict)
    operations: dict[str, Operation] = dataclasses.field(default_factory=dict)

    @staticmethod
    def parse(fields: dict[str, str]) -> Totals:
        totals = Totals()
        for key, value in fields.items():
            operation, separator, statistic = key.partition(".")
            if key in COUNTS or key == "appends":
                totals.counts[key] = int(value)
            elif separator and statistic in OPERATION_STATISTICS:
                totals.add_statistic(operation, statistic, value)
        return totals

    def add_statistic(self, operation: str, statistic: str, value: str) -> None:
        calls = self.operations.setdefault(operation, Operation())
        if statistic == "histogram":
            pairs = [int(number) for number in value.split(",")] if value else []
            for bucket, count in zip(pairs[::2], pairs[1::2], strict=True):
                calls.histogram[bucket] = calls.histogram.get(bucket, 0) + count
        else:
            setattr(calls, ATTRIBUTES[statistic], int(value))

    def merge(self, other: Totals) -> None:
        for key, value in other.counts.items():
            self.counts[key] = self.counts.get(key, 0) + value
        for name, operation in other.operations.items():
            self.operations.setdefault(name, Operation()).merge(operation)

    def count(self, key: str) -> int:
        return self.counts.get(key, 0)

    def calls(self, operation: str) -> int:
        return self.operations[operation].calls if operation in self.operations else 0

    def user_bytes(self, operation: str) -> int:
        return self.operations[operation].user_bytes if operation in self.operations else 0

    def device_ns(self) -> int:
        return self.count("readNs") + self.count("programNs") + self.count("eraseNs")

    def charged_ns(self, sector_erase_ns: float) -> float:
        """The device time with the erases charged when sectors are allocated, as metrics::ChargedDeviceTime does."""
        return self.device_ns() - self.count("eraseNs") + sector_erase_ns * self.count("allocatedSectors")

    def latencies(self) -> Iterator[Operation]:
        return (operation for name, operation in self.operations.items() if name != "idle")


@dataclasses.dataclass
class WorkloadRun:
    """The last run of a workload at a fill level on a device: its phases, same-named ones summed, and its windows; only a
    run that ended, and without a failed call, counts."""

    ram_bytes: int = 0
    phases: dict[str, Totals] = dataclasses.field(default_factory=dict)
    windows: dict[str, Totals] = dataclasses.field(default_factory=dict)
    failed: bool = False
    done: bool = False

    @property
    def counts(self) -> bool:
        return self.done and not self.failed

    def phase(self, name: str) -> Totals | None:
        return self.phases.get(name)

    def add_phase(self, name: str, totals: Totals) -> None:
        if name in self.phases:
            self.phases[name].merge(totals)
        else:
            self.phases[name] = totals


@dataclasses.dataclass
class DeviceRuns:
    """The workloads that ran on one device with one subject, by workload and fill level."""

    device: str
    subject: str
    runs: dict[tuple[str, int], WorkloadRun]
    model: dict[str, str]

    def run(self, workload: str, fill: int) -> WorkloadRun | None:
        run = self.runs.get((workload, fill))
        return run if run is not None and run.counts else None

    def completed(self) -> Iterator[tuple[str, int, WorkloadRun]]:
        return ((workload, fill, run) for (workload, fill), run in self.runs.items() if run.counts)

    def sector_erase_ns(self) -> float:
        """The mean erase of a sector over the whole run, which stands in for the typical erase of a model on the host."""
        erased = sum(totals.count("sectorsErased") for _, _, run in self.completed() for totals in run.phases.values())
        erase_ns = sum(totals.count("eraseNs") for _, _, run in self.completed() for totals in run.phases.values())
        return erase_ns / erased if erased else 0.0


def quotient(numerator: float, denominator: float, when_both_zero: float) -> float:
    if denominator != 0:
        return numerator / denominator
    return when_both_zero if numerator == 0 else math.inf


def percentile_ns(operations: Iterable[Operation], fraction: float) -> int:
    """The nearest-rank percentile of the merged histograms, at the end of its bucket and at most the longest call."""
    histogram: dict[int, int] = {}
    longest = 0
    for operation in operations:
        longest = max(longest, operation.max_ns)
        for bucket, count in operation.histogram.items():
            histogram[bucket] = histogram.get(bucket, 0) + count
    total = sum(histogram.values())
    if total == 0:
        return 0
    rank = max(math.ceil(fraction * total), 1)
    counted = 0
    for bucket in sorted(histogram):
        counted += histogram[bucket]
        if counted >= rank:
            return min(bucket_upper_bound(bucket), longest)
    return longest


def longest_ns(operations: Iterable[Operation]) -> int:
    return max((operation.max_ns for operation in operations), default=0)


class Results:
    """The results of one device and subject, as the entries of a fragment."""

    def __init__(self, runs: DeviceRuns) -> None:
        self.runs = runs
        self.entries: list[dict[str, Any]] = []

    def add(self, metric: str, workload: str, unit: str, better: str, value: float, ram_bytes: int, fill: int | None = None) -> None:
        entry: dict[str, Any] = {
            "metric": metric,
            "workload": workload,
            "subject": self.runs.subject,
            "device": self.runs.device,
            "unit": unit,
            "value": None if math.isinf(value) or math.isnan(value) else value,
            "ramBytes": ram_bytes,
            "better": better,
        }
        if fill is not None:
            entry["fill"] = fill
        self.entries.append(entry)


def sequential_write(results: Results, run: WorkloadRun, sector_erase_ns: float) -> None:
    for name, totals in run.phases.items():
        if SEQUENTIAL_WRITE_PHASE.match(name):
            written = totals.user_bytes("write")
            results.add("W-1", name, "B/s", "higher", quotient(written, totals.charged_ns(sector_erase_ns) / 1e9, 0), run.ram_bytes)
            results.add("W-4", name, "ratio", "lower", quotient(totals.count("bytesProgrammed"), written, 0), run.ram_bytes)


def append_time_ns(window: Totals | None, sector_erase_ns: float) -> float | None:
    if window is None or window.count("appends") == 0:
        return None
    return window.charged_ns(sector_erase_ns) / window.count("appends")


def data_logger(results: Results, run: WorkloadRun, fill: int, sector_erase_ns: float) -> None:
    log = run.phase("L2")
    if log is None:
        return
    first = append_time_ns(run.windows.get("first"), sector_erase_ns)
    second = append_time_ns(run.windows.get("second"), sector_erase_ns)
    ratio = math.inf if first is None or second is None else quotient(first, second, 1)
    results.add("W-2", "L2", "ratio", "lower", ratio, run.ram_bytes, fill)
    results.add("W-4", "L2", "ratio", "lower", quotient(log.count("bytesProgrammed"), log.user_bytes("write"), 0), run.ram_bytes, fill)


def per_append_ns(run: WorkloadRun | None, sector_erase_ns: float) -> float | None:
    log = None if run is None else run.phase("L2")
    if log is None or log.calls("write") == 0:
        return None
    return log.charged_ns(sector_erase_ns) / log.calls("write")


def fill_scaling(results: Results, sector_erase_ns: float) -> None:
    low = per_append_ns(results.runs.run("L2", 10), sector_erase_ns)
    high = per_append_ns(results.runs.run("L2", 90), sector_erase_ns)
    if low is not None and high is not None:
        results.add("W-3", "L2", "ratio", "lower", quotient(high, low, 1), results.runs.run("L2", 90).ram_bytes)


def sized_phases(run: WorkloadRun, pattern: re.Pattern[str]) -> list[tuple[int, Totals]]:
    return sorted((int(match.group(1)), totals) for name, totals in run.phases.items() if (match := pattern.match(name)))


def overwrite_scaling(results: Results, run: WorkloadRun, fill: int, sector_erase_ns: float) -> None:
    phases = sized_phases(run, OVERWRITE_PHASE)
    if len(phases) < 2:
        return
    (_, small), (_, large) = phases[0], phases[-1]
    per_small = quotient(small.charged_ns(sector_erase_ns), small.calls("write"), 0)
    per_large = quotient(large.charged_ns(sector_erase_ns), large.calls("write"), 0)
    results.add("W-5", "L3", "ratio", "lower", quotient(per_large, per_small, 1), run.ram_bytes, fill)


def latency(results: Results, workload: str, run: WorkloadRun, phases: list[Totals]) -> None:
    operations = [operation for totals in phases for operation in totals.latencies()]
    if not operations:
        return
    results.add("W-6", f"{workload} p99", "ms", "lower", percentile_ns(operations, 0.99) / 1e6, run.ram_bytes, LATENCY_FILL)
    results.add("W-6", f"{workload} maximum", "ms", "lower", longest_ns(operations) / 1e6, run.ram_bytes, LATENCY_FILL)


def call_latencies(results: Results) -> None:
    logger = results.runs.run("L2", LATENCY_FILL)
    if logger is not None:
        latency(results, "L2", logger, [totals for name in DATA_LOGGER_PHASES if (totals := logger.phase(name)) is not None])
    overwrite = results.runs.run("L3", LATENCY_FILL)
    if overwrite is not None:
        latency(results, "L3", overwrite, [totals for _, totals in sized_phases(overwrite, OVERWRITE_PHASE)])


def reads(results: Results, run: WorkloadRun) -> None:
    sequential = run.phase("L6 sequential")
    if sequential is not None:
        read = sequential.user_bytes("read")
        results.add("R-1", "L6 sequential", "B/s", "higher", quotient(read, sequential.device_ns() / 1e9, 0), run.ram_bytes)
        results.add("R-5", "L6 sequential", "ratio", "lower", quotient(sequential.count("bytesRead"), read, 0), run.ram_bytes)
    random_reads = sized_phases(run, RANDOM_READ_PHASE)
    requests = [quotient(totals.count("readRequests"), totals.calls("read"), 0) for _, totals in random_reads]
    for (size, _), value in zip(random_reads, requests, strict=True):
        results.add("R-2", f"L6 random {size}", "requests", "lower", value, run.ram_bytes)
    if len(requests) >= 2:
        results.add("R-2", "L6 random ratio", "ratio", "lower", quotient(requests[-1], requests[0], 1), run.ram_bytes)
    for entries, totals in sized_phases(run, LOOKUP_PHASE):
        per_open_ns = quotient(totals.device_ns(), totals.calls("open"), 0)
        results.add("R-3", f"L6 lookup {entries}", "ms", "lower", per_open_ns / 1e6, run.ram_bytes)


def mounts(results: Results, run: WorkloadRun) -> None:
    for percent, totals in sized_phases(run, MOUNT_PHASE):
        results.add("R-4", "L7", "ms", "lower", totals.device_ns() / 1e6, run.ram_bytes, percent)


def model_time_ns(totals: Totals, model: dict[str, str]) -> float:
    """The device time that a NOR model gives the requests of a phase, as metrics::ProgramTime, EraseTime and ReadTime give it."""
    return (
        int(model["typical.pageProgramNs"]) * totals.count("pagesProgrammed")
        + int(model["typical.sectorEraseNs"]) * totals.count("sectorsErased")
        + totals.count("bytesRead") * 1e9 / int(model["typical.readBytesPerSecond"])
    )


def model_error(results: Results) -> None:
    model = results.runs.model
    if "typical.pageProgramNs" not in model:
        return
    for workload, fill, run in results.runs.completed():
        for name, totals in run.phases.items():
            measured = totals.device_ns()
            if measured != 0:
                error = abs(model_time_ns(totals, model) - measured) / measured
                phase = name if name.startswith(workload) else f"{workload} {name}"
                results.add("C-1", phase, "ratio", "lower", error, run.ram_bytes, fill if fill != 0 else None)


def latency_bound(results: Results) -> None:
    bound = results.runs.model.get("w6BoundMs")
    logger = results.runs.run("L2", LATENCY_FILL)
    if bound is None or logger is None:
        return
    phases = [totals for name in DATA_LOGGER_PHASES if (totals := logger.phase(name)) is not None]
    operations = [operation for totals in phases for operation in totals.latencies()]
    results.add("C-2", "L2", "ratio", "lower", longest_ns(operations) / 1e6 / float(bound), logger.ram_bytes, LATENCY_FILL)


def workload_results(runs: DeviceRuns) -> list[dict[str, Any]]:
    """The metrics of the workloads of a device and subject, from the last run of every workload and fill level."""
    results = Results(runs)
    sector_erase_ns = runs.sector_erase_ns()
    for workload, fill, run in runs.completed():
        if workload == "L1":
            sequential_write(results, run, sector_erase_ns)
        elif workload == "L2":
            data_logger(results, run, fill, sector_erase_ns)
        elif workload == "L3":
            overwrite_scaling(results, run, fill, sector_erase_ns)
        elif workload == "L6":
            reads(results, run)
        elif workload == "L7":
            mounts(results, run)
    fill_scaling(results, sector_erase_ns)
    call_latencies(results)
    model_error(results)
    latency_bound(results)
    return results.entries
