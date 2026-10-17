# Roadmap

## Introduction

embedded-filesystem is built in phases.
The documentation comes first, so that the goals, the designs and the metrics are agreed before code is written.
After that, every phase is developed test first, one pull request per increment, as described in [Testing.md](Testing.md).

## Phase 1: documentation

The goals, the comparison with FatFs and littlefs, the architecture, the designs of both filesystems, the metrics and the testing approach are written down and reviewed.

## Phase 2: repository scaffolding

The build follows the layout of embedded-infra-lib, hal-st and hal-ti:

* a top-level `CMakeLists.txt` that fetches EmIL with FetchContent, pinned to a commit and updated automatically;
* CMake presets for the host, coverage, Windows, and compile-only Cortex-M4, Cortex-M7 and Cortex-M33 builds;
* workflows for continuous integration, linting, pull request titles, releases, static analysis with SonarCloud and the EmIL version update;
* a development container and editor settings;
* a first target, `fs.interfaces`, with one test, so that every pipeline is green from the start.

## Phase 3: prerequisites in EmIL

`hal::BlockDevice` gains a `Flush` operation, and `EraseBlocks` is documented as a discard, with the test doubles updated accordingly.
`hal::Flash` stays unchanged.

## Phase 4: metrics harness

The metered devices, the device models, the power-cut devices, the workloads, the FatFs and littlefs baselines and the report generator are built before the filesystems, so that the baseline numbers are recorded and every later increment is measured.
This phase also adds the `EMFS_BUILD_METRICS` option, the `metrics` presets and the metrics workflow that comments on pull requests.
It is delivered in four steps:

1. Device models, metered devices and the virtual clock (`metrics/device`).
2. Power-cut devices and the power-cut sweep.
3. The interface every filesystem under test implements, the workloads, the runner, the metric computations and the report (`metrics/workload`, `metrics/report`).
4. The FatFs and littlefs baselines, the metric tests and the metrics workflow.

## Phase 5: shared building blocks

1. The asynchronous file and directory interfaces next to `fs::Result` (`fs/interfaces`), with their mocks.
2. Path parsing and the mapping of block device results onto `fs::Result` (`fs/common`).
3. The per-volume operation queue, deferred completions and handle pools.
4. The sector cache.

CRC-protected records are added to `fs/common` with the NOR journal in phase 7.

## Phase 6: FAT

The design is settled in [Fat.md](Fat.md) first.
The formatter comes early, because every later test and the metrics need formatted volumes, and the power-cut sweep grows with every step that adds a write path.

1. On-disk structures: the boot sector, the partition table, FSINFO, the geometry of a volume and the entries of the FAT.
2. Names: UTF-8, the rules for names, short names and the codecs of directory entries.
3. Formatting with SD alignment, the sparse block device for FAT32 tests, and the first interoperability tests.
4. Direct transfers, discards and the flush between ranks in the sector cache.
5. Mount and unmount, and the lifecycle of a volume.
6. Reading directory entries, with long names.
7. Lookup by path, `Stat` and listing directories.
8. Reading files through cluster chains and the extent cache.
9. Free space: the background scan that counts free clusters, and `Usage`.
10. The independent consistency checker for FAT images.
11. Allocation: the cursor from the FSINFO hint, the cluster after the last one, next fit, and the flush before a link into another FAT sector.
12. Writing, `Sync` and `Close` of existing files, FSINFO at unmount, and the first power-cut sweep.
13. Creating files with long names and short aliases, and `ExpectSize` with allocation-unit-aligned runs.
14. `Remove` with discard and `MakeDirectory`, and the FAT subject of the metrics.
15. `Rename` and `Truncate`.
16. Multi-block write-back in the sector cache (M-1), and the cache of free runs, with which a file opened in `append` mode starts at an allocation unit.
17. The dirty flag, and the sweep with reordered unflushed writes.
18. The targets of embedded-filesystem in the metrics, and P-1.

## Phase 7: NOR

The design is settled in [NorFilesystem.md](NorFilesystem.md) first.
As for FAT, the formatter and an independent checker come early, and every step that adds a write path adds its cases to the power-cut sweep in `metrics/nor`, which holds at 100 % with the interrupted program or erase not started, partial and completed.

1. On-flash structures: the geometry and the checks of `fs::NorConfig`, the superblock, the block header, the node header and the records, with `fs::CrcRecord` in `fs/common`.
2. The formatter, `fs::NorFormatter`, with the codecs of inode records and checkpoints.
3. The independent checker of NOR images, the small device models of the sweep, and the sweep of `Format`, with P-1 as the first target of `metrics::NorTargets`.
4. `fs::NorImageBuilder`, and mount and unmount with the lifecycle of a volume: the scan of the headers, the erase counts, the checkpoint, the journal chain and the states of the blocks.
5. The copy-on-write B+-tree and radix tree over the node cache.
6. Lookup by path, `Stat`, `OpenDirectory` and `Next`.
7. Reading files, through the data map and the inline area.
8. Writing records and checkpoints, with replay through the same path, and `MakeDirectory` as the first operation in the sweep.
9. Creating files and appending in place, with the buffer of a page, the inline area, `Sync` and `Close`.
10. Overwriting by copy-on-write, `Truncate` and `Remove`.
11. `Rename`, `Usage` without the reserve, and the NOR subject of the metrics, reported without targets; it skips L4, L5, L6 on `internal-flash`, and the call latency of L2 and L3 on `nor-qspi-16m`.
12. Background work: the pre-erased pool and its watermark, dynamic wear leveling, the background checkpoint, and the reserve in `Usage`; the results of the NOR subject become diagnostic.
13. Garbage collection of node blocks: a bit per block in RAM for the node blocks, a collection step in every checkpoint while it is due, and in the background; L4 and L5 join the metrics, and L6 on `internal-flash` with blocks of 2 sectors and names of 32 bytes.
14. Static wear leveling of data and node blocks in the background, with its foreground fallback ahead of the next call, the counters of the volume, and WL-4 on L5.
15. The other targets of the NOR subject in `metrics::NorTargets`: the W-6 bound on L2 on `internal-flash`, the charge of erases, the W-2 periods on a second log, the ratio of R-2, and the wear metrics on the lifetime run with a fallback that erases its destination on an empty pool.
    Also the factor and the ratio in `merge_report.py`, `littlefs/ram-matched` with the most RAM littlefs can use, checkpoints that only the calls start, and the results of the NOR subject compared with `main` again.
16. The exhaustive sweep of every operation and background step, and P-1, with the checkpoint that a mount with a smaller cache writes during replay, an unmount that commits a file left open, and the results of the NOR subject and of the sweep in Metrics.md.
17. A collection that is also due when the free blocks beyond the reserve run low, so that a write does not complete with `noSpace` while node blocks hold unused nodes: L2 on `internal-flash` at 90 % full showed 12 such blocks when its second log ran out of space.
    The background writes checkpoints for it, a write whose data block would come from the reserve first writes one, and it compacts only node blocks where fewer than half the slots are live; the second log of L2 holds as much as the first again.

## Phase 8: on target

The filesystems run on two ST boards with the drivers of hal-st, the STM32H757I-EVAL and the STM32H745I-DISCO: NOR flash over QSPI on both and a parallel NOR on the EVAL, through `hal::Flash`, and a microSD card on the EVAL and an eMMC on the DISCO, through `hal::BlockDevice`.
The device models are calibrated against measurements, and C-1 and C-2 check them; F-1 is measured with `cortex-memory-budget` on reference images in CI, and F-2 with the cycle counter on the boards.
The parameters that `fs::FatFormatter` chooses are compared with those of cards formatted by the SD Memory Card Formatter of the SD Association.
The design is settled in [Validation.md](Validation.md) first.
Every image builds in CI, and the boards are run by hand, with their runs committed in `validation/results`.
The drivers of hal-st and the cycle counter of EmIL need changes first, made as pull requests in those repositories, as the prerequisites in EmIL were in phase 3.

1. The design of the validation in [Validation.md](Validation.md), and C-1 and C-2 in [Metrics.md](Metrics.md).
2. The scaffolding: `EMFS_BUILD_VALIDATION` in the presets of [Validation.md](Validation.md#build), hal-st fetched in `validation/` and bumped daily, the four board presets in CI and in `tools/verify.sh`, and `validation/firmware` in the coverage, clang-format and SonarCloud settings.
   The firmware with its board profiles, clocks, caches, memory protection, terminal and the parked image of the Cortex-M4, the host package with the boot scenario, its unit tests and their job in CI, and `validation/README.md` and `validation/PROTOCOL.md`.
3. The result lines of the firmware, the conversion of a transcript into fragments with the run and the model among the parameters of the device and `ramBytes` in every result, and the merge of the committed runs and of fragments made elsewhere into the report of the metrics.
4. F-1: the pins of FatFs and littlefs in `cmake/emfs_third_party.cmake`, the reference images on the Cortex-M presets, and the footprint job of the metrics workflow.
5. The prerequisites in hal-st and EmIL, one pull request each: `hal::QuadSpiStm` completing from interrupts, `hal::NorFlashStm` programming through the write buffer and completing without busy-waiting, the allocation unit and the CID in `hal::SdCardStm`, an eMMC driver, and the unlock of the cycle counter in `hal::cortex::DataWatchpointAndTrace`.
6. F-2: the RAM devices, the cycle counter, `fs::FileSystem::Idle()` and the cycles until the volume is idle, and `cycles.run` for the four subjects, through the glue of FatFs and littlefs that step 4 adds.
7. NOR devices on the boards: the region of the QSPI flash and the parallel NOR with all its address lines, the counting `hal::Flash`, the benchmarks of NOR flash, and `nor-parallel-16m` from its datasheet.
8. The workload runner with its operations, histograms and windows, L1, L2, L3, L4, L6 and L7 on the NOR devices, and C-1 and C-2 from `metrics.device_models`.
9. Buffers aligned for drivers that use DMA: the sectors of the cache and the buffer of the formatter aligned to 32 bytes, and `FatConfig::transferAlignment`, as [Fat.md](Fat.md#direct-transfers) describes.
10. Block devices on the boards: the regions of the microSD card and of the eMMC, the counting `hal::BlockDevice`, the benchmarks of block devices, and the model of the eMMC from its datasheet.
11. The workloads on the FAT volumes of the microSD card and the eMMC.
12. The comparison of `fs::FatFormatter` with the SD Memory Card Formatter: `validation::FatLayout`, `layout.read` and `layout.format`.
13. The runs, one pull request each: the run is committed and summarized in [Metrics.md](Metrics.md), the models are calibrated, the layouts of the cards go to [Fat.md](Fat.md), and once the runs show their spread, C-1 and C-2 get their targets.

## Footprint reduction

F-1 measures embedded-filesystem at several times the flash of FatFs and littlefs.
This plan records where the size comes from and the steps that reduce it, measured with Arm GCC 13.2.1 in `MinSizeRel` on the Cortex-M4.
None of the steps is scheduled yet.

### Where the size comes from

Most of the gap comes from the asynchronous execution, not from the FAT or NOR logic.
FatFs waits in `disk_read()` and keeps the progress of a call on the stack, at no cost: `move_window` and `sync_window`, which load and write back one sector, need 116 bytes.
The same job in `fs::FatLite` needs 1,606 bytes in 36 functions, because every wait needs an object that keeps the state of the operation, a stored `infra::Function` with its invoker and table, and a separate function for the rest of the work.

Six patterns repeat across both filesystems:

1. Every wait splits a function: `fs::FatLite` has 588 functions against 56 in `fatfs/minimal`, with 91 callback invokers and 108 tables, 4.4 KB of plumbing or 15 % of its code.
   Its file handle needs 4.7 KB in 89 functions, against about 2 KB in 9 for `f_read`, `f_write`, `f_lseek`, `f_sync` and `f_close`, and its volume 4.9 KB in 76 functions.
2. Layered pipelines with visitors: a directory is read through a visitor, an assembler and the path walk or the name scan, 4.8 KB in 77 functions against 1.2 KB in 14 for FatFs.
   Every layer keeps its own state, and the same 68-byte entry is copied four times.
3. A `services::TableStateMachine` per lifecycle costs about 1 KB: 1.1 KB in `fs::FatLite`, and 1.9 KB in `fs::NorLite` for its lifecycle and its leveling.
4. Defensive breadth: the handling of media removal costs 0.9 KB, the checks of the boot sector and the geometry 1.5 KB against 0.8 KB for the whole mount of FatFs, and handles check their generation and calls are deferred with `busy`.
   A volume for fixed media would leave out only 130 B of it, because the generation checks of the handles serve unmount as well.
5. Features that `fatfs/minimal` leaves out: reading long names costs `fs::FatLite` 844 B of flash and 216 B of RAM, which `fs::FatReaderFeatures::longNames` leaves out; converting short names to UTF-8 and validating paths stay.
6. The on-flash machinery of NOR makes up 81 % of the code of `fs::NorLite`: 3.0 KB for the B-tree, 1.8 KB for the radix tree, 6.4 KB for the journal and the replay of records, 3.7 KB for checkpoints, 2.9 KB for garbage collection and 2.4 KB for the wear leveler.

### Sharing buffers and callbacks

Buffers are shared already: one sector cache serves the files, the FAT and the directories, `fs::FatLite` uses a single sector for all of them, NOR shares its node cache and its journal buffer, and every operation reuses one scratch slot of its volume.
The large items of NOR are the state of the volume, not scratch space: 4 KB of erase counts, 1 KB of block states and 8 KB of node cache, which shrink by configuration, with larger blocks or fewer cached nodes.
The page buffer of a NOR file cannot go to a pool, because it holds a partial page that is not programmed yet, and NOR flash does not program a page twice.

A pool of callbacks saves no RAM: every handle and path slot can have a call pending at the same time, so a pool for the worst case is as large as today, and a smaller pool would complete calls with `busy`.
One completion slot per owner, instead of one per kind of call, saves 100 to 150 B on `fs::FatLite`, because every owner runs one call at a time: a file holds three slots, the volume five and the sector window three.
Flash is spent per distinct continuation, wherever its callback is stored, so pooling saves no flash; one continuation per operation, with a step field and a `switch` instead of a lambda per wait, saves 15 to 20 B per continuation, 1.5 to 2 KB on `fs::FatLite`.
Together with a single copy of the directory entry, this saves about 300 B of RAM, 11 %, and 1.5 to 2 KB of flash, 5 to 6 %, on `fs::FatLite`.

### Steps

| Step                                                                                                                                                                     | `fs::FatLite`, from 31.2 KB              | Effort           | Confidence        |
|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------|------------------|-------------------|
| A. Scope switches: names read in 8.3 only, done; no media removal for fixed media, left out at 130 B; an enumerated lifecycle in the lite volumes, open                  | 30.4 KB with short names only            | done in part     | measured          |
| B. One continuation per operation, one directory iterator instead of the visitor chain, one copy of the entry                                                            | about 23 to 24 KB                        | one to two weeks | medium            |
| C. A straight-line core: operations written in sequence that suspend at device calls, as C++20 coroutines with a frame arena per volume, or on a small cooperative stack | about 13 to 18 KB, twice FatFs           | weeks            | low until a spike |
| D. NOR only: a simpler format profile, with one kind of index and compaction of the journal instead of checkpoints                                                       | `fs::NorLite`, 60.9 KB today, much lower | months           | low               |

* Only C reaches twice FatFs: it removes patterns 1 to 3 together, with no step functions, no callbacks per step and no operation objects, and local variables instead of stored state, while the event dispatcher still never blocks.
* C and the enumerated lifecycle of A conflict with two rules of [AGENTS.md](../AGENTS.md), operations as classes with named steps and lifecycles as `services::TableStateMachine`, so the rules are decided first.
* The coverage, the power-cut sweep and the interoperability tests check behaviour, not structure, so they carry over to a rewrite.
* NOR needs both C and D to come near littlefs: C removes the 11 % of plumbing and the split steps of NOR, but not its format machinery.
* Step A gave less than its estimate, which counted code that the rest of the volume still needs.
* The first move is a spike of one to two days that rewrites the read path of `fs::FatReader`, its mount, lookup and read, as coroutines, measured against its 19,140 bytes, to decide whether C is worth weeks of work.

## Later

These are outside the first version, and may be added when a product needs them:

* USB mass storage, as a host and as a device; as a device in front of NOR flash, this needs a translation layer with wear leveling between FAT and the flash;
* raw NAND flash, with bad-block management and error correction;
* exFAT;
* packing of small files, to use less space for many tiny files on NOR;
* a hashed lookup structure that keeps lookups in very large directories constant.
