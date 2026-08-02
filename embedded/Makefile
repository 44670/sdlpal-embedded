CC ?= gcc
CXX ?= g++
OBJDUMP ?= objdump
NM ?= nm
SIZE ?= size
PYTHON ?= python3
SDL_CONFIG ?= pkg-config sdl2
PACK_NOR ?= /tmp/pal_nor_default.pak
PACK_TF ?= /tmp/pal_tf_default.pak
PACK_MANIFEST ?= /tmp/pal_pack_default_manifest.json
PAL_DATA_DIR ?= /mnt/hgfs/deb13/PALSteam/PAL_DOS
FONT10_ARCHIVE ?= $(abspath ../third_party/fusion-pixel-font/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip)
FONT10_PACK_NOR ?= /tmp/pal_nor_font10.pak
FONT10_PACK_TF ?= /tmp/pal_tf_font10.pak
FONT10_PACK_MANIFEST ?= /tmp/pal_pack_font10_manifest.json
FONT10_MAX_NOR_BYTES ?= 16777216

CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections
CXXFLAGS ?= -std=gnu++11 -Wall -Wextra -Werror -Os -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections
LDFLAGS ?= -Wl,--gc-sections
MAP_LDFLAGS = -Wl,-Map,$@.map,--cref
NOHEAP_LDFLAGS = -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
SDL_CFLAGS := $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS := $(shell $(SDL_CONFIG) --libs)
SANITIZE_BUILD_DIR ?= build-sanitize
SANITIZE_CFLAGS = -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -ffunction-sections -fdata-sections
SANITIZE_LDFLAGS = -fsanitize=address,undefined -Wl,--gc-sections

BUILD_DIR := build
SMOKE := $(BUILD_DIR)/pal_pack_smoke
MEMORY_SMOKE := $(BUILD_DIR)/pal_memory_smoke
NATIVE_SDL_SMOKE := $(BUILD_DIR)/pal_native_sdl_smoke
REALDATA_SDL_SMOKE := $(BUILD_DIR)/pal_realdata_sdl_smoke
PACK_CHECK := $(BUILD_DIR)/pal_pack_check
MAME_OPL2_STATIC_OBJ := $(BUILD_DIR)/pal_mame_opl2_static.o
MAME_OPL2_STATIC_SMOKE := $(BUILD_DIR)/pal_mame_opl2_static_smoke
RIX_MAME_OPL2_SMOKE := $(BUILD_DIR)/pal_rix_mame_opl2_smoke
RIX_MAME_OPL2_PLAYER_OBJ := $(BUILD_DIR)/pal_rix_player.o
RIX_MAME_OPL2_DECODER_OBJ := $(BUILD_DIR)/pal_rix_decoder.o
RIX_MAME_OPL2_PACK_OBJ := $(BUILD_DIR)/pal_rix_pack.o
RIX_MAME_OPL2_CACHE_OBJ := $(BUILD_DIR)/pal_rix_music_cache.o
RIX_MAME_OPL2_NOHEAP_OBJ := $(BUILD_DIR)/pal_rix_noheap.o
RIX_EXPECTED_TRACKS ?= 86
FONT10_SMOKE := $(BUILD_DIR)/pal_font10_cache_smoke
NATIVE_UI_HEADER_240 := ../esp32s3/main/generated/pal_native_ui_240x135.h
NATIVE_UI_HEADER_160 := ../esp32s3/main/generated/pal_native_ui_160x128.h
NATIVE_UI_240_SMOKE := $(BUILD_DIR)/pal_native_ui_240x135_smoke
NATIVE_UI_160_SMOKE := $(BUILD_DIR)/pal_native_ui_160x128_smoke
NATIVE_UI_SMOKES := $(NATIVE_UI_240_SMOKE) $(NATIVE_UI_160_SMOKE)
NATIVE_UI_240_OBJECT := $(BUILD_DIR)/pal_native_ui_240x135.o
NATIVE_UI_160_OBJECT := $(BUILD_DIR)/pal_native_ui_160x128.o
NATIVE_UI_OBJECTS := $(NATIVE_UI_240_OBJECT) $(NATIVE_UI_160_OBJECT)
NATIVE_UI_FLASH_MAX ?= 8192
NATIVE_UI_SRAM_MAX ?= 16

.PHONY: all clean check pack-build font10-pack-build font10-check realdata-check artifact-check contract-check sanitize-check mame-opl2-static-check rix-mame-opl2-check native-ui-check

all: $(SMOKE) $(MEMORY_SMOKE) $(NATIVE_SDL_SMOKE) $(REALDATA_SDL_SMOKE) $(PACK_CHECK) $(NATIVE_UI_SMOKES)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SMOKE): pal_noheap.c pal_pack.c pal_pack.h pal_pack_smoke.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) pal_noheap.c pal_pack.c pal_pack_smoke.c -o $@ $(LDFLAGS) $(NOHEAP_LDFLAGS) $(MAP_LDFLAGS)

$(MEMORY_SMOKE): pal_noheap.c pal_memory.c pal_memory.h pal_memory_smoke.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) pal_noheap.c pal_memory.c pal_memory_smoke.c -o $@ $(LDFLAGS) $(NOHEAP_LDFLAGS) $(MAP_LDFLAGS)

$(NATIVE_SDL_SMOKE): pal_noheap.c pal_memory.c pal_memory.h pal_pack.c pal_pack.h pal_video_static.c pal_video_static.h pal_native_sdl_smoke.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) pal_noheap.c pal_memory.c pal_pack.c pal_video_static.c pal_native_sdl_smoke.c -o $@ $(LDFLAGS) $(NOHEAP_LDFLAGS) $(MAP_LDFLAGS) $(SDL_LIBS)

$(REALDATA_SDL_SMOKE): pal_noheap.c pal_memory.c pal_memory.h pal_pack.c pal_pack.h pal_scene_cache.c pal_scene_cache.h pal_battle_cache.c pal_battle_cache.h pal_rng_cache.c pal_rng_cache.h pal_sfx_cache.c pal_sfx_cache.h pal_audio_static.c pal_audio_static.h pal_global_cache.c pal_global_cache.h pal_script_static.c pal_script_static.h pal_text_cache.c pal_text_cache.h pal_font_cache.c pal_font_cache.h pal_save_cache.c pal_save_cache.h pal_video_static.c pal_video_static.h pal_ui_cache.c pal_ui_cache.h pal_music_cache.c pal_music_cache.h pal_menu_static.c pal_menu_static.h pal_ending_static.c pal_ending_static.h pal_palette_static.c pal_palette_static.h pal_dialog_static.c pal_dialog_static.h pal_realdata_sdl_smoke.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) pal_noheap.c pal_memory.c pal_pack.c pal_scene_cache.c pal_battle_cache.c pal_rng_cache.c pal_sfx_cache.c pal_audio_static.c pal_global_cache.c pal_script_static.c pal_text_cache.c pal_font_cache.c pal_save_cache.c pal_video_static.c pal_ui_cache.c pal_music_cache.c pal_menu_static.c pal_ending_static.c pal_palette_static.c pal_dialog_static.c pal_realdata_sdl_smoke.c -o $@ $(LDFLAGS) $(NOHEAP_LDFLAGS) $(MAP_LDFLAGS) $(SDL_LIBS)

$(PACK_CHECK): pal_noheap.c pal_pack.c pal_pack.h ../tools/pal_pack_check.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I. pal_noheap.c pal_pack.c ../tools/pal_pack_check.c -o $@ $(LDFLAGS) $(NOHEAP_LDFLAGS) $(MAP_LDFLAGS)

$(FONT10_SMOKE): pal_noheap.c pal_pack.c pal_pack.h pal_font10_cache.c pal_font10_cache.h pal_font10_cache_smoke.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) pal_noheap.c pal_pack.c pal_font10_cache.c pal_font10_cache_smoke.c -o $@ $(LDFLAGS) $(NOHEAP_LDFLAGS) $(MAP_LDFLAGS)

$(NATIVE_UI_240_SMOKE): pal_noheap.c pal_native_ui.c pal_native_ui.h pal_native_ui_smoke.c $(NATIVE_UI_HEADER_240) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I../esp32s3/main -DPAL_NATIVE_UI_GENERATED_HEADER='"generated/pal_native_ui_240x135.h"' pal_noheap.c pal_native_ui.c pal_native_ui_smoke.c -o $@ $(LDFLAGS) $(NOHEAP_LDFLAGS) $(MAP_LDFLAGS)

$(NATIVE_UI_160_SMOKE): pal_noheap.c pal_native_ui.c pal_native_ui.h pal_native_ui_smoke.c $(NATIVE_UI_HEADER_160) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I../esp32s3/main -DPAL_NATIVE_UI_GENERATED_HEADER='"generated/pal_native_ui_160x128.h"' pal_noheap.c pal_native_ui.c pal_native_ui_smoke.c -o $@ $(LDFLAGS) $(NOHEAP_LDFLAGS) $(MAP_LDFLAGS)

$(NATIVE_UI_240_OBJECT): pal_native_ui.c pal_native_ui.h $(NATIVE_UI_HEADER_240) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I../esp32s3/main -DPAL_NATIVE_UI_GENERATED_HEADER='"generated/pal_native_ui_240x135.h"' -c pal_native_ui.c -o $@

$(NATIVE_UI_160_OBJECT): pal_native_ui.c pal_native_ui.h $(NATIVE_UI_HEADER_160) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I../esp32s3/main -DPAL_NATIVE_UI_GENERATED_HEADER='"generated/pal_native_ui_160x128.h"' -c pal_native_ui.c -o $@

native-ui-check: $(NATIVE_UI_SMOKES) $(NATIVE_UI_OBJECTS)
	$(NATIVE_UI_240_SMOKE)
	$(NATIVE_UI_160_SMOKE)
	! $(NM) -u $(NATIVE_UI_SMOKES) | grep -E 'malloc|calloc|realloc|free'
	$(PYTHON) -B ../tools/embedded_contract_check.py --root . --fail-on-source --source-file pal_native_ui.c --source-file pal_native_ui.h --binary $(NATIVE_UI_240_OBJECT) --max text=$(NATIVE_UI_FLASH_MAX) --max .data=$(NATIVE_UI_SRAM_MAX) --max .bss=$(NATIVE_UI_SRAM_MAX)
	$(PYTHON) -B ../tools/embedded_contract_check.py --root . --fail-on-source --source-file pal_native_ui.c --source-file pal_native_ui.h --binary $(NATIVE_UI_160_OBJECT) --max text=$(NATIVE_UI_FLASH_MAX) --max .data=$(NATIVE_UI_SRAM_MAX) --max .bss=$(NATIVE_UI_SRAM_MAX)

$(MAME_OPL2_STATIC_OBJ): pal_mame_opl2_static.cpp pal_mame_opl2_static.h pal_mame_opl2_fixed_tables.inc ../adplug/mame/fmopl.cpp.h ../adplug/mame/fmopl.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I.. -c pal_mame_opl2_static.cpp -o $@

$(MAME_OPL2_STATIC_SMOKE): pal_mame_opl2_static_smoke.cpp $(MAME_OPL2_STATIC_OBJ) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -Wno-class-memaccess -Wno-unused-parameter -Wno-unused-function -I.. pal_mame_opl2_static_smoke.cpp $(MAME_OPL2_STATIC_OBJ) -o $@ $(LDFLAGS) -lm

mame-opl2-static-check: $(MAME_OPL2_STATIC_SMOKE)
	$(PYTHON) -B ../tools/pal_mame_opl2_tables.py --output pal_mame_opl2_fixed_tables.inc --check
	$(MAME_OPL2_STATIC_SMOKE)
	! $(NM) -u -C $(MAME_OPL2_STATIC_OBJ) | grep -E 'malloc|calloc|realloc|free|operator new|operator delete|pow|sin|log'

$(RIX_MAME_OPL2_PLAYER_OBJ): ../adplug/player.cpp ../adplug/player.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -Wno-ignored-qualifiers -Wno-unused-parameter -Wno-unused-function -DPAL_NO_RUNTIME_HEAP=1 -DUSE_RIX_EXTRA_INIT=0 -DUSE_SDL3=0 -I../unix -I../sdl_compat -I.. -c $< -o $@

$(RIX_MAME_OPL2_DECODER_OBJ): ../adplug/rix.cpp ../adplug/rix.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -Wno-ignored-qualifiers -Wno-unused-parameter -Wno-unused-function -DPAL_NO_RUNTIME_HEAP=1 -DUSE_RIX_EXTRA_INIT=0 -DUSE_SDL3=0 -I../unix -I../sdl_compat -I.. -c $< -o $@

$(RIX_MAME_OPL2_PACK_OBJ): pal_pack.c pal_pack.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I.. -c $< -o $@

$(RIX_MAME_OPL2_CACHE_OBJ): pal_music_cache.c pal_music_cache.h pal_pack.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I.. -c $< -o $@

$(RIX_MAME_OPL2_NOHEAP_OBJ): pal_noheap.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I.. -c $< -o $@

$(RIX_MAME_OPL2_SMOKE): pal_rix_mame_opl2_smoke.cpp $(MAME_OPL2_STATIC_OBJ) $(RIX_MAME_OPL2_PLAYER_OBJ) $(RIX_MAME_OPL2_DECODER_OBJ) $(RIX_MAME_OPL2_PACK_OBJ) $(RIX_MAME_OPL2_CACHE_OBJ) $(RIX_MAME_OPL2_NOHEAP_OBJ) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -Wno-ignored-qualifiers -Wno-unused-parameter -Wno-unused-function -DPAL_NO_RUNTIME_HEAP=1 -DUSE_RIX_EXTRA_INIT=0 -DUSE_SDL3=0 -I../unix -I../sdl_compat -I.. pal_rix_mame_opl2_smoke.cpp $(MAME_OPL2_STATIC_OBJ) $(RIX_MAME_OPL2_PLAYER_OBJ) $(RIX_MAME_OPL2_DECODER_OBJ) $(RIX_MAME_OPL2_PACK_OBJ) $(RIX_MAME_OPL2_CACHE_OBJ) $(RIX_MAME_OPL2_NOHEAP_OBJ) -o $@ $(LDFLAGS) $(NOHEAP_LDFLAGS) $(SDL_LIBS)

rix-mame-opl2-check: $(RIX_MAME_OPL2_SMOKE)
	$(RIX_MAME_OPL2_SMOKE) $(PACK_NOR) $(RIX_EXPECTED_TRACKS)
	! $(NM) -u -C $(RIX_MAME_OPL2_PLAYER_OBJ) $(RIX_MAME_OPL2_DECODER_OBJ) $(MAME_OPL2_STATIC_OBJ) | grep -E ' U (malloc|calloc|realloc|free|pow|sin|log)(@[^ ]*)?$$| U operator (new|delete)'

check: $(SMOKE) $(MEMORY_SMOKE) $(NATIVE_SDL_SMOKE) $(NATIVE_UI_SMOKES) native-ui-check
	$(SMOKE)
	$(MEMORY_SMOKE)
	SDL_VIDEODRIVER=dummy $(NATIVE_SDL_SMOKE)
	$(NATIVE_UI_240_SMOKE)
	$(NATIVE_UI_160_SMOKE)

pack-build:
	$(PYTHON) -B ../tools/pal_pack_build.py $(PAL_DATA_DIR) --out-nor $(PACK_NOR) --out-tf $(PACK_TF) --manifest $(PACK_MANIFEST)

font10-pack-build:
	@test -n "$(FONT10_ARCHIVE)" || { echo "set FONT10_ARCHIVE to the pinned Fusion Pixel Font release zip" >&2; exit 2; }
	$(PYTHON) -B ../tools/pal_pack_build.py $(PAL_DATA_DIR) \
		--font10-archive "$(FONT10_ARCHIVE)" \
		--out-nor $(FONT10_PACK_NOR) \
		--out-tf $(FONT10_PACK_TF) \
		--manifest $(FONT10_PACK_MANIFEST)

font10-check: font10-pack-build $(FONT10_SMOKE) $(PACK_CHECK)
	$(PACK_CHECK) $(FONT10_PACK_NOR) $(FONT10_PACK_TF)
	$(FONT10_SMOKE) $(FONT10_PACK_NOR)
	! $(NM) -u $(FONT10_SMOKE) | grep -E 'malloc|calloc|realloc|free'
	$(PYTHON) -B ../tools/embedded_contract_check.py \
		--root . \
		--fail-on-source \
		--source-file pal_font10_cache.c \
		--source-file pal_font10_cache.h \
		--source-file pal_font10_cache_smoke.c \
		--binary $(FONT10_SMOKE) \
		--pack $(FONT10_PACK_NOR) \
		--pack $(FONT10_PACK_TF) \
		--manifest $(FONT10_PACK_MANIFEST) \
		--forbid-pack-archive VOC \
		--max-pack-size $(FONT10_PACK_NOR)=$(FONT10_MAX_NOR_BYTES)

realdata-check: pack-build $(PACK_CHECK) $(REALDATA_SDL_SMOKE)
	$(PACK_CHECK) $(PACK_NOR) $(PACK_TF)
	SDL_VIDEODRIVER=dummy $(REALDATA_SDL_SMOKE) $(PACK_NOR) $(PACK_TF) $(PAL_DATA_DIR)

sanitize-check:
	$(MAKE) BUILD_DIR=$(SANITIZE_BUILD_DIR) CFLAGS='$(SANITIZE_CFLAGS)' LDFLAGS='$(SANITIZE_LDFLAGS)' realdata-check

contract-check: pack-build $(REALDATA_SDL_SMOKE)
	$(PYTHON) -B ../tools/embedded_contract_check.py \
		--root . \
		--fail-on-source \
		--binary $(REALDATA_SDL_SMOKE) \
		--link-map $(REALDATA_SDL_SMOKE).map \
		--pack $(PACK_NOR) \
		--pack $(PACK_TF) \
		--manifest $(PACK_MANIFEST) \
		--forbid-pack-archive VOC \
		--max-pack-size $(PACK_NOR)=16777216 \
		--max text=65536 \
		--max .rodata=4096 \
		--max data=4096 \
		--max bss=7500000 \
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
		--max-symbol-prefix pal_music_=4096 \
		--max-symbol-prefix pal_menu_=4096 \
		--max-symbol-prefix pal_ending_=4096 \
		--max-symbol-prefix pal_palette_=4096 \
		--max-symbol-prefix pal_dialog_=4096

artifact-check: check $(REALDATA_SDL_SMOKE) $(PACK_CHECK)
	$(SIZE) $(SMOKE)
	$(OBJDUMP) -h $(SMOKE)
	$(OBJDUMP) -t $(SMOKE)
	$(NM) -S --size-sort -C $(SMOKE)
	$(SIZE) $(MEMORY_SMOKE)
	$(OBJDUMP) -h $(MEMORY_SMOKE)
	$(OBJDUMP) -t $(MEMORY_SMOKE)
	$(NM) -S --size-sort -C $(MEMORY_SMOKE)
	$(SIZE) $(NATIVE_SDL_SMOKE)
	$(OBJDUMP) -h $(NATIVE_SDL_SMOKE)
	$(OBJDUMP) -t $(NATIVE_SDL_SMOKE)
	$(NM) -S --size-sort -C $(NATIVE_SDL_SMOKE)
	$(SIZE) $(REALDATA_SDL_SMOKE)
	$(OBJDUMP) -h $(REALDATA_SDL_SMOKE)
	$(OBJDUMP) -t $(REALDATA_SDL_SMOKE)
	$(NM) -S --size-sort -C $(REALDATA_SDL_SMOKE)
	$(SIZE) $(PACK_CHECK)
	$(OBJDUMP) -h $(PACK_CHECK)
	$(OBJDUMP) -t $(PACK_CHECK)
	$(NM) -S --size-sort -C $(PACK_CHECK)

clean:
	rm -rf $(BUILD_DIR)
