#ifndef SPIKE_FAT_READER_FAT_READER_WALK_HPP
#define SPIKE_FAT_READER_FAT_READER_WALK_HPP

#include "fs/common/Path.hpp"
#include "fs/interfaces/Result.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "spike/fat_reader/FatReaderScan.hpp"
#include <optional>

namespace spike
{
    class FatWalk
    {
    public:
        FatWalk(const FatLayout& layout, FatScan& scan);
        FatWalk(const FatWalk& other) = delete;
        FatWalk& operator=(const FatWalk& other) = delete;
        ~FatWalk() = default;

        void Find(infra::BoundedConstString path, const infra::Function<void(fs::Result result, const FatEntry& entry)>& onDone);

    private:
        void NextComponent();
        void Scan();
        void Visited(fs::Result result, std::optional<FatEntry> entry);
        bool Matches(const FatEntry& entry) const;

    private:
        const FatLayout& layout;
        FatScan& scan;
        fs::PathComponents components{ "/" };
        infra::BoundedConstString component;
        FatPosition position;
        infra::AutoResetFunction<void(fs::Result result, const FatEntry& entry)> onDone;
    };
}

#endif
