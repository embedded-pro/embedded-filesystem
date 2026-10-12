#include "fs/common/HandlePool.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    PooledHandle::PooledHandle()
        : access([this]
              {
                  UnReferenced();
              })
    {}

    void PooledHandle::Release()
    {
        really_assert(inUse && !access.Referenced());

        inUse = false;
    }

    bool PooledHandle::InUse() const
    {
        return inUse;
    }
}
