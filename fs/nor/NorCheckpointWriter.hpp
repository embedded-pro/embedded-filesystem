#ifndef FS_NOR_NOR_CHECKPOINT_WRITER_HPP
#define FS_NOR_NOR_CHECKPOINT_WRITER_HPP

#include "fs/common/Crc32.hpp"
#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorBlockAllocator.hpp"
#include "fs/nor/NorCheckpoint.hpp"
#include "fs/nor/NorLayout.hpp"
#include "fs/nor/NorNodeWriteBack.hpp"
#include "fs/nor/NorVolumeState.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    class NorVolume;

    class NorCheckpointWriter
    {
    public:
        explicit NorCheckpointWriter(NorVolume& volume);
        NorCheckpointWriter(const NorCheckpointWriter& other) = delete;
        NorCheckpointWriter& operator=(const NorCheckpointWriter& other) = delete;
        ~NorCheckpointWriter();

        void Write(const infra::Function<void(Result result)>& onDone);
        void WriteBefore(const NorRecordPosition& records, const infra::Function<void(Result result)>& onDone);

    private:
        void Start(const infra::Function<void(Result result)>& onDone);
        void WriteNextNode();
        void Allocate(NorBlockType type, NorWear wear);
        void NodeBlockAllocated(uint32_t block);
        void ProgramNode();
        void NodeProgrammed();
        void CheckpointBlockAllocated(uint32_t block);
        void ProgramNextChunk();
        void ChunkProgrammed();
        void Program(infra::ConstByteRange bytes, uint32_t address);
        void Commit();
        uint8_t ByteAt(uint32_t offset) const;
        bool IsInNewCheckpoint(uint32_t block) const;
        NorNodeWriteBack WriteBack() const;

    private:
        NorVolume& volume;
        std::size_t slot{ 0 };
        uint32_t block{ 0 };
        NorCheckpoint checkpoint;
        std::optional<NorRecordPosition> replayRecords;
        std::array<uint8_t, norCheckpointFixedBytes> fixedPart{};
        uint32_t nodeAddress{ 0 };
        uint32_t written{ 0 };
        uint32_t chunk{ 0 };
        bool collectionDue{ false };
        bool collect{ false };
        NorWear wear{ NorWear::least };
        NorBlockType programming{ NorBlockType::node };
        Crc32 crc;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
