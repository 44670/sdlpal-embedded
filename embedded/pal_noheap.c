#include <stddef.h>

static void pal_noheap_trap(void)
{
#if defined(__GNUC__)
    __builtin_trap();
#else
    for (;;) {
    }
#endif
}

void *__wrap_malloc(size_t size)
{
    (void)size;
    pal_noheap_trap();
    return NULL;
}

void *__wrap_calloc(size_t count, size_t size)
{
    (void)count;
    (void)size;
    pal_noheap_trap();
    return NULL;
}

void *__wrap_realloc(void *ptr, size_t size)
{
    (void)ptr;
    (void)size;
    pal_noheap_trap();
    return NULL;
}

void __wrap_free(void *ptr)
{
    (void)ptr;
    pal_noheap_trap();
}
