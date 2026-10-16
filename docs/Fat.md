# FAT filesystem

## Introduction

`fs::FatFileSystem` implements FAT12, FAT16 and FAT32 with long file names on any `hal::BlockDevice`, such as an SD card or eMMC.
Its on-disk format is the standard one, so media written by the device can be used on Windows, Linux and macOS, and media prepared on a PC can be used by the device.
SD cards and eMMC contain a controller that performs wear leveling, bad-block management and error correction, so the FAT filesystem adds none of these.
Instead, it is designed to give the card the access pattern its controller handles best: large, aligned, sequential writes, and a discard for clusters that are no longer used.
`fs::FatReader` reads the same volumes for products that never write, with a fraction of the flash and the RAM, as described under [Read-only volume](#read-only-volume).

## Scope

| Supported                                                    | Not supported in the first version                                      |
|--------------------------------------------------------------|-------------------------------------------------------------------------|
| FAT12, FAT16, FAT32                                          | exFAT                                                                   |
| Long file names, read and write, with short alias generation | Non-ASCII short names; non-ASCII names are stored in the long name only |
| A volume on the whole device, or in an MBR partition         | GPT partition tables                                                    |
| Files up to 4 GiB minus one byte                             | Files of 4 GiB and larger                                               |
| Formatting with SD-compatible alignment                      | Formatting of partitioned layouts other than one primary partition      |

## Names

Names are passed as UTF-8; a name that is not valid UTF-8 completes with `invalidPath`.
Long names are stored as UTF-16 in long name entries, at most 255 UTF-16 units per name; a longer name completes with `nameTooLong`.
The longest name a volume creates and reports is also limited by `FatLimits::maxNameBytes`, 255 UTF-8 bytes by default, which sizes the buffer that holds a name while it is reported.
A name on the medium that does not fit that buffer, for instance one written by a PC with many non-ASCII characters, is reported by its short name, which also opens it.

The characters `"`, `*`, `/`, `:`, `<`, `>`, `?`, `\` and `|`, the control characters 0x00 to 0x1F and 0x7F complete with `invalidPath`, and so does a name that ends with a dot or a space.
A name may start with a space or a dot.
Names are compared case-insensitively for ASCII letters, as on Windows, and byte by byte otherwise; a path component matches either the long or the short name of an entry.

A name that fits the 8.3 format, with a base and an extension that are each all lower case or all upper case, is stored as a short entry only, with the lower-case flags that Windows NT introduced.
Every other name is stored in long name entries, followed by a short alias, or completes with `unsupported` when `fs::FatFeatures::longNames` is left out:

1. The basis of the alias is the name in upper case, without spaces and leading dots; every character that a short name cannot hold, including `+`, `,`, `;`, `=`, `[`, `]` and every non-ASCII character, becomes `_`.
2. Its base is the part before the last dot, cut to 8 characters, and its extension the part after the last dot, cut to 3.
3. A numeric tail `~1` to `~32` replaces the end of the base; the lowest tail that neither a short nor a long name in the directory uses is taken, found while the directory is scanned for the name.
4. If every tail is in use, the base becomes the first 2 characters of the basis, 4 hexadecimal digits of the CRC-16/CCITT-FALSE of the name and `~1`, and the directory is scanned once more; if that alias is in use too, the operation completes with `noSpace`.

Short names are read with the lower-case flags applied, and their bytes from 0x80 upwards are decoded as code page 437.
They are never generated with such bytes.

## Directory entries

An entry that starts with 0xE5 is free, and 0x05 stands for a first byte of 0xE5.
An entry that starts with 0x00 ends the directory, so a scan stops there; clusters added to a directory are filled with zeros.
Long name entries precede their short entry in reverse order, carry the checksum of the short name, and the first of them is flagged with 0x40.
A set of long name entries with a gap in its sequence numbers, without its flagged entry or with a wrong checksum is an orphan: listing and lookup skip it and report the short entry that follows by its short name, and its slots are not reused.
Volume labels and the `.` and `..` entries are skipped by listing and lookup.

| Attribute | Effect                                                                                    |
|-----------|-------------------------------------------------------------------------------------------|
| Directory | Reported as `EntryType::directory`                                                        |
| Read-only | `Open` for writing, `Truncate` and `Remove` complete with `readOnly`; `Rename` is allowed |
| Hidden    | Listed like any other entry                                                               |
| System    | Listed like any other entry                                                               |
| Archive   | Set when an entry is created and when a file is written                                   |

The fixed root directory of FAT12 and FAT16 holds the number of entries given in its boot sector, and any other directory at most 65536 entries; an operation that needs more completes with `noSpace`, and every scan is bounded by the same limits.
A new entry set takes the first run of free slots that is long enough and lies in one cluster, or in the fixed root; the free slots at the end of a directory count as part of that run.
When no run is long enough, the set starts at the entry that ends the directory and continues into the clusters that follow it, which hold only free entries, and then into new clusters.
It does not start in free slots before that entry, so that the directory still ends before the new clusters until every sector of the set in front of them is written.
A new cluster is written as zeros at rank 0, together with the entries of the set that fall into it, and is linked to the directory at rank 1 only after that.
The entries of the set in existing sectors are written at rank 2.

## Timestamps

Timestamps come from the timer services of EmIL, in the same way as the timestamps of `services::TracerWithDateTime`: the volume reads `infra::Now()` of the timer service given by `FatConfig::timerServiceId` and splits it into a date and a time with `infra::PartitionedTime`.
FAT stores local time in steps of two seconds, from 1980 to 2107; earlier and later times are clamped to these bounds.
The default is the system timer service, which counts from the start of 1970 unless the application sets it, so every timestamp is then 1980-01-01 00:00:00.
An application that knows the time, from SNTP or a real-time clock, passes `infra::localTimeTimerServiceId` of `services::TimeWithLocalization`.
The creation time is set when an entry is created, and the modification time by the `Sync` or `Close` that follows a write; the access date is not updated, so reading never writes.

## Mount

Mounting reads the first sector of the device.
A sector with a valid boot sector makes the whole device the volume; otherwise, a sector with a valid MBR makes the first primary partition of a FAT type (0x01, 0x04, 0x06, 0x0B, 0x0C or 0x0E) the volume, and its boot sector is read.
A device with neither, such as an empty card, completes with `corrupted`, and a device with a GPT protective MBR with `unsupported`.

The BIOS parameter block is validated: values that no FAT volume may have, such as a cluster size that is not a power of two, a volume larger than its device or a FAT too small for its clusters, complete with `corrupted`, and values the volume does not support, such as a sector size other than the block size of the device, with `unsupported`.
The FAT type is determined from the number of clusters, as the Microsoft FAT specification requires: fewer than 4085 clusters is FAT12, fewer than 65525 is FAT16, and anything else is FAT32.
On FAT32, when mirroring is disabled in the extended flags, only the active FAT is read and written.

On FAT32, the FSINFO sector and the start of the FAT are read too.
The free-cluster count and the next-free hint of FSINFO are used only when its signatures are valid, the values are in range, and the volume was cleanly unmounted, as recorded in the FAT; otherwise they are ignored.
A FAT12 or FAT16 mount therefore reads one or two sectors, and a FAT32 mount three or four.
Mounting writes nothing.

## Corruption

Every walk over a cluster chain or a directory is bounded by the number of clusters of the volume and the number of entries of a directory, so a loop cannot hang the volume.
A link that points outside the volume, to a free cluster or to a bad cluster, a chain longer than the volume, and a chain shorter than the size of its file complete the operation that finds them with `corrupted`.
The volume stays mounted, so that other files remain accessible; only a volume that cannot be mounted ends in the `Fault` state of its lifecycle.

## Sector cache

Metadata and small transfers go through `fs::SectorCache`, a write-back cache with a compile-time number of sectors: modified sectors stay in RAM until they are evicted, until `Sync`, or until `Unmount`.
Transfers of whole sectors go through `fs::DirectTransfers`, and every write of either goes through `fs::FlushBarrier`, so the flush between ranks covers every write.
Every modified sector carries a rank, which orders the writes:

| Rank | Sectors                                                                                               |
|------|-------------------------------------------------------------------------------------------------------|
| 0    | Data, and the clusters of a directory that is created or grown, as zeros or with `.` and `..`         |
| 1    | The FAT and FSINFO                                                                                    |
| 2    | Existing directory entries, and new entry sets                                                        |
| 3    | The short entry of a new entry set that spans two sectors, and the long name entries of a removed set |
| 4    | The FAT sector with the clean-shutdown bit, when `Unmount` sets it                                    |

Writing back writes the sectors of the lowest rank first, and within a rank in ascending sector order.
When the cache is full, it replaces a free slot, else the least recently used sector that is not modified, else the least recently used sector of the lowest modified rank, written together with the consecutive sectors of its rank in the adjacent slots; so making room writes at most one device request per copy, and never a sector of a higher rank before one of a lower rank.
A sector that is modified again with another rank first makes the cache write back everything.
A write that fails leaves its sector modified, and writing back stops at the first failure, so a sector of a higher rank is never written after a failed write of a lower rank.
FAT sectors are written to the first FAT and then to every other copy in turn.
The volume issues one device request at a time.

### Flushing between ranks

A device with a cache of its own, such as eMMC with its cache enabled, may write its cache back in another order than it received the writes.
So before `fs::FlushBarrier` writes a sector of rank r, it calls `hal::BlockDevice::Flush` when a sector of a lower rank was written since the last flush.
This costs one device request per change of rank, and makes the order of the ranks hold on every device.
An operation that needs a barrier, as described under [Writing](#writing), asks the cache to flush: the cache writes back every modified sector and then flushes the device.

### Direct transfers

The whole sectors of a read go directly from the device into the buffer of the caller, one request per contiguous run of clusters, and the cache then copies the sectors it holds of that range over the buffer, so that the caller sees the latest contents.
A write goes directly to the device when its whole sectors form a contiguous run longer than the cache can collect, which is the number of cache sectors minus two; the cache drops the sectors it holds of that range, and the run is written at rank 0 in one request.
Shorter writes, and the partial sectors at the ends of any transfer, go through the cache.
A driver that moves data with DMA may need aligned buffers: `hal::SdCardStm` of hal-st needs 4 bytes, and, while the data cache of a Cortex-M7 is on, buffers aligned and sized to its cache line of 32 bytes, so that it can clean and invalidate the cache itself.
The sectors of the cache, and the buffer of `fs::FatFormatter`, are therefore aligned to 32 bytes, and `FatConfig::transferAlignment` gives the alignment the driver needs: the whole sectors of a transfer go directly to or from the buffer of the caller only when their place in it is aligned to it, and through the cache otherwise.
The cache writes back consecutive sectors of the same rank that it holds in adjacent slots in one multi-block request, so that collected writes leave the cache as multi-block requests.
To keep such sectors adjacent, a sector that is written takes the slot after the one of its predecessor; when that slot holds a modified sector, the cache moves it to the first free or unmodified slot after it, and otherwise a written sector takes the first free slot, else the first unmodified one.
Modified metadata thereby moves to the last slots, and a stream of collected writes fills the others in order.
Clusters that are freed are discarded through `fs::DirectTransfers`, after the cache drops the sectors it holds of them.
Background reads, such as the scan of the FAT for free clusters, use `SectorCache::ReadOnce`, which takes only free or unmodified slots and leaves them least recently used, so a background read is one device request and never pushes useful sectors out of the cache; when every slot is modified, it completes with `busy`.
As everywhere in EmIL, a buffer passed to `Read` or `Write` must stay valid until the operation completes.

## Open files

Each open file keeps its first cluster, its current cluster and position, its size, the location of its directory entry, its last cluster, a reservation, and an extent cache.
The extent cache holds `FatLimits::extentsPerFile` runs of contiguous clusters, 4 by default, each as the cluster index in the file, the cluster on the device and a length; it keeps the first run, and replaces the least recently used of the others.
Seeking within a run is computed without any device access; seeking outside the cached runs walks the cluster chain from the nearest preceding run instead of from the start of the file.
The volume remembers the location of the directory entry, the first cluster, the size and the last run of the last `FatLimits::closedFiles` files that were closed, 2 by default.
Opening one of them again, with the same first cluster and size, starts with that run in its extent cache, so appending after a reopen does not walk the cluster chain.

## Allocation

Free clusters are found through a cursor, initialised from the FSINFO next-free hint.
When the volume is idle, a background scan of the FAT counts the free clusters and keeps the longest free runs it finds in a cache of `FatLimits::freeRuns` runs, 8 by default.
The scan checks before every device request whether a foreground operation is waiting, and yields to it.
A cluster that is allocated leaves the cache of free runs, and a cluster that is freed does not join it, so every cluster in the cache is free.
`Usage` reports the free clusters counted by this scan; when the scan has not finished, the first `Usage` completes it, and from then on the count is kept in RAM until `Unmount`.
Clusters allocated or freed while the scan runs are counted in the sectors it has already read.
On a clean FAT32 volume, the free count of FSINFO is used instead, so `Usage` does not read the FAT; the scan runs there only to fill the cache of free runs.
Clusters are allocated in this order:

1. A file with a reservation takes its clusters from the reservation.
2. A file that grows takes the cluster after its last cluster when it is free.
3. Otherwise the first cluster of a new chain is the first free cluster of the allocation unit of the cursor, and any other cluster is the first free cluster at or after the cursor; either search continues past the unit, wrapping around at the end of the volume.
4. A reservation that would cross an allocation unit boundary starts at the next boundary instead, when a free run of its length starts there.
5. A file opened in `append` mode that has no clusters yet starts at the first allocation unit boundary at or after the cursor, wrapping around, whose whole allocation unit the cache of free runs holds, so that a log fills allocation units one after the other; when the cache holds no such allocation unit, rule 3 applies.
6. When the allocation unit of the card is not known, the rules about allocation units do not apply.

With rule 3, new files take the clusters that removed files freed in the unit the card is already writing; its search from the start of the unit reads the FAT sectors that cover it, one or two on a card formatted to the SD specification.
The allocation unit is given by `FatConfig::blocksPerAllocationUnit`, because `hal::BlockDevice` does not report it.
On an SD card it is the allocation unit of the SD status, and on eMMC the high-capacity erase group of the EXT_CSD register; on the boards of [Validation.md](Validation.md), the drivers of hal-st give both, and a benchmark checks by timing that the card behaves as the unit says.
On FAT12, the allocator never takes a cluster whose 12-bit FAT entry straddles two sectors, because such an entry is changed by two writes; these clusters count as used.
A file whose last cluster has such an entry, which only another implementation creates, is extended by writing both halves of that entry, so a power loss between the two writes can corrupt its chain.
When the volume is full, writes complete with `noSpace`.

## ExpectSize

`ExpectSize` reserves a contiguous run of free clusters for the expected size, following the allocation rules above, and the writes of the file then take their clusters from it.
It reads the FAT from the cluster after the last cluster of the file, and otherwise from the cursor, until it finds such a run; other files skip the reserved clusters.
The reservation is kept in RAM only: nothing is written for it, and `Close` returns what was not used.
A later `ExpectSize` replaces the reservation, and one for a size that the clusters of the file already hold drops it.
When no run of that length is free, it reserves the longest run it finds; it never completes with `noSpace`.

## Writing

Writes go through the cache or directly to the device, as described under [Direct transfers](#direct-transfers).
The order of writes is fixed so that a power loss never leaves a directory entry that refers to clusters that were not written:

1. The data clusters are written.
2. The FAT entries of the new clusters are written, to every copy of the FAT in turn.
3. The FAT entry that links the new clusters to the end of the file is written.
4. The directory entry, with the new size and first cluster, is written.

The ranks of the sector cache enforce the order of data, FAT and directory entries.
Within the FAT, sectors are written in ascending order, so when the entries of the new clusters are in another FAT sector than the link of step 3, the operation flushes the cache between steps 2 and 3.
A file whose directory entry does not refer to a cluster yet, such as a new file before its first `Sync`, needs no such flush: no entry refers to its chain until step 4, which the ranks write after a flush, so a power loss leaves at most clusters allocated but unused.
`Sync` writes back every modified sector in this order and flushes the device.
The directory entry is written only when its contents changed, so an overwrite inside a file that keeps its size and timestamps costs no write of the directory.

An operation that needs another order flushes the cache between its steps:

| Operation       | Order                                                                                                                                                      |
|-----------------|------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Write, `Sync`   | Data, FAT entries of the new clusters, flush if needed, the link, the directory entry                                                                      |
| Create          | New directory clusters with their entries, their FAT entries, then the rest of the set at rank 2, with its short entry at rank 3 when it spans two sectors |
| `MakeDirectory` | The new cluster with `.` and `..`, its FAT entry, then the entry set in the parent                                                                         |
| `Remove`        | The short entry marked free, then the rest of its set at rank 3, flush, the FAT entries of the chain freed, then the discard of each freed run             |
| `Truncate`      | The directory entry with the new size, flush, the end of chain at the new last cluster, flush, the FAT entries of the cut clusters freed, then the discard |
| `Rename`        | The new entry set, flush, the old entry set marked free, then the `..` entry of a moved directory                                                          |
| `Unmount`       | The directory entries of the open files, FSINFO when the FAT changed, flush, the clean-shutdown bit, flush                                                 |

## Discard

When clusters are freed by `Remove` or `Truncate`, their blocks are passed to `hal::BlockDevice::EraseBlocks`, which the device treats as a discard, one request per contiguous run.
This tells the card that the data is no longer needed, so its controller does not have to copy it during its own garbage collection.
Discard can be disabled with `FatConfig::discard` for devices that implement it slowly.

## Durability

The clean-shutdown bit of the second FAT entry is cleared before the first change to the metadata of a mount is written, and set again by a clean `Unmount`, so that a PC checks the volume after a power loss.
The first operation of a mount that changes the volume clears the bit in every FAT copy and flushes before it writes anything else, which costs one write per copy and one flush per mount.
These operations are a write, an `Open` that creates or truncates, `MakeDirectory`, `Remove`, `Rename` and `Truncate`; a `Sync` or `Close` only commits what one of them changed.
`Unmount` sets the bit at rank 4, so it is written only after everything else has been flushed.
FAT12 has no such bit.
A volume that was not clean at mount stays marked as not clean, and its FSINFO is not trusted.
FSINFO is written only by `Unmount`, when the FAT changed during the mount, with the free count when it is known and as unknown otherwise; writing it on every `Sync` would add a write to every `Sync`, and an outdated FSINFO is harmless because it is only a hint.
A write-protected card mounts and can be read; operations that write complete with `readOnly`, because clearing the clean-shutdown bit fails before they change anything, and the cache drops the sector it could not write.
`Remove`, `MakeDirectory`, `Rename` and `Truncate` flush the cache before they complete, so they are durable then, and a power loss afterwards leaves no clusters allocated but unused; a file that `Open` creates is written with its first `Sync` or `Close`, or at `Unmount`.
`Open` in `write` mode truncates an existing file to zero in the same way as `Truncate`, before it completes.
A `Truncate` at the end of the file writes nothing, and one within the last cluster writes only the directory entry.

## Behaviour on power loss

| Interrupted during          | State after the next mount                                                                                        |
|-----------------------------|-------------------------------------------------------------------------------------------------------------------|
| Writing data clusters       | The file has its previous size and contents; the new clusters are not yet allocated                               |
| Writing the FAT             | The file has its previous size; some clusters may be allocated but unused, which a PC check reclaims              |
| Writing the directory entry | The file has either its previous or its new size and first cluster                                                |
| An overwrite inside a file  | Every sector holds either its previous or its new contents                                                        |
| Creating a file             | The file exists, with what its first `Sync` committed, or not at all; a new directory cluster may stay unused     |
| `Remove`                    | The file exists or not; its clusters may stay allocated but unused                                                |
| `MakeDirectory`             | The directory exists, empty, or not at all; its cluster may stay allocated but unused                             |
| `Truncate`                  | The file has its previous size or its new one; the cut clusters may stay allocated but unused                     |
| `Rename`                    | The entry exists under its old name, its new name or both; in the last case both names refer to the same clusters |
| Between `Sync` calls        | Data written after the last completed `Sync` may be lost                                                          |

After an interrupted `Rename` of a directory into another parent, its `..` entry may still name the previous parent, which a PC check corrects.
Because the cache flushes the device between ranks, these guarantees also hold on devices that cache writes.
They assume that the device writes a sector either completely or not at all.
Some SD cards corrupt a sector that is being written when power fails, and some even corrupt other sectors that the card's controller was moving at the time.
FAT has no checksums, so such corruption cannot be detected or repaired by the filesystem.
Where this matters, use cards with power-loss protection, or store critical data in the NOR filesystem.
The power-cut sweep described in [Testing.md](Testing.md) verifies the guarantees above under the assumption of atomic sector writes, also when the device loses or reorders unflushed writes, and separately reports what happens with torn sector writes.

## Limits

A write beyond 4 GiB minus one byte writes up to that size and completes with `noSpace`.
A directory holds at most 65536 entries, and the fixed root directory of FAT12 and FAT16 the number given in its boot sector.

## Formatting

`fs::FatFormatter` creates a volume on a block device.
It follows the SD Association file system specification, part 2 of the SD specifications: the type, the cluster size and the alignment of the data area are chosen from the capacity of the card.

| Capacity     | Type  | Sectors per cluster | Boundary unit in sectors |
|--------------|-------|---------------------|--------------------------|
| Up to 8 MB   | FAT12 | 16                  | 16                       |
| Up to 64 MB  | FAT12 | 32                  | 32                       |
| Up to 256 MB | FAT16 | 32                  | 32                       |
| Up to 1 GB   | FAT16 | 32                  | 64                       |
| Up to 2 GB   | FAT16 | 64                  | 64                       |
| Up to 32 GB  | FAT32 | 64                  | 8192                     |
| Larger       | FAT32 | 64                  | 32768                    |

The specification formats cards larger than 32 GB, SDXC cards, with exFAT; the formatter uses FAT32 for them, as exFAT is not supported.
These values have not yet been checked against the specification document itself; they are compared with cards formatted by the SD Memory Card Formatter of the SD Association on a board, as [Validation.md](Validation.md#formatter-comparison) describes, and the layouts of the cards are recorded here.

The device holds an MBR with one primary partition, and the system area, from the boot sector up to and including the fixed root directory, ends on a boundary unit, so that the data area starts on one and clusters never straddle one.
On FAT12 and FAT16, the start of the partition absorbs the difference; on FAT32, the partition starts on a boundary unit and the reserved sectors absorb it.
When the allocation unit of the card is known, it can be passed in `FatFormatConfig` and is used as the boundary unit.
Without an MBR, which `FatFormatConfig` can select, the volume starts at the first sector and the reserved sectors absorb the difference.

The formatter writes 2 copies of the FAT by default, as the specification requires, and the number is configurable.
The fixed root directory of FAT12 and FAT16 holds 512 entries; on FAT32, FSINFO is in sector 1 and a backup of the boot sector in sector 6.
The boot sector has the volume label `NO NAME` and the volume ID given in `FatFormatConfig`.
Formatting writes the MBR, the boot sector, FSINFO and their backups, the FAT with zeros except its first two entries, and the root directory with zeros, then discards the rest of the device and flushes it.

## Read-only volume

`fs::FatReader`, in `fs/fat/reader`, is a FAT volume that only reads, for a product that never writes to its card, such as a bootloader that loads an image or a device that plays files.
It mounts, looks up, lists and reads with the parts of `fs::FatFileSystem` that read, so it validates a volume as described under [Mount](#mount), compares and reports names as described under [Names](#names) and [Directory entries](#directory-entries), and bounds every walk and reports what it finds as described under [Corruption](#corruption); both accept the same volumes and report the same names.
A removed medium faults it in the same way.
It leaves out:

* every change: `Write`, `Truncate`, `ExpectSize`, `Remove`, `Rename`, `MakeDirectory` and `Open` in a mode other than `read` complete with `readOnly`, and `Usage` completes with `unsupported`, as FatFs built read-only has no free-space query either;
* the sector cache: one buffer of one sector holds the sector that the last step read, of the boot sector, the FAT or a directory, so a mount reads the first sector of the device and, in a partition, its boot sector, and nothing else;
* the extent cache: a read continues the cluster chain from the cluster it reached last, and a `Seek` back follows the chain again from the start of the file;
* the background scan of the FAT and the closed files.

A read copies a partial sector from the sector buffer and reads whole sectors straight into the buffer of the caller, at most one cluster per request.

```cpp
hal::BlockDevice& sdCard = ...;
fs::FatReader::WithStorage<2> reader{ sdCard };
```

The template arguments of `fs::FatReader::WithStorage<Files, Directories, Limits>` are the number of open files, the number of open directory handles, 1 by default, and an optional `fs::PathLimits`, the longest name and the longest path in UTF-8 bytes, 255 each by default.
The longest name also sizes the buffer that holds a name while it is reported, as `FatLimits::maxNameBytes` does for `fs::FatFileSystem`.
On a Cortex-M4, F-1 measures 22.2 KB of flash and 2.0 KB of RAM for `fs::FatReader::WithStorage<1>`, against 67.0 KB and 7.7 KB for `fs::FatFileSystem` with the same calls and 5.9 KB and 1.6 KB for FatFs built read-only, as [Metrics.md](Metrics.md#on-target-metrics) reports.

## Configuration

```cpp
hal::BlockDevice& sdCard = ...;
fs::FatFileSystem::WithStorage<4, 8> fatVolume{ sdCard, fs::FatConfig{ .blocksPerAllocationUnit = 8192, .timerServiceId = infra::localTimeTimerServiceId } };
```

The template arguments of `fs::FatFileSystem::WithStorage<Files, CacheSectors, Limits, Features>` are the number of open files, the number of sectors in the cache, an optional `fs::FatLimits` with the other sizes and an optional `fs::FatFeatures`:

| `fs::FatLimits`  | Default | Sizes                                                         |
|------------------|---------|---------------------------------------------------------------|
| `directories`    | 1       | The open directory handles                                    |
| `pathOperations` | 2       | The slots for waiting operations on paths, such as `Open`     |
| `extentsPerFile` | 4       | The extent cache of every open file                           |
| `closedFiles`    | 2       | The closed files whose last run is remembered                 |
| `freeRuns`       | 8       | The cache of free runs                                        |
| `maxNameBytes`   | 255     | The longest name in UTF-8 bytes, and the buffer that holds it |

`fs::FatFeatures` leaves out what a product does not need, so that its code is not linked: calls it never makes, which then complete with `unsupported` on a mounted volume, and with `notMounted` otherwise, before their paths are validated, and work that it can do without.
Every feature is in by default.

| `fs::FatFeatures` | Leaves out                                                                                                                                                                                                                                                           |
|-------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `makeDirectory`   | `MakeDirectory`                                                                                                                                                                                                                                                      |
| `remove`          | `Remove`                                                                                                                                                                                                                                                             |
| `rename`          | `Rename`                                                                                                                                                                                                                                                             |
| `expectSize`      | `ExpectSize`                                                                                                                                                                                                                                                         |
| `usage`           | `Usage`                                                                                                                                                                                                                                                              |
| `backgroundScan`  | The count of the free clusters while the volume is idle: `Usage` counts them on its first call, and until then a file opened for appending starts at the allocation cursor instead of in a whole free allocation unit                                                |
| `timestamps`      | The time of entries: every entry is dated 1980-01-01 00:00:00, as with a clock that is not set                                                                                                                                                                       |
| `cleanShutdown`   | The clean-shutdown bit of the FAT: the volume never marks itself dirty, so a PC cannot tell that it was removed without `Unmount`, and a FAT32 mount reads neither FSINFO nor the FAT, so `Usage` and the allocation do not start from its counts                    |
| `longNames`       | The creation of long names: `Open`, `MakeDirectory` and `Rename` complete with `unsupported` for a name that is not 8.3 with each part all lower or all upper case, while long names already on the volume are still found, read, renamed to short names and removed |

F-1 measures the volume with every feature left out as `embedded-filesystem/fat-minimal`.

`fs::FatConfig` holds the settings that need no storage:

| `fs::FatConfig`           | Default                       | Meaning                                                                  |
|---------------------------|-------------------------------|--------------------------------------------------------------------------|
| `maxPathBytes`            | 255                           | The longest path in UTF-8 bytes                                          |
| `idleDelay`               | 10 ms                         | The idle time after which the background scan of the FAT starts          |
| `discard`                 | `true`                        | Whether freed clusters are discarded                                     |
| `blocksPerAllocationUnit` | 0                             | The allocation unit of the card in blocks, 0 when it is unknown          |
| `transferAlignment`       | 1                             | The alignment in bytes of the buffers that the driver transfers directly |
| `timerServiceId`          | `infra::systemTimerServiceId` | The timer service that gives the time of timestamps                      |

## RAM

The sector cache takes most of the RAM of a volume: every cache sector costs its 512 bytes and an entry of a few bytes.
`fs::FatFileSystem::WithStorage<1, 8>` takes 7712 bytes on a Cortex-M4, 4 KiB of which is the cache, and `WithStorage<2, 8>` 7992 bytes: every further open file costs 280 bytes.
FatFs needs less, 1652 bytes with one open file as F-1 measures, because it works with one sector buffer per volume and one per file; the metrics report the RAM of every subject next to its results.

## Testing

The FAT filesystem is tested on `hal::BlockDeviceStub` from EmIL and on `fs::SparseBlockDevice`, which holds FAT32 volumes of tens of megabytes in a few kilobytes, with volumes created by `fs::FatFormatter` and arranged with `fs::FatImageBuilder` in memory.
Interoperability tests create images with `mkfs.fat`, write them with the library, check them with `fsck.fat` and read them back with `mtools`, and the reverse.
`fs::FatReader` has tests of its own in `fs/fat/reader/test`, on the same devices and volumes, and the interoperability tests read the images of `mkfs.fat` and `mtools` with both volumes.
The power-cut sweep and the metrics W-2, R-2 and M-1 to M-3 cover the behaviour described above.
See [Testing.md](Testing.md) and [Metrics.md](Metrics.md).
