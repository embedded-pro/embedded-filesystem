#include "infra/stream/StringOutputStream.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "lfs.h"
#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/cycles/CycleRun.hpp"
#include "validation/firmware/cycles/subjects/CycleConfigurations.hpp"
#include "validation/firmware/cycles/subjects/FatFsSubject.hpp"
#include "validation/firmware/cycles/subjects/FileSystemSubject.hpp"
#include "validation/firmware/cycles/subjects/LittleFsSubject.hpp"
#include "validation/firmware/devices/RamBlockDevice.hpp"
#include "validation/firmware/devices/RamFlash.hpp"
#include "gmock/gmock.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

using namespace std::chrono_literals;

namespace
{
    constexpr int32_t notCompleted = -1000;

    class StoppedCycleCounter
        : public validation::CycleCounter
    {
    public:
        uint32_t Cycles() const override
        {
            return 0;
        }
    };

    class NoIdleCycles
        : public validation::IdleCycleSource
    {
    public:
        uint64_t IdleCycles() const override
        {
            return 0;
        }
    };

    std::array<uint8_t, validation::cycles::ramFlashSectors * validation::cycles::ramFlashSectorSize>& Memory()
    {
        static std::array<uint8_t, validation::cycles::ramFlashSectors * validation::cycles::ramFlashSectorSize> memory;
        return memory;
    }

    validation::CyclePlan SmallPlan()
    {
        validation::CyclePlan plan;
        plan.smallFiles = 3;
        plan.bigFileBytes = 4 * validation::CycleRun::writeBytes;
        return plan;
    }

    class CycleSubjectTest
        : public services::HilFixture
    {
    public:
        template<class Start>
        int32_t Await(Start start)
        {
            std::optional<int32_t> status;
            start([&status](int32_t result)
                {
                    status = result;
                });
            ExecuteAllActions();
            return status.value_or(notCompleted);
        }

        void ExpectFullRun(validation::CycleSubject& subject, const char* device)
        {
            validation::CycleRun run{ results, counter, idle, SmallPlan() };
            testing::StrictMock<infra::MockCallback<void(bool)>> done;
            run.Start(subject, device, 2, [&done](bool succeeded)
                {
                    done.callback(succeeded);
                });

            EXPECT_CALL(done, callback(true));
            ForwardTime(60s);

            const auto output = Output();
            std::size_t records = 0;
            for (auto position = output.find("EVT cycles id="); position != std::string::npos; position = output.find("EVT cycles id=", position + 1))
                ++records;
            EXPECT_EQ(12, records);

            infra::BoundedString::WithStorage<128> expected;
            infra::StringOutputStream stream{ expected };
            stream << "subject=" << subject.Name() << " device=" << device << " op=unmount n=2 ramBytes=" << static_cast<uint32_t>(subject.RamBytes()) << " ";
            EXPECT_NE(std::string::npos, output.find(expected.data(), 0, expected.size())) << output;
        }

        int32_t OpenAndWrite(validation::CycleSubject& subject, infra::BoundedConstString path, std::size_t bytes)
        {
            std::array<uint8_t, validation::CycleRun::writeBytes> data{};
            auto opened = Await([&](const auto& done)
                {
                    subject.Open(path, validation::CycleOpenMode::write, done);
                });
            if (opened != 0)
                return opened;

            auto written = Await([&](const auto& done)
                {
                    subject.Write(infra::Head(infra::MakeRange(data), bytes), done);
                });
            if (written != 0)
                return written;

            return Await([&](const auto& done)
                {
                    subject.Close(done);
                });
        }

        void FormatAndMount(validation::CycleSubject& subject)
        {
            EXPECT_EQ(0, Await([&](const auto& done)
                             {
                                 subject.Reset(done);
                             }));
            EXPECT_EQ(0, Await([&](const auto& done)
                             {
                                 subject.Format(done);
                             }));
            EXPECT_EQ(0, Await([&](const auto& done)
                             {
                                 subject.Mount(done);
                             }));
        }

        void ExpectResetWithAnOpenFileUnmounts(validation::CycleSubject& subject)
        {
            FormatAndMount(subject);
            EXPECT_EQ(0, Await([&](const auto& done)
                             {
                                 subject.Open("/open", validation::CycleOpenMode::write, done);
                             }));

            EXPECT_EQ(0, Await([&](const auto& done)
                             {
                                 subject.Reset(done);
                             }));
            EXPECT_EQ(0, Await([&](const auto& done)
                             {
                                 subject.Mount(done);
                             }));
        }

        int32_t ReadMoreThanTheFileHolds(validation::CycleSubject& subject)
        {
            FormatAndMount(subject);
            EXPECT_EQ(0, OpenAndWrite(subject, "/short", 64));
            EXPECT_EQ(0, Await([&](const auto& done)
                             {
                                 subject.Open("/short", validation::CycleOpenMode::read, done);
                             }));

            std::array<uint8_t, 512> buffer{};
            return Await([&](const auto& done)
                {
                    subject.Read(infra::MakeRange(buffer), done);
                });
        }

        int32_t ListAMissingDirectory(validation::CycleSubject& subject)
        {
            FormatAndMount(subject);
            return Await([&](const auto& done)
                {
                    subject.List("/missing", done);
                });
        }

        StoppedCycleCounter counter;
        NoIdleCycles idle;
        validation::ResultLines results{ response };
    };

    class EmfsFatSubjectTest
        : public CycleSubjectTest
    {
    public:
        validation::RamBlockDevice device{ infra::Head(infra::MakeRange(Memory()), validation::cycles::ramBlockDeviceBlocks * validation::RamBlockDevice::blockSize) };
        validation::EmfsFatSubject subject{ device };
    };

    class EmfsNorSubjectTest
        : public CycleSubjectTest
    {
    public:
        validation::RamFlash flash{ infra::MakeRange(Memory()), validation::cycles::ramFlashSectorSize };
        validation::EmfsNorSubject subject{ flash };
    };

    class FatFsSubjectTest
        : public CycleSubjectTest
    {
    public:
        validation::RamBlockDevice device{ infra::Head(infra::MakeRange(Memory()), validation::cycles::ramBlockDeviceBlocks * validation::RamBlockDevice::blockSize), validation::RamCompletion::immediate };
        validation::FatFsSubject subject{ device };
    };

    class LittleFsSubjectTest
        : public CycleSubjectTest
    {
    public:
        validation::RamFlash flash{ infra::MakeRange(Memory()), validation::cycles::ramFlashSectorSize, validation::RamCompletion::immediate };
        validation::LittleFsSubject subject{ flash };
    };
}

TEST_F(EmfsFatSubjectTest, runs_f2_on_the_ram_block_device)
{
    EXPECT_EQ("embedded-filesystem/fat", subject.Name());
    EXPECT_EQ(validation::cycles::fatRamBytes, subject.RamBytes());
    ExpectFullRun(subject, "ramblock");
}

TEST_F(EmfsFatSubjectTest, a_reset_closes_the_open_file_and_unmounts)
{
    ExpectResetWithAnOpenFileUnmounts(subject);
}

TEST_F(EmfsFatSubjectTest, a_read_beyond_the_end_of_the_file_is_short)
{
    EXPECT_EQ(validation::FileSystemSubject::shortTransfer, ReadMoreThanTheFileHolds(subject));
}

TEST_F(EmfsFatSubjectTest, listing_a_missing_directory_completes_with_the_result_of_the_volume)
{
    EXPECT_EQ(static_cast<int32_t>(fs::Result::notFound), ListAMissingDirectory(subject));
}

TEST_F(EmfsNorSubjectTest, runs_f2_on_the_ram_flash)
{
    EXPECT_EQ("embedded-filesystem/nor", subject.Name());
    EXPECT_EQ(validation::cycles::norRamBytes, subject.RamBytes());
    ExpectFullRun(subject, "ramflash");
}

TEST_F(FatFsSubjectTest, runs_f2_on_the_ram_block_device)
{
    EXPECT_EQ("fatfs/tuned", subject.Name());
    ExpectFullRun(subject, "ramblock");
}

TEST_F(FatFsSubjectTest, a_reset_closes_the_open_file_and_unmounts)
{
    ExpectResetWithAnOpenFileUnmounts(subject);
}

TEST_F(FatFsSubjectTest, a_read_beyond_the_end_of_the_file_is_short)
{
    EXPECT_EQ(validation::BlockingSubject::shortTransfer, ReadMoreThanTheFileHolds(subject));
}

TEST_F(FatFsSubjectTest, listing_a_missing_directory_completes_with_the_result_of_fatfs)
{
    EXPECT_NE(0, ListAMissingDirectory(subject));
}

TEST_F(FatFsSubjectTest, reads_a_file_with_more_fragments_than_its_link_map_holds)
{
    FormatAndMount(subject);
    constexpr uint32_t files = 72;
    for (uint32_t file = 0; file != files; ++file)
    {
        infra::BoundedString::WithStorage<8> path;
        infra::StringOutputStream{ path } << "/a" << file;
        ASSERT_EQ(0, OpenAndWrite(subject, path, 512));
    }
    for (uint32_t file = 0; file != files; file += 2)
    {
        infra::BoundedString::WithStorage<8> path;
        infra::StringOutputStream{ path } << "/a" << file;
        ASSERT_EQ(0, Await([&](const auto& done)
                         {
                             subject.Remove(path, done);
                         }));
    }
    ASSERT_EQ(0, Await([&](const auto& done)
                     {
                         subject.Open("/frag", validation::CycleOpenMode::write, done);
                     }));
    std::array<uint8_t, validation::CycleRun::writeBytes> data{};
    for (uint32_t chunk = 0; chunk != files / 2; ++chunk)
        ASSERT_EQ(0, Await([&](const auto& done)
                         {
                             subject.Write(infra::MakeRange(data), done);
                         }));
    ASSERT_EQ(0, Await([&](const auto& done)
                     {
                         subject.Close(done);
                     }));

    ASSERT_EQ(0, Await([&](const auto& done)
                     {
                         subject.Open("/frag", validation::CycleOpenMode::read, done);
                     }));
    subject.Seek(100 * 1024);
    std::array<uint8_t, 512> buffer{};
    EXPECT_EQ(0, Await([&](const auto& done)
                     {
                         subject.Read(infra::MakeRange(buffer), done);
                     }));
}

TEST_F(LittleFsSubjectTest, runs_f2_on_the_ram_flash)
{
    EXPECT_EQ("littlefs/ram-matched", subject.Name());
    ExpectFullRun(subject, "ramflash");
}

TEST_F(LittleFsSubjectTest, a_reset_closes_the_open_file_and_unmounts)
{
    ExpectResetWithAnOpenFileUnmounts(subject);
}

TEST_F(LittleFsSubjectTest, a_read_beyond_the_end_of_the_file_is_short)
{
    EXPECT_EQ(validation::BlockingSubject::shortTransfer, ReadMoreThanTheFileHolds(subject));
}

TEST_F(LittleFsSubjectTest, listing_a_missing_directory_completes_with_the_error_of_littlefs)
{
    EXPECT_EQ(LFS_ERR_NOENT, ListAMissingDirectory(subject));
}

TEST_F(LittleFsSubjectTest, a_write_beyond_the_space_completes_with_the_error_of_littlefs)
{
    FormatAndMount(subject);
    ASSERT_EQ(0, Await([&](const auto& done)
                     {
                         subject.Open("/full", validation::CycleOpenMode::write, done);
                     }));

    std::array<uint8_t, validation::CycleRun::writeBytes> data{};
    int32_t status = 0;
    for (uint32_t chunk = 0; chunk != validation::cycles::ramFlashSectors && status == 0; ++chunk)
        status = Await([&](const auto& done)
            {
                subject.Write(infra::MakeRange(data), done);
            });

    EXPECT_EQ(LFS_ERR_NOSPC, status);
}
