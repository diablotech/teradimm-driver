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

#ifndef _TD_IOCTL_H_
#define _TD_IOCTL_H_

#include <asm/ioctl.h>

#include "td_defs.h"
#include "td_limits.h"
#include "td_compat.h"

#define TERADIMM_IOC (0xD1^0xAB^0x10)

struct td_device;
struct td_ucmd;
struct td_raid;

#define USER_PTR(_name)                                 \
	union {                                         \
		void*           _name;                  \
		uint64_t        _name ## _padding;      \
	}

#define SET_USER_PTR(_name, _val)                       \
	do {                                            \
		if (sizeof(void*) !=  sizeof(uint64_t)) \
			_name ## _padding = 0;          \
		_name = (_val);                         \
	} while (0);

/* This is the conf entry we use for ALL conf related ioctls */
struct __packed td_ioctl_conf_entry {
	uint32_t type;          /* !< use enum td_*_conf_type           */
	uint32_t var;           /* !< use enum variables from td_defs.h */
	uint64_t val;           /* !< The value of the variable         */
};
#define TD_CONF_ENTRIES_SIZE(_n) (_n * sizeof(struct td_ioctl_conf_entry))

struct __packed td_ioctl_conf {
	uint32_t  count;
	struct td_ioctl_conf_entry entries[0];
};

/** return size of buffer needed to allocate enough space for some number of entries */
#define TD_IOCTL_CONF_SIZE(entries) (sizeof(struct td_ioctl_conf) \
		+ TD_CONF_ENTRIES_SIZE(entries) )


/* ioctls for /dev/td-control character device */

struct __packed td_ioctl_devgroup_list {
	uint32_t  buf_size;     /* !< size of buffer */
	uint32_t  group_count;  /* !< number of entries */
	char      buffer[0];    /* !< buffer filling the rest of the page */
};
/** return size of buffer needed to allocate enough space for some number of entries */
#define TD_IOCTL_DEVGROUP_LIST_SIZE(entries) (sizeof(struct td_ioctl_devgroup_list) \
		+ (entries) * (TD_DEVGROUP_NAME_MAX+1))

struct __packed td_ioctl_devgroup_create {
	char     group_name[TD_DEVGROUP_NAME_MAX];
	int32_t  thread_socket;
	int32_t  thread_nice;
};

struct __packed td_ioctl_devgroup_name {
	char     group_name[TD_DEVGROUP_NAME_MAX];
};

struct __packed td_ioctl_devgroup_status {
	char     group_name[TD_DEVGROUP_NAME_MAX];
	int32_t  thread_socket;
	int32_t  thread_nice;
	uint32_t thread_running:1;
};

struct __packed td_ioctl_devgroup_conf {
	char     group_name[TD_DEVGROUP_NAME_MAX];
	uint32_t  count;
	struct td_ioctl_conf_entry entries[0];
};
/** return size of buffer needed to allocate enough space for some number of entries */
#define TD_IOCTL_DEVGROUP_CONF_SIZE(entries) (sizeof(struct td_ioctl_devgroup_conf) \
		+ TD_CONF_ENTRIES_SIZE(entries) )

struct td_ioctl_devgroup_counter_entry {
	uint32_t type; /* !< use enum td_devgroup_counter_type */
	uint32_t var;  /* !< use enum td_devgroup_(...)_counter
			* from td_defs.h
			*/
	uint32_t worker;
	uint64_t val;
};

struct __packed td_ioctl_devgroup_counters {
	char     group_name[TD_DEVGROUP_NAME_MAX];
	uint32_t count;
	struct td_ioctl_devgroup_counter_entry entries[0];
};
/** return size of buffer needed to allocate enough space for some number of entries */
#define TD_IOCTL_DEVGROUP_COUNTER_SIZE(entries) (sizeof(struct td_ioctl_devgroup_counters) \
		+ (entries) * sizeof(struct td_ioctl_devgroup_counter_entry))

/* ioctls for handling device creation and management, target /dev/td-control */

struct __packed td_ioctl_device_list {
	uint32_t  buf_size;     /* !< size of buffer */
	uint32_t  dev_count;    /* !< number of entries */
	char      buffer[0];    /* !< buffer filling the rest of the page */
};
/** return size of buffer needed to allocate enough space for some number of entries */
#define TD_IOCTL_DEVICE_LIST_SIZE(entries) (sizeof(struct td_ioctl_device_list) \
		+ (entries) * (TD_DEVICE_NAME_MAX+1))


struct __packed td_ioctl_device_create {
	char       dev_name[TD_DEVICE_NAME_MAX];
	char       phys_slot_name[TD_DEVICE_SLOT_NAME_MAX];
	uint64_t   phys_mem_base;
	uint64_t   phys_mem_size;
	uint32_t   irq_num;
	uint16_t   memspeed;
	uint16_t   cpu_socket;
};

struct __packed td_ioctl_device_name {
	char       dev_name[TD_DEVICE_NAME_MAX];
};


/* ioctls targetting an individual device, ie /dev/tdX-ctl */

/* ioctls targetting an individual device, ie /dev/tdX-ctl */
struct td_ioctl_device_counter_entry {
	uint32_t type; /* !< use enum td_device_counter_type from td_defs.h */
	uint32_t var;  /* !< use enum td_dev_(gen, token, misc)_counter
			* from td_defs.h
			*/
	uint64_t val;
};

struct __packed td_ioctl_device_counters {
	uint32_t  count;
	struct td_ioctl_device_counter_entry entries[0];
};
/** return size of buffer needed to allocate enough space for some number of entries */
#define TD_IOCTL_DEVICE_COUNTER_SIZE(entries) \
		( sizeof(struct td_ioctl_device_counters) \
		+ (entries) * sizeof(struct td_ioctl_device_counter_entry) )

struct __packed td_ioctl_device_stats {
	struct {
		uint64_t  req_queued_cnt;          /* !< number of items in the request queue */
		uint64_t  req_active_cnt;          /* !< number of active requests */
		uint64_t  req_completed_cnt;       /* !< number of requests successfully completed */
		uint64_t  bytes_transfered;        /* !< number of bytes transfered */
		uint64_t  req_timedout_cnt;        /* !< number of timedout requests */
		uint64_t  req_failed_cnt;          /* !< number of failed requests */
		uint64_t  iops;                    /* !< average IOPS in sample last period */
		uint64_t  bandwidth;               /* !< average BW in sample last period */
		uint64_t  max_concurrent_completed;/* !< maximum concurrent requests */
		uint64_t  split_req_cnt;           /* !< number of requests split into muliple requests */
	} read, write, control;
};


struct __packed td_ioctl_device_counters_internal {
	union {
		uint64_t data[ 3 * TD_DEV_GEN_COUNT_MAX + /* 3 is for read, write, control */
				   TD_DEV_TOKEN_COUNT_MAX +
				   TD_DEV_MISC_COUNT_MAX    ];
		struct {
			struct {
				struct td_io_latency_counters {
					uint64_t latency;          /* !< latest random latency (nsec) */
					uint64_t lat_acumu;        /* !< accumulated latencies */
					uint64_t lat_cnt;          /* !< number accumulated latencies */
				} hw, req;
			} read, write, control;

			struct {
				uint64_t  timedout_cnt;            /* !< number of tokens timed out */
				uint64_t  failed_cnt;              /* !< number of failed tokens */
				uint64_t  resets_cnt;              /* !< number of resets issued (after timeout) */
				uint64_t  retries_cnt;             /* !< number of retries issued (after reset) */
				uint64_t  recovered_cnt;           /* !< number of recovered errors (through retries) */
				uint64_t  free_list_cnt;           /* !< number of tokens on free list */
				uint64_t  active_list_cnt;         /* !< number of tokens on active list */
				uint64_t  timedout_list_cnt;       /* !< number of tokens on timedout list */
				uint64_t  resumable_list_cnt;      /* !< number of tokens on resumable list */
				uint64_t  partial_read_list_cnt;   /* !< number of tokens on partial read list */
				uint64_t  offset_error_cnt;        /* !< number of command with offset errors */
				uint64_t  data_ecc_error_cnt;      /* !< number of command ecc errors on write data  */
				uint64_t  cmd_ecc_error_cnt;       /* !< number of command ecc errors on cmd data  */
				uint64_t  weptimeout_cnt;          /* !< number of commands with hw errors */
				uint64_t  weptimeout_recovered_cnt;  /* !< number of commands with hw errors */
				uint64_t  badxsum_cnt;             /* !< number of commands with bad checksums */
				uint64_t  badxsum_recovered_cnt;     /* !< number of commands with bad checksums */
				uint64_t  cmd_error_cnt;           /* !< number of commands with hw errors */
				uint64_t  seq_dup_cnt;             /* !< number of commands with sequence ooo */
				uint64_t  seq_ooo_cnt;             /* !< number of commands with sequence dup */
				uint64_t  seq_replay_cnt;          /* !< number of commands replayed for missing sequence */
				uint64_t  forced_seq_advance;      /* !< number of times sequence had to be advanced */
				uint64_t  sec_ignore_retry;        /* !< number of tiems seq made us ignore failures */
				uint64_t  double_sec_replay;       /* !< number of times a SEC was replayed on both tokens */
				uint64_t  lost_refresh_cnt;        /* !< number of times a lost token had to be refreshed */
				uint64_t  noupdate_cnt;            /* !< number of inflight command */
				uint64_t  noupdate_limit_reached;   /* !< number of times the number of inflight/not-updated tokens exceeded limit */
			} token;

			struct {
				uint64_t  read_modify_write_cnt;   /* !< number of read-modify-write operations */
				uint64_t  deallocation_error_cnt;  /* !< number of failed deallocations */
				uint64_t  deallocation_timeout_cnt;/* !< number of timedout deallocations */
				uint64_t  rdbuf_progress_timeout_cnt; /* !< number of RDBUF progress timeouts */
				uint64_t  rdbuf_matching_timeout_cnt; /* !< number of RDBUF matching timeouts */
				uint64_t  orphan_read_buffer_cnt;  /* !< number of recovered lost read buffers */
				uint64_t  fwstatus_sema_success_cnt;  /* !< number of flips of the fw status semaphore */
				uint64_t  fwstatus_sema_misses_cnt;  /* !< number of misses on fw status semaphore */
				uint64_t  fwstatus_sema_timeout_cnt;  /* !< number of timeouts on fw status semaphore */
				uint64_t  fwstatus_sema_timeout_max_cnt;  /* !< number of times the timeout max was reached */
				uint64_t  rdbuf_marker_error_cnt; /* !< number of misses on readbuf metadata */
			} misc;
		};
	};
};

/* HACK: remove later */
struct __packed td_ioctl_device_rdbufs {
	struct __td_rd_buf {
		struct list_head  drb_lru_link;         /**< position in LRU list */
		uint16_t          drb_rd_bufid;         /**< ID of this read buffer */
		uint16_t          drb_last_failed:1;    /**< flag indicating last deallocation failed */
		uint32_t          drb_was_orphaned;     /**< number of times it was orphaned */
		uint64_t          drb_last_used;        /**< jiffies when last used this entry */
	} _td_rd_bufs[TD_HOST_RD_BUFS_PER_DEV];
	struct list_head        _td_rd_buf_lru;
	uint32_t _td_rd_buf_lru_seen_other_reads; /**< count number of reads seen while this entry was LRU */
	uint32_t _td_rd_buf_lru_seen_read_timeouts; /**< count number of reads that timed out while this entry was LRU */
};


/* trace support */

struct __packed td_ioctl_device_trace_config {
	uint64_t trace_mask;
	uint64_t trace_wrap:1;                          /**< wrap enable/disable */
	uint64_t trace_reset:1;                         /**< set only; resets trace */
};

struct __packed td_trace_iterator {
	uint32_t first_id;
	uint32_t index;
	uint64_t start_cycles;
	uint64_t last_cycles;
	uint64_t offset;
};

struct __packed td_ioctl_device_trace_read {
	struct td_trace_iterator iterator;
	uint32_t buf_size;
	uint32_t buf_used;
	USER_PTR(buffer);
};

static inline void td_ioctl_device_trace_read_init(
		struct td_ioctl_device_trace_read *rd,
		char *buffer, unsigned buf_size)
{
	memset(&rd->iterator, 0, sizeof(rd->iterator));
	rd->buf_size = buf_size;
	rd->buf_used = 0;
	rd->buffer = buffer;
}

/*
 * These 2 interfaces split up the device info (static) and state (dynamic).
 * The info shouldn't change unless by admin commands.
 * The state is dynamic, and reflects the current health/operation
 * of the device
 */
struct __packed td_ioctl_device_info {
	char      phys_slot_name[TD_DEVICE_SLOT_NAME_MAX];
	uint64_t  phys_mem_base;    /* !< where it's located in physical memory */
	uint64_t  phys_mem_size;    /* !< physical memory size */
	uint32_t  phys_mem_speed;   /* !< DDR memory speed */
	uint32_t  irq_num;          /* !< irq assigned to device */
	uint8_t   uuid[16];         /* !< UUID of device */
	uint16_t  memspeed;         /* !< memory speed */
	uint16_t  cpu_socket;       /* !< cpu socket */
};

struct __packed td_ioctl_device_state {
	char      group_name[TD_DEVGROUP_NAME_MAX];
	uint32_t  device_state;     /* !< use enum td_device_state_type */
	uint32_t  run_state;        /* !< use enum td_run_state_type */
	uint32_t  control_users;    /* !< number of users of the control interface */
	uint32_t  block_users;      /* !< number of users of the block interface */
	uint64_t  storage_capacity; /* !< number of bytes of storage */
};

/* the following can only happen when the device is online */

struct __packed td_ioctl_device_cmd_pt {
	uint64_t cmd[8];            /* raw diablo protocol command */
	uint64_t xstatus;           /* raw diablo protocol extended status */

	__s32 result;               /* result (negative means error) */

	uint16_t data_len_to_device;   /* data buffer contains data for device */
	uint16_t data_len_from_device; /* data buffer to be filled with device data */

	USER_PTR(data);

	struct {
		struct {
			__u64           start;
			__u64           end;
		} io, ioctl;
	} cycles;
};

/* the following can only happen when the device is online */


struct __packed td_ioctl_device_ssd_pt {
	uint8_t ata_cmd[16];
	uint8_t ata_response[14];

	uint16_t data_len_to_device;   /* data buffer contains data for device */
	uint16_t data_len_from_device; /* data buffer to be filled with device data */

	uint8_t ssd_id:2;

	USER_PTR(data);

	struct {
		struct {
			__u64           start;
			__u64           end;
		} io, ioctl;
	} cycles;
};

struct __packed td_ioctl_device_put_reg {
	uint32_t reg;
	uint32_t data;
	uint32_t mask;
};

struct __packed td_ioctl_device_get_reg {
	uint32_t reg;
	uint32_t value;

};

struct __packed td_ioctl_device_params {
	uint32_t page;
	uint32_t size;
	USER_PTR(buf);
};

struct __packed td_ioctl_device_lock {
	uint8_t core_start;
	uint8_t core_size;
};

/* protocol debug ioctls */

struct __packed td_ioctl_device_raw_buffer {
	uint16_t type;    /* use enum td_buf_type */
	uint16_t index;   /* which buffer index */
	uint32_t buf_len; /* size of buffer */
	USER_PTR(buf);
};

#define VERSION_MAX_STR_SIZE	64
struct __packed td_ioctl_device_ver {
	char version[VERSION_MAX_STR_SIZE]; /* Big enough for the next crazy numbering */
};

struct __packed td_ioctl_monitor_rate {
	int rate;
};

struct __packed td_ioctl_device_global_ext_status {
	union {
		uint8_t u8[8];
		uint64_t u64;
	};
};

struct __packed td_ioctl_device_error_injection {
	uint64_t errors_mask;
	uint64_t errors_count;
};

enum td_ioctl_device_ecc_counters_type {
	TD_IOCTL_DEV_ECC_CNT_DDR3_SECS      = 0,
	TD_IOCTL_DEV_ECC_CNT_DDR3_HOURS     = 1,
	TD_IOCTL_DEV_ECC_CNT_INTERNAL_SECS  = 2,
	TD_IOCTL_DEV_ECC_CNT_INTERNAL_HOURS = 3,

	TD_IOCTL_DEV_ECC_CNT_NUM_TYPES      = 4
};

struct __packed td_ioctl_device_ecc_counters {
	uint32_t type;
	uint32_t num_counters;
	uint64_t threshold;
	uint64_t status;
	uint64_t counters[0];
};

#define TD_IOCTL_DEVICE_ECC_COUNTERS_NUM_MAX 60

/** return size of buffer needed to allocate enough space for some number of entries */
#define TD_IOCTL_DEVICE_ECC_COUNTERS_SIZE (sizeof(struct td_ioctl_device_ecc_counters) \
		+ ((TD_IOCTL_DEVICE_ECC_COUNTERS_NUM_MAX) * sizeof(uint64_t)))


/* ioctls for managing TR devices */

#ifdef CONFIG_TERADIMM_DEPREICATED_RAID_CREATE_V0
/** used with TD_IOCTL_RAID_CREATE/TD_IOCTL_RAID_STAT */
struct __packed td_ioctl_raid_device_create_v0 {
	char        raid_name[TD_DEVICE_NAME_MAX];  /* !< name, like tra */
	uint8_t     raid_uuid[TR_UUID_LENGTH];                  /* !< UUID of device */
	uint32_t    raid_level;                     /* !< use enum td_raid_level */
	uint32_t    raid_members_count;             /* !< number of TD devices */
};
#endif

struct __packed td_ioctl_raid_create {
	char        raid_name[TD_DEVICE_NAME_MAX];  /* !< name, like tra */
	uint8_t     raid_uuid[TR_UUID_LENGTH];                  /* !< UUID of device */
	uint32_t    raid_level;                     /* !< use enum td_raid_level */
	uint32_t    raid_members_count;             /* !< number of TD devices */
	struct td_ioctl_conf raid_conf;
};

#define TD_IOCTL_RAID_CREATE_SIZE(entries) (sizeof(struct td_ioctl_raid_create) \
			+ TD_IOCTL_CONF_SIZE(entries) )


struct __packed td_ioctl_raid_info {
	uint8_t     raid_uuid[TR_UUID_LENGTH];                  /* !< UUID of device */
	uint32_t    raid_level;                     /* !< use enum td_raid_level */
};


struct __packed td_ioctl_raid_member_entry {
	uint8_t     uuid[TR_UUID_LENGTH];
	uint32_t    state;
	char        td_name[TD_DEVICE_NAME_MAX];
};

struct __packed td_ioctl_raid_state {
	enum td_raid_state_type    state;
	uint32_t  members_count;             /* !< number of TD devices */
	uint32_t  control_users;    /* !< number of users of the control interface */
	uint32_t  block_users;      /* !< number of users of the block interface */
	uint64_t  storage_capacity; /* !< number of bytes of storage */
	

	struct td_ioctl_raid_member_entry members[0];
};

#define TD_IOCTL_RAID_STATE_SIZE(entries) \
	(sizeof(struct td_ioctl_raid_state) \
	 + ((entries) * sizeof(struct td_ioctl_raid_member_entry) ) )

/* ------------------------------------------------------------------------ */


/* ioctls for /dev/td-control character device */

/** ioctl used to query names of device groups */
#define TD_IOCTL_DEVGROUP_LIST   _IOWR(TERADIMM_IOC, 0, struct td_ioctl_devgroup_list)

/** ioctl used to create a device group */
#define TD_IOCTL_DEVGROUP_CREATE   _IOW(TERADIMM_IOC, 1, struct td_ioctl_devgroup_create)

/** ioctl used to delete a device group */
#define TD_IOCTL_DEVGROUP_DELETE   _IOW(TERADIMM_IOC, 2, struct td_ioctl_devgroup_name)

/** ioctl used to start the thread of a device group */
#define TD_IOCTL_DEVGROUP_START    _IOW(TERADIMM_IOC, 3, struct td_ioctl_devgroup_name)

/** ioctl used to stop the thread of a device group */
#define TD_IOCTL_DEVGROUP_STOP     _IOW(TERADIMM_IOC, 4, struct td_ioctl_devgroup_name)

/** ioctl used to query the status of a device group */
#define TD_IOCTL_DEVGROUP_GET_STATUS  _IOWR(TERADIMM_IOC, 5, struct td_ioctl_devgroup_status)

/** ioctl used to query all debug counters of a device group */
#define TD_IOCTL_DEVGROUP_GET_ALL_COUNTERS  _IOWR(TERADIMM_IOC, 7, struct td_ioctl_devgroup_counters)

/** Driver Version */
#define TD_IOCTL_DEVICE_DRIVER_VER _IOR(TERADIMM_IOC, 6, struct td_ioctl_device_ver)

/** ioctl used to quere debug counters of a device group */
#define TD_IOCTL_DEVGROUP_GET_COUNTERS  _IOWR(TERADIMM_IOC, 7, struct td_ioctl_devgroup_counters)

#define TD_IOCTL_MONITOR_RATE     _IOW(TERADIMM_IOC, 8, struct td_ioctl_monitor_rate)

/* ioctls for handling device creation and management, target /dev/td-control */

/** ioctl used to query names of devices */
#define TD_IOCTL_DEVICE_LIST   _IOWR(TERADIMM_IOC, 10, struct td_ioctl_device_list)

/** ioctl used to create a new device */
#define TD_IOCTL_DEVICE_CREATE  _IOW(TERADIMM_IOC, 11, struct td_ioctl_device_create)

/** ioctl used to delete an existing device */
#define TD_IOCTL_DEVICE_DELETE  _IOW(TERADIMM_IOC, 12, struct td_ioctl_device_name)


/* ioctls targetting an individual device, ie /dev/tdX-ctl */
/** ioctl used to attach a device to a device group */
#define TD_IOCTL_DEVICE_ATTACH  _IOW(TERADIMM_IOC, 20, struct td_ioctl_devgroup_name)

/** ioctl used to detach a device from a device group */
#define TD_IOCTL_DEVICE_DETACH  _IO(TERADIMM_IOC, 21)

/** ioctl used to query the configuration of a device group */
#define TD_IOCTL_DEVICE_GET_ALL_CONF  _IOWR(TERADIMM_IOC, 22, struct td_ioctl_conf)

/** ioctl used to query the configuration of a device group */
#define TD_IOCTL_DEVICE_GET_CONF  _IOWR(TERADIMM_IOC, 23, struct td_ioctl_conf)

/** ioctl used to change the configuration of a device group */
#define TD_IOCTL_DEVICE_SET_CONF  _IOW(TERADIMM_IOC, 24, struct td_ioctl_conf)

/** ioctl used to enter on-line mode */
#define TD_IOCTL_DEVICE_GO_ONLINE _IO(TERADIMM_IOC, 26)

/** ioctl used to enter off-line mode */
#define TD_IOCTL_DEVICE_GO_OFFLINE _IO(TERADIMM_IOC, 27)

/** ioctl used to get device counters */
#define TD_IOCTL_DEVICE_GET_STATS       _IOR(TERADIMM_IOC, 28, struct td_ioctl_device_stats)

#define TD_IOCTL_DEVICE_GET_RDBUFS       _IOR(TERADIMM_IOC, 29, struct td_ioctl_device_counters)

#define TD_IOCTL_DEVICE_TRACE_GET_CONF _IOR(TERADIMM_IOC, 31, struct td_ioctl_device_trace_config)

#define TD_IOCTL_DEVICE_TRACE_SET_CONF _IOW(TERADIMM_IOC, 32, struct td_ioctl_device_trace_config)

#define TD_IOCTL_DEVICE_TRACE_READ _IOWR(TERADIMM_IOC, 33, struct td_ioctl_device_trace_read)

/** ioctl used to get device info/state */
#define TD_IOCTL_DEVICE_GET_INFO  _IOWR(TERADIMM_IOC, 34, struct td_ioctl_device_info)
#define TD_IOCTL_DEVICE_GET_STATE  _IOWR(TERADIMM_IOC, 35, struct td_ioctl_device_state)

/** ioctl used to reset a device */
#define TD_IOCTL_DEVICE_RESET  _IO(TERADIMM_IOC, 36)


/* ioctl used to run through the training sequence */
#define TD_IOCTL_DEVICE_TRAIN   _IO(TERADIMM_IOC, 41)


/** ioctl used to send a pass-through command to device */
#define TD_IOCTL_DEVICE_CMD_PT   _IOW(TERADIMM_IOC, 42, struct td_ioctl_device_cmd_pt)

/** ioctl used to send a pass-through command to device */
#define TD_IOCTL_DEVICE_SSD_PT   _IOWR(TERADIMM_IOC, 42, struct td_ioctl_device_ssd_pt)
/** set and get hardware registers. */
#define TD_IOCTL_DEVICE_GET_REG _IOR(TERADIMM_IOC, 43, struct td_ioctl_device_get_reg)
#define TD_IOCTL_DEVICE_PUT_REG _IOW(TERADIMM_IOC, 44, struct td_ioctl_device_put_reg)

/** set and get firmware parameters */
#define TD_IOCTL_DEVICE_GET_PARAMS _IOR(TERADIMM_IOC, 45, struct td_ioctl_device_params)
#define TD_IOCTL_DEVICE_SET_PARAMS _IOW(TERADIMM_IOC, 46, struct td_ioctl_device_params)

#define TD_IOCTL_DEVICE_LOCK   _IOW(TERADIMM_IOC, 47, struct td_ioctl_device_lock)
#define TD_IOCTL_DEVICE_UNLOCK _IO(TERADIMM_IOC, 48)
/* protocol debug ioctls */
#define TD_IOCTL_DEVICE_GET_RAW_BUFFER _IOR(TERADIMM_IOC, 50, struct td_ioctl_device_raw_buffer)
#define TD_IOCTL_DEVICE_SET_RAW_BUFFER _IOR(TERADIMM_IOC, 51, struct td_ioctl_device_raw_buffer)

#define TD_IOCTL_DEVICE_GLOBAL_EXT_STATUS _IOR(TERADIMM_IOC, 52, struct td_ioctl_device_global_ext_status)

#define TD_IOCTL_DEVICE_SET_ERRORS _IOW(TERADIMM_IOC, 53, struct td_ioctl_device_error_injection)
#define TD_IOCTL_DEVICE_GET_ERRORS _IOR(TERADIMM_IOC, 54, struct td_ioctl_device_error_injection)

/** ioctl used to query device ECC counts */
#define TD_IOCTL_DEVICE_GET_ECC_COUNTERS _IOWR(TERADIMM_IOC, 55, struct td_ioctl_device_ecc_counters)

/** ioctl used for online upgrade.   Stops/Starts BIOs*/
#define TD_IOCTL_DEVICE_STOP_BIO  _IO(TERADIMM_IOC, 56)
#define TD_IOCTL_DEVICE_START_BIO _IO(TERADIMM_IOC, 57)

#define TD_IOCTL_DEVICE_GET_COUNTERS       _IOR(TERADIMM_IOC, 58, struct td_ioctl_device_counters)
#define TD_IOCTL_DEVICE_GET_ALL_COUNTERS       _IOR(TERADIMM_IOC, 59, struct td_ioctl_device_counters)

/** ioctl used to query the configuration of a device group */
#define TD_IOCTL_DEVGROUP_GET_CONF  _IOWR(TERADIMM_IOC, 60, struct td_ioctl_conf)

/** ioctl used to query configuration of a device group */
#define TD_IOCTL_DEVGROUP_GET_ALL_CONF  _IOWR(TERADIMM_IOC, 61, struct td_ioctl_devgroup_conf)

/** ioctl used to change the configuration of a device group */
#define TD_IOCTL_DEVGROUP_SET_CONF  _IOW(TERADIMM_IOC, 62, struct td_ioctl_conf)

/** ioctls used with TR device management */

#ifdef CONFIG_TERADIMM_DEPREICATED_RAID_CREATE_V0
/* called on /dev/td-control to create a new unconfigured /dev/trX device */
#define TD_IOCTL_RAID_CREATE_V0 _IOWR(TERADIMM_IOC, 70, struct td_ioctl_raid_device_create_v0)
#endif

#define TD_IOCTL_RAID_CREATE _IOWR(TERADIMM_IOC, 70, struct td_ioctl_raid_create)

/* called on /dev/td-control to delete a /dev/trX device */
#define TD_IOCTL_RAID_DELETE _IOW(TERADIMM_IOC, 71, struct td_ioctl_device_name)

/* called on /dev/td-control to list all /dev/trX devices */
#define TD_IOCTL_RAID_LIST   _IOWR(TERADIMM_IOC, 72, struct td_ioctl_device_list)


/** called on /dev/trX, to add one or more /dev/td.
 * When called, the buffer contains NUL delimited strings which identify the
 * names of the devices to add. */
#define TD_IOCTL_RAID_ADD_MEMBER _IOW(TERADIMM_IOC, 73, struct td_ioctl_device_list)

/* called on /dev/trX, removing membmers */
#define TD_IOCTL_RAID_DEL_MEMBER _IOW(TERADIMM_IOC, 74, struct td_ioctl_device_list)

/* called on /dev/trX, returns member list
 * Driver populates the buffer provided with NUL separated entries.
 * When called with an undersized buf_size, -ETOOSMALL is returned and
 * buf_size set to the minimum required buffer size to return all entries. */
#define TD_IOCTL_RAID_GET_MEMBER_LIST _IOWR(TERADIMM_IOC, 75, struct td_ioctl_device_list)

/* called on /dev/trX, returns status, info, and counters */
#define TD_IOCTL_RAID_GET_INFO          _IOWR(TERADIMM_IOC, 76, struct td_ioctl_raid_info)
#define TD_IOCTL_RAID_GET_STATE         _IOWR(TERADIMM_IOC, 77, struct td_ioctl_raid_state)
#define TD_IOCTL_RAID_GET_COUNTERS      _IOWR(TERADIMM_IOC, 78, struct td_ioctl_device_counters)
#define TD_IOCTL_RAID_GET_ALL_COUNTERS  _IOWR(TERADIMM_IOC, 79, struct td_ioctl_device_counters)
#define TD_IOCTL_RAID_GET_CONF		_IOWR(TERADIMM_IOC, 80, struct td_ioctl_conf)
#define TD_IOCTL_RAID_GET_ALL_CONF	_IOWR(TERADIMM_IOC, 81, struct td_ioctl_conf)
#define TD_IOCTL_RAID_SET_CONF	 	_IOWR(TERADIMM_IOC, 82, struct td_ioctl_conf)

/* called on /dev/trX, starts resync process on DEGRADED device */
#define TD_IOCTL_RAID_RESYNC __future__


#ifdef __KERNEL__
int td_ioctl_device_get_conf(struct td_device *dev,
		struct td_ioctl_conf *conf, bool fill_mode);
int td_ioctl_device_set_conf(struct td_device *dev,
		struct td_ioctl_conf *conf);

int td_ioctl_device_get_info(struct td_device *dev,
		struct td_ioctl_device_info *info);
int td_ioctl_device_get_state(struct td_device *dev,
		struct td_ioctl_device_state *state);

int td_ioctl_device_cmd_pt(struct td_device *dev,
		struct td_ioctl_device_cmd_pt *dev_cmd,
		void *locker_context);

int td_ioctl_device_get_stats(struct td_device *dev,
		struct td_ioctl_device_stats *stats);
int td_ioctl_device_get_counters(struct td_device *dev,
		struct td_ioctl_device_counters *cntrs, bool fill_mode);

int td_ioctl_device_get_rdbufs(struct td_device *dev,
		struct td_ioctl_device_rdbufs *rb);
int td_ioctl_device_get_raw_buffer(struct td_device *dev,
		struct td_ioctl_device_raw_buffer *raw);
int td_ioctl_device_set_raw_buffer(struct td_device *dev,
		struct td_ioctl_device_raw_buffer *raw);

int td_ioctl_device_ata_pt (struct td_device *dev,
		struct td_ioctl_device_ssd_pt *ssd_cmd);

int td_ioctl_device_get_reg(struct td_device *dev,
		struct td_ioctl_device_get_reg *gr);
int td_ioctl_device_put_reg(struct td_device *dev,
		struct td_ioctl_device_put_reg *pr);

int td_ioctl_device_get_params(struct td_device *dev,
		struct td_ioctl_device_params *p);
int td_ioctl_device_set_params(struct td_device *dev,
		struct td_ioctl_device_params *p);

int td_ioctl_device_lock(struct td_device *dev,
		struct td_ioctl_device_lock *lock,
		void *locker_context);
int td_ioctl_device_unlock(struct td_device *dev,
		void *locker_context);
int td_device_is_locked(struct td_device *dev,
		void *locker_context);

int td_ioctl_device_go_online (struct td_device *dev);
int td_ioctl_device_go_offline (struct td_device *dev);

int td_ioctl_device_attach (struct td_device *dev,
		const char* devgroup);
int td_ioctl_device_detach (struct td_device *dev);
int td_ioctl_device_reset (struct td_device *dev);

int td_ioctl_control_driver_ver (struct td_ioctl_device_ver *ver);

int td_ioctl_device_global_ext_status (struct td_device *dev,
		struct td_ioctl_device_global_ext_status *gstatus);

int td_ioctl_device_set_errors (struct td_device *dev,
		struct td_ioctl_device_error_injection *conf);
int td_ioctl_device_get_errors (struct td_device *dev,
		struct td_ioctl_device_error_injection *conf);

int td_ioctl_device_get_ecc_counters(struct td_device *dev,
		struct td_ioctl_device_ecc_counters *cntrs);

int td_ioctl_device_flush(struct td_device *dev);
int td_ioctl_device_start_bio(struct td_device *dev);
int td_ioctl_device_stop_bio(struct td_device *dev, void *bio_context);

int td_ioctl_control_mon_rate( struct td_ioctl_monitor_rate *mr);

int td_ioctl_raid_get_conf(struct td_raid *dev,
                           struct td_ioctl_conf *conf, bool fill_mode);

#endif

#endif
