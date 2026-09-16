#pragma once
#include <stddef.h>
typedef unsigned UINT;
typedef int FRESULT;
typedef struct { int unused; } FATFS;
typedef struct { int unused; } FIL;
#define FR_OK 0
#define FA_READ 1
FRESULT f_mount(FATFS *fs, const char *path, int mount);
FRESULT f_open(FIL *file, const char *path, int mode);
FRESULT f_read(FIL *file, void *buffer, UINT size, UINT *count);
FRESULT f_close(FIL *file);
