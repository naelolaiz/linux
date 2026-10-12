/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ESP32-S31 per-hart timer interrupt, provided by
 * drivers/irqchip/irq-esp32s31-smp.c for timer-esp32s31-systimer.c.
 */
#ifndef __LINUX_IRQCHIP_IRQ_ESP32S31_H
#define __LINUX_IRQCHIP_IRQ_ESP32S31_H

#ifdef CONFIG_ESP32S31_SMP
int esp32s31_local_timer_init(void);
void esp32s31_local_timer_enable(unsigned int cpu);
void esp32s31_local_timer_disable(unsigned int cpu);
#endif

#endif /* __LINUX_IRQCHIP_IRQ_ESP32S31_H */
