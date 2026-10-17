#include "validation/firmware/cycles/subjects/BlockingSubject.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace validation
{
    namespace
    {
        const char* Terminate(std::array<char, BlockingSubject::maxPath + 1>& storage, infra::BoundedConstString path)
        {
            really_assert(path.size() <= BlockingSubject::maxPath);
            *std::copy(path.begin(), path.end(), storage.begin()) = 0;
            return storage.data();
        }
    }

    BlockingSubject::~BlockingSubject() = default;

    bool BlockingSubject::Idle() const
    {
        return true;
    }

    void BlockingSubject::Complete(const Done& onDone, int32_t status)
    {
        pending = onDone;
        pendingStatus = status;
        infra::EventDispatcher::Instance().Schedule([this]
            {
                pending(pendingStatus);
            });
    }

    const char* BlockingSubject::Terminated(infra::BoundedConstString text)
    {
        return Terminate(path, text);
    }

    const char* BlockingSubject::TerminatedTarget(infra::BoundedConstString text)
    {
        return Terminate(target, text);
    }
}
