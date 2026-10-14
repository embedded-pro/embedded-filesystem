#include "fs/nor/NorDirectoryKey.hpp"
#include "fs/nor/NorFileSystem.hpp"
#include "fs/nor/test_doubles/NorFlashStubImage.hpp"
#include "fs/nor/test_doubles/NorImageBuilder.hpp"
#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <optional>

namespace
{
    constexpr fs::NorConfig config{ .maxBlocks = 32, .cachedNodes = 15, .maxNameBytes = 16, .maxPathBytes = 40, .directories = 2 };
    constexpr uint32_t root = fs::NorImageBuilder::rootInode;

    using Name = infra::BoundedString::WithStorage<16>;
    using Listing = infra::BoundedVector<std::pair<Name, fs::EntryInfo>>::WithMaxSize<80>;

    infra::ConstByteRange Bytes(const char* text)
    {
        return infra::StringAsByteRange(infra::BoundedConstString(text));
    }

    class NorDirectoriesTest
        : public testing::Test
        , public infra::ClockFixture
    {
    public:
        NorDirectoriesTest()
        {
            volume.Format([](fs::Result) {});
            ExecuteAllActions();
        }

        fs::NorImageBuilder& Builder()
        {
            if (!builder.has_value())
                builder.emplace(image, config);

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

        void ExpectStat(infra::BoundedConstString path, fs::Result result, const fs::EntryInfo& info = {})
        {
            testing::StrictMock<infra::MockCallback<void(fs::Result, fs::EntryInfo)>> done;
            volume.Stat(path, [&done](fs::Result statted, const fs::EntryInfo& found)
                {
                    done.callback(statted, found);
                });

            EXPECT_CALL(done, callback(result, info));
            ExecuteAllActions();
        }

        infra::SharedPtr<fs::Directory> OpenDirectory(infra::BoundedConstString path, fs::Result result = fs::Result::success)
        {
            infra::SharedPtr<fs::Directory> opened;
            testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
            volume.OpenDirectory(path, [&done, &opened](fs::Result openResult, infra::SharedPtr<fs::Directory> directory)
                {
                    opened = directory;
                    done.callback(openResult);
                });

            EXPECT_CALL(done, callback(result));
            ExecuteAllActions();
            EXPECT_EQ(result == fs::Result::success, opened != nullptr);
            return opened;
        }

        fs::Result Next(fs::Directory& directory, std::optional<std::pair<Name, fs::EntryInfo>>& entry)
        {
            std::optional<fs::Result> result;
            directory.Next([&result, &entry](fs::Result nextResult, std::optional<fs::EntryInfo> info)
                {
                    result = nextResult;
                    entry = std::nullopt;
                    if (info.has_value())
                        entry.emplace(Name(info->name), fs::EntryInfo{ {}, info->type, info->size });
                });

            EXPECT_FALSE(result.has_value());
            ExecuteAllActions();
            return result.value_or(fs::Result::busy);
        }

        Listing List(infra::BoundedConstString path)
        {
            Listing listing;
            auto directory = OpenDirectory(path);
            std::optional<std::pair<Name, fs::EntryInfo>> entry;

            while (directory != nullptr && Next(*directory, entry) == fs::Result::success && entry.has_value())
                listing.push_back(*entry);

            return listing;
        }

        hal::FlashStub flash{ 32, 4096 };
        fs::NorFlashStubImage image{ flash };
        std::optional<fs::NorImageBuilder> builder;
        fs::NorFileSystem::WithStorage<config, 1> volume{ flash };
    };
}

TEST_F(NorDirectoriesTest, stat_of_the_root_is_a_directory_with_an_empty_name)
{
    Mount();

    ExpectStat("/", fs::Result::success, fs::EntryInfo{ "", fs::EntryType::directory, 0 });
}

TEST_F(NorDirectoriesTest, stat_reports_the_type_and_size_of_a_directory_and_of_a_file)
{
    Builder().AddDirectory(root, "docs");
    Builder().AddFile(root, "a.txt", Bytes("hello"));
    Mount();

    ExpectStat("/docs", fs::Result::success, fs::EntryInfo{ "docs", fs::EntryType::directory, 0 });
    ExpectStat("/a.txt", fs::Result::success, fs::EntryInfo{ "a.txt", fs::EntryType::file, 5 });
}

TEST_F(NorDirectoriesTest, stat_walks_a_path_of_several_directories)
{
    auto docs = Builder().AddDirectory(root, "docs");
    auto sub = Builder().AddDirectory(docs, "sub");
    Builder().AddFile(sub, "b", Bytes("12"));
    Mount();

    ExpectStat("/docs/sub/b", fs::Result::success, fs::EntryInfo{ "b", fs::EntryType::file, 2 });
}

TEST_F(NorDirectoriesTest, stat_of_a_missing_name_is_not_found)
{
    Builder().AddDirectory(root, "docs");
    Mount();

    ExpectStat("/missing", fs::Result::notFound);
    ExpectStat("/missing/a", fs::Result::notFound);
    ExpectStat("/docs/missing", fs::Result::notFound);
}

TEST_F(NorDirectoriesTest, a_file_inside_a_path_is_not_a_directory)
{
    Builder().AddFile(root, "a", Bytes("x"));
    Mount();

    ExpectStat("/a/b", fs::Result::notADirectory);
}

TEST_F(NorDirectoriesTest, names_with_the_same_hash_are_told_apart_by_their_records)
{
    Builder().AddFile(root, "t15311", Bytes("1"));
    Builder().AddFile(root, "t636020", Bytes("333"));
    Mount();

    ExpectStat("/t15311", fs::Result::success, fs::EntryInfo{ "t15311", fs::EntryType::file, 1 });
    ExpectStat("/t636020", fs::Result::success, fs::EntryInfo{ "t636020", fs::EntryType::file, 3 });
    ExpectStat("/t498781", fs::Result::notFound);
}

TEST_F(NorDirectoriesTest, a_name_found_after_the_end_of_a_leaf_is_found_in_the_next_leaf)
{
    for (uint32_t index = 0; index != 70; ++index)
    {
        std::array<char, 3> name{ 'f', static_cast<char>('a' + index / 10), static_cast<char>('0' + index % 10) };
        Builder().AddFile(root, infra::BoundedConstString(name.data(), name.size()), Bytes("x"));
    }
    Mount();

    for (uint32_t index = 0; index != 70; ++index)
    {
        std::array<char, 4> path{ '/', 'f', static_cast<char>('a' + index / 10), static_cast<char>('0' + index % 10) };
        ExpectStat(infra::BoundedConstString(path.data(), path.size()), fs::Result::success, fs::EntryInfo{ infra::BoundedConstString(path.data() + 1, 3), fs::EntryType::file, 1 });
    }
}

TEST_F(NorDirectoriesTest, paths_and_names_are_checked_before_any_lookup)
{
    Mount();

    ExpectStat("docs", fs::Result::invalidPath);
    ExpectStat("/docs/", fs::Result::invalidPath);
    ExpectStat("/0123456789abcdefg", fs::Result::nameTooLong);
    ExpectStat("/0123456789/0123456789/0123456789/0123456789", fs::Result::nameTooLong);
    OpenDirectory("docs", fs::Result::invalidPath);
}

TEST_F(NorDirectoriesTest, a_name_below_every_key_of_its_directory_is_not_found)
{
    Builder().AddFile(root, "a", Bytes("x"));
    Mount();

    ExpectStat("/docs", fs::Result::notFound);
}

TEST_F(NorDirectoriesTest, a_name_above_every_key_of_its_directory_is_not_found)
{
    Builder().AddFile(root, "docs", Bytes("x"));
    Mount();

    ExpectStat("/a", fs::Result::notFound);
}

TEST_F(NorDirectoriesTest, a_name_that_differs_from_the_last_collision_index_is_not_found)
{
    Builder().AddRecord(9, fs::NorInodeRecord{ root, fs::NorInodeType::file, 0, 0, 0, {}, Bytes("zzz") });
    Builder().AddEntry(root, fs::NorLastDirectoryKey(fs::NorDirectoryKey("abc")), 9);
    Mount();

    ExpectStat("/abc", fs::Result::notFound);
}

TEST_F(NorDirectoriesTest, stat_completes_only_through_the_event_dispatcher)
{
    Mount();
    testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;

    volume.Stat("/", [&done](fs::Result result, const fs::EntryInfo&)
        {
            done.callback(result);
        });
    testing::Mock::VerifyAndClearExpectations(&done);

    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(NorDirectoriesTest, a_call_beyond_the_path_slots_completes_with_busy)
{
    Mount();
    testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    auto onDone = [&done](fs::Result result, const fs::EntryInfo&)
    {
        done.callback(result);
    };

    volume.Stat("/a", onDone);
    volume.Stat("/b", onDone);
    volume.Stat("/c", onDone);
    volume.OpenDirectory("/d", [&done](fs::Result result, infra::SharedPtr<fs::Directory>)
        {
            done.callback(result);
        });
    volume.Open("/e", fs::OpenMode::read, [&done](fs::Result result, infra::SharedPtr<fs::File>)
        {
            done.callback(result);
        });

    EXPECT_CALL(done, callback(fs::Result::notFound)).Times(2);
    EXPECT_CALL(done, callback(fs::Result::busy)).Times(3);
    ExecuteAllActions();
}

TEST_F(NorDirectoriesTest, an_entry_whose_inode_has_no_record_is_corrupted)
{
    Builder().AddEntry(root, fs::NorDirectoryKey("ghost"), 9);
    Mount();

    ExpectStat("/ghost", fs::Result::corrupted);
}

TEST_F(NorDirectoriesTest, a_record_whose_parent_is_another_directory_is_corrupted)
{
    Builder().AddRecord(9, fs::NorInodeRecord{ 5, fs::NorInodeType::file, 0, 0, 0, {}, Bytes("lost") });
    Builder().AddEntry(root, fs::NorDirectoryKey("lost"), 9);
    Mount();

    ExpectStat("/lost", fs::Result::corrupted);
}

TEST_F(NorDirectoriesTest, a_record_that_does_not_parse_is_corrupted)
{
    Builder().AddRecord(9, fs::NorInodeRecord{ root, static_cast<fs::NorInodeType>(7), 0, 0, 0, {}, Bytes("odd") });
    Builder().AddEntry(root, fs::NorDirectoryKey("odd"), 9);
    Mount();

    ExpectStat("/odd", fs::Result::corrupted);
}

TEST_F(NorDirectoriesTest, a_root_that_is_not_a_directory_is_corrupted)
{
    Builder().AddRecord(root, fs::NorInodeRecord{ 0, fs::NorInodeType::file });
    Mount();

    ExpectStat("/a", fs::Result::corrupted);
}

TEST_F(NorDirectoriesTest, a_directory_node_with_a_bad_crc_is_corrupted)
{
    Builder().AddDirectory(root, "docs");
    Builder().Commit();
    Builder().Corrupt(Builder().MapOf(root).address + 13);
    Mount();

    ExpectStat("/docs", fs::Result::corrupted);
}

TEST_F(NorDirectoriesTest, an_inode_table_node_with_a_bad_crc_is_corrupted)
{
    Builder().AddDirectory(root, "docs");
    Builder().Commit();
    Builder().Corrupt(Builder().InodeTable().address + 20);
    Mount();

    ExpectStat("/docs", fs::Result::corrupted);
}

TEST_F(NorDirectoriesTest, the_root_directory_starts_empty)
{
    Mount();

    EXPECT_TRUE(List("/").empty());
}

TEST_F(NorDirectoriesTest, next_lists_the_entries_in_the_order_of_their_keys)
{
    auto docs = Builder().AddDirectory(root, "docs");
    Builder().AddFile(root, "a", Bytes("abc"));
    Builder().AddFile(docs, "inner", Bytes("12345"));
    Mount();

    auto listing = List("/");

    ASSERT_EQ(2u, listing.size());
    EXPECT_EQ("docs", listing[0].first);
    EXPECT_EQ(fs::EntryType::directory, listing[0].second.type);
    EXPECT_EQ(0u, listing[0].second.size);
    EXPECT_EQ("a", listing[1].first);
    EXPECT_EQ(fs::EntryType::file, listing[1].second.type);
    EXPECT_EQ(3u, listing[1].second.size);

    auto inner = List("/docs");
    ASSERT_EQ(1u, inner.size());
    EXPECT_EQ("inner", inner[0].first);
}

TEST_F(NorDirectoriesTest, next_lists_a_directory_of_several_leaves_once)
{
    for (uint32_t index = 0; index != 70; ++index)
    {
        std::array<char, 3> name{ 'f', static_cast<char>('a' + index / 10), static_cast<char>('0' + index % 10) };
        Builder().AddFile(root, infra::BoundedConstString(name.data(), name.size()), Bytes("x"));
    }
    Mount();

    auto listing = List("/");

    EXPECT_EQ(70u, listing.size());
    for (std::size_t index = 0; index != listing.size(); ++index)
        for (std::size_t other = 0; other != index; ++other)
            EXPECT_NE(listing[index].first, listing[other].first);
}

TEST_F(NorDirectoriesTest, next_lists_names_with_the_same_hash)
{
    Builder().AddFile(root, "t15311", Bytes("1"));
    Builder().AddFile(root, "t498781", Bytes("22"));
    Mount();

    auto listing = List("/");

    ASSERT_EQ(2u, listing.size());
    EXPECT_EQ("t15311", listing[0].first);
    EXPECT_EQ("t498781", listing[1].first);
}

TEST_F(NorDirectoriesTest, next_after_the_highest_key_ends_the_directory)
{
    Builder().AddFile(root, "x", Bytes(""));
    Builder().AddEntry(root, 0xffffffff, 2);
    Mount();

    auto listing = List("/");

    EXPECT_EQ(2u, listing.size());
}

TEST_F(NorDirectoriesTest, open_directory_of_a_file_is_not_a_directory)
{
    Builder().AddFile(root, "a", Bytes("x"));
    Mount();

    OpenDirectory("/a", fs::Result::notADirectory);
    OpenDirectory("/b", fs::Result::notFound);
}

TEST_F(NorDirectoriesTest, open_directory_beyond_the_pool_completes_with_too_many_open_files)
{
    Mount();

    auto first = OpenDirectory("/");
    auto second = OpenDirectory("/");
    OpenDirectory("/", fs::Result::tooManyOpenFiles);

    first = nullptr;
    OpenDirectory("/");
}

TEST_F(NorDirectoriesTest, next_while_next_is_pending_completes_with_busy)
{
    Builder().AddFile(root, "a", Bytes("x"));
    Mount();
    auto directory = OpenDirectory("/");
    testing::StrictMock<infra::MockCallback<void(fs::Result)>> done;
    auto onDone = [&done](fs::Result result, std::optional<fs::EntryInfo>)
    {
        done.callback(result);
    };

    directory->Next(onDone);
    directory->Next(onDone);

    EXPECT_CALL(done, callback(fs::Result::busy));
    EXPECT_CALL(done, callback(fs::Result::success));
    ExecuteAllActions();
}

TEST_F(NorDirectoriesTest, next_after_unmount_and_a_new_mount_completes_with_not_mounted)
{
    Mount();
    auto directory = OpenDirectory("/");
    std::optional<std::pair<Name, fs::EntryInfo>> entry;

    Unmount();
    EXPECT_EQ(fs::Result::notMounted, Next(*directory, entry));

    Mount();
    EXPECT_EQ(fs::Result::notMounted, Next(*directory, entry));
}

TEST_F(NorDirectoriesTest, next_with_a_corrupted_record_of_the_directory_is_corrupted)
{
    Builder().AddDirectory(root, "docs");
    Builder().Commit();
    auto inodeTable = Builder().InodeTable().address;
    Mount();
    auto opened = OpenDirectory("/");
    Builder().Corrupt(inodeTable + 20);
    std::optional<std::pair<Name, fs::EntryInfo>> entry;

    EXPECT_EQ(fs::Result::corrupted, Next(*opened, entry));
}

TEST_F(NorDirectoriesTest, next_over_an_entry_without_a_record_is_corrupted)
{
    Builder().AddEntry(root, fs::NorDirectoryKey("ghost"), 9);
    Mount();
    auto directory = OpenDirectory("/");
    std::optional<std::pair<Name, fs::EntryInfo>> entry;

    EXPECT_EQ(fs::Result::corrupted, Next(*directory, entry));
}

TEST_F(NorDirectoriesTest, open_directory_of_an_entry_without_a_record_is_corrupted)
{
    Builder().AddEntry(root, fs::NorDirectoryKey("ghost"), 9);
    Mount();

    OpenDirectory("/ghost", fs::Result::corrupted);
}

TEST_F(NorDirectoriesTest, next_with_a_corrupted_directory_node_is_corrupted)
{
    Builder().AddDirectory(root, "docs");
    Builder().Commit();
    auto directory = Builder().MapOf(root).address;
    Mount();
    auto opened = OpenDirectory("/");
    Builder().Corrupt(directory + 13);
    std::optional<std::pair<Name, fs::EntryInfo>> entry;

    EXPECT_EQ(fs::Result::corrupted, Next(*opened, entry));
}

TEST_F(NorDirectoriesTest, next_over_an_entry_of_another_directory_is_corrupted)
{
    Builder().AddRecord(9, fs::NorInodeRecord{ 5, fs::NorInodeType::file, 0, 0, 0, {}, Bytes("lost") });
    Builder().AddEntry(root, fs::NorDirectoryKey("lost"), 9);
    Mount();
    auto directory = OpenDirectory("/");
    std::optional<std::pair<Name, fs::EntryInfo>> entry;

    EXPECT_EQ(fs::Result::corrupted, Next(*directory, entry));
}
