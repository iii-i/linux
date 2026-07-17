.. SPDX-License-Identifier: GPL-2.0

======================================
Persistent memory from memmap= on s390
======================================

Overview
========

s390 supports the protected form of ``memmap=``::

    memmap=<size>!<addr>

This removes ``size`` bytes of physical memory starting at ``addr`` from
normal RAM. With ``CONFIG_PMEM_LEGACY`` and ``CONFIG_BLK_DEV_PMEM`` the
range shows up as ``/dev/pmem0``.

Only a single region is supported. Both ``size`` and ``addr`` accept the
usual ``K``/``M``/``G`` suffixes. The range is rounded out to page
boundaries. It must lie within online memory, or it is ignored with a
warning. It must not overlap the memory the boot loader puts the
decompressor, the initrd or the IPL report in, or the decompressor panics:
the data in the range is already gone at that point.

The primary use case is passing a large data blob, for example an installer
ISO image, to a kexec'd kernel without going through a storage device: the
origin kernel writes the data into ``/dev/pmem0`` and kexecs a target kernel
that specifies the same ``memmap=`` region.

Persistence across kexec
========================

The range lives at a fixed physical address that both kernels reserve, so
no kexec handover metadata is needed. Three things must not touch it:

* The origin kernel's page allocator. The range is removed from memblock.

* kexec in the origin kernel. It places segments only in free memory.

* The decompressor of the target kernel. It places the kernel image, the
  relocs, the amode31 section and early page tables top-down anywhere in
  online memory, so it reserves the range before doing any of that; see
  ``RR_MEMMAP`` in ``arch/s390/boot/startup.c``.

The range survives a crash as well, but it holds user data rather than
kernel memory, so a kdump kernel booted with the same ``memmap=`` leaves
it out of ``/proc/vmcore``.

struct page backing
===================

With ``CONFIG_ZONE_DEVICE``, which the s390 defconfig enables, the pmem
driver maps the range with ``devm_memremap_pages()``: the range gets
``struct page`` backing and ``fsdax`` works. This requires both ``size``
and ``addr`` to be multiples of 2 MiB, the memory hotplug subsection size;
otherwise the pmem device fails to probe.

Without ``CONFIG_ZONE_DEVICE`` the range is available in raw mode only,
without ``struct page`` backing, and ``fsdax`` is not available. The block
device and a plain file system on top of it work either way.
