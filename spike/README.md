# Spike: a size-first read-only FAT volume

## Introduction

This spike answers one question of the plan to make embedded-filesystem smaller: can a FAT volume that keeps the rules of [AGENTS.md](../AGENTS.md) stay within twice the flash of FatFs with the same features?
It keeps every rule: every call is asynchronous and completes through `infra::EventDispatcher`, nothing is allocated, operations are classes whose steps are named member functions, the lifecycle is a `services::TableStateMachine`, and handles are `infra::SharedPtr` from fixed pools.
It is written for size: one sector buffer, no caches beyond it, no background work and no writing.
It is not part of the libraries, and builds only with `EMFS_BUILD_SPIKE`.

## Scope

`spike::FatReader` implements `fs::FileSystem` read-only on any `hal::BlockDevice`:

* `Mount` of a whole device or of the first FAT partition of an MBR, FAT12, FAT16 and FAT32, and `Unmount`;
* `Open` in `read` mode, `Read`, `Seek`, `Close`, `OpenDirectory`, `Next` and `Stat`;
* long names, read and matched case-insensitively for ASCII, and short names with the lower-case flags and code page 437;
* `Write`, `Truncate`, `ExpectSize`, `Remove`, `Rename`, `MakeDirectory` and `Open` in another mode complete with `readOnly`, and `Usage` with `unsupported`, as FatFs built read-only has no free-space query.

Its host tests run every behaviour on FAT12, FAT16 and FAT32 volumes made by `fs::FatFormatter` and `fs::FatImageBuilder`.

## Build

```bash
cmake --preset host -DEMFS_BUILD_SPIKE=On
cmake --build --preset host-Debug --target spike.fat_reader_test
build/host/spike/fat_reader/test/Debug/spike.fat_reader_test

cmake --preset cortex-m4 -DEMFS_BUILD_SPIKE=On
cmake --build --preset cortex-m4-MinSizeRel --target spike.footprint.fat_reader validation.footprint
```

`spike.footprint.fat_reader` is the image of F-1 for the spike: the platform of `validation/footprint`, `spike::FatReader::WithStorage<1, 1>` and the calls of `validation::footprint::ExerciseReads`, the same calls as `embedded-filesystem/fat-reads` and `fatfs/read-only`.
It is measured against `validation.footprint.empty`, and its map attributes it with `python3 -m emfs_validation.attribution`.

## Result

With Arm GCC 13.2.1 on the Cortex-M4 in `MinSizeRel`, what each image adds to the empty image:

| Image                           | Flash (B) | RAM (B) | Flash with link-time optimization (B) |
|---------------------------------|-----------|---------|---------------------------------------|
| The spike                       | 18712     | 2220    | 15976                                 |
| `fatfs/read-only`               | 5888      | 1636    | 5432                                  |
| `embedded-filesystem/fat-reads` | 66448     | 7748    | 55640                                 |

The spike takes 3.2 times the flash of `fatfs/read-only`, and 2.9 times with link-time optimization, so it misses the mark of twice; its RAM is within 1.4 times.
It takes 28 % of what the full FAT volume links for the same calls.

| Part of the spike                                                                     | Flash (B) |
|---------------------------------------------------------------------------------------|-----------|
| The volume: calls, path slot, deferred results and lifecycle                          | 6242      |
| Directory scan with long names, UTF-8 and code page 437                               | 2542      |
| File reads                                                                            | 1940      |
| Path walk and the validation of paths and names                                       | 1689      |
| The building blocks of `fs/common` and EmIL: pools, claim queue, deferred completions | 1322      |
| The calls of the application, `ExerciseReads`                                         | 1235      |
| Directory handle                                                                      | 1200      |
| Sector buffer and cluster chain                                                       | 934       |
| Boot sector and partition table                                                       | 638       |
| `strlen`, `memchr` and `memcmp`, which the path helpers bring, and the rest of `main` | 989       |

Across these parts, the callbacks of `infra::Function` take 2261 B and the lifecycle state machine 1488 B; both follow from the rules.
The trims still open, such as path helpers without the C string functions, one entry for the three path calls, and the runtime consistency check of `services::TableStateMachine` that EmIL could drop, add up to about 1 KB, not the 7 KB the mark needs.
