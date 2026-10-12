// SPDX-License-Identifier: GPL-2.0-only
/*
 * Built-in helpers for the ESP32-S31 radio module: thin wrappers around
 * kernel internals that are not exported to modules.
 */

#include <linux/export.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/sched.h>

#include <asm/esp32s31_ext.h>

bool esp32s31_current_uses_init_mm(void)
{
	return current->active_mm == &init_mm;
}
EXPORT_SYMBOL_GPL(esp32s31_current_uses_init_mm);

void esp32s31_kthread_use_init_mm(void)
{
	kthread_use_mm(&init_mm);
}
EXPORT_SYMBOL_GPL(esp32s31_kthread_use_init_mm);

void esp32s31_kthread_unuse_init_mm(void)
{
	kthread_unuse_mm(&init_mm);
}
EXPORT_SYMBOL_GPL(esp32s31_kthread_unuse_init_mm);

int esp32s31_sched_setscheduler(struct task_struct *task, int policy,
			       int priority)
{
	struct sched_param param = { .sched_priority = priority };

	return sched_setscheduler_nocheck(task, policy, &param);
}
EXPORT_SYMBOL_GPL(esp32s31_sched_setscheduler);
