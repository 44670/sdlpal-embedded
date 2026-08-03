#include "pal_memory_profile.h"

#if defined(MEM_LEVEL2)

#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define PAL_MEM_LEVEL2_PSRAM EXT_RAM_BSS_ATTR __attribute__((aligned(4)))
#elif defined(__GNUC__)
#define PAL_MEM_LEVEL2_PSRAM \
   __attribute__((section(".bss.pal_psram"), aligned(4)))
#else
#define PAL_MEM_LEVEL2_PSRAM
#endif

typedef struct tagPALMEMORYARENA
{
   uint8_t *base;
   size_t capacity;
   size_t used;
} PALMEMORYARENA;

/*
 * These are lifecycle owners, not a general allocator: allocation is forward
 * only and each owner is reset as a unit after its engine references die.
 */

uint8_t pal_mem_level2_scene_arena[
   PAL_MEM_LEVEL2_SCENE_ARENA_BYTES] PAL_MEM_LEVEL2_PSRAM;
uint8_t pal_mem_level2_player_arena[
   PAL_MEM_LEVEL2_PLAYER_ARENA_BYTES] PAL_MEM_LEVEL2_PSRAM;
uint8_t pal_mem_level2_battle_arena[
   PAL_MEM_LEVEL2_BATTLE_ARENA_BYTES] PAL_MEM_LEVEL2_PSRAM;
uint8_t pal_mem_level2_fight_effect[
   PAL_MEM_LEVEL2_FIGHT_EFFECT_BYTES] PAL_MEM_LEVEL2_PSRAM;
uint8_t pal_mem_level2_fight_summon[
   PAL_MEM_LEVEL2_FIGHT_SUMMON_BYTES] PAL_MEM_LEVEL2_PSRAM;

#if defined(PAL_STORAGE_SD_ONLY)
uint8_t pal_mem_level2_resident_pack[
   PAL_MEM_LEVEL2_RESIDENT_PACK_BYTES] PAL_MEM_LEVEL2_PSRAM;
uint8_t pal_mem_level2_tf_toc[
   PAL_MEM_LEVEL2_TF_TOC_BYTES] PAL_MEM_LEVEL2_PSRAM;
uint8_t pal_mem_level2_transient_chunk[
   PAL_MEM_LEVEL2_TRANSIENT_CHUNK_BYTES] PAL_MEM_LEVEL2_PSRAM;
#endif

static PALMEMORYARENA pal_scene_arena = {
   pal_mem_level2_scene_arena,
   sizeof(pal_mem_level2_scene_arena),
   0u
};
static PALMEMORYARENA pal_player_arena = {
   pal_mem_level2_player_arena,
   sizeof(pal_mem_level2_player_arena),
   0u
};
static PALMEMORYARENA pal_battle_arena = {
   pal_mem_level2_battle_arena,
   sizeof(pal_mem_level2_battle_arena),
   0u
};

static void *
PAL_MemoryArenaAlloc(
   PALMEMORYARENA *arena,
   size_t          size
)
{
   size_t offset;

   if (arena == NULL || size == 0u)
   {
      return NULL;
   }
   offset = (arena->used + 3u) & ~(size_t)3u;
   if (size > arena->capacity || offset > arena->capacity - size)
   {
      return NULL;
   }
   arena->used = offset + size;
   return arena->base + offset;
}

void
PAL_MemoryLevel2Touch(
   void
)
{
#define PAL_TOUCH(array) do { \
   (array)[0] = 0u; \
   (array)[sizeof(array) - 1u] = 0u; \
} while (0)
   PAL_TOUCH(pal_mem_level2_scene_arena);
   PAL_TOUCH(pal_mem_level2_player_arena);
   PAL_TOUCH(pal_mem_level2_battle_arena);
   PAL_TOUCH(pal_mem_level2_fight_effect);
   PAL_TOUCH(pal_mem_level2_fight_summon);
#if defined(PAL_STORAGE_SD_ONLY)
   PAL_TOUCH(pal_mem_level2_resident_pack);
   PAL_TOUCH(pal_mem_level2_tf_toc);
   PAL_TOUCH(pal_mem_level2_transient_chunk);
#endif
#undef PAL_TOUCH
}

void
PAL_MemorySceneReset(
   void
)
{
   pal_scene_arena.used = 0u;
}

void *
PAL_MemorySceneAlloc(
   size_t size
)
{
   return PAL_MemoryArenaAlloc(&pal_scene_arena, size);
}

size_t
PAL_MemorySceneUsed(
   void
)
{
   return pal_scene_arena.used;
}

void
PAL_MemoryPlayerReset(
   void
)
{
   pal_player_arena.used = 0u;
}

void *
PAL_MemoryPlayerAlloc(
   size_t size
)
{
   return PAL_MemoryArenaAlloc(&pal_player_arena, size);
}

size_t
PAL_MemoryPlayerUsed(
   void
)
{
   return pal_player_arena.used;
}

void
PAL_MemoryBattleReset(
   void
)
{
   pal_battle_arena.used = 0u;
}

void *
PAL_MemoryBattleAlloc(
   size_t size
)
{
   return PAL_MemoryArenaAlloc(&pal_battle_arena, size);
}

size_t
PAL_MemoryBattleUsed(
   void
)
{
   return pal_battle_arena.used;
}

#endif /* MEM_LEVEL2 */
