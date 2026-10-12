/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Reader-writer lock whose lock word is only read and written with 32-bit
 * AMOs, for CONFIG_RISCV_AMO_SPINLOCKS (asm/amo_spinlock.h).
 *
 * The queued rwlock changes its count with AMOs, but releases a writer
 * with a byte store and waits with plain loads (atomic_cond_read_*()):
 * the mix that lets the ticket lock spin forever on a free lock on the
 * two ESP32-S31 harts. tasklist_lock is such a lock, and fork, exit and
 * wait take it for every process. The type (struct qrwlock) stays; only
 * the operations change.
 *
 * As in the queued rwlock, a reader in interrupt context only waits for a
 * writer that holds the lock, not for one that waits for the readers to
 * leave: the writer may be waiting for the reader that was interrupted.
 * Writers queue on wait_lock.
 */
#ifndef __ASM_RISCV_AMO_RWLOCK_H
#define __ASM_RISCV_AMO_RWLOCK_H

#include <linux/atomic.h>
#include <linux/preempt.h>

#define AMO_RW_LOCKED	0x1U	/* a writer holds the lock */
#define AMO_RW_WAITING	0x2U	/* a writer waits for the readers */
#define AMO_RW_WMASK	(AMO_RW_LOCKED | AMO_RW_WAITING)
#define AMO_RW_READER	0x4U	/* one reader */

#define AMO_RW_OP(name, insn)						\
static __always_inline u32 name(arch_rwlock_t *lock, u32 val)		\
{									\
	u32 old;							\
									\
	__asm__ __volatile__(insn " %0, %2, %1"				\
		: "=r" (old), "+A" (lock->cnts.counter)			\
		: "r" (val)						\
		: "memory");						\
	return old;							\
}

AMO_RW_OP(amo_rw_add, "amoadd.w")
AMO_RW_OP(amo_rw_add_acquire, "amoadd.w.aq")
AMO_RW_OP(amo_rw_add_release, "amoadd.w.rl")
AMO_RW_OP(amo_rw_or, "amoor.w")
AMO_RW_OP(amo_rw_or_acquire, "amoor.w.aq")
AMO_RW_OP(amo_rw_and, "amoand.w")
AMO_RW_OP(amo_rw_and_release, "amoand.w.rl")
AMO_RW_OP(amo_rw_swap_release, "amoswap.w.rl")

static __always_inline u32 amo_rw_read(arch_rwlock_t *lock)
{
	return amo_rw_add_acquire(lock, 0);
}

/* Locks in memory that was just allocated, as arch_spin_lock_init(). */
static __always_inline void arch_rwlock_init(arch_rwlock_t *lock)
{
	amo_rw_swap_release(lock, 0);
	arch_spin_lock_init(&lock->wait_lock);
}
#define arch_rwlock_init arch_rwlock_init

static __always_inline void arch_read_lock(arch_rwlock_t *lock)
{
	u32 wmask = in_interrupt() ? AMO_RW_LOCKED : AMO_RW_WMASK;

	while (amo_rw_add_acquire(lock, AMO_RW_READER) & wmask) {
		amo_rw_add(lock, -AMO_RW_READER);
		while (amo_rw_read(lock) & wmask)
			cpu_relax();
	}
}

static __always_inline int arch_read_trylock(arch_rwlock_t *lock)
{
	if (amo_rw_read(lock) & AMO_RW_WMASK)
		return 0;
	if (!(amo_rw_add_acquire(lock, AMO_RW_READER) & AMO_RW_WMASK))
		return 1;
	amo_rw_add(lock, -AMO_RW_READER);
	return 0;
}

static __always_inline void arch_read_unlock(arch_rwlock_t *lock)
{
	amo_rw_add_release(lock, -AMO_RW_READER);
}

/*
 * Only the writer holding wait_lock sets AMO_RW_LOCKED, so the word reads
 * AMO_RW_WAITING once the previous writer and all readers have left. A
 * reader in interrupt context can still come in before AMO_RW_LOCKED is
 * set: then AMO_RW_LOCKED is taken back and the writer waits again.
 */
static __always_inline void arch_write_lock(arch_rwlock_t *lock)
{
	arch_spin_lock(&lock->wait_lock);
	amo_rw_or(lock, AMO_RW_WAITING);
	for (;;) {
		while (amo_rw_read(lock) != AMO_RW_WAITING)
			cpu_relax();
		if (amo_rw_or_acquire(lock, AMO_RW_LOCKED) == AMO_RW_WAITING)
			break;
		amo_rw_and(lock, ~AMO_RW_LOCKED);
	}
	amo_rw_and(lock, ~AMO_RW_WAITING);
	arch_spin_unlock(&lock->wait_lock);
}

static __always_inline int arch_write_trylock(arch_rwlock_t *lock)
{
	u32 old;

	if (amo_rw_read(lock) || !arch_spin_trylock(&lock->wait_lock))
		return 0;
	old = amo_rw_or_acquire(lock, AMO_RW_LOCKED);
	/* Readers came in: take the bit back. Another writer: it is theirs. */
	if (old && !(old & AMO_RW_LOCKED))
		amo_rw_and(lock, ~AMO_RW_LOCKED);
	arch_spin_unlock(&lock->wait_lock);
	return !old;
}

static __always_inline void arch_write_unlock(arch_rwlock_t *lock)
{
	amo_rw_and_release(lock, ~AMO_RW_LOCKED);
}

static __always_inline int arch_rwlock_is_contended(arch_rwlock_t *lock)
{
	return arch_spin_is_locked(&lock->wait_lock);
}
#define arch_rwlock_is_contended arch_rwlock_is_contended

#endif /* __ASM_RISCV_AMO_RWLOCK_H */
