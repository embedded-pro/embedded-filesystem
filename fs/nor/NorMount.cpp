#include "fs/nor/NorMount.hpp"
#include "fs/nor/NorInodeRecord.hpp"
#include "fs/nor/NorLayout.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    namespace
    {
        constexpr uint32_t superblocks = 2;
    }

    NorMount::NorMount(hal::Flash& flash, const NorConfig& config, NorVolumeState& volume, infra::ByteRange buffer, const infra::Function<void(Result result)>& onDone)
        : flash(flash)
        , config(config)
        , volume(volume)
        , buffer(buffer)
        , onDone(onDone)
    {
        really_assert(buffer.size() >= std::max(norSuperblockBytes, norCheckpointFixedBytes + norCheckpointCrcBytes) && buffer.size() >= NorHeaderSize(config.programUnit));
    }

    NorMount::~NorMount() = default;

    void NorMount::Start()
    {
        volume.geometry = MakeNorGeometry(config, flash);
        if (!volume.geometry.has_value())
        {
            Done(Result::unsupported);
            return;
        }

        volume.blocks.Reset(volume.geometry->blocks);
        volume.eraseCounts.Reset(volume.geometry->blocks);
        ReadSuperblock(0);
    }

    void NorMount::ReadSuperblock(uint32_t block)
    {
        onSuperblock = [this, block]
        {
            OnSuperblock(block);
        };
        flash.ReadBuffer(infra::Head(buffer, norSuperblockBytes), volume.geometry->AddressOfBlock(block), [this]
            {
                onSuperblock();
            });
    }

    void NorMount::OnSuperblock(uint32_t block)
    {
        auto superblock = ParseNorSuperblock(infra::Head(buffer, norSuperblockBytes));
        if (superblock.has_value() && Check(*superblock) == Result::success)
            ScanHeaders();
        else if (superblock.has_value())
            Done(Check(*superblock));
        else if (block == 0)
            ReadSuperblock(1);
        else
            Done(Result::corrupted);
    }

    Result NorMount::Check(const NorSuperblock& superblock) const
    {
        if (superblock.sectorsPerBlock != config.sectorsPerBlock || superblock.programUnit != config.programUnit || superblock.nodeSize != config.nodeSize || superblock.inlineBytes != config.inlineBytes || superblock.maxNameBytes != config.maxNameBytes || superblock.blocks > config.maxBlocks)
            return Result::unsupported;

        if (superblock.sectorSize != volume.geometry->sectorSize || superblock.blocks != volume.geometry->blocks)
            return Result::corrupted;

        return Result::success;
    }

    void NorMount::ScanHeaders()
    {
        step.emplace<NorHeaderScan>(flash, volume, buffer, summary, [this]
                {
                    LoadCheckpoint();
                })
            .Start();
    }

    void NorMount::LoadCheckpoint()
    {
        step.emplace<NorCheckpointLoad>(flash, volume, buffer, summary, [this](Result loaded)
                {
                    if (loaded == Result::success)
                        ScanJournal();
                    else
                        Done(loaded);
                })
            .Start();
    }

    void NorMount::ScanJournal()
    {
        step.emplace<NorJournalScan>(flash, volume, buffer, NorInodeRecordBytes(config), [this](Result scanned)
                {
                    if (scanned == Result::success)
                        Finish();
                    else
                        Done(scanned);
                })
            .Start();
    }

    void NorMount::Finish()
    {
        for (uint32_t block = 0; block != superblocks; ++block)
            volume.blocks.Set(block, NorBlockState::inUse);

        volume.eraseCounts.Normalize(superblocks);
        volume.nextSequence = summary.highestSequence + 1;
        volume.inodeTable = NorMapRoot{ volume.checkpoint.inodeTableRoot, volume.checkpoint.inodeTableHeight };
        Done(Result::success);
    }

    void NorMount::Done(Result mountResult)
    {
        result = mountResult;
        infra::EventDispatcher::Instance().Schedule([this]
            {
                onDone(result);
            });
    }
}
