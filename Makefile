# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2026, UAB Kurokesu. All rights reserved.
#
# Makefile for AR0234 camera driver on Raspberry Pi (device tree overlay + kernel module)

SRC_DIR   := $(shell pwd)
BUILD_DIR := build

DTS       := ar0234-overlay.dts
DTBO      := ar0234.dtbo
DTC       := dtc
DTC_FLAGS := -Wno-interrupts_property -Wno-unit_address_vs_reg -@ -I dts -O dtb

KDIR      ?= /lib/modules/$(shell uname -r)/build
CCFLAGS   := -Werror

.PHONY: all dtbo module clean

all: $(BUILD_DIR)/$(DTBO) $(BUILD_DIR)/ar0234.ko

dtbo: $(BUILD_DIR)/$(DTBO)
module: $(BUILD_DIR)/ar0234.ko

$(BUILD_DIR)/$(DTBO): $(DTS) | $(BUILD_DIR)
	$(DTC) $(DTC_FLAGS) -o $@ $<

$(BUILD_DIR)/Kbuild: | $(BUILD_DIR)
	@echo "ccflags-y += $(CCFLAGS)" > $@
	@echo "obj-m += ar0234.o" >> $@
	@ln -sf $(SRC_DIR)/ar0234.c $(BUILD_DIR)/ar0234.c

$(BUILD_DIR)/ar0234.o: ar0234.c $(BUILD_DIR)/Kbuild
	$(MAKE) -C $(KDIR) M=$(SRC_DIR)/$(BUILD_DIR) ar0234.o

$(BUILD_DIR)/ar0234.ko: ar0234.c $(BUILD_DIR)/Kbuild
	$(MAKE) -C $(KDIR) M=$(SRC_DIR)/$(BUILD_DIR) modules

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
