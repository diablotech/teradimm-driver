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
#include "td_raid.h"
#include "td_compat.h"
#include "td_ucmd.h"
#include "td_eng_hal.h"
#include "td_discovery.h"
#include "td_sgio.h"
#include "td_memspace.h"
#include "td_bio.h"

#include "td_raidmeta.h"

#define RD_DEBUG(rd, fmt, ...)  pr_info("tr_raid[%s]" fmt, rd->os.name, ##__VA_ARGS__)
#define NO_RD_DEBUG(rd, fmt, ...)  while (0) { pr_info("tr_raid[%s]" fmt, rd->os.name, ##__VA_ARGS__); }

/* These are the 2 types of RAID we do */
extern struct td_raid_ops tr_stripe_ops;
extern struct td_raid_ops tr_mirror_ops;

/* Takes raid device, pointer to string of TR_UUID_LENGTH*2+5 */
static inline void td_raid_format_uuid(uint8_t *uuid, char *buffer)
{
	sprintf(buffer, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
			"%02x%02x%02x%02x%02x%02x",
			uuid[0],  uuid[1],  uuid[2],
			uuid[3],  uuid[4],  uuid[5],
			uuid[6],  uuid[7],  uuid[8],
			uuid[9],  uuid[10], uuid[11],
			uuid[12], uuid[13], uuid[14],
			uuid[15]);
}

static inline const char* tr_member_state_name(enum td_raid_member_state s)
{
	switch (s) {
	case TR_MEMBER_EMPTY:
		return "EMPTY";
	case TR_MEMBER_ACTIVE:
		return "ACTIVE";
	case TR_MEMBER_FAILED:
		return "FAILED";
	case TR_MEMBER_SYNC:
		return "SYNC";
	case TR_MEMBER_SPARE:
	case TD_RAID_MEMBER_STATE_MAX:
		/* fall out */;
	}
	return "<UNKNOWN>";
}

/* Forward declartions - we give these functions to the osdev API */
void td_raid_destroy (struct td_osdev *odev);
int td_raid_ioctl(struct td_osdev* rdev, unsigned int cmd, unsigned long raw_arg);

/* Other forward declarations */
static void td_raid_init_member(struct td_raid *rdev, int member_idx,
		struct td_device *dev, enum td_raid_member_state state);

static void td_raid_apply_metadata (struct td_raid *rdev,
		struct tr_meta_data_struct *md, int redo);

static void td_raid_resync_wait(struct td_raid *dev);

#define STATIC_ASSERT(expr)                                             \
	switch (0) {                                                    \
	case (expr):                                                    \
	case 0:                                                         \
		/* DO NOTHING */;                                        \
	}

/* Create a dump of raid metadata */
static void __td_raid_fill_meta (struct td_raid *rdev, struct tr_meta_data_struct *md)
{
	int i;

	STATIC_ASSERT(sizeof(*md)           == TERADIMM_DATA_BUF_SIZE);
	STATIC_ASSERT(sizeof(md->signature) == TR_META_DATA_SIGNATURE_SIZE);
	STATIC_ASSERT(sizeof(md->raid_info) == TR_META_DATA_INFO_SIZE);
	STATIC_ASSERT(sizeof(md->member[0]) == TR_META_DATA_MEMBER_SIZE);
	STATIC_ASSERT(sizeof(md->member)    == TR_META_DATA_MEMBER_SIZE * TR_META_DATA_MEMBERS_MAX);

	memset(md, 0, TERADIMM_DATA_BUF_SIZE);
	memcpy(md->signature.uuid, rdev->os.uuid, TD_UUID_LENGTH);
	md->signature.uuid_check =0;
	for (i = 0; i < TD_UUID_LENGTH; i++) {
		uint16_t t = md->signature.uuid[i];
		t <<= 8;
		i += 1;
		t += md->signature.uuid[i];
		md->signature.uuid_check ^= t;
	}


	md->signature.version = TR_METADATA_VERSION;
	md->signature.generation = rdev->tr_generation;
	
	for (i = 0; i < TR_CONF_GENERAL_MAX; i++) {
		struct td_ioctl_conf_entry *c = md->raid_info.conf + i;
		c->type = TD_RAID_CONF_GENERAL;
		c->var = i;
		c->val = rdev->conf.general[i];
	}
	if (rdev->ops->_get_conf) {
		uint64_t val;
		uint32_t t = 0;
		for (t = 0; rdev->ops->_get_conf(rdev, t, &val) == 0; t++) {
			struct td_ioctl_conf_entry *c = md->raid_info.conf + i++;
			c->type = TD_RAID_CONF_OPS;
			c->var = t;
			c->val = val;
		}
	}
	md->raid_info.conf[i].type = TD_RAID_CONF_INVALID;

	for (i = 0; i < TR_META_DATA_MEMBERS_MAX; i++) {
		struct tr_member *trm;
		if (! TR_MEMBERSET_TEST(rdev, i) )
			continue;

		trm = rdev->tr_members + i;
		BUG_ON(trm->trm_device == NULL);

		memcpy(md->member[i].uuid, trm->trm_device->os.uuid, TD_UUID_LENGTH);
		md->member[i].state = trm->trm_state;
	}

}

void td_raid_save_meta (struct td_raid *rdev, int wait)
{
	struct tr_meta_data_struct *md;
	struct td_ucmd **member_ucmds;

	int i;

	/* We must be locked, so only one at a time */
	WARN_TD_DEVICE_UNLOCKED(rdev);

	member_ucmds = kmalloc(sizeof(struct td_ucmd)*tr_conf_var_get(rdev, MEMBERS), GFP_KERNEL);

td_raid_warn(rdev, "***** Saving metadata generation %llu *****\n",
		rdev->tr_generation);

	md = kmalloc(TD_PAGE_SIZE, GFP_KERNEL);
	__td_raid_fill_meta(rdev, md);

	td_dump_data("RAID: ", md, sizeof(*md));

	for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
		struct td_ucmd *ucmd;
		struct tr_member *trm;
		struct td_engine *eng;

		if (! TR_MEMBERSET_TEST(rdev, i) ) {
			member_ucmds[i] = NULL;
			continue;
		}

		trm = rdev->tr_members + i;
		BUG_ON(trm->trm_device == NULL);

		if (trm->trm_state == TR_MEMBER_FAILED) {
			td_raid_warn(rdev, "Skipping metadata on failed device %d: %s\n",
					i, td_device_name(trm->trm_device));
			member_ucmds[i] = NULL;
			continue;
		}
		eng = td_device_engine(trm->trm_device);

		ucmd = member_ucmds[i] = td_ucmd_alloc(TD_PAGE_SIZE);
		ucmd->ioctl.data_len_to_device = TD_PAGE_SIZE;
		memcpy(ucmd->data_virt, md, TD_PAGE_SIZE);

		td_eng_cmdgen(eng, set_params, ucmd->ioctl.cmd, 13);

		/* The UCMD boiler code, minus the wait */
		td_ucmd_ready(ucmd);
		td_enqueue_ucmd(eng, ucmd);
		td_engine_sometimes_poke(eng);
	}

	for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
		struct td_ucmd *ucmd = member_ucmds[i];

		if (ucmd == NULL)
			continue;

		/* Now we wait */
		if (wait)
			td_ucmd_wait(ucmd);
		else {
			struct tr_member *trm = rdev->tr_members + i;
			td_raid_info(rdev, "Throwing update at %d: %s\n",
					i, td_device_name(trm->trm_device));
		}

		td_ucmd_put(ucmd);
	}

	kfree(member_ucmds);
	kfree(md);
}


/* Prevent races on device creation/deletion */
static struct mutex td_raid_list_mutex;
static unsigned td_raid_list_count;

/* Prevent races in discovery trying to create raids */
static struct mutex tr_discovery_mutex; /**< prevents races with/between creates */

/* --- init/exit functions ----*/

int __init td_raid_init(void)
{
	mutex_init(&td_raid_list_mutex);
	mutex_init(&tr_discovery_mutex);
	td_raid_list_count = 0;

	return 0;
}

void td_raid_exit(void)
{
	WARN_ON(td_raid_list_count);

	return;
}


/** 
 * \brief delete a raid device
 * 
 * @param name    - name of device to find/delete
 * @return 0 if success, -ERROR
 *
 */
int td_raid_delete(const char* name)
{
	int rc, i;
	struct td_raid *rdev;
	mutex_lock(&td_raid_list_mutex);

	rc = -ENOENT;
	rdev = td_raid_get(name);
	if (!rdev) {
		pr_err("ERROR: Could not find raid '%s'.\n", name);
		goto bail_no_ref;
	}
	td_raid_lock(rdev);

#if 0
	/*
	 * Check if all members are remvoed */
	rc = -EBUSY;
	if (! TR_MEMBERSET_EMPTY(rdev) ) {
		td_raid_err(rdev, "Cannot delete: Active members\n");
		goto bail_active_members;
	}
#endif

	/* check if the raid is bing used (mounted/open/etc) */
	rc = -EBUSY;
	if (atomic_read(&rdev->os.control_users)
			|| atomic_read(&rdev->os.block_users)) {
		goto bail_in_use;
	}
	NO_RD_DEBUG(rdev, "unregister\n");

	switch (rdev->tr_dev_state) {
	case TD_RAID_STATE_ONLINE:
		goto bail_in_use;
		break;

	default: /* Other dev states don't require action */
		/* break */;
	}

	td_raid_info(rdev, "Deleting raid\n");
	td_osdev_unregister(&rdev->os);
	
	/*
	 * First we need to make sure resync will stop
	 * We set all devices to FAILED.  When the resync sees this, it will
	 * fail the sync target (no-op, failed)
	 * We explicity try and avoid meta-data updates here
	 */
	for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
		struct tr_member *trm = rdev->tr_members + i;
		trm->trm_state = TR_MEMBER_FAILED;
	}
	td_raid_resync_wait(rdev);

	/* And now we forcibly remove all members */
	for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
		if (TR_MEMBERSET_TEST(rdev, i) ) {
			struct tr_member *trm = rdev->tr_members + i;
			struct td_device *dev = trm->trm_device;
			/*
			 * We do the basics of detach, with no metadata or
			 * notification
			 */
			td_device_lock(dev);
			td_raid_info(rdev, "Removing member %d: %s\n", 
					i, td_device_name(dev));

			/* This device is now just OFFLINE */
			td_device_enter_state(dev, OFFLINE);
			dev->td_raid =  NULL;

			/* And now we unlock */
			td_device_unlock(dev);

			/* we need to release the main raid reference */
			td_device_put(dev);

			trm->trm_device = NULL;
			if (trm->trm_ucmd) {
				td_ucmd_put(trm->trm_ucmd);
				trm->trm_ucmd = NULL;
			}
		}
	}


	td_raid_list_count --;

	if (rdev->ops) {
		rc = rdev->ops->_destroy(rdev);
		if (rc)
			td_raid_warn(rdev, "Raid ops cleanup rc = %d\n", rc);
	}

	NO_RD_DEBUG(rdev, "done\n");
	rc = 0;

bail_in_use:
	NO_RD_DEBUG(rdev, "unlock\n");
	td_raid_unlock(rdev);
	/* return the reference obtained above with _get() */
	td_raid_put(rdev);

bail_no_ref:

	mutex_unlock(&td_raid_list_mutex);

	if(!rc) {
		NO_RD_DEBUG(rdev, "put\n");
		/* return the reference held by the OS */
		td_raid_put(rdev);
		/* return the module reference held by this device.*/
		module_put(THIS_MODULE);
	}

	return rc;
}


struct td_device_check_exists_state {
	const char      *name;
	const char      *uuid;
};

static int __iter_raid_check_exists(struct td_osdev *dev, void* data)
{
	struct td_device_check_exists_state *st = data;

	if (strncmp(st->name, dev->name, TD_DEVICE_NAME_MAX) == 0) {
		pr_err("Raid '%s' already exists.\n", st->name);
		return -EEXIST;
	}

	if (dev->type == TD_OSDEV_RAID) {
		if (memcmp(st->uuid, dev->uuid, TR_UUID_LENGTH) == 0) {
			char uuid_buf[TR_UUID_LENGTH * 2 + 5];
			td_raid_format_uuid(dev->uuid, uuid_buf);
			pr_err("UUID '%s' already exists in raid '%s'.\n",
					uuid_buf, dev->name);
			return -EEXIST;
		}
	}



	return 0;
}

int td_raid_create (const char *name, const uint8_t uuid[TR_UUID_LENGTH],
		int conf_count, struct td_ioctl_conf_entry* conf)
{
	int rc = -EINVAL;
	struct td_raid *dev;
	enum td_raid_level raid_level;

	int i;

	mutex_lock(&td_raid_list_mutex);

	/* 
	 * This is checking if we are a duplicate name or UUID
	 * This happens under the list mutex
	 */
	if (1) {
		struct td_device_check_exists_state check_state = {
			name, uuid
		};
		rc = td_osdev_list_iter(__iter_raid_check_exists, &check_state);
		if (rc)
			goto error_create;
	}

	printk("CREATING RAID DEVICE \"%s\"\n", name);

	rc = -ENOMEM;
	dev = kzalloc(sizeof(struct td_raid), GFP_KERNEL);
	if (!dev)
		goto error_alloc;

	/* Fill in the general CONF */
	for (i = 0; i < conf_count; i++) {
		struct td_ioctl_conf_entry *c = conf + i;
		if (c->type != TD_RAID_CONF_GENERAL)
			continue;
		if (c->var < TR_CONF_GENERAL_MAX) {
			dev->conf.general[c->var] = c->val;
		}
	}

	raid_level = tr_conf_var_get(dev, LEVEL);
	/* TODO: Some raid level management */
	switch (raid_level) {
	case TD_RAID_STRIPE:
		dev->ops = &tr_stripe_ops;
		break;

	case TD_RAID_MIRROR:
		dev->ops = &tr_mirror_ops;
		break;
	case TD_RAID_10:
	case TD_RAID_UNKNOWN:
		/* break */;

	}
	if (! dev->ops) {
		td_raid_err(dev, "Invalid raid level %d\n", raid_level);
		goto error_ops;
	}

	rc = dev->ops->_init(dev);
	if (rc) {
		td_raid_err(dev, "Failed to initialize raid level %d\n", raid_level);
		goto error_ops;
	}

	/* Fill in the OPS CONF */
	if (dev->ops->_set_conf) {
		for (i = 0; i < conf_count; i++) {
			struct td_ioctl_conf_entry *c = conf + i;
			if (c->type != TD_RAID_CONF_OPS)
				continue;
			dev->ops->_set_conf(dev, c->var, c->val);
		}
	}

	/* Now go about making things, we are failed when we start */
	tr_enter_run_state(dev, FAILED);

	/* Add our members */
	rc = sizeof(struct tr_member) * tr_conf_var_get(dev, MEMBERS);
	dev->tr_members = kzalloc(rc, GFP_KERNEL);
	rc = -ENOMEM;
	if (!dev->tr_members)
		goto error_members;

	rc = td_osdev_init(&dev->os, TD_OSDEV_RAID, name, td_raid_ioctl, td_raid_destroy);
	if (rc) {
		pr_err("Failed to create OS device '%s', err=%d.\n",
				name, rc);
		goto error_os_init;
	}

	td_raid_lock(dev);

	memcpy(dev->os.uuid, uuid, TD_UUID_LENGTH);
	snprintf(dev->os.vendor, TD_VENDOR_LENGTH, "%s", "Diablo");
	snprintf(dev->os.model, TD_MODEL_LENGTH, "%s", "MCS-RAID");
	snprintf(dev->os.revision, TD_REVISION_LENGTH, "%06x", 0xd1ab10);
	*(uint32_t*)dev->os.serial = dev->os.unique_id;

	/* Get a printable uuid. */
	if (1) {
		unsigned char uuid_printed[TR_UUID_LENGTH*2+5];
		td_raid_format_uuid(dev->os.uuid, uuid_printed);

		pr_err("%s:%d: rdev %p uuid = %s\n", __func__, __LINE__,
				dev, uuid_printed);
	}


	rc = td_osdev_register(&dev->os);
	if (rc) {
		td_raid_err(dev, "Failed to register OS device, err=%d.\n", rc);
		goto error_os_register;
	}

	tr_enter_dev_state(dev, CREATED);
	dev->tr_generation = 1;

	td_raid_list_count ++;
	mutex_unlock(&td_raid_list_mutex);

	__module_get(THIS_MODULE);

	td_raid_unlock(dev);

	return 0;

error_os_register:
	td_raid_unlock(dev);

error_os_init:
error_ops:
error_members:
	if (dev->ops)
		dev->ops->_destroy(dev);

	dev->ops = NULL;

	if (dev->tr_members)
		kfree(dev->tr_members);
	kfree(dev);
error_alloc:
error_create:
	mutex_unlock(&td_raid_list_mutex);
	return rc;
}


int td_raid_discover_device(struct td_device *dev, void *meta_data)
{
	struct tr_meta_data_struct *md = meta_data;
	typeof(md->signature.uuid_check) uuid_check;
	char buffer[64];
	struct td_raid *rdev;
	struct tr_member *trm;
	int i, rc;

	if (0) {
		printk("***** RAID DISCOVER DEVICE %p ****\n", dev);
		td_dump_data("MD: ", meta_data, TERADIMM_DATA_BUF_SIZE);
	}

	uuid_check = 0;
	for (i = 0; i < TD_UUID_LENGTH; i++) {
		uint16_t t = md->signature.uuid[i];
		t <<= 8;
		i += 1;
		t += md->signature.uuid[i];
		uuid_check ^= t;
	}
	if (uuid_check != md->signature.uuid_check) {
		td_dev_info(dev, "INVALID RAID Signature: %u[%04x], %u[%04x]\n",
				uuid_check, uuid_check,
				md->signature.uuid_check, md->signature.uuid_check);
		return -EINVAL;
	}
	
	if (md->signature.version != TR_METADATA_VERSION) {
		td_dev_info(dev, "Invalid METADATAVERSION %llx; Cannot rebuild raid\n",
				md->signature.version);
		return -EINVAL;
	}

	mutex_lock(&tr_discovery_mutex);

	td_raid_format_uuid(md->signature.uuid, buffer);
	td_dev_info(dev, "RAID Signature: %s [%llu]\n", buffer,
						md->signature.generation);

	rdev = td_raid_get_uuid(md->signature.uuid);
	if (rdev == NULL) {
		/* Need to create a raid for this */
		rc = td_osdev_assign_name("tr", buffer, sizeof(buffer));
		if (rc) {
			td_dev_err(dev, "Couldn't find raid name for RAID");
			goto error_no_rdev;
		}
		
		for (i = 0; i < TR_META_DATA_CONF_MAX; i++) {
			struct td_ioctl_conf_entry *c = md->raid_info.conf + i;
			if (c->type == TD_RAID_CONF_INVALID)
				break;
		}

		rc = td_raid_create (buffer, md->signature.uuid,
			i, md->raid_info.conf);
		if (rc) {
			td_dev_err(dev, "Could not create discovered raid\n");
			goto error_no_rdev;
		}
		rdev = td_raid_get_uuid(md->signature.uuid);
		BUG_ON(rdev == NULL);

		td_raid_apply_metadata(rdev, md, 0);

	}

	td_raid_lock(rdev);

	/*
	 * Need to add this device to the raid.
	 */
	td_raid_format_uuid(dev->os.uuid, buffer);
	trm = NULL; /* Just make sure */
	for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
		trm = rdev->tr_members + i;
		if (memcmp(trm->trm_uuid, dev->os.uuid, TD_UUID_LENGTH) == 0)
			break;
		trm = NULL;
	}
	if (trm == NULL) {
		td_raid_err(rdev, "Couldn't find member %s\n", buffer);
		goto error_no_dev;
	}

	td_raid_info(rdev, "Found device %d [%s] %s\n", i, buffer, td_device_name(dev));

	/* Make sure it's in a state to be ready for online */
	if (td_eng_hal_online(td_device_engine(dev))) {
		td_raid_err(rdev, "Device '%s' not ready for raid\n", td_device_name(dev));
		goto error_no_dev;
	}

	if (rdev->ops->_check_member) {
		if (rdev->ops->_check_member(rdev, dev) ) {
			td_raid_err(rdev, "Device '%s' not compatible for raid\n", td_device_name(dev));
			goto error_no_dev;
		}
	}

	if (md->signature.generation+1 == rdev->tr_generation) {
		td_raid_init_member(rdev, i, dev, md->member[i].state);
	} else if (md->signature.generation+1 > rdev->tr_generation) {

		td_raid_info(rdev, "Metadata newer than raid, applying\n");
		td_raid_apply_metadata(rdev, md, 1);
		td_raid_init_member(rdev, i, dev, md->member[i].state);
	} else {
		td_raid_warn(rdev, "Old metadata on %d: %s, FAILED\n",
				i, td_device_name(dev));
		td_raid_init_member(rdev, i, dev, TR_MEMBER_FAILED);
	}


	if (rdev->ops->_handle_member) {
		if (rdev->ops->_handle_member(rdev, i) ) {
			td_raid_err(rdev, "Error handling '%s'\n",
					td_device_name(dev));
			return -EIO;
		}
	}

	/* Now see if it's complete */
	if (TR_MEMBERSET_FULL(rdev) ) {
		td_raid_info(rdev, "Discovery complete, attempting to go online\n");
		td_raid_go_online(rdev);
	}

error_no_dev:
	td_raid_unlock(rdev);

	/* Return our reference */
	td_raid_put(rdev);

error_no_rdev:
	mutex_unlock(&tr_discovery_mutex);
	return 0;
}

/**
 * \brief - Make a raid device go online
 * @param dev         - Raid device
 * @return              0 if success, -ERROR
 */
int td_raid_go_online(struct td_raid *dev)
{
	int rc;

	WARN_TD_DEVICE_UNLOCKED(dev);

	/*  already in this state */
	if (!tr_check_dev_state(dev, OFFLINE)
			&& !tr_check_dev_state(dev, CREATED)) {
		td_raid_err(dev, "Cannot go online, not offline\n");
		return -EBUSY;
	}

	if (! dev->ops->_online) {
		td_raid_err(dev, "No online support\n");
		return -ENOENT;
	}

	rc = dev->ops->_online(dev);
	if (rc) {
		td_raid_err(dev, "Unable to go online\n");
		goto error_failed_online;
	}
	
	dev->os.block_params.capacity = tr_conf_var_get(dev, CAPACITY);
	dev->os.block_params.bio_max_bytes = tr_conf_var_get(dev, BIO_MAX_BYTES);
	dev->os.block_params.bio_sector_size = tr_conf_var_get(dev, BIO_SECTOR_SIZE);
	dev->os.block_params.hw_sector_size = tr_conf_var_get(dev, HW_SECTOR_SIZE);
	dev->os.block_params.discard = 0;

	rc = td_osdev_online(&dev->os);
	if (rc) {
		td_raid_err(dev, "Unable to create OS I/O device.\n");
		goto error_failed_online;
	}

	tr_enter_dev_state(dev, ONLINE);

	td_raid_save_meta(dev, 1);
	return 0;

error_failed_online:
	tr_enter_dev_state(dev, OFFLINE);
	return rc;
}

int td_raid_go_offline(struct td_raid *dev)
{
	WARN_TD_DEVICE_UNLOCKED(dev);
	
	/*  already in this state */
	if (!tr_check_dev_state(dev, ONLINE))
		return -EEXIST;

	switch (dev->tr_run_state) {
	case TR_RUN_STATE_DEGRADED:
	case TR_RUN_STATE_FAILED:
		td_raid_warn(dev, "Raid going offline in inconsistent state");
		/* Fall through */
	case TR_RUN_STATE_OPTIMAL:
		break;
		
	default:
		return -EEXIST;
	}


	if (atomic_read(&dev->os.block_users))
		return -EBUSY;

	// TODO: set members offline

	if (td_osdev_offline(&dev->os))
		return -EBUSY;

	tr_enter_dev_state(dev, OFFLINE);

	return 0;
}

/* ---- reference counting interface ---- */
static int __check_raid_name(struct td_osdev *dev, void* data)
{
	if (dev->type != TD_OSDEV_RAID)
		return 0;

	return strncmp(dev->name, data, TD_DEVICE_NAME_MAX) == 0;
}
static int __check_raid_uuid(struct td_osdev *dev, void* data)
{
	if (dev->type != TD_OSDEV_RAID)
		return 0;

	return memcmp(dev->uuid, data, TD_UUID_LENGTH) == 0;
}


struct td_raid *td_raid_get(const char *name)
{
	struct td_osdev *odev = td_osdev_find(__check_raid_name, (void*)name);

	if (odev)
		return td_raid_from_os(odev);

	return NULL;
}
struct td_raid *td_raid_get_uuid(const char *uuid)
{
	struct td_osdev *odev = td_osdev_find(__check_raid_uuid, (void*)uuid);

	if (odev)
		return td_raid_from_os(odev);

	return NULL;
}

void td_raid_put(struct td_raid *dev)
{
	td_osdev_put(&dev->os);
}

int __td_raid_change_member(struct td_raid *rdev, int idx,
		enum td_raid_member_state state, int lock)
{
	struct tr_member *trm = rdev->tr_members + idx;
	
	if (trm->trm_state == state)
		return 0;

	trm->trm_state = state;
	switch (trm->trm_state) {
	case TR_MEMBER_EMPTY:
		TR_MEMBERSET_CLEAR(rdev, idx);
		TR_ACTIVESET_CLEAR(rdev, idx);
		memset(trm->trm_uuid, 0, sizeof(trm->trm_uuid));
		break;

	case TR_MEMBER_FAILED:
	case TR_MEMBER_SYNC:
		TR_ACTIVESET_CLEAR(rdev, idx);
		TR_MEMBERSET_SET(rdev, idx);
		break;

	case TR_MEMBER_ACTIVE:
		TR_MEMBERSET_SET(rdev, idx);
		TR_ACTIVESET_SET(rdev, idx);
		break;


	case TR_MEMBER_SPARE:
	case TD_RAID_MEMBER_STATE_MAX:
		return -EINVAL;
	}

	if (rdev->ops->_handle_member)
		rdev->ops->_handle_member(rdev, idx);

	rdev->tr_generation++;

	if (lock)
		td_raid_lock(rdev);
	td_raid_save_meta(rdev, 0);
	if (lock)
		td_raid_unlock(rdev);

	return 0;
};

int td_raid_change_member(struct td_raid *rdev, int idx,
		enum td_raid_member_state state)
{
	/* Internal things need to lock the rdev */
	return __td_raid_change_member(rdev, idx, state, 1);
}

void __td_raid_fail_member (struct td_raid *rdev, int idx)
{
	struct tr_member *trm = rdev->tr_members + idx;

	TR_ACTIVESET_CLEAR(rdev, idx);
	trm->trm_state = TR_MEMBER_FAILED;
	td_raid_err(rdev, "I/O Error on %s; FAILING device %d\n",
			td_device_name(trm->trm_device), idx);

	if (rdev->ops->_fail_member)
		rdev->ops->_fail_member(rdev, idx);

	rdev->tr_generation++;

}

int td_raid_fail_member (struct td_raid *rdev, int idx)
{
	struct tr_member *trm = rdev->tr_members + idx;

	if (trm->trm_state == TR_MEMBER_FAILED)
		return 0;

	__td_raid_fail_member(rdev, idx);

	td_raid_lock(rdev);
	td_raid_save_meta(rdev, 0);
	td_raid_unlock(rdev);

	return 0;
}

static void td_raid_init_member(struct td_raid *rdev, int member_idx,
		struct td_device *dev, enum td_raid_member_state state)
{
	struct tr_member *trm = rdev->tr_members + member_idx;

	td_device_hold(dev);

	trm->trm_device = dev;
	memcpy(trm->trm_uuid, dev->os.uuid, TD_UUID_LENGTH);

	dev->td_raid = rdev;

	/* Save a UCMD for emergency */
	trm->trm_ucmd = td_ucmd_alloc(TD_PAGE_SIZE);
	BUG_ON(trm->trm_ucmd == NULL);

	trm->trm_ucmd->ioctl.data_len_to_device = TD_PAGE_SIZE;
	trm->trm_ucmd->ioctl.data_len_from_device = 0;

	/* And finally, device is now officially a raid member */
	td_device_enter_state(dev, RAID_MEMBER);
	trm->trm_state = state;

	TR_MEMBERSET_SET(rdev, member_idx);

	trm->trm_counter[TR_MEMBER_COUNT_REQ] = 0xd1ab10;
	trm->trm_counter[TR_MEMBER_COUNT_ERROR] = 0xc0ffee;

	if (state == TR_MEMBER_ACTIVE)
		TR_ACTIVESET_SET(rdev, member_idx);
	else
		TR_ACTIVESET_CLEAR(rdev, member_idx);

	td_raid_info(rdev, "dev [%s] added as member %d: %s\n",
		td_device_name(dev), member_idx, tr_member_state_name(state));


}

static void td_raid_apply_metadata (struct td_raid *rdev,
		struct tr_meta_data_struct *md, int redo)
{
	int i;

	if (redo) {
		/* We need to verify the conf, in theory, this can never change */
		for (i = 0; i < TR_META_DATA_CONF_MAX; i++) {
			struct td_ioctl_conf_entry *c = md->raid_info.conf + i;
			uint64_t cval;

			if (c->type == TD_RAID_CONF_INVALID)
				break;

			if (td_raid_get_conf(rdev, c->type, c->var, &cval) ) {
				td_raid_info(rdev, "apply: unknown %u:%u =%llu\n",
					c->type, c->var, c->val);
				continue;
			}

			if (c->val == cval)
				continue;

			td_raid_err(rdev, "Apply: Mismatch conf: %u:%u = %llu\n",
					c->type, c->var, c->val);
		}
	}

	/* And we always increase our generation when we re-make it */
	rdev->tr_generation = md->signature.generation + 1;

	for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
		struct tr_member *trm;
		if (1) {
			char buffer[64];
			td_raid_format_uuid(md->member[i].uuid, buffer);
			td_raid_info(rdev, "DEV %d: %s [%x]\n",
				i, buffer, md->member[i].state);
		}

		trm = rdev->tr_members + i;

		/* initialize members to empty. as the remaining
		 * members are discovered, their state is converted to
		 * active or failed depending on the matching of meta
		 * data */
		trm->trm_state = TR_MEMBER_EMPTY;

		if (! redo) {
			/* Easy case, update and go on */
			memcpy(trm->trm_uuid, md->member[i].uuid, TD_UUID_LENGTH);
			continue;
		}

		/*
		 * We have a device here, we need to see if it matches this
		 * metadata
		 */
		if (1) {
			char buffer[64];
			td_raid_format_uuid(trm->trm_uuid, buffer);
			td_raid_info(rdev, "old %d: %s [%x]\n",
					i, buffer, trm->trm_state);
		}

		/* Do we match? */
		if (memcmp(trm->trm_uuid, md->member[i].uuid, TD_UUID_LENGTH) == 0) {
			trm->trm_state = TR_MEMBER_FAILED;
			TR_ACTIVESET_CLEAR(rdev, i);
			td_raid_info(rdev, "Flipping %d (%s) to FAILED\n", i,
					trm->trm_device ?
						td_device_name(trm->trm_device):
						"<unknown>"
					);

			continue;
		}

		if (trm->trm_device) {
			td_raid_info(rdev, "Removing member %u: %s\n",
					i, td_device_name(trm->trm_device));

			td_device_lock(trm->trm_device);
			trm->trm_device->td_raid = NULL;
			td_device_enter_state(trm->trm_device, OFFLINE);
			td_device_unlock(trm->trm_device);
			td_device_put(trm->trm_device);

			/* And we're done with it */
			trm->trm_device = NULL;
		}

		if (trm->trm_ucmd)
			td_ucmd_put(trm->trm_ucmd);
		TR_ACTIVESET_CLEAR(rdev, i);
		TR_MEMBERSET_CLEAR(rdev, i);
	}

	rdev->tr_generation = md->signature.generation+1;
}

static void td_raid_resync_wait(struct td_raid *dev)
{
	if (dev->resync_context.resync_task) {
		struct task_struct *rts = dev->resync_context.resync_task;
		if (rts)
			kthread_stop(rts);
		printk("Waiting for resync to stop\n");
		while (dev->resync_context.resync_task)
			schedule();
	}
}

int td_raid_attach_device(struct td_raid *rdev, const char *dev_name)
{
	int rc, i;
	struct td_device *dev;

	WARN_TD_DEVICE_UNLOCKED(rdev);

	dev = td_device_get(dev_name);
	if (dev == NULL) {
		rc = -ENOENT;
		pr_err("dev %s not found\n", dev_name);
		goto error_get_dev;
	}

	/* Lock the device, so we can control it's state */
	td_device_lock(dev);

	if (! (td_device_check_state(dev, OFFLINE) ||
			td_device_check_state(dev, CREATED) ) )
	{
		td_raid_err(rdev, "Device \"%s\" not ready for attaching\n",
				td_device_name(dev));
		rc = -EIO;
		goto error_not_capable;
	}

	if (! td_run_state_check(td_device_engine(dev), RUNNING) ) {
		td_raid_err(rdev, "Engine \"%s\" not ready for attaching\n",
				td_device_name(dev));
		rc = -EIO;
		goto error_not_capable;
	}

	if (! td_eng_conf_hw_var_get(td_device_engine(dev), RAID_PAGE) ) {
		rc = -EMEDIUMTYPE;
		pr_err("dev %s not mcs-raid capable\n", dev_name);
		goto error_not_capable;
	}

	rc = -ESRCH;
	for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
		struct tr_member *trm = rdev->tr_members + i;
		struct td_engine *eng;

		/* We are looking for an empty slot */
		if (trm->trm_device)
			continue;

		/* This not should be marked as present */
		WARN_ON(TR_MEMBERSET_TEST(rdev, i));

		eng = td_device_engine(dev);

		/* Make sure it's in a state to be ready for online */
		if (td_eng_hal_online(eng)) {
			td_raid_err(rdev, "Device '%s' not ready for raid\n", td_device_name(dev));
			rc = -EIO;
			break;
		}


		if (rdev->ops->_check_member) {
			if (rdev->ops->_check_member(rdev, dev) ) {
				td_raid_err(rdev, "Device '%s' not compatible for raid\n", td_device_name(dev));
				rc = -EIO;
				break;
			}
		}
		
		if (TR_MEMBERSET_EMPTY(rdev) ) {
			td_raid_info(rdev, "Empty raid initialized with %i: %s\n",
					i, td_device_name(dev));
			td_raid_init_member(rdev, i, dev, TR_MEMBER_ACTIVE);
		} else {
			td_raid_init_member(rdev, i, dev, TR_MEMBER_SYNC);
		}

		if (rdev->ops->_handle_member) {
			if (rdev->ops->_handle_member(rdev, i) ) {
				td_raid_err(rdev, "Error handling '%s'\n",
						td_device_name(dev));
				return -EIO;
			}
		}
	
		/* And since we found one, we can exit the search */
		rc = 0;
		break;
	}

	/* Something was changed */
	td_raid_save_meta(rdev, 1);

	/* Unlock and return our reference  */

error_not_capable:
	td_device_unlock(dev);
	td_device_put(dev);
error_get_dev:

	if (rc)
		pr_err("Failed to attach device '%s' to raid '%s', error=%d.\n",
				dev_name, rdev->os.name, rc);
	return rc;
}

static int td_raid_member_find(struct td_raid *rdev, const char* dev_name)
{
	int rc, i;
	struct td_device *dev;

	rc = -ENODEV;
	/* this will get a reference to it too */
	dev = td_device_get(dev_name);
	if (dev == NULL) {
		rc = -ENOENT;
		pr_err("dev %s not found\n", dev_name);
		return -EINVAL;
	}

	/* Lock the device for consistency */
	td_device_lock(dev);

	rc = -EINVAL;
	if (!td_device_check_state(dev, RAID_MEMBER)) {
		pr_err("dev %s not raid member\n", dev_name);
		goto error_member;
	}
	if (dev->td_raid != rdev) {
		td_raid_err(rdev, "dev %s not a member", dev_name);
		goto error_member;
	}

	for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
		struct tr_member *trm = rdev->tr_members + i;

		if (trm->trm_device != dev)
			continue;

		/* This should be marked as present */
		WARN_ON(! TR_MEMBERSET_TEST(rdev, i));

		/* We return this index.  The device has a reference, and is
		 * locked, caller must manage that */
		return i;
	}

error_member:
	td_device_unlock(dev);
	td_device_put(dev);
	return rc;
}

int td_raid_detach_device(struct td_raid *rdev, const char *dev_name)
{
	int idx, sync_cnt;
	struct td_device *dev;
	struct tr_member *trm;

	sync_cnt = 0;
	for (idx = 0; idx < tr_conf_var_get(rdev, MEMBERS); idx++) {
		if (tr_raid_member_check_state(rdev->tr_members + idx, SYNC))
			sync_cnt++;
	}

	/* This does a lock on the member for us when if it gets it, in
	 * addition to the get reference */
	idx = td_raid_member_find(rdev, dev_name);

	if (idx < 0)
		return idx;

	trm = rdev->tr_members + idx;
	dev = trm->trm_device;
	BUG_ON(!dev);

	switch (trm->trm_state) {
	case TR_MEMBER_ACTIVE:
		if (tr_check_dev_state(rdev, ONLINE)) {
			td_raid_err(rdev, "Can't remove active member from online raid");

			/* Unlock, and return our reference */
			td_device_unlock(dev);
			td_device_put(dev);
			return -EBUSY;
		}
		if (sync_cnt) {
			td_raid_err(rdev, "Can't remove active member while volume sync in progress");

			/* Unlock, and return our reference */
			td_device_unlock(dev);
			td_device_put(dev);
			return -EBUSY;
		}

		break;
	case TR_MEMBER_SYNC:
		trm->trm_state = TR_MEMBER_FAILED;
		td_raid_resync_wait(rdev);
		break;
	default:
		break;
	}

	/* We alrady have the rdev lock, so we can't try to lock it*/
	__td_raid_change_member(rdev, idx, TR_MEMBER_EMPTY, 0);

	/* This device is now just OFFLINE */
	td_device_enter_state(dev, OFFLINE);
	trm->trm_device = NULL;

	if (trm->trm_ucmd) {
		td_ucmd_put(trm->trm_ucmd);
		trm->trm_ucmd = NULL;
	}

	/**! we need to release the main raid reference */
	td_device_put(dev);

	/* And now we unlock/return our reference */
	td_device_unlock(dev);
	td_device_put(dev);

	return 0;
}

int td_raid_fail_device(struct td_raid *rdev, const char* dev_name)
{
	int idx, rc, sync_cnt;
	struct td_device *dev;
	struct tr_member *trm;

	td_raid_info(rdev, "Attempt to FAIL device \"%s\"\n", dev_name);

	sync_cnt = 0;
	for (idx = 0; idx < tr_conf_var_get(rdev, MEMBERS); idx++) {
		if (tr_raid_member_check_state(rdev->tr_members + idx, SYNC))
			sync_cnt++;
	}

	/* This does a lock on the member for us when if it gets it, in
	 * addition to the get reference */
	idx = td_raid_member_find(rdev, dev_name);

	if (idx < 0)
		return idx;

	trm = rdev->tr_members + idx;
	dev = trm->trm_device;
	BUG_ON(!dev);

	switch (trm->trm_state) {
	case TR_MEMBER_FAILED: /* no action required */
		rc = 0;
		goto done;
	case TR_MEMBER_ACTIVE: /* don't fail if sync in progress */
		if (sync_cnt) {
			td_raid_info(rdev, "Can't FAIL active device \"%s\" while volume sync in progress\n", dev_name);
			rc = -EBUSY;
			goto done;
		}
		break;
	default:
		break;
	}

	__td_raid_fail_member(rdev, idx);

	rc = 0;

	/* Check if resync is running */
	td_raid_resync_wait(rdev);

	/* The ioctl has the rdev lock already, so we don't lock this time */
	td_raid_save_meta(rdev, 1);

done:
	/* And now we unlock/return our reference */
	td_device_unlock(dev);
	td_device_put(dev);

	return rc;
}

int td_raid_list_members(struct td_raid *rdev,
		char *buf, size_t len, uint32_t *count)
{
	int rc = 0;
	char *p, *e;
	int i;

	WARN_TD_DEVICE_UNLOCKED(rdev);

	*count = 0;

	if (len < TD_DEVICE_NAME_MAX)
		return -ETOOSMALL;

	p = buf;
	e = buf + len - TD_DEVICE_NAME_MAX;

	for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
		struct tr_member *trm = rdev->tr_members+i;
		if (!trm->trm_device)
			continue;

		rc = snprintf(p, e-p, "%s\n", td_device_name(trm->trm_device));
		if (rc<0)
			break;

		p += rc;
		(*count) ++;

		if ((e-p) < TD_DEVGROUP_NAME_MAX) {
			rc = -ETOOSMALL;
			break;
		}
	}

	if (rc<0)
		return rc;

	*p=0;

	return p-buf;
}

int td_raid_get_info (struct td_raid *rdev, struct td_ioctl_raid_info *info)
{
	memcpy(info->raid_uuid, rdev->os.uuid, sizeof(rdev->os.uuid));
	info->raid_level = tr_conf_var_get(rdev, LEVEL);
	
	return 0;
}

int td_raid_get_state (struct td_raid *rdev, struct td_ioctl_raid_state *state)
{
	int i;
	state->run_state = rdev->tr_run_state;
	state->dev_state = rdev->tr_dev_state;

	state->storage_capacity = rdev->os.block_params.capacity;

	state->control_users = atomic_read(&rdev->os.control_users);
	state->block_users   = atomic_read(&rdev->os.block_users);
	
	i = 0;
	for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
		struct tr_member *trm = rdev->tr_members + i;
		state->members[i].state = trm->trm_state;
		memcpy(state->members[i].uuid, trm->trm_uuid, TD_UUID_LENGTH);
		if (trm->trm_device)
			memcpy(state->members[i].td_name, td_device_name(trm->trm_device), TD_DEVICE_NAME_MAX);
	}
	state->members_count = i;
	
	return 0;
}

int td_raid_get_counters (struct td_raid *rdev, struct td_ioctl_counters *cntrs,
		int fill_mode)
{
	// td_ioctl_device_get_counters()
	int i, rc = -EINVAL;

	WARN_TD_DEVICE_UNLOCKED(rdev);

	if (fill_mode) {
		/* determine the total number of counters */
		i = TR_GENERAL_COUNT_MAX;

		/* Member counters */
		i += TR_MEMBER_COUNT_MAX * tr_conf_var_get(rdev, MEMBERS);

		/* How many counters do our ops have */
		i += rdev->ops_counter_max;

		if (cntrs->count < i) {
			/* the user buffer isn't big enough, return the count required */
			cntrs->count = i;
			rc = -ENOBUFS;
			goto error;
		}

		cntrs->count = 0; /* increase the count in loop body */

		for (i = 0 ; i < TR_GENERAL_COUNT_MAX ; i++) {
			cntrs->entries[cntrs->count].type = TD_RAID_COUNTER_GENERAL;
			cntrs->entries[cntrs->count].var = i;
			cntrs->entries[cntrs->count].val = rdev->counter[i];

			cntrs->count++;
		}

		for (i = 0 ; i < TR_MEMBER_COUNT_MAX ; i++) {
			int m;
			for (m = 0; m < tr_conf_var_get(rdev, MEMBERS); m++) {
				struct tr_member *trm = rdev->tr_members + m;
				cntrs->entries[cntrs->count].type = TD_RAID_COUNTER_MEMBER;
				cntrs->entries[cntrs->count].part.var = i;
				cntrs->entries[cntrs->count].part.id = m;
				cntrs->entries[cntrs->count].val = trm->trm_counter[i];

				cntrs->count++;
			}
		}

		/* fill the read counters */
		for (i = 0 ; i < rdev->ops_counter_max; i++) {
			cntrs->entries[cntrs->count].type = TD_RAID_COUNTER_OPS;
			cntrs->entries[cntrs->count].part.var = i;
			cntrs->entries[cntrs->count].part.id = tr_conf_var_get(rdev, LEVEL);
			rc = rdev->ops->_get_counter(rdev,
					cntrs->entries[cntrs->count].part.var,
					&cntrs->entries[cntrs->count].val);

			if (rc == -ENOENT)
				continue;
			if (rc)
				goto error;

			cntrs->count++;
		}

		if (rc == -ENOENT)
			rc = 0;
	} else {
		for (i = 0; i < cntrs->count; i++) {
			switch (cntrs->entries[i].type) {
			case TD_RAID_COUNTER_GENERAL:
				if  (cntrs->entries[i].var < TR_GENERAL_COUNT_MAX)
					cntrs->entries[i].val = rdev->counter[cntrs->entries[i].var];
				else
					rc = -ENOENT;
				break;

			case TD_RAID_COUNTER_MEMBER:
				td_raid_warn(rdev, "GET_COUNTER for MEMBER not supported\n");
				rc = -EINVAL;
				break;
			case TD_RAID_COUNTER_OPS:
				if (i < rdev->ops_counter_max)
					rc = rdev->ops->_get_counter(rdev,
							cntrs->entries[i].var,
							&cntrs->entries[i].val);
				else
					rc = -ENOENT;
				break;
			}

			if (rc)
				break;
		}
		cntrs->count = i;
		if (rc == -ENOENT)
			rc = 0;
	}

error:
	return rc;
}


static int __td_raid_ioctl(struct td_raid* rdev, unsigned int cmd, unsigned long raw_arg)
{
	int rc;
	union {
		struct td_ioctl_device_name             	member_name;
		struct td_ioctl_device_list             	member_list;
		struct td_ioctl_raid_info                       raid_info;
		struct td_ioctl_raid_state                      raid_state;
		struct td_ioctl_conf                            conf;
		struct td_ioctl_counters                        counters;
#ifdef CONFIG_TERADIMM_SGIO
		sg_io_hdr_t sg_hdr;
#endif
	} __user *u_arg, *k_arg, __static_arg, *__big_arg = NULL;

	unsigned copy_in_size, copy_out_size, big_size = 0;

	/* prepare */

	u_arg = (__user void*)raw_arg;
	k_arg = &__static_arg;

	copy_in_size = 0;
	copy_out_size = 0;
	switch (cmd) {
	case TD_IOCTL_RAID_GET_ALL_CONF:
	case TD_IOCTL_RAID_GET_CONF:
	case TD_IOCTL_RAID_SET_CONF:
		/* copy in the base structure */
		rc = -EFAULT;
		if (copy_from_user(k_arg, u_arg,
					sizeof(struct td_ioctl_conf)))
			goto bail_ioctl;

		/* based on count provided, figure out how much actually */
		big_size = TD_IOCTL_CONF_SIZE(k_arg->conf.count);

		copy_in_size = big_size;
		if (cmd != TD_IOCTL_DEVICE_SET_CONF)
			copy_out_size = copy_in_size;
		break;

	case TD_IOCTL_RAID_GET_COUNTERS:
	case TD_IOCTL_RAID_GET_ALL_COUNTERS:
		/* copy in the base structure */
		rc = -EFAULT;
		if (copy_from_user(k_arg, u_arg,
					sizeof(struct td_ioctl_counters)))
			goto bail_ioctl;

		/* based on count provided, figure out how much actually */
		big_size = TD_IOCTL_COUNTER_SIZE(k_arg->counters.count);

		copy_in_size = big_size;
		copy_out_size = copy_in_size;
		break;

	case TD_IOCTL_RAID_GET_MEMBER_LIST:
		/* copy in the base structure */
		rc = -EFAULT;
		copy_in_size = sizeof(k_arg->member_list);
		if (copy_from_user(k_arg, u_arg, copy_in_size))
			goto bail_ioctl;

		if (!k_arg->member_list.buf_size)
			goto bail_ioctl;

		/* based on count provided, figure out how much actually */
		big_size = k_arg->member_list.buf_size;
		copy_out_size = big_size;
		break;

	case TD_IOCTL_RAID_ADD_MEMBER:
	case TD_IOCTL_RAID_DEL_MEMBER:
		rc = -EFAULT;
		copy_in_size = sizeof(k_arg->member_list);
		if (copy_from_user(k_arg, u_arg, copy_in_size))
			goto bail_ioctl;

		if (!k_arg->member_list.buf_size)
			goto bail_ioctl;

		big_size = k_arg->member_list.buf_size;
		copy_in_size = big_size;
		break;
		
	case TD_IOCTL_RAID_FAIL_MEMBER:
		copy_in_size = sizeof(k_arg->member_name);
		break;

	case TD_IOCTL_RAID_GET_INFO:
		copy_out_size = sizeof(k_arg->raid_info);
		break;
		
	case TD_IOCTL_RAID_GET_STATE:
		big_size = TD_IOCTL_RAID_STATE_SIZE(tr_conf_var_get(rdev, MEMBERS));
		copy_out_size = big_size;
		break;

#ifdef CONFIG_TERADIMM_SGIO
	case SG_IO:
		copy_in_size = sizeof(sg_io_hdr_t);
		copy_out_size = sizeof(sg_io_hdr_t);
		break;
#endif

	default:
		/* nothing to copy in */
		break;
	}

	/* allocate a big buffer if needed */
	if (big_size) {
		rc = -ENOMEM;
		__big_arg = kzalloc(big_size, GFP_KERNEL);
		if (!__big_arg) {
			pr_err("RAID ioctl failed to allocate %u bytes.",
					big_size);
			goto bail_ioctl;
		}
		k_arg = __big_arg;
	}

	/* copy in the data struct */

	if (copy_in_size) {
		rc = -EFAULT;
		if (copy_from_user(k_arg, u_arg, copy_in_size)) {
			pr_err("RAID ioctl failed to copy in %u bytes.",
					copy_in_size);
			goto bail_ioctl;
		}
	}

	/* check if output can be written to */

	if (copy_out_size) {
		rc = -EFAULT;
		if (!access_ok(VERIFY_WRITE, u_arg, copy_out_size)) {
			pr_err("RAID ioctl cannot write %u bytes.",
					copy_out_size);
			goto bail_ioctl;
		}
	}

	/* perform the operation under lock to prevent races with other users */

	td_raid_lock(rdev);

	switch (cmd) {
	case TD_IOCTL_RAID_RESYNC:
		rc = td_ioctl_raid_resync(rdev);
		break;
	case TD_IOCTL_RAID_GET_ALL_CONF:
	case TD_IOCTL_RAID_GET_CONF:
		rc = td_ioctl_raid_get_conf(rdev, &k_arg->conf,
				cmd == TD_IOCTL_RAID_GET_ALL_CONF);
		break;
#if 0
	case TD_IOCTL_RAID_SET_CONF:
	/* No set conf yet */
		rc = td_ioctl_raid_set_conf(rdev, &k_arg->conf);
		break;
#endif

	case TD_IOCTL_RAID_GET_COUNTERS:
	case TD_IOCTL_RAID_GET_ALL_COUNTERS:
		rc = td_raid_get_counters(rdev, &k_arg->counters,
				cmd == TD_IOCTL_RAID_GET_ALL_COUNTERS);
		break;

	case TD_IOCTL_RAID_GET_MEMBER_LIST:
		/** ioctl used to query available device groups */
		rc = td_raid_list_members(rdev,
				k_arg->member_list.buffer,
				k_arg->member_list.buf_size,
				&k_arg->member_list.dev_count);

		if (rc >= 0) {
			k_arg->member_list.buf_size = rc;
			copy_out_size = sizeof(k_arg->member_list) + rc;
			rc = 0;
		}
		break;

	case TD_IOCTL_DEVICE_GO_ONLINE:
		/** ioctl used to enter on-line mode */
		rc = td_raid_go_online(rdev);
		break;

	case TD_IOCTL_DEVICE_GO_OFFLINE:
		/** ioctl used to enter off-line mode */
		rc = td_raid_go_offline(rdev);
		break;

	case TD_IOCTL_RAID_ADD_MEMBER:
		rc = td_raid_attach_device(rdev, k_arg->member_list.buffer);
		break;

	case TD_IOCTL_RAID_DEL_MEMBER:
		rc = td_raid_detach_device(rdev, k_arg->member_list.buffer);
		break;

	case TD_IOCTL_RAID_FAIL_MEMBER:
		rc = td_raid_fail_device(rdev, k_arg->member_name.dev_name);
		break;

	case TD_IOCTL_RAID_GET_INFO:
		rc = td_raid_get_info(rdev, &k_arg->raid_info);
		break;

	case TD_IOCTL_RAID_GET_STATE:
		rc = td_raid_get_state(rdev, &k_arg->raid_state);
		break;

	case TD_IOCTL_RAID_METASAVE:
		td_raid_save_meta(rdev, 1);
		rc = 0;
		break;
#ifdef CONFIG_TERADIMM_SGIO
	case SG_IO:
		rc = td_block_sgio(&rdev->os, &k_arg->sg_hdr);
		break;
#endif

	default:
		rc = -ENOIOCTLCMD;
		break;
	}

	td_raid_unlock(rdev);

	switch(rc) {
	case 0:
		break;

	case -ENOBUFS:
		/* in these cases we'd like to copy_to_user so that
		 * the user app can check the partial results and allocate
		 * a larger buffer.
		 */
		break;

	default:
		goto bail_ioctl;
	}

	/* copy data back */

	if (copy_out_size) {
		if (copy_to_user(u_arg, k_arg, copy_out_size)) {
			rc = -EFAULT;
			pr_err("RAID ioctl failed to copy out %u bytes.",
					copy_out_size);
			goto bail_ioctl;
		}
	}

bail_ioctl:
	if (__big_arg)
		kfree(__big_arg);
	return rc;
}

static void __td_raid_destroy (struct td_raid *rdev)
{
	if (rdev->tr_members)
		kfree(rdev->tr_members);
	kfree(rdev);
}

/* --- td_osdev callback interface --- */

void td_raid_destroy (struct td_osdev* dev)
{
	__td_raid_destroy(td_raid_from_os(dev));
}

int td_raid_ioctl (struct td_osdev* dev, unsigned int cmd,
		unsigned long raw_arg)
{
	if (dev->type != TD_OSDEV_RAID)
		return -ENODEV;

	return __td_raid_ioctl(td_raid_from_os(dev), cmd, raw_arg);
}
