# NOR filesystem

## Introduction

`fs::NorFileSystem` is a log-structured, copy-on-write filesystem for raw NOR flash accessed through `hal::Flash`.
It is designed around three requirements that existing NOR filesystems do not meet together:
the cost of an operation must not depend on the size of the file or on how full the volume is,
wear must be spread evenly even when most of the volume holds data that never changes,
and every operation must be asynchronous.
It also has to survive a power loss at any moment.
This document describes the on-flash format and the algorithms.
Details such as field sizes are settled during the test-driven implementation and recorded here as they are fixed.

## Terms

| Term         | Meaning                                                                                                       |
|--------------|---------------------------------------------------------------------------------------------------------------|
| Sector       | The erase unit of `hal::Flash`, for instance 4 KiB on SPI NOR                                                 |
| Block        | The allocation unit of the filesystem: `sectorsPerBlock` consecutive sectors, erased together                 |
| Program unit | The smallest aligned unit that is programmed at once; every program unit is programmed at most once per erase |
| Node         | A fixed-size piece of an index map, smaller than a block; several nodes share a node block                    |
| Record       | An entry in the journal, protected by a CRC                                                                   |
| Sequence     | A 64-bit counter that increases with every allocation, record and checkpoint, and orders them                 |

The filesystem never programs a program unit twice without erasing it in between.
This allows it to run on internal microcontroller flash with error correction, where a second programming of the same word is not allowed, as well as on SPI and QSPI NOR.

## Layout

| Block type       | Purpose                                                            | Written                      |
|------------------|--------------------------------------------------------------------|------------------------------|
| Superblock       | Format parameters; blocks 0 and 1 hold two identical copies        | Only when formatting         |
| Journal block    | A sequence of records describing changes since the last checkpoint | Appended, record by record   |
| Checkpoint block | A snapshot of the allocation state and of the roots of the maps    | At every checkpoint          |
| Node block       | Nodes of the index maps                                            | At every checkpoint          |
| Data block       | File contents                                                      | When a block of a file fills |

Except for the superblocks, every block can hold any type, and the type of a block changes every time it is erased and reused.
This is what allows wear to be spread over the whole device.

## Block header

Every block except the superblocks starts with a header in two parts, each in its own program units.

| Part | Contents                         | Programmed                            |
|------|----------------------------------|---------------------------------------|
| A    | Magic value, erase count, CRC    | Immediately after the block is erased |
| B    | Block type, sequence, owner, CRC | When the block is allocated           |

The owner identifies what refers to the block: for a data block, the inode and the position of the block in the file; for a node block, the map it belongs to.
The owner is what lets static wear leveling move a block without searching for the reference to it.
Because the two parts are programmed at different times, the header never needs to be reprogrammed.

## Superblocks

Blocks 0 and 1 hold the format parameters: magic value, format version, block size, number of blocks, program unit, node size and CRC.
They are written only by the formatter, so they do not wear, and the second copy protects against a power loss during formatting.
The superblocks do not point to the current state; the current checkpoint is found by the mount scan.

## Maps and nodes

All metadata is kept in maps from a 32-bit key to a fixed-size value, implemented once as a copy-on-write B+-tree of fixed-size nodes:

| Map         | Key                         | Value                     |
|-------------|-----------------------------|---------------------------|
| Inode table | Inode number                | Inode record              |
| Directory   | Hash of the entry name      | Inode number of the entry |
| File data   | Block index within the file | Physical block number     |

The node size is a format parameter, for instance 512 bytes, so a node holds about 120 four-byte entries.
With 4 KiB data blocks, a file data map of depth 1 covers about 480 KiB, depth 2 covers about 56 MiB, and depth 3 covers more than any NOR device; the depth is therefore bounded by 3.
Nodes are cached in RAM.
A modified node is not written back immediately: the change is made durable by a journal record, and the node itself is written at the next checkpoint.

## Files and directories

An inode record holds the inode number, the parent directory, the type, the size, the name, the root of the file data map and an inline area.
A file that fits in the inline area has no data blocks at all.
Names are stored in the inode record, so a directory map only holds hashes and inode numbers; names with the same hash are told apart by reading their inode records.
Hard links are not supported, so every inode has exactly one name and one parent.

## Journal

Every change is first written to the journal as one or more records.
A record holds its length, type, sequence, payload and a CRC, and is padded to a whole number of program units.

| Record          | Payload                                      |
|-----------------|----------------------------------------------|
| Map update      | Map, key and new value, or removal of a key  |
| Inline data     | Inode, offset and the bytes of a small write |
| Block allocated | Block number and purpose                     |
| Block released  | Block number                                 |
| Commit          | Closes a transaction                         |

Records that belong to one operation, for instance a rename that removes one directory entry and adds another, form a transaction that ends with a commit record.
At mount, records are replayed up to the last commit with a valid CRC, and anything after it is ignored.

Small writes are recorded with their data in inline-data records.
An append of 64 bytes followed by `Sync` therefore costs one record of about 96 bytes, independent of the size of the file.
When the inline data of a file fills a block, the block is written to a fresh data block, and a map update record maps it into the file.

The journal is bounded to a configured number of blocks.
When it is full, or when the node cache has no clean node left to evict, a checkpoint is written.

## Checkpoints

A checkpoint makes the journal obsolete.
It writes every modified node to fresh node blocks, then writes a checkpoint block holding:

* the sequence of the checkpoint;
* one bit per block telling whether the block is in use;
* for every node block, which of its nodes are still in use;
* the roots of the inode table;
* the first journal block that follows the checkpoint.

The checkpoint ends with a commit record with a CRC; until that record is complete, the previous checkpoint and its journal remain the valid state.
After the commit, the previous checkpoint, the journal blocks it replaced and the node blocks that no longer hold any used node become free.
The work of a checkpoint is bounded by the size of the node cache and of the checkpoint itself, and does not depend on the size of the files.

## Allocation and the pre-erased pool

At run time, the state of every block is kept in RAM in two bits: in use, free and erased, or free and not yet erased.
The erase count of every block is kept in RAM as one byte, relative to the lowest erase count on the device.

Allocation takes a block from a pool of free, erased blocks, choosing the block with the lowest erase count; this is dynamic wear leveling, and it costs no flash access.
When the event dispatcher is idle, the background task erases free blocks that are not yet erased, lowest erase count first, programs part A of their header and adds them to the pool.
Only if the pool is empty does a foreground write erase a block itself, and then only one.

A reserve of blocks is kept free at all times, enough for a checkpoint and for one step of garbage collection.
A write that would use the reserve fails with `Result::noSpace`.

## Garbage collection

Data blocks belong to exactly one position of one file, so a data block is either fully in use or fully obsolete; reclaiming it only takes an erase, which the background task does.
Journal blocks and checkpoint blocks become obsolete as a whole at the next checkpoint.

Node blocks hold several nodes, and a node block may keep some nodes in use while others are replaced.
To reclaim such a block, the background task loads its remaining nodes into the cache and marks them modified, so that the next checkpoint writes them elsewhere and the whole block becomes obsolete.
Nodes are a small fraction of the data, so this relocation is a small fraction of the flash work.
Live file data is never relocated by garbage collection; only static wear leveling moves it.

## Static wear leveling

When the difference between the highest and the lowest erase count exceeds `staticWearLevelingThreshold`, the background task moves the data of the least-worn block in use to the most-worn free block.
The least-worn block in use usually holds data that never changes; after the move, the data rests on a worn block, and the young block returns to the pool for frequently changing data.
The owner field in the header of the moved block tells which map entry to update, and the update is an ordinary map update record.
At most one block is moved per `staticWearLevelingInterval` erases, so the overhead is bounded by the configuration and measured by WL-4.

## Mount

1. Read both superblocks and use a copy with a valid CRC.
2. Read the header of every block, which gives the erase counts and the location of every checkpoint and journal block.
3. Load the checkpoint with the highest sequence whose commit record is valid.
4. Replay the journal blocks that follow it, in order of sequence, up to the last valid commit record; if the node cache fills up during replay, write a checkpoint.
5. Classify the remaining blocks: blocks with a valid part A and no part B are free and erased; every other block that is not in use is free and not yet erased.
6. Start a new journal block, so that no program unit whose programming may have been interrupted is ever written again.

Step 2 reads a few dozen bytes from every block, so the time to mount grows with the size of the device, and is measured by R-4.
Steps 3 and 4 are bounded by the size of the checkpoint and of the journal.
A block whose part A cannot be read because its erase was interrupted gets the average erase count of the device as an estimate, as UBI does on Linux; this error happens at most once per power loss and per block.

## Power-fail safety

A state change becomes visible only through a committed journal record or a committed checkpoint, both protected by a CRC.
Records and blocks are only ever written into program units that have been erased and not programmed since.
A block is erased only when no committed state refers to it.
The superblocks never change after formatting.

| Interrupted during               | State after the next mount                                                |
|----------------------------------|---------------------------------------------------------------------------|
| Programming a journal record     | The record has an invalid CRC and is ignored with its transaction         |
| Programming a data block         | The block is not referenced by any committed record and is reclaimed      |
| Writing a checkpoint             | The previous checkpoint and its journal are used                          |
| Erasing a block                  | The block has no valid header; it is erased again and its count estimated |
| Programming header part A or B   | As for an interrupted erase, or the block is not in use and is reclaimed  |
| Moving a block for wear leveling | Either the old or the new location is referenced, never neither           |

The power-cut sweep in [Testing.md](Testing.md) interrupts every workload after every single program and erase step, and after a partial program of the interrupted step, and checks after every remount that the volume is consistent and that every file holds either its previous or its new contents.

## Cost of operations

| Operation                       | Flash work                                                                              | Independent of           |
|---------------------------------|-----------------------------------------------------------------------------------------|--------------------------|
| Small append followed by `Sync` | One journal record                                                                      | File size and fill level |
| Append that completes a block   | One block programmed and one journal record                                             | File size and fill level |
| Overwrite within one block      | One block read and programmed, one journal record, at most 3 node reads when not cached | File size and fill level |
| Seek and read                   | At most 3 node reads when not cached, then the data                                     | File size and fill level |
| Allocation                      | None, unless the pre-erased pool is empty                                               | File size and fill level |
| Checkpoint                      | The modified nodes and the checkpoint block                                             | File size                |
| Mount                           | One header read per block, the checkpoint and the journal                               | File size                |

## RAM use

| Item           | Size                                       |
|----------------|--------------------------------------------|
| Block states   | 2 bits per block                           |
| Erase counts   | 1 byte per block                           |
| Node cache     | Number of cached nodes times the node size |
| Journal buffer | One program-unit-aligned buffer            |
| Each open file | Position, inode record and inline area     |

For a 16 MiB device with 4 KiB blocks, that is 4096 blocks, so 1 KiB for the block states and 4 KiB for the erase counts, plus the node cache, for instance 4 nodes of 512 bytes.
Using larger blocks reduces this proportionally: with 64 KiB blocks, the same device needs 320 bytes for block states and erase counts.
This is more RAM than littlefs needs for the same device; it is the price of allocation without traversal and of wear leveling based on actual erase counts.

## Configuration

| Field                         | Meaning                                                                                                             |
|-------------------------------|---------------------------------------------------------------------------------------------------------------------|
| `maxBlocks`                   | The largest number of blocks a volume may span; sizes the RAM tables                                                |
| `sectorsPerBlock`             | Sectors of `hal::Flash` per block                                                                                   |
| `programUnit`                 | Smallest aligned unit programmed at once, for instance 1 for SPI NOR or 16 for internal flash with error correction |
| `nodeSize`, `cachedNodes`     | Size of an index node and number of nodes cached in RAM                                                             |
| `journalBlocks`               | Maximum size of the journal before a checkpoint                                                                     |
| `reserveBlocks`               | Blocks kept free for checkpoints and garbage collection                                                             |
| `staticWearLevelingThreshold` | Erase-count difference that triggers a move                                                                         |
| `staticWearLevelingInterval`  | Minimum number of erases between two moves                                                                          |
| `idleDelay`                   | Time without foreground work before background work starts                                                          |

## Internal flash with error correction

Internal flash with error correction, such as on many STM32 devices, requires writes aligned to its program unit and does not allow a second programming of the same unit, which the format respects.
Reading a program unit whose programming was interrupted can raise an error-correction fault on such devices.
The `hal::Flash` driver for such a device has to report this condition instead of faulting, so that the mount can treat the unit as invalid; this is verified when an internal flash driver is used on target.

## Testing

The NOR filesystem is tested on `hal::FlashStub` from EmIL, which models NOR programming as a bitwise AND and can stop after a number of write steps.
The metrics harness adds wrappers that count operations per block and simulate the timing of a NOR device, and a wrapper that performs partial programs at a power cut.
The metrics W-2 to W-5, R-2, R-4, WL-1 to WL-4 and P-1 cover the behaviour described above.
See [Testing.md](Testing.md) and [Metrics.md](Metrics.md).
