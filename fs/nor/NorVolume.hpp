#ifndef FS_NOR_NOR_VOLUME_HPP
#define FS_NOR_NOR_VOLUME_HPP

#include "fs/common/DeferredCompletions.hpp"
#include "fs/common/Deliver.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorConfig.hpp"
#include "fs/nor/NorFormatter.hpp"
#include "fs/nor/NorMount.hpp"
#include "fs/nor/NorVolumeState.hpp"
#include "hal/interfaces/Flash.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/ByteRange.hpp"
#include <variant>

namespace fs
{
    struct NorVolumeParts
    {
        hal::Flash& flash;
        DeferredCompletions& deferred;
        infra::ByteRange blockStates;
        infra::ByteRange eraseCounts;
        infra::ByteRange journalBuffer;
        infra::ByteRange nodeStorage;
    };

    using NorOperation = std::variant<std::monostate, NorFormatter, NorMount>;

    class NorVolume
    {
    public:
        NorVolume(const NorVolumeParts& parts, const NorConfig& config, infra::Duration idleDelay);
        NorVolume(const NorVolume& other) = delete;
        NorVolume& operator=(const NorVolume& other) = delete;
        ~NorVolume();

        hal::Flash& Flash() const;
        const NorConfig& Config() const;
        NorVolumeState& State();
        infra::ByteRange JournalBuffer() const;
        infra::ByteRange NodeStorage() const;
        OperationQueue& Queue();
        NorOperation& Operation();

        template<class Callback>
        void Defer(const Callback& onDone, Result result);

    private:
        hal::Flash& flash;
        DeferredCompletions& deferred;
        NorConfig config;
        NorVolumeState state;
        infra::ByteRange journalBuffer;
        infra::ByteRange nodeStorage;
        OperationQueue queue;
        NorOperation operation;
    };

    template<class Callback>
    void NorVolume::Defer(const Callback& onDone, Result result)
    {
        deferred.Defer([onDone, result]
            {
                detail::Deliver(onDone, result);
            });
    }
}

#endif
