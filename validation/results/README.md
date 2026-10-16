# Results of the boards

## Introduction

The runs on the boards are committed here, as [docs/Validation.md](../../docs/Validation.md#results) describes:

```text
validation/results/<board>/<run>/
    transcript.log
    junit.xml
    run.json
```

`<board>` is the name of the board file, such as `stm32h757i_eval`, and `<run>` the date of the run, such as `2026_10_20`, with a number for a later run of the day, such as `2026_10_20_2`; other directories are not runs.

## Conversion

The metrics build converts the latest run of every board into a report fragment, and merges it with the other fragments:

```bash
PYTHONPATH=validation/host python3 -m emfs_validation.convert --results validation/results --output-directory build/metrics/validation/fragments
```

`ctest --preset metrics` runs it as `validation.convert_runs`, before `metrics.merge`.
A run that is not committed yet is converted with `--run <directory>` instead of `--results`.
