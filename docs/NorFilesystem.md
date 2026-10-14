# NOR filesystem

## Introduction

`fs::NorFileSystem` is a log-structured, copy-on-write filesystem for raw NOR flash accessed through `hal::Flash`.
It is designed around three requirements that existing NOR filesystems do not meet together:
the cost of an operation must not depend on the size of the file or on how full the volume is,
wear must be spread evenly even when most of the volume holds data that never changes,
and every operation must be asynchronous.
It also has to survive a power loss at any moment.
This document describes the on-flash format and the algorithms.

## Terms

| Term            | Meaning                                                                                                       |
|-----------------|---------------------------------------------------------------------------------------------------------------|
| Sector          | The erase unit of `hal::Flash`, for instance 4 KiB on SPI NOR                                                 |
| Block           | The allocation unit of the filesystem: `sectorsPerBlock` consecutive sectors                                  |
| Program unit    | The smallest aligned unit that is programmed at once; every program unit is programmed at most once per erase |
| Page            | The unit in which the device programs fastest, for instance 256 B on SPI NOR; programs are aligned to pages   |
| Payload         | The bytes of a block after its header                                                                         |
| Node            | A fixed-size piece of a map, smaller than a block; several nodes share a node block                           |
| Record          | One transaction in the journal, protected by a CRC                                                            |
| Epoch           | The journal that follows one checkpoint                                                                       |
| Sequence        | A 64-bit counter that increases with every block that is allocated, and orders blocks and checkpoints         |
| Pre-erased pool | The free blocks that are erased and carry part A of their header, ready to be allocated without waiting       |
| Inline area     | A fixed-size area in the record of an inode that holds the end of a file that is not in a data block          |

The filesystem never programs a program unit twice without erasing it in between.
This allows it to run on internal microcontroller flash with error correction, where a second programming of the same word is not allowed, as well as on SPI and QSPI NOR.

## Layout

A volume spans the whole device.
Every sector of the device has the same size; a device with sectors of several sizes completes `Format` and `Mount` with `unsupported`.
The device is divided into blocks of `sectorsPerBlock` sectors, and sectors after the last whole block are not used.

| Block type       | Purpose                                                                             | Written                                    |
|------------------|-------------------------------------------------------------------------------------|--------------------------------------------|
| Superblock       | Format parameters; blocks 0 and 1 hold two identical copies                         | Only when formatting                       |
| Checkpoint block | A snapshot of the allocation state and of the root of the inode table, then records | At every checkpoint, then record by record |
| Journal block    | Records that follow the checkpoint block when it is full                            | Record by record                           |
| Node block       | Nodes of the maps                                                                   | At every checkpoint                        |
| Data block       | File contents                                                                       | When a file is written                     |

Except for the superblocks, every block can hold any type, and the type of a block changes every time it is erased and reused.
This is what allows wear to be spread over the whole device.

Integers are stored in little-endian order, and every CRC is `infra::Crc32`.
In this document, rounding a size up to the program unit is written as `Round(size)`.

## Superblocks

Blocks 0 and 1 hold the same superblock at offset 0, written only by `Format`, so they do not wear:

| Offset | Size | Field                            |
|--------|------|----------------------------------|
| 0      | 4    | Magic value `EFSN`               |
| 4      | 2    | Format version, 1                |
| 6      | 2    | Sectors per block                |
| 8      | 4    | Sector size in bytes             |
| 12     | 4    | Number of blocks                 |
| 16     | 2    | Program unit in bytes            |
| 18     | 2    | Node size in bytes               |
| 20     | 2    | Size of the inline area in bytes |
| 22     | 1    | Longest name in bytes            |
| 23     | 1    | Reserved, 0                      |
| 24     | 4    | CRC of bytes 0 to 23             |

The second copy protects against a power loss during formatting and against a superblock that can no longer be read.
The superblocks do not point to the current state; the current checkpoint is found by the mount scan.

## Block header

Every block except the superblocks starts with a header in two parts, each in its own program units.

| Part | Offset      | Size        | Contents                                                                                               | Programmed                                         |
|------|-------------|-------------|--------------------------------------------------------------------------------------------------------|----------------------------------------------------|
| A    | 0           | `Round(12)` | Magic value `EFSB`, erase count (4 bytes), CRC                                                         | Right after the last sector of the block is erased |
| B    | `Round(12)` | `Round(24)` | Type (1 byte), 3 reserved bytes, sequence (8 bytes), owner inode (4 bytes), owner index (4 bytes), CRC | When the block is allocated, before its contents   |

The header takes 36 bytes with a program unit of 1 to 4 bytes, 40 with 8 and 48 with 16, and the payload is the rest of the block: 4060 bytes of a 4 KiB block on SPI NOR, and 2000 bytes of a 2 KiB sector of internal flash.
The types are 1 for a checkpoint block, 2 for a journal block, 3 for a node block and 4 for a data block.
The owner of a data block is the inode and the index of the block in the file, which is what lets static wear leveling move a block without searching for the reference to it; for other types the owner is 0.
Because the two parts are programmed at different times, the header never needs to be reprogrammed.

A block is free and erased when part A is valid and the bytes of part B all read 0xff.
Part A is programmed only after the last sector of the block has been erased, and part B before anything else is programmed into the block, so such a block holds nothing but its part A.
Every other block that the current state does not use is free and not erased, including blocks with a valid part B from an earlier use and blocks whose erase or part A was interrupted.

## Records

A record is one transaction: the changes of one operation, applied together or not at all.
Records follow each other in the checkpoint block of an epoch and in its journal blocks, each starting at a program unit boundary:

| Offset | Size | Field                                          |
|--------|------|------------------------------------------------|
| 0      | 2    | Length of the record, from offset 0 to its CRC |
| 2      | 1    | Number of entries                              |
| 3      | 1    | Reserved, 0                                    |
| 4      | n    | Entries                                        |
| 4 + n  | 4    | CRC of bytes 0 to 3 + n                        |

A record is programmed as `Round(length)` bytes, padded with 0xff, and its CRC commits it.
A length of 0xffff, which is erased flash, ends the records of a block, and so does a record whose length or CRC is not valid.
`fs::CrcRecord` in `fs/common` frames and checks records; the entries are defined by the NOR filesystem.

Each entry starts with its kind:

| Kind | Entry                  | Fields after the kind                                         | Applying it                                                                                            |
|------|------------------------|---------------------------------------------------------------|--------------------------------------------------------------------------------------------------------|
| 1    | Link                   | Previous block (4), end of its records (4)                    | Names the block that this journal block follows; only first in the first record of a journal block     |
| 2    | Inode put              | Inode number (4), inode record                                | Creates or replaces the record of the inode in the inode table                                         |
| 3    | Inode remove           | Inode number (4)                                              | Removes the inode, releases every block of its data map and its nodes                                  |
| 4    | Directory entry put    | Directory inode (4), key (4), inode number (4)                | Adds the entry to the map of the directory                                                             |
| 5    | Directory entry remove | Directory inode (4), key (4)                                  | Removes the entry from the map of the directory                                                        |
| 6    | Block set              | Inode (4), block index (4), previous block (4), new block (4) | Maps the index to the new block, which becomes used, and releases the previous block; 0 means none     |
| 7    | Size                   | Inode (4), size (4), inline length (2), inline bytes          | Sets the size of the file and the contents of its inline area, and releases the blocks beyond the size |

An operation writes one record with every entry it needs: creating a file puts its directory entry and its inode, `Rename` removes one entry, puts another and puts the inode with its new parent and name, and an overwrite that spans two blocks sets both blocks and the size.
Replay checks that the previous block of a block set is the one the map holds, and a mismatch makes the mount complete with `corrupted`.
Allocation and release need no entries of their own: they follow from block sets, from sizes and from removed inodes, and the nodes of a removed data map are found by walking it, at run time and at replay alike.
The largest record is a `Rename`, `8 + 9 + 13 + 5 + inode record` bytes; the journal buffer of the volume holds it, rounded to the program unit.

## Journal

Every change is first committed as a record.
The records of an epoch start in its checkpoint block, right after the checkpoint, and continue in journal blocks.
A new journal block is started when the record does not fit in the current block, and for the first record after every mount, so that no program unit whose programming may have been interrupted before the mount is programmed again.
The first record of a journal block starts with a link entry, which names the block before it and the offset where the records of that block ended.

Replay starts after the checkpoint and reads records up to the end of the block, an erased length or a record that is not valid.
The next block is the journal block with the lowest sequence above that of the current block whose link names the current block and the offset where its records ended; journal blocks that do not qualify, such as one whose link was not written before a power loss, are skipped.
Replay ends when no block qualifies.
So a record that was cut by a power loss is ignored, and the records written after the next mount go into a new journal block that follows the last valid record.

Run time and replay apply records through the same function, so the state after a mount is the state that the records produced at run time, and replay needs no more of the node cache than run time did.
An epoch holds at most `journalBlocks` blocks, its checkpoint block included; a record that would need one more first makes a checkpoint.

## Maps and nodes

Metadata is kept in maps, each made of fixed-size nodes:

| Map         | Structure  | Key                                          | Value                     | Root in                    |
|-------------|------------|----------------------------------------------|---------------------------|----------------------------|
| Inode table | B+-tree    | Inode number                                 | Inode record              | The checkpoint             |
| Directory   | B+-tree    | Hash of the name, shifted by 8, and an index | Inode number of the entry | The inode of the directory |
| File data   | Radix tree | Index of the block in the file               | Block number              | The inode of the file      |

A node starts with a header of 12 bytes, holds its entries, and ends with a CRC of the rest of the node:

| Offset | Size | Field                                                                     |
|--------|------|---------------------------------------------------------------------------|
| 0      | 1    | Map: 1 for the inode table, 2 for a directory, 3 for file data            |
| 1      | 1    | Level, 0 for a leaf                                                       |
| 2      | 2    | Number of entries                                                         |
| 4      | 4    | Owner inode, 0 for the inode table                                        |
| 8      | 4    | Lowest key of the node; for a radix node, the first block index it covers |

With the default node size of 512 bytes, a node has 496 bytes for entries: a node of a radix tree holds 124 block numbers or child addresses, an inner node of a B+-tree 62 keys with their child addresses, a directory leaf 62 keys with their inode numbers, and an inode table leaf 3 inode records with their keys.
A node is addressed by its address on the device, and 0 means no node.
Node slots start at the header rounded up to the page, so that a node is programmed in whole pages: a 4 KiB block on SPI NOR holds 7 nodes, and a 2 KiB sector of internal flash 3.

A radix tree of height h covers `124^h` blocks: height 1 covers about 490 KiB of a file on SPI NOR, height 2 about 59 MiB, and height 3 more than any NOR device.
A B+-tree has at most 3 levels: the inode table then holds about 11 000 inodes and a directory about 238 000 entries.
An insertion that would need a fourth level completes with `noSpace`.
Removing entries never merges nodes: a node that becomes empty is removed, and a root with one child is replaced by that child.
The walks over a map use a stack of at most 3 nodes and no recursion.

Nodes are cached in RAM, `cachedNodes` at a time.
A modified node is not written back immediately: the change is made durable by its record, and the node itself is written at the next checkpoint, at a new address.
A modified node stays in the cache until then.
Before an operation changes anything, it walks the maps it changes without modifying them and counts the nodes it would modify for the first time; when they do not fit next to the nodes already modified, a checkpoint comes first.
The largest operation, `Rename` between two directories, modifies at most 11 nodes: 3 to remove the old entry, 5 to insert the new one with two splits, and 3 for the inode record.
The cache needs at least 4 more nodes for the root updates of a checkpoint and for reading, so `cachedNodes` must be at least 15, which `static_assert` checks.

## Inodes and directories

An inode record has a fixed size of `20 + inlineBytes + maxNameBytes` bytes, 148 with the defaults:

| Offset           | Size           | Field                                                       |
|------------------|----------------|-------------------------------------------------------------|
| 0                | 4              | Parent directory                                            |
| 4                | 1              | Type: 1 for a file, 2 for a directory                       |
| 5                | 1              | Length of the name                                          |
| 6                | 1              | Height of the map, 0 when it is empty                       |
| 7                | 1              | Reserved, 0                                                 |
| 8                | 4              | Size of a file, 0 for a directory                           |
| 12               | 4              | Root of the data map of a file or of the map of a directory |
| 16               | 2              | Number of bytes in the inline area                          |
| 18               | 2              | Reserved, 0                                                 |
| 20               | `inlineBytes`  | Inline area                                                 |
| 20 + inlineBytes | `maxNameBytes` | Name, in UTF-8                                              |

The root directory is inode 1, with an empty name.
Inode numbers are given out in increasing order from the counter in the checkpoint, and never reused; when the counter would wrap, creating completes with `noSpace`.
Hard links are not supported, so every inode has exactly one name and one parent.

The key of a directory entry is the low 24 bits of the CRC of its name, shifted left by 8, plus an index from 0 to 255 that tells apart names with the same hash.
Lookup reads the entries of the directory whose keys share the hash and compares the name of each one's inode record byte by byte.
Creating an entry takes the lowest index that its hash does not use; when all 256 are used, it completes with `noSpace`.
`Next` returns the entries of a directory in the order of their keys, and continues after the key it returned last, so entries that are added or removed meanwhile do not disturb it.

A name is at most `maxNameBytes` bytes, 64 by default, and a longer one completes with `nameTooLong`; a path is at most `maxPathBytes` bytes.
Names are compared byte by byte.
NOR volumes store no timestamps.

## Files

The contents of a file are divided into logical blocks of the payload size: logical block i holds the bytes from `i × payload`.
Each logical block is either a data block, mapped by the data map, or the inline area.

### Inline area

The inline area of an inode holds the bytes of the last logical block that are not in a data block:

* all of them, when the last logical block has no data block and fits the inline area, 64 bytes by default;
* otherwise the bytes after the last whole program unit of the last logical block, fewer than one program unit.

The inline area is at least as large as the block header, so a file of 4096 bytes needs one data block of 4060 bytes and 36 bytes in its inline area, and a file that fits the inline area needs no data block at all.
When the end of the file grows beyond the inline area, a data block is allocated, and the bytes of the inline area are programmed into it with the new ones.

### Appending

An append is programmed in place, into the erased program units of the data block of the last logical block, and the record that commits it holds only a size entry with the bytes of the inline area.
An append of 64 bytes followed by `Sync` therefore programs 64 bytes and one record of 19 bytes on SPI NOR, independent of the size of the file.
Each open file collects the bytes it writes in a buffer of one page, and programs the buffer when it reaches the end of a page of the device, and at `Sync` and `Close`, up to the last whole program unit; so a series of small writes is programmed page by page.
When a logical block is full, the next one gets a new data block.

A handle appends in place only into a data block that it allocated itself, and only above the high-water mark of what it has programmed into it.
The first append to a partial last block that the handle did not allocate, for instance after a mount, copies the block to a new one first, and so does an append below the high-water mark after `Truncate`.
This is what guarantees that no program unit is programmed twice, also after a power loss in the middle of a program.

### Overwriting

An overwrite is copy-on-write: every logical block that it touches is read, merged with the new bytes in the buffer of the file, and programmed into a new data block, and the record of the write maps the new block and releases the old one.
An overwrite of up to one payload touches at most two blocks.

### Committing

Data that a write programs is not visible until a record commits it.
The blocks it allocated are in RAM in the state allocated but not committed, and the handle keeps the block entries, the size and the inline bytes that are not yet committed.
`Sync` and `Close` program the buffer and write one record with all of them.
A handle keeps at most 8 uncommitted block entries; the write that would add a ninth first commits them.
A read through the handle sees its uncommitted entries and its buffer.

### Other operations on files

`Truncate` commits the file with its new size; the blocks after the new end are released when the record is applied.
`Open` in `write` mode does the same with a size of 0.
`Remove` writes one record that removes the directory entry and the inode; applying it walks the data map and releases every data block and node of the file.
`Truncate`, `Remove`, `MakeDirectory` and `Rename` write their record before they complete, so they are durable then.
`ExpectSize` completes with `success` and has no effect, because NOR blocks need no contiguity.
A file is at most 4 GiB minus one byte; a write beyond writes up to that size and completes with `noSpace`.

## Checkpoints

A checkpoint makes the journal obsolete.
It is written between operations, so it holds only committed state, and as one unit that no foreground operation interrupts:

1. A collection step for the garbage collection of node blocks, described below.
2. Every modified node is written to node blocks, the leaves of a map before its inner nodes and every other map before the inode table; the new root of a data map or a directory map is written into the record of its inode in the inode table.
3. A new checkpoint block is allocated and programmed with the checkpoint, its CRC last.

| Offset          | Size       | Field                                           |
|-----------------|------------|-------------------------------------------------|
| 0               | 4          | Root of the inode table                         |
| 4               | 1          | Height of the inode table                       |
| 5               | 3          | Reserved, 0                                     |
| 8               | 4          | Next inode number                               |
| 12              | 4          | Block where the records of the epoch start      |
| 16              | 4          | Offset where the records of the epoch start     |
| 20              | 4          | Number of live nodes                            |
| 24              | 4          | Number of blocks                                |
| 28              | blocks / 8 | One bit per block, set when the block is in use |
| 28 + blocks / 8 | 4          | CRC of the checkpoint                           |

Until the CRC is complete, the previous checkpoint and its journal remain the valid state.
After it, the previous checkpoint block, the journal blocks of the previous epoch and the node blocks that the collection step emptied become free, and the previous copies of the written nodes are no longer used.
The records of the new epoch start right after the checkpoint, in the checkpoint block itself, so a checkpoint allocates no separate journal block.
A checkpoint of a device with 4096 blocks takes 544 bytes, and a checkpoint always fits in one block: `Format` and `Mount` complete with `unsupported` when the bitmap would not.

A checkpoint continues to fill the last node block of the previous checkpoint of the same mount; after a mount, the first checkpoint starts a new node block.
The work of a checkpoint is bounded by the size of the node cache and of one collection step, and does not depend on the size of the files.
A checkpoint is written when the journal is full, when the next operation would modify more nodes than the cache can hold, and in the background as described below.

## Allocation and the pre-erased pool

At run time, the state of every block is kept in RAM in two bits: free and not erased, free and erased, in use, or allocated but not committed.
During the mount scan, the fourth state marks the checkpoint and journal blocks instead.
The erase count of every block is kept in RAM as one byte, relative to the lowest erase count of all blocks except the superblocks; counts are clamped at 255, and when no block remains at the lowest count, the counts are moved down to the new lowest one.

Allocation takes the free erased block with the lowest erase count; this is dynamic wear leveling, and it needs no flash access.
Erasing a block erases one sector per request, then programs part A with the erase count, so an erase in the background keeps a foreground call waiting for at most one sector erase.
The background erases every free block that is not erased, lowest erase count first, so when the volume has been idle long enough, every free block is in the pre-erased pool.

The pre-erased pool also has a watermark: the blocks one call may take, which is two data blocks, the node blocks and the checkpoint block of one checkpoint, one journal block and the destination of one block move.
A foreground call that finds the pool below the watermark erases one sector of a free block before it completes, unless it waited for an erase of the background.
When the pool is empty and a call needs a block, the call erases one itself.

A reserve of free blocks is kept for the checkpoints and the garbage collection: twice the node blocks of one checkpoint, plus one checkpoint block and one journal block, 8 blocks on SPI NOR with the defaults.
A write that would allocate a data block into the reserve completes with `noSpace`; `Sync`, `Close`, `Truncate` and `Remove` may use the reserve, because they write only records and checkpoints, and the last two free space.
`Usage` reports as total the payload of every block except the superblocks and the reserve, and as used the total minus the payload of the free blocks beyond the reserve.

## Garbage collection

Data blocks belong to exactly one position of one file, so a data block is either in use or free as a whole; reclaiming it only takes an erase, which the background does.
Checkpoint and journal blocks become free as a whole at the next checkpoint.

Node blocks hold several nodes, and a node block may keep some nodes in use while others are replaced.
The volume does not track which nodes of a block are in use; it counts the live nodes, and the checkpoint stores the count.
When the node blocks could hold more than twice the live nodes, every checkpoint starts with a collection step:

1. A cursor goes over the node blocks in turn, and over the slots of each.
2. For each node, the header tells the map, the owner and the lowest key; the node is live when a walk of that map from its root in RAM, with the modifications that are not yet written, reaches this address.
3. A live node is marked modified, so this checkpoint writes it elsewhere; a node that is not live is skipped.
4. The step stops when the cache has no room for more modified nodes, and the cursor stays where it stopped.

A node block whose last slot the step has passed becomes free when this checkpoint is complete, because every node of it that was live is written elsewhere by the same checkpoint.
Nodes are a small fraction of the data, so this relocation is a small fraction of the flash work.
Live file data is never relocated by garbage collection; only static wear leveling moves it.

## Static wear leveling

When the difference between the highest and the lowest erase count exceeds `staticWearLevelingThreshold`, 64 by default, and at least `staticWearLevelingInterval` erases, 32 by default, happened since the last move, the background moves the least-worn block in use to the most-worn erased block of the pool.
The least-worn block in use usually holds data that never changes; after the move, the data rests on a worn block, and the young block returns to the pool for frequently changing data.

* A data block is copied, and a record with a block set maps the copy; its owner in part B tells which inode and index to update.
  Before the record is written, the move checks that the map still holds the block and that no open handle appends into it or holds an uncommitted entry for it; otherwise it gives the move up.
* A node block is moved by marking its live nodes modified, as the collection step does; the next checkpoint writes them into the most-worn erased block.
* Checkpoint and journal blocks are not moved; they become free at the next checkpoint anyway.

When the background has had no time for a move, because the volume is never idle, the foreground makes it: the first allocation after `2 × staticWearLevelingInterval` erases without a move, with the spread above the threshold, moves one block before it completes, unless the same call writes a checkpoint.
So at most one move happens per interval, and its overhead is bounded by the configuration and measured by WL-4.

## Format

`fs::NorFileSystem::Format` creates a volume on the device of an unmounted volume, with the format parameters of its configuration:

1. Read part A of every block, to keep its erase count.
2. Erase the two superblocks, so that a power loss from now on leaves a device that does not mount as the previous volume.
3. Erase every other block, one sector per request, and program part A with its previous erase count plus one; a block without a valid part A gets the average of the others.
4. Allocate the first checkpoint block and a node block, write the inode table with the root directory into the node block, and the checkpoint, with the epoch starting after it.
5. Program the two superblocks.

Every block of the new volume except the superblocks, the first checkpoint block and its node block is in the pre-erased pool.
A power loss before step 5 leaves a device that completes `Mount` with `corrupted`, and `Format` can be started again.

## Mount

1. Read the superblock in block 0, or in block 1 when block 0 is not valid; when neither is, complete with `corrupted`.
   Format parameters that differ from the configuration, and more blocks than `maxBlocks`, complete with `unsupported`, and a number of blocks that does not match the device with `corrupted`.
2. Read the header of every block, which gives the erase counts, the highest sequence, the free and erased blocks, and the checkpoint and journal blocks.
3. Load the checkpoint with the highest sequence whose CRC is valid.
   A checkpoint whose CRC is not valid was interrupted and is skipped, but a valid checkpoint is never skipped: if it is not consistent, the mount completes with `corrupted`, because the blocks of an older one may have been reused.
4. Replay the records of the epoch, as described under [Journal](#journal).
   Only a configuration with a smaller cache than the one that wrote the journal can fill the cache during replay; the mount then writes a checkpoint whose epoch starts at the next record to replay, which is why a checkpoint names where its records start.
5. Classify the remaining blocks: blocks with a valid part A and an erased part B are free and erased; every other block that is not in use is free and not erased.
   A block whose part A is not valid, because its erase was interrupted, gets the average erase count of the device as an estimate, as UBI does on Linux; this happens at most once per power loss and per block.
6. Continue the sequence after the highest one found.

Step 2 reads the header of every block, so the time to mount grows with the size of the device, and is measured by R-4.
Steps 3 and 4 are bounded by the size of the checkpoint and of the journal.
The scan keeps only the highest checkpoint in RAM and marks the checkpoint and journal blocks in the block states, and choosing a checkpoint or the next journal block reads their headers again.
Apart from that case, mounting writes nothing; the first record after it starts a new journal block.

## Unmount

`Unmount` programs the buffers of the open files and commits them, stops the background, and writes a checkpoint when the journal is more than half full, so that the next mount replays at most half a journal.

## Background work

Background work runs when the volume has been idle for the idle delay, 10 ms by default, as described in [Architecture.md](Architecture.md#operation-queue).
Its lifecycle is a `services::TableStateMachine` with the states `Stopped`, `Waiting`, `Claiming` and `Working`.
Each time it holds the queue, it does one unit of work, in this order of priority:

1. A checkpoint, when the journal is more than half full, when more than half the cache holds modified nodes, or when the node blocks could hold more than twice the live nodes, and only when the pool holds every block it needs.
2. One sector erase of the free block that is not erased and has the lowest erase count, followed by its part A once its last sector is erased.
3. A static wear leveling move, when one is due.

A checkpoint and a move are units that are not interrupted; an erase is one device request.
After each unit, the background releases the queue when a foreground operation waits, and continues after the next idle delay.

## Power-fail safety

A state change becomes visible only through a committed record or a committed checkpoint, both protected by a CRC.
Records and blocks are only ever written into program units that have been erased and not programmed since.
A block is erased only when no committed state refers to it, and a block released by a committed record may be erased at once, because every mount replays that record.
The superblocks never change after formatting.

| Interrupted during                 | State after the next mount                                                                              |
|------------------------------------|---------------------------------------------------------------------------------------------------------|
| Programming a record               | The record has an invalid CRC and is ignored; the operation did not happen                              |
| Programming data before its record | The blocks are not referenced by any committed record and are free; the file has its committed contents |
| Writing a checkpoint               | The previous checkpoint and its journal are used                                                        |
| Erasing a block                    | The block has no valid part A; it is erased again and its count is estimated                            |
| Programming part A or part B       | As for an interrupted erase, or the block is not in use and is free                                     |
| Moving a block for wear leveling   | Either the old or the new location is referenced, never neither                                         |
| `Rename`                           | The entry exists under its old name or under its new name, never both                                   |
| Between `Sync` calls               | Data written after the last completed `Sync` may be lost                                                |

The power-cut sweep in [Testing.md](Testing.md) interrupts every operation and background step after every single program and sector erase, and in the middle of the interrupted one, and checks after every remount that the volume is consistent and that every file holds either its previous or its new contents.

## Cost of operations

| Operation                       | Flash work                                                                      | Independent of           |
|---------------------------------|---------------------------------------------------------------------------------|--------------------------|
| Small append followed by `Sync` | Programming the bytes in place and one record                                   | File size and fill level |
| Append that completes a block   | Programming the bytes, part B of the next block and one record                  | File size and fill level |
| Overwrite within one block      | One block read and programmed, one record, at most 3 node reads when not cached | File size and fill level |
| Seek and read                   | At most 3 node reads when not cached, then the data                             | File size and fill level |
| Allocation                      | None, unless the pre-erased pool is below its watermark; then one sector erase  | File size and fill level |
| Checkpoint                      | One collection step, the modified nodes and the checkpoint                      | File size                |
| Mount                           | One header read per block, the checkpoint and the journal                       | File size                |

## RAM use

| Item           | Size                                                                   |
|----------------|------------------------------------------------------------------------|
| Block states   | 2 bits per block                                                       |
| Erase counts   | 1 byte per block                                                       |
| Node cache     | `cachedNodes` times the node size, plus a few bytes per node           |
| Journal buffer | The largest record, rounded up to the program unit                     |
| Each open file | Position, inode record, a buffer of one page and 8 uncommitted entries |

For a 16 MiB device with 4 KiB blocks, that is 4096 blocks, so 1 KiB for the block states and 4 KiB for the erase counts, plus 8 KiB for the default cache of 16 nodes of 512 bytes, and about 0.6 KiB per open file: about 14 KiB with one open file.
Using larger blocks reduces the tables proportionally: with 64 KiB blocks, the same device needs 320 bytes for block states and erase counts, and about 9 KiB in all.
This is more RAM than littlefs needs for the same device; it is the price of allocation without traversal and of wear leveling based on actual erase counts.

## Configuration

```cpp
constexpr fs::NorConfig norConfig{
    .maxBlocks = 4096,
    .sectorsPerBlock = 1,
    .programUnit = 1,
    .pageSize = 256,
};

hal::Flash& norFlash = ...;
fs::NorFileSystem::WithStorage<norConfig, 4> norVolume{ norFlash };
```

The template arguments of `fs::NorFileSystem::WithStorage<Config, Files>` are the configuration and the number of open files; the constructor takes the device and an optional idle delay, 10 ms by default.
The fields marked as format parameters are stored in the superblock, and a volume mounts only with the format parameters it was formatted with.

| `fs::NorConfig`               | Default | Meaning                                                                                                                               |
|-------------------------------|---------|---------------------------------------------------------------------------------------------------------------------------------------|
| `maxBlocks`                   |         | The largest number of blocks a volume may span; sizes the block states and erase counts                                               |
| `sectorsPerBlock`             | 1       | Sectors of `hal::Flash` per block; format parameter                                                                                   |
| `programUnit`                 | 1       | Smallest aligned unit programmed at once, for instance 1 for SPI NOR or 16 for internal flash with error correction; format parameter |
| `pageSize`                    | 256     | The page of the device: the buffer of each open file, and the alignment of nodes and programs                                         |
| `nodeSize`                    | 512     | Size of a node; format parameter                                                                                                      |
| `cachedNodes`                 | 16      | Nodes cached in RAM, at least 15                                                                                                      |
| `inlineBytes`                 | 64      | Size of the inline area, at least the block header; format parameter                                                                  |
| `maxNameBytes`                | 64      | The longest name in UTF-8 bytes; format parameter                                                                                     |
| `maxPathBytes`                | 255     | The longest path in UTF-8 bytes                                                                                                       |
| `directories`                 | 1       | The open directory handles                                                                                                            |
| `pathOperations`              | 2       | The slots for waiting operations on paths, such as `Open`                                                                             |
| `journalBlocks`               | 2       | Blocks of an epoch, its checkpoint block included, before a checkpoint                                                                |
| `staticWearLevelingThreshold` | 64      | Erase-count difference that triggers a move                                                                                           |
| `staticWearLevelingInterval`  | 32      | Minimum number of erases between two moves                                                                                            |

The reserve and the watermark of the pool are computed from the configuration and the geometry of the device.

## Counters

The volume counts, since it was mounted, the blocks it allocated, the sectors it erased, the checkpoints it wrote and the blocks static wear leveling moved, as 32-bit counters that `Counters()` returns without a device access.
A product can read them for diagnostics, and the metrics use them to charge erases, and for W-2 and WL-4.

## Internal flash with error correction

Internal flash with error correction, such as on many STM32 devices, requires writes aligned to its program unit and does not allow a second programming of the same unit, which the format respects.
Reading a program unit whose programming was interrupted can raise an error-correction fault on such devices.
The completions of `hal::Flash` carry no result, so the `hal::Flash` driver for such a device has to return the data of such a unit, for instance as all ones, instead of faulting; the CRC of the record or header then rejects it.
This is verified when an internal flash driver is used on target.
Because `hal::Flash` reports no errors, a NOR volume never completes with `deviceError`; a node, record or header whose CRC does not match completes the operation that reads it with `corrupted`, and the volume stays mounted.

## Testing

The NOR filesystem is tested on `hal::FlashStub` from EmIL, which models NOR programming as a bitwise AND, and on `hal::FlashMock` where the order of device requests is the behaviour under test.
`fs::NorImageBuilder` arranges directories and files synchronously on an image before the operation under test.
The metrics harness adds wrappers that count operations per block and simulate the timing of a NOR device, and a wrapper that cuts power in the middle of a program or an erase.
The metrics W-1 to W-6, R-2, R-4, WL-1 to WL-4 and P-1 cover the behaviour described above.
See [Testing.md](Testing.md) and [Metrics.md](Metrics.md).
