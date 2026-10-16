#!/usr/bin/env python3
"""Merge the report fragments of the metric executables into metrics.json and metrics.md."""

import argparse
import json
import re
import sys
from pathlib import Path

OWN_SUBJECT_PREFIX = "embedded-filesystem"
ALL_BASELINES = "baselines"
REGRESSION_TOLERANCE = 0.05
TARGET_PATTERN = re.compile(r"^(<=|>=) (.+)$")
LIFETIME_METRIC = "WL-3"
LIFETIME_FAMILY = "littlefs"
METRIC_PATTERN = re.compile(r"^([A-Z]+)-(\d+)$")
NUMBER_PATTERN = re.compile(r"(\d+)")


class ReportError(Exception):
    """A fragment cannot be merged."""


def is_own(subject):
    return subject == OWN_SUBJECT_PREFIX or subject.startswith(OWN_SUBJECT_PREFIX + "/")


def in_family(subject, family):
    if family == ALL_BASELINES:
        return not is_own(subject)
    return subject == family or subject.startswith(family + "/")


def parse_target(text):
    match = TARGET_PATTERN.match(text)
    if match is None:
        raise ReportError(f"unknown target '{text}'")
    comparison, reference = match.groups()
    try:
        return comparison, float(reference), 1.0
    except ValueError:
        pass
    factor, _, family = reference.partition(" ")
    try:
        return comparison, family, float(factor)
    except ValueError:
        return comparison, reference, 1.0


def comparison_key(result):
    return (result["metric"], result["workload"], result["device"], result.get("fill"))


def identity_key(result):
    return comparison_key(result) + (result["subject"],)


def merge_devices(devices, fragment_devices, source):
    for name, parameters in fragment_devices.items():
        if name in devices and devices[name] != parameters:
            raise ReportError(f"{source}: device '{name}' has parameters that differ from another fragment")
        devices[name] = parameters


def load_fragments(paths):
    devices = {}
    results = []
    for path in paths:
        fragment = json.loads(Path(path).read_text(encoding="utf-8"))
        merge_devices(devices, fragment.get("devices", {}), path)
        results.extend(fragment.get("results", []))
    return devices, results


def holds(comparison, value, reference):
    return value <= reference if comparison == "<=" else value >= reference


def best_reference(results, result, comparison, family):
    candidates = [
        candidate
        for candidate in results
        if candidate is not result
        and comparison_key(candidate) == comparison_key(result)
        and in_family(candidate["subject"], family)
        and candidate.get("value") is not None
    ]
    if not candidates:
        return None
    choose = min if comparison == "<=" else max
    return choose(candidates, key=lambda candidate: candidate["value"])


def decide_relative_target(results, result):
    comparison, reference, factor = parse_target(result["target"])
    if isinstance(reference, float):
        return
    best = best_reference(results, result, comparison, reference)
    if best is None:
        result["note"] = f"no result of {reference} to compare with"
        return
    result["referenceSubject"] = best["subject"]
    result["referenceValue"] = best["value"]
    result["pass"] = result.get("value") is not None and holds(comparison, result["value"], factor * best["value"])


def decide_relative_targets(results):
    for result in results:
        if "target" in result:
            decide_relative_target(results, result)


def worsening(result, earlier):
    if earlier == 0:
        return 0.0
    change = (result["value"] - earlier) / abs(earlier)
    return -change if result["better"] == "higher" else change


def compare_with_baseline(results, devices, baseline):
    earlier_results = {identity_key(earlier): earlier for earlier in baseline.get("results", [])}
    earlier_devices = baseline.get("devices", {})
    for result in results:
        earlier = earlier_results.get(identity_key(result))
        if earlier is None or earlier.get("value") is None or result.get("value") is None:
            continue
        result["baselineValue"] = earlier["value"]
        if devices.get(result["device"]) != earlier_devices.get(result["device"]):
            result["note"] = "device model changed since the baseline"
            continue
        if "target" not in result and not result.get("diagnostic") and not earlier.get("diagnostic") and is_own(result["subject"]) and worsening(result, earlier["value"]) > REGRESSION_TOLERANCE:
            result["regression"] = True
            result["pass"] = False


def metric_order(metric):
    match = METRIC_PATTERN.match(metric)
    if match is None:
        return (metric, 0)
    return (match.group(1), int(match.group(2)))


def natural_order(text):
    return tuple(int(part) if index % 2 else part for index, part in enumerate(NUMBER_PATTERN.split(text)))


def row_order(result):
    fill = result.get("fill")
    return (metric_order(result["metric"]), natural_order(result["workload"]), -1 if fill is None else fill)


def result_order(result):
    return row_order(result) + (result["device"], result["subject"])


def merge(fragment_paths, baseline=None):
    devices, results = load_fragments(fragment_paths)
    decide_relative_targets(results)
    if baseline is not None:
        compare_with_baseline(results, devices, baseline)
    results.sort(key=result_order)
    return {"devices": devices, "results": results}


def format_value(value):
    return "n/a" if value is None else f"{value:.4g}"


def format_cell(result):
    value = format_value(result.get("value"))
    if result.get("regression"):
        return f"{value} ❌ (main: {format_value(result['baselineValue'])})"
    if result.get("pass") is True:
        return f"{value} ✅"
    if result.get("pass") is False:
        return f"{value} ❌"
    return value


def subject_order(subject):
    return (not is_own(subject), subject)


def table_row(cells):
    return "| " + " | ".join(cells) + " |"


def device_table(results):
    subjects = sorted({result["subject"] for result in results}, key=subject_order)
    rows = {}
    for result in results:
        rows.setdefault(row_order(result), {})[result["subject"]] = result

    lines = [table_row(["Metric", "Workload", "Fill", "Unit", *subjects]), table_row(["---"] * (4 + len(subjects)))]
    for key in sorted(rows):
        row = rows[key]
        first = next(iter(row.values()))
        fill = first.get("fill")
        cells = [first["metric"], first["workload"], "" if fill is None else f"{fill} %", first["unit"]]
        cells.extend(format_cell(row[subject]) if subject in row else "" for subject in subjects)
        lines.append(table_row(cells))

    ram = {result["subject"]: result["ramBytes"] for result in results}
    lines.append(table_row(["RAM", "", "", "B", *(str(ram[subject]) for subject in subjects)]))
    return lines


def summary(results):
    passed = sum(1 for result in results if result.get("pass") is True)
    failed = sum(1 for result in results if result.get("pass") is False)
    reported = len(results) - passed - failed
    return f"{len(results)} results: {passed} passed, {failed} failed, {reported} reported."


def failure_lines(results):
    failures = [result for result in results if result.get("pass") is False]
    if not failures:
        return []
    lines = ["### Missed targets and regressions", ""]
    for result in failures:
        expected = result.get("target") or f"main: {format_value(result.get('baselineValue'))}"
        fill = result.get("fill")
        at_fill = "" if fill is None else f" at {fill} %"
        lines.append(f"- {result['metric']} {result['workload']}{at_fill}, {result['subject']} on {result['device']}: {format_value(result.get('value'))} {result['unit']}, expected {expected}")
    return lines + [""]


def lifetime_lines(results):
    lines = []
    for result in results:
        if result["metric"] != LIFETIME_METRIC or not is_own(result["subject"]) or result.get("value") is None:
            continue
        best = best_reference(results, result, ">=", LIFETIME_FAMILY)
        if best is None or not best["value"]:
            continue
        ratio = result["value"] / best["value"]
        lines.append(f"{LIFETIME_METRIC} on {result['device']}: {result['subject']} writes {format_value(ratio)} times the user data of {best['subject']}.")
    return lines + [""] if lines else []


def device_rows(devices):
    for name in sorted(devices):
        for parameter, value in devices[name].items():
            yield table_row([name, parameter, value])


def markdown(report):
    results = report["results"]
    lines = ["## Metrics", "", summary(results), ""]
    lines.extend(lifetime_lines(results))
    lines.extend(failure_lines(results))
    for device in sorted({result["device"] for result in results}):
        lines.extend([f"### {device}", ""])
        lines.extend(device_table([result for result in results if result["device"] == device]))
        lines.append("")
    lines.extend(["<details><summary>Device models</summary>", "", "| Device | Parameter | Value |", "|--------|-----------|-------|"])
    lines.extend(device_rows(report["devices"]))
    lines.extend(["", "</details>", ""])
    return "\n".join(lines)


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fragments", nargs="*", help="report fragments written by the metric executables")
    parser.add_argument(
        "--fragment-directory", type=Path, action="append", default=[], help="directory whose *.json files are added to the fragments; repeatable"
    )
    parser.add_argument("--json", type=Path, required=True, help="merged report to write")
    parser.add_argument("--markdown", type=Path, required=True, help="Markdown summary to write")
    parser.add_argument("--baseline", type=Path, help="merged report of main, to detect regressions")
    parser.add_argument("--check", action="store_true", help="exit with 1 when a target is missed or a metric regressed")
    return parser.parse_args(argv)


def fragment_paths(arguments):
    paths = list(arguments.fragments)
    for directory in arguments.fragment_directory:
        if not directory.is_dir():
            print(f"merge_report: no fragment directory {directory}", file=sys.stderr)
        paths.extend(sorted(str(path) for path in directory.glob("*.json")))
    return paths


def main(argv=None):
    arguments = parse_arguments(argv)
    baseline = None
    if arguments.baseline is not None and arguments.baseline.exists():
        baseline = json.loads(arguments.baseline.read_text(encoding="utf-8"))
    try:
        report = merge(fragment_paths(arguments), baseline)
    except ReportError as error:
        print(f"merge_report: {error}", file=sys.stderr)
        return 2
    arguments.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    arguments.markdown.write_text(markdown(report), encoding="utf-8")
    missed = any(result.get("pass") is False for result in report["results"])
    return 1 if arguments.check and missed else 0


if __name__ == "__main__":
    sys.exit(main())
