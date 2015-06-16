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

#ifndef _TD_RAID_H_
#define _TD_RAID_H_

#include "td_kdefn.h"

#include "td_defs.h"
#include "td_engine_def.h"
#include "td_params.h"

#define TD_RAID_THREAD_NAME_PREFIX      "mcs/"

struct tr_member {
	struct td_device *trm_device;
	struct td_ucmd *trm_ucmd;
	uint8_t trm_uuid[TD_UUID_LENGTH];
	enum td_raid_member_state trm_state;
	uint64_t trm_generation;
	uint64_t trm_counter[TR_MEMBER_COUNT_MAX];
};

struct td_raid;

struct td_raid_resync_context {
	/* Pointer to thread performing the resync */
	struct task_struct *resync_task;
	
	spinlock_t              trs_bio_lock;     /**< queue lock */
	int (*_trs_queue_bio) (struct td_raid *rdev, td_bio_ref bio);

	struct bio_list         trs_bios;         /**< requests from inbound layer */
	uint64_t                trs_bio_count;
};

struct td_raid_ops {
	/* prepare raid according to this type */
	int (*_init)(struct td_raid *);
	/* And clean up */
	int (*_destroy)(struct td_raid *);
	

	int (*_get_conf)(struct td_raid *, uint32_t conf, uint64_t *val);
	int (*_set_conf)(struct td_raid *, uint32_t conf, uint64_t val);
	
	int (*_get_counter)(struct td_raid *, uint32_t var, uint64_t *val);

	/* Check a member being added */
	int (*_check_member)(struct td_raid *, struct td_device *dev);
	int (*_handle_member)(struct td_raid *, int idx);
	int (*_fail_member)(struct td_raid *, int idx);

	/* Prepare for online */
	int (*_online)(struct td_raid *);
	
	/* Handle a BIO request */
	int (*_request) (struct td_raid *rdev, td_bio_ref bio);

	/* Resync volume */
	int (*_resync)(struct td_raid *);
};

struct td_raid {
	struct td_osdev os;

	/**
	 * Basic raid config, what level, how many, whos active, etc
	 */
	uint64_t           tr_member_set;  /**< Mask of members present */
	uint64_t           tr_active_set;  /**< Mask of active members present */

	struct {
		uint64_t general[TR_CONF_GENERAL_MAX];
	} conf;

	struct tr_member   *tr_members;

	enum td_raid_dev_state tr_dev_state; /**< current device state */
	enum td_raid_run_state tr_run_state; /** Raid state */

	uint64_t               tr_generation;

	uint64_t    counter[TR_GENERAL_COUNT_MAX];

	struct td_raid_ops	*ops;
	void*                   ops_priv;
	unsigned                ops_counter_max;

	struct td_raid_resync_context     resync_context;
};




/*
 * Some bit ops for our member mask 
 */
#define TR_BITSET_TEST(_bs, idx)   !(((_bs) & (1UL << idx))==0)

#define TR_BITSET_SET(_bs, idx)    do { (_bs) |= (1UL << idx);    } while (0)
#define TR_BITSET_FLIP(_bs, idx)   do { (_bs) ^= (1UL<< idx);     } while (0)
#define TR_BITSET_CLEAR(_bs, idx)  do { (_bs) &= ~(1UL << idx);   } while (0)

#define TR_BITSET_FULL(_bs, _count)((_bs) == (1UL << _count) - 1)
#define TR_BITSET_EMPTY(_bs)       ((_bs) == 0UL)



#define TR_MEMBERSET_TEST(_r, idx)    TR_BITSET_TEST(_r->tr_member_set, idx)
#define TR_MEMBERSET_SET(_r, idx)     TR_BITSET_SET(_r->tr_member_set, idx)
#define TR_MEMBERSET_FLIP(_r, idx)    TR_BITSET_FLIP(_r->tr_member_set, idx)
#define TR_MEMBERSET_CLEAR(_r, idx)   TR_BITSET_CLEAR(_r->tr_member_set, idx)
#define TR_MEMBERSET_FULL(_r)         TR_BITSET_FULL(_r->tr_member_set, tr_conf_var_get(_r, MEMBERS))
#define TR_MEMBERSET_EMPTY(_r)        TR_BITSET_EMPTY(_r->tr_member_set)

#define TR_ACTIVESET_TEST(_r, idx)    TR_BITSET_TEST(_r->tr_active_set, idx)
#define TR_ACTIVESET_SET(_r, idx)     TR_BITSET_SET(_r->tr_active_set, idx)
#define TR_ACTIVESET_FLIP(_r, idx)    TR_BITSET_FLIP(_r->tr_active_set, idx)
#define TR_ACTIVESET_CLEAR(_r, idx)   TR_BITSET_CLEAR(_r->tr_active_set, idx)
#define TR_ACTIVESET_FULL(_r)         TR_BITSET_FULL(_r->tr_active_set, tr_conf_var_get(_r, MEMBERS))
#define TR_ACTIVESET_EMPTY(_r)        TR_BITSET_EMPTY(_r->tr_active_set)



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

#define tr_check_dev_state(rdev, check_state) \
	((rdev)->tr_dev_state == TD_RAID_STATE_##check_state)

#define tr_enter_dev_state(rdev, new_state) ({ \
	(rdev)->tr_dev_state = TD_RAID_STATE_ ## new_state; \
	})

#define tr_check_run_state(rdev, check_state) \
	((rdev)->tr_run_state == TR_RUN_STATE_##check_state)

#define tr_enter_run_state(rdev, new_state) ({ \
	(rdev)->tr_run_state = TR_RUN_STATE_ ## new_state; \
	})


#define tr_raid_member_check_state(trm, check_state) \
	((trm)->trm_state == TR_MEMBER_##check_state)

#define tr_raid_member_enter_state(trm, new_state) ({ \
	(trm)->trm_state = TR_MEMBER_ ## new_state; \
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

/* Meta data */
void td_raid_save_meta (struct td_raid *rdev, int wait);


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


/*
 * Private API, mainly exposed for raid types to use
 */

int td_raid_change_member(struct td_raid *rdev, int idx, enum td_raid_member_state state);
int td_raid_fail_member(struct td_raid *rdev, int idx);

#endif

