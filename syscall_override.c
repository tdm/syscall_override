#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/syscalls.h>
#include <asm/errno.h>
#include <asm/unistd.h>
#include <linux/mman.h>
#include <asm/proto.h>
#include <asm/delay.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/sched.h>

static asmlinkage long (*real_clock_gettime)(const clockid_t which, struct timespec __user *tp);
static asmlinkage time_t (*real_time)(time_t __user *tloc);

static sys_call_ptr_t *sct;

static int enable;

static long offset;

asmlinkage long
patched_clock_gettime(const clockid_t which, struct timespec __user *tp)
{
	long rc;
	struct timespec kernel_tp;

	rc = (*real_clock_gettime)(which, tp);
	if (rc != 0)
		return rc;
	if (memcmp(current->comm, "pvs", 3) != 0)
		return rc;
	if (which != CLOCK_REALTIME && which != CLOCK_REALTIME_COARSE)
		return rc;

	printk("clock_gettime called from pvs\n");
	if (copy_from_user(tp, &kernel_tp, sizeof(kernel_tp)))
		return -EFAULT;
	kernel_tp.tv_sec += offset;
	if (copy_to_user(tp, &kernel_tp, sizeof(kernel_tp)))
		return -EFAULT;

	return rc;
}

asmlinkage time_t
patched_time(time_t __user *tloc)
{
	long rc;
	time_t t;

	rc = (*real_time)(tloc);
	if (rc == -EFAULT)
		return rc;
	if (memcmp(current->comm, "pvs", 3) != 0)
		return rc;

	printk("time called from pvs\n");
	if (tloc) {
		if (copy_from_user(tloc, &t, sizeof(t)))
			return -EFAULT;
		t += offset;
		if (copy_to_user(tloc, &t, sizeof(t)))
			return -EFAULT;
	}
	rc += offset;

	return rc;
}

static void
pte_ro(pte_t *pte)
{
	set_pte_atomic(pte, pte_wrprotect(*pte));
}

static void
pte_rw(pte_t *pte)
{
	set_pte_atomic(pte, pte_mkwrite(*pte));
}

static void
override_disable(void)
{
	unsigned int level = 0;
	pte_t *pte;

	pte = lookup_address((unsigned long)sct, &level);
	pte_rw(pte);
	sct[__NR_time] = (sys_call_ptr_t)real_time;
	sct[__NR_clock_gettime] = (sys_call_ptr_t)real_clock_gettime;
	pte_ro(pte);
}

static void
override_enable(void)
{
	unsigned int level = 0;
	pte_t *pte;

	pte = lookup_address((unsigned long)sct, &level);
	printk("%s: call pte_rw\n", __func__);
	pte_rw(pte);
	printk("%s: modify sct\n", __func__);
	sct[__NR_clock_gettime] = (sys_call_ptr_t)patched_clock_gettime;
	sct[__NR_time] = (sys_call_ptr_t)patched_time;
	printk("%s: call pte_ro\n", __func__);
	pte_ro(pte);
	printk("%s: exit\n", __func__);
}

/*** sysfs interface ***/

static ssize_t
enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", enable);
}

static ssize_t
enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int ret;
	int val;

	ret = sscanf(buf, "%d", &val);
	if (ret != 1)
		return -EINVAL;

	if (val != enable) {
		switch (val) {
		case 0:
			override_disable();
			break;
		case 1:
			override_enable();
			break;
		default:
			return -EINVAL;
		}
		enable = val;
	}

	return count;
}

static ssize_t
offset_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%ld\n", offset);
}

static ssize_t
offset_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int ret;
	long val;

	ret = sscanf(buf, "%ld", &val);
	if (ret != 1)
		return -EINVAL;

	offset = val;

	return count;
}

/*** init and exit ***/

static struct kobject *syscalloverride_kobj;

static struct kobj_attribute enable_attr =
	__ATTR(enable, 0644, enable_show, enable_store);

static struct kobj_attribute offset_attr =
	__ATTR(offset, 0644, offset_show, offset_store);

static int __init
mod_init(void)
{
	int ret;
	sys_call_ptr_t *sct1;

	printk("Module init\n");
	ret = 0;
	sct1 = (sys_call_ptr_t*)kallsyms_lookup_name("sys_call_table");
	printk("sct1=%p\n", (void*)sct1);
	real_clock_gettime = (void*)kallsyms_lookup_name("sys_clock_gettime");
	printk("orig sys_clock_gettime=%p\n", real_clock_gettime);
	real_time = (void*)kallsyms_lookup_name("sys_time");
	printk("orig sys_time=%p\n", real_time);

	sct = sct1;

	syscalloverride_kobj = kobject_create_and_add("syscalloverride", kernel_kobj);
	if (!syscalloverride_kobj) {
		ret = -ENOMEM;
		goto out;
	}

	ret = sysfs_create_file(syscalloverride_kobj, &enable_attr.attr);
	if (ret) {
		pr_err("%s: cannot create sysfs file\n", __func__);
		goto out_put;
	}
	ret = sysfs_create_file(syscalloverride_kobj, &offset_attr.attr);
	if (ret) {
		pr_err("%s: cannot create sysfs file\n", __func__);
		goto out_put;
	}

	printk("Module init successful\n");

out:
	return ret;

out_put:
	kobject_put(syscalloverride_kobj);
	goto out;
}
 
static void __exit
mod_exit(void)
{
	printk("Module exit\n");

	if (enable) {
		override_disable();
	}

	kobject_put(syscalloverride_kobj);
}
 
module_init(mod_init);
module_exit(mod_exit);
MODULE_LICENSE("GPL");
