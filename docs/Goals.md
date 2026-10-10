# Goals and non-goals

## Introduction

embedded-filesystem provides filesystems for microcontrollers that run an event-driven, single-threaded application on top of [embedded-infra-lib](https://github.com/embedded-pro/embedded-infra-lib) (EmIL).
It contains two filesystems behind one asynchronous file API: FAT for managed media such as SD cards and eMMC, and a log-structured filesystem for raw NOR flash.
This document states what the library must achieve, what it deliberately leaves out, and how each goal is verified.

## Goals

### 1. Fully asynchronous

Every operation that touches a storage device returns immediately and reports its completion through a callback.
Completions are always delivered through `infra::EventDispatcher`, never from inside the call that started the operation, so a callback may start the next operation without growing the stack.
Nothing in the library blocks, sleeps, busy-waits, or requires an RTOS thread.
A NOR sector erase of tens of milliseconds, or an SD card that stays busy for hundreds of milliseconds, never stalls the rest of the application.

### 2. Heap-less, with RAM sized at compile time

The library never uses `new`, `delete`, `malloc` or `free`, and never uses standard containers that allocate.
The number of volumes, open files, cache sectors, pending operations and journal buffers is chosen at compile time through `WithStorage`-style templates, as in EmIL.
The RAM cost of a configuration is therefore known before the firmware runs, and is reported by the footprint metrics.

### 3. PC compatible on SD cards and eMMC

SD cards and eMMC use FAT12, FAT16 or FAT32 with long file names, so a card written by the device can be read and written by Windows, Linux and macOS.
The formatter follows the alignment rules of the SD Association file system specification, so that the card's internal controller sees the access pattern it was designed for.

### 4. Flat-cost operations on NOR flash

On NOR flash, the cost of an append, an overwrite, a seek and a block allocation does not depend on the size of the file, nor on how full the volume is.
Appends cost a constant amount of flash work, and overwrites and seeks cost at most a fixed number of index levels.
The filesystem never walks the whole volume to find free space during normal operation.

### 5. Dynamic and static wear leveling on NOR flash

New data is written to the least-worn free block (dynamic wear leveling).
Blocks that hold data which never changes are migrated in the background when their erase count falls too far behind the rest of the device (static wear leveling), so that a volume that is mostly full of static data still wears evenly.
SD cards and eMMC level wear inside the card, so the FAT side does not add wear leveling of its own; instead it keeps its writes large, aligned and sequential, and tells the card which clusters are no longer in use.

### 6. Power-fail safety

On NOR flash, every update is made by appending new records and blocks and then committing them with a CRC-protected record, so an interruption at any point leaves either the old or the new state after the next mount.
On FAT, writes are ordered so that a directory entry never refers to clusters that were not written, and the remaining exposure is documented in [Fat.md](Fat.md).

### 7. Bounded latency

Erasing obsolete blocks, refilling a pool of pre-erased blocks and migrating static data happen in the background, while the event dispatcher has nothing else to do.
A fixed reserve of spare blocks guarantees that a foreground write never waits for a large relocation.
When a volume is full, writes fail with `noSpace` instead of becoming slower.

### 8. Every claim is measured

Each performance or wear claim in this repository is tied to a metric with an identifier, a workload and a target, defined in [Metrics.md](Metrics.md).
The metrics run in CI on deterministic device models, against FatFs and littlefs as baselines, and a change that misses a target fails the build.

## Non-goals

The first version does not support USB mass storage, neither as a host reading a USB stick nor as a device exposing its storage to a PC.
It does not support raw NAND flash, which needs bad-block management and error correction in addition to wear leveling.
It does not support exFAT; SDXC cards can be formatted as FAT32.
It does not provide a POSIX or C standard library file API; the API is asynchronous and follows EmIL conventions.
It is not safe to call from several threads or from interrupt handlers; all calls come from the event dispatcher context.
It does not provide encryption or compression.

These may be added later; see [Roadmap.md](Roadmap.md).
