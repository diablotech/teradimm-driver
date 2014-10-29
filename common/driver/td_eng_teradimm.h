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

#ifndef _TD_TERADIMM_H_
#define _TD_TERADIMM_H_

#include "td_kdefn.h"

#include <linux/types.h>

#include "td_limits.h"
#include "td_eng_hal.h"
#include "td_params.h"
#include "td_memspace.h"
#include "td_protocol.h"
#include "td_util.h"

struct td_device;
struct td_engine;
struct td_token;

struct td_core_buf {
	uint8_t         data[TERADIMM_DATA_BUF_SIZE];
	uint8_t         meta[TERADIMM_META_BUF_SIZE];
};

#ifdef CONFIG_TERADIMM_FWSTATUS_HISTORY
struct td_fwstatus_debug_layout {
	uint64_t fw_fwstatus_generation;
	uint32_t fw_cmd_isr_ts;
	uint32_t fw_ssd0_cmd_ts;
	uint32_t fw_ssd1_cmd_ts;
	uint32_t fw_other_cmd_ts;
	uint32_t fw_cmdtask_heartbeet;
};
struct td_fwstatus_hist {
	cycles_t ts_gpio;
	cycles_t ts_status;
	uint32_t polls;
	uint32_t retries;
	struct td_fwstatus_debug_layout fw_debug;
};
#endif

#define TERADIMM_SAFE_CONF_CLFLUSH    (TD_FLUSH_STATUS_CLF_PRE | TD_FLUSH_RDBUF_CLF_PRE)
#define TERADIMM_DEFUALT_CONF_CLFLUSH (TD_FLUSH_STATUS_CLF_PRE | TD_FLUSH_RDBUF_NTF_PRE)

struct td_eng_teradimm {

	/** pointer back to the device */
	struct td_engine        *td_engine;

	/* TeraDIMM memory mapper */
	struct td_mapper        *td_mapper;

	/* sequence number of last command generated */
	atomic_t cmd_seq;

	/* teradimm aliasing */
	uint next_read_page_alias[TD_HOST_RD_BUFS_PER_DEV];
	uint next_status_alias;

	/* hold onto reserved tokens while enabled */
	struct td_token_list    td_reserved_tokens;

	uint64_t td_ssd_count;
	uint64_t td_ssd_sector_count;

#ifdef CONFIG_TERADIMM_MCEFREE_STATUS_POLLING
	struct {
		uint32_t count_request_posted;
		uint32_t count_updates_completed;
		uint32_t count_sema_poll;
		uint32_t count_retries;

		uint64_t last_sema_value;

		//cycles_t last_request_posted; // now use eng->td_last_fwstatus_request_posted
		cycles_t last_updates_completed;
		cycles_t last_sema_poll;

#ifdef CONFIG_TERADIMM_TOKEN_HISTORY
		struct td_token *last_gpio_tok;
#endif

		/* 64 bytes to hold the last bytes in read buffer */
		uint64_t scratch[8] __aligned64;
	} mcefree;
#endif

#ifdef CONFIG_TERADIMM_FWSTATUS_HISTORY
	struct td_fwstatus_hist fwstatus_hist[CONFIG_TERADIMM_FWSTATUS_HISTORY];
	unsigned fwstatus_hist_now;
#endif

	/* Timestamp of our last status update, in case we were starved */
	union {
		uint64_t                u64;
		struct {
			uint16_t        bad_xsum;
			uint16_t        wep_timeout;
			uint16_t        other;
			uint16_t        extra;
		};
	} td_tok_retries[TD_TOKENS_PER_DEV];

	uint64_t    td_status_ts[TD_TOK_TYPE_MAX];

	struct {
		int step;
		int slot;
		uint8_t poll_gstatus;
		uint8_t poll_mask;
		uint8_t gstatus;
		uint8_t gstatus_2x_count;
	} hw_init;

	struct {
		struct {
			unsigned count;
			struct {
				struct td_token *tok;
				uint64_t word[TD_CMD_WORD_LEN];
			} cmd[256];
		} held;
		cycles_t next_sema_poll;
	} ei;
};

static inline void td_fwstatus_start(struct td_eng_teradimm *td)
{
#ifdef CONFIG_TERADIMM_FWSTATUS_HISTORY
	struct td_fwstatus_hist *hist;

	hist = td->fwstatus_hist + td->fwstatus_hist_now;

	if (hist->ts_gpio)
		return;

	hist->ts_gpio = td_get_cycles();
#endif
}

struct td_fwstatus_debug_layout;
static inline void td_fwstatus_complete(struct td_eng_teradimm *td,
		uint64_t polls, uint64_t retries,
		struct td_fwstatus_debug_layout *fw_debug)
{
#ifdef CONFIG_TERADIMM_FWSTATUS_HISTORY
	struct td_fwstatus_hist *hist;

	hist = td->fwstatus_hist + td->fwstatus_hist_now;

	hist->ts_status           = td_get_cycles();
	hist->polls               = polls   > 0xFFFFFFFULL ? 0xFFFFFFF : polls;
	hist->retries             = retries > 0xFFFFFFFULL ? 0xFFFFFFF : retries;

	memcpy(&hist->fw_debug, fw_debug, sizeof(hist->fw_debug));

	td->fwstatus_hist_now ++;
	td->fwstatus_hist_now %= CONFIG_TERADIMM_FWSTATUS_HISTORY;

	hist = td->fwstatus_hist + td->fwstatus_hist_now;
	hist->ts_gpio = 0;
#endif
}


extern void td_fwstatus_dump(struct td_engine *eng);


static inline struct td_eng_teradimm *td_eng_td_hal(struct td_engine *eng)
{
	return (struct td_eng_teradimm *)eng->ops_priv;
}

extern struct td_eng_hal_ops td_eng_teradimm_ops;

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
extern void teradimm_force_read_real_status(struct td_engine *eng, uint tokid, uint8_t *byte);
extern void teradimm_force_read_all_real_status(struct td_engine *eng, uint8_t *bytes);
#endif

#endif
