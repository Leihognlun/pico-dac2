#pragma once
#include <stddef.h>
typedef unsigned UINT;
typedef int FRESULT;
typedef struct { int unused; } FATFS;
typedef struct { int unused; } FIL;
typedef struct { int unused; } DIR;
typedef struct { unsigned char fattrib; char fname[64]; } FILINFO;
#define AM_DIR 0x10
#define FR_OK 0
#define FA_READ 1
FRESULT f_mount(FATFS *fs, const char *path, int mount);
FRESULT f_open(FIL *file, const char *path, int mode);
FRESULT f_read(FIL *file, void *buffer, UINT size, UINT *count);
FRESULT f_close(FIL *file);
FRESULT f_opendir(DIR*,const char*);
FRESULT f_readdir(DIR*,FILINFO*);
FRESULT f_closedir(DIR*);
