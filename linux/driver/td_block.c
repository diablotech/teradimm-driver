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

#include "td_kdefn.h"

#include "td_device.h"
#include "td_engine.h"
#include "td_eng_hal.h"
#include "td_osdev.h"
#include "td_biogrp.h"

#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/module.h>



/*
 * Request entry point for single device block requests
 */
#ifdef KABI__make_request_fn_returns_void
void td_device_make_request (struct request_queue *q, struct bio* bio)
{
	struct td_device *dev = td_device_from_os(q->queuedata);
	struct td_engine *eng = &dev->td_engine;

	(void)td_engine_queue_bio(eng, bio);
}
#else
int td_device_make_request (struct request_queue* q, struct bio* bio)
{
	struct td_device *dev = td_device_from_os(q->queuedata);
	struct td_engine *eng = &dev->td_engine;

	return td_engine_queue_bio(eng, bio);
}
#endif

void td_device_bio_error (struct td_osdev *odev, td_bio_ref bio)
{
	td_os_err(odev, "BIO: Error on bio %p\n", bio);
}

void td_raid_bio_error (struct td_osdev *odev, td_bio_ref bio)
{
	td_os_err(odev, "BIO: Error on bio %p\n", bio);
}

/*
 * Request entry point for raid device block requests
 */
#ifdef KABI__make_request_fn_returns_void
void td_raid_make_request (struct request_queue *q, struct bio* bio)
{
	struct td_raid *rdev = td_raid_from_os(q->queuedata);


	if ( rdev->ops->_request(rdev, bio) < 0) {
#if KABI__bio_endio == 3
		bio_endio(bio, 0, -EIO);
#else
		bio_endio(bio, -EIO);
#endif
	}
}
#else
int td_raid_make_request (struct request_queue* q, struct bio* bio)
{
	struct td_raid *rdev = td_raid_from_os(q->queuedata);
	
	if ( rdev->ops->_request(rdev, bio) < 0) {
#if KABI__bio_endio == 3
		bio_endio(bio, 0, -EIO);
#else
		bio_endio(bio, -EIO);
#endif
		return -EIO;
	}
	return 0;
}
#endif


/* This is a messy import from td_device.c */
static int __check_dev_bdisk (struct td_osdev* dev, void* data)
{
	return dev->disk && dev->disk == data;
}

/* locate the td_device based on it's block device */
struct td_osdev *td_osdev_from_bdevice(struct block_device *bdev)
{
	return td_osdev_find(__check_dev_bdisk, bdev->bd_disk);
}

static int td_device_block_open(struct bdev_compat_dev *bdev,
		bdev_compat_fmode mode)
{
	struct td_osdev *dev;

	dev = bdev_compat_pdata_dev(bdev);

	if (!dev)
		return -ENODEV;

	/*  device is held until td_device_block_release() */
	td_osdev_get(dev);
	atomic_inc(&dev->block_users);

	return 0;
}


#if   defined(KABI__block_ops_release_returns_int)
static int td_device_block_release(struct bdev_compat_gdisk *disk,
		bdev_compat_fmode mode)
#else
static void td_device_block_release(struct bdev_compat_gdisk *disk,
		bdev_compat_fmode mode)
#endif
{
	struct td_osdev *dev;

	dev = bdev_compat_pdata_gdisk(disk);

	atomic_dec(&dev->block_users);

	td_osdev_put(dev);

#if  defined(KABI__block_ops_release_returns_int)
	return 0;
#endif
}

static int td_device_block_ioctl(struct bdev_compat_dev *bdev,
		bdev_compat_fmode mode, unsigned int cmd,
		unsigned long raw_arg)
{
	struct td_osdev *dev;
	int rc;

	dev =  bdev_compat_pdata_dev(bdev);

	rc = dev->_ioctl(dev, cmd, raw_arg);
	if (rc == -ENOIOCTLCMD)
		return -EINVAL;
	return rc;
}

#ifdef CONFIG_COMPAT
static int td_device_block_compat_ioctl(struct bdev_compat_dev *bdev,
		bdev_compat_fmode mode, unsigned int cmd,
		unsigned long raw_arg)
{
	struct td_osdev *dev;
	int rc;


	dev =  bdev_compat_pdata_dev(bdev);
	rc = dev->_ioctl(dev, cmd, (unsigned long)compat_ptr(raw_arg));
	/* COMPAT_IOCTL is supposed to return ENOIOCTLCMD */
	return rc;
}
#endif

static struct block_device_operations td_device_block_fops = {
	.owner =	THIS_MODULE,
	.open =		td_device_block_open,
	.release =	td_device_block_release,
	.ioctl =	td_device_block_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = td_device_block_compat_ioctl,
#endif
};





#if defined QUEUE_FLAG_PLUGGED
static void td_device_queue_unplug(struct request_queue *q)
{
	/* struct dimsum_device *ds = q->queuedata; */

#ifdef queue_flag_clear_unlocked 
	queue_flag_clear_unlocked(QUEUE_FLAG_PLUGGED, q);
#else
	generic_unplug_device(q);
#endif
	/* blk_run_address_space(ds->dimsum_backing_file->f_mapping); */
}
#endif


int td_linux_block_create(struct td_osdev *dev)
{
	int rc;
	struct request_queue *queue;
	unsigned bio_sector_size = dev->block_params.bio_sector_size;
	unsigned hw_sector_size = dev->block_params.hw_sector_size;

	/* very simple sector size support */
	if (!bio_sector_size || bio_sector_size & 511 || bio_sector_size > 4096) {
		td_os_err(dev, "bio sector size of %u is not supported\n", bio_sector_size);
		return -EINVAL;
	}

	/* MetaData is reported here */
	if (hw_sector_size == 520)
		hw_sector_size = 512;
	if (!hw_sector_size || hw_sector_size & 511 || hw_sector_size > 4096) {
		td_os_err(dev, "hw sector size of %u is not supported\n", hw_sector_size);
		return -EINVAL;
	}

	td_os_notice(dev, " - Set capacity to %llu (%u bytes/sector)\n",
		dev->block_params.capacity, dev->block_params.hw_sector_size);

	/* create a new bio queue */
	queue = blk_alloc_queue(GFP_KERNEL);
	if (!queue) {
		td_os_err(dev, "Error allocating disk queue.\n");
		rc = -ENOMEM;
		goto error_alloc_queue;
	}

#ifdef QUEUE_FLAG_NONROT
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, queue);
#endif
	
	switch (dev->type) {
	case TD_OSDEV_DEVICE:
		blk_queue_make_request(queue, td_device_make_request);
		dev->_bio_error = td_device_bio_error;
		break;
	case TD_OSDEV_RAID:
		blk_queue_make_request(queue, td_raid_make_request);
		dev->_bio_error = td_raid_bio_error;
		break;
		
	default:
		td_os_err(dev, "Unkonwn OS Type, cannot register block request handler\n");
		goto error_config_queue;
	}
	queue->queuedata = dev;

#if defined QUEUE_FLAG_PLUGGED 
	queue->unplug_fn = td_device_queue_unplug;
#endif

	/* configure queue ordering */

	/* in QUEUE_ORDERED_DRAIN we will get BARRIERS after the queue has
	 * been drained. */
#if defined KABI__blk_queue_ordered

#if KABI__blk_queue_ordered == 2
	blk_queue_ordered(queue, QUEUE_ORDERED_DRAIN);
#elif KABI__blk_queue_ordered == 3
	blk_queue_ordered(queue, QUEUE_ORDERED_DRAIN, NULL);
#else
#error unhandled value of KABI__blk_queue_ordered
#endif

#elif defined KABI__blk_queue_flush
	/*
	 * blk_queue_ordered was replaced with blk_queue_flush 
	 * The default implementation is QUEUE_ORDERED_DRAIN
	 */
	blk_queue_flush(queue, 0);
#else
#error undefined KABI__blk_queue_flush or KABI__blk_queue_ordered
#endif

	/* max out the throttling */
#ifdef KABI__blk_queue_max_hw_sectors
	blk_queue_max_hw_sectors(queue, dev->block_params.bio_max_bytes/512);
#elif defined KABI__blk_queue_max_sectors
	blk_queue_max_sectors(queue, dev->block_params.bio_max_bytes/512);
#else
	td_os_err(dev, "No kernel API for maximum sectors\n");
#endif

#if defined KABI__blk_queue_max_segments
	blk_queue_max_segments(queue, BLK_MAX_SEGMENTS);
#elif defined KABI__blk_queue_max_phys_segments
	blk_queue_max_phys_segments(queue, MAX_SEGMENT_SIZE);
	blk_queue_max_hw_segments(queue, MAX_SEGMENT_SIZE);
#else
	td_os_err(dev, "No kernel API for maximum segments\n");
#endif

	blk_queue_max_segment_size(queue, dev->block_params.bio_max_bytes);

	blk_queue_bounce_limit(queue, BLK_BOUNCE_ANY);

	/* setup paged based access */
	td_os_info(dev, "Set queue physical block size to %u\n", hw_sector_size);
#ifdef KABI__blk_queue_physical_block_size
	blk_queue_physical_block_size(queue, hw_sector_size);
#elif defined KABI__blk_queue_hardsect_size
	blk_queue_hardsect_size(queue, hw_sector_size);
#else
	td_os_err(dev, "No kernel API for physical sector size\n");
#endif

#ifdef KABI__blk_queue_logical_block_size
	td_os_info(dev, "Set queue logical block size to %u\n", bio_sector_size);
	blk_queue_logical_block_size(queue, bio_sector_size);
#else
	td_os_err(dev, "No kernel API for logical block size\n");
#endif
#ifdef KABI__blk_queue_io_min
	td_os_info(dev, "Set queue io_min to %u\n", bio_sector_size);
	blk_queue_io_min(queue, bio_sector_size);
#else
	td_os_err(dev, "No kernel API for minimum IO size\n");
#endif
#ifdef KABI__blk_queue_io_opt
	td_os_info(dev, "Set queue io_opt to %u\n", dev->block_params.bio_max_bytes);
	blk_queue_io_opt(queue,  dev->block_params.bio_max_bytes);
#else
	td_os_err(dev, "No kernel API for optimal IO size\n");
#endif

	if (dev->block_params.discard)
	{
		int did_something = 0;
#if defined KABI__blk_queue_discard_granularity
		queue->limits.discard_granularity = bio_sector_size;
		did_something++;
#endif
#ifdef KABI__blk_queue_max_discard_sectors
		/* 0xFFFF (max sector size of chunk on trim) * 64  * # SSD */
		blk_queue_max_discard_sectors(queue, TD_MAX_DISCARD_LBA_COUNT * 2);
		did_something++;
#endif
#ifdef KABI__blk_queue_discard_zeroes_data
		queue->limits.discard_zeroes_data = 1;
		did_something++;
#endif
#ifdef KABI__queue_flag_set_unlocked
		queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, queue);
		did_something++;
#endif
		/* Maybe some day.. But not today. 
		queue_flag_set_unlocked(QUEUE_FLAG_SECDISCARD, queue);
		*/
		if (did_something)
			td_os_info(dev, "Enabling discard support\n");
		else
			td_os_notice(dev, "No kernel API for discard support\n");
	} else {
		td_os_info(dev, "No DISCARD support enabled\n");
	}

	/*  assign */
	dev->queue = queue;

	return 0;

error_config_queue:
	blk_cleanup_queue(dev->queue);
	dev->queue = NULL;

error_alloc_queue:
	return rc;
}

#ifdef CONFIG_PM
static void td_osdev_os_platform_release(struct device *dev)
{
	struct platform_device *pdev = container_of(dev, struct
			platform_device, dev);
	struct td_osdev *tdev = td_osdev_from_platform(pdev);
	memset(&tdev->pdevice, 0, sizeof(tdev->pdevice));
	/* the platform_device will be freed with the td_device. */
}

static int td_osdev_os_register_platform_device(struct td_osdev *dev)
{
	struct platform_device *pdev = &dev->pdevice;
	int rc;

	WARN_DEVICE_UNLOCKED(dev);

	memset(pdev, 0, sizeof(*pdev));

	pdev->name = "MCS";
	pdev->id = dev->unique_id;
	pdev->dev.release = &td_osdev_os_platform_release;


	rc = platform_device_register(pdev);

	if (rc) {
		td_os_err(dev, "Failed to register platform device\n");
		goto error_plat_register;
	}

error_plat_register:
	return rc;
}

static void td_osdev_os_unregister_platform_device(struct td_osdev *dev)
{
	WARN_DEVICE_UNLOCKED(dev);
	platform_device_unregister(&dev->pdevice);
}

#else
#define td_osdev_os_register_platform_device
#define td_osdev_os_unregister_platform_device
#endif

void td_linux_block_destroy(struct td_osdev *dev)
{
	if (dev->queue) {
		blk_cleanup_queue(dev->queue);
		dev->queue = NULL;
	}
}

int td_linux_block_register(struct td_osdev *dev, int major)
{
	int rc;
	int first_minor;
	struct gendisk *disk;

	/* WARN_TD_DEVICE_UNLOCKED(dev); */
	WARN_ON(!dev->queue);

	if (!dev->block_params.capacity) {
		td_os_err(dev, "Cannot create a block device until "
				"capacity is known\n");
		rc = -EINVAL;
		goto error_capacity;
	}

	/* generate a disk */
	disk = dev->disk = alloc_disk(TD_DEVICE_PARTITIONS_MAX);
	if (!disk) {
		td_os_err(dev, "Error allocating disk structure\n");
		rc = -ENOMEM;
		goto error_alloc_disk;
	}

	/* figure out where the first minor number will be */
	first_minor = dev->unique_id * TD_DEVICE_PARTITIONS_MAX;

	disk->major		= major;
	disk->first_minor	= first_minor;
	disk->minors		= TD_DEVICE_PARTITIONS_MAX;
	disk->fops		= &td_device_block_fops;
	disk->private_data	= dev;
	disk->queue		= dev->queue;
	strncpy(disk->disk_name, dev->name, sizeof(disk->disk_name)-1);

	/* set the disk capacity */
	td_os_info(dev, "Setting disk capacity to %llu bytes (%llu sectors)\n",
			dev->block_params.capacity, dev->block_params.capacity>>SECTOR_SHIFT);
	set_capacity(disk, dev->block_params.capacity >> SECTOR_SHIFT);

	add_disk(disk);

#ifdef CONFIG_PM
	rc = td_osdev_os_register_platform_device(dev);
	if (rc) {
		pr_err("Failed to platform device err=%d.\n", rc);
		goto error_plat_dev;
	}
#endif

	return 0;

#ifdef CONFIG_PM
error_plat_dev:
#endif
	dev->disk = NULL;
	del_gendisk(disk);
	put_disk(disk);
error_alloc_disk:	
error_capacity:
	return rc;
}

void td_linux_block_unregister(struct td_osdev *dev)
{
	WARN_DEVICE_UNLOCKED(dev);
	WARN_ON(atomic_read(&dev->block_users));

	if (dev->disk) {
		del_gendisk(dev->disk);
		put_disk(dev->disk);
		dev->disk = NULL;
	}
#ifdef CONFIG_PM
	td_osdev_os_unregister_platform_device(dev);
#endif
}



