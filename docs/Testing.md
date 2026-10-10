# Testing

## Introduction

embedded-filesystem is developed test first.
Every behaviour is specified by a failing test before it is implemented, and every performance or wear claim is specified by a failing metric test before it is optimised.
This document describes the kinds of tests, the test doubles they use, and the conventions they follow.

## Test-driven development

Work proceeds in small increments, each following the same cycle:

1. Red: write a test that describes the next behaviour, and see it fail.
2. Green: write the least code that makes it pass.
3. Refactor: improve the code while all tests stay green.

An increment that adds a write path also extends the power-cut sweep, and an increment that is covered by a metric also adds or tightens the corresponding metric test.

## Conventions

Tests live next to the code they test, in `test/` folders, and test doubles in `test_doubles/` folders, following EmIL:

```text
fs/fat/FatFileSystem.hpp
fs/fat/FatFileSystem.cpp
fs/fat/test/TestFatFileSystem.cpp
fs/fat/test_doubles/...
```

Tests use GoogleTest and GoogleMock.
Mocks are always `testing::StrictMock`, never `testing::NiceMock`.
Tests follow the same rules as production code: no heap allocation, bounded containers, and ranges built with `infra::MakeRange`, `infra::Head` and `infra::DiscardHead`.
Test names describe the behaviour, in the form `TEST_F(FatFileSystemTest, append_after_reopen_does_not_walk_cluster_chain)`, and the body is arranged as arrange, act, assert.

## Asynchronous tests

Asynchronous code is tested with `infra::EventDispatcherFixture` or `infra::ClockFixture` from EmIL.
A test starts an operation, runs the event dispatcher with `ExecuteAllActions()`, and checks the completion callback with a `infra::MockCallback`.
Every test also checks that no completion is delivered from inside the call that started the operation, by asserting that the callback has not been called before the event dispatcher runs.
Time-dependent behaviour, such as the idle delay before background work, is tested with `ForwardTime()` of `infra::ClockFixture`.

## Test doubles

| Double                                                   | From            | Use                                                                                                                          |
|----------------------------------------------------------|-----------------|------------------------------------------------------------------------------------------------------------------------------|
| `hal::FlashStub`                                         | EmIL            | NOR flash in RAM: programming is a bitwise AND, erase sets 0xff, `stopAfterWriteSteps` stops writing after a number of steps |
| `hal::BlockDeviceStub`                                   | EmIL            | Block device in RAM, with `FailNextOperationWith` to inject errors                                                           |
| `hal::FlashMock`, `hal::BlockDeviceMock`                 | EmIL            | Exact request sequences, where the order of device requests is the behaviour under test                                      |
| `metrics::MeteredFlash`, `metrics::MeteredBlockDevice`   | This repository | Counting requests, bytes, erases per block and simulated time                                                                |
| `metrics::PowerCutFlash`, `metrics::PowerCutBlockDevice` | This repository | Cutting power after a given step, with an optional partial program or torn sector                                            |

## Power-cut sweep

The power-cut sweep runs a workload once to count the number of device write steps it makes, then runs it again once for every step, cutting power after that step.
After each cut, the device is remounted and checked:

* the mount succeeds;
* a consistency check of the on-flash structures passes;
* every file holds either its contents before the interrupted operation or its contents after it.

For NOR flash, every cut is also repeated with a partial program of the interrupted step, where only part of the bytes are programmed.
For block devices, every cut is repeated once with the interrupted sector written completely and once with it left unwritten; a separate run with torn sectors measures the exposure described in [Fat.md](Fat.md).
The result of the sweep is metric P-1 in [Metrics.md](Metrics.md).

## Interoperability tests for FAT

Interoperability tests check that FAT volumes are exchanged correctly with the tools of a PC:

* an image created by `mkfs.fat` is mounted, written by the library, verified with `fsck.fat -n` and read back with `mtools`;
* files written into an image with `mtools` are read back by the library;
* an image created by `fs::FatFormatter` passes `fsck.fat -n`.

These tests need `dosfstools` and `mtools`; they carry the CTest label `interop` and are skipped when the tools are not installed.
CI installs both.

## Metric tests

Metric tests are GoogleTest executables with the CTest label `metrics`, built when `EMFS_BUILD_METRICS` is enabled.
Each test runs a workload from [Metrics.md](Metrics.md) on a metered device and asserts the target of the metric.
Like any other test, a metric test is written before the code that makes it pass.

## Running

```bash
cmake --preset host
cmake --build --preset host-Debug
ctest --preset host
```

The coverage preset builds the same tests with coverage instrumentation:

```bash
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage
```
