/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2013 Diablo Technologies Inc. ("Diablo").  All       *
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
 *    Modified BSD License                                               *
 *                                                                       *
 *    If you have explicit permission from Diablo, then (1) use in       *
 *    source and binary forms, with or without modification; as well as  *
 *    (2) redistribution ONLY in binary form, with or without            *
 *    modification; are permitted provided that the following conditions *
 *    are met:                                                           *
 *                                                                       *
 *    * Redistributions in binary form must reproduce the above          *
 *    copyright notice, this list of conditions and the following        *
 *    disclaimer in the documentation and/or other materials provided    *
 *    with the distribution.                                             *
 *                                                                       *
 *    * Neither the name of the DIABLO nor the names of its contributors *
 *    may be used to endorse or promote products derived from this       *
 *    software without specific prior written permission.                *
 *                                                                       *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND             *
 *    CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,        *
 *    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF           *
 *    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE           *
 *    DISCLAIMED. IN NO EVENT SHALL DIABLO BE LIABLE FOR ANY DIRECT,     *
 *    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES *
 *    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR *
 *    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) *
 *    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN          *
 *    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR       *
 *    OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,     *
 *    EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                 *
 *                                                                       *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef _TERADIMM_KDEFN_H_
#define _TERADIMM_KDEFN_H_

/*
 * These must be defined before including any kernel/driver headers
 */
#define TERADIMM_NAME "teradimm"
#define KMSG_COMPONENT TERADIMM_NAME
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt


#ifdef SUPPRESS_KERNEL_WARNINGS
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpointer-arith"
#endif  /* SUPPRESS_KERNEL_WARNINGS */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#ifdef KABI__export_header
#include <linux/export.h>
#endif

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/delay.h>

#include <linux/bio.h>

#include <linux/types.h>
#include <linux/rbtree.h>

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ctype.h>

#include <linux/ata.h>
#include <scsi/scsi_cmnd.h> /*  needed for scsi cmnd size */
#include <scsi/scsi.h> /* scsi defines. */

#include <linux/err.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/wait.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/init.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/suspend.h>
#include <linux/completion.h>
#include <linux/highmem.h>
#include <linux/kthread.h>


#include <linux/device-mapper.h>    /*  for SECTOR_SHIFT */
#include <linux/file.h>
#include <linux/cpuset.h>
#include <linux/buffer_head.h>		/* for invalidate_bdev() */
#include <linux/writeback.h>

#include <linux/dmi.h>
#include <linux/acpi.h>
#include <linux/nls.h>


#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>
#include <acpi/actypes.h>

/*  older gendisk def. */
#ifndef disk_to_dev
#define disk_to_dev(disk)         (disk->driverfs_dev)
#define disk_to_kobj(disk)        (disk->kobj)
#else
#define disk_to_kobj(disk)        (disk_to_dev(disk)->kobj)
#endif

/* new_blk_dev_ops: Added #defines to make code cleaner in actual file. */
#ifdef KABI__new_blk_dev_ops
#define bdev_compat_fmode fmode_t
#define bdev_compat_dev block_device
#define bdev_compat_gdisk gendisk
#define bdev_compat_pdata_dev(bdev) bdev->bd_disk->private_data
#define bdev_compat_pdata_gdisk(disk) disk->private_data
#define bdev_compat_to_dev(bdev) bdev
#else
#define bdev_compat_fmode struct file*
#define bdev_compat_dev inode
#define bdev_compat_gdisk inode
#define bdev_compat_pdata_dev(bdev) bdev->i_bdev->bd_disk->private_data
#define bdev_compat_pdata_gdisk(disk) disk->i_bdev->bd_disk->private_data
#define bdev_compat_to_dev(bdev) bdev->i_bdev
#endif

#ifdef SUPPRESS_KERNEL_WARNINGS
#pragma GCC diagnostic pop
#endif  /* SUPPRESS_KERNEL_WARNINGS */

#define MODULE_PARAM(_type, _name, _default)            \
	static _type _name = _default;

#endif
