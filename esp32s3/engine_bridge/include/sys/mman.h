#ifndef PAL_CORES3SE_ENGINE_BRIDGE_SYS_MMAN_H
#define PAL_CORES3SE_ENGINE_BRIDGE_SYS_MMAN_H

#include <stddef.h>

#define PROT_READ 0x1
#define MAP_PRIVATE 0x02
#define MAP_FAILED ((void *)-1)

void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);
int munmap(void *addr, size_t length);

#endif
