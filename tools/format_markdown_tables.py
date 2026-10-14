#!/usr/bin/env python3
"""Pad every table of the given Markdown files to the column widths that markdown-table-formatter expects.

Cells are split at a | that is neither escaped nor in a code span, wide characters count double,
alignment colons are kept, and fenced code blocks are left alone.

Usage: tools/format_markdown_tables.py docs/Metrics.md ...
"""

import argparse
import re
import unicodedata
from pathlib import Path

SEPARATOR_CELL = re.compile(r"^:?-+:?$")
MINIMUM_WIDTH = 3


def width(text):
    return sum(2 if unicodedata.east_asian_width(character) in "WF" else 1 for character in text)


def split_row(line):
    cells = []
    current = ""
    escaped = False
    code = False
    for character in line.strip()[1:-1]:
        if character == "`" and not escaped:
            code = not code
        if character == "|" and not escaped and not code:
            cells.append(current.strip())
            current = ""
        else:
            current += character
        escaped = character == "\\" and not escaped
    cells.append(current.strip())
    return cells


def is_separator(row):
    return all(SEPARATOR_CELL.match(cell) for cell in row)


def separator_cell(cell, cell_width):
    left = ":" if cell.startswith(":") else "-"
    right = ":" if cell.endswith(":") else "-"
    return left + "-" * cell_width + right


def format_table(lines):
    rows = [split_row(line) for line in lines]
    columns = max(len(row) for row in rows)
    rows = [row + [""] * (columns - len(row)) for row in rows]
    widths = [max([MINIMUM_WIDTH] + [width(row[column]) for row in rows if not is_separator(row)]) for column in range(columns)]

    formatted = []
    for row in rows:
        if is_separator(row):
            formatted.append("|" + "|".join(separator_cell(cell, cell_width) for cell, cell_width in zip(row, widths)) + "|")
        else:
            formatted.append("|" + "|".join(" " + cell + " " * (cell_width - width(cell)) + " " for cell, cell_width in zip(row, widths)) + "|")
    return formatted


def format_text(text):
    output = []
    table = []
    fenced = False
    for line in text.split("\n"):
        if line.startswith("```"):
            fenced = not fenced
        if not fenced and line.startswith("|"):
            table.append(line)
            continue
        if table:
            output.extend(format_table(table))
            table = []
        output.append(line)
    if table:
        output.extend(format_table(table))
    return "\n".join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+", type=Path, help="Markdown files whose tables to pad")
    arguments = parser.parse_args()

    for path in arguments.files:
        text = path.read_text(encoding="utf-8")
        formatted = format_text(text)
        if formatted != text:
            path.write_text(formatted, encoding="utf-8")
            print(f"{path}: tables padded")


if __name__ == "__main__":
    main()
