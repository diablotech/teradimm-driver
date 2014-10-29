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

#ifndef _TD_DEFS_H_
#define _TD_DEFS_H_

#include "td_compat.h"
#include "td_limits.h"

/** max length (including zero-termination) of a device group name */
#define TD_DEVGROUP_NAME_MAX 16

#define TD_DEVGROUP_THREAD_NAME_PREFIX "td/"

#define TD_DEVICE_NAME_PREFIX "td"     /** devices are named /dev/td[a-z]+ */
#define TD_DEVICE_NAME_MAX 16          /** max length (including zero-termination) of a device name */

#define TR_DEVICE_NAME_PREFIX "tr"     /** raid devices are tdr[a-z] */
#define TR_UUID_LENGTH  16                /** raid uuid length. */

#define TD_DEVICE_SLOT_NAME_MAX 32

/** number of partitions supported per device */
#define TD_DEVICE_PARTITIONS_MAX 16

/* valid buffer encodings */

#define TD_INVALID_CORE_BUFID 0xFFFF
#define TD_IS_CORE_BUFID_VALID(n) ((unsigned)(n) < (unsigned)TD_CORE_BUFS_PER_DEV)
#if TD_INVALID_CORE_BUFID < TD_CORE_BUFS_PER_DEV
#error TD_INVALID_CORE_BUFID value is valid
#endif

#define TD_INVALID_WR_BUFID 0xFF
#define TD_IS_WR_BUFID_VALID(n)   ((unsigned)(n) < (unsigned)TD_HOST_WR_BUFS_PER_DEV)
#if TD_INVALID_WR_BUFID < TD_HOST_WR_BUFS_PER_DEV
#error TD_INVALID_WR_BUFID value is valid
#endif

#define TD_INVALID_RD_BUFID 0xFF
#define TD_IS_RD_BUFID_VALID(n)   ((unsigned)(n) < (unsigned)TD_HOST_RD_BUFS_PER_DEV)
#if TD_INVALID_RD_BUFID < TD_HOST_RD_BUFS_PER_DEV
#error TD_INVALID_RD_BUFID value is valid
#endif

#define TD_INVALID_TOKEN_ID 0xFFFF
#define TD_IS_TOKEN_ID_VALID(n)   ((unsigned)(n) < (unsigned)TD_TOKENS_PER_DEV)
#if TD_INVALID_TOKEN_ID < TD_TOKENS_PER_DEV
#error TD_INVALID_TOKEN_ID value is valid
#endif


enum td_eng_hw_reg {
	TD_CONF_HW_SSD_COUNT,		/**< The number of SSDs on this device. */
	TD_CONF_HW_SSD_STRIPE_LBAS,	/**< The stride size for splitting this device. */
	TD_CONF_HW_SSD_SECTOR_COUNT,       /**< How many sectors each SSD provides */
	TD_CONF_HW_HW_SECTOR_METADATA,     /**< what size the hardware operates in */
	TD_CONF_HW_HW_SECTOR_SIZE,         /**< what size the hardware operates in */
	TD_CONF_HW_HW_SECTOR_ALIGN,        /**< what sector alignment the hardware operates with (in sectors) */
	TD_CONF_HW_BIO_SECTOR_SIZE,        /**< what size we present to the block layer */
	TD_CONF_HW_E2E_MODE,               /**< E2E protection encoding mode */

	TD_CONF_HW_VER_MAJOR,              /**< FW Version info **/
	TD_CONF_HW_VER_MINOR,              /**< FW Version info **/
	TD_CONF_HW_VER_PATCH,              /**< FW Version info **/
	TD_CONF_HW_VER_BUILD,              /**< FW Version info **/


	TD_CONF_HW_SPD,                    /**< FW can give SPD data in params page7 */

	TD_CONF_HW_DISCARD,                /**< discard support enabled */

	TD_CONF_HW_MCEFREE_SUPPORT,        /**< indicates fw has support for MCEFREE */

#define TD_MCEFREE_FWSTATUS_DISABLED  0
#define TD_MCEFREE_FWSTATUS_WEP7      1
#define TD_MCEFREE_FWSTATUS_RDBUF0    2

	TD_CONF_HW_RAID_PAGE,              /**< FW can store RAID METADATA data in params page 13 */

	TD_CONF_HW_REGS_MAX,
};

enum td_eng_regs {
	TD_CONF_TOKENS,			/**< number of tokens per device */

	TD_CONF_HOST_READ_BUFS,		/**< number of host visible read buffers (read exit point) */
	TD_CONF_HOST_WRITE_BUFS,	/**< number of host visible write buffers (write entry point) */
	TD_CONF_CORE_BUFS,		/**< number of shared core buffers */
	TD_CONF_HW_CMDS,                /**< number of hardware commands that can be executed at once */

	TD_CONF_HOST_READ_ALIASES,	/**< number of read buffer aliases */
	TD_CONF_HOST_STATUS_ALIASES,	/**< number of status buffer aliases */

	TD_CONF_WRITE_WAIT_USEC,	/**< how long to wait for a write burst */

	TD_CONF_QUICK_WRBUF,            /**< release wrbuf immediately after sending command */
	TD_CONF_QUICK_TOK_REUSE,        /**< if enabled, the token free list is stack */

	TD_CONF_HALT_ON_TIMEOUT,        /**< if enabled, stop the device on first timeout */
	TD_CONF_VERBOSE_TOK_FAILURES,   /**< if enabled, dump info on failed tokens */
	TD_CONF_RETRY_TIMEDOUT_DEALLOCATES,/**< if enabled, timedout deallocates are retried */

	TD_CONF_ORPHAN_RDBUF_REQ_CNT,   /**< number of reads observed to qualify for orphan status */
	TD_CONF_ORPHAN_RDBUF_TIMEOUT_CNT,/**< number of reads timeouts to qualify for orphan status */
	TD_CONF_ORPHAN_RDBUF_REQ_MSEC,  /**< elapsed time required to qualify for orphan status */
	TD_CONF_ORPHAN_RDBUF_ORDER_CHECK,  /**< used on MegaDIMM to enforce low-rdbuf first order */

	TD_CONF_EARLY_COMMIT,           /**< release bio as soon as driver sees WAIT_SSD */
	TD_CONF_EARLY_DEALLOCATE,       /**< send deallocates from completion loop */
	TD_CONF_EARLY_START,            /**< send new commands from completion loop */

	TD_CONF_MAX_START_BURST,        /**< longest burst of commands allowed */

	TD_CONF_EARLY_SPLIT_REQ,        /**< if enabled, block layer is charged with splitting requests */
	TD_CONF_COLLISION_CHECK,        /**< hold off starting requests for active LBAs */

	TD_CONF_BIO_R_START_TIMEOUT_USEC,     /**< how long to wait for a token in start state */
	TD_CONF_BIO_R_OOO_TIMEOUT_USEC,    /**< how long to wait for a token in OOO state */
	TD_CONF_BIO_R_QUEUED_TIMEOUT_USEC,    /**< how long to wait for a token in queued state */
	TD_CONF_BIO_R_RECEIVED_TIMEOUT_USEC,  /**< how long to wait for a token in received state */
	TD_CONF_BIO_R_WAIT_SSD_TIMEOUT_USEC,  /**< how long to wait for a token in wait_ssd state */

	TD_CONF_BIO_W_START_TIMEOUT_USEC,     /**< how long to wait for a token in start state */
	TD_CONF_BIO_W_OOO_TIMEOUT_USEC,    /**< how long to wait for a token in OOO state */
	TD_CONF_BIO_W_QUEUED_TIMEOUT_USEC,    /**< how long to wait for a token in queued state */
	TD_CONF_BIO_W_RECEIVED_TIMEOUT_USEC,  /**< how long to wait for a token in received state */
	TD_CONF_BIO_W_WAIT_SSD_TIMEOUT_USEC,  /**< how long to wait for a token in wait_ssd state */

	TD_CONF_START_TIMEOUT_USEC,     /**< how long to wait for a token in start state */
	TD_CONF_OOO_TIMEOUT_USEC,    /**< how long to wait for a token in OOO state */
	TD_CONF_QUEUED_TIMEOUT_USEC,    /**< how long to wait for a token in queued state */
	TD_CONF_RECEIVED_TIMEOUT_USEC,  /**< how long to wait for a token in received state */
	TD_CONF_WAIT_SSD_TIMEOUT_USEC,  /**< how long to wait for a token in wait_ssd state */

	TD_CONF_STUCK_TIMEOUT_USEC,  /**< how long to wait for a token in wait_ssd state */

	TD_CONF_TIMEOUT_RETRY,		/**< non-zero enables timeout retries */

	TD_CONF_XSUM_ERR_RETRIES,	/**< how many times to retry a  xsum error retries */
	TD_CONF_WEP_TIMEOUT_RETRIES,	/**< how many times to retry a timed out WEP write */

	TD_CONF_RETRIES,		/**< how many times to retry failed commands */

	TD_CONF_RESETS,                 /**< number of resets to send to timed out commands */

	TD_CONF_CORE_ONLY,		/**< converts LBAs to -1 */
	TD_CONF_MAGIC_FLAGS,		/**< forces firmware to behave in magic ways */
	TD_CONF_INDEPENDENT_DEALLOCATE, /**< only sends deallocates in deallocate commands */

	TD_CONF_CLFLUSH,		/**< non-zero causes flushing cachelines */
	TD_CONF_WBINVD,		        /**< non-zero causes WBINVD to be used */

	TD_CONF_TARGET_IOPS,		/**< after reaching target IOPS, wait while polling */
	TD_CONF_IOPS_SAMPLE_MSEC,	/**< how often to update IOPS rate */

	TD_CONF_DELAY_POST_WRBUF_USEC,  /**< stall after writing data to the write buffer */
	TD_CONF_DELAY_POST_CMD_USEC,    /**< stall after writing command to the cmd buffer */
	TD_CONF_DELAY_WRITE_TO_READ_USEC, /**< on write-to-read transition, wait this long */

	TD_CONF_HOLD_TIMEDOUT_TOKENS,   /**< when enabled, timedout tokens are never reused */

	TD_CONF_TIMEDOUT_CNT_THRESHOLD, /**< How many timeouts before we start failing all IO */

	TD_CONF_SHUTDOWN,               /**< shutdown support enabled */

	/* for use with CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES only */
	TD_CONF_HW_TOKID_START,         /**< first token ID for HW commands */
	TD_CONF_RESERVED_TOKID_START,   /**< first token ID reserved for internal commands */

	TD_CONF_MCEFREE_FWSTATUS,       /**< indicates MCEFREE is enabled in driver */
	
	TD_CONF_SPY_UCMD,               /**< spy on ucmds as they go through the engine */

	TD_CONF_DOUBLE_WEP_WRITES,      /**< double WEP writes */

	TD_CONF_BIO_MAX_BYTES,          /**< how big do we tell the OS we can handle */

	TD_CONF_FW_SEQ_WINDOW,          /**< firmware sequence window */

	TD_CONF_OOO_REPLAY,             /**< should we replay OOO commands */

	TD_CONF_FW_CMD_WRITE_MODE,      /**< fw command write mode see td_conf_cmd_write_mode */
	TD_CONF_HW_CMD_WRITE_MODE,      /**< hw command write mode see td_conf_cmd_write_mode */

	TD_CONF_LOST_CMD_REFRESH_USEC,       /**< after this time a token that had no status updates is considered lost */
	TD_CONF_LOST_CMD_REFRESH_MAX,        /**< number of times to reissue the token if there are no updates */

	TD_CONF_NOUPDATE_CMD_LIMIT,     /**< number of commands that are allowed to be inflight w/o an update*/

	TD_CONF_INCOMING_SLEEP,         /**< incoming queue sleep threshold; no more are accepted */
	TD_CONF_INCOMING_WAKE,          /**< incoming queue wake-up threshold */

	/* END */
	TD_CONF_REGS_MAX
};

/* Start of Device Counter enums */

/* Device counter types */
enum td_device_counter_type {
	TD_DEVICE_COUNTER_READ,
	TD_DEVICE_COUNTER_WRITE,
	TD_DEVICE_COUNTER_CONTROL,
	TD_DEVICE_COUNTER_TOKEN,
	TD_DEVICE_COUNTER_MISC,
	TD_DEVICE_COUNTER_TYPE_MAX,
};

/* Device General Counters */
enum td_dev_gen_counter {
	TD_DEV_GEN_COUNT_REQ_LAT,                   /* !< latest random latency (nsec) */
	TD_DEV_GEN_COUNT_REQ_LAT_ACUMU,             /* !< accumulated latencies */
	TD_DEV_GEN_COUNT_REQ_LAT_CNT,               /* !< number accumulated latencies */
	TD_DEV_GEN_COUNT_HW_LAT,                    /* !< latest random latency (nsec) */
	TD_DEV_GEN_COUNT_HW_LAT_ACUMU,              /* !< accumulated latencies */
	TD_DEV_GEN_COUNT_HW_LAT_CNT,                /* !< number accumulated latencies */
	TD_DEV_GEN_COUNT_MAX
};

#define TD_DEV_READ_REQ_LAT		TD_DEV_GEN_COUNT_REQ_LAT
#define TD_DEV_READ_REQ_LAT_ACUMU	TD_DEV_GEN_COUNT_REQ_LAT_ACUMU
#define TD_DEV_READ_REQ_LAT_CNT		TD_DEV_GEN_COUNT_REQ_LAT_CNT
#define TD_DEV_READ_HW_LAT		TD_DEV_GEN_COUNT_HW_LAT
#define TD_DEV_READ_HW_LAT_ACUMU	TD_DEV_GEN_COUNT_HW_LAT_ACUMU
#define TD_DEV_READ_HW_LAT_ACUMU	TD_DEV_GEN_COUNT_HW_LAT_ACUMU
#define TD_DEV_READ_HW_LAT_CNT		TD_DEV_GEN_COUNT_HW_LAT_CNT

#define TD_DEV_WRITE_REQ_LAT		TD_DEV_GEN_COUNT_REQ_LAT
#define TD_DEV_WRITE_REQ_LAT_ACUMU	TD_DEV_GEN_COUNT_REQ_LAT_ACUMU
#define TD_DEV_WRITE_REQ_LAT_CNT	TD_DEV_GEN_COUNT_REQ_LAT_CNT
#define TD_DEV_WRITE_HW_LAT		TD_DEV_GEN_COUNT_HW_LAT
#define TD_DEV_WRITE_HW_LAT_ACUMU	TD_DEV_GEN_COUNT_HW_LAT_ACUMU
#define TD_DEV_WRITE_HW_LAT_ACUMU	TD_DEV_GEN_COUNT_HW_LAT_ACUMU
#define TD_DEV_WRITE_HW_LAT_CNT		TD_DEV_GEN_COUNT_HW_LAT_CNT

#define TD_DEV_GEN_COUNTER_READ_DATA(_x) (_x)
#define TD_DEV_GEN_COUNTER_WRITE_DATA(_x) (_x + TD_DEV_GEN_COUNT_MAX)
#define TD_DEV_GEN_COUNTER_CONTROL_DATA(_x) (_x + (2 * TD_DEV_GEN_COUNT_MAX) )

#define TD_DEV_TOKEN_COUNTER_START 3 * TD_DEV_GEN_COUNT_MAX
#define TD_DEV_TOKEN_COUNTER_DATA(_x) (_x + TD_DEV_TOKEN_COUNTER_START)

/* Device Token Counters */
enum td_dev_token_counter {
	TD_DEV_TOKEN_TIMEDOUT_CNT,                  /* !< number of tokens timed out */
	TD_DEV_TOKEN_FAILED_CNT,                    /* !< number of failed tokens */
	TD_DEV_TOKEN_RESETS_CNT,                    /* !< number of resets issued (after timeout) */
	TD_DEV_TOKEN_RETRIES_CNT,                   /* !< number of retries issued (after reset) */
	TD_DEV_TOKEN_RECOVERED_CNT,                 /* !< number of recovered errors (through retries) */
	TD_DEV_TOKEN_FREE_LIST_CNT,                 /* !< number of tokens on free list */
	TD_DEV_TOKEN_ACTIVE_LIST_CNT,               /* !< number of tokens on active list */
	TD_DEV_TOKEN_TIMEDOUT_LIST_CNT,             /* !< number of tokens on timedout list */
	TD_DEV_TOKEN_RESUMABLE_LIST_CNT,            /* !< number of tokens on resumable list */
	TD_DEV_TOKEN_PARTIAL_READ_LIST_CNT,         /* !< number of tokens on partial read list */
	TD_DEV_TOKEN_OFFSET_ERROR_CNT,              /* !< number of command with offset errors */
	TD_DEV_TOKEN_DATA_ECC_ERROR_CNT,            /* !< number of command ecc errors on write data  */
	TD_DEV_TOKEN_CMD_ECC_ERROR_CNT,             /* !< number of command ecc errors on cmd data  */
	TD_DEV_TOKEN_WEPTIMEOUT_CNT,                /* !< number of commands with hw errors */
	TD_DEV_TOKEN_WEPTIMEOUT_RECOVERED_CNT,      /* !< number of commands with hw errors */
	TD_DEV_TOKEN_BADXSUM_CNT,                   /* !< number of commands with bad checksums */
	TD_DEV_TOKEN_BADXSUM_RECOVERED_CNT,         /* !< number of commands with bad checksums */
	TD_DEV_TOKEN_CMD_ERROR_CNT,                 /* !< number of commands with hw errors */
	TD_DEV_TOKEN_SEQ_DUP_CNT,                   /* !< number of commands with sequence ooo */
	TD_DEV_TOKEN_SEQ_OOO_CNT,                   /* !< number of commands with sequence dup */
	TD_DEV_TOKEN_SEQ_REPLAY_CNT,                /* !< number of commands replayed for missing sequence */
	TD_DEV_TOKEN_FORCED_SEQ_ADVANCE,            /* !< number of times sequence had to be advanced */
	TD_DEV_TOKEN_SEC_IGNORE_RETRY,              /* !< number of tiems seq made us ignore failures */
	TD_DEV_TOKEN_DOUBLE_SEC_REPLAY,             /* !< number of times a SEC was replayed on both tokens */
	TD_DEV_TOKEN_LOST_REFRESH_CNT,              /* !< number of times a lost token had to be refreshed */
	TD_DEV_TOKEN_NOUPDATE_CNT,                  /* !< number of inflight command */
	TD_DEV_TOKEN_NOUPDATE_LIMIT_REACHED,        /* !< number of times the number of inflight/not-updated tokens exceeded limit */
	TD_DEV_TOKEN_COUNT_MAX

};

#define TD_DEV_MISC_COUNTER_START \
	TD_DEV_TOKEN_COUNTER_START + TD_DEV_TOKEN_COUNT_MAX

#define TD_DEV_MISC_COUNTER_DATA(_x) (_x + TD_DEV_MISC_COUNTER_START)

/* Device Misc Counters */
enum td_dev_misc_counter {
	TD_DEV_MISC_READ_MODIFY_WRITE_CNT,          /* !< number of read-modify-write operations */
	TD_DEV_MISC_DEALLOCATION_ERROR_CNT,         /* !< number of failed deallocations */
	TD_DEV_MISC_DEALLOCATION_TIMEOUT_CNT,       /* !< number of timedout deallocations */
	TD_DEV_MISC_RDBUF_PROGRESS_TIMEOUT_CNT,     /* !< number of RDBUF progress timeouts */
	TD_DEV_MISC_RDBUF_MATCHING_TIMEOUT_CNT,     /* !< number of RDBUF matching timeouts */
	TD_DEV_MISC_ORPHAN_READ_BUFFER_CNT,         /* !< number of recovered lost read buffers */
	TD_DEV_MISC_FWSTATUS_SEMA_SUCCESS_CNT,      /* !< number of flips of the fw status semaphore */
	TD_DEV_MISC_FWSTATUS_SEMA_MISSES_CNT,       /* !< number of misses on fw status semaphore */
	TD_DEV_MISC_FWSTATUS_SEMA_TIMEOUT_CNT,      /* !< number of timeouts on fw status semaphore */
	TD_DEV_MISC_FWSTATUS_SEMA_TIMEOUT_MAX_CNT,  /* !< number of times the timeout max was reached */
	TD_DEV_MISC_RDBUF_MARKER_ERROR_CNT,         /* !< number of misses on readbuf metadata */
	TD_DEV_MISC_COUNT_MAX
};

/* End of Device Counter enums */

enum td_devgroup_conf_type {
	TD_DEVGROUP_CONF_GENERAL,
	TD_DEVGROUP_CONF_WORKER,
	TD_DEVGROUP_CONF_TYPE_MAX
};

enum td_devgroup_conf_general {
	TD_DEVGROUP_CONF_GENERAL_ENDIO_ENABLE,	/**< number of host visible write buffers (write entry point) */
	TD_DEVGROUP_CONF_GENERAL_ENDIO_SPIN_MSEC,
	TD_DEVGROUP_CONF_GENERAL_MAX
};

enum td_devgroup_conf_worker {
	TD_DEVGROUP_CONF_WORKER_MAX_OCCUPANCY,
	TD_DEVGROUP_CONF_WORKER_EXTRA_TOKENS,
	TD_DEVGROUP_CONF_WORKER_MAX_LOOPS,
	TD_DEVGROUP_CONF_WORKER_WITHOUT_DEVS_NSEC,
	_deprecated_TD_DEVGROUP_CONF_WORKER_SHARE_ENDIO_NSEC,
	TD_DEVGROUP_CONF_WORKER_SHARE_NSEC,
	TD_DEVGROUP_CONF_WORKER_RELEASE_NSEC,
	TD_DEVGROUP_CONF_WORKER_FORCE_RELEASE_NSEC,
	TD_DEVGROUP_CONF_WORKER_DEV_IDLE_JIFFIES,
	TD_DEVGROUP_CONF_WORKER_DEV_WAIT_JIFFIES,
	TD_DEVGROUP_CONF_WORKER_SLEEP_JIFFIES_MIN,
	TD_DEVGROUP_CONF_WORKER_SLEEP_JIFFIES_MAX,
	TD_DEVGROUP_CONF_WORKER_SYNC_JIFFIES,
	TD_DEVGROUP_CONF_WORKER_WAKE_SHARE,
	TD_DEVGROUP_CONF_WORKER_WAKE_SLEEP,
	TD_DEVGROUP_CONF_WORKER_MAX
};

/* Start of Device Group Counter enums */
enum td_devgroup_counter_type {
	TD_DEVGROUP_COUNTER_ENDIO,
	TD_DEVGROUP_COUNTER_WORKER,
	TD_DEVGROUP_COUNTER_TYPE_MAX
};

/* Device Group End IO Counters */
enum td_devgroup_endio_counter {
	TD_DEVGROUP_ENDIO_COUNT_QUEUED,
	TD_DEVGROUP_ENDIO_COUNT_WAKES,
	TD_DEVGROUP_ENDIO_COUNT_CORKS,
	TD_DEVGROUP_ENDIO_COUNT_RUNS,
	TD_DEVGROUP_ENDIO_COUNT_DONE,
	TD_DEVGROUP_ENDIO_COUNT_STOLEN,
	TD_DEVGROUP_ENDIO_COUNT_MAX
};

/* Device Group Worker Counters */
enum td_devgroup_worker_counter {
	TD_DEVGROUP_WORKER_COUNT_LOOP1,
	TD_DEVGROUP_WORKER_COUNT_LOOP2,
	TD_DEVGROUP_WORKER_COUNT_LOOP3,
	TD_DEVGROUP_WORKER_COUNT_EXIT_CORRECTLY,
	TD_DEVGROUP_WORKER_COUNT_EXIT_ENDIO,
	TD_DEVGROUP_WORKER_COUNT_EXIT_FINAL,
	TD_DEVGROUP_WORKER_COUNT_EXIT_IDLE,
	TD_DEVGROUP_WORKER_COUNT_WAKE_CHECK,
	TD_DEVGROUP_WORKER_COUNT_NO_WAKE_EARLY,
	TD_DEVGROUP_WORKER_COUNT_NO_WAKE_TOKEN,
	TD_DEVGROUP_WORKER_COUNT_MAX
};

enum td_conf_cmd_write_mode {
	TD_CMD_WRITE_MEMCPY = 0,
	TD_CMD_WRITE_MOVNTI,
	TD_CMD_WRITE_FENCED_MOVNTI,
	TD_CMD_WRITE_MOVNTQ,
	TD_CMD_WRITE_FENCED_MOVNTQ,
	TD_CMD_WRITE_MODE_MAX
};

enum td_conf_ooo_replay_mode {
	TD_OOO_REPLAY_OFF = 0,
	TD_OOO_REPLAY_QUICK,
	TD_OOO_REPLAY_DELAYED,
	TD_OOO_REPLAY_MAX
};

enum td_eng_mcefree_reg {

	TD_CONF_MCEFREE_STATUS_R2R_NSEC,        /*< request-to-request time */
	TD_CONF_MCEFREE_STATUS_R2P_NSEC,        /*< request-to-poll time */
	TD_CONF_MCEFREE_STATUS_P2P_NSEC,        /*< poll-to-poll time */

	TD_CONF_MCEFREE_STATUS_R2P_TIMEOUT_NSEC,/*< if r2r misses, when to give up and send another req */
	TD_CONF_MCEFREE_STATUS_R2P_RETRY_MAX,   /*< if r2r misses, how many retries */
	TD_CONF_MCEFREE_HALT_ON_STATUS_TIMEOUT, /*< if there is a semaphore timeout, halt */

	TD_CONF_MCEFREE_PROGRESS_TIMEOUT_USEC, /**< MCEFREE matching rdbufs to tokens timed out */
	TD_CONF_MCEFREE_TOKEN_TIMEOUT_USEC, /**< partially completed reads will timeout in this time */

	TD_CONF_MCEFREE_STATUS_HOLD_NSEC, /**< don't touch hardware after sending hardware commands */

	/* read buffer metadata hold */

	TD_CONF_MCEFREE_RDBUF_RETRY_NSEC,       /*< current hold time */

	TD_CONF_MCEFREE_RDBUF_HOLD_NSEC,        /*< current hold time */
	TD_CONF_MCEFREE_RDBUF_HOLD_MIN_NSEC,    /*< min hold off time */
	TD_CONF_MCEFREE_RDBUF_HOLD_MAX_NSEC,    /*< max hold off time */
	TD_CONF_MCEFREE_RDBUF_HOLD_CLIMB_NSEC,  /*< increases hold time by this much */
	TD_CONF_MCEFREE_RDBUF_HOLD_DROP_NSEC,   /*< decreases hold time by this much */
	TD_CONF_MCEFREE_RDBUF_HOLD_COOL_MSEC,   /*< start dropping after this time */

	TD_CONF_MCEFREE_DEALLOC_HOLD_NSEC,      /*< how long to wait after a write before a deallocate */
	TD_CONF_MCEFREE_POST_DEALLOC_DELAY_NSEC, /*< after deallocate wait this much */

	TD_CONF_MCEFREE_WRITE_HOLD_NSEC,        /*< how long to wait after a deallocate before a write */

	TD_CONF_MCEFREE_JIT_DEALLOCATES,        /*< only deallocate when processing completed reads */

	TD_CONF_MCEFREE_NO_WRITE_WHILE_FWSTATUS, /*< prevent writes from starting while FWSTATUS request is pending */

	TD_CONF_MCEFREE_FWSTATUS_HOLD_NSEC,     /**< don't request FWSTATUS after sending write commands */

	TD_CONF_MCEFREE_STATUS_R2P_MIN_NSEC,    /*< min hold off time */
	TD_CONF_MCEFREE_STATUS_R2P_MAX_NSEC,    /*< max hold off time */
	TD_CONF_MCEFREE_STATUS_R2P_CLIMB_NSEC,  /*< increases hold time by this much */
	TD_CONF_MCEFREE_STATUS_R2P_DROP_NSEC,   /*< decreases hold time by this much */
	TD_CONF_MCEFREE_STATUS_R2P_COOL_MSEC,   /*< start dropping after this time */

	TD_CONF_MCEFREE_REGS_MAX,
};


enum td_commit_type {
	TD_FULL_COMMIT        = 0,
	TD_WAIT_SSD_COMMIT    = 1,
	TD_SEQUENCED_COMMIT   = 2,
	TD_RECEIVED_COMMIT    = 3,
	TD_QUEUED_COMMIT      = 4,
	TD_SUPER_EARLY_COMMIT = 5,
	TD_TRIPLE_SEC         = 6,
};

/** device configuration variables */
enum td_device_conf_type {
	TD_DEVICE_CONF_ENGINE,
	TD_DEVICE_CONF_HARDWARE,
	TD_DEVICE_CONF_HAL,
	TD_DEVICE_CONF_MCEFREE,
	TD_DEVICE_CONF_RAID,
	TD_DEVICE_CONF_TYPE_MAX,
};

/* TODO: Change these enums into using the macros like the engine ones */
enum td_device_conf_sim {
	TD_CONF_SIM_CHANNELS,
	TD_CONF_SIM_LUNS,
	TD_CONF_SIM_LUNBUSSES,
	TD_CONF_SIM_XFER_HOST_CORE_NSEC,
	TD_CONF_SIM_XFER_CORE_SSD_NSEC,
	TD_CONF_SIM_XFER_SSD_LUN_NSEC,
	TD_CONF_SIM_PROC_CMD_NSEC,
	TD_CONF_SIM_PROC_STAT_NSEC,
};

enum td_device_conf_md {
	TD_CONF_MD_CHANNELS,
};

/* end of todo */

enum td_timeout {
	TD_TIMEOUT_OOO,
	TD_TIMEOUT_QUEUED,
	TD_TIMEOUT_RECEIVED,
	TD_TIMEOUT_WAIT_SSD,
	TD_TIMEOUT_MAX,
};

struct td_timeout_desc {
	enum td_timeout to;
	enum td_eng_regs bio_r;
	enum td_eng_regs bio_w;
	enum td_eng_regs ucmd;
};


/** the device can be in one of these operational states */
enum td_device_state_type {
	TD_DEVICE_STATE_CREATED = -1,
	TD_DEVICE_STATE_OFFLINE,
	TD_DEVICE_STATE_ONLINE,

	TD_DEVICE_STATE_RAID_MEMBER, /*!< TD_NORMAL_DEVICE is part of a raid set */

	TD_DEVICE_STATE_TYPE_MAX
};

enum td_raid_state_type {
	TD_RAID_STATE_CREATED = -1,
	TD_RAID_STATE_OFFLINE,
	TD_RAID_STATE_OPTIMAL,
	TD_RAID_STATE_DEGRADED,
	TD_RAID_STATE_RESYNC,
	TD_RAID_STATE_FAILED,
	TD_RAID_STATE_TYPE_MAX
};

/** the engine can be in one of these run-time states */
enum td_engine_run_state {
	TD_RUN_STATE_INIT,
	/** INIT state is entered when td_engine_init() completes.
	 * Engine remains in this state until user attempts to start
	 * the device to bring it online.  Next state is READ_PURGE. */

	TD_RUN_STATE_READ_PURGE,
	/** READ_PURGE state is entered when td_engine_start()
	 * completes.  Engine will now have to deallocate all read
	 * buffers before continuing.  When finished, engine will
	 * enter FW_PROBE state. */

	TD_RUN_STATE_FW_PROBE,
	/** FW_PROBE state is entered automatically when READ_PURGE
	 * state finishes.  The engine will query firmware for
	 * configuration.  On success it enters RUNNING state, on
	 * failure it enters DEAD state.  Either way a callback is
	 * issued to the device object to inform it of the
	 * configuration change */

	TD_RUN_STATE_RUNNING,
	/** RUNNING state is entered automatically when FW_PROBE state
	 * finishes successfully.  Next state is PRE_SYNC_IO, SYNC_IO,
	 * or DEAD. */

	TD_RUN_STATE_PRE_SYNC_IO,
	/** PRE_SYNC_IO when the next bio in the queue is a
	 * synchronising request.  Engine waits for all other requests
	 * to drain, and enters SYNC_IO state. */

	TD_RUN_STATE_SYNC_IO,
	/** SYNC_IO is entered when the next request is a
	 * synchronising one, and waits for it to complete in
	 * isolation.  Next state is RUNNING */

	TD_RUN_STATE_TERMINATING,
	/** TERMINATING state is entered by by call to td_engine_stop()
	 * next state will be DEAD, as soon as thread running IOs is
	 * able to advance there safely */

	TD_RUN_STATE_OOO_SEQUENCE,
	/** OOO_SEQUENCE state is entered by the device when we receive
	 * an Out Of Order error.  We can accept new requests, but we won't
	 * send them until the sequence command is returned successful.
	 */

	TD_RUN_STATE_PM_DRAIN,
	/** PM_DRAIM state is entered by the device when we receive a power
	 * management (PM) request to hibernate/suspend.  The device will train
	 * all outstanding read/writes, but no new requests will start. */

	TD_RUN_STATE_PM_SHUTDOWN,
	/** PM_SHUTDOWN state is entered by the device from the PM_DRIAN state
	 * when there are no outstanding commands.  We then issue a shutdown
	 * if required. */

	TD_RUN_STATE_PM_SLEEP,
	/** PM_SLEEP state is entered when the device is finished draining the
	 * started I/O and the device is in the PM_DRIAN State.
	 * */

	TD_RUN_STATE_BIO_DRAIN,
	/** BIO_DRAIN state is entered when the device wants to do only UCMDs.
	 * Once all BIOs are done, UCMD_ONLY state is automatically entered.
	 * */

	TD_RUN_STATE_UCMD_ONLY,
	/** UCMD_ONLY state is entered when no BIOs are left and state =
	 * BIO_DRIAN.  This allows upgrade to occur while returning -EBUSY for
	 * any IOs.
	 * */

	TD_RUN_STATE_DEAD,
	/** DEAD state is entered when device stops responding. */

	TD_RUN_STATE_MAX
};

/** enumerates different types of buffers */

enum td_buf_type {
	TD_BUF_GLOBAL_STATUS     = 0, /**!< single instance (1B) */
	TD_BUF_GLOBAL_EX_STATUS  = 1, /**!< single instance (8B) */
	TD_BUF_CMD_STATUS        = 2, /**!< single instance (256B) */
	TD_BUF_READ              = 3, /**!< 4k read buffers */
	TD_BUF_READ_META         = 4, /**!< 128B read meta data */
	TD_BUF_WRITE             = 5, /**!< 4k write buffers */
	TD_BUF_WRITE_META        = 6, /**!< 128B write meta data */
	TD_BUF_COMMAND           = 7, /**!< 64B command buffers */
	TD_BUF_EX_STATUS         = 8, /**!< 8B extended status */
	TD_BUF_TYPE_MAX
};

/* Raid definitions */

enum td_raid_level {
	TD_RAID_UNKNOWN = -1,          /**!< assigned to normal /dev/tdX devices */
	TD_RAID_0       = 0,
	TD_RAID_1       = 1,
	TD_RAID_10      = 10,
	TD_RAID_STRIPE  = TD_RAID_0,
	TD_RAID_MIRROR  = TD_RAID_1,
};

enum td_raid_member_state {
	TR_MEMBER_EMPTY = 0,
	TR_MEMBER_ACTIVE,
	TR_MEMBER_FAILED,
	TR_MEMBER_SPARE,
	TD_RAID_MEMBER_STATE_MAX
};

enum td_raid_conf_type {
	TD_RAID_CONF_INVALID     = 0,
	TD_RAID_CONF_GENERAL     = 1,
	TD_RAID_CONF_OPS,
	TD_RAID_CONF_MAX
};

enum tr_general_conf_type {
	TR_CONF_GENERAL_LEVEL    = 0,
	TR_CONF_GENERAL_MEMBERS,
	TR_CONF_GENERAL_MAX
};

enum tr_stripe_conf_type {
	TR_CONF_STRIPE_STRIDE    = 0,
	TR_CONF_STRIPE_MAX
};

enum tr_mirror_conf_type {
	TR_CONF_MIRROR_UNUSED    = 0,
	TR_CONF_MIRROR_MAX
};

struct td_uuid {
	uint8_t uuid[16];
};



#endif
