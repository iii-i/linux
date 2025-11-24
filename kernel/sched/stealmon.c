#include <linux/debugfs.h> /* for debugfs_create_dir */
#include <linux/init.h> /* for late_initcall */
#include <linux/kernel_stat.h> /* for kcpustat_cpu */
#include <linux/minmax.h> /* for max() */
#include <linux/workqueue.h> /* for schedule_delayed_work */

#define HISTORY_BITS 4
#define HISTORY_SIZE (1 << (HISTORY_BITS))
#define HISTORY_MASK (HISTORY_SIZE - 1)

struct entry {
	ktime_t now;
	u64 steal_ns;
};
static struct entry history[HISTORY_SIZE];
static u64 history_pos;

static u64 max_seen_delta_ns;
static u64 max_seen_delta_steal_ns;
static u32 max_seen_n_cpus_to_avoid;

static void fill_entry(struct entry *e)
{
	int cpu;

	e->now = ktime_get();
	e->steal_ns = 0;
	for_each_cpu(cpu, cpu_online_mask) {
		e->steal_ns += kcpustat_cpu(cpu).cpustat[CPUTIME_STEAL];
	}
}

static void __sm_work_fn(void)
{
	u32 cpu, n_cpus_to_avoid, n_cpus_to_keep;
	u64 delta_ns, delta_steal_ns;
	struct entry *cur, *prev;

	cur = &history[(history_pos++) & HISTORY_MASK];
	fill_entry(cur);
	if (unlikely(history_pos == 1))
		return;
	prev = &history[(history_pos - 2) & HISTORY_MASK];

	delta_ns = ktime_to_ns(ktime_sub(cur->now, prev->now));
	delta_steal_ns = cur->steal_ns - prev->steal_ns;
	max_seen_delta_ns = max(max_seen_delta_ns, delta_ns);
	max_seen_delta_steal_ns = max(max_seen_delta_steal_ns, delta_steal_ns);

	n_cpus_to_keep = 1;
	n_cpus_to_avoid = delta_ns ? (delta_steal_ns / delta_ns) : 0;
	max_seen_n_cpus_to_avoid =
		max(max_seen_n_cpus_to_avoid, n_cpus_to_avoid);
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
	schedule_delayed_work(&sm_work, HZ);
}

static int __init sm_init(void)
{
	struct dentry *debugfs_sm;

	debugfs_sm = debugfs_create_dir("stealmon", NULL);
	debugfs_create_u64("history_pos", 0400, debugfs_sm, &history_pos);
	debugfs_create_u64("max_seen_delta_ns", 0400, debugfs_sm,
			   &max_seen_delta_ns);
	debugfs_create_u64("max_seen_delta_steal_ns", 0400, debugfs_sm,
			   &max_seen_delta_steal_ns);
	debugfs_create_u32("max_seen_n_cpus_to_avoid", 0400, debugfs_sm,
			   &max_seen_n_cpus_to_avoid);

	schedule_delayed_work(&sm_work, 0);

	return 0;
}
late_initcall(sm_init);
