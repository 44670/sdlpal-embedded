# General makefile for generic unix & linux platforms

TARGET = sdlpal
TEST_TARGET = ./sdlpal-tests

INTER=.unix

HOST =
PYTHON ?= python3
TEST_CCFLAGS =
USE_SDL3 ?= 1
EMBEDDED_CONTRACT ?= 0
EMBEDDED_CONTRACT_NO_AUDIO ?= 0
EMBEDDED_CONTRACT_SANITIZE ?= 0
PACK_NOR ?= /tmp/pal_nor_default.pak
PACK_TF ?= /tmp/pal_tf_default.pak
PACK_MANIFEST ?= /tmp/pal_pack_default_manifest.json
PAL_DATA_DIR ?= /mnt/hgfs/deb13/PALSteam/PAL_DOS
ifeq ($(EMBEDDED_CONTRACT_NO_AUDIO),1)
CONTRACT_SOURCE_MANIFEST ?= embedded_contract_noaudio_sources.json
else
CONTRACT_SOURCE_MANIFEST ?= embedded_contract_sources.json
endif
PAL_DETERMINISTIC ?= 0
DETERMINISTIC_GOLDEN ?=
DETERMINISTIC_CANDIDATE ?=
DETERMINISTIC_GOLDEN_SCRIPT ?=
DETERMINISTIC_CANDIDATE_SCRIPT ?=
DETERMINISTIC_GOLDEN_EVENT ?=
DETERMINISTIC_CANDIDATE_EVENT ?=
DETERMINISTIC_TRACE_METADATA ?=
DETERMINISTIC_WRITE_METADATA ?=
DETERMINISTIC_SMOKE_FRAMES ?= 6
DETERMINISTIC_ROUTE ?=
DETERMINISTIC_ROUTE_FRAMES ?= 120
DETERMINISTIC_ROUTE_OUT_DIR ?=
DETERMINISTIC_ROUTE_MIN_FRAMES ?= 1
DETERMINISTIC_ROUTE_MIN_SCRIPT ?= 0
DETERMINISTIC_ROUTE_MIN_EVENT ?= 1
DETERMINISTIC_ROUTE_EXTRA_CHECK ?=
DETERMINISTIC_TIMEOUT ?= timeout 10
DETERMINISTIC_NEWGAME_ROUTE ?= ../tools/deterministic_routes/newgame.route
DETERMINISTIC_NEWGAME_EXTENDED_ROUTE ?= ../tools/deterministic_routes/newgame_extended.route
DETERMINISTIC_NEWGAME_WALK_ROUTE ?= ../tools/deterministic_routes/newgame_walk.route
DETERMINISTIC_PARITY_FRAMES ?= 900
DETERMINISTIC_EXTENDED_PARITY_FRAMES ?= 1400
DETERMINISTIC_WALK_PARITY_FRAMES ?= 3000
DETERMINISTIC_SCREENSHOT ?= /tmp/sdlpal-contract-deterministic/newgame.png
DETERMINISTIC_SCREENSHOT_FRAME ?= 899
DETERMINISTIC_SCREENSHOT_LCD ?= 0
DETERMINISTIC_SCREENSHOT_WIDTH ?= 320
DETERMINISTIC_SCREENSHOT_HEIGHT ?= 200
DETERMINISTIC_SCREENSHOT_MIN_NONBLACK ?= 10000
DETERMINISTIC_SCREENSHOT_MIN_COLORS ?= 32
LIBPNG_CFLAGS := $(shell pkg-config --cflags libpng)
LIBPNG_LIBS := $(shell pkg-config --libs libpng)

GENERATED = -DPAL_HAS_GIT_REVISION $(shell ../scripts/gengitrev)

DEPFLAGS = -MT $@ -MMD -MP -MF $*$(INTER).Td

GTEST_DIR = ../3rd/googletest/googletest

USE_ALSA ?= $(shell echo "int main(){}" | gcc -o /dev/null -x c - -lasound 2>/dev/null && echo 1)
ifeq (${USE_ALSA},1)
	MIDI_CFILES = $(wildcard ../native_midi/*.c) native_midi_alsa.c
	MIDI_CPPFILES =
	MIDI_LDFLAGS = -lasound
else
	MIDI_CFILES =
	MIDI_CPPFILES = native_midi.cpp
	MIDI_LDFLAGS =
endif

ifeq ($(EMBEDDED_CONTRACT),1)
	TARGET = sdlpal-embedded-contract
	INTER = .embedded.unix
	GENERATED =
	USE_SDL3 = 0
	MIDI_CFILES =
	MIDI_CPPFILES =
	MIDI_LDFLAGS =
	SOURCES = .. ../sdl_compat
	CONTRACT_CFILE_EXCLUDES = ../aviplay.c ../font.c ../glslp.c ../midi_timidity.c ../midi_tsf.c ../mini_glloader.c ../mp3play.c ../oggplay.c ../opusplay.c ../resampler.c ../sound.c ../text.c ../video_glsl.c ../yj1.c
	CONTRACT_CPPFILE_EXCLUDES = ../rixplay.cpp
	CONTRACT_CFILES = embedded_contract_stubs.c ../embedded/pal_noheap.c ../embedded/pal_pack.c ../embedded/pal_text_cache.c ../embedded/pal_font_cache.c ../embedded/pal_music_cache.c
	CONTRACT_CPPFILES = contract_music.cpp contract_opl2.cpp ../adplug/player.cpp ../adplug/rix.cpp
	EMBEDDED_CCFLAGS = -ffunction-sections -fdata-sections -DPAL_HAS_MP3=0 -DPAL_HAS_OGG=0 -DPAL_HAS_OPUS=0 -DPAL_HAS_NATIVEMIDI=0 -DPAL_HAS_GLSL=0 -DPAL_HAS_CONFIG_PAGE=0 -DPAL_NO_LAUNCH_UI -DPAL_NO_RUNTIME_DECOMPRESS=1 -DPAL_NO_RUNTIME_HEAP=1 -DPAL_CONTRACT_EXTERNAL_RIX=1 -DUSE_RIX_EXTRA_INIT=0
	EMBEDDED_LDFLAGS = -Wl,--gc-sections -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
	ifeq ($(EMBEDDED_CONTRACT_NO_AUDIO),1)
		TARGET = sdlpal-embedded-contract-noaudio
		INTER = .embedded.noaudio.unix
		CONTRACT_CFILE_EXCLUDES += ../audio.c ../midi.c
		CONTRACT_CFILES = embedded_contract_stubs.c contract_noaudio.c ../embedded/pal_noheap.c ../embedded/pal_pack.c ../embedded/pal_text_cache.c ../embedded/pal_font_cache.c
		CONTRACT_CPPFILES =
		EMBEDDED_CCFLAGS += -DPAL_CONTRACT_NO_AUDIO=1 -DPAL_ESP_CORES3SE_NO_AUDIO=1 -DPAL_ESP_CORES3SE_NO_SFX=1
		CONTRACT_BINARY_EXTRA_FLAGS = \
			--forbid-symbol-prefix pal_audio_ \
			--forbid-symbol-prefix pal_sfx_ \
			--forbid-symbol-prefix pal_music_ \
			--forbid-symbol-prefix pal_sram_audio_ \
			--forbid-symbol-prefix pal_sram_contract_rix \
			--forbid-symbol-prefix pal_psram_contract_sfx \
			--forbid-symbol-prefix RIX_ \
			--forbid-symbol-prefix SOUND_ \
			--forbid-symbol-prefix MIDI_ \
			--forbid-symbol-prefix TIMIDITY_ \
			--forbid-symbol-prefix TSF_ \
			--forbid-symbol-prefix MP3_ \
			--forbid-symbol-prefix OGG_ \
			--forbid-symbol-prefix OPUS_
	endif
	ifeq ($(EMBEDDED_CONTRACT_SANITIZE),1)
		TARGET = sdlpal-embedded-contract-asan
		INTER = .embedded.asan.unix
		EMBEDDED_CCFLAGS += -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
		EMBEDDED_LDFLAGS = -fsanitize=address,undefined
	endif
else
	SOURCES = .. ../sdl_compat ../adplug ../liboggvorbis/src ../libopusfile/src ../libopusfile/celt ../libopusfile/silk ../libopusfile/silk/float ../libmad ../timidity
	CONTRACT_CFILE_EXCLUDES =
	CONTRACT_CPPFILE_EXCLUDES =
	CONTRACT_CFILES =
	CONTRACT_CPPFILES =
	EMBEDDED_CCFLAGS =
	EMBEDDED_LDFLAGS =
endif

ifeq ($(EMBEDDED_CONTRACT),1)
	ifeq ($(EMBEDDED_CONTRACT_NO_AUDIO),1)
		DETERMINISTIC_TARGET = sdlpal-embedded-contract-noaudio-deterministic
	else
		DETERMINISTIC_TARGET = sdlpal-embedded-contract-deterministic
	endif
else
	DETERMINISTIC_TARGET = sdlpal-deterministic
endif
ifeq ($(EMBEDDED_CONTRACT_NO_AUDIO),1)
	DETERMINISTIC_CONTRACT_TARGET = sdlpal-embedded-contract-noaudio-deterministic
else
	DETERMINISTIC_CONTRACT_TARGET = sdlpal-embedded-contract-deterministic
endif

DETERMINISTIC_CFILES =
WS_CFILES =
ifeq ($(EMBEDDED_CONTRACT),0)
	WS_CFILES = pal_ws_server.c
	EMBEDDED_CCFLAGS += -DPAL_HAS_WS_SERVER=1
endif
ifeq ($(PAL_DETERMINISTIC),1)
	TARGET = $(DETERMINISTIC_TARGET)
	INTER := .deterministic$(INTER)
	DETERMINISTIC_CFILES = deterministic.c
	EMBEDDED_CCFLAGS += -DPAL_DETERMINISTIC=1 $(LIBPNG_CFLAGS)
	EMBEDDED_LDFLAGS += -Wl,--wrap=SDL_RenderPresent -Wl,--wrap=PAL_RunTriggerScript -Wl,--wrap=PAL_RunAutoScript -Wl,--wrap=PAL_SaveGame -Wl,--wrap=PAL_InitGameData -Wl,--wrap=PAL_ReloadInNextTick $(LIBPNG_LIBS)
endif

CFILES = $(filter-out $(CONTRACT_CFILE_EXCLUDES),$(foreach dir, $(SOURCES), $(wildcard $(dir)/*.c))) $(MIDI_CFILES) $(CONTRACT_CFILES) $(DETERMINISTIC_CFILES) $(WS_CFILES)
CPPFILES = $(filter-out $(CONTRACT_CPPFILE_EXCLUDES),$(foreach dir, $(SOURCES), $(wildcard $(dir)/*.cpp))) unix.cpp $(MIDI_CPPFILES) $(CONTRACT_CPPFILES)
TOBJFILES = $(CFILES:.c=$(INTER).o) $(CPPFILES:.cpp=$(INTER).o)
CONTRACT_SOURCE_ARGS = $(foreach src,$(CFILES) $(CPPFILES),--source-file $(src))
CONTRACT_OBJECT_ARGS = $(foreach obj,$(OBJFILES),--object $(obj))
CONTRACT_CHECK_SOURCE_FLAGS = \
	$(CONTRACT_SOURCE_ARGS) \
	--source-define PAL_NO_RUNTIME_HEAP=1 \
	--source-define PAL_NO_RUNTIME_DECOMPRESS=1 \
	--source-define PAL_CONTRACT_EXTERNAL_RIX=1 \
	--source-define USE_RIX_EXTRA_INIT=0 \
	$(if $(filter 1,$(EMBEDDED_CONTRACT_NO_AUDIO)),--source-define PAL_CONTRACT_NO_AUDIO=1 --source-define PAL_ESP_CORES3SE_NO_AUDIO=1 --source-define PAL_ESP_CORES3SE_NO_SFX=1,) \
	--source-define PAL_NO_LAUNCH_UI \
	--source-define PAL_HAS_MP3=0 \
	--source-define PAL_HAS_OGG=0 \
	--source-define PAL_HAS_OPUS=0 \
	--source-define PAL_HAS_NATIVEMIDI=0 \
	--source-define PAL_HAS_GLSL=0 \
	--source-define PAL_HAS_CONFIG_PAGE=0 \
	--source-define USE_SDL3=0 \
	--source-define SDL_MAJOR_VERSION=2 \
	--source-define SDL_MINOR_VERSION=0 \
	--source-define SDL_PATCHLEVEL=0 \
	--source-exclude native_midi.cpp \
	--source-exclude native_midi_alsa.c
DEPFILES = $(OBJFILES:.o=.d)
TEST_CPPFILES = $(wildcard ../tests/*.cpp)
TEST_OBJFILES = $(TEST_CPPFILES:.cpp=.o)
ifeq ($(origin SDL_CONFIG), undefined)
	ifeq ($(USE_SDL3),1)
		SDL_CONFIG = pkg-config sdl3
	else
		SDL_CONFIG = pkg-config sdl2
	endif
endif


FLTK_INSTALLED := $(shell command -v fltk-config 2> /dev/null)
ifdef FLTK_INSTALLED
	FLTK_CXXFLAGS = `fltk-config --cxxflags`
	FLTK_CFLAGS = `fltk-config --cflags`
	FLTK_LDFLAGS = `fltk-config --ldflags`
else
	FLTK_CXXFLAGS = -DPAL_NO_LAUNCH_UI
	FLTK_CFLAGS =
	FLTK_LDFLAGS =
endif


CC = $(HOST)gcc
CXX = $(HOST)g++
override CCFLAGS += `$(SDL_CONFIG) --cflags` -DUSE_SDL3=$(USE_SDL3) -I../sdl_compat -D_GNU_SOURCE -g -Wall -O2 -fno-strict-aliasing $(EXTRA_CCFLAGS) $(TEST_CCFLAGS) $(GENERATED) $(EMBEDDED_CCFLAGS)
CXXFLAGS = $(CCFLAGS) -std=c++11 $(FLTK_CXXFLAGS)
CFLAGS = $(CCFLAGS) -std=gnu99 $(FLTK_CFLAGS)
LDFLAGS = `$(SDL_CONFIG) --libs` $(FLTK_LDFLAGS) $(MIDI_LDFLAGS) -lstdc++ -lm $(EMBEDDED_LDFLAGS)
TEST_CXXFLAGS += -isystem $(GTEST_DIR)/include -I $(GTEST_DIR) -g -Wall -Wextra -pthread

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	LDFLAGS += -framework OpenGL
	OBJFILES = $(filter-out ../mini_glloader.unix.o, $(TOBJFILES))
else
	ifeq ($(EMBEDDED_CONTRACT),1)
		LDFLAGS += -pthread
	else
		LDFLAGS += -lGL -pthread
	endif
	OBJFILES = $(TOBJFILES)
endif

POSTCOMPILE = @mv -f $*$(INTER).Td $*$(INTER).d && touch $@

.PHONY : all clean check ws-check deterministic-check deterministic-smoke deterministic-newgame-check deterministic-newgame-parity-check deterministic-newgame-noaudio-parity-check deterministic-newgame-extended-noaudio-parity-check deterministic-newgame-extended-target-parity-check deterministic-newgame-walk-target-parity-check deterministic-newgame-png-check deterministic-newgame-cores3se-png-check deterministic-route-check deterministic-trace-check contract-pack-build contract-check contract-noaudio-check contract-sanitize-check

all: $(TARGET)

$(TARGET): $(OBJFILES)
	@echo [LD] $@
	@$(CXX) $^ -o $@ $(LDFLAGS)

gtest-all.o : $(GTEST_DIR)/src/gtest-all.cc %.d
	@echo [CC] $<
	@$(CXX) $(DEPFLAGS) $(TEST_CXXFLAGS) -c $< -o $@
	$(POSTCOMPILE)

%$(INTER).o: %.c %$(INTER).d
	@echo [CC] $<
	@$(CC) $(DEPFLAGS) $(CFLAGS) -c $< -o $@
	$(POSTCOMPILE)

%$(INTER).o: %.cpp %$(INTER).d
	@echo [CC] $<
	@$(CXX) $(DEPFLAGS) $(CXXFLAGS) -c $< -o $@
	$(POSTCOMPILE)

$(TEST_TARGET): $(OBJFILES) $(TEST_OBJFILES) gtest-all.o
	@echo [LD] $@
	@$(CXX) $^ -o $@ $(LDFLAGS) -lpthread

clean:
	-@rm -f $(TARGET) $(TEST_TARGET) $(OBJFILES) $(TEST_OBJFILES) $(DEPFILES)

%.d: ;
.PRECIOUS: %.d

-include $(DEPFILES)

../liboggvorbis/%$(INTER).o: EXTRA_CCFLAGS := -I../liboggvorbis/include -I../liboggvorbis/src
../libopusfile/%$(INTER).o: EXTRA_CCFLAGS := -I../liboggvorbis/include -I../libopusfile/include -I../libopusfile/src -I../libopusfile/celt -I../libopusfile/silk -I../libopusfile/silk/float -DHAVE_CONFIG_H
%$(INTER).o: EXTRA_CCFLAGS := -I. -I.. -I../liboggvorbis/include -I../libopusfile/include -I../timidity -DPAL_HAS_PLATFORM_SPECIFIC_UTILS
contract_opl2$(INTER).o: EXTRA_CCFLAGS += -Wno-array-bounds
check: TEST_CCFLAGS = -I. -I.. -DUNIT_TEST=1 -isystem $(GTEST_DIR)/include
check: $(TEST_TARGET)
	@echo [EXEC] $(TEST_TARGET)
	@chmod +x $(TEST_TARGET)
	@exec $(TEST_TARGET)

ws-check: pal_ws_server$(INTER).o
	PYTHONPATH=../tools $(PYTHON) -B ../tools/test_ws_cli.py

deterministic-check:
	$(MAKE) PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= $(DETERMINISTIC_TARGET)

deterministic-smoke: deterministic-check contract-pack-build
	@tmpd=$$(mktemp -d /tmp/pal-deterministic-smoke-XXXXXX); \
	trap 'rm -rf "$$tmpd"' EXIT; \
	for n in 1 2; do \
		(cd "$(PAL_DATA_DIR)" && \
			SDL_VIDEODRIVER=dummy \
			SDL_AUDIODRIVER=dummy \
			SDL_RENDER_DRIVER=software \
			PAL_DETERMINISTIC_CHECKPOINTS="$$tmpd/run$$n.trace" \
				PAL_DETERMINISTIC_SCRIPT_TRACE="$$tmpd/run$$n.script" \
				PAL_DETERMINISTIC_EVENT_TRACE="$$tmpd/run$$n.event" \
				PAL_DETERMINISTIC_MAX_PRESENTS=$(DETERMINISTIC_SMOKE_FRAMES) \
				$(DETERMINISTIC_TIMEOUT) "$(CURDIR)/$(DETERMINISTIC_TARGET)") >"$$tmpd/run$$n.out" 2>&1; \
	done; \
	$(PYTHON) -B ../tools/deterministic_trace_check.py \
		--manifest $(PACK_MANIFEST) \
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
		--min-records $(DETERMINISTIC_SMOKE_FRAMES) \
		--require-event-tag scene \
		--min-event-records 1 \
		--golden-script-trace "$$tmpd/run1.script" \
		--candidate-script-trace "$$tmpd/run2.script" \
		--golden-event-trace "$$tmpd/run1.event" \
		--candidate-event-trace "$$tmpd/run2.event" \
		"$$tmpd/run1.trace" \
		"$$tmpd/run2.trace"

deterministic-route-check: deterministic-check contract-pack-build
	@test -n "$(DETERMINISTIC_ROUTE)" || (echo "Set DETERMINISTIC_ROUTE=/path/to/replay.route"; exit 1)
	@outd="$(DETERMINISTIC_ROUTE_OUT_DIR)"; \
	if [ -z "$$outd" ]; then outd=$$(mktemp -d /tmp/pal-deterministic-route-XXXXXX); cleanup=1; else mkdir -p "$$outd"; cleanup=0; fi; \
	trap 'if [ "$$cleanup" = 1 ]; then rm -rf "$$outd"; fi' EXIT; \
	for n in 1 2; do \
		(cd "$(PAL_DATA_DIR)" && \
			SDL_VIDEODRIVER=dummy \
			SDL_AUDIODRIVER=dummy \
			SDL_RENDER_DRIVER=software \
			PAL_DETERMINISTIC_REPLAY="$(abspath $(DETERMINISTIC_ROUTE))" \
			PAL_DETERMINISTIC_CHECKPOINTS="$$outd/run$$n.trace" \
			PAL_DETERMINISTIC_SCRIPT_TRACE="$$outd/run$$n.script" \
			PAL_DETERMINISTIC_EVENT_TRACE="$$outd/run$$n.event" \
			PAL_DETERMINISTIC_MAX_PRESENTS=$(DETERMINISTIC_ROUTE_FRAMES) \
			$(DETERMINISTIC_TIMEOUT) "$(CURDIR)/$(DETERMINISTIC_TARGET)") >"$$outd/run$$n.out" 2>&1; \
	done; \
	$(PYTHON) -B ../tools/deterministic_trace_check.py \
		--manifest $(PACK_MANIFEST) \
		$(if $(DETERMINISTIC_TRACE_METADATA),--metadata $(DETERMINISTIC_TRACE_METADATA),) \
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
		--min-records $(DETERMINISTIC_ROUTE_MIN_FRAMES) \
		--min-script-records $(DETERMINISTIC_ROUTE_MIN_SCRIPT) \
		--require-event-tag scene \
		--min-event-records $(DETERMINISTIC_ROUTE_MIN_EVENT) \
		--golden-script-trace "$$outd/run1.script" \
		--candidate-script-trace "$$outd/run2.script" \
		--golden-event-trace "$$outd/run1.event" \
		--candidate-event-trace "$$outd/run2.event" \
		$(DETERMINISTIC_ROUTE_EXTRA_CHECK) \
		"$$outd/run1.trace" \
		"$$outd/run2.trace"

deterministic-newgame-check:
	$(MAKE) deterministic-route-check \
		DETERMINISTIC_ROUTE=$(DETERMINISTIC_NEWGAME_ROUTE) \
		DETERMINISTIC_ROUTE_FRAMES=900 \
		DETERMINISTIC_ROUTE_MIN_FRAMES=900 \
		DETERMINISTIC_ROUTE_MIN_SCRIPT=60 \
		DETERMINISTIC_ROUTE_MIN_EVENT=4 \
		DETERMINISTIC_ROUTE_EXTRA_CHECK='--require-event-tag reload --require-event-tag init --require-script-tag script'

deterministic-newgame-parity-check: contract-pack-build
	$(MAKE) EMBEDDED_CONTRACT=0 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= sdlpal-deterministic
	$(MAKE) EMBEDDED_CONTRACT=1 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= sdlpal-embedded-contract-deterministic
	@tmpd=$$(mktemp -d /tmp/pal-deterministic-parity-XXXXXX); \
	trap 'rm -rf "$$tmpd"' EXIT; \
	for profile in stock contract; do \
		if [ "$$profile" = stock ]; then bin=sdlpal-deterministic; else bin=sdlpal-embedded-contract-deterministic; fi; \
		(cd "$(PAL_DATA_DIR)" && \
			SDL_VIDEODRIVER=dummy \
			SDL_AUDIODRIVER=dummy \
			SDL_RENDER_DRIVER=software \
			PAL_DETERMINISTIC_REPLAY="$(abspath $(DETERMINISTIC_NEWGAME_ROUTE))" \
			PAL_DETERMINISTIC_CHECKPOINTS="$$tmpd/$$profile.trace" \
			PAL_DETERMINISTIC_SCRIPT_TRACE="$$tmpd/$$profile.script" \
			PAL_DETERMINISTIC_EVENT_TRACE="$$tmpd/$$profile.event" \
			PAL_DETERMINISTIC_MAX_PRESENTS=$(DETERMINISTIC_PARITY_FRAMES) \
			$(DETERMINISTIC_TIMEOUT) "$(CURDIR)/$$bin") >"$$tmpd/$$profile.out" 2>&1; \
	done; \
	$(PYTHON) -B ../tools/deterministic_trace_check.py \
		--manifest $(PACK_MANIFEST) \
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
		--min-records $(DETERMINISTIC_PARITY_FRAMES) \
		--min-script-records 60 \
		--require-event-tag scene \
		--require-event-tag reload \
		--require-event-tag init \
		--min-event-records 4 \
		--require-script-tag script \
		--golden-script-trace "$$tmpd/stock.script" \
		--candidate-script-trace "$$tmpd/contract.script" \
		--golden-event-trace "$$tmpd/stock.event" \
		--candidate-event-trace "$$tmpd/contract.event" \
		"$$tmpd/stock.trace" \
		"$$tmpd/contract.trace"

deterministic-newgame-noaudio-parity-check: contract-pack-build
	$(MAKE) EMBEDDED_CONTRACT=1 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= sdlpal-embedded-contract-deterministic
	$(MAKE) EMBEDDED_CONTRACT=1 EMBEDDED_CONTRACT_NO_AUDIO=1 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= sdlpal-embedded-contract-noaudio-deterministic
	@tmpd=$$(mktemp -d /tmp/pal-deterministic-noaudio-parity-XXXXXX); \
	trap 'rm -rf "$$tmpd"' EXIT; \
	for profile in contract noaudio; do \
		if [ "$$profile" = contract ]; then bin=sdlpal-embedded-contract-deterministic; else bin=sdlpal-embedded-contract-noaudio-deterministic; fi; \
		(cd "$(PAL_DATA_DIR)" && \
			SDL_VIDEODRIVER=dummy \
			SDL_AUDIODRIVER=dummy \
			SDL_RENDER_DRIVER=software \
			PAL_DETERMINISTIC_REPLAY="$(abspath $(DETERMINISTIC_NEWGAME_ROUTE))" \
			PAL_DETERMINISTIC_CHECKPOINTS="$$tmpd/$$profile.trace" \
			PAL_DETERMINISTIC_SCRIPT_TRACE="$$tmpd/$$profile.script" \
			PAL_DETERMINISTIC_EVENT_TRACE="$$tmpd/$$profile.event" \
			PAL_DETERMINISTIC_MAX_PRESENTS=$(DETERMINISTIC_PARITY_FRAMES) \
			$(DETERMINISTIC_TIMEOUT) "$(CURDIR)/$$bin") >"$$tmpd/$$profile.out" 2>&1; \
	done; \
	$(PYTHON) -B ../tools/deterministic_trace_check.py \
		--manifest $(PACK_MANIFEST) \
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
		--min-records $(DETERMINISTIC_PARITY_FRAMES) \
		--min-script-records 60 \
		--require-event-tag scene \
		--require-event-tag reload \
		--require-event-tag init \
		--min-event-records 4 \
		--require-script-tag script \
		--golden-script-trace "$$tmpd/contract.script" \
		--candidate-script-trace "$$tmpd/noaudio.script" \
		--golden-event-trace "$$tmpd/contract.event" \
		--candidate-event-trace "$$tmpd/noaudio.event" \
		"$$tmpd/contract.trace" \
		"$$tmpd/noaudio.trace"

deterministic-newgame-extended-noaudio-parity-check: contract-pack-build
	$(MAKE) EMBEDDED_CONTRACT=1 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= sdlpal-embedded-contract-deterministic
	$(MAKE) EMBEDDED_CONTRACT=1 EMBEDDED_CONTRACT_NO_AUDIO=1 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= sdlpal-embedded-contract-noaudio-deterministic
	@tmpd=$$(mktemp -d /tmp/pal-deterministic-extended-noaudio-parity-XXXXXX); \
	trap 'rm -rf "$$tmpd"' EXIT; \
	for profile in contract noaudio; do \
		if [ "$$profile" = contract ]; then bin=sdlpal-embedded-contract-deterministic; else bin=sdlpal-embedded-contract-noaudio-deterministic; fi; \
		(cd "$(PAL_DATA_DIR)" && \
			SDL_VIDEODRIVER=dummy \
			SDL_AUDIODRIVER=dummy \
			SDL_RENDER_DRIVER=software \
			PAL_DETERMINISTIC_REPLAY="$(abspath $(DETERMINISTIC_NEWGAME_EXTENDED_ROUTE))" \
			PAL_DETERMINISTIC_CHECKPOINTS="$$tmpd/$$profile.trace" \
			PAL_DETERMINISTIC_SCRIPT_TRACE="$$tmpd/$$profile.script" \
			PAL_DETERMINISTIC_EVENT_TRACE="$$tmpd/$$profile.event" \
			PAL_DETERMINISTIC_MAX_PRESENTS=$(DETERMINISTIC_EXTENDED_PARITY_FRAMES) \
			$(DETERMINISTIC_TIMEOUT) "$(CURDIR)/$$bin") >"$$tmpd/$$profile.out" 2>&1; \
	done; \
	$(PYTHON) -B ../tools/deterministic_trace_check.py \
		--manifest $(PACK_MANIFEST) \
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
		--min-records $(DETERMINISTIC_EXTENDED_PARITY_FRAMES) \
		--min-script-records 60 \
		--require-event-tag scene \
		--require-event-tag reload \
		--require-event-tag init \
		--min-event-records 4 \
		--require-script-tag script \
		--min-distinct-field screen=400 \
		--min-distinct-field state=20 \
		--min-distinct-field dir=5 \
		--golden-script-trace "$$tmpd/contract.script" \
		--candidate-script-trace "$$tmpd/noaudio.script" \
		--golden-event-trace "$$tmpd/contract.event" \
		--candidate-event-trace "$$tmpd/noaudio.event" \
		"$$tmpd/contract.trace" \
		"$$tmpd/noaudio.trace"

deterministic-newgame-extended-target-parity-check: contract-pack-build
	$(MAKE) EMBEDDED_CONTRACT=0 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= sdlpal-deterministic
	$(MAKE) EMBEDDED_CONTRACT=1 EMBEDDED_CONTRACT_NO_AUDIO=1 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= sdlpal-embedded-contract-noaudio-deterministic
	@tmpd=$$(mktemp -d /tmp/pal-deterministic-extended-target-parity-XXXXXX); \
	trap 'rm -rf "$$tmpd"' EXIT; \
	for profile in stock noaudio; do \
		if [ "$$profile" = stock ]; then bin=sdlpal-deterministic; else bin=sdlpal-embedded-contract-noaudio-deterministic; fi; \
		(cd "$(PAL_DATA_DIR)" && \
			SDL_VIDEODRIVER=dummy \
			SDL_AUDIODRIVER=dummy \
			SDL_RENDER_DRIVER=software \
			PAL_DETERMINISTIC_REPLAY="$(abspath $(DETERMINISTIC_NEWGAME_EXTENDED_ROUTE))" \
			PAL_DETERMINISTIC_CHECKPOINTS="$$tmpd/$$profile.trace" \
			PAL_DETERMINISTIC_SCRIPT_TRACE="$$tmpd/$$profile.script" \
			PAL_DETERMINISTIC_EVENT_TRACE="$$tmpd/$$profile.event" \
			PAL_DETERMINISTIC_MAX_PRESENTS=$(DETERMINISTIC_EXTENDED_PARITY_FRAMES) \
			$(DETERMINISTIC_TIMEOUT) "$(CURDIR)/$$bin") >"$$tmpd/$$profile.out" 2>&1; \
	done; \
	$(PYTHON) -B ../tools/deterministic_trace_check.py \
		--manifest $(PACK_MANIFEST) \
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
		--min-records $(DETERMINISTIC_EXTENDED_PARITY_FRAMES) \
		--min-script-records 60 \
		--require-event-tag scene \
		--require-event-tag reload \
		--require-event-tag init \
		--min-event-records 4 \
		--require-script-tag script \
		--min-distinct-field screen=400 \
		--min-distinct-field state=20 \
		--min-distinct-field dir=5 \
		--golden-script-trace "$$tmpd/stock.script" \
		--candidate-script-trace "$$tmpd/noaudio.script" \
		--golden-event-trace "$$tmpd/stock.event" \
		--candidate-event-trace "$$tmpd/noaudio.event" \
		"$$tmpd/stock.trace" \
		"$$tmpd/noaudio.trace"

deterministic-newgame-walk-target-parity-check: contract-pack-build
	$(MAKE) EMBEDDED_CONTRACT=0 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= sdlpal-deterministic
	$(MAKE) EMBEDDED_CONTRACT=1 EMBEDDED_CONTRACT_NO_AUDIO=1 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= sdlpal-embedded-contract-noaudio-deterministic
	@tmpd=$$(mktemp -d /tmp/pal-deterministic-walk-target-parity-XXXXXX); \
	trap 'rm -rf "$$tmpd"' EXIT; \
	for profile in stock noaudio; do \
		if [ "$$profile" = stock ]; then bin=sdlpal-deterministic; else bin=sdlpal-embedded-contract-noaudio-deterministic; fi; \
		(cd "$(PAL_DATA_DIR)" && \
			SDL_VIDEODRIVER=dummy \
			SDL_AUDIODRIVER=dummy \
			SDL_RENDER_DRIVER=software \
			PAL_DETERMINISTIC_REPLAY="$(abspath $(DETERMINISTIC_NEWGAME_WALK_ROUTE))" \
			PAL_DETERMINISTIC_CHECKPOINTS="$$tmpd/$$profile.trace" \
			PAL_DETERMINISTIC_SCRIPT_TRACE="$$tmpd/$$profile.script" \
			PAL_DETERMINISTIC_EVENT_TRACE="$$tmpd/$$profile.event" \
			PAL_DETERMINISTIC_MAX_PRESENTS=$(DETERMINISTIC_WALK_PARITY_FRAMES) \
			$(DETERMINISTIC_TIMEOUT) "$(CURDIR)/$$bin") >"$$tmpd/$$profile.out" 2>&1; \
	done; \
	$(PYTHON) -B ../tools/deterministic_trace_check.py \
		--manifest $(PACK_MANIFEST) \
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
		--min-records $(DETERMINISTIC_WALK_PARITY_FRAMES) \
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
		--candidate-script-trace "$$tmpd/noaudio.script" \
		--golden-event-trace "$$tmpd/stock.event" \
		--candidate-event-trace "$$tmpd/noaudio.event" \
		"$$tmpd/stock.trace" \
		"$$tmpd/noaudio.trace"

deterministic-newgame-png-check: EMBEDDED_CONTRACT=1
deterministic-newgame-png-check: contract-pack-build
	$(MAKE) EMBEDDED_CONTRACT=1 PAL_DETERMINISTIC=1 USE_SDL3=0 GENERATED= $(DETERMINISTIC_CONTRACT_TARGET)
	@mkdir -p "$(dir $(abspath $(DETERMINISTIC_SCREENSHOT)))"; \
	(cd "$(PAL_DATA_DIR)" && \
		SDL_VIDEODRIVER=dummy \
		SDL_AUDIODRIVER=dummy \
		SDL_RENDER_DRIVER=software \
		PAL_DETERMINISTIC_REPLAY="$(abspath $(DETERMINISTIC_NEWGAME_ROUTE))" \
		PAL_DETERMINISTIC_SCREENSHOT="$(abspath $(DETERMINISTIC_SCREENSHOT))" \
		PAL_DETERMINISTIC_SCREENSHOT_FRAME=$(DETERMINISTIC_SCREENSHOT_FRAME) \
		PAL_DETERMINISTIC_SCREENSHOT_LCD=$(DETERMINISTIC_SCREENSHOT_LCD) \
		PAL_DETERMINISTIC_MAX_PRESENTS=$(DETERMINISTIC_PARITY_FRAMES) \
		$(DETERMINISTIC_TIMEOUT) "$(CURDIR)/$(DETERMINISTIC_CONTRACT_TARGET)") >/tmp/sdlpal-contract-deterministic/newgame-png.out 2>&1
	$(PYTHON) -B ../esp32s3/native_shim/check_png.py "$(DETERMINISTIC_SCREENSHOT)" $(DETERMINISTIC_SCREENSHOT_WIDTH) $(DETERMINISTIC_SCREENSHOT_HEIGHT) $(DETERMINISTIC_SCREENSHOT_MIN_NONBLACK) $(DETERMINISTIC_SCREENSHOT_MIN_COLORS)

deterministic-newgame-cores3se-png-check:
	$(MAKE) deterministic-newgame-png-check \
		DETERMINISTIC_SCREENSHOT=/tmp/sdlpal-cores3se-native/cores3se_engine.png \
		DETERMINISTIC_SCREENSHOT_LCD=1 \
		DETERMINISTIC_SCREENSHOT_WIDTH=320 \
		DETERMINISTIC_SCREENSHOT_HEIGHT=240 \
		DETERMINISTIC_SCREENSHOT_MIN_NONBLACK=10000 \
		DETERMINISTIC_SCREENSHOT_MIN_COLORS=32

deterministic-trace-check:
	@test -n "$(DETERMINISTIC_GOLDEN)" || (echo "Set DETERMINISTIC_GOLDEN=/path/to/golden.trace"; exit 1)
	@test -n "$(DETERMINISTIC_CANDIDATE)" || (echo "Set DETERMINISTIC_CANDIDATE=/path/to/candidate.trace"; exit 1)
	$(PYTHON) -B ../tools/deterministic_trace_check.py \
		--manifest $(PACK_MANIFEST) \
		$(if $(DETERMINISTIC_TRACE_METADATA),--metadata $(DETERMINISTIC_TRACE_METADATA),) \
		$(if $(DETERMINISTIC_WRITE_METADATA),--write-metadata $(DETERMINISTIC_WRITE_METADATA),) \
		$(if $(DETERMINISTIC_GOLDEN_SCRIPT),--golden-script-trace $(DETERMINISTIC_GOLDEN_SCRIPT),) \
		$(if $(DETERMINISTIC_CANDIDATE_SCRIPT),--candidate-script-trace $(DETERMINISTIC_CANDIDATE_SCRIPT),) \
		$(if $(DETERMINISTIC_GOLDEN_EVENT),--golden-event-trace $(DETERMINISTIC_GOLDEN_EVENT),) \
		$(if $(DETERMINISTIC_CANDIDATE_EVENT),--candidate-event-trace $(DETERMINISTIC_CANDIDATE_EVENT),) \
		$(DETERMINISTIC_GOLDEN) \
		$(DETERMINISTIC_CANDIDATE)

contract-pack-build:
	$(PYTHON) -B ../tools/pal_pack_build.py $(PAL_DATA_DIR) --out-nor $(PACK_NOR) --out-tf $(PACK_TF) --manifest $(PACK_MANIFEST)

ifeq ($(EMBEDDED_CONTRACT),1)
contract-check: $(TARGET) contract-pack-build
	$(PYTHON) -B ../tools/engine_divergence_check.py --root .. --manifest ../tools/engine_divergence_manifest.json
	$(PYTHON) -B ../tools/embedded_contract_check.py \
		--root .. \
		--fail-on-source \
		--source-manifest $(CONTRACT_SOURCE_MANIFEST) \
		$(CONTRACT_CHECK_SOURCE_FLAGS) \
		--binary $(TARGET) \
		$(CONTRACT_BINARY_EXTRA_FLAGS) \
		$(CONTRACT_OBJECT_ARGS) \
		--pack $(PACK_NOR) \
		--pack $(PACK_TF) \
		--manifest $(PACK_MANIFEST) \
		--forbid-pack-archive VOC \
		--max-pack-size $(PACK_NOR)=16777216 \
		--max text=500000 \
		--max data=524288 \
		--max bss=9000000 \
		--max-symbol-prefix pal_sram_=307200 \
		--max-symbol-prefix pal_psram_=8388608
else
contract-check:
	@echo "Run as: make -C unix EMBEDDED_CONTRACT=1 contract-check"
	@exit 1
endif

contract-noaudio-check:
	$(MAKE) EMBEDDED_CONTRACT=1 EMBEDDED_CONTRACT_NO_AUDIO=1 contract-check

ifeq ($(EMBEDDED_CONTRACT_SANITIZE),1)
contract-sanitize-check: $(TARGET) contract-pack-build
	$(PYTHON) -B ../tools/engine_divergence_check.py --root .. --manifest ../tools/engine_divergence_manifest.json
	$(PYTHON) -B ../tools/embedded_contract_check.py \
		--root .. \
		--fail-on-source \
		--source-manifest $(CONTRACT_SOURCE_MANIFEST) \
		$(CONTRACT_CHECK_SOURCE_FLAGS) \
		$(CONTRACT_OBJECT_ARGS) \
		--pack $(PACK_NOR) \
		--pack $(PACK_TF) \
		--manifest $(PACK_MANIFEST) \
		--forbid-pack-archive VOC \
		--max-pack-size $(PACK_NOR)=16777216
else
contract-sanitize-check:
	$(MAKE) EMBEDDED_CONTRACT=1 EMBEDDED_CONTRACT_SANITIZE=1 contract-sanitize-check
endif
