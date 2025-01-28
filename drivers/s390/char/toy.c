#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>

void *patchme(void);
void *patchme(void) { return patchme; }

static ssize_t sysfs_write(struct kobject *kobj, struct kobj_attribute *attr,
                           const char *buf, size_t count)
{
	printk(KERN_NOTICE "patchme=0x%lx patchme()=0x%lx\n",
	       (unsigned long)patchme, (unsigned long)patchme());
	return count;
}

static struct kobject *kobj;
static struct kobj_attribute attr = __ATTR(knob, 0220, NULL, sysfs_write);

static int __init toy_init(void)
{
	kobj = kobject_create_and_add("s390toy", kernel_kobj);
	if (!kobj)
		return -ENOMEM;

	return sysfs_create_file(kobj, &attr.attr);
}

static void __exit toy_exit(void)
{
	kobject_put(kobj);
}

module_init(toy_init);
module_exit(toy_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ilya Leoshkevich <iii@linux.ibm.com>");
MODULE_DESCRIPTION("Toy driver");
