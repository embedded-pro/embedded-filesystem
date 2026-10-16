from __future__ import annotations

import json
from pathlib import Path

import pytest

from emfs_validation.footprint import IMAGES, FootprintError, fragment, main

CORES = ("cortex-m4", "cortex-m7")


def write_report(reports: Path, core: str, image: str, flash: int, ram: int) -> None:
    directory = reports / core / image
    directory.mkdir(parents=True)
    (directory / "memory_metrics.json").write_text(json.dumps({"flash_bytes": flash, "ram_static_bytes": ram}), encoding="utf-8")


def write_reports(reports: Path, cores: tuple[str, ...] = CORES) -> None:
    for offset, core in enumerate(cores):
        write_report(reports, core, "empty", 6000 + offset, 2500 + offset)
        for index, image in enumerate(IMAGES, start=1):
            write_report(reports, core, image, 6000 + offset + 1000 * index, 2500 + offset + 100 * index)


def result(subject: str, workload: str, value: int, ram: int, device: str = "cortex-m4") -> dict[str, object]:
    return {
        "metric": "F-1",
        "workload": workload,
        "subject": subject,
        "device": device,
        "unit": "B",
        "value": value,
        "ramBytes": ram,
        "better": "lower",
    }


def test_every_subject_is_measured_against_the_empty_image(tmp_path):
    write_reports(tmp_path, ("cortex-m4",))

    report = fragment(tmp_path, "arm-none-eabi-gcc 15.2.1", "MinSizeRel")

    assert report["results"][:2] == [
        result("embedded-filesystem/fat", "flash", 1000, 100),
        result("embedded-filesystem/fat", "ram", 100, 100),
    ]
    assert len(report["results"]) == 2 * len(IMAGES)


def test_every_core_is_a_device_with_the_toolchain_and_the_configuration(tmp_path):
    write_reports(tmp_path)

    report = fragment(tmp_path, "arm-none-eabi-gcc 15.2.1", "MinSizeRel")

    assert report["devices"] == {core: {"toolchain": "arm-none-eabi-gcc 15.2.1", "configuration": "MinSizeRel"} for core in CORES}
    assert {(entry["device"], entry["value"]) for entry in report["results"] if entry["subject"] == "embedded-filesystem/fat"} == {
        ("cortex-m4", 1000),
        ("cortex-m4", 100),
        ("cortex-m7", 1000),
        ("cortex-m7", 100),
    }


def test_a_core_built_with_link_time_optimization_says_so(tmp_path):
    write_reports(tmp_path, ("cortex-m4", "cortex-m4-lto"))

    report = fragment(tmp_path, "arm-none-eabi-gcc 15.2.1", "MinSizeRel")

    expected = {"toolchain": "arm-none-eabi-gcc 15.2.1", "configuration": "MinSizeRel", "linkTimeOptimization": True}
    assert report["devices"]["cortex-m4-lto"] == expected
    assert "linkTimeOptimization" not in report["devices"]["cortex-m4"]


def test_a_core_without_the_empty_image_is_an_error(tmp_path):
    write_report(tmp_path, "cortex-m4", "fat", 7000, 2600)

    with pytest.raises(FootprintError, match="cortex-m4: no report of the empty image"):
        fragment(tmp_path, "gcc", "MinSizeRel")


def test_a_missing_subject_is_an_error(tmp_path):
    write_reports(tmp_path, ("cortex-m4",))
    (tmp_path / "cortex-m4" / "nor" / "memory_metrics.json").unlink()

    with pytest.raises(FootprintError, match="cortex-m4: no report of the image nor"):
        fragment(tmp_path, "gcc", "MinSizeRel")


def test_an_unknown_image_is_an_error(tmp_path):
    write_reports(tmp_path, ("cortex-m4",))
    write_report(tmp_path, "cortex-m4", "spiffs", 8000, 2600)

    with pytest.raises(FootprintError, match="cortex-m4: unknown image spiffs"):
        fragment(tmp_path, "gcc", "MinSizeRel")


def test_a_report_without_the_sizes_is_an_error(tmp_path):
    write_reports(tmp_path, ("cortex-m4",))
    (tmp_path / "cortex-m4" / "fat" / "memory_metrics.json").write_text("{}", encoding="utf-8")

    with pytest.raises(FootprintError, match="memory_metrics.json has no flash_bytes"):
        fragment(tmp_path, "gcc", "MinSizeRel")


def test_no_reports_are_an_error(tmp_path):
    with pytest.raises(FootprintError, match="no reports in"):
        fragment(tmp_path, "gcc", "MinSizeRel")


def test_the_command_writes_the_fragment(tmp_path, capsys):
    write_reports(tmp_path / "reports")
    output = tmp_path / "fragments" / "footprint.json"

    arguments = ["--reports", str(tmp_path / "reports"), "--toolchain", "gcc", "--configuration", "MinSizeRel", "--output", str(output)]
    assert main(arguments) == 0

    assert len(json.loads(output.read_text(encoding="utf-8"))["results"]) == 2 * len(IMAGES) * len(CORES)
    assert "2 cores, 56 results" in capsys.readouterr().out


def test_the_command_reports_an_error(tmp_path, capsys):
    arguments = ["--reports", str(tmp_path), "--toolchain", "gcc", "--configuration", "MinSizeRel", "--output", str(tmp_path / "f.json")]
    assert main(arguments) == 2

    assert "footprint: no reports in" in capsys.readouterr().err


def test_an_unreadable_report_is_an_error(tmp_path):
    write_reports(tmp_path, ("cortex-m4",))
    (tmp_path / "cortex-m4" / "fat" / "memory_metrics.json").write_text("{", encoding="utf-8")

    with pytest.raises(FootprintError, match="cannot read"):
        fragment(tmp_path, "gcc", "MinSizeRel")


def test_an_image_smaller_than_the_empty_image_is_an_error(tmp_path):
    write_reports(tmp_path, ("cortex-m4",))
    write_report_over = tmp_path / "cortex-m4" / "nor" / "memory_metrics.json"
    write_report_over.write_text(json.dumps({"flash_bytes": 5000, "ram_static_bytes": 2600}), encoding="utf-8")

    with pytest.raises(FootprintError, match="cortex-m4: the image nor is smaller than the empty image"):
        fragment(tmp_path, "gcc", "MinSizeRel")
