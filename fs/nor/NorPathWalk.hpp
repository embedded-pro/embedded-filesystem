#ifndef FS_NOR_NOR_PATH_WALK_HPP
#define FS_NOR_NOR_PATH_WALK_HPP

#include "fs/common/Path.hpp"
#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorInodeRead.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorNameLookup.hpp"
#include "fs/nor/NorNode.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <optional>

namespace fs
{
    struct NorFoundInode
    {
        uint32_t inode{ 0 };
        NorInodeType type{ NorInodeType::directory };
        uint32_t size{ 0 };
        NorMapRoot map;
    };

    class NorPathWalk
    {
    public:
        static constexpr uint32_t rootInode = 1;

        NorPathWalk(NorInodeRead& inodeRead, NorNameLookup& nameLookup);
        NorPathWalk(const NorPathWalk& other) = delete;
        NorPathWalk& operator=(const NorPathWalk& other) = delete;
        ~NorPathWalk() = default;

        void Find(const NorMapRoot& inodeTable, infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone);
        const NorFoundInode& Found() const;
        infra::BoundedConstString FoundName() const;

    private:
        void RootRead(Result result);
        void LookUp();
        void LookedUp(Result result);

    private:
        NorInodeRead& inodeRead;
        NorNameLookup& nameLookup;
        NorMapRoot inodeTable;
        std::optional<PathComponents> components;
        infra::BoundedConstString component;
        NorFoundInode found;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
