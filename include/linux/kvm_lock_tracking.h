/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KVM_LOCK_TRACKING_H
#define _LINUX_KVM_LOCK_TRACKING_H

#ifdef CONFIG_KVM_GUEST_LOCK_TRACKING

#include <linux/cache.h>
#include <linux/percpu-defs.h>
#include <linux/types.h>

/*
 * Per-CPU count of raw spinlocks each guest CPU holds, kept in guest RAM (a
 * "guest-nominated page") so the host can sample it and spot a vCPU descheduled
 * while holding a lock. Guest RAM rather than a device BAR keeps it working on
 * MMIO-less architectures (s390).
 *
 * inc/dec are inlined onto the raw-spinlock fast path from the arch spinlock
 * headers, not generic <linux/spinlock.h>, which cannot reach this_cpu_*()
 * without an include cycle. Counting is unconditional, so inc/dec stay balanced
 * and the count is >= 0 by construction.
 */

struct kvm_lock_counter {
	u32 magic;
	u32 version;
	u32 cpu_id;
	u32 flags;
	long count;
};

DECLARE_PER_CPU_ALIGNED(struct kvm_lock_counter, kvm_lock_counter);

static __always_inline void kvm_lock_tracking_inc(void)
{
	this_cpu_inc(kvm_lock_counter.count);
}

static __always_inline void kvm_lock_tracking_dec(void)
{
	this_cpu_dec(kvm_lock_counter.count);
}

#else /* !CONFIG_KVM_GUEST_LOCK_TRACKING */

static __always_inline void kvm_lock_tracking_inc(void) { }
static __always_inline void kvm_lock_tracking_dec(void) { }

#endif /* CONFIG_KVM_GUEST_LOCK_TRACKING */

#endif /* _LINUX_KVM_LOCK_TRACKING_H */
