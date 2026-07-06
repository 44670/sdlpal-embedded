PYTHON ?= python3
PAL_DATA_DIR ?= /mnt/hgfs/deb13/PAL
PACK_NOR ?= /tmp/pal_nor_default.pak
PACK_TF ?= /tmp/pal_tf_default.pak
PACK_MANIFEST ?= /tmp/pal_pack_default_manifest.json
BUILD_DIR ?= ../build-cores3se
IDF_EXPORT ?= /home/john/esp-idf/export.sh

.PHONY: all build pack-build check clean

all: build

build:
	bash -lc 'source "$(IDF_EXPORT)" >/dev/null && if [ ! -f "$(BUILD_DIR)/build.ninja" ]; then idf.py -C . -B "$(BUILD_DIR)" set-target esp32s3; fi && idf.py -C . -B "$(BUILD_DIR)" build'

pack-build:
	$(PYTHON) -B ../tools/pal_pack_build.py $(PAL_DATA_DIR) --out-nor $(PACK_NOR) --out-tf $(PACK_TF) --manifest $(PACK_MANIFEST)

check: build pack-build
	bash -lc 'source "$(IDF_EXPORT)" >/dev/null && $(PYTHON) -B check_contract.py --root .. --build-dir "$(BUILD_DIR)" --nor-pack "$(PACK_NOR)"'

clean:
	rm -rf "$(BUILD_DIR)"
