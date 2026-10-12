/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_RISCV_ESP32S31_EXT_H
#define _ASM_RISCV_ESP32S31_EXT_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

struct task_struct;

bool esp32s31_current_uses_init_mm(void);
void esp32s31_kthread_use_init_mm(void);
void esp32s31_kthread_unuse_init_mm(void);
int esp32s31_sched_setscheduler(struct task_struct *task, int policy,
			       int priority);

#endif /* !__ASSEMBLY__ */

#endif
