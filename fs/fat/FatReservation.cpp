#include "fs/fat/FatReservation.hpp"

namespace fs
{
    bool FatReservation::Contains(uint32_t cluster) const
    {
        return cluster >= first && cluster - first < count;
    }

    bool FatReservation::operator==(const FatReservation& other) const = default;

    FatReservations::FatReservations() = default;
}
