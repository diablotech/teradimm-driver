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
#include "td_compat.h"


#include "td_eng_teradimm.h"
#include "td_histogram.h"
#include "td_util.h"
#include "td_engine.h"
#include "td_eng_completion.h"
#include "td_checksum.h"
#include "td_command.h"
#include "td_crypto.h"
#include "td_protocol.h"
#include "td_device.h"
#include "td_devgroup.h"
#include "td_compat.h"
#include "td_memcpy.h"
#include "td_cache.h"
#include "td_ucmd.h"
#include "td_memspace.h"
#include "td_params.h"
#include "td_biogrp.h"
#include "td_eng_mcefree.h"

#ifdef CONFIG_TERADIMM_SGIO
#include "td_ata_cmd.h"
#endif

MODULE_PARAM(uint, td_default_magicflags, 0)
MODULE_PARAM(uint, td_default_corebufs, 0)
MODULE_PARAM(uint, td_default_writebufs, 0)
MODULE_PARAM(uint, td_engine_skipreset, 0)
MODULE_PARAM(uint, start_seq, 0)

MODULE_PARAM(uint, td_ntf_enable, 1)
MODULE_PARAM(uint, td_mcefree_enable, 1)

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
#ifdef CONFIG_TERADIMM_DIRECT_STATUS_ACCESS
#warning FCEFREE_FWSTATUS is not compatible with DIRECT_STATUS_ACCESS
#endif
#endif


	
module_param_named(magic_flags, td_default_magicflags, uint, 0644);
MODULE_PARM_DESC(magic_flags, "Default MAGIC_FLAGS for new devices");
module_param_named(core_bufs, td_default_corebufs, uint, 0644);
MODULE_PARM_DESC(core_bufs, "Default CORE_BUFS for new devices");
module_param_named(write_bufs, td_default_writebufs, uint, 0644);
MODULE_PARM_DESC(write_bufs, "Default HOST_WRITE_BUFS for new devices");
module_param_named(skip_reset, td_engine_skipreset, uint, 0644);
MODULE_PARM_DESC(skip_reset, "Suppress FW Reset when restarting");

module_param_named(ntf, td_ntf_enable, uint, 0444);
MODULE_PARM_DESC(ntf, "Enable NTF flushing mode.");

module_param_named(mcefree, td_mcefree_enable, uint, 0444);
MODULE_PARM_DESC(mcefree, "Enable mcefree mode.");

/* this is a HACK, remove later */
module_param(start_seq, uint, 0444);
MODULE_PARM_DESC(start_seq, "commands start with this sequence");



/* Declarations of things we steal from the engine */
void td_request_end_common_bio(struct td_token *tok);
void td_request_end_common_ucmd(struct td_token *tok);

static int teradimm_ops_reset_token(struct td_engine *eng,
		struct td_token *tok);
static int teradimm_ops_reverse_cmd_polarity(struct td_engine *eng,
		struct td_token *tok);
static int teradimm_ops_start_token(struct td_engine *eng,
		struct td_token *tok);
static void teradimm_token_write_cmd(struct td_engine *eng, td_cmd_t *tdcmd,
		unsigned id, unsigned alias);

/* ---------------------------------------------------------------- */
/* mapping pages */

struct td_fw_state_info {
	const char *name;
	const char *warning;
};

static struct {
	uint8_t state;
	struct td_fw_state_info progress;
	struct td_fw_state_info final;
} td_fw_state_description[] = {
	{ 0x10, {"Waiting for initialization", NULL},
		{"Shutdown complete", NULL} },
	{ 0x21, {"Initializing Flash Controller", NULL},
		{"Could not initialize Flash Controller", NULL} },
	{ 0x22, {"Checking Flash Controller version", NULL},
		{"Flash controller versions not safe", NULL} },
	{ 0x23, {"Checking Flash Controller FTL", NULL},
		{"Flash controller FTL corrupt", "SECURE ERASE NEEDED"} },
	{ 0x24, {"Performing Flash Controller FTL erase", "ALL DATA BEING REMOVED" },
		{ "Automatic FTL erase done, or Flash FTL erase failed", "RESET NEEDED" } },
	{ 0x25, {"Performing Flash Controller FTL erase", "ALL DATA BEING REMOVED" },
		{ "Flash FTL erase failed", "RESET NEEDED" } },
	{ 0x00, {"RESET in progress", NULL },
		{"FW Ready for operation", NULL } }
};

static struct td_fw_state_info *teradimm_fw_state_info (uint8_t gstatus)
{
	int i;

	for (i = 0; i < NUM_ELEMENTS(td_fw_state_description); i++) {
		if (td_fw_state_description[i].state == TD_GLOBAL_STATUS_STATE(gstatus) ) {
			if (TD_GLOBAL_STATUS_FINAL(gstatus) )
				return &td_fw_state_description[i].final;
			return &td_fw_state_description[i].progress;
		}
	}
	return NULL;
}

void teradimm_process_spd(struct td_engine *eng, uint8_t *spd)
{
	struct td_device *dev = td_engine_device(eng);
	struct td_sha1_state sha1_state;
	unsigned char buf[16];

	/* SPD bytes 117 - 125 are unique (9 bytes), product is after*/
	memcpy(&buf, &spd[117], 16);
	memset(dev->os.uuid, 0x00, TD_UUID_LENGTH);

	td_sha1_init(&sha1_state);
	td_sha1_update(&sha1_state, buf, sizeof(buf));
	td_sha1_final(&sha1_state, dev->os.uuid);
	td_sha1_free(&sha1_state);

	memcpy(dev->os.model,  spd+128, TD_MODEL_LENGTH);
	memcpy(dev->os.serial, spd+122, TD_SERIAL_LENGTH);
	snprintf(dev->os.revision, TD_REVISION_LENGTH, "%02x%02x", spd[147], spd[146]);
	switch (spd[118]) {
	case 0x45:
		memcpy(dev->os.vendor, "SanDisk", min(TD_VENDOR_LENGTH, 8UL));
		break;
	default:
		memcpy(dev->os.vendor, "DiabloMCS", min(TD_VENDOR_LENGTH, 10UL));
	}

	td_eng_info(eng, "SPD Data:\n");
	td_eng_info(eng, "  Manufacturer  %02x:%02x\n", spd[118], spd[117]);
	td_eng_info(eng, "  Location  %02x\n", spd[119]);
	td_eng_info(eng, "  Date  20%02x-%02x\n", spd[120], spd[121]);
	td_eng_info(eng, "  Serial %02x %02x %02x %02x\n",
			spd[122], spd[123], spd[124], spd[125]);
	td_eng_info(eng, "  Product \"%.18s\"\n", spd+128);
	td_eng_info(eng, "  Revision %02x%02x\n", spd[146], spd[147]);
	td_eng_info(eng, "  DIMM type %u\n", spd[150]);
	td_eng_info(eng, "  ASIC type %u\n", spd[155]);
	td_eng_info(eng, "  Capacity 0x%02x%02x [%uG]\n", spd[153], spd[152], (unsigned)spd[152] | spd[153]<<8);
	/* Removed for 1.1
	td_eng_info(eng, "  PFAIL %s\n", spd[154] == 1 ? "supported" : "unavailable");
	*/
}

#ifdef CONFIG_TERADIMM_SPY_UCMD
static void __teradimm_spy_ucmd (struct td_engine *eng, struct td_token *tok)
{
	td_cmd_t *tdcmd = (td_cmd_t *)tok->cmd_bytes;
	const char* name = NULL;

	switch (tdcmd->cmd.id) {
	case TD_CMD_SSD_CMD:
		{
			static const char* digits = "0123456789ABCDEF";
			uint64_t *cmd_part = &tdcmd->pad0.u64;
			int i, j, k;
			char buffer[64];

			j = 0;
			for(k=0; k < 3; k++) {
				for(i=0; i < 7; i++) {
					uint8_t b = (cmd_part[k] >> i*8) & 0xFF;
					buffer[j++] = digits[(b>>4) & 0x0F];
					buffer[j++] = digits[b & 0x0F];
					if (j >45) {
						buffer[j] = '\0';
						break;
					} else
						buffer[j++] = ' ';
				}
			}
			td_eng_info(eng, "TOK 0x%02X CMD 0x%02x [%016llx]: SSD_CMD PORT %u: %s\n",
					tok->tokid,
					tdcmd->cmd.id, tdcmd->cmd.u64,
					tdcmd->cmd.port, buffer);
		}
		break;

	case TD_CMD_TRIM:
		td_eng_info(eng, "TOK 0x%02X CMD 0x%02x [%016llx]: TRIM PORT %u",
					tok->tokid,
					tdcmd->cmd.id, tdcmd->cmd.u64,
					tdcmd->cmd.port);
		break;

	case TD_CMD_GET_PARAMS:
		td_eng_info(eng, "TOK 0x%02X CMD 0x%02x [%016llx]: GET_PARAMS page %d\n",
					tok->tokid,
					tdcmd->cmd.id, tdcmd->cmd.u64,
					tdcmd->src.bufid);
		break;
	case TD_CMD_SET_PARAMS:
		td_eng_info(eng, "TOK 0x%02X CMD 0x%02x [%016llx]: SET_PARAMS page %d\n",
					tok->tokid,
					tdcmd->cmd.id, tdcmd->cmd.u64,
					tdcmd->dst.bufid);
		break;

	case TD_CMD_SHUTDOWN:
		name = "SHUTDOWN";
		break;

	case TD_CMD_DIAG:
		td_eng_info(eng, "TOK 0x%02X CMD 0x%02x [%016llx]: FW_DIAG op %d\n",
				tok->tokid,
				tdcmd->cmd.id, tdcmd->cmd.u64,
				tdcmd->dst.bufid);
		break;

	case TD_CMD_SATA_REINIT:
		td_eng_info(eng, "TOK 0x%02X CMD 0x%02x [%016llx]: SATA_INIT op %d\n",
				tok->tokid,
				tdcmd->cmd.id, tdcmd->cmd.u64,
				tdcmd->dst.bufid);
		break;

#define _CASE_CMD(_n) case TD_CMD_##_n:                         \
		name = __stringify(CMD_ ## _n);                  \
		break;
	_CASE_CMD(GET_REG)
	_CASE_CMD(PUT_REG)
	_CASE_CMD(RESET_CMD)
	_CASE_CMD(RD_SEQ)
	_CASE_CMD(RD_PAGE)
	_CASE_CMD(WR_DLY)
	_CASE_CMD(WR_FINAL)
	_CASE_CMD(RD_EXT)
	_CASE_CMD(WR_EXT)
	_CASE_CMD(SSD_STATS)
	_CASE_CMD(COPY)
	_CASE_CMD(FW_NULL)
	_CASE_CMD(STARTUP)
	_CASE_CMD(TRACE_CTRL)
	_CASE_CMD(TEST)
	_CASE_CMD(SEQ)
	_CASE_CMD(FW_DEBUG_LOG)
	_CASE_CMD(SSD_EXT_ERR)
	_CASE_CMD(FW_DOWNLOAD)
	_CASE_CMD(CRASH_LOG)
	_CASE_CMD(TEST_READ)
	_CASE_CMD(TEST_WRITE)
	_CASE_CMD(SATA_NOREAD)
	_CASE_CMD(SATA_NOWRITE)
	_CASE_CMD(CANCEL)
	_CASE_CMD(FAULT_INJECTION)
#undef _CASE_CMD

	default:
		name = "UNKNOWN";
	}

	if (name)
		td_eng_info(eng, "TOK 0x%02X CMD 0x%02x [%016llx]: %s\n",
				tok->tokid,
				tdcmd->cmd.id, tdcmd->cmd.u64, name);
}

static inline void teradimm_spy_ucmd (struct td_engine *eng, struct td_token *tok)
{
	if (td_eng_conf_var_get(eng, SPY_UCMD))
		__teradimm_spy_ucmd(eng, tok);
}
#else
static inline void teradimm_spy_ucmd (struct td_engine *eng, struct td_token *tok)
{
	return;
}
#endif

/* make sure that the CPU we access this from is legit */
#define WARN_ON_ACCESS_FROM_WRONG_CPU(eng) do { \
	struct td_engine *_eng = (eng); \
	struct td_device *_dev = td_engine_device(_eng); \
	int _cpu = smp_processor_id(); \
	int _socket = topology_physical_package_id(_cpu); \
	WARN_ON( !_dev ); \
	WARN_ON( _socket != _dev->td_cpu_socket ); \
} while(0)

/* This ugly hack is how we get the engine to put is into FW-PROBE  */
extern void td_request_end_deallocate(struct td_token *tok);

static void teradimm_hw_init_mcefree_configure(struct td_engine *eng);
/* ---------------------------------------------------------------- */
/* helpers */

/* for each entry in the status array, learn what the device thinks
 * the odd/even bit is */
static void teradimm_learn_odd_even_bits(struct td_engine *eng)
{
#ifndef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	enum td_token_type tt;
#endif
	int t;
	td_status_t *st;
	struct td_token *tok;

	td_eng_trace(eng, TR_FW, "learn_odd_even_bits", 0);

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	teradimm_force_read_all_real_status(eng, eng->td_status);
#else
	for_each_token_type(tt) {
		int rc = td_eng_hal_read_status(eng, tt);
		if (rc<0)
			return;
	}
#endif

	for (t=0; t<TD_TOKENS_PER_DEV; t++) {

		st = (td_status_t*)eng->td_status + t;
		tok = eng->td_tokens + t;

		tok->odd = st->fin.odd;
		tok->last_status = st->byte;
	}
}

void teradimm_assign_default_config(struct td_engine *eng)
{
	td_eng_conf_hw_var_set(eng, HW_SECTOR_SIZE, 512);  /* what size the hardware operates in */
	td_eng_conf_hw_var_set(eng, BIO_SECTOR_SIZE, TERADIMM_DATA_BUF_SIZE);  /* what size the hardware operates in */

	td_eng_conf_var_set(eng, TOKENS, 256);

	td_eng_conf_var_set(eng, INDEPENDENT_DEALLOCATE, 0); /* 0 means use fast deallocates with other commands */

#if 1
	/* Go back to beta2 timeouts */
	td_eng_conf_var_set(eng, BIO_R_START_TIMEOUT_USEC, 5*1000000);
	td_eng_conf_var_set(eng, BIO_R_OOO_TIMEOUT_USEC, 1000);
	td_eng_conf_var_set(eng, BIO_R_QUEUED_TIMEOUT_USEC, 10*1000000);
	td_eng_conf_var_set(eng, BIO_R_RECEIVED_TIMEOUT_USEC, 15*1000000);
	td_eng_conf_var_set(eng, BIO_R_WAIT_SSD_TIMEOUT_USEC, 30*1000000);
#else
	td_eng_conf_var_set(eng, BIO_R_START_TIMEOUT_USEC, 500);              /* after 500 us waiting to LEAVE start state, the token times out */
	td_eng_conf_var_set(eng, BIO_R_OOO_TIMEOUT_USEC, 1000);
	td_eng_conf_var_set(eng, BIO_R_QUEUED_TIMEOUT_USEC, 9*1000);          /* after 9 ms waiting to LEAVE queued state, the token times out */
	td_eng_conf_var_set(eng, BIO_R_RECEIVED_TIMEOUT_USEC, 49*1000);       /* after 49 ms waiting to LEAVE received state, the token times out */
	td_eng_conf_var_set(eng, BIO_R_WAIT_SSD_TIMEOUT_USEC, 99*1000);       /* after 99 ms waiting to LEAVE wait_ssd state, the token times out */
#endif

#if 1
	/* Go back to beta2 timeouts */
	td_eng_conf_var_set(eng, BIO_W_START_TIMEOUT_USEC, 5*1000000);
	td_eng_conf_var_set(eng, BIO_W_OOO_TIMEOUT_USEC, 1000);
	td_eng_conf_var_set(eng, BIO_W_QUEUED_TIMEOUT_USEC, 10*1000000);
	td_eng_conf_var_set(eng, BIO_W_RECEIVED_TIMEOUT_USEC, 15*1000000);
	td_eng_conf_var_set(eng, BIO_W_WAIT_SSD_TIMEOUT_USEC, 40*1000000);
#else
	td_eng_conf_var_set(eng, BIO_W_START_TIMEOUT_USEC, 500);              /* after 500 us waiting to LEAVE start state, the token times out */
	td_eng_conf_var_set(eng, BIO_W_OOO_TIMEOUT_USEC, 1000);
	td_eng_conf_var_set(eng, BIO_W_QUEUED_TIMEOUT_USEC, 9*1000);          /* after 9 ms waiting to LEAVE queued state, the token times out */
	td_eng_conf_var_set(eng, BIO_W_RECEIVED_TIMEOUT_USEC, 49*1000);       /* after 49 ms waiting to LEAVE received state, the token times out */
	td_eng_conf_var_set(eng, BIO_W_WAIT_SSD_TIMEOUT_USEC, 99*1000);       /* after 99 ms waiting to LEAVE wait_ssd state, the token times out */
#endif

#if 1
	/* Go back to beta2 timeouts */
	td_eng_conf_var_set(eng, START_TIMEOUT_USEC, 10*1000000);
	td_eng_conf_var_set(eng, OOO_TIMEOUT_USEC, 1000);
	td_eng_conf_var_set(eng, QUEUED_TIMEOUT_USEC, 15*1000000);
	td_eng_conf_var_set(eng, RECEIVED_TIMEOUT_USEC, 20*1000000);
	td_eng_conf_var_set(eng, WAIT_SSD_TIMEOUT_USEC, 30*1000000);
	td_eng_conf_var_set(eng, STUCK_TIMEOUT_USEC, 10*1000000);
#else
	td_eng_conf_var_set(eng, START_TIMEOUT_USEC, 500);                    /* after 500 us waiting to LEAVE start state, the token times out */
	td_eng_conf_var_set(eng, OOO_TIMEOUT_USEC, 1000);
	td_eng_conf_var_set(eng, QUEUED_TIMEOUT_USEC, 9*1000);                /* after 9 ms waiting to LEAVE queued state, the token times out */
	td_eng_conf_var_set(eng, RECEIVED_TIMEOUT_USEC, 10*1000000);          /* after 10s waiting to LEAVE received state, the token times out */
	td_eng_conf_var_set(eng, WAIT_SSD_TIMEOUT_USEC, 30*1000000);          /* after 10s waiting to LEAVE wait_ssd state, the token times out */
	td_eng_conf_var_set(eng, STUCK_TIMEOUT_USEC, 30*1000000);             /* after 10s a running token is considered stuck.*/
#endif
	

#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
	td_eng_conf_var_set(eng, HW_TOKID_START, 128);
	td_eng_conf_var_set(eng, RESERVED_TOKID_START, 192);
#endif

	td_eng_conf_var_set(eng, TIMEOUT_RETRY, 0);          /* timeouts get retries */
	td_eng_conf_var_set(eng, RETRIES, 3);              /* 1 retry */
	td_eng_conf_var_set(eng, XSUM_ERR_RETRIES, 100);         /* checksum failures get retried */
	td_eng_conf_var_set(eng, WEP_TIMEOUT_RETRIES, 1000);         /* checksum failures get retried */

	td_eng_conf_var_set(eng, RESETS, 2);                 /* 2 resets, need both polarities */
	td_eng_conf_var_set(eng, ORPHAN_RDBUF_ORDER_CHECK, 0); /* don't use on TeraDIMM, Rush uses round-robin */

	td_eng_conf_hw_var_set(eng, SSD_COUNT, 2);
	td_eng_conf_var_set(eng, HW_CMDS, 16);

	td_eng_conf_var_set(eng, HOST_READ_BUFS, 32);
	td_eng_conf_var_set(eng, HOST_WRITE_BUFS, 8);
	td_eng_conf_var_set(eng, CORE_BUFS, 128);          /* reserve core buffer 127 for FW use */

#ifdef CONFIG_TERADIMM_USES_CORE_BUFFS_ONLY
	td_eng_conf_var_set(eng, CORE_ONLY, 0);
#endif
	td_eng_conf_var_set(eng, MAGIC_FLAGS, 0);

	td_eng_conf_var_set(eng, QUICK_WRBUF, 0);   /* don't release of write buffer after token starts */
	td_eng_conf_var_set(eng, WRITE_WAIT_USEC, 0);   /* driver waits for burst of 4 writes */

	td_eng_conf_var_set(eng, HOST_READ_ALIASES, 0); /* there are exactly 8 bits left for aliasing (max 256) */
	td_eng_conf_var_set(eng, HOST_STATUS_ALIASES, 0); /* there are 13 bits left for aliasing (max 8192) */

	if (td_ntf_enable)
		td_eng_conf_var_set(eng, CLFLUSH, TERADIMM_DEFUALT_CONF_CLFLUSH);
	else
		td_eng_conf_var_set(eng, CLFLUSH, TERADIMM_SAFE_CONF_CLFLUSH);

	td_eng_conf_var_set(eng, WBINVD, TD_WBINVD_OFF);

	td_eng_conf_var_set(eng, EARLY_COMMIT, TD_QUEUED_COMMIT);     /* don't release bios on WAIT_SSD update */

	td_eng_conf_var_set(eng, MAX_START_BURST, 4);           /* when starting, allow upto 4 commands to be started */
	td_eng_conf_var_set(eng, EARLY_DEALLOCATE, 1);          /* every completed command will trigger a deallocate */
	td_eng_conf_var_set(eng, EARLY_START, 1);               /* every completed command can start a new command */

	td_eng_conf_var_set(eng, VERBOSE_TOK_FAILURES, 0);

	td_eng_conf_var_set(eng, HOLD_TIMEDOUT_TOKENS, 1); /* TeraDIMM puts timedout tokens on a hold list */
	td_eng_conf_var_set(eng, TIMEDOUT_CNT_THRESHOLD, 128); /* Give up once we hit this many timedout tokens */

	if (td_engine_device(eng)->td_memspeed < 933) {
		/* We have to limit HOST_WRITE_BUFS to 1 because this might be
		 * 800Mhz */
		td_eng_warn(eng, "Limiting HOST_WRITE_BUFS to 1 because of %uMhz RAM",
				td_engine_device(eng)->td_memspeed);
		td_eng_conf_var_set(eng, HOST_WRITE_BUFS, 1);
	}

	if (td_default_corebufs)
		td_eng_conf_var_set(eng, CORE_BUFS, td_default_corebufs);

	if (td_default_writebufs)
		td_eng_conf_var_set(eng, HOST_WRITE_BUFS, td_default_writebufs);

	if (td_default_magicflags)
		td_eng_conf_var_set(eng, MAGIC_FLAGS, td_default_magicflags);

	if (td_mcefree_enable)
		teradimm_hw_init_mcefree_configure(eng);

	/* if hardware commands are remapped UC it makes more sense to
	 * use MOVNTQ copies */
#ifdef CONFIG_TERADIMM_FORCE_CMD_UC_MAP
	if (td_fw_cmd_uc)
		td_eng_conf_var_set(eng, FW_CMD_WRITE_MODE,
			TD_CMD_WRITE_FENCED_MOVNTQ);
	if (td_hw_cmd_uc)
		td_eng_conf_var_set(eng, HW_CMD_WRITE_MODE,
			TD_CMD_WRITE_FENCED_MOVNTQ);
#endif
}

/* ---------------------------------------------------------------- */
/* ops table */

/** prepare hw and allocate any resources */
int teradimm_ops_init(struct td_engine *eng)
{
	struct td_device *dev = td_engine_device(eng);
	struct td_eng_teradimm *td;
	enum td_token_type tt;
	int rc, node;

	/* we expect to have a pointer to hardware */

	rc = td_mapper_verify_ready(&dev->td_mapper);
	if (rc)
		return rc;

	/* allocate and initialize the serial link */

	node = dev->td_cpu_socket;

	td = kzalloc_node(sizeof(*td), GFP_KERNEL, node);
	if (!td)
		return -ENOMEM;

	/* hard-codded initial default config for the TD */

	teradimm_assign_default_config(eng);

	/* set local pointer to virtual memory */

	td->td_mapper = &dev->td_mapper;

	/* initialize a list for reserved tokens */

	td_token_list_init(&td->td_reserved_tokens);

	/* initialize sequence tracking */
	eng->td_sequence_next = start_seq;
	eng->td_sequence_oldest = -1ULL;

	/* last, set engine link back to HAL object */

	eng->ops_priv = td;
	td->td_engine = eng;

	for_each_token_type(tt)
		eng->maint_toks[tt] = NULL;
	
	memset(&td->ei.held, 0, sizeof(td->ei.held));

	/* assign copy ops for this device */
	eng->td_virt_copy_ops = td_token_copy_ops_virt;
	eng->td_bio_copy_ops = td_token_copy_ops_null;

	return 0;
}

/** verify configuration before enabling access */
int teradimm_ops_enable(struct td_engine *eng)
{
	if (0) {
		int i;
		teradimm_global_status_t gstatus  = { .u64 = 0};

		for (i = 0; i < 10; i++) {
			td_eng_hal_read_ext_status(eng, TERADIMM_EXT_STATUS_GLOBAL_IDX,
					&gstatus, 0);
			td_eng_notice(eng, "GLOBAL STATUS: 0x%016llX [0x%02X]\n", gstatus.u64, gstatus.fw);
			if (gstatus.fw & 0xF0)
				break;
			udelay(10*1000); /* 10 second wait */

		}
	}

	return 0;
}

/** device about to go down */
int teradimm_ops_disable(struct td_engine *eng)
{
	return 0;
}

int teradimm_ops_get_conf(struct td_engine *eng,
	      uint32_t conf_type, uint64_t *val)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);

	switch (conf_type) {
	case TD_CONF_HW_SSD_COUNT:
		*val = td->td_ssd_count;
		break;
	case TD_CONF_HW_SSD_SECTOR_COUNT:
		*val = td->td_ssd_sector_count;
		break;

	       /* ... Any TeraDIMM conf stuff we need ... */

	default:
	       return -ENOENT;
	}

	return 0;
}

int teradimm_ops_set_conf(struct td_engine *eng,
	      uint32_t conf_type, uint64_t val)
{
	//struct td_eng_teradimm *td = td_eng_td_hal(eng);

	switch (conf_type) {
	       /* ... Any TeraDIMM conf stuff we need ... */

	default:
	       return -ENOENT;
	}

	return 0;
}

#if 0
static void teradimm_dump_params (void* param_page, int page)
{
	union td_param *p = param_page;
	int i;
	printk("TeraDIMM FW PARAM PAGE %d\n", page);
	for (i = 0; i < 128; i++)
		if (p[i].u32 != 0 && p[i].u32 < 0x7FFFFFFF)
			printk(" - %d: 0x%08x\n", i, p[i].u32);
}
#endif


int teradimm_config_engine_hw_conf_160 (struct td_engine *eng,
	      union td_param *params)
{
	struct td_param_page0_map_160 *p  = (struct td_param_page0_map_160*)params;
	uint64_t lba_count;
	uint64_t lba_size;
	uint64_t lba_align;
	uint64_t lba_count_min;
	uint64_t ssd_count;
	int i;

#ifdef CONFIG_TERADIMM_IGNORE_SSD
	td_eng_conf_hw_var_set(eng, HW_SECTOR_SIZE, 512);
	td_eng_conf_hw_var_set(eng, HW_SECTOR_ALIGN, 0);
	td_eng_conf_hw_var_set(eng, SSD_COUNT, 2);
	td_eng_conf_hw_var_set(eng, SSD_SECTOR_COUNT, 1024*1024);

	return 0;
#endif

	ssd_count = p->ssd_total.u32;    /* SSDs total */

	lba_count_min = 1ULL << 48;

	for (i = 0; i < ssd_count; i++) {
		lba_count = p->mMonetParams[i].lba_count_high.u32;    /* monet lba_count high */
		lba_count <<= 32;                              /* Shift to high bits */
		lba_count += p->mMonetParams[i].lba_count_low.u32;    /* Add in low bits */
		lba_count_min = min(lba_count, lba_count_min);
		td_eng_debug(eng, "SSD %d: %llu/0x%llX LBAs (%08X, %08X)\n", i,
				lba_count, lba_count,
				p->mMonetParams[i].lba_count_high.u32,
				p->mMonetParams[i].lba_count_low.u32);
	}

	switch (p->lba_size.u32) {
	case 520:
		td_eng_notice(eng, " - Metadata detected\n");
		td_eng_conf_hw_var_set(eng, HW_SECTOR_METADATA, 8);
	case 512:
		lba_size = 512;
		break;
	default:
		td_eng_err(eng, " - BAD LBA size: %d (0x%X)\n",
				p->lba_size.u32, p->lba_size.u32);
		return -EINVAL;
	}

	lba_align = p->lba_align_mask.u32;

	td_eng_conf_hw_var_set(eng, HW_SECTOR_SIZE, lba_size);
	td_eng_conf_hw_var_set(eng, HW_SECTOR_ALIGN, lba_align);
	td_eng_conf_hw_var_set(eng, SSD_COUNT, ssd_count);
	td_eng_conf_hw_var_set(eng, SSD_SECTOR_COUNT, lba_count_min);

	td_eng_debug(eng, " - ATA Identify page offset: %u\n", p->ssd_page_offset.u32);

	return 0;
}

int teradimm_config_engine_hw_conf_161 (struct td_engine *eng,
	      union td_param *params)
{
	struct td_param_page0_map_161 *p  = (struct td_param_page0_map_161*)params;
	uint64_t lba_count;
	uint64_t lba_size;
	uint64_t lba_align;
	uint64_t lba_count_min;
	uint64_t ssd_count;
	int i;

#ifdef CONFIG_TERADIMM_IGNORE_SSD
	td_eng_conf_hw_var_set(eng, HW_SECTOR_SIZE, 512);
	td_eng_conf_hw_var_set(eng, HW_SECTOR_ALIGN, 0);
	td_eng_conf_hw_var_set(eng, SSD_COUNT, 2);
	td_eng_conf_hw_var_set(eng, SSD_SECTOR_COUNT, 1024*1024);

	return 0;
#endif

	ssd_count = p->mSSDTotal.u32;    /* SSDs total */

	lba_count_min = 1ULL << 48;

	for (i = 0; i < ssd_count; i++) {
		/* monet lba_count high */
		lba_count = p->mMonetParams[i].lba_count_high.u32;
		/* Shift to high bits */
		lba_count <<= 32;
		/* Add in low bits */
		lba_count += p->mMonetParams[i].lba_count_low.u32;
		lba_count_min = min(lba_count, lba_count_min);
		td_eng_debug(eng, "SSD %d: %llu/0x%llX LBAs (%08X, %08X)\n", i,
				lba_count, lba_count,
				p->mMonetParams[i].lba_count_high.u32,
				p->mMonetParams[i].lba_count_low.u32);
	}

	switch (p->mLbaSize.u32) {
	case 520:
		td_eng_debug(eng, " - Metadata detected\n");
		td_eng_conf_hw_var_set(eng, HW_SECTOR_METADATA, 8);
	case 512:
		lba_size = 512;
		break;
	default:
		td_eng_err(eng, " - BAD LBA size: %d (0x%X)\n",
				p->mLbaSize.u32, p->mLbaSize.u32);
		return -EINVAL;
	}

	lba_align = p->mLbaAlignMask.u32;

	td_eng_conf_hw_var_set(eng, HW_SECTOR_SIZE, lba_size);
	td_eng_conf_hw_var_set(eng, HW_SECTOR_ALIGN, lba_align);
	td_eng_conf_hw_var_set(eng, SSD_COUNT, ssd_count);
	td_eng_conf_hw_var_set(eng, SSD_SECTOR_COUNT, lba_count_min);

	td_eng_debug(eng, " - ATA Identify page offset: %u\n", p->mMonetPageOffset.u32);

	return 0;
}

int teradimm_config_engine_hw_conf_162 (struct td_engine *eng,
	      union td_param *params)
{
	struct td_param_page0_map_162 *p  = (struct td_param_page0_map_162*)params;
	uint64_t lba_count;
	uint64_t lba_align;
	uint64_t lba_count_min;
	uint64_t ssd_count;
	char version_prefix[6]; /* Sized to hold "OLD X\0" */
	int i;

	version_prefix[0] = '\0';
	if ( CAPEX_CHECK(p->mCapexFlags, 0, VERSION) ) {
		if ( (p->currRushFwPkgVersHi.u32 >> 8) & 0xFF ) {
			version_prefix[0] = '@' + ( (p->currRushFwPkgVersHi.u32 >> 8) & 0xFF);
			version_prefix[1] = '\0';
		}

		td_eng_conf_hw_var_set(eng, VER_MAJOR, p->currRushFwPkgVersHi.u32       & 0xFFFF);
		td_eng_conf_hw_var_set(eng, VER_MINOR, p->currRushFwPkgVersHi.u32 >> 16 & 0xFFFF);
		td_eng_conf_hw_var_set(eng, VER_PATCH, p->currRushFwPkgVersLo.u32       & 0xFFFF);
		td_eng_conf_hw_var_set(eng, VER_BUILD, p->currRushFwPkgVersLo.u32 >> 16 & 0xFFFF);

	} else {
		/* In the old version, we need to "make" our major from parts */
		int major = ( p->currRushFwPkgVersHi.u32       & 0x00FF);

		if (p->currRushFwPkgVersLo.u32 & 0x00FF) 
			  major += (((p->currRushFwPkgVersLo.u32       & 0x00FF)-'@') << 8);

		td_eng_conf_hw_var_set(eng, VER_MAJOR, major);
		td_eng_conf_hw_var_set(eng, VER_MINOR, p->currRushFwPkgVersHi.u32 >>  8 & 0x00FF);
		td_eng_conf_hw_var_set(eng, VER_PATCH, p->currRushFwPkgVersHi.u32 >> 16 & 0xFFFF);
		td_eng_conf_hw_var_set(eng, VER_BUILD, p->currRushFwPkgVersLo.u32 >> 16 & 0xFFFF);

		strncpy(version_prefix, "OLD ", 5);
		if (td_eng_conf_hw_var_get(eng, VER_MAJOR) & 0xFF00) {
			version_prefix[4] = '@' + (td_eng_conf_hw_var_get(eng, VER_MAJOR) >> 8);
			version_prefix[5] = '\0';
		}
	}

	td_eng_info(eng, "FW Version: %s%02u.%02u.%03u BUILD %03u\n",
			version_prefix,
			(unsigned)td_eng_conf_hw_var_get(eng, VER_MAJOR) & 0x00FF,
			(unsigned)td_eng_conf_hw_var_get(eng, VER_MINOR),
			(unsigned)td_eng_conf_hw_var_get(eng, VER_PATCH),
			(unsigned)td_eng_conf_hw_var_get(eng, VER_BUILD) );


	if ( CAPEX_CHECK(p->mCapexFlags, 0, SPD_AVAILABLE) ) {
		td_eng_info(eng, "FW supports SPD information\n");
		td_eng_conf_hw_var_set(eng, SPD, 1);
	}

	if ( CAPEX_CHECK(p->mCapexFlags, 0, RAID_PAGE ) ) {
		td_eng_info(eng, "FW supports RAID metadata\n");
		td_eng_conf_hw_var_set(eng, RAID_PAGE, 1);
	}

	if (1) {
		/* Version 162 now has a bitmap of core buffers they want to reserve */
		int rc;
		uint32_t cb_map[4] = {
			p->mRsvdCoreBuffMask1.u32,
			p->mRsvdCoreBuffMask2.u32,
			p->mRsvdCoreBuffMask3.u32,
			p->mRsvdCoreBuffMask4.u32 };
		for (i = 0; i < 4; i++) {
			int j;
			for (j = 0; j < 32; j++) {

				uint core = (i*32) + j;

				if (! (cb_map[i] & (1 << j)))
					continue;

				rc = td_reserve_core_buffer(eng, core);
				if (rc == 0)
					td_eng_info(eng, "FW reserving core_buf %u\n", core);
				else
					td_eng_info(eng, "core_buf %u already allocated\n", core);
			}

		}
	}
	
	if (1) {
		for (i = 0; i < 32; i++)
			if (p->mCapexFlags[0].u32 & (1<<i))
				td_eng_info(eng, "CAPEX: bit %d\n", i);
	}
	
	if (CAPEX_CHECK(p->mCapexFlags, 0, SHUTDOWN) ) {
		td_eng_info(eng, "FW Supports Shutdown CAPEX\n");
		td_eng_conf_var_set(eng, SHUTDOWN, 1);
	}


#ifdef CONFIG_TERADIMM_IGNORE_SSD
	td_eng_conf_hw_var_set(eng, HW_SECTOR_SIZE, 512);
	td_eng_conf_hw_var_set(eng, HW_SECTOR_ALIGN, 0);
	td_eng_conf_hw_var_set(eng, SSD_COUNT, 2);
	td_eng_conf_hw_var_set(eng, SSD_SECTOR_COUNT, 1024*1024);

	return 0;
#endif

	ssd_count = p->mSSDTotal.u32;    /* SSDs total */

	lba_count_min = 1ULL << 48;

	for (i = 0; i < ssd_count; i++) {
		/* monet lba_count high */
		lba_count = p->mMonetParams[i].lba_count_high.u32;
		/* Shift to high bits */
		lba_count <<= 32;
		/* Add in low bits */
		lba_count += p->mMonetParams[i].lba_count_low.u32;
		lba_count_min = min(lba_count, lba_count_min);
		td_eng_debug(eng, "SSD %d: %llu/0x%llX LBAs (%08X, %08X)\n", i,
				lba_count, lba_count,
				p->mMonetParams[i].lba_count_high.u32,
				p->mMonetParams[i].lba_count_low.u32);
	}

	switch (p->mLbaSize.u32) {
	case 520:
		td_eng_debug(eng, " - Metadata detected\n");
		td_eng_conf_hw_var_set(eng, HW_SECTOR_SIZE, 512);
		td_eng_conf_hw_var_set(eng, HW_SECTOR_METADATA, 8);
		break;
	case 512:
		td_eng_conf_hw_var_set(eng, HW_SECTOR_SIZE, 512);
		td_eng_conf_hw_var_set(eng, HW_SECTOR_METADATA, 0);
		break;
	default:
		td_eng_err(eng, " - BAD LBA size: %d (0x%X)\n",
				p->mLbaSize.u32, p->mLbaSize.u32);
		return -EINVAL;
	}

	lba_align = p->mLbaAlignMask.u32;

	td_eng_conf_hw_var_set(eng, HW_SECTOR_ALIGN, lba_align);
	td_eng_conf_hw_var_set(eng, SSD_COUNT, ssd_count);
	td_eng_conf_hw_var_set(eng, SSD_SECTOR_COUNT, lba_count_min);

	td_eng_debug(eng, " - ATA Identify page offset: %u\n", p->mMonetPageOffset.u32);

	return 0;
}

int teradimm_config_engine_conf (struct td_engine *eng,
	      struct td_param_page1_map *param)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	uint64_t host_provision;
	// teradimm_dump_params(param, 1);

	td_eng_conf_hw_var_set(eng, BIO_SECTOR_SIZE, param->host_blocksize.value);
	td_eng_conf_hw_var_set(eng, E2E_MODE, param->host_e2e_mode.value);
	td_eng_conf_hw_var_set(eng, SSD_STRIPE_LBAS, param->host_ssd_stride_sectors.value);
	
	td_eng_conf_hw_var_set(eng, SSD_STRIPE_LBAS, param->host_ssd_stride_sectors.value);

	host_provision = param->host_provision_high.value;
	host_provision <<= 31;
	host_provision += param->host_provision_low.value;
	if (host_provision && host_provision < td->td_ssd_sector_count) {
		td_eng_info(eng, "Host overprovision set to %llu%c [%llu/%llu]\n",
				host_provision * 100 / td->td_ssd_sector_count, '%',
				host_provision, td->td_ssd_sector_count);
		td_eng_conf_hw_var_set(eng, SSD_SECTOR_COUNT, host_provision);
	} else {
		td_eng_conf_hw_var_set(eng, SSD_SECTOR_COUNT, td->td_ssd_sector_count);
	}


	/* On by default! */
	switch (param->host_trim.value) {
	default:        /* For now, default is disbaled */
	case 0x00:
		td_eng_conf_hw_var_set(eng, DISCARD, 0);
		break;
	case 0x01:
		td_eng_conf_hw_var_set(eng, DISCARD, 1);
		break;
	}

	return 0;
}


/* Used by STM */
int teradimm_ops_fw_handshake(struct td_engine *eng)
{
	WARN_ON("We should never be here...");
	return 0;
}

static struct td_token* teradimm_hw_init_reserve (struct td_engine *eng)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	struct td_token *tok, *maint_tok;
	unsigned max, i;

	td_eng_trace(eng, TR_FW, "hw-init:reserve:tok-fw",
			td_eng_fw_maint_tok(eng) ? td_eng_fw_maint_tok(eng)->tokid : -1);
	td_eng_trace(eng, TR_FW, "hw-init:reserve:tok-hw",
			td_eng_hw_maint_tok(eng) ? td_eng_hw_maint_tok(eng)->tokid : -1);

#ifdef CONFIG_TERADIMM_RESERVE_TOKENS
	/* reserve some tokens for firmware debug
	 * this may fail, but that's ok on second enable */
	max = (unsigned)td_eng_conf_var_get(eng, TOKENS);
	max = min_t(unsigned, max, TD_TOKENS_PER_DEV);
	if (CONFIG_TERADIMM_RESERVE_TOKENS > max) {
		td_eng_err(eng, "CONFIG_TERADIMM_RESERVE_TOKENS(%u) "
			"is too large\n", CONFIG_TERADIMM_RESERVE_TOKENS);
		goto error_skip_reserve;
	}
	for (i=0; i<CONFIG_TERADIMM_RESERVE_TOKENS; i++) {
		uint tok_id = max - CONFIG_TERADIMM_RESERVE_TOKENS + i;
		tok = td_alloc_token_id(eng, (uint16_t)tok_id);
		if (tok) {
			/* printk("reserved 0x%02x\n", tok->tokid); */
			td_tokens_enqueue(&td->td_reserved_tokens, tok);
		}
	}

error_skip_reserve:
#endif

	/* Use a NULL to get something through to the next step */
	maint_tok = td_eng_hw_maint_tok(eng);
	td_cmdgen_null(maint_tok->cmd_bytes);

	return maint_tok;
}

#define WARN_ON_TOK_FAILED(tok) do { \
	if (tok->result != TD_TOK_RESULT_OK) { \
		td_eng_warn((tok)->td_engine, \
			"%s:%u: tok %u cmd %016llx result %d\n", \
			__FUNCTION__, __LINE__, (tok)->tokid, \
			(tok)->cmd_bytes[0], (tok)->result); \
	} \
} while(0)

static int teradimm_hw_init_reserve_done (struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);

	WARN_ON_TOK_FAILED(tok);

	td_eng_trace(eng, TR_FW, "hw-init:reserve:done", tok->tokid);

	return 1;
}

#ifdef CONFIG_TERADIMM_ARM_RESET_ON_START
static struct td_token* teradimm_hw_init_arm_reset (struct td_engine *eng)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	struct td_token *maint_tok;
	uint64_t *hw_wep7;
	teradimm_global_status_t gstatus;
	
	td_eng_hal_read_ext_status(eng, TERADIMM_EXT_STATUS_GLOBAL_IDX, &gstatus, 1);
	maint_tok = td_eng_hw_maint_tok(eng);
	maint_tok->quick_n_quiet = 1;

	if (td->hw_init.slot & 0x80) {
		td_cmdgen_null(maint_tok->cmd_bytes);
		return maint_tok;
	}

	switch (td->hw_init.slot & 0x7f) {
		case 0x00:
			if (TD_GLOBAL_STATUS_FINAL(gstatus.fw) && td_engine_skipreset == 0) {
				td_eng_info(eng, "FW Reset Setup\n");

				td_eng_trace(eng, TR_FW, "hw-init:arm_reset0:tok", maint_tok->tokid);

				/* setup for soft fw reset */
				td_cmdgen_put_reg(maint_tok->cmd_bytes, 0xa800, 0x05000000, 0xffffffff);
				td->hw_init.slot = 0x81;

				/* This is so we notice a global status change ASAP */
				td_eng_td_hal(eng)->hw_init.gstatus = 0xFF;

				/* before doing the reset, put a marker into WEP-7.  This marker will be
		 		* altered by the FW resetting, and it will determine completion of the
		 		* reset below */
				hw_wep7 = teradimm_map_write_data_buf(td->td_mapper, 0, 7);

				*(volatile uint64_t*)hw_wep7 = 0xDEADBEEF;

				if (TD_MAPPER_TYPE_TEST(WRITE_DATA, WB)) {
					mb();
					clflush(hw_wep7);
					mb();
				}
			} else {
				td_eng_info(eng, "Skipping FW Reset, gstatus = 0x%02X\n", gstatus.fw);
				td_cmdgen_null(maint_tok->cmd_bytes);
				td->hw_init.slot = 0;
			}
			break;

		case 0x01:
			td_eng_info(eng, "FW Soft Reset\n");
			td_eng_trace(eng, TR_FW, "hw-init:arm_reset1:tok", maint_tok->tokid);
			/* generate fw interrupt to perform reset */
			td_cmdgen_put_reg(maint_tok->cmd_bytes, 0x2000, 0x01000000, 0x01000000);
			td->hw_init.slot = 0x82;
			break;

		case 0x10:
			td_eng_info(eng, "FW Hard Reset\n");
			td_eng_trace(eng, TR_FW, "hw-init:arm_reset2:tok", maint_tok->tokid);
			/* Put-reg - pulse bit 1 (00000010b), touching only bit 1 */
			td_cmdgen_put_reg(maint_tok->cmd_bytes, 0x7004, 0x2, 0x2);
			td->hw_init.slot = 0x91;
			break;
	}

	return maint_tok;
}

static int teradimm_hw_init_arm_reset_done (struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	uint64_t *hw_wep7, wep7;

	td_eng_trace(eng, TR_FW, "hw-init:reset:done", tok->tokid);
	td_eng_trace(eng, TR_FW, "hw-init:reset:result", tok->result);
	
	/*
	 * This expects tokens to timeout, because we know ARM is going
	 * to zero all the status memory.  But we know things are done when
	 * our WEP7 pattern has been cleared.
	 */

	/* if there was no reset, return now */
	if (! td->hw_init.slot)
		goto done;

	/* proceed from step 1 to step 2 without checking */
	if (td->hw_init.slot == 0x81) {
		td->hw_init.slot = 1;
		return 0;
	}

	/* wait for a change in the wep7 buffer */

	hw_wep7 = teradimm_map_write_data_buf(td->td_mapper, 0, 7);

	if (TD_MAPPER_TYPE_TEST(WRITE_DATA, WB)) {
		mb();
		clflush(hw_wep7);
		mb();
	}

	wep7 = *(volatile uint64_t*)hw_wep7;

	if (wep7 == 0xDEADBEEF) {
		/* soft reset complete but fw didn't reset WEP-7 */
		if ((td->hw_init.slot & 0xf0)  == 0x80) {
			/* give the firmware a while to take action */
			msleep(10);
			td->hw_init.slot &= 0x7f;
			td->hw_init.slot++;
			td->hw_init.slot |= 0x80;
		} else {
			td->hw_init.slot &= 0x7f;
		}

		/* arm is not yet been reset, or FW didn't reset WEP-7 yet */
		return 0;
	}

	/* After ARM reset is in init, we want to wait a bit longer for things to settle
	 * before continuing. */
	udelay(10);

done:
	/* This is what our next step (poll) should be polling for*/
	td->hw_init.gstatus = 0xFF;
	td->hw_init.poll_mask = 0xFF;
	td->hw_init.poll_gstatus = 0x10;

	return 1;
}
#endif

static struct td_token* teradimm_hw_init_cancel (struct td_engine *eng)
{
	struct td_token *maint_tok;
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	teradimm_global_status_t gstatus;

	td_eng_hal_read_ext_status(eng, TERADIMM_EXT_STATUS_GLOBAL_IDX, &gstatus, 1);
	if (gstatus.fw & 0x80) {
		if (td->hw_init.slot == 0)
			td_eng_info(eng, "Issuing CANCEL_CMD\n");
		td_eng_debug(eng, "  Cancel Slot %d\n", td->hw_init.slot);

		maint_tok = td_eng_fw_maint_tok(eng);
		td_eng_trace(eng, TR_FW, "hw-init:cancel:tok", maint_tok->tokid);

		td_eng_trace(eng, TR_FW, "hw-init:cancel:tok", maint_tok->tokid);
		td_eng_trace(eng, TR_FW, "hw-init:cancel:slot", td->hw_init.slot);

		td_cmdgen_cancel(maint_tok->cmd_bytes, (uint8_t)td->hw_init.slot);
	} else {
		td_eng_info(eng, "Skipping CANCEL_CMD, gstatus = 0x%02X\n", gstatus.fw);

		maint_tok = td_eng_hw_maint_tok(eng);
		td_eng_trace(eng, TR_FW, "hw-init:null-cancel:tok", maint_tok->tokid);

		td_cmdgen_null(maint_tok->cmd_bytes);
		/* This is the what the cancel_done checks to repeat */
		td->hw_init.slot = (int)td_eng_conf_var_get(eng, TOKENS);
	}
	
	return maint_tok;
}

static int teradimm_hw_init_cancel_done (struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);
	struct td_eng_teradimm *td = td_eng_td_hal(eng);

	WARN_ON_TOK_FAILED(tok);

	td_eng_trace(eng, TR_FW, "hw-init:cancel:done", tok->tokid);
	
	if (tok->result == TD_TOK_RESULT_TIMEOUT) {
		td_eng_err(eng, "Couldn't issue CANCEL_CMD, failing\n");
		return -EIO;
	}

	/* We repeat this step for every cancel slot */
	for (;/* ever */;) {
		td->hw_init.slot++;

		if (td->hw_init.slot > TERADIMM_COMMAND_MAX)
			/* reached the end of command space */
			break;

		/* skip over fw maintenance token */
		if (td->hw_init.slot == td_eng_fw_maint_tok(eng)->tokid)
			continue;

		/* skip over hw maintenance token */
		if (td->hw_init.slot == td_eng_hw_maint_tok(eng)->tokid)
			continue;

		/* not done yet */
		return 0;
	}

	/* And if we are done, we advance to the next step*/
	return 1;
}

static struct td_token* teradimm_hw_init_sata (struct td_engine *eng)
{
	struct td_token *maint_tok;

	td_eng_info(eng, "Issuing SATA_RESET\n");

	maint_tok = td_eng_fw_maint_tok(eng);

	td_eng_trace(eng, TR_FW, "hw-init:sata:tok", maint_tok->tokid);
	
	td_cmdgen_sata_reinit(maint_tok->cmd_bytes, 0);

	return maint_tok;
}

static int teradimm_hw_init_sata_done (struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);
	struct td_eng_teradimm *td = td_eng_td_hal(eng);

	WARN_ON_TOK_FAILED(tok);

	td_eng_trace(eng, TR_FW, "hw-init:sata:done", tok->tokid);
	td_eng_trace(eng, TR_FW, "hw-init:sata:result", tok->result);

	if (tok->result != TD_TOK_RESULT_OK) {
		td_eng_err(eng, "Couldn't issue SATA REINIT, failing\n");
		return -EIO;
	}
	td->hw_init.gstatus = 0;
	
	/* This is what our next step (poll) should be polling for*/
	td->hw_init.poll_mask = 0x80;
	td->hw_init.poll_gstatus = 0x80;

	return 1;
}

static struct td_token* teradimm_hw_init_poll (struct td_engine *eng)
{
	struct td_token *maint_tok;

	maint_tok = td_eng_hw_maint_tok(eng);

	td_eng_trace(eng, TR_FW, "hw-init:poll:start", maint_tok->tokid);

	maint_tok->quick_n_quiet = 1;
	td_cmdgen_null(maint_tok->cmd_bytes);

	return maint_tok;
}

static int teradimm_hw_init_poll_done(struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	teradimm_global_status_t gstatus;

	WARN_ON_TOK_FAILED(tok);

	td_eng_trace(eng, TR_FW, "hw-init:poll:done", tok->tokid);
	td_eng_trace(eng, TR_FW, "hw-init:poll:result", tok->result);

	td_eng_hal_read_ext_status(eng, TERADIMM_EXT_STATUS_GLOBAL_IDX, &gstatus, 1);
	td_eng_trace(eng, TR_FW, "hw-init:poll:gstatus", gstatus.u64);

	if (td->hw_init.gstatus != gstatus.fw) {
		struct td_fw_state_info *info;
		td->hw_init.gstatus = gstatus.fw;
		info = teradimm_fw_state_info(td->hw_init.gstatus);

		if (info) {
			if (info->name)
				td_eng_info(eng, "HW: Global status 0x%02X: %s\n",
						td->hw_init.gstatus, info->name);
			else
				td_eng_info(eng, "HW: Global status 0x%02X\n",
						td->hw_init.gstatus);

			if (info->warning)
				td_eng_info(eng, "   * * * * %s * * * *\n",
						info->warning);

			td->hw_init.gstatus_2x_count = 0;

		} else if (TD_GLOBAL_STATUS_FINAL(gstatus.fw) ) {
			td_eng_info(eng, "HW: Global status 0x%02X (0x%02X) FINAL\n",
					gstatus.fw, TD_GLOBAL_STATUS_STATE(gstatus.fw));

			td->hw_init.gstatus_2x_count = 0;

		} else {
			/* 0x2* messages sometimes repeat a lot --
				* SATA reinit, hide the reruns */
			const char *show = "";
			if ((td->hw_init.gstatus & 0xf0) == 0x20) {
				int cnt = ++td->hw_init.gstatus_2x_count;
#define MAX_2X_REPS 10
				if (cnt > MAX_2X_REPS)
					show = NULL;
				else if (cnt == MAX_2X_REPS)
					show = " ..."; /* last one of this time */

			} else
				td->hw_init.gstatus_2x_count = 0;

			if (show)
				td_eng_info(eng, "HW: Global status 0x%02X%s\n",
					TD_GLOBAL_STATUS_STATE(gstatus.fw),
					show);
		}
	}

	if ((td->hw_init.gstatus & td->hw_init.poll_mask)
			== td->hw_init.poll_gstatus) {
		td_eng_info(eng, "Global Status match: %02X [%02X & %02X]\n",
				td->hw_init.poll_gstatus,
				td->hw_init.gstatus,
				td->hw_init.poll_mask);
		td_eng_trace(eng, TR_FW, "hw-init:poll:match", (uint64_t)td->hw_init.poll_gstatus);
		return 1;
	}

	if (TD_GLOBAL_STATUS_FINAL(td->hw_init.gstatus) ) {
		td_eng_info(eng, "Global Status final: %02X\n",
				td->hw_init.gstatus);
		return 1;
	}
	return 0;
}

#define TD_HW_INIT_DEALLOCATIONS 5

static struct td_token* teradimm_hw_init_deallocate(struct td_engine *eng)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	struct td_token *maint_tok;


	td_eng_trace(eng, TR_FW, "hw-init:dealloc:slot", td->hw_init.slot);

	/* this is not the first deallocate sent in a loop (read below),
	 * so we have to wait a bit to allow the hardware to fill up the read
	 * buffers so our next deallocation doesn't come too soon */
	if (td->hw_init.slot)
		/* this is not the ideal solution (see below, and bug 5686) */
		udelay(60);

	/* send the right number of deallocations */

	maint_tok = td_eng_hw_maint_tok(eng);
	td_eng_trace(eng, TR_FW, "hw-init:dealloc:tok", maint_tok->tokid);

	td_cmdgen_put_reg(maint_tok->cmd_bytes, 0x6005, 1, 0xFFFFFFFF);
	td_set_token_timeout(maint_tok, 1000);

	return maint_tok;
}

static int teradimm_hw_init_deallocate_done(struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);
	struct td_eng_teradimm *td = td_eng_td_hal(eng);

	WARN_ON_TOK_FAILED(tok);

	td_eng_trace(eng, TR_FW, "hw-init:dealloc:done", tok->tokid);
	td_eng_trace(eng, TR_FW, "hw-init:dealloc:result", tok->result);

	/* after 5 forced deallocations, the hardware will have cleared any
	 * backlog reads returning from the FW */
	td->hw_init.slot++;
	if (td->hw_init.slot < TD_HW_INIT_DEALLOCATIONS)
		return 0;

	/* inform engine that all read buffers are now available */
	td_reset_rdbuf_deallocation(eng);

	return 1;
}

static struct td_token* teradimm_hw_init_mcefree_hold_rdbuf0(struct td_engine *eng)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	struct td_token *maint_tok;
	struct td_token *get_reg_tok;
	uint8_t st0, st1;

	if (td_eng_conf_var_get(eng, MCEFREE_FWSTATUS) != TD_MCEFREE_FWSTATUS_RDBUF0) {

		/* not using MCEFREE with status in RDBUF0, so skip this and
		 * go to the next step */

		maint_tok = td_eng_hw_maint_tok(eng);

		td_eng_trace(eng, TR_FW, "hw-init:hold_rdbuf0:skip:tok", maint_tok->tokid);

		td_cmdgen_null(maint_tok->cmd_bytes);
		return maint_tok;
	}

	/* in this step we need to lock down RDBUF0, which we do using a
	 * get-reg that is guaranteed to come out on RDBUF0 because we just
	 * deallocated all read buffers in the previous step
	 *
	 * this is a fire and forget operation intended to
	 * claim read buffer 0 for ever.
	 *
	 * we need a new token, so we will use a reserved one.
	 * we need to send a command that claims a read buffer, using hw get-reg.
	 * this comes after 5 purges of read buffers, so hw will fill rdbuf0 next.
	 * this command will claim it, so that FW can put status there.
	 *
	 * NOTE: we explicitly don't want to send this command through
	 * the status checking machine because it would confuse the
	 * mcefree buffer matching code.
	 */

	get_reg_tok = td_tokens_dequeue(&td->td_reserved_tokens);
	if (!get_reg_tok) {
		td_eng_err(eng, "no reserved tokens available.\n");
		td_eng_trace(eng, TR_FW, "hw-init:hold_rdbuf0:no-tok", -1);
		goto skip_to_null;
	}

	/* construct a get-reg command */

	td_cmdgen_get_reg(get_reg_tok->cmd_bytes, 0x6002);
	teradimm_ops_reverse_cmd_polarity(eng, get_reg_tok);

	/* read status before starting */

	teradimm_force_read_real_status(eng, get_reg_tok->tokid, &st0);

	/* write the command */

	td_eng_trace(eng, TR_TOKEN, "hw-init:hold_rdbuf0:tok    ", get_reg_tok->tokid);
	td_eng_trace(eng, TR_TOKEN, "hw-init:hold_rdbuf0:odd    ", get_reg_tok->odd);

	td_tok_event(get_reg_tok, TD_TOK_EVENT_START, 0);

	teradimm_token_write_cmd(eng, (void*)get_reg_tok->cmd_bytes, get_reg_tok->tokid, 0);
	get_reg_tok->last_cmd_issue = td_get_cycles();

	/* wait and read status back */

	udelay(50);
	teradimm_force_read_real_status(eng, get_reg_tok->tokid, &st1);

	/* complain quietly if something went wrong */

	if (st0==st1)
		td_eng_debug(eng, "reservation of read buffer 0 failed, "
				"%02x -> %02x\n", st0, st1);


	/* release the buffer */

	td_tokens_enqueue(&td->td_reserved_tokens, get_reg_tok);

	/* prevent engine from using read buffer zero */

	td_remove_rdbuf_from_circulatiion(eng, 0);

skip_to_null:
	/* go to the next step with a null */

	maint_tok = td_eng_hw_maint_tok(eng);
	td_eng_trace(eng, TR_FW, "hw-init:hold_rdbuf0:null:tok", maint_tok->tokid);

	td_cmdgen_null(maint_tok->cmd_bytes);

	return maint_tok;
}

static int teradimm_hw_init_mcefree_hold_rdbuf0_done(struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);
	//struct td_eng_teradimm *td = td_eng_td_hal(eng);

	WARN_ON_TOK_FAILED(tok);

	td_eng_trace(eng, TR_FW, "hw-init:hold_rdbuf0:done", tok->tokid);
	td_eng_trace(eng, TR_FW, "hw-init:hold_rdbuf0:result", tok->result);

	return 1;

}

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
static void teradimm_mcefree_disable(struct td_engine *eng);
static int teradimm_mcefree_enable(struct td_engine *eng);

static inline int teradimm_has_mcefree_support(struct td_engine *eng)
{
	if (!td_mcefree_enable)
		return 0;
	return td_eng_conf_hw_var_get(eng, MCEFREE_SUPPORT);
}

static struct td_token* teradimm_hw_init_mcefree_detect(struct td_engine *eng)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	uint64_t *hw_wep7, *hw_rdbuf0, *hw_sema;
	struct td_token *maint_tok;

	/* clear semaphore in WEP 7 */

	hw_wep7 = teradimm_map_write_data_buf(td->td_mapper, 0, 7);
	hw_sema = PTR_OFS(hw_wep7,TERADIMM_WEP7_SEMAPHORE_OFS);

	memset(hw_sema, 0, 64);
	if (TD_MAPPER_TYPE_TEST(WRITE_DATA, WB)) {
		mb();
		clflush(hw_sema);
		mb();
	}

	/* read out semaphore from read buffer 0 */

	hw_rdbuf0 = teradimm_map_read_data_buf(td->td_mapper, 0, 0);
	hw_sema = PTR_OFS(hw_rdbuf0,TERADIMM_RDBUF0_SEMAPHORE_OFS);

	if (TD_MAPPER_TYPE_TEST(WRITE_DATA, WB)) {
		mb();
		clflush(hw_sema);
		mb();
		memcpy(td->mcefree.scratch, hw_sema, 64);

	} else {
		td_memcpy_movntdqa_64(td->mcefree.scratch, hw_sema, 64);
	}

	/* request FW update WEP7 status */

	maint_tok = td_eng_hw_maint_tok(eng);

	td_eng_trace(eng, TR_FW, "hw-init:mcefree-detect:tok", maint_tok->tokid);

	td_cmdgen_put_reg(maint_tok->cmd_bytes, 0x2000, 1<<30, 1<<30);
	td_set_token_timeout(maint_tok, 1000);

	return maint_tok;
}
static int teradimm_hw_init_mcefree_detect_done(struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	uint64_t *hw_wep7, *hw_rdbuf0, *hw_sema, w7 = 0, r0 = 0;
	int en;

	en = TD_MCEFREE_FWSTATUS_DISABLED;

	WARN_ON_TOK_FAILED(tok);

	td_eng_trace(eng, TR_FW, "hw-init:mcefree-detect:done", tok->tokid);
	td_eng_trace(eng, TR_FW, "hw-init:mcefree-detect:result", tok->result);

	/* delay for 100us */

	udelay(100);

	/* read the semaphore from WEP-7 */

	hw_wep7 = teradimm_map_write_data_buf(td->td_mapper, 0, 7);
	hw_sema = PTR_OFS(hw_wep7,TERADIMM_WEP7_SEMAPHORE_OFS);

	if (TD_MAPPER_TYPE_TEST(WRITE_DATA, WB)) {
		mb();
		clflush(hw_sema);
		mb();
	}

	w7 = *(volatile uint64_t*)hw_sema;

	if (w7 == 1) {
		en = TD_MCEFREE_FWSTATUS_WEP7;
		td_eng_warn(eng, "MCEFREE WEP7 detected (0x%016llx)\n", w7);
		goto go;
	}

	/* read the semaphore from READ-0 */

	hw_rdbuf0 = teradimm_map_read_data_buf(td->td_mapper, 0, 0);
	hw_sema = PTR_OFS(hw_rdbuf0,TERADIMM_RDBUF0_SEMAPHORE_OFS);

	if (TD_MAPPER_TYPE_TEST(READ_DATA, WB)) {
		mb();
		clflush(hw_sema);
		mb();
	}

	if (!memcmp(hw_sema, td->mcefree.scratch, 64))
		goto no_change;

	r0 = *(volatile uint64_t*)hw_sema;

	if (r0 == 1 || r0 == 0) {
		td->mcefree.last_sema_value = r0;
		en = TD_MCEFREE_FWSTATUS_RDBUF0;
		td_eng_info(eng, "MCEFREE RDBUF0 detected (0x%016llx)\n", r0);
		goto go;
	}

	/* neither changed, keep trying */

no_change:

	++ td->hw_init.slot;
	if (td->hw_init.slot < 10)
		return 0;

go:
	td_eng_trace(eng, TR_FW, "hw-init:mcefree-detect:available:wep7", w7);
	td_eng_trace(eng, TR_FW, "hw-init:mcefree-detect:available:rdbuf0", r0);

	td_eng_conf_hw_var_set(eng, MCEFREE_SUPPORT, en);

	return 1;
}

static void teradimm_hw_init_mcefree_configure(struct td_engine *eng)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	td_eng_conf_mcefree_var_set(eng, STATUS_R2R_NSEC, 10000); // 10us
	td_eng_conf_mcefree_var_set(eng, STATUS_R2P_NSEC, 10000); // 10us
	td_eng_conf_mcefree_var_set(eng, STATUS_P2P_NSEC,  5000); // 5us
	td_eng_conf_mcefree_var_set(eng, STATUS_R2P_TIMEOUT_NSEC, 1000000000); // 1s
	td_eng_conf_mcefree_var_set(eng, STATUS_R2P_RETRY_MAX, 10);
	td_eng_conf_mcefree_var_set(eng, HALT_ON_STATUS_TIMEOUT, 0);

	td_eng_conf_mcefree_var_set(eng, PROGRESS_TIMEOUT_USEC, 1000000); /* after 1 sec w/o an update on a read buffer, move on */
	td_eng_conf_mcefree_var_set(eng, TOKEN_TIMEOUT_USEC, 10000000); /* after 2 seconds timeout a partial read */
	td_eng_conf_mcefree_var_set(eng, STATUS_HOLD_NSEC, 4000); /* don't allow hardware access (status or rdbuf meta) for 5us after deallocate */

	td_eng_conf_mcefree_var_set(eng, RDBUF_RETRY_NSEC,       3000); /*< wait after stale rdbuf metadata*/

	td_eng_conf_mcefree_var_set(eng, RDBUF_HOLD_NSEC,        1000); /*< current hold back */
	td_eng_conf_mcefree_var_set(eng, RDBUF_HOLD_MIN_NSEC,    3500); /*< min hold off time */
	td_eng_conf_mcefree_var_set(eng, RDBUF_HOLD_MAX_NSEC,   15000); /*< max hold off time */
	td_eng_conf_mcefree_var_set(eng, RDBUF_HOLD_CLIMB_NSEC,  1000); /*< increases hold time by this much */
	td_eng_conf_mcefree_var_set(eng, RDBUF_HOLD_DROP_NSEC,     10); /*< decreases hold time by this much */
	td_eng_conf_mcefree_var_set(eng, RDBUF_HOLD_COOL_MSEC,     40); /*< start dropping after this time */

	td_eng_conf_mcefree_var_set(eng, POST_DEALLOC_DELAY_NSEC,   0); /*< wait after a deallocate */
	td_eng_conf_mcefree_var_set(eng, WRITE_HOLD_NSEC,           0); /*< dealloc to write hold */

	td_eng_conf_mcefree_var_set(eng, JIT_DEALLOCATES,           1); /*< JIT deallocations */

	if (td_engine_device(eng)->td_memspeed < 1199) {
		/* 1066 and 800 */
		td_eng_conf_mcefree_var_set(eng, DEALLOC_HOLD_NSEC,     20000); /*< write to deallocate hold */
		td_eng_conf_mcefree_var_set(eng, NO_WRITE_WHILE_FWSTATUS,   1); /*< don't send writes while waiting for FWSTATUS */
		td_eng_conf_mcefree_var_set(eng, FWSTATUS_HOLD_NSEC,    20000); /* don't request FWSTATUS after write commands */
	} else {
		/* 1600 and 1333 */
		td_eng_conf_mcefree_var_set(eng, DEALLOC_HOLD_NSEC,     10000); /*< write to deallocate hold */
		td_eng_conf_mcefree_var_set(eng, NO_WRITE_WHILE_FWSTATUS,   0); /*< don't send writes while waiting for FWSTATUS */
		td_eng_conf_mcefree_var_set(eng, FWSTATUS_HOLD_NSEC,        0); /* don't request FWSTATUS after write commands */
	}

	td_eng_conf_mcefree_var_set(eng, STATUS_R2P_MIN_NSEC,   10000); /*< min hold off time */
	td_eng_conf_mcefree_var_set(eng, STATUS_R2P_MAX_NSEC,   25000); /*< max hold off time */
	td_eng_conf_mcefree_var_set(eng, STATUS_R2P_CLIMB_NSEC,  1000); /*< increases hold time by this much */
	td_eng_conf_mcefree_var_set(eng, STATUS_R2P_DROP_NSEC,     10); /*< decreases hold time by this much */
	td_eng_conf_mcefree_var_set(eng, STATUS_R2P_COOL_MSEC,     40); /*< start dropping after this time */

#endif
}

static void teradimm_hw_init_mcefree_configure_running(struct td_engine *eng)
{
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	uint64_t min = td_eng_conf_mcefree_var_get(eng, STATUS_R2P_MIN_NSEC);
	td_eng_conf_mcefree_var_set(eng, STATUS_R2P_NSEC, min);
#endif
}

static struct td_token* teradimm_hw_init_mcefree_enable(struct td_engine *eng)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	struct td_token *maint_tok;
	int enable;

	maint_tok = td_eng_hw_maint_tok(eng);

	td_eng_trace(eng, TR_FW, "hw-init:mcefree-enable:tok", maint_tok->tokid);

	switch (td->hw_init.slot) {
	case 0:
		/* first step is to enable/disable the MCEFREE mode by writing
		 * to mailbox register */

		enable = teradimm_has_mcefree_support(eng);
		enable = (enable)?1:0;

		td_eng_trace(eng, TR_FW, "hw-init:mcefree-enable:enable", enable);

		td_cmdgen_put_reg(maint_tok->cmd_bytes,
				0xA800, enable, 0xFFFFFFFF);
		td_set_token_timeout(maint_tok, 1000);

		break;

	case 1:
		/* second step is to poke the FW to read the mailbox */

		td_eng_trace(eng, TR_FW, "hw-init:mcefree-enable:intr", 1);

		td_cmdgen_put_reg(maint_tok->cmd_bytes, 0x2000, 1<<24, 1<<24);
		td_set_token_timeout(maint_tok, 1000);

		break;
	}

	return maint_tok;
}
static int teradimm_hw_init_mcefree_enable_done(struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);
	struct td_eng_teradimm *td = td_eng_td_hal(eng);

	WARN_ON_TOK_FAILED(tok);

	td_eng_trace(eng, TR_FW, "hw-init:mcefree-enable:done", tok->tokid);
	td_eng_trace(eng, TR_FW, "hw-init:mcefree-enable:result", tok->result);

	/* this step runs twice */
	++ td->hw_init.slot;
	if (td->hw_init.slot < 2)
		return 0;

	if (teradimm_has_mcefree_support(eng))
		teradimm_mcefree_enable(eng);
	else
		teradimm_mcefree_disable(eng);

	return 1;
}

#endif


struct td_hw_reg_fixup {
	uint32_t reg, value, mask;
};
static struct td_hw_reg_fixup hw_init_reg_fixups[] = {
	{ 0x6001,    1, 0xffffffff },
	{ 0x6002,  425, 0xffffffff },
};

static struct td_token* teradimm_hw_init_fixregs(struct td_engine *eng)
{
	struct td_token *maint_tok;
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	struct td_hw_reg_fixup *fixup = &hw_init_reg_fixups[td->hw_init.slot];

	if (td->hw_init.slot == 0)
		td_eng_info(eng, "Issuing fixups\n");

	td_eng_info(eng, "  [%d] %04x = 0x%x/0x%x\n", td->hw_init.slot,
			fixup->reg, fixup->value, fixup->mask);

	maint_tok = td_eng_hw_maint_tok(eng);

	td_eng_trace(eng, TR_FW, "hw-init:fixup:tok", maint_tok->tokid);
	td_eng_trace(eng, TR_FW, "hw-init:fixup:slot", td->hw_init.slot);
	td_eng_trace(eng, TR_FW, "hw-init:fixup:reg", fixup->reg);

	td_cmdgen_put_reg(maint_tok->cmd_bytes, fixup->reg,
			fixup->value, fixup->mask);

	return maint_tok;
}

static int teradimm_hw_init_fixregs_done(struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);
	struct td_eng_teradimm *td = td_eng_td_hal(eng);

	WARN_ON_TOK_FAILED(tok);

	td_eng_trace(eng, TR_FW, "hw-init:fixregs:done", tok->tokid);
	td_eng_trace(eng, TR_FW, "hw-init:fixregs:result", tok->result);

	if (tok->result == TD_TOK_RESULT_TIMEOUT) {
		td_eng_err(eng, "Couldn't issue PUT_REG, failing\n");
		return -EIO;
	}

	/* We repeat this step for every cancel slot */
	td->hw_init.slot++;
	if (td->hw_init.slot < NUM_ELEMENTS(hw_init_reg_fixups) )
		return 0;

	return 1;
}

static void teradimm_finalize_tok_cmd_with_buffers(struct td_token *tok)
{
	/* TODO: FIXME: this needs to be absorbed into the 'cmdgen' code
	 *   something like td_eng_cmdgen_finalize(tok, cmd_bytes)
	 *   which will update buffers, do the duplication, etc. */

	td_cmd_t *tdcmd = (void*)&tok->cmd_bytes;

	if (tok->len_dev_to_host) { /* read */

		if (TD_IS_CORE_BUFID_VALID(tok->core_bufid))
			tdcmd->dst.bufid = tok->core_bufid;

	} else if (tok->len_host_to_dev) { /* write */

		if (TD_IS_CORE_BUFID_VALID(tok->core_bufid))
			tdcmd->src.bufid = tok->core_bufid;

		if (TD_IS_WR_BUFID_VALID(tok->wr_bufid))
			tdcmd->src.wep = tok->wr_bufid;

	}
}

static struct td_token* teradimm_hw_init_params(struct td_engine *eng)
{
	struct td_token *tok;

	/* allocate a command token with intent to read */
	tok = td_alloc_token_with_host_page(eng, TD_TOK_FOR_FW, 0, PAGE_SIZE);
	if (!tok)
		goto error_alloc_token;

	// td_cmdgen_get_params
	td_eng_cmdgen(eng, get_params, tok->cmd_bytes, 0);

	teradimm_finalize_tok_cmd_with_buffers(tok);

	td_eng_trace(eng, TR_FW, "hw_init:params:tok", tok->tokid);

	return tok;

error_alloc_token:
	td_eng_err(eng, "Failed start param probe.\n");
	return NULL;
}

static int teradimm_hw_init_params_done(struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	union td_param* params;

	WARN_ON_TOK_FAILED(tok);

	td_eng_trace(eng, TR_FW, "hw_init:params:done", tok->tokid);
	td_eng_trace(eng, TR_FW, "hw_init:params:result", tok->result);


	/* we sent a query to read out variables;
	 * if the request fails, the device is DEAD. */
	if (tok->result)
		goto dead;

	td_eng_trace(eng, TR_FW, "hw_init:params:rdbuf", tok->rd_bufid);
	/* 
	 * We have a bit of trickery here... One of the things that
	 * the params can tell us is to reserve core bufers....  But
	 * we may be using one of them..  This frees teh core buffer
	 * and dis-associates it from the token, so it can be reserved if it
	 * needs to be
	 */
	td_end_token_core_usage(tok);
	td_free_core_buffer(eng, tok->core_bufid);
	tok->core_bufid = TD_INVALID_CORE_BUFID;

	params = tok->host_buf_virt;
	// teradimm_dump_params(tok->host_buf_virt, 0);

	td_eng_notice(eng, "TeraDIMM: Firmware HW PARAMS version 0x%08X\n",
				params[PARAM_IDX_PROTOCOL].u32);

	switch (params[PARAM_IDX_PROTOCOL].u32) {
	case PARAM_PAGE0_VERSION_163:
		/*
		 * 163 is the same as 162, but with only versioning
		 * information different
		 */
	case PARAM_PAGE0_VERSION_162:
		if (teradimm_config_engine_hw_conf_162(eng, tok->host_buf_virt))
			goto dead;
		break;

	case PARAM_PAGE0_VERSION_161_SHORT:
	case PARAM_PAGE0_VERSION_161:
		if (teradimm_config_engine_hw_conf_161(eng, tok->host_buf_virt))
			goto dead;
		break;
	case PARAM_PAGE0_VERSION_160_SHORT:
		if (teradimm_config_engine_hw_conf_160(eng, tok->host_buf_virt))
			goto dead;
		break;
	default:
		td_eng_err(eng, "UNKNOWN PARAM VERSION 0x%08X, Cannot continue\n",
				params[PARAM_IDX_PROTOCOL].u32);
		goto dead;
	}

	/*
	 * We keep a private copy, because sometimes we will lie to the
	 * engine, but right now, after params page 0, they are correct */
	td->td_ssd_count = td_eng_conf_hw_var_get(eng, SSD_COUNT);
	td->td_ssd_sector_count = td_eng_conf_hw_var_get(eng, SSD_SECTOR_COUNT);

	if (td_eng_conf_hw_var_get(eng, SPD) )
		return 1;

	/* No SPD, skip next step */
	return 2;

dead:
	td_eng_trace(eng, TR_FW, "fw-probe:vars:DEAD", 0);
	td_run_state_enter(eng, DEAD);
	return -EIO;
}

static struct td_token* teradimm_hw_init_spd(struct td_engine *eng)
{
	struct td_token *tok;

	/* allocate a command token with intent to read */
	tok = td_alloc_token_with_host_page(eng, TD_TOK_FOR_FW, 0, PAGE_SIZE);
	if (!tok)
		goto error_alloc_token;

	// td_cmdgen_get_params
	td_eng_cmdgen(eng, get_params, tok->cmd_bytes, 7);
	
	teradimm_finalize_tok_cmd_with_buffers(tok);

	td_eng_trace(eng, TR_FW, "hw_init:params:tok", tok->tokid);

	return tok;

error_alloc_token:
	td_eng_err(eng, "Failed start SPD probe.\n");
	return NULL;
}

static int teradimm_hw_init_spd_done(struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);

	WARN_ON_TOK_FAILED(tok);

	td_eng_trace(eng, TR_FW, "hw_init:spd:done", tok->tokid);
	td_eng_trace(eng, TR_FW, "hw_init:spd:result", tok->result);


	/* we sent a query to read out variables;
	 * if the request fails, the device is DEAD. */
	if (tok->result)
		goto dead;

	td_eng_trace(eng, TR_FW, "hw_init:spd:rdbuf", tok->rd_bufid);

	if (0xd1ab10 != *(uint32_t*)tok->host_buf_virt) {
		td_eng_warn(eng, "SPD all 0s, ignoring\n");
		td_eng_conf_hw_var_set(eng, SPD, 0);
		return 1;
	} else {
		teradimm_process_spd(eng, PTR_OFS(tok->host_buf_virt, 256));
	}

	return 1;

dead:
	td_eng_trace(eng, TR_FW, "fw-probe:vars:DEAD", 0);
	td_run_state_enter(eng, DEAD);
	return -EIO;
}

struct hw_init_steps {
	struct td_token * (*prep)(struct td_engine*);
	int (*hook)(struct td_token*);
};

#define HW_INIT_STEP(name) { .prep = teradimm_hw_init_##name , .hook = teradimm_hw_init_##name##_done },

static struct hw_init_steps steps[] =
{
	HW_INIT_STEP(reserve)
#ifdef CONFIG_TERADIMM_ARM_RESET_ON_START
	HW_INIT_STEP(arm_reset)
	HW_INIT_STEP(poll)
#endif
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	HW_INIT_STEP(mcefree_detect)
	HW_INIT_STEP(mcefree_enable)
#endif
	HW_INIT_STEP(fixregs)
	HW_INIT_STEP(cancel)
	HW_INIT_STEP(deallocate)
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	HW_INIT_STEP(mcefree_hold_rdbuf0)
#endif
	HW_INIT_STEP(sata)
	HW_INIT_STEP(poll)
	HW_INIT_STEP(params)
	HW_INIT_STEP(spd)
};

static int teradimm_hw_init_step(struct td_engine *eng, int step);

static int teradimm_hw_init_step_completion (struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	int rc;

	td_eng_trace(eng, TR_FW, "hw-init:step:done", td->hw_init.step);

	rc = (steps[td->hw_init.step].hook)(tok);
	td_eng_trace(eng, TR_FW, "hw-init:step:hook", rc);

	if (rc < 0) {
		td_run_state_enter(eng, DEAD);
		goto done;
	} else if (rc == 0) {
		/* Repeat this step */
		td_eng_trace(eng, TR_FW, "hw-init:step:repeat", td->hw_init.step);
	} else {
		/* Increment the hw_init.step */
		td->hw_init.step += rc;
		td->hw_init.slot = 0;
		td_eng_trace(eng, TR_FW, "hw-init:step:next", td->hw_init.step);
	}
	
	/* Get it ready for whoever might be next */
	if (tok == td_eng_fw_maint_tok(eng)
			|| tok == td_eng_hw_maint_tok(eng))
		td_token_reset(tok);

	if (td->hw_init.step >= (sizeof(steps) / sizeof(steps[0]))) {
		if(!td_state_can_enter_running(eng)) {
			td_run_state_enter(eng, UCMD_ONLY);
		}
		else {
			td_run_state_enter(eng, RUNNING);
		}

		/* reconfigure mcefree variables now that we're in running state */
		teradimm_hw_init_mcefree_configure_running(eng);

		goto done;
	}

	if (td_state_can_accept_requests(eng) &&
			td->hw_init.step < (sizeof(steps) / sizeof(steps[0])) ) {
		teradimm_hw_init_step(eng, td->hw_init.step);
	}

done:
	if (tok == td_eng_fw_maint_tok(eng)
			|| tok == td_eng_hw_maint_tok(eng))
		return TD_TOKEN_PRE_COMPLETION_STOLEN;

	return TD_TOKEN_PRE_COMPLETION_DONE;
}

static int teradimm_hw_init_step(struct td_engine *eng, int step)
{
	struct td_token *tok;

	BUG_ON(td_eng_fw_maint_tok(eng) == NULL);
	BUG_ON(td_eng_hw_maint_tok(eng) == NULL);

	td_eng_trace(eng, TR_FW, "hw-init:step:start", step);

	WARN_ON_ACCESS_FROM_WRONG_CPU(eng);

	tok = (steps[step].prep)(eng);

	WARN_ON(!tok);
	if (!tok)
		return -ENODEV;

	if (td_cmd_is_hardware(tok->cmd_bytes))
		tok->hw = 1;

	td_eng_trace(eng, TR_FW, "hw-init:step:tok", tok->tokid);

	tok->ops.pre_completion_hook = teradimm_hw_init_step_completion;
	td_engine_start_token(eng, tok);

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	/*
	 * Slow down the driver after issuing a command; early on we have a
	 * chance of generating MCEs with A0 hardware if we poll the status
	 * too soon.
	 */
	udelay(100);
#endif

	/*
	 * This token is expected to timeout, and we don't care.  But we
	 * want this to timeout quickly
	 */
	if (tok->quick_n_quiet)
		td_set_token_timeout(tok, TD_TOKEN_QUICK_N_QUIET_TIMEOUT);

	return 0;
}

static int teradimm_hw_init_run_steps (struct td_engine *eng)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	enum td_token_type tt;

	/* now resets are done, we can go about our real HW init */
	teradimm_learn_odd_even_bits(eng);

	/* re-start our sequence number */
	eng->td_sequence_next =  start_seq;

	/* Reserve our maint_tok(s) before anything else happens */
	for_each_token_type(tt) {
		if (td_eng_maint_tok(eng,tt))
			continue;

		td_eng_maint_tok(eng,tt) = td_alloc_token(eng, tt);
	}

	WARN_ON(td_eng_fw_maint_tok(eng) == NULL);
	WARN_ON(td_eng_hw_maint_tok(eng) == NULL);
	if (!td_eng_fw_maint_tok(eng)
			|| !td_eng_hw_maint_tok(eng))
		return -ENODEV;

	/* Init hw_init information */
	memset(&td->hw_init, 0, sizeof(td->hw_init));

	return teradimm_hw_init_step(eng, 0);
}

/*
 * perform the read buffer purge
 */
static int teradimm_hw_init_reset_done (struct td_token* tok)
{
	struct td_engine *eng = td_token_engine(tok);

	WARN_ON_TOK_FAILED(tok);

	td_token_reset(tok);

	if (eng->td_stats.control.req_active_cnt == 0)
	{
		/* This is a bit of a kludge, we need this token "finished" before
		 * we can do all our HW init*/
		td_free_all_buffers(eng, tok);
		td_free_token(eng, tok);

		teradimm_hw_init_run_steps(eng);

		return TD_TOKEN_PRE_COMPLETION_STOLEN;

	}

	return TD_TOKEN_PRE_COMPLETION_DONE;

}


static int teradimm_ops_hw_init(struct td_engine *eng)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
#ifdef CONFIG_TERADIMM_ENABLE_FORCE_RESET
	struct td_token *tok, *nxt;
	enum td_token_type tt;
	struct td_token_list *tok_list;
#endif

	WARN_ON_ACCESS_FROM_WRONG_CPU(eng);

	td_run_state_enter(eng, FW_PROBE);

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	
	/* Before we do anything, we disable all the MCEFREE status checking */
	teradimm_mcefree_disable(eng);

	/* reserve the last 8 t_kens before anything starts
	 * these are important for mcefree polling early on */
	if (td_mcefree_enable) {
		int i;
		for (i=248; i<256; i++) {
			tok = td_alloc_token_id(eng, i);
			if (tok) {
				td_free_all_buffers(eng, tok);
				td_token_reset(tok);
				td_tokens_enqueue(&td->td_reserved_tokens, tok);
			}
		}
	}

	/* Anything that was waiting around for a rdbuf is gone now too... */
	td_eng_info(eng, "Clearing early read (%u)\n", td_early_completed_reads(eng));
	for_each_token_list_token(tok, nxt,
			&eng->td_early_completed_reads_tokens) {
			__td_tokens_del(&eng->td_early_completed_reads_tokens, tok);
			td_token_error_dump(tok, "Clear early read");
			td_tokens_enqueue(&eng->tok_pool[TD_TOK_FOR_FW].td_active_tokens, tok);
	}
#endif

#ifdef CONFIG_TERADIMM_ENABLE_FORCE_RESET
	td_eng_notice(eng, "Reset all active tokens (%d)\n", td_all_active_tokens(eng));
	for_each_token_type(tt) {
		tok_list = &eng->tok_pool[tt].td_active_tokens;
		for_each_token_list_token(tok, nxt, tok_list) {
			__td_tokens_del(tok_list, tok);
			td_token_error_dump(tok, "Reset active");

			tok->result = -EIO;

			/* We need to release any active resources */
			if (tok->host.bio)
				td_request_end_common_bio(tok);
			else if (tok->host.ucmd)
				td_request_end_common_ucmd(tok);

			td_free_all_buffers(eng, tok);
			td_token_reset(tok);
			td_tokens_enqueue(&eng->tok_pool[tt].td_free_tokens, tok);

			tok->odd = !tok->odd;
			if (tok == eng->maint_toks[tt])
				eng->maint_toks[tt] = NULL;
		}
	}

	/* release the maintenance tokens */
	for_each_token_type(tt) {
		/* might have already been done */
		tok = eng->maint_toks[tt];
		if (!tok)
			continue;

		eng->maint_toks[tt] = NULL;

		td_free_all_buffers(eng, tok);
		td_token_reset(tok);
		td_tokens_enqueue(&eng->tok_pool[tt].td_free_tokens, tok);
	}

	/*
	 * And now we're about to send coalescor resets, reset our active
	 * counts now in case we forcibly aborted some of them.
	 */
	eng->td_stats.control.req_active_cnt = 0;
	eng->td_stats.read.req_active_cnt = 0;
	eng->td_stats.write.req_active_cnt = 0;

	tok_list = &td->td_reserved_tokens;
	if (tok_list->count) {
		td_eng_info(eng, "Resetting %u reserved tokens\n", tok_list->count);
		for_each_token_list_token(tok, nxt, tok_list) {
			tok->odd = !tok->odd;
			teradimm_ops_reset_token(eng, tok);
			td_token_reset(tok);
			udelay(1);
		}
	}

	td_eng_info(eng, "Resetting free tokens\n");
	for_each_token_type(tt) {
		tok_list = &eng->tok_pool[tt].td_free_tokens;
		for_each_token_list_token(tok, nxt, tok_list) {
			__td_tokens_del(tok_list, tok);
			td_token_reset(tok);
			td_cmdgen_reset(tok->cmd_bytes);
			tok->ops.pre_completion_hook = teradimm_hw_init_reset_done;
			tok->quick_n_quiet = 1;
			td_engine_start_token(eng, tok);
			udelay(1);
		}
	}

	td_eng_info(eng, "Resetting timedout tokens\n");
	for_each_token_type(tt) {
		tok_list = &eng->tok_pool[tt].td_timedout_tokens;
		for_each_token_list_token(tok, nxt, tok_list) {
			__td_tokens_del(tok_list, tok);
			td_token_reset(tok);
			td_cmdgen_reset(tok->cmd_bytes);
			tok->ops.pre_completion_hook = teradimm_hw_init_reset_done;
			tok->quick_n_quiet = 1;
			td_engine_start_token(eng, tok);
			udelay(1);
		}
	}

	td_eng_info(eng, "Resetting resumable tokens\n");
	for_each_token_type(tt) {
		tok_list = &eng->tok_pool[tt].td_resumable_tokens;
		for_each_token_list_token(tok, nxt, tok_list) {
			__td_tokens_del(tok_list, tok);
			td_token_reset(tok);
			td_cmdgen_reset(tok->cmd_bytes);
			tok->ops.pre_completion_hook = teradimm_hw_init_reset_done;
			tok->quick_n_quiet = 1;
			td_engine_start_token(eng, tok);
			udelay(1);
		}
	}
	td_eng_info(eng, "Waiting for completions\n");
#else
	teradimm_hw_init_run_steps(eng);
#endif

	return 0;
}

int teradimm_ops_online(struct td_engine *eng)
{
	int rc;
	struct td_ucmd *ucmd;
	uint64_t conf_value;
	teradimm_global_status_t gstatus;
	
	rc = td_eng_hal_read_ext_status(eng, TERADIMM_EXT_STATUS_GLOBAL_IDX, &gstatus, 0);
	if ( rc || !TD_GLOBAL_STATUS_PERFECT(gstatus.fw) ) {
		struct td_fw_state_info *info;

		td_eng_err(eng, "Cannot configure TeraDIMM for online\n");

		info = teradimm_fw_state_info(gstatus.fw);

		if (info && info->name)
			td_eng_err(eng, "HW: Global status 0x%02X: %s\n",
					gstatus.fw, info->name);
		else
			td_eng_err(eng, "HW: Global status 0x%02X\n",
					gstatus.fw);

		if (info && info->warning)
			td_eng_err(eng, "   * * * * %s * * * *\n",
					info->warning);

		return -EIO;
	}


	ucmd= td_ucmd_alloc(TERADIMM_DATA_BUF_SIZE);
	if (!ucmd)
		return -ENOMEM;

	ucmd->ioctl.data_len_from_device = TERADIMM_DATA_BUF_SIZE;
	rc = td_eng_cmdgen(eng, get_params, ucmd->ioctl.cmd, 1);
	if (rc) {
		td_eng_err(eng, "Could not gencmd get_params, rc = %d\n", rc);
		goto error;
	}
	rc = td_ucmd_run(ucmd, eng);
	if (rc) {
		td_eng_err(eng, "Could not run get_params, rc = %d\n", rc);
		goto error;
	}

	teradimm_config_engine_conf(eng, ucmd->data_virt);

	rc = -EMEDIUMTYPE;
	conf_value = td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE);

	switch (conf_value) {
	case 0x7fffffff:
		td_eng_notice(eng, "Using 512B sectors on uninitialized device\n");
		/* Uninititilzed, we default to 512 */
		td_eng_conf_hw_var_set(eng, BIO_SECTOR_SIZE, 512);
		break;

	case 512:
		break;
	case 4096:
#if  defined(KABI__4k_support)
		break;
#endif
		/* If 4K isn't supported, fall through is expected */
	default:
		td_eng_err(eng, "Device %s is using an unsupported block size "
				"%llu; Please check your device formatted "
				"block size.\n",
				td_eng_name(eng), conf_value);
		goto inval;
	}

	rc = 0;

	conf_value = td_eng_conf_hw_var_get(eng, SSD_STRIPE_LBAS);

	switch (conf_value) {
	case 8:
	case 16:
	case 32:
	case 64:
		break;

	default:
		td_eng_err(eng, "Invalid SSD_STRIP_LBAS %llu; Setting to 8\n",
				td_eng_conf_hw_var_get(eng, SSD_STRIPE_LBAS));
		td_eng_conf_hw_var_set(eng, SSD_STRIPE_LBAS, 8);

	}
	
	conf_value = td_eng_conf_hw_var_get(eng, E2E_MODE);
	switch (conf_value) {
	case TD_E2E_MODE_OFF:
		break;
	case TD_E2E_MODE_512B:
	case TD_E2E_MODE_4kB:
		td_eng_err(eng, "Obsolete E2E_MODE 0x%llx is not supported; Please reformat your device.\n",
				conf_value);
	default:
		td_eng_err(eng, "Device %s using unsupported E2E mode.\n",
				td_eng_name(eng));
		rc = -EMEDIUMTYPE;
		goto error;
	}

	/*
	 * Now verify which copy ops to  use
	 * We only need to watch for a 520B device (metadata == 8), that is
	 * configured with 512B BIO sectors
	 */
	eng->td_bio_copy_ops = td_token_copy_ops_bio;
	conf_value = td_eng_conf_hw_var_get(eng, HW_SECTOR_METADATA);
	switch (conf_value) {
	default:
		td_eng_err(eng, "Invalid HW_SECTOR_METADATA %llu; Ignoring metadata\n",
				conf_value);
		/* Fall-through desired */
	case 0:
		break;
	case 8:
		/* BIO_SECTOR_SIZE has already been verified */
		if ( td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE) == 512 ) {
			/* We're doing 512B BIOs, w/ metadata */
			td_eng_info(eng, "Metadata detcted; using 520B padded copy_ops\n");
			eng->td_bio_copy_ops = td_token_copy_ops_bio_520B;
		}
	}

	/* Trim/Discard config*/
	conf_value = td_eng_conf_hw_var_get(eng, DISCARD);
	switch (conf_value) {
	case 0x01:
		td_eng_conf_hw_var_set(eng, DISCARD, 1);
		break;
	case 0x00:
	default:
		td_eng_conf_hw_var_set(eng, DISCARD, 0);
		break;
	}

	/* One thing left - make sure the cache flush method works with our
	 * current BIO/METADATA/E2E configuration\
	 */
	conf_value = td_eng_conf_var_get(eng, CLFLUSH);
	if (td_conf_validate_cache(eng, conf_value)) {
		td_eng_err(eng, "Invalid CLFLUSH setting for this device; forcing safe values\n");
		td_eng_conf_var_set(eng, CLFLUSH, TERADIMM_SAFE_CONF_CLFLUSH);

	}

error:
	if (ucmd)
		td_ucmd_put(ucmd);
inval:
	return rc;
}



/** cleanup */
static int teradimm_ops_exit(struct td_engine *eng)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);

	if (td) {
		td->td_mapper = NULL;
		kfree(td);
	}

	return 0;
}

/** toggle polarity (odd/even) */
static int teradimm_ops_reverse_cmd_polarity(struct td_engine *eng,
		struct td_token *tok)
{
	td_cmd_t *tdcmd = (void*)&tok->cmd_bytes;

	tok->odd = !tok->odd;
	td_cmd_set_odd(tdcmd, tok->odd);

	return 0;
}


static int teradimm_ops_read_status(struct td_engine *eng
#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
			, enum td_token_type
#endif
		);

#ifdef CONFIG_TERADIMM_MCEFREE_STATUS_POLLING

static int teradimm_mcefree_enable(struct td_engine *eng)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	int support;
	uint64_t last_sema_value;

	support = teradimm_has_mcefree_support(eng);
	if (!support)
		return 0;

	/* already enabled? */
	if (td_eng_conf_var_get(eng, MCEFREE_FWSTATUS) == support)
		return 1;

	/* reset state, but we need to keep the old sema value that
	 * was detected */
	last_sema_value = td->mcefree.last_sema_value;
	memset(&td->mcefree, 0, sizeof(td->mcefree));
	td->mcefree.last_sema_value = last_sema_value;

	/* one WEP is stolen for status */
	switch (support) {
	default:
		td_eng_warn(eng, "MCEFREE unrecognized mode=%u\n",
				support);
		return 0;

	case TD_MCEFREE_FWSTATUS_WEP7:
		td_eng_info(eng, "MCEFREE enabled using WEP7\n");

		if (td_eng_conf_var_get(eng, HOST_WRITE_BUFS) > 7)
			td_eng_conf_var_set(eng, HOST_WRITE_BUFS, 7);

		break;

	case TD_MCEFREE_FWSTATUS_RDBUF0:
		td_eng_info(eng, "MCEFREE enabled using RDBUF0\n");

		td_remove_rdbuf_from_circulatiion(eng, 0);

		break;
	}

	/* this enables status in WEP-7 */
	td_eng_conf_var_set(eng, MCEFREE_FWSTATUS, support);

	/* mcefree implies SEC */
	td_eng_conf_var_set(eng, EARLY_COMMIT, TD_SUPER_EARLY_COMMIT);
	td_eng_conf_var_set(eng, DOUBLE_WEP_WRITES, 2);


	return 1;
}

static void teradimm_mcefree_disable(struct td_engine *eng)
{
	/* already disabled? */
	if (! td_eng_conf_var_get(eng, MCEFREE_FWSTATUS))
		return;

	td_eng_warn(eng, "MCEFREE disabled\n");

	/* disable */
	td_eng_conf_var_set(eng, MCEFREE_FWSTATUS, 0);
	td_eng_conf_var_set(eng, EARLY_COMMIT, TD_QUEUED_COMMIT);

	/* NOTE: HOST_WRITE_BUFS are not restored to previous value, because
	 * it's not known.  This could be easily fixed by keeping track of the
	 * old value in mcefree_enable(). */
}

static int teradimm_mcefree_is_enabled(struct td_engine *eng)
{
	if (!td_eng_conf_var_get(eng, MCEFREE_FWSTATUS))
		return 0;

	if (!td_state_can_query_mcefree_status(eng))
		return 0;

	return 1;
}


/* ------------------------------------------------------------------------ */

/* tried reading fwstatus semaphore, but found stale data */
static void mcefree_fwstatus_sema_error_occured(struct td_engine *eng)
{
	uint64_t val, max;

	eng->td_counters.misc.fwstatus_sema_misses_cnt ++;
	eng->td_last_fwstatus_sema_error = td_get_cycles();

	/* current value */
	val = td_eng_conf_mcefree_var_get(eng, STATUS_R2P_NSEC);

	/* climb because we got an error */
	val += td_eng_conf_mcefree_var_get(eng, STATUS_R2P_CLIMB_NSEC);

	/* cap it at the ceiling */
	max = td_eng_conf_mcefree_var_get(eng, STATUS_R2P_MAX_NSEC);
	val = min_t(uint64_t, val, max);

	/* put it back */
	td_eng_conf_mcefree_var_set(eng, STATUS_R2P_NSEC, val);

	/* next time, we will wait longer */
}

/* fwstatus semaphore read correctly on first try */
static void mcefree_fwstatus_sema_successfully_read(struct td_engine *eng)
{
	cycles_t now, diff;
	uint64_t msec = 0, cooling;
	uint64_t val, min;

	now = td_get_cycles();
	diff = now - eng->td_last_fwstatus_sema_error;
	msec = td_cycles_to_msec(diff);
	cooling = td_eng_conf_mcefree_var_get(eng, STATUS_R2P_COOL_MSEC);

	/* if cool-off has not been reached, do nothing */
	if (cooling && msec < cooling)
		return;

	/* cooling off */

	/* current value */
	val = td_eng_conf_mcefree_var_get(eng, STATUS_R2P_NSEC);

	/* drop the hold time */
	val -= td_eng_conf_mcefree_var_get(eng, STATUS_R2P_DROP_NSEC);

	/* cap it at the floor */
	min = td_eng_conf_mcefree_var_get(eng, STATUS_R2P_MIN_NSEC);
	val = max_t(int64_t, val, min);

	/* put it back */
	td_eng_conf_mcefree_var_set(eng, STATUS_R2P_NSEC, val);

	/* next time, we will wait for less time */

	/* also don't decrement right away */
	eng->td_last_fwstatus_sema_error = now;
}

/* ------------------------------------------------------------------------ */


#define TD_MCEFREE_REQ_JUST_SENT 0
#define TD_MCEFREE_REQ_SENT_EARLIER 1
/* return 0 if sent request
 * return 1 if no request sent, one is pending
 * return negative on error */
static int teradimm_mcefree_send_request(struct td_engine *eng)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	void *hw_cmd_buf;
	void *hw_wep7 = NULL;
	uint64_t *hw_sema = NULL;
	struct td_token *tok;
	td_cmd_t *tdcmd;
	cycles_t now, diff;
	uint64_t nsec, max;
	int alias;

	if (!teradimm_mcefree_is_enabled(eng))
		return -EIO;

	/* if a request was already sent out, skip it this time */

	if (eng->td_last_fwstatus_request_posted)
		return TD_MCEFREE_REQ_SENT_EARLIER;

	/* FWSTATUS is not always safe to issue, hold back */

	if (td_engine_hold_back_fwstatus_request(eng))
		return -EBUSY;

	/* don't check too soon after last request completed */

	now = td_get_cycles();
	diff = now - td->mcefree.last_updates_completed;
	nsec = td_cycles_to_nsec(diff);
	max = td_eng_conf_mcefree_var_get(eng, STATUS_R2R_NSEC);
	if (nsec < max)
		return -EBUSY;

	/* get the location of the reserved WEP */

	td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:request:start", 0);

	switch (teradimm_has_mcefree_support(eng)) {
	case TD_MCEFREE_FWSTATUS_WEP7:
		alias = 0;
		hw_wep7 = teradimm_map_write_data_buf(td->td_mapper, alias, 7);
		hw_sema = PTR_OFS(hw_wep7, TERADIMM_WEP7_SEMAPHORE_OFS);
		break;

	default:
		break;
	}

	/* allocate a token, if that's not possible, fallback */
	tok = td_tokens_dequeue(&td->td_reserved_tokens);
	if (!tok) {
		td_eng_warn(eng, "MCEFREE: no more reserved tokens\n");
		return -ENXIO;
	}

	if (TE_INJECT(eng, TE_SKIP_FWSTATUS)) {
		eng->td_errors_count --;
		td_eng_info(eng, "EI: Skipping fwstatus request\n");
		td_eng_trace(eng, TR_TOKEN, "EI:mcefree:skip_request", tok->tokid);

		__td_tok_event(tok, -1, -1, TD_TOK_EVENT_START, -1);

	} else {
		/* command is constructed */
		td_cmdgen_put_reg(tok->cmd_bytes, 0x2000, 1<<30, 1<<30);
		/* Finalize ourself, because we don't do finalize cmdgen_finalize */
		tdcmd = (void*)&tok->cmd_bytes;
		tdcmd->cmd.index = tok->tokid;
		/* set polarity to whatever is next */
		teradimm_ops_reverse_cmd_polarity(eng, tok);

#if 0
		td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:request:tok    ", tok->tokid);
		td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:request:odd    ", tok->odd);
		td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:request:cmd    ", tdcmd->cmd.u64);
		td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:request:src    ", tdcmd->src.u64);
		td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:request:dst    ", tdcmd->dst.u64);
		td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:request:pad0   ", tdcmd->pad0.u64);
		td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:request:pad1   ", tdcmd->pad1.u64);
		td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:request:pad2   ", tdcmd->pad2.u64);
		td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:request:xsum0  ", tdcmd->chksum0.u64);
		td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:request:xsum1  ", tdcmd->chksum1.u64);
#endif

		td_tok_event(tok, TD_TOK_EVENT_START, 0);

		td_fwstatus_start(td);

		/* send the command */
		alias = 0;
		hw_cmd_buf = teradimm_map_cmd_buf(td->td_mapper, alias, tok->tokid);
		td_memcpy_8x8_movnti(hw_cmd_buf, tok->cmd_bytes, TERADIMM_COMMAND_SIZE);
	}

	td->mcefree.last_updates_completed = 0;
	eng->td_last_fwstatus_request_posted = td_get_cycles();
	td->mcefree.last_sema_poll = 0;
	td->mcefree.count_sema_poll = 0;
#ifdef CONFIG_TERADIMM_TOKEN_HISTORY
	td->mcefree.last_gpio_tok = tok;
#endif

	td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:request:sent:tok", tok->tokid);

	/* it's sent, so put token back */

	td_tokens_enqueue(&td->td_reserved_tokens, tok);

	td->mcefree.count_request_posted ++;

	eng->td_last_hardware_command = td_get_cycles();

	return TD_MCEFREE_REQ_JUST_SENT;
}

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
void teradimm_force_read_real_status(struct td_engine *eng, uint tokid, uint8_t *byte)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	void *hw_status_buf;
	int alias;

	td_eng_trace(eng, TR_TOKEN, "TD:real_status:tok  ", tokid);

	alias = 0;
	hw_status_buf = teradimm_map_status(td->td_mapper, alias);

	if (TD_MAPPER_TYPE_TEST(STATUS, WB)) {
		clflush_cache_range(hw_status_buf + tokid, 1);
	}

	memcpy(byte, hw_status_buf + tokid, 1);

	td_eng_trace(eng, TR_TOKEN, "TD:real_status:byte ", *byte);
}
void teradimm_force_read_all_real_status(struct td_engine *eng, uint8_t *bytes)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	void *hw_status_buf;
	int alias;

	alias = 0;
	hw_status_buf = teradimm_map_status(td->td_mapper, alias);

	if (TD_MAPPER_TYPE_TEST(STATUS, WB)) {
		clflush_cache_range(hw_status_buf, TERADIMM_COMMAND_MAX);
		memcpy(bytes, hw_status_buf, TERADIMM_COMMAND_MAX);

	} else {
#if TERADIMM_COMMAND_MAX&63ULL
#error TERADIMM_COMMAND_MAX must be a multiple of 64
#endif
		if (((uintptr_t)bytes & 63ULL) == 0) {
			td_memcpy_movntdqa_64(bytes,
					hw_status_buf, TERADIMM_COMMAND_MAX);
		} else {
			td_memcpy_movntdqa_64(td->mcefree.scratch,
					hw_status_buf, TERADIMM_COMMAND_MAX);
			memcpy(bytes, td->mcefree.scratch, TERADIMM_COMMAND_MAX);
		}
	}
}
#endif

/* return 0 if status is new
 * return 1 if status is stale
 * return negative on error */
static int teradimm_mcefree_poll_sema(struct td_engine *eng)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	void *hw_fwstatus;
	uint64_t *hw_sema, v;
	int sema_ready, cached_mapping = 0;
	size_t size;
	cycles_t now, diff;
	uint64_t nsec_r2p, nsec_p2p, max;
#if 0
#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
	uint8_t *hw_status = NULL;
	unsigned hw_start, hw_size;
#endif
#endif
#ifdef CONFIG_TERADIMM_FWSTATUS_HISTORY
	struct td_fwstatus_debug_layout *fw_debug;
#endif

	if (!teradimm_mcefree_is_enabled(eng))
		return -EIO;

	/* don't check too soon after dispatch */

	now = td_get_cycles();
	diff = now - eng->td_last_fwstatus_request_posted;
	nsec_r2p = td_cycles_to_nsec(diff);
	max = td_eng_conf_mcefree_var_get(eng, STATUS_R2P_NSEC);
	if (nsec_r2p < max)
		return TD_ENG_STATUS_STALE;

#if 0
	td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:poll:check", usec);
#endif

	/* don't check too soon after last poll */

	if (td->mcefree.last_sema_poll) {
		now = td_get_cycles();
		diff = now - td->mcefree.last_sema_poll;
		nsec_p2p = td_cycles_to_nsec(diff);
		max = td_eng_conf_mcefree_var_get(eng, STATUS_P2P_NSEC);
		if (nsec_p2p < max)
			return TD_ENG_STATUS_STALE;
	}

	switch (teradimm_has_mcefree_support(eng)) {
	case TD_MCEFREE_FWSTATUS_WEP7:

		/* get the location of the reserved WEP */

		hw_fwstatus = teradimm_map_write_data_buf(td->td_mapper, 0, 7);
		hw_sema     = PTR_OFS(hw_fwstatus, TERADIMM_WEP7_SEMAPHORE_OFS);
		cached_mapping = TD_MAPPER_TYPE_TEST(WRITE_DATA, WB);

		break;

	case TD_MCEFREE_FWSTATUS_RDBUF0:

		/* get the location of the reserved RDBUF */

		hw_fwstatus = teradimm_map_read_data_buf(td->td_mapper, 0, 0);
		hw_sema     = PTR_OFS(hw_fwstatus, TERADIMM_WEP7_SEMAPHORE_OFS);
		cached_mapping = TD_MAPPER_TYPE_TEST(READ_DATA, WB);

		break;

	default:
		return -EIO;
	}

	td->mcefree.count_sema_poll ++;

	if (unlikely (td->ei.next_sema_poll) ) {
		/* Make sure, because we're skipping it */
		sema_ready = 0;

		/* Are we supposed to not see it yet? */
		if (now < td->ei.next_sema_poll)
			goto EI_skip_poll;

		/* Now it's time to let the poll through again */
		td->ei.next_sema_poll = 0;
	}

	if (TE_INJECT(eng, TE_DELAY_SEMA) ) {
		eng->td_errors_count--;
		td_eng_info(eng, "EI: Delaying sema update\n");

		td->ei.next_sema_poll = now + td_usec_to_cycles(1000000);
		td_eng_trace(eng, TR_MCEFREE, "EI:mcefree:delay", td->ei.next_sema_poll);

		/* And now act like it never happened */
		sema_ready = 0;
		goto EI_skip_poll;
	}

	/* flush the location */
	if (cached_mapping) {
		mb();
		clflush(hw_sema);
		mb();
	}

	/* read the location */
	v = *(volatile uint64_t*)hw_sema;

	switch (td_eng_conf_var_get(eng, MCEFREE_FWSTATUS)) {
	case TD_MCEFREE_FWSTATUS_RDBUF0:

		/* rdbuf0 semaphore becomes ready if it flips in polarity */

		sema_ready = ( v==0 && td->mcefree.last_sema_value==1 )
			  || ( v==1 && td->mcefree.last_sema_value==0 );

		break;

	default:

		/* wep7 semaphore becomes ready if it's 1 */

		sema_ready = (v == 1);

		break;
	}

	/* record the last value read */
	td->mcefree.last_sema_value = v;

EI_skip_poll:
	/* limit spinning to something reasonable */

	if (!sema_ready) {
		/* semaphore didn't flip in STATUS_R2P_NSEC / STATUS_P2P_NSEC
		 * time, we keep polling the semaphore for STATUS_R2P_TIMEOUT_USEC
		 * time. */
		td->mcefree.last_sema_poll = td_get_cycles();
		mcefree_fwstatus_sema_error_occured(eng);

		max = td_eng_conf_mcefree_var_get(eng, STATUS_R2P_TIMEOUT_NSEC);
		if (nsec_r2p > max) {
			/* we have been reading the semaphore for STATUS_R2P_TIMEOUT_USEC
			 * and didn't observe a flip.  resetting the counters
			 * below will cause a retry of the GPIO request. */
			eng->td_counters.misc.fwstatus_sema_timeout_cnt ++;

			if (!td_run_state_check(eng, FW_PROBE)
					&& td_eng_conf_mcefree_var_get(eng,
						HALT_ON_STATUS_TIMEOUT)) {
				/* if the configuration is set to halt the
				 * driver on a terminating event do so now */
				td_run_state_enter(eng, DEAD);
				return TD_ENG_STATUS_GOOD;
			}

			td->mcefree.last_updates_completed = eng->td_last_fwstatus_request_posted;
			eng->td_last_fwstatus_request_posted = 0;
			td->mcefree.count_sema_poll = 0;
			td->mcefree.count_retries ++;

			td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:poll:r2p-timeout",
					td->mcefree.count_retries);

			max = td_eng_conf_mcefree_var_get(eng, STATUS_R2P_RETRY_MAX);
			if (td->mcefree.count_retries > max) {
				/* there were STATUS_R2P_RETRY_MAX retries,
				 * due to timeouts of STATUS_R2P_TIMEOUT_USEC
				 * each.  still no update on the semaphore. */
				td_eng_warn(eng, "no status update in %llu usec, "
						"on %u consecutive retry.\n",
						nsec_r2p/1000, td->mcefree.count_retries);

				eng->td_counters.misc.fwstatus_sema_timeout_max_cnt ++;

#ifdef CONFIG_TERADIMM_TOKEN_HISTORY
				td_tok_event(td->mcefree.last_gpio_tok, TD_TOK_EVENT_END,
						0xFFFF);
				td->mcefree.last_gpio_tok = NULL;
#endif

				/* this status is not ready, and we will not
				 * read it, but we tell the upper layer that
				 * it was done so that it can timeout any
				 * stale commands */
				td->mcefree.count_retries = 0;
				td->td_status_ts[TD_TOK_FOR_FW] = td_get_cycles();
				return TD_ENG_STATUS_GOOD;
			}
		}
		return TD_ENG_STATUS_STALE;
	}

#ifdef CONFIG_TERADIMM_TOKEN_HISTORY
	td_tok_event(td->mcefree.last_gpio_tok, TD_TOK_EVENT_END,
			td->mcefree.count_retries);
	td->mcefree.last_gpio_tok = NULL;
#endif

	/* sema was updated, now we can read status */
	td->td_status_ts[TD_TOK_FOR_FW] = eng->td_last_fwstatus_request_posted;

	if (!td->mcefree.count_retries)
		mcefree_fwstatus_sema_successfully_read(eng);
	eng->td_counters.misc.fwstatus_sema_success_cnt ++;

	eng->td_last_fwstatus_request_posted = 0;
	td->mcefree.last_updates_completed = td_get_cycles();
	td->mcefree.count_retries = 0;

	td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:poll:count",
			td->mcefree.count_sema_poll);

	size = td_eng_conf_var_get(eng, TOKENS);
	size = (size+63ULL) & ~63ULL; /* round up to 64 */
	size = min_t(unsigned, size, TD_TOKENS_PER_DEV);

#if 0
#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
	hw_start = td_eng_conf_var_get(eng, HW_TOKID_START);
	if (unlikely(hw_start > size))
		hw_start = 0;
	if (likely(hw_start > 0)) {
		/* split the total token count into wep7 and hardware status */
		hw_size = size - hw_start;
		size = hw_start;

		/* copy the hardware status from the real status buffer */

		alias = 0;
		hw_status = teradimm_map_status(td->td_mapper, alias);

		clflush_cache_range(hw_status + hw_start, hw_size);
		memcpy(eng->td_status + hw_start,
				hw_status + hw_start,
				hw_size);
	} else {
		td_eng_warn(eng, "skipped hw status read due to bad config\n");
	}
#endif
#endif

	/* copy the status */


#ifdef CONFIG_TERADIMM_DIRECT_STATUS_ACCESS
#error for mcefree, direct status is not possible
#endif

	if (cached_mapping) {
		clflush_cache_range(hw_fwstatus, size);
		memcpy(eng->td_status, hw_fwstatus, size);
	} else {
		td_memcpy_movntdqa_64(eng->td_status, hw_fwstatus, size);
	}

	/* the status was read, clear out the semaphore */
	switch (teradimm_has_mcefree_support(eng)) {
	case TD_MCEFREE_FWSTATUS_WEP7:
		memset(hw_sema, 0, 64);
		if (cached_mapping) {
			mb();
			clflush(hw_sema);
			mb();
		}
		break;

	default:
		break;
	}

#ifdef CONFIG_TERADIMM_FWSTATUS_HISTORY
	WARN_ON_ONCE(size < 256);
	fw_debug = (void*)&eng->td_status[128];
	td_fwstatus_complete(td,
			td->mcefree.count_sema_poll,
			td->mcefree.count_retries,
			fw_debug);
#endif


	td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:poll:done    ", 0);

	td->mcefree.count_updates_completed ++;
	td->mcefree.count_request_posted = 0;
	td->mcefree.count_sema_poll = 0;

	return TD_ENG_STATUS_GOOD;
}

void td_fwstatus_dump(struct td_engine *eng)
{
#ifdef CONFIG_TERADIMM_FWSTATUS_HISTORY
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	int n;
	struct td_fwstatus_hist *hist, *first = NULL;
	uint64_t nsec1, nsec2;

	for (n=0; n<CONFIG_TERADIMM_FWSTATUS_HISTORY; n++) {
		int idx = (td->fwstatus_hist_now + n)
			% CONFIG_TERADIMM_FWSTATUS_HISTORY;
		hist = td->fwstatus_hist + idx;

		if (!hist->ts_gpio)
			continue;
		if (!hist->ts_status) {
			first = hist;
			continue;
		}

		nsec1 = td_cycles_to_nsec(hist->ts_gpio);
		nsec2 = td_cycles_to_nsec(hist->ts_status);
		nsec2 -= nsec1;

		td_eng_err(eng,
			"%llu.%03u  d=%llu.%03u  p=%u r=%u  g=%llu hb=%u ts={%u,%u,%u,%u}\n",

			nsec1/1000LLU, (unsigned)(nsec1%1000),
			nsec2/1000LLU, (unsigned)(nsec2%1000),

			hist->polls,
			hist->retries,

			hist->fw_debug.fw_fwstatus_generation,
			hist->fw_debug.fw_cmdtask_heartbeet,

			hist->fw_debug.fw_cmd_isr_ts,
			hist->fw_debug.fw_ssd0_cmd_ts,
			hist->fw_debug.fw_ssd1_cmd_ts,
			hist->fw_debug.fw_other_cmd_ts);
	}

	if (first) {
		nsec1 = td_cycles_to_nsec(first->ts_gpio);

		td_eng_err(eng,
			"%llu.%03u  incomplete p=%u r=%u\n",

			nsec1/1000LLU, (unsigned)(nsec1%1000),

			td->mcefree.count_sema_poll,
			td->mcefree.count_retries);
	}
#endif
}


#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
static int teradimm_mcefree_read_hw_status(struct td_engine *eng)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	uint8_t *hw_status = NULL;
	unsigned size, hw_start, hw_size;
	int alias = 0;

	size = td_eng_conf_var_get(eng, TOKENS);
	size = min_t(unsigned, size, TD_TOKENS_PER_DEV);

	hw_start = td_eng_conf_var_get(eng, HW_TOKID_START);
	if (unlikely(hw_start > size)) {
		td_eng_warn(eng, "bad config reading hw status: hw_start(%d) > size (%d)\n",
				hw_start, size);
		hw_start = size/2;
	}

	hw_status = PTR_OFS(teradimm_map_status(td->td_mapper, alias), hw_start);
	hw_start  = hw_start;
	hw_size   = size - hw_start;

	if (TD_MAPPER_TYPE_TEST(STATUS, WB)) {
		clflush_cache_range(hw_status, hw_size);
		memcpy(eng->td_status + hw_start, hw_status, hw_size);
	} else {
		td_memcpy_movntdqa_64(eng->td_status + hw_start, hw_status, hw_size);
	}

	td->td_status_ts[TD_TOK_FOR_HW] = td_get_cycles();
	/* always good */
	return TD_ENG_STATUS_GOOD;
}
#endif

static int teradimm_non_mcefree_read_status(struct td_engine *eng);

static int teradimm_mcefree_read_status(struct td_engine *eng,
		enum td_token_type tt)
{
	int rc;

#if 0
	td_eng_trace(eng, TR_MCEFREE, "TD:mcefree:read_status", tt);
#endif

#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
	if (tt == TD_TOK_FOR_HW) {
		if (td_engine_hold_back_status_access(eng))
			return TD_ENG_STATUS_STALE;

		/* request for reading hardware status only */
		return teradimm_mcefree_read_hw_status(eng);
	}
#endif

	rc = teradimm_mcefree_send_request(eng);
	switch (rc) {
	case TD_MCEFREE_REQ_JUST_SENT:
		/* just sent, so there is no point of polling sema */
		return TD_ENG_STATUS_STALE;
	case TD_MCEFREE_REQ_SENT_EARLIER:
		/* we are good to poll the semaphore now */
		break;
	case -EBUSY:
		/* request for status poll is coming in too fast */
		return TD_ENG_STATUS_STALE;
	default:
		return rc;
	}

	rc = teradimm_mcefree_poll_sema(eng);

	return rc;
}
#endif

static int teradimm_non_mcefree_read_status(struct td_engine *eng)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	size_t size;
	void *hw_status_buf;
	int alias;

	size = td_eng_conf_var_get(eng, TOKENS);
	size = min(size, sizeof(td_status_t) * TD_TOKENS_PER_DEV);

	alias = 0;
#ifdef CONFIG_TERADIMM_DYNAMIC_STATUS_ALIASING
	{
	int max_aliases = (int)td_eng_conf_var_get(eng, HOST_STATUS_ALIASES);
	if (max_aliases > 0) {
		if (td_eng_conf_var_get(eng, WBINVD) & TD_WBINVD_STATUS_ALIAS) {
			/* invalidate all cache after status aliases run out */
			alias = td->next_status_alias;
			if ( alias >= max_aliases ) {
				/* reset status *and* read aliases */
				memset(td->next_read_page_alias, 0,
						sizeof(td->next_read_page_alias));
				td->next_status_alias = 0;
				wbinvd();
				alias = 0;
			}
			td->next_status_alias++;
		} else {
			unsigned next;
			alias = td->next_status_alias;
			next = (alias + 1) % max_aliases;
			td->next_status_alias = next;
		}
		td_eng_trace(eng, TR_TOKEN, "TD:status:alias          ", alias);
	}
	}
#endif

	hw_status_buf = teradimm_map_status(td->td_mapper, alias);

#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
	td->td_status_ts[TD_TOK_FOR_FW] = td_get_cycles();
	td->td_status_ts[TD_TOK_FOR_HW] = td->td_status_ts[TD_TOK_FOR_FW];
#else
	td->td_status_ts[TD_TOK_SAME_TYPE_FOR_EVERYTHING] = td_get_cycles();
#endif


#ifdef CONFIG_TERADIMM_DIRECT_STATUS_ACCESS
	/* flush, set pointer to the hardware status buffer, then prefetch */
	td_cache_flush(eng, PRE, STATUS, hw_status_buf, size);
	eng->td_status = (uint8_t*)hw_status_buf;
	__builtin_prefetch(hw_status_buf);
	td_cache_flush(eng, POST, STATUS, hw_status_buf, size);
#else
	if (TD_MAPPER_TYPE_TEST(STATUS, WB)) {
		/* flush, then copy using streaming memcpy */
		td_cache_flush(eng, PRE, STATUS, hw_status_buf, size);
		memcpy(eng->td_status, hw_status_buf, size);
		td_cache_flush(eng, POST, STATUS, hw_status_buf, size);
	} else {
		/* cache is off, read using movntdqa */
		td_memcpy_movntdqa_64(eng->td_status, hw_status_buf, size);
	}
#endif

	td_eng_dump_status_changes(eng);

	return TD_ENG_STATUS_GOOD;
}

/** read status entries from hw into buffer */
static int teradimm_ops_read_status(struct td_engine *eng
#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
			, enum td_token_type tt
#endif
		)
{
#ifdef CONFIG_TERADIMM_MCEFREE_STATUS_POLLING
	if (teradimm_mcefree_is_enabled(eng))
		return teradimm_mcefree_read_status(eng, tt);
#endif

	return teradimm_non_mcefree_read_status(eng);
}

static int teradimm_ops_read_ext_status_local(struct td_engine *eng, int idx,
		void* dst)
{
	struct td_eng_teradimm *td;
	size_t size;
	void *hw_status_buf;
	int alias;

	WARN_ON(!eng);
	if (!eng) return -ENODEV;

	WARN_ON_ACCESS_FROM_WRONG_CPU(eng);

	td = td_eng_td_hal(eng);
	WARN_ON(!td);

	alias = 0;
	size = 8;
	
	/*
	 * We are looking for  the extended status of just this
	 * command token
	 */
#if 0
	printk("EXT STATUS: %u\n", idx);
	printk(" - ptr %p\n", teradimm_map_ext_status(td->td_mapper, alias));
	printk(" - ofs %lu\n", size * idx);
#endif	
	hw_status_buf = teradimm_map_ext_status(td->td_mapper, alias);
	hw_status_buf = PTR_OFS( hw_status_buf, size * idx);

	if (TD_MAPPER_TYPE_TEST(EXT_STATUS, WB)) {
		td_cache_flush(eng, PRE, STATUS, hw_status_buf, size);
		memcpy(dst, hw_status_buf, size);
		td_cache_flush(eng, POST, STATUS, hw_status_buf, size);
	} else {
		memcpy(dst, hw_status_buf, size);
	}

	return 0;
}

struct teradimm_read_ext_status_work_state {
	struct td_eng_thread_work work;
	void* dst;
	int idx;
};

static int teradimm_read_ext_status_callback(struct td_engine *eng, void* data)
{
	struct teradimm_read_ext_status_work_state *state = container_of(data, struct teradimm_read_ext_status_work_state, work);
	//printk("SAFE: read_ext_status_callback: %p (%p)\n", data, state);

	return teradimm_ops_read_ext_status_local(eng, state->idx, state->dst);
}


static int teradimm_ops_read_ext_status(struct td_engine *eng, int idx,
		void* dst, int guaranteed_to_be_local_cpu_access)
{
	struct teradimm_read_ext_status_work_state gsr_work = {
		.work.func = teradimm_read_ext_status_callback,
		.work.name = "GSR_callback",
		.idx = idx,
		.dst = dst,
	};

	return td_eng_safe_work(eng, &gsr_work.work);
}

int teradimm_ops_create_cmd(struct td_engine *eng, struct td_token *tok)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	int rc = 0;

	/* Reset any internal retry counters */
	td->td_tok_retries[tok->tokid].u64 = 0;

	tok->ops.clear_deallocation = td_cmd_clear_deallocation;
	
	if (tok->sec_buddy && td_token_is_active(tok->sec_buddy) )
	{
		td_eng_trace(eng, TR_TOKEN, "TD:sec_buddy:dupplicate", tok->sec_buddy->tokid);
		return td_cmd_gen_SEC_dup(eng, tok);
	}

	/* block request baring tokens */
	if (tok->host.bio) {
		if (! tok->cmd_seq)
			tok->cmd_seq = td_engine_next_sequence(eng);
		return td_cmd_gen_bio(eng, tok);
	}

	/* BIOs get special status_check ops, but now we just get a normal one */
	tok->ops.status_check = td_cmd_status_check;
	/* tokens handling user requests */
	if (tok->host.ucmd){
		rc = td_eng_cmdgen(eng, ucmd, tok->cmd_bytes, tok->host.ucmd,
				tok->core_bufid, tok->wr_bufid);
	} else if (tok->independent_deallocate)
		rc = td_eng_cmdgen(eng, dealloc, tok->cmd_bytes);

	if (rc)
		return rc;

	/* keep track of a seq number for to_ssd commands */
	if (td_cmd_is_sequenced(tok->cmd_bytes) )
		tok->cmd_seq = td_engine_next_sequence(eng);
	else
		tok->cmd_seq = 0;


	if (rc)
		return rc;
	
	return 0;
}

int teradimm_ops_can_retry(struct td_engine *eng, struct td_token *tok)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	int tokid = tok->tokid;
	td_status_t st;
	
	st.byte = tok->last_status;
	
	if (st.ext.status == TD_STATUS_ILLEGAL_CMD_ERR) {
		switch (tok->last_xstatus & 0x00ff) {
		case TD_CMD_HWF_TIMEOUT:
			td_eng_debug(eng, "Checking retries against WEP_TIMEOUT for tok %u [%016llx]\n",
					tok->tokid, tok->last_xstatus);
			if (++td->td_tok_retries[tokid].wep_timeout < td_eng_conf_var_get(eng, WEP_TIMEOUT_RETRIES) )
				return 1;
			break;
		case TD_CMD_HWF_XSUM:
			td_eng_debug(eng, "Checking retries against XSUM_ERR for tok %u [%016llx]\n",
					tok->tokid, tok->last_xstatus);
			if (++td->td_tok_retries[tokid].bad_xsum < td_eng_conf_var_get(eng, XSUM_ERR_RETRIES) )
				return 1;
			break;
		}

	}

	td_eng_info(eng, "RETRY TOK %d (%u / %llu)\n", tok->tokid,
			td->td_tok_retries[tokid].other, td_eng_conf_var_get(eng, RETRIES));

	/*
	 * Because of migration after timeouts, we need to chedk the token
	 * retry_count, which is migrated with us
	 */
	if (max(++td->td_tok_retries[tokid].other, ++tok->retry_count)
				< td_eng_conf_var_get(eng, RETRIES) )
		return 1;

	return 0;
}


#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
static enum td_token_type teradimm_ops_cmd_to_token_type(struct td_engine *eng,
		const void *cmd_bytes)
{
	const td_cmd_t *tdcmd = cmd_bytes;

	if (td_cmd_is_hardware_only(tdcmd))
		return TD_TOK_FOR_HW;
	return TD_TOK_FOR_FW;
}
#endif


#if 0
/* exported to stm driver, but a hack, so don't forget to remove later */
void teradimm_ops_command_fixups(struct td_engine *eng,
		struct td_token *tok, td_cmd_t *tdcmd)
{
	int re_duplicate = 0;

#warning remove these command fixups later
#define CMD_FIX(_cmd,_why,_var,_want) do {                      \
	unsigned have = _cmd->_var;                              \
	if (have != _want) {                                     \
		td_eng_trace(eng, -1,                            \
		"BUG: " _why ", want " __stringify(_var) "==" __stringify(_want)", have...", have); \
		_cmd->_var = _want;                              \
		re_duplicate++;                                  \
	}                                                        \
} while (0)
	
	switch (tdcmd->cmd.id) {
	case TD_CMD_DEALLOC_BUF:
	case TD_CMD_NULL:
	case TD_CMD_GET_REG:
	case TD_CMD_PUT_REG:
		CMD_FIX(tdcmd, "HW cmd",       cmd.decode.hardware,  1);
		CMD_FIX(tdcmd, "HW cmd",       cmd.decode.firmware,  0);
		break;

	case TD_CMD_RD_PAGE:
	case TD_CMD_WR_DLY:
	case TD_CMD_WR_FINAL:
	case TD_CMD_WR_EXT:
	case TD_CMD_RD_EXT:
	case TD_CMD_TEST_READ:
	case TD_CMD_TEST_WRITE:
		CMD_FIX(tdcmd, "RD/WR cmd",    cmd.decode.hardware,  0);
		CMD_FIX(tdcmd, "RD/WR cmd",    cmd.decode.firmware,  1);
		CMD_FIX(tdcmd, "RD/WR cmd",    cmd.decode.meta_size, 2);
		CMD_FIX(tdcmd, "RD/WR cmd",    cmd.decode.data_size, 0);

#ifdef TERADIMM_IGNORE_TOK_LBA
		/* FW expects the LBA to be a multiple of 512, not the LBA of host block size... so hard code it for now */
		if (tok->host.bio) {
			int ssd;
			uint64_t lba = 0;
			uint64_t pg_num;

			pg_num = td_bio_get_sector_offset(tok->host.bio) >> 3;
			ssd = pg_num & 1;
			lba = pg_num >> 1;

			tdcmd->src.lba.lba = lba;
			td_eng_trace(eng, -1, "FORCE:LBA", lba);

			tdcmd->cmd.ssd = ssd;
			td_eng_trace(eng, -1, "FORCE:SSD", ssd);
		}
#endif
		break;

	default:
		CMD_FIX(tdcmd, "other cmd",    cmd.decode.hardware,  0);
		CMD_FIX(tdcmd, "other cmd",    cmd.decode.firmware,  0);
		break;
	}

	/* hardware commands are not sequenced, clear it */
	if (tdcmd->cmd.decode.hardware) {
		CMD_FIX(tdcmd, "HW cmd",       cmd.seq,              0);
	}

	if (tdcmd->cmd.ssd)
		tdcmd->cmd._27_26 = 0x2;          /* <---- this bit must be on, or FW doesn't see SSD bit */

	switch (tdcmd->cmd.id) {
	case TD_CMD_TEST_READ:
	case TD_CMD_RD_PAGE:
		CMD_FIX(tdcmd, "RD_PAGE cmd",  dst.bcnt,             0);
		/* Fall through... */
	case TD_CMD_RD_EXT:
		CMD_FIX(tdcmd, "RD_PAGE cmd",  cmd.decode.to_host,   1);
		CMD_FIX(tdcmd, "RD_PAGE cmd",  cmd.decode.from_host, 0);

		switch (tok->magic_flags) {
		case 15:
			tdcmd->cmd.id = TD_CMD_TEST_READ;
			re_duplicate++;
			td_eng_trace(eng, -1, "FORCE:CMD==TEST_READ", tdcmd->cmd.id);
			break;
		case 14:
			tdcmd->cmd.id = TD_CMD_SATA_NOREAD;
			re_duplicate++;
			td_eng_trace(eng, -1, "FORCE:CMD==SATA_NOREAD", tdcmd->cmd.id);
			break;
		}
		break;

	case TD_CMD_TEST_WRITE:
	case TD_CMD_WR_DLY:
	case TD_CMD_WR_FINAL:
		CMD_FIX(tdcmd, "WR_PAGE cmd",  src.bcnt,             0);
		/* Fall through */
	case TD_CMD_WR_EXT:
		CMD_FIX(tdcmd, "WR_PAGE cmd",  cmd.decode.to_host,   0);
		CMD_FIX(tdcmd, "WR_PAGE cmd",  cmd.decode.from_host, 1);

		switch (tok->magic_flags) {
		case 15:
			tdcmd->cmd.id = TD_CMD_TEST_WRITE;
			re_duplicate++;
			td_eng_trace(eng, -1, "FORCE:CMD==TEST_WRITE", tdcmd->cmd.id);
			break;
		case 14:
			tdcmd->cmd.id = TD_CMD_SATA_NOWRITE;
			re_duplicate++;
			td_eng_trace(eng, -1, "FORCE:CMD=SATA_NOWRITE", tdcmd->cmd.id);
			break;
		}
		break;

	case TD_CMD_GET_PARAMS:
		CMD_FIX(tdcmd, "GET_PARAMS cmd",  cmd.decode.to_host,   1);
		break;

	case TD_CMD_SET_PARAMS:
		CMD_FIX(tdcmd, "SET_PARAMS cmd",  cmd.decode.from_host,   1);
		break;

	case TD_CMD_COPY:
		if (tok->len_host_to_dev)
			CMD_FIX(tdcmd, "COPY cmd",  cmd.decode.from_host,   1);
		if (tok->len_dev_to_host)
			CMD_FIX(tdcmd, "COPY cmd",  cmd.decode.to_host,     1);

	}

	if (tdcmd->cmd.decode.firmware) {
		if (tdcmd->word[3] != tdcmd->word[0])
			re_duplicate |=  0x100;
		if (tdcmd->word[4] != tdcmd->word[1])
			re_duplicate |=  0x200;
		if ((tdcmd->word[5] & ~0x3C00000000000000ULL) != (tdcmd->word[2] & ~0x3C00000000000000ULL))
			re_duplicate |=  0x400;

		if (re_duplicate) {
			td_eng_trace(eng, -1, "BUG:FORCE:DUPLICATE", re_duplicate);
			/* this is kinda funky, because it preserves the MSB of the checksum words, as it copies
			* words[0..2] to words[3..5] */
			td_eng_trace(eng, -1, "BUG: pre :DUPLICATE", tdcmd->word[0]);
			td_eng_trace(eng, -1, "BUG: pre :DUPLICATE", tdcmd->word[1]);
			td_eng_trace(eng, -1, "BUG: pre :DUPLICATE", tdcmd->word[2]);
			td_eng_trace(eng, -1, "BUG: pre :DUPLICATE", tdcmd->word[3]);
			td_eng_trace(eng, -1, "BUG: pre :DUPLICATE", tdcmd->word[4]);
			td_eng_trace(eng, -1, "BUG: pre :DUPLICATE", tdcmd->word[5]);
			tdcmd->word[3] = tdcmd->word[0];
			tdcmd->word[4] = tdcmd->word[1];
			tdcmd->word[5] &= 0x3C00000000000000ULL;
			tdcmd->word[5] |= tdcmd->word[2] & ~0x3C00000000000000ULL;
			td_eng_trace(eng, -1, "BUG: post:DUPLICATE", tdcmd->word[0]);
			td_eng_trace(eng, -1, "BUG: post:DUPLICATE", tdcmd->word[1]);
			td_eng_trace(eng, -1, "BUG: post:DUPLICATE", tdcmd->word[2]);
			td_eng_trace(eng, -1, "BUG: post:DUPLICATE", tdcmd->word[3]);
			td_eng_trace(eng, -1, "BUG: post:DUPLICATE", tdcmd->word[4]);
			td_eng_trace(eng, -1, "BUG: post:DUPLICATE", tdcmd->word[5]);
		}
	}

}
#endif

static void te_corrupt_cmd(struct td_engine *eng, td_cmd_t *tdcmd)
{
	unsigned row, bit;
	const char* flip_type = NULL;

	if ((eng->td_errors_mask & TE_CORRUPT_CMD) == TE_CORRUPT_CMD) {
		flip_type = "random";
		row = (td_get_cycles() % 247) % 8;
		bit = (td_get_cycles() % 41) % 64;
	} else if (eng->td_errors_mask & TE_CORRUPT_DUP) {
		if (! tdcmd->cmd.decode.dupcheck)
			return;
		flip_type = "dup";
		row = 2;
		bit = (td_get_cycles() % 41) % 48;
	} else if (eng->td_errors_mask & TE_CORRUPT_IDX) {
		flip_type = "idx";
		row = 1;
		bit = ((td_get_cycles() % 41) % 8) + 48;
	} else if (eng->td_errors_mask & TE_CORRUPT_ODD) {
		flip_type = "odd";
		row = (td_get_cycles() % 247) % 8;
		bit = ((td_get_cycles() % 41) % 2) + 62;
	} else if (eng->td_errors_mask & TE_CORRUPT_XSUM) {
		flip_type = "xsum";
		row = ((td_get_cycles() % 247) % 2) + 6;
		bit = (td_get_cycles() % 41) % 62;
	} else if (eng->td_errors_mask & TE_CORRUPT_FID) {
		if (tdcmd->cmd.decode.hardware)
			return;
		flip_type = "fid";
		row = 0;
		bit = (td_get_cycles() % 41) % 8;
	} else if (eng->td_errors_mask & TE_CORRUPT_HID) {
		if (! tdcmd->cmd.decode.hardware)
			return;
		flip_type = "hid";
		row = 0;
		bit = (td_get_cycles() % 41) % 8;
	} else {
		// Don't know what to corrupt anymore....
		return;
	}

	td_eng_trace(eng, TR_TOKEN, "EI:corrupt:tok", tdcmd->cmd.index);
	td_eng_info(eng, "EI: Corrupting tok %u cmd %016llx seq %u\n",
				tdcmd->cmd.index, tdcmd->word[0], tdcmd->cmd.seq);

	td_eng_info(eng, "EI: Flipping %s row %u bit %u\n", flip_type, row, bit);
	td_eng_trace(eng, TR_TOKEN, "EI:corrupt:row_bit", (row << 16) + bit);

	tdcmd->word[row] ^= 1ULL<<bit;

	td_dump_data(KERN_DEBUG, tdcmd, sizeof(td_cmd_t));
	eng->td_errors_count--;
}

static void teradimm_token_write_cmd(struct td_engine *eng, td_cmd_t *tdcmd,
		unsigned id, unsigned alias)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	enum td_conf_cmd_write_mode cmd_mode;
	td_cmd_t corrupted_cmd;
	void * hw_cmd_buf;

	if (TE_INJECT(eng, TE_CORRUPT_CMD) ) {
		corrupted_cmd.word[0] = tdcmd->word[0];
		corrupted_cmd.word[1] = tdcmd->word[1];
		corrupted_cmd.word[2] = tdcmd->word[2];
		corrupted_cmd.word[3] = tdcmd->word[3];
		corrupted_cmd.word[4] = tdcmd->word[4];
		corrupted_cmd.word[5] = tdcmd->word[5];
		corrupted_cmd.word[6] = tdcmd->word[6];
		corrupted_cmd.word[7] = tdcmd->word[7];
		tdcmd = &corrupted_cmd;

		te_corrupt_cmd(eng, tdcmd);
	}

	hw_cmd_buf = teradimm_map_cmd_buf(td->td_mapper, alias, id);

	if (TE_INJECT(eng, TE_CMD_OFFSET) ) {
		unsigned newid = 240;
		eng->td_errors_count --;
		td_eng_info(eng, "EI: changing offset on token %u\n", id);
		td_eng_trace(eng, TR_TOKEN, "EI:cmd_offset", newid << 16 | id);
		hw_cmd_buf = teradimm_map_cmd_buf(td->td_mapper, alias, newid);
	}

	switch(td_tok_type_from_tokid(td->td_engine, id)) {
	default:
	case TD_TOK_FOR_FW:
		cmd_mode = td_eng_conf_var_get(eng, FW_CMD_WRITE_MODE);
		break;

	case TD_TOK_FOR_HW:
		cmd_mode = td_eng_conf_var_get(eng, HW_CMD_WRITE_MODE);
		break;
	}

	switch (cmd_mode) {
	case TD_CMD_WRITE_MEMCPY:
		memcpy(hw_cmd_buf, tdcmd, TERADIMM_COMMAND_SIZE);
		if (TD_MAPPER_TYPE_TEST(COMMAND, WB)) {
			wmb();
			clflush(hw_cmd_buf);
			wmb();
		}
		break;
	case TD_CMD_WRITE_MOVNTI:
		td_memcpy_8x8_movnti(hw_cmd_buf, tdcmd, TERADIMM_COMMAND_SIZE);
		break;
	default:
	case TD_CMD_WRITE_FENCED_MOVNTI:
		td_memcpy_8x8_movnti_cli_64B(hw_cmd_buf, tdcmd);
		break;
	case TD_CMD_WRITE_MOVNTQ:
		td_memcpy_4x16_movntq(hw_cmd_buf, tdcmd, TERADIMM_COMMAND_SIZE);
		break;
	case TD_CMD_WRITE_FENCED_MOVNTQ:
		td_memcpy_4x16_movntq_cli_64B(hw_cmd_buf, tdcmd);
		break;
	}

	if (tdcmd->cmd.id == TD_CMD_DEALLOC_BUF) {
		uint64_t delay = td_eng_conf_mcefree_var_get(eng,
				POST_DEALLOC_DELAY_NSEC);
		if (delay)
			ndelay(delay);
	}

	if (TE_INJECT(eng, TE_SKIP_CMD_CLF) ) {
		eng->td_errors_count --;
		td_eng_info(eng, "EI: Skipping flush on token %u\n", id);
		td_eng_trace(eng, TR_TOKEN, "TD:start_token:done:tok", id);
		td_eng_trace(eng, TR_TOKEN, "EI:skip_flush:tok", id);
		return;
	}

	if (TD_MAPPER_TYPE_TEST(COMMAND, WB)) {
		td_cache_flush(eng, POST, CMD, hw_cmd_buf, TERADIMM_COMMAND_SIZE);
	}

	if (unlikely (td_eng_conf_var_get(eng, DELAY_POST_CMD_USEC))) {
		wmb(); /* sfence */
		udelay((uint32_t)td_eng_conf_var_get(eng, DELAY_POST_CMD_USEC));
	}
}

/** start command in hw */
static int teradimm_ops_start_token(struct td_engine *eng,
		struct td_token *tok)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	td_cmd_t *tdcmd;

	tdcmd = (void*)&tok->cmd_bytes;

	/* update the command slot number in the command */
	td_cmdgen_finalize(tok->cmd_bytes, tok->odd, tok->tokid,
			(uint16_t)tok->cmd_seq, tok->free_rd_bufid,
			tok->data_xsum);
#if 0
	/* HACK: protocol fixings -- remove this block later */
	teradimm_ops_command_fixups(eng, tok, tdcmd);
#endif

	if (unlikely(tok->host.ucmd) )
		teradimm_spy_ucmd(eng, tok);

	td_eng_trace(eng, TR_TOKEN, "TD:start_token:tok    ", tok->tokid);
	td_eng_trace(eng, TR_TOKEN, "TD:start_token:odd    ", tok->odd);

	td_eng_trace(eng, TR_TOKEN, "TD:start_token:core   ", tok->core_bufid);
	td_eng_trace(eng, TR_TOKEN, "TD:start_token:wrbuf  ", tok->wr_bufid);

	td_eng_trace(eng, TR_TOKEN, "TD:start_token:cmd    ", tdcmd->cmd.u64);
	td_eng_trace(eng, TR_TOKEN, "TD:start_token:src    ", tdcmd->src.u64);
	td_eng_trace(eng, TR_TOKEN, "TD:start_token:dst    ", tdcmd->dst.u64);
	td_eng_trace(eng, TR_TOKEN, "TD:start_token:pad0   ", tdcmd->pad0.u64);
	td_eng_trace(eng, TR_TOKEN, "TD:start_token:pad1   ", tdcmd->pad1.u64);
	td_eng_trace(eng, TR_TOKEN, "TD:start_token:pad2   ", tdcmd->pad2.u64);
	td_eng_trace(eng, TR_TOKEN, "TD:start_token:xsum0  ",
			tdcmd->chksum0.u64);
	td_eng_trace(eng, TR_TOKEN, "TD:start_token:xsum1  ",
			tdcmd->chksum1.u64);
	td_eng_trace(eng, TR_TOKEN, "TD:start_token:xsum0* ",
			((uint64_t)tdcmd->pad2.chksum0_msb << 62)
			| tdcmd->chksum0.chksum0);
	td_eng_trace(eng, TR_TOKEN, "TD:start_token:xsum1* ",
			((uint64_t)tdcmd->pad2.chksum1_msb << 62)
			| tdcmd->chksum1.chksum1);

	if (TE_INJECT(eng, TE_SKIP_CMD) ) {
		if ( ( td_token_is_write(tok) && TE_IS_SET(eng, TE_SKIP_WR_CMD) ) ||
				(td_token_is_read(tok) && TE_IS_SET(eng, TE_SKIP_RD_CMD) ) ||
				(td_token_is_control(tok) && TE_IS_SET(eng, TE_SKIP_CT_CMD) )  ||
				(td_token_is_hw(tok) && TE_IS_SET(eng, TE_SKIP_HW_CMD) ) ) {
			eng->td_errors_count --;
			td_eng_info(eng, "EI: Skipping token %u\n", tok->tokid);
			td_eng_trace(eng, TR_TOKEN, "TD:start_token:done:tok", tok->tokid);
			td_eng_trace(eng, TR_TOKEN, "EI:skip_cmd:tok", tok->tokid);
			tok->last_cmd_issue = td_get_cycles();
			return 0;
		}
	}
	
	if (TE_INJECT(eng, TE_CMD_REORDER) ) {
		if (tdcmd->cmd.seq) {
			eng->td_errors_count--;
			td_eng_trace(eng, TR_TOKEN, "EI:hold_cmd:tok", tok->tokid);
			td_eng_info(eng, "EI: Holding back token %u (%p) in slot %d\n",
					tok->tokid, tok, td->ei.held.count);
			td_eng_trace(eng, TR_TOKEN, "TD:start_token:done:tok", tok->tokid);

			td->ei.held.cmd[td->ei.held.count].tok = tok;
			memcpy(&td->ei.held.cmd[td->ei.held.count].word, tdcmd, TERADIMM_COMMAND_SIZE);

			td->ei.held.count++;

			return 0;
		}
	}

	if (td_eng_using_fwstatus(eng)
			&& tdcmd->cmd.id == TD_CMD_GET_REG) {
		td_eng_err(eng, "ERROR: sending a get-reg %016llx "
				"tok %u in mcefree/fwstatus mode\n",
				tdcmd->cmd.u64, tok->tokid);
	}

	td_tok_event(tok, TD_TOK_EVENT_START, 0);

	/* No aliases for now */
	teradimm_token_write_cmd(eng, tdcmd, tok->tokid, 0);
	tok->last_cmd_issue = td_get_cycles();
	td_eng_trace(eng, TR_TOKEN, "TD:start_token:done:tok", tok->tokid);

	if (unlikely (eng->td_errors_count == 0 && td->ei.held.count)) {
		unsigned i = td->ei.held.count;
		while (i--) {
			td_eng_info(eng, "EI: Flushing slot %d\n", i);
			tok = td->ei.held.cmd[i].tok;
			tdcmd = (void*)&td->ei.held.cmd[i].word[0];
			td_eng_info(eng, "EI:   original tok: %p\n", tok);
			td_eng_info(eng, "EI: Flushing held token %d\n", tok->tokid);
			td_eng_trace(eng, TR_TOKEN, "EI:flush_held:tok", tok->tokid);
			
			/* Without aliasing...*/
			teradimm_token_write_cmd(eng, tdcmd, tok->tokid, 0);
			tok->last_cmd_issue = td_get_cycles();
		}
		td->ei.held.count = 0;
		//eng->td_trace.tt_mask = 0;
	}

	return 0;
}

static int teradimm_ops_reset_token(struct td_engine *eng,
		struct td_token *tok)
{
	int alias;
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	void * hw_cmd_buf;
	td_cmd_t *tdcmd, reset;

	tdcmd = &reset;

	td_eng_trace(eng, TR_TOKEN, "TD:reset_token:tok    ", tok->tokid);
	td_eng_trace(eng, TR_TOKEN, "TD:reset_token:odd    ", tok->odd);

	/* TODO: generate a reset command using a generator */
	memset(tdcmd, 0, sizeof(td_cmd_t));
	tdcmd->cmd.u64 = 0;
	tdcmd->cmd.id = TD_CMD_RESET_CMD;
	tdcmd->cmd.decode.hardware = 1;
	tdcmd->cmd.index = tok->tokid;
	td_cmd_set_odd(tdcmd, tok->odd);

	td_eng_trace(eng, TR_TOKEN, "TD:reset_token:cmd    ", tdcmd->cmd.u64);
	td_eng_trace(eng, TR_TOKEN, "TD:reset_token:src    ", tdcmd->src.u64);
	td_eng_trace(eng, TR_TOKEN, "TD:reset_token:dst    ", tdcmd->dst.u64);
	td_eng_trace(eng, TR_TOKEN, "TD:reset_token:pad0   ", tdcmd->pad0.u64);
	td_eng_trace(eng, TR_TOKEN, "TD:reset_token:pad1   ", tdcmd->pad1.u64);
	td_eng_trace(eng, TR_TOKEN, "TD:reset_token:pad2   ", tdcmd->pad2.u64);

	alias = 0;
	hw_cmd_buf = teradimm_map_cmd_buf(td->td_mapper, alias, tok->tokid);

	__td_tok_event(tok, 0, TD_CMD_RESET_CMD,
			TD_TOK_EVENT_START, 0);

	td_memcpy_8x8_movnti(hw_cmd_buf, tdcmd, TERADIMM_COMMAND_SIZE);

	td_eng_trace(eng, TR_TOKEN, "TD:reset_token:done:tok", tok->tokid);

	if (TD_MAPPER_TYPE_TEST(COMMAND, WB)) {
		td_cache_flush(eng, POST, CMD, hw_cmd_buf, TERADIMM_COMMAND_SIZE);
	}

	return 0;
}

/** write page to hw */
static int teradimm_ops_write_page(struct td_engine *eng,
		struct td_token *tok)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	struct td_token *buddy;
	int alias, accumulated;
	struct td_multi_target mt;
	int i;

	td_eng_trace(eng, TR_TOKEN, "TD:write_page:tok     ", tok->tokid);
	td_eng_trace(eng, TR_TOKEN, "TD:write_page:LBA     ", tok->lba);
	td_eng_trace(eng, TR_TOKEN, "TD:write_page:LBA_ofs ", tok->lba_ofs);
	td_eng_trace(eng, TR_TOKEN, "TD:write_page:core    ", tok->core_bufid);
	td_eng_trace(eng, TR_TOKEN, "TD:write_page:wrbuf   ", tok->wr_bufid);
	td_eng_trace(eng, TR_TOKEN, "TD:write_page:len     ", tok->len_host_to_dev);

	alias = 0;
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	alias = tok->use_wep_alias;
#endif

	td_multi_target_init(&mt);

	td_multi_target_add(&mt,
		teradimm_map_write_data_buf(td->td_mapper, alias, tok->wr_bufid),
		teradimm_map_write_meta_buf(td->td_mapper, alias, tok->wr_bufid));
	
	buddy = tok->sec_buddy;
	if (likely(buddy)) {
		td_multi_target_add(&mt,
			teradimm_map_write_data_buf(td->td_mapper, alias, buddy->wr_bufid),
			teradimm_map_write_meta_buf(td->td_mapper, alias, buddy->wr_bufid));
		td_eng_trace(eng, TR_TOKEN, "TD:write_page:buddy_tok ", buddy->tokid);
		td_eng_trace(eng, TR_TOKEN, "TD:write_page:buddy_core", buddy->core_bufid);
		td_eng_trace(eng, TR_TOKEN, "TD:write_page:buddy_wep ", buddy->wr_bufid);

		/* the leading token should be the only one with an extra WEP */
		WARN_ON (TD_IS_WR_BUFID_VALID(buddy->extra_wr_bufid));
	}

	if (TD_IS_WR_BUFID_VALID(tok->extra_wr_bufid)) {
		td_multi_target_add(&mt,
			teradimm_map_write_data_buf(td->td_mapper, alias, tok->extra_wr_bufid),
			teradimm_map_write_meta_buf(td->td_mapper, alias, tok->extra_wr_bufid));
		td_eng_trace(eng, TR_TOKEN, "TD:write_page:pfail_wep ", tok->extra_wr_bufid);
	}

	BUG_ON(!mt.used || mt.used > TD_MULTI_TARGET_MAX);

	td_eng_trace(eng, TR_TOKEN, "TD:write_page:multi:used", mt.used);

	/* copy data into the WEP, which takes care of E2E and checksumming */

	switch (mt.used) {
	default:
		accumulated = tok->copy_ops.host_to_dev(tok,
				mt.buf[0].data, mt.buf[0].meta);
		break;
	case 2:
	case 3:
		accumulated = tok->copy_ops.host_to_multi_dev(tok, &mt);
		break;
	}

	td_eng_trace(eng, TR_TOKEN, "TD:write_page:accumulated", accumulated);

	if (accumulated < 0)
		return accumulated;

#ifdef CONFIG_TERADIMM_FORCE_4096_128
	if (accumulated < TERADIMM_DATA_BUF_SIZE) {
		/* We need to copy the rest of the page as zeros */
		int needed = TERADIMM_DATA_BUF_SIZE - accumulated;

		td_eng_trace(eng, TR_TOKEN, "TD:write_page:zero_needed", needed);
		
		BUG_ON(needed & 7);

		td_checksum128_advance_zeroed(tok->data_xsum, needed);

		/* We do this in 8B chunks until we are 128B aligned */
		while (needed & 127) {
			switch (mt.used) {
			default:
				td_zero_8B_movnti(PTR_OFS(mt.buf[2].data, accumulated));
				/* fall though expected */
			case 2:
				td_zero_8B_movnti(PTR_OFS(mt.buf[1].data, accumulated));
				/* fall though expected */
			case 1:
				td_zero_8B_movnti(PTR_OFS(mt.buf[0].data, accumulated));
				break;
			}

			accumulated += 8;
			needed -= 8;

			td_eng_trace(eng, TR_COPYOPS, "TD:write_page:xsum[0]    ",
					tok->data_xsum[0]);
			td_eng_trace(eng, TR_COPYOPS, "TD:write_page:xsum[1]    ",
					tok->data_xsum[1]);
			td_eng_trace(eng, TR_COPYOPS, "TD:write_page:needed", needed);
		}

		if (needed) {
			/* fill multiple of 64B with zeros */
			for (i=0; i<mt.used; i++)
				td_zero_8x8_movnti(PTR_OFS(mt.buf[i].data, accumulated), needed);
			accumulated += needed;
		}

		/* the buddy checksum is the same */
		if (buddy)
			memcpy(buddy->data_xsum, tok->data_xsum,
				sizeof(tok->data_xsum));

	} else if (accumulated > TERADIMM_DATA_BUF_SIZE
			&& accumulated < TERADIMM_DATA_BUF_SIZE+TERADIMM_META_BUF_SIZE) {
		/* metadata needs to be filled with zeros */
		int fill_needed = (TERADIMM_DATA_BUF_SIZE+TERADIMM_META_BUF_SIZE) - accumulated;
		int meta_used   = accumulated - TERADIMM_DATA_BUF_SIZE;
		void *fill_ptr;

		td_eng_trace(eng, TR_COPYOPS, "TD:write_page:meta_needed", fill_needed);
		BUG_ON(fill_needed & 7);

		switch (mt.used) {
		default:
			/* 3rd WEP just gets filled */
			fill_ptr = PTR_OFS(mt.buf[2].meta, meta_used);
			td_zero_8x8_movnti(fill_ptr, fill_needed);

			/* fall through expected */
		case 2:
			fill_ptr = PTR_OFS(mt.buf[1].meta, meta_used);
			td_zero_8x8_movnti(fill_ptr, fill_needed);

			/* write zeros for the rest of the metadata, updating xsum*/
			td_checksum128_advance_zeroed(buddy->data_xsum, fill_needed);

			/* fall through expected */
		case 1:
			fill_ptr = PTR_OFS(mt.buf[0].meta, meta_used);

			td_zero_8x8_movnti(fill_ptr, fill_needed);

			/* write zeros for the rest of the metadata, updating xsum*/
			td_checksum128_advance_zeroed(tok->data_xsum, fill_needed);

			break;
		}

		accumulated += fill_needed;
	} else {

		/* the buddy checksum is the same */
		if (buddy)
			memcpy(buddy->data_xsum, tok->data_xsum,
					sizeof(tok->data_xsum));
	}


	td_eng_trace(eng, TR_COPYOPS, "TD:write_page:final        ",
			accumulated);
	td_eng_trace(eng, TR_COPYOPS, "TD:write_page:xsum[0]      ",
			tok->data_xsum[0]);
	td_eng_trace(eng, TR_COPYOPS, "TD:write_page:xsum[1]      ",
			tok->data_xsum[1]);

	if (buddy) {
		td_eng_trace(eng, TR_COPYOPS, "TD:write_page:buddy_xsum[0]",
			buddy->data_xsum[0]);
		td_eng_trace(eng, TR_COPYOPS, "TD:write_page:buddy_xsum[1]",
			buddy->data_xsum[1]);
	}

#endif

	if (unlikely (td_eng_conf_var_get(eng, DELAY_POST_WRBUF_USEC))) {
		wmb(); /* sfence */
		udelay((uint32_t)td_eng_conf_var_get(eng, DELAY_POST_WRBUF_USEC));
	}

	return 0;
}

/* TODO: this should be part of the HAL */
static int teradimm_ops_refresh_wep(struct td_engine *eng, struct td_token *tok)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	void *hw_data_buf_0, *hw_meta_buf_0;
	void *hw_data_buf_1, *hw_meta_buf_1;

	WARN_ON(tok->wr_bufid > TERADIMM_WRITE_DATA_MAX);
	if (unlikely (tok->wr_bufid > TERADIMM_WRITE_DATA_MAX))
		return -EINVAL;

	/* this code copies alias 1 into alias 0 */

	hw_data_buf_0 = teradimm_map_write_data_buf(td->td_mapper, 0, tok->wr_bufid);
	hw_meta_buf_0 = teradimm_map_write_meta_buf(td->td_mapper, 0, tok->wr_bufid);

	hw_data_buf_1 = teradimm_map_write_data_buf(td->td_mapper, 1, tok->wr_bufid);
	hw_meta_buf_1 = teradimm_map_write_meta_buf(td->td_mapper, 1, tok->wr_bufid);

	/* alias 1 is read through the cache, so it must be flushed first */

	if (TD_MAPPER_TYPE_TEST(WRITE_DATA, WB)) {
		clflush_cache_range(hw_data_buf_1, TERADIMM_DATA_BUF_SIZE);
		clflush_cache_range(hw_meta_buf_1, TERADIMM_META_BUF_SIZE);
	}

	/* alias 0 is written non-temporally */

	td_memcpy_8x8_movnti(hw_data_buf_0, hw_data_buf_1, TERADIMM_DATA_BUF_SIZE);
	td_memcpy_8x8_movnti(hw_meta_buf_0, hw_meta_buf_1, TERADIMM_META_BUF_SIZE);

	return 0;
}

/** read page from hw */
static int teradimm_ops_read_page(struct td_engine *eng,
		struct td_token *tok)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	int accumulated;
	int flush_size;
	void * hw_data_buf, * hw_meta_buf;
	void * hw_data_alias, * hw_meta_alias;

	td_eng_trace(eng, TR_TOKEN, "TD:read_page:tok        ", tok->tokid);
	td_eng_trace(eng, TR_TOKEN, "TD:read_page:LBA        ", tok->lba);
	td_eng_trace(eng, TR_TOKEN, "TD:read_page:LBA_ofs    ", tok->lba_ofs);
	td_eng_trace(eng, TR_TOKEN, "TD:read_page:rbuf_id    ", tok->rd_bufid);

	WARN_ON(!TD_IS_RD_BUFID_VALID(tok->rd_bufid));
	if (!TD_IS_RD_BUFID_VALID(tok->rd_bufid))
		return -EINVAL;

#ifdef CONFIG_TERADIMM_DYNAMIC_READ_ALIASING
	Do not compile this code
	BUG_ON(td_cache_flush_exact_test(eng,PRE,NTF,RDBUF));

	{
	uint alias = 0;
	int max_aliases = (uint)td_eng_conf_var_get(eng, HOST_READ_ALIASES);
	if (max_aliases) {
		if (td_eng_conf_var_get(eng, WBINVD) & TD_WBINVD_READ_ALIAS) {
			/* invalidate all cache after any one read buffer's aliases run out */
			alias = td->next_read_page_alias[tok->rd_bufid];
			if ( alias >= max_aliases ) {
				/* reset read *and* status aliases */
				memset(td->next_read_page_alias, 0,
					sizeof(td->next_read_page_alias));
				td->next_status_alias = 0;
				wbinvd();
				alias = 0;
			}
			td->next_read_page_alias[tok->rd_bufid]++;
		} else {
			unsigned next;
			alias = td->next_read_page_alias[tok->rd_bufid];
			next = (alias + 1) % max_aliases;
			td->next_read_page_alias[tok->rd_bufid] = next;
		}
		td_eng_trace(eng, TR_TOKEN, "TD:read_page:alias      ", alias);
	}
	}
#endif

	hw_data_buf = teradimm_map_read_data_buf(td->td_mapper, 0, tok->rd_bufid);
	hw_meta_buf = teradimm_map_read_meta_buf(td->td_mapper, 0, tok->rd_bufid);

	hw_data_alias = teradimm_map_read_data_buf(td->td_mapper, 1, tok->rd_bufid);
	hw_meta_alias = teradimm_map_read_meta_buf(td->td_mapper, 1, tok->rd_bufid);

	/* pre flush */

	if (TD_MAPPER_TYPE_TEST(READ_DATA, WB)) {
	if (td_cache_flush_test(eng, PRE, RDBUF)) {
		unsigned bytes = TERADIMM_DATA_BUF_SIZE
			+ TERADIMM_META_BUF_SIZE;

		/* A BIO copy can possibly request less than all the
		 * data.  So we can try an doptimize for less than that
		 */
		if (likely(tok->host.bio))
			bytes = td_bio_get_byte_size(tok->host.bio);

		if (bytes < TERADIMM_DATA_BUF_SIZE) {
			/*
			 * If we're using METADATA on the device, we need to
			 * include it in our flushing
			 */
			if (td_eng_conf_hw_var_get(eng, HW_SECTOR_METADATA) )
				bytes += (bytes/512 * 8);

			flush_size = min_t(unsigned, bytes,
					TERADIMM_DATA_BUF_SIZE);
			td_cache_flush(eng, PRE, RDBUF, hw_data_buf, flush_size);

			flush_size = min_t(unsigned, bytes - flush_size,
					TERADIMM_META_BUF_SIZE);
			if (flush_size)
				td_cache_flush(eng, PRE, RDBUF, hw_meta_buf, flush_size);

		} else {
			td_cache_flush(eng, PRE, RDBUF, hw_data_buf, TERADIMM_DATA_BUF_SIZE);
			td_cache_flush(eng, PRE, RDBUF, hw_meta_buf, TERADIMM_META_BUF_SIZE);
		}
	}
	}

	/* copy */
	accumulated = tok->copy_ops.dev_to_host(tok, hw_data_buf, hw_meta_buf, hw_data_alias, hw_meta_alias);


	/* post flush */

	if (TD_MAPPER_TYPE_TEST(READ_DATA, WB)) {
	if (td_cache_flush_test(eng, POST, RDBUF) && accumulated>0) {
		unsigned to_flush = accumulated + (accumulated/512 * 8);

		flush_size = min_t(unsigned, to_flush,
				TERADIMM_DATA_BUF_SIZE);
		td_cache_flush(eng, POST, RDBUF, hw_data_buf, flush_size);

		flush_size = min_t(unsigned, to_flush - flush_size,
				TERADIMM_META_BUF_SIZE);
		if (flush_size)
			td_cache_flush(eng, POST, RDBUF, hw_meta_buf, flush_size);
	}
	}

	/* done */

	td_eng_trace(eng, TR_TOKEN, "TD:read_page:accumulated", accumulated);

	return accumulated < 0 ? accumulated : 0;
}

static inline int teradimm_ops_read_rdbuf_metadata(struct td_engine *eng,
		unsigned rdbuf, unsigned off, void *out, unsigned len)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	const void * hw_meta_buf, *src;
	uint alias;

	if (unlikely (off > TERADIMM_META_BUF_SIZE))
		return -EINVAL;
	if (unlikely((off+len) > TERADIMM_META_BUF_SIZE))
		return -EINVAL;

	alias = 0;
	hw_meta_buf = teradimm_map_read_meta_buf(td->td_mapper, alias, rdbuf);

	src = PTR_OFS(hw_meta_buf, off);

	if (TD_MAPPER_TYPE_TEST(READ_META_DATA, WB)) {
		clflush_cache_range(src, len);
		memcpy(out, src, len);
	} else {
		if (unlikely(len&63ULL))
			return -EINVAL;

		td_memcpy_movntdqa_64(out, src, len);
	}

	return 0;
}


/*
 * This is called for all commands that are cmdgen'ed.  If it fails, the
 * cmdgen call will return an error.
 */
int teradimm_ops_filter(struct td_engine *eng, uint64_t bytes[8])
{
	int ret = -EBUSY;
	td_cmd_t *tdcmd = (td_cmd_t*)bytes;
	enum td_device_state_type state;
	struct td_device *dev;


	if (eng->td_run_state != TD_RUN_STATE_RUNNING) {
		td_eng_info(eng, "Not running: %016llX cmd = 0x%02X\n", bytes[0],
				tdcmd->cmd.id);
		switch(tdcmd->cmd.id) {
		case TD_CMD_DEALLOC_BUF:
			if (eng->td_run_state == TD_RUN_STATE_READ_PURGE)
				ret = 0;
			goto enabling;
			break;
		case TD_CMD_GET_PARAMS:
			if (eng->td_run_state == TD_RUN_STATE_FW_PROBE)
				ret = 0;
			goto enabling;
			break;
		default:
			break;
		}
		goto not_running;
	}

	ret = -ENXIO;
	dev = td_engine_device(eng);
	if(!dev)
		goto nodev;

	state = dev->td_state;

	ret = -EINVAL;

	switch (tdcmd->cmd.id) {
	/* Debug commands are always okay */
	case TD_CMD_NULL:
	case TD_CMD_GET_REG:
	case TD_CMD_PUT_REG:
	case TD_CMD_FW_NULL:
	case TD_CMD_TRACE_CTRL:
	case TD_CMD_FW_DEBUG_LOG:
	case TD_CMD_CRASH_LOG:
	case TD_CMD_DEALLOC_BUF:
	case TD_CMD_GET_PARAMS:
	case TD_CMD_SET_PARAMS:
		ret = 0;
		break;

	/* We are always going to let these through */
	case TD_CMD_RESET_CMD:
	case TD_CMD_RD_SEQ:
	case TD_CMD_WR_DLY:
	case TD_CMD_WR_FINAL:
	case TD_CMD_RD_EXT:
	case TD_CMD_WR_EXT:
	case TD_CMD_TRIM:
	case TD_CMD_SSD_STATS:
	case TD_CMD_COPY:
	case TD_CMD_TEST:
	case TD_CMD_SEQ:
	case TD_CMD_SSD_EXT_ERR:
	case TD_CMD_TEST_READ:
	case TD_CMD_TEST_WRITE:
		ret = 0;
		break;


	/* Special cases  */
	case TD_CMD_FW_DOWNLOAD:
	case TD_CMD_STARTUP:
	case TD_CMD_SHUTDOWN:
		if (state == TD_DEVICE_STATE_OFFLINE)
			ret = 0;
		break;
	case TD_CMD_SSD_CMD: /* SSD_CMD include temperature probs */
		ret = td_cmd_ata_ok(tdcmd);
		break;

	default:
		break;
	}

	if (ret)
		td_eng_warning(eng, "Command 0x%02X denied: 0x%0X\n",
				tdcmd->cmd.id, ret);
nodev:
enabling:
not_running:
	return ret;
}

#ifdef CONFIG_TERADIMM_TRIM
static int teradimm_ops_trim(struct td_engine *eng,
		struct td_token *tok)
{
	uint64_t size = td_bio_get_byte_size(tok->host.bio);
	uint64_t *page = (uint64_t*)tok->host.bio->bi_io_vec->bv_page;
	td_bio_flags_t flags = *td_bio_flags_ref(tok->host.bio);
	uint64_t value = 0;
	uint64_t lba = tok->lba;
	uint64_t count, i;

	td_eng_trace(eng, TR_TRIM, "TD:trim:bio",    (uint64_t)tok->host.bio);
	td_eng_trace(eng, TR_TRIM, "TD:trim:sctr",   tok->host.bio->bio_sector);
	td_eng_trace(eng, TR_TRIM, "TD:trim:size",   tok->host.bio->bio_size);
	td_eng_trace(eng, TR_TRIM, "TD:trim:port",    tok->port);
	td_eng_trace(eng, TR_TRIM, "TD:trim:lba",    tok->lba);

	/* convert to LBAs */
	size = size >> SECTOR_SHIFT;
	/* Calculate how many entries in the page there will be.
	 * Note: any remainder will be taken care of after the main loop. */
	count = (size/TD_MAX_DISCARD_CHUNK);
	td_eng_trace(eng, TR_TRIM, "TD:trim:count",   count);

	/* Halt on too many requests for a page. */
	if (count + (size % TD_MAX_DISCARD_CHUNK ? 1 : 0) > 64) {
		BUG_ON(1);
	}

	memset(page, 0, 512);
	//td_eng_info(eng, "TRIM content (SSD: %u  Start LBA: %llu  size: %llu)\n", tok->ssd, lba, size);
	/* Release the hounds! */
	for (i = 0; i < count;i++) {
		/* Set the value we will be pushing to the page */
		value = (TD_MAX_DISCARD_CHUNK << 48) | lba;
		/* Set the value to le64 or be64 based on Monet. */
		td_eng_trace(eng, TR_TRIM, "TD:trim:qword", value);
		*page = cpu_to_le64(value);
		//td_eng_info(eng, "   Trim entry[%u]: 0x%016llX\n", i, value);
		/* Increment LBA count by number of LBAs sent in the last
		 * entry */
		lba += TD_MAX_DISCARD_CHUNK;
		/* Increment the page pointer to the next uint64_t*/
		page++;
	}

	/* If there is a remainder, take care of it now.*/
	if (size % TD_MAX_DISCARD_CHUNK) {
		value = ((size % TD_MAX_DISCARD_CHUNK) << 48) | lba;
		td_eng_trace(eng, TR_TRIM, "TD:trim:qword", value);
		*page = cpu_to_le64(value);
		//td_eng_info(eng, "   Trim entry[%u]: 0x%016llX\n", i, *page);
	}
/*
	td_eng_info(eng, "trim content:");
	page -= count;
	for (i = 0; i < 64; i++) {
		td_eng_info(eng, "0x%016llX", *page);
		page++;
	}
*/
	tok->host.bio->bio_size = 512;
	/* just because they may have been messed up */
	*td_bio_flags_ref(tok->host.bio) = flags;


	teradimm_ops_write_page(eng, tok);
	return 0;
}
#endif

/** get a raw buffer for debugging */
static int teradimm_raw_buffer(struct td_engine *eng, int write,
		enum td_buf_type type, unsigned index,
		void *user_buf, unsigned buf_len)
{
	void * hw_buf;
	unsigned hw_max, copy_len;
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	bool cached_mapping = 1;

	WARN_ON_ACCESS_FROM_WRONG_CPU(eng);

	switch(type) {
	case TD_BUF_GLOBAL_STATUS: /* !< single instance (1B) */
		cached_mapping = TD_MAPPER_TYPE_TEST(STATUS, WB);
		hw_buf = PTR_OFS(teradimm_map_status(td->td_mapper, 0), 255);
		hw_max = 1;
		break;

	case TD_BUF_COMMAND: /* !< 64B command buffers */
		if (index >= TD_TOKENS_PER_DEV)
			return -ERANGE;
		cached_mapping = TD_MAPPER_TYPE_TEST(COMMAND, WB);
		hw_buf = teradimm_map_cmd_buf(td->td_mapper, 0, index);
		hw_max = 64;
		break;

	case TD_BUF_CMD_STATUS: /* !< single instance (256B) */
		cached_mapping = TD_MAPPER_TYPE_TEST(STATUS, WB);
		hw_buf = teradimm_map_status(td->td_mapper, 0);
		hw_max = 256;
		break;

	case TD_BUF_EX_STATUS: /* !< single instance (256*8) */
		cached_mapping = TD_MAPPER_TYPE_TEST(EXT_STATUS, WB);
		hw_buf = teradimm_map_ext_status(td->td_mapper, 0);
		hw_max = TERADIMM_EXT_STATUS_SIZE;
		break;

	case TD_BUF_READ: /* !< 4k read buffers */
		if (index >= TD_HOST_RD_BUFS_PER_DEV)
			return -ERANGE;
		cached_mapping = TD_MAPPER_TYPE_TEST(READ_DATA, WB);
		hw_buf = teradimm_map_read_data_buf(td->td_mapper, 0, index);
		hw_max = TERADIMM_DATA_BUF_SIZE;
		break;

	case TD_BUF_READ_META: /* !< 128B read meta data */
		if (index >= TD_HOST_RD_BUFS_PER_DEV)
			return -ERANGE;
		cached_mapping = TD_MAPPER_TYPE_TEST(READ_META_DATA, WB);
		hw_buf = teradimm_map_read_meta_buf(td->td_mapper, 0, index);
		hw_max = TERADIMM_META_BUF_SIZE;
		break;

	case TD_BUF_WRITE: /* !< 4k write buffers */
		if (index >= TD_HOST_WR_BUFS_PER_DEV)
			return -ERANGE;
		cached_mapping = TD_MAPPER_TYPE_TEST(WRITE_DATA, WB);
		hw_buf = teradimm_map_write_data_buf(td->td_mapper, 0, index);
		hw_max = TERADIMM_DATA_BUF_SIZE;
		break;

	case TD_BUF_WRITE_META: /* !< 128B write meta data */
		if (index >= TD_HOST_WR_BUFS_PER_DEV)
			return -ERANGE;
		cached_mapping = TD_MAPPER_TYPE_TEST(WRITE_META_DATA, WB);
		hw_buf = teradimm_map_write_meta_buf(td->td_mapper, 0, index);
		hw_max = TERADIMM_META_BUF_SIZE;
		break;

	case TD_BUF_GLOBAL_EX_STATUS: /* !< single instance (8B) */
		cached_mapping = TD_MAPPER_TYPE_TEST(EXT_STATUS, WB);
		hw_buf = teradimm_map_ext_status(td->td_mapper, 0);
		hw_buf = PTR_OFS(hw_buf, 0x800);
		hw_max = 8;
		break;

	default:
		return -EINVAL;
	}

	copy_len = min(buf_len, hw_max);

	if (write) {
		int copy_piece = copy_len % 64;

		if (copy_piece)
			copy_len -= copy_piece;

		td_memcpy_8x8_movnti(hw_buf, user_buf, copy_len);
		while (copy_piece) {
			WARN_ON(copy_piece < 8);
			td_memcpy_8B_movnti(PTR_OFS(hw_buf, copy_len),
					PTR_OFS(user_buf, copy_len));
			copy_len += 8;
			copy_piece -= 8;
		}
	} else {
		if (cached_mapping)
			clflush_cache_range(hw_buf, copy_len);
		memcpy(user_buf, hw_buf, copy_len);
	}

	return copy_len;
}

struct teradimm_raw_buffer_work_state {
	struct td_eng_thread_work work;
	int rw;
	enum td_buf_type type;
	unsigned index;
	void *user_buf;
	unsigned buf_len;
};

static int teradimm_raw_buffer_callback (struct td_engine *eng, void* data)
{
	struct teradimm_raw_buffer_work_state *state = container_of(data, struct teradimm_raw_buffer_work_state, work);

	return teradimm_raw_buffer(eng, state->rw, state->type, state->index,
			state->user_buf, state->buf_len);
}

static int teradimm_ops_get_raw_buffer(struct td_engine *eng,
		enum td_buf_type type, unsigned index,
		void *dst_buf, unsigned buf_len)
{
	struct teradimm_raw_buffer_work_state raw_buffer_work = {
		.work.func = teradimm_raw_buffer_callback,
		.work.name = "get_raw_buffer",
		.rw = 0,
		.type = type,
		.index = index,
		.user_buf = dst_buf,
		.buf_len = buf_len
	};

	return td_eng_safe_work(eng, &raw_buffer_work.work);
}

static int teradimm_ops_set_raw_buffer(struct td_engine *eng,
		enum td_buf_type type, unsigned index,
		void *src_buf, unsigned buf_len)
{
	struct teradimm_raw_buffer_work_state raw_buffer_work = {
		.work.func = teradimm_raw_buffer_callback,
		.work.name = "set_raw_buffer",
		.rw = 1,
		.type = type,
		.index = index,
		.user_buf = src_buf,
		.buf_len = buf_len
	};

	return td_eng_safe_work(eng, &raw_buffer_work.work);
}


struct td_eng_hal_ops td_eng_teradimm_ops = {
	._name         = "td",
	._init         = teradimm_ops_init,
	._exit         = teradimm_ops_exit,
	._enable       = teradimm_ops_enable,
	._disable      = teradimm_ops_disable,
	._get_conf     = teradimm_ops_get_conf,
	._set_conf     = teradimm_ops_set_conf,
	._hw_init      = teradimm_ops_hw_init,
	._fw_handshake = teradimm_ops_fw_handshake,
	._online     = teradimm_ops_online,
	._read_status  = teradimm_ops_read_status,
	._read_ext_status  = teradimm_ops_read_ext_status,
	._create_cmd   = teradimm_ops_create_cmd,
	._reverse_cmd_polarity = teradimm_ops_reverse_cmd_polarity,
#ifdef CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
	._cmd_to_token_type = teradimm_ops_cmd_to_token_type,
#endif
	._start_token  = teradimm_ops_start_token,
	._reset_token  = teradimm_ops_reset_token,
	._write_page   = teradimm_ops_write_page,
	._read_page    = teradimm_ops_read_page,
	._refresh_wep  = teradimm_ops_refresh_wep,
	._read_rdbuf_metadata = teradimm_ops_read_rdbuf_metadata,
	._get_raw_buffer = teradimm_ops_get_raw_buffer,
	._set_raw_buffer = teradimm_ops_set_raw_buffer,
	._handle_timeouts = td_eng_migrate_timeout_handler,
	._can_retry    = teradimm_ops_can_retry,
	._filter       = teradimm_ops_filter,
#ifdef CONFIG_TERADIMM_TRIM
	._trim         = teradimm_ops_trim,
#endif
	._generator    = &td_cmdgen_teradimm,
};
