#ifndef FS_FAT_READER_TEST_FAT_READER_FIXTURE_HPP
#define FS_FAT_READER_TEST_FAT_READER_FIXTURE_HPP

#include "fs/common/LittleEndian.hpp"
#include "fs/fat/FatFormatter.hpp"
#include "fs/fat/reader/FatReader.hpp"
#include "fs/fat/test_doubles/FatImageBuilder.hpp"
#include "fs/fat/test_doubles/SparseBlockDevice.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string_view>

namespace fs
{
    inline void PrintTo(const EntryInfo& info, std::ostream* stream)
    {
        *stream << R"({ ")" << std::string_view(info.name.data(), info.name.size()) << R"(", )" << (info.type == EntryType::directory ? "directory" : "file") << ", " << info.size << " }";
    }
}

struct FatReaderVolumeCase
{
    const char* name;
    uint32_t blocks;
    uint8_t sectorsPerCluster;
    bool partitioned;
};

inline void PrintTo(const FatReaderVolumeCase& volumeCase, std::ostream* stream)
{
    *stream << volumeCase.name;
}

inline const FatReaderVolumeCase fat12Volume{ "fat12", 16384, 16, true };
inline const FatReaderVolumeCase fat16Volume{ "fat16", 16384, 1, true };
inline const FatReaderVolumeCase fat16WithoutPartitionTable{ "fat16_without_partition_table", 16384, 1, false };
inline const FatReaderVolumeCase fat32Volume{ "fat32", 81920, 1, true };

template<std::size_t Size>
std::array<uint8_t, Size> Pattern()
{
    std::array<uint8_t, Size> pattern;

    for (std::size_t index = 0; index != Size; ++index)
        pattern[index] = static_cast<uint8_t>(index * 31 + index / 509);

    return pattern;
}

class FatReaderFixture
    : public infra::ClockFixture
{
public:
    using Sector = std::array<uint8_t, 512>;

    explicit FatReaderFixture(const FatReaderVolumeCase& volumeCase = fat16Volume)
        : device(volumeCase.blocks)
    {
        fs::FatFormatConfig config;
        config.sectorsPerCluster = volumeCase.sectorsPerCluster;
        config.partitioned = volumeCase.partitioned;
        std::optional<fs::Result> formatted;
        formatter.Format(config, [&formatted](fs::Result result)
            {
                formatted = result;
            });
        ExecuteAllActions();
        EXPECT_EQ(fs::Result::success, formatted);
        builder.emplace(device);
    }

    Sector ReadSector(uint32_t block) const
    {
        Sector sector;
        device.ReadBlock(block, infra::MakeRange(sector));
        return sector;
    }

    void WriteSector(uint32_t block, const Sector& sector)
    {
        device.WriteBlock(block, infra::MakeRange(sector));
    }

    uint32_t PartitionStart() const
    {
        return fs::LoadLittle32(infra::MakeRange(ReadSector(0)), 446 + 8);
    }

    void StartMount()
    {
        volume.Mount([this](fs::Result result)
            {
                mountDone.callback(result);
            });
    }

    void ExpectMount(fs::Result result = fs::Result::success)
    {
        StartMount();
        EXPECT_CALL(mountDone, callback(result));
        ExecuteAllActions();
    }

    void StartUnmount()
    {
        volume.Unmount([this](fs::Result result)
            {
                unmountDone.callback(result);
            });
    }

    void ExpectUnmount(fs::Result result = fs::Result::success)
    {
        StartUnmount();
        EXPECT_CALL(unmountDone, callback(result));
        ExecuteAllActions();
    }

    void StartStat(infra::BoundedConstString path)
    {
        StartStat(volume, path);
    }

    void StartStat(fs::FileSystem& fileSystem, infra::BoundedConstString path)
    {
        fileSystem.Stat(path, [this](fs::Result result, const fs::EntryInfo& info)
            {
                statDone.callback(result, info);
            });
    }

    void ExpectStat(infra::BoundedConstString path, fs::Result result, const fs::EntryInfo& info = {})
    {
        ExpectStat(volume, path, result, info);
    }

    void ExpectStat(fs::FileSystem& fileSystem, infra::BoundedConstString path, fs::Result result, const fs::EntryInfo& info = {})
    {
        StartStat(fileSystem, path);
        EXPECT_CALL(statDone, callback(result, info));
        ExecuteAllActions();
    }

    infra::SharedPtr<fs::File> ExpectOpen(infra::BoundedConstString path, fs::Result result = fs::Result::success, fs::OpenMode mode = fs::OpenMode::read)
    {
        infra::SharedPtr<fs::File> opened;
        volume.Open(path, mode, [this, &opened](fs::Result result, infra::SharedPtr<fs::File> file)
            {
                opened = file;
                openDone.callback(result);
            });
        EXPECT_CALL(openDone, callback(result));
        ExecuteAllActions();
        EXPECT_EQ(result == fs::Result::success, opened != nullptr);
        return opened;
    }

    infra::SharedPtr<fs::Directory> ExpectOpenDirectory(infra::BoundedConstString path, fs::Result result = fs::Result::success)
    {
        return ExpectOpenDirectory(volume, path, result);
    }

    infra::SharedPtr<fs::Directory> ExpectOpenDirectory(fs::FileSystem& fileSystem, infra::BoundedConstString path, fs::Result result = fs::Result::success)
    {
        infra::SharedPtr<fs::Directory> opened;
        fileSystem.OpenDirectory(path, [this, &opened](fs::Result result, infra::SharedPtr<fs::Directory> directory)
            {
                opened = directory;
                openDone.callback(result);
            });
        EXPECT_CALL(openDone, callback(result));
        ExecuteAllActions();
        EXPECT_EQ(result == fs::Result::success, opened != nullptr);
        return opened;
    }

    void StartRead(fs::File& file, infra::ByteRange buffer)
    {
        file.Read(buffer, [this](fs::Result result, std::size_t bytesRead)
            {
                readDone.callback(result, bytesRead);
            });
    }

    void ExpectRead(fs::File& file, infra::ByteRange buffer, fs::Result result, std::size_t bytesRead)
    {
        StartRead(file, buffer);
        EXPECT_CALL(readDone, callback(result, bytesRead));
        ExecuteAllActions();
    }

    void StartNext(fs::Directory& directory)
    {
        directory.Next([this](fs::Result result, std::optional<fs::EntryInfo> entry)
            {
                nextDone.callback(result, entry);
            });
    }

    void ExpectNext(fs::Directory& directory, fs::Result result, const std::optional<fs::EntryInfo>& entry = std::nullopt)
    {
        StartNext(directory);
        EXPECT_CALL(nextDone, callback(result, entry));
        ExecuteAllActions();
    }

    infra::Function<void(fs::Result)> Done()
    {
        return [this](fs::Result result)
        {
            done.callback(result);
        };
    }

    void ExpectDone(fs::Result result)
    {
        EXPECT_CALL(done, callback(result));
        ExecuteAllActions();
    }

    fs::SparseBlockDevice::WithStorage<192> device;
    fs::FatFormatter::WithStorage<8> formatter{ device };
    std::optional<fs::FatImageBuilder> builder;
    fs::FatReader::WithStorage<2, 2> volume{ device };
    testing::StrictMock<infra::MockCallback<void(fs::Result)>> mountDone;
    testing::StrictMock<infra::MockCallback<void(fs::Result)>> unmountDone;
    testing::StrictMock<infra::MockCallback<void(fs::Result, const fs::EntryInfo&)>> statDone;
    testing::StrictMock<infra::MockCallback<void(fs::Result)>> openDone;
    testing::StrictMock<infra::MockCallback<void(fs::Result, std::size_t)>> readDone;
    testing::StrictMock<infra::MockCallback<void(fs::Result, std::optional<fs::EntryInfo>)>> nextDone;
    testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    std::array<uint8_t, 8192> buffer{};
};

class FatReaderTest
    : public testing::Test
    , public FatReaderFixture
{};

class FatReaderEveryTypeTest
    : public testing::TestWithParam<FatReaderVolumeCase>
    , public FatReaderFixture
{
public:
    FatReaderEveryTypeTest()
        : FatReaderFixture(GetParam())
    {}
};

#endif
