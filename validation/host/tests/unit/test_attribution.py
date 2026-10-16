from __future__ import annotations

from pathlib import Path

import pytest

from emfs_validation.attribution import AttributionError, by_kind, by_library, by_object, difference, main, parse_map, report

FAT = """Archive member included to satisfy reference by file (symbol)

Discarded input sections

 .text._ZN2fs6UnusedEv
                0x00000000       0x10 fs/fat/MinSizeRel/libfs.fat.a(FatUnused.cpp.obj)

Memory Configuration

Name             Origin             Length             Attributes
FLASH            0x08000000         0x00200000         xr

Linker script and memory map

LOAD CMakeFiles/validation.footprint.fat.dir/MinSizeRel/FatMain.cpp.obj

.isr_vector     0x08000000       0x40
 KEEP(*(.isr_vector))
 .isr_vector    0x08000000       0x40 validation/footprint/MinSizeRel/libvalidation.footprint_platform.a(Startup.cpp.obj)

.text           0x08000040      0x200
 *(.text*)
 .text.main     0x08000040       0x30 CMakeFiles/validation.footprint.fat.dir/MinSizeRel/FatMain.cpp.obj
                0x08000040                main
 .text._ZN2fs13FatFileSystem5MountERKN5infra8FunctionIFvNS_6ResultEELj8EEE
                0x08000070       0x48 fs/fat/MinSizeRel/libfs.fat.a(FatFileSystem.cpp.obj)
                0x08000070                fs::FatFileSystem::Mount(infra::Function<void (fs::Result), 8u> const&)
 *fill*         0x080000b8        0x8
 .text._ZN5infra6detail16InvokerFunctionsIFvN2fs6ResultEELj8EE12StaticInvokeIZNS2_7FatSync5StartEvEUlS3_E_EEvRKS5_S3_
                0x080000c0        0x6 fs/fat/MinSizeRel/libfs.fat.a(FatSync.cpp.obj)
 .text.strlen   0x080000c8       0x10 /usr/lib/arm-none-eabi/newlib/thumb/v7e-m+fp/hard/libc_nano.a(libc_a-strlen.o)

.rodata         0x08000240       0x40
 .rodata._ZTVN2fs13FatFileSystemE
                0x08000240       0x34 fs/fat/MinSizeRel/libfs.fat.a(FatFileSystem.cpp.obj)
 .rodata._ZZN8services9ValidatedIN2fs13FatFileSystemEEEDavE4rows
                0x08000274        0xc fs/fat/MinSizeRel/libfs.fat.a(FatFileSystem.cpp.obj)

.data           0x20000000        0x8 load address 0x08000280
 .data.counter  0x20000000        0x8 fs/fat/MinSizeRel/libfs.fat.a(FatVolume.cpp.obj)

.bss            0x20000008      0x104 load address 0x08000288
 .bss._ZZ4mainE8platform
                0x20000008      0x100 CMakeFiles/validation.footprint.fat.dir/MinSizeRel/FatMain.cpp.obj
 COMMON         0x20000108        0x4 fs/fat/MinSizeRel/libfs.fat.a(FatVolume.cpp.obj)

.debug_info     0x00000000      0x500
 .debug_info    0x00000000      0x500 fs/fat/MinSizeRel/libfs.fat.a(FatFileSystem.cpp.obj)

.comment        0x00000000       0x26
 .comment       0x00000000       0x26 CMakeFiles/validation.footprint.fat.dir/MinSizeRel/FatMain.cpp.obj
"""

EMPTY = """Linker script and memory map

.isr_vector     0x08000000       0x40
 .isr_vector    0x08000000       0x40 validation/footprint/MinSizeRel/libvalidation.footprint_platform.a(Startup.cpp.obj)

.text           0x08000040       0x40
 .text.main     0x08000040       0x20 CMakeFiles/validation.footprint.empty.dir/MinSizeRel/EmptyMain.cpp.obj
 .text.strlen   0x08000060       0x10 /usr/lib/arm-none-eabi/newlib/thumb/v7e-m+fp/hard/libc_nano.a(libc_a-strlen.o)

.bss            0x20000000      0x100
 .bss._ZZ4mainE8platform
                0x20000000      0x100 CMakeFiles/validation.footprint.empty.dir/MinSizeRel/EmptyMain.cpp.obj
"""


def write_maps(directory: Path, maps: dict[str, str]) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    for image, text in maps.items():
        (directory / f"validation.footprint.{image}.elf.map").write_text(text, encoding="utf-8")


def test_sections_are_attributed_to_the_objects_they_come_from():
    objects = by_object(parse_map(FAT))

    assert objects == {
        "validation.footprint_platform/Startup": (0x40, 0),
        "main": (0x30, 0x100),
        "fs.fat/FatFileSystem": (0x48 + 0x34 + 0xC, 0),
        "fill": (0x8, 0),
        "fs.fat/FatSync": (0x6, 0),
        "c_nano/libc_a-strlen": (0x10, 0),
        "fs.fat/FatVolume": (0x8, 0x8 + 0x4),
    }


def test_the_main_of_every_image_is_one_object():
    assert by_object(parse_map(EMPTY))["main"] == (0x20, 0x100)


def test_discarded_and_debug_sections_are_left_out():
    objects = by_object(parse_map(FAT))

    assert "fs.fat/FatUnused" not in objects
    assert sum(flash for flash, _ in objects.values()) == 0x40 + 0x30 + 0x48 + 0x8 + 0x6 + 0x10 + 0x34 + 0xC + 0x8


def test_an_image_is_measured_against_the_empty_image():
    added = difference(by_object(parse_map(FAT)), by_object(parse_map(EMPTY)))

    assert added == {
        "main": (0x10, 0),
        "fs.fat/FatFileSystem": (0x88, 0),
        "fill": (0x8, 0),
        "fs.fat/FatSync": (0x6, 0),
        "fs.fat/FatVolume": (0x8, 0xC),
    }


def test_objects_are_summed_by_library():
    added = difference(by_object(parse_map(FAT)), by_object(parse_map(EMPTY)))

    assert by_library(added) == {"fs.fat": (0x88 + 0x6 + 0x8, 0xC), "image": (0x10 + 0x8, 0)}


def test_code_is_sorted_by_kind():
    assert by_kind(parse_map(FAT)) == {"callbacks": 0x6, "state machines": 0xC, "virtual tables": 0x34, "other": 0xD8}


def test_the_report_lists_the_libraries_kinds_and_objects_of_every_image(tmp_path):
    write_maps(tmp_path, {"empty": EMPTY, "fat": FAT})

    text = report(tmp_path, "cortex-m4")

    assert text.startswith("# Attribution of F-1 on cortex-m4\n")
    assert "## embedded-filesystem/fat: 174 B of flash, 12 B of RAM\n" in text
    assert "| fs.fat | 150 | 12 |\n" in text
    assert "| virtual tables | 52 |\n" in text
    assert "| fs.fat/FatFileSystem | 136 | 0 |\n" in text
    assert "## empty" not in text


def test_an_image_without_a_subject_is_named_by_its_image(tmp_path):
    write_maps(tmp_path, {"empty": EMPTY, "spike": FAT})

    assert "## spike: 174 B of flash, 12 B of RAM\n" in report(tmp_path, "cortex-m4")


def test_a_directory_without_the_map_of_the_empty_image_is_an_error(tmp_path):
    write_maps(tmp_path, {"fat": FAT})

    with pytest.raises(AttributionError, match="no map of the empty image"):
        report(tmp_path, "cortex-m4")


def test_a_file_that_is_not_a_map_is_an_error():
    with pytest.raises(AttributionError, match="not a map of the linker"):
        parse_map("text\n")


def test_the_command_writes_the_report(tmp_path, capsys):
    write_maps(tmp_path / "images", {"empty": EMPTY, "fat": FAT})
    output = tmp_path / "attribution" / "cortex-m4.md"

    assert main(["--maps", str(tmp_path / "images"), "--core", "cortex-m4", "--output", str(output)]) == 0

    assert "## embedded-filesystem/fat" in output.read_text(encoding="utf-8")
    assert "cortex-m4: 1 image" in capsys.readouterr().out


def test_the_command_reports_an_error(tmp_path, capsys):
    assert main(["--maps", str(tmp_path), "--core", "cortex-m4", "--output", str(tmp_path / "out.md")]) == 2

    assert "attribution: cortex-m4: no map of the empty image" in capsys.readouterr().err
