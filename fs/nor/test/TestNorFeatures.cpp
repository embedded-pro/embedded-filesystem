#include "fs/nor/NorFileSystem.hpp"
#include "fs/nor/NorFormatter.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <optional>

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr fs::NorFeatures withoutChanges{ .makeDirectory = false, .remove = false, .rename = false, .truncate = false, .format = false };

    class NorFeaturesTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        NorFeaturesTest()
        {
            fs::NorFormatter::WithStorage<config> formatter{ flash };
            std::optional<fs::Result> formatted;
            formatter.Format([&formatted](fs::Result result)
                {
                    formatted = result;
                });
            ExecuteAllActions();
            EXPECT_EQ(fs::Result::success, formatted);
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
        }

        infra::SharedPtr<fs::File> Open(infra::BoundedConstString path, fs::OpenMode mode)
        {
            infra::SharedPtr<fs::File> opened;
            volume.Open(path, mode, [this, &opened](fs::Result result, infra::SharedPtr<fs::File> file)
                {
                    opened = file;
                    done.callback(result);
                });
            ExpectDone(fs::Result::success);
            return opened;
        }

        void StartDisabledCalls()
        {
            volume.MakeDirectory("/a", Done());
            volume.Remove("/a", Done());
            volume.Rename("/a", "/b", Done());
        }

        hal::FlashStub flash{ 32, 4096 };
        fs::NorFileSystem::WithStorage<config, 1, withoutChanges> volume{ flash };
        testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
        std::array<uint8_t, 100> data{};
    };
}

TEST_F(NorFeaturesTest, format_is_unsupported)
{
    volume.Format(Done());
    ExpectDone(fs::Result::unsupported);

    volume.Mount(Done());
    ExpectDone(fs::Result::success);
}

TEST_F(NorFeaturesTest, disabled_calls_complete_with_unsupported)
{
    volume.Mount(Done());
    ExpectDone(fs::Result::success);

    StartDisabledCalls();
    ExpectDone(fs::Result::unsupported, 3);
}

TEST_F(NorFeaturesTest, disabled_calls_are_not_mounted_before_mount)
{
    StartDisabledCalls();
    ExpectDone(fs::Result::notMounted, 3);
}

TEST_F(NorFeaturesTest, disabled_calls_are_unsupported_before_their_paths_are_validated)
{
    volume.Mount(Done());
    ExpectDone(fs::Result::success);

    volume.MakeDirectory("relative", Done());
    volume.Rename("/a/", "/b", Done());
    ExpectDone(fs::Result::unsupported, 2);
}

TEST_F(NorFeaturesTest, truncate_is_unsupported_on_a_writable_file_only)
{
    volume.Mount(Done());
    ExpectDone(fs::Result::success);
    auto file = Open("/log.txt", fs::OpenMode::write);

    file->Truncate(Done());
    ExpectDone(fs::Result::unsupported);
    file->Close(Done());
    ExpectDone(fs::Result::success);
    file = nullptr;

    auto reader = Open("/log.txt", fs::OpenMode::read);
    reader->Truncate(Done());
    ExpectDone(fs::Result::readOnly);
    reader = nullptr;

    file = Open("/log.txt", fs::OpenMode::append);
    volume.Unmount(Done());
    ExpectDone(fs::Result::success);
    file->Truncate(Done());
    ExpectDone(fs::Result::notMounted);
}

TEST_F(NorFeaturesTest, the_calls_that_stay_enabled_write_and_read_files)
{
    volume.Mount(Done());
    ExpectDone(fs::Result::success);
    data.fill(0x5a);

    auto file = Open("/log.txt", fs::OpenMode::write);
    file->Write(infra::MakeRange(data), Done());
    ExpectDone(fs::Result::success);
    file->Close(Done());
    ExpectDone(fs::Result::success);
    file = nullptr;

    auto reader = Open("/log.txt", fs::OpenMode::read);
    std::array<uint8_t, 100> readBack{};
    reader->Read(infra::MakeRange(readBack), [this](fs::Result result, std::size_t bytesRead)
        {
            EXPECT_EQ(100u, bytesRead);
            done.callback(result);
        });
    ExpectDone(fs::Result::success);
    EXPECT_EQ(data, readBack);
}

TEST_F(NorFeaturesTest, mounts_what_a_volume_with_every_feature_wrote)
{
    {
        fs::NorFileSystem::WithStorage<config, 1> full{ flash };
        full.Mount(Done());
        ExpectDone(fs::Result::success);
        full.MakeDirectory("/logs", Done());
        ExpectDone(fs::Result::success);
        infra::SharedPtr<fs::File> file;
        full.Open("/logs/old.txt", fs::OpenMode::write, [this, &file](fs::Result result, infra::SharedPtr<fs::File> opened)
            {
                file = opened;
                done.callback(result);
            });
        ExpectDone(fs::Result::success);
        data.fill(0x33);
        file->Write(infra::MakeRange(data), Done());
        ExpectDone(fs::Result::success);
        file->Close(Done());
        ExpectDone(fs::Result::success);
        file = nullptr;
        full.Rename("/logs/old.txt", "/logs/new.txt", Done());
        ExpectDone(fs::Result::success);
        full.Unmount(Done());
        ExpectDone(fs::Result::success);
    }

    volume.Mount(Done());
    ExpectDone(fs::Result::success);
    volume.Stat("/logs/new.txt", [this](fs::Result result, const fs::EntryInfo& info)
        {
            EXPECT_EQ(fs::EntryType::file, info.type);
            EXPECT_EQ(100u, info.size);
            done.callback(result);
        });
    ExpectDone(fs::Result::success);
}
