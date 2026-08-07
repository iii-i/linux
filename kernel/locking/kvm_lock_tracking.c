// SPDX-License-Identifier: GPL-2.0
/*
 * KVM guest lock-holder tracking. Each CPU hands the host its counter's address
 * (kvm_lock_tracking_register_this_cpu); see <linux/kvm_lock_tracking.h>.
 */

#include <linux/cache.h>
#include <linux/cpuhotplug.h>
#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/kvm_para.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <asm/kvm_para.h>
/* After <linux/percpu.h>: this header uses DECLARE_PER_CPU/this_cpu ops. */
#include <linux/kvm_lock_tracking.h>

#define KVM_LOCK_COUNTER_MAGIC		0x4b434f4cU	/* 'LOCK' */
#define KVM_LOCK_COUNTER_VERSION	1U

/* Isolated cache line (this CPU writes, host reads); exported for the inline hooks. */
DEFINE_PER_CPU_ALIGNED(struct kvm_lock_counter, kvm_lock_counter);
EXPORT_PER_CPU_SYMBOL(kvm_lock_counter);

static void kvm_lock_tracking_register_this_cpu(void)
{
	struct kvm_lock_counter *c = this_cpu_ptr(&kvm_lock_counter);
	phys_addr_t gpa = per_cpu_ptr_to_phys(c);

	c->magic = KVM_LOCK_COUNTER_MAGIC;
	c->version = KVM_LOCK_COUNTER_VERSION;
	c->cpu_id = smp_processor_id();
	/* c->count is live and maintained by the fast path; never reset it. */

	/* Publish the header fields before telling the host where to look. */
	smp_wmb();

	/*
	 * Publish the address via the KVM hypercall (VMCALL on x86, DIAG 0x500 on
	 * s390). An unmodified host returns -KVM_ENOSYS / -EOPNOTSUPP and the guest
	 * proceeds without tracking.
	 */
#if defined(CONFIG_X86) || defined(CONFIG_S390)
	{
		long ret = kvm_hypercall1(KVM_HC_LOCK_TRACKING_REGISTER, gpa);

		if (ret && ret != -KVM_ENOSYS)
			pr_warn_once("kvm_lock_tracking: register hypercall returned %ld\n",
				     ret);
	}
#else
	(void)gpa;
#endif
}

static void kvm_lock_tracking_register_smp(void *unused)
{
	kvm_lock_tracking_register_this_cpu();
}

static int kvm_lock_tracking_cpu_online(unsigned int cpu)
{
	kvm_lock_tracking_register_this_cpu();
	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int kvm_lock_tracking_counts_show(struct seq_file *m, void *v)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu)
		seq_printf(m, "cpu %u: %ld\n", cpu,
			   per_cpu(kvm_lock_counter, cpu).count);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(kvm_lock_tracking_counts);

static void __init kvm_lock_tracking_debugfs_init(void)
{
	debugfs_create_file("kvm_lock_tracking_counts", 0444, NULL, NULL,
			    &kvm_lock_tracking_counts_fops);
}
#else
static inline void kvm_lock_tracking_debugfs_init(void) { }
#endif

static int __init kvm_lock_tracking_init(void)
{
	int ret;

	if (!kvm_para_available())
		return 0;

	/* Register every online CPU now; hotplugged CPUs register on the way up. */
	on_each_cpu(kvm_lock_tracking_register_smp, NULL, 1);

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					"locking/kvm_lock_tracking:online",
					kvm_lock_tracking_cpu_online, NULL);
	if (ret < 0)
		pr_warn("kvm_lock_tracking: cpuhp_setup_state failed: %d\n", ret);

	kvm_lock_tracking_debugfs_init();
	pr_info("kvm_lock_tracking: registered %u CPUs with host\n",
		num_online_cpus());
	return 0;
}
device_initcall(kvm_lock_tracking_init);
