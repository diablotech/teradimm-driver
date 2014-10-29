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

#ifndef _TD_DEVICE_H_
#define _TD_DEVICE_H_

#include "td_kdefn.h"

#include "td_defs.h"
#include "td_osdev.h"
#include "td_device.h"
#include "td_mapper.h"
#include "td_worker.h"

/* We need the struct td_engine, without all it's API yet */
#include "td_engine_def.h"

#include "td_raid.h"

#ifdef CONFIG_TERADIMM_FORCE_SSD_HACK
extern int force_ssd;
#endif


struct td_devgroup;
struct td_engine;

struct td_device {
	struct td_osdev os;
	char td_slot[TD_DEVICE_SLOT_NAME_MAX];
	

	enum td_device_state_type td_state; /**< current device state */
	struct completion   td_state_change_completion; /**< used to notify state changes */


	struct td_devgroup  *td_devgroup;    /**< part of this device group */
	struct list_head    td_devgroup_link; /**< link in the groups list of devices */

	struct td_work_item *td_work_item;   /**< info for td_worker */

	struct td_engine    td_engine;      /**< token tracking implementation */

	struct td_mapper    td_mapper;      /**< TeraDIMM memory mapper */

	int                 td_irq;
	uint16_t            td_memspeed;
	uint16_t            td_cpu_socket;
	
};


#define td_dev_emerg(dev,fmt,...)    td_os_emerg(&(dev)->os, fmt, ##__VA_ARGS__)
#define td_dev_alert(dev,fmt,...)    td_os_alert(&(dev)->os, fmt, ##__VA_ARGS__)
#define td_dev_crit(dev,fmt,...)     td_os_crit(&(dev)->os, fmt, ##__VA_ARGS__)
#define td_dev_err(dev,fmt,...)      td_os_err(&(dev)->os, fmt, ##__VA_ARGS__)
#define td_dev_warning(dev,fmt,...)  td_os_warning(&(dev)->os, fmt, ##__VA_ARGS__)
#define td_dev_notice(dev,fmt,...)   td_os_notice(&(dev)->os, fmt, ##__VA_ARGS__)
#define td_dev_info(dev,fmt,...)     td_os_info(&(dev)->os, fmt, ##__VA_ARGS__)
#define td_dev_debug(dev,fmt,...)    td_os_debug(&(dev)->os, fmt, ##__VA_ARGS__)

#define td_dev_warn td_dev_warning
#define td_dev_dbg td_dev_debug


static inline struct td_device *td_device_from_os(struct td_osdev *odev)
{
	BUG_ON(odev->type != TD_OSDEV_DEVICE);
	return container_of( odev, struct td_device, os);
}

static inline const char* td_device_name (struct td_device *dev)
{
	return dev->os.name;
}

extern int __init td_device_init(void);
extern void td_device_exit(void);

static inline void td_device_hold(struct td_device* dev)
{
	td_osdev_get(&dev->os);
}

extern void td_device_set_uuid(struct td_device *dev, uint8_t *spd);

extern int td_device_list_iter(int (*action)(struct td_device *dev, void *data), void *data);


extern int td_device_dump_names(char *buf, size_t len, uint32_t *count);
extern int td_device_create(const char *name, const char *slot_name,
		uint64_t phys_mem_base, uint64_t phys_mem_size,
		uint32_t irq_num, uint16_t memspeed, uint16_t cpu_socket);
extern int td_device_delete(const char *name);

extern int td_device_shutdown(struct td_device *dev);
extern int td_device_train(struct td_device *dev);

/* access to objects */
extern struct td_device *td_device_find ( int (*is_it)(struct td_device*, void* data),
		void* data);

extern struct td_device *td_device_get(const char *name);
void td_device_put(struct td_device *);

/* make sure cpu can access device memory */

extern int td_device_start_cpu_sensitive_user_access(struct td_device *dev);
extern void td_device_end_cpu_sensitive_user_access(struct td_device *dev);

/* device control */

extern int td_device_attach(struct td_device *dev, const char *group);
extern int td_device_detach(struct td_device *dev);
extern int td_device_reset(struct td_device *dev);

/* device doing work */

extern int td_device_do_work(struct td_device *dev);

/* device configuration */

extern int td_device_set_conf(struct td_device *dev, enum td_device_conf_type conf,
		uint32_t type, uint64_t val);
extern int td_device_get_conf(struct td_device *dev, enum td_device_conf_type conf,
		uint32_t type, uint64_t *val);

/* state control */

/** notification from engine that a state change occured */
extern void td_device_run_state_change(struct td_device *dev,
		enum td_engine_run_state prev_state,
		enum td_engine_run_state run_state);

/** set the state of the device
 * @sa enum td_device_state_type */
#define td_device_enter_state(dev,new_state) ({ \
		__td_device_enter_state(dev, TD_DEVICE_STATE_##new_state); \
		})

static inline void __td_device_enter_state(struct td_device *dev,
		enum td_device_state_type new_state ) {

	(dev)->td_state = new_state;
	switch (new_state) {
	case TD_DEVICE_STATE_ONLINE:
	case TD_DEVICE_STATE_OFFLINE:
		complete_all(&(dev)->td_state_change_completion);
	default:
		break;
	}
}
/** check the state of the device
 * @sa enum td_device_state_type */
#define td_device_check_state(dev,check_state) ( \
		(dev)->td_state == TD_DEVICE_STATE_##check_state \
		)

extern int td_device_go_online(struct td_device *dev);
extern int td_device_go_offline(struct td_device *dev);

/* macros */

static inline struct td_engine *td_device_engine(struct td_device *dev)
{
	return &dev->td_engine;
}

static inline struct td_device *td_engine_device(struct td_engine *eng)
{
	return container_of(eng, struct td_device, td_engine);
}

static inline struct td_devgroup *td_device_group(struct td_device *dev)
{
	return dev->td_devgroup;
}

static inline struct td_devgroup *td_engine_devgroup (struct td_engine *eng)
{
	return td_device_group(td_engine_device(eng));
}

#define td_device_poke(dev) td_devgroup_poke(td_device_group(dev))

/* state transitions */
extern int td_dev_state_wait(struct td_device *dev,
		enum td_device_state_type wanted_state,
		int timeout);
static inline void td_device_lock(struct td_device *dev)
{
	td_osdev_lock(&dev->os);
}

static inline void td_device_unlock(struct td_device *dev)
{
	td_osdev_unlock(&dev->os);
}

#ifdef CONFIG_TERADIMM_STATIC_NODEMAP
extern uint td_socketmap[MAX_NUMNODES];
extern int td_socketmap_size;
#endif

#endif

#ifdef CONFIG_TERADIMM_DEBUG_DEVICE_LOCK
#define WARN_TD_DEVICE_UNLOCKED(dev) ({ \
               WARN_ON( dev->os.mutex_holder != current ); })
#define WARN_TD_DEVICE_LOCKED(dev) ({ \
               WARN_ON( dev->os.mutex_holder == current ); })
#else
#define WARN_TD_DEVICE_UNLOCKED(dev) ({ })
#define WARN_TD_DEVICE_LOCKED(dev) ({ })
#endif

