#!/usr/bin/env python3
"""List the out-of-line nested class templates whose template parameter has the name of a function.

MSVC applies [temp.local]: in a class template that derives from a class with a member of the same
name as a template parameter, the member hides the parameter, and MSVC fails with C2975 where GCC
and Clang compile. A parameter named after anything called in the same file is reported.

Usage: tools/find_hidden_template_parameters.py [ref], with ref HEAD by default.
"""

import argparse
import re
import subprocess

NESTED_TEMPLATE = re.compile(r"template<([^>]*)>\s*class\s+(\w+)::(\w+)\s*:([^{]*)\{")
PARAMETER = re.compile(r"(?:std::size_t|std::uint\w+|uint\w+|class|typename|auto|\w+)\s+(\w+)(?:\s*=\s*[^,]+)?(?:,|$)")
CALLED_NAME = re.compile(r"\b(\w+)\s*\(")


def git(*arguments):
    return subprocess.run(["git", *arguments], capture_output=True, text=True, check=True).stdout


def sources(ref):
    names = git("ls-tree", "-r", "--name-only", ref, "fs", "metrics", "validation/firmware").split()
    return [name for name in names if name.endswith((".hpp", ".cpp"))]


def hidden_parameters(ref, path):
    source = git("show", f"{ref}:{path}")
    called = set(CALLED_NAME.findall(source))
    for template in NESTED_TEMPLATE.finditer(source):
        outer, inner = template.group(2), template.group(3)
        for parameter in PARAMETER.findall(template.group(1)):
            if parameter in called:
                yield f"{path}: {outer}::{inner}: template parameter {parameter} has the name of a function"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("ref", nargs="?", default="HEAD", help="the commit whose sources to scan")
    arguments = parser.parse_args()

    for path in sources(arguments.ref):
        for finding in hidden_parameters(arguments.ref, path):
            print(finding)


if __name__ == "__main__":
    main()
