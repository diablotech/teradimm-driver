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

#ifndef _TD_TOKEN_H_
#define _TD_TOKEN_H_

#include "td_kdefn.h"

#include "td_compat.h"

#include "td_defs.h"
#include "td_bio.h"
#include "td_util.h"

struct page;
struct td_ucmd;
struct td_engine;
struct td_token;

#define TD_TOKEN_PRE_COMPLETION_DONE   0
#define TD_TOKEN_PRE_COMPLETION_STOLEN 1

#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
enum td_token_type {
	/* different types of tokens, one for any occasion */
	TD_TOK_FOR_FW, /* fw commands use lower numbered tokens */
	TD_TOK_FOR_HW, /* hw commands use upper numbered tokens */
	TD_TOK_TYPE_MAX
};
#else
enum td_token_type {
	/* this means it's disabled, so all types to to the same token type pool */
	TD_TOK_SAME_TYPE_FOR_EVERYTHING = 0,
	TD_TOK_FOR_HW = TD_TOK_SAME_TYPE_FOR_EVERYTHING,
	TD_TOK_FOR_FW = TD_TOK_SAME_TYPE_FOR_EVERYTHING,
	TD_TOK_TYPE_MAX = 1 /* there is only one kind */
};
#endif

#define for_each_token_type(_tt) \
	for(_tt=0;_tt<TD_TOK_TYPE_MAX;_tt++)

#define for_each_token_type_reverse(_tt) \
	for(_tt=(TD_TOK_TYPE_MAX-1);(int)_tt>=(int)0;_tt--)

/** some copies target upto 3 wep targets, this structure tracks these pointers */
struct td_multi_target {
	struct {
		void *data;
		void *meta;
#define TD_MULTI_TARGET_MAX 3
	} buf[TD_MULTI_TARGET_MAX];
	unsigned used;
};

static inline void td_multi_target_init(struct td_multi_target *mt)
{
	memset(mt, 0, sizeof(struct td_multi_target));
}

static inline void td_multi_target_add(struct td_multi_target *mt,
		void *data, void *meta)
{
	unsigned idx = mt->used ++;
	WARN_ON(idx > TD_MULTI_TARGET_MAX);
	mt->buf[ idx ].data = data;
	mt->buf[ idx ].meta = meta;
}

/** result codes used in tok->result */
enum td_token_result {
	TD_TOK_RESULT_OK               = 0,           /* finished successfully */
	TD_TOK_RESULT_ACTIVE           = -EBUSY,      /* command executing */
	TD_TOK_RESULT_TIMEOUT          = -ETIMEDOUT,  /* didn't complete in time */
	TD_TOK_RESULT_FAIL_CAN_RETRY   = -EAGAIN,     /* should be retired (xsum error) */
	TD_TOK_RESULT_FAIL_ABORT       = -EIO,        /* device failed command */
	TD_TOK_RESULT_FAIL_ABORT_HARD  = -EREMOTEIO,  /* device had unrecoverable error on SSD */
	TD_TOK_RESULT_WARN             = -EADV,       /* Advertise error */
};

struct td_token_ops {

	/**
	 * check for status change
	 * @param tok - token with outstanding command
	 * @return zero if there is no further work
	 *
	 * When returning true, indicating the token needs further work,
	 * the caller will check tok->result to determine what to do next.
	 */
	int (*status_check)(struct td_token *tok);

	/**
	 * early commit (write only)
	 * @param tok - token with outstanding write command
	 * @param result - result to complete with
	 *
	 * Function to call in case of early commit on a write command.
	 */
	void (*early_commit)(struct td_token *tok,
			int result);

	/**
	 * pre completion helper hook
	 * @param tok - token with outstanding command
	 *
	 * If set, this function will be called after commands fully complete,
	 * but before their buffers are freed.  This allows for any kernel
	 * originated messages to act on completed commands before the engine
	 * frees the buffer.
	 *
	 * If the hook returns zero, the token is freed.  If the hook returns
	 * a 1 no further action is taken.  The latter mode is used to allow
	 * the implementation to reuse the token right away.
	 *
	 * See TD_TOKEN_PRE_COMPLETION_DONE and TD_TOKEN_PRE_COMPLETION_STOLEN
	 */
	int (*pre_completion_hook)(struct td_token *tok);

	/**
	 * function called at token completion
	 * @param tok - token with outstanding command
	 */
	void (*completion)(struct td_token *tok);

	/**
	 * called to clear deallocation request from command
	 * @param tok - token with outstanding command
	 */
	void (*clear_deallocation)(struct td_token *tok);
};

struct td_token_copy_ops {

	/**
	 * called to copy data from device to host buffer
	 * @param tok - token with outstanding command
	 * @param read_buf_virt - device read buffer to read from
	 * @param meta_data_virt - device read meta-data buffer to read from
	 * @return number of bytes copied
	 *
	 * Amount of data transfered is specific to implementation, but most
	 * likely 4k data and 64B metadata.
	 *
	 * The intent of this callback is to use the appropriate destination
	 * {bio,ucmd,page} buffer from the tok->host union.
	 */
	int (*dev_to_host)(struct td_token *tok,
			const void *read_buf_virt,
			const void *meta_data_virt,
			const void *read_buf_alias,
			const void *meta_data_alias);

	/**
	 * called to copy data from host buffer to device
	 * @param tok - token with outstanding command
	 * @param write_buf_virt - device write buffer to write to
	 * @param meta_data_virt - device write meta-data buffer to write to
	 * @return number of bytes copied
	 *
	 * The intent of this callback is to use the appropriate source
	 * {bio,ucmd,page} buffer from the tok->host union.
	 *
	 * Amount of data transfered is specific to implementation, but most
	 * likely 4k data and 64B metadata.
	 *
	 * This function is responsible for setting tok->data_xsum to the
	 * checksum of the data copied.
	 */
	int (*host_to_dev)(struct td_token *tok,
			void *write_buf_virt,
			void *meta_data_virt);

	/**
	 * called to copy data from host buffer to two device buffers
	 * @param tok - token with outstanding command
	 * @param mt - upto 3 pairs of data/meta-data pointers to write to
	 * @return number of bytes copied
	 *
	 * This functions works like the host_to_dev() function, but copies
	 * from one source buffer (from the token) to one, two, or three
	 * device buffers.
	 */
	int (*host_to_multi_dev)(struct td_token *tok,
			struct td_multi_target *mt);

};

/* different types of copy helpers */

extern struct td_token_copy_ops td_token_copy_ops_null;

extern struct td_token_copy_ops md_token_copy_ops_bio;
extern struct td_token_copy_ops md_token_copy_ops_virt;

extern struct td_token_copy_ops td_token_copy_ops_bio;
extern struct td_token_copy_ops td_token_copy_ops_virt;
extern struct td_token_copy_ops td_token_copy_ops_bio_e2e_512B;
extern struct td_token_copy_ops td_token_copy_ops_bio_520B;
extern struct td_token_copy_ops td_token_copy_ops_kbio_512B;
extern struct td_token_copy_ops td_token_copy_ops_bio_e2e_4kB;

extern struct td_token_copy_ops td_token_copy_ops_bio_4kB_zero_meta; /* HACK */

#ifdef CONFIG_TERADIMM_TOKEN_HISTORY
struct td_tok_hist {
	cycles_t ts;
	uint16_t cmd_seq;
	uint8_t  cmd_id;
	uint8_t  cmd_status;
	uint16_t hist_event;
	uint16_t hist_data;
};
#endif

/**
 * a token represents a state of a command while it is active
 */
struct td_token {
	struct td_engine    *td_engine;
	uint16_t            tokid;          /**< this token's id */

	uint16_t            core_bufid;     /**< core buffer ID */
	uint8_t             wr_bufid;       /**< write entry point */
	uint8_t             rd_bufid;       /**< read host buffer */
	uint8_t             free_rd_bufid;  /**< release this RD bufid */
#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
	uint8_t             token_type;     /**< see enum td_token_type */
#endif

	union {
		uint8_t stable_flags;
		struct {
			uint8_t magic_flags:4;  /**< magic firmware hacks */
			uint8_t odd:1;          /**< odd bit for this command */
			uint8_t meta_size:2;    /**< size of metadata. */
		};
	};
	
	union {
		/* grouped together so they can be cleared all at once */
		uint16_t volatile_flags;
		struct {
			uint16_t independent_deallocate:1; /**< command only deallocates */
			uint16_t rmw:1;          /**< this token is used for read-modify-write */
			uint16_t needs_retry:1;  /**< indicator to reset/retry logic that this needs a retry */
			uint16_t to_ssd:1;       /**< to ssd (seqeunced) */
			uint16_t port:2;         /**< which SSD port to write to. */
			uint16_t hw:1;           /**< Set for all hardware commands */
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
			uint16_t early_read:1;   /**< token on td_early_completed_reads_tokens list */
			uint16_t use_wep_alias:1;/**< use a WEP alias for DOUBLE_WEP_WRITES */
#endif
			uint16_t quick_n_quiet:1;/**< Silent, expect this to fail */
			uint16_t ooo_replay:1;   /**< waiting for OOO replay */
			uint16_t ooo_missing:1;  /**< the missing token */
			uint16_t safe_in_hw:1;   /**< the token has safely reached RUSH/FW */
		};
	};

	struct td_token     *sec_buddy;     /**< SEC buddy token, for double writes */
	uint8_t             extra_wr_bufid; /**< TripleSEC write buffer (only 1st sec buddy) */

	uint8_t             last_status;    /**< last status read from h/w */
	uint64_t            last_xstatus;   /**< last xstatus read from h/w */

	int                 result;         /**< zero on success, negative error otherwise */

	uint16_t            len_host_to_dev; /**< amount of data to write out */
	uint16_t            len_dev_to_host; /**< amount of data to read in */
	uint16_t            lba_ofs;        /**< partial LBA access within a 4k block */

	uint64_t            lba;            /**< block hardware address (in conf.hw_sector_size units) */

	uint64_t            data_xsum[2];   /**< used for write buffers */

	uint64_t            cmd_seq;        /**< command sequence, lower 16 bits passed to firmware */

	uint64_t            cmd_bytes[8] __packed_aligned16; /**< cmd bytes sent to TeraDiMM (64 bytes) */

	uint64_t            cmd_metadata[16] __packed_aligned16; /**< metadata bytes (128 bytes) */

	cycles_t            ts_start; /** < timestamp at start of bio */
	cycles_t            ts_end;   /** < timestamp at end of bio   */

	/* IO will use data in a bio (block) or ucmd (ioctl command) or page (kernel) */
	struct {
		td_bio_ref	    bio;   /**< used by block requests */
		struct td_ucmd      *ucmd;  /**< user command, from ioctl */
		struct page         *page;  /**< kernel generated command */
	} host;

	void                *host_buf_virt; /**< pointer to ucmd->data_virt or page_address(tok->page) */

	cycles_t            last_cmd_issue;  /**< the last time the command was written to HW */
	cycles_t            active_timeout; /**< timeout token at this point */
	struct list_head    link;           /**< on td_active_tokens or td_free_tokens */

	/* used for resets */
	uint16_t            reset_count;    /**< number of resets sent in a row before giving up */
	uint16_t            retry_count;    /**< number of retries attempted on this token */
	uint16_t            timeout_count;  /**< number of timeouts on this token */
	uint16_t            cmd_issue_count; /**< number of replays this token had (badxsum,timeout, etc) */
	uint16_t            cmd_refresh_count; /**< number of times this command was written */

#ifdef CONFIG_TERADIMM_TOKEN_HISTORY
	struct td_tok_hist tok_hist[CONFIG_TERADIMM_TOKEN_HISTORY];
	uint tok_hist_next;
#endif

	struct td_token_copy_ops copy_ops;
	struct td_token_ops ops;
};

enum td_tok_event {
	TD_TOK_EVENT_START,
	TD_TOK_EVENT_STATUS,
	TD_TOK_EVENT_STOLEN,
	TD_TOK_EVENT_RDMETA,
	TD_TOK_EVENT_END,
	TD_TOK_EVENT_MAX
};

static inline void __td_tok_event(struct td_token *tok,
		uint16_t cmd_seq, uint8_t  cmd_id,
		enum td_tok_event event, uint16_t data)
{
#ifdef CONFIG_TERADIMM_TOKEN_HISTORY
	struct td_tok_hist *hist;

	hist = tok->tok_hist + tok->tok_hist_next;

	hist->ts         = td_get_cycles();

	hist->cmd_seq    = cmd_seq;
	hist->cmd_id     = cmd_id;
	hist->cmd_status = tok->last_status;

	hist->hist_event = (uint16_t)event;
	hist->hist_data  = data;

	tok->tok_hist_next ++;
	tok->tok_hist_next %= CONFIG_TERADIMM_TOKEN_HISTORY;
#endif
}

static inline void td_tok_event(struct td_token *tok,
		enum td_tok_event event, uint16_t data)
{
	if (unlikely (!tok))
		return;

	__td_tok_event(tok,
		(uint16_t)tok->cmd_seq,
		(uint8_t)tok->cmd_bytes[0],
		event, data);
}

extern void td_dump_tok_events(struct td_token *tok);

#define TD_TOKEN_QUICK_N_QUIET_TIMEOUT  10

static inline struct td_engine *td_token_engine(struct td_token *tok)
{
	return tok->td_engine;
}

static inline int td_token_is_active (struct td_token * tok)
{
	return tok->result == TD_TOK_RESULT_ACTIVE;
}

/** clear configuration in @tok */
static inline void td_token_reset(struct td_token *tok)
{
	/* initialize the token to be a NULL command */
	tok->volatile_flags = 0;
	tok->magic_flags = 0;
	tok->core_bufid = TD_INVALID_CORE_BUFID;
	tok->wr_bufid = TD_INVALID_WR_BUFID;
	tok->rd_bufid = TD_INVALID_RD_BUFID;
	tok->free_rd_bufid = TD_INVALID_RD_BUFID;
	tok->len_host_to_dev = 0;
	tok->len_dev_to_host = 0;
	tok->lba_ofs = 0;
	tok->lba = 0;
	tok->last_cmd_issue = 0;
	tok->active_timeout = 0;
	tok->retry_count = 0;
	tok->reset_count = 0;
	tok->timeout_count = 0;
	tok->cmd_issue_count = 0;
	tok->cmd_refresh_count = 0;
	tok->cmd_seq = 0;

	/* Reset our SEC buddy */
	tok->sec_buddy = NULL;

	/* reset SEC WEP */
	tok->extra_wr_bufid = TD_INVALID_WR_BUFID;

	/* computed at copy time */
	memset(tok->data_xsum, 0, sizeof(tok->data_xsum));

	/* reset buffer link */
	memset(&tok->host, 0, sizeof(tok->host));
	tok->host_buf_virt = NULL;

	/* reset the ops to NULL ops */
	tok->copy_ops = td_token_copy_ops_null;

	/* reset all ops pointers */
	memset(&tok->ops, 0, sizeof(tok->ops));

	/* reset the command bytes */
	memset(tok->cmd_bytes, 0, sizeof(tok->cmd_bytes));
}

/** migrate configuration from @old to @tok, releasing any resources held
 * by @tok */
static inline void td_token_migrate(struct td_token *tok, struct td_token *old)
{

	/* copy over held resources */
	tok->core_bufid      = old->core_bufid;
	tok->wr_bufid        = old->wr_bufid;
	tok->rd_bufid        = old->rd_bufid;

	/* resources are no longer held by the original token */
	old->core_bufid      = TD_INVALID_CORE_BUFID;
	old->wr_bufid        = TD_INVALID_WR_BUFID;
	old->rd_bufid        = TD_INVALID_RD_BUFID;

	/* even if there was a deallocation, we have to assume it was done,
	 * since it not safe to retry deallocations */
	old->free_rd_bufid  = TD_INVALID_RD_BUFID;

	/* copy over applicable token flags */
	tok->magic_flags     = old->magic_flags;
	tok->meta_size       = old->meta_size;
	tok->volatile_flags  = old->volatile_flags;

	/* copy over data parameters */
	tok->len_host_to_dev = old->len_host_to_dev;
	tok->len_dev_to_host = old->len_dev_to_host;
	tok->lba_ofs         = old->lba_ofs;
	tok->lba             = old->lba;;
	tok->cmd_seq         = old->cmd_seq;

	/* copy over the checksum, command, and metadata */
	memcpy(tok->data_xsum, old->data_xsum, sizeof(tok->data_xsum));
	memcpy(tok->cmd_bytes, old->cmd_bytes, sizeof(tok->cmd_bytes));
	memcpy(tok->cmd_metadata, old->cmd_metadata, sizeof(tok->cmd_metadata));

	/* copy over the buffer information */
	tok->host.bio        = old->host.bio;
	tok->host.ucmd       = old->host.ucmd;
	tok->host.page       = old->host.page;
	tok->host_buf_virt   = old->host_buf_virt;

	/* buffers are no longer owned by the old token */
	old->host.bio        = NULL;
	old->host.ucmd       = NULL;
	old->host.page       = NULL;
	old->host_buf_virt   = NULL;

	/* copy over reset/retry counters */
	tok->reset_count     = old->reset_count;
	tok->retry_count     = old->retry_count;
	tok->timeout_count   = old->timeout_count;

	/* copy over the ops */
	tok->copy_ops        = old->copy_ops;
	tok->ops             = old->ops;
}

static inline enum td_token_type td_token_type(struct td_token *tok)
{
#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
	return tok->token_type;
#else
	return TD_TOK_FOR_EVERYTHING; /* there is no type */
#endif
}

/** assign a token ops pointer if one was not yet set */
#define td_token_set_default_op(tok,name,func) \
	do { if (!(tok)->ops.name) { \
	     tok->ops.name = (func); \
	   } \
	} while (0)


#endif
