#include "fs/nor/NorNodeCache.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        uint32_t NodeSizeOf(infra::MemoryRange<NorNodeCache::Slot> slots, infra::ByteRange buffers)
        {
            really_assert(!slots.empty() && slots.size() < norModifiedNodeTag && buffers.size() % slots.size() == 0);

            return static_cast<uint32_t>(buffers.size() / slots.size());
        }
    }

    NorNodeCache::NorNodeCache(hal::Flash& flash, infra::MemoryRange<Slot> slots, infra::ByteRange buffers)
        : flash(flash)
        , slots(slots)
        , buffers(buffers)
        , nodeSize(NodeSizeOf(slots, buffers))
    {}

    NorNodeCache::~NorNodeCache() = default;

    std::size_t NorNodeCache::Capacity() const
    {
        return slots.size();
    }

    uint32_t NorNodeCache::NodeSize() const
    {
        return nodeSize;
    }

    std::size_t NorNodeCache::ModifiedNodes() const
    {
        return static_cast<std::size_t>(std::ranges::count_if(slots, [](const Slot& slot)
            {
                return slot.modified;
            }));
    }

    uint32_t NorNodeCache::ReleasedNodes() const
    {
        return released;
    }

    void NorNodeCache::ClearReleasedNodes()
    {
        released = 0;
    }

    bool NorNodeCache::HasRoom() const
    {
        return Room().has_value();
    }

    std::optional<std::size_t> NorNodeCache::Find(uint32_t address) const
    {
        if ((address & norModifiedNodeTag) != 0)
        {
            auto slot = static_cast<std::size_t>(address & ~norModifiedNodeTag);
            if (slot < slots.size() && slots[slot].modified)
                return slot;

            return std::nullopt;
        }

        if (address == 0)
            return std::nullopt;

        for (std::size_t slot = 0; slot != slots.size(); ++slot)
            if (!slots[slot].modified && slots[slot].address == address)
                return slot;

        return std::nullopt;
    }

    void NorNodeCache::Load(uint32_t address, const infra::Function<void(std::optional<std::size_t> slot)>& onDone)
    {
        auto room = Room();
        really_assert(room.has_value() && address != 0 && (address & norModifiedNodeTag) == 0);

        loading = room.value_or(0);
        loadingAddress = address;
        slots[loading] = Slot{};
        onLoaded = onDone;
        flash.ReadBuffer(BufferOf(loading), address, [this]
            {
                Loaded();
            });
    }

    void NorNodeCache::Pin(std::size_t slot)
    {
        slots[slot].pinned = true;
        Touch(slot);
    }

    void NorNodeCache::UnpinAll() const
    {
        for (auto& slot : slots)
            slot.pinned = false;
    }

    void NorNodeCache::Clear()
    {
        std::ranges::fill(slots, Slot{});
        released = 0;
    }

    uint32_t NorNodeCache::AddressOf(std::size_t slot) const
    {
        return slots[slot].address;
    }

    bool NorNodeCache::IsModified(std::size_t slot) const
    {
        return slots[slot].modified;
    }

    infra::ConstByteRange NorNodeCache::Node(std::size_t slot) const
    {
        return BufferOf(slot);
    }

    infra::ByteRange NorNodeCache::Modify(std::size_t slot)
    {
        really_assert(slots[slot].address != 0);

        if (!slots[slot].modified)
        {
            ++released;
            slots[slot].address = norModifiedNodeTag | static_cast<uint32_t>(slot);
            slots[slot].modified = true;
        }

        return BufferOf(slot);
    }

    std::size_t NorNodeCache::Create(const NorNodeHeader& header)
    {
        auto room = Room();
        really_assert(room.has_value());

        auto slot = room.value_or(0);
        auto buffer = BufferOf(slot);
        std::ranges::fill(buffer, uint8_t{ 0 });
        WriteNorNodeHeader(header, buffer);
        slots[slot] = Slot{ norModifiedNodeTag | static_cast<uint32_t>(slot), 0, true, false };
        Touch(slot);
        return slot;
    }

    void NorNodeCache::Drop(std::size_t slot)
    {
        if (!slots[slot].modified)
            ++released;

        slots[slot] = Slot{};
    }

    void NorNodeCache::Written(std::size_t slot, uint32_t address)
    {
        really_assert(slots[slot].modified && address != 0 && (address & norModifiedNodeTag) == 0);

        slots[slot].address = address;
        slots[slot].modified = false;
        Touch(slot);
    }

    std::optional<std::size_t> NorNodeCache::Room() const
    {
        std::optional<std::size_t> room;
        uint32_t oldest = 0;

        for (std::size_t slot = 0; slot != slots.size(); ++slot)
        {
            const auto& candidate = slots[slot];
            if (candidate.address == 0)
                return slot;

            if (!candidate.modified && !candidate.pinned && (!room.has_value() || candidate.lastUse < oldest))
            {
                room = slot;
                oldest = candidate.lastUse;
            }
        }

        return room;
    }

    void NorNodeCache::Touch(std::size_t slot)
    {
        slots[slot].lastUse = ++uses;
    }

    infra::ByteRange NorNodeCache::BufferOf(std::size_t slot) const
    {
        return infra::Head(infra::DiscardHead(buffers, slot * nodeSize), nodeSize);
    }

    void NorNodeCache::Loaded()
    {
        if (!OpenNorNode(BufferOf(loading)).has_value())
        {
            onLoaded(std::nullopt);
            return;
        }

        slots[loading].address = loadingAddress;
        Touch(loading);
        onLoaded(loading);
    }
}
