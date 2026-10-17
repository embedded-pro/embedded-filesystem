#include "fs/fat/FatTimestamp.hpp"
#include "infra/timer/PartitionedTime.hpp"

namespace fs
{
    namespace
    {
        constexpr uint16_t firstYear = 1980;
        constexpr uint16_t lastYear = 2107;

        uint16_t Date(uint16_t year, uint8_t month, uint8_t day)
        {
            return static_cast<uint16_t>(((year - firstYear) << 9) | (month << 5) | day);
        }

        uint16_t Time(uint8_t hours, uint8_t minutes, uint8_t seconds)
        {
            return static_cast<uint16_t>((hours << 11) | (minutes << 5) | (seconds / 2));
        }
    }

    FatDateTime ToFatDateTime(infra::TimePoint time)
    {
        infra::PartitionedTime parts{ time };

        if (parts.years < firstYear)
            return FatDateTime{ Date(firstYear, 1, 1), Time(0, 0, 0) };

        if (parts.years > lastYear)
            return FatDateTime{ Date(lastYear, 12, 31), Time(23, 59, 58) };

        return FatDateTime{ Date(parts.years, parts.months, parts.days), Time(parts.hours, parts.minutes, parts.seconds) };
    }

    FatDateTime FatNow(uint32_t timerServiceId)
    {
        return ToFatDateTime(infra::Now(timerServiceId));
    }
}
