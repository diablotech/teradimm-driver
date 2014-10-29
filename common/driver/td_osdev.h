/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2013 Diablo Technologies Inc. (Diablo).              *
 *    All rights reserved.                                               *
 *                                                                       *
 *    This program is free software; you can redistribute it and/or      *
 *    modify it under the terms of the GNU General Public License        *
 *    as published by the Free Software Foundation; either version 2     *
 *    of the License, or (at your option) any later version located at   *
 *    <http://www.gnu.org/licenses/                                      *
 *                                                                       *
 *    This program is distributed WITHOUT ANY WARRANTY; without even     *
 *    the implied warranty of MERCHANTABILITY or FITNESS FOR A           *
 *    PARTICULAR PURPOSE.  See the GNU General Public License for        *
 *    more details.                                                      *
 *                                                                       *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef _TD_OS_H_
#define _TD_OS_H_

#include "td_kdefn.h"

#include "td_compat.h"

#include "td_defs.h"
#include "td_memspace.h"
#include "td_trace.h"
#include "td_util.h"
#include "td_cpu.h"
#include "td_eng_conf.h"
#include "td_ioctl.h"
#include "td_eng_latency.h"
#include "td_token.h"
#include "td_token_list.h"
#include "td_trace.h"
#include "td_stash.h"
#include "td_monitor.h"

#include <linux/miscdevice.h>
#include <linux/platform_device.h>

extern int td_os_init(void);
extern void td_os_exit(void);


#define td_os_name(dev) ((dev) && (dev)->name) \
			? (dev)->name : TERADIMM_NAME

#define td_os_pk(dev,lvl,fmt,...) \
	printk(lvl "%s: " fmt, td_os_name(dev), ##__VA_ARGS__)

#define td_os_emerg(d,fmt,...)   td_os_pk(d, KERN_EMERG, fmt, ##__VA_ARGS__)
#define td_os_alert(d,fmt,...)   td_os_pk(d, KERN_ALERT, fmt, ##__VA_ARGS__)
#define td_os_crit(d,fmt,...)    td_os_pk(d, KERN_CRIT, fmt, ##__VA_ARGS__)
#define td_os_err(d,fmt,...)     td_os_pk(d, KERN_ERR, fmt, ##__VA_ARGS__)
#define td_os_warning(d,fmt,...) td_os_pk(d, KERN_WARNING, fmt, ##__VA_ARGS__)
#define td_os_notice(d,fmt,...)  td_os_pk(d, KERN_NOTICE, fmt, ##__VA_ARGS__)
#define td_os_info(d,fmt,...)    td_os_pk(d, KERN_INFO, fmt, ##__VA_ARGS__)
#define td_os_debug(d,fmt,...)   do { if (1) td_os_pk(d, KERN_DEBUG, fmt, ##__VA_ARGS__);} while (0)
#define td_os_warn td_os_warning
#define td_os_dbg td_os_debug


enum td_osdev_type {
	TD_OSDEV_DEVICE = 0,
	TD_OSDEV_RAID,
	TD_OSDEV_MAX
};

struct td_osdev_block_params {
	uint64_t        capacity;
	unsigned        bio_max_bytes;
	unsigned        bio_sector_size;
	unsigned        hw_sector_size;
	unsigned        discard:1;
};


#define TD_UUID_LENGTH  16UL
#define TD_SERIAL_LENGTH 4UL
#define TD_MODEL_LENGTH 18UL
#define TD_VENDOR_LENGTH 10UL
#define TD_REVISION_LENGTH 6UL

/**
 * These are common "OS DEVICE" fields that are embeded in ever
 * td_device/td_raid that has an OS 'device' it uses
 */
struct td_osdev {
	enum td_osdev_type  type;
	char                name[TD_DEVICE_NAME_MAX];
	
	/* This information is relied on for os identification purposes */
	unsigned char       uuid[TD_UUID_LENGTH];

	unsigned char       serial[TD_SERIAL_LENGTH];
	unsigned char       model[TD_MODEL_LENGTH];
	unsigned char       vendor[TD_VENDOR_LENGTH];
	unsigned char       revision[TD_REVISION_LENGTH];


	int                 unique_id;                  /**< used for minor number selection */

	atomic_t            control_users; /**< count of users of control device */
	atomic_t            block_users; /**< count of users of block device */

	struct list_head    devs_link;   /**< link in list of all devices */

	/**
	 * osdev is ref counted
	 *
	 * use td_osdev_get() and td_osdev_put() to modify reference count
	 *
	 * First caller to _get() creates a thread, ref count is 1.
	 * Last caller to _put() destroys it, because ref count is 0.
	 */
	atomic_t            refcnt;

	struct mutex        mutex;       /**< prevents concurrent state changes / deletes */
#ifdef CONFIG_TERADIMM_DEBUG_DEVICE_LOCK
	struct task_struct  *mutex_holder;
#endif


	char                ctrl_dev_name[TD_DEVICE_NAME_MAX + 8];
	struct miscdevice   miscdevice;  /**< this is the /dev/tdX-ctrl */

	struct request_queue *queue;     /**< pending block requests queue here */
	struct gendisk       *disk;      /**< block device disk description */

	struct platform_device pdevice;
	
	struct td_osdev_block_params block_params;
	int (*_ioctl)(struct td_osdev* dev, unsigned int cmd, unsigned long raw_arg);
	void (*_destroy)(struct td_osdev*);
	void (*_bio_error)(struct td_osdev*, td_bio_ref);
};

int td_osdev_assign_name(const char *prefix, char* buffer, int size);
int td_osdev_dump_names(enum td_osdev_type t, char *buf, size_t len, uint32_t *count);
	

int td_osdev_init (struct td_osdev* dev, enum td_osdev_type,
		const char* name,
		int (*ioctl) (struct td_osdev*, unsigned int cmd, unsigned long raw_arg),
		void (*destructor) (struct td_osdev*)
		);

extern int td_osdev_register (struct td_osdev* dev);
extern int td_osdev_online (struct td_osdev* dev);
extern void td_osdev_offline (struct td_osdev* dev);
extern void td_osdev_unregister (struct td_osdev* dev);

extern void td_osdev_error_bio (td_bio_ref);

extern int td_osdev_list_iter(
		int (*action)(struct td_osdev *dev, void *data),
		void *data);

extern struct td_osdev *td_osdev_find (int (*is_it)(struct td_osdev*, void* data),
		void* data);

static inline void td_osdev_get(struct td_osdev *dev)
{
	atomic_inc(&dev->refcnt);
};

extern void __td_osdev_destroy(struct td_osdev *);
static inline void td_osdev_put (struct td_osdev *dev)
{
	if (!atomic_dec_and_test(&dev->refcnt))
		return;

	__td_osdev_destroy(dev);
}


static inline struct td_osdev* td_osdev_from_platform (
		struct platform_device *pdev)
{
	return container_of( pdev, struct td_osdev, pdevice);
}


/* locking the device mutex */

static inline void td_osdev_lock(struct td_osdev *dev)
{
	mutex_lock(&dev->mutex);
#ifdef CONFIG_TERADIMM_DEBUG_DEVICE_LOCK
	dev->mutex_holder = current;
#endif
}

static inline void td_osdev_unlock(struct td_osdev *dev)
{
#ifdef CONFIG_TERADIMM_DEBUG_DEVICE_LOCK
	dev->mutex_holder = NULL;
#endif
	mutex_unlock(&dev->mutex);
}

#ifdef CONFIG_TERADIMM_DEBUG_DEVICE_LOCK
#define WARN_DEVICE_UNLOCKED(dev) ({ \
               WARN_ON( dev->mutex_holder != current ); })
#define WARN_DEVICE_LOCKED(dev) ({ \
               WARN_ON( dev->mutex_holder == current ); })
#else
#define WARN_DEVICE_UNLOCKED(dev) ({ })
#define WARN_DEVICE_LOCKED(dev) ({ })
#endif

#endif
