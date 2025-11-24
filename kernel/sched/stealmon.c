/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/debugfs.h> /* for debugfs_create_dir */
#include <linux/init.h> /* for late_initcall */
#include <linux/kernel_stat.h> /* for kcpustat_cpu */

static ktime_t prev;
static u64 prev_steal_ns;

/* The lower the value, the sooner we consider unblocking a CPU */
static int grudge_factor = 50;

/* For many iterations we were considering unblocking a CPU */
static u32 thawing_streak;

/* The lower the value, the sooner we actually unblock a CPU */
static int grudge_duration = 3;

static void __sm_work_fn(void)
{
	u32 cpu, n_cpus_to_avoid, n_cpus_to_keep;
	u64 steal_ns, delta_ns, delta_steal_ns;
	ktime_t now;

	/* Get the actual steal time */
	now = ktime_get();
	steal_ns = 0;
	for_each_cpu(cpu, cpu_online_mask)
		steal_ns += kcpustat_cpu(cpu).cpustat[CPUTIME_STEAL];

	/*
	 * Smoothen steal time in order to avoid oscillation.
	 * We should still be able to notice 100% steal time within 1 second.
	 */
	steal_ns = (prev_steal_ns + steal_ns) / 2;

	/* Compute steal time % */
	delta_ns = ktime_to_ns(ktime_sub(now, prev));
	delta_steal_ns = steal_ns - prev_steal_ns;
	prev = now;
	prev_steal_ns = steal_ns;
	if (delta_ns == 0)
		return;
	n_cpus_to_avoid = delta_steal_ns / delta_ns;

	/* Account for CPUs that are already blocked */
	n_cpus_to_avoid += cpumask_weight(cpu_paravirt_mask);

	/* If steal time remains low, unblock 1 CPU */
	if (delta_ns > delta_steal_ns * grudge_factor)
		thawing_streak++;
	else
		thawing_streak = 0;
	if (n_cpus_to_avoid > 1 && thawing_streak >= grudge_duration) {
		n_cpus_to_avoid -= 1;
		thawing_streak = 0;
	}

	n_cpus_to_keep = 1;
	for_each_cpu(cpu, cpu_online_mask) {
		if (n_cpus_to_keep) {
			set_cpu_paravirt(cpu, false);
			n_cpus_to_keep -= 1;
		} else if (n_cpus_to_avoid) {
			set_cpu_paravirt(cpu, true);
			n_cpus_to_avoid--;
		} else {
			set_cpu_paravirt(cpu, false);
		}
	}
}

static void sm_work_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(sm_work, sm_work_fn);
static void sm_work_fn(struct work_struct *work)
{
	__sm_work_fn();
	schedule_delayed_work(&sm_work, HZ / 4);
}

static int __init sm_init(void)
{
	schedule_delayed_work(&sm_work, 0);

	return 0;
}
late_initcall(sm_init);
