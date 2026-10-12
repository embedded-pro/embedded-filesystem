#include "fs/fat/FatPathSlot.hpp"

namespace fs
{
    FatPathSlot::FatPathSlot(OperationQueue& queue)
        : claimer(queue)
    {}

    FatPathSlot::~FatPathSlot() = default;

    bool FatPathSlot::InUse() const
    {
        return !std::holds_alternative<std::monostate>(call);
    }
}
