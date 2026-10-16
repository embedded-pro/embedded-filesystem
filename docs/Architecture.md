# Architecture

## Introduction

embedded-filesystem is a set of libraries layered on top of the storage interfaces of [embedded-infra-lib](https://github.com/embedded-pro/embedded-infra-lib) (EmIL).
An application talks to one asynchronous file API; underneath, each volume is either a FAT volume on a block device or a NOR volume on raw flash.
This document describes the layers, the execution model, the public API and the configuration.
The on-disk designs are described in [Fat.md](Fat.md) and [NorFilesystem.md](NorFilesystem.md).

## Layers

```text
                         Application
                              |
            fs::FileSystem / fs::File / fs::Directory
                     (fs/interfaces)
                 /                          \
  fs::FatFileSystem, fs::FatReader      fs::NorFileSystem
      (fs/fat, fs/fat/reader)               (fs/nor)
                 \                          /
             shared building blocks (fs/common)
         path parsing, sector cache, operation queue,
                  handle pools, CRC records
                 |                          |
         hal::BlockDevice                hal::Flash
      SD card, eMMC (drivers         SPI and QSPI NOR, internal
      outside this repository)       flash (services/flash in EmIL)
```

| Folder          | Target          | Contents                                                                                                                       |
|-----------------|-----------------|--------------------------------------------------------------------------------------------------------------------------------|
| `fs/interfaces` | `fs.interfaces` | `fs::Result`, `fs::OpenMode`, `fs::File`, `fs::Directory`, `fs::EntryInfo`, `fs::VolumeUsage`, `fs::FileSystem`                |
| `fs/common`     | `fs.common`     | Path parsing, sector cache, per-volume operation queue, handle pools, CRC-protected records                                    |
| `fs/fat`        | `fs.fat`        | FAT12, FAT16 and FAT32 with long file names, and the formatter                                                                 |
| `fs/fat/reader` | `fs.fat_reader` | `fs::FatReader`, a FAT volume that only reads, built from the parts of `fs.fat` that read                                      |
| `fs/nor`        | `fs.nor`        | The log-structured NOR filesystem, its garbage collection and its wear leveling                                                |
| `metrics`       | `metrics.*`     | Host-only device models, metered devices, workloads and baselines, see [Metrics.md](Metrics.md)                                |
| `validation`    | `validation.*`  | Firmware for boards, the reference images of F-1 and the host package that runs the boards, see [Validation.md](Validation.md) |

Storage drivers are not part of this repository.
NOR flash drivers are in EmIL (`services/flash`), and SD card and eMMC drivers implement `hal::BlockDevice` in EmIL or in the vendor HAL repositories such as hal-st and hal-ti.
The validation on boards uses the drivers of hal-st, which it fetches only when it is built for an ST board; the libraries never depend on hal-st.

## Dependencies on EmIL

| EmIL component                                                            | Used for                                                     |
|---------------------------------------------------------------------------|--------------------------------------------------------------|
| `hal::BlockDevice`                                                        | Storage for FAT volumes                                      |
| `hal::Flash`                                                              | Storage for NOR volumes                                      |
| `infra::EventDispatcher`                                                  | Delivering every completion, and running background work     |
| `infra::Function`                                                         | Completion callbacks                                         |
| `infra::SharedPtr`, `infra::SharedObjectAllocatorFixedSize`               | Handles to open files and directories, from fixed-size pools |
| `infra::BoundedVector`, `infra::BoundedDeque`, `infra::BoundedString`     | Bounded storage for queues, caches, names and paths          |
| `services::TableStateMachine`                                             | Lifecycle of volumes and of background work                  |
| `infra::Crc32`                                                            | Protecting on-flash records                                  |
| `hal::FlashStub`, `hal::BlockDeviceStub`, `infra::EventDispatcherFixture` | Tests                                                        |

`hal::BlockDevice` needs one extension before the FAT volume can give its durability guarantees: a `Flush` operation that writes back the device's own cache, for eMMC cache flush and similar commands.
The meaning of `EraseBlocks` is documented as a discard: after it completes, the contents of the blocks are undefined, and the device may reuse the space internally.
Both changes are made in EmIL; see [Roadmap.md](Roadmap.md).

`hal::Flash` is used unchanged.
The properties the NOR filesystem needs beyond `hal::Flash`, such as the smallest unit that can be programmed, are passed in a `fs::NorConfig` structure.

## Execution model

### Rules

Every operation that may touch a storage device takes a completion callback and returns immediately.
The callback is always invoked from the event dispatcher, never from inside the call that started the operation.
A callback may start the next operation directly; the stack does not grow with the length of a chain of operations.
All calls are made from the event dispatcher context; the library holds no locks.
Buffers and path strings passed to an operation stay valid until its completion is called; the library does not copy them.
Getters that need no device access, such as `File::Size()` and `File::Position()`, are synchronous, and `File::Seek()` only moves the position; the device is accessed by the next read or write.

### Operation queue

Each volume executes one operation at a time and issues at most one device request at a time, which is what `hal::BlockDevice` and `hal::Flash` drivers support.
An operation claims the `fs::OperationQueue` of its volume before its first device request and releases it when it completes; claims are granted in order, always from the event dispatcher.
The queue is bounded by what can claim it: every handle has one call in flight at most, and the volume has a fixed number of slots for the operations on paths, such as `Open`, `Remove` and `Rename`.
A call that finds its handle busy, or no free slot, completes with `Result::busy`; such completions wait in a bounded `fs::DeferredCompletions` until the event dispatcher delivers them.
A queue must outlive the claims on it, so its claimers are declared after it; a volume may then be destroyed while a device request is still in flight, as the power-cut sweep does.

Background work, such as erasing obsolete NOR blocks or scanning the FAT for free clusters, runs only when the queue is empty and after a configurable idle delay.
The idle delay starts when a foreground operation releases the queue and no other operation is waiting, so a volume without work never wakes the processor.
Background work claims the queue like an operation, and checks before every device request whether a foreground operation is waiting; if one is, it releases the queue and continues after the next idle delay.
The queue notifies the volume of each release of a foreground operation, which the fallback of static wear leveling on NOR volumes uses to claim the queue ahead of waiting operations, as described in [NorFilesystem.md](NorFilesystem.md#static-wear-leveling).
A foreground operation that arrives while background work is running therefore waits for at most one device request, for instance one sector erase, or on a NOR volume for one checkpoint or one block move, which run as one unit; W-6 in [Metrics.md](Metrics.md) measures this.

### Lifecycle state machines

The lifecycle of a volume is a `services::TableStateMachine` from EmIL, as described in its `docs/Fsm.md`:

```text
Unmounted --Mount--> Mounting --Mounted--> Mounted --Unmount--> Unmounting --Unmounted--> Unmounted
                      |    ^                  |                      |
                   Failed  Mount        MediaRemoved           MediaRemoved
                      |    |                  |                      |
                      +--> Fault <------------+----------------------+
```

A volume that cannot be mounted, because the device fails or the volume is corrupted, ends in `Fault`, and so does a volume whose medium is removed; `Mount` starts again from `Fault`.
A NOR volume has one more state, `Formatting`, which `fs::NorFileSystem::Format` enters from `Unmounted` or `Fault` and leaves for `Unmounted`; NOR flash is not removable, so it has no `MediaRemoved`.
A FAT volume reports the corruption that an operation finds to that operation and stays mounted, so that other files remain accessible, as described in [Fat.md](Fat.md#corruption).
Background work has its own machine, for instance `Stopped`, `Waiting`, `Claiming` and `Working` on NOR volumes, where each unit of work is a checkpoint, a sector erase or a block move.
Device completions are passed to the machine through `Completion<Ev>()`, so a completion that arrives after the machine has left the state that started it, for instance after the card was removed, is discarded instead of corrupting the volume.
The completions of `Mount` and `Unmount` are delivered through the deferred completions, so a callback that mounts or unmounts again does not re-enter the machine while it is still dispatching.

### Operations as explicit state machines

Each multi-step operation, such as reading a range of a FAT file, is a small class whose steps are named member functions.
Each step starts one device request and names the step that continues when it completes.
There is no generic sequencing framework; the control flow is visible in the names of the steps.

```cpp
void FatReadOperation::Start()
{
    if (file.ExtentCache().Contains(position))
        ReadSectors();
    else
        WalkClusterChain();
}

void FatReadOperation::ReadSectors()
{
    cache.Read(SectorOfPosition(), destination, [this](fs::Result result)
        {
            OnSectorsRead(result);
        });
}

void FatReadOperation::OnSectorsRead(fs::Result result)
{
    if (result != fs::Result::success || Done())
        Complete(result);
    else
        Start();
}
```

Because a volume runs one operation at a time, the state of the running operation lives in one `std::variant` of operation classes inside the volume, emplaced when the operation is granted the queue.
The parameters of a call that waits for the queue stay in the slot of its handle or of its path operation, so the number of operations in flight is bounded at compile time.

## Public API

The interfaces are in `fs/interfaces`.
They follow EmIL conventions: a protected constructor, deleted copy operations and a protected, non-virtual destructor.

```cpp
namespace fs
{
    enum class Result : uint8_t
    {
        success,
        notFound,
        alreadyExists,
        notADirectory,
        isADirectory,
        notEmpty,
        invalidPath,
        nameTooLong,
        noSpace,
        tooManyOpenFiles,
        busy,
        readOnly,
        notMounted,
        unsupported,
        corrupted,
        mediaRemoved,
        deviceError
    };

    enum class OpenMode : uint8_t
    {
        read,
        write,
        append,
        readWrite
    };

    enum class EntryType : uint8_t
    {
        file,
        directory
    };

    struct EntryInfo
    {
        infra::BoundedConstString name;
        EntryType type{ EntryType::file };
        uint32_t size{ 0 };
    };

    struct VolumeUsage
    {
        uint64_t usedBytes{ 0 };
        uint64_t totalBytes{ 0 };
    };

    class File
    {
    public:
        virtual uint32_t Size() const = 0;
        virtual uint32_t Position() const = 0;
        virtual void Seek(uint32_t position) = 0;

        virtual void Read(infra::ByteRange buffer, const infra::Function<void(Result result, std::size_t bytesRead)>& onDone) = 0;
        virtual void Write(infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone) = 0;
        virtual void Truncate(const infra::Function<void(Result result)>& onDone) = 0;
        virtual void ExpectSize(uint32_t size, const infra::Function<void(Result result)>& onDone) = 0;
        virtual void Sync(const infra::Function<void(Result result)>& onDone) = 0;
        virtual void Close(const infra::Function<void(Result result)>& onDone) = 0;
    };

    class Directory
    {
    public:
        virtual void Next(const infra::Function<void(Result result, std::optional<EntryInfo> entry)>& onDone) = 0;
    };

    class FileSystem
    {
    public:
        virtual void Mount(const infra::Function<void(Result result)>& onDone) = 0;
        virtual void Unmount(const infra::Function<void(Result result)>& onDone) = 0;

        virtual void Open(infra::BoundedConstString path, OpenMode mode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone) = 0;
        virtual void OpenDirectory(infra::BoundedConstString path, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone) = 0;
        virtual void Stat(infra::BoundedConstString path, const infra::Function<void(Result result, const EntryInfo& info)>& onDone) = 0;
        virtual void Remove(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone) = 0;
        virtual void Rename(infra::BoundedConstString from, infra::BoundedConstString to, const infra::Function<void(Result result)>& onDone) = 0;
        virtual void MakeDirectory(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone) = 0;
        virtual void Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone) = 0;
    };
}
```

Formatting a volume is not part of `FileSystem`: FAT volumes are formatted by `fs::FatFormatter`, and NOR volumes by `fs::NorFileSystem::Format`.

### Open modes

| Mode        | A missing file | An existing file | Position after `Open` | Writing                                                       |
|-------------|----------------|------------------|-----------------------|---------------------------------------------------------------|
| `read`      | `notFound`     | Kept             | 0                     | `Write`, `Truncate` and `ExpectSize` complete with `readOnly` |
| `write`     | Created        | Truncated to 0   | 0                     | At the position                                               |
| `append`    | Created        | Kept             | `Size()`              | Always at the end; `Seek` only affects `Read`                 |
| `readWrite` | Created        | Kept             | 0                     | At the position                                               |

Opening a directory as a file completes with `isADirectory`.
A missing parent directory completes with `notFound`, and a parent that is a file with `notADirectory`.

### Files

`Read` stops at the end of the file: it completes with `success` and fewer bytes than requested, or with 0 bytes at the end.
A `Write` that fails leaves `Position()` and `Size()` at the bytes that were actually written.
`Seek` beyond the end of the file moves the position to the end.
`Truncate` cuts the file at the current position.
`ExpectSize` announces the size the file will reach, so that the filesystem can reserve contiguous space; it never changes `Size()`, and it may complete with `unsupported`.
A volume that is configured without a call, or without a use of one such as creating a long name or writing before the end of a file, with `fs::FatFeatures` or `fs::NorFeatures`, completes it with `unsupported`, so that its code is not linked.
`Sync` makes everything written so far durable, including on power loss, before it completes.
`Close` writes back pending data, commits it and releases the handle.
Each handle runs one call at a time: a call made while another call on the same handle is pending completes with `busy`.
Calling a file after `Close` is a programming error.

### Directories

`Next` returns the entries of a directory one by one, in no particular order, and never `.` or `..`.
The end of the directory is `success` without an entry.
The name of an entry is valid only during the callback.
Entries that are added or removed during the iteration may or may not be returned.
A directory handle has no `Close`; releasing the last reference returns it to its pool.
`Stat("/")` reports a directory with an empty name, and `Stat` and `Next` report a size of 0 for every directory.

### Paths and names

Paths are absolute within the volume, use `/` as separator and are encoded in UTF-8; `/` is the root directory.
A path that is empty, relative or ends with `/`, an empty component, `.`, `..` and the NUL character complete with `invalidPath`.
A name or a path longer than the limits of the volume completes with `nameTooLong`.
FAT volumes compare names case-insensitively for ASCII letters and reject the characters that FAT reserves, as described in [Fat.md](Fat.md#names); NOR volumes compare names byte by byte.

### Sharing

A file may be open in `read` mode any number of times.
A file that is open in another mode cannot be opened again, and a file that is open cannot be opened in another mode; such an `Open` completes with `busy`.
`Remove` and `Rename` of an open file or directory complete with `busy`.
`Rename` onto an existing name completes with `alreadyExists`, and so does `MakeDirectory` of an existing name or of `/`.
`Rename` of `/`, onto `/` or of a directory into its own subtree completes with `invalidPath`; `Rename` onto the name an entry already has does nothing, and one that changes only the case of the name renames it.
`Remove` of a directory that is not empty completes with `notEmpty`, and `Remove("/")` with `invalidPath`.

### Handles

`Open` and `OpenDirectory` hand out an `infra::SharedPtr` from a fixed-size pool, in the same way as EmIL hands out connections; when the pool is exhausted, they complete with `Result::tooManyOpenFiles`.
Releasing the last reference to a file without closing it writes back and commits the file in the background; errors of that commit are not reported, so call `Close` to see them.
A handle returns to its pool once the last `infra::SharedPtr` and `infra::WeakPtr` to it are released.
`Unmount` writes back every open file and completes even if handles are still referenced; afterwards, calls on those handles complete with `notMounted`, or with `mediaRemoved` after the medium was removed, also after a new `Mount`, and the handles return to their pools when they are released.

## Shared building blocks

The building blocks in `fs/common` are shared by both filesystems.
Each one is sized at compile time:

| Building block                                      | Sized by                                                                  | RAM                                                  |
|-----------------------------------------------------|---------------------------------------------------------------------------|------------------------------------------------------|
| `fs::PathLimits`                                    | The longest name and the longest path in UTF-8 bytes, 255 by default      | None: paths are not copied                           |
| `fs::OperationQueue`                                | Claimed by the handles and the path-operation slots of the volume         | A claimable resource, an idle timer and its callback |
| `fs::DeferredCompletions::WithMaxSize<N>`           | N completions with `busy` that wait for the event dispatcher              | N completions                                        |
| `fs::HandlePool<Handle, N>`                         | N handles, constructed once with the volume                               | N handles                                            |
| `fs::SectorCache::WithStorage<Sectors, SectorSize>` | Sectors buffers of SectorSize bytes, 512 by default; see [Fat.md](Fat.md) | Sectors buffers and their entries                    |
| `fs::FlushBarrier`                                  | One per block device, in front of it                                      | The lowest rank written since the last flush         |
| `fs::DirectTransfers`                               | One per volume, next to its sector cache                                  | One transfer in flight                               |
| `fs::CrcRecord`                                     | The buffer of the caller                                                  | None: it frames and checks records in place          |

The parts of `fs/fat` that only read sectors, the reader of the FAT and the reader of directories, read them through `fs::SectorSource`, which `fs::SectorCache` implements, so that `fs::FatReader` runs them on its one sector buffer.

### Paths

A path is validated once, in one pass, when the operation that receives it starts, with `fs::ValidatePath`.
It is then walked one component at a time with `fs::PathComponents`, so that each step of a lookup handles one name and the walk needs no recursion.
`fs::SplitLast` splits a path into its parent directory and its last name, for the operations that create, remove or rename an entry.
Components are views into the path of the caller; nothing is copied.

### Pooled handles

Handles are constructed once, with the volume, by `fs::HandlePool`, and live as long as the volume.
`Allocate` hands out an `infra::SharedPtr` to a free handle, or none when every handle is in use.
An operation in flight holds an `infra::SharedPtr` to its own handle, so a handle is never unreferenced while it has work to do.
When the last `infra::SharedPtr` and `infra::WeakPtr` to a handle are released, the handle is notified: a file starts its implicit close, and a directory has nothing left to do.
The handle returns to the pool when it calls `Release`.

### Ordered writes

Every write of a FAT volume carries the rank of the sector cache described in [Fat.md](Fat.md), and goes through `fs::FlushBarrier`, which calls `hal::BlockDevice::Flush` before a write when a write of a lower rank has not been flushed yet.
`SectorCache::Flush` writes back every modified sector in rank order and then flushes the device, which is what `Sync` and the barriers between the steps of an operation use.
`fs::DirectTransfers` moves whole sectors between a buffer and the device: a read is overlaid with the sectors the cache holds, and a write or a discard first drops them from the cache.

### Records

`fs::CrcRecord` frames a record in a buffer as its length, its number of entries, the entries and a CRC, and checks a record that was read back, as described in [NorFilesystem.md](NorFilesystem.md#records); the entries themselves belong to the filesystem that writes them.

### Device results

The results of `hal::BlockDevice` are mapped onto `fs::Result` by `fs::ToResult`:

| `hal::BlockDevice::Result`         | `fs::Result`   | Because                                                |
|------------------------------------|----------------|--------------------------------------------------------|
| `success`                          | `success`      |                                                        |
| `notPresent`                       | `mediaRemoved` | The card was removed                                   |
| `writeProtected`                   | `readOnly`     | The card is locked                                     |
| `outOfRange`                       | `corrupted`    | The volume points outside the device                   |
| `timeout`, `crcError` and `failed` | `deviceError`  | The device failed; the medium itself may still be good |

The completions of `hal::Flash` carry no result, so a NOR volume never completes with `deviceError`; a header, record or node whose CRC does not match completes the operation with `corrupted`.

## Configuration

All storage is sized at compile time, following the `WithStorage` pattern of EmIL.

```cpp
hal::BlockDevice& sdCard = ...;
fs::FatFileSystem::WithStorage<4, 8> fatVolume{ sdCard, fs::FatConfig{ .blocksPerAllocationUnit = 8192, .timerServiceId = infra::localTimeTimerServiceId } };

constexpr fs::NorConfig norConfig{
    .maxBlocks = 4096,
    .sectorsPerBlock = 1,
    .programUnit = 1,
    .pageSize = 256,
};

hal::Flash& norFlash = ...;
fs::NorFileSystem::WithStorage<norConfig, 4> norVolume{ norFlash };
```

For FAT, the template arguments are the number of open files and the number of sectors in the cache, followed by an optional `fs::FatLimits` with the other sizes and an optional `fs::FatFeatures` with the calls and the work the volume leaves out; `fs::FatConfig` holds the settings that need no storage, such as the allocation unit of the card and the timer service that gives the time of timestamps, as described in [Fat.md](Fat.md#configuration).
`fs::FatReader::WithStorage<Files, Directories, Limits>` takes the number of open files and of open directory handles, and an optional `fs::PathLimits`, as described in [Fat.md](Fat.md#read-only-volume).
For NOR, they are the configuration, the number of open files and an optional `fs::NorFeatures`; the constructor takes the device and an optional idle delay, as described in [NorFilesystem.md](NorFilesystem.md#configuration).
The block states and the erase-count table are sized by `maxBlocks`, the largest number of blocks the volume may span; mounting a device with more blocks fails with `Result::unsupported`.

## Testing

Every component is developed test first, against the EmIL test doubles and the metered devices of the metrics harness.
See [Testing.md](Testing.md).
