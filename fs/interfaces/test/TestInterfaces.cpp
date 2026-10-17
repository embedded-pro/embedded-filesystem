#include "fs/interfaces/test_doubles/DirectoryMock.hpp"
#include "fs/interfaces/test_doubles/FileMock.hpp"
#include "fs/interfaces/test_doubles/FileSystemMock.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "gmock/gmock.h"
#include <array>

TEST(FileSystemInterfaceTest, file_system_mock_implements_every_operation)
{
    testing::StrictMock<fs::FileSystemMock> mock;
    fs::FileSystem& fileSystem = mock;

    EXPECT_CALL(mock, Mount(testing::_));
    EXPECT_CALL(mock, Unmount(testing::_));
    EXPECT_CALL(mock, Open(infra::BoundedConstString("/log"), fs::OpenMode::append, testing::_));
    EXPECT_CALL(mock, OpenDirectory(infra::BoundedConstString("/"), testing::_));
    EXPECT_CALL(mock, Stat(infra::BoundedConstString("/log"), testing::_));
    EXPECT_CALL(mock, Remove(infra::BoundedConstString("/old"), testing::_));
    EXPECT_CALL(mock, Rename(infra::BoundedConstString("/a"), infra::BoundedConstString("/b"), testing::_));
    EXPECT_CALL(mock, MakeDirectory(infra::BoundedConstString("/dir"), testing::_));
    EXPECT_CALL(mock, Usage(testing::_));
    EXPECT_CALL(mock, Idle()).WillOnce(testing::Return(true));

    fileSystem.Mount([](fs::Result) {});
    fileSystem.Unmount([](fs::Result) {});
    fileSystem.Open("/log", fs::OpenMode::append, [](fs::Result, const infra::SharedPtr<fs::File>&) {});
    fileSystem.OpenDirectory("/", [](fs::Result, const infra::SharedPtr<fs::Directory>&) {});
    fileSystem.Stat("/log", [](fs::Result, const fs::EntryInfo&) {});
    fileSystem.Remove("/old", [](fs::Result) {});
    fileSystem.Rename("/a", "/b", [](fs::Result) {});
    fileSystem.MakeDirectory("/dir", [](fs::Result) {});
    fileSystem.Usage([](fs::Result, fs::VolumeUsage) {});
    EXPECT_TRUE(fileSystem.Idle());
}

TEST(FileSystemInterfaceTest, file_mock_implements_every_operation)
{
    testing::StrictMock<fs::FileMock> mock;
    fs::File& file = mock;
    std::array<uint8_t, 4> buffer{};

    EXPECT_CALL(mock, Size()).WillOnce(testing::Return(12));
    EXPECT_CALL(mock, Position()).WillOnce(testing::Return(4));
    EXPECT_CALL(mock, Seek(8));
    EXPECT_CALL(mock, Read(testing::_, testing::_));
    EXPECT_CALL(mock, Write(testing::_, testing::_));
    EXPECT_CALL(mock, Truncate(testing::_));
    EXPECT_CALL(mock, ExpectSize(4096, testing::_));
    EXPECT_CALL(mock, Sync(testing::_));
    EXPECT_CALL(mock, Close(testing::_));

    EXPECT_EQ(12, file.Size());
    EXPECT_EQ(4, file.Position());
    file.Seek(8);
    file.Read(infra::MakeRange(buffer), [](fs::Result, std::size_t) {});
    file.Write(infra::MakeRange(buffer), [](fs::Result) {});
    file.Truncate([](fs::Result) {});
    file.ExpectSize(4096, [](fs::Result) {});
    file.Sync([](fs::Result) {});
    file.Close([](fs::Result) {});
}

TEST(FileSystemInterfaceTest, directory_mock_reports_end_of_directory)
{
    testing::StrictMock<fs::DirectoryMock> mock;
    fs::Directory& directory = mock;
    testing::StrictMock<infra::MockCallback<void(fs::Result, std::optional<fs::EntryInfo>)>> done;

    EXPECT_CALL(mock, Next(testing::_)).WillOnce([](const auto& onDone)
        {
            onDone(fs::Result::success, std::nullopt);
        });
    EXPECT_CALL(done, callback(fs::Result::success, testing::Eq(std::nullopt)));

    directory.Next([&done](fs::Result result, std::optional<fs::EntryInfo> entry)
        {
            done.callback(result, entry);
        });
}

TEST(FileSystemInterfaceTest, entry_info_compares_name_type_and_size)
{
    fs::EntryInfo file{ "log", fs::EntryType::file, 12 };

    EXPECT_EQ((fs::EntryInfo{ "log", fs::EntryType::file, 12 }), file);
    EXPECT_NE((fs::EntryInfo{ "log", fs::EntryType::directory, 12 }), file);
    EXPECT_NE((fs::EntryInfo{ "log", fs::EntryType::file, 13 }), file);
    EXPECT_NE((fs::EntryInfo{ "data", fs::EntryType::file, 12 }), file);
}
