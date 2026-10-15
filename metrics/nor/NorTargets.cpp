#include "metrics/nor/NorTargets.hpp"
#include "fs/nor/NorGeometry.hpp"
#include "fs/nor/NorLayout.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <chrono>

namespace metrics
{
    namespace
    {
        constexpr uint32_t mebibyte = 1024 * 1024;
        constexpr std::string_view randomReadRatio{ "L6 random ratio" };
    }

    NorTargets::NorTargets(const NorModel& model, const fs::NorConfig& config, uint32_t scaleDivisor)
        : model(model)
        , config(config)
        , scaleDivisor(scaleDivisor)
        , firstPhaseOfL1("L1 " + std::to_string(mebibyte / scaleDivisor) + " 64")
    {}

    std::optional<Target> NorTargets::For(std::string_view metric, std::string_view workload) const
    {
        if (metric == "P-1")
            return AtLeast(1.0);

        if (metric.starts_with("WL-"))
            return WearTarget(metric);

        if (metric.starts_with("W-"))
            return WriteTarget(metric, workload);

        if (metric.starts_with("R-"))
            return ReadTarget(metric, workload);

        return std::nullopt;
    }

    double NorTargets::CommitMilliseconds() const
    {
        auto geometry = fs::MakeNorGeometry(config, model.sectorSize, model.numberOfSectors);
        really_assert(geometry.has_value());

        constexpr auto timing = TimingCase::maximum;
        auto program = [this](uint32_t bytes)
        {
            return ProgramTime(model, timing, 0, bytes);
        };

        auto partB = program(geometry->RoundToProgramUnit(fs::norHeaderPartBBytes));
        auto record = program(geometry->nodeSize);
        auto blockCopy = ReadTime(model, timing, geometry->PayloadSize()) + program(geometry->PayloadSize()) + partB;
        auto nodes = static_cast<infra::Duration::rep>(2 * config.cachedNodes);
        auto nodeBlocks = static_cast<infra::Duration::rep>(2 * geometry->NodeBlocksPerCheckpoint() + 1);
        infra::Duration checkpoint = program(geometry->nodeSize) * nodes + partB * nodeBlocks + program(geometry->RoundToProgramUnit(geometry->CheckpointBytes()));
        auto move = EraseTime(model, timing, config.sectorsPerBlock) + blockCopy + record;
        auto newJournalBlock = partB + record;
        auto commit = EraseTime(model, timing, 1) + std::max(checkpoint, move) + 2 * blockCopy + record + newJournalBlock;

        return std::chrono::duration<double, std::milli>(commit).count();
    }

    std::optional<Target> NorTargets::WriteTarget(std::string_view metric, std::string_view workload) const
    {
        if (metric == "W-1")
            return AtLeastOf(std::string{ allBaselines }, 0.97);

        if (metric == "W-2")
            return AtMost(1.1);

        if (metric == "W-3" || metric == "W-5")
            return AtMost(1.2);

        if (metric == "W-4" && workload.starts_with("L1") && (scaleDivisor == 1 || workload != firstPhaseOfL1))
            return AtMost(1.1);

        if (metric == "W-6" && workload == "L2 maximum")
            return AtMost(CommitMilliseconds());

        return std::nullopt;
    }

    std::optional<Target> NorTargets::ReadTarget(std::string_view metric, std::string_view workload) const
    {
        if (metric == "R-1")
            return AtLeastOf(std::string{ allBaselines });

        if (metric == "R-2" && workload != randomReadRatio)
            return AtMost(4);

        if (metric == "R-2" && scaleDivisor == 1)
            return AtMost(1.2);

        if (metric == "R-5")
            return AtMost(1.1);

        return std::nullopt;
    }

    std::optional<Target> NorTargets::WearTarget(std::string_view metric) const
    {
        if (model.name != norQspi1m.name)
            return std::nullopt;

        if (metric == "WL-1")
            return AtMost(1.2);

        if (metric == "WL-2")
            return AtLeast(0.8);

        if (metric == "WL-4")
            return AtMost(0.05);

        return std::nullopt;
    }
}
