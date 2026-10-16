#include "spike/fat_reader/FatReader.hpp"
#include "fs/common/Path.hpp"
#include <utility>

namespace spike
{
    FatReader::FatReader(hal::BlockDevice& device, infra::ByteRange sector, fs::DeferredCompletions& deferred)
        : volume(device, sector, deferred)
        , lifecycleClaimer(volume.Queue())
        , pathClaimer(volume.Queue())
        , lifecycle(*this, services::Validated<FatReader>())
    {
        lifecycle.OnEntered<FatReaderMounted>([](FatReader& reader, FatReaderMounted&)
            {
                reader.volume.MarkMounted();
                reader.Entered(fs::Result::success);
            });
        lifecycle.OnEntered<FatReaderFault>([](FatReader& reader, const FatReaderFault& fault)
            {
                reader.Entered(fault.result);
            });
        lifecycle.OnEntered<FatReaderUnmounted>([](FatReader& reader, FatReaderUnmounted&)
            {
                reader.Entered(fs::Result::success);
            });
        lifecycle.Start<FatReaderUnmounted>();
    }

    void FatReader::Mount(const infra::Function<void(fs::Result result)>& onDone)
    {
        if (lifecycle.Dispatch(FatReaderMountRequested{}) == services::DispatchResult::transitioned)
            onMountDone = onDone;
        else
            volume.Defer(onDone, fs::Result::busy);
    }

    void FatReader::Unmount(const infra::Function<void(fs::Result result)>& onDone)
    {
        if (lifecycle.Dispatch(FatReaderUnmountRequested{}) == services::DispatchResult::transitioned)
            onUnmountDone = onDone;
        else if (std::holds_alternative<FatReaderMounting>(lifecycle.CurrentState()) || std::holds_alternative<FatReaderUnmounting>(lifecycle.CurrentState()))
            volume.Defer(onDone, fs::Result::busy);
        else
            volume.Defer(onDone, fs::Result::notMounted);
    }

    void FatReader::Open(infra::BoundedConstString path, fs::OpenMode mode, const infra::Function<void(fs::Result result, infra::SharedPtr<fs::File> file)>& onDone)
    {
        auto refusal = mode == fs::OpenMode::read ? Refusal(path) : Unavailable();

        if (refusal != fs::Result::success)
            volume.Defer(onDone, refusal);
        else
            Start(path, FatReaderPathCall{ std::in_place_index<1>, onDone });
    }

    void FatReader::OpenDirectory(infra::BoundedConstString path, const infra::Function<void(fs::Result result, infra::SharedPtr<fs::Directory> directory)>& onDone)
    {
        if (auto refusal = Refusal(path); refusal != fs::Result::success)
            volume.Defer(onDone, refusal);
        else
            Start(path, FatReaderPathCall{ std::in_place_index<2>, onDone });
    }

    void FatReader::Stat(infra::BoundedConstString path, const infra::Function<void(fs::Result result, const fs::EntryInfo& info)>& onDone)
    {
        if (auto refusal = Refusal(path); refusal != fs::Result::success)
            volume.Defer(onDone, refusal);
        else
            Start(path, FatReaderPathCall{ std::in_place_index<0>, onDone });
    }

    void FatReader::Remove(infra::BoundedConstString, const infra::Function<void(fs::Result result)>& onDone)
    {
        volume.Defer(onDone, Unavailable());
    }

    void FatReader::Rename(infra::BoundedConstString, infra::BoundedConstString, const infra::Function<void(fs::Result result)>& onDone)
    {
        volume.Defer(onDone, Unavailable());
    }

    void FatReader::MakeDirectory(infra::BoundedConstString, const infra::Function<void(fs::Result result)>& onDone)
    {
        volume.Defer(onDone, Unavailable());
    }

    void FatReader::Usage(const infra::Function<void(fs::Result result, fs::VolumeUsage usage)>& onDone)
    {
        volume.Defer(onDone, IsMounted() ? fs::Result::unsupported : fs::Result::notMounted);
    }

    FatReaderVolume& FatReader::Volume()
    {
        return volume;
    }

    bool FatReader::IsMounted() const
    {
        return std::holds_alternative<FatReaderMounted>(lifecycle.CurrentState());
    }

    fs::Result FatReader::Refusal(infra::BoundedConstString path) const
    {
        if (!IsMounted())
            return fs::Result::notMounted;

        if (auto validation = fs::ValidatePath(path, fs::PathLimits{}); validation != fs::Result::success)
            return validation;

        return call.has_value() ? fs::Result::busy : fs::Result::success;
    }

    fs::Result FatReader::Unavailable() const
    {
        return IsMounted() ? fs::Result::readOnly : fs::Result::notMounted;
    }

    void FatReader::Start(infra::BoundedConstString path, FatReaderPathCall&& call)
    {
        this->path = path;
        this->call = std::move(call);
        pathClaimer.Claim([this]
            {
                Run();
            });
    }

    void FatReader::Run()
    {
        if (volume.Available(volume.Generation()))
            volume.Walk().Find(path, [this](fs::Result result, const FatEntry& entry)
                {
                    Found(result, entry);
                });
        else
            Found(fs::Result::notMounted, FatEntry{});
    }

    void FatReader::Found(fs::Result result, const FatEntry& entry)
    {
        auto pending = std::move(*call);
        call = std::nullopt;
        pathClaimer.Release();

        if (auto* stat = std::get_if<0>(&pending))
            (*stat)(result, result == fs::Result::success ? fs::EntryInfo{ entry.name, entry.directory ? fs::EntryType::directory : fs::EntryType::file, entry.directory ? 0 : entry.size } : fs::EntryInfo{});
        else if (auto* open = std::get_if<1>(&pending))
            OpenFile(*open, result, entry);
        else
            OpenDirectoryAt(std::get<2>(pending), result, entry);
    }

    void FatReader::OpenFile(infra::AutoResetFunction<void(fs::Result result, infra::SharedPtr<fs::File> file)>& onDone, fs::Result result, const FatEntry& entry)
    {
        infra::SharedPtr<FatReaderFile> file;

        if (result == fs::Result::success && entry.directory)
            result = fs::Result::isADirectory;
        else if (result == fs::Result::success)
            file = AllocateFile();

        if (file != nullptr)
            file->Open(entry);
        else if (result == fs::Result::success)
            result = fs::Result::tooManyOpenFiles;

        onDone(result, file);
    }

    void FatReader::OpenDirectoryAt(infra::AutoResetFunction<void(fs::Result result, infra::SharedPtr<fs::Directory> directory)>& onDone, fs::Result result, const FatEntry& entry)
    {
        infra::SharedPtr<FatReaderDirectory> directory;

        if (result == fs::Result::success && !entry.directory)
            result = fs::Result::notADirectory;
        else if (result == fs::Result::success)
            directory = AllocateDirectory();

        if (directory != nullptr)
            directory->Open(entry.firstCluster);
        else if (result == fs::Result::success)
            result = fs::Result::tooManyOpenFiles;

        onDone(result, directory);
    }

    void FatReader::BeginMount()
    {
        lifecycleClaimer.Claim([this]
            {
                volume.Window().Forget();
                volume.Window().Load(0, [this](fs::Result result)
                    {
                        FirstSectorRead(result);
                    });
            });
    }

    void FatReader::FirstSectorRead(fs::Result result)
    {
        auto& window = volume.Window();
        uint32_t start = 0;

        if (result == fs::Result::success)
            result = ParseBootSector(window.Bytes(), 0, window.Device().NumberOfBlocks(), volume.Layout());

        if (result == fs::Result::corrupted)
            result = FindPartition(window.Bytes(), start);
        else
        {
            Mounted(result);
            return;
        }

        if (result != fs::Result::success)
            Mounted(result);
        else
            window.Load(start, [this, start](fs::Result loaded)
                {
                    BootSectorRead(loaded, start);
                });
    }

    void FatReader::BootSectorRead(fs::Result result, uint32_t start)
    {
        auto& window = volume.Window();

        if (result == fs::Result::success)
            result = ParseBootSector(window.Bytes(), start, window.Device().NumberOfBlocks(), volume.Layout());

        Mounted(result);
    }

    void FatReader::Mounted(fs::Result result)
    {
        lifecycleClaimer.Release();
        lifecycle.Dispatch(FatReaderMountDone{ result });
    }

    void FatReader::BeginUnmount()
    {
        lifecycleClaimer.Claim([this]
            {
                volume.MarkUnmounted();
                lifecycleClaimer.Release();
                lifecycle.Dispatch(FatReaderUnmountDone{});
            });
    }

    void FatReader::Entered(fs::Result result)
    {
        if (onMountDone)
            volume.Defer(std::exchange(onMountDone, nullptr), result);
        if (onUnmountDone)
            volume.Defer(std::exchange(onUnmountDone, nullptr), result);
    }
}
