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

#ifndef _TD_ENGINE_DEF_H_
#define _TD_ENGINE_DEF_H_

#define TD_BACKPRESSURE_NONE   0
#define TD_BACKPRESSURE_SLEEP  1
#define TD_BACKPRESSURE_EVENT  2


#ifndef CONFIG_TERADIMM_INCOMING_BACKPRESSURE
#define CONFIG_TERADIMM_INCOMING_BACKPRESSURE    TD_BACKPRESSURE_NONE
#endif

/*
 * Used to calculate the number of discard/trim commands
 * (max LBAs per entry (65535) * 64 entries)
 */
#define TD_MAX_DISCARD_CHUNK      0xFFFFll
#define TD_MAX_DISCARD_LBA_COUNT  (TD_MAX_DISCARD_CHUNK * 64)
/**
 * used to track read buffers
 */
#ifdef CONFIG_TERADIMM_RDBUF_TRACKING
#ifdef CONFIG_TERADIMM_RDBUF_TRACKING_IOCTL
	#define td_rd_buf __td_rd_buf
#else
struct td_rd_buf {
	struct list_head        drb_lru_link;         /**< position in LRU list */
	uint16_t                drb_rd_bufid;         /**< ID of this read buffer */
	uint16_t                drb_last_failed:1;    /**< flag indicating last deallocation failed */
	uint64_t	            drb_last_used;        /**< jiffies when last used this entry */
};
#endif
#endif

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
struct td_rdbuf {
	struct list_head        rb_lru_link;         /**< position in LRU list */
	uint8_t                 rb_rd_bufid;         /**< ID of this read buffer */
	uint8_t                 rb_in_rush:1;        /**< on td_rdbufs_in_rush list */
	uint8_t                 rb_needs_dealloc:1;  /**< on td_rdbufs_needing_deallocation list */
	uint16_t                rb_last_core_bufid;  /**< last core buffer association */
	uint64_t                rb_last_cmd_word;    /**< last command using this read buffer */
	cycles_t                rb_last_dealloc;     /**< timestamp of last deallocate */
};
struct td_corebuf {
	struct list_head        cb_lru_link;         /**< position in LRU list */
	uint8_t                 cb_core_bufid;       /**< ID of this core buffer */
	uint8_t                 cb_available;        /**< on td_corebufs_available list */
	uint16_t                cb_used_by_tokid;    /**< core buffer used by this token */
};
#endif

/**
 * tracks the state of a hardware engine
 */
struct td_engine {
	const char *td_name;                            /**< pointer to containing device's name */

	/* variables used by the device group */
	uint64_t dg_last_use_jiffies;                   /**< record when the devgroup thread last accessed this device */
	struct {
		/** these counters are updated by the engine while issuing and
		 * finishing commands; the counters are only reset by the
		 * devgroup (which calls td_engine_reset_loop_counters().
		 * the device group uses these to track and trace how much
		 * work was done in each pass through the loop */
		struct {
			unsigned writes, reads, control;
		} started, completed;
	} loop_counters;

	/** health state while active */
	enum td_engine_run_state td_run_state;
	enum td_engine_run_state td_prev_state;
	int64_t td_run_state_jiffies;
	struct completion	td_state_change_completion; /**< used to notify state changes */

	/* For our safe_work API */
	td_atomic_ptr_t         td_safe_work;

	/** counters ready for export via IOCTL */
	struct td_ioctl_device_stats td_stats;
	struct td_ioctl_device_counters_internal td_counters;

	/* structures to help track latencies */
	struct td_eng_latency   td_bio_latency;
	struct td_eng_latency   td_tok_latency;

	/* this queue is locked by thread and block layer */
	spinlock_t              td_incoming_bio_lock;     /**< queue lock */
	struct bio_list         td_incoming_bios;  /**< requests from block layer */
	uint64_t                td_incoming_bio_reads;
	uint64_t                td_incoming_bio_writes;

#if CONFIG_TERADIMM_INCOMING_BACKPRESSURE == TD_BACKPRESSURE_EVENT
	/* if the incoming queue is too busy, we wait */
	atomic_t                td_total_system_bios;
	atomic_t                td_total_incoming_waiters;
	wait_queue_head_t       td_incoming_sleep;
#endif
#if CONFIG_TERADIMM_INCOMING_BACKPRESSURE == TD_BACKPRESSURE_SLEEP
	atomic64_t               td_incoming_backpressure_sleep_count;
#endif

	/* incoming requests that were already removed and owned by the thread
	 * are here (this is done to limit number of times the
	 * td_incoming_bio_lock is used by the thread) */
	struct bio_list         td_queued_bios;  /**< moved from incoming to queued */
	uint64_t                td_queued_bio_reads;
	uint64_t                td_queued_bio_writes;

	/* queued control messages */
	struct list_head        td_queued_ucmd_list;
	spinlock_t              td_queued_ucmd_lock;     /**< queue lock */
	uint64_t                td_queued_ucmd_count;

	/** commands are tracked in tokens */
	struct td_token         td_tokens[TD_TOKENS_PER_DEV];

	/** control the split between hw and fw tokens
	 * tokens are split into the following groups:
	 *
	 *   FW  - starts at token ID zero, goes upto some limit
	 *       - these are sent to FW, and when FWSTATUS is enabled,
	 *         status comes back in WEP-7
	 *       - upper limit defined by TD_CONF_HW_TOKID_START
	 *
	 *   HW  - starts after 'FW' type, goes upto some limit
	 *       - this is for all commands sent to hardware (reg/null/dealloc)
	 *       - upper limit defined by TD_CONF_RESERVED_TOKID_START
	 *
	 *   reserved - anything above the 'HW' group is reserved and cannot
	 *         be allocated via normal means.  These are treated like hardware
	 *         commands in the engine.
	 *       - upper limit defined by TD_CONF_TOKENS
	 */
	struct td_token_pool {
		struct td_token_list    td_free_tokens,        // tokens ready to go
					td_active_tokens,      // tokens with commands in flight
					td_timedout_tokens,    // tokens with timeouts, waiting for recovery
					td_resumable_tokens;   // tokens with resolved problems, needing reset
	} tok_pool[TD_TOK_TYPE_MAX];

#ifdef CONFIG_TERADIMM_RUSH_INGRESS_PIPE
	/* track tokens that have seen no updates */
	DECLARE_BITMAP(td_no_update_map,TD_TOKENS_PER_DEV);
#endif

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	/**
	 * In mcefree mode, reads don't always have a read buffer attached to
	 * them when they complete.  The completion loop will append completed
	 * reads on this list, and the mcefree matcher will move them off once
	 * the read buffers is known.
	 *
	 * Because these are IO reads, they are of type TD_TOK_FOR_FW.
	 */
	struct td_token_list td_early_completed_reads_tokens;

	/**
	 * record when progress was last made on the early completed list.
	 * Once PROGRESS_TIMEOUT_USEC expires, the read buffer is timed
	 * out. */
	cycles_t td_early_completed_reads_last_progress;

	/**
	 * record when the last hardware command was issued, this is used to
	 * throttle status reads, and accesses read buffer metadata */
	cycles_t td_last_hardware_command;

	/**
	 * record when the last deallocate command was issued, this is used to
	 * keep out of the read buffer metadata while it's being filled */
	cycles_t td_last_deallocate_command;

	/**
	 * record when the last write command was issued, so that it can stall
	 * future deallocate commands if DEALLOC_HOLD_NSEC is set */
	cycles_t td_last_write_command_for_dealloc;

	/**
	 * record when the last write command was issued, so that it can stall
	 * future FWSTATUS requests if HOLD_FWSTATUS_NSEC is set */
	cycles_t td_last_write_command_for_fwstatus;

	/**
	 * record the last time the semaphore was observed to not change
	 * in time */
	cycles_t td_last_fwstatus_sema_error;

	/**
	 * record the last time a read buffer marker error occured */
	cycles_t td_last_rdbuf_marker_error;

	/*
	 * FWSTATUS request was sent, and has not completed
	 */
	cycles_t td_last_fwstatus_request_posted;

	/**
	 * track deallocates in flight, when read buffers are filled by
	 * hardware, deallocates of that buffer can be inferred to be
	 * finished */
	uint16_t td_active_rdbuf_deallocation_token[TD_HOST_RD_BUFS_PER_DEV];

	/* temporary fix to blowing the stack with too many loops */
	unsigned mcefree_nested_count;
#endif

	/* Token used for maintenance command such as OoO and Handshake. */
	struct td_token         *maint_toks[TD_TOK_TYPE_MAX];
#define td_eng_maint_tok(eng,tt) (eng)->maint_toks[(tt)]
#define td_eng_fw_maint_tok(eng) td_eng_maint_tok(eng,TD_TOK_FOR_FW)
#define td_eng_hw_maint_tok(eng) td_eng_maint_tok(eng,TD_TOK_FOR_HW)

	/** count read-modify-write transactions */
	unsigned                td_active_rmw_tokens_count;

	/** token sequences need to be tracked **/
	uint64_t td_sequence_next;
	uint64_t td_sequence_oldest;

	/** array of status bytes */
#ifdef CONFIG_TERADIMM_DIRECT_STATUS_ACCESS
	uint8_t                 *td_status;
#else
	uint8_t                 td_status[TD_TOKENS_PER_DEV] __aligned64;
#endif
	
	/** used by td_eng_dump_status_changes(), if enabled */
#ifdef CONFIG_TERADIMM_DEBUG_STATUS_CHANGES
	uint8_t                 td_last_status[TD_TOKENS_PER_DEV];
	uint64_t                td_no_events;
#endif

	/* keeps track of number of times status was polled */
	uint64_t                td_polling_loop_count;

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	/** an array of corebuf tracking structures */
	struct td_corebuf       td_corebufs[TD_CORE_BUFS_PER_DEV];

	/** ordered list of available core buffers in LRU order */
	struct list_head        td_corebufs_available;
	unsigned                td_corebufs_available_count;
#else
	/** core buffer allocation bitmap */
	DECLARE_BITMAP(td_core_buf_used_map,TD_CORE_BUFS_PER_DEV);
	unsigned                td_core_buf_used;
#endif

	/* Things to keep track of while locked. */
	void                    *locker_context;
	uint8_t                 td_core_lock_start;
	uint8_t                 td_core_lock_size;

	/* Used for stopping BIO */
	void			        *bio_context;


	/** hardware command allocation count */
	unsigned		td_hw_cmds_used;

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	/** an array of rdbuf tracking structures */
	struct td_rdbuf         td_rdbufs[TD_HOST_RD_BUFS_PER_DEV];

	/** ordered list of deallocations in LRU order */
	struct list_head        td_rdbufs_needing_deallocation;
	unsigned                td_rdbufs_needing_deallocation_count;

	/** ordered list of read buffers in LRU order */
	struct list_head        td_rdbufs_in_rush;
	unsigned                td_rdbufs_in_rush_count;

	/** temp buffer to retrieve metadata into */
	uint8_t                 td_rdbuf_metadata[128] __aligned64;

#else
	/** read buffer bitmap to tell h/w are finished */
	DECLARE_BITMAP(td_rdbuf_needs_dealloc_map,TD_HOST_RD_BUFS_PER_DEV);
#endif

#ifdef CONFIG_TERADIMM_RDBUF_TRACKING
#ifdef CONFIG_TERADIMM_RDBUF_TRACKING_IOCTL
	struct td_ioctl_device_rdbufs rdbufs;
	#define td_rd_bufs            rdbufs._td_rd_bufs
	#define td_rd_buf_lru         rdbufs._td_rd_buf_lru
	#define td_rd_buf_lru_seen_other_reads   rdbufs._td_rd_buf_lru_seen_other_reads
	#define td_rd_buf_lru_seen_read_timeouts rdbufs._td_rd_buf_lru_seen_read_timeouts
#else
	struct td_rd_buf        td_rd_bufs[TD_HOST_RD_BUFS_PER_DEV];
	struct list_head        td_rd_buf_lru;
	unsigned                td_rd_buf_lru_seen_other_reads; /**< count number of reads seen while this entry was LRU */
	unsigned                td_rd_buf_lru_seen_read_timeouts; /**< count number of reads that timed out while this entry was LRU */
#endif
#endif

	/** write buffers tell us which entry points are available */
	DECLARE_BITMAP(td_wr_buf_used_map,TD_HOST_WR_BUFS_PER_DEV);
	unsigned                td_wr_buf_used;

	/** calculate dynamic IOP rate */
	uint64_t                td_sample_window;   /**< desired length of the sample window */
	uint64_t                td_sample_start;    /**< jiffies at start time of the sample window */
	uint64_t                td_sample_reads;    /**< counter of initial IOs in current window */
	uint64_t                td_sample_writes;   /**< counter of initial IOs in current window */
	uint64_t                td_sample_control;  /**< counter of initial IOs in current window */
	uint64_t                td_sample_rbytes;   /**< counter of initial bytes in current window */
	uint64_t                td_sample_wbytes;   /**< counter of initial bytes in current window */

	/* count all BIOs seen */
	uint64_t                td_total_bios;

	/* abstract interface (simulator or hardware) */
	struct td_eng_hal_ops *ops;
	void *ops_priv;

	/* handlers that copy bio and virt buffers to the device and back
	 * (set by td_eng_hal_enable in each HAL) */
	struct td_token_copy_ops td_bio_copy_ops;
	struct td_token_copy_ops td_virt_copy_ops;

	struct bio_list         td_rmw_bios;
	int                     td_rmw_inflight;
#ifdef CONFIG_TERADIMM_TRACE
	struct td_trace         td_trace;
#endif
	unsigned                td_last_was_write:1;

	struct td_eng_conf      conf;
	/* Used for power management. */
	uint8_t pm_power_cycle;

/* BETA2MERGE TBD


	TD_DECLARE_HISTOGRAM(hist_started);
	TD_DECLARE_HISTOGRAM(hist_post_start_active);
	TD_DECLARE_HISTOGRAM(hist_post_start_queued);
	TD_DECLARE_HISTOGRAM(hist_completed);
	TD_DECLARE_HISTOGRAM(hist_post_comp_active);
	TD_DECLARE_HISTOGRAM(hist_post_comp_queued);
*/
	uint64_t                td_errors_mask;
	uint64_t                td_errors_count;

#ifdef CONFIG_TERADIMM_PRIVATE_SPLIT_STASH
	struct td_stash_info *  td_split_stash;
#endif

#ifdef CONFIG_TERADIMM_DEVGROUP_TASK_WORK
	/* Queue SCSI commands to be processed */
	struct list_head        td_queued_task_list;
	spinlock_t              td_queued_task_lock;     /**< queue lock */
	uint64_t                td_queued_task_count;

	/* A SLAB/POOL for queuing extra
	* work */
	struct kmem_cache_s     *work_slab;
	mempool_t               *work_pool;
#endif

	struct td_ecc_bins ecc_bins;
};




#endif
