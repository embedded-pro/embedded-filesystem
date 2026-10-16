#!/usr/bin/env python3
"""List the added lines that test a std::optional of a scalar as a bool, where Sonar asks for has_value().

The names of optional scalars are collected from fs/, metrics/ and validation/firmware/: variables, members and parameters
declared as one, and auto variables initialised from a function that returns one. They are then
matched in the lines added since base. A name that is declared with another type elsewhere matches
too, so each hit needs a look.

Usage: tools/find_optional_scalar_tests.py [base], with base origin/main by default.
"""

import argparse
import re
import subprocess
from pathlib import Path

SCALAR = r"(?:u?int\d+_t|std::u?int\d+_t|std::size_t|size_t|char16_t|char32_t|char|bool|int|unsigned|long|float|double)"
OPTIONAL_NAME = re.compile(r"std::optional<" + SCALAR + r">\s*&?\s*(\w+)")
OPTIONAL_FUNCTION = re.compile(r"std::optional<" + SCALAR + r">\s+(\w+)\s*\(")
TEST_CONTEXT = r"(?:\bif \(|\bwhile \(|\(|&&|\|\||\breturn|\?|!|EXPECT_TRUE\(|EXPECT_FALSE\(|ASSERT_TRUE\(|ASSERT_FALSE\(|=)\s*!?\s*"
TEST_END = r"\s*(?:\)|&&|\|\||;|\?)"
HUNK = re.compile(r"\+(\d+)")


def git(*arguments):
    return subprocess.run(["git", *arguments], capture_output=True, text=True, check=True).stdout


def optional_scalar_names():
    names = set()
    for path in git("ls-files", "fs", "metrics", "validation/firmware").split():
        if not path.endswith((".hpp", ".cpp")):
            continue
        source = Path(path).read_text(encoding="utf-8")
        names |= set(OPTIONAL_NAME.findall(source))
        for function in OPTIONAL_FUNCTION.findall(source):
            names |= set(re.findall(r"auto\s+(\w+)\s*=\s*(?:\w+::)*" + function + r"\(", source))
    return names


def tests_as_bool(line, name):
    access = r"(?:[\w.]+(?:\.|->))?" + name
    return re.search(TEST_CONTEXT + access + TEST_END, line) is not None and re.search(access + r"\s*=[^=]", line) is None


def added_lines(base):
    path = None
    number = 0
    for line in git("diff", "-U0", base, "--", "fs", "metrics", "validation/firmware").splitlines():
        if line.startswith("+++ "):
            path = line[6:]
        elif line.startswith("@@"):
            number = int(HUNK.search(line).group(1))
        elif line.startswith("+"):
            yield path, number, line[1:]
            number += 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("base", nargs="?", default="origin/main", help="the commit to compare with")
    arguments = parser.parse_args()

    names = optional_scalar_names()
    for path, number, line in added_lines(arguments.base):
        for name in sorted(names):
            if tests_as_bool(line, name):
                print(f"{path}:{number}: {name}: {line.strip()}")


if __name__ == "__main__":
    main()
