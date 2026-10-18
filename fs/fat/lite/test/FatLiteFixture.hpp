#ifndef FS_FAT_LITE_TEST_FAT_LITE_FIXTURE_HPP
#define FS_FAT_LITE_TEST_FAT_LITE_FIXTURE_HPP

#include "fs/common/LittleEndian.hpp"
#include "fs/fat/FatFileSystem.hpp"
#include "fs/fat/FatFormatter.hpp"
#include "fs/fat/FatTable.hpp"
#include "fs/fat/lite/FatLite.hpp"
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

struct FatLiteVolumeCase
{
    const char* name;
    uint32_t blocks;
    uint8_t sectorsPerCluster;
    uint16_t rootEntries;
    uint8_t fatCopies{ 2 };
};

inline void PrintTo(const FatLiteVolumeCase& volumeCase, std::ostream* stream)
{
    *stream << volumeCase.name;
}

inline const FatLiteVolumeCase fat12Volume{ "fat12", 4096, 1, 512 };
inline const FatLiteVolumeCase fat16Volume{ "fat16", 16384, 1, 512 };
inline const FatLiteVolumeCase fat16WithSmallRoot{ "fat16_with_small_root", 16384, 1, 16 };
inline const FatLiteVolumeCase fat32Volume{ "fat32", 81920, 1, 512 };
inline const FatLiteVolumeCase fat16WithLargeClusters{ "fat16_with_large_clusters", 16384, 8, 512 };
inline const FatLiteVolumeCase fat16WithOneFat{ "fat16_with_one_fat", 16384, 1, 512, 1 };

template<std::size_t Size>
std::array<uint8_t, Size> Pattern(uint8_t seed = 0)
{
    std::array<uint8_t, Size> pattern;

    for (std::size_t index = 0; index != Size; ++index)
        pattern[index] = static_cast<uint8_t>(index * 31 + index / 509 + seed);

    return pattern;
}

class FatLiteFixture
    : public infra::ClockFixture
{
public:
    explicit FatLiteFixture(const FatLiteVolumeCase& volumeCase = fat16Volume, std::size_t transferAlignment = 1)
        : device(volumeCase.blocks)
        , volume(device, transferAlignment)
    {
        fs::FatFormatConfig config;
        config.sectorsPerCluster = volumeCase.sectorsPerCluster;
        config.rootEntries = volumeCase.rootEntries;
        config.fatCopies = volumeCase.fatCopies;
        std::optional<fs::Result> formatted;
        formatter.Format(config, [&formatted](fs::Result result)
            {
                formatted = result;
            });
        ExecuteAllActions();
        EXPECT_EQ(fs::Result::success, formatted);
        builder.emplace(device);
    }

    infra::Function<void(fs::Result)> Done()
    {
        return [this](fs::Result result)
        {
            done.callback(result);
        };
    }

    void ExpectDone(fs::Result result, int times = 1)
    {
        EXPECT_CALL(done, callback(result)).Times(times);
        ExecuteAllActions();
        testing::Mock::VerifyAndClearExpectations(&done);
    }

    void ExpectMount(fs::Result result = fs::Result::success)
    {
        volume.Mount(Done());
        ExpectDone(result);
    }

    void ExpectUnmount(fs::Result result = fs::Result::success)
    {
        volume.Unmount(Done());
        ExpectDone(result);
    }

    void ExpectStat(infra::BoundedConstString path, fs::Result result, const fs::EntryInfo& info = {})
    {
        ExpectStat(volume, path, result, info);
    }

    void ExpectStat(fs::FileSystem& fileSystem, infra::BoundedConstString path, fs::Result result, const fs::EntryInfo& info = {})
    {
        fileSystem.Stat(path, [this](fs::Result result, const fs::EntryInfo& info)
            {
                statDone.callback(result, info);
            });
        EXPECT_CALL(statDone, callback(result, info));
        ExecuteAllActions();
        testing::Mock::VerifyAndClearExpectations(&statDone);
    }

    infra::SharedPtr<fs::File> ExpectOpen(infra::BoundedConstString path, fs::OpenMode mode, fs::Result result = fs::Result::success)
    {
        return ExpectOpen(volume, path, mode, result);
    }

    infra::SharedPtr<fs::File> ExpectOpen(fs::FileSystem& fileSystem, infra::BoundedConstString path, fs::OpenMode mode, fs::Result result = fs::Result::success)
    {
        infra::SharedPtr<fs::File> opened;
        fileSystem.Open(path, mode, [this, &opened](fs::Result result, infra::SharedPtr<fs::File> file)
            {
                opened = file;
                done.callback(result);
            });
        ExpectDone(result);
        EXPECT_EQ(result == fs::Result::success, opened != nullptr);
        return opened;
    }

    void ExpectWrite(fs::File& file, infra::ConstByteRange data, fs::Result result = fs::Result::success)
    {
        file.Write(data, Done());
        ExpectDone(result);
    }

    void ExpectRead(fs::File& file, infra::ByteRange buffer, fs::Result result, std::size_t bytesRead)
    {
        file.Read(buffer, [this](fs::Result result, std::size_t bytesRead)
            {
                readDone.callback(result, bytesRead);
            });
        EXPECT_CALL(readDone, callback(result, bytesRead));
        ExecuteAllActions();
        testing::Mock::VerifyAndClearExpectations(&readDone);
    }

    void ExpectSync(fs::File& file, fs::Result result = fs::Result::success)
    {
        file.Sync(Done());
        ExpectDone(result);
    }

    void ExpectClose(fs::File& file, fs::Result result = fs::Result::success)
    {
        file.Close(Done());
        ExpectDone(result);
    }

    void ExpectTruncate(fs::File& file, fs::Result result = fs::Result::success)
    {
        file.Truncate(Done());
        ExpectDone(result);
    }

    void WriteFile(infra::BoundedConstString path, infra::ConstByteRange data)
    {
        auto file = ExpectOpen(path, fs::OpenMode::write);
        ExpectWrite(*file, data);
        ExpectClose(*file);
    }

    template<std::size_t Size>
    void ExpectContents(infra::BoundedConstString path, const std::array<uint8_t, Size>& expected)
    {
        auto file = ExpectOpen(path, fs::OpenMode::read);
        ASSERT_EQ(Size, file->Size());
        std::array<uint8_t, Size + 1> readBack{};
        ExpectRead(*file, infra::MakeRange(readBack), fs::Result::success, Size);
        EXPECT_TRUE(std::equal(expected.begin(), expected.end(), readBack.begin()));
    }

    template<std::size_t Size>
    void ExpectContentsOnFullVolume(infra::BoundedConstString path, const std::array<uint8_t, Size>& expected)
    {
        fs::FatFileSystem::WithStorage<1, 4> full{ device };
        full.Mount(Done());
        ExpectDone(fs::Result::success);

        infra::SharedPtr<fs::File> file;
        full.Open(path, fs::OpenMode::read, [this, &file](fs::Result result, infra::SharedPtr<fs::File> opened)
            {
                file = opened;
                done.callback(result);
            });
        ExpectDone(fs::Result::success);
        ASSERT_NE(nullptr, file);
        EXPECT_EQ(Size, file->Size());

        std::array<uint8_t, Size + 1> readBack{};
        file->Read(infra::MakeRange(readBack), [this](fs::Result result, std::size_t bytesRead)
            {
                readDone.callback(result, bytesRead);
            });
        EXPECT_CALL(readDone, callback(fs::Result::success, Size));
        ExecuteAllActions();
        testing::Mock::VerifyAndClearExpectations(&readDone);
        EXPECT_TRUE(std::equal(expected.begin(), expected.end(), readBack.begin()));

        file = nullptr;
        full.Unmount(Done());
        ExpectDone(fs::Result::success);
    }

    void UseEveryClusterFrom(uint32_t first)
    {
        for (uint32_t cluster = first; cluster != builder->Geometry().clusterCount + 2; ++cluster)
            builder->SetFatEntry(cluster, fs::EndOfChain(builder->Geometry().type));
    }

    uint32_t FreeClusters()
    {
        fs::FatImageBuilder image{ device };
        uint32_t free = 0;

        for (uint32_t cluster = 2; cluster != image.Geometry().clusterCount + 2; ++cluster)
            if (image.FatEntry(cluster) == 0)
                ++free;

        return free;
    }

    fs::SparseBlockDevice::WithStorage<192> device;
    fs::FatFormatter::WithStorage<8> formatter{ device };
    std::optional<fs::FatImageBuilder> builder;
    fs::FatLite::WithStorage<2, 1> volume;
    testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    testing::StrictMock<infra::MockCallback<void(fs::Result, const fs::EntryInfo&)>> statDone;
    testing::StrictMock<infra::MockCallback<void(fs::Result, std::size_t)>> readDone;
};

class FatLiteTest
    : public testing::Test
    , public FatLiteFixture
{};

class FatLiteEveryTypeTest
    : public testing::TestWithParam<FatLiteVolumeCase>
    , public FatLiteFixture
{
public:
    FatLiteEveryTypeTest()
        : FatLiteFixture(GetParam())
    {}
};

#endif
