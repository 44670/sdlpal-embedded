# General makefile for generic unix & linux platforms

TARGET = sdlpal
TEST_TARGET = ./sdlpal-tests

INTER=.unix

HOST =
PYTHON ?= python3
TEST_CCFLAGS =
EMBEDDED_CONTRACT ?= 0
PACK_NOR ?= /tmp/pal_nor_default.pak
PACK_TF ?= /tmp/pal_tf_default.pak
PACK_MANIFEST ?= /tmp/pal_pack_default_manifest.json
PAL_DATA_DIR ?= /mnt/hgfs/deb13/PAL

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
	MIDI_CFILES =
	MIDI_CPPFILES =
	MIDI_LDFLAGS =
	SOURCES = .. ../sdl_compat
	CONTRACT_CFILE_EXCLUDES = ../aviplay.c ../font.c ../glslp.c ../midi_timidity.c ../midi_tsf.c ../mini_glloader.c ../mp3play.c ../oggplay.c ../opusplay.c ../resampler.c ../sound.c ../text.c ../video_glsl.c ../yj1.c
	CONTRACT_CPPFILE_EXCLUDES = ../rixplay.cpp
	CONTRACT_CFILES = embedded_contract_stubs.c ../embedded/pal_noheap.c ../embedded/pal_pack.c ../embedded/pal_text_cache.c ../embedded/pal_font_cache.c
	EMBEDDED_CCFLAGS = -ffunction-sections -fdata-sections -UUSE_SDL3 -DUSE_SDL3=0 -DPAL_HAS_MP3=0 -DPAL_HAS_OGG=0 -DPAL_HAS_OPUS=0 -DPAL_HAS_NATIVEMIDI=0 -DPAL_HAS_GLSL=0 -DPAL_HAS_CONFIG_PAGE=0 -DPAL_NO_LAUNCH_UI -DPAL_NO_RUNTIME_DECOMPRESS=1 -DPAL_NO_RUNTIME_HEAP=1
	EMBEDDED_LDFLAGS = -Wl,--gc-sections -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
else
	SOURCES = .. ../sdl_compat ../adplug ../liboggvorbis/src ../libopusfile/src ../libopusfile/celt ../libopusfile/silk ../libopusfile/silk/float ../libmad ../timidity
	CONTRACT_CFILE_EXCLUDES =
	CONTRACT_CPPFILE_EXCLUDES =
	CONTRACT_CFILES =
	EMBEDDED_CCFLAGS =
	EMBEDDED_LDFLAGS =
endif

CFILES = $(filter-out $(CONTRACT_CFILE_EXCLUDES),$(foreach dir, $(SOURCES), $(wildcard $(dir)/*.c))) $(MIDI_CFILES) $(CONTRACT_CFILES)
CPPFILES = $(filter-out $(CONTRACT_CPPFILE_EXCLUDES),$(foreach dir, $(SOURCES), $(wildcard $(dir)/*.cpp))) unix.cpp $(MIDI_CPPFILES)
TOBJFILES = $(CFILES:.c=$(INTER).o) $(CPPFILES:.cpp=$(INTER).o)
CONTRACT_SOURCE_ARGS = $(foreach src,$(CFILES) $(CPPFILES),--source-file $(src))
DEPFILES = $(OBJFILES:.o=.d)
TEST_CPPFILES = $(wildcard ../tests/*.cpp)
TEST_OBJFILES = $(TEST_CPPFILES:.cpp=.o)
SDL_CONFIG = pkg-config sdl3
ifeq ($(EMBEDDED_CONTRACT),1)
	SDL_CONFIG = pkg-config sdl2
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
override CCFLAGS += `$(SDL_CONFIG) --cflags` -DUSE_SDL3=1 -I../sdl_compat -D_GNU_SOURCE -g -Wall -O2 -fno-strict-aliasing $(EXTRA_CCFLAGS) $(TEST_CCFLAGS) $(GENERATED) $(EMBEDDED_CCFLAGS)
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

.PHONY : all clean check contract-pack-build contract-check

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
check: TEST_CCFLAGS = -I. -I.. -DUNIT_TEST=1 -isystem $(GTEST_DIR)/include
check: $(TEST_TARGET)
	@echo [EXEC] $(TEST_TARGET)
	@chmod +x $(TEST_TARGET)
	@exec $(TEST_TARGET)

contract-pack-build:
	$(PYTHON) -B ../tools/pal_pack_build.py $(PAL_DATA_DIR) --out-nor $(PACK_NOR) --out-tf $(PACK_TF) --manifest $(PACK_MANIFEST)

ifeq ($(EMBEDDED_CONTRACT),1)
contract-check: $(TARGET) contract-pack-build
	$(PYTHON) -B ../tools/embedded_contract_check.py \
		--root .. \
		--fail-on-source \
		$(CONTRACT_SOURCE_ARGS) \
		--source-define PAL_NO_RUNTIME_HEAP=1 \
		--source-define PAL_NO_RUNTIME_DECOMPRESS=1 \
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
		--source-exclude native_midi_alsa.c \
		--binary $(TARGET) \
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
