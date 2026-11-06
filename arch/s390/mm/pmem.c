// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/libnvdimm.h>

/* CPU caches are coherent with memory. */

void arch_wb_cache_pmem(void *addr, size_t size)
{
}
EXPORT_SYMBOL_GPL(arch_wb_cache_pmem);

void arch_invalidate_pmem(void *addr, size_t size)
{
}
EXPORT_SYMBOL_GPL(arch_invalidate_pmem);
