/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2013 Diablo Technologies Inc. ("DIABLO").  All       *
 *    rights reserved.                                                   *
 *                                                                       *
 *    This software is being licensed under a dual license, at Diablo's  *
 *    sole discretion.                                                   *
 *                                                                       *
 *    GPL License                                                        *
 *                                                                       *
 *    If you do not have explicit permission from Diablo, then you may   *
 *    only redistribute it and/or modify it under the terms of the GNU   *
 *    General Public License as published by the Free Software           *
 *    Foundation; either version 2 of the License, or (at your option)   *
 *    any later version located at <http://www.gnu.org/licenses/>.  See  *
 *    the GNU General Public License for more details.                   *
 *                                                                       *
 *    BSD License                                                        *
 *                                                                       *
 *    If you have explicit permission from Diablo, then redistribution   *
 *    and use in source and binary forms, with or without modification,  *
 *    are permitted provided that the following conditions are met:      *
 *                                                                       *
 *        * Redistributions of source code must retain the above         *
 *        copyright notice, this list of conditions and the following    *
 *        disclaimer.                                                    *
 *                                                                       *
 *        * Redistributions in binary form must reproduce the above      *
 *        copyright notice, this list of conditions and the following    *
 *        disclaimer in the documentation and/or other materials         *
 *        provided with the distribution.                                *
 *                                                                       *
 *        * Neither the name of the DIABLO nor the names of its          *
 *        contributors may be used to endorse or promote products        *
 *        derived from this software without specific prior written      *
 *        permission.                                                    *
 *                                                                       *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND             *
 *    CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,        *
 *    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF           *
 *    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE           *
 *    DISCLAIMED. IN NO EVENT SHALL DIABLO BE LIABLE FOR ANY DIRECT,     *
 *    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL         *
 *    DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE   *
 *    GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS      *
 *    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,       *
 *    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT(INCLUDING           *
 *    NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS *
 *    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.       *
 *                                                                       *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "td_kdefn.h"

#include "td_compat.h"

#include "td_device.h"
#include "td_devgroup.h"
#include "td_ioctl.h"
#include "td_engine.h"
#include "td_eng_conf_sysfs.h"
#include "td_eng_conf.h"
#include "td_compat.h"
#include "td_ucmd.h"
#include "td_eng_hal.h"
#include "td_discovery.h"
#include "td_dev_ata.h"
#include "td_memspace.h"

#include "td_osdev.h"

#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/module.h>
#ifdef KABI__export_header
#include <linux/export.h>
#endif
#include <linux/delay.h>


#ifdef CONFIG_TERADIMM_USERSPACE_API_V1
static char* td_mmap_prot = "wc";
module_param_named(mmap_prot, td_mmap_prot, charp, 0644);
#endif

/* declartions - These are our API to linux/driver/td_block.c */
int td_linux_block_create(struct td_osdev *dev);
int td_linux_block_register(struct td_osdev *dev, int major);
void td_linux_block_unregister(struct td_osdev *dev);
void td_linux_block_destroy(struct td_osdev *dev);



#define TD_OS_UNIQUE_MAX        (26*26*26)

static struct {
	const char* prefix;
	int base;
} prefix_offsets[] = {
	{ "td",    0 },
	{ "tr",  128 },
	{ "md",  192 },
	{ "zap", 192 },
	{ "ts",  222 },
	{ "ms",  222 },
	{ "stm", 222 },
};

/** returns a unique id from the name */
int __td_osdev_unique_id_from_name(const char *name)
{
	char fmt[16];
	int count;
	int unique_id;
	char a=0, b=0, c=0;
	int i, base;

	for (i = 0; i < NUM_ELEMENTS(prefix_offsets); i++) {

		snprintf(fmt, 12, "%s%s", prefix_offsets[i].prefix, "%c%c%c");
		base = prefix_offsets[i].base;

		count = sscanf(name, fmt, &a, &b, &c);
		if (count)
			break;
	}

	/* validate characters */
	switch (count) {
	case 3:
		if (c < 'a' || c > 'z')
			goto error;
		c -= 'a';
		/* fall through expected */
	case 2:
		if (b < 'a' || b > 'z')
			goto error;
		b -= 'a';
		/* fall through expected */
	case 1:
		if (a < 'a' || a > 'z')
			goto error;
		a -= 'a';
		break;
	default:
		goto error;
	}

	/* convert to number */
	switch (count) {
	case 1:
		unique_id = a;
		break;
	case 2:
		unique_id = ((a+1) * 26) + b;
		break;
	case 3:
		unique_id = ((((a+1) * 26) + (b+1)) * 26) + c;
		break;
	default:
		unique_id = -1;
		break;
	}
	
	return base + unique_id;

error:
	pr_err("New device name is invalid: %s\n", name);
	return -1;
}


int __osdev_check_name (struct td_osdev* dev, void* data)
{
	if (strcmp(dev->name, (const char*)data) == 0)
		return 1;
	return 0;
}

int td_osdev_assign_name(const char *prefix, char* buffer, int size)
{
	const char* dev_prefix;
	int dev_index = 0;
	int i, rc;
	
	for (i = 0; i < NUM_ELEMENTS(prefix_offsets); i++) {
		if (strcmp(prefix_offsets[i].prefix, prefix) == 0)
			break;
	}
	
	if (i >= NUM_ELEMENTS(prefix_offsets))
		return -ENODEV;

	dev_prefix = prefix_offsets[i].prefix;

	for (dev_index = 0; dev_index < TD_OS_UNIQUE_MAX; dev_index++) {
		if (dev_index < 26) {
			snprintf(buffer, size, "%s%c", dev_prefix, 'a'+dev_index);
		} else if (dev_index < (26*26) ) {
			snprintf(buffer, size, "%s%c%c", dev_prefix,
				'a'+(dev_index/26)-1, 'a'+(dev_index%26));
		} else {
			snprintf(buffer, size, "%s%c%c%c", dev_prefix,
				'a'+(dev_index/(26*26))-1, 'a'+(dev_index%(26*26)),
				'a'+(dev_index%26));
		}
		rc = td_osdev_list_iter(__osdev_check_name, buffer);
		if (rc == 0) {
			return 0;
		}
			
	}
	return -ESRCH;
};


/* ---- database of all device groups ---- */

static struct list_head td_osdev_list;
static unsigned td_osdev_list_count;
/*
 * The list mutex is taken on any intention to modify the list.
 * The spinlock must be taken on actual modification.  This strategy
 * allows quick readers to grab the spinlock even if a long lived
 * intention to modify the list is under way.
 */
static struct mutex td_osdev_list_mutex;
static spinlock_t td_osdev_list_lock;

//============================================================================
struct td_osdev_dump_name_state {
	enum        td_osdev_type type;
	char        *p;
	char        *e;
	uint32_t    count;
};

int __osdev_dump_name (struct td_osdev* dev, void* data)
{
	struct td_osdev_dump_name_state *st = data;
	if (dev->type == st->type) {
		int rc;

		int left = st->e - st->p;

		if (left < TD_DEVICE_NAME_MAX)
			return -ETOOSMALL;

		st->p += rc = snprintf(st->p, left, "%s\n", dev->name);

		if (rc<0)
			return rc;

		st->count ++;
	}
	return 0;
}

int td_osdev_dump_names(enum td_osdev_type t, char *buf, size_t len, uint32_t *count)
{
	int rc;
	struct td_osdev_dump_name_state state = {
		.type = t,
		.p = buf,
		.e = buf + len - TD_DEVICE_NAME_MAX,
		.count = 0,
	};



	if (len < TD_DEVICE_NAME_MAX)
		return -ETOOSMALL;

	state.count = 0;
	state.p = buf;
	state.e = buf + len - TD_DEVICE_NAME_MAX;

	rc = td_osdev_list_iter(__osdev_dump_name, &state);

	if (rc<0)
		return rc;

	*state.p=0;
	*count = state.count;

	return state.p-buf;
}



int td_osdev_list_iter(
		int (*action)(struct td_osdev *dev, void *data),
		void *data)
{
	int rc = 0;
	struct td_osdev *dev;

	/*
	 * Take the mutex for the life of our iterations.
	 * The mutex assures no list insert/deletion is even
	 * intended.  We don't need the spinlock here
	 * since list modification is not going to happen,
	 * and there is no reason to block other readers.
	 */
	mutex_lock(&td_osdev_list_mutex);

	list_for_each_entry(dev, &td_osdev_list, devs_link) {

		rc = action(dev, data);
		if (rc) break;
	}

	mutex_unlock(&td_osdev_list_mutex);

	return rc;
}

struct td_osdev *td_osdev_find (
		int (*is_it)(struct td_osdev*, void* data),
		void* data)
{
	struct td_osdev *have, *dev = NULL;
	
	spin_lock_bh(&td_osdev_list_lock);

	list_for_each_entry(have, &td_osdev_list, devs_link) {
		if (is_it(have, data) ) {
			dev = have;
			td_osdev_get(dev);
			break;
		}
	}
	spin_unlock_bh(&td_osdev_list_lock);

	return dev;
}


void __td_osdev_destroy(struct td_osdev *dev)
{

	/* getting here indicates that the last reference was removed */

	WARN_DEVICE_LOCKED(dev);

	WARN_ON(atomic_read(&dev->control_users));
	WARN_ON(atomic_read(&dev->block_users));
	WARN_ON(dev->queue);
	WARN_ON(dev->disk);

	if (! dev->_destroy) {
		WARN_ON("DON'T KNOW WHAT TO DESTROY");
		kfree(dev);
		return;
	}

	/* Let whoever created us clean up now */
	dev->_destroy(dev);
}

/* ---- external interface ---- */

/* Linux CHAR MISC sybsystem */

static int __check_dev_miscdev_minor(struct td_osdev *dev, void* data)
{
	int minor = *(int*)data;
#if 0
	if (dev->type != TD_OSDEV_DEVICE)
		return 0;
#endif
	return dev->miscdevice.minor ==  minor;
}

/* locate the td_device based on it's char misc device */
struct td_osdev *td_osdev_get_for_inode(struct inode *inode)
{
	int minor = iminor(inode);
	return td_osdev_find(__check_dev_miscdev_minor, &minor);
}


static int td_device_char_open(struct inode *inode, struct file *filp)
{
	struct td_osdev *dev;

	dev = td_osdev_get_for_inode(inode);
	if (!dev)
		return -ENODEV;

	atomic_inc(&dev->control_users);

	filp->private_data = dev;

	return 0;
}

static int td_device_char_release(struct inode *inode, struct file *filp)
{
	struct td_osdev *dev = filp->private_data;

	if (dev->type == TD_OSDEV_DEVICE) {
		struct td_device *td_dev = td_device_from_os(dev);
		struct td_engine *eng = td_device_engine(td_dev);

		if (current == eng->locker_context) {
#ifdef KABI__task_struct_comm
			td_os_warn(dev, "pid %u [%s] releasing device with locked buffers\n",
					current->pid, current->comm);
#else
			td_os_warn(dev, "pid %u releasing device with locked buffers\n",
					current->pid);
#endif
			td_engine_unlock(eng, current);
		}

		if (current == eng->bio_context) {
#ifdef KABI__task_struct_comm
			td_os_warn(dev, "pid %u [%s] releasing device with BIOs stopped\n",
					current->pid, current->comm);
#else
			td_os_warn(dev, "pid %u releasing device with BIOs stopped\n",
					current->pid);
#endif
			td_engine_start_bio(eng);
		}
		
#ifdef CONFIG_TERADIMM_USERSPACE_API_V1
		if (td_dev->td_usermode_context && current->tgid == td_dev->td_usermode_context->tgid) {
			td_os_warn(dev, "pid %u [%s] releasing device with USERMODE_EXCLUSIVE\n",
					current->tgid, current->comm);
			td_device_enter_state(td_dev, OFFLINE);
			td_dev->td_usermode_context = NULL;
		}
#endif
	}
	atomic_dec(&dev->control_users);

	td_osdev_put(dev);

	return 0;
}

static ssize_t td_device_char_read(struct file *filp, char __user *buf,
		size_t len, loff_t *ppos)
{
	/*struct td_device *dev = filp->private_data; */

	return -EIO;
}

static ssize_t td_device_char_write(struct file *filp, const char __user *buf,
		size_t len, loff_t *ppos)
{
	/*struct td_device *dev = filp->private_data; */

	return -EIO;
}

#ifdef CONFIG_TERADIMM_USERSPACE_API_V1
static int td_device_char_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct td_osdev *osdev = filp->private_data;
	struct td_device *dev;
	
	unsigned long req_offset, req_size;
	unsigned long phys_base, phys_size;
	unsigned long pfn;
	char *dup, *mp;
	
	if (osdev->type != TD_OSDEV_DEVICE) {
		td_os_err(osdev, "Attempt to mmap; not supported\n");
		return -EIO;
	}
	dev = td_device_from_os(osdev);

	if (! td_device_check_state(dev, USERMODE_EXCLUSIVE)) {
		td_os_err(osdev, "cannot mmap a device that's not exlusive\n");
		return -EBUSY;
	}

	dup = kstrndup(td_mmap_prot, 16, GFP_KERNEL);
	if (!dup)
		return -ENOMEM;

	mp = strim(dup);

	if (!strcmp(mp, "wb")) {
		/* default is WB */

	} else if (!strcmp(mp, "wc")) {
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	} else if (!strcmp(mp, "uc")) {
		pgprotval_t val = pgprot_val(vma->vm_page_prot);
		val &= ~_PAGE_CACHE_MASK;
		val |= _PAGE_CACHE_UC;
		vma->vm_page_prot = __pgprot(val);

	} else if (!strcmp(mp, "uc-")) {
		pgprotval_t val = pgprot_val(vma->vm_page_prot);
		val &= ~_PAGE_CACHE_MASK;
		val |= _PAGE_CACHE_UC_MINUS;
		vma->vm_page_prot = __pgprot(val);
	} else {
		td_os_err(osdev, "mmap_prot module parameter value '%s' is invalid\n", mp);

		kfree(dup);
		return -EINVAL;
	}

	kfree(dup);
	mp = dup = NULL;

	/* this is the actual device size */
	phys_base = dev->td_mapper.phys_base;
	phys_size = dev->td_mapper.phys_size;

	/* this is what the user wants to map */
	req_size   = vma->vm_end - vma->vm_start;
	req_offset = vma->vm_pgoff << PAGE_SHIFT;

	if (req_offset > phys_size
			|| (req_size+req_offset) > phys_size
			|| req_size & ~PAGE_MASK)
		return -EINVAL;

	/* PFN of the first page */
	pfn = phys_base >> PAGE_SHIFT;

	/* PFN of user requested page */
	pfn += vma->vm_pgoff;

	if (io_remap_pfn_range(vma, vma->vm_start, pfn,
				req_size, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}
#else
static int td_device_char_mmap(struct file *filp, struct vm_area_struct *vma)
{
	return -EIO;
}
#endif

static loff_t td_device_char_llseek(struct file *filp, loff_t off, int seek)
{
	/*struct td_device *dev = filp->private_data; */

	return -EIO;
}

static long td_device_char_ioctl(struct file *filp, unsigned int cmd,
		unsigned long raw_arg)
{
	struct td_osdev *dev = filp->private_data;
	int rc;

	WARN_DEVICE_LOCKED(dev);

	rc = dev->_ioctl(dev, cmd, raw_arg);
	if (rc == -ENOIOCTLCMD)
		return -EINVAL;
	return rc;
}

#ifdef CONFIG_COMPAT
static long td_device_char_compat_ioctl(struct file *filp, unsigned int cmd,
		unsigned long raw_arg)
{
	struct td_osdev *dev = filp->private_data;
	int rc;

	WARN_DEVICE_LOCKED(dev);

	rc = dev->_ioctl(dev, cmd, (unsigned long)compat_ptr(raw_arg));
	/* COMPAT_IOCTL is supposed to return ENOIOCTLCMD */
	return rc;
}
#endif
static struct file_operations td_device_char_fops = {
	.open           = td_device_char_open,
	.release        = td_device_char_release,
	.read           = td_device_char_read,
	.write          = td_device_char_write,
	.mmap           = td_device_char_mmap,
	.llseek         = td_device_char_llseek,
	.unlocked_ioctl = td_device_char_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = td_device_char_compat_ioctl,
#endif
};



static int td_osdev_register_control_device(struct td_osdev *dev)
{
	int rc;

	WARN_DEVICE_UNLOCKED(dev);

	snprintf(dev->ctrl_dev_name, sizeof(dev->ctrl_dev_name),
			"%s-ctrl", dev->name);

	memset(&dev->miscdevice, 0, sizeof(dev->miscdevice));

	dev->miscdevice.minor = MISC_DYNAMIC_MINOR,
	dev->miscdevice.name  = dev->ctrl_dev_name,
	dev->miscdevice.fops  = &td_device_char_fops,

	rc = misc_register(&dev->miscdevice);
	if (rc) {
		pr_err("Failed to register control device for '%s', err = %d\n",
				dev->name, rc);
		return rc;
	}

	td_os_info(dev, "Registered misc device '%s' with minor %d\n",
			dev->ctrl_dev_name, dev->miscdevice.minor);
	return 0;
}

static void td_osdev_unregister_control_device(struct td_osdev *dev)
{
	WARN_DEVICE_UNLOCKED(dev);

	misc_deregister(&dev->miscdevice);
}




/*
 * Linux BLOCK subsystem
 */
static int td_block_major = -1;

int td_osdev_init (struct td_osdev* dev, enum td_osdev_type type,
		const char* name,
		int (*ioctl) (struct td_osdev*, unsigned int cmd, unsigned long raw_arg),
		void (*destructor) (struct td_osdev*)
		)
{
	int unique_id = __td_osdev_unique_id_from_name(name);

	if (unique_id < 0)
		goto error_uid;

	strncpy(dev->name, name, TD_DEVICE_NAME_MAX);

	dev->type = type;

	dev->_ioctl = ioctl;
	dev->_destroy = destructor;

	dev->unique_id = unique_id;

	mutex_init(&dev->mutex);
	atomic_set(&dev->refcnt, 1);
	atomic_set(&dev->control_users, 0);
	atomic_set(&dev->block_users, 0);
	
	return 0;

error_uid:
	return -EINVAL;
}

static int td_osdev_sort_compare(void *priv, struct list_head *a, struct list_head *b)
{
	struct td_osdev *dev_a = list_entry(a, struct td_osdev, devs_link);
	struct td_osdev *dev_b = list_entry(b, struct td_osdev, devs_link);

	if (dev_a->unique_id != dev_b->unique_id)
		return dev_a->unique_id - dev_b->unique_id;

	return strcmp(dev_a->name, dev_b->name);
}


int td_osdev_register (struct td_osdev* dev)
{
	int rc;

	/* intention to modify the list */
	mutex_lock(&td_osdev_list_mutex);

	rc = td_osdev_register_control_device(dev);
	if (rc)
		goto error_control;

	/* add the new osdev to the list */
	spin_lock_bh(&td_osdev_list_lock);

	if (td_osdev_list_count++) {
		struct list_head* head = &td_osdev_list;
		struct td_osdev *have;
		/* Lets sort it... */
		list_for_each_entry(have, &td_osdev_list, devs_link) {
			if (td_osdev_sort_compare(NULL, &dev->devs_link, &have->devs_link) < 0)
				break;
			head = &have->devs_link;
		}
		list_add(&dev->devs_link, head);
	} else {
		list_add_tail(&dev->devs_link, &td_osdev_list);
	}
	spin_unlock_bh(&td_osdev_list_lock);

	rc = 0;

error_control:

	mutex_unlock(&td_osdev_list_mutex);
	return rc;
}

int td_osdev_online (struct td_osdev *dev)
{
	int rc;

	rc = td_linux_block_create(dev);
	if (rc)
		goto error_block_create;

	rc = td_linux_block_register(dev, td_block_major);
	if (rc)
		goto error_block_register;

	return 0;

error_block_register:
	td_linux_block_destroy(dev);
error_block_create:
	return rc;
}

int td_osdev_offline (struct td_osdev *dev)
{
	td_linux_block_unregister(dev);
	td_linux_block_destroy(dev);
	dev->block_params.capacity = 0;
	return 0;
}

void td_osdev_unregister (struct td_osdev *dev)
{
	/* Intention to modify the list */
	mutex_lock(&td_osdev_list_mutex);

	td_osdev_unregister_control_device(dev);

	spin_lock_bh(&td_osdev_list_lock);
	list_del(&dev->devs_link);
	td_osdev_list_count --;
	spin_unlock_bh(&td_osdev_list_lock);

	mutex_unlock(&td_osdev_list_mutex);
}




int td_os_init (void)
{
	INIT_LIST_HEAD(&td_osdev_list);
	mutex_init(&td_osdev_list_mutex);
	spin_lock_init(&td_osdev_list_lock);
	td_osdev_list_count = 0;

	td_block_major = register_blkdev(0, TERADIMM_NAME);
	if (td_block_major <= 0) {
		pr_warning("Unable to get major number.\n");
		return -EBUSY;
	}
	return 0;
}

void td_os_exit (void)
{
	WARN_ON(!list_empty(&td_osdev_list));
	WARN_ON(td_osdev_list_count);

	unregister_blkdev(td_block_major, TERADIMM_NAME);
}

