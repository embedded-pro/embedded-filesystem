"""Turns the reports of cortex-memory-budget on the reference images of F-1 into a report fragment (docs/Validation.md).

The reports are `<reports>/<core>/<image>/memory_metrics.json`, one per image built for a core, and every subject is
measured against the empty image of its core. Only the standard library is imported, as in emfs_validation.convert.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

EMPTY = "empty"
METRICS = "memory_metrics.json"
LINK_TIME_OPTIMIZATION = "-lto"
IMAGES = {
    "fat": "embedded-filesystem/fat",
    "nor": "embedded-filesystem/nor",
    "fat_reads": "embedded-filesystem/fat-reads",
    "nor_reads": "embedded-filesystem/nor-reads",
    "fat_read_only": "embedded-filesystem/fat-read-only",
    "fat_minimal": "embedded-filesystem/fat-minimal",
    "fat_lite": "embedded-filesystem/fat-lite",
    "nor_minimal": "embedded-filesystem/nor-minimal",
    "nor_lite": "embedded-filesystem/nor-lite",
    "fatfs_default": "fatfs/default",
    "fatfs_tuned": "fatfs/tuned",
    "fatfs_minimal": "fatfs/minimal",
    "fatfs_read_only": "fatfs/read-only",
    "littlefs_default": "littlefs/default",
    "littlefs_ram_matched": "littlefs/ram-matched",
    "littlefs_read_only": "littlefs/read-only",
}


class FootprintError(Exception):
    """The reports cannot be turned into a fragment."""


def sizes(path: Path) -> tuple[int, int]:
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise FootprintError(f"cannot read {path}: {error}") from None
    for key in ("flash_bytes", "ram_static_bytes"):
        if not isinstance(report, dict) or not isinstance(report.get(key), int):
            raise FootprintError(f"{path} has no {key}")
    return report["flash_bytes"], report["ram_static_bytes"]


def result(subject: str, core: str, workload: str, value: int, ram: int) -> dict[str, Any]:
    return {
        "metric": "F-1",
        "workload": workload,
        "subject": subject,
        "device": core,
        "unit": "B",
        "value": value,
        "ramBytes": ram,
        "better": "lower",
    }


def core_results(directory: Path) -> list[dict[str, Any]]:
    core = directory.name
    reports = {path.parent.name: path for path in directory.glob(f"*/{METRICS}")}
    if EMPTY not in reports:
        raise FootprintError(f"{core}: no report of the empty image")
    unknown = sorted(reports.keys() - IMAGES.keys() - {EMPTY})
    if unknown:
        raise FootprintError(f"{core}: unknown image {', '.join(unknown)}")
    empty_flash, empty_ram = sizes(reports[EMPTY])
    results = []
    for image, subject in IMAGES.items():
        if image not in reports:
            raise FootprintError(f"{core}: no report of the image {image}")
        flash, ram = sizes(reports[image])
        if flash < empty_flash or ram < empty_ram:
            raise FootprintError(f"{core}: the image {image} is smaller than the empty image")
        results.append(result(subject, core, "flash", flash - empty_flash, ram - empty_ram))
        results.append(result(subject, core, "ram", ram - empty_ram, ram - empty_ram))
    return results


def parameters(core: str, toolchain: str, configuration: str) -> dict[str, Any]:
    device: dict[str, Any] = {"toolchain": toolchain, "configuration": configuration}
    if core.endswith(LINK_TIME_OPTIMIZATION):
        device["linkTimeOptimization"] = True
    return device


def fragment(reports: Path, toolchain: str, configuration: str) -> dict[str, Any]:
    cores = sorted(path for path in reports.glob("*") if path.is_dir()) if reports.is_dir() else []
    if not cores:
        raise FootprintError(f"no reports in {reports}")
    devices = {core.name: parameters(core.name, toolchain, configuration) for core in cores}
    results = [entry for core in cores for entry in core_results(core)]
    return {"devices": devices, "results": results}


def parse_arguments(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python3 -m emfs_validation.footprint", description=__doc__.splitlines()[0])
    parser.add_argument("--reports", type=Path, required=True, help="the reports, <reports>/<core>/<image>/memory_metrics.json")
    parser.add_argument("--toolchain", required=True, help="the compiler the images were built with, a parameter of every core")
    parser.add_argument("--configuration", required=True, help="the build configuration of the images, a parameter of every core")
    parser.add_argument("--output", type=Path, required=True, help="the fragment to write")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    try:
        report = fragment(arguments.reports, arguments.toolchain, arguments.configuration)
    except FootprintError as error:
        print(f"footprint: {error}", file=sys.stderr)
        return 2
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{len(report['devices'])} cores, {len(report['results'])} results")
    return 0


if __name__ == "__main__":
    sys.exit(main())
