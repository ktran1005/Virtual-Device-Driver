// SPDX-License-Identifier: GPL-2.0

#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/miscdevice.h>

static struct dentry *dir;
static char kbuffer[PAGE_SIZE];
static struct kset *swapstore_kset;
static struct swapstore *current_swapstore;
static struct kobject *should_eject;
static DEFINE_SPINLOCK(swapstore_spinlock);
static DEFINE_MUTEX(swapstore_mutex);
static struct swapstore *swap_device0;
static int val;

struct swapstore {
	struct kobject kobj;
	char data[PAGE_SIZE];
	int readonly;
	int removable;
};

struct swapstore_attr {
	struct attribute attr;
	ssize_t (*show)(struct swapstore *foo, struct swapstore_attr *attr, char *buf);
	ssize_t (*store)(struct swapstore *foo, struct swapstore_attr *attr, const char *buf, size_t count);
};

static ssize_t readonly_show(struct swapstore *fobj, struct swapstore_attr *fattr, char *buf)
{
	return sprintf(buf, "%d\n", fobj->readonly);
}

static ssize_t readonly_store(struct swapstore *fobj, struct swapstore_attr *fattr, const char *buf, size_t len)
{
	int ret;


	if ((buf[0] == '0') || (buf[0] == '1')) {
		ret = kstrtoint(buf, 10, &fobj->readonly);
		if (ret < 0)
			return ret;

		return len;
	} else
		return -EINVAL;
}

static ssize_t removable_show(struct swapstore *fobj, struct swapstore_attr *fattr, char *buf)
{
	return sprintf(buf, "%d\n", fobj->removable);
}

static ssize_t removable_store(struct swapstore *fobj, struct swapstore_attr *attr, const char *buf, size_t len)
{
	return len;
}

static ssize_t swapstore_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct swapstore_attr *fattr = container_of(attr, struct swapstore_attr, attr);
	struct swapstore *fobj = container_of(kobj, struct swapstore, kobj);

	if (!fattr->show)
		return -EIO;

	return fattr->show(fobj, fattr, buf);
}

static ssize_t swapstore_attr_store(struct kobject *kobj, struct attribute *attr, const char *buf, size_t len)
{
	struct swapstore_attr *fattr = container_of(attr, struct swapstore_attr, attr);
	struct swapstore *fobj = container_of(kobj, struct swapstore, kobj);

	if (!fattr->store)
		return -EIO;

	return fattr->store(fobj, fattr, buf, len);
}

static struct swapstore_attr readonly_attr =
	__ATTR(readonly, 0600, readonly_show, readonly_store);

static struct swapstore_attr removable_attr =
	__ATTR(removable, 0400, removable_show, removable_store);

static struct attribute *swapstore_default_attrs[] = {
	&readonly_attr.attr,
	&removable_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(swapstore_default);


static const struct sysfs_ops swapstore_sysfs_ops = {
	.show = swapstore_attr_show,
	.store = swapstore_attr_store,
};

void swapstore_release(struct kobject *kobj)
{
	struct swapstore *fobj;

	fobj = container_of(kobj, struct swapstore, kobj);

	kfree(fobj);
}

static struct kobj_type swapstore_ktype = {
	.sysfs_ops = &swapstore_sysfs_ops,
	.release = swapstore_release,
	.default_groups = swapstore_default_groups,
};

static struct kobject *swapper_find(char *name)
{
	struct kobject *cur = NULL;

	list_for_each_entry(cur, &swapstore_kset->list, entry) {
		if (!(strcmp(cur->name, name)))
			return cur;
	}

	return NULL;
}

static struct swapstore *swapstore_create(const char *name)
{
	struct swapstore *new;
	int ret;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	new->kobj.kset = swapstore_kset;

	ret = kobject_init_and_add(&new->kobj, &swapstore_ktype, NULL, "%s", name);
	if (ret) {
		kobject_put(&new->kobj);
		return NULL;
	}

	kobject_uevent(&new->kobj, KOBJ_ADD);

	return new;
}

static ssize_t insert_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t ret;
	struct kobject *cur = NULL;
	struct swapstore *swap_device;
	unsigned long swap_flag;

	ret = mutex_lock_interruptible(&swapstore_mutex);
	if (ret)
		goto out;
	ret = simple_write_to_buffer(kbuffer, PAGE_SIZE, ppos, buf, count);
	kbuffer[strlen(buf)] = '\0';
	mutex_unlock(&swapstore_mutex);
	spin_lock_irqsave(&swapstore_spinlock, swap_flag);
	list_for_each_entry(cur, &swapstore_kset->list, entry) {
		if (!(strcmp(kbuffer, cur->name))) {
			spin_unlock_irqrestore(&swapstore_spinlock, swap_flag);
			return -EINVAL;
		}
	}

	spin_unlock_irqrestore(&swapstore_spinlock, swap_flag);
	swap_device = swapstore_create(kbuffer);
	swap_device->removable = 1;
	swap_device->readonly = 0;
	memset(kbuffer, 0, strlen(kbuffer));

out:
	return ret;
}

static ssize_t swapstore_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t ret;

	ret = mutex_lock_interruptible(&swapstore_mutex);
	if (ret)
		goto out;

	strcat(kbuffer, current_swapstore->kobj.name);
	strcat(kbuffer, "\n");
	ret = simple_read_from_buffer(buf, count, ppos, kbuffer, PAGE_SIZE);
	memset(kbuffer, 0, strlen(kbuffer));
	mutex_unlock(&swapstore_mutex);

out:
	return ret;
}

static ssize_t swapstore_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t ret;
	struct kobject *cur = NULL;
	unsigned long swap_flag;

	if (val == 0) {
		ret = mutex_lock_interruptible(&swapstore_mutex);
		if (ret)
			goto out;

		ret = simple_write_to_buffer(kbuffer, PAGE_SIZE, ppos, buf, count);
		kbuffer[strlen(buf)] = '\0';
		mutex_unlock(&swapstore_mutex);
		spin_lock_irqsave(&swapstore_spinlock, swap_flag);
		cur = swapper_find(kbuffer);
		spin_unlock_irqrestore(&swapstore_spinlock, swap_flag);
		current_swapstore = container_of(cur, struct swapstore, kobj);
		memset(kbuffer, 0, strlen(kbuffer));
		mutex_lock(&swapstore_mutex);
		if (should_eject != NULL) {
			kobject_put(should_eject);
			should_eject = NULL;
			mutex_unlock(&swapstore_mutex);
			goto out;
		}

		mutex_unlock(&swapstore_mutex);

	} else
		return -EBUSY;
out:
	return ret;
}

static ssize_t eject_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t ret;
	struct kobject *cur = NULL;
	struct kobject *next = NULL;

	ret = mutex_lock_interruptible(&swapstore_mutex);
	if (ret)
		goto out;
	ret = simple_write_to_buffer(kbuffer, PAGE_SIZE, ppos, buf, count);
	kbuffer[strlen(buf)] = '\0';
	mutex_unlock(&swapstore_mutex);
	if (!(strcmp(kbuffer, "default"))) {
		pr_info("swapper: device %s is not removable\n", kbuffer);
		memset(kbuffer, 0, strlen(kbuffer));
		return -EINVAL;
	}

	if (!(strcmp(kbuffer, current_swapstore->kobj.name))) {
		should_eject = &current_swapstore->kobj;
		pr_info("swapper: releasing %s\n", kbuffer);
		goto out;

	}

	list_for_each_entry_safe(cur, next, &swapstore_kset->list, entry) {
		if (!(strcmp(kbuffer, cur->name))) {
			kobject_put(cur);
			pr_info("swapper: releasing %s\n", kbuffer);
			memset(kbuffer, 0, strlen(kbuffer));
			goto out;
		}
	}

	memset(kbuffer, 0, strlen(kbuffer));
	return -EINVAL;

out:
	return ret;
}

static int swapper_open(struct inode *inode, struct file *filp)
{
	int ret;

	ret = mutex_lock_interruptible(&swapstore_mutex);
	if (ret)
		goto out;

	pr_info("swapper: open count: 1\n");
	val = 1;
	mutex_unlock(&swapstore_mutex);

out:
	return ret;
}

static int swapper_release(struct inode *inode, struct file *filp)
{
	int ret;

	ret = mutex_lock_interruptible(&swapstore_mutex);
	if (ret)
		goto out;

	pr_info("swapper: open count: 0\n");
	val = 0;
	mutex_unlock(&swapstore_mutex);

out:
	return ret;
}

static ssize_t swapper_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t ret;

	if (current_swapstore->readonly == 1)
		return -EPERM;

	memset(current_swapstore->data, 0, PAGE_SIZE);
	ret = mutex_lock_interruptible(&swapstore_mutex);
	if (ret)
		goto out;

	ret = simple_write_to_buffer(current_swapstore->data, PAGE_SIZE, ppos, buf, count);
	mutex_unlock(&swapstore_mutex);

out:
	return ret;

}

static ssize_t swapper_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t ret;

	ret = mutex_lock_interruptible(&swapstore_mutex);
	if (ret)
		goto out;
	ret = simple_read_from_buffer(buf, count, ppos, current_swapstore->data, PAGE_SIZE);
	mutex_unlock(&swapstore_mutex);

out:
	return ret;
}

static const struct file_operations swapper_fops = {
	.owner = THIS_MODULE,
	.read = swapper_read,
	.write = swapper_write,
	.open = swapper_open,
	.release = swapper_release,
};


static const struct file_operations insert_ops = {
	.owner = THIS_MODULE,
	.write = insert_write,
};

static const struct file_operations swapstore_ops = {
	.owner = THIS_MODULE,
	.read = swapstore_read,
	.write = swapstore_write,
};

static const struct file_operations eject_ops = {
	.owner = THIS_MODULE,
	.write = eject_write,
};

static struct miscdevice swapper_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "swapper",
	.fops = &swapper_fops,
};

static int __init swapstore_init(void)
{
	struct dentry *insert;
	struct dentry *swapstore;
	struct dentry *eject;
	int ret;

	dir = debugfs_create_dir("swapper", NULL);
	if (!dir)
		goto error;

	insert = debugfs_create_file("insert", 0200, dir, NULL, &insert_ops);
	if (!insert)
		goto error;

	swapstore = debugfs_create_file("swapstore", 0600, dir, NULL, &swapstore_ops);
	if (!swapstore)
		goto error;

	eject = debugfs_create_file("eject", 0200, dir, NULL, &eject_ops);
	if (!eject)
		goto error;

	swapstore_kset = kset_create_and_add("swapstore", NULL, kernel_kobj);
	if (!swapstore_kset)
		return -ENOMEM;

	swap_device0 = swapstore_create("default");
	if (!swap_device0)
		goto dev0_error;
	swap_device0->readonly = 0;
	swap_device0->removable = 0;
	current_swapstore = swap_device0;
	ret = misc_register(&swapper_device);
	if (ret)
		pr_info("Project 3: misc_register() failed\n");

	else
		pr_info("Project 3: misc char device registered\n");

	return 0;

dev0_error:
	kset_unregister(swapstore_kset);

	return -EINVAL;
error:
	pr_info("Project 3: debugfs failed to load\n");
	debugfs_remove_recursive(dir);
	return -ENOMEM;

}

static void __exit swapstore_exit(void)
{
	struct kobject *entry = NULL;
	struct kobject *sav = NULL;

	list_for_each_entry_safe(entry, sav, &swapstore_kset->list, entry) {
		pr_info("swapper: releasing %s\n", entry->name);
		kobject_put(entry);
	}

	debugfs_remove_recursive(dir);
	kset_unregister(swapstore_kset);
	misc_deregister(&swapper_device);
	pr_info("swapper: clean complete\n");
}

module_init(swapstore_init);
module_exit(swapstore_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Charles Tran");
MODULE_DESCRIPTION("Project 3");
