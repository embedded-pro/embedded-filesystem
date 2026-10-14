#!/usr/bin/env python3
"""List the member functions of the given sources that still compile when declared const.

Each candidate is compiled with -fsyntax-only from copies of its source and header in a temporary
overlay, with the flags of build/host/compile_commands.json, so the working tree is never changed.
A function that changes state only through a pointer or a range member compiles as const too, so
every candidate needs judging.

Usage: tools/find_const_candidates.py fs/fat/FatAllocator.cpp ...
"""

import argparse
import json
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COMPILE_COMMANDS = ROOT / "build" / "host" / "compile_commands.json"
DEFINITION = re.compile(r"^    ([\w:<>,\s\*&]+?)\s+(\w+)::(\w+)\(([^)]*)\)\n", re.MULTILINE)
SKIPPED_WITH_VALUE = ("-o", "-MF", "-MT", "-MQ")


def load_commands():
    entries = json.loads(COMPILE_COMMANDS.read_text(encoding="utf-8"))
    return {Path(entry["directory"], entry["file"]).resolve(): entry for entry in entries}


def syntax_check_command(entry, source, overlay):
    arguments = entry.get("arguments") or shlex.split(entry["command"])
    command = [arguments[0], "-I", str(overlay)]
    skip = False
    for argument in arguments[1:]:
        if skip:
            skip = False
        elif argument in SKIPPED_WITH_VALUE:
            skip = True
        elif argument == "-c" or argument.startswith("-M"):
            continue
        elif not argument.startswith("-") and Path(entry["directory"], argument).resolve() == source:
            continue
        else:
            command.append(argument)
    return command + ["-fsyntax-only", str(overlay / source.relative_to(ROOT))]


def compiles(entry, source, header, source_text, header_text):
    with tempfile.TemporaryDirectory() as directory:
        overlay = Path(directory)
        for path, text in ((source, source_text), (header, header_text)):
            copy = overlay / path.relative_to(ROOT)
            copy.parent.mkdir(parents=True, exist_ok=True)
            copy.write_text(text, encoding="utf-8")
        result = subprocess.run(syntax_check_command(entry, source, overlay), cwd=entry["directory"], capture_output=True, check=False)
        return result.returncode == 0


def declaration_of(header_text, name, parameters):
    pattern = r"^(\s+)([\w:<>,\s\*&]+?\s+" + re.escape(name) + r"\(" + re.escape(parameters) + r"\));\s*$"
    declaration = re.search(pattern, header_text, re.MULTILINE)
    if declaration is None or "static" in declaration.group(2) or "virtual" in declaration.group(2):
        return None
    return declaration


def candidates(argument, commands):
    source = (ROOT / argument).resolve()
    header = source.with_suffix(".hpp")
    if not header.exists():
        return
    if source not in commands:
        print(f"{argument}: not in {COMPILE_COMMANDS.relative_to(ROOT)}, skipped", file=sys.stderr)
        return

    source_text = source.read_text(encoding="utf-8")
    header_text = header.read_text(encoding="utf-8")
    for definition in DEFINITION.finditer(source_text):
        owner, name, parameters = definition.group(2), definition.group(3), definition.group(4)
        if name == owner:
            continue
        declaration = declaration_of(header_text, name, parameters)
        if declaration is None:
            continue
        const_source = source_text[: definition.end() - 1] + " const" + source_text[definition.end() - 1 :]
        const_header = header_text[: declaration.end(2)] + " const" + header_text[declaration.end(2) :]
        if compiles(commands[source], source, header, const_source, const_header):
            yield f"{argument}: {owner}::{name}({parameters}) could be const"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sources", nargs="+", help="sources whose member functions to check")
    arguments = parser.parse_args()

    commands = load_commands()
    for source in arguments.sources:
        for candidate in candidates(source, commands):
            print(candidate)


if __name__ == "__main__":
    main()
