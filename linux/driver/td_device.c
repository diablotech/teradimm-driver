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
#include "td_sgio.h"
#include "td_memspace.h"
#include "td_osdev.h"
#include "td_mapper.h"
#include "td_worker.h"

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

#define TD_DISCOVERY_TARGET_NONE        0
#define TD_DISCPVERY_TARGET_RUNNING     1
#define TD_DISCOVERY_TARGET_ONLINE      2
#define TD_DISCOVERY_TARGET_RAID        3

static uint td_scan_enable = 1;
static ulong td_scan_tm = TD_TRACE_DEFAULT_MASK;  /*  ~(TR_STATUS);  STATUS are too verbose by default */
static uint td_magic_speed = 1;
static uint td_magic_type = 1;
static uint td_scan_wait_sec = 20;
static ulong td_external_train = 0;
static uint td_dev_per_group = 2;
static int td_default_dg_nice = -19;
static char* td_discovery = "bios";
static int td_discovery_target = TD_DISCOVERY_TARGET_RAID;
static uint td_discovery_threaded = 1;

module_param_named(scan, td_scan_enable, uint, 0444);
module_param_named(scan_wait_sec, td_scan_wait_sec, uint, 0444);
module_param_named(scan_tracemask, td_scan_tm, ulong, 0444);
module_param_named(magic_speed, td_magic_speed, uint, 0444);
module_param_named(magic_type, td_magic_type, uint, 0444);
module_param_named(external_train, td_external_train, ulong, 0444);
module_param_named(dev_per_group, td_dev_per_group, uint, 0444);
module_param_named(default_devgroup_nice, td_default_dg_nice, int, 0444);

module_param_named(discovery, td_discovery, charp, 0444);
module_param_named(discovery_target, td_discovery_target, int, 0444);
module_param_named(discovery_threaded, td_discovery_threaded, int, 0444);

#ifdef CONFIG_TERADIMM_STATIC_NODEMAP
uint td_socketmap[MAX_NUMNODES];
int td_socketmap_size;

module_param_array_named(socketmap, td_socketmap, uint, &td_socketmap_size, 0444);
MODULE_PARM_DESC(socketmap, "Static socket -> CPU map");
#endif

atomic_t        td_discovery_in_progress;


MODULE_PARM_DESC(scan,
		"Scan the ACPI & SMBIOS table for device information and "
		"add them automatically (0 disable, 1 enable/default, "
		"2 create ZAP devices, 3 assume all devices are MegaDIMM, "
		"4 assume all devices are TeraDIMM");

MODULE_PARM_DESC(scan_tracemask, "Default Trace mask for UEFI scan");
MODULE_PARM_DESC(scan_wait_sec,  "Seconds wait for device ready scan");

MODULE_PARM_DESC(magic_speed,
		"Assume ram speed is 1333 (0 disable, 1 enable/default)");

MODULE_PARM_DESC(magic_type,
		"Assume type is TDL if blank if type is not detected "
		"(0 disable, 1 enable/default) "
		"NOTE: scan parameter takes precedence");

MODULE_PARM_DESC(external_train,
		"Symbol for external training function to do MCS training");

MODULE_PARM_DESC(dev_per_group,
		"How many device to place in one group at load time.");

static struct mutex td_device_list_mutex; /**< prevents races with/between creates */

static struct semaphore td_discovery_sema; /**< prevents races with/between creates */

/*! Used by td_dev_debug() macro. */
int td_device_debug = 1;

/* Forward declartions - we give these functions to the osdev API */
void td_device_destroy (struct td_osdev *odev);
int td_device_ioctl(struct td_osdev* dev, unsigned int cmd,
		unsigned long raw_arg);

extern int __td_osdev_unique_id_from_name(const char *name);


int td_dev_state_wait(struct td_device *dev,
		enum td_device_state_type wanted_state,
		int timeout)
{
	int rc;

	if (dev->td_state == wanted_state)
		return 0;

	rc = wait_for_completion_interruptible_timeout(
			&dev->td_state_change_completion,
			timeout);
	if (rc <= 0) {
		/* Don't return restartsys to user. */
		if (rc == -ERESTARTSYS) {
			td_eng_debug(td_device_engine(dev), "TIMEOUT = %d, -ERESTARTSYS\n", rc);
			WARN_ON(1);
			rc = -EAGAIN;
		} else if (rc == 0) {
			td_eng_debug(td_device_engine(dev), "TIMEOUT = 0, complete timeout\n");
			rc = -ETIMEDOUT;
		}

		goto error_completion;
	}

	if (dev->td_state == wanted_state)
		rc = 0;
	else
		rc = -EIO;

error_completion:
	return rc;
}



/** allocate, initialize, and return a new device with refcnt=1 */
static struct td_device *__td_device_create(
		const char *name, const char *slot_name,
		uint64_t phys_mem_base, uint64_t phys_mem_size,
		uint64_t avoid_mask, uint32_t irq_num, uint16_t memspeed,
		uint16_t cpu_socket)
{
	int rc;
	struct td_device *dev;

	rc = td_mapper_check_memory_mapping(name, phys_mem_base, phys_mem_size);
	if (rc)
		goto error_args;

	rc = -ENOMEM;
	dev = kzalloc_node(sizeof(*dev), GFP_KERNEL, cpu_socket);
	if (!dev)
		goto error_alloc;

	rc = td_osdev_init(&dev->os, TD_OSDEV_DEVICE, name, td_device_ioctl, td_device_destroy);
	if (rc)
		goto error_osinit;

	strncpy(dev->td_slot, slot_name, TD_DEVICE_SLOT_NAME_MAX);

	dev->td_irq = irq_num;
	dev->td_memspeed = memspeed;
	dev->td_cpu_socket = cpu_socket;

	td_os_info(&dev->os, "Device %s is found in %s\n",
			name, (dev->td_slot ? : "(unknown)"));

	rc = td_mapper_init(&dev->td_mapper, name, phys_mem_base, phys_mem_size, avoid_mask);
	if (unlikely(rc))
		goto error_mapper;

	rc = td_engine_init(&dev->td_engine, dev);
	if (rc)
		goto error_engine;

	td_device_enter_state(dev, CREATED);

	return dev;
error_engine:
	td_mapper_cleanup(&dev->td_mapper);
error_mapper:
error_osinit:
	kfree(dev);
error_alloc:
error_args:
	return ERR_PTR(rc);
}

static void __td_device_destroy (struct td_device *dev)
{
	WARN_ON(dev->td_devgroup);

	td_mapper_cleanup(&dev->td_mapper);

	td_engine_exit(&dev->td_engine);

	kfree(dev);
}
void td_device_destroy (struct td_osdev *dev)
{
	__td_device_destroy(td_device_from_os(dev));
}

#define VALID_CPU(cpu) ((cpu)>=0 && (cpu)<(MAX_CPU_NUMBER))

/** 
 * \brief find a cpu on a socket within a range 
 * 
 * @param socket    - searching for a cpu on this socket
 * @param start_cpu - lowest acceptable CPU number
 * @param end_cpu   - highest acceptable CPU number
 * @param dir       - direction (+1 or -1)
 * @return non-negative CPU number, or negative error
 *
 * dir of -1 searches from end_cpu to start_cpu
 * dir of +1 searches from start_cpu to end_cpu
 */
static int __td_device_select_cpu_on_socket(uint16_t socket,
		uint start_cpu, uint end_cpu, int dir)
{
#ifdef CONFIG_TERADIMM_STATIC_NODEMAP
	if (dir != -1)
		return -EINVAL;
	
		
	if (socket < td_socketmap_size)
		return min(end_cpu,td_socketmap[socket]);
	
	/* This is in case we were not given a socket map, we
	 * will assume CPU 1 is on socket 0 */
	if (socket == 0 && td_socketmap_size == 0) {
		pr_warn("No socketmap, socket 0, returning default 1\n");
		return 1;
	}
#else
	uint cpu;

	switch (dir) {
	case 1:
		/* walking forward */
		cpu = start_cpu;
		break;
	case -1:
		/* walking backwards */
		cpu = end_cpu;
		break;
	default:
		/* not supported */
		return -EINVAL;
	}

	for (; cpu >= start_cpu && cpu <= end_cpu; cpu += dir) {

		if (socket != topology_physical_package_id(cpu))
			continue;

		return (int)cpu;
	}
#endif
	return -ENODEV;
}

struct td_cpu_distribution_state {
	struct td_cpu_distribution_socket {
		uint using_cpu;
		uint dev_count;
	} socket[MAX_NUMNODES];

	uint fallback_using_zero;
} td_cpu_distribution_state;

static inline void td_cpu_distribution_state_reset(void)
{
	memset(&td_cpu_distribution_state, 0, sizeof(td_cpu_distribution_state));
}

static int td_device_select_cpu_on_socket(uint16_t socket)
{
	struct td_cpu_distribution_state *state = &td_cpu_distribution_state;
	struct td_cpu_distribution_socket *s;
	uint cpu = 0, end_cpu = MAX_CPU_NUMBER-1;

	/* first we have to figure out where this device will run */

	s = state->socket + socket;

	if (s->using_cpu) {
		/* this socket was already in use, try to continue on the same CPU
		 *
		 * NOTE: this assumes CPU 0 is never used, except as a
		 * fallback if nothing else can be found
		 */

		if (s->dev_count < td_dev_per_group) {
			/* can reuse a group */
			cpu = s->using_cpu;
			s->dev_count ++;

		} else {
			/* this group is full, find another */
			cpu = 0;
			end_cpu = s->using_cpu - 1;

			/* reset socket state */
			s->using_cpu = 0;
			s->dev_count = 0;
		}
	}

	if (!cpu) {
		/* need to find another cpu to use
		 *
		 * scan in reverse order from MAX_CPU_NUMBER-1 to 0, looking for a
		 * matching CPU on this socket */
		cpu = __td_device_select_cpu_on_socket(socket, 0, end_cpu, -1);
		if (likely (VALID_CPU(cpu))) {

			s->using_cpu = cpu;
			s->dev_count = 1;

		} else {
			/* fallback to 0 */
			state->fallback_using_zero ++;
			cpu = 0;
		}
	}

	return cpu;
}

/* helper that creates a group on a cpu or reuses one if one exists */
static struct td_devgroup *
td_device_group_on_node(const char *devname, const char *source, int socket)
{
	int ret;
	char dg_name[32];
	struct td_devgroup *dg;

	/* try to get a group, or if one doesn't exist create one */

	sprintf(dg_name, "td-node-%d", socket);

	dg = td_devgroup_get_by_node(socket);
	if (!dg) {
		ret = td_devgroup_create(dg_name, socket, td_default_dg_nice);
		switch (ret) {
		case 0:            /* We created it successfully */
		case -EEXIST:      /* We failed, but it was created elsewhere */
			break;
		default:
			pr_err("%s ERROR: Could not create group "
					"for device %s: %d\n",
					source, devname, ret);
			return NULL;
		}
		dg = td_devgroup_get_by_name(dg_name);
		if (!dg)
			return NULL;
	}

	return dg;
}



int td_device_train(struct td_device *dev)
{
	void (*td_train)(void* ptr, uint64_t size, const char* name) =
		(void*)td_external_train;
	void *virt;

	if (!dev->os.name)
		return -ENODEV;
	
	if (td_external_train == 0) {
		td_eng_info(td_device_engine(dev), "External training disabled.\n");
		return 0;
	}
	if (!IS_KERNEL_SYMBOL(td_external_train))
		return -ENOTSUPP;

	td_eng_info(td_device_engine(dev), "Calling external training symbol: %p\n",
			td_train);

	virt = td_mapper_map_wb(&dev->td_mapper, 0, TD_IOREMAP_SIZE);

	td_train(virt, TD_IOREMAP_SIZE, dev->os.name);

	/* TODO: Test if the device is trained */

	td_mapper_unmap(&dev->td_mapper, virt);

	return 0;
}

#ifdef CONFIG_TERADIMM_ACPI_ASL
static int td_device_create_and_online (void* arg)
{
	struct td_device_info *info = arg;
	const char *source = "PROBE";
	int ret = 0;
	struct td_devgroup *dg;
	struct td_device *dev;
	int loop_count = 0;

	if (info->source)
		source = info->source;

	pr_info("%s: Starting device \"%s\"\n", source, info->dev_name);

#ifdef CONFIG_TERADIMM_MEGADIMM
	/* HACK: convert md* devices to zap* devices if so requested */
	if (td_scan_enable == 2 && !strncmp(info->dev_name, "md", 2)) {
		char md_name[8];

		if (strlen(info->dev_name) != 3)
			return -EINVAL;

		strncpy(md_name, "zap", 3);
		md_name[3] = info->dev_name[2];
		md_name[4] = 0;

		memcpy(info->dev_name, md_name, 5);
	}

#endif

	ret = -ENODEV;
	dg = td_device_group_on_node(source, info->dev_name, info->uid.cpu_socket);
	if (!dg)
		goto exit_nogrp;

	/* create device.. */
	pr_info("%s create device '%s' with base=0x%llx and size=0x%llx in group '%s'\n",
			source, info->dev_name, info->offset, info->size, dg->dg_name);
	ret = td_device_create(info->dev_name, info->bank_locator, info->offset,
			info->size, info->avoid_mask, info->irq, info->memspeed, info->uid.cpu_socket);
	if (ret) {
		pr_err("%s ERROR: Can't create device: error %d\n", source, ret);
		goto exit_nocreate;
	}

	dev = td_device_get(info->dev_name);
	ret = -ENODEV;
	if (!dev) {
		pr_err("%s ERROR: Can't find device we created\n", source);
		goto exit_nodev;
	}

	td_trace_set_mask(&td_device_engine(dev)->td_trace,
				td_scan_tm);

	ret = td_device_train(dev);
	if (ret && ret != -ENOTSUPP) {
		pr_err("%s ERROR: MCS training failed: %d\n", source, ret);
		goto exit_train;
	}

	/* Attach. */
	td_device_lock(dev);
	ret = td_device_attach(dev, dg->dg_name);

	if (ret) {
		pr_err("%s ERROR: device attach failed: %d\n", source, ret);
		goto exit_notattached;
	}

	pr_info("%s waiting for %s to be RUNNING.\n", source,
					info->dev_name);

	ret = -ETIMEDOUT;
	do {
		if (loop_count++ > td_scan_wait_sec) {
			td_eng_err(td_device_engine(dev),
					"Error: device didn't reach RUNNING.\n");
			ret = -EIO;
			goto exit_notonline;
			break;
		}
		ret = td_run_state_wait_ms(td_device_engine(dev), RUNNING,1000);
		switch (ret) {
		case 0:
			/* Our wanted condition */
			break;
		case -ETIMEDOUT:
		case -EAGAIN:
			/* We'll loop again */
			break;
		case -EIO:
			td_eng_err(td_device_engine(dev),
				"Error: device didn't reach RUNNING: %d\n",
					ret);
			goto exit_notonline;
			break;
		}
	} while (ret != 0);

	ret = td_device_go_online(dev);
	if(ret) {
		td_eng_err(td_device_engine(dev),
				"Error: device online failed: %d\n", ret);
		goto exit_notonline;
	}

	/* Everything was good */
	pr_info("%s put device %s online\n", source, info->dev_name);

exit_notonline:
exit_notattached:
	td_device_unlock(dev);
exit_train:
	/* If we have a device, we took a reference to it, now
	 * we are done
	 */
	td_device_put(dev);
exit_nodev:
exit_nocreate:
	td_devgroup_put(dg);
exit_nogrp:
	return ret;
}

static int td_device_probe_callback (struct td_device_info *info, void *opaque)
{
	struct task_struct * worker;
	int cpu;

	cpu = td_device_select_cpu_on_socket(info->uid.cpu_socket);

	info->use_cpu = cpu;

	worker = kthread_create(td_device_create_and_online,
			info, "td/%s-%s", info->source, info->dev_name);

	if (cpu != 0) kthread_bind(worker, cpu);

	wake_up_process(worker);
	schedule();

	return 0;
}
#endif

struct td_discovery_thread_args {
	struct td_discovered_info* dev_info;
	char dev_name[8];
};

static int td_assign_new_name(const struct td_discovered_info* dev_info,
		char* buffer, int size)
{
	const char* dev_prefix = "UNKNOWN";
	switch (dev_info->spd_type) {
		default:
			pr_info("SPD Type %d, assuming TeraDIMM\n", dev_info->spd_type);
		case 1:
			dev_prefix = "td";
			break;
		case 0:
			dev_prefix = "md";
			break;
	}
printk("Assigning new name for %p with prefix '%s'\n", dev_info, dev_prefix);
	return td_osdev_assign_name(dev_prefix, buffer, size);
};

int __td_device_create_discovered (void* data)
{
	struct td_discovery_thread_args *args = data;
	struct td_discovered_info *dev_info = args->dev_info;
	const char* dev_name = args->dev_name;
	struct td_devgroup *dg;
	struct td_device *dev;
	int ret;
	int loop_count = 0;

	ret = -ENODEV;
	dg = td_device_group_on_node(args->dev_name, dev_info->source, dev_info->socket);
	if (!dg)
		goto exit_nogrp;

	/* create device.. */
	pr_info("%s create device '%s' with base=0x%llx and size=0x%llx in group '%s'\n",
			dev_info->source, dev_name,
			dev_info->mem_base, dev_info->mem_size, dg->dg_name);
	ret = td_device_create(dev_name, dev_info->bank_locator,
			dev_info->mem_base, dev_info->mem_size,
			dev_info->mem_avoid_mask, dev_info->irq, dev_info->mem_speed,
			dev_info->socket);

	/* And now we are created, we release the mutex */
	atomic_dec(&td_discovery_in_progress);
	up(&td_discovery_sema);

	if (ret) {
		pr_err("%s ERROR: Can't create device: error %d\n",
				dev_info->source, ret);
		goto exit_nocreate;
	}

	dev = td_device_get(dev_name);
	ret = -ENODEV;
	if (!dev) {
		pr_err("%s ERROR: Can't find device we created\n", dev_info->source);
		goto exit_nodev;
	}
	td_trace_set_mask(&td_device_engine(dev)->td_trace,
				td_scan_tm);
	
	td_device_train(dev);

	ret = 0;

	if (ret) {
		pr_err("%s ERROR: MCS training failed: %d\n", dev_info->source, ret);
		goto exit_train;
	}


	/* Attach. */
	td_device_lock(dev);
	ret = td_device_attach(dev, dg->dg_name);

	if (ret) {
		pr_err("%s ERROR: device attach failed: %d\n", dev_info->source, ret);
		goto exit_notattached;
	}

	pr_info("%s waiting for %s to be RUNNING.\n", dev_info->source,
					dev_name);

	ret = -ETIMEDOUT;
	do {
		if (loop_count++ > td_scan_wait_sec) {
			td_eng_err(td_device_engine(dev),
					"Error: device didn't reach RUNNING.\n");
			ret = -EIO;
			goto exit_notonline;
			break;
		}
		ret = td_run_state_wait_ms(td_device_engine(dev), RUNNING,1000);
		switch (ret) {
		case 0:
			/* Our wanted condition */
			break;
		case -ETIMEDOUT:
		case -EAGAIN:
			/* We'll loop again */
			break;
		case -EIO:
			td_eng_err(td_device_engine(dev),
				"Error: device didn't reach RUNNING: %d\n",
					ret);
			goto exit_notonline;
			break;
		}
	} while (ret != 0);

	switch (td_discovery_target) {
	case TD_DISCOVERY_TARGET_RAID:
		if (td_eng_conf_hw_var_get(td_device_engine(dev), RAID_PAGE) ) {
			/* We check for RAID info in params page 13, and let RAID
			* handle it if there is */
			struct td_ucmd *ucmd = td_ucmd_alloc(TERADIMM_DATA_BUF_SIZE);
			if (ucmd) {
				int rc;
				struct td_engine *eng = td_device_engine(dev);

				ucmd->ioctl.data_len_from_device = TERADIMM_DATA_BUF_SIZE;
				rc = td_eng_cmdgen(eng, get_params, ucmd->ioctl.cmd, 13);
				WARN_ON(rc < 0);

				rc = td_ucmd_run(ucmd, eng);
				td_eng_info(eng, "RAID get_params %d [0x%02X]\n",
						rc, ucmd->ioctl.result & 0x00ff);

				/*
				* Just ignore any error, it will fall through to
				* normal online  mode
				*/
				(void) td_raid_discover_device(dev, ucmd->data_virt);

				td_ucmd_put(ucmd);
			}

			if (td_device_check_state(dev, RAID_MEMBER) ) {
				pr_info("%s put device %s into raid %s\n",
						dev_info->source, dev_name,
						td_raid_name(dev->td_raid));
				break;
			}
		}
		/* Fall through, just normal device online */

	case TD_DISCOVERY_TARGET_ONLINE:

		ret = td_device_go_online(dev);
		if (! ret) {
			/* Everything was good */
			pr_info("%s put device %s online\n", dev_info->source, dev_name);
			break;
		}

		td_eng_err(td_device_engine(dev),
					"Error: device online failed: %d\n", ret);

		/* Fall through */
	default:
		pr_info("%s created device %s\n", dev_info->source, dev_name);
	}



exit_notonline:
exit_notattached:
	td_device_unlock(dev);
exit_train:
	/* If we have a device, we took a reference to it, now
	 * we are done
	 */
	td_device_put(dev);

exit_nodev:
exit_nocreate:
	td_devgroup_put(dg);
exit_nogrp:
	dev_info->done(dev_info);
	kfree(args);

	return ret;
}

int td_device_create_discovered (struct td_discovered_info *dev_info, void* opaque)
{
	struct task_struct * worker;
	struct td_discovery_thread_args* args;
	int cpu;
	

	args = kmalloc( sizeof(*args), GFP_KERNEL);
	if (! args) {
		pr_err("%s ERROR: Couldn't allocate memory for new device\n",
				dev_info->source);
		return -ENOMEM;
	}

	down(&td_discovery_sema);

	/* Find a name */
	args->dev_info = dev_info;
	td_assign_new_name(dev_info, args->dev_name, sizeof(args->dev_name));

	cpu = td_device_select_cpu_on_socket(dev_info->socket);

#ifndef CONFIG_TERADIMM_ALLOW_UNBOUND_DIMM
	if (cpu == 0) {
		pr_err("Couldn't find CPU for device on socket %u, not creating device \'%s\"\n",
				dev_info->socket, args->dev_name);
		dev_info->done(dev_info);
		kfree(args);
		return 0;
	}
#endif

	atomic_inc(&td_discovery_in_progress);

	switch (td_discovery_threaded) {
	case 0:
		__td_device_create_discovered(args);
		break;
	case 1:
	default:
		worker = kthread_create(__td_device_create_discovered,
				args, "td/%s-%s", dev_info->source, args->dev_name);

		if (cpu != 0) kthread_bind(worker, cpu);

		wake_up_process(worker);
		schedule();
		break;
	}
	
	return 0;
}

/* ---- init/exit ---- */

int __init td_device_init(void)
{
	char* next_discovery = td_discovery;

#ifdef CONFIG_TERADIMM_STATIC_NODEMAP
	if (td_socketmap_size == 0) {
		pr_err("No socketmap specified; aborting module load\n");
		return -EINVAL;
	}
#endif
	mutex_init(&td_device_list_mutex);
	sema_init(&td_discovery_sema, 1);

	atomic_set(&td_discovery_in_progress, 0);
	td_cpu_distribution_state_reset();

	while (*next_discovery) {
		int left = 0;
		pr_info("Performing discovery: %s\n", next_discovery);
		/*
		* This is a bit of a hack...
		* We want to do multiple discoveries, but they sometimes
		* try to do the same devices...  There are checks on the
		* memory ranges, but they screw up ordering...
		* Lets just wait for previous discovery to finish
		*/
		while ( (left = atomic_read(&td_discovery_in_progress)) > 0) {
			pr_warn("Waiting for previous discovery; (%u left)\n",
					left);
			ssleep(1);
		}

		if (strncmp(next_discovery, "bios", 4) == 0) {
			if (td_bios_scan() ) {
				pr_err("Couldn't scan BIOS for devices\n");
			} else {
				if (td_discovery_target > TD_DISCOVERY_TARGET_NONE)
					td_bios_enumerate(td_device_create_discovered, NULL);
				else
					td_bios_enumerate(NULL, NULL);
			}
		}

#ifdef CONFIG_TERADIMM_ACPI_ASL
		else if (strncmp(next_discovery, "asl", 3) == 0) {
			struct td_dev_callback_opts opts;
			opts.td_magic_speed = td_magic_speed;
			opts.td_magic_type = td_magic_type;
			opts.td_scan_enable = td_scan_enable;

			td_asl_scan(&opts);
			td_asl_print_table();

			if (td_scan_enable)
				td_asl_enumerate(td_device_probe_callback, NULL);
		}
#endif
		else {
			pr_err("Unknown discovery method: %s\n\n", next_discovery);
		}

		for (next_discovery++; *next_discovery; next_discovery++) {
			if (*next_discovery == ',') {
				next_discovery += 1;
				break;
			}

		}
	}

	return 0;
}


void td_device_exit(void)
{

	td_bios_cleanup();

#ifdef CONFIG_TERADIMM_ACPI_ASL
	td_asl_cleanup();
#endif

	return;
}

static int __check_device_name(struct td_osdev *dev, void* data)
{
	if (dev->type != TD_OSDEV_DEVICE)
		return 0;
	return strncmp(dev->name, data, TD_DEVICE_NAME_MAX) == 0;
}

struct td_device * td_device_get(const char* name)
{
	struct td_osdev *odev = td_osdev_find(__check_device_name, (void*)name);
	if (odev)
		return td_device_from_os(odev);
	return NULL;
}

void td_device_put (struct td_device *dev)
{
	td_osdev_put(&dev->os);
}


struct td_device_check_exists_state {
	const char      *name;
	uint64_t        base;
	uint64_t        size;
};

static int __iter_device_check_exists(struct td_osdev *dev, void* data)
{
	struct td_device_check_exists_state *st = data;

	if (strncmp(st->name, dev->name, TD_DEVICE_NAME_MAX) == 0) {
		pr_err("Device '%s' already exists.\n", st->name);
		return -EEXIST;
	}

	if (dev->type == TD_OSDEV_DEVICE) {
		/* the base/end we're looking for is passed in as uint64_t[2] */
		struct td_device *tdev = td_device_from_os(dev);

		if (st->base >= tdev->td_mapper.phys_base) {
			/* our base is higher than theirs, we need to be
			 * higher than their size
			 */
			if (st->base < tdev->td_mapper.phys_base + tdev->td_mapper.phys_size) {
				pr_err("Device '%s' base overlaps '%s'\n", st->name, dev->name);
				return -ERANGE;
			}
		} else {
			/* our base is lower than theirs, we need our end
			 * to be below their start
			 */
			if (st->base + st->size > tdev->td_mapper.phys_base) {
				pr_err("Device '%s' end overlaps '%s'\n", st->name, dev->name);
				return -ERANGE;
			}
		}
	}
	
	return 0;
}



int td_device_create(const char *name, const char *slot_name,
		uint64_t phys_mem_base, uint64_t phys_mem_size,
		uint64_t avoid_mask, uint32_t irq_num, uint16_t memspeed,
		uint16_t cpu_socket)
{
	int rc;
	struct td_device *dev = NULL;

	mutex_lock(&td_device_list_mutex);

	if (1) {
		struct td_device_check_exists_state check_state = {
			name, phys_mem_base, phys_mem_size
		};
		rc = td_osdev_list_iter(__iter_device_check_exists, &check_state);
		if (rc)
			goto error_create;
	}

	/* allocate/create a new device */
	dev = __td_device_create(name, slot_name, phys_mem_base, phys_mem_size,
			avoid_mask, irq_num, memspeed, cpu_socket);
	if (IS_ERR(dev)) {
		rc = PTR_ERR(dev);
		pr_err("Failed to create device '%s', err=%d.\n", name, rc);
		goto error_create;
	}

	td_device_lock(dev);

	/*
	 * Our device is now crated and safe, so we can unlock this
	 * We need to unlock it before the CHAR device is registered
	 * or we have a lock (in device char layer) that get's locked after
	 * this (inour misc_register(), or before this (in sys_open,
	 * td_device_char_open, td_device_get_for_inode)
	 */
	mutex_unlock(&td_device_list_mutex);

	init_completion(&dev->td_state_change_completion);

	rc = td_osdev_register(&dev->os);
	if (rc) {
		pr_err("Failed to register OS device '%s', err=%d.\n",
				name, rc);
		goto error_osdev;
	}

	td_device_unlock(dev);

	__module_get(THIS_MODULE);

	return 0;

error_osdev:
	td_device_unlock(dev);
	td_device_put(dev);
error_create:
	mutex_unlock(&td_device_list_mutex);

	return rc;
}

int td_device_delete(const char *name)
{
	int rc;
	struct td_device *dev;

	mutex_lock(&td_device_list_mutex);

	rc = -ENOENT;
	dev = td_device_get(name);
	if (!dev)
		goto bail_no_ref;

	td_device_lock(dev);

	/* check if the device is participating in a group */
	rc = -EBUSY;
	if (dev->td_devgroup)
		goto bail_with_mutex;

	/* check if the device is bing used (mounted/open/etc) */
	rc = -EBUSY;
	if (atomic_read(&dev->os.control_users)
			|| atomic_read(&dev->os.block_users)) {
		td_eng_err(td_device_engine(dev), "device \"%s\" in use.\n", dev->os.name);
		goto bail_with_mutex;
	}
	
	if (td_device_check_state(dev, RAID_MEMBER) ) {
		td_dev_err(dev, "ERROR: device in raid \"%s\"\n",
				td_raid_name(dev->td_raid));
		goto bail_with_mutex;
	}

	td_osdev_unregister(&dev->os);

	/* return the reference held by the list */
	td_device_put(dev);
	/* TODO: ^^^ this put should be moved to after mutex is released
	 *     either that or the WARN_ON() in _put() needs to be smarter
	 *     and not complain when called while mutex is held */

	module_put(THIS_MODULE);

	rc = 0;

bail_with_mutex:
	td_device_unlock(dev);
	/* return the reference obtained above with _get() */
	td_device_put(dev);
bail_no_ref:
	mutex_unlock(&td_device_list_mutex);
	return rc;
}

static int __td_device_detach(struct td_device *dev);

/**
 * attach a device to a named group
 *
 * NOTE: call this function while holding td_device_lock()
 */
int td_device_attach(struct td_device *dev, const char *group)
{
	int rc, running;
	struct td_devgroup *dg, *old_dg;
	struct td_engine *eng;
	WARN_TD_DEVICE_UNLOCKED(dev);

	rc = -ENOENT;
	eng = td_device_engine(dev);
	old_dg = dev->td_devgroup;
	running = 0;
	
	if ( td_device_check_state(dev, USERMODE_EXCLUSIVE) ) {
		td_eng_err(eng, "Device in USERMODE_EXCLUSIVE, cannot attach\n");
		return -EPERM;
	}

	/* Check things that can fail */
	dg = td_devgroup_get_by_name(group);
	if (!dg)
		goto error_get_grp;

	rc = -EIO;
	if (!td_devgroup_is_running(dg))
		goto error_grp_stopped;

	/* detach from current group, if any */
	if (old_dg) {
		running = td_devgroup_is_running(old_dg);
		rc = td_devgroup_remove_device(old_dg, dev);
		if (rc)
			goto error_remove_grp;
	}

	/* If we had a group and it was running. sync */
	if (running)
		(void) td_devgroup_sync_device(old_dg, dev);

	/* Verify the engine can start and is read */
	rc = td_engine_start(td_device_engine(dev), 0);
	if (rc < 0)
		goto error_start;

	/* Do the switch */
	rc = td_devgroup_add_device(dg, dev);
	if (rc)
		goto error_add_to_grp;

	/* wake up the thread */
	td_devgroup_poke(dg);

	/* We wait for at least a cycle through to see if it dies */
	(void) td_devgroup_sync_device(dg, dev);
	if ( td_run_state_check(eng, TERMINATING) ||
			td_run_state_check(eng, DEAD) ) {
		td_eng_err(eng, "Engine died");
		goto error_completion;
	}
#if 0
	rc = td_run_state_wait(eng, RUNNING);
	if (rc < 0) {
		td_eng_err(eng, "RUNNING was not reached: %d\n", rc);
		goto error_completion;
	}
#endif
	/* NOTE: returning with ref held on group,
	 * returned in td_device_detach() */
	return rc;

error_completion:
	td_engine_stop(td_device_engine(dev));
error_start:
/* add_to_grp fails, we re-attach to old. */
error_remove_grp:
error_grp_stopped:
error_get_grp:
	return rc;


error_add_to_grp:
	if(old_dg) {
		if (!td_devgroup_add_device(old_dg, dev))
			td_devgroup_poke(old_dg);
	}
	td_devgroup_put(dg);
	return rc;
}

/**
 * detach a device from the group it's part of
 *
 * NOTE: call this function while holding td_device_lock()
 */
int td_device_detach(struct td_device *dev)
{
	WARN_TD_DEVICE_UNLOCKED(dev);

	if (td_device_check_state(dev, ONLINE)
			|| td_device_check_state(dev, RAID_MEMBER) )
		return -EBUSY;

	return __td_device_detach(dev);
}

/**
 * reset a device
 *
 * NOTE: call this function while holding td_device_lock()
 */
int td_device_reset(struct td_device *dev)
{
	int rc;
	struct td_devgroup *dg;
	struct td_engine *eng;
	WARN_TD_DEVICE_UNLOCKED(dev);

	rc = -ENOENT;
	eng = td_device_engine(dev);
	dg = dev->td_devgroup;

	/* Check things that can fail */
	if (!dg)
		goto error_get_grp;

	rc = -EIO;
	if (!td_devgroup_is_running(dg))
		goto error_grp_stopped;

	/*
	 * Get the engine to reset itself, since it's running
	 */
	rc = td_engine_reset(eng);
	if (rc)
		goto error_start;

	/* We wait for at least a cycle through to see if it dies */
	(void) td_devgroup_sync_device(dg, dev);
	if ( td_run_state_check(eng, TERMINATING) ||
			td_run_state_check(eng, DEAD) ) {
		td_eng_err(eng, "Engine died");
		goto error_completion;
	}

error_completion:
error_start:
error_grp_stopped:
error_get_grp:
	return rc;

}

int td_device_shutdown(struct td_device *dev) {
	int rc = 0;
	struct td_engine *eng;
	eng = td_device_engine(dev);
	if (td_run_state_check(eng, DEAD)) {
		return -EIO;
	}
	if (td_eng_conf_var_get(eng, SHUTDOWN) ) {
		struct td_ucmd *ucmd;

		td_eng_info(eng, "Issuing shutdown\n");

		rc = -ENOMEM;
		ucmd = td_ucmd_alloc(TERADIMM_DATA_BUF_SIZE);
		if (ucmd) {
			rc = td_eng_cmdgen(eng, shutdown, ucmd->ioctl.cmd);
			WARN_ON(rc < 0);

			rc = td_ucmd_run(ucmd, eng);
			td_eng_info(eng, "Shutdown returned %d [0x%02X]\n",
					rc, ucmd->ioctl.result & 0x00ff);

			WARN_ON(rc < 0);

			td_ucmd_put(ucmd);
		}

	}
	return rc;
}

static int __td_device_detach(struct td_device *dev)
{
	int rc, running;
	struct td_devgroup *dg;
	struct td_engine *eng;

	WARN_TD_DEVICE_UNLOCKED(dev);

	rc = -ENOENT;
	dg = dev->td_devgroup;
	if (!dg)
		goto error_no_grp;

	eng = td_device_engine(dev);

	/* Before we stop, we send the shutdown command
	 */
	td_device_shutdown(dev);
	td_engine_stop(eng);

#if 0
	/* engine will call td_device_run_state_change() when finished, and
	 * wake us up */
	
	rc = wait_for_completion_interruptible_timeout(
			&eng->td_state_change_completion,
			5 * HZ);
	if (rc<0) {
		rc = -EIO;
		goto error_engine_stop;
	}

	if (!td_run_state_check(td_device_engine(dev), DEAD)) {
		rc = -EIO;
		goto error_engine_stop;
	}
#else
	rc = td_run_state_wait(td_device_engine(dev), DEAD);
	if (rc < 0) {
		td_eng_err(td_device_engine(dev), "engine didn't go dead: rc = %d\n", rc);
		goto error_engine_stop;
	}
#endif
	/* if the devgroup is running, we need to synchronize with */
	running = td_devgroup_is_running(dg);

	rc = td_devgroup_remove_device(dg, dev);
	if (rc)
		goto error_remove_grp;

	if (running)
		(void) td_devgroup_sync_device(dg, dev);

	/* return the ref held since td_device_attach() */
	td_devgroup_put(dg);

	return 0;

error_engine_stop:
	td_eng_err(td_device_engine(dev),
			"can't stop engine, device not detached\n");
error_remove_grp:
error_no_grp:
	return rc;
}

/* ---- device doing work ---- */

static inline bool td_device_could_do_something_for_tt(struct td_device *dev,
		enum td_token_type tt)
{
	struct td_engine *eng = &dev->td_engine;

	if (td_active_tokens(eng, tt))
		return true;

	if (tt == TD_TOK_FOR_FW) {

		if (td_early_completed_reads(eng))
			return true;

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
		if (eng->td_last_fwstatus_request_posted)
			return true;
#endif
	}

	return false;
}


/* do one unit of work; which includes:
 *  - validate state
 *  - complete any outstanding work possible
 *  - start any new work possible
 *
 * return how much work was done (started and completed IOs)
 */
int td_device_do_work(struct td_device *dev)
{
	struct td_engine *eng = &dev->td_engine;
	uint64_t completed, started;
	uint64_t lost_time;
	enum td_token_type tt;
	int rc;

	td_engine_reset_loop_counters(eng);

	lost_time = get_jiffies_64() - eng->dg_last_use_jiffies;

	td_eng_trace(eng, TR_LOOP,
			"dev-0-start-lost-time",
			lost_time);


	rc = td_engine_io_sanity_check(eng);
	if (rc)
		return 0;

	td_eng_trace(eng, TR_LOOP,
			"dev-1-before-queued",
			td_engine_queued_work(eng));
	td_eng_trace(eng, TR_LOOP,
			"dev-1-before-active",
			td_all_active_tokens(eng));
	td_eng_trace(eng, TR_LOOP,
			"dev-1-before-early-reads",
			td_early_completed_reads(eng));

	for_each_token_type_reverse(tt) {
		if (! td_device_could_do_something_for_tt(dev, tt) )
			continue;

		rc = td_eng_hal_read_status(eng, tt);
		if (rc == TD_ENG_STATUS_GOOD) {
			td_engine_io_complete(eng, tt);
			continue;
		}

		/* if the status is stale, but we have pending early
		 * completed reads, and at least one hardware token,
		 * proceed to the completion routine */
		if (tt == TD_TOK_FOR_FW
				&& td_early_completed_reads(eng)
				&& td_available_tokens(eng, tt))
			td_engine_io_complete(eng, tt);
	}

	if (td_engine_queued_work(eng))
		td_engine_io_begin(eng);

	completed = eng->loop_counters.completed.reads
		+ eng->loop_counters.completed.writes
		+ eng->loop_counters.completed.control;

	started = eng->loop_counters.started.reads
		+ eng->loop_counters.started.writes
		+ eng->loop_counters.started.control;

	td_eng_trace(eng, TR_LOOP,
			"dev-2-work-completed",
			completed);

	td_eng_trace(eng, TR_LOOP,
			"dev-2-work-started",
			started);

	td_eng_trace(eng, TR_LOOP,
			"dev-3-end-queued",
			td_engine_queued_work(eng));
	td_eng_trace(eng, TR_LOOP,
			"dev-3-end-active",
			td_all_active_tokens(eng));
	td_eng_trace(eng, TR_LOOP,
			"dev-1-end-early-reads",
			td_early_completed_reads(eng));

	eng->dg_last_use_jiffies = get_jiffies_64();

	return completed + started;
}

/* ---- ioctl handling ---- */

int __td_device_ioctl(struct td_device* dev, unsigned int cmd,
		unsigned long raw_arg)
{
	int rc;
	union {
		struct td_ioctl_devgroup_name dg_name;
		struct td_ioctl_conf dev_conf;
		struct td_ioctl_device_name dev_name;
		struct td_ioctl_device_info dev_info;
		struct td_ioctl_device_state dev_state;
		struct td_ioctl_device_cmd_pt dev_cmd;
		struct td_ioctl_device_ssd_pt ssd_cmd;
		struct td_ioctl_device_get_reg gr;
		struct td_ioctl_device_put_reg pr;
		struct td_ioctl_device_trace_config trc_conf;
		struct td_ioctl_device_trace_read trc_read;
		struct td_ioctl_counters dev_cntrs;
		struct td_ioctl_device_stats dev_stats;
		struct td_ioctl_device_raw_buffer dev_raw;
		struct td_ioctl_device_params param;
		struct td_ioctl_device_lock lock;
		struct td_ioctl_device_global_ext_status gstatus;
		struct td_ioctl_device_error_injection e_conf;
		struct td_ioctl_device_ecc_counters e_counters;
#ifdef CONFIG_TERADIMM_SGIO
		void *sg_hdr;
#endif
	}__user *u_arg, *k_arg, __static_arg, *__big_arg = NULL;
	unsigned copy_in_size, copy_out_size, big_size = 0;

	WARN_TD_DEVICE_LOCKED(dev);

	/* prepare */

	if (unlikely(td_device_engine(dev)->locker_context)) {
		if(!td_device_is_locked(dev, current))
			return -EPERM;
	}

	u_arg = (__user void*)raw_arg;
	k_arg = &__static_arg;

	copy_in_size = 0;
	copy_out_size = 0;
	switch (cmd) {
	case TD_IOCTL_DEVICE_ATTACH:
		copy_in_size = sizeof(struct td_ioctl_devgroup_name);
		break;

	case TD_IOCTL_DEVICE_GET_ALL_CONF:
	case TD_IOCTL_DEVICE_GET_CONF:
	case TD_IOCTL_DEVICE_SET_CONF:

		/* copy in the base structure */
		rc = -EFAULT;
		if (copy_from_user(k_arg, u_arg,
					sizeof(struct td_ioctl_conf)))
			goto bail_ioctl;

		/* based on count provided, figure out how much actually */
		big_size = TD_IOCTL_CONF_SIZE(k_arg->dev_conf.count);

		copy_in_size = big_size;
		if (cmd != TD_IOCTL_DEVICE_SET_CONF)
			copy_out_size = copy_in_size;
		break;

	case TD_IOCTL_DEVICE_GET_ALL_COUNTERS:
	case TD_IOCTL_DEVICE_GET_COUNTERS:

		/* copy in the base structure */
		rc = -EFAULT;
		if (copy_from_user(k_arg, u_arg,
					sizeof(struct td_ioctl_counters)))
			goto bail_ioctl;

		/* based on count provided, figure out how much actually */
		big_size = TD_IOCTL_COUNTER_SIZE(k_arg->dev_cntrs.count);

		copy_in_size = big_size;
		copy_out_size = copy_in_size;
		break;


#ifdef CONFIG_INCLUDE_OLD_IOCTL_STATUS
	case TD_IOCTL_DEVICE_GET_STATUS:
		copy_in_size = sizeof(struct td_ioctl_device_status);
		copy_out_size = sizeof(struct td_ioctl_device_status);
		break;
#endif
	case TD_IOCTL_DEVICE_GET_INFO:
		copy_out_size = sizeof(struct td_ioctl_device_info);
		break;

	case TD_IOCTL_DEVICE_GET_STATE:
		copy_out_size = sizeof(struct td_ioctl_device_state);
		break;

	case TD_IOCTL_DEVICE_CMD_PT:
		copy_in_size = sizeof(struct td_ioctl_device_cmd_pt);
		copy_out_size = sizeof(struct td_ioctl_device_cmd_pt);
		//big_size = sizeof(struct td_ucmd);
		break;
	case TD_IOCTL_DEVICE_SSD_PT:
		copy_in_size = sizeof(struct td_ioctl_device_ssd_pt);
		copy_out_size = sizeof(struct td_ioctl_device_ssd_pt);
		break;

	case TD_IOCTL_DEVICE_GET_REG:
		copy_in_size = sizeof(struct td_ioctl_device_get_reg);
		copy_out_size = sizeof(struct td_ioctl_device_get_reg);
		break;

	case TD_IOCTL_DEVICE_PUT_REG:
		copy_in_size = sizeof(struct td_ioctl_device_put_reg);
		copy_out_size = sizeof(struct td_ioctl_device_put_reg);
		break;

	case TD_IOCTL_DEVICE_GET_PARAMS:
		copy_in_size = sizeof(struct td_ioctl_device_params);
		copy_out_size = sizeof(struct td_ioctl_device_params);
		break;

	case TD_IOCTL_DEVICE_SET_PARAMS:
		copy_in_size = sizeof(struct td_ioctl_device_params);
		copy_out_size = sizeof(struct td_ioctl_device_params);
		break;

	case TD_IOCTL_DEVICE_TRACE_GET_CONF:
		copy_out_size = sizeof(struct td_ioctl_device_trace_config);
		break;

	case TD_IOCTL_DEVICE_TRACE_SET_CONF:
		copy_in_size = sizeof(struct td_ioctl_device_trace_config);
		break;

	case TD_IOCTL_DEVICE_TRACE_READ:
		copy_in_size = sizeof(struct td_ioctl_device_trace_read);
		copy_out_size = sizeof(struct td_ioctl_device_trace_read);
		break;

	case TD_IOCTL_DEVICE_GET_STATS:
		copy_out_size = sizeof(struct td_ioctl_device_stats);
		break;

	case TD_IOCTL_DEVICE_GET_RDBUFS:
		copy_out_size = sizeof(struct td_ioctl_device_rdbufs);
		big_size = copy_out_size;
		break;

	case TD_IOCTL_DEVICE_GET_RAW_BUFFER:
	case TD_IOCTL_DEVICE_SET_RAW_BUFFER:
		copy_in_size = sizeof(struct td_ioctl_device_raw_buffer);
		copy_out_size = sizeof(struct td_ioctl_device_raw_buffer);
		break;

	case TD_IOCTL_DEVICE_GLOBAL_EXT_STATUS:
		copy_out_size = sizeof(struct td_ioctl_device_global_ext_status);
		break;

#ifdef CONFIG_TERADIMM_SGIO
	case SG_IO: /* Regular sg passthrough utilities */
		copy_in_size = sizeof(sg_io_hdr_t);
		copy_out_size = sizeof(sg_io_hdr_t);
		break;
#endif

	case BLKFLSBUF: /* block flush buffers */
		copy_in_size = 0;
		copy_out_size = 0;
		break;
	case TD_IOCTL_DEVICE_GET_ERRORS:
		copy_out_size = sizeof(struct td_ioctl_device_error_injection);
		break;
	case TD_IOCTL_DEVICE_SET_ERRORS:
		copy_in_size = sizeof(struct td_ioctl_device_error_injection);
		break;

	case TD_IOCTL_DEVICE_GET_ECC_COUNTERS:
		copy_in_size = sizeof(struct td_ioctl_device_ecc_counters);
		/* copy in the structure header */
		rc = -EFAULT;
		if (copy_from_user(k_arg, u_arg, copy_in_size))
			goto bail_ioctl;

		big_size = copy_in_size + (k_arg->e_counters.num_counters *
				sizeof(k_arg->e_counters.counters[0]));
		copy_out_size = big_size;
		break;

	case TD_IOCTL_DEVICE_STOP_BIO:
		/* Fall-through expected. */
	case TD_IOCTL_DEVICE_START_BIO:
		copy_in_size = 0;
		copy_out_size = 0;
		break;

	case TD_IOCTL_DEVICE_LOCK:
		copy_in_size = sizeof(struct td_ioctl_device_lock);
		break;

	case TD_IOCTL_DEVICE_UNLOCK:
		
	default:
		/* nothing to copy in */
		break;
	}

	/* allocate a big buffer if needed */
	if (big_size) {
		rc = -ENOMEM;
		__big_arg = kzalloc(big_size, GFP_KERNEL);
		if (!__big_arg) {
			pr_err("Device ioctl failed to allocate %u bytes.",
					big_size);
			goto bail_ioctl;
		}
		k_arg = __big_arg;
	}

	/* copy in the data struct */

	if (copy_in_size) {
		rc = -EFAULT;
		if (copy_from_user(k_arg, u_arg, copy_in_size)) {
			pr_err("Device ioctl failed to copy in %u bytes.",
					copy_in_size);
			goto bail_ioctl;
		}
	}

	/* check if output can be written to */

	if (copy_out_size) {
		rc = -EFAULT;
		if (!access_ok(VERIFY_WRITE, u_arg, copy_out_size)) {
			pr_err("Device ioctl cannot write %u bytes.",
					copy_out_size);
			goto bail_ioctl;
		}
	}

	/* handle some IOCTLs w/o lock */

	switch (cmd) {

	case TD_IOCTL_DEVICE_GET_INFO:
		/** ioctl used to get current state */
		rc = td_ioctl_device_get_info(dev, &k_arg->dev_info);
		goto handled;

	case TD_IOCTL_DEVICE_GET_STATE:
		/** ioctl used to get current state */
		rc = td_ioctl_device_get_state(dev, &k_arg->dev_state);
		goto handled;

	case TD_IOCTL_DEVICE_TRACE_READ:
		rc = td_trace_iterator_read(&td_device_engine(dev)->td_trace,
				&k_arg->trc_read.iterator,
				k_arg->trc_read.buffer,
				k_arg->trc_read.buf_size);
		if (rc>0) {
			k_arg->trc_read.buf_used = rc;
			rc = 0;
		}
		goto handled;

	case TD_IOCTL_DEVICE_GET_STATS:
		rc = td_ioctl_device_get_stats(dev, &k_arg->dev_stats);
		goto handled;

#ifdef CONFIG_TERADIMM_SGIO
	case SG_IO:
		rc = td_block_sgio(&dev->os, &k_arg->sg_hdr);
		goto handled;
#endif
	case BLKFLSBUF: /* block flush buffers */
		rc = td_ioctl_device_flush(dev);
		goto handled;
	default:
		/* rest require a lock on the device */
		break;
	}

	/* perform the operation under lock to prevent races with other users */

	td_device_lock(dev);

	switch (cmd) {
	case TD_IOCTL_DEVICE_USERMODE_EXCLUSIVE:
		rc = td_ioctl_device_usermode_exclusive(dev, current);
		break;

	case TD_IOCTL_DEVICE_ATTACH:
		/** ioctl used to attach a device to a device group */
		rc = td_ioctl_device_attach(dev, k_arg->dg_name.group_name);
		break;

	case TD_IOCTL_DEVICE_DETACH:
		/** ioctl used to detach a device from a device group */
		rc = td_ioctl_device_detach(dev);
		break;

	case TD_IOCTL_DEVICE_RESET:
		/** ioctl used to detach a device from a device group */
		rc = td_ioctl_device_reset(dev);
		break;

	case TD_IOCTL_DEVICE_GET_ALL_CONF:
	case TD_IOCTL_DEVICE_GET_CONF:
		/** ioctl used to query the configuration of a device group */
		rc = td_ioctl_device_get_conf(dev, &k_arg->dev_conf,
				cmd == TD_IOCTL_DEVICE_GET_ALL_CONF);
		break;

	case TD_IOCTL_DEVICE_GET_ALL_COUNTERS:
	case TD_IOCTL_DEVICE_GET_COUNTERS:
		rc = td_ioctl_device_get_counters(dev, &k_arg->dev_cntrs,
				cmd == TD_IOCTL_DEVICE_GET_ALL_COUNTERS);
		break;

	case TD_IOCTL_DEVICE_SET_CONF:
		/** ioctl used to change the configuration of a device group */
		rc = td_ioctl_device_set_conf(dev, &k_arg->dev_conf);
		break;

	case TD_IOCTL_DEVICE_GO_ONLINE:
		/** ioctl used to enter on-line mode */
		rc = td_ioctl_device_go_online(dev);
		break;

	case TD_IOCTL_DEVICE_GO_OFFLINE:
		/** ioctl used to enter off-line mode */
		rc = td_ioctl_device_go_offline(dev);
		break;

	case TD_IOCTL_DEVICE_TRAIN:
		rc = td_device_train(dev);
		break;


	case TD_IOCTL_DEVICE_CMD_PT:
		/** ioctl used to send a pass-through command to device */
		rc = td_ioctl_device_cmd_pt(dev, &k_arg->dev_cmd, current);
		/* this handler consumed the buffer */
		__big_arg = NULL;
		break;
	case TD_IOCTL_DEVICE_SSD_PT:
		//rc = td_eng_hal_ssd_pt(td_device_engine(dev),&k_arg->ssd_cmd);
		rc = td_ioctl_device_ata_pt(dev, &k_arg->ssd_cmd);
		break;

	case TD_IOCTL_DEVICE_GET_REG:
		rc = td_ioctl_device_get_reg(dev, &k_arg->gr);
		break;

	case TD_IOCTL_DEVICE_PUT_REG:
		rc = td_ioctl_device_put_reg(dev, &k_arg->pr);
		break;

	case TD_IOCTL_DEVICE_GET_PARAMS:
		rc = td_ioctl_device_get_params(dev,
				&k_arg->param);
		break;

	case TD_IOCTL_DEVICE_SET_PARAMS:
		rc = td_ioctl_device_set_params(dev,
				&k_arg->param);
		break;

	case TD_IOCTL_DEVICE_TRACE_GET_CONF:
		rc = 0;
		k_arg->trc_conf.trace_mask = td_trace_get_mask(
				&td_device_engine(dev)->td_trace);
		k_arg->trc_conf.trace_wrap = td_trace_get_wrap(
				&td_device_engine(dev)->td_trace);
		break;

	case TD_IOCTL_DEVICE_TRACE_SET_CONF:
		rc = 0;
		td_trace_set_mask(&td_device_engine(dev)->td_trace,
				k_arg->trc_conf.trace_mask);
		td_trace_set_wrap(&td_device_engine(dev)->td_trace,
				k_arg->trc_conf.trace_wrap);
		if (k_arg->trc_conf.trace_reset)
			td_trace_reset(&td_device_engine(dev)->td_trace);
		break;

	case TD_IOCTL_DEVICE_GET_RDBUFS:
		rc = td_ioctl_device_get_rdbufs(dev, (void*)__big_arg);
		break;

	case TD_IOCTL_DEVICE_GET_RAW_BUFFER:
		rc = td_ioctl_device_get_raw_buffer(dev,
				&k_arg->dev_raw);
		break;

	case TD_IOCTL_DEVICE_SET_RAW_BUFFER:
		rc = td_ioctl_device_set_raw_buffer(dev,
				&k_arg->dev_raw);
		break;


	case TD_IOCTL_DEVICE_GLOBAL_EXT_STATUS:
		rc = td_ioctl_device_global_ext_status(dev, &k_arg->gstatus);
		break;

	case TD_IOCTL_DEVICE_GET_ERRORS:
		rc = td_ioctl_device_get_errors(dev, &k_arg->e_conf);
		break;
	case TD_IOCTL_DEVICE_SET_ERRORS:
		rc = td_ioctl_device_set_errors(dev, &k_arg->e_conf);
		break;

	case TD_IOCTL_DEVICE_GET_ECC_COUNTERS:
		rc = td_ioctl_device_get_ecc_counters(dev, &k_arg->e_counters);
		break;
	case TD_IOCTL_DEVICE_STOP_BIO:
		rc = td_ioctl_device_stop_bio(dev, current);
		break;
	case TD_IOCTL_DEVICE_START_BIO:
		rc = td_ioctl_device_start_bio(dev);
		break;

	case TD_IOCTL_DEVICE_LOCK:
		rc = td_ioctl_device_lock(dev, &k_arg->lock, current);
		break;

	case TD_IOCTL_DEVICE_UNLOCK:
		rc = td_ioctl_device_unlock(dev, current);
		break;

	default:
		rc = -ENOIOCTLCMD;
		break;
	}

	td_device_unlock(dev);

handled:

	switch(rc) {
	case 0:
		break;

	case -ENOBUFS:
		/* in these cases we'd like to copy_to_user so that
		 * the user app can check the count and allocate a
		 * larger buffer.
		 */
		if (cmd == TD_IOCTL_DEVICE_GET_ALL_COUNTERS)
			break;
		if (cmd == TD_IOCTL_DEVICE_GET_ALL_CONF)
			break;

	default:
		goto bail_ioctl;
	}

	/* copy data back */

	if (copy_out_size) {
		if (copy_to_user(u_arg, k_arg, copy_out_size)) {
			rc = -EFAULT;
			pr_err("Device ioctl failed to copy out %u bytes.",
					copy_out_size);
			goto bail_ioctl;
		}
	}

bail_ioctl:
	if (__big_arg)
		kfree(__big_arg);
	return rc;
}

int td_device_ioctl(struct td_osdev* dev, unsigned int cmd,
		unsigned long raw_arg)
{
	if (dev->type != TD_OSDEV_DEVICE)
		return -ENODEV;
	return __td_device_ioctl(td_device_from_os(dev), cmd, raw_arg);
}
/*  ---- block device ---- */

#if defined QUEUE_FLAG_PLUGGED
void td_device_queue_unplug(struct request_queue *q) {
	return;
}
#endif

/**
 * make the device go into on-line mode
 *
 * NOTE: call this function while holding td_device_lock()
 */
int td_device_go_online(struct td_device *dev)
{
	int rc;
	struct td_engine *eng = td_device_engine(dev);

	WARN_TD_DEVICE_UNLOCKED(dev);

	/*  already in this state */
	if (!td_device_check_state(dev, OFFLINE)
			&& !td_device_check_state(dev, CREATED))
		return -EEXIST;

	rc = -EIO;
	if (!td_run_state_check(eng, RUNNING)) {
		td_dev_err(dev, "engine not running\n");
		goto error_failed_running;
	}

	rc = td_eng_hal_online(eng);
	if (rc) {
		td_eng_err(eng, "Unable to bring device online.\n");
		goto error_failed_online;
	}

	dev->os.block_params.bio_max_bytes = td_eng_conf_var_get(eng, BIO_MAX_BYTES);
	dev->os.block_params.bio_sector_size = td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE);
	dev->os.block_params.hw_sector_size = td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE);
	dev->os.block_params.discard = !!td_eng_conf_hw_var_get(eng, DISCARD);

	/* The engine can now calculate capacity */
	dev->os.block_params.capacity = td_engine_capacity(eng);

	rc = td_osdev_online(&dev->os);
	if (rc) {
		td_os_err(&dev->os, "Unable to create OS I/O device.\n");
		goto error_failed_queue;
	}

	td_device_enter_state(dev, ONLINE);

	return 0;

error_failed_queue:
error_failed_online:
error_failed_running:
	td_device_enter_state(dev, OFFLINE);
	return rc;
}

/**
 * make the device go into off-line mode
 *
 * NOTE: call this function while holding td_device_lock()
 */
int td_device_go_offline(struct td_device *dev)
{
	WARN_TD_DEVICE_UNLOCKED(dev);

	/*  already in this state */
	if (!td_device_check_state(dev, ONLINE))
		return -EEXIST;

	if (atomic_read(&dev->os.block_users))
		return -EBUSY;

	if (td_osdev_offline(&dev->os))
		return -EBUSY;

	td_eng_hal_offline(td_device_engine(dev));

	td_device_enter_state(dev, OFFLINE);

	return 0;
}



#if 0
static int td_device_block_ioctl(struct block_device *bdev, fmode_t mode,
			unsigned int cmd, unsigned long raw_arg)
{
	int rc;
	struct td_device *dev = td_device_from_bdevice(bdev);
	union {
		struct td_ioctl_device_ssd_pt ssd_cmd;
		sg_io_hdr_t sg_hdr;
	}__user *u_arg, *k_arg, __static_arg, *__big_arg = NULL;
	unsigned copy_in_size, copy_out_size, big_size = 0;

	/* prepare */

	u_arg = (__user void*)raw_arg;
	k_arg = &__static_arg;
	/* struct td_device *dev = filp->private_data; */

	copy_in_size = 0;
	copy_out_size = 0;
	switch (cmd) {
	case SG_IO: /* Regular sg passthrough utilities */
		copy_in_size = sizeof(sg_io_hdr_t);
		copy_out_size = sizeof(sg_io_hdr_t);
		break;

	default:
		break;
	}
	/* allocate a big buffer if needed */
	if (big_size) {
		rc = -ENOMEM;
		__big_arg = kzalloc(big_size, GFP_KERNEL);
		if (!__big_arg)
			goto bail_ioctl;
		k_arg = __big_arg;
	}

	/* copy in the data struct */

	if (copy_in_size) {
		rc = -EFAULT;
		if (copy_from_user(k_arg, u_arg, copy_in_size))
			goto bail_ioctl;
	}

	/* check if output can be written to */

	if (copy_out_size) {
		rc = -EFAULT;
		if (!access_ok(VERIFY_WRITE, u_arg, copy_out_size))
			goto bail_ioctl;
	}

	switch (cmd) {
	case SG_IO:
		rc = td_eng_hal_sg_io(td_device_engine(dev), &k_arg->sg_hdr);
		goto handled;
	default:
		break;
	}
	/*  none implemented (yet) */
	return -EINVAL;

handled:
	if (rc)
		goto bail_ioctl;

	/* copy data back */

	if (copy_out_size) {
		rc = -EFAULT;
		if (copy_to_user(u_arg, k_arg, copy_out_size))
			goto bail_ioctl;
	}

	rc = 0;

bail_ioctl:
	if (__big_arg)
		kfree(__big_arg);
	return rc;
}

#endif

struct td_device_iter_state {
	int (*action)(struct td_device *, void*);
	void* data;
};

static int __action_osdev_wrapper(struct td_osdev *dev, void* data)
{
	if (dev->type == TD_OSDEV_DEVICE) {
		struct td_device_iter_state *st = data;
		
		return st->action(td_device_from_os(dev), st->data);
	}
	return 0;
}


int td_device_list_iter(
		int (*action)(struct td_device *dev, void *data),
		void *data)
{
	int rc = 0;
	
	struct td_device_iter_state state = {
		action, data
	};

	mutex_lock(&td_device_list_mutex);

	rc = td_osdev_list_iter(__action_osdev_wrapper, &state);

	mutex_unlock(&td_device_list_mutex);

	return rc;
}

