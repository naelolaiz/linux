// SPDX-License-Identifier: GPL-2.0
/*
 * ESP32-S31 systimer clocksource + clockevent.
 *
 * The systimer is an always-on, XTAL-fed counter (40 MHz / 2.5 = 16 MHz),
 * independent of CPU_CLK and free-running through WFI -- unlike the chip's
 * CPU-clocked CLINT MTIME that backs the SBI timer. OpenSBI enables the
 * peripheral clock (s31_systimer_clk_init); Linux owns the counter (UNIT0)
 * and a one-shot alarm (COMP0/TARGET0) here, so timekeeping and ticks need no
 * SBI ecall and no rdtime trap. The alarm IRQ arrives as a normal peripheral
 * interrupt via INTMTX -> CLIC (ETS source 33).
 *
 * 52-bit counter (20-bit HI + 32-bit LO); a snapshot is latched by pulsing
 * UNIT0_OP.UPDATE and waiting for VALID before reading HI/LO.
 *
 * With CONFIG_ESP32S31_SMP each hart n gets its own clockevent on
 * comparator n (COMPn/TARGETn, ETS source 33 + n), routed to CLIC 41 of
 * that hart by irq-esp32s31-smp.c instead of the DT interrupt.
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/cpuhotplug.h>
#include <linux/interrupt.h>
#include <linux/irqchip/irq-esp32s31.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>
#include <linux/spinlock.h>

#define ST_CONF			0x00	/* b30 UNIT0 work_en, b27/28 stall, b24 - n TARGETn work_en */
#define  ST_CONF_UNIT0_WORK_EN	BIT(30)
#define  ST_CONF_UNIT0_STALL	(BIT(28) | BIT(27))	/* stop UNIT0 while core 0 / core 1 stalls */
#define  ST_CONF_TARGET_WORK_EN(n) BIT(24 - (n))
#define ST_UNIT0_OP		0x04	/* b30 UPDATE, b29 VALID */
#define  ST_UNIT0_UPDATE	BIT(30)
#define  ST_UNIT0_VALID		BIT(29)
#define ST_TARGET_HI(n)		(0x1c + 8 * (n))	/* bits [19:0] */
#define ST_TARGET_LO(n)		(0x20 + 8 * (n))
#define ST_TARGET_CONF(n)	(0x34 + 4 * (n))	/* b31 unit_sel(0=UNIT0), b30 period_mode(0=oneshot) */
#define  ST_TARGET_UNIT_SEL	BIT(31)
#define  ST_TARGET_PERIOD_MODE	BIT(30)
#define ST_UNIT0_VALUE_HI	0x40	/* bits [19:0] */
#define ST_UNIT0_VALUE_LO	0x44
#define ST_COMP_LOAD(n)		(0x50 + 4 * (n))	/* b0 load */
#define ST_INT_ENA		0x64	/* bn TARGETn */
#define ST_INT_RAW		0x68
#define ST_INT_CLR		0x6c

#define ST_HI_MASK		0xfffffU
#define ST_COUNTER_BITS		52

static struct esp_systimer {
	void __iomem		*base;
	u32			rate;
	struct clock_event_device ce;
} systimer;

/*
 * The UNIT0 snapshot latch and the ST_CONF / ST_INT_ENA bits of all
 * comparators are shared by both harts. Taken with interrupts off, and
 * from sched_clock, so it is an arch spinlock (no lockdep, no tracing).
 */
static arch_spinlock_t systimer_lock = __ARCH_SPIN_LOCK_UNLOCKED;

static u64 notrace systimer_count(void)
{
	void __iomem *b = systimer.base;
	unsigned long flags;
	int guard = 100000;
	u32 hi, lo;

	raw_local_irq_save(flags);
	arch_spin_lock(&systimer_lock);
	writel(readl(b + ST_UNIT0_OP) | ST_UNIT0_UPDATE, b + ST_UNIT0_OP);
	while (!(readl(b + ST_UNIT0_OP) & ST_UNIT0_VALID) && --guard)
		cpu_relax();
	hi = readl(b + ST_UNIT0_VALUE_HI) & ST_HI_MASK;
	lo = readl(b + ST_UNIT0_VALUE_LO);
	arch_spin_unlock(&systimer_lock);
	raw_local_irq_restore(flags);

	return ((u64)hi << 32) | lo;
}

/* Read-modify-write of a shared register: clear @clr, then set @set. */
static void systimer_rmw(unsigned int off, u32 clr, u32 set)
{
	void __iomem *reg = systimer.base + off;
	unsigned long flags;

	raw_local_irq_save(flags);
	arch_spin_lock(&systimer_lock);
	writel((readl(reg) & ~clr) | set, reg);
	arch_spin_unlock(&systimer_lock);
	raw_local_irq_restore(flags);
}

static u64 systimer_cs_read(struct clocksource *cs)
{
	return systimer_count();
}

static u64 notrace systimer_sched_read(void)
{
	return systimer_count();
}

static struct clocksource systimer_cs = {
	.name	= "esp32s31-systimer",
	.rating	= 450,	/* beat riscv_clocksource (400) so reads skip the rdtime trap */
	.read	= systimer_cs_read,
	.mask	= CLOCKSOURCE_MASK(ST_COUNTER_BITS),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

/*
 * Comparator n serves the clockevent of CPU n; on a UP kernel only
 * comparator 0 is used. Each comparator's own TARGET, CONF and LOAD
 * registers are written by its CPU only.
 */
static int systimer_arm(unsigned int n, unsigned long delta)
{
	void __iomem *b = systimer.base;
	u64 tgt = systimer_count() + delta;

	systimer_rmw(ST_CONF, ST_CONF_TARGET_WORK_EN(n), 0);
	writel((u32)(tgt >> 32) & ST_HI_MASK, b + ST_TARGET_HI(n));
	writel((u32)tgt, b + ST_TARGET_LO(n));
	writel(readl(b + ST_TARGET_CONF(n)) & ~(ST_TARGET_UNIT_SEL | ST_TARGET_PERIOD_MODE),
	       b + ST_TARGET_CONF(n));
	writel(BIT(0), b + ST_COMP_LOAD(n));
	systimer_rmw(ST_CONF, 0, ST_CONF_TARGET_WORK_EN(n));
	return 0;
}

static void systimer_disarm(unsigned int n)
{
	systimer_rmw(ST_CONF, ST_CONF_TARGET_WORK_EN(n), 0);
	systimer_rmw(ST_INT_ENA, BIT(n), 0);
	writel(BIT(n), systimer.base + ST_INT_CLR);
}

/* One-shot: disarm before clearing so the level IRQ can't re-assert. */
static void systimer_expired(unsigned int n, struct clock_event_device *ce)
{
	systimer_rmw(ST_CONF, ST_CONF_TARGET_WORK_EN(n), 0);
	writel(BIT(n), systimer.base + ST_INT_CLR);
	ce->event_handler(ce);
}

static int systimer_set_next_event(unsigned long delta, struct clock_event_device *ce)
{
	return systimer_arm(0, delta);
}

static int systimer_shutdown(struct clock_event_device *ce)
{
	systimer_disarm(0);
	return 0;
}

static int systimer_set_oneshot(struct clock_event_device *ce)
{
	systimer_rmw(ST_INT_ENA, 0, BIT(0));
	return 0;
}

static irqreturn_t systimer_isr(int irq, void *dev_id)
{
	systimer_expired(0, dev_id);
	return IRQ_HANDLED;
}

#ifdef CONFIG_ESP32S31_SMP
static DEFINE_PER_CPU(struct clock_event_device, systimer_pcpu_ce);
static int systimer_pcpu_irq_nr;

/* Hart n's clockevent runs on hart n only: n is smp_processor_id(). */
static int systimer_pcpu_set_next_event(unsigned long delta,
					struct clock_event_device *ce)
{
	return systimer_arm(smp_processor_id(), delta);
}

static int systimer_pcpu_shutdown(struct clock_event_device *ce)
{
	systimer_disarm(smp_processor_id());
	return 0;
}

static int systimer_pcpu_set_oneshot(struct clock_event_device *ce)
{
	systimer_rmw(ST_INT_ENA, 0, BIT(smp_processor_id()));
	return 0;
}

/* CLIC 41 of this hart, a per-CPU interrupt: @dev_id is this CPU's ce. */
static irqreturn_t systimer_pcpu_irq(int irq, void *dev_id)
{
	unsigned int cpu = smp_processor_id();

	if (!(readl(systimer.base + ST_INT_RAW) & BIT(cpu)))
		return IRQ_NONE;

	systimer_expired(cpu, dev_id);
	return IRQ_HANDLED;
}

static int systimer_starting_cpu(unsigned int cpu)
{
	struct clock_event_device *ce = per_cpu_ptr(&systimer_pcpu_ce, cpu);

	if (cpu > 1)
		return -EINVAL;

	ce->name		= "esp32s31-systimer";
	ce->features		= CLOCK_EVT_FEAT_ONESHOT;
	ce->rating		= 500;	/* see systimer_init() */
	ce->set_next_event	= systimer_pcpu_set_next_event;
	ce->set_state_shutdown	= systimer_pcpu_shutdown;
	ce->set_state_oneshot	= systimer_pcpu_set_oneshot;
	ce->cpumask		= cpumask_of(cpu);
	ce->irq			= systimer_pcpu_irq_nr;

	systimer_disarm(cpu);
	enable_percpu_irq(systimer_pcpu_irq_nr, IRQ_TYPE_NONE);
	esp32s31_local_timer_enable(cpu);
	clockevents_config_and_register(ce, systimer.rate, 0x10, 0x7fffffff);
	return 0;
}

static int systimer_dying_cpu(unsigned int cpu)
{
	systimer_disarm(cpu);
	esp32s31_local_timer_disable(cpu);
	disable_percpu_irq(systimer_pcpu_irq_nr);
	return 0;
}

static int __init systimer_pcpu_init(void)
{
	int irq, ret;

	irq = esp32s31_local_timer_init();
	if (irq < 0)
		return irq;

	/* Per-CPU, so /proc/interrupts counts each hart's timer interrupts. */
	ret = request_percpu_irq(irq, systimer_pcpu_irq, "esp32s31-systimer",
				 &systimer_pcpu_ce);
	if (ret)
		return ret;
	systimer_pcpu_irq_nr = irq;

	ret = cpuhp_setup_state(CPUHP_AP_ESP32S31_SYSTIMER_STARTING,
				"clockevents/esp32s31/systimer:starting",
				systimer_starting_cpu, systimer_dying_cpu);
	if (ret)
		return ret;

	pr_info("esp32s31-systimer: %u Hz clocksource, per-hart clockevents on comparators 0-1\n",
		systimer.rate);
	return 0;
}
#else
static inline int systimer_pcpu_init(void) { return -ENODEV; }
#endif

static int __init systimer_init(struct device_node *np)
{
	u32 rate = 16000000;
	int irq, ret;

	systimer.base = of_iomap(np, 0);
	if (!systimer.base)
		return -ENXIO;

	of_property_read_u32(np, "clock-frequency", &rate);
	systimer.rate = rate;

	/*
	 * UNIT0 free-running (OpenSBI already clocked it), and counting on
	 * while either core is stalled: both harts take their time from it.
	 */
	systimer_rmw(ST_CONF, ST_CONF_UNIT0_STALL, ST_CONF_UNIT0_WORK_EN);

	/*
	 * Register the counter-based clocksource + sched_clock FIRST: they need no
	 * IRQ, and timer-riscv.c gates off the riscv sched_clock when this driver
	 * is built -- so a later clockevent/IRQ failure must not leave the kernel
	 * with no clocksource/sched_clock. Once these are up we never tear them
	 * back down (sched_clock would dereference a freed mapping).
	 */
	ret = clocksource_register_hz(&systimer_cs, rate);
	if (ret) {
		iounmap(systimer.base);
		return ret;
	}
	sched_clock_register(systimer_sched_read, ST_COUNTER_BITS, rate);

	/*
	 * Two harts: per-hart comparators on CLIC 41. The DT interrupt
	 * (source 33 to a CLIC slot picked by INTMTX) is not mapped then, its
	 * route would share core 0's source 33 register with the local one.
	 * -ENODEV: the cross-core IPI is not up, nothing was set up, and
	 * esp32s31_local_timer_init() has kept the other hart down: hart 0
	 * then uses comparator 0 on the DT interrupt, as without ESP32S31_SMP.
	 */
	if (IS_ENABLED(CONFIG_ESP32S31_SMP)) {
		ret = systimer_pcpu_init();
		if (ret != -ENODEV) {
			if (ret)
				pr_err("esp32s31-systimer: no per-hart clockevents (%d)\n", ret);
			return ret;
		}
		pr_warn("esp32s31-systimer: no per-hart clockevents, hart 0 only\n");
	}

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		pr_err("esp32s31-systimer: no IRQ; clockevent unavailable\n");
		return -EINVAL;
	}

	systimer.ce.name		= "esp32s31-systimer";
	systimer.ce.features		= CLOCK_EVT_FEAT_ONESHOT;
	/* Above riscv_timer's max rating (450 with Sstc): the systimer clockevent
	 * MUST win, or the kernel falls back to the CLINT clockevent whose MTIME
	 * freezes in WFI (FORCE_ON is intentionally not set) -> idle hang.
	 */
	systimer.ce.rating		= 500;
	systimer.ce.set_next_event	= systimer_set_next_event;
	systimer.ce.set_state_shutdown	= systimer_shutdown;
	systimer.ce.set_state_oneshot	= systimer_set_oneshot;
	/* For hart 0 alone on ESP32S31_SMP: CPU-local, so it beats riscv_timer. */
	systimer.ce.cpumask		= IS_ENABLED(CONFIG_ESP32S31_SMP) ?
					  cpumask_of(0) : cpu_possible_mask;
	systimer.ce.irq			= irq;

	ret = request_irq(irq, systimer_isr, IRQF_TIMER | IRQF_IRQPOLL,
			  "esp32s31-systimer", &systimer.ce);
	if (ret) {
		pr_err("esp32s31-systimer: request_irq %d failed (%d)\n", irq, ret);
		return ret;
	}

	/* min_delta well above the snapshot read+program latency so a one-shot
	 * deadline can't be programmed already in the past.
	 */
	clockevents_config_and_register(&systimer.ce, rate, 0x10, 0x7fffffff);
	pr_info("esp32s31-systimer: %u Hz clocksource+clockevent, irq %d\n", rate, irq);
	return 0;
}

TIMER_OF_DECLARE(esp32s31_systimer, "esp,esp32s31-systimer", systimer_init);
