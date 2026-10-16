#!/usr/bin/env bash
# Runs the local checks of a pull request: tools/verify.sh [base], with base origin/main by default.
set -u
cd "$(dirname "$0")/.."

base=${1:-origin/main}
from=$(git merge-base "$base" HEAD)
jobs=$(nproc)

heading() { printf '\n== %s\n' "$1"; }
configure() { [ -d "build/$1" ] || cmake --preset "$1" > /dev/null; }
changed() { git diff --name-only --diff-filter=d "$from" -- "$@"; }

heading host
configure host
cmake --build --preset host-Debug -j "$jobs" 2>&1 | grep -E 'error|warning: ' | head
ctest --preset host 2>&1 | grep -E 'tests passed|Failed'
for test in build/host/fs/*/test/Debug/fs.*_test build/host/validation/firmware/*/test/Debug/validation.*_test; do
    echo "$(basename "$test"): $("$test" --gtest_brief=1 2>&1 | tail -1)"
done

heading "changed sources"
mapfile -t sources < <(changed '*.cpp' '*.hpp')
if [ "${#sources[@]}" -eq 0 ]; then
    echo "no C++ source changed since $base"
else
    clang-format --dry-run --Werror "${sources[@]}" && echo "clang-format: clean"
    cppcheck --quiet --error-exitcode=1 --inline-suppr --std=c++20 --language=c++ "${sources[@]}" && echo "cppcheck: clean"
fi

heading "coverage of fs/ and validation/firmware/"
configure coverage
find build/coverage \( -path '*fs*' -o -path '*validation*' \) \( -name '*.gcda' -o -name '*.gcno' -o -name '*.o' \) -delete
cmake --build --preset coverage -j "$jobs" 2>&1 | grep -E 'error:|FAILED:' | head -3
ctest --preset coverage 2>&1 | grep -E 'tests passed|Failed'
gcovr --root . --filter 'fs/' --filter 'validation/firmware/' --exclude 'validation/firmware/boards/' --exclude '.*/test/.*' --exclude '.*/test_doubles/.*' --json-summary - build/coverage 2>/dev/null | python3 -c '
import json, sys
summary = json.load(sys.stdin)
for file in summary["files"]:
    if file["line_covered"] != file["line_total"]:
        print("%s: %d of %d lines" % (file["filename"], file["line_covered"], file["line_total"]))
print("lines covered: %d of %d" % (summary["line_covered"], summary["line_total"]))'

heading "cortex builds"
for core in cortex-m4 cortex-m7 cortex-m33; do
    configure "$core"
    echo "$core: $(cmake --build --preset "$core-RelWithDebInfo" -j "$jobs" 2>&1 | grep -cE 'error:|FAILED:') errors"
done

heading "board builds"
for preset in stm32h757-cm7 stm32h757-cm4 stm32h745-cm7 stm32h745-cm4; do
    configure "$preset"
    echo "$preset: $(cmake --build --preset "$preset-RelWithDebInfo" -j "$jobs" 2>&1 | grep -cE 'error:|FAILED:') errors"
done

heading "footprint"
for core in m4 m7 m33; do
    echo "cortex-$core MinSizeRel images: $(cmake --build --preset "cortex-$core-MinSizeRel" --target validation.footprint -j "$jobs" 2>&1 | grep -cE 'error:|FAILED:') errors"
done
budget=build/footprint-venv
stamp=$(sha256sum validation/footprint/requirements.txt | cut -d' ' -f1)
if [ "$(cat "$budget/installed" 2>/dev/null)" != "$stamp" ]; then
    { [ -x "$budget/bin/python" ] || python3 -m venv "$budget"; } && "$budget/bin/pip" install --quiet --require-hashes -r validation/footprint/requirements.txt > /dev/null && echo "$stamp" > "$budget/installed"
fi
PATH="$PWD/$budget/bin:$PATH" validation/footprint/measure.sh build/footprint

heading "validation host package"
venv=build/validation-host
stamp=$(sha256sum validation/host/pyproject.toml | cut -d' ' -f1)
if [ "$(cat "$venv/installed" 2>/dev/null)" != "$stamp" ]; then
    { [ -x "$venv/bin/python" ] || python3 -m venv "$venv"; } && "$venv/bin/pip" install --quiet "./validation/host[dev]" > /dev/null && echo "$stamp" > "$venv/installed"
fi
(cd validation/host && "../../$venv/bin/ruff" check --quiet . && "../../$venv/bin/ruff" format --check --quiet . && echo "ruff: clean")
(cd validation/host && "../../$venv/bin/python" -m pytest tests/unit -q -p no:cacheprovider 2>&1 | tail -1)
for board in stm32h757i_eval stm32h745i_disco; do
    echo "$board on the fake firmware: $(cd validation/host && "../../$venv/bin/python" -m pytest tests/hil -q -p no:cacheprovider --fake --no-ad3 --board "$board" 2>&1 | tail -1)"
done

heading "metrics harness"
configure metrics
cmake --build --preset metrics -j "$jobs" 2>&1 | grep -E 'error:|FAILED:' | head -3
ctest --preset metrics -LE metrics 2>&1 | grep -E 'tests passed|Failed|\*\*\*'

heading scans
mapfile -t library < <(changed 'fs/*.cpp' 'validation/firmware/*.cpp' | grep -vE '/test(_doubles)?/|validation/firmware/boards/')
if [ "${#library[@]}" -ne 0 ]; then
    clang-tidy -p build/coverage --quiet --checks='-*,bugprone-branch-clone' "${library[@]}" 2>/dev/null | grep 'warning:'
    timeout 1800 python3 tools/find_const_candidates.py "${library[@]}"
fi
git diff -U0 "$from" -- fs ':!fs/**/test/**' ':!fs/**/test_doubles/**' | grep -E '^\+.*if \(auto\*? \w+ = [^;]*\)$' | sed 's/^+ */if with an initializer, check that it does not test an optional scalar: /'
python3 tools/find_optional_scalar_tests.py "$from"
python3 tools/find_hidden_template_parameters.py HEAD
python3 tools/find_implicit_moves.py HEAD
