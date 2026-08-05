// SPDX-License-Identifier: GPL-2.0
/*
 * KVM guest lock-holder tracking. Each CPU hands the host its counter's address
 * (kvm_lock_tracking_register_this_cpu); see <linux/kvm_lock_tracking.h>.
 */

#include <linux/cache.h>
#include <linux/export.h>
#include <linux/percpu.h>
/* After <linux/percpu.h>: this header uses DECLARE_PER_CPU/this_cpu ops. */
#include <linux/kvm_lock_tracking.h>

/* Isolated cache line (this CPU writes, host reads); exported for the inline hooks. */
DEFINE_PER_CPU_ALIGNED(struct kvm_lock_counter, kvm_lock_counter);
EXPORT_PER_CPU_SYMBOL(kvm_lock_counter);
