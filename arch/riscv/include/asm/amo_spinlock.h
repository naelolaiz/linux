/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Test-and-set spinlock that reads and writes the lock word only with
 * 32-bit AMOs, for the ESP32-S31 (CONFIG_RISCV_AMO_SPINLOCKS).
 *
 * The generic ticket lock takes a ticket with a 32-bit AMO, releases with
 * a 16-bit store and waits with plain loads. On the two S31 harts both
 * were seen spinning on a ticket lock that was free, which was put down
 * to mixing AMOs with plain accesses to the same word. No documentation
 * confirms that; every access here goes through the AMO path to avoid
 * the mix.
 */
#ifndef __ASM_RISCV_AMO_SPINLOCK_H
#define __ASM_RISCV_AMO_SPINLOCK_H

#include <asm/processor.h>
#include <asm-generic/spinlock_types.h>

static __always_inline u32 amo_spin_xchg(arch_spinlock_t *lock, u32 new)
{
	u32 old;

	__asm__ __volatile__("amoswap.w.aq %0, %2, %1"
		: "=r" (old), "+A" (lock->val.counter)
		: "r" (new)
		: "memory");
	return old;
}

static __always_inline u32 amo_spin_read(arch_spinlock_t *lock)
{
	u32 val;

	__asm__ __volatile__("amoadd.w.aq %0, zero, %1"
		: "=r" (val), "+A" (lock->val.counter)
		:
		: "memory");
	return val;
}

static __always_inline void amo_spin_release(arch_spinlock_t *lock)
{
	u32 old;

	__asm__ __volatile__("amoswap.w.rl %0, zero, %1"
		: "=r" (old), "+A" (lock->val.counter)
		:
		: "memory");
}

/*
 * Locks in memory that was just allocated: write the initial 0 with an
 * AMO too, so the first acquire sees it on the path it will use.
 */
static __always_inline void arch_spin_lock_init(arch_spinlock_t *lock)
{
	amo_spin_release(lock);
}
#define arch_spin_lock_init arch_spin_lock_init

/*
 * Spin on the AMO itself. The wait does not process IPIs: the
 * CALL_FUNCTION queue carries scheduler wake-up callbacks, which take
 * runqueue locks and would come back here.
 */
static __always_inline void arch_spin_lock(arch_spinlock_t *lock)
{
	while (amo_spin_xchg(lock, 1))
		cpu_relax();
}

static __always_inline bool arch_spin_trylock(arch_spinlock_t *lock)
{
	return !amo_spin_xchg(lock, 1);
}

static __always_inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	amo_spin_release(lock);
}

static __always_inline int arch_spin_value_unlocked(arch_spinlock_t lock)
{
	return !lock.val.counter;
}

static __always_inline int arch_spin_is_locked(arch_spinlock_t *lock)
{
	return !!amo_spin_read(lock);
}

#endif /* __ASM_RISCV_AMO_SPINLOCK_H */
