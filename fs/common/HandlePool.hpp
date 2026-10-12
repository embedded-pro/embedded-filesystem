#ifndef FS_COMMON_HANDLE_POOL_HPP
#define FS_COMMON_HANDLE_POOL_HPP

#include "infra/util/BoundedVector.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstddef>
#include <type_traits>

namespace fs
{
    template<class Handle, std::size_t Size>
    class HandlePool;

    class PooledHandle
    {
    protected:
        PooledHandle();
        PooledHandle(const PooledHandle& other) = delete;
        PooledHandle& operator=(const PooledHandle& other) = delete;
        ~PooledHandle() = default;

        void Release();
        virtual void UnReferenced() = 0;

    public:
        bool InUse() const;

    private:
        template<class Handle, std::size_t Size>
        friend class HandlePool;

        infra::AccessedBySharedPtr access;
        bool inUse{ false };
    };

    template<class Handle, std::size_t Size>
    class HandlePool
    {
        static_assert(std::is_base_of_v<PooledHandle, Handle>);
        static_assert(Size > 0);

    public:
        template<class... Args>
        explicit HandlePool(Args&... args);
        HandlePool(const HandlePool& other) = delete;
        HandlePool& operator=(const HandlePool& other) = delete;
        ~HandlePool() = default;

        infra::SharedPtr<Handle> Allocate();
        bool Exhausted() const;

    private:
        typename infra::BoundedVector<Handle>::template WithMaxSize<Size> handles;
    };

    ////    Implementation    ////

    template<class Handle, std::size_t Size>
    template<class... Args>
    HandlePool<Handle, Size>::HandlePool(Args&... args)
    {
        for (std::size_t index = 0; index != Size; ++index)
            handles.emplace_back(args...);
    }

    template<class Handle, std::size_t Size>
    infra::SharedPtr<Handle> HandlePool<Handle, Size>::Allocate()
    {
        for (auto& handle : handles)
        {
            PooledHandle& pooled = handle;

            if (!pooled.inUse)
            {
                pooled.inUse = true;
                return pooled.access.MakeShared(handle);
            }
        }

        return nullptr;
    }

    template<class Handle, std::size_t Size>
    bool HandlePool<Handle, Size>::Exhausted() const
    {
        for (const auto& handle : handles)
            if (!handle.InUse())
                return false;

        return true;
    }
}

#endif
