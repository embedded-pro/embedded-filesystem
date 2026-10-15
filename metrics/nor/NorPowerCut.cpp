#include "fs/nor/NorBlockHeader.hpp"
#include "fs/nor/NorFormatter.hpp"
#include "fs/nor/NorLayout.hpp"
#include "metrics/nor/NorChecker.hpp"
#include "metrics/nor/NorSweep.hpp"
#include "metrics/nor/NorTargets.hpp"
#include "metrics/power_cut/PowerCutSweep.hpp"
#include "metrics/report/Report.hpp"
#include "metrics/suite/MetricRecorder.hpp"
#include "gtest/gtest.h"
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct SweepMode
    {
        std::string name;
        metrics::NorCutMode mode;
    };

    const std::vector<SweepMode> modes{
        { "notStarted", metrics::NorCutMode::notStarted },
        { "partial", metrics::NorCutMode::partial },
        { "completed", metrics::NorCutMode::completed },
    };

    using Context = metrics::NorPowerCutSweep::Context;
    using Verifier = std::function<std::optional<std::string>(Context& context, bool uncut)>;

    std::string Describe(const metrics::SweepResult& result)
    {
        if (result.failures.empty())
            return {};

        const auto& first = result.failures.front();
        return std::to_string(result.failures.size()) + " failures, the first at step " + (first.step.has_value() ? std::to_string(*first.step) : std::string("uncut")) + ": " + first.reason;
    }

    void SweepEveryMode(const metrics::NorModel& model, const std::string& caseName, std::size_t ramBytes, const metrics::NorPowerCutSweep::Phase& setUp, const metrics::NorPowerCutSweep::Phase& operate, const Verifier& verify)
    {
        metrics::NorTargets targets{ model, metrics::SweepConfig(model), 1 };
        metrics::MetricRecorder recorder{ metrics::GlobalReport(), "embedded-filesystem/nor", model.name, ramBytes, &targets };

        for (const auto& mode : modes)
        {
            metrics::NorPowerCutSweep sweep{ model, { mode.mode } };
            bool uncut = true;
            auto result = sweep.Run(setUp, operate, [&verify, &uncut](Context& context)
                {
                    return verify(context, std::exchange(uncut, false));
                });

            recorder.Record("P-1", caseName + " " + mode.name, "ratio", metrics::Better::higher, result.ConsistentShare());
            EXPECT_TRUE(result.failures.empty()) << mode.name << ": " << Describe(result);
            EXPECT_NE(0u, result.writeSteps) << mode.name;
        }
    }

    std::string ParameterName(const testing::TestParamInfo<const metrics::NorModel*>& info)
    {
        auto name = std::string(info.param->name);
        std::ranges::replace(name, '-', '_');
        return name;
    }
}

class NorFormatPowerCut
    : public testing::TestWithParam<const metrics::NorModel*>
{
public:
    const metrics::NorModel& model{ *GetParam() };
    fs::NorConfig config{ metrics::SweepConfig(model) };
    fs::NorConfig previous{ PreviousConfig(config) };
    uint32_t blockSize{ model.sectorSize * config.sectorsPerBlock };
    std::vector<std::optional<uint32_t>> previousCounts;

    static fs::NorConfig PreviousConfig(fs::NorConfig config)
    {
        config.inlineBytes = 96;
        config.maxNameBytes = 32;
        return config;
    }

    void SetUpVolume(Context& context)
    {
        ASSERT_EQ(fs::Result::success, metrics::Format(context, previous));
        ASSERT_EQ(fs::Result::success, metrics::Format(context, previous));

        for (uint32_t sector = 4 * config.sectorsPerBlock; sector != 5 * config.sectorsPerBlock; ++sector)
            metrics::EraseSector(context, sector);
        std::vector<uint8_t> partA(fs::RoundUpTo(fs::norHeaderPartABytes, config.programUnit));
        fs::SerializeNorHeaderA(100, infra::MakeRange(partA));
        metrics::Program(context, 4 * blockSize, partA);

        metrics::EraseSector(context, 5 * config.sectorsPerBlock);

        std::vector<uint8_t> partB(fs::RoundUpTo(fs::norHeaderPartBBytes, config.programUnit));
        fs::SerializeNorHeaderB(fs::NorAllocation{ fs::NorBlockType::data, 7, { 9, 0 } }, infra::MakeRange(partB));
        metrics::Program(context, 6 * blockSize + fs::NorHeaderPartBOffset(config.programUnit), partB);
        metrics::Program(context, 6 * blockSize + fs::NorHeaderSize(config.programUnit), std::vector<uint8_t>(100, 0x5a));

        auto check = metrics::CheckImage(context, model);
        ASSERT_EQ(std::nullopt, check.problem);
        previousCounts = check.eraseCounts;
    }

    std::optional<std::string> Verify(Context& context, bool uncut) const
    {
        auto check = metrics::CheckImage(context, model);
        if (!check.superblock.has_value())
            return uncut ? std::optional<std::string>("the formatted device has no valid superblock") : ExpectMount(context, config, fs::Result::corrupted);

        if (check.problem.has_value())
            return check.problem;

        if (*check.superblock == metrics::SuperblockOf(model, previous))
            return uncut ? std::optional<std::string>("the device still holds the previous volume") : ExpectMount(context, previous, fs::Result::success);

        if (*check.superblock != metrics::SuperblockOf(model, config))
            return "the device holds a volume of other parameters";

        if (uncut && check.validSuperblocks != 2)
            return "the formatted volume lacks a superblock";

        if (auto problem = VerifyNewVolume(check))
            return problem;

        return ExpectMount(context, config, fs::Result::success);
    }

    static std::optional<std::string> ExpectMount(Context& context, const fs::NorConfig& mountConfig, fs::Result expected)
    {
        metrics::NorSweepVolume volume{ context, mountConfig };
        auto result = volume.Mount();
        if (result != expected)
            return "the mount completes with result " + std::to_string(static_cast<int>(result)) + " instead of " + std::to_string(static_cast<int>(expected));

        return std::nullopt;
    }

    std::optional<std::string> VerifyNewVolume(const metrics::NorCheck& check) const
    {
        if (!check.entries.empty())
            return "the new volume is not empty";

        for (uint32_t block = 0; block != check.blocks.size(); ++block)
        {
            if (check.blocks[block] != ExpectedUse(block))
                return "block " + std::to_string(block) + " is not where Format puts it";

            if (block >= 2 && check.eraseCounts[block] != previousCounts[block].value_or(AverageCount()) + 1)
                return "block " + std::to_string(block) + " has the erase count " + std::to_string(check.eraseCounts[block].value_or(0));
        }

        return std::nullopt;
    }

    static metrics::NorBlockUse ExpectedUse(uint32_t block)
    {
        if (block < 2)
            return metrics::NorBlockUse::superblock;

        if (block == 2)
            return metrics::NorBlockUse::node;

        return block == 3 ? metrics::NorBlockUse::checkpoint : metrics::NorBlockUse::freeErased;
    }

    uint32_t AverageCount() const
    {
        uint64_t sum = 0;
        uint32_t known = 0;
        for (const auto& count : previousCounts)
            if (count.has_value())
            {
                sum += *count;
                ++known;
            }

        return known != 0 ? static_cast<uint32_t>(sum / known) : 0;
    }
};

TEST_P(NorFormatPowerCut, every_cut_leaves_the_previous_volume_no_volume_or_the_new_empty_volume)
{
    SweepEveryMode(
        model, "format", metrics::SweepRamBytes(model), [this](Context& context)
        {
            SetUpVolume(context);
        },
        [this](Context& context)
        {
            metrics::Format(context, config);
        },
        [this](Context& context, bool uncut)
        {
            return Verify(context, uncut);
        });
}

INSTANTIATE_TEST_SUITE_P(Nor, NorFormatPowerCut, testing::Values(&metrics::norSweep128k, &metrics::internalSweep96k, &metrics::norSweep128kTwoSectors), ParameterName);

class NorMakeDirectoryPowerCut
    : public testing::TestWithParam<const metrics::NorModel*>
{
public:
    const metrics::NorModel& model{ *GetParam() };
    fs::NorConfig config{ metrics::SweepConfig(model) };
    uint32_t blockSize{ model.sectorSize * config.sectorsPerBlock };
    uint32_t blocks{ model.numberOfSectors / config.sectorsPerBlock };
    std::vector<std::string> paths{ Paths() };

    static constexpr std::size_t flatDirectories = 60;

    static std::vector<std::string> Paths()
    {
        std::vector<std::string> made;
        for (std::size_t index = 0; index != flatDirectories; ++index)
            made.push_back("/a" + std::to_string(index));

        std::string nested;
        for (std::size_t index = 0; index != 12; ++index)
        {
            nested += "/n" + std::to_string(index);
            made.push_back(nested);
        }

        return made;
    }

    void SetUpVolume(Context& context) const
    {
        ASSERT_EQ(fs::Result::success, metrics::Format(context, config));

        std::vector<uint8_t> partB(fs::RoundUpTo(fs::norHeaderPartBBytes, config.programUnit));
        for (uint32_t block = 6; block != blocks; ++block)
        {
            fs::SerializeNorHeaderB(fs::NorAllocation{ fs::NorBlockType::data, 100 + block, { 99, 0 } }, infra::MakeRange(partB));
            metrics::Program(context, block * blockSize + fs::NorHeaderPartBOffset(config.programUnit), partB);
        }

        ASSERT_EQ(std::nullopt, metrics::CheckImage(context, model).problem);
    }

    void Operate(Context& context) const
    {
        metrics::NorSweepVolume volume{ context, config };
        if (volume.Mount() != fs::Result::success)
            return;

        for (std::size_t index = 0; index != paths.size(); ++index)
        {
            if (index == flatDirectories && (volume.Unmount() != fs::Result::success || volume.Mount() != fs::Result::success))
                return;

            if (volume.MakeDirectory(paths[index]) != fs::Result::success)
                return;
        }

        volume.Unmount();
    }

    std::optional<std::string> Verify(Context& context, bool uncut) const
    {
        auto check = metrics::CheckImage(context, model);
        if (check.problem.has_value())
            return check.problem;

        auto count = check.entries.size();
        if (count > paths.size())
            return "the volume holds more directories than were made";

        std::set<std::string> made(paths.begin(), paths.begin() + static_cast<std::ptrdiff_t>(count));
        for (const auto& entry : check.entries)
            if (!entry.directory || !made.contains(entry.path))
                return "the volume holds " + entry.path + ", which is not one of the first " + std::to_string(count) + " directories made";

        if (uncut && count != paths.size())
            return "the uncut run made " + std::to_string(count) + " directories";

        return VerifyMount(context, count);
    }

    std::optional<std::string> VerifyMount(Context& context, std::size_t count) const
    {
        metrics::NorSweepVolume volume{ context, config };
        if (volume.Mount() != fs::Result::success)
            return "the volume does not mount";

        for (std::size_t index = 0; index != count; ++index)
            if (volume.TypeOf(paths[index]) != fs::EntryType::directory)
                return paths[index] + " is not a directory after the mount";

        if (count != paths.size() && volume.TypeOf(paths[count]).has_value())
            return paths[count] + " exists after the mount, but the checker does not find it";

        if (volume.MakeDirectory("/after") != fs::Result::success || volume.Unmount() != fs::Result::success)
            return "a directory cannot be made after the mount";

        auto check = metrics::CheckImage(context, model);
        if (check.problem.has_value())
            return "after a directory made after the mount: " + *check.problem;

        if (check.Find("/after") == nullptr)
            return "the directory made after the mount is lost";

        return std::nullopt;
    }
};

TEST_P(NorMakeDirectoryPowerCut, every_cut_keeps_the_directories_made_before_it_and_the_volume_takes_new_ones)
{
    SweepEveryMode(
        model, "make directory", metrics::SweepRamBytes(model), [this](Context& context)
        {
            SetUpVolume(context);
        },
        [this](Context& context)
        {
            Operate(context);
        },
        [this](Context& context, bool uncut)
        {
            return Verify(context, uncut);
        });
}

INSTANTIATE_TEST_SUITE_P(Nor, NorMakeDirectoryPowerCut, testing::Values(&metrics::norSweep128k, &metrics::internalSweep96k, &metrics::norSweep128kTwoSectors), ParameterName);

namespace
{
    std::vector<uint8_t> FileBytes(std::size_t from, std::size_t count)
    {
        std::vector<uint8_t> bytes(count);
        for (std::size_t index = 0; index != count; ++index)
        {
            auto offset = from + index;
            bytes[index] = static_cast<uint8_t>(offset * 7 + offset / 251);
        }

        return bytes;
    }

    std::optional<std::string> ExpectCommitted(const std::vector<uint8_t>& contents, const std::set<std::size_t>& sizes, const std::string& path)
    {
        if (!sizes.contains(contents.size()))
            return path + " holds " + std::to_string(contents.size()) + " bytes, a size that no commit left";

        if (contents != FileBytes(0, contents.size()))
            return path + " does not hold the bytes that were written";

        return std::nullopt;
    }

    class PowerLossFlash
        : public hal::Flash
    {
    public:
        explicit PowerLossFlash(hal::Flash& flash)
            : flash(flash)
        {}

        void Lose()
        {
            lost = true;
        }

        uint32_t NumberOfSectors() const override
        {
            return flash.NumberOfSectors();
        }

        uint32_t SizeOfSector(uint32_t sectorIndex) const override
        {
            return flash.SizeOfSector(sectorIndex);
        }

        uint32_t SectorOfAddress(uint32_t address) const override
        {
            return flash.SectorOfAddress(address);
        }

        uint32_t AddressOfSector(uint32_t sectorIndex) const override
        {
            return flash.AddressOfSector(sectorIndex);
        }

        void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override
        {
            if (!lost)
                flash.WriteBuffer(buffer, address, onDone);
        }

        void ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone) override
        {
            if (!lost)
                flash.ReadBuffer(buffer, address, onDone);
        }

        void EraseSectors(uint32_t beginIndex, uint32_t endIndex, infra::Function<void()> onDone) override
        {
            if (!lost)
                flash.EraseSectors(beginIndex, endIndex, onDone);
        }

    private:
        hal::Flash& flash;
        bool lost{ false };
    };
}

class NorAppendPowerCut
    : public testing::TestWithParam<const metrics::NorModel*>
{
public:
    const metrics::NorModel& model{ *GetParam() };
    fs::NorConfig config{ metrics::SweepConfig(model) };
    uint32_t blockSize{ model.sectorSize * config.sectorsPerBlock };
    uint32_t payload{ blockSize - fs::NorHeaderSize(config.programUnit) };

    fs::NorConfig longJournal{ WithJournalBlocks(config, 4) };
    fs::NorConfig leveling{ WithLeveling(config) };
    uint32_t releasedTail{ 0 };
    std::vector<std::pair<uint32_t, std::optional<fs::NorAllocation>>> setUpNodeBlocks;
    std::map<uint32_t, uint32_t> setUpDataBlocks;
    uint32_t mostMoves{ 0 };
    std::vector<std::string> removedDirectories;

    static constexpr std::size_t initialLog = 100;
    static constexpr std::size_t appendAfterLoss = 1000;
    const std::vector<std::size_t> appends{ 700, 3000, 600, 10 };

    static fs::NorConfig WithJournalBlocks(fs::NorConfig config, uint32_t journalBlocks)
    {
        config.journalBlocks = journalBlocks;
        return config;
    }

    static fs::NorConfig WithLeveling(fs::NorConfig config)
    {
        config.staticWearLevelingThreshold = 2;
        config.staticWearLevelingInterval = 1;
        return config;
    }

    std::size_t SizeAfter(std::size_t writes) const
    {
        auto size = initialLog;
        for (std::size_t index = 0; index != writes; ++index)
            size += appends[index];

        return size;
    }

    std::set<std::size_t> LogSizes() const
    {
        std::set<std::size_t> sizes;
        for (std::size_t writes = 0; writes <= appends.size(); ++writes)
            sizes.insert(SizeAfter(writes));

        return sizes;
    }

    std::size_t BigSize() const
    {
        return 9 * std::size_t{ payload } + 100;
    }

    void MakeFreeBlocksNotErased(Context& context, std::size_t keep) const
    {
        auto check = metrics::CheckImage(context, model);
        std::vector<uint8_t> partB(fs::RoundUpTo(fs::norHeaderPartBBytes, config.programUnit));
        std::size_t kept = 0;

        for (uint32_t block = 2; block != check.blocks.size(); ++block)
            if (check.blocks[block] == metrics::NorBlockUse::freeErased && kept++ >= keep)
            {
                fs::SerializeNorHeaderB(fs::NorAllocation{ fs::NorBlockType::data, 1000 + block, { 99, 0 } }, infra::MakeRange(partB));
                metrics::Program(context, block * blockSize + fs::NorHeaderPartBOffset(config.programUnit), partB);
            }
    }

    void WearFreeBlocks(Context& context, std::size_t keepErased, std::optional<uint32_t> spared = std::nullopt, uint32_t eraseCount = 5) const
    {
        auto check = metrics::CheckImage(context, model);
        std::vector<uint8_t> partA(fs::RoundUpTo(fs::norHeaderPartABytes, config.programUnit));
        std::vector<uint8_t> partB(fs::RoundUpTo(fs::norHeaderPartBBytes, config.programUnit));
        std::size_t kept = 0;

        for (uint32_t block = 2; block != check.blocks.size(); ++block)
        {
            auto use = check.blocks[block];
            if (block == spared || (use != metrics::NorBlockUse::free && use != metrics::NorBlockUse::freeErased) || (use == metrics::NorBlockUse::freeErased && kept++ < keepErased))
                continue;

            for (auto sector = block * config.sectorsPerBlock; sector != (block + 1) * config.sectorsPerBlock; ++sector)
                metrics::EraseSector(context, sector);

            fs::SerializeNorHeaderA(eraseCount, infra::MakeRange(partA));
            metrics::Program(context, block * blockSize, partA);
            fs::SerializeNorHeaderB(fs::NorAllocation{ fs::NorBlockType::data, 1000 + block, { 99, 0 } }, infra::MakeRange(partB));
            metrics::Program(context, block * blockSize + fs::NorHeaderPartBOffset(config.programUnit), partB);
        }
    }

    void ProgramBeyondTheEnd(Context& context, const std::string& path) const
    {
        auto check = metrics::CheckImage(context, model);
        const auto* entry = check.Find(path);
        ASSERT_NE(nullptr, entry);
        ASSERT_FALSE(entry->blocks.empty());

        auto last = entry->blocks.rbegin();
        auto programmed = entry->size - last->first * payload - entry->inlineBytes.size();
        metrics::Program(context, last->second * blockSize + check.layout.headerSize + programmed, std::vector<uint8_t>(std::max<uint32_t>(16, config.programUnit), 0x00));
    }

    void SetUpLog(Context& context) const
    {
        ASSERT_EQ(fs::Result::success, metrics::Format(context, config));
        {
            metrics::NorSweepVolume volume{ context, config };
            ASSERT_EQ(fs::Result::success, volume.Mount());
            ASSERT_EQ(fs::Result::success, volume.OpenFile("/log", fs::OpenMode::write));
            ASSERT_EQ(fs::Result::success, volume.Write(FileBytes(0, initialLog)));
            ASSERT_EQ(fs::Result::success, volume.Close());
            ASSERT_EQ(fs::Result::success, volume.Unmount());
        }

        ProgramBeyondTheEnd(context, "/log");
        MakeFreeBlocksNotErased(context, 2);
        ASSERT_EQ(std::nullopt, metrics::CheckImage(context, model).problem);
    }

    void SetUpLossDuringAppend(Context& context, std::size_t writes) const
    {
        SetUpLog(context);
        PowerLossFlash flash{ context.device };
        Context losing{ context.time, flash };
        {
            metrics::NorSweepVolume volume{ losing, config };
            ASSERT_EQ(fs::Result::success, volume.Mount());
            ASSERT_EQ(fs::Result::success, volume.OpenFile("/log", fs::OpenMode::append));
            for (std::size_t index = 0; index != writes; ++index)
            {
                if (index != 0)
                    ASSERT_EQ(fs::Result::success, volume.Sync());

                ASSERT_EQ(fs::Result::success, volume.Write(FileBytes(SizeAfter(index), appends[index])));
            }

            flash.Lose();
            EXPECT_NE(fs::Result::success, volume.Close());
        }

        auto check = metrics::CheckImage(context, model);
        ASSERT_EQ(std::nullopt, check.problem);
        ASSERT_NE(nullptr, check.Find("/log"));
        ASSERT_EQ(SizeAfter(writes - 1), check.Find("/log")->size);
    }

    void SetUpReleasedTail(Context& context)
    {
        SetUpLog(context);
        auto check = metrics::CheckImage(context, model);
        ASSERT_NE(nullptr, check.Find("/log"));
        releasedTail = check.Find("/log")->blocks.rbegin()->second;
        WearFreeBlocks(context, 2);

        {
            metrics::NorSweepVolume volume{ context, longJournal };
            ASSERT_EQ(fs::Result::success, volume.Mount());
            ASSERT_EQ(fs::Result::success, volume.OpenFile("/log", fs::OpenMode::append));
            ASSERT_EQ(fs::Result::success, volume.Write(FileBytes(initialLog, appends[0])));
            ASSERT_EQ(fs::Result::success, volume.Close());
        }

        WearFreeBlocks(context, 0, releasedTail);
        check = metrics::CheckImage(context, model);
        ASSERT_EQ(std::nullopt, check.problem);
        ASSERT_EQ(metrics::NorBlockUse::free, check.blocks[releasedTail]);
    }

    void CreateIntoTheReleasedTail(Context& context) const
    {
        metrics::NorSweepVolume volume{ context, longJournal };
        auto committed = SizeAfter(1);
        if (volume.Mount() != fs::Result::success || volume.OpenFile("/new", fs::OpenMode::write) != fs::Result::success || volume.Write(FileBytes(0, 50)) != fs::Result::success || volume.Close() != fs::Result::success)
            return;

        if (volume.OpenFile("/log", fs::OpenMode::append) != fs::Result::success || volume.Write(FileBytes(committed, appendAfterLoss)) != fs::Result::success || volume.Sync() != fs::Result::success || volume.Write(FileBytes(committed + appendAfterLoss, 10)) != fs::Result::success)
            return;

        volume.Close();
    }

    std::optional<std::string> VerifyReleasedTail(Context& context, bool uncut) const
    {
        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        if (check.problem.has_value())
            return check.problem;

        const auto* log = check.Find("/log");
        if (log == nullptr)
            return "the log is gone";

        auto committed = SizeAfter(1);
        auto contents = metrics::ReadContents(image, check, *log);
        if (auto problem = ExpectCommitted(contents, { committed, committed + appendAfterLoss, committed + appendAfterLoss + 10 }, "/log"))
            return problem;

        const auto* created = check.Find("/new");
        std::optional<std::vector<uint8_t>> createdContents;
        if (created != nullptr)
            createdContents = metrics::ReadContents(image, check, *created);
        if (createdContents.has_value())
            if (auto problem = ExpectCommitted(*createdContents, { 0, 50 }, "/new"))
                return problem;

        if (uncut && (contents.size() != committed + appendAfterLoss + 10 || !createdContents.has_value() || createdContents->size() != 50 || check.blocks[releasedTail] != metrics::NorBlockUse::journal))
            return "the run without a cut did not write everything into a journal that reuses the released block";

        {
            metrics::NorSweepVolume volume{ context, longJournal };
            if (volume.Mount() != fs::Result::success)
                return "the volume does not mount";

            if (volume.ReadFile("/new") != createdContents)
                return "the library reads another /new than the checker";
        }

        return VerifyAfterMount(context, "/log", contents);
    }

    void AppendToLog(Context& context) const
    {
        metrics::NorSweepVolume volume{ context, config };
        if (volume.Mount() != fs::Result::success || volume.OpenFile("/log", fs::OpenMode::append) != fs::Result::success)
            return;

        for (std::size_t index = 0; index != appends.size(); ++index)
            if ((index != 0 && volume.Sync() != fs::Result::success) || volume.Write(FileBytes(SizeAfter(index), appends[index])) != fs::Result::success)
                return;

        if (volume.Close() != fs::Result::success || volume.OpenFile("/new", fs::OpenMode::write) != fs::Result::success || volume.Write(FileBytes(0, 50)) != fs::Result::success || volume.Close() != fs::Result::success)
            return;

        volume.Unmount();
    }

    void AppendAfterLoss(Context& context, std::size_t committed) const
    {
        metrics::NorSweepVolume volume{ context, config };
        if (volume.Mount() != fs::Result::success || volume.OpenFile("/log", fs::OpenMode::append) != fs::Result::success || volume.Write(FileBytes(committed, appendAfterLoss)) != fs::Result::success || volume.Sync() != fs::Result::success || volume.Write(FileBytes(committed + appendAfterLoss, 10)) != fs::Result::success || volume.Close() != fs::Result::success)
            return;

        volume.Unmount();
    }

    std::optional<std::string> VerifyLog(Context& context, bool uncut) const
    {
        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        if (check.problem.has_value())
            return check.problem;

        const auto* log = check.Find("/log");
        if (log == nullptr)
            return "the log is gone";

        auto contents = metrics::ReadContents(image, check, *log);
        if (auto problem = ExpectCommitted(contents, LogSizes(), "/log"))
            return problem;

        const auto* created = check.Find("/new");
        if (created != nullptr)
            if (auto problem = ExpectCommitted(metrics::ReadContents(image, check, *created), { 0, 50 }, "/new"))
                return problem;

        if (uncut && (contents.size() != SizeAfter(appends.size()) || created == nullptr || created->size != 50))
            return "the run without a cut did not write everything";

        return VerifyAfterMount(context, "/log", contents);
    }

    std::optional<std::string> VerifyLogAfterLoss(Context& context, bool uncut, std::size_t committed) const
    {
        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        if (check.problem.has_value())
            return check.problem;

        const auto* log = check.Find("/log");
        if (log == nullptr)
            return "the log is gone";

        auto contents = metrics::ReadContents(image, check, *log);
        if (auto problem = ExpectCommitted(contents, { committed, committed + appendAfterLoss, committed + appendAfterLoss + 10 }, "/log"))
            return problem;

        if (uncut && contents.size() != committed + appendAfterLoss + 10)
            return "the run without a cut did not write everything";

        return VerifyAfterMount(context, "/log", contents);
    }

    void SetUpEmpty(Context& context) const
    {
        ASSERT_EQ(fs::Result::success, metrics::Format(context, config));
        MakeFreeBlocksNotErased(context, 2);
    }

    void WriteBig(Context& context) const
    {
        metrics::NorSweepVolume volume{ context, config };
        if (volume.Mount() != fs::Result::success || volume.OpenFile("/big", fs::OpenMode::write) != fs::Result::success || volume.Write(FileBytes(0, BigSize())) != fs::Result::success || volume.Close() != fs::Result::success)
            return;

        volume.Unmount();
    }

    std::optional<std::string> VerifyBig(Context& context, bool uncut) const
    {
        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        if (check.problem.has_value())
            return check.problem;

        const auto* big = check.Find("/big");
        if (big == nullptr)
            return uncut ? std::optional<std::string>("the run without a cut did not create the file") : std::nullopt;

        auto contents = metrics::ReadContents(image, check, *big);
        if (auto problem = ExpectCommitted(contents, { 0, 8 * std::size_t{ payload } + config.inlineBytes, BigSize() }, "/big"))
            return problem;

        if (uncut && contents.size() != BigSize())
            return "the run without a cut did not write everything";

        return VerifyAfterMount(context, "/big", contents);
    }

    std::optional<std::string> VerifyAfterMount(Context& context, const std::string& path, const std::vector<uint8_t>& contents) const
    {
        {
            metrics::NorSweepVolume volume{ context, config };
            if (volume.Mount() != fs::Result::success)
                return "the volume does not mount";

            if (volume.ReadFile(path) != contents)
                return "the library reads other contents of " + path + " than the checker";

            if (volume.OpenFile(path, fs::OpenMode::append) != fs::Result::success || volume.Write(FileBytes(contents.size(), 200)) != fs::Result::success || volume.Close() != fs::Result::success || volume.Unmount() != fs::Result::success)
                return "an append after the mount fails";
        }

        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        if (check.problem.has_value())
            return "after an append after the mount: " + *check.problem;

        auto appended = contents;
        auto tail = FileBytes(contents.size(), 200);
        appended.insert(appended.end(), tail.begin(), tail.end());
        const auto* entry = check.Find(path);
        if (entry == nullptr || metrics::ReadContents(image, check, *entry) != appended)
            return "the append after the mount did not keep " + path;

        return std::nullopt;
    }

    static std::vector<uint8_t> OtherBytes(std::size_t from, std::size_t count)
    {
        auto bytes = FileBytes(from, count);
        for (auto& byte : bytes)
            byte ^= 0x5a;

        return bytes;
    }

    static void Put(std::vector<uint8_t>& contents, std::size_t at, const std::vector<uint8_t>& bytes)
    {
        if (contents.size() < at + bytes.size())
            contents.resize(at + bytes.size());

        std::ranges::copy(bytes, contents.begin() + static_cast<std::ptrdiff_t>(at));
    }

    void SetUpFiles(Context& context, const std::vector<std::pair<std::string, std::size_t>>& files, const std::vector<std::string>& directories = {}) const
    {
        ASSERT_EQ(fs::Result::success, metrics::Format(context, config));
        {
            metrics::NorSweepVolume volume{ context, config };
            ASSERT_EQ(fs::Result::success, volume.Mount());
            for (const auto& directory : directories)
                ASSERT_EQ(fs::Result::success, volume.MakeDirectory(directory));
            for (const auto& [path, size] : files)
            {
                ASSERT_EQ(fs::Result::success, volume.OpenFile(path, fs::OpenMode::write));
                ASSERT_EQ(fs::Result::success, volume.Write(FileBytes(0, size)));
                ASSERT_EQ(fs::Result::success, volume.Close());
            }
            ASSERT_EQ(fs::Result::success, volume.Unmount());
        }

        MakeFreeBlocksNotErased(context, 2);
        ASSERT_EQ(std::nullopt, metrics::CheckImage(context, model).problem);
    }

    std::size_t LogSize() const
    {
        return 3 * std::size_t{ payload } + 100;
    }

    std::vector<std::vector<uint8_t>> OverwriteStates() const
    {
        auto original = FileBytes(0, LogSize());
        auto synced = original;
        Put(synced, 100, OtherBytes(100, 50));
        Put(synced, payload - 20, OtherBytes(payload - 20, 40));
        auto closed = synced;
        Put(closed, LogSize() - 10, OtherBytes(LogSize() - 10, 30));
        return { original, synced, closed };
    }

    void Overwrite(Context& context) const
    {
        metrics::NorSweepVolume volume{ context, config };
        if (volume.Mount() != fs::Result::success || volume.OpenFile("/log", fs::OpenMode::readWrite) != fs::Result::success)
            return;

        volume.Seek(100);
        if (volume.Write(OtherBytes(100, 50)) != fs::Result::success)
            return;

        volume.Seek(payload - 20);
        if (volume.Write(OtherBytes(payload - 20, 40)) != fs::Result::success || volume.Sync() != fs::Result::success)
            return;

        volume.Seek(static_cast<uint32_t>(LogSize() - 10));
        if (volume.Write(OtherBytes(LogSize() - 10, 30)) != fs::Result::success || volume.Close() != fs::Result::success)
            return;

        volume.Unmount();
    }

    std::optional<std::string> VerifyStates(Context& context, bool uncut, const std::vector<std::vector<uint8_t>>& states) const
    {
        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        if (check.problem.has_value())
            return check.problem;

        const auto* log = check.Find("/log");
        if (log == nullptr)
            return "the log is gone";

        auto contents = metrics::ReadContents(image, check, *log);
        if (std::ranges::find(states, contents) == states.end())
            return "/log holds contents that no commit left";

        if (uncut && contents != states.back())
            return "the run without a cut did not write everything";

        return VerifyAfterMount(context, "/log", contents);
    }

    void SetUpLossDuringOverwrite(Context& context) const
    {
        SetUpFiles(context, { { "/log", LogSize() } });
        PowerLossFlash flash{ context.device };
        Context losing{ context.time, flash };
        {
            metrics::NorSweepVolume volume{ losing, config };
            ASSERT_EQ(fs::Result::success, volume.Mount());
            ASSERT_EQ(fs::Result::success, volume.OpenFile("/log", fs::OpenMode::readWrite));
            volume.Seek(100);
            ASSERT_EQ(fs::Result::success, volume.Write(OtherBytes(100, 50)));
            flash.Lose();
            EXPECT_NE(fs::Result::success, volume.Close());
        }

        auto check = metrics::CheckImage(context, model);
        ASSERT_EQ(std::nullopt, check.problem);
        ASSERT_NE(nullptr, check.Find("/log"));
        ASSERT_EQ(LogSize(), check.Find("/log")->size);
    }

    void OverwriteAfterLoss(Context& context) const
    {
        metrics::NorSweepVolume volume{ context, config };
        if (volume.Mount() != fs::Result::success || volume.OpenFile("/log", fs::OpenMode::readWrite) != fs::Result::success)
            return;

        volume.Seek(payload - 20);
        if (volume.Write(OtherBytes(payload - 20, 40)) != fs::Result::success || volume.Close() != fs::Result::success)
            return;

        volume.Unmount();
    }

    std::vector<std::vector<uint8_t>> OverwriteAfterLossStates() const
    {
        auto original = FileBytes(0, LogSize());
        auto overwritten = original;
        Put(overwritten, payload - 20, OtherBytes(payload - 20, 40));
        return { original, overwritten };
    }

    std::size_t TruncatedLog() const
    {
        return std::size_t{ payload } + 50;
    }

    std::size_t OldSize() const
    {
        return 2 * std::size_t{ payload } + 50;
    }

    void TruncateFiles(Context& context) const
    {
        metrics::NorSweepVolume volume{ context, config };
        if (volume.Mount() != fs::Result::success || volume.OpenFile("/log", fs::OpenMode::readWrite) != fs::Result::success)
            return;

        volume.Seek(static_cast<uint32_t>(TruncatedLog()));
        if (volume.Truncate() != fs::Result::success || volume.Write(FileBytes(TruncatedLog(), 30)) != fs::Result::success || volume.Close() != fs::Result::success)
            return;

        if (volume.OpenFile("/old", fs::OpenMode::write) != fs::Result::success || volume.Write(FileBytes(0, 10)) != fs::Result::success || volume.Close() != fs::Result::success)
            return;

        volume.Unmount();
    }

    std::optional<std::string> VerifyTruncated(Context& context, bool uncut) const
    {
        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        if (check.problem.has_value())
            return check.problem;

        const auto* log = check.Find("/log");
        const auto* old = check.Find("/old");
        if (log == nullptr || old == nullptr)
            return "a file is gone";

        auto logContents = metrics::ReadContents(image, check, *log);
        auto oldContents = metrics::ReadContents(image, check, *old);
        if (auto problem = ExpectCommitted(logContents, { LogSize(), TruncatedLog(), TruncatedLog() + 30 }, "/log"))
            return problem;

        if (auto problem = ExpectCommitted(oldContents, { OldSize(), 0, 10 }, "/old"))
            return problem;

        if (oldContents.size() != OldSize() && logContents.size() != TruncatedLog() + 30)
            return "/old changed before /log was closed";

        if (uncut && (logContents.size() != TruncatedLog() + 30 || oldContents.size() != 10))
            return "the run without a cut did not write everything";

        return VerifyAfterMount(context, "/log", logContents);
    }

    void SetUpRemovedBlock(Context& context)
    {
        ASSERT_EQ(fs::Result::success, metrics::Format(context, config));
        {
            metrics::NorSweepVolume volume{ context, longJournal };
            ASSERT_EQ(fs::Result::success, volume.Mount());
            ASSERT_EQ(fs::Result::success, volume.OpenFile("/x", fs::OpenMode::write));
            ASSERT_EQ(fs::Result::success, volume.Write(FileBytes(0, 1000)));
            ASSERT_EQ(fs::Result::success, volume.Close());
            auto check = metrics::CheckImage(context, model);
            ASSERT_NE(nullptr, check.Find("/x"));
            ASSERT_FALSE(check.Find("/x")->blocks.empty());
            releasedTail = check.Find("/x")->blocks.begin()->second;
            ASSERT_EQ(fs::Result::success, volume.Remove("/x"));
        }

        WearFreeBlocks(context, 0, releasedTail);
        auto check = metrics::CheckImage(context, model);
        ASSERT_EQ(std::nullopt, check.problem);
        ASSERT_EQ(metrics::NorBlockUse::free, check.blocks[releasedTail]);
    }

    void MakeIntoTheRemovedBlock(Context& context) const
    {
        metrics::NorSweepVolume volume{ context, longJournal };
        if (volume.Mount() != fs::Result::success || volume.MakeDirectory("/n1") != fs::Result::success || volume.MakeDirectory("/n2") != fs::Result::success)
            return;

        if (volume.OpenFile("/y", fs::OpenMode::write) == fs::Result::success && volume.Write(FileBytes(0, 10)) == fs::Result::success)
            volume.Close();
    }

    std::optional<std::string> VerifyRemovedBlock(Context& context, bool uncut) const
    {
        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        if (check.problem.has_value())
            return check.problem;

        if (check.Find("/x") != nullptr)
            return "/x is back";

        auto first = check.Find("/n1") != nullptr;
        auto second = check.Find("/n2") != nullptr;
        const auto* created = check.Find("/y");
        if ((second && !first) || (created != nullptr && !second))
            return "the changes do not form a prefix";

        if (created != nullptr)
            if (auto problem = ExpectCommitted(metrics::ReadContents(image, check, *created), { 0, 10 }, "/y"))
                return problem;

        if (uncut && (created == nullptr || created->size != 10 || check.blocks[releasedTail] != metrics::NorBlockUse::journal))
            return "the run without a cut did not write everything into a journal that reuses the removed block";

        {
            metrics::NorSweepVolume volume{ context, longJournal };
            if (volume.Mount() != fs::Result::success)
                return "the volume does not mount";

            if (volume.TypeOf("/n1").has_value() != first || volume.TypeOf("/n2").has_value() != second || volume.TypeOf("/x").has_value())
                return "the library and the checker disagree";

            if (volume.MakeDirectory("/z") != fs::Result::success || volume.Unmount() != fs::Result::success)
                return "a change after the mount fails";
        }

        auto after = metrics::CheckImage(context, model);
        if (after.problem.has_value())
            return "after a change after the mount: " + *after.problem;

        return std::nullopt;
    }

    const std::vector<std::string> removed{ "/a", "/d/e", "/d", "/f" };

    void RemoveEntries(Context& context) const
    {
        metrics::NorSweepVolume volume{ context, config };
        if (volume.Mount() != fs::Result::success)
            return;

        for (const auto& path : removed)
            if (volume.Remove(path) != fs::Result::success)
                return;

        if (volume.MakeDirectory("/n") != fs::Result::success)
            return;

        volume.Unmount();
    }

    std::optional<std::string> VerifyRemoved(Context& context, bool uncut) const
    {
        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        if (check.problem.has_value())
            return check.problem;

        std::size_t gone = 0;
        while (gone != removed.size() && check.Find(removed[gone]) == nullptr)
            ++gone;

        for (auto index = gone; index != removed.size(); ++index)
            if (check.Find(removed[index]) == nullptr)
                return removed[index] + " is gone before " + removed[gone];

        const auto* a = check.Find("/a");
        if (a != nullptr && metrics::ReadContents(image, check, *a) != FileBytes(0, 2 * std::size_t{ payload } + 100))
            return "/a does not hold its contents";

        auto made = check.Find("/n") != nullptr;
        if (made && gone != removed.size())
            return "/n was made before every remove";

        if (uncut && !made)
            return "the run without a cut did not make /n";

        {
            metrics::NorSweepVolume volume{ context, config };
            if (volume.Mount() != fs::Result::success)
                return "the volume does not mount";

            for (const auto& path : removed)
                if (volume.TypeOf(path).has_value() != (check.Find(path) != nullptr))
                    return "the library and the checker disagree about " + path;

            if (volume.MakeDirectory("/z") != fs::Result::success || volume.Unmount() != fs::Result::success)
                return "a change after the mount fails";
        }

        auto after = metrics::CheckImage(context, model);
        if (after.problem.has_value())
            return "after a change after the mount: " + *after.problem;

        return std::nullopt;
    }

    const std::vector<std::pair<std::string, std::string>> renames{ { "/a", "/b" }, { "/d/e", "/g/e" }, { "/b", "/d/c" }, { "/d", "/g/d" } };

    struct RenamedPaths
    {
        std::string file;
        std::string moved;
        std::string parent;
    };

    const std::vector<RenamedPaths> afterRenames{ { "/a", "/d/e", "/d" }, { "/b", "/d/e", "/d" }, { "/b", "/g/e", "/d" }, { "/d/c", "/g/e", "/d" }, { "/g/d/c", "/g/e", "/g/d" } };

    std::size_t RenamedSize() const
    {
        return 2 * std::size_t{ payload } + 100;
    }

    void RenameEntries(Context& context) const
    {
        metrics::NorSweepVolume volume{ context, config };
        if (volume.Mount() != fs::Result::success)
            return;

        for (const auto& [from, to] : renames)
            if (volume.Rename(from, to) != fs::Result::success)
                return;

        if (volume.MakeDirectory("/n") != fs::Result::success)
            return;

        volume.Unmount();
    }

    static bool OnlyAt(const metrics::NorCheck& check, const std::string& expected, const std::vector<std::string>& candidates)
    {
        for (const auto& candidate : candidates)
            if ((check.Find(candidate) != nullptr) != (candidate == expected))
                return false;

        return true;
    }

    std::optional<std::size_t> RenamesDone(const metrics::NorCheck& check) const
    {
        std::vector<std::string> files;
        std::vector<std::string> moved;
        std::vector<std::string> parents;
        for (const auto& paths : afterRenames)
        {
            files.push_back(paths.file);
            moved.push_back(paths.moved);
            parents.push_back(paths.parent);
        }

        for (std::size_t done = 0; done != afterRenames.size(); ++done)
        {
            const auto& expected = afterRenames[done];
            if (OnlyAt(check, expected.file, files) && OnlyAt(check, expected.moved, moved) && OnlyAt(check, expected.parent, parents))
                return done;
        }

        return std::nullopt;
    }

    std::optional<std::string> VerifyRenamed(Context& context, bool uncut) const
    {
        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        if (check.problem.has_value())
            return check.problem;

        auto done = RenamesDone(check);
        if (!done.has_value())
            return "the entries are not where a prefix of the renames leaves them";

        const auto& paths = afterRenames[*done];
        if (metrics::ReadContents(image, check, *check.Find(paths.file)) != FileBytes(0, RenamedSize()))
            return paths.file + " does not hold its contents";

        auto made = check.Find("/n") != nullptr;
        if (made && *done != renames.size())
            return "/n was made before every rename";

        if (uncut && !made)
            return "the run without a cut did not make /n";

        {
            metrics::NorSweepVolume volume{ context, config };
            if (volume.Mount() != fs::Result::success)
                return "the volume does not mount";

            if (volume.ReadFile(paths.file) != FileBytes(0, RenamedSize()) || volume.TypeOf(paths.moved) != fs::EntryType::directory || volume.TypeOf(paths.parent) != fs::EntryType::directory)
                return "the library and the checker disagree";

            if (volume.MakeDirectory("/z") != fs::Result::success || volume.Unmount() != fs::Result::success)
                return "a change after the mount fails";
        }

        auto after = metrics::CheckImage(context, model);
        if (after.problem.has_value())
            return "after a change after the mount: " + *after.problem;

        return std::nullopt;
    }

    static constexpr std::size_t settledDirectories = 40;

    static std::string SettledName(std::size_t index)
    {
        return "/d" + std::to_string(index / 10) + std::to_string(index % 10);
    }

    void SettleInBackground(Context& context) const
    {
        metrics::NorSweepVolume volume{ context, config };
        if (volume.Mount() != fs::Result::success)
            return;

        for (std::size_t index = 0; index != settledDirectories && !volume.JournalMoreThanHalfFull(); ++index)
            if (volume.MakeDirectory(SettledName(index)) != fs::Result::success)
                return;

        volume.Settle();
        if (volume.MakeDirectory("/n") != fs::Result::success)
            return;

        volume.Unmount();
    }

    std::optional<std::string> VerifySettled(Context& context, bool uncut) const
    {
        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        if (check.problem.has_value())
            return check.problem;

        std::size_t made = 0;
        while (made != settledDirectories && check.Find(SettledName(made)) != nullptr)
            ++made;

        for (auto index = made; index != settledDirectories; ++index)
            if (check.Find(SettledName(index)) != nullptr)
                return SettledName(index) + " was made before " + SettledName(made);

        auto madeAfter = check.Find("/n") != nullptr;
        if (uncut && !madeAfter)
            return "the run without a cut did not make /n";

        if (uncut && std::ranges::find(check.blocks, metrics::NorBlockUse::free) != check.blocks.end())
            return "the run without a cut left a free block that is not erased";

        {
            metrics::NorSweepVolume volume{ context, config };
            if (volume.Mount() != fs::Result::success)
                return "the volume does not mount";

            for (std::size_t index = 0; index != made; ++index)
                if (volume.TypeOf(SettledName(index)) != fs::EntryType::directory)
                    return "the library and the checker disagree about " + SettledName(index);

            if (volume.TypeOf("/n").has_value() != madeAfter)
                return "the library and the checker disagree about /n";

            if (volume.MakeDirectory("/z") != fs::Result::success || volume.Unmount() != fs::Result::success)
                return "a change after the mount fails";
        }

        auto after = metrics::CheckImage(context, model);
        if (after.problem.has_value())
            return "after a change after the mount: " + *after.problem;

        return std::nullopt;
    }

    static constexpr std::size_t collectedDirectories = 6;

    static std::string CollectedName(std::size_t index)
    {
        return "/d" + std::to_string(index);
    }

    std::size_t CollectedSize() const
    {
        return 2 * std::size_t{ payload } + 100;
    }

    std::optional<fs::NorAllocation> AllocationOf(const std::vector<uint8_t>& image, uint32_t block) const
    {
        auto header = infra::Head(infra::DiscardHead(infra::MakeRange(image), block * blockSize), fs::NorHeaderSize(config.programUnit));
        return fs::ParseNorBlockHeader(header, config.programUnit).allocation;
    }

    void FillJournalAndSettle(metrics::NorSweepVolume& volume, const std::string& prefix)
    {
        for (std::size_t index = 0; index != settledDirectories && !volume.JournalMoreThanHalfFull(); ++index)
        {
            removedDirectories.push_back(prefix + std::to_string(index));
            ASSERT_EQ(fs::Result::success, volume.MakeDirectory(removedDirectories.back()));
        }

        volume.Settle();
    }

    void SetUpCollection(Context& context)
    {
        ASSERT_EQ(fs::Result::success, metrics::Format(context, config));
        {
            metrics::NorSweepVolume volume{ context, config };
            ASSERT_EQ(fs::Result::success, volume.Mount());
            for (std::size_t index = 0; index != collectedDirectories; ++index)
                ASSERT_EQ(fs::Result::success, volume.MakeDirectory(CollectedName(index)));
            ASSERT_EQ(fs::Result::success, volume.MakeDirectory("/d0/e"));
            ASSERT_EQ(fs::Result::success, volume.OpenFile("/f", fs::OpenMode::write));
            ASSERT_EQ(fs::Result::success, volume.Write(FileBytes(0, CollectedSize())));
            ASSERT_EQ(fs::Result::success, volume.Close());
            removedDirectories.clear();
            FillJournalAndSettle(volume, "/p");
            FillJournalAndSettle(volume, "/q");
            ASSERT_EQ(fs::Result::success, volume.Unmount());
        }

        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        ASSERT_EQ(std::nullopt, check.problem);

        setUpNodeBlocks.clear();
        for (uint32_t block = 2; block != check.blocks.size(); ++block)
            if (check.blocks[block] == metrics::NorBlockUse::node)
                setUpNodeBlocks.emplace_back(block, AllocationOf(image, block));
        ASSERT_FALSE(setUpNodeBlocks.empty());
    }

    void CollectInBackground(Context& context) const
    {
        metrics::NorSweepVolume volume{ context, config };
        if (volume.Mount() != fs::Result::success || volume.MakeDirectory("/n") != fs::Result::success)
            return;

        for (const auto& path : removedDirectories)
            if (volume.Remove(path) != fs::Result::success)
                return;

        volume.Settle();
        if (volume.MakeDirectory("/m") != fs::Result::success)
            return;

        volume.Unmount();
    }

    std::size_t LeveledSize() const
    {
        return 2 * std::size_t{ payload } + 100;
    }

    void SetUpLeveling(Context& context, bool withFile)
    {
        ASSERT_EQ(fs::Result::success, metrics::Format(context, leveling));
        if (withFile)
        {
            metrics::NorSweepVolume volume{ context, leveling };
            ASSERT_EQ(fs::Result::success, volume.Mount());
            ASSERT_EQ(fs::Result::success, volume.MakeDirectory("/a"));
            ASSERT_EQ(fs::Result::success, volume.MakeDirectory("/a/b"));
            ASSERT_EQ(fs::Result::success, volume.OpenFile("/f", fs::OpenMode::write));
            ASSERT_EQ(fs::Result::success, volume.Write(FileBytes(0, LeveledSize())));
            ASSERT_EQ(fs::Result::success, volume.Close());
            ASSERT_EQ(fs::Result::success, volume.Unmount());
        }

        WearFreeBlocks(context, 0, std::nullopt, 9);

        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        ASSERT_EQ(std::nullopt, check.problem);
        const auto* file = check.Find("/f");
        ASSERT_EQ(withFile, file != nullptr);
        setUpDataBlocks = withFile ? file->blocks : std::map<uint32_t, uint32_t>{};

        setUpNodeBlocks.clear();
        for (uint32_t block = 2; block != check.blocks.size(); ++block)
            if (check.blocks[block] == metrics::NorBlockUse::node)
                setUpNodeBlocks.emplace_back(block, AllocationOf(image, block));
        ASSERT_FALSE(setUpNodeBlocks.empty());
    }

    void LevelInBackground(Context& context)
    {
        metrics::NorSweepVolume volume{ context, leveling };
        if (volume.Mount() != fs::Result::success)
            return;

        volume.Settle();
        mostMoves = std::max(mostMoves, volume.Counters().movedBlocks);
        if (volume.MakeDirectory("/m") != fs::Result::success)
            return;

        volume.Unmount();
    }

    void LevelInTheForeground(Context& context)
    {
        metrics::NorSweepVolume volume{ context, leveling };
        if (volume.Mount() != fs::Result::success || volume.OpenFile("/g", fs::OpenMode::write) != fs::Result::success)
            return;

        if (volume.Write(FileBytes(1000, LeveledSize())) != fs::Result::success || volume.Close() != fs::Result::success)
            return;

        mostMoves = std::max(mostMoves, volume.Counters().movedBlocks);
        volume.Unmount();
    }

    std::optional<std::string> VerifyLeveledInTheForeground(Context& context, bool uncut) const
    {
        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        if (check.problem.has_value())
            return check.problem;

        const auto* file = check.Find("/f");
        if (check.Find("/a/b") == nullptr || file == nullptr || metrics::ReadContents(image, check, *file) != FileBytes(0, LeveledSize()))
            return "the set-up is lost";

        const auto* written = check.Find("/g");
        auto writtenContents = written != nullptr ? metrics::ReadContents(image, check, *written) : std::vector<uint8_t>{};
        if (!writtenContents.empty() && writtenContents != FileBytes(1000, LeveledSize()))
            return "/g holds neither nothing nor its contents";

        if (uncut && writtenContents.empty())
            return "the run without a cut did not write /g";

        {
            metrics::NorSweepVolume volume{ context, leveling };
            if (volume.Mount() != fs::Result::success)
                return "the volume does not mount";

            if (volume.ReadFile("/f") != FileBytes(0, LeveledSize()) || volume.TypeOf("/g").has_value() != (written != nullptr))
                return "the library and the checker disagree";

            if (volume.MakeDirectory("/z") != fs::Result::success || volume.Unmount() != fs::Result::success)
                return "a change after the mount fails";
        }

        auto after = metrics::CheckImage(context, model);
        if (after.problem.has_value())
            return "after a change after the mount: " + *after.problem;

        return std::nullopt;
    }

    std::optional<std::string> VerifyLeveled(Context& context, bool uncut) const
    {
        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        if (check.problem.has_value())
            return check.problem;

        auto withFile = !setUpDataBlocks.empty();
        if (withFile && check.Find("/a/b") == nullptr)
            return "/a/b is lost";

        const auto* file = check.Find("/f");
        if (withFile && (file == nullptr || metrics::ReadContents(image, check, *file) != FileBytes(0, LeveledSize())))
            return "/f does not hold its contents";

        if (uncut && check.Find("/m") == nullptr)
            return "the run without a cut did not make /m";

        auto dataMoved = [&file](const auto& setUp)
        {
            auto found = file->blocks.find(setUp.first);
            return found == file->blocks.end() || found->second != setUp.second;
        };
        if (uncut && !std::ranges::all_of(setUpDataBlocks, dataMoved))
            return "the run without a cut did not move every data block of /f";

        auto nodesLeft = [&](const auto& setUp)
        {
            return check.blocks[setUp.first] != metrics::NorBlockUse::node || AllocationOf(image, setUp.first) != setUp.second;
        };
        if (uncut && !std::ranges::all_of(setUpNodeBlocks, nodesLeft))
            return "the run without a cut left a node block of the set-up in use";

        {
            metrics::NorSweepVolume volume{ context, leveling };
            if (volume.Mount() != fs::Result::success)
                return "the volume does not mount";

            if (withFile && (volume.ReadFile("/f") != FileBytes(0, LeveledSize()) || volume.TypeOf("/a/b") != fs::EntryType::directory))
                return "the library and the checker disagree on the set-up";

            if (volume.TypeOf("/m").has_value() != (check.Find("/m") != nullptr))
                return "the library and the checker disagree on /m";

            if (volume.MakeDirectory("/z") != fs::Result::success || volume.Unmount() != fs::Result::success)
                return "a change after the mount fails";
        }

        auto after = metrics::CheckImage(context, model);
        if (after.problem.has_value())
            return "after a change after the mount: " + *after.problem;

        return std::nullopt;
    }

    void SweepLeveling(const std::string& caseName, bool withFile)
    {
        mostMoves = 0;
        SweepEveryMode(
            model, caseName, metrics::SweepRamBytes(model), [this, withFile](Context& context)
            {
                SetUpLeveling(context, withFile);
            },
            [this](Context& context)
            {
                LevelInBackground(context);
            },
            [this](Context& context, bool uncut)
            {
                return VerifyLeveled(context, uncut);
            });
    }

    std::optional<std::string> VerifyCollected(Context& context, bool uncut) const
    {
        auto image = metrics::ReadImage(context);
        auto check = metrics::CheckNorImage(image, model.sectorSize, model.pageSize);
        if (check.problem.has_value())
            return check.problem;

        for (std::size_t index = 0; index != collectedDirectories; ++index)
            if (check.Find(CollectedName(index)) == nullptr)
                return CollectedName(index) + " is lost";

        if (check.Find("/d0/e") == nullptr)
            return "/d0/e is lost";

        const auto* file = check.Find("/f");
        if (file == nullptr || metrics::ReadContents(image, check, *file) != FileBytes(0, CollectedSize()))
            return "/f does not hold its contents";

        auto madeFirst = check.Find("/n") != nullptr;
        auto madeSecond = check.Find("/m") != nullptr;
        if (madeSecond && !madeFirst)
            return "/m was made before /n";

        if (uncut && !madeSecond)
            return "the run without a cut did not make /m";

        auto removed = static_cast<std::size_t>(std::ranges::find_if(removedDirectories, [&check](const std::string& path)
                                                    {
                                                        return check.Find(path) != nullptr;
                                                    }) -
                                                removedDirectories.begin());
        for (auto index = removed; index != removedDirectories.size(); ++index)
            if (check.Find(removedDirectories[index]) == nullptr)
                return removedDirectories[index] + " was removed before " + removedDirectories[removed];

        if ((removed != 0 && !madeFirst) || (madeSecond && removed != removedDirectories.size()))
            return "the removes did not happen between /n and /m";

        auto collected = [&](const auto& setUp)
        {
            return check.blocks[setUp.first] != metrics::NorBlockUse::node || AllocationOf(image, setUp.first) != setUp.second;
        };
        if (uncut && std::ranges::none_of(setUpNodeBlocks, collected))
            return "the run without a cut collected no node block of the set-up";

        {
            metrics::NorSweepVolume volume{ context, config };
            if (volume.Mount() != fs::Result::success)
                return "the volume does not mount";

            if (volume.ReadFile("/f") != FileBytes(0, CollectedSize()) || volume.TypeOf("/d0/e") != fs::EntryType::directory || volume.TypeOf("/n").has_value() != madeFirst)
                return "the library and the checker disagree";

            if (volume.MakeDirectory("/z") != fs::Result::success || volume.Unmount() != fs::Result::success)
                return "a change after the mount fails";
        }

        auto after = metrics::CheckImage(context, model);
        if (after.problem.has_value())
            return "after a change after the mount: " + *after.problem;

        return std::nullopt;
    }

    void SweepAppendAfterLoss(const std::string& caseName, std::size_t writes)
    {
        auto committed = SizeAfter(writes - 1);
        SweepEveryMode(
            model, caseName, metrics::SweepRamBytes(model), [this, writes](Context& context)
            {
                SetUpLossDuringAppend(context, writes);
            },
            [this, committed](Context& context)
            {
                AppendAfterLoss(context, committed);
            },
            [this, committed](Context& context, bool uncut)
            {
                return VerifyLogAfterLoss(context, uncut, committed);
            });
    }
};

TEST_P(NorAppendPowerCut, appends_after_a_mount_keep_the_last_commit_and_the_file_takes_appends_after_the_next_mount)
{
    SweepEveryMode(
        model, "append", metrics::SweepRamBytes(model), [this](Context& context)
        {
            SetUpLog(context);
        },
        [this](Context& context)
        {
            AppendToLog(context);
        },
        [this](Context& context, bool uncut)
        {
            return VerifyLog(context, uncut);
        });
}

TEST_P(NorAppendPowerCut, an_append_after_a_power_loss_with_programmed_pages_beyond_the_commit_keeps_the_last_commit)
{
    SweepAppendAfterLoss("append after a cut with uncommitted pages", 2);
}

TEST_P(NorAppendPowerCut, an_append_after_a_power_loss_with_an_uncommitted_block_keeps_the_last_commit)
{
    SweepAppendAfterLoss("append after a cut with an uncommitted block", 3);
}

TEST_P(NorAppendPowerCut, records_in_a_released_block_that_became_a_journal_block_survive_a_cut)
{
    SweepEveryMode(
        model, "journal in a released block", metrics::SweepRamBytes(model), [this](Context& context)
        {
            SetUpReleasedTail(context);
        },
        [this](Context& context)
        {
            CreateIntoTheReleasedTail(context);
        },
        [this](Context& context, bool uncut)
        {
            return VerifyReleasedTail(context, uncut);
        });
}

TEST_P(NorAppendPowerCut, a_file_of_more_blocks_than_a_handle_keeps_uncommitted_keeps_its_last_commit)
{
    SweepEveryMode(
        model, "write nine blocks", metrics::SweepRamBytes(model), [this](Context& context)
        {
            SetUpEmpty(context);
        },
        [this](Context& context)
        {
            WriteBig(context);
        },
        [this](Context& context, bool uncut)
        {
            return VerifyBig(context, uncut);
        });
}

TEST_P(NorAppendPowerCut, overwrites_keep_the_contents_of_the_last_commit)
{
    SweepEveryMode(
        model, "overwrite", metrics::SweepRamBytes(model), [this](Context& context)
        {
            SetUpFiles(context, { { "/log", LogSize() } });
        },
        [this](Context& context)
        {
            Overwrite(context);
        },
        [this](Context& context, bool uncut)
        {
            return VerifyStates(context, uncut, OverwriteStates());
        });
}

TEST_P(NorAppendPowerCut, an_overwrite_after_a_power_loss_with_an_uncommitted_copy_keeps_the_last_commit)
{
    SweepEveryMode(
        model, "overwrite after a cut with an uncommitted copy", metrics::SweepRamBytes(model), [this](Context& context)
        {
            SetUpLossDuringOverwrite(context);
        },
        [this](Context& context)
        {
            OverwriteAfterLoss(context);
        },
        [this](Context& context, bool uncut)
        {
            return VerifyStates(context, uncut, OverwriteAfterLossStates());
        });
}

TEST_P(NorAppendPowerCut, truncations_keep_the_last_commit)
{
    SweepEveryMode(
        model, "truncate", metrics::SweepRamBytes(model), [this](Context& context)
        {
            SetUpFiles(context, { { "/log", LogSize() }, { "/old", OldSize() } });
        },
        [this](Context& context)
        {
            TruncateFiles(context);
        },
        [this](Context& context, bool uncut)
        {
            return VerifyTruncated(context, uncut);
        });
}

TEST_P(NorAppendPowerCut, a_block_that_a_record_mapped_and_a_remove_released_can_become_a_journal_block_of_the_epoch)
{
    SweepEveryMode(
        model, "journal in a removed block", metrics::SweepRamBytes(model), [this](Context& context)
        {
            SetUpRemovedBlock(context);
        },
        [this](Context& context)
        {
            MakeIntoTheRemovedBlock(context);
        },
        [this](Context& context, bool uncut)
        {
            return VerifyRemovedBlock(context, uncut);
        });
}

TEST_P(NorAppendPowerCut, removes_happen_in_order_and_each_completely_or_not_at_all)
{
    SweepEveryMode(
        model, "remove", metrics::SweepRamBytes(model), [this](Context& context)
        {
            SetUpFiles(context, { { "/a", 2 * std::size_t{ payload } + 100 }, { "/f", 10 } }, { "/d", "/d/e" });
        },
        [this](Context& context)
        {
            RemoveEntries(context);
        },
        [this](Context& context, bool uncut)
        {
            return VerifyRemoved(context, uncut);
        });
}

TEST_P(NorAppendPowerCut, renames_happen_in_order_and_leave_each_entry_under_exactly_one_name)
{
    SweepEveryMode(
        model, "rename", metrics::SweepRamBytes(model), [this](Context& context)
        {
            SetUpFiles(context, { { "/a", RenamedSize() } }, { "/d", "/d/e", "/g" });
        },
        [this](Context& context)
        {
            RenameEntries(context);
        },
        [this](Context& context, bool uncut)
        {
            return VerifyRenamed(context, uncut);
        });
}

TEST_P(NorAppendPowerCut, the_background_erases_free_blocks_and_writes_a_checkpoint_that_a_cut_leaves_consistent)
{
    SweepEveryMode(
        model, "background", metrics::SweepRamBytes(model), [this](Context& context)
        {
            SetUpFiles(context, { { "/f", 2 * std::size_t{ payload } + 100 } });
        },
        [this](Context& context)
        {
            SettleInBackground(context);
        },
        [this](Context& context, bool uncut)
        {
            return VerifySettled(context, uncut);
        });
}

TEST_P(NorAppendPowerCut, the_collection_of_node_blocks_moves_the_used_nodes_and_a_cut_leaves_consistent)
{
    SweepEveryMode(
        model, "collection", metrics::SweepRamBytes(model), [this](Context& context)
        {
            SetUpCollection(context);
        },
        [this](Context& context)
        {
            CollectInBackground(context);
        },
        [this](Context& context, bool uncut)
        {
            return VerifyCollected(context, uncut);
        });
}

TEST_P(NorAppendPowerCut, static_wear_leveling_moves_data_blocks_and_a_cut_leaves_consistent)
{
    SweepLeveling("leveling-data", true);

    EXPECT_LE(setUpDataBlocks.size(), mostMoves);
}

TEST_P(NorAppendPowerCut, static_wear_leveling_moves_node_blocks_and_a_cut_leaves_consistent)
{
    SweepLeveling("leveling-nodes", false);

    EXPECT_LE(setUpNodeBlocks.size(), mostMoves);
}

TEST_P(NorAppendPowerCut, the_fallback_of_static_wear_leveling_erases_its_destination_on_an_empty_pool_and_a_cut_leaves_consistent)
{
    mostMoves = 0;
    SweepEveryMode(
        model, "leveling-fallback", metrics::SweepRamBytes(model), [this](Context& context)
        {
            SetUpLeveling(context, true);
        },
        [this](Context& context)
        {
            LevelInTheForeground(context);
        },
        [this](Context& context, bool uncut)
        {
            return VerifyLeveledInTheForeground(context, uncut);
        });

    EXPECT_LE(1, mostMoves);
}

INSTANTIATE_TEST_SUITE_P(Nor, NorAppendPowerCut, testing::Values(&metrics::norSweep128k, &metrics::internalSweep96k, &metrics::norSweep128kTwoSectors), ParameterName);
