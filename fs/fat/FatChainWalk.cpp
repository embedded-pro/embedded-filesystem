#include "fs/fat/FatChainWalk.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    FatChainWalk::FatChainWalk(SectorCache& cache, const FatGeometry& geometry)
        : table(cache, geometry)
        , geometry(geometry)
    {}

    FatChainWalk::~FatChainWalk() = default;

    void FatChainWalk::Walk(const FatExtent& from, uint32_t target, uint32_t limit, const infra::Function<void(Result result, const FatExtent& run)>& onDone)
    {
        really_assert(from.index <= target && target <= limit);

        run = from;
        this->target = target;
        this->limit = limit;
        this->onDone = onDone;
        steps = 0;

        really_assert(!Covered());
        Step();
    }

    void FatChainWalk::Step()
    {
        if (Covered())
            onDone(Result::success, run);
        else
            table.Next(run.ClusterAt(run.End() - 1), [this](Result result, std::optional<uint32_t> next)
                {
                    OnNext(result, next);
                });
    }

    void FatChainWalk::OnNext(Result result, std::optional<uint32_t> next)
    {
        if (result != Result::success)
            onDone(result, run);
        else if (++steps == geometry.clusterCount)
            onDone(Result::corrupted, run);
        else if (next == run.ClusterAt(run.End()))
        {
            ++run.length;
            Step();
        }
        else if (run.Covers(target))
            onDone(Result::success, run);
        else
            StartNextRun(next);
    }

    void FatChainWalk::StartNextRun(std::optional<uint32_t> next)
    {
        if (next.has_value())
        {
            run = FatExtent{ run.End(), *next, 1 };
            Step();
        }
        else
            onDone(Result::corrupted, run);
    }

    bool FatChainWalk::Covered() const
    {
        return run.Covers(target) && run.Covers(limit);
    }
}
