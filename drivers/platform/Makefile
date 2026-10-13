# SPDX-License-Identifier: GPL-2.0
#
# Makefile for linux/drivers/platform
#

obj-$(CONFIG_ESP32S31_RADIO_XIP) += esp32s31-radio-loader-core.o
obj-$(CONFIG_ESP32S31_RADIO_SMODE_DRIVER) += esp32s31-radio.o
esp32s31-radio-y := esp32s31-radio-module.o \
	esp32s31-radio-smode.o esp32s31-radio-rtos.o \
	esp32s31-radio-loader.o esp32s31-radio-imports.o \
	esp32s31-radio-wifi.o esp32s31-radio-pmu.o

# RX buffer processing and ACK submission are throughput critical on S31.
CFLAGS_esp32s31-radio-wifi.o += -Os
# rtos.c saves and restores the F registers around the payload's code in
# inline asm, and the kernel itself is built without F: add F to -march only.
# All of CC_FLAGS_FPU would also mark it as a kernel-FPU unit, which may not
# include <linux/fpu.h>. CC_FLAGS_FPU's -march has D too: drop it, so that
# no D instruction can be emitted for the F-only hart.
CFLAGS_esp32s31-radio-rtos.o += $(subst imafd,imaf,$(filter -march=%,$(CC_FLAGS_FPU)))

LDFLAGS_esp32s31-radio.o += --gc-sections \
	-T $(src)/esp32s31-radio-core.lds --undefined=init_module \
	--undefined=s31_radio_modinfo_anchor

obj-$(CONFIG_X86)		+= x86/
obj-$(CONFIG_LOONGARCH)		+= loongarch/
obj-$(CONFIG_MELLANOX_PLATFORM)	+= mellanox/
obj-$(CONFIG_MIPS)		+= mips/
obj-$(CONFIG_OLPC_EC)		+= olpc/
obj-$(CONFIG_GOLDFISH)		+= goldfish/
obj-$(CONFIG_CHROME_PLATFORMS)	+= chrome/
obj-$(CONFIG_CZNIC_PLATFORMS)	+= cznic/
obj-$(CONFIG_SURFACE_PLATFORMS)	+= surface/
obj-$(CONFIG_ARM64_PLATFORM_DEVICES)	+= arm64/
