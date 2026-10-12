#ifndef FS_FAT_FAT_RESERVATION_HPP
#define FS_FAT_FAT_RESERVATION_HPP

#include <cstdint>

namespace fs
{
    struct FatReservation
    {
        uint32_t first{ 0 };
        uint32_t count{ 0 };

        bool Contains(uint32_t cluster) const;
        bool operator==(const FatReservation& other) const;
    };

    class FatReservations
    {
    protected:
        FatReservations();
        FatReservations(const FatReservations& other) = delete;
        FatReservations& operator=(const FatReservations& other) = delete;
        ~FatReservations() = default;

    public:
        virtual bool Reserved(uint32_t cluster) const = 0;
    };
}

#endif
