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
        metrics::NorTargets targets;
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
    uint32_t releasedTail{ 0 };

    static constexpr std::size_t initialLog = 100;
    static constexpr std::size_t appendAfterLoss = 1000;
    const std::vector<std::size_t> appends{ 700, 3000, 600, 10 };

    static fs::NorConfig WithJournalBlocks(fs::NorConfig config, uint32_t journalBlocks)
    {
        config.journalBlocks = journalBlocks;
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

    void WearFreeBlocks(Context& context, std::size_t keepErased) const
    {
        auto check = metrics::CheckImage(context, model);
        std::vector<uint8_t> partA(fs::RoundUpTo(fs::norHeaderPartABytes, config.programUnit));
        std::vector<uint8_t> partB(fs::RoundUpTo(fs::norHeaderPartBBytes, config.programUnit));
        std::size_t kept = 0;

        for (uint32_t block = 2; block != check.blocks.size(); ++block)
        {
            auto use = check.blocks[block];
            if ((use != metrics::NorBlockUse::free && use != metrics::NorBlockUse::freeErased) || (use == metrics::NorBlockUse::freeErased && kept++ < keepErased))
                continue;

            for (auto sector = block * config.sectorsPerBlock; sector != (block + 1) * config.sectorsPerBlock; ++sector)
                metrics::EraseSector(context, sector);

            fs::SerializeNorHeaderA(5, infra::MakeRange(partA));
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

        const auto* entry = check.Find(path);
        if (entry == nullptr || metrics::ReadContents(image, check, *entry) != FileBytes(0, contents.size() + 200))
            return "the append after the mount did not keep " + path;

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

INSTANTIATE_TEST_SUITE_P(Nor, NorAppendPowerCut, testing::Values(&metrics::norSweep128k, &metrics::internalSweep96k, &metrics::norSweep128kTwoSectors), ParameterName);
