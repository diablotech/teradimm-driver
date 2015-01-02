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
MODULE_AUTHOR("Diablo Technologies <info@diablo-technologies.com>");
MODULE_DESCRIPTION("TeraDIMM block device driver.");
MODULE_VERSION(TERADIMM_VERSION);
