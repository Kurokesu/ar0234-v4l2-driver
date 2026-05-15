# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026, UAB Kurokesu. All rights reserved.
#
# Makefile for camera driver on Raspberry Pi (device tree overlay + kernel module)

SRC_DIR   := $(shell pwd)
BUILD_DIR := build

DRV_SRC   := $(wildcard *.c)
DRV_NAME  := $(shell grep '^BUILT_MODULE_NAME=' dkms.conf | cut -d'"' -f2)
DTS       := $(wildcard *-overlay.dts)
DTBO      := $(DRV_NAME).dtbo
DTC       := dtc
DTC_FLAGS := -Wno-interrupts_property -Wno-unit_address_vs_reg -@ -I dts -O dtb

KDIR      ?= /lib/modules/$(shell uname -r)/build
CCFLAGS   := -Werror

# Status line formatter (8-char tag column, arg at col 11)
PRINT = printf '  %-7s %s\n'

ifeq ($(DRV_SRC),)
  $(error No .c source file found in project root)
endif

ifeq ($(DRV_NAME),)
  $(error No BUILT_MODULE_NAME found in dkms.conf)
endif

ifeq ($(DTS),)
  $(error No *-overlay.dts file found in project root)
endif

.PHONY: all obj dtbo module clean

all: $(BUILD_DIR)/$(DTBO) $(BUILD_DIR)/$(DRV_NAME).ko

obj: $(BUILD_DIR)/$(DRV_NAME).o

dtbo: $(BUILD_DIR)/$(DTBO)

module: $(BUILD_DIR)/$(DRV_NAME).ko

$(BUILD_DIR)/$(DTBO): $(DTS) | $(BUILD_DIR)
	@$(PRINT) DTC $@
	@$(DTC) $(DTC_FLAGS) -o $@ $<
	@$(PRINT) BUILT $@

$(BUILD_DIR)/Kbuild: | $(BUILD_DIR)
	@echo "ccflags-y += $(CCFLAGS)" > $@
	@echo "obj-m += $(DRV_NAME).o" >> $@
	@ln -sf $(SRC_DIR)/$(DRV_SRC) $(BUILD_DIR)/$(DRV_SRC)

$(BUILD_DIR)/$(DRV_NAME).o: $(DRV_SRC) $(BUILD_DIR)/Kbuild
	@$(PRINT) KBUILD $(DRV_NAME).o
	@$(MAKE) -C $(KDIR) M=$(SRC_DIR)/$(BUILD_DIR) $(DRV_NAME).o
	@$(PRINT) BUILT $@

$(BUILD_DIR)/$(DRV_NAME).ko: $(DRV_SRC) $(BUILD_DIR)/Kbuild
	@$(PRINT) KBUILD $(DRV_NAME).ko
	@$(MAKE) -C $(KDIR) M=$(SRC_DIR)/$(BUILD_DIR) modules
	@$(PRINT) BUILT $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

clean:
	@$(PRINT) CLEAN $(BUILD_DIR)
	@rm -rf $(BUILD_DIR)
