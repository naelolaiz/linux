/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_RISCV_ESP32S31_RADIO_MAP_H
#define _ASM_RISCV_ESP32S31_RADIO_MAP_H

/*
 * ESP32-S31 physical windows that ESP-IDF's closed radio libraries
 * address directly.  With CONFIG_ESP32S31_RADIO_SMODE, setup_vm_final()
 * maps each one to one with 4 MiB leaves in swapper_pg_dir.
 */
#define ESP32S31_MODEM_WINDOW		0x20000000UL	/* modem, peripherals */
#define ESP32S31_MODEM_WINDOW_SIZE	0x00c00000UL
#define ESP32S31_CACHE_WINDOW		0x2c000000UL	/* cache controller */
#define ESP32S31_SRAM_WINDOW		0x2f000000UL	/* internal SRAM */
#define ESP32S31_ROM_WINDOW		0x2f800000UL	/* mask ROM */

#endif /* _ASM_RISCV_ESP32S31_RADIO_MAP_H */
