#include "metrics/nor/NorTargets.hpp"

namespace metrics
{
    std::optional<Target> NorTargets::For(std::string_view metric, std::string_view) const
    {
        if (metric == "P-1")
            return AtLeast(1.0);

        return std::nullopt;
    }
}
