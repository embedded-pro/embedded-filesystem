#include "fs/fat/FatAllocationUnits.hpp"
#include "fs/fat/test/FatFileSystemFixture.hpp"
#include <array>

namespace
{
    class FatAppendPlacementTest
        : public FatFileSystemFixture
    {
    public:
        explicit FatAppendPlacementTest(uint32_t blocksPerUnit = 64)
            : FatFileSystemFixture(16384, ClusterSize(1), Config(blocksPerUnit))
        {}

        static fs::FatConfig Config(uint32_t blocksPerUnit)
        {
            fs::FatConfig config;
            config.blocksPerAllocationUnit = blocksPerUnit;
            return config;
        }

        void SetUp() override
        {
            std::array<uint8_t, 1500> contents{};
            builder->AddFile(0, "", "BUSY    BIN", infra::MakeRange(contents));
            builder->AddFile(0, "", "EMPTY   LOG", infra::ConstByteRange());
            ExpectMount();
        }

        infra::Function<void(fs::Result)> Done()
        {
            return [this](fs::Result result)
            {
                done.callback(result);
            };
        }

        void CountFreeClusters()
        {
            volume.Usage([this](fs::Result result, fs::VolumeUsage)
                {
                    done.callback(result);
                });
            ExpectDone(fs::Result::success);
        }

        infra::SharedPtr<fs::File> Open(infra::BoundedConstString path, fs::OpenMode mode)
        {
            auto file = ExpectOpen(path, mode);
            EXPECT_NE(nullptr, file);
            return file;
        }

        void WriteAndClose(fs::File& file)
        {
            std::array<uint8_t, 600> data{};
            file.Write(infra::MakeRange(data), Done());
            ExpectDone(fs::Result::success);
            file.Close(Done());
            ExpectDone(fs::Result::success);
        }

        void Write(infra::BoundedConstString path, fs::OpenMode mode)
        {
            WriteAndClose(*Open(path, mode));
        }

        uint32_t UnitAfter(uint32_t cluster) const
        {
            return fs::AllocationUnitsOf(builder->Geometry(), 64)->NextBoundaryAfter(cluster);
        }

        bool Allocated(uint32_t cluster) const
        {
            return builder->FatEntry(cluster) != 0;
        }

        const uint32_t firstFree{ 5 };
    };

    class FatAppendPlacementWithoutUnitsTest
        : public FatAppendPlacementTest
    {
    public:
        FatAppendPlacementWithoutUnitsTest()
            : FatAppendPlacementTest(0)
        {}
    };
}

TEST_F(FatAppendPlacementTest, file_created_for_appending_starts_at_an_allocation_unit)
{
    ASSERT_NE(firstFree, UnitAfter(firstFree - 1));
    CountFreeClusters();

    Write("/new.log", fs::OpenMode::append);

    EXPECT_TRUE(Allocated(UnitAfter(firstFree)));
    EXPECT_TRUE(Allocated(UnitAfter(firstFree) + 1));
    EXPECT_FALSE(Allocated(firstFree));
}

TEST_F(FatAppendPlacementTest, empty_file_opened_for_appending_starts_at_an_allocation_unit)
{
    CountFreeClusters();

    Write("/empty.log", fs::OpenMode::append);

    EXPECT_TRUE(Allocated(UnitAfter(firstFree)));
    EXPECT_FALSE(Allocated(firstFree));
}

TEST_F(FatAppendPlacementTest, each_file_created_for_appending_starts_at_its_own_unit)
{
    CountFreeClusters();

    Write("/first.log", fs::OpenMode::append);
    Write("/second.log", fs::OpenMode::append);

    EXPECT_TRUE(Allocated(UnitAfter(UnitAfter(firstFree))));
    EXPECT_FALSE(Allocated(UnitAfter(firstFree) + 2));
}

TEST_F(FatAppendPlacementTest, file_created_for_writing_starts_at_the_cursor)
{
    CountFreeClusters();

    Write("/new.log", fs::OpenMode::write);

    EXPECT_TRUE(Allocated(firstFree));
    EXPECT_FALSE(Allocated(UnitAfter(firstFree)));
}

TEST_F(FatAppendPlacementTest, file_for_appending_starts_at_the_cursor_before_free_runs_are_known)
{
    Write("/new.log", fs::OpenMode::append);

    EXPECT_TRUE(Allocated(firstFree));
    EXPECT_FALSE(Allocated(UnitAfter(firstFree)));
}

TEST_F(FatAppendPlacementTest, reservation_of_a_file_for_appending_comes_first)
{
    CountFreeClusters();
    auto file = Open("/new.log", fs::OpenMode::append);
    file->ExpectSize(600, Done());
    ExpectDone(fs::Result::success);

    WriteAndClose(*file);

    EXPECT_TRUE(Allocated(firstFree));
    EXPECT_FALSE(Allocated(UnitAfter(firstFree)));
}

TEST_F(FatAppendPlacementWithoutUnitsTest, file_for_appending_starts_at_the_cursor_when_the_allocation_unit_is_unknown)
{
    CountFreeClusters();

    Write("/new.log", fs::OpenMode::append);

    EXPECT_TRUE(Allocated(firstFree));
}
