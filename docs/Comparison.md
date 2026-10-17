# Comparison with FatFs and littlefs

## Introduction

FatFs and littlefs are the two filesystems most often used on microcontrollers.
FatFs implements FAT and exFAT for block devices such as SD cards, and littlefs implements a power-loss resilient filesystem for raw flash.
Both are mature, portable C libraries, and both are good choices for many products.
This document explains in which aspects embedded-filesystem is designed to do better, why, and how each claim is measured.
It also lists the aspects in which FatFs and littlefs remain the better choice.
Metric identifiers such as W-2 refer to [Metrics.md](Metrics.md).

## Summary

| Aspect                     | FatFs                                                  | littlefs                                                      | embedded-filesystem                                                               |
|----------------------------|--------------------------------------------------------|---------------------------------------------------------------|-----------------------------------------------------------------------------------|
| Execution model            | Blocking `disk_read`/`disk_write` calls                | Blocking `read`/`prog`/`erase` callbacks                      | Asynchronous, completions through `infra::EventDispatcher`, no RTOS thread needed |
| Memory                     | Static objects, optional heap for long file names      | Static buffers or `malloc`                                    | Static only, sized at compile time                                                |
| Free space on NOR          | Not applicable                                         | Walks the filesystem whenever the lookahead window is used up | Block states in RAM, built once at mount; allocation without flash access         |
| Writing inside large files | Cluster chain walked from the start on reopen and seek | A write before the end rewrites every block up to the end     | NOR: index tree, O(depth). FAT: extent cache kept per open file                   |
| Wear leveling              | None, left to the card                                 | Dynamic only                                                  | NOR: dynamic and static. FAT: left to the card                                    |
| Erase latency              | Not applicable                                         | Erases inline, inside the write that needs the block          | Pre-erased pool, refilled in the background                                       |
| Writes to SD cards         | Direct multi-sector transfers for aligned writes       | Not applicable                                                | Write-back cache that coalesces small writes, aligned allocation, contiguous runs |
| Integration with EmIL      | Needs a blocking adapter or a thread                   | Needs a blocking adapter or a thread                          | Uses `hal::BlockDevice`, `hal::Flash`, EmIL containers and test doubles directly  |
| Code size on a Cortex-M4   | About 12 KB of flash                                   | About 15 KB of flash                                          | About 71 KB for FAT and 86 KB for NOR (F-1)                                       |

## Asynchronous execution

FatFs and littlefs call the storage driver and wait for it to finish before they return.
In an EmIL application, where all work runs on one event dispatcher, that leaves two options: run the filesystem in its own RTOS thread with its own stack, or stall the event dispatcher for the duration of every flash operation.
A 4 KiB NOR sector erase typically takes 45 ms and may take up to 400 ms, and an SD card may legally stay busy for 250 ms on a write.
Neither filesystem can be made asynchronous with a thin wrapper, because the state of an operation in progress lives on the call stack of the library.

embedded-filesystem is written as explicit state machines whose state lives in objects, so every operation returns immediately and completes through a callback.
Measured by W-6, which reports the longest time a single call keeps the caller waiting, and by F-2, which reports the processor cycles spent per operation on the target.

## Constant cost on NOR flash

littlefs finds free blocks with a lookahead bitmap.
When the window of the bitmap is used up, littlefs traverses every file and directory to find out which blocks are in use.
The cost of this traversal grows with the amount of data stored, so writes become slower as the volume fills.
Recent versions offer `lfs_fs_gc` to do part of this work ahead of time, but it is still a blocking call whose cost grows with the stored data.

littlefs stores file data as a backwards-linked skip-list (a CTZ skip-list).
Appending to the end is cheap, but writing anywhere before the end rewrites every block from that point up to the end of the file, so the cost of the write grows with the size of the file.

embedded-filesystem keeps the state of every block in RAM, built once at mount and kept up to date by the journal, so allocation never traverses the volume.
File data is reached through a shallow index tree that is updated copy-on-write along a single path, so an overwrite costs the same in a small file and in a large one.
Small appends are programmed in place at the end of the last block of the file and committed by a small journal record, so an append followed by a sync costs a constant amount of flash work.
Measured by W-2 (append cost against file size), W-3 (append cost against fill level), W-5 (overwrite cost against file size) and R-2 (read cost against file size).

## Wear leveling on NOR flash

littlefs uses dynamic wear leveling: new data goes to blocks found by the allocator, and metadata blocks are moved after a configurable number of erases.
Blocks that hold data which never changes are never moved, so on a volume that is mostly full of static data, all wear concentrates on the remaining blocks.

embedded-filesystem allocates the least-worn free block, and in addition migrates the coldest block in the background when the difference between the highest and the lowest erase count exceeds a threshold.
The migration work is bounded per step and rate limited, so its overhead stays small.
Measured by WL-1 and WL-2 (spread of erase counts), WL-3 (data written until the first block reaches its endurance) and WL-4 (overhead of the migrations).

## Erase and garbage collection in the background

littlefs erases a block inside the write operation that needs it, so the write that happens to need a fresh block pays the full erase time.

embedded-filesystem erases obsolete blocks while the event dispatcher is idle and keeps a pool of pre-erased blocks ready.
A reserve of spare blocks guarantees that foreground writes never wait for a large relocation; when the volume is never idle, a write may wait for one static wear leveling move, bounded by the erase of one block.
Measured by W-6.

## Large files on FAT

FatFs follows the cluster chain from the start of the file whenever it needs to reach a position it has not visited, for example after reopening a file to append to it, or after a seek backwards.
For a large file with small clusters this means reading many FAT sectors.
FatFs offers a fast seek mode, but the application has to set it up per file with a table it provides, and the mode does not cover a file that is still growing.
FatFs also offers `f_expand` for contiguous allocation, which the application has to call explicitly.

embedded-filesystem keeps an extent cache for every open file automatically, remembers the last cluster of a file, allocates in contiguous runs aligned to the allocation unit of the card, and uses a write-back sector cache that turns small writes into larger multi-block transfers.
Measured by W-2 and R-2 on FAT, and by M-1 (blocks per write command), M-2 (aligned writes) and M-3 (allocation units touched per MiB written).

## Where FatFs and littlefs remain better

FatFs and littlefs have been used in a very large number of products for many years, and much of their behaviour under real-world conditions is known.
embedded-filesystem is new; its power-fail tests and metrics are designed to close this gap as quickly as possible, but field experience cannot be replaced by tests.

Both are portable C libraries without dependencies, and both can be used synchronously, for instance in a bootloader without an event dispatcher.
embedded-filesystem requires C++20 and EmIL.

FatFs supports exFAT, which embedded-filesystem does not support in its first version.

littlefs mounts quickly, because it reads only its superblock and root directory at mount.
embedded-filesystem reads the header of every block at mount to build its free-block bitmap and erase counts, which takes longer on large devices; this cost is paid once per mount instead of during writes, and is measured by R-4.

littlefs needs less RAM.
A littlefs configuration with caches of a few hundred bytes is common, while embedded-filesystem keeps two bits and one byte per block plus a node cache of 8 KiB by default: the volume takes 17,728 bytes for a 16 MiB device with 4 KiB blocks on a Cortex-M4, as F-1 measures, and about 9 KiB with 64 KiB blocks; see [NorFilesystem.md](NorFilesystem.md#ram-use).
littlefs cannot use as much: its caches hold at most one block and its lookahead one bit per block, 13,120 bytes on the same device with one open file.
This is the price of allocation without traversal and of wear leveling based on actual erase counts.

On SD cards, FatFs needs less RAM as well: 1652 bytes with one open file, against 7712 bytes for `fs::FatFileSystem::WithStorage<1, 8>` on a Cortex-M4.
Most of that is the sector cache that collects small writes into multi-block requests; a smaller cache needs less RAM and coalesces fewer writes.

littlefs stores small files inline in their metadata, which uses flash space efficiently for many tiny files.
embedded-filesystem keeps only files of up to 64 bytes in the record of their inode, and allocates larger files in whole blocks in its first version, so many small files use more space.

FatFs and littlefs need much less code.
F-1 measures what each subject adds to an EmIL application on a Cortex-M4 in `MinSizeRel`, with its glue: 70,968 bytes of flash for `embedded-filesystem/fat` against 11,940 for `fatfs/default`, and 85,752 bytes for `embedded-filesystem/nor` against 15,244 for `littlefs/default`.
The asynchronous state machines, the index trees, the caches and the background work take that code; a product that needs the smallest image keeps using FatFs or littlefs.
A product that makes fewer calls can leave the others out with `fs::FatFeatures` and `fs::NorFeatures`: with every feature left out, F-1 measures 51,492 bytes for FAT, against 71,924 with every feature and the formatter, and 71,892 bytes for NOR, against 85,952, with Arm GCC 13.2.1.
A product that only reads FAT volumes can use `fs::FatReader`, which leaves out every change, the caches and the background work: 22,176 bytes of flash and 2,020 of RAM, 3.8 times the flash and 1.2 times the RAM of `fatfs/read-only`, which needs 5,888 and 1,636, as F-1 measures with Arm GCC 13.2.1.
A product that only creates and writes files can use `fs::FatLite`, which writes with one sector buffer and leaves out removing, renaming and making directories: 35,260 bytes of flash and 2,636 of RAM, 4.7 times the flash and 2.3 times the RAM of `fatfs/minimal`, which needs 7,444 and 1,132 and also removes, renames and makes directories, as F-1 measures with Arm GCC 13.2.1.
A product that links with link-time optimization saves about 15 % of the flash of embedded-filesystem, as F-1 shows on `cortex-m4-lto`, but FatFs gains 11 % too, so the ratio barely changes on FAT.
