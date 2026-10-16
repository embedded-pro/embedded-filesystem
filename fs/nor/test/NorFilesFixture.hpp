#ifndef FS_NOR_TEST_NOR_FILES_FIXTURE_HPP
#define FS_NOR_TEST_NOR_FILES_FIXTURE_HPP

#include "fs/nor/NorFile.hpp"
#include "fs/nor/test/NorRecordsFixture.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace fs
{
    inline infra::ConstByteRange Bytes(const char* text)
    {
        return infra::StringAsByteRange(infra::BoundedConstString(text));
    }

    template<std::size_t Size>
    std::array<uint8_t, Size> Pattern(uint8_t seed = 0)
    {
        std::array<uint8_t, Size> pattern{};
        for (std::size_t index = 0; index != Size; ++index)
            pattern[index] = static_cast<uint8_t>(index * 13 + index / 251 + seed);

        return pattern;
    }

    template<NorConfig Config, uint32_t Sectors, uint32_t SectorSize>
    class NorFilesFixture
        : public NorRecordsFixture<Config, Sectors, SectorSize, 2>
    {
    public:
        infra::SharedPtr<File> Open(infra::BoundedConstString path, OpenMode mode, Result expected = Result::success)
        {
            std::optional<Result> result;
            infra::SharedPtr<File> opened;
            this->fileSystem.Open(path, mode, [&result, &opened](Result openResult, infra::SharedPtr<File> file)
                {
                    result = openResult;
                    opened = file;
                });
            this->ExecuteAllActions();
            EXPECT_EQ(expected, result);
            EXPECT_EQ(expected == Result::success, opened != nullptr);
            return opened;
        }

        Result Write(File& file, infra::ConstByteRange data)
        {
            return Complete([&file, data](const infra::Function<void(Result)>& onDone)
                {
                    file.Write(data, onDone);
                });
        }

        Result Sync(File& file)
        {
            return Complete([&file](const infra::Function<void(Result)>& onDone)
                {
                    file.Sync(onDone);
                });
        }

        Result Truncate(File& file)
        {
            return Complete([&file](const infra::Function<void(Result)>& onDone)
                {
                    file.Truncate(onDone);
                });
        }

        Result Close(File& file)
        {
            return Complete([&file](const infra::Function<void(Result)>& onDone)
                {
                    file.Close(onDone);
                });
        }

        std::pair<Result, std::size_t> Read(File& file, infra::ByteRange buffer)
        {
            std::optional<std::pair<Result, std::size_t>> read;
            file.Read(buffer, [&read](Result result, std::size_t bytesRead)
                {
                    read.emplace(result, bytesRead);
                });
            this->ExecuteAllActions();
            return read.value_or(std::make_pair(Result::busy, std::size_t{ 0 }));
        }

        template<std::size_t Size>
        void ExpectContents(infra::BoundedConstString path, const std::array<uint8_t, Size>& contents)
        {
            auto file = Open(path, OpenMode::read);
            ASSERT_NE(nullptr, file);
            EXPECT_EQ(Size, file->Size());

            static std::array<uint8_t, Size + 1> read;
            read.fill(0);
            EXPECT_EQ(std::make_pair(Result::success, Size), Read(*file, infra::MakeRange(read)));
            EXPECT_TRUE(infra::ContentsEqual(infra::MakeRange(contents), infra::Head(infra::MakeRange(read), Size)));
            Close(*file);
        }

        uint32_t InodeOf(infra::BoundedConstString path)
        {
            auto file = Open(path, OpenMode::read);
            auto inode = static_cast<NorFile&>(*file).State().inode;
            Close(*file);
            return inode;
        }

        uint32_t BlockOf(infra::BoundedConstString path, uint32_t index)
        {
            return this->BlockInVolume(InodeOf(path), index);
        }

        void AddDirectoriesWithUnusedNodeSlots()
        {
            for (auto name : { "u", "v", "w", "x", "y" })
                this->Builder().AddDirectory(NorImageBuilder::rootInode, name);
            this->Builder().AddDirectory(this->Builder().AddDirectory(NorImageBuilder::rootInode, "z"), "x");
        }

        void LeaveFreeBlocks(uint32_t free)
        {
            for (uint32_t block = 2; block != this->State().blocks.Blocks(); ++block)
                if (this->State().blocks.State(block) != NorBlockState::freeErased && this->State().blocks.State(block) != NorBlockState::freeNotErased)
                    continue;
                else if (free == 0)
                    this->State().blocks.Set(block, NorBlockState::inUse);
                else
                    --free;
        }

        void UseEveryFreeBlock()
        {
            for (uint32_t block = 2; block != this->State().blocks.Blocks(); ++block)
                if (this->State().blocks.State(block) != NorBlockState::inUse)
                    this->State().blocks.Set(block, NorBlockState::inUse);
        }

        template<class Start>
        Result Complete(const Start& start)
        {
            std::optional<Result> result;
            start([&result](Result completed)
                {
                    result = completed;
                });
            EXPECT_FALSE(result.has_value());
            this->ExecuteAllActions();
            return result.value_or(Result::busy);
        }
    };
}

#endif
