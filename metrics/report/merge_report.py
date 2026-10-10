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
METRIC_PATTERN = re.compile(r"^([A-Z]+)-(\d+)$")


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
        return comparison, float(reference)
    except ValueError:
        return comparison, reference


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
    comparison, reference = parse_target(result["target"])
    if isinstance(reference, float):
        return
    best = best_reference(results, result, comparison, reference)
    if best is None:
        result["note"] = f"no result of {reference} to compare with"
        return
    result["referenceSubject"] = best["subject"]
    result["referenceValue"] = best["value"]
    result["pass"] = result.get("value") is not None and holds(comparison, result["value"], best["value"])


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
        if "target" not in result and is_own(result["subject"]) and worsening(result, earlier["value"]) > REGRESSION_TOLERANCE:
            result["regression"] = True
            result["pass"] = False


def metric_order(metric):
    match = METRIC_PATTERN.match(metric)
    if match is None:
        return (metric, 0)
    return (match.group(1), int(match.group(2)))


def result_order(result):
    fill = result.get("fill")
    return (metric_order(result["metric"]), result["workload"], -1 if fill is None else fill, result["device"], result["subject"])


def merge(fragment_paths, baseline=None):
    devices, results = load_fragments(fragment_paths)
    decide_relative_targets(results)
    if baseline is not None:
        compare_with_baseline(results, devices, baseline)
    results.sort(key=result_order)
    return {"devices": devices, "results": results}


def format_value(value):
    return "n/a" if value is None else f"{value:.4g}"


def format_outcome(result):
    if result.get("regression"):
        return f"❌ worse than main ({format_value(result['baselineValue'])})"
    if result.get("pass") is True:
        return "✅"
    if result.get("pass") is False:
        return "❌"
    if "note" in result:
        return result["note"]
    if "baselineValue" in result:
        return f"reported (main: {format_value(result['baselineValue'])})"
    return "reported"


def format_row(result):
    fill = result.get("fill")
    cells = [
        result["metric"],
        result["workload"],
        "" if fill is None else f"{fill} %",
        result["subject"],
        result["device"],
        format_value(result.get("value")),
        result["unit"],
        result.get("target", ""),
        format_outcome(result),
        str(result["ramBytes"]),
    ]
    return "| " + " | ".join(cells) + " |"


def summary(results):
    passed = sum(1 for result in results if result.get("pass") is True)
    failed = sum(1 for result in results if result.get("pass") is False)
    reported = len(results) - passed - failed
    return f"{len(results)} results: {passed} passed, {failed} failed, {reported} reported."


def device_rows(devices):
    for name in sorted(devices):
        for parameter, value in devices[name].items():
            yield f"| {name} | {parameter} | {value} |"


def markdown(report):
    lines = [
        "## Metrics",
        "",
        summary(report["results"]),
        "",
        "| Metric | Workload | Fill | Subject | Device | Value | Unit | Target | Result | RAM (B) |",
        "|--------|----------|------|---------|--------|-------|------|--------|--------|---------|",
    ]
    lines.extend(format_row(result) for result in report["results"])
    lines.extend(["", "<details><summary>Device models</summary>", "", "| Device | Parameter | Value |", "|--------|-----------|-------|"])
    lines.extend(device_rows(report["devices"]))
    lines.extend(["", "</details>", ""])
    return "\n".join(lines)


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fragments", nargs="*", help="report fragments written by the metric executables")
    parser.add_argument("--fragment-directory", type=Path, help="directory whose *.json files are added to the fragments")
    parser.add_argument("--json", type=Path, required=True, help="merged report to write")
    parser.add_argument("--markdown", type=Path, required=True, help="Markdown summary to write")
    parser.add_argument("--baseline", type=Path, help="merged report of main, to detect regressions")
    parser.add_argument("--check", action="store_true", help="exit with 1 when a target is missed or a metric regressed")
    return parser.parse_args(argv)


def fragment_paths(arguments):
    paths = list(arguments.fragments)
    if arguments.fragment_directory is not None:
        paths.extend(sorted(str(path) for path in arguments.fragment_directory.glob("*.json")))
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
