#include "fs/fat/FatTable.hpp"
#include "fs/fat/test_doubles/FatImageBuilder.hpp"
#include "metrics/fat/FatChecker.hpp"
#include "metrics/fat/FatSweep.hpp"
#include "metrics/power_cut/PowerCutSweep.hpp"
#include "metrics/report/Report.hpp"
#include "metrics/suite/MetricRecorder.hpp"
#include "gtest/gtest.h"
#include <algorithm>
#include <functional>
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
        uint32_t expectedSize{ 0 };
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

    using Context = metrics::BlockPowerCutSweep::Context;
    using Verifier = std::function<std::optional<std::string>(Context& context, metrics::FatCheckMode mode)>;

    metrics::SweepResult SweepWith(const metrics::BlockDeviceModel& model, const SweepMode& mode, const metrics::BlockPowerCutSweep::Phase& setUp, const metrics::BlockPowerCutSweep::Phase& operate, const Verifier& verify)
    {
        metrics::BlockPowerCutSweep sweep{ model, { mode.cut } };
        if (model.numberOfBlocks > metrics::fatSweep16.numberOfBlocks)
            sweep.LimitCutPoints(256);

        bool uncut = true;
        return sweep.Run(setUp, operate, [&verify, &uncut](Context& context)
            {
                return verify(context, std::exchange(uncut, false) ? metrics::FatCheckMode::strict : metrics::FatCheckMode::afterCut);
            });
    }

    void SweepEveryMode(const metrics::BlockDeviceModel& model, const std::string& caseName, const std::function<metrics::SweepResult(const SweepMode& mode)>& sweep)
    {
        metrics::MetricRecorder recorder{ metrics::GlobalReport(), "embedded-filesystem/fat", model.name, sizeof(fs::FatFileSystem::WithStorage<1, 8>) };

        for (const auto& mode : modes)
        {
            auto result = sweep(mode);
            recorder.Record("P-1", caseName + " " + mode.name, "ratio", metrics::Better::higher, result.ConsistentShare());

            if (mode.mustHold)
            {
                EXPECT_TRUE(result.failures.empty()) << mode.name << ": " << result.failures.size() << " failures, the first at step "
                                                     << (result.failures.empty() || !result.failures.front().step ? std::string("uncut") : std::to_string(*result.failures.front().step))
                                                     << ": " << (result.failures.empty() ? std::string() : result.failures.front().reason);
                EXPECT_NE(0u, result.writeSteps) << mode.name;
            }
        }
    }
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

        if (sweepCase.expectedSize != 0)
            volume.ExpectSize(*file, sweepCase.expectedSize);

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
        return SweepWith(
            model, mode, [this](Context& context)
            {
                SetUpVolume(context);
            },
            [this](Context& context)
            {
                Operate(context);
            },
            [this](Context& context, metrics::FatCheckMode mode)
            {
                return Verify(context, mode);
            });
    }
};

TEST_P(FatPowerCut, every_cut_leaves_a_consistent_volume_with_previous_or_new_contents)
{
    if (sweepCase.atFatSectorEdge && model.numberOfBlocks / ClusterBytesOf(model) * model.blockSize * 3 / 2 < model.blockSize * 2)
        GTEST_SKIP() << "the FAT of " << model.name << " has no second sector";

    SweepEveryMode(model, sweepCase.name, [this](const SweepMode& mode)
        {
            return Sweep(mode);
        });
}

INSTANTIATE_TEST_SUITE_P(Fat, FatPowerCut,
    testing::Combine(testing::Values(&metrics::fatSweep12, &metrics::fatSweep16, &metrics::fatSweep32),
        testing::Values(SweepCase{ "append_and_sync", 1, 188, false, fs::OpenMode::append, { { std::nullopt, 100 } }, false, false },
            SweepCase{ "append_across_cluster", 1, 188, false, fs::OpenMode::append, { { std::nullopt, 0 } }, false, false },
            SweepCase{ "append_across_fat_sector", 1, -100, true, fs::OpenMode::append, { { std::nullopt, 200 } }, false, true },
            SweepCase{ "overwrite_three_sectors", 1, 4 * 512, true, fs::OpenMode::readWrite, { { 512, 3 * 512 } }, false, false },
            SweepCase{ "close_after_writes", 1, 188, false, fs::OpenMode::append, { { std::nullopt, 300 }, { std::nullopt, 300 }, { std::nullopt, 300 } }, true, false },
            SweepCase{ "expect_size_write_close", 1, 188, false, fs::OpenMode::append, { { std::nullopt, 2000 }, { std::nullopt, 2000 } }, true, false, 8000 })),
    ParameterName);

namespace
{
    enum class EntryOperation : uint8_t
    {
        create,
        remove,
        makeDirectory
    };

    struct EntryCase
    {
        std::string name;
        EntryOperation operation;
        bool inDirectory;
        int32_t fillersBeforeClusterEnd;
        uint32_t bytes;
        bool closeAtEnd;
        bool fragmented{ false };
        bool directory{ false };
    };

    using EntryParameter = std::tuple<const metrics::BlockDeviceModel*, EntryCase>;

    std::string EntryParameterName(const testing::TestParamInfo<EntryParameter>& info)
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

    void FillWithStaleData(const fs::FatImageBuilder& builder, metrics::SweepImage& image, uint32_t cluster, uint32_t blockSize)
    {
        std::vector<uint8_t> stale(blockSize, 0x41);
        for (uint32_t sector = 0; sector != builder.Geometry().sectorsPerCluster; ++sector)
            image.WriteBlock(builder.Geometry().SectorOfCluster(cluster) + sector, infra::MakeRange(stale));
    }
}

class FatEntryPowerCut
    : public testing::TestWithParam<EntryParameter>
{
public:
    const metrics::BlockDeviceModel& model{ *std::get<0>(GetParam()) };
    const EntryCase& entryCase{ std::get<1>(GetParam()) };
    uint32_t clusterBytes{ ClusterBytesOf(model) };
    std::string parent{ entryCase.inDirectory ? "/Directory" : "" };
    std::string path{ parent + (entryCase.operation == EntryOperation::makeDirectory ? "/New Directory" : "/Created Long Name.txt") };
    std::string inside{ path + "/Inside.txt" };
    std::vector<uint8_t> data{ Pattern(entryCase.bytes, 7) };
    std::size_t originalEntries{ 0 };

    void SetUpVolume(Context& context)
    {
        ASSERT_EQ(fs::Result::success, metrics::Format(context, metrics::SweepFormat(model)));
        metrics::SweepImage image{ context };
        fs::FatImageBuilder builder{ image };
        builder.AddFile(0, "", "FIRST   TXT", infra::ConstByteRange());

        uint32_t directory = builder.RootCluster();
        if (entryCase.inDirectory)
        {
            directory = builder.AddDirectory(directory, "Directory", "DIRECT~1   ");
            FillWithStaleData(builder, image, directory + 1, model.blockSize);
        }

        auto entriesPerCluster = clusterBytes / fs::directoryEntryBytes;
        auto reserved = entryCase.inDirectory ? 2 : 1;
        AddFillers(builder, directory, static_cast<uint32_t>(entryCase.fillersBeforeClusterEnd < 0 ? static_cast<int32_t>(entriesPerCluster) - reserved + entryCase.fillersBeforeClusterEnd : entryCase.fillersBeforeClusterEnd));

        if (entryCase.operation == EntryOperation::remove && entryCase.directory)
            builder.AddDirectory(directory, "Created Long Name.txt", "CREATE~1TXT");
        else if (entryCase.operation == EntryOperation::remove)
            builder.AddFile(directory, "Created Long Name.txt", "CREATE~1TXT", infra::MakeRange(data), entryCase.fragmented);
        else if (entryCase.operation == EntryOperation::makeDirectory)
            FillWithStaleData(builder, image, builder.Geometry().type == fs::FatType::fat32 ? builder.RootCluster() + 1 : 2, model.blockSize);

        originalEntries = metrics::CheckFatImage(metrics::ReaderOf(context), metrics::FatCheckMode::strict).entries.size();
    }

    void Operate(Context& context)
    {
        metrics::SweepVolume volume{ context };
        volume.Mount();

        if (entryCase.operation == EntryOperation::remove)
            volume.Remove(path);
        else if (entryCase.operation == EntryOperation::makeDirectory)
        {
            if (volume.MakeDirectory(path) == fs::Result::success)
                CreateFile(volume, inside);
        }
        else
            CreateFile(volume, path);
    }

    void CreateFile(metrics::SweepVolume& volume, const std::string& name)
    {
        auto file = volume.Open(name, fs::OpenMode::write);
        if (file == nullptr)
            return;

        if (!data.empty())
            volume.Write(*file, infra::MakeRange(data));

        if (entryCase.closeAtEnd)
            volume.Close(*file);
        else
            volume.Sync(*file);
    }

    std::optional<std::string> Verify(Context& context, metrics::FatCheckMode mode)
    {
        auto reader = metrics::ReaderOf(context);
        auto check = metrics::CheckFatImage(reader, mode);
        if (check.problem)
            return check.problem;

        auto problem = entryCase.operation == EntryOperation::remove ? VerifyRemoval(reader, check, mode) : VerifyCreation(reader, check, mode);
        if (problem)
            return problem;

        metrics::SweepVolume volume{ context };
        if (volume.Mount() != fs::Result::success)
            return "the volume does not mount";

        return std::nullopt;
    }

    std::optional<std::string> VerifyCreation(const metrics::SectorReader& reader, const metrics::FatCheck& check, metrics::FatCheckMode mode) const
    {
        const auto* created = check.Find(path);
        const auto* file = entryCase.operation == EntryOperation::makeDirectory ? check.Find(inside) : created;
        auto expectedEntries = originalEntries + (created != nullptr ? 1 : 0) + (file != nullptr && file != created ? 1 : 0);

        if (check.entries.size() != expectedEntries)
            return std::to_string(check.entries.size()) + " entries instead of " + std::to_string(expectedEntries);

        if (mode == metrics::FatCheckMode::strict && file == nullptr)
            return "the new file is missing";

        if (file != nullptr)
            return Compare(metrics::ReadContents(reader, check, *file), {}, data);

        return std::nullopt;
    }

    std::optional<std::string> VerifyRemoval(const metrics::SectorReader& reader, const metrics::FatCheck& check, metrics::FatCheckMode mode) const
    {
        const auto* removed = check.Find(path);
        auto expectedEntries = originalEntries - (removed != nullptr ? 0 : 1);

        if (check.entries.size() != expectedEntries)
            return std::to_string(check.entries.size()) + " entries instead of " + std::to_string(expectedEntries);

        if (mode == metrics::FatCheckMode::strict && removed != nullptr)
            return "the removed entry is still there";

        if (removed != nullptr && !entryCase.directory && metrics::ReadContents(reader, check, *removed) != data)
            return "the file that was being removed lost its contents";

        return std::nullopt;
    }

    metrics::SweepResult Sweep(const SweepMode& mode)
    {
        return SweepWith(
            model, mode, [this](Context& context)
            {
                SetUpVolume(context);
            },
            [this](Context& context)
            {
                Operate(context);
            },
            [this](Context& context, metrics::FatCheckMode mode)
            {
                return Verify(context, mode);
            });
    }
};

TEST_P(FatEntryPowerCut, every_cut_leaves_the_entry_in_its_previous_or_its_new_state)
{
    SweepEveryMode(model, entryCase.name, [this](const SweepMode& mode)
        {
            return Sweep(mode);
        });
}

INSTANTIATE_TEST_SUITE_P(Fat, FatEntryPowerCut,
    testing::Combine(testing::Values(&metrics::fatSweep12, &metrics::fatSweep16, &metrics::fatSweep32),
        testing::Values(EntryCase{ "create_and_sync", EntryOperation::create, false, 0, 0, false },
            EntryCase{ "create_spanning_sectors", EntryOperation::create, false, 14, 0, false },
            EntryCase{ "create_with_directory_growth", EntryOperation::create, true, -1, 0, false },
            EntryCase{ "create_write_and_close", EntryOperation::create, false, 0, 1000, true },
            EntryCase{ "remove_file", EntryOperation::remove, false, 0, 3000, false },
            EntryCase{ "remove_fragmented_file", EntryOperation::remove, false, 0, 3000, false, true },
            EntryCase{ "remove_spanning_sectors", EntryOperation::remove, false, 13, 600, false },
            EntryCase{ "remove_directory", EntryOperation::remove, true, 0, 0, false, false, true },
            EntryCase{ "make_directory_then_create_and_sync", EntryOperation::makeDirectory, false, 0, 600, false })),
    EntryParameterName);
