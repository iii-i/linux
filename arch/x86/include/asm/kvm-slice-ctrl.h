/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Guest support for the kvm-slice-ctrl PCI device.
 *
 * The host QEMU exposes per-vCPU pages backed by the host vCPU thread's
 * struct rseq. Each guest CPU's per-CPU pointer below addresses that
 * thread's rseq.slice_ctrl byte (request at +0, granted at +1).
 *
 * On spinlock acquire we set request=1 to ask the host scheduler for a
 * time-slice extension (so the host doesn't preempt us mid-critical
 * section). On the outermost release we clear request and, if granted
 * was set, do a KVM yield to give the slice back voluntarily.
 *
 * The hooks compile to nothing when the device is absent: the static key
 * stays disabled, gating the per-CPU touches.
 */

#ifndef _ASM_X86_KVM_SLICE_CTRL_H
#define _ASM_X86_KVM_SLICE_CTRL_H

#include <linux/compiler.h>
#include <linux/jump_label.h>
#include <linux/percpu-defs.h>
#include <linux/types.h>

#ifdef CONFIG_KVM_SLICE_CTRL_GUEST

DECLARE_STATIC_KEY_FALSE(kvm_slice_ctrl_key);
DECLARE_PER_CPU(u8 *, kvm_slice_ctrl_ptr);
/*
 * Signed so that we can detect dec underflow caused by the static_branch
 * enabling while a CPU is mid-critical-section: the unmatched releases
 * drive depth negative, we reset, and tracking resumes correctly on the
 * next outer acquire.
 */
DECLARE_PER_CPU(int, kvm_slice_ctrl_depth);

void kvm_slice_ctrl_yield(void);

static __always_inline void kvm_slice_ctrl_request(void)
{
	u8 *ctrl;

	if (!static_branch_unlikely(&kvm_slice_ctrl_key))
		return;
	if (this_cpu_inc_return(kvm_slice_ctrl_depth) != 1)
		return;
	ctrl = this_cpu_read(kvm_slice_ctrl_ptr);
	if (ctrl)
		WRITE_ONCE(ctrl[0], 1);
}

static __always_inline void kvm_slice_ctrl_release(void)
{
	u8 *ctrl;
	int cur;

	if (!static_branch_unlikely(&kvm_slice_ctrl_key))
		return;
	cur = this_cpu_dec_return(kvm_slice_ctrl_depth);
	if (unlikely(cur < 0)) {
		/* Resync after a static_branch transition during a held
		 * critical section. */
		this_cpu_write(kvm_slice_ctrl_depth, 0);
		return;
	}
	if (cur != 0)
		return;
	ctrl = this_cpu_read(kvm_slice_ctrl_ptr);
	if (!ctrl)
		return;
	WRITE_ONCE(ctrl[0], 0);
	/* Order our request=0 store before the granted load. */
	smp_mb();
	if (READ_ONCE(ctrl[1]))
		kvm_slice_ctrl_yield();
}

#else /* !CONFIG_KVM_SLICE_CTRL_GUEST */

static __always_inline void kvm_slice_ctrl_request(void) { }
static __always_inline void kvm_slice_ctrl_release(void) { }

#endif

#endif /* _ASM_X86_KVM_SLICE_CTRL_H */
