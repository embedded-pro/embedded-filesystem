#include "ff.h"
#include "infra/util/StaticStorage.hpp"
#include "validation/firmware/baselines/FatFsDisk.hpp"
#include "validation/footprint/Platform.hpp"
#include <array>
#include <cstdint>

int main()
{
    constexpr bool tuned = FF_USE_FASTSEEK != 0;

    static validation::footprint::Platform platform;
    static infra::StaticStorage<validation::FatFsDisk> disk;
    disk.Construct(platform.blockDevice, tuned ? validation::footprint::allocationUnitBlocks : 0);
    static FATFS volume;
    static FIL file;
    auto& buffer = platform.buffer;
    UINT bytes = 0;

#if FF_USE_MKFS
    std::array<BYTE, FF_MAX_SS> workArea{};
    MKFS_PARM parameters{ FM_ANY, 0, validation::footprint::allocationUnitBlocks, 0, 0 };
    f_mkfs("", tuned ? &parameters : nullptr, workArea.data(), static_cast<UINT>(workArea.size()));
#endif
    f_mount(&volume, "", 1);
    f_open(&file, "/log.txt", FA_READ | FA_WRITE | FA_OPEN_ALWAYS);
#if FF_USE_FASTSEEK
    static std::array<DWORD, 64> linkMap{};
    linkMap[0] = static_cast<DWORD>(linkMap.size());
    file.cltbl = linkMap.data();
    f_lseek(&file, CREATE_LINKMAP);
#endif
#if FF_USE_EXPAND
    f_expand(&file, 4096, 1);
#endif
    f_write(&file, buffer.data(), static_cast<UINT>(buffer.size()), &bytes);
    f_lseek(&file, 0);
    f_read(&file, buffer.data(), static_cast<UINT>(buffer.size()), &bytes);
    f_truncate(&file);
    f_sync(&file);
    f_close(&file);

    DIR directory;
    FILINFO information;
    f_opendir(&directory, "/");
    f_readdir(&directory, &information);
    f_closedir(&directory);
    f_stat("/log.txt", &information);
    f_rename("/log.txt", "/old.txt");
    f_unlink("/old.txt");
    f_mkdir("/data");

    DWORD freeClusters = 0;
    FATFS* mounted = nullptr;
    f_getfree("", &freeClusters, &mounted);
    f_unmount("");

    platform.Run();
}
