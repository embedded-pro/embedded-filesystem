#ifndef FS_COMMON_FIBER_HPP
#define FS_COMMON_FIBER_HPP

#include "infra/event/ClaimableResource.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstddef>

namespace fs
{
    class Fiber
    {
    public:
#if defined(__ARM_ARCH_PROFILE) && __ARM_ARCH_PROFILE == 'M'
        static constexpr std::size_t defaultStackBytes = 1024;
#else
        static constexpr std::size_t defaultStackBytes = 65536;
#endif

        explicit Fiber(infra::ByteRange stack);
        Fiber(const Fiber& other) = delete;
        Fiber& operator=(const Fiber& other) = delete;
        ~Fiber();

        void Run(infra::ClaimableResource::Claimer& claimer, const infra::Function<void()>& body);
        void Suspend();
        void Resume();
        std::size_t Peak() const;

    private:
        friend class FiberEntry;

        void SwitchIn();
        void RunBody();

    private:
        infra::ByteRange stack;
        infra::Function<void()> body;
        infra::ClaimableResource::Claimer* claimer{ nullptr };
        bool finished{ true };
        void* fiber{ nullptr };
        void* caller{ nullptr };
    };
}

#endif
