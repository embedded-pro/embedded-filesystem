#ifndef FS_NOR_LITE_TEST_NOR_LITE_FIXTURE_HPP
#define FS_NOR_LITE_TEST_NOR_LITE_FIXTURE_HPP

#include "fs/interfaces/FileSystem.hpp"
#include "fs/nor/NorFileSystem.hpp"
#include "fs/nor/lite/NorLite.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"
#include "gmock/gmock.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace fs
{
    constexpr NorConfig liteConfig{ .maxBlocks = 32, .cachedNodes = 15 };

    class ExposedNorLite
        : public NorLite::WithStorage<liteConfig, 2>
    {
    public:
        using NorLite::WithStorage<liteConfig, 2>::WithStorage;
        using NorLite::Volume;
    };

    class NorLiteFixture
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        using Contents = infra::BoundedVector<uint8_t>::WithMaxSize<8192>;

        NorLiteFixture()
        {
            full.Format([](Result) {});
            ExecuteAllActions();
        }

        Result Mount(FileSystem& fileSystem)
        {
            return Complete([&fileSystem](const infra::Function<void(Result)>& onDone)
                {
                    fileSystem.Mount(onDone);
                });
        }

        Result Unmount(FileSystem& fileSystem)
        {
            return Complete([&fileSystem](const infra::Function<void(Result)>& onDone)
                {
                    fileSystem.Unmount(onDone);
                });
        }

        Result MakeDirectory(FileSystem& fileSystem, infra::BoundedConstString path)
        {
            return Complete([&fileSystem, path](const infra::Function<void(Result)>& onDone)
                {
                    fileSystem.MakeDirectory(path, onDone);
                });
        }

        std::pair<Result, infra::SharedPtr<File>> Open(FileSystem& fileSystem, infra::BoundedConstString path, OpenMode mode)
        {
            std::optional<Result> result;
            infra::SharedPtr<File> opened;
            fileSystem.Open(path, mode, [&result, &opened](Result openResult, infra::SharedPtr<File> file)
                {
                    result = openResult;
                    opened = file;
                });
            ExecuteAllActions();
            return { result.value_or(Result::busy), opened };
        }

        std::pair<Result, EntryInfo> Stat(FileSystem& fileSystem, infra::BoundedConstString path)
        {
            std::optional<Result> result;
            EntryInfo found;
            fileSystem.Stat(path, [&result, &found](Result statResult, const EntryInfo& info)
                {
                    result = statResult;
                    found = info;
                });
            ExecuteAllActions();
            return { result.value_or(Result::busy), found };
        }

        Result Write(File& file, infra::ConstByteRange data)
        {
            return Complete([&file, data](const infra::Function<void(Result)>& onDone)
                {
                    file.Write(data, onDone);
                });
        }

        Result Close(File& file)
        {
            return Complete([&file](const infra::Function<void(Result)>& onDone)
                {
                    file.Close(onDone);
                });
        }

        Contents ReadAll(File& file)
        {
            Contents contents(file.Size(), 0);
            std::optional<Result> result;
            file.Read(infra::MakeRange(contents), [&result](Result readResult, std::size_t)
                {
                    result = readResult;
                });
            ExecuteAllActions();
            EXPECT_EQ(Result::success, result);
            return contents;
        }

        Result WriteFile(FileSystem& fileSystem, infra::BoundedConstString path, OpenMode mode, infra::ConstByteRange data)
        {
            auto [opened, file] = Open(fileSystem, path, mode);
            if (opened != Result::success)
                return opened;

            auto written = Write(*file, data);
            auto closed = Close(*file);
            return written != Result::success ? written : closed;
        }

        Contents ReadFile(FileSystem& fileSystem, infra::BoundedConstString path)
        {
            auto [opened, file] = Open(fileSystem, path, OpenMode::read);
            EXPECT_EQ(Result::success, opened);
            if (file == nullptr)
                return {};

            auto contents = ReadAll(*file);
            EXPECT_EQ(Result::success, Close(*file));
            return contents;
        }

        template<class Start>
        Result Complete(const Start& start)
        {
            std::optional<Result> result;
            start([&result](Result completed)
                {
                    result = completed;
                });
            ExecuteAllActions();
            return result.value_or(Result::busy);
        }

        hal::FlashStub flash{ 32, 4096 };
        NorFileSystem::WithStorage<liteConfig, 2> full{ flash };
        ExposedNorLite lite{ flash };
    };
}

#endif
