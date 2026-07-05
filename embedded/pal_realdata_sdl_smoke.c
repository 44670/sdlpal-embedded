#define _XOPEN_SOURCE 700

#include "pal_battle_cache.h"
#include "pal_audio_static.h"
#include "pal_dialog_static.h"
#include "pal_ending_static.h"
#include "pal_font_cache.h"
#include "pal_global_cache.h"
#include "pal_menu_static.h"
#include "pal_memory.h"
#include "pal_music_cache.h"
#include "pal_pack.h"
#include "pal_palette_static.h"
#include "pal_rng_cache.h"
#include "pal_save_cache.h"
#include "pal_scene_cache.h"
#include "pal_sfx_cache.h"
#include "pal_text_cache.h"
#include "pal_ui_cache.h"
#include "pal_video_static.h"

#include <SDL.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static char pal_save_path[512];

typedef struct MappedPack {
    const uint8_t *data;
    uint32_t size;
    PalPack pack;
} MappedPack;

static const SDL_Color kPalette[8] = {
    { 0x00, 0x00, 0x00, 0xff },
    { 0x24, 0x18, 0x10, 0xff },
    { 0x49, 0x30, 0x20, 0xff },
    { 0x6d, 0x48, 0x30, 0xff },
    { 0x92, 0x60, 0x40, 0xff },
    { 0xb6, 0x78, 0x50, 0xff },
    { 0xdb, 0x90, 0x60, 0xff },
    { 0xff, 0xa8, 0x70, 0xff },
};

static const uint8_t kPaletteRgb[24] = {
    0x00, 0x00, 0x00,
    0x24, 0x18, 0x10,
    0x49, 0x30, 0x20,
    0x6d, 0x48, 0x30,
    0x92, 0x60, 0x40,
    0xb6, 0x78, 0x50,
    0xdb, 0x90, 0x60,
    0xff, 0xa8, 0x70,
};

static int map_pack_file(const char *path, MappedPack *mapped)
{
    int fd;
    struct stat st;
    const uint8_t *data;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 1;
    }
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > 0x7fffffffL) {
        close(fd);
        return 2;
    }
    data = (const uint8_t *)mmap(0, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (data == MAP_FAILED) {
        return 3;
    }

    mapped->data = data;
    mapped->size = (uint32_t)st.st_size;
    if (!PalPack_OpenConst(&mapped->pack, data, mapped->size)) {
        munmap((void *)data, (size_t)st.st_size);
        mapped->data = 0;
        mapped->size = 0;
        return 4;
    }
    return 0;
}

static void unmap_pack_file(MappedPack *mapped)
{
    if (mapped->data != 0) {
        munmap((void *)mapped->data, mapped->size);
        mapped->data = 0;
        mapped->size = 0;
    }
}

static int expect_chunk_count(const PalPack *pack, uint16_t archive_id, uint16_t expected)
{
    uint16_t actual = 0;
    if (!PalPack_GetChunkCount(pack, archive_id, &actual)) {
        return 1;
    }
    return actual == expected ? 0 : 2;
}

static int map_native_nonempty(const PalPack *pack, uint16_t archive_id, uint16_t chunk_id)
{
    PalPackSpan span;
    if (!PalPack_MapConst(pack, archive_id, chunk_id, &span)) {
        return 1;
    }
    if (span.data == 0 || span.size == 0 || span.format != PAL_PACK_FORMAT_NATIVE) {
        return 2;
    }
    return 0;
}

static uint32_t checksum32(const uint8_t *data, uint32_t size)
{
    uint32_t i;
    uint32_t sum = 0;

    for (i = 0; i < size; i++) {
        sum = (sum << 5) - sum + data[i];
    }
    return sum;
}

static int make_data_path(const char *data_dir, const char *name)
{
    uint32_t i = 0;
    uint32_t j = 0;

    if (data_dir == 0 || name == 0 || data_dir[0] == 0 || name[0] == 0) {
        return 1;
    }
    while (data_dir[i] != 0) {
        if (i + 1u >= sizeof(pal_save_path)) {
            return 2;
        }
        pal_save_path[i] = data_dir[i];
        i++;
    }
    if (pal_save_path[i - 1u] != '/') {
        if (i + 1u >= sizeof(pal_save_path)) {
            return 3;
        }
        pal_save_path[i] = '/';
        i++;
    }
    while (name[j] != 0) {
        if (i + 1u >= sizeof(pal_save_path)) {
            return 4;
        }
        pal_save_path[i] = name[j];
        i++;
        j++;
    }
    pal_save_path[i] = 0;
    return 0;
}

static int exercise_tf_reads(const PalPack *tf)
{
    uint32_t copied = 0;

    if (!PalPack_CopyRaw(tf, PAL_PACK_ARCHIVE_FBP, 0, pal_sram_framebuffer, PAL_SRAM_FRAMEBUFFER_BYTES, &copied)) {
        return 1;
    }
    if (copied != PAL_SRAM_FRAMEBUFFER_BYTES) {
        return 2;
    }
    if (!PalPack_CopyRaw(tf, PAL_PACK_ARCHIVE_MAP, 1, pal_psram_map_tiles, PAL_PSRAM_MAP_TILES_BYTES, &copied)) {
        return 3;
    }
    if (copied != PAL_PSRAM_MAP_TILES_BYTES) {
        return 4;
    }
    if (!PalPack_CopyRaw(tf, PAL_PACK_ARCHIVE_GOP, 1, pal_psram_gop_copy, PAL_PSRAM_GOP_COPY_BYTES, &copied)) {
        return 5;
    }
    if (copied == 0 || copied > PAL_PSRAM_GOP_COPY_BYTES) {
        return 6;
    }
    if (!PalPack_CopyRaw(tf, PAL_PACK_ARCHIVE_SFX, 1, pal_psram_sfx_bank, PAL_PSRAM_SFX_BANK_BYTES, &copied)) {
        return 7;
    }
    if (copied == 0 || copied > PAL_PSRAM_SFX_BANK_BYTES) {
        return 8;
    }
    if (checksum32(pal_sram_framebuffer, PAL_SRAM_FRAMEBUFFER_BYTES) == 0) {
        return 9;
    }
    if (checksum32(pal_psram_map_tiles, PAL_PSRAM_MAP_TILES_BYTES) == 0) {
        return 10;
    }
    return 0;
}

static int exercise_tf_toc_reads(const MappedPack *tf)
{
    PalPackToc toc;
    PalPackChunkInfo info;
    uint16_t chunk_count = 0;
    uint32_t copied = 0;

    if (tf == 0 || !PalPack_OpenTocCopy(&toc, tf->data, tf->size, pal_psram_tf_toc, PAL_PSRAM_TF_TOC_BYTES)) {
        return 1;
    }
    if (toc.base != pal_psram_tf_toc || toc.toc_size != 13084u || toc.pack_size != tf->size) {
        return 2;
    }
    if (!PalPackToc_GetChunkCount(&toc, PAL_PACK_ARCHIVE_MAP, &chunk_count) || chunk_count != 226u) {
        return 3;
    }
    if (!PalPackToc_GetChunkCount(&toc, PAL_PACK_ARCHIVE_SFX, &chunk_count) || chunk_count != 276u) {
        return 4;
    }
    if (!PalPackToc_GetChunkInfo(&toc, PAL_PACK_ARCHIVE_SFX, 255, &info) ||
        info.size != 211152u || info.format != PAL_PACK_FORMAT_SFX_PCM16 || info.flags != 0u) {
        return 5;
    }
    if (!PalPackToc_CopyRawFromImage(&toc, tf->data, PAL_PACK_ARCHIVE_FBP, 0, pal_sram_framebuffer, PAL_SRAM_FRAMEBUFFER_BYTES, &copied) ||
        copied != PAL_SRAM_FRAMEBUFFER_BYTES || checksum32(pal_sram_framebuffer, copied) == 0) {
        return 6;
    }
    if (!PalPackToc_CopyRawFromImage(&toc, tf->data, PAL_PACK_ARCHIVE_MAP, 1, pal_psram_map_tiles, PAL_PSRAM_MAP_TILES_BYTES, &copied) ||
        copied != PAL_PSRAM_MAP_TILES_BYTES || checksum32(pal_psram_map_tiles, copied) == 0) {
        return 7;
    }
    return 0;
}

static bool read_at_fd(void *user, uint32_t offset, uint8_t *dst, uint32_t size)
{
    int fd = *(const int *)user;
    uint32_t total = 0;

    while (total < size) {
        ssize_t got = pread(fd, dst + total, (size_t)(size - total), (off_t)(offset + total));
        if (got <= 0) {
            return false;
        }
        total += (uint32_t)got;
    }
    return true;
}

static int exercise_tf_toc_fd_reads(const char *path)
{
    PalPackToc toc;
    PalPackChunkInfo info;
    struct stat st;
    uint16_t chunk_count = 0;
    uint32_t copied = 0;
    int fd;
    int rc = 0;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 1;
    }
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > 0x7fffffffL) {
        close(fd);
        return 2;
    }
    if (!PalPack_OpenTocRead(&toc, read_at_fd, &fd, (uint32_t)st.st_size, pal_psram_tf_toc, PAL_PSRAM_TF_TOC_BYTES)) {
        close(fd);
        return 3;
    }
    if (toc.base != pal_psram_tf_toc || toc.toc_size != 13084u || toc.pack_size != (uint32_t)st.st_size) {
        rc = 4;
    }
    if (rc == 0 && (!PalPackToc_GetChunkCount(&toc, PAL_PACK_ARCHIVE_RNG, &chunk_count) || chunk_count != 12u)) {
        rc = 5;
    }
    if (rc == 0 && (!PalPackToc_GetChunkInfo(&toc, PAL_PACK_ARCHIVE_MAP, 1, &info) ||
        info.size != PAL_PSRAM_MAP_TILES_BYTES || info.format != PAL_PACK_FORMAT_NATIVE || info.flags != 0u)) {
        rc = 6;
    }
    if (rc == 0 && (!PalPackToc_CopyRawReadAt(&toc, read_at_fd, &fd, PAL_PACK_ARCHIVE_MAP, 1, pal_psram_map_tiles, PAL_PSRAM_MAP_TILES_BYTES, &copied) ||
        copied != PAL_PSRAM_MAP_TILES_BYTES || checksum32(pal_psram_map_tiles, copied) == 0)) {
        rc = 7;
    }
    if (rc == 0 && (!PalPackToc_CopyRawReadAt(&toc, read_at_fd, &fd, PAL_PACK_ARCHIVE_FBP, 0, pal_sram_framebuffer, PAL_SRAM_FRAMEBUFFER_BYTES, &copied) ||
        copied != PAL_SRAM_FRAMEBUFFER_BYTES || checksum32(pal_sram_framebuffer, copied) == 0)) {
        rc = 8;
    }

    close(fd);
    return rc;
}

static int exercise_sdl_surface(void)
{
    SDL_Surface *surface;
    const uint16_t *line = 0;
    uint16_t pixels = 0;
    uint8_t saved_pixel;
    uint8_t rect_pixel;
    uint8_t big_pixel;

    if (!PalVideo_SetPaletteRgb(0, 8, kPaletteRgb)) {
        return 6;
    }
    PalVideo_SaveScreen();
    PalVideo_Clear(7);
    if (pal_sram_framebuffer[0] != 7u) {
        return 7;
    }
    PalVideo_RestoreScreen();
    rect_pixel = pal_sram_framebuffer[PAL_VIDEO_WIDTH + 1u];
    if (!PalVideo_SaveRect(1, 1, 8, 4)) {
        return 12;
    }
    PalVideo_Clear(7);
    if (!PalVideo_RestoreRect(1, 1, 8, 4)) {
        return 13;
    }
    if (pal_sram_framebuffer[PAL_VIDEO_WIDTH + 1u] != rect_pixel ||
        pal_sram_framebuffer[0] != 7u ||
        PalVideo_SaveRect(319, 199, 2, 1) ||
        PalVideo_RestoreRect(0, 0, 0, 1)) {
        return 14;
    }
    PalVideo_RestoreScreen();
    big_pixel = pal_sram_framebuffer[0];
    PalVideo_SaveBigBuffer();
    PalVideo_Clear(6);
    PalVideo_RestoreBigBuffer();
    if (pal_sram_framebuffer[0] != big_pixel) {
        return 15;
    }
    saved_pixel = pal_sram_framebuffer[0];
    pal_sram_framebuffer[0] = 1u;
    if (!PalVideo_ConvertLineRgb565(0, &line, &pixels) || line == 0 || pixels != PAL_VIDEO_WIDTH || line[0] != 0x20c2u) {
        pal_sram_framebuffer[0] = saved_pixel;
        return 8;
    }
    pal_sram_framebuffer[0] = saved_pixel;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return 1;
    }
    surface = SDL_CreateRGBSurfaceFrom(pal_sram_framebuffer, 320, 200, 8, 320, 0, 0, 0, 0);
    if (surface == 0) {
        SDL_Quit();
        return 2;
    }
    if (surface->pixels != pal_sram_framebuffer || surface->pitch != 320) {
        SDL_FreeSurface(surface);
        SDL_Quit();
        return 3;
    }
    if (surface->format == 0 || surface->format->palette == 0) {
        SDL_FreeSurface(surface);
        SDL_Quit();
        return 4;
    }
    if (SDL_SetPaletteColors(surface->format->palette, kPalette, 0, 8) != 0) {
        SDL_FreeSurface(surface);
        SDL_Quit();
        return 5;
    }
    SDL_FreeSurface(surface);
    SDL_Quit();
    return 0;
}

static int check_scene(const PalPack *nor, const PalPack *tf, uint16_t scene_num, uint16_t expected_events, uint16_t expected_refs, uint16_t expected_unique)
{
    PalSceneSnapshot snapshot;

    if (!PalScene_LoadSnapshot(nor, tf, scene_num, &snapshot)) {
        return 1;
    }
    if (snapshot.scene_num != scene_num || snapshot.event_count != expected_events) {
        return 2;
    }
    if (snapshot.sprite_ref_count != expected_refs || snapshot.unique_sprite_count != expected_unique) {
        return 3;
    }
    if (snapshot.map_num == 0 || snapshot.gop_size == 0 || snapshot.unique_sprite_bytes == 0) {
        return 4;
    }
    if (snapshot.sprite_refs == 0) {
        return 5;
    }
    return 0;
}

static int check_scene_pinned(
    const PalPack *nor,
    const PalPack *tf,
    uint16_t scene_num,
    uint16_t expected_events,
    uint16_t expected_refs,
    uint16_t expected_unique,
    uint32_t expected_unique_bytes,
    uint32_t expected_pin_bytes)
{
    PalSceneSnapshot snapshot;
    uint16_t i;
    bool saw_sprite = false;

    if (!PalScene_LoadPinnedSnapshot(nor, tf, nor, scene_num, &snapshot)) {
        return 1;
    }
    if (snapshot.scene_num != scene_num || snapshot.event_count != expected_events) {
        return 2;
    }
    if (snapshot.sprite_ref_count != expected_refs || snapshot.unique_sprite_count != expected_unique) {
        return 3;
    }
    if (snapshot.unique_sprite_bytes != expected_unique_bytes || snapshot.sprite_pin_bytes != expected_pin_bytes) {
        return 4;
    }
    if (snapshot.sprite_refs == 0 || snapshot.sprite_pin_bytes == 0 || snapshot.sprite_pin_bytes > PAL_PSRAM_SPRITE_PIN_BYTES) {
        return 5;
    }
    for (i = 0; i < snapshot.event_count; i++) {
        const PalSceneSpriteRef *ref = snapshot.sprite_refs + i;
        uintptr_t data = (uintptr_t)ref->data;
        uintptr_t begin = (uintptr_t)pal_psram_sprite_pin;
        uintptr_t end = begin + snapshot.sprite_pin_bytes;

        if (ref->sprite_num == 0) {
            continue;
        }
        saw_sprite = true;
        if (ref->data == 0 || ref->size == 0 || data < begin || data > end || ref->size > end - data) {
            return 6;
        }
        if (checksum32(ref->data, ref->size) == 0) {
            return 7;
        }
    }
    return saw_sprite ? 0 : 8;
}

static int check_battle(const PalPack *nor, const PalPack *tf, uint16_t team_num, uint16_t expected_refs, uint16_t expected_unique)
{
    static const uint16_t player_sprites[3] = { 0, 1, 2 };
    PalBattleSnapshot snapshot;
    PalBattleBuffer effect;

    if (!PalBattle_LoadSnapshot(nor, tf, team_num, 0, player_sprites, 3, 7, &snapshot)) {
        return 1;
    }
    if (snapshot.team_num != team_num || snapshot.enemy_ref_count != expected_refs) {
        return 2;
    }
    if (snapshot.player_count != 3 || snapshot.unique_enemy_sprite_count != expected_unique) {
        return 3;
    }
    if (snapshot.background_size != PAL_PSRAM_FBP_BACKGROUND_BYTES || snapshot.player_sprite_bytes == 0) {
        return 4;
    }
    if (snapshot.unique_enemy_sprite_bytes == 0 || snapshot.effect_size == 0 || snapshot.effect_data == 0) {
        return 5;
    }
    if (snapshot.battle_effect_size != 17478u || snapshot.battle_effect_data == 0 ||
        checksum32(snapshot.battle_effect_data, snapshot.battle_effect_size) == 0) {
        return 6;
    }
    if (snapshot.player_sprites == 0 || snapshot.enemy_sprites == 0) {
        return 7;
    }
    if (!PalBattle_LoadEffectScratch(nor, 37, &effect) ||
        effect.data != pal_psram_effect ||
        effect.size != 65502u ||
        checksum32(effect.data, effect.size) == 0) {
        return 8;
    }
    return 0;
}

static int check_rng_frame(
    const PalPack *tf,
    uint16_t movie_num,
    uint16_t frame_num,
    uint16_t expected_frame_count,
    uint32_t expected_frame_size,
    PalRngFrameBuffer frame_buffer)
{
    PalRngFrame frame;

    if (!PalRng_LoadFrame(tf, movie_num, frame_num, frame_buffer, &frame)) {
        return 1;
    }
    if (frame.movie_num != movie_num || frame.frame_num != frame_num) {
        return 2;
    }
    if (frame.frame_count != expected_frame_count || frame.size != expected_frame_size) {
        return 3;
    }
    if (frame.data == 0 || checksum32(frame.data, frame.size) == 0) {
        return 4;
    }
    if (frame_buffer == PAL_RNG_FRAME_BUFFER_A && frame.data != pal_psram_rng_frame_a) {
        return 5;
    }
    if (frame_buffer == PAL_RNG_FRAME_BUFFER_B && frame.data != pal_psram_rng_frame_b) {
        return 6;
    }
    return 0;
}

static int check_rng_frame_readat(
    const char *path,
    uint16_t movie_num,
    uint16_t frame_num,
    uint16_t expected_frame_count,
    uint32_t expected_frame_size,
    PalRngFrameBuffer frame_buffer)
{
    PalPackToc toc;
    PalRngMovieStream movie;
    PalRngFrame frame;
    struct stat st;
    int fd;
    int rc = 0;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 1;
    }
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > 0x7fffffffL) {
        close(fd);
        return 2;
    }
    if (!PalPack_OpenTocRead(&toc, read_at_fd, &fd, (uint32_t)st.st_size, pal_psram_tf_toc, PAL_PSRAM_TF_TOC_BYTES)) {
        close(fd);
        return 3;
    }
    if (!PalRng_OpenMovieReadAt(&toc, read_at_fd, &fd, movie_num, &movie)) {
        close(fd);
        return 4;
    }
    if (movie.movie_num != movie_num || movie.frame_count != expected_frame_count ||
        movie.table != pal_psram_tf_readahead || movie.table_size == 0 ||
        movie.table_size > PAL_PSRAM_TF_READAHEAD_BYTES) {
        rc = 5;
    }
    if (rc == 0 && !PalRng_LoadFrameReadAt(&movie, read_at_fd, &fd, frame_num, frame_buffer, &frame)) {
        rc = 6;
    }
    if (rc == 0 && (frame.movie_num != movie_num || frame.frame_num != frame_num ||
        frame.frame_count != expected_frame_count || frame.size != expected_frame_size)) {
        rc = 7;
    }
    if (rc == 0 && (frame.data == 0 || checksum32(frame.data, frame.size) == 0)) {
        rc = 8;
    }
    if (rc == 0 && frame_buffer == PAL_RNG_FRAME_BUFFER_A && frame.data != pal_psram_rng_frame_a) {
        rc = 9;
    }
    if (rc == 0 && frame_buffer == PAL_RNG_FRAME_BUFFER_B && frame.data != pal_psram_rng_frame_b) {
        rc = 10;
    }

    close(fd);
    return rc;
}

static int check_rng_all_frames_readat(const char *path)
{
    PalPackToc toc;
    PalRngMovieStream movie;
    PalRngFrame frame;
    struct stat st;
    uint16_t movie_count = 0;
    uint32_t total_frames = 0;
    uint32_t total_payload = 0;
    uint32_t max_size = 0;
    uint16_t max_movie = 0;
    uint16_t max_frame = 0;
    int fd;
    int rc = 0;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 1;
    }
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > 0x7fffffffL) {
        close(fd);
        return 2;
    }
    if (!PalPack_OpenTocRead(&toc, read_at_fd, &fd, (uint32_t)st.st_size, pal_psram_tf_toc, PAL_PSRAM_TF_TOC_BYTES)) {
        close(fd);
        return 3;
    }
    if (!PalPackToc_GetChunkCount(&toc, PAL_PACK_ARCHIVE_RNG, &movie_count) || movie_count != 12u) {
        close(fd);
        return 4;
    }

    for (uint16_t movie_num = 0; rc == 0 && movie_num < movie_count; movie_num++) {
        if (!PalRng_OpenMovieReadAt(&toc, read_at_fd, &fd, movie_num, &movie)) {
            rc = 5;
            break;
        }
        total_frames += movie.frame_count;
        for (uint16_t frame_num = 0; frame_num < movie.frame_count; frame_num++) {
            PalRngFrameBuffer frame_buffer = (frame_num & 1u) ? PAL_RNG_FRAME_BUFFER_B : PAL_RNG_FRAME_BUFFER_A;
            if (!PalRng_LoadFrameReadAt(&movie, read_at_fd, &fd, frame_num, frame_buffer, &frame)) {
                rc = 6;
                break;
            }
            if ((frame_buffer == PAL_RNG_FRAME_BUFFER_A && frame.data != pal_psram_rng_frame_a) ||
                (frame_buffer == PAL_RNG_FRAME_BUFFER_B && frame.data != pal_psram_rng_frame_b)) {
                rc = 7;
                break;
            }
            if (frame.size != 0u && checksum32(frame.data, frame.size) == 0u) {
                rc = 8;
                break;
            }
            total_payload += frame.size;
            if (frame.size > max_size) {
                max_size = frame.size;
                max_movie = movie_num;
                max_frame = frame_num;
            }
        }
    }

    if (rc == 0 && (total_frames != 1476u || total_payload != 7301725u ||
        max_size != 64288u || max_movie != 4u || max_frame != 0u)) {
        rc = 9;
    }

    close(fd);
    return rc;
}

static int check_sfx_bank(const PalPack *tf)
{
    static const uint16_t chunks[] = { 1, 62, 192, 213, 214, 255, 272 };
    static const uint32_t sizes[] = { 12622, 111738, 132856, 204664, 148342, 211152, 200526 };
    PalSfxBank bank;
    uint32_t used = 0;
    uint16_t i;

    if (!PalSfx_LoadBank(tf, chunks, (uint16_t)(sizeof(chunks) / sizeof(chunks[0])), &bank)) {
        return 1;
    }
    if (bank.entry_count != (uint16_t)(sizeof(chunks) / sizeof(chunks[0])) || bank.entries == 0 || bank.data != pal_psram_sfx_bank) {
        return 2;
    }

    for (i = 0; i < bank.entry_count; i++) {
        PalAudioSfx sfx;
        const uint8_t *data = 0;
        uint32_t size = 0;
        uint32_t cursor = 0;

        used = (used + 3u) & ~3u;
        if (!PalSfx_Get(&bank, chunks[i], &data, &size)) {
            return 3;
        }
        if (size != sizes[i] || data != pal_psram_sfx_bank + used || checksum32(data, size) == 0) {
            return 4;
        }
        if (!PalAudio_OpenSfx(data, size, &sfx) || sfx.sample_count == 0 || sfx.pcm == 0) {
            return 5;
        }
        PalAudio_Clear(512);
        if (!PalAudio_MixSfx(&sfx, &cursor, 512) || cursor != 512 || checksum32((const uint8_t *)PalAudio_MixBuffer(), 1024) == 0) {
            return 6;
        }
        used += size;
    }

    if (bank.used_bytes != 1021906u || used != bank.used_bytes) {
        return 7;
    }
    return 0;
}

static int check_global_cache(const PalPack *nor)
{
    const PalGlobalCache *cache = 0;

    if (!PalGlobal_LoadDefault(nor, &cache)) {
        return 1;
    }
    if (cache == 0 || cache->mutable_bytes != 182176u) {
        return 2;
    }
    if (cache->event_objects.data != pal_psram_save_state || cache->event_objects.count != 5369u) {
        return 3;
    }
    if (cache->scenes.count != 300u || cache->objects_dos.count != 589u || cache->player_roles.size != PAL_GLOBAL_PLAYER_ROLES_BYTES) {
        return 4;
    }
    if (cache->script_entries.count != 42494u || cache->stores.count != 21u || cache->enemies.count != 350u) {
        return 5;
    }
    if (cache->enemy_teams.count != 390u || cache->magics.count != 114u || cache->battlefields.count != 130u) {
        return 6;
    }
    if (cache->levelup_magics.count != 50u || cache->battle_effect_index.size != 40u ||
        cache->enemy_positions.size != 100u || cache->levelup_exp.count != 100u) {
        return 7;
    }
    if (checksum32(cache->event_objects.data, cache->event_objects.size) == 0 ||
        checksum32(cache->script_entries.data, cache->script_entries.size) == 0 ||
        checksum32(cache->player_roles.data, cache->player_roles.size) == 0) {
        return 8;
    }
    return 0;
}

static int check_text_cache(const PalPack *nor)
{
    PalTextCache cache;
    const uint8_t *text = 0;
    uint32_t size = 0;

    if (!PalText_Open(nor, &cache)) {
        return 1;
    }
    if (cache.word_count != 589u || cache.message_count != 10495u || cache.text_size != 210386u) {
        return 2;
    }
    if (!PalText_GetWord(&cache, 2, &text, &size) || size != 6u || checksum32(text, size) == 0) {
        return 3;
    }
    if (!PalText_GetMessage(&cache, 0, &text, &size) || size != 10u || checksum32(text, size) == 0) {
        return 4;
    }
    if (!PalText_GetMessage(&cache, 10494, &text, &size) || size != 38u || checksum32(text, size) == 0) {
        return 5;
    }
    return 0;
}

static int check_font_cache(const PalPack *nor)
{
    PalFontCache cache;
    const uint8_t *glyph = 0;
    uint16_t glyph_bytes = 0;

    if (!PalFont_Open(nor, &cache)) {
        return 1;
    }
    if (cache.glyph_count != 2600u || cache.glyph_bytes != PAL_FONT_GLYPH_BYTES || cache.glyph_data_size != 83200u) {
        return 2;
    }
    if (!PalFont_FindGlyph(&cache, 0x7d93u, &glyph, &glyph_bytes) || glyph_bytes != PAL_FONT_GLYPH_BYTES || checksum32(glyph, glyph_bytes) == 0) {
        return 3;
    }
    if (!PalFont_FindGlyph(&cache, 0x9a57u, &glyph, &glyph_bytes) || glyph_bytes != PAL_FONT_GLYPH_BYTES || checksum32(glyph, glyph_bytes) == 0) {
        return 4;
    }
    if (!PalFont_FindGlyph(&cache, 0x503cu, &glyph, &glyph_bytes) || glyph_bytes != PAL_FONT_GLYPH_BYTES || checksum32(glyph, glyph_bytes) == 0) {
        return 5;
    }
    if (PalFont_FindGlyph(&cache, 0x0030u, &glyph, &glyph_bytes)) {
        return 6;
    }
    return 0;
}

static int check_save_file(
    const char *data_dir,
    const char *name,
    uint32_t expected_size,
    uint16_t expected_saved_times,
    uint16_t expected_scene,
    uint32_t expected_cash)
{
    PalSaveSlot slot;

    if (make_data_path(data_dir, name) != 0) {
        return 1;
    }
    if (!PalSave_ReadFile(pal_save_path, &slot)) {
        return 2;
    }
    if (slot.data != pal_psram_save_state || slot.size != expected_size || slot.checksum == 0) {
        return 3;
    }
    if (slot.saved_times != expected_saved_times || slot.scene_num != expected_scene || slot.cash != expected_cash) {
        return 4;
    }
    if (slot.viewport_x == 0 || slot.viewport_y == 0 || slot.music_num == 0 || slot.battle_music_num == 0) {
        return 5;
    }
    return 0;
}

static int check_save_cache(const char *data_dir)
{
    static const char roundtrip_path[] = "/tmp/sdlpal-embedded-save-roundtrip.rpg";
    PalSaveSlot slot;
    PalSaveSlot roundtrip;
    uint32_t checksum;

    if (check_save_file(data_dir, "1.rpg", 184672u, 1u, 1u, 0u) != 0 ||
        check_save_file(data_dir, "2.rpg", 188864u, 8u, 17u, 580u) != 0 ||
        check_save_file(data_dir, "4.RPG", 183488u, 1u, 1u, 899999u) != 0) {
        return 1;
    }
    if (make_data_path(data_dir, "2.rpg") != 0 || !PalSave_ReadFile(pal_save_path, &slot)) {
        return 2;
    }
    checksum = slot.checksum;
    unlink(roundtrip_path);
    if (!PalSave_WriteFile(roundtrip_path, slot.data, slot.size)) {
        return 3;
    }
    if (!PalSave_ReadFile(roundtrip_path, &roundtrip)) {
        unlink(roundtrip_path);
        return 4;
    }
    unlink(roundtrip_path);
    if (roundtrip.size != 188864u || roundtrip.checksum != checksum ||
        roundtrip.saved_times != 8u || roundtrip.scene_num != 17u || roundtrip.cash != 580u) {
        return 5;
    }
    return 0;
}

static int check_music_track_pair(const PalPack *nor, uint16_t track_num, uint32_t expected_midi_size, uint32_t expected_mus_size)
{
    PalMusicTrack track;

    if (!PalMusic_MapMidi(nor, track_num, &track) || track.track_num != track_num || track.format != PAL_MUSIC_FORMAT_MIDI) {
        return 1;
    }
    if (track.size != expected_midi_size || track.data[0] != 'M' || track.data[1] != 'T' || checksum32(track.data, track.size) == 0) {
        return 2;
    }

    if (!PalMusic_MapMus(nor, track_num, &track) || track.track_num != track_num || track.format != PAL_MUSIC_FORMAT_RIX) {
        return 3;
    }
    if (track.size != expected_mus_size || track.data[0] != 0xaau || track.data[1] != 0x55u || checksum32(track.data, track.size) == 0) {
        return 4;
    }

    return 0;
}

static int check_music_cache(const PalPack *nor)
{
    uint16_t midi_count = 0;
    uint16_t mus_count = 0;

    if (!PalPack_GetChunkCount(nor, PAL_PACK_ARCHIVE_MIDI, &midi_count) || midi_count != 88u) {
        return 1;
    }
    if (!PalPack_GetChunkCount(nor, PAL_PACK_ARCHIVE_MUS, &mus_count) || mus_count != 88u) {
        return 2;
    }
    if (check_music_track_pair(nor, 31u, 6162u, 3220u) != 0) {
        return 3;
    }
    if (check_music_track_pair(nor, 37u, 24548u, 3956u) != 0) {
        return 4;
    }
    if (check_music_track_pair(nor, 77u, 5180u, 3898u) != 0) {
        return 5;
    }
    return 0;
}

static int check_menu_static(const PalPack *nor, const PalPack *tf)
{
    PalMenuBuffer buffer;
    PalMenuConstAsset asset;

    if (!PalMenu_LoadBackground(tf, 0, &buffer) || buffer.data != pal_psram_menu_background || buffer.size != PAL_MENU_BACKGROUND_BYTES) {
        return 1;
    }
    if (checksum32(buffer.data, buffer.size) == 0) {
        return 2;
    }
    if (!PalMenu_LoadBackground(tf, 1, &buffer) || buffer.data != pal_psram_menu_background || buffer.size != PAL_MENU_BACKGROUND_BYTES) {
        return 3;
    }
    if (checksum32(buffer.data, buffer.size) == 0) {
        return 4;
    }
    if (!PalMenu_LoadBackground(tf, 60, &buffer) || buffer.data != pal_psram_menu_background || buffer.size != PAL_MENU_BACKGROUND_BYTES) {
        return 5;
    }
    if (checksum32(buffer.data, buffer.size) == 0) {
        return 6;
    }
    if (!PalMenu_CopyImage(nor, PAL_PACK_ARCHIVE_RGM, 72, &buffer) || buffer.data != pal_psram_menu_image || buffer.size != 8024u) {
        return 7;
    }
    if (checksum32(buffer.data, buffer.size) == 0) {
        return 8;
    }
    if (!PalMenu_MapImage(nor, PAL_PACK_ARCHIVE_BALL, 95, &asset) || asset.data == 0 || asset.size != 1876u) {
        return 9;
    }
    if (checksum32(asset.data, asset.size) == 0) {
        return 10;
    }
    if (!PalMenu_PrepareBox(72, 72, 0x5au, &buffer) || buffer.data != pal_psram_menu_box || buffer.size != PAL_MENU_BOX_MAX_BYTES) {
        return 11;
    }
    if (checksum32(buffer.data, buffer.size) == 0) {
        return 12;
    }
    return 0;
}

static int check_ending_static(const PalPack *nor, const PalPack *tf)
{
    PalEndingBuffer buffer;
    PalEndingScreenPair pair;
    PalEndingConstAsset asset;

    if (!PalEnding_LoadFbp(tf, 68, &buffer) || buffer.data != pal_psram_ending_fbp_a || buffer.size != PAL_ENDING_FBP_BYTES) {
        return 1;
    }
    if (checksum32(buffer.data, buffer.size) == 0) {
        return 2;
    }
    if (!PalEnding_LoadFbpPair(tf, 61, 62, &pair)) {
        return 3;
    }
    if (pair.upper.data != pal_psram_ending_fbp_a || pair.upper.size != PAL_ENDING_FBP_BYTES ||
        pair.lower.data != pal_psram_ending_fbp_b || pair.lower.size != PAL_ENDING_FBP_BYTES) {
        return 4;
    }
    if (checksum32(pair.upper.data, pair.upper.size) == 0 || checksum32(pair.lower.data, pair.lower.size) == 0) {
        return 5;
    }
    if (!PalEnding_MapSprite(nor, 571, &asset) || asset.data == 0 || asset.size != 59516u) {
        return 6;
    }
    if (checksum32(asset.data, asset.size) == 0) {
        return 7;
    }
    if (!PalEnding_MapSprite(nor, 572, &asset) || asset.data == 0 || asset.size != 5736u) {
        return 8;
    }
    if (checksum32(asset.data, asset.size) == 0) {
        return 9;
    }
    if (!PalEnding_MapSprite(nor, 627, &asset) || asset.data == 0 || asset.size != 3136u) {
        return 10;
    }
    if (checksum32(asset.data, asset.size) == 0) {
        return 11;
    }
    return 0;
}

static int check_palette_static(const PalPack *nor)
{
    const uint8_t *rgb = 0;
    const uint8_t *night_rgb = 0;
    uint32_t size = 0;
    uint32_t night_size = 0;
    PalPaletteBuffer buffer;

    if (!PalUi_LoadPaletteRgb(nor, 0, false, &rgb, &size) || rgb != pal_sram_misc || size != PAL_PALETTE_RGB_BYTES) {
        return 1;
    }
    if (!PalPalette_LoadCurrentRgb(rgb, size, &buffer) || buffer.data != pal_sram_palette_current || buffer.size != PAL_PALETTE_RGB_BYTES) {
        return 2;
    }
    if (checksum32(buffer.data, buffer.size) == 0) {
        return 3;
    }
    if (!PalUi_LoadPaletteRgb(nor, 0, true, &night_rgb, &night_size) || night_rgb != pal_sram_misc || night_size != PAL_PALETTE_RGB_BYTES) {
        return 4;
    }
    if (!PalPalette_BlendRgb(night_rgb, night_size, 16, 32, &buffer) ||
        buffer.data != pal_sram_palette_work || buffer.size != PAL_PALETTE_RGB_BYTES) {
        return 5;
    }
    if (checksum32(buffer.data, buffer.size) == 0 || !PalVideo_SetPaletteRgb(0, PAL_PALETTE_COLORS, buffer.data)) {
        return 6;
    }
    if (PalVideo_GetRgb565(1) == 0) {
        return 7;
    }
    if (!PalPalette_ScaleCurrent(0, 32, &buffer) || buffer.data != pal_sram_palette_work || buffer.size != PAL_PALETTE_RGB_BYTES) {
        return 8;
    }
    if (checksum32(buffer.data, buffer.size) != 0) {
        return 9;
    }
    if (!PalPalette_FillColor(pal_sram_palette_current, PAL_PALETTE_RGB_BYTES, 1, &buffer) ||
        buffer.data != pal_sram_palette_work || buffer.size != PAL_PALETTE_RGB_BYTES) {
        return 10;
    }
    if (buffer.data[0] != pal_sram_palette_current[3] ||
        buffer.data[1] != pal_sram_palette_current[4] ||
        buffer.data[2] != pal_sram_palette_current[5] ||
        checksum32(buffer.data, buffer.size) == 0) {
        return 11;
    }
    return 0;
}

static int check_dialog_static(const PalPack *nor)
{
    PalDialogAsset asset;

    if (!PalDialog_MapIcons(nor, &asset) || asset.data == 0 || asset.size != PAL_DIALOG_ICON_BYTES) {
        return 1;
    }
    if (checksum32(asset.data, asset.size) == 0) {
        return 2;
    }
    if (!PalDialog_MapFace(nor, 72, &asset) || asset.data == 0 || asset.size != 8024u) {
        return 3;
    }
    if (checksum32(asset.data, asset.size) == 0) {
        return 4;
    }
    return 0;
}

static int check_ui_cache(const PalPack *nor)
{
    PalUiAsset asset;
    const uint8_t *palette = 0;
    uint32_t palette_size = 0;

    if (!PalUi_MapUiSprite(nor, &asset) || asset.size != 25532u || checksum32(asset.data, asset.size) == 0) {
        return 1;
    }
    if (!PalUi_MapBattleEffect(nor, &asset) || asset.size != 17478u || checksum32(asset.data, asset.size) == 0) {
        return 2;
    }
    if (!PalUi_MapItemBitmap(nor, 95, &asset) || asset.size != 1876u || checksum32(asset.data, asset.size) == 0) {
        return 3;
    }
    if (!PalUi_MapFaceBitmap(nor, 72, &asset) || asset.size != 8024u || checksum32(asset.data, asset.size) == 0) {
        return 4;
    }
    if (!PalUi_LoadPaletteRgb(nor, 0, false, &palette, &palette_size) || palette != pal_sram_misc || palette_size != PAL_UI_PALETTE_RGB_BYTES) {
        return 5;
    }
    if (checksum32(palette, palette_size) == 0 || !PalVideo_SetPaletteRgb(0, PAL_UI_PALETTE_COLORS, palette)) {
        return 6;
    }
    if (!PalUi_LoadPaletteRgb(nor, 0, true, &palette, &palette_size) || palette != pal_sram_misc || palette_size != PAL_UI_PALETTE_RGB_BYTES) {
        return 7;
    }
    if (checksum32(palette, palette_size) == 0) {
        return 8;
    }
    return 0;
}

int main(int argc, char **argv)
{
    MappedPack nor = { 0, 0, { 0, 0, 0, 0 } };
    MappedPack tf = { 0, 0, { 0, 0, 0, 0 } };
    int rc;

    if (argc != 4) {
        return 1;
    }
    if (map_pack_file(argv[1], &nor) != 0) {
        return 2;
    }
    if (map_pack_file(argv[2], &tf) != 0) {
        unmap_pack_file(&nor);
        return 3;
    }

    rc =
        expect_chunk_count(&nor.pack, PAL_PACK_ARCHIVE_MGO, 637) ||
        expect_chunk_count(&nor.pack, PAL_PACK_ARCHIVE_ABC, 160) ||
        expect_chunk_count(&nor.pack, PAL_PACK_ARCHIVE_FIRE, 55) ||
        expect_chunk_count(&nor.pack, PAL_PACK_ARCHIVE_TEXT, 1) ||
        expect_chunk_count(&nor.pack, PAL_PACK_ARCHIVE_FONT, 1) ||
        expect_chunk_count(&tf.pack, PAL_PACK_ARCHIVE_FBP, 72) ||
        expect_chunk_count(&tf.pack, PAL_PACK_ARCHIVE_MAP, 226) ||
        expect_chunk_count(&tf.pack, PAL_PACK_ARCHIVE_GOP, 226) ||
        expect_chunk_count(&tf.pack, PAL_PACK_ARCHIVE_RNG, 12) ||
        expect_chunk_count(&tf.pack, PAL_PACK_ARCHIVE_SFX, 276);
    if (rc == 0) {
        rc =
            map_native_nonempty(&nor.pack, PAL_PACK_ARCHIVE_MGO, 1) ||
            map_native_nonempty(&nor.pack, PAL_PACK_ARCHIVE_ABC, 1) ||
            map_native_nonempty(&nor.pack, PAL_PACK_ARCHIVE_FIRE, 0);
    }
    if (rc == 0) {
        rc = exercise_tf_reads(&tf.pack);
    }
    if (rc == 0) {
        rc = exercise_tf_toc_reads(&tf);
    }
    if (rc == 0) {
        rc = exercise_tf_toc_fd_reads(argv[2]);
    }
    if (rc == 0) {
        rc =
            check_scene(&nor.pack, &tf.pack, 59, 142, 122, 11) ||
            check_scene(&nor.pack, &tf.pack, 65, 120, 91, 8) ||
            check_scene(&nor.pack, &tf.pack, 156, 130, 123, 10) ||
            check_scene(&nor.pack, &tf.pack, 260, 72, 58, 11);
    }
    if (rc == 0) {
        rc = check_scene_pinned(&nor.pack, &tf.pack, 153, 14, 14, 7, 65150u, 65156u);
    }
    if (rc == 0) {
        rc =
            check_battle(&nor.pack, &tf.pack, 156, 3, 1) ||
            check_battle(&nor.pack, &tf.pack, 342, 3, 2) ||
            check_battle(&nor.pack, &tf.pack, 385, 3, 1);
    }
    if (rc == 0) {
        rc =
            check_rng_frame(&tf.pack, 4, 0, 41, 64288, PAL_RNG_FRAME_BUFFER_A) ||
            check_rng_frame(&tf.pack, 5, 0, 83, 64104, PAL_RNG_FRAME_BUFFER_B) ||
            check_rng_frame(&tf.pack, 9, 0, 257, 61773, PAL_RNG_FRAME_BUFFER_A);
    }
    if (rc == 0) {
        rc =
            check_rng_frame_readat(argv[2], 4, 0, 41, 64288, PAL_RNG_FRAME_BUFFER_A) ||
            check_rng_frame_readat(argv[2], 5, 0, 83, 64104, PAL_RNG_FRAME_BUFFER_B) ||
            check_rng_frame_readat(argv[2], 9, 0, 257, 61773, PAL_RNG_FRAME_BUFFER_A);
    }
    if (rc == 0) {
        rc = check_rng_all_frames_readat(argv[2]);
    }
    if (rc == 0) {
        rc = check_sfx_bank(&tf.pack);
    }
    if (rc == 0) {
        rc = check_global_cache(&nor.pack);
    }
    if (rc == 0) {
        rc = check_text_cache(&nor.pack);
    }
    if (rc == 0) {
        rc = check_font_cache(&nor.pack);
    }
    if (rc == 0) {
        rc = check_save_cache(argv[3]);
    }
    if (rc == 0) {
        rc = check_music_cache(&nor.pack);
    }
    if (rc == 0) {
        rc = check_menu_static(&nor.pack, &tf.pack);
    }
    if (rc == 0) {
        rc = check_ending_static(&nor.pack, &tf.pack);
    }
    if (rc == 0) {
        rc = check_palette_static(&nor.pack);
    }
    if (rc == 0) {
        rc = check_dialog_static(&nor.pack);
    }
    if (rc == 0) {
        rc = check_ui_cache(&nor.pack);
    }
    if (rc == 0) {
        rc = exercise_sdl_surface();
    }

    unmap_pack_file(&tf);
    unmap_pack_file(&nor);
    return rc == 0 ? 0 : 4;
}
