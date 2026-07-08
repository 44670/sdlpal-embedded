PYTHON ?= python3
PAL_DATA_DIR ?= /mnt/hgfs/deb13/PAL
PACK_NOR ?= /tmp/pal_nor_default.pak
PACK_TF ?= /tmp/pal_tf_default.pak
PACK_MANIFEST ?= /tmp/pal_pack_default_manifest.json
PACK_LAYOUT ?= ../tools/pal_pack_layout_default.json
PACK_NOR_ARCHIVES ?=
PACK_TF_ARCHIVES ?=
SCAFFOLD_BUILD_DIR ?= ../build-cores3se-scaffold
BUILD_DIR ?= $(SCAFFOLD_BUILD_DIR)
ENGINE_HOST_BUILD_DIR ?= ../build-cores3se-engine-host
NATIVE_BUILD_DIR ?= /tmp/sdlpal-cores3se-native
NATIVE_TARGET ?= $(NATIVE_BUILD_DIR)/cores3se_native_smoke
NATIVE_SCREENSHOT ?= $(NATIVE_BUILD_DIR)/cores3se_scene.png
NATIVE_ENGINE_SCREENSHOT ?= $(NATIVE_BUILD_DIR)/cores3se_engine.png
NATIVE_ENGINE_SHIM_SCREENSHOT ?= $(NATIVE_BUILD_DIR)/cores3se_engine_shim.png
NATIVE_ENGINE_BRIDGE_TARGET ?= $(NATIVE_BUILD_DIR)/cores3se_engine_bridge
NATIVE_ENGINE_BRIDGE_SCREENSHOT ?= $(NATIVE_BUILD_DIR)/cores3se_engine_bridge.png
NATIVE_ENGINE_HOST_TARGET ?= $(NATIVE_BUILD_DIR)/cores3se_engine_host
NATIVE_ENGINE_HOST_SCREENSHOT ?= $(NATIVE_BUILD_DIR)/cores3se_engine_host.png
NATIVE_ENGINE_HOST_BUILD_DIR ?= $(NATIVE_BUILD_DIR)/engine-host-objects
NATIVE_ENGINE_HOST_OBJECTS ?= $(NATIVE_ENGINE_HOST_BUILD_DIR)/objects.list
NATIVE_ENGINE_HOST_DETERMINISTIC_TARGET ?= $(NATIVE_BUILD_DIR)/cores3se_engine_host_deterministic
NATIVE_ENGINE_HOST_DETERMINISTIC_SCREENSHOT ?= $(NATIVE_BUILD_DIR)/cores3se_engine_host_deterministic.png
NATIVE_ENGINE_HOST_DETERMINISTIC_BUILD_DIR ?= $(NATIVE_BUILD_DIR)/engine-host-deterministic-objects
NATIVE_ENGINE_HOST_DETERMINISTIC_OBJECTS ?= $(NATIVE_ENGINE_HOST_DETERMINISTIC_BUILD_DIR)/objects.list
NATIVE_PACK_PROVIDER_CACHE_TARGET ?= $(NATIVE_BUILD_DIR)/pack_provider_cache_smoke
NATIVE_ENGINE_SHIM_BUILD_DIR ?= native_engine_shim/build
NATIVE_ENGINE_SHIM_LIB ?= $(NATIVE_ENGINE_SHIM_BUILD_DIR)/libsdlshim.a
NATIVE_ENGINE_SHIM_SDL_CONFIG = $(PYTHON) ../esp32s3/native_engine_shim/sdl2_config.py
NATIVE_ENGINE_BRIDGE_ROUTE ?= ../tools/deterministic_routes/newgame_walk.route
NATIVE_ENGINE_BRIDGE_FRAMES ?= 3000
NATIVE_ENGINE_SAVE_FRAME ?= 2500
NATIVE_ENGINE_SAVE_SLOT ?= 5
NATIVE_ENGINE_SAVE_TIMES ?= 99
NATIVE_ENGINE_RELOAD_FRAME ?= 2600
NATIVE_ENGINE_RELOAD_SLOT ?= $(NATIVE_ENGINE_SAVE_SLOT)
IDF_ENGINE_BRIDGE_BUILD_DIR ?= $(NATIVE_BUILD_DIR)/idf-engine-objects
IDF_ENGINE_BRIDGE_OBJECTS ?= $(IDF_ENGINE_BRIDGE_BUILD_DIR)/objects.list
IDF_ENGINE_BRIDGE_ELF ?= $(IDF_ENGINE_BRIDGE_BUILD_DIR)/sdlpal-engine-bridge.elf
IDF_EXPORT ?= /home/john/esp-idf/export.sh
PORT ?=
BAUD ?= 460800
ENGINE_HARDWARE_SMOKE_TIMEOUT ?= 45
ENGINE_HARDWARE_SMOKE_LOG ?= $(NATIVE_BUILD_DIR)/cores3se_engine_hardware.log
TF_MOUNT ?=
PAL_NOR_OFFSET ?= 0x310000
FLASH_MODE ?= dio
FLASH_SIZE ?= 16MB
FLASH_FREQ ?= 80m
LIBPNG_CFLAGS := $(shell pkg-config --cflags libpng)
LIBPNG_LIBS := $(shell pkg-config --libs libpng)
PACK_BUILD_ARGS = --layout "$(PACK_LAYOUT)"
ifneq ($(strip $(PACK_NOR_ARCHIVES)),)
PACK_BUILD_ARGS += --nor "$(PACK_NOR_ARCHIVES)"
endif
ifneq ($(strip $(PACK_TF_ARCHIVES)),)
PACK_BUILD_ARGS += --tf "$(PACK_TF_ARCHIVES)"
endif

NATIVE_CFLAGS = -std=c99 -Wall -Wextra -O2 -ffunction-sections -fdata-sections \
	-I native_shim/include -I main -I ../embedded \
	$(LIBPNG_CFLAGS) \
	-DPAL_ESP_CORES3SE=1 \
	-DPAL_CORES3SE_NATIVE=1 \
	-DPAL_SCENE_ENABLE_PINNED=1 \
	-DPAL_ESP_CORES3SE_NO_AUDIO=1 \
	-DPAL_ESP_CORES3SE_NO_SFX=1
NATIVE_LDFLAGS = -Wl,--gc-sections $(LIBPNG_LIBS)
NATIVE_SRCS = \
	main/app_main.c \
	main/cores3se_memory.c \
	main/pal_save_fatfs.c \
	native_shim/cores3se_native_shim.c \
	../embedded/pal_battle_cache.c \
	../embedded/pal_dialog_static.c \
	../embedded/pal_ending_static.c \
	../embedded/pal_font_cache.c \
	../embedded/pal_global_cache.c \
	../embedded/pal_menu_static.c \
	../embedded/pal_music_cache.c \
	../embedded/pal_pack.c \
	../embedded/pal_palette_static.c \
	../embedded/pal_rng_cache.c \
	../embedded/pal_scene_cache.c \
	../embedded/pal_text_cache.c \
	../embedded/pal_ui_cache.c \
	../embedded/pal_video_static.c

NATIVE_ENGINE_BRIDGE_CFLAGS = -std=gnu99 -Wall -O2 -g -fno-strict-aliasing \
	-ffunction-sections -fdata-sections \
	-I engine_bridge -I engine_bridge/include -I native_engine_shim/include -I .. -I ../sdl_compat -I ../embedded \
	$(LIBPNG_CFLAGS) \
	-DPAL_CORES3SE_NATIVE_BRIDGE=1 \
	-DPAL_HEADLESS_SDL_SHIM=1 \
	-DPAL_HAS_JOYSTICKS=0 \
	-DPAL_HAS_TOUCH=0 \
	-DPAL_HAS_MP3=0 \
	-DPAL_HAS_OGG=0 \
	-DPAL_HAS_OPUS=0 \
	-DPAL_HAS_NATIVEMIDI=0 \
	-DPAL_HAS_GLSL=0 \
	-DPAL_HAS_CONFIG_PAGE=0 \
	-DPAL_NO_LAUNCH_UI \
	-DPAL_NO_RUNTIME_DECOMPRESS=1 \
	-DPAL_NO_RUNTIME_HEAP=1 \
	-DPAL_CONTRACT_EXTERNAL_RIX=1 \
	-DUSE_RIX_EXTRA_INIT=0 \
	-DPAL_CONTRACT_NO_AUDIO=1 \
	-DPAL_ESP_CORES3SE_NO_AUDIO=1 \
	-DPAL_ESP_CORES3SE_NO_SFX=1 \
	-DUSE_SDL3=0 \
	-DPAL_DETERMINISTIC=1
NATIVE_ENGINE_BRIDGE_LDFLAGS = -Wl,--gc-sections \
	-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
	-Wl,--wrap=SDL_RenderPresent \
	-Wl,--wrap=PAL_RunTriggerScript \
	-Wl,--wrap=PAL_RunAutoScript \
	-Wl,--wrap=PAL_SaveGame \
	-Wl,--wrap=PAL_InitGameData \
	-Wl,--wrap=PAL_ReloadInNextTick \
	-Wl,--wrap=PAL_MKFOpenPackArchive \
	-Wl,--wrap=PAL_MKFIsPackArchive \
	-Wl,--wrap=PAL_MKFMapChunk \
	-Wl,--wrap=PAL_MKFGetChunkCount \
	-Wl,--wrap=PAL_MKFGetChunkSize \
	-Wl,--wrap=PAL_MKFReadChunk \
	-Wl,--wrap=fclose \
	$(LIBPNG_LIBS) -lm
NATIVE_ENGINE_BRIDGE_SRCS = \
	../battle.c \
	../ending.c \
	../fight.c \
	../game.c \
	../global.c \
	../input.c \
	../itemmenu.c \
	../magicmenu.c \
	../main.c \
	../map.c \
	../overlay.c \
	../palcfg.c \
	../palcommon.c \
	../paldebug.c \
	../palette.c \
	../play.c \
	../res.c \
	../rngplay.c \
	../scene.c \
	../script.c \
	../ui.c \
	../uibattle.c \
	../uigame.c \
	../util.c \
	../video.c \
	../sdl_compat/sdl_compat.c \
	../unix/embedded_contract_stubs.c \
	../unix/contract_noaudio.c \
	../embedded/pal_noheap.c \
	../embedded/pal_pack.c \
	../embedded/pal_text_cache.c \
	../embedded/pal_font_cache.c \
	engine_bridge/pal_engine_pack_provider.c \
	engine_bridge/pal_engine_native_packs.c \
	../unix/deterministic.c
IDF_ENGINE_BRIDGE_SRCS = \
	../battle.c \
	../ending.c \
	../fight.c \
	../game.c \
	../global.c \
	../input.c \
	../itemmenu.c \
	../magicmenu.c \
	../main.c \
	../map.c \
	../overlay.c \
	../palcfg.c \
	../palcommon.c \
	../paldebug.c \
	../palette.c \
	../play.c \
	../res.c \
	../rngplay.c \
	../scene.c \
	../script.c \
	../ui.c \
	../uibattle.c \
	../uigame.c \
	../util.c \
	../video.c \
	../sdl_compat/sdl_compat.c \
	../unix/embedded_contract_stubs.c \
	../unix/contract_noaudio.c \
	../embedded/pal_noheap.c \
	../embedded/pal_pack.c \
	../embedded/pal_text_cache.c \
	../embedded/pal_font_cache.c \
	engine_bridge/pal_engine_pack_provider.c \
	engine_bridge/pal_engine_posix_shims.c \
	native_engine_shim/sdl_shim.c
IDF_ENGINE_BRIDGE_TARGET_GLUE_SRCS = \
	engine_bridge/pal_engine_app_main.c \
	engine_bridge/pal_engine_target_input.c \
	engine_bridge/pal_engine_target_time.c \
	engine_bridge/pal_engine_target_video.c \
	engine_bridge/pal_engine_target_packs.c
IDF_ENGINE_BRIDGE_CFLAGS = -std=gnu99 -Wall -O2 -fno-strict-aliasing \
	-ffunction-sections -fdata-sections \
	-I engine_bridge -I engine_bridge/include -I main -I native_engine_shim/include -I native_shim/include -I .. -I ../sdl_compat -I ../embedded \
	-DPAL_HEADLESS_SDL_SHIM=1 \
	-DPAL_HAS_JOYSTICKS=0 \
	-DPAL_HAS_TOUCH=0 \
	-DPAL_HAS_MP3=0 \
	-DPAL_HAS_OGG=0 \
	-DPAL_HAS_OPUS=0 \
	-DPAL_HAS_NATIVEMIDI=0 \
	-DPAL_HAS_GLSL=0 \
	-DPAL_HAS_CONFIG_PAGE=0 \
	-DPAL_NO_LAUNCH_UI \
	-DPAL_NO_RUNTIME_DECOMPRESS=1 \
	-DPAL_NO_RUNTIME_HEAP=1 \
	-DPAL_CONTRACT_EXTERNAL_RIX=1 \
	-DPAL_CONTRACT_TARGET_PACK_PROVIDER=1 \
	-DUSE_RIX_EXTRA_INIT=0 \
	-DPAL_CONTRACT_NO_AUDIO=1 \
	-DPAL_ESP_CORES3SE_NO_AUDIO=1 \
	-DPAL_ESP_CORES3SE_NO_SFX=1 \
	-DSHIM_MAX_SURFACES=8 \
	-DSHIM_MAX_PALETTES=8 \
	-DSHIM_MAX_TEXTURES=1 \
	-DSHIM_SURFACE_PIXELS=65536u \
	-DUSE_SDL3=0
IDF_ENGINE_BRIDGE_LDFLAGS = -nostartfiles -Wl,--gc-sections -Wl,-e,PAL_EngineMain \
	-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
	-Wl,--wrap=PAL_MKFOpenPackArchive \
	-Wl,--wrap=PAL_MKFIsPackArchive \
	-Wl,--wrap=PAL_MKFMapChunk \
	-Wl,--wrap=PAL_MKFGetChunkCount \
	-Wl,--wrap=PAL_MKFGetChunkSize \
	-Wl,--wrap=PAL_MKFReadChunk \
	-Wl,--wrap=fclose

NATIVE_ENGINE_HOST_CFLAGS = -std=gnu99 -Wall -O2 -g -fno-strict-aliasing \
	-ffunction-sections -fdata-sections \
	-I engine_bridge -I engine_bridge/include -I main -I native_engine_shim/include -I native_shim/include -I .. -I ../sdl_compat -I ../embedded \
	$(LIBPNG_CFLAGS) \
	-DPAL_CORES3SE_NATIVE=1 \
	-DPAL_CORES3SE_NATIVE_ENGINE_HOST=1 \
	-DPAL_HEADLESS_SDL_SHIM=1 \
	-DPAL_HAS_JOYSTICKS=0 \
	-DPAL_HAS_TOUCH=0 \
	-DPAL_HAS_MP3=0 \
	-DPAL_HAS_OGG=0 \
	-DPAL_HAS_OPUS=0 \
	-DPAL_HAS_NATIVEMIDI=0 \
	-DPAL_HAS_GLSL=0 \
	-DPAL_HAS_CONFIG_PAGE=0 \
	-DPAL_NO_LAUNCH_UI \
	-DPAL_NO_RUNTIME_DECOMPRESS=1 \
	-DPAL_NO_RUNTIME_HEAP=1 \
	-DPAL_CONTRACT_EXTERNAL_RIX=1 \
	-DPAL_CONTRACT_TARGET_PACK_PROVIDER=1 \
	-DPAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS=1 \
	-DUSE_RIX_EXTRA_INIT=0 \
	-DPAL_CONTRACT_NO_AUDIO=1 \
	-DPAL_ESP_CORES3SE_NO_AUDIO=1 \
	-DPAL_ESP_CORES3SE_NO_SFX=1 \
	-DSHIM_MAX_SURFACES=8 \
	-DSHIM_MAX_PALETTES=8 \
	-DSHIM_MAX_TEXTURES=1 \
	-DSHIM_SURFACE_PIXELS=65536u \
	-DUSE_SDL3=0
NATIVE_ENGINE_HOST_LDFLAGS = -Wl,--gc-sections \
	-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
	-Wl,--wrap=PAL_MKFOpenPackArchive \
	-Wl,--wrap=PAL_MKFIsPackArchive \
	-Wl,--wrap=PAL_MKFMapChunk \
	-Wl,--wrap=PAL_MKFGetChunkCount \
	-Wl,--wrap=PAL_MKFGetChunkSize \
	-Wl,--wrap=PAL_MKFReadChunk \
	-Wl,--wrap=fopen \
	-Wl,--wrap=fclose \
	-Wl,--wrap=fread \
	-Wl,--wrap=fwrite \
	-Wl,--wrap=fseek \
	-Wl,--wrap=ftell \
	-Wl,--wrap=fgets \
	-Wl,--wrap=fputs \
	-Wl,--wrap=fflush \
	-Wl,--wrap=feof \
	-Wl,--wrap=ferror \
	-Wl,--wrap=rewind \
	$(LIBPNG_LIBS) -lm
NATIVE_ENGINE_HOST_SRCS = \
	../battle.c \
	../ending.c \
	../fight.c \
	../game.c \
	../global.c \
	../input.c \
	../itemmenu.c \
	../magicmenu.c \
	../main.c \
	../map.c \
	../overlay.c \
	../palcfg.c \
	../palcommon.c \
	../paldebug.c \
	../palette.c \
	../play.c \
	../res.c \
	../rngplay.c \
	../scene.c \
	../script.c \
	../ui.c \
	../uibattle.c \
	../uigame.c \
	../util.c \
	../video.c \
	../sdl_compat/sdl_compat.c \
	../unix/embedded_contract_stubs.c \
	../unix/contract_noaudio.c \
	../embedded/pal_noheap.c \
	../embedded/pal_pack.c \
	../embedded/pal_text_cache.c \
	../embedded/pal_font_cache.c \
	engine_bridge/pal_engine_app_main.c \
	engine_bridge/pal_engine_fatfs_stdio.c \
	engine_bridge/pal_engine_pack_provider.c \
	engine_bridge/pal_engine_posix_shims.c \
	engine_bridge/pal_engine_target_input.c \
	engine_bridge/pal_engine_target_packs.c \
	engine_bridge/pal_engine_target_time.c \
	engine_bridge/pal_engine_target_video.c \
	main/cores3se_memory.c \
	native_engine_shim/sdl_shim.c \
	native_shim/cores3se_native_shim.c

NATIVE_ENGINE_HOST_DETERMINISTIC_CFLAGS = $(NATIVE_ENGINE_HOST_CFLAGS) \
	-DPAL_DETERMINISTIC=1
NATIVE_ENGINE_HOST_DETERMINISTIC_LDFLAGS = $(NATIVE_ENGINE_HOST_LDFLAGS) \
	-Wl,--wrap=SDL_RenderPresent \
	-Wl,--wrap=PAL_RunTriggerScript \
	-Wl,--wrap=PAL_RunAutoScript \
	-Wl,--wrap=PAL_SaveGame \
	-Wl,--wrap=PAL_InitGameData \
	-Wl,--wrap=PAL_ReloadInNextTick
NATIVE_ENGINE_HOST_DETERMINISTIC_SRCS = \
	$(NATIVE_ENGINE_HOST_SRCS) \
	../unix/deterministic.c

.PHONY: all build scaffold-build engine-port-check engine-host-build engine-host-check pack-build prepare-tf check native-smoke native-engine-smoke native-engine-shim-smoke native-engine-bridge-smoke native-engine-host-smoke native-engine-host-parity-check native-engine-host-save-parity-check native-pack-provider-cache-smoke engine-bridge-idf-compile engine-bridge-idf-link native-engine-parity-check native-engine-shim-parity-check native-engine-bridge-parity-check flash-app flash-scaffold flash-engine-host flash-engine-port engine-hardware-smoke flash-nor clean

all: build

build: engine-host-build

scaffold-build:
	bash -lc 'source "$(IDF_EXPORT)" >/dev/null && if [ ! -f "$(BUILD_DIR)/build.ninja" ]; then idf.py -C . -B "$(BUILD_DIR)" -DPAL_CORES3SE_ENGINE_HOST=0 set-target esp32s3; fi && idf.py -C . -B "$(BUILD_DIR)" -DPAL_CORES3SE_ENGINE_HOST=0 build'

engine-host-build:
	bash -lc 'source "$(IDF_EXPORT)" >/dev/null && if [ ! -f "$(ENGINE_HOST_BUILD_DIR)/build.ninja" ]; then idf.py -C . -B "$(ENGINE_HOST_BUILD_DIR)" -DPAL_CORES3SE_ENGINE_HOST=1 set-target esp32s3; fi && idf.py -C . -B "$(ENGINE_HOST_BUILD_DIR)" -DPAL_CORES3SE_ENGINE_HOST=1 build'

engine-host-check: engine-host-build pack-build
	bash -lc 'source "$(IDF_EXPORT)" >/dev/null && $(PYTHON) -B check_engine_host.py --root .. --build-dir "$(ENGINE_HOST_BUILD_DIR)" --nor-pack "$(PACK_NOR)" --tf-pack "$(PACK_TF)" --manifest "$(PACK_MANIFEST)"'

engine-port-check:
	$(MAKE) native-pack-provider-cache-smoke
	$(MAKE) native-engine-parity-check
	$(MAKE) native-engine-bridge-smoke
	$(MAKE) native-engine-host-smoke
	$(MAKE) native-engine-bridge-parity-check
	$(MAKE) native-engine-host-parity-check
	$(MAKE) native-engine-host-save-parity-check
	$(MAKE) engine-bridge-idf-link
	$(MAKE) engine-host-check

pack-build:
	$(PYTHON) -B ../tools/pal_pack_build.py $(PAL_DATA_DIR) $(PACK_BUILD_ARGS) --out-nor $(PACK_NOR) --out-tf $(PACK_TF) --manifest $(PACK_MANIFEST)

prepare-tf: pack-build
	@test -n "$(TF_MOUNT)" || { echo "TF_MOUNT=/media/... is required"; exit 2; }
	@test -d "$(TF_MOUNT)" || { echo "TF_MOUNT does not exist: $(TF_MOUNT)"; exit 2; }
	cp -f "$(PACK_TF)" "$(TF_MOUNT)/pal_tf.pak"
	sync "$(TF_MOUNT)/pal_tf.pak"
	@ls -l "$(TF_MOUNT)/pal_tf.pak"

check: scaffold-build pack-build
	bash -lc 'source "$(IDF_EXPORT)" >/dev/null && $(PYTHON) -B check_contract.py --root .. --build-dir "$(BUILD_DIR)" --nor-pack "$(PACK_NOR)" --tf-pack "$(PACK_TF)" --manifest "$(PACK_MANIFEST)"'

$(NATIVE_TARGET): $(NATIVE_SRCS) Makefile
	mkdir -p "$(NATIVE_BUILD_DIR)"
	$(CC) $(NATIVE_CFLAGS) $(NATIVE_SRCS) -o "$@" $(NATIVE_LDFLAGS)

$(NATIVE_ENGINE_SHIM_LIB): native_engine_shim/sdl_shim.c native_engine_shim/include/SDL.h native_engine_shim/include/SDL_endian.h native_engine_shim/include/SDL_events.h native_engine_shim/include/SDL_video.h Makefile
	mkdir -p "$(dir $(NATIVE_ENGINE_SHIM_LIB))"
	$(CC) -std=c99 -Wall -Wextra -O2 -I native_engine_shim/include -c native_engine_shim/sdl_shim.c -o "$(dir $(NATIVE_ENGINE_SHIM_LIB))/sdl_shim.o"
	$(AR) rcs "$(NATIVE_ENGINE_SHIM_LIB)" "$(dir $(NATIVE_ENGINE_SHIM_LIB))/sdl_shim.o"

$(NATIVE_ENGINE_BRIDGE_TARGET): $(NATIVE_ENGINE_BRIDGE_SRCS) engine_bridge/include/pal_config.h $(NATIVE_ENGINE_SHIM_LIB) Makefile
	mkdir -p "$(NATIVE_BUILD_DIR)"
	$(CC) $(NATIVE_ENGINE_BRIDGE_CFLAGS) $(NATIVE_ENGINE_BRIDGE_SRCS) "$(NATIVE_ENGINE_SHIM_LIB)" -o "$@" $(NATIVE_ENGINE_BRIDGE_LDFLAGS)

$(NATIVE_ENGINE_HOST_TARGET): $(NATIVE_ENGINE_HOST_SRCS) engine_bridge/include/pal_config.h engine_bridge/include/sys/mman.h native_engine_shim/include/SDL.h native_engine_shim/include/SDL_endian.h native_engine_shim/include/SDL_events.h native_engine_shim/include/SDL_video.h native_shim/include/ff.h Makefile
	mkdir -p "$(NATIVE_ENGINE_HOST_BUILD_DIR)"
	: >"$(NATIVE_ENGINE_HOST_OBJECTS)"
	@set -e; for src in $(NATIVE_ENGINE_HOST_SRCS); do \
		obj="$(NATIVE_ENGINE_HOST_BUILD_DIR)/$$(basename "$$src" .c).native.o"; \
		extra=""; \
		case "$$src" in ../main.c) extra="-Dmain=PAL_EngineMain";; esac; \
		echo "[NATIVE-HOST-CC] $$src"; \
		$(CC) $(NATIVE_ENGINE_HOST_CFLAGS) $$extra -c "$$src" -o "$$obj"; \
		printf "%s\n" "$$obj" >>"$(NATIVE_ENGINE_HOST_OBJECTS)"; \
	done
	$(CC) $$(cat "$(NATIVE_ENGINE_HOST_OBJECTS)") -o "$@" $(NATIVE_ENGINE_HOST_LDFLAGS)

$(NATIVE_ENGINE_HOST_DETERMINISTIC_TARGET): $(NATIVE_ENGINE_HOST_DETERMINISTIC_SRCS) engine_bridge/include/pal_config.h engine_bridge/include/sys/mman.h native_engine_shim/include/SDL.h native_engine_shim/include/SDL_endian.h native_engine_shim/include/SDL_events.h native_engine_shim/include/SDL_video.h native_shim/include/ff.h Makefile
	mkdir -p "$(NATIVE_ENGINE_HOST_DETERMINISTIC_BUILD_DIR)"
	: >"$(NATIVE_ENGINE_HOST_DETERMINISTIC_OBJECTS)"
	@set -e; for src in $(NATIVE_ENGINE_HOST_DETERMINISTIC_SRCS); do \
		obj="$(NATIVE_ENGINE_HOST_DETERMINISTIC_BUILD_DIR)/$$(basename "$$src" .c).native.o"; \
		extra=""; \
		case "$$src" in ../main.c) extra="-Dmain=PAL_EngineMain";; esac; \
		echo "[NATIVE-HOST-DET-CC] $$src"; \
		$(CC) $(NATIVE_ENGINE_HOST_DETERMINISTIC_CFLAGS) $$extra -c "$$src" -o "$$obj"; \
		printf "%s\n" "$$obj" >>"$(NATIVE_ENGINE_HOST_DETERMINISTIC_OBJECTS)"; \
	done
	$(CC) $$(cat "$(NATIVE_ENGINE_HOST_DETERMINISTIC_OBJECTS)") -o "$@" $(NATIVE_ENGINE_HOST_DETERMINISTIC_LDFLAGS)

$(NATIVE_PACK_PROVIDER_CACHE_TARGET): native_shim/pack_provider_cache_smoke.c engine_bridge/pal_engine_pack_provider.c ../embedded/pal_pack.c engine_bridge/pal_engine_pack_provider.h Makefile
	mkdir -p "$(NATIVE_BUILD_DIR)"
	$(CC) -std=gnu99 -Wall -Wextra -O2 \
		-I engine_bridge -I engine_bridge/include -I native_engine_shim/include -I .. -I ../embedded -I ../sdl_compat \
		native_shim/pack_provider_cache_smoke.c engine_bridge/pal_engine_pack_provider.c ../embedded/pal_pack.c \
		-o "$@" -Wl,--wrap=fclose

native-pack-provider-cache-smoke: pack-build $(NATIVE_PACK_PROVIDER_CACHE_TARGET)
	"$(NATIVE_PACK_PROVIDER_CACHE_TARGET)" "$(PACK_TF)"

native-smoke: pack-build $(NATIVE_TARGET)
	PAL_CORES3SE_NATIVE_NOR_PACK="$(abspath $(PACK_NOR))" \
	PAL_CORES3SE_NATIVE_TF_PACK="$(abspath $(PACK_TF))" \
	PAL_CORES3SE_NATIVE_SCREENSHOT="$(abspath $(NATIVE_SCREENSHOT))" \
	PAL_CORES3SE_NATIVE_SCENE=1 \
	PAL_CORES3SE_NATIVE_VIEW_X=512 \
	PAL_CORES3SE_NATIVE_VIEW_Y=960 \
	PAL_CORES3SE_NATIVE_FRAMES=1 \
	"$(NATIVE_TARGET)"
	$(PYTHON) -B native_shim/check_png.py "$(NATIVE_SCREENSHOT)" 320 240 10000 32

native-engine-smoke:
	$(MAKE) -C ../unix deterministic-newgame-cores3se-png-check \
		EMBEDDED_CONTRACT_NO_AUDIO=1 \
		DETERMINISTIC_SCREENSHOT="$(abspath $(NATIVE_ENGINE_SCREENSHOT))"

native-engine-shim-smoke: $(NATIVE_ENGINE_SHIM_LIB)
	$(MAKE) -C ../unix EMBEDDED_CONTRACT=1 EMBEDDED_CONTRACT_NO_AUDIO=1 PAL_DETERMINISTIC=1 clean
	$(MAKE) -C ../unix deterministic-newgame-png-check \
		EMBEDDED_CONTRACT_NO_AUDIO=1 \
		SDL_CONFIG='$(NATIVE_ENGINE_SHIM_SDL_CONFIG)' \
		DETERMINISTIC_SCREENSHOT="$(abspath $(NATIVE_ENGINE_SHIM_SCREENSHOT))" \
		DETERMINISTIC_SCREENSHOT_LCD=1 \
		DETERMINISTIC_SCREENSHOT_WIDTH=320 \
		DETERMINISTIC_SCREENSHOT_HEIGHT=240 \
		DETERMINISTIC_SCREENSHOT_MIN_NONBLACK=10000 \
		DETERMINISTIC_SCREENSHOT_MIN_COLORS=32

native-engine-bridge-smoke: pack-build $(NATIVE_ENGINE_BRIDGE_TARGET)
	@if command -v ldd >/dev/null 2>&1; then \
		if ldd "$(NATIVE_ENGINE_BRIDGE_TARGET)" 2>/dev/null | grep -i 'SDL' >/dev/null; then \
			ldd "$(NATIVE_ENGINE_BRIDGE_TARGET)"; \
			exit 1; \
		fi; \
	fi
	(cd "$(PAL_DATA_DIR)" && \
		PAL_CONTRACT_NOR_PACK="$(abspath $(PACK_NOR))" \
		PAL_CONTRACT_TF_PACK="$(abspath $(PACK_TF))" \
		PAL_DETERMINISTIC_REPLAY="$(abspath $(NATIVE_ENGINE_BRIDGE_ROUTE))" \
		PAL_DETERMINISTIC_SCREENSHOT="$(abspath $(NATIVE_ENGINE_BRIDGE_SCREENSHOT))" \
		PAL_DETERMINISTIC_SCREENSHOT_LCD=1 \
		PAL_DETERMINISTIC_SCREENSHOT_FRAME=899 \
		PAL_DETERMINISTIC_MAX_PRESENTS=900 \
		SDL_VIDEODRIVER=dummy \
		SDL_AUDIODRIVER=dummy \
		timeout 10 "$(abspath $(NATIVE_ENGINE_BRIDGE_TARGET))")
	$(PYTHON) -B native_shim/check_png.py "$(NATIVE_ENGINE_BRIDGE_SCREENSHOT)" 320 240 10000 32

native-engine-host-smoke: pack-build $(NATIVE_ENGINE_HOST_TARGET)
	@if command -v ldd >/dev/null 2>&1; then \
		if ldd "$(NATIVE_ENGINE_HOST_TARGET)" 2>/dev/null | grep -i 'SDL' >/dev/null; then \
			ldd "$(NATIVE_ENGINE_HOST_TARGET)"; \
			exit 1; \
		fi; \
	fi
	(cd "$(PAL_DATA_DIR)" && \
		PAL_CORES3SE_NATIVE_NOR_PACK="$(abspath $(PACK_NOR))" \
		PAL_CORES3SE_NATIVE_TF_PACK="$(abspath $(PACK_TF))" \
		PAL_CORES3SE_NATIVE_SCREENSHOT="$(abspath $(NATIVE_ENGINE_HOST_SCREENSHOT))" \
		PAL_CORES3SE_NATIVE_SCREENSHOT_FRAME=899 \
		PAL_CORES3SE_NATIVE_MAX_PRESENTS=1200 \
		PAL_CORES3SE_NATIVE_FRAMES=100000 \
		SDL_VIDEODRIVER=dummy \
		SDL_AUDIODRIVER=dummy \
		timeout 10 "$(abspath $(NATIVE_ENGINE_HOST_TARGET))")
	$(PYTHON) -B native_shim/check_png.py "$(NATIVE_ENGINE_HOST_SCREENSHOT)" 320 240 10000 32

native-engine-host-parity-check: pack-build $(NATIVE_ENGINE_HOST_DETERMINISTIC_TARGET)
	$(MAKE) -C ../unix EMBEDDED_CONTRACT=0 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= sdlpal-deterministic
	@tmpd=$$(mktemp -d /tmp/pal-cores3se-host-parity-XXXXXX); \
	trap 'rm -rf "$$tmpd"' EXIT; \
	for profile in stock host; do \
		if [ "$$profile" = stock ]; then \
			bin="$(abspath ../unix)/sdlpal-deterministic"; \
			gpscreen_png="$$tmpd/stock.png"; \
			flush_png="$$tmpd/stock-unused.png"; \
		else \
			bin="$(abspath $(NATIVE_ENGINE_HOST_DETERMINISTIC_TARGET))"; \
			gpscreen_png="$$tmpd/host-gpscreen.png"; \
			flush_png="$(abspath $(NATIVE_ENGINE_HOST_DETERMINISTIC_SCREENSHOT))"; \
		fi; \
		(cd "$(PAL_DATA_DIR)" && \
			PAL_CORES3SE_NATIVE_NOR_PACK="$(abspath $(PACK_NOR))" \
			PAL_CORES3SE_NATIVE_TF_PACK="$(abspath $(PACK_TF))" \
			PAL_CORES3SE_NATIVE_SCREENSHOT="$$flush_png" \
			PAL_CORES3SE_NATIVE_SCREENSHOT_FRAME=$$(($(NATIVE_ENGINE_BRIDGE_FRAMES) - 1)) \
			PAL_CORES3SE_NATIVE_MAX_PRESENTS=$$(($(NATIVE_ENGINE_BRIDGE_FRAMES) + 10)) \
			PAL_CORES3SE_NATIVE_FRAMES=100000 \
			SDL_VIDEODRIVER=dummy \
			SDL_AUDIODRIVER=dummy \
			SDL_RENDER_DRIVER=software \
			PAL_DETERMINISTIC_REPLAY="$(abspath $(NATIVE_ENGINE_BRIDGE_ROUTE))" \
			PAL_DETERMINISTIC_SCREENSHOT="$$gpscreen_png" \
			PAL_DETERMINISTIC_SCREENSHOT_LCD=1 \
			PAL_DETERMINISTIC_SCREENSHOT_FRAME=$$(($(NATIVE_ENGINE_BRIDGE_FRAMES) - 1)) \
			PAL_DETERMINISTIC_CHECKPOINTS="$$tmpd/$$profile.trace" \
			PAL_DETERMINISTIC_SCRIPT_TRACE="$$tmpd/$$profile.script" \
			PAL_DETERMINISTIC_EVENT_TRACE="$$tmpd/$$profile.event" \
			PAL_DETERMINISTIC_MAX_PRESENTS=$(NATIVE_ENGINE_BRIDGE_FRAMES) \
			timeout 10 "$$bin") >"$$tmpd/$$profile.out" 2>&1; \
	done; \
	$(PYTHON) -B ../tools/deterministic_trace_check.py \
		--manifest "$(PACK_MANIFEST)" \
		--require-field screen \
		--require-field state \
		--require-field script \
		--require-field script_count \
		--require-field scene \
		--require-field viewport_x \
		--require-field viewport_y \
		--require-field party_x \
		--require-field party_y \
		--require-field battle \
		--require-field battle_result \
		--require-field battle_enemies \
		--require-field cash \
		--require-field game_frame \
		--require-field dir \
		--require-field keys \
		--require-tag present \
		--min-records $(NATIVE_ENGINE_BRIDGE_FRAMES) \
		--min-script-records 4000 \
		--require-event-tag scene \
		--require-event-tag reload \
		--require-event-tag init \
		--min-event-records 5 \
		--require-script-tag script \
		--min-distinct-field screen=700 \
		--min-distinct-field state=1000 \
		--min-distinct-field dir=5 \
		--min-distinct-field scene=3 \
		--min-distinct-field game_frame=1000 \
		--golden-script-trace "$$tmpd/stock.script" \
		--candidate-script-trace "$$tmpd/host.script" \
		--golden-event-trace "$$tmpd/stock.event" \
		--candidate-event-trace "$$tmpd/host.event" \
		"$$tmpd/stock.trace" \
		"$$tmpd/host.trace"; \
	cmp -s "$$tmpd/stock.png" "$$tmpd/host-gpscreen.png"; \
	cmp -s "$$tmpd/stock.png" "$(abspath $(NATIVE_ENGINE_HOST_DETERMINISTIC_SCREENSHOT))"; \
	$(PYTHON) -B native_shim/check_png.py "$$tmpd/stock.png" 320 240 10000 32; \
	$(PYTHON) -B native_shim/check_png.py "$$tmpd/host-gpscreen.png" 320 240 10000 32; \
	$(PYTHON) -B native_shim/check_png.py "$(NATIVE_ENGINE_HOST_DETERMINISTIC_SCREENSHOT)" 320 240 10000 32

native-engine-host-save-parity-check: pack-build $(NATIVE_ENGINE_HOST_DETERMINISTIC_TARGET)
	$(MAKE) -C ../unix EMBEDDED_CONTRACT=0 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= sdlpal-deterministic
	@tmpd=$$(mktemp -d /tmp/pal-cores3se-host-save-XXXXXX); \
	trap 'rm -rf "$$tmpd"' EXIT; \
	mkdir -p "$$tmpd/stock-data" "$$tmpd/host-cwd" "$$tmpd/host-save"; \
	find "$(PAL_DATA_DIR)" -maxdepth 1 -type f ! -iname '*.rpg' -exec ln -s '{}' "$$tmpd/stock-data/" ';'; \
	for profile in stock host; do \
		if [ "$$profile" = stock ]; then \
			bin="$(abspath ../unix)/sdlpal-deterministic"; \
			cwd="$$tmpd/stock-data"; \
			save_dir=""; \
		else \
			bin="$(abspath $(NATIVE_ENGINE_HOST_DETERMINISTIC_TARGET))"; \
			cwd="$$tmpd/host-cwd"; \
			save_dir="$$tmpd/host-save"; \
		fi; \
		(cd "$$cwd" && \
			PAL_CORES3SE_NATIVE_NOR_PACK="$(abspath $(PACK_NOR))" \
			PAL_CORES3SE_NATIVE_TF_PACK="$(abspath $(PACK_TF))" \
			PAL_CORES3SE_NATIVE_SAVE_DIR="$$save_dir" \
			PAL_CORES3SE_NATIVE_SCREENSHOT="$$tmpd/$$profile-unused.png" \
			PAL_CORES3SE_NATIVE_SCREENSHOT_FRAME=$$(($(NATIVE_ENGINE_BRIDGE_FRAMES) + 100)) \
			PAL_CORES3SE_NATIVE_MAX_PRESENTS=$$(($(NATIVE_ENGINE_BRIDGE_FRAMES) + 1000)) \
			PAL_CORES3SE_NATIVE_FRAMES=100000 \
			SDL_VIDEODRIVER=dummy \
			SDL_AUDIODRIVER=dummy \
			SDL_RENDER_DRIVER=software \
			PAL_DETERMINISTIC_REPLAY="$(abspath $(NATIVE_ENGINE_BRIDGE_ROUTE))" \
			PAL_DETERMINISTIC_CHECKPOINTS="$$tmpd/$$profile.trace" \
			PAL_DETERMINISTIC_SCRIPT_TRACE="$$tmpd/$$profile.script" \
			PAL_DETERMINISTIC_EVENT_TRACE="$$tmpd/$$profile.event" \
			PAL_DETERMINISTIC_SAVE_FRAME=$(NATIVE_ENGINE_SAVE_FRAME) \
			PAL_DETERMINISTIC_SAVE_SLOT=$(NATIVE_ENGINE_SAVE_SLOT) \
			PAL_DETERMINISTIC_SAVE_TIMES=$(NATIVE_ENGINE_SAVE_TIMES) \
			PAL_DETERMINISTIC_RELOAD_FRAME=$(NATIVE_ENGINE_RELOAD_FRAME) \
			PAL_DETERMINISTIC_RELOAD_SLOT=$(NATIVE_ENGINE_RELOAD_SLOT) \
			PAL_DETERMINISTIC_MAX_PRESENTS=$(NATIVE_ENGINE_BRIDGE_FRAMES) \
			timeout 10 "$$bin") >"$$tmpd/$$profile.out" 2>&1; \
	done; \
	grep -F "reload frame=$(NATIVE_ENGINE_RELOAD_FRAME)" "$$tmpd/stock.event" | grep -F "slot=$(NATIVE_ENGINE_RELOAD_SLOT)"; \
	grep -F "reload frame=$(NATIVE_ENGINE_RELOAD_FRAME)" "$$tmpd/host.event" | grep -F "slot=$(NATIVE_ENGINE_RELOAD_SLOT)"; \
	$(PYTHON) -B ../tools/deterministic_trace_check.py \
		--manifest "$(PACK_MANIFEST)" \
		--require-field screen \
		--require-field state \
		--require-field script \
		--require-field script_count \
		--require-field scene \
		--require-field viewport_x \
		--require-field viewport_y \
		--require-field party_x \
		--require-field party_y \
		--require-field battle \
		--require-field battle_result \
		--require-field battle_enemies \
		--require-field cash \
		--require-field game_frame \
		--require-field dir \
		--require-field keys \
		--require-tag present \
		--min-records $(NATIVE_ENGINE_BRIDGE_FRAMES) \
		--min-script-records 4000 \
		--require-event-tag scene \
		--require-event-tag reload \
		--require-event-tag init \
		--require-event-tag save \
		--ignore-event-field save \
		--min-event-records 6 \
		--require-script-tag script \
		--min-distinct-field screen=700 \
		--min-distinct-field state=1000 \
		--min-distinct-field dir=5 \
		--min-distinct-field scene=3 \
		--min-distinct-field game_frame=1000 \
		--golden-script-trace "$$tmpd/stock.script" \
		--candidate-script-trace "$$tmpd/host.script" \
		--golden-event-trace "$$tmpd/stock.event" \
		--candidate-event-trace "$$tmpd/host.event" \
		"$$tmpd/stock.trace" \
		"$$tmpd/host.trace"; \
	test -s "$$tmpd/stock-data/$(NATIVE_ENGINE_SAVE_SLOT).rpg"; \
	test -s "$$tmpd/host-save/$(NATIVE_ENGINE_SAVE_SLOT).rpg"; \
	$(PYTHON) -B ../tools/pal_save_compare.py \
		--manifest "$(PACK_MANIFEST)" \
		"$$tmpd/stock-data/$(NATIVE_ENGINE_SAVE_SLOT).rpg" \
		"$$tmpd/host-save/$(NATIVE_ENGINE_SAVE_SLOT).rpg"

engine-bridge-idf-compile: $(IDF_ENGINE_BRIDGE_SRCS) $(IDF_ENGINE_BRIDGE_TARGET_GLUE_SRCS) engine_bridge/include/pal_config.h engine_bridge/include/sys/mman.h native_engine_shim/include/SDL.h native_engine_shim/include/SDL_endian.h native_engine_shim/include/SDL_events.h native_engine_shim/include/SDL_video.h
	bash -lc 'set -e; source "$(IDF_EXPORT)" >/dev/null; mkdir -p "$(IDF_ENGINE_BRIDGE_BUILD_DIR)"; : >"$(IDF_ENGINE_BRIDGE_OBJECTS)"; for src in $(IDF_ENGINE_BRIDGE_SRCS); do obj="$(IDF_ENGINE_BRIDGE_BUILD_DIR)/$$(basename "$$src" .c).idf.o"; extra=""; case "$$src" in ../main.c) extra="-Dmain=PAL_EngineMain";; esac; echo "[XTENSA-CC] $$src"; xtensa-esp32s3-elf-gcc $(IDF_ENGINE_BRIDGE_CFLAGS) $$extra -c "$$src" -o "$$obj"; printf "%s\n" "$$obj" >>"$(IDF_ENGINE_BRIDGE_OBJECTS)"; done; for src in $(IDF_ENGINE_BRIDGE_TARGET_GLUE_SRCS); do obj="$(IDF_ENGINE_BRIDGE_BUILD_DIR)/$$(basename "$$src" .c).idf.o"; echo "[XTENSA-CC] $$src"; xtensa-esp32s3-elf-gcc $(IDF_ENGINE_BRIDGE_CFLAGS) -c "$$src" -o "$$obj"; done'

engine-bridge-idf-link: engine-bridge-idf-compile
	bash -lc 'set -e; source "$(IDF_EXPORT)" >/dev/null; objs="$$(cat "$(IDF_ENGINE_BRIDGE_OBJECTS)")"; xtensa-esp32s3-elf-gcc $(IDF_ENGINE_BRIDGE_LDFLAGS) $$objs -o "$(IDF_ENGINE_BRIDGE_ELF)" -lm; xtensa-esp32s3-elf-size "$(IDF_ENGINE_BRIDGE_ELF)"; if xtensa-esp32s3-elf-nm -u "$(IDF_ENGINE_BRIDGE_ELF)" | grep .; then exit 1; fi; if xtensa-esp32s3-elf-nm "$(IDF_ENGINE_BRIDGE_ELF)" | grep -E " [Tt] PAL_MKF(OpenPackArchive|IsPackArchive|MapChunk|GetChunkCount|GetChunkSize|ReadChunk)$$"; then exit 1; fi'

native-engine-parity-check:
	$(MAKE) -C ../unix deterministic-newgame-walk-target-parity-check

native-engine-shim-parity-check: $(NATIVE_ENGINE_SHIM_LIB)
	$(MAKE) -C ../unix contract-pack-build
	$(MAKE) -C ../unix EMBEDDED_CONTRACT=0 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= sdlpal-deterministic
	$(MAKE) -C ../unix EMBEDDED_CONTRACT=1 EMBEDDED_CONTRACT_NO_AUDIO=1 PAL_DETERMINISTIC=1 clean
	$(MAKE) -C ../unix EMBEDDED_CONTRACT=1 EMBEDDED_CONTRACT_NO_AUDIO=1 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= SDL_CONFIG='$(NATIVE_ENGINE_SHIM_SDL_CONFIG)' sdlpal-embedded-contract-noaudio-deterministic
	@tmpd=$$(mktemp -d /tmp/pal-cores3se-shim-parity-XXXXXX); \
	trap 'rm -rf "$$tmpd"' EXIT; \
	for profile in stock shim; do \
		if [ "$$profile" = stock ]; then bin=sdlpal-deterministic; else bin=sdlpal-embedded-contract-noaudio-deterministic; fi; \
		(cd "$(PAL_DATA_DIR)" && \
			SDL_VIDEODRIVER=dummy \
			SDL_AUDIODRIVER=dummy \
			SDL_RENDER_DRIVER=software \
			PAL_DETERMINISTIC_REPLAY="$(abspath ../tools/deterministic_routes/newgame_walk.route)" \
			PAL_DETERMINISTIC_CHECKPOINTS="$$tmpd/$$profile.trace" \
			PAL_DETERMINISTIC_SCRIPT_TRACE="$$tmpd/$$profile.script" \
			PAL_DETERMINISTIC_EVENT_TRACE="$$tmpd/$$profile.event" \
			PAL_DETERMINISTIC_MAX_PRESENTS=3000 \
			timeout 10 "$(abspath ../unix)/$$bin") >"$$tmpd/$$profile.out" 2>&1; \
	done; \
	$(PYTHON) -B ../tools/deterministic_trace_check.py \
		--manifest "$(PACK_MANIFEST)" \
		--require-field screen \
		--require-field state \
		--require-field script \
		--require-field script_count \
		--require-field scene \
		--require-field viewport_x \
		--require-field viewport_y \
		--require-field party_x \
		--require-field party_y \
		--require-field battle \
		--require-field battle_result \
		--require-field battle_enemies \
		--require-field cash \
		--require-field game_frame \
		--require-field dir \
		--require-field keys \
		--require-tag present \
		--min-records 3000 \
		--min-script-records 4000 \
		--require-event-tag scene \
		--require-event-tag reload \
		--require-event-tag init \
		--min-event-records 5 \
		--require-script-tag script \
		--min-distinct-field screen=700 \
		--min-distinct-field state=1000 \
		--min-distinct-field dir=5 \
		--min-distinct-field scene=3 \
		--min-distinct-field game_frame=1000 \
		--golden-script-trace "$$tmpd/stock.script" \
		--candidate-script-trace "$$tmpd/shim.script" \
		--golden-event-trace "$$tmpd/stock.event" \
		--candidate-event-trace "$$tmpd/shim.event" \
		"$$tmpd/stock.trace" \
		"$$tmpd/shim.trace"

native-engine-bridge-parity-check: pack-build $(NATIVE_ENGINE_BRIDGE_TARGET)
	$(MAKE) -C ../unix EMBEDDED_CONTRACT=0 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= sdlpal-deterministic
	@tmpd=$$(mktemp -d /tmp/pal-cores3se-bridge-parity-XXXXXX); \
	trap 'rm -rf "$$tmpd"' EXIT; \
	for profile in stock bridge; do \
		if [ "$$profile" = stock ]; then bin="$(abspath ../unix)/sdlpal-deterministic"; else bin="$(abspath $(NATIVE_ENGINE_BRIDGE_TARGET))"; fi; \
		(cd "$(PAL_DATA_DIR)" && \
			PAL_CONTRACT_NOR_PACK="$(abspath $(PACK_NOR))" \
			PAL_CONTRACT_TF_PACK="$(abspath $(PACK_TF))" \
			SDL_VIDEODRIVER=dummy \
			SDL_AUDIODRIVER=dummy \
			SDL_RENDER_DRIVER=software \
			PAL_DETERMINISTIC_REPLAY="$(abspath $(NATIVE_ENGINE_BRIDGE_ROUTE))" \
			PAL_DETERMINISTIC_CHECKPOINTS="$$tmpd/$$profile.trace" \
			PAL_DETERMINISTIC_SCRIPT_TRACE="$$tmpd/$$profile.script" \
			PAL_DETERMINISTIC_EVENT_TRACE="$$tmpd/$$profile.event" \
			PAL_DETERMINISTIC_MAX_PRESENTS=$(NATIVE_ENGINE_BRIDGE_FRAMES) \
			timeout 10 "$$bin") >"$$tmpd/$$profile.out" 2>&1; \
	done; \
	$(PYTHON) -B ../tools/deterministic_trace_check.py \
		--manifest "$(PACK_MANIFEST)" \
		--require-field screen \
		--require-field state \
		--require-field script \
		--require-field script_count \
		--require-field scene \
		--require-field viewport_x \
		--require-field viewport_y \
		--require-field party_x \
		--require-field party_y \
		--require-field battle \
		--require-field battle_result \
		--require-field battle_enemies \
		--require-field cash \
		--require-field game_frame \
		--require-field dir \
		--require-field keys \
		--require-tag present \
		--min-records $(NATIVE_ENGINE_BRIDGE_FRAMES) \
		--min-script-records 4000 \
		--require-event-tag scene \
		--require-event-tag reload \
		--require-event-tag init \
		--min-event-records 5 \
		--require-script-tag script \
		--min-distinct-field screen=700 \
		--min-distinct-field state=1000 \
		--min-distinct-field dir=5 \
		--min-distinct-field scene=3 \
		--min-distinct-field game_frame=1000 \
		--golden-script-trace "$$tmpd/stock.script" \
		--candidate-script-trace "$$tmpd/bridge.script" \
		--golden-event-trace "$$tmpd/stock.event" \
		--candidate-event-trace "$$tmpd/bridge.event" \
		"$$tmpd/stock.trace" \
		"$$tmpd/bridge.trace"

flash-app: flash-engine-host

flash-scaffold: scaffold-build
	bash -lc 'source "$(IDF_EXPORT)" >/dev/null && idf.py -C . -B "$(BUILD_DIR)" $(if $(PORT),-p "$(PORT)",) flash'

flash-engine-host: engine-host-build
	bash -lc 'source "$(IDF_EXPORT)" >/dev/null && idf.py -C . -B "$(ENGINE_HOST_BUILD_DIR)" $(if $(PORT),-p "$(PORT)",) flash'

flash-engine-port:
	@test -n "$(TF_MOUNT)" || { echo "TF_MOUNT=/media/... is required"; exit 2; }
	$(MAKE) engine-port-check
	$(MAKE) TF_MOUNT="$(TF_MOUNT)" prepare-tf
	$(MAKE) flash-nor
	$(MAKE) flash-engine-host

engine-hardware-smoke:
	@test -n "$(PORT)" || { echo "PORT=/dev/ttyACM0 is required"; exit 2; }
	@test -n "$(TF_MOUNT)" || { echo "TF_MOUNT=/media/... is required"; exit 2; }
	$(MAKE) engine-port-check
	$(MAKE) TF_MOUNT="$(TF_MOUNT)" prepare-tf
	$(MAKE) PORT="$(PORT)" flash-nor
	bash -lc 'set -o pipefail; source "$(IDF_EXPORT)" >/dev/null; log="$(abspath $(ENGINE_HARDWARE_SMOKE_LOG))"; mkdir -p "$$(dirname "$$log")"; rm -f "$$log"; (timeout "$(ENGINE_HARDWARE_SMOKE_TIMEOUT)" idf.py -C . -B "$(ENGINE_HOST_BUILD_DIR)" -p "$(PORT)" -b "$(BAUD)" flash monitor; rc=$$?; if [ "$$rc" -ne 0 ] && [ "$$rc" -ne 124 ]; then exit "$$rc"; fi; exit 0) 2>&1 | tee "$$log"; grep -F "engine packs ready" "$$log"; grep -F "engine first present" "$$log"; grep -F "engine present stats: frames=60" "$$log"'

flash-nor: pack-build
	$(PYTHON) -m esptool --chip esp32s3 -b $(BAUD) --before default-reset --after hard-reset $(if $(PORT),--port "$(PORT)",) write-flash --flash-mode $(FLASH_MODE) --flash-size $(FLASH_SIZE) --flash-freq $(FLASH_FREQ) $(PAL_NOR_OFFSET) $(PACK_NOR)

clean:
	rm -rf "$(BUILD_DIR)" "$(ENGINE_HOST_BUILD_DIR)" "$(NATIVE_BUILD_DIR)" "$(NATIVE_ENGINE_SHIM_BUILD_DIR)"
