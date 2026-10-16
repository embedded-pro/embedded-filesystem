# Tools

Scripts that check a change locally before it is pushed, on a Linux host.
They find the repository from their own location, so they run from any directory.

## verify.sh

`tools/verify.sh [base]` runs the checks of a pull request against `base`, `origin/main` by default:

- the host build and tests, with the result of each `fs.*_test`;
- clang-format and cppcheck on the C++ sources changed since `base`;
- the line coverage of `fs/` and of the libraries of `validation/firmware`, listing every file that is not fully covered;
- the cortex-m4, cortex-m7 and cortex-m33 builds, and the four board presets of the validation;
- the reference images of F-1 in `MinSizeRel`, measured with `validation/footprint/measure.sh` into `build/footprint`, with `cortex-memory-budget` in a virtual environment in `build/footprint-venv` that it creates the first time;
- ruff, the unit tests and the scenarios on the fake firmware of the host package of the validation, in a virtual environment in `build/validation-host` that it creates the first time;
- the unit tests of the metrics harness, without the metrics themselves;
- clang-tidy's `bugprone-branch-clone` and the scans below, on the changed sources.

It configures each preset the first time and prints only problems and summaries, so a clean run is short.
The metrics take several minutes more: when a change can move them, run `ctest --preset metrics`, then `metrics/report/merge_report.py --check` with `--baseline` set to the `metrics.json` of `main`, and with `--fragment-directory build/footprint/fragments` for F-1.

It needs CMake, Ninja, GCC, the Arm GNU toolchain, xsltproc, clang-format, clang-tidy, cppcheck, gcovr and Python 3 with `venv`, and dosfstools and mtools for the interoperability tests.

## Scans

The scans look for patterns that CI reported on earlier pull requests although the local compilers accept them.
A hit is a candidate to look at, not necessarily a defect.

| Script                                     | Finds                                                                                                               | Because                                                           |
|--------------------------------------------|---------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------|
| `find_optional_scalar_tests.py [base]`     | A `std::optional` of a scalar tested as a bool, in the lines added since `base`                                     | Sonar asks for `has_value()`                                      |
| `find_implicit_moves.py [ref]`             | A class in `fs/` or `validation/firmware/` that holds an `infra::Function` and declares no copy, move or destructor | Its implicit move is not `noexcept`, which Sonar reports          |
| `find_hidden_template_parameters.py [ref]` | A template parameter of an out-of-line nested class template that has the name of a function                        | MSVC fails with C2975 when a member of a base hides the parameter |
| `find_const_candidates.py <sources>`       | A member function that still compiles when it is declared `const`                                                   | It can be `const`, unless it changes state through a pointer      |

## format_markdown_tables.py

`tools/format_markdown_tables.py <files>` pads every table of the files to the column widths that MegaLinter's markdown-table-formatter expects, for instance after a row grew wider than its table.
