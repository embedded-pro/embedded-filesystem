#include "fs/fat/FatRunLocator.hpp"

namespace fs
{
    FatRunLocator::FatRunLocator(FatChainWalk& chain)
        : chain(chain)
    {}

    FatRunLocator::~FatRunLocator() = default;

    void FatRunLocator::Locate(FatExtentCache& extents, uint32_t index, uint32_t limit, const infra::Function<void(Result result, const FatExtent& run)>& onDone)
    {
        auto run = extents.Covering(index);

        if (run && run->Covers(limit))
        {
            onDone(Result::success, *run);
            return;
        }

        this->extents = &extents;
        this->onDone = onDone;
        chain.Walk(run ? *run : *extents.NearestBefore(index), index, limit, [this](Result result, const FatExtent& found)
            {
                if (result == Result::success)
                    this->extents->Add(found);

                this->onDone(result, found);
            });
    }
}
