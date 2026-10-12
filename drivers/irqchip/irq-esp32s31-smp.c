// SPDX-License-Identifier: GPL-2.0-only
/*
 * ESP32-S31 per-hart interrupts: the cross-core IPI and the timer.
 *
 * Device interrupts stay in the CLIC and INTMTX domains, routed to hart 0.
 * Each hart also takes two interrupts in its own S-mode CLIC bank, routed
 * in its own INTMTX table:
 *
 * - CLIC 40: the doorbell to this hart (HP_SYSTEM CPU_INT_FROM_CPU_n,
 *   matrix source 65 + n). Linux's IPIs are multiplexed on it.
 * - CLIC 41: systimer comparator n (matrix source 33 + n), for the per-CPU
 *   clockevent of timer-esp32s31-systimer.c.
 *
 * Linux IPIs then never go through the SBI, and neither do remote TLB and
 * I-cache flushes: riscv_ipi_set_virq_range() runs here, before
 * sbi_ipi_init(), which then neither installs SBI IPIs nor turns on SBI
 * remote fences. Espressif's OpenSBI has no IPI device for either.
 */

#include <linux/cpu.h>
#include <linux/cpuhotplug.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqchip/irq-esp32s31.h>
#include <linux/moduleparam.h>
#include <linux/reboot.h>
#include <linux/smp.h>

#include <asm/esp-clic.h>
#include <asm/smp.h>

#include "irq-esp32s31-internal.h"

/* HP_SYSTEM CPU_INT_FROM_CPU_0 (rings hart 0) and _1 (rings hart 1). */
#define ESP32S31_DOORBELL_BASE		0x20586010U
#define ESP32S31_DOORBELL_SIZE		0x8U

#define ESP32S31_IPI_SOURCE(cpu)	(65U + (cpu))
#define ESP32S31_TIMER_SOURCE(cpu)	(33U + (cpu))

#define ESP32S31_NR_HARTS		2U

static void __iomem *esp32s31_doorbells;

static void __iomem *esp32s31_doorbell(unsigned int cpu)
{
	return esp32s31_doorbells + cpu * 4;
}

static void esp32s31_ipi_send(unsigned int cpu)
{
	if (WARN_ON_ONCE(cpu >= ESP32S31_NR_HARTS))
		return;

	/* writel() orders the reason bit ipi_mux set before the doorbell. */
	writel(1, esp32s31_doorbell(cpu));
}

/*
 * The doorbell is a level that software sets and clears. Clear it before
 * processing, then look again: a sender that found its reason bit already
 * set did not ring, so a reason can be pending with the doorbell clear.
 */
static void esp32s31_ipi_chained(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	void __iomem *db = esp32s31_doorbell(raw_smp_processor_id());

	chained_irq_enter(chip, desc);
	if (readl(db)) {
		do {
			writel(0, db);
			ipi_mux_process();
			/* Order the processing before the re-check of the bits. */
			smp_mb();
		} while (readl(db) || ipi_mux_pending());
	}
	chained_irq_exit(chip, desc);
}

/*
 * Idle-poll counts per CPU, read-only in
 * /sys/module/irq_esp32s31_smp/parameters/: idle_poll_drops counts the
 * polls that found a CLIC slot pending and lowered the level,
 * idle_poll_stuck those that found a slot still pending from the poll
 * before, and idle_poll_stuck_slots holds the slots of the last of those
 * (bit n is CLIC ID n + 16). A hart whose level is lowered at every
 * interrupt and idle entry takes a pending interrupt before a poll can
 * see it, so both counts should stay near 0.
 */
static unsigned int idle_poll_drops[NR_CPUS];
static unsigned int idle_poll_stuck[NR_CPUS];
static unsigned int idle_poll_stuck_slots[NR_CPUS];
module_param_array(idle_poll_drops, uint, NULL, 0444);
module_param_array(idle_poll_stuck, uint, NULL, 0444);
module_param_array(idle_poll_stuck_slots, hexint, NULL, 0444);

/**
 * esp32s31_irq_poll - let the idle loop take what is pending
 *
 * The idle loop enters wfi with interrupts off, and in that state a
 * doorbell does not wake the hart: an IPI to an idle hart is stranded.
 * With both harts up they idle by polling with interrupts on instead
 * (esp32s31_ipi_starting_cpu()). A hart can also be left unable to take
 * an interrupt that is pending, with the CLIC's S-mode level stuck at
 * its 0xff sentinel; the polling loop calls this to lower the level when
 * something is pending, and the hart then takes it as a normal interrupt.
 * Both from GrieferPig's linux-esp32-s31, where this poll runs the
 * pending handlers itself.
 */
void esp32s31_irq_poll(void)
{
	static u64 last_pending[NR_CPUS];
	unsigned long flags;
	unsigned int cpu;
	u64 pending, stuck;

	raw_local_irq_save(flags);
	cpu = smp_processor_id();
	pending = esp_clic_pending_local();
	if (pending) {
		idle_poll_drops[cpu]++;
		stuck = pending & last_pending[cpu];
		if (stuck) {
			idle_poll_stuck[cpu]++;
			idle_poll_stuck_slots[cpu] = stuck >> ESP32S31_CLIC_EXT_FIRST;
		}
		esp_clic_drop_level();
	}
	last_pending[cpu] = pending;
	raw_local_irq_restore(flags);
}

static int esp32s31_ipi_starting_cpu(unsigned int cpu)
{
	if (cpu >= ESP32S31_NR_HARTS)
		return -EINVAL;

	if (cpu)
		cpu_idle_poll_ctrl(true);

	writel(0, esp32s31_doorbell(cpu));
	esp_intmtx_route_local(cpu, ESP32S31_IPI_SOURCE(cpu),
			       ESP32S31_CLIC_IPI_SLOT);
	esp_clic_configure_local(cpu, ESP32S31_CLIC_IPI_SLOT, true);
	return 0;
}

static int esp32s31_ipi_dying_cpu(unsigned int cpu)
{
	esp_clic_configure_local(cpu, ESP32S31_CLIC_IPI_SLOT, false);
	esp_intmtx_unroute_local(cpu, ESP32S31_IPI_SOURCE(cpu));
	writel(0, esp32s31_doorbell(cpu));
	if (cpu)
		cpu_idle_poll_ctrl(false);
	return 0;
}

/*
 * Without the doorbells no other hart can take an IPI or a per-hart timer
 * interrupt: keep Linux on the boot hart. Runs before smp_prepare_cpus(),
 * which marks the possible CPUs present, so no other CPU is brought up or
 * offered for hotplug.
 */
static void __init esp32s31_boot_hart_only(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		if (cpu == smp_processor_id())
			continue;
		set_cpu_possible(cpu, false);
		pr_warn("esp32s31-smp: no cross-core IPI, CPU%u stays down\n", cpu);
	}
}

/**
 * esp32s31_local_timer_init - take CLIC 41 for the per-hart comparators
 *
 * Return: the per-CPU Linux interrupt of CLIC 41, for request_percpu_irq(),
 * or a negative error. -ENODEV: the cross-core IPI is not up and nothing
 * was set up; Linux then stays on the boot hart.
 */
int __init esp32s31_local_timer_init(void)
{
	if (!esp32s31_doorbells) {
		esp32s31_boot_hart_only();
		return -ENODEV;
	}

	return esp_clic_map_local(ESP32S31_CLIC_TIMER_SLOT);
}

/* From a CPU hotplug STARTING callback on @cpu, interrupts off. */
void esp32s31_local_timer_enable(unsigned int cpu)
{
	esp_intmtx_route_local(cpu, ESP32S31_TIMER_SOURCE(cpu),
			       ESP32S31_CLIC_TIMER_SLOT);
	esp_clic_configure_local(cpu, ESP32S31_CLIC_TIMER_SLOT, true);
}

void esp32s31_local_timer_disable(unsigned int cpu)
{
	esp_clic_configure_local(cpu, ESP32S31_CLIC_TIMER_SLOT, false);
	esp_intmtx_unroute_local(cpu, ESP32S31_TIMER_SOURCE(cpu));
}

/* From the INTMTX init, once the CLIC and INTMTX drivers are up. */
int __init esp32s31_smp_irq_init(void)
{
	int ret, virq;

	esp32s31_doorbells = ioremap(ESP32S31_DOORBELL_BASE,
				     ESP32S31_DOORBELL_SIZE);
	if (!esp32s31_doorbells) {
		ret = -ENOMEM;
		goto err_boot_hart_only;
	}

	ret = esp_clic_install_local(ESP32S31_CLIC_IPI_SLOT,
				     esp32s31_ipi_chained, NULL);
	if (ret)
		goto err_unmap;

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_IRQ_ESP32S31_IPI_STARTING,
					"irqchip/esp32s31-ipi:starting",
					esp32s31_ipi_starting_cpu,
					esp32s31_ipi_dying_cpu);
	if (ret)
		goto err_unmap;

	ret = esp32s31_ipi_starting_cpu(smp_processor_id());
	if (ret)
		goto err_remove_state;

	/*
	 * A second ipi_mux_create() fails, so nothing that can fail comes
	 * after it: on any failure sbi_ipi_init() still creates its own.
	 */
	virq = ipi_mux_create(BITS_PER_BYTE, esp32s31_ipi_send);
	if (virq <= 0) {
		ret = virq < 0 ? virq : -ENOMEM;
		goto err_stop_cpu;
	}

	/*
	 * Last, as it cannot be undone. sbi_ipi_init() returns early once the
	 * IPI range is set, and neither IPIs nor remote fences go through the
	 * SBI.
	 */
	riscv_ipi_set_virq_range(virq, BITS_PER_BYTE);

	pr_info("esp32s31-smp: cross-core IPI on CLIC %u, per-hart timer on CLIC %u\n",
		ESP32S31_CLIC_IPI_SLOT, ESP32S31_CLIC_TIMER_SLOT);
	return 0;

err_stop_cpu:
	esp32s31_ipi_dying_cpu(smp_processor_id());
err_remove_state:
	cpuhp_remove_state_nocalls(CPUHP_AP_IRQ_ESP32S31_IPI_STARTING);
err_unmap:
	iounmap(esp32s31_doorbells);
	esp32s31_doorbells = NULL;
err_boot_hart_only:
	esp32s31_boot_hart_only();
	return ret;
}

/*
 * Espressif's OpenSBI resets only hart 0 for a reboot or a power-off, and
 * hart 1 would keep running this kernel. Take it down through the SBI HSM
 * stop first, which holds it in reset. From kernel_restart(), kernel_halt()
 * and kernel_power_off(), in process context, before device_shutdown().
 */
static int esp32s31_smp_reboot(struct notifier_block *nb,
			       unsigned long action, void *data)
{
	smp_shutdown_nonboot_cpus(0);
	return NOTIFY_DONE;
}

static struct notifier_block esp32s31_smp_reboot_nb = {
	.notifier_call = esp32s31_smp_reboot,
};

/*
 * Keep hart 0 online: every device interrupt is routed to it and cannot
 * move (esp_intmtx_set_affinity()), the radio runs on it, and Espressif's
 * OpenSBI starts only hart 1, so a stopped hart 0 would not come back.
 * Writes to /sys/devices/system/cpu/cpu0/online then fail with EPERM.
 * The CPU devices are registered before the initcalls run.
 */
static int __init esp32s31_smp_late_init(void)
{
	struct device *dev;

	if (!esp32s31_doorbells)
		return 0;

	dev = get_cpu_device(0);
	if (dev)
		dev->offline_disabled = true;

	register_reboot_notifier(&esp32s31_smp_reboot_nb);
	return 0;
}
device_initcall(esp32s31_smp_late_init);
