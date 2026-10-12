/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared between the ESP32-S31 CLIC, INTMTX and cross-core IPI drivers.
 */
#ifndef _IRQ_ESP32S31_INTERNAL_H
#define _IRQ_ESP32S31_INTERNAL_H

#include <linux/types.h>

struct irq_desc;

/* CLIC IDs delegated to S-mode by OpenSBI. */
#define ESP32S31_CLIC_EXT_FIRST		16U
#define ESP32S31_CLIC_EXT_LAST		47U
/* Per-hart IDs, kept out of the INTMTX allocator. */
#define ESP32S31_CLIC_IPI_SLOT		40U
#define ESP32S31_CLIC_TIMER_SLOT	41U

int esp_clic_map_local(unsigned int slot);
int esp_clic_install_local(unsigned int slot,
			   void (*handler)(struct irq_desc *), void *data);
u64 esp_clic_pending_local(void);
void esp_clic_configure_local(unsigned int cpu, unsigned int slot,
			      bool enable);
void esp_intmtx_route_local(unsigned int cpu, unsigned int source,
			    unsigned int slot);
void esp_intmtx_unroute_local(unsigned int cpu, unsigned int source);

#ifdef CONFIG_ESP32S31_SMP
int __init esp32s31_smp_irq_init(void);
#else
static inline int esp32s31_smp_irq_init(void) { return 0; }
#endif

#endif /* _IRQ_ESP32S31_INTERNAL_H */
