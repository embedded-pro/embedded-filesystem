#ifndef FS_COMMON_DEFERRED_COMPLETIONS_HPP
#define FS_COMMON_DEFERRED_COMPLETIONS_HPP

#include "infra/util/BoundedDeque.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/WithStorage.hpp"
#include <cstddef>

#ifndef FS_DEFERRED_COMPLETION_EXTRA_SIZE
#define FS_DEFERRED_COMPLETION_EXTRA_SIZE (INFRA_DEFAULT_FUNCTION_EXTRA_SIZE + 2 * sizeof(void*))
#endif

namespace fs
{
    class DeferredCompletions
    {
    public:
        using Completion = infra::Function<void(), FS_DEFERRED_COMPLETION_EXTRA_SIZE>;

        template<std::size_t Size>
        using WithMaxSize = infra::WithStorage<DeferredCompletions, typename infra::BoundedDeque<Completion>::template WithMaxSize<Size>>;

        explicit DeferredCompletions(infra::BoundedDeque<Completion>& completions);
        DeferredCompletions(const DeferredCompletions& other) = delete;
        DeferredCompletions& operator=(const DeferredCompletions& other) = delete;
        ~DeferredCompletions() = default;

        bool Full() const;
        void Defer(const Completion& completion);

    private:
        void Run();

    private:
        infra::BoundedDeque<Completion>& completions;
        bool scheduled{ false };
    };
}

#endif
