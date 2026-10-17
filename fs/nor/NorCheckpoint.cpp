#include "fs/nor/NorCheckpoint.hpp"
#include "fs/common/Crc32.hpp"
#include "fs/common/LittleEndian.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace fs
{
    namespace
    {
        constexpr std::size_t blocksOffset = 24;

        uint32_t BlocksOf(infra::ConstByteRange checkpoint)
        {
            return LoadLittle32(checkpoint, blocksOffset);
        }

        uint32_t CrcOffset(uint32_t blocks)
        {
            return norCheckpointFixedBytes + NorCheckpointBitmapBytes(blocks);
        }

        uint32_t Crc(infra::ConstByteRange checkpoint, uint32_t blocks)
        {
            return Crc32Of(infra::Head(checkpoint, CrcOffset(blocks)));
        }

        uint8_t Bit(uint32_t block)
        {
            return static_cast<uint8_t>(1u << (block % 8));
        }

        std::size_t ByteOfBlock(uint32_t block)
        {
            return norCheckpointFixedBytes + block / 8;
        }
    }

    void SerializeNorCheckpoint(const NorCheckpoint& checkpoint, infra::ByteRange bytes)
    {
        really_assert(bytes.size() >= NorCheckpointBytes(checkpoint.blocks));

        std::ranges::fill(infra::Head(bytes, CrcOffset(checkpoint.blocks)), uint8_t{ 0 });
        SerializeNorCheckpointFixedPart(checkpoint, bytes);
    }

    void SerializeNorCheckpointFixedPart(const NorCheckpoint& checkpoint, infra::ByteRange bytes)
    {
        really_assert(bytes.size() >= norCheckpointFixedBytes);

        std::ranges::fill(infra::Head(bytes, norCheckpointFixedBytes), uint8_t{ 0 });
        StoreLittle32(bytes, 0, checkpoint.inodeTableRoot);
        bytes[4] = checkpoint.inodeTableHeight;
        StoreLittle32(bytes, 8, checkpoint.nextInode);
        StoreLittle32(bytes, 12, checkpoint.recordsBlock);
        StoreLittle32(bytes, 16, checkpoint.recordsOffset);
        StoreLittle32(bytes, 20, checkpoint.liveNodes);
        StoreLittle32(bytes, blocksOffset, checkpoint.blocks);
    }

    void MarkNorBlockInUse(infra::ByteRange checkpoint, uint32_t block)
    {
        really_assert(block < BlocksOf(checkpoint));

        checkpoint[ByteOfBlock(block)] |= Bit(block);
    }

    void SealNorCheckpoint(infra::ByteRange checkpoint)
    {
        auto blocks = BlocksOf(checkpoint);
        StoreLittle32(checkpoint, CrcOffset(blocks), Crc(checkpoint, blocks));
    }

    NorCheckpoint ParseNorCheckpointFixedPart(infra::ConstByteRange fixedPart)
    {
        really_assert(fixedPart.size() >= norCheckpointFixedBytes);

        return NorCheckpoint{ LoadLittle32(fixedPart, 0), fixedPart[4], LoadLittle32(fixedPart, 8), LoadLittle32(fixedPart, 12), LoadLittle32(fixedPart, 16), LoadLittle32(fixedPart, 20), BlocksOf(fixedPart) };
    }

    std::optional<NorCheckpoint> OpenNorCheckpoint(infra::ConstByteRange bytes)
    {
        really_assert(bytes.size() >= norCheckpointFixedBytes + norCheckpointCrcBytes);

        auto blocks = BlocksOf(bytes);
        if (blocks > (bytes.size() - norCheckpointFixedBytes - norCheckpointCrcBytes) * 8 || LoadLittle32(bytes, CrcOffset(blocks)) != Crc(bytes, blocks))
            return std::nullopt;

        return ParseNorCheckpointFixedPart(bytes);
    }

    bool IsNorBlockInUse(infra::ConstByteRange checkpoint, uint32_t block)
    {
        really_assert(block < BlocksOf(checkpoint));

        return (checkpoint[ByteOfBlock(block)] & Bit(block)) != 0;
    }
}
