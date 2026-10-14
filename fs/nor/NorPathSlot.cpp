#include "fs/nor/NorPathSlot.hpp"

namespace fs
{
    NorPathSlot::NorPathSlot(OperationQueue& queue)
        : claimer(queue)
    {}

    NorPathSlot::~NorPathSlot() = default;

    bool NorPathSlot::InUse() const
    {
        return call.has_value();
    }
}
