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
A FAT32 volume needs at least 65525 clusters, so FAT32 tests run on `fs::SparseBlockDevice` instead of a block device that holds every block in RAM.
Test names describe the behaviour, in the form `TEST_F(FatFileSystemTest, append_after_reopen_does_not_walk_cluster_chain)`, and the body is arranged as arrange, act, assert.

## Asynchronous tests

Asynchronous code is tested with `infra::EventDispatcherFixture` or `infra::ClockFixture` from EmIL.
A test starts an operation, runs the event dispatcher with `ExecuteAllActions()`, and checks the completion callback with a `infra::MockCallback`.
Every test also checks that no completion is delivered from inside the call that started the operation, by asserting that the callback has not been called before the event dispatcher runs.
Time-dependent behaviour, such as the idle delay before background work, is tested with `ForwardTime()` of `infra::ClockFixture`.

## Test doubles

| Double                                                     | From                                   | Use                                                                                                                                                                                                                                |
|------------------------------------------------------------|----------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `hal::FlashStub`                                           | EmIL                                   | NOR flash in RAM: programming is a bitwise AND, erase sets 0xff, `stopAfterWriteSteps` stops writing after a number of steps                                                                                                       |
| `hal::BlockDeviceStub`                                     | EmIL                                   | Block device in RAM, with `FailNextOperationWith` to inject errors                                                                                                                                                                 |
| `hal::FlashMock`, `hal::BlockDeviceMock`                   | EmIL                                   | Exact request sequences, where the order of device requests is the behaviour under test                                                                                                                                            |
| `metrics::MeteredFlash`, `metrics::MeteredBlockDevice`     | This repository                        | Counting requests, bytes, erases per block and simulated time                                                                                                                                                                      |
| `metrics::PowerCutFlash`, `metrics::PowerCutBlockDevice`   | This repository                        | Cutting power at a given write step: a NOR program or erase not started, partial or completed; a block unwritten, written or torn, optionally losing every write since the last flush, or every one before the interrupted request |
| `metrics::NorPowerCutSweep`, `metrics::BlockPowerCutSweep` | This repository                        | Running an operation once per cut point and mode, and verifying the image after each cut                                                                                                                                           |
| `fs::SparseBlockDevice`                                    | This repository, `fs/fat/test_doubles` | Block device that stores only blocks that are not all zeros, so that FAT32 volumes of tens of megabytes fit in a few kilobytes                                                                                                     |
| `fs::FatImageBuilder`                                      | This repository, `fs/fat/test_doubles` | Arranging directories, files, fragmented chains and raw entries synchronously on a volume, before the operation under test                                                                                                         |
| `fs::NorImageBuilder`                                      | This repository, `fs/nor/test_doubles` | Arranging directories, files and raw blocks, records and nodes synchronously on a NOR image, before the operation under test                                                                                                       |

## Power-cut sweep

The power-cut sweep runs a workload once to count the number of device write steps it makes, then runs it again once for every step, cutting power after that step.
After each cut, the device is remounted and checked:

* the mount succeeds;
* a consistency check of the on-flash structures passes;
* every file holds either its contents before the interrupted operation or its contents after it.

A write step is one program request or one erased sector on NOR flash, and one written block or one discard request on a block device.
For NOR flash, every cut is repeated with the interrupted step not started, partially done and completed: a partial program programs only the first bytes of the request, and a partial erase leaves the second half of the sector programmed.
For block devices, every cut is repeated with the interrupted block left unwritten, written completely and torn, where only the first bytes of the block hold the new data.
A run with loss of unflushed writes models a device cache that is not written back, and a run that keeps the writes of the interrupted request but loses the earlier unflushed ones models a device cache that writes back in another order.
The result of the sweep is metric P-1 in [Metrics.md](Metrics.md).

The sweep is implemented by `metrics::NorPowerCutSweep` and `metrics::BlockPowerCutSweep` in `metrics/power_cut`.
It takes three functions: one that prepares the device, one that runs the operation under test, and one that verifies the device after a cut and returns the reason of an inconsistency.
Each run starts from the image left by the preparation, in a fresh environment with its own event dispatcher and virtual clock, and each verification runs in another fresh environment, as a device would after a reset.
For large operations, the number of cut points can be limited; the points are then sampled with a fixed stride, so that the same commit always runs the same cuts.

On FAT volumes, the sweep runs in `metrics/fat` on small volumes of each FAT type, and checks after each cut that:

* the library mounts the volume;
* an independent checker, which parses the image without the code of `fs/fat`, finds every link in range, no free or bad cluster in a chain, no cluster in two chains, and every chain at least as long as the size of its file;
* every file holds, sector by sector, either its contents before the interrupted operation or its contents after it;
* a file or directory that was being created exists under its name or not at all, and no other entry appeared in its directory;
* an entry that was being removed exists with its previous contents or not at all, and no other entry disappeared;
* an entry that was being renamed exists under its old name, its new name or both, with its contents.

Chains longer than their file, lost clusters, orphaned long name entries and copies of the FAT that lag behind the first one are allowed after a cut, as described in [Fat.md](Fat.md#behaviour-on-power-loss), and the one exception to clusters in two chains is the old and the new name of an interrupted `Rename`.
The run without a cut must pass the checker in its strict mode, which allows none of these.
A volume that is marked clean after a cut must also pass the strict mode, because a PC does not check it, and the run without a cut of a case that ends with `Unmount` must leave it marked clean.
The checker is itself checked against the images that the interoperability tests write with `mtools`: it must accept them in its strict mode and read back every file they hold.

On NOR volumes, the sweep runs in `metrics/nor` on small device models: one with a program unit of 1 byte, one with a program unit of 16 bytes, and one with two sectors per block.
Every cut is made with the interrupted program or erase not started, partial and completed, and every mode must hold in every cut; a NOR device has no write cache, so there is no loss or reordering of earlier writes to model.
After each cut, the sweep checks that:

* the library mounts the volume;
* an independent checker, which parses the image without the code of `fs/nor`, finds valid superblocks, a checkpoint with a valid CRC, a journal whose records replay without a mismatch, every node of every map with a valid CRC and reachable once, every block that the state uses marked in use with a header of the right type and owner, and no block used twice;
* every block marked in use is used, except node blocks whose nodes were all replaced, which stay in use until garbage collection passes them;
* the checker also finds every free block that holds only its part A erased;
* no program unit was programmed twice since its last erase, as counted by `metrics::MeteredFlash`, which takes every unit that does not read erased when the operation starts as programmed;
* every file holds, byte by byte, either its contents before the interrupted operation or its contents after it;
* a file or directory that was being created or removed exists with its previous contents or not at all, one that was being renamed exists under exactly one of its old and new names with its contents, and no other entry appeared or disappeared.

The cases cover every operation that writes, the start of a new journal block after a mount, checkpoints, the background work of erasing, collecting node blocks and moving blocks, and chains of a cut, a mount, an append and another cut.
`Format` is swept too: after a cut, the mount finds the previous volume as long as its second superblock is not erased, completes with `corrupted`, or finds the new, empty volume with every other block in the pre-erased pool.
Small configurations, with a few cached nodes and a short journal, make the sweep cross journal blocks and checkpoints within a few operations.
Unlike FAT, a NOR volume allows nothing extra after a cut: blocks that an interrupted operation allocated are free, because no committed state refers to them, so the run without a cut and the runs with a cut pass the same checks.

## Interoperability tests for FAT

Interoperability tests check that FAT volumes are exchanged correctly with the tools of a PC:

* an image created by `mkfs.fat` is mounted, written by the library, verified with `fsck.fat -n` and read back with `mtools`;
* files that the library creates are listed by `mtools` under their long names and short aliases;
* directories that the library makes and entries that it removes leave an image that `fsck.fat -n` accepts and `mtools` lists as expected;
* entries that the library renames or moves, with the `..` entry of a moved directory, and files that it truncates leave an image that `fsck.fat -n` accepts and `mtools` lists and reads back as expected;
* files written into an image with `mtools` are read back by the library;
* an image created by `fs::FatFormatter` passes `fsck.fat -n`.

These tests need `dosfstools` and `mtools`, and image files on the host, so they live in `metrics/interop` and are built with the metrics.
They carry the CTest label `interop` and are skipped when the tools are not installed; the metrics workflow installs both.

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
