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

#include "td_control.h"
#include "td_devgroup.h"
#include "td_device.h"
#include "td_raid.h"
#include "td_mon.h"
#include "td_osdev.h"


static int __init teradimm_init(void)
{
	int rc;

	printk("TeraDIMM %s\n", TERADIMM_VERSION);

	rc = td_os_init();
	if (rc)
		goto error_os_init;

	rc = td_devgroup_init();
	if (rc)
		goto error_devgroup;

	/* Raid must be ready for device */
	rc = td_raid_init();
	if (rc)
		goto error_raid;

	/* device must be ready for monitor */
	rc = td_device_init();
	if (rc)
		goto error_device;

	rc = td_monitor_init();
	if (rc)
		goto error_monitor;

	/*  this one should be last */
	rc = td_control_init();
	if (rc)
		goto error_control;

	return 0;

error_control:
	td_monitor_exit();
error_monitor:
	td_device_exit();
error_device:
	td_raid_exit();
error_raid:
	td_devgroup_exit();
error_devgroup:
	td_os_exit();
error_os_init:
	return rc;
}

static void __exit teradimm_exit(void)
{
	/*  remove control first, so groups cannot be created */
	td_control_exit();

	td_monitor_exit();

	td_raid_exit();
	td_device_exit();
	td_devgroup_exit();

	td_os_exit();
	printk("TeraDIMM module unloaded\n");
}

module_init(teradimm_init);
module_exit(teradimm_exit);

static char* td_revision = "revision=" TERADIMM_REVISION;
module_param_named(revision, td_revision, charp, 0000);
MODULE_PARM_DESC(revision, TERADIMM_REVISION);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Diableo Technologies <info@diablo-technologies.com>");
MODULE_DESCRIPTION("TeraDIMM block device driver.");
MODULE_VERSION(TERADIMM_VERSION);
