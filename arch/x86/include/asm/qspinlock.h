/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_QSPINLOCK_H
#define _ASM_X86_QSPINLOCK_H

#include <linux/jump_label.h>
#include <asm/cpufeature.h>
#include <asm-generic/qspinlock_types.h>
#include <asm/paravirt.h>
#include <asm/rmwcc.h>
#ifdef CONFIG_PARAVIRT
#include <asm/paravirt-spinlock.h>
#endif

#define _Q_PENDING_LOOPS	(1 << 9)

#define queued_fetch_set_pending_acquire queued_fetch_set_pending_acquire
static __always_inline u32 queued_fetch_set_pending_acquire(struct qspinlock *lock)
{
	u32 val;

	/*
	 * We can't use GEN_BINARY_RMWcc() inside an if() stmt because asm goto
	 * and CONFIG_PROFILE_ALL_BRANCHES=y results in a label inside a
	 * statement expression, which GCC doesn't like.
	 */
	val = GEN_BINARY_RMWcc(LOCK_PREFIX "btsl", lock->val.counter, c,
			       "I", _Q_PENDING_OFFSET) * _Q_PENDING_VAL;
	val |= atomic_read(&lock->val) & ~_Q_PENDING_MASK;

	return val;
}

#ifndef CONFIG_PARAVIRT
static inline void native_pv_lock_init(void) { }
#endif

#include <linux/kvm_lock_tracking.h>

/*
 * Override the generic lock/trylock fast paths to bump the counter inline once
 * the lock is held (matching dec in <asm/paravirt-spinlock.h>). Without
 * PARAVIRT_SPINLOCKS the slowpath prototype comes from asm-generic below, so
 * forward-declare it.
 */
#ifndef CONFIG_PARAVIRT_SPINLOCKS
extern void queued_spin_lock_slowpath(struct qspinlock *lock, u32 val);
#endif

#define queued_spin_lock queued_spin_lock
static __always_inline void queued_spin_lock(struct qspinlock *lock)
{
	int val = 0;

	if (likely(atomic_try_cmpxchg_acquire(&lock->val, &val, _Q_LOCKED_VAL))) {
		kvm_lock_tracking_inc();
		return;
	}
	queued_spin_lock_slowpath(lock, val);
	kvm_lock_tracking_inc();
}

#define queued_spin_trylock queued_spin_trylock
static __always_inline int queued_spin_trylock(struct qspinlock *lock)
{
	int val = atomic_read(&lock->val);

	if (unlikely(val))
		return 0;
	if (likely(atomic_try_cmpxchg_acquire(&lock->val, &val, _Q_LOCKED_VAL))) {
		kvm_lock_tracking_inc();
		return 1;
	}
	return 0;
}

#include <asm-generic/qspinlock.h>

#endif /* _ASM_X86_QSPINLOCK_H */
