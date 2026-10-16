#!/usr/bin/env python3
"""List the classes and structs of fs/ and validation/firmware that hold a function and declare no copy, move or destructor.

A member infra::Function, or a std::variant of calls, gives them an implicit move that is not
noexcept, which Sonar reports. infra::AutoResetFunction, or a declared noexcept move, avoids it.
Tests and test doubles are skipped, because Sonar does not report it there.

Usage: tools/find_implicit_moves.py [ref], with ref HEAD by default.
"""

import argparse
import re
import subprocess

CLASS = re.compile(r"\n(\s*)(struct|class)\s+(\w+)\b[^;{]*\{")
NESTED_BODY = re.compile(r"\{[^{}]*\}")
FUNCTION_MEMBER = re.compile(r"^\s*infra::Function<[^;]*>\s+\w+\s*(\{[^;]*\})?;", re.MULTILINE)
CALL_VARIANT_MEMBER = re.compile(r"^\s*std::variant<[^;]*Call[^;]*>\s+\w+;", re.MULTILINE)
TEST_DIRECTORY = re.compile(r"/test(_doubles)?/")


def git(*arguments):
    return subprocess.run(["git", *arguments], capture_output=True, text=True, check=True).stdout


def headers(ref):
    names = git("ls-tree", "-r", "--name-only", ref, "fs", "validation/firmware").split()
    return [name for name in names if name.endswith(".hpp") and not TEST_DIRECTORY.search(name)]


def body_after(source, start):
    depth = 1
    end = start
    while depth != 0 and end < len(source):
        depth += {"{": 1, "}": -1}.get(source[end], 0)
        end += 1
    return source[start : end - 1]


def own_members(body):
    for _ in range(2):
        body = NESTED_BODY.sub("{}", body)
    return body


def declares_copy_move_or_destructor(name, members):
    special = r"\b" + name + r"\s*\(\s*(const\s+)?" + name + r"\s*&&?"
    return re.search(special, members) is not None or re.search("~" + name + r"\s*\(", members) is not None


def implicit_moves(ref, header):
    source = git("show", f"{ref}:{header}")
    for match in CLASS.finditer(source):
        kind, name = match.group(2), match.group(3)
        members = own_members(body_after(source, match.end()))
        holds_function = FUNCTION_MEMBER.search(members) or CALL_VARIANT_MEMBER.search(members)
        if holds_function and not declares_copy_move_or_destructor(name, members):
            line = source[: match.start()].count("\n") + 2
            yield f"{header}:{line}: {kind} {name}"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("ref", nargs="?", default="HEAD", help="the commit whose headers to scan")
    arguments = parser.parse_args()

    for header in headers(arguments.ref):
        for finding in implicit_moves(arguments.ref, header):
            print(finding)


if __name__ == "__main__":
    main()
