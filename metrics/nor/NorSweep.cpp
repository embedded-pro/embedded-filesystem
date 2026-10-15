#include "metrics/nor/NorSweep.hpp"
#include "fs/nor/NorFormatter.hpp"
#include "fs/nor/NorRecord.hpp"
#include <chrono>

namespace metrics
{
    using namespace std::chrono_literals;

    namespace
    {
        constexpr NorTiming qspiTypical{ 400us, 45ms, 50'000'000 };
        constexpr NorTiming qspiMaximum{ 3ms, 400ms, 50'000'000 };
        constexpr NorTiming internalTiming{ 8us, 20ms, 100'000'000 };

        void Wait(SweepContext<hal::Flash>& context, const bool& done)
        {
            context.time.RunUntil([&done]
                {
                    return done;
                });
        }
    }

    const NorModel norSweep128k{ "nor-128k", 32, 4096, 256, 1, qspiTypical, qspiMaximum };
    const NorModel internalSweep96k{ "internal-96k", 48, 2048, 16, 16, internalTiming, internalTiming };
    const NorModel norSweep128kTwoSectors{ "nor-128k-two-sector-blocks", 64, 2048, 256, 1, qspiTypical, qspiMaximum };

    fs::NorConfig SweepConfig(const NorModel& model)
    {
        if (&model == &internalSweep96k)
            return internalSweep96kConfig;

        if (&model == &norSweep128kTwoSectors)
            return norSweep128kTwoSectorsConfig;

        return norSweep128kConfig;
    }

    std::size_t SweepRamBytes(const NorModel& model)
    {
        if (&model == &internalSweep96k)
            return sizeof(fs::NorFileSystem::WithStorage<internalSweep96kConfig, 1>);

        if (&model == &norSweep128kTwoSectors)
            return sizeof(fs::NorFileSystem::WithStorage<norSweep128kTwoSectorsConfig, 1>);

        return sizeof(fs::NorFileSystem::WithStorage<norSweep128kConfig, 1>);
    }

    NorImageSuperblock SuperblockOf(const NorModel& model, const fs::NorConfig& config)
    {
        return { config.sectorsPerBlock, model.sectorSize, model.numberOfSectors / config.sectorsPerBlock, config.programUnit, config.nodeSize, config.inlineBytes, config.maxNameBytes };
    }

    std::optional<fs::Result> Format(SweepContext<hal::Flash>& context, const fs::NorConfig& config)
    {
        std::vector<uint8_t> buffer(fs::NorFormatter::BufferBytes(config));
        fs::NorFormatter formatter{ context.device, config, infra::MakeRange(buffer) };
        std::optional<fs::Result> result;
        formatter.Format([&result](fs::Result formatted)
            {
                result = formatted;
            });
        context.time.RunUntil([&result]
            {
                return result.has_value();
            });
        return result;
    }

    std::vector<uint8_t> ReadImage(SweepContext<hal::Flash>& context)
    {
        std::vector<uint8_t> image;

        for (uint32_t sector = 0; sector != context.device.NumberOfSectors(); ++sector)
        {
            std::vector<uint8_t> contents(context.device.SizeOfSector(sector));
            bool done = false;
            context.device.ReadBuffer(infra::MakeRange(contents), context.device.AddressOfSector(sector), [&done]
                {
                    done = true;
                });
            Wait(context, done);
            image.insert(image.end(), contents.begin(), contents.end());
        }

        return image;
    }

    NorCheck CheckImage(SweepContext<hal::Flash>& context, const NorModel& model)
    {
        return CheckNorImage(ReadImage(context), model.sectorSize, model.pageSize);
    }

    void Program(SweepContext<hal::Flash>& context, uint32_t address, const std::vector<uint8_t>& bytes)
    {
        bool done = false;
        context.device.WriteBuffer(infra::MakeRange(bytes), address, [&done]
            {
                done = true;
            });
        Wait(context, done);
    }

    void EraseSector(SweepContext<hal::Flash>& context, uint32_t sector)
    {
        bool done = false;
        context.device.EraseSectors(sector, sector + 1, [&done]
            {
                done = true;
            });
        Wait(context, done);
    }

    namespace detail
    {
        NorSweepVolumeStorage::NorSweepVolumeStorage(const fs::NorConfig& config)
            : blockStates(fs::NorBlockStateBytes(config.maxBlocks))
            , eraseCounts(config.maxBlocks)
            , journal(fs::RoundUpTo(static_cast<uint32_t>(fs::NorLargestRecordBytes(config.inlineBytes, config.maxNameBytes)), config.programUnit))
            , nodes(config.cachedNodes * config.nodeSize)
            , nodeSlots(config.cachedNodes)
        {}
    }

    NorSweepVolume::FileSystem::FileSystem(hal::Flash& flash, const fs::NorConfig& config)
        : NorSweepVolumeStorage(config)
        , fs::NorFileSystem(fs::NorVolumeParts{ flash, deferred, infra::MakeRange(blockStates), infra::MakeRange(eraseCounts), infra::MakeRange(journal), infra::MakeRange(nodes), infra::MakeRange(nodeSlots) }, config, defaultIdleDelay)
    {
        for (std::size_t index = 0; index != config.pathOperations; ++index)
            pathSlots.emplace_back(Volume().Queue());

        AttachPathSlots(infra::MakeRange(pathSlots));
    }

    bool NorSweepVolume::FileSystem::JournalMoreThanHalfFull()
    {
        return Volume().Journal().IsMoreThanHalfFull();
    }

    infra::SharedPtr<fs::NorDirectory> NorSweepVolume::FileSystem::AllocateDirectory()
    {
        return directories.Allocate();
    }

    infra::SharedPtr<fs::NorFile> NorSweepVolume::FileSystem::AllocateFile()
    {
        return files.Allocate();
    }

    NorSweepVolume::NorSweepVolume(SweepContext<hal::Flash>& context, const fs::NorConfig& config)
        : context(context)
        , fileSystem(context.device, config)
    {}

    fs::Result NorSweepVolume::Format()
    {
        return Run([this](const infra::Function<void(fs::Result)>& onDone)
            {
                fileSystem.Format(onDone);
            });
    }

    fs::Result NorSweepVolume::Mount()
    {
        return Run([this](const infra::Function<void(fs::Result)>& onDone)
            {
                fileSystem.Mount(onDone);
            });
    }

    fs::Result NorSweepVolume::Unmount()
    {
        return Run([this](const infra::Function<void(fs::Result)>& onDone)
            {
                fileSystem.Unmount(onDone);
            });
    }

    fs::Result NorSweepVolume::MakeDirectory(const std::string& path)
    {
        return Run([this, &path](const infra::Function<void(fs::Result)>& onDone)
            {
                fileSystem.MakeDirectory(infra::BoundedConstString(path.data(), path.size()), onDone);
            });
    }

    fs::Result NorSweepVolume::Remove(const std::string& path)
    {
        return Run([this, &path](const infra::Function<void(fs::Result)>& onDone)
            {
                fileSystem.Remove(infra::BoundedConstString(path.data(), path.size()), onDone);
            });
    }

    fs::Result NorSweepVolume::Rename(const std::string& from, const std::string& to)
    {
        return Run([this, &from, &to](const infra::Function<void(fs::Result)>& onDone)
            {
                fileSystem.Rename(infra::BoundedConstString(from.data(), from.size()), infra::BoundedConstString(to.data(), to.size()), onDone);
            });
    }

    void NorSweepVolume::Settle()
    {
        context.time.RunUntilIdle();
    }

    bool NorSweepVolume::JournalMoreThanHalfFull()
    {
        return fileSystem.JournalMoreThanHalfFull();
    }

    std::optional<fs::EntryType> NorSweepVolume::TypeOf(const std::string& path)
    {
        std::optional<fs::Result> result;
        fs::EntryType type{ fs::EntryType::file };
        fileSystem.Stat(infra::BoundedConstString(path.data(), path.size()), [&result, &type](fs::Result found, const fs::EntryInfo& info)
            {
                type = info.type;
                result = found;
            });
        context.time.RunUntil([&result]
            {
                return result.has_value();
            });

        if (result != fs::Result::success)
            return std::nullopt;

        return type;
    }

    fs::Result NorSweepVolume::OpenFile(const std::string& path, fs::OpenMode mode)
    {
        std::optional<fs::Result> result;
        fileSystem.Open(infra::BoundedConstString(path.data(), path.size()), mode, [this, &result](fs::Result opened, infra::SharedPtr<fs::File> openedFile)
            {
                file = openedFile;
                result = opened;
            });
        context.time.RunUntil([&result]
            {
                return result.has_value();
            });
        return result.value_or(fs::Result::deviceError);
    }

    fs::Result NorSweepVolume::Write(const std::vector<uint8_t>& data)
    {
        return Run([this, &data](const infra::Function<void(fs::Result)>& onDone)
            {
                file->Write(infra::MakeRange(data), onDone);
            });
    }

    void NorSweepVolume::Seek(uint32_t position)
    {
        file->Seek(position);
    }

    fs::Result NorSweepVolume::Truncate()
    {
        return Run([this](const infra::Function<void(fs::Result)>& onDone)
            {
                file->Truncate(onDone);
            });
    }

    fs::Result NorSweepVolume::Sync()
    {
        return Run([this](const infra::Function<void(fs::Result)>& onDone)
            {
                file->Sync(onDone);
            });
    }

    fs::Result NorSweepVolume::Close()
    {
        auto result = Run([this](const infra::Function<void(fs::Result)>& onDone)
            {
                file->Close(onDone);
            });
        file = nullptr;
        return result;
    }

    std::optional<std::vector<uint8_t>> NorSweepVolume::ReadFile(const std::string& path)
    {
        if (OpenFile(path, fs::OpenMode::read) != fs::Result::success)
            return std::nullopt;

        std::vector<uint8_t> contents(file->Size());
        std::optional<fs::Result> result;
        file->Read(infra::MakeRange(contents), [&result](fs::Result read, std::size_t)
            {
                result = read;
            });
        context.time.RunUntil([&result]
            {
                return result.has_value();
            });
        Close();

        if (result != fs::Result::success)
            return std::nullopt;

        return contents;
    }

    template<class Start>
    fs::Result NorSweepVolume::Run(const Start& start)
    {
        std::optional<fs::Result> result;
        start([&result](fs::Result completed)
            {
                result = completed;
            });
        context.time.RunUntil([&result]
            {
                return result.has_value();
            });
        return result.value_or(fs::Result::deviceError);
    }
}
