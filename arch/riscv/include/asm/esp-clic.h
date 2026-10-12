/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The S-mode in-service level of the Espressif ESP32-S31 CLIC
 * (sintstatus.SIL, CONFIG_RISCV_ESP_CLIC_TRAP_QUIRK).
 *
 * Taking an S-mode interrupt raises SIL, and only an sret with an
 * interrupt-type scause lowers it again (entry.S). All S-mode interrupts
 * have one level, so until then the hart takes none of them: not its
 * timer, not an IPI. GrieferPig lowers it after every SBI call as well;
 * whether the mret that ends an SBI call leaves it raised is unverified.
 */
#ifndef __ASM_RISCV_ESP_CLIC_H
#define __ASM_RISCV_ESP_CLIC_H

#ifdef CONFIG_RISCV_ESP_CLIC_TRAP_QUIRK
/*
 * Lower SIL to the base level: an sret from S-mode to S-mode with an
 * interrupt-type scause, SPIL 0 and SPIE clear. Call it with interrupts
 * off (sstatus.SIE clear). It writes scause before sstatus, since the SPP
 * and SPIE bits of scause alias those of sstatus, and it overwrites sepc,
 * scause and sstatus and then restores them, sstatus last. From
 * GrieferPig's linux-esp32-s31 (asm/sbi-ecall.h).
 */
static __always_inline void esp_clic_drop_level(void)
{
	unsigned long status, epc, cause, tmp;

	__asm__ __volatile__(
		"csrr	%0, sstatus\n"
		"csrr	%1, sepc\n"
		"csrr	%2, scause\n"
		"li	%3, 0x80000000\n"
		"csrw	scause, %3\n"
		"ori	%3, %0, 0x100\n"	/* SPP: back to S-mode */
		"andi	%3, %3, -35\n"		/* SIE, SPIE clear */
		"csrw	sstatus, %3\n"
		"lla	%3, 1f\n"
		"csrw	sepc, %3\n"
		"sret\n"
		"1:	csrw	scause, %2\n"
		"csrw	sepc, %1\n"
		"csrw	sstatus, %0\n"
		: "=&r" (status), "=&r" (epc), "=&r" (cause), "=&r" (tmp)
		:
		: "memory");
}
#else
static __always_inline void esp_clic_drop_level(void) { }
#endif

#endif /* __ASM_RISCV_ESP_CLIC_H */
