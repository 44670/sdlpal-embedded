#ifndef PAL_MEMORY_PROFILE_H
#define PAL_MEMORY_PROFILE_H

#include <stddef.h>
#include <stdint.h>

/*
 * The memory level describes resource ownership only.  Board wiring, display
 * geometry, and the choice of NOR/SD storage remain separate build choices.
 */
#if defined(MEM_LEVEL1) && defined(MEM_LEVEL2)
#error "MEM_LEVEL1 and MEM_LEVEL2 are mutually exclusive"
#endif

#if (defined(MEM_LEVEL1) || defined(MEM_LEVEL2)) && \
   !defined(PAL_NO_RUNTIME_HEAP)
#error "Resource-memory profiles require PAL_NO_RUNTIME_HEAP"
#endif

#if (defined(MEM_LEVEL1) || defined(MEM_LEVEL2)) && \
   !defined(PAL_NO_RUNTIME_DECOMPRESS)
#error "Resource-memory profiles require host-decoded native packs"
#endif

#if defined(PAL_NO_RUNTIME_HEAP) && \
   !defined(MEM_LEVEL1) && !defined(MEM_LEVEL2)
#error "A no-heap engine build must define MEM_LEVEL1 or MEM_LEVEL2"
#endif

#if defined(PAL_STORAGE_SD_ONLY) && !defined(MEM_LEVEL2)
#error "PAL_STORAGE_SD_ONLY requires MEM_LEVEL2"
#endif

#if defined(MEM_LEVEL2)

#define PAL_MEM_LEVEL2_SCENE_ARENA_BYTES       (256u * 1024u)
#define PAL_MEM_LEVEL2_PLAYER_ARENA_BYTES      (128u * 1024u)
#define PAL_MEM_LEVEL2_BATTLE_ARENA_BYTES      (512u * 1024u)
#define PAL_MEM_LEVEL2_FIGHT_EFFECT_BYTES      (64u * 1024u)
#define PAL_MEM_LEVEL2_FIGHT_SUMMON_BYTES      (64u * 1024u)
#define PAL_MEM_LEVEL2_RESOURCE_BYTES ( \
   PAL_MEM_LEVEL2_SCENE_ARENA_BYTES + \
   PAL_MEM_LEVEL2_PLAYER_ARENA_BYTES + \
   PAL_MEM_LEVEL2_BATTLE_ARENA_BYTES + \
   PAL_MEM_LEVEL2_FIGHT_EFFECT_BYTES + \
   PAL_MEM_LEVEL2_FIGHT_SUMMON_BYTES)

extern uint8_t pal_mem_level2_scene_arena[
   PAL_MEM_LEVEL2_SCENE_ARENA_BYTES];
extern uint8_t pal_mem_level2_player_arena[
   PAL_MEM_LEVEL2_PLAYER_ARENA_BYTES];
extern uint8_t pal_mem_level2_battle_arena[
   PAL_MEM_LEVEL2_BATTLE_ARENA_BYTES];
extern uint8_t pal_mem_level2_fight_effect[
   PAL_MEM_LEVEL2_FIGHT_EFFECT_BYTES];
extern uint8_t pal_mem_level2_fight_summon[
   PAL_MEM_LEVEL2_FIGHT_SUMMON_BYTES];

#if defined(PAL_STORAGE_SD_ONLY)
#define PAL_MEM_LEVEL2_CORE_PACK_BYTES          (1536u * 1024u)
#define PAL_MEM_LEVEL2_TF_TOC_BYTES             (40u * 1024u)
#define PAL_MEM_LEVEL2_TRANSIENT_CHUNK_BYTES    (64u * 1024u)
#define PAL_MEM_LEVEL2_SD_STORAGE_BYTES ( \
   PAL_MEM_LEVEL2_CORE_PACK_BYTES + \
   PAL_MEM_LEVEL2_TF_TOC_BYTES + \
   PAL_MEM_LEVEL2_TRANSIENT_CHUNK_BYTES)

extern uint8_t pal_mem_level2_core_pack[
   PAL_MEM_LEVEL2_CORE_PACK_BYTES];
extern uint8_t pal_mem_level2_tf_toc[
   PAL_MEM_LEVEL2_TF_TOC_BYTES];
extern uint8_t pal_mem_level2_transient_chunk[
   PAL_MEM_LEVEL2_TRANSIENT_CHUNK_BYTES];
#endif

void PAL_MemoryLevel2Touch(void);

/* Reset invalidates every pointer previously returned by that owner. */
void PAL_MemorySceneReset(void);
void *PAL_MemorySceneAlloc(size_t size);
size_t PAL_MemorySceneUsed(void);

void PAL_MemoryPlayerReset(void);
void *PAL_MemoryPlayerAlloc(size_t size);
size_t PAL_MemoryPlayerUsed(void);

void PAL_MemoryBattleReset(void);
void *PAL_MemoryBattleAlloc(size_t size);
size_t PAL_MemoryBattleUsed(void);

#endif /* MEM_LEVEL2 */

#endif
