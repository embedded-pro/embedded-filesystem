#ifndef FS_FAT_FAT_FREE_RUNS_HPP
#define FS_FAT_FAT_FREE_RUNS_HPP

#include "fs/fat/FatAllocationUnits.hpp"
#include "fs/fat/FatReservation.hpp"
#include "infra/util/MemoryRange.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    class FatFreeRuns
    {
    public:
        explicit FatFreeRuns(infra::MemoryRange<FatReservation> storage);
        FatFreeRuns(const FatFreeRuns& other) = delete;
        FatFreeRuns& operator=(const FatFreeRuns& other) = delete;
        ~FatFreeRuns();

        void Clear();
        void Extend(uint32_t cluster);
        void Close();
        void Take(uint32_t cluster);
        std::optional<uint32_t> WholeUnit(const FatAllocationUnits& units, uint32_t from, const FatReservations& reservations) const;

    private:
        infra::MemoryRange<FatReservation> storage;
        FatReservation extending;
    };
}

#endif
