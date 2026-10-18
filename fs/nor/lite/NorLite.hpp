#ifndef FS_NOR_LITE_NOR_LITE_HPP
#define FS_NOR_LITE_NOR_LITE_HPP

#include "fs/common/HandlePool.hpp"
#include "fs/common/LiteLifecycle.hpp"
#include "fs/common/OperationQueue.hpp"
#include "fs/interfaces/FileSystem.hpp"
#include "fs/nor/NorConfig.hpp"
#include "fs/nor/NorCounters.hpp"
#include "fs/nor/NorFile.hpp"
#include "fs/nor/NorForegroundLeveling.hpp"
#include "fs/nor/NorOperations.hpp"
#include "fs/nor/NorPathSlot.hpp"
#include "fs/nor/NorVolume.hpp"
#include "fs/nor/NorVolumeMount.hpp"
#include "hal/interfaces/Flash.hpp"
#include "infra/util/Function.hpp"
#include "infra/util/SharedPtr.hpp"
#include <cstddef>
#include <optional>
#include <variant>

namespace fs
{
    using NorLitePathCall = std::variant<NorStatCall, NorOpenCall>;

    class NorLite
        : public FileSystem
        , private NorVolumeMount<NorLite>
    {
    public:
        template<NorConfig Config, std::size_t Files>
        class WithStorage;

        NorCounters Counters() const;

        void Mount(const infra::Function<void(Result result)>& onDone) override;
        void Unmount(const infra::Function<void(Result result)>& onDone) override;
        void Open(infra::BoundedConstString path, OpenMode mode, const infra::Function<void(Result result, infra::SharedPtr<File> file)>& onDone) override;
        void OpenDirectory(infra::BoundedConstString path, const infra::Function<void(Result result, infra::SharedPtr<Directory> directory)>& onDone) override;
        void Stat(infra::BoundedConstString path, const infra::Function<void(Result result, const EntryInfo& info)>& onDone) override;
        void Remove(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone) override;
        void Rename(infra::BoundedConstString from, infra::BoundedConstString to, const infra::Function<void(Result result)>& onDone) override;
        void MakeDirectory(infra::BoundedConstString path, const infra::Function<void(Result result)>& onDone) override;
        void Usage(const infra::Function<void(Result result, VolumeUsage usage)>& onDone) override;
        bool Idle() const override;

    protected:
        NorLite(const NorVolumeParts& parts, const NorConfig& config);
        NorLite(const NorLite& other) = delete;
        NorLite& operator=(const NorLite& other) = delete;
        ~NorLite();

        NorVolume& Volume();

    private:
        friend class NorVolumeMount<NorLite>;

        virtual infra::SharedPtr<NorFile> AllocateFile() = 0;

        Result Unsupported() const;
        Result Refusal(infra::BoundedConstString path) const;
        void Start(NorLitePathCall&& pathCall);
        void Run();

        void BeginChange(const infra::Function<void(Result result)>& onDone);
        void Mounted(Result result);
        void Entered(Result result);

    private:
        NorVolume volume;
        NorForegroundLeveling leveling;
        OperationQueue::Claimer lifecycleClaimer;
        OperationQueue::Claimer pathClaimer;
        std::optional<NorLitePathCall> pathCall;
        LiteLifecycle lifecycle;
        infra::Function<void(Result result)> onChanged;
    };

    template<NorConfig Config, std::size_t Files>
    class NorLite::WithStorage
        : private detail::NorVolumeStorage<Config, Files + 4>
        , public NorLite
    {
        static_assert(IsValidNorConfig(Config));
        static_assert(Files > 0);

    public:
        explicit WithStorage(hal::Flash& flash)
            : NorLite(NorVolumeParts{ flash, this->deferredStorage, infra::MakeRange(this->blockStateStorage), infra::MakeRange(this->eraseCountStorage), infra::MakeRange(this->nodeBlockStorage), infra::MakeRange(this->journalStorage), infra::MakeRange(this->nodeStorage), infra::MakeRange(this->nodeSlotStorage), operations }, Config)
        {}

    private:
        static constexpr NorOperations operations{};

        infra::SharedPtr<NorFile> AllocateFile() override
        {
            return files.Allocate();
        }

    private:
        HandlePool<NorFile::WithBuffer<NorFileBufferBytes(Config)>, Files> files{ Volume() };
    };
}

#endif
