#include "metrics/workload/CompletionQueue.hpp"
#include "infra/event/EventDispatcher.hpp"

namespace metrics
{
    void CompletionQueue::Defer(const std::function<void()>& action)
    {
        actions.push_back(action);
        infra::EventDispatcher::Instance().Schedule([this]
            {
                RunFirst();
            });
    }

    bool CompletionQueue::Empty() const
    {
        return actions.empty();
    }

    void CompletionQueue::RunFirst()
    {
        auto action = std::move(actions.front());
        actions.pop_front();
        action();
    }
}
