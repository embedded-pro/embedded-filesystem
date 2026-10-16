"""Attributes what the reference images of F-1 add to the empty image to libraries, objects and kinds of code (docs/Validation.md).

Each image is linked with a map of GNU ld, `validation.footprint.<image>.elf.map` next to the image, and every image is
measured against the empty image of its core, so that a change shows which part of a subject grew or shrank. Only the
standard library is imported, as in emfs_validation.footprint.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

from emfs_validation.footprint import EMPTY, IMAGES

MAP_PATTERN = "validation.footprint.*.elf.map"
MEMORY_MAP = "Linker script and memory map"
SKIPPED = (".debug", ".comment", ".ARM.attributes", ".stab")
RAM_ONLY = (".bss", ".noinit")
RAM_AND_FLASH = (".data",)
FILL = "fill"
MAIN = "main"
IMAGE_LIBRARY = "image"
KINDS = (
    ("callbacks", ("InvokerFunctions",)),
    ("state machines", ("TableStateMachine", "8services9Validated", "TransitionTableAnalysis")),
    ("virtual tables", ("_ZTV",)),
    ("variants", ("__variant",)),
)
OTHER = "other"

OUTPUT_SECTION = re.compile(r"^(\.\S+)")
INPUT_SECTION = re.compile(r"^ (\S+)\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S.*)$")
FILL_SECTION = re.compile(r"^ \*fill\*\s+0x[0-9a-f]+\s+0x([0-9a-f]+)$")
NAME_ONLY = re.compile(r"^ (\S+)$")
CONTINUATION = re.compile(r"^\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S.*)$")
ARCHIVE_MEMBER = re.compile(r"lib([^/()]+)\.a\(([^()]+?)(?:\.cpp|\.c)?\.(?:obj|o)\)$")
OBJECT_FILE = re.compile(r"([^/]+?)(?:\.cpp|\.c|\.s|\.S)?\.(?:obj|o)$")

Sizes = tuple[int, int]


class AttributionError(Exception):
    """The maps cannot be attributed."""


@dataclass(frozen=True)
class Section:
    output: str
    name: str
    size: int
    origin: str


def object_name(origin: str) -> str:
    if origin == FILL:
        return FILL
    member = ARCHIVE_MEMBER.search(origin)
    if member is not None:
        return f"{member.group(1)}/{member.group(2)}"
    found = OBJECT_FILE.search(origin)
    stem = found.group(1) if found is not None else origin
    return MAIN if stem.endswith("Main") else stem


def parse_map(text: str) -> list[Section]:
    start = text.find(MEMORY_MAP)
    if start < 0:
        raise AttributionError("not a map of the linker")
    sections = []
    output = None
    pending = None
    for line in text[start:].splitlines():
        line = line.rstrip()
        if heading := OUTPUT_SECTION.match(line):
            output, pending = heading.group(1), None
        elif output is None or output.startswith(SKIPPED):
            continue
        elif fill := FILL_SECTION.match(line):
            sections.append(Section(output, FILL, int(fill.group(1), 16), FILL))
        elif entry := INPUT_SECTION.match(line):
            sections.append(Section(output, entry.group(1), int(entry.group(2), 16), entry.group(3)))
            pending = None
        elif name := NAME_ONLY.match(line):
            pending = name.group(1)
        elif pending is not None and (continued := CONTINUATION.match(line)):
            sections.append(Section(output, pending, int(continued.group(1), 16), continued.group(2)))
            pending = None
    return sections


def sizes(section: Section) -> Sizes:
    if section.output.startswith(RAM_ONLY):
        return 0, section.size
    if section.output.startswith(RAM_AND_FLASH):
        return section.size, section.size
    return section.size, 0


def by_object(sections: list[Section]) -> dict[str, Sizes]:
    objects: dict[str, Sizes] = {}
    for section in sections:
        flash, ram = sizes(section)
        name = object_name(section.origin)
        known = objects.get(name, (0, 0))
        objects[name] = (known[0] + flash, known[1] + ram)
    return objects


def kind(section: Section) -> str:
    for name, patterns in KINDS:
        if any(pattern in section.name for pattern in patterns):
            return name
    return OTHER


def by_kind(sections: list[Section]) -> dict[str, int]:
    kinds: dict[str, int] = {}
    for section in sections:
        flash, _ = sizes(section)
        if flash != 0:
            kinds[kind(section)] = kinds.get(kind(section), 0) + flash
    return kinds


def difference(image: dict[str, Sizes], empty: dict[str, Sizes]) -> dict[str, Sizes]:
    added = {}
    for name in image.keys() | empty.keys():
        flash, ram = image.get(name, (0, 0))
        empty_flash, empty_ram = empty.get(name, (0, 0))
        if (flash, ram) != (empty_flash, empty_ram):
            added[name] = (flash - empty_flash, ram - empty_ram)
    return added


def by_library(objects: dict[str, Sizes]) -> dict[str, Sizes]:
    libraries: dict[str, Sizes] = {}
    for name, (flash, ram) in objects.items():
        library = name.split("/", 1)[0] if "/" in name else IMAGE_LIBRARY
        known = libraries.get(library, (0, 0))
        libraries[library] = (known[0] + flash, known[1] + ram)
    return libraries


def ordered(rows: dict[str, Sizes]) -> list[tuple[str, Sizes]]:
    return sorted(rows.items(), key=lambda row: (-row[1][0], -row[1][1], row[0]))


def table(heading: str, rows: dict[str, Sizes]) -> list[str]:
    lines = [f"| {heading} | Flash (B) | RAM (B) |", "|---|---:|---:|"]
    lines.extend(f"| {name} | {flash} | {ram} |" for name, (flash, ram) in ordered(rows))
    return lines


def kind_difference(image: dict[str, int], empty: dict[str, int]) -> dict[str, int]:
    changes = {name: image.get(name, 0) - empty.get(name, 0) for name in image.keys() | empty.keys()}
    return {name: change for name, change in changes.items() if change != 0}


def kind_table(kinds: dict[str, int]) -> list[str]:
    lines = ["| Kind of code | Flash (B) |", "|---|---:|"]
    lines.extend(f"| {name} | {flash} |" for name, flash in sorted(kinds.items(), key=lambda row: (-row[1], row[0])))
    return lines


def image_section(image: str, sections: list[Section], empty: list[Section]) -> list[str]:
    added = difference(by_object(sections), by_object(empty))
    flash = sum(value[0] for value in added.values())
    ram = sum(value[1] for value in added.values())
    lines = [f"## {IMAGES.get(image, image)}: {flash} B of flash, {ram} B of RAM", ""]
    lines.extend(table("Library", by_library(added)))
    lines.append("")
    lines.extend(kind_table(kind_difference(by_kind(sections), by_kind(empty))))
    lines.append("")
    lines.extend(table("Object", added))
    lines.append("")
    return lines


def read_maps(directory: Path, core: str) -> dict[str, list[Section]]:
    maps = {}
    for path in sorted(directory.glob(MAP_PATTERN)):
        image = path.name.removeprefix("validation.footprint.").removesuffix(".elf.map")
        try:
            maps[image] = parse_map(path.read_text(encoding="utf-8"))
        except OSError as error:
            raise AttributionError(f"{core}: cannot read {path}: {error}") from None
        except AttributionError as error:
            raise AttributionError(f"{core}: {path.name} is {error}") from None
    if EMPTY not in maps:
        raise AttributionError(f"{core}: no map of the empty image in {directory}")
    return maps


def report(directory: Path, core: str) -> str:
    maps = read_maps(directory, core)
    lines = [
        f"# Attribution of F-1 on {core}",
        "",
        "What each image adds to the empty image, by library, by kind of code and by object, from the maps of the linker.",
        "The fill that aligns the sections is the object `fill`, and the `main` of every image is the object `main`.",
        "",
    ]
    order = list(IMAGES)
    for image in sorted(maps.keys() - {EMPTY}, key=lambda name: (order.index(name) if name in IMAGES else len(order), name)):
        lines.extend(image_section(image, maps[image], maps[EMPTY]))
    return "\n".join(lines)


def parse_arguments(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python3 -m emfs_validation.attribution", description=__doc__.splitlines()[0])
    parser.add_argument("--maps", type=Path, required=True, help="the directory with the maps of the images of one core")
    parser.add_argument("--core", required=True, help="the core the images were built for, such as cortex-m4")
    parser.add_argument("--output", type=Path, required=True, help="the Markdown report to write")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    try:
        text = report(arguments.maps, arguments.core)
    except AttributionError as error:
        print(f"attribution: {error}", file=sys.stderr)
        return 2
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(text, encoding="utf-8")
    images = text.count("\n## ")
    print(f"{arguments.core}: {images} image{'s' if images != 1 else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
