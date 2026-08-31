// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * virtio-kcov: advertise a kcov coverage buffer to the hypervisor.
 *
 * A coverage-guided VM fuzzer runs the guest under a host that reads the guest's
 * kcov buffer directly and folds edges off the fuzzed data path. This driver is
 * the guest half of that channel, replacing an earlier arch-specific
 * (reserved-SCLP-command) advertisement with a generic virtio device that works
 * on any transport.
 *
 * The coverage buffer is owned *by this driver*, not by a user file descriptor:
 * it is never mmap()ed and has no fd whose close()/exit() could vfree it, so the
 * host's guest-physical page list stays valid for the life of the device (a
 * dying userspace process cannot pull it out from under the host). The driver
 * resolves the buffer's guest-physical pages and publishes a control block
 * describing them through config space; the host latches on the generation
 * counter, written last.
 */
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/kcov.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <uapi/linux/virtio_kcov.h>

/*
 * Common-handle instance the guest subsystems collect into. Must match the
 * annotation that brackets the fuzzed code (s390 sclp: SCLP_FUZZ_KCOV_INSTANCE).
 */
#define VIRTIO_KCOV_INSTANCE	1
/* Coverage buffer size, in longs, and the resulting page-list cap. */
#define VIRTIO_KCOV_WORDS	(64UL << 10)
#define VIRTIO_KCOV_MAX_PAGES	256

struct virtio_kcov {
	struct virtio_device	*vdev;
	struct virtqueue	*vq;	/* unused; the advertise rides config space */
	struct kcov		*kcov;	/* device-owned remote area */
	u64			handle;
	__le64			*ctl;	/* control block page: {npages, words, gpa[]} */
	u32			generation;
};

/* The advertise is config-space only; this queue exists but is never used. */
static void virtio_kcov_vq_done(struct virtqueue *vq)
{
}

static int virtio_kcov_advertise(struct virtio_kcov *vk)
{
	phys_addr_t phys[VIRTIO_KCOV_MAX_PAGES];
	unsigned int words = 0;
	int n, i;

	n = kcov_remote_area_phys(vk->handle, phys, VIRTIO_KCOV_MAX_PAGES, &words);
	if (n <= 0)
		return n < 0 ? n : -ENODEV;

	vk->ctl[0] = cpu_to_le64(n);
	vk->ctl[1] = cpu_to_le64(words);
	for (i = 0; i < n; i++)
		vk->ctl[2 + i] = cpu_to_le64(phys[i]);

	/* Publish desc_gpa first, then bump generation last to commit. */
	virtio_cwrite64(vk->vdev, offsetof(struct virtio_kcov_config, desc_gpa),
			virt_to_phys(vk->ctl));
	vk->generation++;
	virtio_cwrite32(vk->vdev, offsetof(struct virtio_kcov_config, generation),
			vk->generation);
	return 0;
}

static int virtio_kcov_probe(struct virtio_device *vdev)
{
	struct virtio_kcov *vk;
	int err;

	vk = kzalloc(sizeof(*vk), GFP_KERNEL);
	if (!vk)
		return -ENOMEM;
	vk->vdev = vdev;
	vdev->priv = vk;
	vk->handle = kcov_remote_handle(KCOV_SUBSYSTEM_COMMON, VIRTIO_KCOV_INSTANCE);

	vk->ctl = (__le64 *)get_zeroed_page(GFP_KERNEL);
	if (!vk->ctl) {
		err = -ENOMEM;
		goto err_free;
	}

	vk->vq = virtio_find_single_vq(vdev, virtio_kcov_vq_done, "unused");
	if (IS_ERR(vk->vq)) {
		err = PTR_ERR(vk->vq);
		goto err_page;
	}

	vk->kcov = kcov_remote_alloc(vk->handle, VIRTIO_KCOV_WORDS,
				     VIRTIO_KCOV_WORDS);
	if (IS_ERR(vk->kcov)) {
		err = PTR_ERR(vk->kcov);
		goto err_vqs;
	}

	virtio_device_ready(vdev);

	err = virtio_kcov_advertise(vk);
	if (err)
		goto err_kcov;

	dev_info(&vdev->dev, "advertised %lu-word kcov buffer\n",
		 VIRTIO_KCOV_WORDS);
	return 0;

err_kcov:
	kcov_remote_free(vk->kcov);
err_vqs:
	vdev->config->del_vqs(vdev);
err_page:
	free_page((unsigned long)vk->ctl);
err_free:
	kfree(vk);
	return err;
}

static void virtio_kcov_remove(struct virtio_device *vdev)
{
	struct virtio_kcov *vk = vdev->priv;

	virtio_reset_device(vdev);
	vdev->config->del_vqs(vdev);
	kcov_remote_free(vk->kcov);
	free_page((unsigned long)vk->ctl);
	kfree(vk);
}

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_KCOV, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtio_kcov_driver = {
	.driver.name	= KBUILD_MODNAME,
	.id_table	= id_table,
	.probe		= virtio_kcov_probe,
	.remove		= virtio_kcov_remove,
};

module_virtio_driver(virtio_kcov_driver);
MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio kcov coverage-advertisement driver");
MODULE_LICENSE("GPL");
