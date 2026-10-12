#ifndef FS_FAT_TEST_FAT_FILE_SYSTEM_FIXTURE_HPP
#define FS_FAT_TEST_FAT_FILE_SYSTEM_FIXTURE_HPP

#include "fs/fat/FatFileSystem.hpp"
#include "fs/fat/FatFormatter.hpp"
#include "fs/fat/test_doubles/FatImageBuilder.hpp"
#include "fs/fat/test_doubles/SparseBlockDevice.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <optional>
#include <ostream>
#include <string_view>

namespace fs
{
    inline void PrintTo(const EntryInfo& info, std::ostream* stream)
    {
        *stream << "{ \"" << std::string_view(info.name.data(), info.name.size()) << "\", " << (info.type == EntryType::directory ? "directory" : "file") << ", " << info.size << " }";
    }
}

class FatFileSystemFixture
    : public testing::Test
    , public infra::ClockFixture
{
public:
    explicit FatFileSystemFixture(uint32_t blocks = 16384, const fs::FatFormatConfig& config = ClusterSize(1))
        : device(blocks)
    {
        std::optional<fs::Result> formatted;
        formatter.Format(config, [&formatted](fs::Result result)
            {
                formatted = result;
            });
        ExecuteAllActions();
        EXPECT_EQ(fs::Result::success, formatted);
        builder.emplace(device);
    }

    static fs::FatFormatConfig ClusterSize(uint8_t sectors)
    {
        fs::FatFormatConfig config;
        config.sectorsPerCluster = sectors;
        return config;
    }

    void ExpectMount(fs::Result result = fs::Result::success)
    {
        volume.Mount([this](fs::Result result)
            {
                lifecycleDone.callback(result);
            });
        EXPECT_CALL(lifecycleDone, callback(result));
        ExecuteAllActions();
    }

    void ExpectUnmount(fs::Result result = fs::Result::success)
    {
        volume.Unmount([this](fs::Result result)
            {
                lifecycleDone.callback(result);
            });
        EXPECT_CALL(lifecycleDone, callback(result));
        ExecuteAllActions();
    }

    void StartStat(infra::BoundedConstString path)
    {
        volume.Stat(path, [this](fs::Result result, const fs::EntryInfo& info)
            {
                statDone.callback(result, info);
            });
    }

    void ExpectStat(infra::BoundedConstString path, fs::Result result, const fs::EntryInfo& info = {})
    {
        StartStat(path);
        EXPECT_CALL(statDone, callback(result, info));
        ExecuteAllActions();
    }

    infra::SharedPtr<fs::Directory> ExpectOpenDirectory(infra::BoundedConstString path, fs::Result result = fs::Result::success)
    {
        infra::SharedPtr<fs::Directory> opened;
        volume.OpenDirectory(path, [this, &opened](fs::Result result, infra::SharedPtr<fs::Directory> directory)
            {
                opened = directory;
                openDone.callback(result);
            });
        EXPECT_CALL(openDone, callback(result));
        ExecuteAllActions();
        EXPECT_EQ(result == fs::Result::success, opened != nullptr);
        return opened;
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

    fs::SparseBlockDevice::WithStorage<160> device;
    fs::FatFormatter::WithStorage<8> formatter{ device };
    std::optional<fs::FatImageBuilder> builder;
    fs::FatFileSystem::WithStorage<1, 4> volume{ device };
    testing::StrictMock<infra::MockCallback<void(fs::Result)>> lifecycleDone;
    testing::StrictMock<infra::MockCallback<void(fs::Result, const fs::EntryInfo&)>> statDone;
    testing::StrictMock<infra::MockCallback<void(fs::Result)>> openDone;
    testing::StrictMock<infra::MockCallback<void(fs::Result, std::optional<fs::EntryInfo>)>> nextDone;
};

#endif
