CC ?= gcc
OBJDUMP ?= objdump
NM ?= nm
SIZE ?= size

CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections
LDFLAGS ?= -Wl,--gc-sections

BUILD_DIR := build
SMOKE := $(BUILD_DIR)/pal_pack_smoke
MEMORY_SMOKE := $(BUILD_DIR)/pal_memory_smoke

.PHONY: all clean check artifact-check

all: $(SMOKE) $(MEMORY_SMOKE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SMOKE): pal_pack.c pal_pack.h pal_pack_smoke.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) pal_pack.c pal_pack_smoke.c -o $@ $(LDFLAGS)

$(MEMORY_SMOKE): pal_memory.c pal_memory.h pal_memory_smoke.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) pal_memory.c pal_memory_smoke.c -o $@ $(LDFLAGS)

check: $(SMOKE) $(MEMORY_SMOKE)
	$(SMOKE)
	$(MEMORY_SMOKE)

artifact-check: check
	$(SIZE) $(SMOKE)
	$(OBJDUMP) -h $(SMOKE)
	$(NM) -S --size-sort -C $(SMOKE)
	$(SIZE) $(MEMORY_SMOKE)
	$(OBJDUMP) -h $(MEMORY_SMOKE)
	$(NM) -S --size-sort -C $(MEMORY_SMOKE)

clean:
	rm -rf $(BUILD_DIR)
