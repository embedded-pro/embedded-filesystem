#ifndef METRICS_FAT_FAT_SWEEP_HPP
#define METRICS_FAT_FAT_SWEEP_HPP

#include "fs/fat/FatFileSystem.hpp"
#include "fs/fat/FatFormatLayout.hpp"
#include "fs/fat/test_doubles/BlockImage.hpp"
#include "fs/interfaces/Result.hpp"
#include "metrics/device/DeviceModels.hpp"
#include "metrics/fat/FatChecker.hpp"
#include "metrics/power_cut/PowerCutSweep.hpp"
#include <optional>
#include <string>

namespace metrics
{
    extern const BlockDeviceModel fatSweep12;
    extern const BlockDeviceModel fatSweep16;
    extern const BlockDeviceModel fatSweep32;

    fs::FatFormatConfig SweepFormat(const BlockDeviceModel& model);
    std::optional<fs::Result> Format(SweepContext<hal::BlockDevice>& context, const fs::FatFormatConfig& config);
    SectorReader ReaderOf(SweepContext<hal::BlockDevice>& context);

    class SweepImage
        : public fs::BlockImage
    {
    public:
        explicit SweepImage(SweepContext<hal::BlockDevice>& context);

        uint32_t Blocks() const override;
        void ReadBlock(uint32_t block, infra::ByteRange out) const override;
        void WriteBlock(uint32_t block, infra::ConstByteRange data) override;

    private:
        SweepContext<hal::BlockDevice>& context;
    };

    class SweepVolume
    {
    public:
        using FileSystem = fs::FatFileSystem::WithStorage<2, 8>;

        explicit SweepVolume(SweepContext<hal::BlockDevice>& context);

        fs::Result Mount();
        infra::SharedPtr<fs::File> Open(const std::string& path, fs::OpenMode mode);
        fs::Result Write(fs::File& file, infra::ConstByteRange data);
        fs::Result ExpectSize(fs::File& file, uint32_t size);
        fs::Result Sync(fs::File& file);
        fs::Result Close(fs::File& file);
        fs::Result Remove(const std::string& path);
        fs::Result MakeDirectory(const std::string& path);

    private:
        template<class Start>
        fs::Result Run(const Start& start);

    private:
        SweepContext<hal::BlockDevice>& context;
        FileSystem fileSystem;
    };
}

#endif
