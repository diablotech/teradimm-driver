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

#ifndef _TD_ENG_HAL_H_
#define _TD_ENG_HAL_H_

#include "td_kdefn.h"


#include "td_engine.h"

struct td_device;
struct td_engine;
struct td_token;
struct td_eng_hal_ops;
struct td_ioctl_device_ssd_pt;
struct td_ioctl_device_get_reg;
struct td_ioctl_device_put_reg;
struct list_head;
enum td_token_type;

/**
 * \brief locate an implementation for this device name
 *
 * tdX -> teradimm
 * mdX -> megadimm
 * ... etc
 */
struct td_eng_hal_ops *td_eng_hal_ops_for_name(const char *name);

/**
 * \brief eng device operations
 *
 * These allow for the eng device to be implemented in hardware or
 * simulator.
 */
struct td_eng_hal_ops {

	/* init and cleanup */

	/** prepare hw/sim and allocate any resources */
	int (*_init)(struct td_engine *);
	/** prepare HAL specific config */
	/** verify configuration before enabling access */
	int (*_enable)(struct td_engine *);
	/** Get ready to go online (reload-HAL persistent configuration) */
	int (*_online)(struct td_engine *);
	/** Go offline, if anythign is necessary */
	int (*_offline)(struct td_engine *);
	/** device is down */
	int (*_disable)(struct td_engine *);
	/** cleanup, device going away */
	int (*_exit)(struct td_engine *);

	/* Config get/set */

	int (*_get_conf)(struct td_engine *, uint32_t conf, uint64_t *val);
	int (*_set_conf)(struct td_engine *, uint32_t conf, uint64_t val);

	/* state transitions */

	/** kick off hardware init */
	int (*_hw_init)(struct td_engine *);

	/** initiate firmware handshake mode */
	int (*_fw_handshake)(struct td_engine *);

	/* run time features */

#define TD_ENG_STATUS_GOOD  0
#define TD_ENG_STATUS_STALE 1
	/** read status entries from hw/sim into buffer
	 * return 0 if status is good
	 * return 1 if status is stale
	 * return negative on error */
	int (*_read_status)(struct td_engine *
#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
			, enum td_token_type
#endif
			);

	/** read extended status into token
	 * NOTE: local=0 indicates that access is coming in from another CPU */
	int (*_read_ext_status)(struct td_engine *, int idx, void*, int local);

	/** handle timedout commands */
	void (*_handle_timeouts)(struct td_engine *eng,
				struct list_head *timedout,
				struct list_head *completed);
	int (*_can_retry) (struct td_engine*, struct td_token *);

	/** generate command from token */    /// sim_td_ops_create_cmd(...)
	int (*_create_cmd)(struct td_engine *, struct td_token *);
	/** reverse command polarity (odd/even) */
	int (*_reverse_cmd_polarity)(struct td_engine *, struct td_token *);

#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
	enum td_token_type (*_cmd_to_token_type)(struct td_engine *, const void*);
#endif

	/** start command in hw/sim */
	int (*_start_token)(struct td_engine *, struct td_token *);
	/** reset command in hw/sim */
	int (*_reset_token)(struct td_engine *, struct td_token *);

	/** write page to hw/sim */
	int (*_write_page)(struct td_engine *, struct td_token *);
	/** read page from hw/sim */
	int (*_read_page)(struct td_engine *, struct td_token *);

	/** refresh a write-entry-point */
	int (*_refresh_wep)(struct td_engine *, struct td_token *);

	/** read just the metadata out */
	int (*_read_rdbuf_metadata)(struct td_engine *eng,
			unsigned rdbuf, unsigned ofs, void *, unsigned len);


	struct td_command_generator * _generator;

	int (*_filter)(struct td_engine *eng, uint64_t bytes[8]);
	/* debug */

	/** get a raw buffer for debugging */
	int (*_get_raw_buffer)(struct td_engine *, enum td_buf_type,
			unsigned, void *, unsigned);
	int (*_set_raw_buffer)(struct td_engine *, enum td_buf_type,
			unsigned, void *, unsigned);

	int (*_out_of_order)(struct td_engine*, struct list_head *repeatit,
			uint64_t OoO);

	int (*_trim)(struct td_engine*, struct td_token *);
	/** name of this implementation */
	const char *_name;
};

/** return the name of this implementation */
static inline const char *td_eng_hal_name(struct td_engine *eng)
{
	if (!eng || !eng->ops->_name)
		return NULL;

	return eng->ops->_name;
}

/** prepare hw/sim and allocate any resources */
static inline int td_eng_hal_init(struct td_engine *eng)
{
	if (!eng || !eng->ops->_init)
		return -EFAULT;

	return eng->ops->_init(eng);
}

static inline int td_eng_hal_get_conf (struct td_engine* eng, uint32_t conf,
		uint64_t *val)
{
	if (!eng || !eng->ops->_get_conf)
		return -EFAULT;

	return eng->ops->_get_conf(eng, conf, val);
}

static inline int td_eng_hal_set_conf (struct td_engine* eng, uint32_t conf,
		uint64_t val)
{
	if (!eng || !eng->ops->_set_conf)
		return -EFAULT;

	return eng->ops->_set_conf(eng, conf, val);
}


/** verify configuration before enabling access */
static inline int td_eng_hal_enable(struct td_engine *eng)
{
	if (!eng || !eng->ops->_enable)
		return -EFAULT;

	return eng->ops->_enable(eng);
}

/** reload hw/sim persitent config */
static inline int td_eng_hal_online(struct td_engine *eng)
{
	if (!eng)
		return -EFAULT;

	/* online is optional */
	if (!eng->ops->_online)
		return 0;

	return eng->ops->_online(eng);
}

/** reload hw/sim persitent config */
static inline int td_eng_hal_offline(struct td_engine *eng)
{
	if (!eng)
		return -EFAULT;
	
	/* offline is optional */
	if (!eng->ops->_offline)
		return 0;

	return eng->ops->_offline(eng);
}



/** device about to go down */
static inline int td_eng_hal_disable(struct td_engine *eng)
{
	int rc;

	if (!eng)
		return -EFAULT;

	/* _disable is optional */
	if (!eng->ops->_disable)
		return 0;

	rc = eng->ops->_disable(eng);

	return rc;
}

/** cleanup */
static inline int td_eng_hal_exit(struct td_engine *eng)
{
	int rc;

	if (!eng || !eng->ops || !eng->ops->_exit)
		return -EFAULT;

	rc = eng->ops->_exit(eng);

	return rc;
}

/**
 * initialize hardware
 * @param eng - engine to operate on
 * @return zero if successful
 *
 * This function is called first before any commands are sent.  It is used to
 * kick off any buffer deallocations etc.  Specifics are implemented in the
 * HAL.
 *
 * If the HAL has no work to do for this step, it can skip directly to
 * firmware handshake (see td_eng_hal_fw_handshake()) or jump into the
 * RUNNING state.
 */
static inline int td_eng_hal_hw_init(struct td_engine *eng)
{
	int rc;

	if (!eng || !eng->ops->_hw_init)
		return -EFAULT;

	rc = eng->ops->_hw_init(eng);

	return rc;
}

/** initiate firmware handshake mode
 * @param eng - engine to operate on
 * @return zero if handshake started
 *
 * This function is called to initiate communication with firmware.
 * When firmware probing is completed, the HAL implementation will
 * set the engine run state to RUNNING, on failure it will set it to DEAD.
 *
 * If the HAL has no work to do for this step, it can set the state to
 * RUNNING immediately.
 */
static inline int td_eng_hal_fw_handshake(struct td_engine *eng)
{
	int rc;

	if (!eng || !eng->ops->_fw_handshake)
		return -EFAULT;

	rc = eng->ops->_fw_handshake(eng);

	return rc;
}

/** read status entries from hw/sim into buffer
 * return 0 if status is good
 * return 1 if status is stale
 * return negative on error */
static inline int td_eng_hal_read_status(struct td_engine *eng,
		enum td_token_type tt)
{
	int rc;

	(void)tt;

	if (!eng || !eng->ops->_read_status)
		return -EFAULT;

	rc = eng->ops->_read_status(eng
#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
			, tt
#endif
			);

	return rc;
}

/** read ext_status entries from hw/sim into token */
static inline int td_eng_hal_read_ext_status(struct td_engine *eng, int idx,
		void* dst, int local)
{
	int rc;

	if (!eng || !eng->ops->_read_ext_status)
		return -EFAULT;

	rc = eng->ops->_read_ext_status(eng, idx, dst, local);

	return rc;
}


/** handle timedout commands
 * @param eng         - engine on which timeouts occurred
 * @param timedout    - linked list of timed out commands
 * @param completed   - commands not elegible for recovery are appended here
 *
 * This function is called when the engine has one or more timed out commands
 * that need to be retried.  Depending on hardware, this is reissued on the
 * same command slot or migrated to another slot.  Any tokens that cannot be
 * fixed are enqueued onto the completed list with tok->result set to an error.
 */
static inline void td_eng_hal_handle_timeouts(struct td_engine *eng,
		struct list_head *timedout,
		struct list_head *completed)
{
	if (!eng || !eng->ops->_handle_timeouts)
		return;

	eng->ops->_handle_timeouts(eng, timedout, completed);
}

/** can_retry for a token
 * @param eng         - engine on which timeouts occurred
 * @param token       - token we want to retry
 *
 * This function will return if the engine is allowed to retry
 * this token.  If the HAL has a specific can_retry function, the
 * HAL will be allowed to make this decision.  If the HAL hasn't
 * wired this up, we'll take the normal "RETRIES" count.
 */
static inline int td_eng_hal_can_retry(struct td_engine *eng,
		struct td_token *tok)
{
	if (! eng)
		return -EFAULT;

	if (eng->ops->_can_retry)
		return eng->ops->_can_retry(eng, tok);

	if (++tok->retry_count > (int)td_eng_conf_var_get(eng, RETRIES) )
		return 0;

	return 1;
}

/** generate command from token */
static inline int td_eng_hal_create_cmd(struct td_engine *eng,
		struct td_token *tok)
{
	int rc;

	if (!eng || !eng->ops->_create_cmd)
		return -EFAULT;

	rc = eng->ops->_create_cmd(eng,tok);

	return rc;
}

/** map an existing command to a token type */
static inline int td_eng_hal_cmd_to_token_type(struct td_engine *eng,
		void *cmd)
{
#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
	int rc;

	if (!eng || !eng->ops->_cmd_to_token_type)
		goto error;

	rc = eng->ops->_cmd_to_token_type(eng,cmd);

	return rc;

error:
	/* on error, map everything to the first type (FW) */
	return 0;
#else
	return TD_TOK_SAME_TYPE_FOR_EVERYTHING;
#endif
}


/** reverse command polarity (odd/even) */
static inline int td_eng_hal_reverse_cmd_polarity(struct td_engine *eng,
		struct td_token *tok)
{
	int rc;

	if (!eng || !eng->ops->_reverse_cmd_polarity)
		return -EFAULT;

	rc = eng->ops->_reverse_cmd_polarity(eng,tok);

	return rc;
}

/** start command in hw/sim */
static inline int td_eng_hal_start_token(struct td_engine *eng,
		struct td_token *tok)
{
	int rc;

	if (!eng || !eng->ops->_start_token)
		return -EFAULT;

	rc = eng->ops->_start_token(eng,tok);

	return rc;
}

/** reset command in hw/sim */
static inline int td_eng_hal_reset_token(struct td_engine *eng,
		struct td_token *tok)
{
	int rc;

	if (!eng || !eng->ops->_reset_token)
		return -EFAULT;

	rc = eng->ops->_reset_token(eng,tok);

	return rc;
}

/** write page to hw/sim */
static inline int td_eng_hal_write_page(struct td_engine *eng,
		struct td_token *tok)
{
	int rc;

	if (!eng || !eng->ops->_write_page)
		return -EFAULT;

	rc = eng->ops->_write_page(eng,tok);

	return rc;
}

/** read page from hw/sim */
static inline int td_eng_hal_read_page(struct td_engine *eng,
		struct td_token *tok)
{
	int rc;

	if (!eng || !eng->ops->_read_page)
		return -EFAULT;

	rc = eng->ops->_read_page(eng,tok);

	return rc;
}

/** refresh a write-entry-point */
static inline int td_eng_hal_refresh_wep(struct td_engine *eng,
		struct td_token *tok)
{
	int rc;

	if (!eng || !eng->ops->_refresh_wep)
		return -EFAULT;

	rc = eng->ops->_refresh_wep(eng,tok);

	return rc;
}

/** read just the metadata out */
static inline int td_eng_hal_read_rdbuf_metadata(struct td_engine *eng,
		unsigned rdbuf, unsigned off, void *data, unsigned len)
{
	int rc;

	if (!eng || !eng->ops->_read_rdbuf_metadata)
		return -EFAULT;

	rc = eng->ops->_read_rdbuf_metadata(eng,rdbuf,off,data,len);

	return rc;
}


/** Deal with out of order return code. */
static inline int td_eng_hal_out_of_order(struct td_engine *eng,
		struct list_head *repeatit, uint64_t OoO)
{
	int rc;
	if (!eng || !eng->ops->_out_of_order)
		return -EFAULT;

	rc = eng->ops->_out_of_order(eng, repeatit, OoO);
	return rc;
}


static inline int td_eng_hal_trim(struct td_engine *eng,
		struct td_token *tok)
{
	int rc;
	if (!eng || !eng->ops->_trim)
		return -EFAULT;

	rc = eng->ops->_trim(eng, tok);
	return rc;
}

/** get a raw buffer for debugging
 * returns number of bytes copied, or negative error */
static inline int td_eng_hal_get_raw_buffer(struct td_engine *eng,
		enum td_buf_type type, unsigned index,
		void *buf, unsigned buf_len)
{
	int rc;

	if (!eng || !eng->ops->_get_raw_buffer)
		return -EFAULT;

	rc = eng->ops->_get_raw_buffer(eng,type,index,buf,buf_len);

	return rc;
}

/** get a raw buffer for debugging
 * returns number of bytes copied, or negative error */
static inline int td_eng_hal_set_raw_buffer(struct td_engine *eng,
		enum td_buf_type type, unsigned index,
		void *buf, unsigned buf_len)
{
	int rc;

	if (!eng || !eng->ops->_set_raw_buffer)
		return -EFAULT;

	rc = eng->ops->_set_raw_buffer(eng,type,index,buf,buf_len);

	return rc;
}


#endif
