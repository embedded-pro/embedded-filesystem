# FAT filesystem

## Introduction

`fs::FatFileSystem` implements FAT12, FAT16 and FAT32 with long file names on any `hal::BlockDevice`, such as an SD card or eMMC.
Its on-disk format is the standard one, so media written by the device can be used on Windows, Linux and macOS, and media prepared on a PC can be used by the device.
SD cards and eMMC contain a controller that performs wear leveling, bad-block management and error correction, so the FAT filesystem adds none of these.
Instead, it is designed to give the card the access pattern its controller handles best: large, aligned, sequential writes, and a discard for clusters that are no longer used.

## Scope

| Supported                                                    | Not supported in the first version                                      |
|--------------------------------------------------------------|-------------------------------------------------------------------------|
| FAT12, FAT16, FAT32                                          | exFAT                                                                   |
| Long file names, read and write, with short alias generation | Non-ASCII short names; non-ASCII names are stored in the long name only |
| A volume on the whole device, or in an MBR partition         | GPT partition tables                                                    |
| Files up to 4 GiB minus one byte                             | Files of 4 GiB and larger                                               |
| Formatting with SD-compatible alignment                      | Formatting of partitioned layouts other than one primary partition      |

Names are passed as UTF-8 and stored as UTF-16 in long name entries.
Name comparison is case-insensitive for ASCII letters, as on Windows.
The longest supported name and path are configuration parameters, at most 255 characters per name.

## Mount

Mounting reads the first sector of the device, decides whether it holds an MBR or a boot sector, and parses the BIOS parameter block of the volume.
The FAT type is determined from the number of clusters, as the Microsoft FAT specification requires: fewer than 4085 clusters is FAT12, fewer than 65525 is FAT16, and anything else is FAT32.
On FAT32, the FSINFO sector is read for the free-cluster count and the next-free hint; both are treated as hints and validated before use.
The volume dirty flag in the FAT is set while the volume is mounted for writing and cleared on a clean unmount, so that a PC checks the volume after a power loss.

## Sector cache

All metadata accesses go through a sector cache with a compile-time number of sectors.
The cache is write-back: modified sectors stay in RAM until they are evicted, until `Sync`, or until `Unmount`.
A modified FAT sector is written to every copy of the FAT when it is written back.
Transfers that cover whole clusters bypass the cache and go directly between the caller's buffer and the device, as multi-block requests.
As everywhere in EmIL, a buffer passed to `Read` or `Write` must stay valid until the operation completes.

## Open files

Each open file keeps its first cluster, its current cluster and position, its size, the location of its directory entry, its last cluster, and an extent cache.
The extent cache holds a bounded number of runs of contiguous clusters, each as a start cluster and a length.
Seeking within a run is computed without any device access; seeking outside the cached runs walks the cluster chain from the nearest known run instead of from the start of the file.
Opening a file for appending positions it at the last cluster without walking the chain, once the chain has been walked a first time.

## Allocation

Free clusters are found through the FSINFO next-free hint and through a cache of free runs.
When the volume is idle, a background scan of the FAT fills the free-run cache, so that a foreground allocation rarely needs to read the FAT.
Clusters are allocated in contiguous runs: a file that grows takes the next free clusters after its last cluster when they are free, and a new file starts at the beginning of a free run.
When the allocation unit of the card is known, new runs prefer to start at an allocation unit boundary.

## Writing

Small writes are collected in the sector cache and written as multi-block requests.
A write that covers whole clusters is sent to the device directly, as one request per contiguous run.
The order of writes is fixed so that a power loss never leaves a directory entry that refers to clusters that were not written:

1. The data clusters are written.
2. The FAT entries that link the new clusters are written, to every copy of the FAT in turn.
3. The directory entry, with the new size and first cluster, is written.

`Sync` writes back every modified sector of the file in this order and then calls `Flush` on the block device, so that data in the device's own cache is also made durable.
FSINFO is updated on `Sync` and on `Unmount`, but because it is only a hint, an outdated FSINFO after a power loss is harmless.

## Discard

When clusters are freed by `Remove` or `Truncate`, their blocks are passed to `hal::BlockDevice::EraseBlocks`, which the device treats as a discard.
This tells the card that the data is no longer needed, so its controller does not have to copy it during its own garbage collection.
Discard can be disabled in the configuration for devices that implement it slowly.

## Behaviour on power loss

| Interrupted during          | State after the next mount                                                                           |
|-----------------------------|------------------------------------------------------------------------------------------------------|
| Writing data clusters       | The file has its previous size and contents; the new clusters are not yet allocated                  |
| Writing the FAT             | The file has its previous size; some clusters may be allocated but unused, which a PC check reclaims |
| Writing the directory entry | The file has either its previous or its new size and first cluster                                   |
| Between `Sync` calls        | Data written after the last completed `Sync` may be lost                                             |

These guarantees assume that the device writes a sector either completely or not at all.
Some SD cards corrupt a sector that is being written when power fails, and some even corrupt other sectors that the card's controller was moving at the time.
FAT has no checksums, so such corruption cannot be detected or repaired by the filesystem.
Where this matters, use cards with power-loss protection, or store critical data in the NOR filesystem.
The power-cut sweep described in [Testing.md](Testing.md) verifies the guarantees above under the assumption of atomic sector writes, and separately reports what happens with torn sector writes.

## Formatting

`fs::FatFormatter` creates a volume on a block device.
For SD cards, it follows the SD Association file system specification: the cluster size and the start of the partition and of the data area are chosen so that clusters never straddle an allocation unit of the card.
SDSC cards are formatted as FAT12 or FAT16, and SDHC and SDXC cards as FAT32.
Formatting writes only the metadata and discards the rest of the device.

## Testing

The FAT filesystem is tested on `hal::BlockDeviceStub` from EmIL, with volumes created by `fs::FatFormatter` in memory.
Interoperability tests create images with `mkfs.fat`, write them with the library, check them with `fsck.fat` and read them back with `mtools`, and the reverse.
The power-cut sweep and the metrics W-2, R-2 and M-1 to M-3 cover the behaviour described above.
See [Testing.md](Testing.md) and [Metrics.md](Metrics.md).
