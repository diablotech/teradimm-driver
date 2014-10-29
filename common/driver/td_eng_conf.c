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

#include "td_eng_conf.h"
#include "td_engine.h"
#include "td_device.h"

MODULE_PARAM(uint, td_spy_ucmd, 0)
module_param_named(spy_ucmd, td_spy_ucmd, uint, 0644);
MODULE_PARM_DESC(spy_ucmd, "Trace UCMDs as they go through to a device");

MODULE_PARAM(uint, td_bio_max_bytes, TERADIMM_DATA_BUF_SIZE)
module_param_named(max_bio_size, td_bio_max_bytes, uint, 0644);
MODULE_PARM_DESC(max_bio_size, "Maximum size of BIOs to accept from the OS");

#  define TD_CONF_ENTRY(what, when, min, max) \
	[TD_CONF_##what] = { check_##when, min, max },
#  define TD_CONF_HW_ENTRY(what, when, min, max) \
	[TD_CONF_HW_##what] = { check_##when, min, max },
#  define TD_CONF_MCEFREE_ENTRY(what, when, min, max) \
	[TD_CONF_MCEFREE_##what] = { check_##when, min, max },

#define TD_TIMEOUT_ENTRY(what) \
	{ TD_TIMEOUT_##what, \
		TD_CONF_BIO_R_##what##_TIMEOUT_USEC, \
		TD_CONF_BIO_W_##what##_TIMEOUT_USEC, \
		TD_CONF_##what##_TIMEOUT_USEC },

static int check_always(struct td_engine *eng, uint32_t u1, uint64_t u2) {
	return 0;
}

static int check_inactive(struct td_engine *eng, uint32_t u1, uint64_t u2) {
	if (td_run_state_check(eng, RUNNING))
		return -EBUSY;
	return 0;
}

static int check_offline(struct td_engine *eng, uint32_t u1, uint64_t u2) {
	if (td_device_check_state(td_engine_device(eng), ONLINE))
		return -EBUSY;
	return 0;
}


static int check_flush(struct td_engine *eng, uint32_t c, uint64_t value)
{
	if (c != TD_CONF_CLFLUSH)
		return -ENODEV;

	if (td_conf_validate_cache(eng, value) ) {
		td_eng_err(eng,  "Incompatible CFLUSH setting; ignored.\n");
		return 1;
	}

	return 0;
}

/* Offline is currently not used.  commented out to remove compile warning.
 *
static int check_offline(struct td_engine *eng) {
	if (!td_device_check_state(td_engine_device(eng), OFFLINE))
		return -EBUSY;
	return 0;
}
*/
#define check_NULL NULL

/* WINDOWS NEEDS THESE IN ORDER OF ENUMS IN td_defs.h */
const struct td_conf_var_desc td_eng_conf_var_desc[TD_CONF_REGS_MAX] = {
	TD_CONF_ENTRY(TOKENS,                      inactive,  0,  TD_TOKENS_PER_DEV-1)

	TD_CONF_ENTRY(HOST_READ_BUFS,              inactive,  0,  TD_HOST_RD_BUFS_PER_DEV-1)
	TD_CONF_ENTRY(HOST_WRITE_BUFS,             inactive,  0,  TD_HOST_WR_BUFS_PER_DEV-1)
	TD_CONF_ENTRY(CORE_BUFS,                   inactive,  0,  TD_CORE_BUFS_PER_DEV-1)
	TD_CONF_ENTRY(HW_CMDS,                     NULL,      0,  0)

	TD_CONF_ENTRY(HOST_READ_ALIASES,           inactive,  0,  TD_MAX_ALLOWED_ALIASES)
	TD_CONF_ENTRY(HOST_STATUS_ALIASES,         inactive,  0,  TD_MAX_ALLOWED_ALIASES)

	TD_CONF_ENTRY(WRITE_WAIT_USEC,             always,    0,  UINT_MAX)

	TD_CONF_ENTRY(QUICK_WRBUF,                 always,    0,  UINT_MAX)
	TD_CONF_ENTRY(QUICK_TOK_REUSE,             always,    0,  UINT_MAX)

	TD_CONF_ENTRY(HALT_ON_TIMEOUT,             always,    0,  UINT_MAX)
	TD_CONF_ENTRY(VERBOSE_TOK_FAILURES,        always,    0,  UINT_MAX)
	TD_CONF_ENTRY(RETRY_TIMEDOUT_DEALLOCATES,  always,    0,  UINT_MAX)

	TD_CONF_ENTRY(ORPHAN_RDBUF_REQ_CNT,        always,    1,  UINT_MAX)
	TD_CONF_ENTRY(ORPHAN_RDBUF_TIMEOUT_CNT,    always,    1,  UINT_MAX)
	TD_CONF_ENTRY(ORPHAN_RDBUF_REQ_MSEC,       always,    1,  UINT_MAX)
	TD_CONF_ENTRY(ORPHAN_RDBUF_ORDER_CHECK,    always,    0,  1)

	TD_CONF_ENTRY(EARLY_COMMIT,                always,    TD_FULL_COMMIT,  TD_TRIPLE_SEC)
	TD_CONF_ENTRY(EARLY_DEALLOCATE,            always,    0,  UINT_MAX)
	TD_CONF_ENTRY(EARLY_START,                 always,    0,  UINT_MAX)

	TD_CONF_ENTRY(MAX_START_BURST,             always,    1,  UINT_MAX)

	TD_CONF_ENTRY(EARLY_SPLIT_REQ,             always,    0,  UINT_MAX)
	TD_CONF_ENTRY(COLLISION_CHECK,             always,    0,  3)

	TD_CONF_ENTRY(BIO_R_START_TIMEOUT_USEC,    always,    1000,  UINT_MAX)
	TD_CONF_ENTRY(BIO_R_OOO_TIMEOUT_USEC,      always,    0,     UINT_MAX)
	TD_CONF_ENTRY(BIO_R_QUEUED_TIMEOUT_USEC,   always,    1000,  UINT_MAX)
	TD_CONF_ENTRY(BIO_R_RECEIVED_TIMEOUT_USEC, always,    1000,  UINT_MAX)
	TD_CONF_ENTRY(BIO_R_WAIT_SSD_TIMEOUT_USEC, always,    1000,  UINT_MAX)

	TD_CONF_ENTRY(BIO_W_START_TIMEOUT_USEC,    always,    1000,  UINT_MAX)
	TD_CONF_ENTRY(BIO_W_OOO_TIMEOUT_USEC,      always,    0,     UINT_MAX)
	TD_CONF_ENTRY(BIO_W_QUEUED_TIMEOUT_USEC,   always,    1000,  UINT_MAX)
	TD_CONF_ENTRY(BIO_W_RECEIVED_TIMEOUT_USEC, always,    1000,  UINT_MAX)
	TD_CONF_ENTRY(BIO_W_WAIT_SSD_TIMEOUT_USEC, always,    1000,  UINT_MAX)

	TD_CONF_ENTRY(START_TIMEOUT_USEC,          always,    1000,  UINT_MAX)
	TD_CONF_ENTRY(OOO_TIMEOUT_USEC,            always,    1000,  UINT_MAX)
	TD_CONF_ENTRY(QUEUED_TIMEOUT_USEC,         always,    1000,  UINT_MAX)
	TD_CONF_ENTRY(RECEIVED_TIMEOUT_USEC,       always,    1000,  UINT_MAX)
	TD_CONF_ENTRY(WAIT_SSD_TIMEOUT_USEC,       always,    1000000,  UINT_MAX)

	TD_CONF_ENTRY(STUCK_TIMEOUT_USEC,          always,    10* 1000000,  UINT_MAX)

	TD_CONF_ENTRY(TIMEOUT_RETRY,               always,    0,  UINT_MAX)
	TD_CONF_ENTRY(RETRIES,                     always,    0,  UINT_MAX)
	TD_CONF_ENTRY(XSUM_ERR_RETRIES,            always,    0,  UINT_MAX)
	TD_CONF_ENTRY(WEP_TIMEOUT_RETRIES,         always,    0,  UINT_MAX)

	TD_CONF_ENTRY(RESETS,                      always,    1,  UINT_MAX)

	TD_CONF_ENTRY(CORE_ONLY,                   always,    0,  UINT_MAX)
	TD_CONF_ENTRY(MAGIC_FLAGS,                 always,    0,  UINT_MAX)
	TD_CONF_ENTRY(INDEPENDENT_DEALLOCATE,      always,    0,  UINT_MAX)

	TD_CONF_ENTRY(CLFLUSH,                     flush,     0,  UINT_MAX)
	TD_CONF_ENTRY(WBINVD,                      always,    0,  UINT_MAX)

	TD_CONF_ENTRY(TARGET_IOPS,                 always,    0,  UINT_MAX)
	TD_CONF_ENTRY(IOPS_SAMPLE_MSEC,            always,    0,  UINT_MAX)

	TD_CONF_ENTRY(DELAY_POST_WRBUF_USEC,       always,    0,  UINT_MAX)
	TD_CONF_ENTRY(DELAY_POST_CMD_USEC,         always,    0,  UINT_MAX)
	TD_CONF_ENTRY(DELAY_WRITE_TO_READ_USEC,    always,    0,  UINT_MAX)

	TD_CONF_ENTRY(HOLD_TIMEDOUT_TOKENS,        always,    0,  1)

	TD_CONF_ENTRY(TIMEDOUT_CNT_THRESHOLD,      always,    0,  UINT_MAX)

	TD_CONF_ENTRY(SHUTDOWN,                    always,    0,  1)

	TD_CONF_ENTRY(HW_TOKID_START,              NULL,      0,  0)
	TD_CONF_ENTRY(RESERVED_TOKID_START,        NULL,      0,  0)

	TD_CONF_ENTRY(HW_TOKID_START,              NULL,      0,  0)
	TD_CONF_ENTRY(RESERVED_TOKID_START,        NULL,      0,  0)

	TD_CONF_ENTRY(MCEFREE_FWSTATUS,            NULL,      0,  1)

	TD_CONF_ENTRY(SPY_UCMD,                    always,    0,  2)

	TD_CONF_ENTRY(DOUBLE_WEP_WRITES,           always,    0,  2)

	TD_CONF_ENTRY(BIO_MAX_BYTES,               offline,   TERADIMM_DATA_BUF_SIZE,  TERADIMM_DATA_BUF_SIZE * 128)

	TD_CONF_ENTRY(FW_SEQ_WINDOW,               always,    128,  0x8000)

	TD_CONF_ENTRY(OOO_REPLAY,                  always,    0,  TD_OOO_REPLAY_MAX-1)

	TD_CONF_ENTRY(FW_CMD_WRITE_MODE,           always,    0,  TD_CMD_WRITE_MODE_MAX-1)
	TD_CONF_ENTRY(HW_CMD_WRITE_MODE,           always,    0,  TD_CMD_WRITE_MODE_MAX-1)

	TD_CONF_ENTRY(LOST_CMD_REFRESH_USEC,       always,    0,  UINT_MAX)
	TD_CONF_ENTRY(LOST_CMD_REFRESH_MAX,        always,    0,  100)

	TD_CONF_ENTRY(NOUPDATE_CMD_LIMIT,          always,    0,  TD_TOKENS_PER_DEV)

	TD_CONF_ENTRY(INCOMING_SLEEP,              always,    0,  10000)
	TD_CONF_ENTRY(INCOMING_WAKE,               always,    0,  10000)
};

/* WINDOWS NEEDS THESE IN ORDER OF ENUMS IN td_defs.h */
const struct td_conf_var_desc td_eng_conf_hw_var_desc[TD_CONF_HW_REGS_MAX] = {
	TD_CONF_HW_ENTRY(SSD_COUNT,                 NULL,      0, 0)
	TD_CONF_HW_ENTRY(SSD_STRIPE_LBAS,           NULL,      0, 0)
	TD_CONF_HW_ENTRY(SSD_SECTOR_COUNT,          NULL,      0, 0)
	TD_CONF_HW_ENTRY(HW_SECTOR_METADATA,        NULL,      0, 0)
	TD_CONF_HW_ENTRY(HW_SECTOR_SIZE,            inactive,  512, 4096)
	TD_CONF_HW_ENTRY(HW_SECTOR_ALIGN,           NULL,      0, 0)
	TD_CONF_HW_ENTRY(BIO_SECTOR_SIZE,           inactive,  512, 4096)
	TD_CONF_HW_ENTRY(E2E_MODE,                  NULL,      0, 0)
	TD_CONF_HW_ENTRY(SPD,                       NULL,      0, 0)
	TD_CONF_HW_ENTRY(DISCARD,                   NULL,      0, 1)
	TD_CONF_HW_ENTRY(MCEFREE_SUPPORT,           NULL,      0, 2)
	TD_CONF_HW_ENTRY(RAID_PAGE,                 NULL,      0, 0)
};

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
/* WINDOWS NEEDS THESE IN ORDER OF ENUMS IN td_defs.h */
const struct td_conf_var_desc td_eng_conf_mcefree_var_desc[TD_CONF_MCEFREE_REGS_MAX] = {
	TD_CONF_MCEFREE_ENTRY(STATUS_R2R_NSEC,       always,  1000,  UINT_MAX)
	TD_CONF_MCEFREE_ENTRY(STATUS_R2P_NSEC,       always,  1000,  UINT_MAX)
	TD_CONF_MCEFREE_ENTRY(STATUS_P2P_NSEC,       always,  1000,  UINT_MAX)
	TD_CONF_MCEFREE_ENTRY(STATUS_R2P_TIMEOUT_NSEC,always, 1000,  UINT_MAX)
	TD_CONF_MCEFREE_ENTRY(STATUS_R2P_RETRY_MAX,  always,  1,     UINT_MAX)
	TD_CONF_MCEFREE_ENTRY(HALT_ON_STATUS_TIMEOUT,always,  0,     1)

	TD_CONF_MCEFREE_ENTRY(PROGRESS_TIMEOUT_USEC, always,  1000,  UINT_MAX)
	TD_CONF_MCEFREE_ENTRY(TOKEN_TIMEOUT_USEC,    always,  1000,  UINT_MAX)
	TD_CONF_MCEFREE_ENTRY(STATUS_HOLD_NSEC,      always,  0,     100000)

	TD_CONF_MCEFREE_ENTRY(RDBUF_RETRY_NSEC,      always,  0,     100000)  // 0ns..100us

	TD_CONF_MCEFREE_ENTRY(RDBUF_HOLD_NSEC,       always,  1,     1000000) // 1ns..1ms
	TD_CONF_MCEFREE_ENTRY(RDBUF_HOLD_MIN_NSEC,   always,  1,     10000)   // 1ns..10us
	TD_CONF_MCEFREE_ENTRY(RDBUF_HOLD_MAX_NSEC,   always,  1000,  1000000) // 1us..1ms
	TD_CONF_MCEFREE_ENTRY(RDBUF_HOLD_CLIMB_NSEC, always,  1,     100000)  // 1ns..100us
	TD_CONF_MCEFREE_ENTRY(RDBUF_HOLD_DROP_NSEC,  always,  1,     100000)  // 1ns..100us
	TD_CONF_MCEFREE_ENTRY(RDBUF_HOLD_COOL_MSEC,  always,  0,     100000)  // 0ns..100s

	TD_CONF_MCEFREE_ENTRY(DEALLOC_HOLD_NSEC,       always, 0,    1000000) // 0ns..1ms
	TD_CONF_MCEFREE_ENTRY(POST_DEALLOC_DELAY_NSEC, always, 0,    1000000) // 0ns..1ms
	TD_CONF_MCEFREE_ENTRY(WRITE_HOLD_NSEC,         always, 0,    1000000) // 0ns..1ms

	TD_CONF_MCEFREE_ENTRY(JIT_DEALLOCATES,         always, 0,    1)
	TD_CONF_MCEFREE_ENTRY(NO_WRITE_WHILE_FWSTATUS, always, 0,    1)

	TD_CONF_MCEFREE_ENTRY(FWSTATUS_HOLD_NSEC,      always, 0,    100000)

	TD_CONF_MCEFREE_ENTRY(STATUS_R2P_MIN_NSEC,     always, 1,      5000)
	TD_CONF_MCEFREE_ENTRY(STATUS_R2P_MAX_NSEC,     always, 1000,  25000)
	TD_CONF_MCEFREE_ENTRY(STATUS_R2P_CLIMB_NSEC,   always, 1,      1000)
	TD_CONF_MCEFREE_ENTRY(STATUS_R2P_DROP_NSEC,    always, 1,        10)
	TD_CONF_MCEFREE_ENTRY(STATUS_R2P_COOL_MSEC,    always, 0,        40)
};
#endif

const struct td_timeout_desc td_timeout_lookup[TD_TIMEOUT_MAX] = {
	TD_TIMEOUT_ENTRY(OOO)
	TD_TIMEOUT_ENTRY(QUEUED)
	TD_TIMEOUT_ENTRY(RECEIVED)
	TD_TIMEOUT_ENTRY(WAIT_SSD)
};

#include "td_kdefn.h"

#include <linux/module.h>

#include <linux/moduleparam.h>

static uint td_sector_size = TERADIMM_DATA_BUF_SIZE;
module_param_named(sector_size, td_sector_size, uint, 0444);
MODULE_PARM_DESC(sector_size,
		"Sector size presented to block layer (512,1024,2048,4096).");

static inline uint get_default_logical_sector_size(struct td_eng_conf *conf)
{
	uint ret = td_sector_size;
	switch(ret) {
	case 512:
	case 1024:
	case 2048:
	case 4096:
		return ret;
	default:
		td_sector_size = TERADIMM_DATA_BUF_SIZE;
		return TERADIMM_DATA_BUF_SIZE;
	}
}

void td_eng_conf_init(struct td_engine *eng, struct td_eng_conf *conf)
{
	/* setup defaults */
	td_eng_conf_var_set(eng, TOKENS, TD_TOKENS_PER_DEV);

	td_eng_conf_var_set(eng, HOST_READ_BUFS, TD_HOST_RD_BUFS_PER_DEV);
	td_eng_conf_var_set(eng, HOST_WRITE_BUFS, TD_HOST_WR_BUFS_PER_DEV);
	td_eng_conf_var_set(eng, CORE_BUFS, TD_CORE_BUFS_PER_DEV);

	td_eng_conf_var_set(eng, WRITE_WAIT_USEC, 75);    /* driver waits for burst of 4 writes */

	td_eng_conf_var_set(eng, QUICK_WRBUF, 0);         /* release of write buffer after token starts */
	td_eng_conf_var_set(eng, QUICK_TOK_REUSE, 0);     /* use a queue for free token list */
	td_eng_conf_var_set(eng, HALT_ON_TIMEOUT, 0);     /* don't terminate on timeout by default */
	td_eng_conf_var_set(eng, VERBOSE_TOK_FAILURES, 0);/* don't spam when tokens fail */
	td_eng_conf_var_set(eng, EARLY_COMMIT, TD_FULL_COMMIT); /* don't release bios early by default */
	td_eng_conf_var_set(eng, EARLY_DEALLOCATE, 1);    /* send deallocates from completion loop */
	td_eng_conf_var_set(eng, EARLY_START, 4);         /* send new commands from completion loop */

	td_eng_conf_var_set(eng, MAX_START_BURST, 8);     /* number of commands to burst in */

	td_eng_conf_var_set(eng, BIO_R_START_TIMEOUT_USEC, 10);       /* after 10 us waiting to LEAVE start state, the token times out */
	td_eng_conf_var_set(eng, BIO_R_OOO_TIMEOUT_USEC, 1);       /* after 1 us waiting to LEAVE OOO state, the token times out */
	td_eng_conf_var_set(eng, BIO_R_QUEUED_TIMEOUT_USEC, 90);      /* after 90 us waiting to LEAVE queued state, the token times out */
	td_eng_conf_var_set(eng, BIO_R_RECEIVED_TIMEOUT_USEC, 900);   /* after 9 ms waiting to LEAVE received state, the token times out */
	td_eng_conf_var_set(eng, BIO_R_WAIT_SSD_TIMEOUT_USEC, 99000); /* after 99 ms waiting to LEAVE wait_ssd state, the token times out */

	td_eng_conf_var_set(eng, BIO_W_START_TIMEOUT_USEC, 10);       /* after 10 us waiting to LEAVE start state, the token times out */
	td_eng_conf_var_set(eng, BIO_W_OOO_TIMEOUT_USEC, 1);       /* after 1 us waiting to LEAVE OOO state, the token times out */
	td_eng_conf_var_set(eng, BIO_W_QUEUED_TIMEOUT_USEC, 90);      /* after 90 us waiting to LEAVE queued state, the token times out */
	td_eng_conf_var_set(eng, BIO_W_RECEIVED_TIMEOUT_USEC, 900);   /* after 9 ms waiting to LEAVE received state, the token times out */
	td_eng_conf_var_set(eng, BIO_W_WAIT_SSD_TIMEOUT_USEC, 99000); /* after 99 ms waiting to LEAVE wait_ssd state, the token times out */

	td_eng_conf_var_set(eng, START_TIMEOUT_USEC, 10);             /* after 10 us waiting to LEAVE start state, the token times out */
	td_eng_conf_var_set(eng, OOO_TIMEOUT_USEC, 90);               /* after 1 us waiting to LEAVE OOO state, the token times out */
	td_eng_conf_var_set(eng, QUEUED_TIMEOUT_USEC, 90);            /* after 90 us waiting to LEAVE queued state, the token times out */
	td_eng_conf_var_set(eng, RECEIVED_TIMEOUT_USEC, 900);         /* after 9 ms waiting to LEAVE received state, the token times out */
	td_eng_conf_var_set(eng, WAIT_SSD_TIMEOUT_USEC, 99000);       /* after 99 ms waiting to LEAVE wait_ssd state, the token times out */
	td_eng_conf_var_set(eng, STUCK_TIMEOUT_USEC, 100000);         /* after 100 ms a running token is considered stuck.*/

	td_eng_conf_var_set(eng, TIMEOUT_RETRY, 1);       /* timeouts get retries */

	td_eng_conf_var_set(eng, RETRIES, 1);                         /* 1 retry */
	td_eng_conf_var_set(eng, WEP_TIMEOUT_RETRIES, 4);             /* timeouts get retries */
	td_eng_conf_var_set(eng, XSUM_ERR_RETRIES, 4);                /* checksum failures get retried */

	td_eng_conf_var_set(eng, RESETS, 1);              /* 1 reset */
	td_eng_conf_var_set(eng, HOLD_TIMEDOUT_TOKENS, 0); /* disable holding timedout tokens */

	td_eng_conf_var_set(eng, EARLY_SPLIT_REQ, 0);     /* default, split requests in driver thread */
	td_eng_conf_var_set(eng, COLLISION_CHECK, 1);     /* don't start new requests while RMW active on the LBA */

	td_eng_conf_var_set(eng, RETRY_TIMEDOUT_DEALLOCATES, 0);    /* don't reissue timedout deallocates */
	td_eng_conf_var_set(eng, ORPHAN_RDBUF_REQ_CNT, 2);          /**< number of reads observed to qualify for orphan status */
	td_eng_conf_var_set(eng, ORPHAN_RDBUF_TIMEOUT_CNT, 20);     /**< number of reads timeouts to qualify for orphan status */
	td_eng_conf_var_set(eng, ORPHAN_RDBUF_REQ_MSEC, 100);       /**< elapsed time required to qualify for orphan status */
	td_eng_conf_var_set(eng, ORPHAN_RDBUF_ORDER_CHECK, 1);      /**< used on MegaDIMM to enforce low-rdbuf first order */

	td_eng_conf_var_set(eng, MAGIC_FLAGS, 0);                   /* 0 == normal operation */

#ifdef CONFIG_TERADIMM_USES_CORE_BUFFS_ONLY
	td_eng_conf_var_set(eng, CORE_ONLY, 0);                     /* 0 == use LBA addresses */
#endif
	td_eng_conf_var_set(eng, INDEPENDENT_DEALLOCATE, 0);        /* 0 means use fast deallocates with other commands */

	td_eng_conf_var_set(eng, TARGET_IOPS, 2000000);             /* after N IOPS call schedule() */
	td_eng_conf_var_set(eng, IOPS_SAMPLE_MSEC, 100);            /* frequency for updating eng->td_iops */

	td_eng_conf_var_set(eng, CLFLUSH, TD_FLUSH_STATUS_CLF_PRE); /*  clflush before start read */
	td_eng_conf_var_set(eng, WBINVD, TD_WBINVD_READ_ALIAS);     /*  wbinvd when read aliases run out */

	td_eng_conf_var_set(eng, HOST_READ_ALIASES, (1 << 10));     /*  10 ignore bits */
	td_eng_conf_var_set(eng, HOST_STATUS_ALIASES, (1 << 14));   /*  14 ignore bits */

	td_eng_conf_var_set(eng, SHUTDOWN, 0);                        /* don't issued shutdown by default */

	td_eng_conf_var_set(eng, SPY_UCMD, td_spy_ucmd);            /* spy on ucmds*/
	td_eng_conf_var_set(eng, DOUBLE_WEP_WRITES, 0);             /* double WEP writes */

	td_eng_conf_hw_var_set(eng, E2E_MODE, TD_E2E_MODE_OFF);                      /* E2E protection disabled */
	td_eng_conf_hw_var_set(eng, HW_SECTOR_SIZE, 512);                   /* what size the hardware operates in */
	td_eng_conf_hw_var_set(eng, BIO_SECTOR_SIZE, get_default_logical_sector_size(conf)); /* what size we present to the block layer */
	td_eng_conf_hw_var_set(eng, SSD_STRIPE_LBAS, 8);
	td_eng_conf_hw_var_set(eng, SSD_SECTOR_COUNT, 0);

	td_eng_conf_hw_var_set(eng, VER_MAJOR, (unsigned)'D' << 8);
	td_eng_conf_hw_var_set(eng, VER_MINOR, 0);
	td_eng_conf_hw_var_set(eng, VER_PATCH, 0);
	td_eng_conf_hw_var_set(eng, VER_BUILD, 0);

	td_eng_conf_hw_var_set(eng, SPD, 0);
	td_eng_conf_hw_var_set(eng, DISCARD, 0);                        /* support discard*/

	td_eng_conf_hw_var_set(eng, RAID_PAGE, 0);                      /* Not unless they say so */

	td_eng_conf_var_set(eng, BIO_MAX_BYTES, td_bio_max_bytes);      /* OS max bio size */

	td_eng_conf_var_set(eng, FW_SEQ_WINDOW, 0x7000);

	td_eng_conf_var_set(eng, OOO_REPLAY, TD_OOO_REPLAY_QUICK);       /* OoO replay enabled */

	td_eng_conf_var_set(eng, FW_CMD_WRITE_MODE, TD_CMD_WRITE_FENCED_MOVNTI);
	td_eng_conf_var_set(eng, HW_CMD_WRITE_MODE, TD_CMD_WRITE_FENCED_MOVNTI);

	td_eng_conf_var_set(eng, LOST_CMD_REFRESH_USEC, 1000);
	td_eng_conf_var_set(eng, LOST_CMD_REFRESH_MAX, 1);

	td_eng_conf_var_set(eng, NOUPDATE_CMD_LIMIT, 32);

#if CONFIG_TERADIMM_INCOMING_BACKPRESSURE != TD_BACKPRESSURE_NONE
	td_eng_conf_var_set(eng, INCOMING_SLEEP, 1000);
#endif
#if CONFIG_TERADIMM_INCOMING_BACKPRESSURE == TD_BACKPRESSURE_EVENT
	td_eng_conf_var_set(eng, INCOMING_WAKE, 250);
#endif
}


void td_eng_conf_sim_init(struct td_eng_conf_sim *msconf)
{
	msconf->channels = min(  4, TD_CHANS_PER_DEV);
	msconf->luns     = min(  8, TD_LUNS_PER_CHAN);
	msconf->lunbuses = min(  4, TD_LUNBUSES_PER_CHAN);

	msconf->ssd_read_bufs     = TD_SSD_RD_BUFS_PER_CHAN;
	msconf->ssd_write_bufs    = TD_SSD_WR_BUFS_PER_CHAN;

	msconf->horizon_usec      = 500;   /* when to drop interval data in simulator */

	msconf->host_core_xfer_nsec = 350;   /* external to internal copy */
	msconf->core_ssd_xfer_nsec  = 1280;  /* assuming 3.2 GB/s on the core-SSD bus */
	msconf->ssd_lun_xfer_nsec   = 20000; /* transfer over the LUN bus */

	msconf->flash_read_nsec     = 85000; /* flash delay for reads */
	msconf->flash_write_nsec    = 300000;/* flash delay for writes */

	msconf->proc_cmd_nsec       = 2000;  /* microproc starting command */
	msconf->proc_stat_nsec      = 1000;  /* microproc updating status */

}


int td_conf_validate_cache (struct td_engine *eng, uint64_t cache)
{
	switch (cache & TD_FLUSH_RDBUF_MASK) {
	case TD_FLUSH_RDBUF_NTF_POST:
	case TD_FLUSH_RDBUF_NTF_PRE:
		if (td_eng_conf_hw_var_get(eng, HW_SECTOR_METADATA) && td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE) == 512) {
			td_eng_err(eng, "Can't use RDBUF NTF with 512B sectors and HW metadata\n");
			return -EINVAL;
		}

	default:
		break;
	}

	return 0;
}

