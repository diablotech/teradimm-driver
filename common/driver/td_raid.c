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
#include "td_dev_ata.h"
#include "td_memspace.h"
#include "td_biogrp.h"

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


/* Forward declartions - we give these functions to the osdev API */
void td_raid_destroy (struct td_osdev *odev);
int td_raid_ioctl(struct td_osdev* rdev, unsigned int cmd, unsigned long raw_arg);

/* Other forward declarations */
static void td_raid_save_meta (struct td_raid *rdev);
static void td_raid_update_member(struct td_raid *rdev, int member_idx,
		struct td_device *dev);


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
		if (! (rdev->tr_member_mask & 1UL<<i))
			continue;

		trm = rdev->tr_members + i;
		BUG_ON(trm->trm_device == NULL);

		memcpy(md->member[i].uuid, trm->trm_device->os.uuid, TD_UUID_LENGTH);
		md->member[i].state = trm->trm_state;
		md->member[i].generation = 1;
	}

}

static void td_raid_save_meta (struct td_raid *rdev)
{
	struct tr_meta_data_struct *md;
	int i;

	/* We must be locked, so only one at a time */
	WARN_TD_DEVICE_UNLOCKED(rdev);

td_raid_warn(rdev, "***** Saving metadata *****\n");
	md = kmap(rdev->tr_meta_page);

	__td_raid_fill_meta(rdev, md);
	td_dump_data("RAID: ", md, sizeof(*md));

	for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
		struct tr_member *trm;
		struct td_engine *eng;
		if (! (rdev->tr_member_mask & 1UL<<i))
			continue;

		trm = rdev->tr_members + i;
		BUG_ON(trm->trm_device == NULL);

		eng = td_device_engine(trm->trm_device);

		td_eng_cmdgen(eng, set_params, trm->ucmd->ioctl.cmd, 13);

		/* The UCMD boiler code, minus the wait */
		td_ucmd_ready(trm->ucmd);
		td_ucmd_get(trm->ucmd);
		td_enqueue_ucmd(eng, trm->ucmd);
		td_engine_poke(eng);
	}

	for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
		struct tr_member *trm;
		if (! (rdev->tr_member_mask & 1UL<<i))
			continue;

		trm = rdev->tr_members + i;

		/* Now we wait */
		td_ucmd_wait(trm->ucmd);
	}

	kunmap((void*)md);
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
	int rc;
	struct td_raid *dev;
	mutex_lock(&td_raid_list_mutex);

	rc = -ENOENT;
	dev = td_raid_get(name);
	if (!dev) {
		pr_err("ERROR: Could not find device '%s'.\n", name);
		goto bail_no_ref;
	}
	td_raid_lock(dev);

	/*
	 * Check if all members are remvoed */
	rc = -EBUSY;
	if (dev->tr_member_mask) {
		td_raid_err(dev, "Cannod delete: Active members\n");
		goto bail_active_members;
	}

	/* check if the raid is bing used (mounted/open/etc) */
	rc = -EBUSY;
	if (atomic_read(&dev->os.control_users)
			|| atomic_read(&dev->os.block_users)) {
		goto bail_in_use;
	}
	NO_RD_DEBUG(dev, "unregister\n");

	switch (td_raid_state(dev)) {
	case TD_RAID_STATE_DEGRADED:
	case TD_RAID_STATE_FAILED:
	case TD_RAID_STATE_RESYNC:
		td_raid_warn(dev, "Raid destroyed in inconsistent state");
		/* Fall through */
	case TD_RAID_STATE_OPTIMAL:
		td_osdev_offline(&dev->os);

	default:
		/* break */;

	}

	td_osdev_unregister(&dev->os);
	td_raid_list_count --;

	if (dev->ops) {
		rc = dev->ops->_destroy(dev);
		if (rc)
			td_raid_warn(dev, "Raid ops cleanup rc = %d\n", rc);
	}

	NO_RD_DEBUG(dev, "done\n");
	rc = 0;

bail_in_use:
bail_active_members:
	NO_RD_DEBUG(dev, "unlock\n");
	td_raid_unlock(dev);
	/* return the reference obtained above with _get() */
	td_raid_put(dev);

bail_no_ref:

	mutex_unlock(&td_raid_list_mutex);

	if(!rc) {
		NO_RD_DEBUG(dev, "put\n");
		/* return the reference held by the OS */
		td_raid_put(dev);
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


#ifdef CONFIG_TERADIMM_DEPREICATED_RAID_CREATE_V0
/** 
 * \brief create a raid device
 * 
 * @param name    - name of device to create
 * @param uuid    - UUID of device
 * @param level   - Raid level of device, for config
 * @param member_count  - Number of members the raid should expect
 * @return 0 if success, -ERROR
 *
 */
int td_raid_create_v0 (const char *name, const uint8_t uuid[TR_UUID_LENGTH],
		enum td_raid_level level, int members_count)
{
	struct td_ioctl_conf_entry conf[3];
	conf[0].type = TD_RAID_CONF_GENERAL;
	conf[0].var = TR_CONF_GENERAL_LEVEL;
	conf[0].val = level;
	conf[1].type = TD_RAID_CONF_GENERAL;
	conf[1].var = TR_CONF_GENERAL_MEMBERS;
	conf[1].val = members_count;

	return td_raid_create(name, uuid, 2, conf);
}
#endif

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

	printk("CREATING RAID DEVCE \"%s\"\n", name);

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
		td_raid_err(dev, "Failed to init raid level %d\n", raid_level);
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

	rc = sizeof(struct tr_member) * tr_conf_var_get(dev, MEMBERS);
	dev->tr_members = kzalloc(rc, GFP_KERNEL);
	rc = -ENOMEM;
	if (!dev->tr_members)
		goto error_members;

	dev->tr_meta_page = alloc_page(GFP_KERNEL);
	

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


	init_completion(&dev->tr_state_change_completion);

	rc = td_osdev_register(&dev->os);
	if (rc) {
		td_raid_err(dev, "Failed to register OS device, err=%d.\n", rc);
		goto error_os_register;
	}

	td_raid_enter_state(dev, CREATED);

	pr_err("%s:%d: rdev %p in state %d\n", __func__, __LINE__, dev, dev->tr_state);

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

	if (dev->tr_meta_page)
		__free_page(dev->tr_meta_page);

	dev->ops = NULL;

	if (dev->tr_members)
		kfree(dev->tr_members);
	kfree(dev);
error_alloc:
error_create:
	mutex_unlock(&td_raid_list_mutex);
	return rc;
}


#if 0
int td_raid_create_new (const char *name, const uint8_t uuid[TR_UUID_LENGTH],
		enum td_raid_level level, int members_count)
{

	uint32_t size = TD_IOCTL_DEVICE_CONF_SIZE(1);
	struct td_ioctl_device_conf *conf = kmalloc(size, GFP_KERNEL);

	strncpy(create.raid_name, td->raid_name, sizeof(create.raid_name));
	memcpy(create->raid_name, name, sizeof(create->raid_name)
	create.raid_members_count = td->val2;

	kfree(create);
}
#endif

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
	td_dev_info(dev, "RAID Signature: %s\n", buffer);

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

		for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
			trm = rdev->tr_members + i;
			memcpy(trm->trm_uuid, md->member[i].uuid, TD_UUID_LENGTH);
		}
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
		if (rdev->ops->_check_member(rdev, dev, rdev->tr_member_mask == 0) ) {
			td_raid_err(rdev, "Device '%s' not compatible for raid\n", td_device_name(dev));
			goto error_no_dev;
		}
	}

	td_raid_update_member(rdev, i, dev);
	/* Now see if it's complete */
	if (rdev->tr_member_mask == (1UL << tr_conf_var_get(rdev, MEMBERS)) - 1) {
		td_raid_info(rdev, "Discovery complete, going online\n");
		td_raid_go_online(rdev);
	}

error_no_dev:
	td_raid_unlock(rdev);

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
	if (!td_raid_check_state(dev, OFFLINE)
			&& !td_raid_check_state(dev, CREATED)) {
		td_raid_err(dev, "Cannot go online, not offline\n");
		return -EBUSY;
	}
	
	if (dev->tr_member_mask != (1UL << tr_conf_var_get(dev, MEMBERS)) - 1) {
		td_raid_err(dev, "Cannot go online, members missing\n");
		return -EINVAL;
	}
	
	if (dev->ops->_online) {
		rc = dev->ops->_online(dev);
		if (rc) {
			td_raid_err(dev, "Unable to go online\n");
			goto error_failed_online;
		}
	}

	rc = td_osdev_online(&dev->os);
	if (rc) {
		td_raid_err(dev, "Unable to create OS I/O device.\n");
		goto error_failed_online;
	}

	/* Since for now we only handle optimal */
	td_raid_enter_state(dev, OPTIMAL);

	td_raid_save_meta(dev);
	return 0;

error_failed_online:
	td_raid_enter_state(dev, OFFLINE);
	return rc;
}

int td_raid_go_offline(struct td_raid *dev)
{
	WARN_TD_DEVICE_UNLOCKED(dev);

	switch (td_raid_state(dev)) {
	case TD_RAID_STATE_DEGRADED:
	case TD_RAID_STATE_FAILED:
	case TD_RAID_STATE_RESYNC:
		td_raid_warn(dev, "Raid going offline in inconsistent state");
		/* Fall through */
	case TD_RAID_STATE_OPTIMAL:
		break;
		
	default:
		return -EEXIST;
	}


	if (atomic_read(&dev->os.block_users))
		return -EBUSY;

	// TODO: set members offline

	td_osdev_offline(&dev->os);

	td_raid_enter_state(dev, OFFLINE);

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

static void td_raid_update_member(struct td_raid *rdev, int member_idx,
		struct td_device *dev)
{
	struct tr_member *trm = rdev->tr_members + member_idx;
	int rc;

	td_device_hold(dev);

	trm->trm_device = dev;
	memcpy(trm->trm_uuid, dev->os.uuid, TD_UUID_LENGTH);
	trm->trm_state = TR_MEMBER_ACTIVE;
	rdev->tr_member_mask |= (1<<member_idx);

	trm->ucmd = kmalloc(sizeof(struct td_ucmd), GFP_KERNEL);
	BUG_ON(trm->ucmd == NULL);

	td_ucmd_init(trm->ucmd);
	/* this extra GET means it stays around forever */
	td_ucmd_get(trm->ucmd);

	trm->ucmd->ioctl.data_len_to_device = 4096;
	trm->ucmd->ioctl.data_len_from_device = 0;
	trm->ucmd->ioctl.data = rdev->tr_meta_page;
	rc = td_ucmd_map(trm->ucmd, NULL, (unsigned long) trm->ucmd->ioctl.data);
	BUG_ON(rc);

	/* And finally, device is now officially a raid member */
	td_device_enter_state(dev, RAID_MEMBER);
}


int td_raid_add_member(struct td_raid *rdev, struct td_device *dev)
{
	int i;

	WARN_TD_DEVICE_UNLOCKED(rdev);

	for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
		struct tr_member *trm = rdev->tr_members + i;
		struct td_engine *eng;

		if (trm->trm_device)
			continue;

		/* This not should be marked as present */
		WARN_ON(rdev->tr_member_mask & 1<<i);

		eng = td_device_engine(dev);

		/* Make sure it's in a state to be ready for online */
		if (td_eng_hal_online(eng)) {
			td_raid_err(rdev, "Device '%s' not ready for raid\n", td_device_name(dev));
			break;
		}


		if (rdev->ops->_check_member) {
			if (rdev->ops->_check_member(rdev, dev, rdev->tr_member_mask == 0) ) {
				td_raid_err(rdev, "Device '%s' not compatible for raid\n", td_device_name(dev));
				break;
			}
		}
		
		td_raid_update_member(rdev, i, dev);
		return 0;
	}

	return -EINVAL;
}

int td_raid_attach_device(struct td_raid *rdev, const char *dev_name)
{
	int rc;
	struct td_device *dev;

	WARN_TD_DEVICE_UNLOCKED(rdev);

	/* Check things that can fail */
	if (!td_raid_check_state(rdev, CREATED) &&
	    !td_raid_check_state(rdev, OFFLINE)) {
		rc = -EBUSY;
		pr_err("dev %s not offline\n", rdev->os.name);
		goto error_raid_not_offline;
	}

	dev = td_device_get(dev_name);
	if (dev == NULL) {
		rc = -ENOENT;
		pr_err("dev %s not found\n", dev_name);
		goto error_get_dev;
	}

	/* Lock the device, so we can control it's state */
	td_device_lock(dev);

	pr_err("%s:%d: rdev %p in state %d, dev %p\n", __func__, __LINE__, rdev, rdev->tr_state, dev);

	if (! td_eng_conf_hw_var_get(td_device_engine(dev), RAID_PAGE) ) {
		rc = -EMEDIUMTYPE;
		pr_err("dev %s not mcs-raid capable\n", dev_name);
		goto error_not_capable;
	}

	if (!(td_device_check_state(dev, OFFLINE) || td_device_check_state(dev, CREATED))) {
		rc = -EBUSY;
		pr_err("dev %s not offline: %d\n", dev_name,
				dev->td_state);
		goto error_dev_not_offline;
	}

	rc = td_raid_add_member(rdev, dev);
	if (rc)
		goto error_adding_member;


	td_raid_enter_state(rdev, OFFLINE);

	// FIXME: member state, whole raid state?

error_adding_member:
error_dev_not_offline:
error_not_capable:
	td_device_unlock(dev);
	td_device_put(dev);
error_get_dev:
error_raid_not_offline:

	if (rc)
		pr_err("Failed to attach device '%s' to raid '%s', error=%d.\n",
				dev_name, rdev->os.name, rc);
	return rc;
}

int td_raid_del_member(struct td_raid *rdev, struct td_device *dev)
{
	int i;

	WARN_TD_DEVICE_UNLOCKED(rdev);

	for (i = 0; i < tr_conf_var_get(rdev, MEMBERS); i++) {
		struct tr_member *trm = rdev->tr_members + i;

		if (trm->trm_device != dev)
			continue;

		/* This should be marked as present */
		WARN_ON(! (rdev->tr_member_mask & 1<<i));

		rdev->tr_member_mask ^= (1<<i);
		/* Fond the member */
		trm->trm_device = NULL;
		trm->trm_state = TR_MEMBER_EMPTY;

		/* this puts the extra reference we fetched */
		td_ucmd_put(trm->ucmd);
		mb();

		/**! we need to release the device reference */
		td_device_put(dev);

		return 0;
	}

	return -EINVAL;
}

int td_raid_detach_device(struct td_raid *rdev, const char *dev_name)
{
	int rc;
	struct td_device *dev;

	WARN_TD_DEVICE_UNLOCKED(rdev);

	/* Check things that can fail */
	if (!td_raid_check_state(rdev, CREATED) &&
	    !td_raid_check_state(rdev, OFFLINE)) {
		rc = -EBUSY;
		pr_err("dev %s not offline\n", rdev->os.name);
		goto error_raid_not_offline;
	}

	dev = td_device_get(dev_name);
	if (dev == NULL) {
		rc = -ENOENT;
		pr_err("dev %s not found\n", dev_name);
		goto error_get_dev;
	}

	if (!td_device_check_state(dev, RAID_MEMBER)) {
		pr_warning("dev %s not online raid member\n", dev_name);
	}

	/* Lock the device for consistency */
	td_device_lock(dev);

	// FIXME: figure out offline
	// td_device_go_offline(dev);

	rc = td_raid_del_member(rdev, dev);
	if (rc)
		goto error_removing_member;

	td_device_enter_state(dev, OFFLINE);

error_removing_member:
	td_device_unlock(dev);
	td_device_put(dev);
error_get_dev:
error_raid_not_offline:

	if (rc)
		pr_err("Failed to remove device '%s' from raid '%s', error=%d.\n",
				dev_name, rdev->os.name, rc);
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
	state->state = rdev->tr_state;

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


static int __td_raid_ioctl(struct td_raid* rdev, unsigned int cmd, unsigned long raw_arg)
{
	int rc;
	union {
		struct td_ioctl_device_list             	member_list;
		struct td_ioctl_raid_info                       raid_info;
		struct td_ioctl_raid_state                      raid_state;
		struct td_ioctl_conf                            conf;
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
		
	case TD_IOCTL_RAID_GET_INFO:
		copy_out_size = sizeof(k_arg->raid_info);
		break;
		
	case TD_IOCTL_RAID_GET_STATE:
		big_size = TD_IOCTL_RAID_STATE_SIZE(tr_conf_var_get(rdev, MEMBERS));
		copy_out_size = big_size;
		break;

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

	case TD_IOCTL_RAID_GET_INFO:
		rc = td_raid_get_info(rdev, &k_arg->raid_info);
		break;

	case TD_IOCTL_RAID_GET_STATE:
		rc = td_raid_get_state(rdev, &k_arg->raid_state);
		break;

	default:
		rc = -ENOIOCTLCMD;
		break;
	}

	td_raid_unlock(rdev);

	if (rc)
		goto bail_ioctl;

	/* copy data back */

	if (copy_out_size) {
		rc = -EFAULT;
		if (copy_to_user(u_arg, k_arg, copy_out_size)) {
			pr_err("RAID ioctl failed to copy out %u bytes.",
					copy_out_size);
			goto bail_ioctl;
		}
	}

	rc = 0;

bail_ioctl:
	if (__big_arg)
		kfree(__big_arg);
	return rc;
}

static void __td_raid_destroy (struct td_raid *rdev)
{
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
