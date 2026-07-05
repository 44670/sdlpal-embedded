CC ?= gcc
OBJDUMP ?= objdump
NM ?= nm
SIZE ?= size
PYTHON ?= python3
SDL_CONFIG ?= pkg-config sdl2
PACK_NOR ?= /tmp/pal_nor_default.pak
PACK_TF ?= /tmp/pal_tf_default.pak
PAL_DATA_DIR ?= /mnt/hgfs/deb13/PAL

CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections
LDFLAGS ?= -Wl,--gc-sections
SDL_CFLAGS := $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS := $(shell $(SDL_CONFIG) --libs)

BUILD_DIR := build
SMOKE := $(BUILD_DIR)/pal_pack_smoke
MEMORY_SMOKE := $(BUILD_DIR)/pal_memory_smoke
NATIVE_SDL_SMOKE := $(BUILD_DIR)/pal_native_sdl_smoke
REALDATA_SDL_SMOKE := $(BUILD_DIR)/pal_realdata_sdl_smoke
PACK_CHECK := $(BUILD_DIR)/pal_pack_check

.PHONY: all clean check pack-build realdata-check artifact-check contract-check

all: $(SMOKE) $(MEMORY_SMOKE) $(NATIVE_SDL_SMOKE) $(REALDATA_SDL_SMOKE) $(PACK_CHECK)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SMOKE): pal_pack.c pal_pack.h pal_pack_smoke.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) pal_pack.c pal_pack_smoke.c -o $@ $(LDFLAGS)

$(MEMORY_SMOKE): pal_memory.c pal_memory.h pal_memory_smoke.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) pal_memory.c pal_memory_smoke.c -o $@ $(LDFLAGS)

$(NATIVE_SDL_SMOKE): pal_memory.c pal_memory.h pal_pack.c pal_pack.h pal_video_static.c pal_video_static.h pal_native_sdl_smoke.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) pal_memory.c pal_pack.c pal_video_static.c pal_native_sdl_smoke.c -o $@ $(LDFLAGS) $(SDL_LIBS)

$(REALDATA_SDL_SMOKE): pal_memory.c pal_memory.h pal_pack.c pal_pack.h pal_scene_cache.c pal_scene_cache.h pal_battle_cache.c pal_battle_cache.h pal_rng_cache.c pal_rng_cache.h pal_sfx_cache.c pal_sfx_cache.h pal_audio_static.c pal_audio_static.h pal_global_cache.c pal_global_cache.h pal_text_cache.c pal_text_cache.h pal_font_cache.c pal_font_cache.h pal_save_cache.c pal_save_cache.h pal_video_static.c pal_video_static.h pal_ui_cache.c pal_ui_cache.h pal_music_cache.c pal_music_cache.h pal_realdata_sdl_smoke.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) pal_memory.c pal_pack.c pal_scene_cache.c pal_battle_cache.c pal_rng_cache.c pal_sfx_cache.c pal_audio_static.c pal_global_cache.c pal_text_cache.c pal_font_cache.c pal_save_cache.c pal_video_static.c pal_ui_cache.c pal_music_cache.c pal_realdata_sdl_smoke.c -o $@ $(LDFLAGS) $(SDL_LIBS)

$(PACK_CHECK): pal_pack.c pal_pack.h ../tools/pal_pack_check.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I. pal_pack.c ../tools/pal_pack_check.c -o $@ $(LDFLAGS)

check: $(SMOKE) $(MEMORY_SMOKE) $(NATIVE_SDL_SMOKE)
	$(SMOKE)
	$(MEMORY_SMOKE)
	SDL_VIDEODRIVER=dummy $(NATIVE_SDL_SMOKE)

pack-build:
	$(PYTHON) -B ../tools/pal_pack_build.py $(PAL_DATA_DIR) --out-nor $(PACK_NOR) --out-tf $(PACK_TF)

realdata-check: pack-build $(PACK_CHECK) $(REALDATA_SDL_SMOKE)
	$(PACK_CHECK) $(PACK_NOR) $(PACK_TF)
	SDL_VIDEODRIVER=dummy $(REALDATA_SDL_SMOKE) $(PACK_NOR) $(PACK_TF) $(PAL_DATA_DIR)

contract-check: $(REALDATA_SDL_SMOKE)
	$(PYTHON) -B ../tools/embedded_contract_check.py \
		--root . \
		--fail-on-source \
		--binary $(REALDATA_SDL_SMOKE) \
		--max text=65536 \
		--max data=4096 \
		--max bss=7200000 \
		--max-symbol-prefix pal_sram_=307200 \
		--max-symbol-prefix pal_psram_=8388608 \
		--max-symbol-prefix pal_scene_=8192 \
		--max-symbol-prefix pal_battle_=2048 \
		--max-symbol-prefix pal_sfx_=4096 \
		--max-symbol-prefix pal_audio_=4096 \
		--max-symbol-prefix pal_global_=4096 \
		--max-symbol-prefix pal_save_=4096 \
		--max-symbol-prefix pal_video_=4096 \
		--max-symbol-prefix pal_ui_=4096 \
		--max-symbol-prefix pal_music_=4096

artifact-check: check $(REALDATA_SDL_SMOKE) $(PACK_CHECK)
	$(SIZE) $(SMOKE)
	$(OBJDUMP) -h $(SMOKE)
	$(NM) -S --size-sort -C $(SMOKE)
	$(SIZE) $(MEMORY_SMOKE)
	$(OBJDUMP) -h $(MEMORY_SMOKE)
	$(NM) -S --size-sort -C $(MEMORY_SMOKE)
	$(SIZE) $(NATIVE_SDL_SMOKE)
	$(OBJDUMP) -h $(NATIVE_SDL_SMOKE)
	$(NM) -S --size-sort -C $(NATIVE_SDL_SMOKE)
	$(SIZE) $(REALDATA_SDL_SMOKE)
	$(OBJDUMP) -h $(REALDATA_SDL_SMOKE)
	$(NM) -S --size-sort -C $(REALDATA_SDL_SMOKE)
	$(SIZE) $(PACK_CHECK)
	$(OBJDUMP) -h $(PACK_CHECK)
	$(NM) -S --size-sort -C $(PACK_CHECK)

clean:
	rm -rf $(BUILD_DIR)
