/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ESP32S31_RADIO_XIP_H
#define ESP32S31_RADIO_XIP_H

#define S31_XIP_MAGIC 0x58313353U
#define S31_XIP_MAP_BASE 0xbe000000U
#define S31_XIP_MAP_PHYS 0x40000000U
#define S31_XIP_MAP_SIZE 0x400000U
/*
 * The radio slot: raw flash 0x380000, between the DTB (0x300000) and the
 * kernel (0x500000). Espressif's SPL maps raw flash 0x100000 at 0x40000000,
 * so the slot is at 0x40280000 and ends at the kernel's 0x40400000.
 * radio_image.py must link the image at the same S31_XIP_BASE.
 */
#define S31_XIP_BASE 0xbe280000U
#define S31_XIP_PHYS 0x40280000U
#define S31_XIP_HEADER_SIZE 4096U
#define S31_XIP_SLOT_SIZE 0x180000U
#define S31_XIP_RAM_SIZE 40960U
#define S31_XIP_WIFI_IRAM_BASE 0xbe420000U
#define S31_XIP_WIFI_IRAM_PHYS 0x2f060000U
#define S31_XIP_WIFI_IRAM_CAPACITY 0x11800U
#define S31_XIP_WIFI_IRAM_MAP_SIZE 0x12000U
#define S31_XIP_EXPORTS 23U
/* The last export is the address of the payload's ISR nesting counter. */
#define S31_XIP_EXPORT_ISR_DEPTH (S31_XIP_EXPORTS - 1U)
/* Header layout: 16 u32 fields, the exports, the imports, 2 u32 fields. */
#define S31_XIP_IMPORTS_OFFSET ((16U + S31_XIP_EXPORTS) * 4U)
#define S31_XIP_IMPORT_SIZE 68U
#define S31_XIP_WIFI_IRAM_FIELDS_SIZE 8U
#define S31_XIP_MAX_IMPORTS ((S31_XIP_HEADER_SIZE - S31_XIP_IMPORTS_OFFSET - \
			      S31_XIP_WIFI_IRAM_FIELDS_SIZE) / S31_XIP_IMPORT_SIZE)
#define S31_XIP_WIFI_IRAM_FIELDS_OFFSET (S31_XIP_IMPORTS_OFFSET + \
					 S31_XIP_MAX_IMPORTS * S31_XIP_IMPORT_SIZE)
/* The 48-entry CLIC vector table the loader copies into the arena. */
#define S31_XIP_VECTORS_SIZE 192U

struct s31_xip_import {
	u32 offset;
	char name[64];
};

struct s31_xip_header {
	u32 magic, version, abi, image_size;
	u32 base, ram, ram_capacity, data_offset;
	u32 data_size, bss_offset, bss_size, vectors_offset;
	u32 import_count, export_count, body_crc, header_crc;
	u32 exports[S31_XIP_EXPORTS];
	struct s31_xip_import imports[S31_XIP_MAX_IMPORTS];
	/* Version 2: pristine Wi-Fi IRAM copy inside the flash body. */
	u32 wifi_iram_offset, wifi_iram_size;
};

/* Provided by the built-in esp32s31-radio-loader-core.c. */
extern u8 esp32s31_radio_xip_ram[S31_XIP_RAM_SIZE];
const void *esp32s31_radio_xip_get(void);
void *esp32s31_radio_wifi_iram_get(void);

/* Bounds/address preflight shared with the image-format validator. */
static inline bool s31_xip_range(u32 offset, u32 size, u32 capacity)
{
	return offset <= capacity && size <= capacity - offset;
}

static inline bool s31_xip_valid(const struct s31_xip_header *h, u32 ram)
{
	u32 i, body_end, isr_depth;

	if (h->magic != S31_XIP_MAGIC ||
	    h->version != 2 || h->abi != 1 ||
	    h->base != S31_XIP_BASE || h->ram != ram ||
	    h->ram_capacity != S31_XIP_RAM_SIZE ||
	    h->image_size <= S31_XIP_HEADER_SIZE ||
	    h->image_size > S31_XIP_SLOT_SIZE ||
	    h->export_count != S31_XIP_EXPORTS ||
	    h->import_count > S31_XIP_MAX_IMPORTS ||
	    h->data_offset < S31_XIP_HEADER_SIZE ||
	    !s31_xip_range(h->data_offset, h->data_size, h->image_size) ||
	    !s31_xip_range(0, h->data_size, S31_XIP_RAM_SIZE) ||
	    h->bss_offset != h->data_size ||
	    !s31_xip_range(h->bss_offset, h->bss_size, S31_XIP_RAM_SIZE) ||
	    !s31_xip_range(h->vectors_offset, S31_XIP_VECTORS_SIZE,
			   h->data_size))
		return false;
	if (!h->wifi_iram_size ||
	    h->wifi_iram_size > S31_XIP_WIFI_IRAM_CAPACITY ||
	    h->wifi_iram_offset < S31_XIP_HEADER_SIZE ||
	    !s31_xip_range(h->wifi_iram_offset, h->wifi_iram_size,
			  h->data_offset))
		return false;
	body_end = S31_XIP_BASE + h->data_offset;
	for (i = 0; i < S31_XIP_EXPORT_ISR_DEPTH; i++) {
		if ((h->exports[i] < S31_XIP_BASE + S31_XIP_HEADER_SIZE ||
		     h->exports[i] >= body_end ||
		     (h->exports[i] >= S31_XIP_BASE + h->wifi_iram_offset &&
		      h->exports[i] < S31_XIP_BASE + h->wifi_iram_offset +
					 h->wifi_iram_size)) &&
		    !(h->exports[i] >= S31_XIP_WIFI_IRAM_BASE &&
		      h->exports[i] < S31_XIP_WIFI_IRAM_BASE + h->wifi_iram_size))
			return false;
		if (h->exports[i] & 1)
			return false;
	}
	isr_depth = h->exports[S31_XIP_EXPORT_ISR_DEPTH];
	if (isr_depth < ram ||
	    !s31_xip_range(isr_depth - ram, 4,
			  h->bss_offset + h->bss_size) || (isr_depth & 3))
		return false;
	for (i = 0; i < h->import_count; i++) {
		const struct s31_xip_import *p = &h->imports[i];
		u32 j;

		if ((p->offset & 3) ||
		    !s31_xip_range(p->offset, 4, h->data_size) ||
		    !p->name[0] || !memchr(p->name, 0, sizeof(p->name)))
			return false;
		for (j = 0; j < i; j++)
			if (p->offset == h->imports[j].offset ||
			    !strcmp(p->name, h->imports[j].name))
				return false;
	}
	return true;
}

#endif
