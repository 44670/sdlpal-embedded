#ifndef FF_H
#define FF_H

#include <stdint.h>
#include <stdio.h>

typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef uint32_t FSIZE_t;
typedef int FRESULT;

typedef struct FIL {
    FILE *fp;
    FSIZE_t size;
} FIL;

#define FR_OK 0
#define FR_NO_FILE 4
#define FR_NO_PATH 5
#define FR_DENIED 7
#define FR_WRITE_PROTECTED 10
#define FR_INVALID_NAME 6
#define FR_INVALID_PARAMETER 19

#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_OPEN_EXISTING 0x00
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_APPEND 0x30

FRESULT f_open(FIL *file, const char *path, unsigned char mode);
FRESULT f_read(FIL *file, void *dst, UINT size, UINT *read_bytes);
FRESULT f_write(FIL *file, const void *src, UINT size, UINT *written_bytes);
FRESULT f_lseek(FIL *file, FSIZE_t offset);
FRESULT f_close(FIL *file);
FRESULT f_sync(FIL *file);
FSIZE_t f_size(FIL *file);
FSIZE_t f_tell(FIL *file);

#endif
