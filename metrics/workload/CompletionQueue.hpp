#ifndef METRICS_WORKLOAD_COMPLETION_QUEUE_HPP
#define METRICS_WORKLOAD_COMPLETION_QUEUE_HPP

#include <deque>
#include <functional>

namespace metrics
{
    class CompletionQueue
    {
    public:
        CompletionQueue() = default;
        CompletionQueue(const CompletionQueue& other) = delete;
        CompletionQueue& operator=(const CompletionQueue& other) = delete;

        void Defer(const std::function<void()>& action);
        bool Empty() const;

    private:
        void RunFirst();

    private:
        std::deque<std::function<void()>> actions;
    };
}

#endif
