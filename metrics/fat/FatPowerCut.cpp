#include "fs/fat/FatTable.hpp"
#include "fs/fat/test_doubles/FatImageBuilder.hpp"
#include "metrics/fat/FatChecker.hpp"
#include "metrics/fat/FatSweep.hpp"
#include "metrics/power_cut/PowerCutSweep.hpp"
#include "metrics/report/Report.hpp"
#include "metrics/suite/MetricRecorder.hpp"
#include "gtest/gtest.h"
#include <algorithm>
#include <iterator>
#include <optional>
#include <ostream>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    struct SweepWrite
    {
        std::optional<uint32_t> offset;
        uint32_t length;
    };

    struct SweepCase
    {
        std::string name;
        uint32_t originalSectors;
        int32_t originalExtraBytes;
        bool originalInClusters;
        fs::OpenMode mode;
        std::vector<SweepWrite> writes;
        bool closeAtEnd;
        bool atFatSectorEdge;
    };

    struct SweepMode
    {
        std::string name;
        metrics::BlockCut cut;
        bool mustHold;
    };

    using SweepParameter = std::tuple<const metrics::BlockDeviceModel*, SweepCase>;

    std::vector<uint8_t> Pattern(std::size_t size, uint8_t seed)
    {
        std::vector<uint8_t> pattern(size);

        for (std::size_t index = 0; index != size; ++index)
            pattern[index] = static_cast<uint8_t>(index * 29 + seed + index / 499);

        return pattern;
    }

    uint32_t ClusterBytesOf(const metrics::BlockDeviceModel& model)
    {
        auto config = metrics::SweepFormat(model);
        return config.sectorsPerCluster.value_or(16) * model.blockSize;
    }

    uint32_t OriginalSize(const SweepCase& sweepCase, uint32_t clusterBytes)
    {
        auto base = sweepCase.originalInClusters ? clusterBytes : 512u;
        return static_cast<uint32_t>(static_cast<int32_t>(sweepCase.originalSectors * base) + sweepCase.originalExtraBytes);
    }

    std::vector<uint8_t> Expected(const SweepCase& sweepCase, std::vector<uint8_t> contents, uint32_t clusterBytes)
    {
        uint8_t seed = 100;

        for (const auto& write : sweepCase.writes)
        {
            auto length = write.length == 0 ? clusterBytes + 100 : write.length;
            auto data = Pattern(length, seed++);
            auto offset = write.offset.value_or(static_cast<uint32_t>(contents.size()));
            contents.resize(std::max<std::size_t>(contents.size(), offset + data.size()));
            std::copy(data.begin(), data.end(), contents.begin() + offset);
        }

        return contents;
    }

    uint32_t EdgeCluster(const fs::FatGeometry& geometry)
    {
        for (uint32_t cluster = 3; cluster != geometry.clusterCount; ++cluster)
        {
            auto here = fs::LocateEntry(geometry.type, cluster, 512);
            auto next = fs::LocateEntry(geometry.type, cluster + 1, 512);

            if (here.sector != next.sector && !here.straddles && !next.straddles)
                return cluster;
        }

        return 0;
    }

    std::optional<std::string> Compare(const std::vector<uint8_t>& read, const std::vector<uint8_t>& before, const std::vector<uint8_t>& after)
    {
        if (read.size() != before.size() && read.size() != after.size())
            return "size " + std::to_string(read.size()) + " is neither " + std::to_string(before.size()) + " nor " + std::to_string(after.size());

        for (std::size_t offset = 0; offset < read.size(); offset += 512)
        {
            auto end = std::min<std::size_t>(offset + 512, read.size());
            auto matches = [&read, offset, end](const std::vector<uint8_t>& candidate)
            {
                return candidate.size() >= end && std::equal(read.begin() + offset, read.begin() + end, candidate.begin() + offset);
            };

            if (!matches(before) && !matches(after))
                return "sector " + std::to_string(offset / 512) + " holds neither its previous nor its new contents";
        }

        return std::nullopt;
    }

    std::string ParameterName(const testing::TestParamInfo<SweepParameter>& info)
    {
        auto name = std::string(std::get<0>(info.param)->name) + "_" + std::get<1>(info.param).name;
        std::replace(name.begin(), name.end(), '-', '_');
        return name;
    }

    const std::vector<SweepMode> modes{
        { "unwritten", { 0, metrics::InterruptedBlock::unwritten }, true },
        { "written", { 0, metrics::InterruptedBlock::written }, true },
        { "lost", { 0, metrics::InterruptedBlock::unwritten, true }, true },
        { "torn", { 0, metrics::InterruptedBlock::torn }, false },
    };
}

class FatPowerCut
    : public testing::TestWithParam<SweepParameter>
{
public:
    const metrics::BlockDeviceModel& model{ *std::get<0>(GetParam()) };
    const SweepCase& sweepCase{ std::get<1>(GetParam()) };
    uint32_t clusterBytes{ ClusterBytesOf(model) };
    std::vector<uint8_t> before{ Pattern(OriginalSize(sweepCase, clusterBytes), 1) };
    std::vector<uint8_t> after{ Expected(sweepCase, before, clusterBytes) };

    void SetUpVolume(metrics::SweepContext<hal::BlockDevice>& context)
    {
        ASSERT_EQ(fs::Result::success, metrics::Format(context, metrics::SweepFormat(model)));
        metrics::SweepImage image{ context };
        fs::FatImageBuilder builder{ image };

        if (sweepCase.atFatSectorEdge)
        {
            auto edge = EdgeCluster(builder.Geometry());
            auto used = edge - 2 - (builder.Geometry().type == fs::FatType::fat32 ? 1 : 0);
            fs::ShortEntry filler;
            filler.name.bytes = { 'F', 'I', 'L', 'L', 'E', 'R', ' ', ' ', 'B', 'I', 'N' };
            filler.attributes = fs::FatAttributes::archive;
            filler.firstCluster = builder.AllocateChain(used);
            filler.size = used * clusterBytes;
            builder.AddEntries(0, "", filler);
        }

        builder.AddFile(0, "", "FILE    BIN", infra::MakeRange(before));
    }

    void Operate(metrics::SweepContext<hal::BlockDevice>& context)
    {
        metrics::SweepVolume volume{ context };
        volume.Mount();
        auto file = volume.Open("/FILE.BIN", sweepCase.mode);
        if (file == nullptr)
            return;

        uint8_t seed = 100;
        for (const auto& write : sweepCase.writes)
        {
            auto data = Pattern(write.length == 0 ? clusterBytes + 100 : write.length, seed++);
            if (write.offset)
                file->Seek(*write.offset);
            volume.Write(*file, infra::MakeRange(data));
        }

        if (sweepCase.closeAtEnd)
            volume.Close(*file);
        else
            volume.Sync(*file);
    }

    std::optional<std::string> Verify(metrics::SweepContext<hal::BlockDevice>& context, metrics::FatCheckMode mode)
    {
        auto reader = metrics::ReaderOf(context);
        auto check = metrics::CheckFatImage(reader, mode);
        if (check.problem)
            return check.problem;

        const auto* entry = check.Find("/FILE.BIN");
        if (entry == nullptr)
            return "the file is missing";

        if (auto difference = Compare(metrics::ReadContents(reader, check, *entry), before, after))
            return difference;

        metrics::SweepVolume volume{ context };
        if (volume.Mount() != fs::Result::success)
            return "the volume does not mount";

        return std::nullopt;
    }

    metrics::SweepResult Sweep(const SweepMode& mode)
    {
        metrics::BlockPowerCutSweep sweep{ model, { mode.cut } };
        if (model.numberOfBlocks > metrics::fatSweep16.numberOfBlocks)
            sweep.LimitCutPoints(256);

        bool uncut = true;
        return sweep.Run([this](auto& context)
            {
                SetUpVolume(context);
            },
            [this](auto& context)
            {
                Operate(context);
            },
            [this, &uncut](auto& context)
            {
                auto mode = std::exchange(uncut, false) ? metrics::FatCheckMode::strict : metrics::FatCheckMode::afterCut;
                return Verify(context, mode);
            });
    }
};

TEST_P(FatPowerCut, every_cut_leaves_a_consistent_volume_with_previous_or_new_contents)
{
    if (sweepCase.atFatSectorEdge && model.numberOfBlocks / ClusterBytesOf(model) * model.blockSize * 3 / 2 < model.blockSize * 2)
        GTEST_SKIP() << "the FAT of " << model.name << " has no second sector";

    metrics::MetricRecorder recorder{ metrics::GlobalReport(), "embedded-filesystem/fat", model.name, sizeof(fs::FatFileSystem::WithStorage<1, 8>) };

    for (const auto& mode : modes)
    {
        auto result = Sweep(mode);
        recorder.Record("P-1", sweepCase.name + " " + mode.name, "ratio", metrics::Better::higher, result.ConsistentShare());

        if (mode.mustHold)
        {
            EXPECT_TRUE(result.failures.empty()) << mode.name << ": " << result.failures.size() << " failures, the first at step "
                                                 << (result.failures.empty() || !result.failures.front().step ? std::string("uncut") : std::to_string(*result.failures.front().step))
                                                 << ": " << (result.failures.empty() ? std::string() : result.failures.front().reason);
            EXPECT_NE(0u, result.writeSteps) << mode.name;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Fat, FatPowerCut,
    testing::Combine(testing::Values(&metrics::fatSweep12, &metrics::fatSweep16, &metrics::fatSweep32),
        testing::Values(SweepCase{ "append_and_sync", 1, 188, false, fs::OpenMode::append, { { std::nullopt, 100 } }, false, false },
            SweepCase{ "append_across_cluster", 1, 188, false, fs::OpenMode::append, { { std::nullopt, 0 } }, false, false },
            SweepCase{ "append_across_fat_sector", 1, -100, true, fs::OpenMode::append, { { std::nullopt, 200 } }, false, true },
            SweepCase{ "overwrite_three_sectors", 1, 4 * 512, true, fs::OpenMode::readWrite, { { 512, 3 * 512 } }, false, false },
            SweepCase{ "close_after_writes", 1, 188, false, fs::OpenMode::append, { { std::nullopt, 300 }, { std::nullopt, 300 }, { std::nullopt, 300 } }, true, false })),
    ParameterName);

namespace
{
    struct CreateCase
    {
        std::string name;
        bool inDirectory;
        int32_t fillersBeforeClusterEnd;
        uint32_t bytes;
        bool closeAtEnd;
    };

    using CreateParameter = std::tuple<const metrics::BlockDeviceModel*, CreateCase>;

    std::string CreateParameterName(const testing::TestParamInfo<CreateParameter>& info)
    {
        auto name = std::string(std::get<0>(info.param)->name) + "_" + std::get<1>(info.param).name;
        std::replace(name.begin(), name.end(), '-', '_');
        return name;
    }

    void AddFillers(fs::FatImageBuilder& builder, uint32_t directory, uint32_t count)
    {
        for (uint32_t index = 0; index != count; ++index)
        {
            char name[12] = "F0000000TXT";
            auto number = std::to_string(index);
            std::copy(number.rbegin(), number.rend(), std::reverse_iterator<char*>(name + 8));
            builder.AddFile(directory, "", name, infra::ConstByteRange());
        }
    }
}

class FatCreatePowerCut
    : public testing::TestWithParam<CreateParameter>
{
public:
    const metrics::BlockDeviceModel& model{ *std::get<0>(GetParam()) };
    const CreateCase& createCase{ std::get<1>(GetParam()) };
    uint32_t clusterBytes{ ClusterBytesOf(model) };
    std::string path{ std::string(createCase.inDirectory ? "/Directory" : "") + "/Created Long Name.txt" };
    std::vector<uint8_t> data{ Pattern(createCase.bytes, 7) };
    std::size_t originalEntries{ 0 };

    void SetUpVolume(metrics::SweepContext<hal::BlockDevice>& context)
    {
        ASSERT_EQ(fs::Result::success, metrics::Format(context, metrics::SweepFormat(model)));
        metrics::SweepImage image{ context };
        fs::FatImageBuilder builder{ image };
        builder.AddFile(0, "", "FIRST   TXT", infra::ConstByteRange());

        uint32_t parent = builder.RootCluster();
        if (createCase.inDirectory)
        {
            parent = builder.AddDirectory(parent, "Directory", "DIRECT~1   ");
            std::vector<uint8_t> stale(model.blockSize, 0x41);
            for (uint32_t sector = 0; sector != builder.Geometry().sectorsPerCluster; ++sector)
                image.WriteBlock(builder.Geometry().SectorOfCluster(parent + 1) + sector, infra::MakeRange(stale));
        }

        auto entriesPerCluster = clusterBytes / fs::directoryEntryBytes;
        auto reserved = createCase.inDirectory ? 2 : 1;
        AddFillers(builder, parent, static_cast<uint32_t>(createCase.fillersBeforeClusterEnd < 0 ? static_cast<int32_t>(entriesPerCluster) - reserved + createCase.fillersBeforeClusterEnd : createCase.fillersBeforeClusterEnd));

        originalEntries = metrics::CheckFatImage(metrics::ReaderOf(context), metrics::FatCheckMode::strict).entries.size();
    }

    void Operate(metrics::SweepContext<hal::BlockDevice>& context)
    {
        metrics::SweepVolume volume{ context };
        volume.Mount();
        auto file = volume.Open(path, fs::OpenMode::write);
        if (file == nullptr)
            return;

        if (!data.empty())
            volume.Write(*file, infra::MakeRange(data));

        if (createCase.closeAtEnd)
            volume.Close(*file);
        else
            volume.Sync(*file);
    }

    std::optional<std::string> Verify(metrics::SweepContext<hal::BlockDevice>& context, metrics::FatCheckMode mode)
    {
        auto reader = metrics::ReaderOf(context);
        auto check = metrics::CheckFatImage(reader, mode);
        if (check.problem)
            return check.problem;

        const auto* created = check.Find(path);
        auto expectedEntries = originalEntries + (created != nullptr ? 1 : 0);
        if (check.entries.size() != expectedEntries)
            return std::to_string(check.entries.size()) + " entries instead of " + std::to_string(expectedEntries);

        if (created == nullptr && mode == metrics::FatCheckMode::strict)
            return "the new file is missing";

        if (created != nullptr)
            if (auto difference = Compare(metrics::ReadContents(reader, check, *created), {}, data))
                return difference;

        metrics::SweepVolume volume{ context };
        if (volume.Mount() != fs::Result::success)
            return "the volume does not mount";

        return std::nullopt;
    }

    metrics::SweepResult Sweep(const SweepMode& mode)
    {
        metrics::BlockPowerCutSweep sweep{ model, { mode.cut } };
        if (model.numberOfBlocks > metrics::fatSweep16.numberOfBlocks)
            sweep.LimitCutPoints(256);

        bool uncut = true;
        return sweep.Run([this](auto& context)
            {
                SetUpVolume(context);
            },
            [this](auto& context)
            {
                Operate(context);
            },
            [this, &uncut](auto& context)
            {
                auto mode = std::exchange(uncut, false) ? metrics::FatCheckMode::strict : metrics::FatCheckMode::afterCut;
                return Verify(context, mode);
            });
    }
};

TEST_P(FatCreatePowerCut, every_cut_leaves_the_new_file_absent_or_complete)
{
    metrics::MetricRecorder recorder{ metrics::GlobalReport(), "embedded-filesystem/fat", model.name, sizeof(fs::FatFileSystem::WithStorage<1, 8>) };

    for (const auto& mode : modes)
    {
        auto result = Sweep(mode);
        recorder.Record("P-1", createCase.name + " " + mode.name, "ratio", metrics::Better::higher, result.ConsistentShare());

        if (mode.mustHold)
        {
            EXPECT_TRUE(result.failures.empty()) << mode.name << ": " << result.failures.size() << " failures, the first at step "
                                                 << (result.failures.empty() || !result.failures.front().step ? std::string("uncut") : std::to_string(*result.failures.front().step))
                                                 << ": " << (result.failures.empty() ? std::string() : result.failures.front().reason);
            EXPECT_NE(0u, result.writeSteps) << mode.name;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Fat, FatCreatePowerCut,
    testing::Combine(testing::Values(&metrics::fatSweep12, &metrics::fatSweep16, &metrics::fatSweep32),
        testing::Values(CreateCase{ "create_and_sync", false, 0, 0, false },
            CreateCase{ "create_spanning_sectors", false, 14, 0, false },
            CreateCase{ "create_with_directory_growth", true, -1, 0, false },
            CreateCase{ "create_write_and_close", false, 0, 1000, true })),
    CreateParameterName);
