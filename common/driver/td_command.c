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
#include <scsi/scsi.h>

#include "td_bio.h"
#include "td_command.h"
#include "td_defs.h"
//#include "td_drv.h"
#include "td_ucmd.h"
#include "td_protocol.h"
#include "td_eng_hal.h"

#include "td_eng_teradimm.h"

/*TODO: I think windows will need the full list? */
#define TD_ENUM_R_ENTRY(what) [TD_READ_##what]  = TD_TIMEOUT_##what,
#define TD_ENUM_W_ENTRY(what) [TD_WRITE_##what] = TD_TIMEOUT_##what,

/* For windows, we need to initialize every element in order */
const enum td_timeout td_r_to[TD_TIMEOUT_MAX] = {
	TD_TIMEOUT_QUEUED,
	TD_TIMEOUT_RECEIVED,
	TD_TIMEOUT_WAIT_SSD,
};

const enum td_timeout td_w_to[TD_TIMEOUT_MAX] = {
	TD_TIMEOUT_QUEUED,
	TD_TIMEOUT_RECEIVED,
	TD_TIMEOUT_WAIT_SSD,
};

/*
 * Extract ata command from tdcmd.  ata_cmd should be 16 bytes.
 * This is the opposite of the td_cmdgen_ata embedding in td_protocol.c
 * This is used by the TeraSIMM and the ATA filtering
 */
void td_cmd_ata_extract_cmd(td_cmd_t *tdcmd, uint8_t *ata_cmd) {
	uint64_t cmd_part[3];
	int i, j, k;

	cmd_part[0] = tdcmd->pad0.u64;
	cmd_part[1] = tdcmd->pad1.u64;
	cmd_part[2] = tdcmd->pad2.u64;
	for(j = 0, k=0; k < 3; k++) {
		for(i=0; i < 7 && j < 16; i++) {
			ata_cmd[j] = (cmd_part[k] >> i*8) & 0xFF;
			j++;
		}
	}
#if 0
	printk("ATA CMD:");
	for (i = 0; i < 16; i++)
		printk(" %0X", ata_cmd[i]);
	printk("\n");
	printk("Diablo CMD: ");
	for (i = 0; i < 3; i++)
		printk(" 0x%08llX", cmd_part[i]);
	printk("\n");
#endif
}


int td_cmd_ata_ok(td_cmd_t *tdcmd){
	uint8_t ata_cmd[16];
	td_cmd_ata_extract_cmd(tdcmd, ata_cmd);
	return td_cmd_ata_filter(ata_cmd);
}

int td_cmd_ata_filter(uint8_t *ata_cmd)
{
	int rc = -EINVAL;

	/* IDENTIFY */
	if (0x12 == ata_cmd[0]) {
		switch (ata_cmd[2]) {
		case 0x00:
		case 0x80:
		case 0x83:
			rc = 0;
			break;
		default:
			goto fail;
			break;
		}
	}

	/* ATA_16 pass-through (0x85) */
	if (ATA_16 == ata_cmd[0]) {
		switch (ata_cmd[14]) {
		case 0xEC: /* Identify Device */
		case 0xB0: /* smart */
		case 0xF3: /* Security Erase Prepare*/
		case 0xF4: /* Security Erase Unit*/
		case 0x2F: /* Read log.  */
			rc = 0;
			break;
		default:
			goto fail;
			break;
		}
	}

	if (ATA_12 == ata_cmd[0]) {
		switch (ata_cmd[9]) {
		case 0xEC: /* Identify Device */
			rc = 0;
			break;
		default:
			goto fail;
			break;
		}
	}
fail:
	return rc;

}

int td_cmd_gen_SEC_dup (struct td_engine *eng, struct td_token *tok)
{
	struct td_token *buddy = tok->sec_buddy;
	td_cmd_t *tdcmd = (td_cmd_t*)tok->cmd_bytes;

	/* Copy data from buddy token */
	tok->cmd_seq = buddy->cmd_seq;
	memcpy(tdcmd, buddy->cmd_bytes, sizeof(*tdcmd));

	/* Touch up our WEB/CB */
	tdcmd->src.bufid = (uint8_t)tok->core_bufid;
	tdcmd->src.wep = tok->wr_bufid;

	td_cmd_set_xsum(tdcmd, buddy->data_xsum);

	/* We grab our buddy status check */
	tok->ops.status_check = buddy->ops.status_check;

	return 0;
}

static inline void td_error_decode_ssd_err(uint64_t xs)
{
	/*
	 * ISR Status (0-7):
	 * 0:            Unknown FIS (UFS)
	 * 1:            OverFlow (OFS)
	 * 2:            Interface Non Fatal (INFS)
	 * 3:            Interface Fatal (IFS)
	 * 4:            Host Bus Data Error (HBDS)
	 * 5:            Host Bus Fatal Error (HBFS)
	 * 6:            Task File Error Status (TFES)
	 * 7:            Rsvd
	 *
	 * PxSerr (8-31)
	 * Err:
	 * 8:            Recovered Data Integrity Error (I)
	 * 9:            Recovered Communications Error (M)
	 * 10:          Transient Data Integrity Error (T)
	 * 11:          Persistent Communication or Data Integrity Error (C)
	 * 12:          Protocol Error (P)
	 * 13:          Internal Error (E)
	 *
	 * DIAG:
	 * 14:          PhyRdy Change (N)
	 * 15:          Phy Internal Error (I)
	 * 16:          Comm Wake (W)
	 * 17:          10B to 8B Decode Error (B)
	 * 18:          Disparity Error (D)
	 * 19:          CRC Error (C)
	 * 20:          Handshake Error (H)
	 * 21:          Link Sequence Error (S)
	 * 22:          Transport State transition Error (T)
	 * 23:          Unknown FIS Type (F)
	 * 24:          Exchanged (X)
	 *
	 * 25-31:    Rsvd
	 *
	 * Taskfile Register (32-63)
	 */

	pr_err("SSD Error debug: \n");
	if (xs & 0x1 << 0)
		pr_err(" Unkown FIS Interrupt bit set.\n");
	if (xs & 0x1 << 1)
		pr_err(" OverFlow (OFS)\n");
	if (xs & 0x1 << 2)
		pr_err(" Interface Non Fatal (INFS)\n");
	if (xs & 0x1 << 3)
		pr_err(" Interface Fatal (IFS)\n");
	if (xs & 0x1 << 4)
		pr_err(" Host Bus Data Error (HBDS)\n");
	if (xs & 0x1 << 5)
		pr_err(" Host Bus Fatal Error (HBFS)\n");
	if (xs & 0x1 << 6)
		pr_err(" Task File Error Status (TFES)\n");
	if (xs & 0x1 << 7)
		pr_err(" PHY Ready Change Status (PRCS)\n");

	if (xs & 0x1 << 8)
		pr_err(" Recovered Data Integrity Error (I)\n");
	if (xs & 0x1 << 9)
		pr_err(" Recovered Communications Error (M)\n");
	if (xs & 0x1 << 10)
		pr_err(" Transient Data Integrity Error (T)\n");
	if (xs & 0x1 << 11)
		pr_err(" Persistent Communication or Data Integrity Error (C)\n");
	if (xs & 0x1 << 12)
		pr_err(" Protocol Error (P)\n");
	if (xs & 0x1 << 13)
		pr_err(" Internal Error (E)\n");
	if (xs & 0x1 << 14)
		pr_err(" PhyRdy Change (N\n");
	if (xs & 0x1 << 15)
		pr_err(" Phy Internal Error (I)\n");
	if (xs & 0x1 << 16)
		pr_err(" Comm Wake (W)\n");
	if (xs & 0x1 << 17)
		pr_err(" 10B to 8B Decode Error (B)\n");
	if (xs & 0x1 << 18)
		pr_err(" Disparity Error (D)\n");
	if (xs & 0x1 << 19)
		pr_err(" CRC Error (C)\n");
	if (xs & 0x1 << 20)
		pr_err(" Handshake Error (H)\n");
	if (xs & 0x1 << 21)
		pr_err(" Link Sequence Error (S)\n");
	if (xs & 0x1 << 22)
		pr_err(" Transport State transition Error (T)\n");
	if (xs & 0x1 << 23)
		pr_err(" Unknown FIS Type (F)\n");
	if (xs & 0x1 << 24)
		pr_err(" Exchanged (X)\n");
/*
	if (xs & 0x1 << 25)
		pr_err(" Rsvd\n");
	if (xs & 0x1 << 26)
		pr_err(" Rsvd\n");
	if (xs & 0x1 << 27)
		pr_err(" Rsvd\n");
	if (xs & 0x1 << 28)
		pr_err(" Rsvd\n");
	if (xs & 0x1 << 29)
		pr_err(" Rsvd\n");
	if (xs & 0x1 << 30)
		pr_err(" Rsvd\n");
*/
	if (xs & 0x1 << 31)
		pr_err(" Port Connect Change Status (PCS)\n");

	pr_err("Taskfile register contained 0x%08X\n", (uint32_t)(xs >> 32));

}

static inline uint32_t td_cmd_status_timeout(struct td_engine *eng,
		struct td_token *tok, uint32_t status)
{
	const struct td_timeout_desc *to;
	td_cmd_t *tdcmd = (td_cmd_t*)tok->cmd_bytes;
	if(tdcmd->cmd.to_ssd) { /* hot path */
		if (tok->len_dev_to_host) {
			to = &td_timeout_lookup[td_r_to[status]];
			return (uint32_t)((eng)->conf.regs[to->bio_r]);
		}
		else {
			to = &td_timeout_lookup[td_w_to[status]];
			return (uint32_t)((eng)->conf.regs[to->bio_w]);
		}
	} else { /* Slow path. */
		/* We will need to decide on what to do here for special
		 * commands like upgrade, etc */
		if (eng->locker_context) {
			return 60000000; /* 60 seconds */
		}
		else {
			to = &td_timeout_lookup[td_r_to[status]];
			return (uint32_t)((eng)->conf.regs[to->ucmd]);
		}
	}
}

static inline unsigned td_cmd_status_commit (td_status_t st)
{
	switch (st.ext.status) {
		case TD_STATUS_QUEUED:
			return TD_QUEUED_COMMIT;
		case TD_STATUS_RECEIVED:
			return TD_RECEIVED_COMMIT;
		case TD_STATUS_SEQUENCED:
			return TD_SEQUENCED_COMMIT;
		case TD_STATUS_PENDING:
			return TD_WAIT_SSD_COMMIT;
	}
	return TD_FULL_COMMIT;
}

static int __td_cmd_fcode_handling (struct td_engine *eng,
		struct td_token *tok)
{
	uint64_t xs;

	/* Worst case... */
	tok->result = TD_TOK_RESULT_FAIL_ABORT;

	td_eng_hal_read_ext_status(eng, tok->tokid, &xs, 1);
	td_eng_trace(eng, TR_TOKEN, "TD_ENG:status:xstatus   ", xs);

	switch (xs & 0x00ff) {
	case 0xFF:      /* FW tells us to f*** off */
		td_eng_err(eng, "Firmware Fatal Error: %016llx\n", xs);
		td_eng_err(eng, "  * * * Firmware has gone DEAD * * *\n");
		tok->result = TD_TOK_RESULT_FAIL_ABORT_HARD;
		break;
	case TD_CMD_HWF_XSUM:
		eng->td_counters.token.badxsum_cnt ++;
		td_eng_debug(eng, "  HW DMA XSUM %016llx\n", xs);
		if (td_eng_conf_var_get(eng, XSUM_ERR_RETRIES))
			tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
		break;

	case TD_CMD_HWF_TIMEOUT:
		td_eng_debug(eng, "  HW WEP timeout %016llx\n", xs);
		eng->td_counters.token.weptimeout_cnt ++;
		if (td_eng_conf_var_get(eng, WEP_TIMEOUT_RETRIES))
			tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
		break;

	case TD_CMD_HWF_DATAECC:
		td_eng_err(eng, "  HW WEP ECC %016llx\n", xs);
		eng->td_counters.token.data_ecc_error_cnt ++;
		tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
		break;

	case TD_CMD_HWF_CMDOFFSET:
		td_eng_err(eng, "  HW CMD offset %016llx\n", xs);
		eng->td_counters.token.offset_error_cnt ++;
		tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
		break;

	case TD_CMD_HWF_HWCMD:
	default:
		eng->td_counters.token.cmd_error_cnt ++;
		td_eng_err(eng, "  unknown extended status %016llx\n", xs);
		tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
	}
	
	tok->last_xstatus = xs;
	return 1;
}

static int td_lost_command_refresh(struct td_engine *eng,
		struct td_token *tok)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	td_status_t st;
	uint64_t now, diff, usec;
	uint64_t max_usec, max_refresh;

	/* no need if there was a status update for this polarity. */
	st.byte = tok->last_status;
	if (likely (st.fin.odd == tok->odd))
		return 0;

	/* read config */
	max_usec = td_eng_conf_var_get(eng, LOST_CMD_REFRESH_USEC);
	max_refresh = td_eng_conf_var_get(eng, LOST_CMD_REFRESH_MAX);

	/* if disabled stop now */
	if (!max_usec || !max_refresh)
		return 0;

	/* no need if the token is already marked as needing an ooo refresh */
	if (unlikely (tok->ooo_missing))
		return 0;

	/* bail if the issue timestamp was not set for some reason */
	if (unlikely (!tok->last_cmd_issue))
		return 0;

	/* if exceeded number of reissues stop now */
	if (unlikely (tok->cmd_refresh_count > max_refresh))
		return 0;

	/* We need to work with the timestamp of the status we have */
	now = td->td_status_ts[td_token_type(tok)];

	/* we need a status update since the last time we issued */
	if (unlikely (now < tok->last_cmd_issue))
		return 0;

	/* how long have we been waiting for an update? */
	diff = now - tok->last_cmd_issue;
	usec = td_cycles_to_usec(diff);

	/* if we didn't wait long enough, stop now */
	if (likely (usec < max_usec))
		return 0;

	/* sending a new token is dangerous, since we don't want to
	 * overflow the fifo ...
	 * TODO: make sure we are not in some kind of hold */
	udelay(2);

	/* at this point we have to refresh the command, we believe it was lost */

	tok->cmd_refresh_count ++;

	td_eng_debug(eng, "re-push delayed tok %u cmd %16llx status %02x "
			"(%llu usec, %u refresh)\n",
			tok->tokid, tok->cmd_bytes[0], st.byte,
			usec, tok->cmd_refresh_count);

	td_eng_hal_start_token(eng, tok);

	td_engine_update_token_sent_timestamps(eng, tok);

	eng->td_counters.token.lost_refresh_cnt ++;

	return 1;
}


static int __td_cmd_timeout_handling (struct td_engine *eng,
		struct td_token *tok)
{
	struct td_eng_teradimm *td = td_eng_td_hal(eng);
	td_status_t st;
	st.byte = tok->last_status;

	/* We may have timed out simply because we were starved */
	if ( unlikely (td->td_status_ts[td_token_type(tok)] <
				tok->active_timeout) )
			return 0;

#ifdef CONFIG_TERADIMM_RDBUF_TRACKING
	if (td_token_is_read(tok) ) {
		/* special handling for read timeouts in QUEUED state...
		 * Rush will update status to QUEUED when it is waiting for
		 * a read buffer to be released */
		int recovered;

		/* timeout before this state is a timeout */
		if (st.ext.status != TD_RD_STATUS_QUEUED)
			goto common_timeout_handling;

		/* we allow a few attempts of read buffer recovery */
		if (tok->timeout_count > 2)
			goto common_timeout_handling;

		/* there is a chance that we have lost read buffers */
		recovered = td_engine_recover_read_buffer_orphans(eng);
		if (recovered > 0) {
			/* recovered an orphan, give it another chance */
			tok->timeout_count ++;
			td_reset_token_timeout(eng, tok);

			/* we are not done yet */
			return 0;
		}

		/* we were not able to recover a read buffer, give up */
	}
common_timeout_handling:
#endif
	
	td_eng_trace(eng, TR_TOKEN, "timeout:tok", tok->tokid);
	td_eng_trace(eng, TR_TOKEN, "timeout:cmd", tok->cmd_bytes[0]);

	if (tok->ooo_missing) {
		uint64_t timeout;

		/* this is a pre-mature timeout, for OOO */
		eng->td_counters.token.seq_replay_cnt ++;

		td_eng_debug(eng, "re-push missing tok %u cmd %16llx status %02x\n",
				tok->tokid, tok->cmd_bytes[0], st.byte);
		td_eng_trace(eng, TR_TOKEN, "TD:ooo_repush:tok", tok->tokid);

		/* sending a new token is dangerous, since we don't want to
		 * overflow the fifo ...
		 * TODO: make sure we are not in some kind of hold */
		udelay(2);

		td_eng_hal_start_token(eng, tok);

		td_engine_update_token_sent_timestamps(eng, tok);

		/* We want the quick timeout on this again */
		if (tok->to_ssd) {
			if (td_token_is_write(tok) )
				timeout = td_eng_conf_var_get(eng, BIO_W_OOO_TIMEOUT_USEC);
			else
				timeout = td_eng_conf_var_get(eng, BIO_R_OOO_TIMEOUT_USEC);
		} else
			timeout = td_eng_conf_var_get(eng, OOO_TIMEOUT_USEC);

		td_set_token_timeout(tok, timeout);
		return 0;
	}

	if (! tok->quick_n_quiet) {
		teradimm_global_status_t gstatus  = { .u64 = 0};
		td_eng_hal_read_ext_status(eng, TERADIMM_EXT_STATUS_GLOBAL_IDX,
				&gstatus, 0);
		td_eng_trace(eng, TR_TOKEN, "timeout:gsr", gstatus.u64);
		td_eng_warn(eng, "timeout tok %u cmd %016llx status %02x, GSR 0x%016llX [0x%02X]\n",
			tok->tokid, tok->cmd_bytes[0], st.byte, gstatus.u64,
			gstatus.fw);
	}

	eng->td_counters.token.timedout_cnt ++;
	tok->result = TD_TOK_RESULT_TIMEOUT;

#ifdef CONFIG_TERADIMM_RDBUF_TRACKING
	/* when tracking read-buffers, a command with
	 * a deallocation will not be retired as per
	 * user configuration. */
	if (!td_eng_conf_var_get(eng, RETRY_TIMEDOUT_DEALLOCATES)) {
		/* reset command's deallocation for the retry */
		td_cmd_clear_deallocation((void*)&tok->cmd_bytes);
	}
#endif

	return 1;
}

int td_speedup_command_on_ooo_seq(struct td_engine *eng, uint32_t missing_seq, uint64_t timeout)
{
	struct td_token_list *act_tok_list;
	struct td_token *nxt, *otok;
	td_cmd_t *ocmd = NULL;
	int found = 0;

	act_tok_list = &eng->tok_pool[TD_TOK_FOR_FW].td_active_tokens;

	for_each_token_list_token(otok, nxt, act_tok_list) {
		ocmd = (void*)&otok->cmd_bytes;
		if (ocmd->cmd.seq == missing_seq) {

			++ found;
			td_eng_debug(eng, "re-set timeout token %u [%llx] #%u\n", 
					otok->tokid, ocmd->cmd.u64, found);
			td_eng_trace(eng, TR_TOKEN, "TD:sequence:tok", otok->tokid);
			td_eng_trace(eng, TR_TOKEN, "TD:sequence:timeout", timeout);

			otok->ooo_missing = 1;
			td_set_token_timeout(otok, timeout);

			/* SEC needs two replays, otherwise only 1 replay */
			if ( !otok->sec_buddy || found>1 )
				break;
		}
	}

	if (found)
		return found;

	td_eng_debug(eng, "Could not find previous command with sequence "
			"%u (0x%x)\n", missing_seq, missing_seq);

	return 0;
}

static void __td_cmd_ooo_handling (struct td_token *tok, td_status_t *st)
{
	struct td_engine *eng = td_token_engine(tok);
	uint64_t xs = 0;

	td_eng_hal_read_ext_status(eng, tok->tokid, &xs, 1);
	tok->last_xstatus = xs;
	td_eng_debug(eng, "  current %llu, missing %llu (%llu/%llu)\n",
			xs & 0xFFFF, xs >> 32, tok->cmd_seq, tok->cmd_seq &0xFFFF);
	td_eng_trace(eng, TR_TOKEN, "TD:sequence:xstatus", xs);
	td_eng_trace(eng, TR_TOKEN, "TD:sequence:current", xs & 0xFFFF);
	td_eng_trace(eng, TR_TOKEN, "TD:sequence:missing", xs >> 32);

	td_eng_debug(eng, "TOKEN OOO handling tok %u current %llu missing %llu\n", 
			tok->tokid, xs & 0xffff, xs >> 32);

	switch ( td_eng_conf_var_get(eng, OOO_REPLAY) ) {
	case TD_OOO_REPLAY_OFF:
		td_eng_trace(eng, TR_TOKEN, "TD:sequence:no-replay", 0);
		break;

	case TD_OOO_REPLAY_QUICK:
		{
			uint64_t timeout;
			if (tok->to_ssd) {
				if (td_token_is_write(tok) )
					timeout = td_eng_conf_var_get(eng, BIO_W_OOO_TIMEOUT_USEC);
				else
					timeout = td_eng_conf_var_get(eng, BIO_R_OOO_TIMEOUT_USEC);
			} else
				timeout = td_eng_conf_var_get(eng, OOO_TIMEOUT_USEC);
			
			td_eng_debug(eng, "OOO REPLAY for tok %u seq %llu timeout %llu\n",
					tok->tokid, xs>>32, timeout);

			if (timeout) {
				/* the idea here is to give one mor FW status chance before we
				*  timeout the missing seq token, not this one */
				td_speedup_command_on_ooo_seq(eng, xs >> 32, timeout);
			} else
				td_replay_command_on_ooo_token(tok, st->ext.status);
		}
		break;

	case TD_OOO_REPLAY_DELAYED:
		td_set_token_timeout(tok,
			td_cmd_status_timeout(eng, tok, st->ext.status));
		tok->ooo_replay = 1;
		break;
	}

}

int td_replay_command_on_ooo_token(struct td_token *tok, uint8_t last_status_byte)
{
	struct td_engine *eng = td_token_engine(tok);
	struct td_token_list *act_tok_list;
	struct td_token *nxt, *otok;
	uint64_t xs = tok->last_xstatus;
	td_cmd_t *ocmd = NULL;
	int found = 0;

	if (last_status_byte != TD_STATUS_OoO)
		return 0;

	act_tok_list = &eng->tok_pool[TD_TOK_FOR_FW].td_active_tokens;

	for_each_token_list_token(otok, nxt, act_tok_list) {
		ocmd = (void*)&otok->cmd_bytes;
		if (ocmd->cmd.seq == (xs >> 32)) {
			eng->td_counters.token.seq_replay_cnt ++;
			++ found;
			td_eng_debug(eng, "re-pushing token %u [%llx] #%u\n", 
					otok->tokid, ocmd->cmd.u64, found);
			td_eng_trace(eng, TR_TOKEN, "TD:sequence:tok", otok->tokid);
			td_eng_hal_start_token(eng, otok);

			/* SEC needs two replays, otherwise only 1 replay */
			if ( !otok->sec_buddy || found>1 )
				break;
		}
	}

	if (found)
		return found;

	td_eng_debug(eng, "Could not find previous command with sequence "
			"%u (0x%x)\n", (unsigned)(xs >> 32),
			(unsigned)(xs >> 32));

#if 0
	act_tok_list = &eng->tok_pool[TD_TOK_FOR_FW].td_active_tokens;
	for_each_token_list_token(otok, nxt, act_tok_list) {
		td_cmd_t *ocmd = (void*)&otok->cmd_bytes;
		printk("[%u] SEQ %llu (0x%x) CMD %016llx\n", otok->tokid,
			otok->cmd_seq, ocmd->cmd.seq,
			otok->cmd_bytes[0]);
	}
#endif
	return 0;
}

/**
 * Our status handler for BIO reads
 */
static int td_cmd_status_bio_read (struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);
	td_cmd_t *tdcmd = (void*)&tok->cmd_bytes;
	td_status_t st;

	/* read the status */
	st = *((td_status_t*)eng->td_status + tok->tokid);

	/* if it didn't change, there is no need to parser it */
	if (tok->last_status == st.byte)
		goto skip_status_check;

	td_eng_trace(eng, TR_TOKEN, "TD_ENG:rd:status:tok    ", tok->tokid);
	td_eng_trace(eng, TR_TOKEN, "TD_ENG:rd:status:byte   ", st.byte);

	/* store the change for later */
	tok->last_status = st.byte;
	tok->last_xstatus = 0;

	td_tok_event(tok, TD_TOK_EVENT_STATUS, 0);

	/* ignore stale status data */
	if (st.fin.odd != tok->odd)
		goto skip_status_check;

	/* we saw a status update with the correct polarity */
	td_token_received_update(eng, tok);

	if (st.fin.success) {
		/* If we have xstatus, there is an SSD warning to log */
		if (st.fin.extend) {
			td_eng_hal_read_ext_status(eng, tok->tokid, &tok->last_xstatus, 1);
			td_eng_trace(eng, TR_TOKEN, "TD_ENG:rd:status:xstatus", tok->last_xstatus);
			td_eng_err(eng, "SSD Warning, logging issue\n");
			td_eng_err(eng, "Read warning tok %u cmd %016llx\n",
						tok->tokid, tdcmd->cmd.u64);
			td_error_decode_ssd_err(tok->last_xstatus);
		}

		/* READs are finished after _endread() happens later
		 * They will have to free resources after they are
		 * done with it
		 */
		if (!td_eng_using_fwstatus(eng)
				|| td_cmd_is_hardware_only(tdcmd))
			tok->rd_bufid = st.fin.rdbuf;

		td_eng_trace(eng, TR_RDBUF, "rdbuf:drv:stat",
					tok->rd_bufid);

		tok->result = TD_TOK_RESULT_OK;
		return 1;
	}
	
	/* an uncompleted read */
	switch (st.ext.status) {
	case TD_RD_STATUS_SEQUENCED:
		/*
		 * td_cmd_status_timeout can't handle SEQEUNCED,
		 * so we trick it...
		 */
		st.ext.status = TD_RD_STATUS_WAIT_SSD;
		/* And fall through to normal WAIT_SSD handling */
	case TD_RD_STATUS_RECEIVED:
	case TD_RD_STATUS_WAIT_SSD:
	case TD_RD_STATUS_QUEUED:
		/* Advance our timeout to the next stage timeout */
		td_set_token_timeout(tok,
			td_cmd_status_timeout(eng, tok, st.ext.status));
		break;

	case TD_RD_STATUS_SSD_WARNING:
		/* WARNING,  FW had to re-do SATA for this  */
		td_eng_err(eng, "FLASH read warning %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid, tdcmd->cmd.u64);
		if (st.ext.extend) {
			td_eng_hal_read_ext_status(eng, tok->tokid, &tok->last_xstatus, 1);
			td_eng_trace(eng, TR_TOKEN, "TD_ENG:rd:status:xstatus   ", tok->last_xstatus);
			td_error_decode_ssd_err(tok->last_xstatus);
		}
		/* Reset our timeout */
		td_set_token_timeout(tok,
			td_cmd_status_timeout(eng, tok, TD_WR_STATUS_WAIT_SSD));
		break;

	case TD_RD_STATUS_ILLEGAL_CMD_ERR:
		td_eng_debug(eng, "FLASH Read F-code tok %u cmd %016llx\n",
				tok->tokid, tdcmd->cmd.u64);
		return __td_cmd_fcode_handling(eng, tok);

	case TD_RD_STATUS_HDATA_DMA_XSUM_ERR:
	case TD_RD_STATUS_HCMD_ECC_ERR:
	case TD_RD_STATUS_UNKNOWN_HW_CMD_ERR:
		td_eng_err(eng, "BETA read error %02x tok %u\n",
				st.byte,
				tok->tokid);
		tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
		eng->td_counters.token.cmd_error_cnt ++;
		return 1;

	case TD_RD_STATUS_EXE_ERROR:
		/* HALT Driver */
		if (tdcmd->cmd.to_ssd) {
			td_eng_err(eng, "SSD Error! Halting device!\n");
			td_eng_err(eng, "FLASH Read Error %02x tok %u cmd %016llx\n",
					st.byte, tok->tokid,
					tdcmd->cmd.u64);
			if (st.ext.extend) {
				td_eng_hal_read_ext_status(eng, tok->tokid, &tok->last_xstatus, 1);
				td_eng_trace(eng, TR_TOKEN, "TD_ENG:rd:status:xstatus   ", tok->last_xstatus);
				td_error_decode_ssd_err(tok->last_xstatus);
			}

			tok->result = TD_TOK_RESULT_FAIL_ABORT_HARD;
		} else
			tok->result = TD_TOK_RESULT_FAIL_ABORT;
		return 1;

	case TD_RD_STATUS_COLLISION:
		eng->td_counters.token.cmd_error_cnt ++;
		td_eng_warn(eng, "FLASH Read correctable error: collision %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid,
				tok->cmd_bytes[0]);
		td_eng_trace(eng, TR_TOKEN, "TD:collision_err:tok", tok->tokid);
		tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
		return 1;

	case TD_RD_STATUS_FIELD_ERR:
		eng->td_counters.token.cmd_error_cnt ++;
		td_eng_warn(eng, "FLASH Read correctable field error %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid,
				tok->cmd_bytes[0]);
		td_eng_trace(eng, TR_TOKEN, "TD:field_err:tok", tok->tokid);
		tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
		return 1;
		
	case TD_RD_STATUS_OoO_DUP_ERR:
		eng->td_counters.token.seq_ooo_cnt ++;
		td_eng_debug(eng, "FLASH Read correctable sequence error %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid,
				tok->cmd_bytes[0]);
		/* This is a read, BIO better still be here */
		tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
		return 1;

	case TD_RD_STATUS_OoO:
		/* TODO: abstract this code into a function so it's shared
		 * between read/write/control paths */
		eng->td_counters.token.seq_ooo_cnt ++;
		td_eng_debug(eng, "FLASH Read sequence hiccup %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid, tdcmd->cmd.u64);
		td_eng_trace(eng, TR_TOKEN, "TD:sequence:rd:error", st.byte);
		if (st.fin.extend)
			__td_cmd_ooo_handling(tok, &st);

		/* And we keep waiting here....*/
		break;

	default:
		td_eng_warn(eng, "FLASH Read unknown status %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid, tdcmd->cmd.u64);
		/* Nothing to do, just keep waiting... */
		break;
	}

skip_status_check:
	if (td_lost_command_refresh(eng, tok))
		return 0;

	/* check timeout */
	if (likely (!td_has_token_timedout(tok)))
		return 0;

	return __td_cmd_timeout_handling(eng, tok);
}

/**
 * Our status handler for BIO writes
 *  - Writes can be early_committed, based on lots of stuff...
 */
static int td_cmd_status_bio_write (struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);
	td_cmd_t *tdcmd = (void*)&tok->cmd_bytes;
	td_status_t st;

	/* read the status */
	st = *((td_status_t*)eng->td_status + tok->tokid);

	/* if it didn't change, there is no need to parser it */
	if (tok->last_status == st.byte)
		goto skip_status_check;

	td_eng_trace(eng, TR_TOKEN, "TD_ENG:wr:status:tok    ", tok->tokid);
	td_eng_trace(eng, TR_TOKEN, "TD_ENG:wr:status:byte   ", st.byte);

	/* store the change for later */
	tok->last_status = st.byte;
	tok->last_xstatus = 0;

	td_tok_event(tok, TD_TOK_EVENT_STATUS, 0);

	/* ignore stale status data */
	if (st.fin.odd != tok->odd)
		goto skip_status_check;

	/* we saw a status update with the correct polarity */
	td_token_received_update(eng, tok);

	if (st.fin.success) {
		struct td_eng_teradimm *td = td_eng_td_hal(eng);
		/* If we have xstatus, there is an SSD warning to log */
		if (st.fin.extend) {
			td_eng_hal_read_ext_status(eng, tok->tokid, &tok->last_xstatus, 1);
			td_eng_trace(eng, TR_TOKEN, "TD_ENG:wr:status:xstatus", tok->last_xstatus);
			td_eng_err(eng, "SSD Warning, logging issue\n");
			td_eng_err(eng, "FLASH Write warning tok %u cmd %016llx\n",
						tok->tokid, tdcmd->cmd.u64);
			td_error_decode_ssd_err(tok->last_xstatus);
		}


		/* writes can be finalized now */
		td_free_all_buffers(eng, tok);
		
		tok->result = TD_TOK_RESULT_OK;

		if (td->td_tok_retries[tok->tokid].bad_xsum)
			eng->td_counters.token.badxsum_recovered_cnt ++;
		if (td->td_tok_retries[tok->tokid].wep_timeout)
			eng->td_counters.token.weptimeout_recovered_cnt ++;
		return 1;
	}

	/* an uncompleted write */
	switch (st.ext.status) {
	default:
		/* nothing to do here (keep waiting) */
		break;

	case TD_WR_STATUS_SEQUENCED:
		/*
		 * td_cmd_status_timeout can't handle SEQUENCED,
		 * So we trick it
		 */
		st.ext.status = TD_WR_STATUS_WAIT_SSD;
		/* And fall through to normal WAIT_SSD handling */
	case TD_WR_STATUS_WAIT_SSD:
	case TD_WR_STATUS_RECEIVED:
	case TD_WR_STATUS_QUEUED:
		td_free_wr_buffers(eng, tok);

		if (tok->host.bio && tok->ops.early_commit
				&& td_bio_flags_ref(tok->host.bio)->commit_level
					>= td_cmd_status_commit(st) ) {

			tok->ops.early_commit(tok, 0);
			tok->ops.early_commit = NULL;
		}

		td_set_token_timeout(tok,
			td_cmd_status_timeout(eng, tok, st.ext.status));
		tok->safe_in_hw = 1;
		break;

	case TD_WR_STATUS_SSD_WARNING:
		/* FW had to reset sata for us... */
		td_eng_err(eng, "FLASH Write Warning %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid, tdcmd->cmd.u64);
		if (st.ext.extend) {
			td_eng_hal_read_ext_status(eng, tok->tokid, &tok->last_xstatus, 1);
			td_eng_trace(eng, TR_TOKEN, "TD_ENG:wr:status:xstatus   ", tok->last_xstatus);
			td_error_decode_ssd_err(tok->last_xstatus);
		}
		/* Reset our timeout */
		td_set_token_timeout(tok,
			td_cmd_status_timeout(eng, tok, TD_WR_STATUS_WAIT_SSD));
		/* Just in case we missed the normal ones */
		tok->safe_in_hw = 1;
		break;

	case TD_WR_STATUS_ILLEGAL_CMD_ERR:
		/*
		 * Bit error in the CMD, hardware made this an f-code
		 * FW Should have set extend, and the original command should
		 * be there
		 */
		td_eng_debug(eng, "FLASH Write F-code tok %u cmd %016llx\n",
				tok->tokid, tdcmd->cmd.u64);
		return __td_cmd_fcode_handling(eng, tok);

	case TD_WR_STATUS_HDATA_ECC_ERR:
		/* Beta 4 style HW ECC */
		td_eng_err(eng, "BETA ECC DATA error %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid, tdcmd->cmd.u64);
		eng->td_counters.token.data_ecc_error_cnt ++;
		if (td_eng_conf_var_get(eng, XSUM_ERR_RETRIES)) {
			tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
			return 1;
		}

		tok->result = TD_TOK_RESULT_FAIL_ABORT;
		return 1;

	case TD_WR_STATUS_HCMD_ECC_ERR:
		td_eng_err(eng, "BETA ECC CMD error %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid, tdcmd->cmd.u64);
		/* command failed ECC, increment, retry */
		eng->td_counters.token.cmd_ecc_error_cnt ++;
		if (td_eng_conf_var_get(eng, XSUM_ERR_RETRIES)) {
			tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
			return 1;
		}

		tok->result = TD_TOK_RESULT_FAIL_ABORT;
		return 1;

	case TD_WR_STATUS_EXE_ERROR:
		if (tdcmd->cmd.to_ssd)
		{
			/* HALT Driver */
			td_eng_err(eng, "SSD Error! Halting device!\n");
			td_eng_err(eng, "FLASH Write Error %02x tok %u cmd %016llx\n",
					st.byte, tok->tokid,
				tdcmd->cmd.u64);
			if (st.ext.extend) {
				td_eng_hal_read_ext_status(eng, tok->tokid, &tok->last_xstatus, 1);
				td_error_decode_ssd_err(tok->last_xstatus);
			}
			tok->result = TD_TOK_RESULT_FAIL_ABORT_HARD;
		} else
			tok->result = TD_TOK_RESULT_FAIL_ABORT;

		return 1;

	case TD_WR_STATUS_HDATA_DMA_XSUM_ERR:
		/* this error is issued on a write command which fails
			* checksum, it's possible that the data was held in the
			* write back buffer... try to send it again. */
		eng->td_counters.token.badxsum_cnt ++;
		if (td_eng_conf_var_get(eng, XSUM_ERR_RETRIES)) {
			td_eng_err(eng, "BETA correctable write xsum error %02x tok %u cmd %016llx\n",
					st.byte, tok->tokid,
					tdcmd->cmd.u64);
			tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
			return 1;
		}
		/* fall through desired */

	case TD_WR_STATUS_UNKNOWN_HW_CMD_ERR:

		td_eng_err(eng, "write error %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid,
				tdcmd->cmd.u64);
#ifdef CONFIG_TERADIMM_HALT_ON_WRITE_ERROR
		tok->result = TD_TOK_RESULT_FAIL_ABORT_HARD;
#else
		tok->result = TD_TOK_RESULT_FAIL_ABORT;
#endif
		if (st.ext.extend) {
		}
		return 1;

	case TD_STATUS_COLLISION:
		eng->td_counters.token.cmd_error_cnt ++;
		td_eng_err(eng, "FLASH Write: Command collision error %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid,
				tok->cmd_bytes[0]);
		td_eng_trace(eng, TR_TOKEN, "TD:collision_err:tok", tok->tokid);
		tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
		return 1;

	case TD_STATUS_FIELD_ERR:
		eng->td_counters.token.cmd_error_cnt ++;
		td_eng_trace(eng, TR_TOKEN, "TD:field_err:tok", tok->tokid);
		if (! tok->host.bio) {
			/*
			 * BIO was early committed... 
			 * We still have the core buffer assigned to us, but the
			 * WEP would have previously been returned already and
			 * may have been reused.  But our data reached queued,
			 * so should be in the CB already already.  So we
			 * adjust the command now before it's retried.
			 */
			td_eng_err(eng, "FLASH Write correctable field error %02x tok %u cmd %016llx after COMMIT\n",
				st.byte, tok->tokid,
				tok->cmd_bytes[0]);
			tok->len_host_to_dev = 0;
			tdcmd->cmd.decode.from_host = 0;
		} else {
			/* If the BIO is still here, we missed queued, and can
			 * just replay it like normal, we still have the WEP
			 * and data to write */
			td_eng_err(eng, "FLASH Write correctable field error %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid,
				tok->cmd_bytes[0]);
		}
		tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
		return 1;
		
	case TD_STATUS_OoO_DUP_ERR:
		if (tok->sec_buddy) {
			tok->result = TD_TOK_RESULT_OK;
			return 1;
		}
		
		eng->td_counters.token.seq_dup_cnt ++;
		if (tok->host.bio) {
			tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
			return 1;
		}
		td_eng_err(eng, "FLASH Write sequence error %02x tok %u cmd %016llx\n",
			st.byte, tok->tokid,
			tdcmd->cmd.u64);
		
#ifdef CONFIG_TERADIMM_HALT_ON_WRITE_ERROR
		tok->result = TD_TOK_RESULT_FAIL_ABORT_HARD;
#else
		tok->result = TD_TOK_RESULT_FAIL_ABORT;
#endif
		break;

	case TD_STATUS_OoO:
		/* TODO: abstract this code into a function so it's shared
		 * between read/write/control paths */
		eng->td_counters.token.seq_ooo_cnt ++;
		td_eng_debug(eng, "FLASH Write sequence hiccup %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid, tdcmd->cmd.u64);
		td_eng_trace(eng, TR_TOKEN, "TD:sequence:wr:error", st.byte);
		if (st.fin.extend)
			__td_cmd_ooo_handling(tok, &st);
		/* And if we OOO, we obviously made it to HW */
		tok->safe_in_hw = 1;
	}
skip_status_check:
	if (td_lost_command_refresh(eng, tok))
		return 0;

	/* check timeout */
	if (likely (!td_has_token_timedout(tok)))
		return 0;

	return __td_cmd_timeout_handling(eng, tok);
}

/**
 * Our status handler for BIO trims
 */
static int td_cmd_status_bio_trim (struct td_token *tok)
{
	return td_cmd_status_check(tok);
}

int td_cmd_status_check(struct td_token *tok)
{
	struct td_engine *eng = td_token_engine(tok);
	td_status_t st;
	td_cmd_t *tdcmd = (void*)&tok->cmd_bytes;

	/* read the status */
	st = *((td_status_t*)eng->td_status + tok->tokid);

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	/* it may be necessary to send a hardware command on a FW token, for
	 * example to reset it.  this code forces a status read from status
	 * buffer for such an occasion. */
	if (td_eng_using_fwstatus(eng)
			&& td_token_type(tok) == TD_TOK_FOR_FW
			&& td_cmd_is_hardware_only(tdcmd))
		teradimm_force_read_real_status(eng, tok->tokid, &st.byte);
#endif

	/* if it didn't change, there is no need to parser it */
	if (tok->last_status == st.byte)
		goto skip_status_check;

	td_eng_trace(eng, TR_TOKEN, "TD_ENG:status:tok    ", tok->tokid);
	td_eng_trace(eng, TR_TOKEN, "TD_ENG:status:byte   ", st.byte);

	/* store the change for later */
	tok->last_status = st.byte;
	tok->last_xstatus = 0;

	td_tok_event(tok, TD_TOK_EVENT_STATUS, 0);

	/* ignore stale status data */
	if (st.fin.odd != tok->odd)
		goto skip_status_check;

	/* we saw a status update with the correct polarity */
	td_token_received_update(eng, tok);

	/* handle extended status */
	if (st.fin.extend) {
		td_eng_hal_read_ext_status(eng, tok->tokid, &tok->last_xstatus, 1);
		td_eng_trace(eng, TR_TOKEN, "TD_ENG:status:xstatus", tok->last_xstatus);
		if (tok->host.ucmd)
			tok->host.ucmd->ioctl.xstatus = tok->last_xstatus;
	}

	if (st.fin.success) {
		/* reeds are finished after _endread() happens below,
		 * writes and control messages can be finalized now */
		if (td_token_is_read(tok)) {
			if (!td_eng_using_fwstatus(eng)
					|| td_cmd_is_hardware_only(tdcmd))
				tok->rd_bufid = st.fin.rdbuf;

			td_eng_trace(eng, TR_RDBUF, "rdbuf:drv:stat",
					tok->rd_bufid);

		} else
			td_free_all_buffers(eng, tok);

		tok->result = TD_TOK_RESULT_OK;
		return 1;

	} else if (td_token_is_write(tok)) {
		/* an uncompleted write */
		switch (st.ext.status) {
		default:
			/* nothing to do here (keep waiting) */
			break;

		case TD_WR_STATUS_WAIT_SSD:
		case TD_WR_STATUS_RECEIVED:
		case TD_WR_STATUS_QUEUED:
			td_set_token_timeout(tok,
				td_cmd_status_timeout(eng, tok, st.ext.status));
		case TD_WR_STATUS_SEQUENCED: /* No timeout for this */
			td_free_wr_buffers(eng, tok);

			if (tok->host.bio && tok->ops.early_commit
					&& td_bio_flags_ref(tok->host.bio)->commit_level
						>= td_cmd_status_commit(st) ) {

				tok->ops.early_commit(tok, 0);
				tok->ops.early_commit = NULL;
			}
			break;

		case TD_WR_STATUS_HDATA_ECC_ERR:
			/* write data failed ECC, increment, retry */
			eng->td_counters.token.data_ecc_error_cnt ++;
			if (td_eng_conf_var_get(eng, XSUM_ERR_RETRIES)) {
				tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
				return 1;
			}

			tok->result = TD_TOK_RESULT_FAIL_ABORT;
			return 1;

		case TD_WR_STATUS_HCMD_ECC_ERR:
			/* command failed ECC, increment, retry */
			eng->td_counters.token.cmd_ecc_error_cnt ++;
			if (td_eng_conf_var_get(eng, XSUM_ERR_RETRIES)) {
				tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
				return 1;
			}

			tok->result = TD_TOK_RESULT_FAIL_ABORT;
			return 1;

		case TD_WR_STATUS_EXE_ERROR:
			if (tdcmd->cmd.to_ssd)
			{
				/* HALT Driver */
				uint64_t xs = 0;

				td_eng_err(eng, "SSD Error! Halting device!\n");
				td_eng_err(eng, "Write Error %02x tok %u cmd %016llx\n",
						st.byte, tok->tokid,
					tdcmd->cmd.u64);
				td_eng_hal_read_ext_status(eng, tok->tokid, &xs, 1);
				td_error_decode_ssd_err(xs);
				tok->result = TD_TOK_RESULT_FAIL_ABORT_HARD;
			} else
				tok->result = TD_TOK_RESULT_FAIL_ABORT;

			return 1;

		case TD_WR_STATUS_SSD_WARNING:
			td_eng_err(eng, "SSD Warning, logging issue\n");
			td_eng_err(eng, "Write Warning %02x tok %u cmd %016llx\n",
					st.byte, tok->tokid, tdcmd->cmd.u64);
			{
				uint64_t xs = 0;
				td_eng_hal_read_ext_status(eng, tok->tokid, &xs, 1);
				td_error_decode_ssd_err(xs);
			}
			break;

		case TD_WR_STATUS_HDATA_DMA_XSUM_ERR:
			/* this error is issued on a write command which fails
			 * checksum, it's possible that the data was held in the
			 * write back buffer... try to send it again. */
			eng->td_counters.token.badxsum_cnt ++;
			if (td_eng_conf_var_get(eng, XSUM_ERR_RETRIES)) {
				td_eng_err(eng, "correctable write error %02x tok %u cmd %016llx\n",
						st.byte, tok->tokid,
						tdcmd->cmd.u64);
				tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
				return 1;
			}
			/* fall through desired */

		case TD_WR_STATUS_ILLEGAL_CMD_ERR:
			td_eng_err(eng, "write f-code tok %u cmd %016llx\n",
					tok->tokid, tdcmd->cmd.u64);
			return __td_cmd_fcode_handling(eng, tok);

		case TD_WR_STATUS_UNKNOWN_HW_CMD_ERR:

			td_eng_err(eng, "write error %02x tok %u cmd %016llx\n",
					st.byte, tok->tokid,
					tdcmd->cmd.u64);
#ifdef CONFIG_TERADIMM_HALT_ON_WRITE_ERROR
			tok->result = TD_TOK_RESULT_FAIL_ABORT_HARD;
#else
			tok->result = TD_TOK_RESULT_FAIL_ABORT;
#endif
			return 1;

		case TD_WR_STATUS_OoO_DUP_ERR:
			if (tok->sec_buddy) {
				tok->result = TD_TOK_RESULT_OK;
				return 1;
			}
		}

	} else if (td_token_is_read(tok)) {

		/* an uncompleted read */

		switch (st.ext.status) {
		case TD_RD_STATUS_SEQUENCED:
		default:
			/* nothing to do here (keep waiting) */
			break;

		case TD_RD_STATUS_QUEUED:
		case TD_RD_STATUS_RECEIVED:
		case TD_RD_STATUS_WAIT_SSD:
			/* Advance our timeout to the next stage timeout */
			td_set_token_timeout(tok, td_cmd_status_timeout(eng,
						tok, st.ext.status));
			break;

		case TD_RD_STATUS_SSD_WARNING:
			td_eng_err(eng, "SSD Warning, logging issue\n");
			td_eng_err(eng, "Read warning %02x tok %u cmd %016llx\n",
					st.byte, tok->tokid, tdcmd->cmd.u64);
			{
				uint64_t xs = 0;
				td_eng_hal_read_ext_status(eng, tok->tokid, &xs, 1);
				td_error_decode_ssd_err(xs);
			}
			break;

		case TD_RD_STATUS_ILLEGAL_CMD_ERR:
			td_eng_err(eng, "read f-code tok %u cmd %016llx\n",
					tok->tokid, tdcmd->cmd.u64);
			return __td_cmd_fcode_handling(eng, tok);

		case TD_RD_STATUS_HDATA_DMA_XSUM_ERR:
		case TD_RD_STATUS_HCMD_ECC_ERR:
		case TD_RD_STATUS_UNKNOWN_HW_CMD_ERR:
			td_eng_err(eng, "BETA read error %02x tok %u\n",
					st.byte,
					tok->tokid);
			tok->result = TD_TOK_RESULT_FAIL_ABORT;
			eng->td_counters.token.cmd_error_cnt ++;
			return 1;

		case TD_RD_STATUS_OoO_DUP_ERR:
			td_eng_err(eng, "read dup error %02x tok %u\n",
					st.byte,
					tok->tokid);
			tok->result = TD_TOK_RESULT_FAIL_ABORT;
			eng->td_counters.token.seq_dup_cnt ++;
			return 1;

		case TD_RD_STATUS_EXE_ERROR:
			/* HALT Driver */
			if (tdcmd->cmd.to_ssd) {
				td_eng_err(eng, "SSD Error! Halting device!\n");
				td_eng_err(eng, "Read Error %02x tok %u cmd %016llx\n",
						st.byte, tok->tokid,
						tdcmd->cmd.u64);
				{
					uint64_t xs = 0;
					td_eng_hal_read_ext_status(eng, tok->tokid, &xs, 1);
					td_error_decode_ssd_err(xs);
				}

				tok->result = TD_TOK_RESULT_FAIL_ABORT_HARD;
			} else
				tok->result = TD_TOK_RESULT_FAIL_ABORT;
			return 1;
		}
	} else {
		/* an uncompleted control message */
		switch (st.ext.status) {
		case TD_STATUS_SEQUENCED:
		case TD_STATUS_OoO:
			/* Sometimes we get these..., on SSD_CMD that don't
			 * have data */
			break;

		case TD_STATUS_QUEUED:
		case TD_STATUS_RECEIVED:
		case TD_STATUS_PENDING:
			/* nothing to do here (keep waiting) */
			if (tok->quick_n_quiet)
				td_set_token_timeout(tok, TD_TOKEN_QUICK_N_QUIET_TIMEOUT);
			else
				td_set_token_timeout(tok, td_cmd_status_timeout(eng,
						tok, st.ext.status));
			break;

		case TD_STATUS_ILLEGAL_CMD_ERR:
			td_eng_err(eng, "cmd error %02x tok %u cmd %016llx\n",
					st.byte, tok->tokid, tdcmd->cmd.u64);
			return __td_cmd_fcode_handling(eng, tok);

		case TD_STATUS_EXE_ERR: /*download microcode failure and ??? */
		default:
			td_eng_err(eng, "control error %02x tok %u cmd %016llx\n",
					st.byte, tok->tokid,
					tdcmd->cmd.u64);
			tok->result = TD_TOK_RESULT_FAIL_ABORT;
			return 1;
		}
	}

	switch (st.ext.status) {
	case TD_STATUS_COLLISION:
		eng->td_counters.token.cmd_error_cnt ++;
		td_eng_err(eng, "FW: Command collision error %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid,
				tok->cmd_bytes[0]);
		td_eng_trace(eng, TR_TOKEN, "TD:collision_err:tok", tok->tokid);
		tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
		return 1;

	case TD_STATUS_FIELD_ERR:
		eng->td_counters.token.cmd_error_cnt ++;
		td_eng_err(eng, "correctable cmd error %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid,
				tok->cmd_bytes[0]);
		td_eng_trace(eng, TR_TOKEN, "TD:field_err:tok", tok->tokid);
		tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
		return 1;
		
	case TD_STATUS_OoO_DUP_ERR:
		eng->td_counters.token.seq_dup_cnt ++;
		if (tok->host.bio) {
			tok->result = TD_TOK_RESULT_FAIL_CAN_RETRY;
			return 1;
		}
		td_eng_err(eng, "sequence error %02x tok %u cmd %016llx\n",
			st.byte, tok->tokid,
			tdcmd->cmd.u64);
		
#ifdef CONFIG_TERADIMM_HALT_ON_WRITE_ERROR
		tok->result = TD_TOK_RESULT_FAIL_ABORT_HARD;
#else
		tok->result = TD_TOK_RESULT_FAIL_ABORT;
#endif
		break;

	case TD_STATUS_OoO:
		/* TODO: abstract this code into a function so it's shared
		 * between read/write/control paths */
		eng->td_counters.token.seq_ooo_cnt ++;
		td_eng_warn(eng, "Control sequence hiccup %02x tok %u cmd %016llx\n",
				st.byte, tok->tokid, tdcmd->cmd.u64);
		td_eng_trace(eng, TR_TOKEN, "TD:sequence:ctl:error", st.byte);
		if (st.fin.extend)
			__td_cmd_ooo_handling(tok, &st);
	}

skip_status_check:
	if (td_lost_command_refresh(eng, tok))
		return 0;

	/* check timeout */
	if (likely (!td_has_token_timedout(tok)))
		return 0;

	/* TIMEOUT */
	return __td_cmd_timeout_handling(eng, tok);
}

int td_cmd_gen_bio(struct td_engine *eng, struct td_token *tok)
{
	td_cmd_t *tdcmd = (void*)&tok->cmd_bytes;
	td_bio_ref bio = tok->host.bio;

	WARN_ON(!bio);
	if (!bio) {
		td_eng_trace(eng, TR_CMD, "BUG:TD:gen_bio:bio==0", tok->tokid);
		return -EINVAL;
	}

	/* megadimm doesn't support bi-directional commands */
	if (tok->len_host_to_dev && tok->len_dev_to_host) {
		td_eng_trace(eng, TR_CMD, "BUG:TD:gen_bio:bidirectional", (long)bio);
		td_eng_err(eng, "bio bi-directional!?\n");
		return -EINVAL;
	}

	if (bio && td_bio_is_discard(bio))
	{
		/* Construct the command. */
		td_cmdgen_trim(tok->cmd_bytes, (uint8_t)tok->port, 1);

		tdcmd->src.bufid = (uint8_t)tok->core_bufid;
		tdcmd->src.wep = tok->wr_bufid;
		tok->ops.status_check = td_cmd_status_bio_trim;

	} else if (tok->len_host_to_dev) {
		/* Working on a WRITE bio */
		tok->ops.status_check = td_cmd_status_bio_write;
#ifdef CONFIG_TERADIMM_USES_CORE_BUFFS_ONLY
		if (td_eng_conf_var_get(eng, CORE_ONLY)) {
			uint core_bufs = (uint)td_eng_conf_var_get(eng, CORE_BUFS);
			uint64_t sector = td_bio_get_sector_offset(bio);
			if (0 == core_bufs)
				core_bufs = 1;
			tdcmd->cmd.id = TD_CMD_TEST_WRITE;

			tdcmd->cmd.decode.dupcheck = 1;
			tdcmd->cmd.decode.from_host = 1;
			tdcmd->cmd.decode.data_size = TD_DEC_DATA_4K;
			if ( td_eng_conf_hw_var_get(eng, E2E_MODE) ||
				   ( td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE) == 512
				     && td_eng_conf_hw_var_get(eng, HW_SECTOR_METADATA)) )
				tdcmd->cmd.decode.meta_size = TD_DEC_META_128;
			else
				tdcmd->cmd.decode.meta_size = TD_DEC_META_NONE;

			tdcmd->src.bufid = (uint8_t)((sector/8) % core_bufs);
			tdcmd->src.wep = tok->wr_bufid;

			td_eng_trace(eng, TR_CMD, "FORCE:CMD==TEST_WRITE", tdcmd->cmd.id);
		} else
#endif
		if( tok->len_host_to_dev == TERADIMM_DATA_BUF_SIZE) {
			/*
			 * WR_FINAL is the 4K optimized version.  Everything
			 * must be 4K, and bcnt is 0
			 */
			tdcmd->cmd.id = TD_CMD_WR_FINAL;
			tdcmd->cmd.port = tok->port;
			tdcmd->cmd.to_ssd = 1;

			tdcmd->cmd.decode.dupcheck = 1;
			tdcmd->cmd.decode.from_host = 1;
			tdcmd->cmd.decode.data_size = TD_DEC_DATA_4K;
			if ( td_eng_conf_hw_var_get(eng, E2E_MODE) ||
				   ( td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE) == 512
				     && td_eng_conf_hw_var_get(eng, HW_SECTOR_METADATA)) )
				tdcmd->cmd.decode.meta_size = TD_DEC_META_128;
			else
				tdcmd->cmd.decode.meta_size = TD_DEC_META_NONE;

			tdcmd->src.bufid = (uint8_t)tok->core_bufid;
			tdcmd->src.wep = tok->wr_bufid;
			tdcmd->src.bcnt = 0;
			tdcmd->dst.lba.lba = tok->lba;
		} else {
			/*
			 * WR_EXT is 512B command. bcnt is how many
			 * 512B sectors.  But we know we are less than 8,
			 * or we would be above using WR_FINAL.
			 */
			tdcmd->src.bufid = (uint8_t)tok->core_bufid;
			tdcmd->src.wep = tok->wr_bufid;
			tdcmd->src.bcnt = (uint8_t)(tok->len_host_to_dev / td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE));
			tdcmd->dst.lba.lba = tok->lba;


			tdcmd->cmd.decode.dupcheck = 1;
			tdcmd->cmd.decode.from_host = 1;
			tdcmd->cmd.decode.data_size = (uint8_t)(td_dec_data_size(tdcmd->src.bcnt) * 
					(td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE) +
					td_eng_conf_hw_var_get(eng, HW_SECTOR_METADATA)));

			tdcmd->cmd.decode.meta_size = TD_DEC_META_NONE;

			tdcmd->cmd.to_ssd = 1;
			tdcmd->cmd.port = tok->port;
			tdcmd->cmd.id = TD_CMD_WR_EXT;
		}

		switch (tok->magic_flags) {
		case 15:
			tdcmd->cmd.id = TD_CMD_TEST_WRITE;
			td_eng_trace(eng, -1, "FORCE:CMD==TEST_WRITE", tdcmd->cmd.id);
			break;
		case 14:
			tdcmd->cmd.id = TD_CMD_SATA_NOWRITE;
			td_eng_trace(eng, -1, "FORCE:CMD==SATA_NOWRITE", tdcmd->cmd.id);
			break;
		}
		if (TE_INJECT(eng, TE_LBA) ) {
			td_eng_info(eng, "EI: -1 LBA on write dst tok %u\n", tok->tokid);
			td_eng_trace(eng, TR_TOKEN, "error:lba", tok->tokid);
			tdcmd->dst.lba.lba = -1;
			eng->td_errors_count --;
		}

		/* for TripleSEC, need to also fill in the extra WEP */
		if (tok->sec_buddy) {
			switch (td_bio_flags_ref(bio)->commit_level) {
			case TD_TRIPLE_SEC:
				if (TD_IS_WR_BUFID_VALID(tok->extra_wr_bufid)) {
					tdcmd->src.sec_mode = TD_SEC_MODE_TRIPLE;
					tdcmd->src.sec_pfail_wep
						= tok->extra_wr_bufid;
					break;
				}
				/* fall back to double */
			case TD_SUPER_EARLY_COMMIT:
				tdcmd->src.sec_mode = TD_SEC_MODE_DOUBLE;
				break;
			default:
				tdcmd->src.sec_mode = TD_SEC_MODE_DISABLED;
				break;
			}
		} else {
			tdcmd->src.sec_mode = TD_SEC_MODE_DISABLED;
		}

	} else if (tok->len_dev_to_host) {
		/* Working on a READ bio */
		tok->ops.status_check = td_cmd_status_bio_read;
#ifdef CONFIG_TERADIMM_USES_CORE_BUFFS_ONLY
		if (td_eng_conf_var_get(eng, CORE_ONLY)) {
			uint core_bufs = (uint)td_eng_conf_var_get(eng, CORE_BUFS);
			uint64_t sector = td_bio_get_sector_offset(bio);
			if (0 == core_bufs)
				core_bufs = 1;
			tdcmd->cmd.id = TD_CMD_TEST_READ;

			tdcmd->cmd.decode.dupcheck = 1;
			tdcmd->cmd.decode.to_host = 1;
			tdcmd->cmd.decode.data_size = TD_DEC_DATA_4K;
			if ( td_eng_conf_hw_var_get(eng, E2E_MODE) ||
				   ( td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE) == 512
				     && td_eng_conf_hw_var_get(eng, HW_SECTOR_METADATA)) )
				tdcmd->cmd.decode.meta_size = TD_DEC_META_128;
			else
				tdcmd->cmd.decode.meta_size = TD_DEC_META_NONE;


			tdcmd->dst.bufid = (uint8_t)((sector/8) % core_bufs);

			td_eng_trace(eng, TR_CMD, "FORCE:CMD==TEST_READ", tdcmd->cmd.id);
		} else
#endif
		if (tok->len_dev_to_host == TERADIMM_DATA_BUF_SIZE) {
			/*
			* RD_PAGE is the 4K optimized version.  Everything must be
			* 4K, and bcnt is 0
			*/
			tdcmd->cmd.id = TD_CMD_RD_PAGE;
			tdcmd->cmd.port = tok->port;
			tdcmd->cmd.to_ssd = 1;

			tdcmd->cmd.decode.dupcheck = 1;
			tdcmd->cmd.decode.to_host = 1;
			tdcmd->cmd.decode.data_size = TD_DEC_DATA_4K;
			if ( td_eng_conf_hw_var_get(eng, E2E_MODE) ||
				   ( td_eng_conf_hw_var_get(eng, BIO_SECTOR_SIZE) == 512
				     && td_eng_conf_hw_var_get(eng, HW_SECTOR_METADATA)) )
				tdcmd->cmd.decode.meta_size = TD_DEC_META_128;
			else
				tdcmd->cmd.decode.meta_size = TD_DEC_META_NONE;

			tdcmd->dst.bufid = (uint8_t)tok->core_bufid;
			tdcmd->dst.bcnt = 0;
			tdcmd->src.lba.lba = tok->lba;
		} else {
			/*
			* RD_EXT is the 512B version.  bcnt is how many
			* sectors.  Again, we know it's less than 8, so
			* data+MD all fits in the CB
			*/
			tdcmd->dst.bufid = (uint8_t)tok->core_bufid;
			tdcmd->dst.bcnt = (uint8_t)(tok->len_dev_to_host / td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE));
			tdcmd->src.lba.lba = tok->lba;

			tdcmd->cmd.decode.dupcheck = 1;
			tdcmd->cmd.decode.to_host = 1;
			tdcmd->cmd.decode.data_size = (uint8_t)(td_dec_data_size(tdcmd->dst.bcnt) * 
					(td_eng_conf_hw_var_get(eng, HW_SECTOR_SIZE) +
					td_eng_conf_hw_var_get(eng, HW_SECTOR_METADATA)));

			tdcmd->cmd.id = TD_CMD_RD_EXT;
			tdcmd->cmd.to_ssd = 1;
			tdcmd->cmd.port = tok->port;

		}

		switch (tok->magic_flags) {
		case 15:
			tdcmd->cmd.id = TD_CMD_TEST_READ;
			td_eng_trace(eng, -1, "FORCE:CMD==TEST_READ", tdcmd->cmd.id);
			break;
		case 14:
			tdcmd->cmd.id = TD_CMD_SATA_NOREAD;
			td_eng_trace(eng, -1, "FORCE:CMD==SATA_NOREAD", tdcmd->cmd.id);
			break;
		}

		if (TE_INJECT(eng, TE_LBA) ) {
			td_eng_info(eng, "EI: -1 LBA on read src tok %u\n", tok->tokid);
			td_eng_trace(eng, TR_TOKEN, "error:lba", tok->tokid);
			tdcmd->src.lba.lba = -1;
			eng->td_errors_count --;
		}
	} else {
		/* bio request with no payload? */
		td_eng_trace(eng, TR_CMD, "BUG:TD:gen_bio:len==0", (long)bio);
		return -EINVAL;
	}

	
	return 0;
}

/* generate a read command, to advance the sequence number */
int td_cmd_gen_seq_advance(struct td_engine *eng, struct td_token *tok)
{
	td_cmd_t *tdcmd = (td_cmd_t *)tok->cmd_bytes;

	memset(tdcmd, 0, sizeof(*tdcmd));

	td_eng_trace(eng, TR_TOKEN, "forceful:advance:core ", tok->core_bufid);
	td_eng_trace(eng, TR_TOKEN, "forceful:advance:lba  ", tok->lba);

	tdcmd->cmd.id = TD_CMD_DIAG;
	tdcmd->cmd.port = tok->port;
	tdcmd->cmd.to_ssd = 1;

	tdcmd->cmd.decode.meta_size = TD_DEC_META_NONE;
	tdcmd->cmd.decode.data_size = TD_DEC_DATA_4K;
	tdcmd->cmd.decode.hardware = 0;
	tdcmd->cmd.decode.dupcheck = 0;  /* words are not duplicated */
	tdcmd->cmd.decode.to_host = 0;   /* no data.. */
	tdcmd->cmd.decode.from_host = 0; /* no data.. */

	tdcmd->src.diag_reg_src.sub_cmd = TD_FW_DIAG_GET_REG;
	tdcmd->src.diag_reg_src.reg = 7;

	tok->ops.status_check = td_cmd_status_check;

	return 0;
}

/** clear deallocation byte from command attached to token */
void td_cmd_clear_deallocation(struct td_token *tok)
{
	td_cmd_t *tdcmd = (void*)&tok->cmd_bytes;

	tdcmd->cmd.dealloc_buf = 0;
	tdcmd->cmd.dealloc_enable = 0;
}

int td_cmd_ucmd (uint64_t bytes[8], struct td_ucmd* ucmd, uint16_t core_buf, uint8_t wep)
{
	return td_cmdgen_raw(bytes, ucmd->ioctl.cmd, ucmd->ioctl.data_len_to_device, ucmd->ioctl.data_len_from_device, core_buf, wep);
}


struct td_command_generator td_cmdgen_teradimm = {
	._name      = "TeraDIMM",
	._dealloc    = td_cmdgen_dealloc,

	._ata        = td_cmdgen_ata,

	._get_reg    = td_cmdgen_get_reg,
	._put_reg    = td_cmdgen_put_reg,

	._get_params = td_cmdgen_get_params,
	._set_params = td_cmdgen_set_params,

	._ucmd       = td_cmd_ucmd,
	._OoO        = td_cmdgen_OoO,

	._shutdown   = td_cmdgen_shutdown,

};
