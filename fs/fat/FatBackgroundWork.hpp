#ifndef FS_FAT_FAT_BACKGROUND_WORK_HPP
#define FS_FAT_FAT_BACKGROUND_WORK_HPP

namespace fs
{
    class FatBackgroundWork
    {
    protected:
        FatBackgroundWork() = default;
        FatBackgroundWork(const FatBackgroundWork& other) = delete;
        FatBackgroundWork& operator=(const FatBackgroundWork& other) = delete;
        ~FatBackgroundWork() = default;

    public:
        virtual void Start() = 0;
        virtual void Stop() = 0;
        virtual void ForegroundClaiming() = 0;
        virtual void ForegroundReleased() = 0;
        virtual bool Scheduled() const = 0;
    };
}

#endif
