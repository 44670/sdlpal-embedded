#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
DEVKITPRO := /opt/devkitpro
endif
ifeq ($(strip $(DEVKITARM)),)
DEVKITARM := $(DEVKITPRO)/devkitARM
endif

include $(DEVKITARM)/ds_rules

GAME_TITLE := SDLPAL
GAME_SUBTITLE1 := Nintendo DS native port
GAME_SUBTITLE2 := sdlpal-embedded
NDS_MAKER_CODE := 00
NDS_ROM_VERSION := 0
# Keep the shipped image on the classic NTR path.  Calico's shared ARM9/ARM7
# ELFs contain optional TWL program headers; ndstool only copies those sections
# when the ROM header is larger than the legacy 0x200-byte NTR header.
NDS_HEADER_SIZE := 0x200
PAL_NDS_SILENT_DIAGNOSTIC ?= 0

TARGET := sdlpal
BUILD := build
PROJECT_DIR ?= $(CURDIR)
PAL_FULL_PACK ?= $(abspath ../esp32s3/TF_datapak/pal_full.pak)
NITRO_STAGE := $(PROJECT_DIR)/$(BUILD)/nitrofiles/pal_full.pak

SOURCES := source .. ../embedded ../adplug ../sdl_compat ../unix \
   ../esp32s3/engine_bridge ../esp32s3/native_engine_shim
INCLUDES := include ../esp32s3/engine_bridge \
   ../esp32s3/engine_bridge/include \
   ../esp32s3/native_engine_shim/include .. ../embedded ../sdl_compat

ARCH := -march=armv5te -mtune=arm946e-s
GIT_REV := $(shell git -C .. rev-parse --short=12 HEAD 2>/dev/null || printf unknown)

PAL_DEFINES := \
   -DMEM_LEVEL2=1 \
   -DPAL_STORAGE_NITROFS=1 \
   -DPAL_TARGET_NDS=1 \
   -DPAL_EXTREME_TWO_SCREENS=1 \
   -DPAL_EXTREME_SPRITES_TO_DRAW=512 \
   -DPAL_GLOBAL_BUFFER_SIZE=256 \
   -DPAL_HEADLESS_SDL_SHIM=1 \
   -DPAL_SDL_SHIM_EXTERNAL_SURFACES_ONLY=1 \
   -DPAL_MEM_LEVEL2_RESIDENT_PACK_BYTES=1048576u \
   -DPAL_ENGINE_TF_TOC_BYTES=40960u \
   -DPAL_LOG_BUFFER_SIZE=1024 \
   -DPAL_NO_LAUNCH_UI \
   -DPAL_NO_RUNTIME_DECOMPRESS=1 \
   -DPAL_NO_RUNTIME_HEAP=1 \
   -DPAL_TARGET_SAVE_BACKEND=1 \
   -DPAL_CONTRACT_EXTERNAL_RIX=1 \
   -DPAL_CONTRACT_TARGET_PACK_PROVIDER=1 \
   -DPAL_CONTRACT_NO_SFX=1 \
   -DPAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS=1 \
   -DPAL_TARGET_GURU_MEDITATION=1 \
   -DPAL_NDS_SILENT_DIAGNOSTIC=$(PAL_NDS_SILENT_DIAGNOSTIC) \
   -DPAL_HAS_JOYSTICKS=0 \
   -DPAL_HAS_TOUCH=0 \
   -DPAL_HAS_MP3=0 \
   -DPAL_HAS_OGG=0 \
   -DPAL_HAS_OPUS=0 \
   -DPAL_HAS_NATIVEMIDI=0 \
   -DPAL_HAS_GLSL=0 \
   -DPAL_HAS_CONFIG_PAGE=0 \
   -DUSE_RIX_EXTRA_INIT=0 \
   -DUSE_SDL3=0 \
   -DSHIM_MAX_SURFACES=6 \
   -DSHIM_MAX_PALETTES=1 \
   -DSHIM_MAX_TEXTURES=1 \
   -DSHIM_SURFACE_PIXELS=1u \
   -DSHIM_TEXTURE_PIXELS=1u \
   -DSHIM_KEYBOARD_KEYS=256 \
   -DSHIM_EVENT_QUEUE=8 \
   -DNDS_GIT_REVISION=\"$(GIT_REV)\"

CFLAGS := -g -Wall -Wextra -Os -ffunction-sections -fdata-sections \
   -fno-strict-aliasing $(ARCH) $(INCLUDE) -DARM9 $(PAL_DEFINES)
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -fno-use-cxa-atexit
ASFLAGS := -g $(ARCH)
# Calico's legacy ARM9 linker script omits the TWL loadlist and uses the
# classic NTR main-RAM layout.  Pair it with the 0x200 NTR ROM header below.
LDFLAGS := -specs=$(CALICO)/share/ds9-legacy.specs -g $(ARCH) \
   -Wl,-Map,$(PROJECT_DIR)/$(BUILD)/$(TARGET).map,--gc-sections \
   -Wl,--wrap=PAL_MKFOpenPackArchive \
   -Wl,--wrap=PAL_MKFIsPackArchive \
   -Wl,--wrap=PAL_MKFMapChunk \
   -Wl,--wrap=PAL_MKFGetChunkCount \
   -Wl,--wrap=PAL_MKFGetChunkSize \
   -Wl,--wrap=PAL_MKFReadChunk \
   -Wl,--wrap=access \
   -Wl,--wrap=fclose

LIBS := -lfilesystem -lfat -lnds9 -lm
LIBDIRS := $(LIBNDS) $(PORTLIBS)

CFILES := \
   battle.c ending.c fight.c game.c global.c input.c itemmenu.c \
   magicmenu.c main.c map.c overlay.c palcfg.c palcommon.c paldebug.c \
   palette.c play.c res.c rngplay.c scene.c script.c ui.c uibattle.c \
   uigame.c util.c video.c \
   sdl_compat.c embedded_contract_stubs.c \
   pal_pack.c pal_level2_resident_pack.c pal_memory_profile.c \
   pal_text_cache.c pal_font_cache.c pal_font10_cache.c pal_native_ui.c \
   pal_music_cache.c \
   pal_engine_pack_provider.c pal_engine_posix_shims.c sdl_shim.c \
   nds_entry.c nds_memory.c nds_board.c nds_input.c nds_time.c \
   nds_video.c nds_packs.c nds_guru.c nds_save.c
CPPFILES := player.cpp rix.cpp nds_music.cpp
SFILES :=

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(PROJECT_DIR)/$(TARGET)
export VPATH := $(foreach dir,$(SOURCES),$(PROJECT_DIR)/$(dir))
export DEPSDIR := $(PROJECT_DIR)/$(BUILD)
export NITRO_FILES := $(PROJECT_DIR)/$(BUILD)/nitrofiles
export PROJECT_DIR PAL_FULL_PACK NITRO_STAGE GIT_REV
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(PROJECT_DIR)/$(dir)) \
   $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(PROJECT_DIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export LD := $(CXX)
export OFILES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)

.PHONY: all clean check $(BUILD)

all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(PROJECT_DIR)/Makefile

check: all
	@python3 -B check_nds.py --elf $(TARGET).elf --nds $(TARGET).nds \
	   --pack $(PAL_FULL_PACK)

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nds

else

$(OUTPUT).nds: $(OUTPUT).elf $(NITRO_STAGE) $(GAME_ICON) $(PROJECT_DIR)/Makefile
	$(SILENTCMD)ndstool -c $@ -h $(NDS_HEADER_SIZE) -9 $< $(_ARM7_ELF) \
	   -b $(GAME_ICON) "$(GAME_TITLE);$(GAME_SUBTITLE1);$(GAME_SUBTITLE2)" \
	   $(_ADDFILES) -g '####' $(NDS_MAKER_CODE) "$(GAME_TITLE)" \
	   $(NDS_ROM_VERSION)
	$(SILENTMSG) built ... $(notdir $@)
$(OUTPUT).elf: $(OFILES)

$(NITRO_STAGE): $(PAL_FULL_PACK)
	@mkdir -p $(dir $@)
	cp --reflink=auto $< $@

main.o: CFLAGS += -Dmain=PAL_EngineMain

-include $(DEPSDIR)/*.d

endif
