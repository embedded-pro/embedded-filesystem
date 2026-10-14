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
