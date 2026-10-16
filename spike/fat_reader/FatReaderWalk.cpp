#include "spike/fat_reader/FatReaderWalk.hpp"
#include "fs/fat/FatName.hpp"

namespace spike
{
    FatWalk::FatWalk(const FatLayout& layout, FatScan& scan)
        : layout(layout)
        , scan(scan)
    {}

    void FatWalk::Find(infra::BoundedConstString path, const infra::Function<void(fs::Result result, const FatEntry& entry)>& onDone)
    {
        this->onDone = onDone;
        components = fs::PathComponents(path);
        position = scan.Root();
        NextComponent();
    }

    void FatWalk::NextComponent()
    {
        auto next = components.Next();

        if (!next.has_value())
        {
            onDone(fs::Result::success, FatEntry{ {}, {}, position.cluster, 0, true });
            return;
        }

        component = *next;

        if (auto validation = fs::ValidateFatName(component); validation != fs::Result::success)
            onDone(validation, FatEntry{});
        else
            Scan();
    }

    void FatWalk::Scan()
    {
        scan.Next(position, [this](fs::Result result, std::optional<FatEntry> entry)
            {
                Visited(result, entry);
            });
    }

    void FatWalk::Visited(fs::Result result, std::optional<FatEntry> entry)
    {
        if (result != fs::Result::success || !entry.has_value())
            onDone(result == fs::Result::success ? fs::Result::notFound : result, FatEntry{});
        else if (!Matches(*entry))
            Scan();
        else if (components.Done())
            onDone(fs::Result::success, *entry);
        else if (!entry->directory)
            onDone(fs::Result::notADirectory, FatEntry{});
        else if (!IsCluster(layout, entry->firstCluster))
            onDone(fs::Result::corrupted, FatEntry{});
        else
        {
            position = FatPosition{ entry->firstCluster, 0, 0 };
            NextComponent();
        }
    }

    bool FatWalk::Matches(const FatEntry& entry) const
    {
        return fs::EqualIgnoringAsciiCase(entry.name, component) || fs::EqualIgnoringAsciiCase(entry.shortName, component);
    }
}
