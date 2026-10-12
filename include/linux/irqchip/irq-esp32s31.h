/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ESP32-S31 per-hart interrupt helpers, provided by
 * drivers/irqchip/irq-esp32s31-smp.c for timer-esp32s31-systimer.c and
 * arch/riscv.
 */
#ifndef __LINUX_IRQCHIP_IRQ_ESP32S31_H
#define __LINUX_IRQCHIP_IRQ_ESP32S31_H

struct cpumask;

#ifdef CONFIG_ESP32S31_SMP
int esp32s31_local_timer_init(void);
void esp32s31_local_timer_enable(unsigned int cpu);
void esp32s31_local_timer_disable(unsigned int cpu);
void esp32s31_irq_poll(void);
#endif

/* Called by arch/riscv/kernel/smp.c after it sends the backtrace IPI. */
#ifdef CONFIG_ESP32S31_SMP_LOCKUP_REPORT
void esp32s31_report_harts(const struct cpumask *mask);
#else
static inline void esp32s31_report_harts(const struct cpumask *mask) { }
#endif

#endif /* __LINUX_IRQCHIP_IRQ_ESP32S31_H */
