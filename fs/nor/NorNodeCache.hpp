#ifndef FS_NOR_NOR_NODE_CACHE_HPP
#define FS_NOR_NOR_NODE_CACHE_HPP

#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorNode.hpp"
#include "hal/interfaces/Flash.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/MemoryRange.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class NorNodeCache
    {
    public:
        struct Slot
        {
            uint32_t address{ 0 };
            uint32_t lastUse{ 0 };
            bool modified{ false };
            bool pinned{ false };
        };

        NorNodeCache(hal::Flash& flash, infra::MemoryRange<Slot> slots, infra::ByteRange buffers);
        NorNodeCache(const NorNodeCache& other) = delete;
        NorNodeCache& operator=(const NorNodeCache& other) = delete;
        ~NorNodeCache() = default;

        std::size_t Capacity() const;
        uint32_t NodeSize() const;
        std::size_t ModifiedNodes() const;
        uint32_t ReleasedNodes() const;
        void ClearReleasedNodes();
        bool HasRoom() const;

        std::optional<std::size_t> Find(uint32_t address) const;
        void Load(uint32_t address, const infra::Function<void(std::optional<std::size_t> slot)>& onDone);
        void Pin(std::size_t slot);
        void UnpinAll() const;
        void Clear();

        uint32_t AddressOf(std::size_t slot) const;
        bool IsModified(std::size_t slot) const;
        infra::ConstByteRange Node(std::size_t slot) const;
        infra::ByteRange Modify(std::size_t slot);
        std::size_t Create(const NorNodeHeader& header);
        void Drop(std::size_t slot);
        void Written(std::size_t slot, uint32_t address);

    private:
        std::optional<std::size_t> Room() const;
        void Touch(std::size_t slot);
        infra::ByteRange BufferOf(std::size_t slot) const;
        void Loaded();

    private:
        hal::Flash& flash;
        infra::MemoryRange<Slot> slots;
        infra::ByteRange buffers;
        uint32_t nodeSize;
        uint32_t uses{ 0 };
        uint32_t released{ 0 };
        std::size_t loading{ 0 };
        uint32_t loadingAddress{ 0 };
        infra::AutoResetFunction<void(std::optional<std::size_t> slot)> onLoaded;
    };
}

#endif
