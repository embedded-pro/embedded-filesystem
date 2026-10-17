#include "fs/fat/lite/test/FatLiteFixture.hpp"
#include <algorithm>

namespace
{
    class FatLiteFaultEnvironment
        : public FatLiteFixture
    {
    public:
        explicit FatLiteFaultEnvironment(const FatLiteVolumeCase& volumeCase)
            : FatLiteFixture(volumeCase)
        {
            auto logs = builder->AddDirectory(0, "", "LOGS       ");

            for (uint32_t index = 0; index != builder->Geometry().sectorsPerCluster * 16 - 2; ++index)
            {
                char name[12] = "F00     TXT";
                name[1] = static_cast<char>('0' + index / 10);
                name[2] = static_cast<char>('0' + index % 10);
                builder->AddFile(logs, "", name, infra::ConstByteRange());
            }
        }

        infra::Function<void(fs::Result)> Record()
        {
            auto* slot = &results[steps++];
            return [slot](fs::Result result)
            {
                *slot = result;
            };
        }

        infra::SharedPtr<fs::File> RecordOpen(infra::BoundedConstString path)
        {
            infra::SharedPtr<fs::File> opened;
            auto* slot = &results[steps++];
            volume.Open(path, fs::OpenMode::write, [slot, &opened](fs::Result result, infra::SharedPtr<fs::File> file)
                {
                    *slot = result;
                    opened = file;
                });
            ExecuteAllActions();
            return opened;
        }

        void Scenario()
        {
            auto contents = Pattern<1500>();
            volume.Mount(Record());
            ExecuteAllActions();

            if (auto file = RecordOpen("/a.txt"))
            {
                file->Write(infra::MakeRange(contents), Record());
                ExecuteAllActions();
                file->Sync(Record());
                ExecuteAllActions();
                file->Seek(1100);
                file->Truncate(Record());
                ExecuteAllActions();
                file->Seek(700);
                file->Truncate(Record());
                ExecuteAllActions();
                file->Write(infra::Head(infra::MakeRange(contents), 100), Record());
                ExecuteAllActions();
                file->Close(Record());
                ExecuteAllActions();
            }

            auto again = RecordOpen("/a.txt");
            if (again)
            {
                again->Write(infra::Head(infra::MakeRange(contents), 600), Record());
                ExecuteAllActions();
            }

            if (auto grown = RecordOpen("/logs/new.txt"))
            {
                grown->Close(Record());
                ExecuteAllActions();
            }

            volume.Unmount(Record());
            ExecuteAllActions();
        }

        void GrowAcrossFatSectors()
        {
            for (uint32_t cluster = 3; cluster != 255; ++cluster)
                builder->SetFatEntry(cluster, fs::EndOfChain(builder->Geometry().type));

            auto contents = Pattern<1024>();
            volume.Mount(Record());
            ExecuteAllActions();

            if (auto file = RecordOpen("/b.bin"))
            {
                file->Write(infra::MakeRange(contents), Record());
                ExecuteAllActions();
                file->Write(infra::MakeRange(contents), Record());
                ExecuteAllActions();
                file->Close(Record());
                ExecuteAllActions();
            }

            volume.Unmount(Record());
            ExecuteAllActions();
        }

        bool AllCompleted() const
        {
            return std::all_of(results.begin(), results.begin() + steps, [](const std::optional<fs::Result>& result)
                {
                    return result.has_value();
                });
        }

        bool AnyFailed() const
        {
            return std::any_of(results.begin(), results.begin() + steps, [](const std::optional<fs::Result>& result)
                {
                    return result != fs::Result::success;
                });
        }

        bool MountsAgain()
        {
            fs::FatLite::WithStorage<1> remount{ device };
            std::optional<fs::Result> mounted;
            remount.Mount([&mounted](fs::Result result)
                {
                    mounted = result;
                });
            ExecuteAllActions();
            return mounted == fs::Result::success;
        }

        std::array<std::optional<fs::Result>, 16> results{};
        std::size_t steps{ 0 };
    };

    class FatLiteFaultTest
        : public testing::TestWithParam<FatLiteVolumeCase>
    {
    public:
        template<class Scenario>
        void ExpectEveryFailureReported(const Scenario& scenario)
        {
            uint32_t requests = 0;
            {
                FatLiteFaultEnvironment environment{ GetParam() };
                environment.device.Observe([&requests](fs::SparseBlockDevice::Request, uint32_t)
                    {
                        ++requests;
                    });
                scenario(environment);
                ASSERT_TRUE(environment.AllCompleted());
                ASSERT_FALSE(environment.AnyFailed());
            }

            for (uint32_t failing = 0; failing != requests; ++failing)
            {
                FatLiteFaultEnvironment environment{ GetParam() };
                environment.device.FailNextOperationWith(hal::BlockDevice::Result::failed, failing);
                scenario(environment);

                EXPECT_TRUE(environment.AllCompleted()) << failing;
                EXPECT_TRUE(environment.AnyFailed()) << failing;
                EXPECT_TRUE(environment.MountsAgain()) << failing;
            }
        }
    };
}

INSTANTIATE_TEST_SUITE_P(EveryType, FatLiteFaultTest, testing::Values(fat12Volume, fat16Volume, fat32Volume));

TEST_P(FatLiteFaultTest, every_failing_device_request_is_reported_and_the_volume_mounts_again)
{
    ExpectEveryFailureReported([](FatLiteFaultEnvironment& environment)
        {
            environment.Scenario();
        });
}

TEST_P(FatLiteFaultTest, every_failing_device_request_while_a_file_grows_into_the_next_fat_sector_is_reported)
{
    ExpectEveryFailureReported([](FatLiteFaultEnvironment& environment)
        {
            environment.GrowAcrossFatSectors();
        });
}
