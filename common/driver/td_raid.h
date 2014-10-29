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

#ifndef _TD_RAID_H_
#define _TD_RAID_H_

#include "td_kdefn.h"

#include "td_defs.h"
#include "td_engine_def.h"
#include "td_params.h"

struct tr_member {
	struct td_device *trm_device;
	struct td_ucmd *ucmd;
	uint8_t trm_uuid[TD_UUID_LENGTH];
	enum td_raid_member_state trm_state;
};

struct td_raid;

struct td_raid_ops {
	/* prepare raid according to this type */
	int (*_init)(struct td_raid *);
	/* And clean up */
	int (*_destroy)(struct td_raid *);
	

	int (*_get_conf)(struct td_raid *, uint32_t conf, uint64_t *val);
	int (*_set_conf)(struct td_raid *, uint32_t conf, uint64_t val);

	/* Check a member being added */
	int (*_check_member)(struct td_raid *, struct td_device *dev, int first);

	/* Prepare for online */
	int (*_online)(struct td_raid *);
	
	/* Handle a BIO request */
	int (*_request) (struct td_raid *rdev, td_bio_ref bio);
	int (*_degraded_request) (struct td_raid *rdev, td_bio_ref bio);
};

struct td_raid {
	struct td_osdev os;

	/**
	 * Basic raid config, what level, how many, whos active, etc
	 */
	uint64_t           tr_member_mask;  /**< Mask of active members present */

	struct {
		uint64_t general[TR_CONF_GENERAL_MAX];
	} conf;

	struct tr_member   *tr_members;

	enum td_raid_state_type tr_state; /**< current device state */
	struct completion  tr_state_change_completion; /**< used to notify state changes */

	
	struct td_raid_ops	*ops;
	void*                   ops_priv;

	/* Used for params */
	struct page             *tr_meta_page;
};

#define td_raid_emerg(dev,fmt,...)    td_os_emerg(&(dev)->os, fmt, ##__VA_ARGS__)
#define td_raid_alert(dev,fmt,...)    td_os_alert(&(dev)->os, fmt, ##__VA_ARGS__)
#define td_raid_crit(dev,fmt,...)     td_os_crit(&(dev)->os, fmt, ##__VA_ARGS__)
#define td_raid_err(dev,fmt,...)      td_os_err(&(dev)->os, fmt, ##__VA_ARGS__)
#define td_raid_warning(dev,fmt,...)  td_os_warning(&(dev)->os, fmt, ##__VA_ARGS__)
#define td_raid_notice(dev,fmt,...)   td_os_notice(&(dev)->os, fmt, ##__VA_ARGS__)
#define td_raid_info(dev,fmt,...)     td_os_info(&(dev)->os, fmt, ##__VA_ARGS__)
#define td_raid_debug(dev,fmt,...)    td_os_debug(&(dev)->os, fmt, ##__VA_ARGS__)

#define td_raid_warn td_raid_warning
#define td_raid_dbg td_raid_debug

static inline struct td_raid *td_raid_from_os(struct td_osdev *odev)
{
	return container_of( odev, struct td_raid, os);
}

static inline const char* td_raid_name (struct td_raid *dev)
{
	return dev->os.name;
}

#define td_raid_check_state(rdev, check_state) \
	((rdev)->tr_state == TD_RAID_STATE_##check_state)

#define td_raid_enter_state(rdev, new_state) ({ \
	(rdev)->tr_state = TD_RAID_STATE_ ## new_state; \
	})

extern int __init td_raid_init(void);
extern void td_raid_exit(void);

extern int td_raid_list_iter(int (*action)(struct td_raid *dev, void *data), void *data);

extern int td_raid_dump_names(char *buf, size_t len, uint32_t *count);

extern struct td_raid *td_raid_get(const char *name);
extern struct td_raid *td_raid_get_uuid(const char *uuid);
void td_raid_put(struct td_raid *);


extern int td_raid_create (const char *name, const uint8_t uuid[TR_UUID_LENGTH],
		int conf_count, struct td_ioctl_conf_entry* conf);

#ifdef CONFIG_TERADIMM_DEPREICATED_RAID_CREATE_V0
extern int td_raid_create_v0 (const char *name, const uint8_t uuid[TR_UUID_LENGTH],
		enum td_raid_level level, int members_count);
#endif

extern int td_raid_delete(const char *name);

extern int td_raid_discover_device(struct td_device*, void *meta_data);

/* device configuration */
#if 0
/* No set_conf supported/needed yet  */
extern int td_raid_set_conf(struct td_raid *dev, enum td_raid_conf_type conf,
		uint32_t type, uint64_t val);
#endif
extern int td_raid_get_conf(struct td_raid *dev, enum td_raid_conf_type conf,
		uint32_t type, uint64_t *val);

/* state control */

extern int td_raid_go_online(struct td_raid *dev);
extern int td_raid_go_offline(struct td_raid *dev);


#define td_raid_state(_r) (_r->tr_state)

/* macros */
static inline void td_raid_lock(struct td_raid *rdev)
{
	td_osdev_lock(&rdev->os);
}

static inline void td_raid_unlock(struct td_raid *rdev)
{
	td_osdev_unlock(&rdev->os);
}

#define tr_conf_var_get(rdev, which)                             \
	((rdev)->conf.general[TR_CONF_GENERAL_##which])
#define tr_conf_var_set(rdev, which, val)                            \
	do { (rdev)->conf.general[TR_CONF_GENERAL_##which] = val;                   \
	td_raid_debug(rdev, "CONF %s set to %llu\n", __stringify(which), (rdev)->conf.general[TR_CONF_GENERAL_##which]); \
	} while (0)


#endif

