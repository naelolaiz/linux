// SPDX-License-Identifier: GPL-2.0-only
/*
 * ESP32-S31 radio: the HP connectivity power domain.
 *
 * The payload runs ESP-IDF's pmu_init() once, right before it brings the
 * radio up, and calls s31_linux_pmu_reclaim_after_radio_init() after it.
 * pmu_init() clears every PMU force field, the HP connectivity (HPCNNT)
 * domain's included, and that domain also holds the EMAC and the USB host.
 * GrieferPig's port puts the domain back from its PMU power-domain
 * driver. This tree has no PMU driver, so the module forces the domain on
 * itself, in the same order as that driver, and reads the result back.
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/esp32s31-radio.h>
#include <linux/io.h>
#include <linux/spinlock.h>

#include "esp32s31-radio-internal.h"

/* PMU (0x20704000) power-domain control of HPCNNT. */
#define S31_PMU_PD_HPCNNT_CTRL	0x20704130U

#define PMU_FORCE_RESET		BIT(0)
#define PMU_FORCE_ISO		BIT(1)
#define PMU_FORCE_PU		BIT(2)
#define PMU_FORCE_NO_RESET	BIT(3)
#define PMU_FORCE_NO_ISO	BIT(4)
#define PMU_FORCE_PD		BIT(5)
#define PMU_FORCE_ON_SET	(PMU_FORCE_PU | PMU_FORCE_NO_ISO | \
				 PMU_FORCE_NO_RESET)
#define PMU_FORCE_ON_CLEAR	(PMU_FORCE_PD | PMU_FORCE_ISO | PMU_FORCE_RESET)

static void __iomem *s31_pmu_hpcnnt;
static DEFINE_RAW_SPINLOCK(s31_pmu_lock);

static void s31_pmu_update(void __iomem *reg, u32 clear, u32 set)
{
	u32 value = readl(reg);

	value &= ~clear;
	value |= set;
	writel(value, reg);
	readl(reg);
}

static void s31_pmu_hpcnnt_power_on(void)
{
	void __iomem *reg = READ_ONCE(s31_pmu_hpcnnt);
	unsigned long flags;
	u32 ctrl;

	if (!reg)
		return;

	raw_spin_lock_irqsave(&s31_pmu_lock, flags);
	/* Power first, then remove isolation, finally release reset. */
	s31_pmu_update(reg, PMU_FORCE_PD, PMU_FORCE_PU);
	udelay(2);
	s31_pmu_update(reg, PMU_FORCE_ISO, PMU_FORCE_NO_ISO);
	udelay(1);
	s31_pmu_update(reg, PMU_FORCE_RESET, PMU_FORCE_NO_RESET);
	ctrl = readl(reg);
	raw_spin_unlock_irqrestore(&s31_pmu_lock, flags);

	if ((ctrl & PMU_FORCE_ON_SET) != PMU_FORCE_ON_SET ||
	    (ctrl & PMU_FORCE_ON_CLEAR))
		pr_err("esp32s31-radio: HPCNNT power-on readback failed: 0x%08x\n",
		       ctrl);
}

/* Imported by the payload, called right after IDF's pmu_init(). */
void s31_linux_pmu_reclaim_after_radio_init(void)
{
	void __iomem *reg = READ_ONCE(s31_pmu_hpcnnt);

	if (reg)
		pr_info("esp32s31-radio: HPCNNT power control after pmu_init(): 0x%08x\n",
			readl(reg));
	s31_pmu_hpcnnt_power_on();
}

/*
 * The domain stays on when the radio stops: the EMAC and the USB host,
 * which holds the root file system, are in it.
 */
void s31_linux_pmu_radio_vote(bool active)
{
	if (active)
		s31_pmu_hpcnnt_power_on();
}

static void s31_radio_pmu_release(void *data)
{
	WRITE_ONCE(s31_pmu_hpcnnt, NULL);
}

int s31_radio_pmu_probe(struct device *dev)
{
	void __iomem *reg;

	reg = devm_ioremap(dev, S31_PMU_PD_HPCNNT_CTRL, sizeof(u32));
	if (!reg)
		return -ENOMEM;

	dev_info(dev, "HPCNNT power control before the radio: 0x%08x\n",
		 readl(reg));
	WRITE_ONCE(s31_pmu_hpcnnt, reg);
	return devm_add_action_or_reset(dev, s31_radio_pmu_release, NULL);
}
