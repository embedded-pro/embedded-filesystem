#ifndef FS_NOR_NOR_REPLAY_HPP
#define FS_NOR_NOR_REPLAY_HPP

#include "fs/common/CrcRecord.hpp"
#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorRecordReader.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    class NorReplay
    {
    public:
        NorReplay(NorVolume& volume, const infra::Function<void(Result result)>& onDone);
        NorReplay(const NorReplay& other) = delete;
        NorReplay& operator=(const NorReplay& other) = delete;
        ~NorReplay();

        void Start();

    private:
        void ReadRecord();
        void OnRecord(const std::optional<OpenedRecord>& record);
        void Applied(Result result);

    private:
        NorVolume& volume;
        NorRecordReader records;
        std::size_t index{ 0 };
        uint32_t offset{ 0 };
        bool linked{ false };
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
