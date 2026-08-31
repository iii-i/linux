/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef _LINUX_VIRTIO_KCOV_H
#define _LINUX_VIRTIO_KCOV_H
/*
 * Virtio-kcov: the guest hands the host a kcov coverage buffer so a
 * coverage-guided VM fuzzer can read guest edges directly.
 *
 * The device is configuration-space only (no virtqueues). The guest driver
 * publishes a control block describing its (device-owned, un-freeable) coverage
 * buffer and bumps @generation to commit; the host reads @desc_gpa last.
 *
 * The control block at @desc_gpa is a guest-physical page holding, little-endian:
 *   __le64 npages;         number of coverage-buffer pages that follow
 *   __le64 words;          coverage-buffer size in longs (kcov area size)
 *   __le64 gpa[npages];    guest-physical address of each coverage page
 */
#include <linux/types.h>
#include <linux/virtio_types.h>

struct virtio_kcov_config {
	/* Guest-physical address of the control block (see above). */
	__le64 desc_gpa;
	/* Bumped by the guest after desc_gpa is written; the host latches here. */
	__le32 generation;
};

#endif /* _LINUX_VIRTIO_KCOV_H */
