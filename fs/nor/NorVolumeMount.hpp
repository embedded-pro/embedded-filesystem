#ifndef FS_NOR_NOR_VOLUME_MOUNT_HPP
#define FS_NOR_NOR_VOLUME_MOUNT_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorVolume.hpp"
#include "fs/nor/NorVolumeState.hpp"
#include "infra/event/EventDispatcher.hpp"
#include <optional>

namespace fs
{
    template<class Owner>
    class NorVolumeMount
    {
    protected:
        NorVolumeMount() = default;
        NorVolumeMount(const NorVolumeMount& other) = delete;
        NorVolumeMount& operator=(const NorVolumeMount& other) = delete;
        ~NorVolumeMount() = default;

        void StartMount();

    private:
        void Mount();
        void Replay();

    private:
        std::optional<NorRecordPosition> replayCheckpointBefore;
    };

    template<class Owner>
    void NorVolumeMount<Owner>::StartMount()
    {
        replayCheckpointBefore = std::nullopt;
        Mount();
    }

    template<class Owner>
    void NorVolumeMount<Owner>::Mount()
    {
        NorVolume& volume = static_cast<Owner&>(*this).Volume();
        volume.Cache().Clear();
        volume.Operation().emplace<NorMount>(volume.Flash(), volume.Config(), volume.State(), volume.JournalBuffer(), [this](Result result)
                              {
                                  if (result == Result::success)
                                      Replay();
                                  else
                                      static_cast<Owner&>(*this).Mounted(result);
                              })
            .Start();
    }

    template<class Owner>
    void NorVolumeMount<Owner>::Replay()
    {
        NorVolume& volume = static_cast<Owner&>(*this).Volume();
        volume.Operation().emplace<NorReplay>(volume, replayCheckpointBefore, [this](Result result, std::optional<NorRecordPosition> overflow)
                              {
                                  if (!overflow.has_value())
                                      static_cast<Owner&>(*this).Mounted(result);
                                  else
                                  {
                                      replayCheckpointBefore = overflow;
                                      infra::EventDispatcher::Instance().Schedule([this]
                                          {
                                              Mount();
                                          });
                                  }
                              })
            .Start();
    }
}

#endif
