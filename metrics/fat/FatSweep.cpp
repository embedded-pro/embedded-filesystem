#include "metrics/fat/FatSweep.hpp"
#include "fs/fat/FatFormatter.hpp"
#include <algorithm>
#include <chrono>

namespace metrics
{
    using namespace std::chrono_literals;

    namespace
    {
        constexpr BlockDeviceTiming typicalTiming{ 100us, 25us, 50us, 0us, 10ms };
        constexpr BlockDeviceTiming maximumTiming{ 100us, 25us, 50us, 250ms, 10ms };
    }

    const BlockDeviceModel fatSweep12{ "fat12-2m", 512, 4096, 64, 2, typicalTiming, maximumTiming };
    const BlockDeviceModel fatSweep16{ "fat16-8m", 512, 16384, 64, 2, typicalTiming, maximumTiming };
    const BlockDeviceModel fatSweep32{ "fat32-40m", 512, 81920, 64, 2, typicalTiming, maximumTiming };

    fs::FatFormatConfig SweepFormat(const BlockDeviceModel& model)
    {
        fs::FatFormatConfig config;

        if (model.numberOfBlocks > fatSweep12.numberOfBlocks)
            config.sectorsPerCluster = 1;

        return config;
    }

    std::optional<fs::Result> Format(SweepContext<hal::BlockDevice>& context, const fs::FatFormatConfig& config)
    {
        fs::FatFormatter::WithStorage<8> formatter{ context.device };
        std::optional<fs::Result> result;
        formatter.Format(config, [&result](fs::Result formatted)
            {
                result = formatted;
            });
        context.time.RunUntil([&result]
            {
                return result.has_value();
            });
        return result;
    }

    SectorReader ReaderOf(SweepContext<hal::BlockDevice>& context)
    {
        return [&context](uint32_t sector)
        {
            std::vector<uint8_t> contents(context.device.BlockSize());
            bool done = false;
            context.device.ReadBlocks(infra::MakeRange(contents), sector, [&done](hal::BlockDevice::Result)
                {
                    done = true;
                });
            context.time.RunUntil([&done]
                {
                    return done;
                });
            return contents;
        };
    }
}

namespace metrics
{
    SweepImage::SweepImage(SweepContext<hal::BlockDevice>& context)
        : context(context)
    {}

    uint32_t SweepImage::Blocks() const
    {
        return context.device.NumberOfBlocks();
    }

    void SweepImage::ReadBlock(uint32_t block, infra::ByteRange out) const
    {
        auto contents = ReaderOf(context)(block);
        std::copy(contents.begin(), contents.begin() + static_cast<std::ptrdiff_t>(out.size()), out.begin());
    }

    void SweepImage::WriteBlock(uint32_t block, infra::ConstByteRange data)
    {
        bool done = false;
        context.device.WriteBlocks(data, block, [&done](hal::BlockDevice::Result)
            {
                done = true;
            });
        context.time.RunUntil([&done]
            {
                return done;
            });
    }

    const char* SubjectName(SweepSubject subject)
    {
        return subject == SweepSubject::fat ? "embedded-filesystem/fat" : "embedded-filesystem/fat-lite";
    }

    SweepVolume::SweepVolume(SweepContext<hal::BlockDevice>& context, SweepSubject subject)
        : context(context)
        , fileSystem(subject == SweepSubject::fat ? static_cast<fs::FileSystem&>(full.emplace(context.device)) : lite.emplace(context.device))
    {}

    fs::Result SweepVolume::Mount()
    {
        return Run([this](const infra::Function<void(fs::Result)>& onDone)
            {
                fileSystem.Mount(onDone);
            });
    }

    fs::Result SweepVolume::Unmount()
    {
        return Run([this](const infra::Function<void(fs::Result)>& onDone)
            {
                fileSystem.Unmount(onDone);
            });
    }

    infra::SharedPtr<fs::File> SweepVolume::Open(const std::string& path, fs::OpenMode mode)
    {
        infra::SharedPtr<fs::File> opened;
        std::optional<fs::Result> result;
        fileSystem.Open(infra::BoundedConstString(path.data(), path.size()), mode, [&opened, &result](fs::Result openResult, infra::SharedPtr<fs::File> file)
            {
                opened = file;
                result = openResult;
            });
        context.time.RunUntil([&result]
            {
                return result.has_value();
            });
        return opened;
    }

    fs::Result SweepVolume::Write(fs::File& file, infra::ConstByteRange data)
    {
        return Run([&file, data](const infra::Function<void(fs::Result)>& onDone)
            {
                file.Write(data, onDone);
            });
    }

    fs::Result SweepVolume::ExpectSize(fs::File& file, uint32_t size)
    {
        return Run([&file, size](const infra::Function<void(fs::Result)>& onDone)
            {
                file.ExpectSize(size, onDone);
            });
    }

    fs::Result SweepVolume::Truncate(fs::File& file)
    {
        return Run([&file](const infra::Function<void(fs::Result)>& onDone)
            {
                file.Truncate(onDone);
            });
    }

    fs::Result SweepVolume::Sync(fs::File& file)
    {
        return Run([&file](const infra::Function<void(fs::Result)>& onDone)
            {
                file.Sync(onDone);
            });
    }

    fs::Result SweepVolume::Close(fs::File& file)
    {
        return Run([&file](const infra::Function<void(fs::Result)>& onDone)
            {
                file.Close(onDone);
            });
    }

    fs::Result SweepVolume::Remove(const std::string& path)
    {
        return Run([this, &path](const infra::Function<void(fs::Result)>& onDone)
            {
                fileSystem.Remove(infra::BoundedConstString(path.data(), path.size()), onDone);
            });
    }

    fs::Result SweepVolume::MakeDirectory(const std::string& path)
    {
        return Run([this, &path](const infra::Function<void(fs::Result)>& onDone)
            {
                fileSystem.MakeDirectory(infra::BoundedConstString(path.data(), path.size()), onDone);
            });
    }

    fs::Result SweepVolume::Rename(const std::string& from, const std::string& to)
    {
        return Run([this, &from, &to](const infra::Function<void(fs::Result)>& onDone)
            {
                fileSystem.Rename(infra::BoundedConstString(from.data(), from.size()), infra::BoundedConstString(to.data(), to.size()), onDone);
            });
    }

    template<class Start>
    fs::Result SweepVolume::Run(const Start& start)
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
