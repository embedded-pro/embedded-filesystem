#include "fs/common/DeferredCompletions.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <utility>

namespace fs
{
    DeferredCompletions::DeferredCompletions(infra::BoundedDeque<Completion>& completions)
        : completions(completions)
    {}

    bool DeferredCompletions::Full() const
    {
        return completions.full();
    }

    void DeferredCompletions::Defer(const Completion& completion)
    {
        really_assert(!Full());

        completions.push_back(completion);

        if (!scheduled)
        {
            scheduled = true;
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    Run();
                });
        }
    }

    void DeferredCompletions::Run()
    {
        scheduled = false;

        for (auto pending = completions.size(); pending != 0; --pending)
        {
            auto completion = std::move(completions.front());
            completions.pop_front();
            completion();
        }
    }
}
