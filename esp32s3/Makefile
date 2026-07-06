PYTHON ?= python3
PAL_DATA_DIR ?= /mnt/hgfs/deb13/PAL
PACK_NOR ?= /tmp/pal_nor_default.pak
PACK_TF ?= /tmp/pal_tf_default.pak
PACK_MANIFEST ?= /tmp/pal_pack_default_manifest.json
BUILD_DIR ?= ../build-cores3se
IDF_EXPORT ?= /home/john/esp-idf/export.sh
PORT ?=
BAUD ?= 460800
PAL_NOR_OFFSET ?= 0x310000
FLASH_MODE ?= dio
FLASH_SIZE ?= 16MB
FLASH_FREQ ?= 80m

.PHONY: all build pack-build check flash-app flash-nor clean

all: build

build:
	bash -lc 'source "$(IDF_EXPORT)" >/dev/null && if [ ! -f "$(BUILD_DIR)/build.ninja" ]; then idf.py -C . -B "$(BUILD_DIR)" set-target esp32s3; fi && idf.py -C . -B "$(BUILD_DIR)" build'

pack-build:
	$(PYTHON) -B ../tools/pal_pack_build.py $(PAL_DATA_DIR) --out-nor $(PACK_NOR) --out-tf $(PACK_TF) --manifest $(PACK_MANIFEST)

check: build pack-build
	bash -lc 'source "$(IDF_EXPORT)" >/dev/null && $(PYTHON) -B check_contract.py --root .. --build-dir "$(BUILD_DIR)" --nor-pack "$(PACK_NOR)" --tf-pack "$(PACK_TF)"'

flash-app: build
	bash -lc 'source "$(IDF_EXPORT)" >/dev/null && idf.py -C . -B "$(BUILD_DIR)" $(if $(PORT),-p "$(PORT)",) flash'

flash-nor: pack-build
	$(PYTHON) -m esptool --chip esp32s3 -b $(BAUD) --before default-reset --after hard-reset $(if $(PORT),--port "$(PORT)",) write-flash --flash-mode $(FLASH_MODE) --flash-size $(FLASH_SIZE) --flash-freq $(FLASH_FREQ) $(PAL_NOR_OFFSET) $(PACK_NOR)

clean:
	rm -rf "$(BUILD_DIR)"
