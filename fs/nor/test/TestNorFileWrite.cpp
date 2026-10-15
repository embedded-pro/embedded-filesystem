#include "fs/nor/NorBTree.hpp"
#include "fs/nor/NorDirectoryKey.hpp"
#include "fs/nor/NorFile.hpp"
#include "fs/nor/NorRadixTree.hpp"
#include "fs/nor/test/NorRecordsFixture.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr uint32_t blockSize = 4096;
    constexpr uint32_t headerSize = 36;
    constexpr uint32_t payload = blockSize - headerSize;
    constexpr uint32_t root = fs::NorImageBuilder::rootInode;

    infra::ConstByteRange Bytes(const char* text)
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

    template<fs::NorConfig Config, uint32_t Sectors, uint32_t SectorSize>
    class NorFileWriteFixture
        : public fs::NorRecordsFixture<Config, Sectors, SectorSize, 2>
    {
    public:
        infra::SharedPtr<fs::File> Open(infra::BoundedConstString path, fs::OpenMode mode, fs::Result expected = fs::Result::success)
        {
            std::optional<fs::Result> result;
            infra::SharedPtr<fs::File> opened;
            this->fileSystem.Open(path, mode, [&result, &opened](fs::Result openResult, infra::SharedPtr<fs::File> file)
                {
                    result = openResult;
                    opened = file;
                });
            this->ExecuteAllActions();
            EXPECT_EQ(expected, result);
            EXPECT_EQ(expected == fs::Result::success, opened != nullptr);
            return opened;
        }

        fs::Result Write(fs::File& file, infra::ConstByteRange data)
        {
            return Complete([&file, data](const infra::Function<void(fs::Result)>& onDone)
                {
                    file.Write(data, onDone);
                });
        }

        fs::Result Sync(fs::File& file)
        {
            return Complete([&file](const infra::Function<void(fs::Result)>& onDone)
                {
                    file.Sync(onDone);
                });
        }

        fs::Result Close(fs::File& file)
        {
            return Complete([&file](const infra::Function<void(fs::Result)>& onDone)
                {
                    file.Close(onDone);
                });
        }

        std::pair<fs::Result, std::size_t> Read(fs::File& file, infra::ByteRange buffer)
        {
            std::optional<std::pair<fs::Result, std::size_t>> read;
            file.Read(buffer, [&read](fs::Result result, std::size_t bytesRead)
                {
                    read.emplace(result, bytesRead);
                });
            this->ExecuteAllActions();
            return read.value_or(std::make_pair(fs::Result::busy, std::size_t{ 0 }));
        }

        template<std::size_t Size>
        void ExpectContents(infra::BoundedConstString path, const std::array<uint8_t, Size>& contents)
        {
            auto file = Open(path, fs::OpenMode::read);
            ASSERT_NE(nullptr, file);
            EXPECT_EQ(Size, file->Size());

            static std::array<uint8_t, Size + 1> read;
            read.fill(0);
            EXPECT_EQ(std::make_pair(fs::Result::success, Size), Read(*file, infra::MakeRange(read)));
            EXPECT_TRUE(infra::ContentsEqual(infra::MakeRange(contents), infra::Head(infra::MakeRange(read), Size)));
            Close(*file);
        }

        uint32_t InodeOf(infra::BoundedConstString path)
        {
            auto file = Open(path, fs::OpenMode::read);
            auto inode = static_cast<fs::NorFile&>(*file).State().inode;
            Close(*file);
            return inode;
        }

        uint32_t BlockOf(infra::BoundedConstString path, uint32_t index)
        {
            return this->BlockInVolume(InodeOf(path), index);
        }

        void UseEveryFreeBlock()
        {
            for (uint32_t block = 2; block != this->State().blocks.Blocks(); ++block)
                if (this->State().blocks.State(block) != fs::NorBlockState::inUse)
                    this->State().blocks.Set(block, fs::NorBlockState::inUse);
        }

        template<class Start>
        fs::Result Complete(const Start& start)
        {
            std::optional<fs::Result> result;
            start([&result](fs::Result completed)
                {
                    result = completed;
                });
            EXPECT_FALSE(result.has_value());
            this->ExecuteAllActions();
            return result.value_or(fs::Result::busy);
        }
    };

    class NorFileWriteTest
        : public NorFileWriteFixture<config, 32, blockSize>
    {
    public:
        NorFileWriteTest()
        {
            Mount();
        }
    };
}

TEST_F(NorFileWriteTest, open_in_write_mode_creates_an_empty_file_that_is_there_after_a_mount)
{
    auto file = Open("/a", fs::OpenMode::write);

    EXPECT_EQ(0, file->Size());
    EXPECT_EQ(0, file->Position());
    EXPECT_EQ(std::make_pair(fs::Result::success, fs::EntryInfo{ {}, fs::EntryType::file, 0 }), Stat("/a"));
    Close(*file);
    Unmount();
    Mount();
    EXPECT_EQ(fs::EntryType::file, Stat("/a").second.type);
}

TEST_F(NorFileWriteTest, open_in_append_or_read_write_mode_creates_a_missing_file)
{
    auto appended = Open("/a", fs::OpenMode::append);
    auto readWritten = Open("/b", fs::OpenMode::readWrite);

    EXPECT_THAT(List("/"), testing::UnorderedElementsAre("a", "b"));
}

TEST_F(NorFileWriteTest, open_for_writing_of_a_directory_is_a_directory)
{
    MakeDirectory("/docs");

    Open("/", fs::OpenMode::write, fs::Result::isADirectory);
    Open("/docs", fs::OpenMode::append, fs::Result::isADirectory);
}

TEST_F(NorFileWriteTest, open_for_writing_checks_the_parent)
{
    Open("/f", fs::OpenMode::write);

    Open("/x/a", fs::OpenMode::write, fs::Result::notFound);
    Open("/f/a", fs::OpenMode::write, fs::Result::notADirectory);
}

TEST_F(NorFileWriteTest, open_for_writing_beyond_the_pool_completes_with_too_many_open_files_and_creates_nothing)
{
    auto first = Open("/a", fs::OpenMode::write);
    auto second = Open("/b", fs::OpenMode::write);

    Open("/c", fs::OpenMode::write, fs::Result::tooManyOpenFiles);

    EXPECT_EQ(fs::Result::notFound, Stat("/c").first);
}

TEST_F(NorFileWriteTest, a_file_open_for_writing_cannot_be_opened_again)
{
    auto file = Open("/a", fs::OpenMode::write);

    Open("/a", fs::OpenMode::read, fs::Result::busy);
    Open("/a", fs::OpenMode::append, fs::Result::busy);
    Close(*file);
    Open("/a", fs::OpenMode::read);
}

TEST_F(NorFileWriteTest, a_file_open_for_reading_cannot_be_opened_for_writing)
{
    auto created = Open("/a", fs::OpenMode::write);
    Close(*created);
    auto reading = Open("/a", fs::OpenMode::read);

    Open("/a", fs::OpenMode::append, fs::Result::busy);
}

TEST_F(NorFileWriteTest, small_writes_stay_in_the_buffer_and_sync_commits_them_in_the_inline_area)
{
    auto file = Open("/a", fs::OpenMode::write);
    device.recording = true;

    EXPECT_EQ(fs::Result::success, Write(*file, Bytes("hello")));
    EXPECT_EQ(fs::Result::success, Write(*file, Bytes(" world")));
    EXPECT_TRUE(device.requests.empty());
    EXPECT_EQ(11, file->Size());
    EXPECT_EQ(11, file->Position());

    EXPECT_EQ(fs::Result::success, Sync(*file));

    ASSERT_EQ(1, device.requests.size());
    EXPECT_EQ(4 * blockSize + State().journal.end - device.requests[0].size, device.requests[0].address);
    EXPECT_EQ(11, Stat("/a").second.size);
    Close(*file);
    ExpectContents("/a", std::array<uint8_t, 11>{ 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd' });
}

TEST_F(NorFileWriteTest, a_read_through_the_handle_sees_what_is_not_synced)
{
    auto contents = Pattern<5000>();
    auto file = Open("/a", fs::OpenMode::readWrite);
    Write(*file, infra::MakeRange(contents));

    file->Seek(0);
    std::array<uint8_t, 5000> read{};

    EXPECT_EQ(std::make_pair(fs::Result::success, std::size_t{ 5000 }), Read(*file, infra::MakeRange(read)));
    EXPECT_EQ(contents, read);
}

TEST_F(NorFileWriteTest, beyond_the_inline_area_a_data_block_is_programmed_page_by_page)
{
    auto contents = Pattern<300>();
    auto file = Open("/a", fs::OpenMode::write);
    device.recording = true;

    for (std::size_t offset = 0; offset != contents.size(); offset += 30)
        ASSERT_EQ(fs::Result::success, Write(*file, infra::Head(infra::DiscardHead(infra::MakeRange(contents), offset), 30)));

    ASSERT_EQ(2, device.requests.size());
    EXPECT_EQ((fs::RecordingFlash::Request{ 5 * blockSize + 12, 24, false }), device.requests[0]);
    EXPECT_EQ((fs::RecordingFlash::Request{ 5 * blockSize + headerSize, 256 - headerSize, false }), device.requests[1]);

    EXPECT_EQ(fs::Result::success, Sync(*file));

    EXPECT_EQ((fs::RecordingFlash::Request{ 5 * blockSize + 256, 300 - (256 - headerSize), false }), device.requests[2]);
    Close(*file);
    ExpectContents("/a", contents);
}

TEST_F(NorFileWriteTest, a_full_block_continues_in_a_new_data_block)
{
    auto contents = Pattern<9000>();
    auto file = Open("/a", fs::OpenMode::write);

    EXPECT_EQ(fs::Result::success, Write(*file, infra::MakeRange(contents)));
    Close(*file);

    ExpectContents("/a", contents);
    EXPECT_NE(BlockOf("/a", 0), BlockOf("/a", 1));
}

TEST_F(NorFileWriteTest, a_write_that_needs_a_ninth_block_commits_the_first_eight)
{
    static auto contents = Pattern<8 * payload + 100>();
    auto file = Open("/a", fs::OpenMode::write);

    EXPECT_EQ(fs::Result::success, Write(*file, infra::MakeRange(contents)));

    EXPECT_EQ(8 * payload + config.inlineBytes, Stat("/a").second.size);
    Close(*file);
    ExpectContents("/a", contents);
}

TEST_F(NorFileWriteTest, sync_without_changes_writes_nothing)
{
    auto file = Open("/a", fs::OpenMode::write);
    device.recording = true;

    EXPECT_EQ(fs::Result::success, Sync(*file));
    EXPECT_TRUE(device.requests.empty());
}

TEST_F(NorFileWriteTest, releasing_the_last_reference_commits_in_the_background)
{
    auto file = Open("/a", fs::OpenMode::write);
    Write(*file, Bytes("abc"));

    file = nullptr;
    ExecuteAllActions();

    ExpectContents("/a", std::array<uint8_t, 3>{ 'a', 'b', 'c' });
}

TEST_F(NorFileWriteTest, unmount_commits_the_open_files_and_their_handles_complete_with_not_mounted)
{
    auto file = Open("/a", fs::OpenMode::write);
    Write(*file, Bytes("abc"));

    Unmount();

    EXPECT_EQ(fs::Result::notMounted, Write(*file, Bytes("d")));
    EXPECT_EQ(fs::Result::notMounted, Sync(*file));
    Mount();
    EXPECT_EQ(fs::Result::notMounted, Close(*file));
    ExpectContents("/a", std::array<uint8_t, 3>{ 'a', 'b', 'c' });
}

TEST_F(NorFileWriteTest, append_mode_writes_at_the_end_wherever_the_position_is)
{
    Unmount();
    Builder().AddFile(root, "a", Bytes("abc"));
    Builder().Commit();
    Mount();
    auto file = Open("/a", fs::OpenMode::append);
    EXPECT_EQ(3, file->Position());

    file->Seek(0);
    EXPECT_EQ(fs::Result::success, Write(*file, Bytes("de")));

    EXPECT_EQ(5, file->Position());
    Close(*file);
    ExpectContents("/a", std::array<uint8_t, 5>{ 'a', 'b', 'c', 'd', 'e' });
}

TEST_F(NorFileWriteTest, a_write_below_the_end_is_unsupported_until_overwriting)
{
    Unmount();
    Builder().AddFile(root, "a", Bytes("abc"));
    Builder().Commit();
    Mount();
    auto file = Open("/a", fs::OpenMode::readWrite);

    EXPECT_EQ(fs::Result::unsupported, Write(*file, Bytes("x")));
    file->Seek(3);
    EXPECT_EQ(fs::Result::success, Write(*file, Bytes("d")));
}

TEST_F(NorFileWriteTest, an_append_to_a_block_written_before_the_mount_copies_the_block_first)
{
    Unmount();
    auto contents = Pattern<5000>();
    Builder().AddFile(root, "a", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto previous = BlockOf("/a", 1);
    Builder().Program(previous * blockSize + headerSize + 940, infra::MakeRange(std::array<uint8_t, 4>{ 0, 1, 2, 3 }));
    auto file = Open("/a", fs::OpenMode::append);

    EXPECT_EQ(fs::Result::success, Write(*file, Bytes("0123456789")));
    EXPECT_EQ(fs::Result::success, Close(*file));

    auto copied = BlockOf("/a", 1);
    EXPECT_NE(previous, copied);
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(previous));
    EXPECT_EQ(fs::NorBlockState::inUse, State().blocks.State(copied));
    std::array<uint8_t, 5010> expected{};
    std::ranges::copy(contents, expected.begin());
    std::ranges::copy(Bytes("0123456789"), expected.begin() + 5000);
    ExpectContents("/a", expected);
}

TEST_F(NorFileWriteTest, a_block_that_a_commit_released_and_that_was_erased_since_is_free_after_a_power_loss)
{
    Unmount();
    auto contents = Pattern<5000>();
    Builder().AddFile(root, "a", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto previous = BlockOf("/a", 1);
    auto file = Open("/a", fs::OpenMode::append);
    ASSERT_EQ(fs::Result::success, Write(*file, Bytes("0123456789")));
    ASSERT_EQ(fs::Result::success, Sync(*file));
    Builder().EraseBlock(previous);

    fs::ExposedNorFileSystem<config, 2> afterPowerLoss{ flash };
    std::optional<fs::Result> mounted;
    afterPowerLoss.Mount([&mounted](fs::Result result)
        {
            mounted = result;
        });
    ExecuteAllActions();

    EXPECT_EQ(fs::Result::success, mounted);
    EXPECT_EQ(fs::NorBlockState::freeNotErased, afterPowerLoss.Volume().State().blocks.State(previous));
}

TEST_F(NorFileWriteTest, an_append_to_an_inline_tail_continues_it)
{
    Unmount();
    auto contents = Pattern<payload + 30>();
    Builder().AddFile(root, "a", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto file = Open("/a", fs::OpenMode::append);
    auto more = Pattern<50>(7);

    EXPECT_EQ(fs::Result::success, Write(*file, infra::MakeRange(more)));
    Close(*file);

    std::array<uint8_t, payload + 80> expected{};
    std::ranges::copy(contents, expected.begin());
    std::ranges::copy(more, expected.begin() + payload + 30);
    ExpectContents("/a", expected);
}

TEST_F(NorFileWriteTest, an_append_to_a_file_that_ends_at_a_block_boundary_starts_a_new_block)
{
    Unmount();
    auto contents = Pattern<payload>();
    Builder().AddFile(root, "a", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto first = BlockOf("/a", 0);
    auto file = Open("/a", fs::OpenMode::append);
    auto more = Pattern<100>(3);

    EXPECT_EQ(fs::Result::success, Write(*file, infra::MakeRange(more)));
    Close(*file);

    EXPECT_EQ(first, BlockOf("/a", 0));
    std::array<uint8_t, payload + 100> expected{};
    std::ranges::copy(contents, expected.begin());
    std::ranges::copy(more, expected.begin() + payload);
    ExpectContents("/a", expected);
}

TEST_F(NorFileWriteTest, a_write_without_a_free_block_has_no_space_and_keeps_what_it_wrote)
{
    auto file = Open("/a", fs::OpenMode::write);
    for (uint32_t block = 2; block != 32; ++block)
        if (State().blocks.State(block) != fs::NorBlockState::inUse)
            State().blocks.Set(block, fs::NorBlockState::inUse);
    auto contents = Pattern<100>();

    EXPECT_EQ(fs::Result::noSpace, Write(*file, infra::MakeRange(contents)));

    EXPECT_EQ(config.inlineBytes, file->Size());
    EXPECT_EQ(config.inlineBytes, file->Position());
}

TEST_F(NorFileWriteTest, a_copy_without_a_free_block_has_no_space)
{
    Unmount();
    auto contents = Pattern<5000>();
    Builder().AddFile(root, "a", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto file = Open("/a", fs::OpenMode::append);
    Write(*file, Bytes("x"));
    for (uint32_t block = 2; block != 32; ++block)
        if (State().blocks.State(block) != fs::NorBlockState::inUse)
            State().blocks.Set(block, fs::NorBlockState::inUse);

    EXPECT_EQ(fs::Result::noSpace, Sync(*file));
}

TEST_F(NorFileWriteTest, a_write_whose_copy_has_no_space_takes_back_the_bytes_it_could_not_program_and_a_later_write_continues)
{
    Unmount();
    auto contents = Pattern<5000>();
    Builder().AddFile(root, "a", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto file = Open("/a", fs::OpenMode::append);
    std::array<bool, 32> taken{};
    for (uint32_t block = 2; block != 32; ++block)
        if (State().blocks.State(block) != fs::NorBlockState::inUse)
        {
            taken[block] = true;
            State().blocks.Set(block, fs::NorBlockState::inUse);
        }
    auto appended = Pattern<300>(7);

    EXPECT_EQ(fs::Result::noSpace, Write(*file, infra::MakeRange(appended)));
    EXPECT_EQ(5000, file->Size());

    for (uint32_t block = 2; block != 32; ++block)
        if (taken[block])
            State().blocks.Set(block, fs::NorBlockState::freeNotErased);

    EXPECT_EQ(fs::Result::success, Write(*file, infra::MakeRange(appended)));
    EXPECT_EQ(fs::Result::success, Close(*file));
    std::array<uint8_t, 5300> expected{};
    std::ranges::copy(contents, expected.begin());
    std::ranges::copy(appended, expected.begin() + 5000);
    ExpectContents("/a", expected);
}

TEST_F(NorFileWriteTest, a_write_up_to_the_end_of_a_block_whose_copy_has_no_space_keeps_the_block)
{
    Unmount();
    auto contents = Pattern<2 * payload - 10>();
    Builder().AddFile(root, "a", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto file = Open("/a", fs::OpenMode::append);
    std::array<bool, 32> taken{};
    for (uint32_t block = 2; block != 32; ++block)
        if (State().blocks.State(block) != fs::NorBlockState::inUse)
        {
            taken[block] = true;
            State().blocks.Set(block, fs::NorBlockState::inUse);
        }
    auto appended = Pattern<50>(7);

    EXPECT_EQ(fs::Result::noSpace, Write(*file, infra::MakeRange(appended)));
    EXPECT_EQ(2 * payload - 10, file->Size());

    for (uint32_t block = 2; block != 32; ++block)
        if (taken[block])
            State().blocks.Set(block, fs::NorBlockState::freeNotErased);

    EXPECT_EQ(fs::Result::success, Write(*file, infra::MakeRange(appended)));
    EXPECT_EQ(fs::Result::success, Close(*file));
    std::array<uint8_t, 2 * payload + 40> expected{};
    std::ranges::copy(contents, expected.begin());
    std::ranges::copy(appended, expected.begin() + 2 * payload - 10);
    ExpectContents("/a", expected);
}

TEST_F(NorFileWriteTest, a_file_at_the_largest_size_takes_no_more_bytes)
{
    auto file = Open("/a", fs::OpenMode::write);
    auto& state = static_cast<fs::NorFile&>(*file).State();
    state.size = std::numeric_limits<uint32_t>::max();
    state.programmed = state.size;
    state.position = state.size;

    EXPECT_EQ(fs::Result::noSpace, Write(*file, Bytes("x")));
}

TEST_F(NorFileWriteTest, calls_while_a_call_is_pending_complete_with_busy)
{
    auto file = Open("/a", fs::OpenMode::write);
    std::optional<fs::Result> written;
    file->Write(Bytes("abc"), [&written](fs::Result result)
        {
            written = result;
        });

    testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    file->Write(Bytes("d"), [&done](fs::Result result)
        {
            done.callback(result);
        });
    file->Truncate([&done](fs::Result result)
        {
            done.callback(result);
        });
    file->ExpectSize(10, [&done](fs::Result result)
        {
            done.callback(result);
        });
    file->Sync([&done](fs::Result result)
        {
            done.callback(result);
        });
    file->Close([&done](fs::Result result)
        {
            done.callback(result);
        });
    std::array<uint8_t, 4> buffer{};
    file->Read(infra::MakeRange(buffer), [&done](fs::Result result, std::size_t)
        {
            done.callback(result);
        });

    EXPECT_CALL(done, callback(fs::Result::busy)).Times(6);
    ExecuteAllActions();
    EXPECT_EQ(fs::Result::success, written);
}

TEST_F(NorFileWriteTest, truncate_is_unsupported_until_its_step_and_expect_size_has_no_effect)
{
    auto file = Open("/a", fs::OpenMode::write);

    EXPECT_EQ(fs::Result::unsupported, Complete([&file](const infra::Function<void(fs::Result)>& onDone)
                                           {
                                               file->Truncate(onDone);
                                           }));
    EXPECT_EQ(fs::Result::success, Complete([&file](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           file->ExpectSize(1000, onDone);
                                       }));
    EXPECT_EQ(0, file->Size());
    Unmount();
    EXPECT_EQ(fs::Result::notMounted, Complete([&file](const infra::Function<void(fs::Result)>& onDone)
                                          {
                                              file->Truncate(onDone);
                                          }));
}

TEST_F(NorFileWriteTest, a_file_open_for_reading_takes_no_writes)
{
    auto created = Open("/a", fs::OpenMode::write);
    Close(*created);
    auto file = Open("/a", fs::OpenMode::read);

    EXPECT_EQ(fs::Result::readOnly, Write(*file, Bytes("x")));
    EXPECT_EQ(fs::Result::readOnly, Complete([&file](const infra::Function<void(fs::Result)>& onDone)
                                        {
                                            file->ExpectSize(1000, onDone);
                                        }));
    EXPECT_EQ(fs::Result::success, Sync(*file));
    EXPECT_EQ(fs::Result::success, Close(*file));
}

TEST_F(NorFileWriteTest, an_inline_area_longer_than_the_file_is_corrupted)
{
    Unmount();
    Builder().AddRecord(2, fs::NorInodeRecord{ root, fs::NorInodeType::file, 0, 2, 0, Bytes("abcd"), Bytes("a") });
    Builder().AddEntry(root, fs::NorDirectoryKey("a"), 2);
    Builder().Commit();
    Mount();

    Open("/a", fs::OpenMode::append, fs::Result::corrupted);
}

TEST_F(NorFileWriteTest, a_tail_that_does_not_match_the_inline_area_is_corrupted)
{
    Unmount();
    auto file = Builder().AddFile(root, "a", Bytes("abc"));
    Builder().MapBlock(file, 0, 9);
    Builder().AddFile(root, "b", infra::MakeRange(Pattern<5000>()));
    Builder().Commit();
    Builder().AddRecord(3, fs::NorInodeRecord{ root, fs::NorInodeType::file, Builder().MapOf(3).height, 5000, Builder().MapOf(3).address, Bytes("xyz"), Bytes("b") });
    Builder().Commit();
    Mount();

    Open("/a", fs::OpenMode::append, fs::Result::corrupted);
    Open("/b", fs::OpenMode::append, fs::Result::corrupted);
}

TEST_F(NorFileWriteTest, a_tail_block_that_is_not_a_data_block_in_use_is_corrupted_at_open)
{
    Unmount();
    auto contents = Pattern<5000>();
    Builder().MapBlock(Builder().AddFile(root, "a", infra::MakeRange(contents)), 1, 40);
    Builder().MapBlock(Builder().AddFile(root, "b", infra::MakeRange(contents)), 1, 1);
    Builder().MapBlock(Builder().AddFile(root, "c", infra::MakeRange(contents)), 1, 25);
    Builder().Commit();
    Mount();

    Open("/a", fs::OpenMode::append, fs::Result::corrupted);
    Open("/b", fs::OpenMode::append, fs::Result::corrupted);
    Open("/c", fs::OpenMode::append, fs::Result::corrupted);
}

TEST_F(NorFileWriteTest, a_data_node_with_a_bad_crc_is_corrupted_at_open)
{
    Unmount();
    auto file = Builder().AddFile(root, "a", infra::MakeRange(Pattern<5000>()));
    Builder().Commit();
    Builder().Corrupt(Builder().MapOf(file).address + 12);
    Mount();

    Open("/a", fs::OpenMode::append, fs::Result::corrupted);
}

TEST_F(NorFileWriteTest, a_file_whose_record_is_gone_does_not_commit)
{
    auto file = Open("/a", fs::OpenMode::write);
    Write(*file, Bytes("abc"));
    static_cast<fs::NorFile&>(*file).State().inode = 99;

    EXPECT_EQ(fs::Result::corrupted, Sync(*file));
}

TEST_F(NorFileWriteTest, an_inode_table_node_with_a_bad_crc_does_not_commit)
{
    auto file = Open("/a", fs::OpenMode::write);
    Write(*file, Bytes("abc"));
    ASSERT_EQ(fs::Result::success, Sync(*file));
    Write(*file, Bytes("d"));
    std::optional<fs::Result> written;
    Volume().Checkpoints().Write([&written](fs::Result result)
        {
            written = result;
        });
    ExecuteAllActions();
    ASSERT_EQ(fs::Result::success, written);
    Builder().Corrupt(State().inodeTable.address + 20);
    Volume().Cache().Clear();

    EXPECT_EQ(fs::Result::corrupted, Sync(*file));
}

namespace
{
    constexpr fs::NorConfig wideUnitConfig{ .maxBlocks = 48, .programUnit = 16, .pageSize = 16, .cachedNodes = 15 };

    class NorFileWriteWideUnitTest
        : public NorFileWriteFixture<wideUnitConfig, 48, 2048>
    {
    public:
        NorFileWriteWideUnitTest()
        {
            Mount();
        }
    };
}

TEST_F(NorFileWriteWideUnitTest, the_part_of_a_program_unit_after_the_last_whole_one_stays_in_the_inline_area)
{
    auto contents = Pattern<200>();
    auto file = Open("/a", fs::OpenMode::write);
    Write(*file, infra::Head(infra::MakeRange(contents), 100));
    Close(*file);
    file = Open("/a", fs::OpenMode::append);
    device.recording = true;

    Write(*file, infra::DiscardHead(infra::MakeRange(contents), 100));
    Close(*file);

    for (const auto& request : device.requests)
        if (!request.erase)
            EXPECT_EQ(0, request.size % 16);
    ExpectContents("/a", contents);
}

namespace
{
    constexpr fs::NorConfig smallNodes{ .maxBlocks = 64, .pageSize = 16, .nodeSize = 160, .cachedNodes = 15, .inlineBytes = 36, .maxNameBytes = 8 };

    class NorFileWriteSmallNodesTest
        : public NorFileWriteFixture<smallNodes, 64, blockSize>
    {
    public:
        NorFileWriteSmallNodesTest()
        {
            Mount();
        }
    };
}

TEST_F(NorFileWriteSmallNodesTest, a_block_beyond_the_reach_of_the_data_map_has_no_space)
{
    auto file = Open("/a", fs::OpenMode::write);
    auto& state = static_cast<fs::NorFile&>(*file).State();
    state.size = 36 * 36 * 36 * payload;
    state.programmed = state.size;
    state.position = state.size;
    auto contents = Pattern<100>();

    EXPECT_EQ(fs::Result::noSpace, Write(*file, infra::MakeRange(contents)));
    EXPECT_EQ(state.programmed + smallNodes.inlineBytes, file->Size());
}

TEST_F(NorFileWriteTest, an_existing_empty_file_opens_for_writing_at_size_0)
{
    auto created = Open("/a", fs::OpenMode::write);
    Close(*created);

    for (auto mode : { fs::OpenMode::write, fs::OpenMode::append, fs::OpenMode::readWrite })
    {
        auto file = Open("/a", mode);
        EXPECT_EQ(0, file->Size());
        Close(*file);
    }
}

TEST_F(NorFileWriteTest, open_for_writing_of_an_existing_file_beyond_the_pool_completes_with_too_many_open_files)
{
    auto created = Open("/a", fs::OpenMode::write);
    Close(*created);
    created = nullptr;
    auto first = Open("/b", fs::OpenMode::write);
    auto second = Open("/c", fs::OpenMode::write);

    Open("/a", fs::OpenMode::append, fs::Result::tooManyOpenFiles);
}

TEST_F(NorFileWriteTest, unmount_completes_with_the_result_of_a_commit_that_fails)
{
    Unmount();
    Builder().AddFile(root, "a", infra::MakeRange(Pattern<5000>()));
    Builder().Commit();
    Mount();
    auto file = Open("/a", fs::OpenMode::append);
    Write(*file, Bytes("x"));
    UseEveryFreeBlock();

    Unmount(fs::Result::noSpace);
}

TEST_F(NorFileWriteTest, a_commit_before_a_ninth_block_that_fails_stops_the_write)
{
    static auto contents = Pattern<8 * payload + 10>();
    auto file = Open("/a", fs::OpenMode::write);
    Write(*file, infra::MakeRange(contents));
    static_cast<fs::NorFile&>(*file).State().inode = 99;
    auto more = Pattern<100>();

    EXPECT_EQ(fs::Result::corrupted, Write(*file, infra::MakeRange(more)));
}

TEST_F(NorFileWriteTest, a_page_flush_that_fails_stops_the_write)
{
    Unmount();
    Builder().AddFile(root, "a", infra::MakeRange(Pattern<5000>()));
    Builder().Commit();
    Mount();
    auto file = Open("/a", fs::OpenMode::append);
    UseEveryFreeBlock();
    auto more = Pattern<100>();

    EXPECT_EQ(fs::Result::noSpace, Write(*file, infra::MakeRange(more)));
}

TEST_F(NorFileWriteTest, a_commit_whose_record_cannot_be_applied_completes_with_the_result)
{
    auto file = Open("/a", fs::OpenMode::write);
    auto contents = Pattern<100>();
    Write(*file, infra::MakeRange(contents));
    static_cast<fs::NorFile&>(*file).State().entries[0].previous = 7;

    EXPECT_EQ(fs::Result::corrupted, Sync(*file));
}

namespace
{
    constexpr fs::NorConfig sixtyFourBlocks{ .maxBlocks = 64, .cachedNodes = 15 };

    class NorFileWriteManyFilesTest
        : public NorFileWriteFixture<sixtyFourBlocks, 64, blockSize>
    {
    public:
        NorFileWriteManyFilesTest()
        {
            Mount();
        }
    };
}

TEST_F(NorFileWriteManyFilesTest, files_committed_one_after_the_other_make_checkpoints_and_stay)
{
    auto contents = Pattern<100>();
    for (uint32_t index = 0; index != 24; ++index)
    {
        auto file = Open(NameOf(index).insert(0, "/"), fs::OpenMode::write);
        ASSERT_EQ(fs::Result::success, Write(*file, infra::MakeRange(contents)));
        ASSERT_EQ(fs::Result::success, Close(*file));
    }
    Unmount();
    Mount();

    for (uint32_t index = 0; index < 24; index += 5)
        ExpectContents(NameOf(index).insert(0, "/"), contents);
}

namespace
{
    constexpr fs::NorConfig deepConfig{ .maxBlocks = 128, .pageSize = 16, .nodeSize = 160, .cachedNodes = 15, .inlineBytes = 36, .maxNameBytes = 8 };

    class NorFileWriteDeepTest
        : public NorFileWriteFixture<deepConfig, 128, blockSize>
    {
    public:
        template<std::size_t Size>
        void PrepareFileAndModifiedNodes(const std::array<uint8_t, Size>& contents)
        {
            AddDirectories(root, 60);
            Builder().AddFile(root, "f", infra::MakeRange(contents));
            Builder().Commit();
            Mount();
            ModifyLeavesOf({ 3, 5, 7, 9, 11, 13, 15, 17, 19, 21 });
            ASSERT_EQ(12, Volume().Cache().ModifiedNodes());
        }
    };
}

TEST_F(NorFileWriteDeepTest, a_commit_that_does_not_fit_next_to_the_modified_nodes_makes_a_checkpoint_first)
{
    static auto contents = Pattern<36 * payload>();
    PrepareFileAndModifiedNodes(contents);
    auto file = Open("/f", fs::OpenMode::append);
    auto more = Pattern<100>();
    Write(*file, infra::MakeRange(more));
    auto checkpointBlock = State().checkpointBlock;

    EXPECT_EQ(fs::Result::success, Sync(*file));

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
}

TEST_F(NorFileWriteDeepTest, a_commit_whose_paths_do_not_fit_in_the_cache_makes_a_checkpoint_first)
{
    static auto contents = Pattern<36 * payload + 1000>();
    PrepareFileAndModifiedNodes(contents);
    auto file = Open("/f", fs::OpenMode::append);
    Write(*file, Bytes("0123456789"));
    auto checkpointBlock = State().checkpointBlock;

    EXPECT_EQ(fs::Result::success, Sync(*file));

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
}

TEST_F(NorFileWriteDeepTest, a_checkpoint_before_a_commit_without_a_free_block_has_no_space)
{
    static auto contents = Pattern<36 * payload>();
    PrepareFileAndModifiedNodes(contents);
    auto file = Open("/f", fs::OpenMode::append);
    auto more = Pattern<100>();
    Write(*file, infra::MakeRange(more));
    UseEveryFreeBlock();

    EXPECT_EQ(fs::Result::noSpace, Sync(*file));
}

TEST_F(NorFileWriteDeepTest, a_close_that_fails_frees_the_blocks_that_its_handle_allocated)
{
    static auto contents = Pattern<36 * payload>();
    PrepareFileAndModifiedNodes(contents);
    auto file = Open("/f", fs::OpenMode::append);
    auto more = Pattern<100>();
    Write(*file, infra::MakeRange(more));
    auto entries = static_cast<fs::NorFile&>(*file).State().entries;
    ASSERT_EQ(1, entries.size());
    UseEveryFreeBlock();

    EXPECT_EQ(fs::Result::noSpace, Close(*file));
    EXPECT_EQ(fs::NorBlockState::freeNotErased, State().blocks.State(entries[0].block));
}

TEST_F(NorFileWriteTest, releasing_a_handle_without_changes_writes_nothing)
{
    auto file = Open("/a", fs::OpenMode::write);
    device.recording = true;

    file = nullptr;
    ExecuteAllActions();

    EXPECT_TRUE(device.requests.empty());
    Open("/a", fs::OpenMode::append);
}

TEST_F(NorFileWriteWideUnitTest, an_inline_area_that_does_not_end_a_whole_program_unit_is_corrupted)
{
    Unmount();
    auto file = Builder().AddFile(root, "a", infra::MakeRange(Pattern<100>()));
    Builder().Commit();
    Builder().AddRecord(file, fs::NorInodeRecord{ root, fs::NorInodeType::file, Builder().MapOf(file).height, 100, Builder().MapOf(file).address, Bytes("xyz"), Bytes("a") });
    Builder().Commit();
    Mount();

    Open("/a", fs::OpenMode::append, fs::Result::corrupted);
}

TEST_F(NorFileWriteDeepTest, a_file_created_after_a_checkpoint_takes_one_handle)
{
    AddDirectories(root, 3);
    auto parent = Builder().AddDirectory(root, "p");
    AddDirectories(parent, 18);
    Mount();
    ModifyLeavesOf({ 2, 4, 6 });
    for (uint32_t index = 0; Volume().Cache().ModifiedNodes() < 12; ++index)
        ModifyLeavesOf({ 8 + 2 * index });
    auto checkpointBlock = State().checkpointBlock;

    auto file = Open("/p/x", fs::OpenMode::write);

    EXPECT_NE(checkpointBlock, State().checkpointBlock);
    auto other = Open("/y", fs::OpenMode::write);
    Open("/z", fs::OpenMode::write, fs::Result::tooManyOpenFiles);
}

namespace
{
    constexpr fs::NorConfig longJournal{ .maxBlocks = 32, .cachedNodes = 15, .journalBlocks = 4 };

    class NorFileWriteLongJournalTest
        : public NorFileWriteFixture<longJournal, 32, blockSize>
    {
    public:
        NorFileWriteLongJournalTest()
        {
            Mount();
        }
    };
}

TEST_F(NorFileWriteLongJournalTest, a_released_block_that_became_a_journal_block_keeps_its_records_after_a_power_loss)
{
    Unmount();
    auto contents = Pattern<5000>();
    Builder().AddFile(root, "a", infra::MakeRange(contents));
    Builder().Commit();
    Mount();
    auto previous = BlockOf("/a", 1);
    auto file = Open("/a", fs::OpenMode::append);
    ASSERT_EQ(fs::Result::success, Write(*file, Bytes("0")));
    ASSERT_EQ(fs::Result::success, Sync(*file));
    for (uint32_t block = 2; block != State().blocks.Blocks(); ++block)
        if (block != previous && State().blocks.State(block) != fs::NorBlockState::inUse)
            State().blocks.Set(block, fs::NorBlockState::inUse);

    uint32_t size = 5001;
    while (State().journal.block != previous && size != 6000)
    {
        ASSERT_EQ(fs::Result::success, Write(*file, Bytes("1")));
        ASSERT_EQ(fs::Result::success, Sync(*file));
        ++size;
    }
    ASSERT_EQ(previous, State().journal.block);
    ASSERT_EQ(fs::Result::success, Write(*file, Bytes("2")));
    ASSERT_EQ(fs::Result::success, Sync(*file));
    ++size;

    fs::ExposedNorFileSystem<longJournal, 2> afterPowerLoss{ flash };
    std::optional<fs::Result> mounted;
    afterPowerLoss.Mount([&mounted](fs::Result result)
        {
            mounted = result;
        });
    ExecuteAllActions();
    std::optional<uint32_t> statSize;
    afterPowerLoss.Stat("/a", [&statSize](fs::Result, const fs::EntryInfo& info)
        {
            statSize = info.size;
        });
    ExecuteAllActions();

    EXPECT_EQ(fs::Result::success, mounted);
    EXPECT_EQ(size, statSize);
    EXPECT_EQ(fs::NorBlockState::inUse, afterPowerLoss.Volume().State().blocks.State(previous));
    EXPECT_EQ(State().journal, afterPowerLoss.Volume().State().journal);
}
