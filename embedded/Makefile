CC ?= gcc
OBJDUMP ?= objdump
NM ?= nm
SIZE ?= size
SDL_CONFIG ?= pkg-config sdl2

CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections
LDFLAGS ?= -Wl,--gc-sections
SDL_CFLAGS := $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS := $(shell $(SDL_CONFIG) --libs)

BUILD_DIR := build
SMOKE := $(BUILD_DIR)/pal_pack_smoke
MEMORY_SMOKE := $(BUILD_DIR)/pal_memory_smoke
NATIVE_SDL_SMOKE := $(BUILD_DIR)/pal_native_sdl_smoke

.PHONY: all clean check artifact-check

all: $(SMOKE) $(MEMORY_SMOKE) $(NATIVE_SDL_SMOKE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SMOKE): pal_pack.c pal_pack.h pal_pack_smoke.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) pal_pack.c pal_pack_smoke.c -o $@ $(LDFLAGS)

$(MEMORY_SMOKE): pal_memory.c pal_memory.h pal_memory_smoke.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) pal_memory.c pal_memory_smoke.c -o $@ $(LDFLAGS)

$(NATIVE_SDL_SMOKE): pal_memory.c pal_memory.h pal_pack.c pal_pack.h pal_native_sdl_smoke.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) pal_memory.c pal_pack.c pal_native_sdl_smoke.c -o $@ $(LDFLAGS) $(SDL_LIBS)

check: $(SMOKE) $(MEMORY_SMOKE) $(NATIVE_SDL_SMOKE)
	$(SMOKE)
	$(MEMORY_SMOKE)
	SDL_VIDEODRIVER=dummy $(NATIVE_SDL_SMOKE)

artifact-check: check
	$(SIZE) $(SMOKE)
	$(OBJDUMP) -h $(SMOKE)
	$(NM) -S --size-sort -C $(SMOKE)
	$(SIZE) $(MEMORY_SMOKE)
	$(OBJDUMP) -h $(MEMORY_SMOKE)
	$(NM) -S --size-sort -C $(MEMORY_SMOKE)
	$(SIZE) $(NATIVE_SDL_SMOKE)
	$(OBJDUMP) -h $(NATIVE_SDL_SMOKE)
	$(NM) -S --size-sort -C $(NATIVE_SDL_SMOKE)

clean:
	rm -rf $(BUILD_DIR)
