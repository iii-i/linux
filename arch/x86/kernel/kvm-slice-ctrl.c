// SPDX-License-Identifier: GPL-2.0
/*
 * Guest driver for the QEMU kvm-slice-ctrl PCI device.
 *
 * BAR0 layout (matches qemu/hw/misc/kvm-slice-ctrl.c):
 *
 *   page 0 (header)
 *     u32 magic            = 'S','L','C','S'
 *     u32 version
 *     u32 nr_cpus
 *     u32 page_size
 *     u32 slice_ctrl_offset[nr_cpus]
 *
 *   pages 1..nr_cpus       host pages containing each vCPU thread's struct rseq
 */

#define pr_fmt(fmt) "kvm-slice-ctrl: " fmt

#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/kvm_para.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/types.h>

#include <asm/apic.h>
#include <asm/kvm-slice-ctrl.h>
#include <asm/kvm_para.h>
#include <asm/smp.h>

#define KSC_MAGIC   0x53434C53u   /* 'S','L','C','S' little-endian */
#define KSC_VERSION 1u
#define KSC_OFFSET_INVALID (~0u)

#define KSC_PCI_VENDOR 0x1234   /* PCI_VENDOR_ID_QEMU */
#define KSC_PCI_DEVICE 0x11ec

struct ksc_header {
	u32 magic;
	u32 version;
	u32 nr_cpus;
	u32 page_size;
	u32 slice_ctrl_offset[];
};

DEFINE_STATIC_KEY_FALSE(kvm_slice_ctrl_key);
EXPORT_SYMBOL_GPL(kvm_slice_ctrl_key);

DEFINE_PER_CPU(u8 *, kvm_slice_ctrl_ptr);
EXPORT_SYMBOL_GPL(kvm_slice_ctrl_ptr);

DEFINE_PER_CPU(int, kvm_slice_ctrl_depth);
EXPORT_SYMBOL_GPL(kvm_slice_ctrl_depth);

static void __iomem *ksc_bar;

void kvm_slice_ctrl_yield(void)
{
	/*
	 * The host has granted a slice extension that we no longer need.
	 * Hand it back via a directed-yield hypercall. The target apicid
	 * doesn't matter much: the act of trapping out lets the host
	 * scheduler decide whether to keep us on-CPU or run someone else.
	 */
	kvm_hypercall1(KVM_HC_SCHED_YIELD, per_cpu(x86_cpu_to_apicid,
						   smp_processor_id()));
}
EXPORT_SYMBOL_GPL(kvm_slice_ctrl_yield);

static int ksc_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ksc_header __iomem *hdr;
	resource_size_t bar_start, bar_len;
	u32 nr_cpus, page_size, magic, version;
	void __iomem *bar;
	int rc, cpu;

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	bar_start = pci_resource_start(pdev, 0);
	bar_len = pci_resource_len(pdev, 0);
	if (!bar_start || bar_len < PAGE_SIZE) {
		pci_err(pdev, "BAR0 is missing or too small\n");
		rc = -ENODEV;
		goto err_disable;
	}

	rc = pci_request_region(pdev, 0, "kvm-slice-ctrl");
	if (rc)
		goto err_disable;

	/*
	 * The BAR is RAM-backed on the host (memslot), so map it cached.
	 * We need ordinary load/store ordering, not MMIO posting.
	 */
	bar = ioremap_cache(bar_start, bar_len);
	if (!bar) {
		rc = -ENOMEM;
		goto err_release;
	}

	hdr = bar;
	magic = readl(&hdr->magic);
	version = readl(&hdr->version);
	nr_cpus = readl(&hdr->nr_cpus);
	page_size = readl(&hdr->page_size);

	if (magic != KSC_MAGIC) {
		pci_err(pdev, "bad magic 0x%08x\n", magic);
		rc = -ENODEV;
		goto err_unmap;
	}
	if (version != KSC_VERSION) {
		pci_err(pdev, "unsupported version %u\n", version);
		rc = -ENODEV;
		goto err_unmap;
	}
	if (page_size != PAGE_SIZE) {
		pci_err(pdev, "host page size %u != guest %lu\n",
			page_size, PAGE_SIZE);
		rc = -ENODEV;
		goto err_unmap;
	}
	if ((u64)(1 + nr_cpus) * page_size > bar_len) {
		pci_err(pdev, "BAR too small for %u cpus\n", nr_cpus);
		rc = -ENODEV;
		goto err_unmap;
	}

	for (cpu = 0; cpu < nr_cpus && cpu < num_possible_cpus(); cpu++) {
		u32 off = readl(&hdr->slice_ctrl_offset[cpu]);
		u8 *p;

		if (off == KSC_OFFSET_INVALID || off >= page_size) {
			per_cpu(kvm_slice_ctrl_ptr, cpu) = NULL;
			continue;
		}
		p = (u8 __force *)bar + (cpu + 1) * page_size + off;
		per_cpu(kvm_slice_ctrl_ptr, cpu) = p;
	}

	ksc_bar = bar;
	pci_set_drvdata(pdev, bar);

	/* Make per-CPU pointer stores visible before flipping the key. */
	smp_wmb();
	static_branch_enable(&kvm_slice_ctrl_key);

	pci_info(pdev, "armed for %u vCPUs\n", nr_cpus);
	return 0;

err_unmap:
	iounmap(bar);
err_release:
	pci_release_region(pdev, 0);
err_disable:
	pci_disable_device(pdev);
	return rc;
}

static void ksc_pci_remove(struct pci_dev *pdev)
{
	void __iomem *bar = pci_get_drvdata(pdev);
	int cpu;

	static_branch_disable(&kvm_slice_ctrl_key);
	/* After the key is off no new touches happen; depth counters wind
	 * down naturally as in-flight unlocks complete. */

	for_each_possible_cpu(cpu)
		per_cpu(kvm_slice_ctrl_ptr, cpu) = NULL;

	ksc_bar = NULL;
	if (bar)
		iounmap(bar);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
}

static const struct pci_device_id ksc_pci_ids[] = {
	{ PCI_DEVICE(KSC_PCI_VENDOR, KSC_PCI_DEVICE) },
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, ksc_pci_ids);

static struct pci_driver ksc_pci_driver = {
	.name     = "kvm-slice-ctrl",
	.id_table = ksc_pci_ids,
	.probe    = ksc_pci_probe,
	.remove   = ksc_pci_remove,
};

static int __init ksc_init(void)
{
	if (!kvm_para_available())
		return -ENODEV;
	return pci_register_driver(&ksc_pci_driver);
}

static void __exit ksc_exit(void)
{
	pci_unregister_driver(&ksc_pci_driver);
}

module_init(ksc_init);
module_exit(ksc_exit);

MODULE_DESCRIPTION("KVM rseq slice_ctrl PV spinlock guest driver");
MODULE_LICENSE("GPL");
