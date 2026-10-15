#include "fs/nor/NorDirectoryKey.hpp"
#include "fs/nor/NorFileSystem.hpp"
#include "fs/nor/test_doubles/NorFlashStubImage.hpp"
#include "fs/nor/test_doubles/NorImageBuilder.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15 };
    constexpr fs::NorConfig wideUnitConfig{ .maxBlocks = 48, .programUnit = 16, .pageSize = 16, .cachedNodes = 15 };
    constexpr uint32_t root = fs::NorImageBuilder::rootInode;

    infra::ConstByteRange Bytes(const char* text)
    {
        return infra::StringAsByteRange(infra::BoundedConstString(text));
    }

    template<std::size_t Size>
    std::array<uint8_t, Size> Pattern()
    {
        std::array<uint8_t, Size> pattern{};
        for (std::size_t index = 0; index != Size; ++index)
            pattern[index] = static_cast<uint8_t>(index * 13 + index / 251);

        return pattern;
    }

    template<fs::NorConfig Config, std::size_t Files>
    class ExposedVolume
        : public fs::NorFileSystem::WithStorage<Config, Files>
    {
    public:
        using fs::NorFileSystem::WithStorage<Config, Files>::WithStorage;
        using fs::NorFileSystem::Volume;
    };

    template<fs::NorConfig Config, std::size_t Files, uint32_t Sectors, uint32_t SectorSize>
    class NorFilesFixture
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        NorFilesFixture()
        {
            volume.Format([](fs::Result) {});
            ExecuteAllActions();
        }

        fs::NorImageBuilder& Builder()
        {
            if (!builder.has_value())
                builder.emplace(image, Config);

            return *builder;
        }

        void Mount()
        {
            if (builder.has_value())
                builder->Commit();

            testing::StrictMock<infra::MockCallback<void(fs::Result)>> mounted;
            volume.Mount([&mounted](fs::Result result)
                {
                    mounted.callback(result);
                });
            EXPECT_CALL(mounted, callback(fs::Result::success));
            ExecuteAllActions();
        }

        void Unmount()
        {
            volume.Unmount([](fs::Result) {});
            ExecuteAllActions();
        }

        infra::SharedPtr<fs::File> Open(infra::BoundedConstString path, fs::Result result = fs::Result::success, fs::OpenMode mode = fs::OpenMode::read)
        {
            infra::SharedPtr<fs::File> opened;
            testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
            volume.Open(path, mode, [&done, &opened](fs::Result openResult, infra::SharedPtr<fs::File> file)
                {
                    opened = file;
                    done.callback(openResult);
                });

            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
            EXPECT_EQ(result == fs::Result::success, opened != nullptr);
            return opened;
        }

        std::pair<fs::Result, std::size_t> Read(fs::File& file, infra::ByteRange buffer)
        {
            std::optional<std::pair<fs::Result, std::size_t>> read;
            file.Read(buffer, [&read](fs::Result result, std::size_t bytesRead)
                {
                    read.emplace(result, bytesRead);
                });

            EXPECT_FALSE(read.has_value());
            ExecuteAllActions();
            return read.value_or(std::make_pair(fs::Result::busy, std::size_t{ 0 }));
        }

        template<std::size_t Size>
        void ExpectContentsInPieces(fs::File& file, const std::array<uint8_t, Size>& contents, std::size_t piece)
        {
            std::array<uint8_t, Size> read{};
            std::size_t done = 0;
            while (done != Size)
            {
                auto result = Read(file, infra::Head(infra::DiscardHead(infra::MakeRange(read), done), std::min(piece, Size - done)));
                ASSERT_EQ(fs::Result::success, result.first);
                ASSERT_NE(0u, result.second);
                done += result.second;
            }

            EXPECT_EQ(contents, read);
            EXPECT_EQ(Size, file.Position());
        }

        fs::Result Complete(const infra::Function<void(const infra::Function<void(fs::Result)>&)>& call)
        {
            std::optional<fs::Result> completed;
            call([&completed](fs::Result result)
                {
                    completed = result;
                });

            EXPECT_FALSE(completed.has_value());
            ExecuteAllActions();
            return completed.value_or(fs::Result::deviceError);
        }

        hal::FlashStub flash{ Sectors, SectorSize };
        fs::NorFlashStubImage image{ flash };
        std::optional<fs::NorImageBuilder> builder;
        ExposedVolume<Config, Files> volume{ flash };
    };

    class NorFilesTest
        : public NorFilesFixture<config, 2, 32, 4096>
    {};

    class NorFilesWideUnitTest
        : public NorFilesFixture<wideUnitConfig, 1, 48, 2048>
    {};
}

TEST_F(NorFilesTest, open_of_a_missing_file_is_not_found)
{
    Mount();

    Open("/missing", fs::Result::notFound);
}

TEST_F(NorFilesTest, open_of_a_directory_or_the_root_is_a_directory)
{
    Builder().AddDirectory(root, "docs");
    Mount();

    Open("/docs", fs::Result::isADirectory);
    Open("/", fs::Result::isADirectory);
}

TEST_F(NorFilesTest, open_in_write_mode_of_a_file_that_is_not_empty_empties_it)
{
    Builder().AddFile(root, "a", Bytes("x"));
    Mount();

    auto file = Open("/a", fs::Result::success, fs::OpenMode::write);

    ASSERT_NE(nullptr, file);
    EXPECT_EQ(0, file->Size());
}

TEST_F(NorFilesTest, open_checks_the_path_and_the_volume)
{
    Open("/a", fs::Result::notMounted);
    Mount();

    Open("a", fs::Result::invalidPath);
}

TEST_F(NorFilesTest, open_beyond_the_pool_completes_with_too_many_open_files)
{
    Builder().AddFile(root, "a", Bytes("x"));
    Mount();

    auto first = Open("/a");
    auto second = Open("/a");
    Open("/a", fs::Result::tooManyOpenFiles);

    first = nullptr;
    Open("/a");
}

TEST_F(NorFilesTest, a_file_opens_at_position_0_with_its_size)
{
    auto contents = Pattern<9000>();
    Builder().AddFile(root, "big", infra::MakeRange(contents));
    Mount();

    auto file = Open("/big");

    EXPECT_EQ(9000u, file->Size());
    EXPECT_EQ(0u, file->Position());
}

TEST_F(NorFilesTest, reading_an_empty_file_reads_nothing)
{
    Builder().AddFile(root, "empty", {});
    Mount();
    auto file = Open("/empty");
    std::array<uint8_t, 4> buffer{};

    EXPECT_EQ(std::make_pair(fs::Result::success, std::size_t{ 0 }), Read(*file, infra::MakeRange(buffer)));
}

TEST_F(NorFilesTest, reads_a_file_held_in_its_inline_area)
{
    Builder().AddFile(root, "a", Bytes("hello"));
    Mount();
    auto file = Open("/a");
    std::array<uint8_t, 5> buffer{};

    EXPECT_EQ(std::make_pair(fs::Result::success, std::size_t{ 5 }), Read(*file, infra::MakeRange(buffer)));
    EXPECT_TRUE(infra::ContentsEqual(Bytes("hello"), infra::MakeRange(buffer)));
}

TEST_F(NorFilesTest, reads_a_file_of_one_data_block_and_an_inline_tail)
{
    auto contents = Pattern<4096>();
    Builder().AddFile(root, "exact", infra::MakeRange(contents));
    Mount();
    auto file = Open("/exact");
    std::array<uint8_t, 4096> buffer{};

    EXPECT_EQ(std::make_pair(fs::Result::success, std::size_t{ 4096 }), Read(*file, infra::MakeRange(buffer)));
    EXPECT_EQ(contents, buffer);
}

TEST_F(NorFilesTest, reads_a_file_of_several_data_blocks_in_one_read)
{
    auto contents = Pattern<9000>();
    Builder().AddFile(root, "big", infra::MakeRange(contents));
    Mount();
    auto file = Open("/big");
    std::array<uint8_t, 9000> buffer{};

    EXPECT_EQ(std::make_pair(fs::Result::success, std::size_t{ 9000 }), Read(*file, infra::MakeRange(buffer)));
    EXPECT_EQ(contents, buffer);
}

TEST_F(NorFilesTest, reads_across_the_ends_of_data_blocks_in_small_pieces)
{
    auto contents = Pattern<9000>();
    Builder().AddFile(root, "big", infra::MakeRange(contents));
    Mount();
    auto file = Open("/big");

    ExpectContentsInPieces(*file, contents, 333);
}

TEST_F(NorFilesTest, read_stops_at_the_end_of_the_file)
{
    Builder().AddFile(root, "a", Bytes("hello"));
    Mount();
    auto file = Open("/a");
    std::array<uint8_t, 8> buffer{};

    EXPECT_EQ(std::make_pair(fs::Result::success, std::size_t{ 5 }), Read(*file, infra::MakeRange(buffer)));
    EXPECT_EQ(std::make_pair(fs::Result::success, std::size_t{ 0 }), Read(*file, infra::MakeRange(buffer)));
    EXPECT_EQ(5u, file->Position());
}

TEST_F(NorFilesTest, seek_moves_the_position_and_stops_at_the_end)
{
    auto contents = Pattern<4096>();
    Builder().AddFile(root, "exact", infra::MakeRange(contents));
    Mount();
    auto file = Open("/exact");
    std::array<uint8_t, 40> buffer{};

    file->Seek(4050);
    EXPECT_EQ(std::make_pair(fs::Result::success, std::size_t{ 40 }), Read(*file, infra::MakeRange(buffer)));
    EXPECT_TRUE(infra::ContentsEqual(infra::Head(infra::DiscardHead(infra::MakeRange(contents), 4050), 40), infra::MakeRange(buffer)));

    file->Seek(5000);
    EXPECT_EQ(4096u, file->Position());
}

TEST_F(NorFilesTest, a_read_while_a_read_is_pending_completes_with_busy)
{
    Builder().AddFile(root, "a", Bytes("hello"));
    Mount();
    auto file = Open("/a");
    std::array<uint8_t, 5> buffer{};
    testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    auto onRead = [&done](fs::Result result, std::size_t)
    {
        done.callback(result);
    };

    file->Read(infra::MakeRange(buffer), onRead);
    file->Read(infra::MakeRange(buffer), onRead);

    EXPECT_CALL(done, callback(fs::Result::busy));
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(NorFilesTest, changes_to_a_file_opened_for_reading_complete_with_read_only)
{
    Builder().AddFile(root, "a", Bytes("hello"));
    Mount();
    auto file = Open("/a");

    EXPECT_EQ(fs::Result::readOnly, Complete([&file](const infra::Function<void(fs::Result)>& onDone)
                                        {
                                            file->Write(Bytes("x"), onDone);
                                        }));
    EXPECT_EQ(fs::Result::readOnly, Complete([&file](const infra::Function<void(fs::Result)>& onDone)
                                        {
                                            file->Truncate(onDone);
                                        }));
    EXPECT_EQ(fs::Result::readOnly, Complete([&file](const infra::Function<void(fs::Result)>& onDone)
                                        {
                                            file->ExpectSize(10, onDone);
                                        }));
}

TEST_F(NorFilesTest, sync_and_close_of_a_file_opened_for_reading_succeed)
{
    Builder().AddFile(root, "a", Bytes("hello"));
    Mount();
    auto file = Open("/a");

    EXPECT_EQ(fs::Result::success, Complete([&file](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           file->Sync(onDone);
                                       }));
    EXPECT_EQ(fs::Result::success, Complete([&file](const infra::Function<void(fs::Result)>& onDone)
                                       {
                                           file->Close(onDone);
                                       }));
}

TEST_F(NorFilesTest, calls_while_a_read_is_pending_complete_with_busy)
{
    Builder().AddFile(root, "a", Bytes("hello"));
    Mount();
    auto file = Open("/a");
    std::array<uint8_t, 5> buffer{};
    testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    auto onDone = [&done](fs::Result result)
    {
        done.callback(result);
    };

    file->Read(infra::MakeRange(buffer), [&done](fs::Result result, std::size_t)
        {
            done.callback(result);
        });
    file->Write(Bytes("x"), onDone);
    file->Truncate(onDone);
    file->ExpectSize(10, onDone);
    file->Sync(onDone);
    file->Close(onDone);

    EXPECT_CALL(done, callback(fs::Result::busy)).Times(5);
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(NorFilesTest, calls_after_unmount_and_a_new_mount_complete_with_not_mounted)
{
    Builder().AddFile(root, "a", Bytes("hello"));
    Mount();
    auto file = Open("/a");
    std::array<uint8_t, 5> buffer{};

    Unmount();
    EXPECT_EQ(std::make_pair(fs::Result::notMounted, std::size_t{ 0 }), Read(*file, infra::MakeRange(buffer)));

    Mount();
    EXPECT_EQ(std::make_pair(fs::Result::notMounted, std::size_t{ 0 }), Read(*file, infra::MakeRange(buffer)));
    EXPECT_EQ(fs::Result::notMounted, Complete([&file](const infra::Function<void(fs::Result)>& onDone)
                                          {
                                              file->Sync(onDone);
                                          }));
    EXPECT_EQ(fs::Result::notMounted, Complete([&file](const infra::Function<void(fs::Result)>& onDone)
                                          {
                                              file->Close(onDone);
                                          }));
}

TEST_F(NorFilesTest, a_logical_block_without_a_data_block_is_corrupted)
{
    Builder().AddRecord(9, fs::NorInodeRecord{ root, fs::NorInodeType::file, 0, 5000, 0, {}, Bytes("gap") });
    Builder().AddEntry(root, fs::NorDirectoryKey("gap"), 9);
    Mount();
    auto file = Open("/gap");
    std::array<uint8_t, 10> buffer{};

    EXPECT_EQ(std::make_pair(fs::Result::corrupted, std::size_t{ 0 }), Read(*file, infra::MakeRange(buffer)));
    EXPECT_EQ(0u, file->Position());
}

TEST_F(NorFilesTest, an_inline_area_longer_than_the_file_is_corrupted)
{
    Builder().AddRecord(9, fs::NorInodeRecord{ root, fs::NorInodeType::file, 0, 2, 0, Bytes("hello"), Bytes("odd") });
    Builder().AddEntry(root, fs::NorDirectoryKey("odd"), 9);
    Mount();
    auto file = Open("/odd");
    std::array<uint8_t, 2> buffer{};

    EXPECT_EQ(fs::Result::corrupted, Read(*file, infra::MakeRange(buffer)).first);
}

TEST_F(NorFilesTest, a_data_block_outside_the_volume_is_corrupted)
{
    auto contents = Pattern<5000>();
    auto file = Builder().AddFile(root, "big", infra::MakeRange(contents));
    Builder().MapBlock(file, 0, 40);
    Mount();
    auto opened = Open("/big");
    std::array<uint8_t, 10> buffer{};

    EXPECT_EQ(fs::Result::corrupted, Read(*opened, infra::MakeRange(buffer)).first);
}

TEST_F(NorFilesTest, a_data_block_in_the_superblocks_is_corrupted)
{
    auto contents = Pattern<5000>();
    auto file = Builder().AddFile(root, "big", infra::MakeRange(contents));
    Builder().MapBlock(file, 0, 1);
    Mount();
    auto opened = Open("/big");
    std::array<uint8_t, 10> buffer{};

    EXPECT_EQ(fs::Result::corrupted, Read(*opened, infra::MakeRange(buffer)).first);
}

TEST_F(NorFilesTest, a_data_block_that_is_not_in_use_is_corrupted)
{
    auto contents = Pattern<5000>();
    auto file = Builder().AddFile(root, "big", infra::MakeRange(contents));
    Builder().MapBlock(file, 1, 25);
    Mount();
    auto opened = Open("/big");
    std::array<uint8_t, 5000> buffer{};

    EXPECT_EQ(fs::Result::corrupted, Read(*opened, infra::MakeRange(buffer)).first);
}

TEST_F(NorFilesTest, a_data_map_node_with_a_bad_crc_is_corrupted)
{
    auto contents = Pattern<5000>();
    auto file = Builder().AddFile(root, "big", infra::MakeRange(contents));
    Builder().Commit();
    Builder().Corrupt(Builder().MapOf(file).address + 12);
    Mount();
    auto opened = Open("/big");
    std::array<uint8_t, 10> buffer{};

    EXPECT_EQ(fs::Result::corrupted, Read(*opened, infra::MakeRange(buffer)).first);
}

TEST_F(NorFilesTest, a_read_whose_record_cannot_be_read_is_corrupted)
{
    Builder().AddFile(root, "a", Bytes("hello"));
    Mount();
    auto file = Open("/a");
    volume.Volume().State().inodeTable = fs::NorMapRoot{ 20 * 4096 + 256, 1 };
    std::array<uint8_t, 5> buffer{};

    EXPECT_EQ(fs::Result::corrupted, Read(*file, infra::MakeRange(buffer)).first);
}

TEST_F(NorFilesWideUnitTest, reads_a_file_whose_tail_is_less_than_a_program_unit)
{
    auto contents = Pattern<2107>();
    Builder().AddFile(root, "f", infra::MakeRange(contents));
    Mount();
    auto file = Open("/f");

    ExpectContentsInPieces(*file, contents, 100);
}

TEST_F(NorFilesWideUnitTest, reads_a_file_in_one_read)
{
    auto contents = Pattern<2107>();
    Builder().AddFile(root, "f", infra::MakeRange(contents));
    Mount();
    auto file = Open("/f");

    ExpectContentsInPieces(*file, contents, 4096);
}
