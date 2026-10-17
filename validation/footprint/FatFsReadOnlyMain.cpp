#include "ff.h"
#include "infra/util/StaticStorage.hpp"
#include "validation/firmware/baselines/FatFsDisk.hpp"
#include "validation/footprint/Platform.hpp"

int main()
{
    static validation::footprint::Platform platform;
    static infra::StaticStorage<validation::FatFsDisk> disk;
    disk.Construct(platform.blockDevice, 0);
    static FATFS volume;
    static FIL file;
    auto& buffer = platform.buffer;
    UINT bytes = 0;

    f_mount(&volume, "", 1);
    f_open(&file, "/log.txt", FA_READ);
    f_lseek(&file, 0);
    f_read(&file, buffer.data(), static_cast<UINT>(buffer.size()), &bytes);
    f_close(&file);

    DIR directory;
    FILINFO information;
    f_opendir(&directory, "/");
    f_readdir(&directory, &information);
    f_closedir(&directory);
    f_stat("/log.txt", &information);
    f_unmount("");

    platform.Run();
}
