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



#include "td_device.h"
#include "td_ioctl.h"
#include "td_engine.h"
#include "td_compat.h"
#include "td_ucmd.h"
#include "td_ata_cmd.h"
#include "td_command.h"
#include "td_devgroup.h"
#include "td_eng_hal.h"
#include "td_ata_cmd.h"
#include "td_params.h"

#if 0
static unsigned char t10[] =     "ATA    ";
static unsigned char vendor[] =  " Diablo            ";
static unsigned char model[] =   "TeraSIMM";
static unsigned char serial[] =  "314159  ";
static unsigned char version[] = " 1.6";
#endif

static int td_dev_ata16_notok(sg_io_hdr_t *hdr)
{
	/*FIXME: What is the correct return on a failed diablo command? */

	hdr->status = 0x40; /* Task aborted */
	hdr->masked_status = COMMAND_TERMINATED;
	hdr->info = 0;
	hdr->host_status = 0;
	hdr->driver_status = 0x08;
	hdr->sb_len_wr = 0; /* no sense buffer */

	return 0;
}


/* Get the status buffer from the get_params call and return the result in the
 * sg_io_hdr structure */
static int td_dev_ata_check_sb (struct td_dev_ata_state* state)
{
	int rc;
	sg_io_hdr_t *hdr = state->hdr;
	struct td_ucmd *g_params;
	struct page *p;
	struct td_param_page0_monet_map *monet[2];
	unsigned char *sb;
	struct td_ata_pt_resp *resp;
	int length = sizeof(struct td_ata_pt_resp) + 8;
	int rm = 0; /* return monet */
	int chk_cond = state->ata_pt_cmd->ata_cmd[2] & 0x20;

	rc = -ENOMEM;
	g_params = kzalloc (sizeof(struct td_ucmd), GFP_KERNEL);
	if (!g_params) {
		/* Assume ata command failed? */
		td_dev_ata16_notok(state->hdr);
		goto no_mem;
	}

	td_ucmd_init(g_params);
	g_params->ioctl.data_len_to_device = 0;
	g_params->ioctl.data_len_from_device = 4096;

	p = alloc_page(GFP_KERNEL);
	g_params->ioctl.data = p;
	rc = td_eng_cmdgen_chk(state->eng, get_params, &g_params->ioctl.cmd[0], 1);
	if (rc) {
		/* Assume ata command failed? */
		td_dev_ata16_notok(state->hdr);
		goto paramsgen_fail;
	}

	monet[0] = &((struct td_param_page0_map_161*)p)->mMonetParams[0];
	monet[1] = &((struct td_param_page0_map_161*)p)->mMonetParams[1];

	/* Not catching error condition on the command because the Diablo
	 * command is okay, but the ATA command return status is in the sense
	 * data. */
	if ((monet[0]->d2h_reg_3.u8[3] & 0x20) >> 5 ^
			(monet[0]->d2h_reg_3.u8[3] & 0x01)) {
		hdr->masked_status = CHECK_CONDITION;
		hdr->status = monet[0]->d2h_reg_3.u8[3];
	}
	else if ((monet[1]->d2h_reg_3.u8[3] & 0x20) >> 5 ^
			(monet[1]->d2h_reg_3.u8[3] & 0x01)) {
		hdr->masked_status = CHECK_CONDITION;
		hdr->status = monet[1]->d2h_reg_3.u8[3];
		rm = 1;
	}
	else if(!chk_cond) {
		hdr->status = 0x0;
		hdr->masked_status = 0x0;
		rc =  0;
		goto no_err;
	}

	/*No errors, but check condition means return sb. */
	hdr->status = 0x02;
	hdr->masked_status = CHECK_CONDITION;

	hdr->info = 0;
	hdr->host_status = 0;
	hdr->driver_status = 0x08;
	hdr->sb_len_wr = length;

	sb = kmalloc(length, GFP_KERNEL);
	memset(sb, 0, length);
	sb[0] = 0x72;
	sb[7] = 0x0E;
	resp = (struct td_ata_pt_resp*)(sb + 8);

	/* Based on extended bit, we need to return 28 bit if it's 0, 48 bit
	 * if it's 1.
	 */
	/* We don't have room for these in get_params */
	resp->desc = 0x09;
	resp->len = 0x0C;
	/* the device and status is always here. */
	resp->dev = monet[rm]->d2h_reg_3.u8[2];
	resp->status = monet[rm]->d2h_reg_3.u8[3];
	resp->error = monet[rm]->d2h_reg_1.u8[1];

	if (monet[rm]->d2h_reg_1.u8[0] & 0x01) {
		/* 48 bit return */
		resp->sector_cnt = monet[rm]->d2h_reg_1.u16[1];
		resp->lba_low = monet[rm]->d2h_reg_2.u16[0];
		resp->lba_mid = monet[rm]->d2h_reg_2.u16[1];
		resp->lba_high = monet[rm]->d2h_reg_3.u16[0];
	}
	else {
		/* 28 bit return. */
		resp->sector_cnt = monet[rm]->d2h_reg_1.u16[1];
		resp->lba_l = monet[rm]->d2h_reg_2.u8[1];
		resp->lba_m = monet[rm]->d2h_reg_2.u8[3];
		resp->lba_h = monet[rm]->d2h_reg_3.u8[1];
	}
	rc = copy_to_user(hdr->sbp, sb, length);
	if (rc)
		rc = -ENOMEM;
	kfree(sb);

	return rc;

paramsgen_fail:
	__free_page(p);
	kfree(g_params);
no_mem: /* No memory for get_params */
	/* FIXME: Return a failure to the caller in the sense data if the
	 * diablo command failed */
no_err:
	return rc;

}
#if 0
static int td_dev_ata16_ok(sg_io_hdr_t *hdr) {
	int ret;
	unsigned char *sb;
	struct td_ata_pt_resp *resp;
	int length = sizeof(struct td_ata_pt_resp) + 8;

	hdr->status = 0x02;
	hdr->masked_status = CONDITION_GOOD;
	hdr->info = 0;
	hdr->host_status = 0;
	hdr->driver_status = 0x08;
	hdr->sb_len_wr = length;

	sb = kmalloc(length, GFP_KERNEL);
	memset(sb, 0, length);
	sb[0] = 0x72;
	sb[7] = 0x0E;
	resp = (struct td_ata_pt_resp*)(sb + 8);

	resp->desc = 0x09;
	resp->len = 0x0C;
	resp->lba_m = 0x4f;
	resp->lba_h = 0xc2;
	resp->status = 0x50;
	ret = copy_to_user(hdr->sbp, sb, length);

	kfree(sb);

	return ret;
}
#endif
static int td_dev_ata_send_cmds(struct td_dev_ata_state *state,
		struct td_ucmd *ucmd[2])
{
	int rc = -ENOMEM;
	struct page *p[2];
	int size;

	ucmd[0] = kzalloc (sizeof(*ucmd[0]), GFP_KERNEL);
	if (!ucmd[0])
		goto no_mem0;


	ucmd[0]->ioctl.data_len_from_device = state->data_from_device;
	ucmd[0]->ioctl.data_len_to_device = state->data_to_device;

	p[0] = alloc_page(GFP_KERNEL);
	ucmd[0]->ioctl.data = p[0];

	ucmd[1] = kzalloc (sizeof(*ucmd[1]), GFP_KERNEL);
	if (!ucmd[1])
		goto no_mem1;

	ucmd[1]->ioctl.data_len_from_device = state->data_from_device;
	ucmd[1]->ioctl.data_len_to_device = state->data_to_device;

	p[1] = alloc_page(GFP_KERNEL);
	ucmd[1]->ioctl.data = p[1];

	size = state->data_from_device;

	if(state->data_to_device)
		size = state->data_to_device;


	rc = td_eng_cmdgen_chk(state->eng, ata, (&ucmd[0]->ioctl.cmd[0]),
			state->ata, 0, size);
	rc |= td_eng_cmdgen_chk(state->eng, ata, (&ucmd[1]->ioctl.cmd[0]),
			state->ata, 1, size);

	/* If either command is not generated, fail out.*/
	if (rc) {
		goto ucmdgen_fail;
	}

	td_ucmd_init(ucmd[0]);
	td_ucmd_init(ucmd[1]);

	rc = td_ucmd_map(ucmd[0], NULL, (unsigned long)ucmd[0]->ioctl.data);
	if (rc)
		goto bail_setup0;

	rc = td_ucmd_map(ucmd[1], NULL, (unsigned long)ucmd[1]->ioctl.data);
	if (rc)
		goto bail_setup1;

	if(state->data_to_device) {
		rc = copy_from_user(ucmd[0]->data_virt, state->hdr->dxferp,
			state->hdr->dxfer_len);
		if (rc) {
			rc = -ECOMM;
			goto cfail0;
		}
		rc = copy_from_user(ucmd[1]->data_virt, state->hdr->dxferp,
			state->hdr->dxfer_len);
		if (rc) {
			rc = -ECOMM;
			goto cfail1;
		}
	}
	/*  Ready!? */
	td_ucmd_ready(ucmd[0]);
	td_ucmd_ready(ucmd[1]);

	/*  a reference for the execution thread */
	td_ucmd_get(ucmd[0]);
	td_ucmd_get(ucmd[1]);

	td_enqueue_ucmd(state->eng, ucmd[0]);
	td_enqueue_ucmd(state->eng, ucmd[1]);

	/* Poke the beast! */
	td_device_poke(td_engine_device(state->eng));

	/* And now we play the waiting game. */
	rc = td_ucmd_wait(ucmd[0]);

	if (rc == -ETIMEDOUT) {
		rc = -ECOMM;
		goto bail_running0;
	}

	rc = td_ucmd_wait(ucmd[1]);
	if (rc == -ETIMEDOUT) {
		rc = -ECOMM;
		goto bail_running1;
	}

	if (ucmd[0]->ioctl.result < 0) {
		rc = -ECOMM;
		goto cmd0_fail;
	}

	if (ucmd[1]->ioctl.result < 0) {
		rc = -ECOMM;
		goto cmd1_fail;
	}

	/* FIXME: Check return.. sb will be set one way or another..*/
	rc = td_dev_ata_check_sb(state);

	return rc;

cmd1_fail:
cmd0_fail:
bail_running1:
bail_running0:
cfail1:
cfail0:
bail_setup1:
bail_setup0:
	td_dev_ata16_notok(state->hdr);
	return rc;

ucmdgen_fail:
	__free_page(p[1]);
	kfree(ucmd[1]);
no_mem1:
	__free_page(p[0]);
	kfree(ucmd[0]);
no_mem0:
	return -ENOMSG;
}
static int td_dev_ata16_generic(struct td_dev_ata_state *state) {
	struct td_ucmd *ucmd[2];

	void *dx = state->hdr->dxferp;
	union td_ata_identify *id[2];
	int rc = 0;


	rc = td_dev_ata_send_cmds(state, ucmd);
	if (unlikely(-ENOMSG == rc))
		goto nomem;

	if (unlikely(rc))
		goto send_fail;

	pr_err("ERROR: Only using data from device 1.\n");
	/* FIXME: one command used */
	id[0] = (union td_ata_identify *)ucmd[0]->data_virt;
	/* For some reason, the checksum from the raw commands fail checksums
	 * sometimes..
	id[0]->chksum = td_ata16_chksum((char*)id[0]);
	*/
	rc = 0;
	if (state->data_from_device)
		rc = copy_to_user(dx, id[0], state->data_from_device);
	if (rc)
		rc = -ENOMEM;
	return rc;

send_fail:
	td_ucmd_put(ucmd[0]);
	td_ucmd_put(ucmd[1]);
nomem:

	return rc;
}

void dump_spd(struct td_engine *eng, uint8_t *spd);

static int td_dev_ata_ident(struct td_dev_ata_state *state)
{
	struct td_engine *eng = state->eng;
	union td_ata_identify response;
	int rc = 0;
	struct td_ucmd *ucmd;
	struct page *p;
	struct td_param_page7_map *page_map;
	struct {
		uint8_t hdr[8];
		struct td_ata_pt_resp ata;
	} pt_status;

	rc = -ENOMEM;

	ucmd = (struct td_ucmd*)kzalloc(sizeof(*ucmd), GFP_KERNEL);
	if (!ucmd) goto nomem_fail;

	p = alloc_page(GFP_KERNEL);
	if (!p) goto nomem_fail;

	ucmd->ioctl.data = p;
	ucmd->ioctl.data_len_from_device = TERADIMM_DATA_BUF_SIZE;

	td_ucmd_init(ucmd);
	rc = td_ucmd_map(ucmd, NULL, (unsigned long)ucmd->ioctl.data);
	if (rc) goto ucmd_fail;

	rc = td_eng_cmdgen_chk(eng, get_params, ucmd->ioctl.cmd, 7);
	if (rc) goto ucmd_fail;

	rc = td_ucmd_run(ucmd, eng);
	if (rc) goto ucmd_fail;

	memset(&response, 0, sizeof(response));
	page_map = ucmd->data_virt;

	if (td_eng_conf_hw_var_get(eng, SPD)) {
		uint8_t *spd = PTR_OFS(ucmd->data_virt, 256);
		uint32_t serial = *(uint32_t*) &(spd[122]);
		int i;

		// SPD has 18 bytes for "product type", starting at 128
		for (i = 0; i < min(sizeof(response.model), 18UL); i++)
				response.model[i^1] = spd[128+i];
		for (i = 0; i < 10; i++) {
			response.serial[(9-i)^1] = (serial % 10) + '0';
			serial /= 10;
		}
	} else {
		strncpy(response.model, "DT L                      ", sizeof(response.model));
		strncpy(response.serial, "0000000000", sizeof(response.serial));
	}

	rc = 0;
	response.fw[rc++ ^ 1] = (td_eng_conf_hw_var_get(eng, VER_MAJOR) >> 8) + '@';
	response.fw[rc++ ^ 1] = (td_eng_conf_hw_var_get(eng, VER_MAJOR) && 0x00ff) + '0';
	response.fw[rc++ ^ 1] = '.';
	response.fw[rc++ ^ 1] = (td_eng_conf_hw_var_get(eng, VER_MINOR) / 10) % 10 + '0';
	response.fw[rc++ ^ 1] = (td_eng_conf_hw_var_get(eng, VER_MINOR) / 10) % 10 + '0';
	response.fw[rc++ ^ 1] = '.';
	response.fw[rc++ ^ 1] = (td_eng_conf_hw_var_get(eng, VER_PATCH) / 10) % 10 + '0';
	response.fw[rc++ ^ 1] = (td_eng_conf_hw_var_get(eng, VER_PATCH)       % 10) + '0';
	rc = 0;

	response.size = td_engine_device(eng)->os.block_params.capacity >> SECTOR_SHIFT;

	/* w2: 0x8C37 for ATA-8*/
	response.w[2] = 0x8C37;

	/* w47: 0x80 is a magic number, 0x01 is the max number of logical sectors */
	response.w[47+0] = 0x8001;


	/* w49 sata requires bit 11 & 10 to be set. Bit 12 = ident */
	response.w[49] = 0x1C00;


	/* w50 b14 shall be 1 */
	response.w[50] = 0x4000;

	/* w53: word 88 (bit2) and w70 - w64 (bit1) are valresponse.*/
	response.w[53] = 0x0006;

	/* w63 Bit 0-2 are set for SATA */
	response.w[63] = 0; //0x0007;

	/* w64 Bit 0-1 are set for SATA */
	response.w[64] = 0; //0x0003;

	/* w65 - w68 are set to 0x0078 for SATA */
	response.w[65] = 0x0078; /* 120ms ("Word" 65) */
	response.w[66] = 0x0078;
	response.w[67] = 0x0078;
	response.w[68] = 0x0078;

	/* w76 Bit2 = sata2, Bit1 = sata1 */
	response.w[76] = 0; //0x0006;

	/* w78 in order*/
	response.w[78] = 0; //0x1000;

	/* w79 in order enabled.*/
	response.w[79] = 0x0001; // 0x1000;

	/* w80 is major version: ATA8-ACS */
	response.w[80] = 0x01F0;
	/* w81 is minor version: ACS-2 T13/2015-D revision 3 */
	response.w[81] = 0x0110;

	/* w82  bit14=nop, bit13=rd, bit12=wr, bit0=smart */
	response.w[82] = 0x0001; // 0x7001;

	/* w83 bit14 must be 1. */
	response.w[83] = 0x4000;

	/* Word 84:
	 *  b15=0
	 *  b14=1
	 *  b8=wwn
	 *  b5=log
	 *  b2=serial
	 *  b1=smart self-test
	 *  b0=smart logs
	 */
	response.w[84] = BIT(14) | BIT(8) | BIT(5) |BIT(2) | BIT(0);

	/* w85 copy of 82, make sure bit 15 isn't set.*/
	response.w[85] = response.w[82];
	/* w86 */
	response.w[86] = response.w[83];
	/* w87 */
	response.w[87] = response.w[84];

	/* udma0-6 support enabled for SATA */
	response.w[88] = 0x007F;

	response.chksum = td_ata16_chksum(&response);

	state->data_from_device = 512;
	state->data_to_device = 0;

	/*Now pretend to finish the ATA PT*/
	state->hdr->status = 0x02;
	state->hdr->masked_status = CHECK_CONDITION;

	state->hdr->info = 0;
	state->hdr->host_status = 0;
	state->hdr->driver_status = 0x08;

	state->hdr->sb_len_wr = sizeof(pt_status);

	memset(&pt_status, 0, sizeof(pt_status));
	pt_status.hdr[0] = 0x72;
	pt_status.hdr[7] = 0x0E;

	/* These are ATA specified */
	pt_status.ata.desc = 0x09;
	pt_status.ata.len = 0x0C;


	rc = copy_to_user(state->hdr->dxferp, &response, sizeof(response));
	if (rc == 0)
		rc = copy_to_user(state->hdr->sbp, &pt_status, sizeof(pt_status));

	if (rc)
		rc = -ENOMEM;

ucmd_fail:
	td_ucmd_put(ucmd);
nomem_fail:
	return rc;
}

static int td_smart_log_err_sum(struct td_dev_ata_state *state)
{

	int rc = -ENOMEM;
	struct td_ucmd *ucmd[2];
	struct td_smart_log_err_sum *ret_sum = state->hdr->dxferp;
	struct td_smart_log_err_sum *sum[2];
	struct td_smart_err_log_data *log[2];
	uint16_t ts[2];
	uint8_t idx[2];
	uint8_t ret_idx;
	uint64_t start, end;
	start = td_get_cycles();

	rc = td_dev_ata_send_cmds(state, ucmd);
	if (unlikely(-ENOMSG == rc))
		goto nomem;

	if (unlikely(rc))
		goto send_fail;

	sum[0] = (struct td_smart_log_err_sum *)ucmd[0]->data_virt;
	sum[1] = (struct td_smart_log_err_sum *)ucmd[1]->data_virt;

	/* Now we must combine the table of errors by keeping the newest 5 */

	/* Version is the same */
	ret_sum->ver = sum[0]->ver;

	/* Total error count is added */
	ret_sum->count = sum[0]->count + sum[1]->count;

	/* Current index is the count % 5 */
	ret_idx = ret_sum->count % 5;
	ret_sum->index = ret_idx;

	/* Set the log pointers to the most recent log.*/
	idx[0] = sum[0]->index;
	idx[1] = sum[1]->index;

	/* These are array indexes, so subtract 1. */
	if (idx[0])
		idx[0]--;
	if (idx[1])
		idx[1]--;

	/* This cleans up the loop below. */
	log[0] = &sum[0]->log[idx[0]];
	log[1] = &sum[1]->log[idx[1]];

	/* Copy at most 5, at least the number of errors we have. */
	rc = (ret_sum->count > 5) ? 5 : ret_sum->count;

	for(; rc > 0; rc--) {
		/* Set the timestamps of the logs.*/
		ts[0] = (uint16_t) log[0]->err.l_ts;
		ts[1] = (uint16_t) log[1]->err.l_ts;

		/* If the index is over 5, reset it to 0.*/
		if (ret_idx >= 5)
			ret_idx = 0;

		if (ts[0] >= ts[1]) {
			memcpy(&ret_sum->log[ret_idx], log[0],
				sizeof(struct td_smart_err_log_data));
			idx[0] = (idx[0] + 1) % 5;
			log[0] = &sum[0]->log[idx[0]];
		}
		else if (ts[0] < ts[1]) {
			memcpy(&ret_sum->log[ret_idx], log[1],
				sizeof(struct td_smart_err_log_data));
			idx[1] = (idx[1] + 1) % 5;
			log[1] = &sum[1]->log[idx[1]];
		}
		ret_idx++;
	}
	rc = 0;

	ret_sum->chksum = td_ata16_chksum((char*)ret_sum);

	end = td_get_cycles();
	ucmd[0]->ioctl.cycles.ioctl.start = start;
	ucmd[0]->ioctl.cycles.ioctl.end = end;
	ucmd[1]->ioctl.cycles.ioctl.start = start;
	ucmd[1]->ioctl.cycles.ioctl.end = end;

	return rc;

send_fail:
	td_ucmd_put(ucmd[0]);
	td_ucmd_put(ucmd[1]);
nomem:
	return rc;

}

static int td_smart_log_comp_err(struct td_dev_ata_state *state){
	int rc;
	//struct td_smart_log_comp_err *cerr = state->hdr->dxferp;
	//cerr->ver = 0x01;
	//cerr->chksum = td_ata16_chksum((char*)cerr);
	//rc = td_dev_ata16_ok(state->hdr);
	rc = td_dev_ata16_generic(state);
	if (rc)
		goto notok;
notok:
	return rc;
}

static int td_smart_log_stat(struct td_dev_ata_state *state)
{
	int rc;
	/* do stuff.. */
	//rc = td_dev_ata16_ok(state->hdr);
	rc = td_dev_ata16_generic(state);
	if (rc)
		goto notok;
notok:
	return rc;
}

static int td_smart_rd_log(struct td_dev_ata_state *state)
{

	int rc = -EINVAL;

	switch (state->log_addr) {
	case TD_SMART_LOG_ERR:
		rc = td_smart_log_err_sum(state);
		break;
	case TD_SMART_LOG_CERR:
		rc = td_smart_log_comp_err(state);
		break;
	case TD_SMART_LOG_STATS:
		rc = td_smart_log_stat(state);
		break;
	case TD_SMART_LOG_DIR:
	case TD_SMART_LOG_ECERR:
	case TD_SMART_LOG_STEST:
	case TD_SMART_LOG_ESTEST:
	case TD_SMART_LOG_PCOND:
	case TD_SMART_LOG_SSTEST:
	case TD_SMART_LOG_LPS:
	case TD_SMART_LOG_NCQ_ERR:
	case TD_SMART_LOG_SATA_PHY_ERR:
	case TD_SMART_LOG_WR_STR_ERR:
	case TD_SMART_LOG_RD_STR_ERR:
	default:
		printk("smart log addr = %X and will be wrong..\n",
				state->log_addr);
		rc = td_dev_ata16_generic(state);
		break;
	}

	return rc;
}

static void td_smart_cp_val(struct td_smart_attribute *val,
		struct td_smart_attribute *set)
{
	set->norm_val = val->norm_val;
	set->worst_val = val->worst_val;
	set->raw_val = val->raw_val;
	set->vend[0] = val->vend[0];
	set->vend[1] = val->vend[1];
	set->vend[2] = val->vend[2];

}

static int td_smart_set_val(struct td_smart_attribute *a1, struct
		td_smart_attribute *a2, struct td_smart_attribute *set)
{
	int rc = 0;
	set->id = a1->id;
	set->flags = a1->flags;

	/* FIXME: How does one normalize ? */

	switch (a1->id) {
	case TD_SMART_ATTR_ID_FLASH_ROM_CK: /* 0x02 */
		set->raw_val = a1->raw_val + a2->raw_val;
		/* Normalized and worst are set to 0
		 *  - Smart Storage 03/07/2013 */
		set->worst_val = 0;
		set->norm_val = 0;
		break;

	case TD_SMART_ATTR_ID_POWER_ON_HR:
		if (a1->raw_val != a2->raw_val) {
			pr_err("ERROR DETECTED! %s differs: %u <> %u\n",
					"Power on hours",
					a1->raw_val, a2->raw_val);
		}
		td_smart_cp_val(a1, set);
		break;

	case TD_SMART_ATTR_ID_POWER_CYCLE:
		/* Take the highest of the two
		 *  - Smart Storage 03/07/2013 YD_attr_to_use */
		if (a1->raw_val >= a2->raw_val)
			td_smart_cp_val(a1, set);
		else
			td_smart_cp_val(a2, set);

		break;

	case TD_SMART_ATTR_ID_ECC_SOFT_ERR:
		if (a1->raw_val >= a2->raw_val)
			td_smart_cp_val(a1, set);
		else
			td_smart_cp_val(a2, set);
		/* Normalized and worst are set to 120d
		 *  - Smart Storage 03/07/2013 */
		set->worst_val = 0x78;
		set->norm_val = 0x78;
		break;

	case TD_SMART_ATTR_ID_WR_AMP: /* 0x20 */
		set->raw_val = (a1->raw_val + a2->raw_val)/2;

		break;
	case TD_SMART_ATTR_RESERVE_BLK_CNT: /*0xAA*/
	case TD_SMART_ATTR_ID_PERC_LIFE_LEFT: /* 0xB1 */
		/* Use lower values */
		if (a1->raw_val >= a2->raw_val)
			td_smart_cp_val(a2, set);
		else
			td_smart_cp_val(a1, set);
		break;

	case TD_SMART_ATTR_ID_PROG_FAIL: /* 0xAB */
	case TD_SMART_ATTR_ID_ERASE_FAIL: /* 0xAC */
	case TD_SMART_ATTR_ID_UNKNOWN: /* 0xAD */
	case TD_SMART_ATTR_ID_PFAIL: /* 0xAE */
	case TD_SMART_ATTR_ID_E2E_DETECT: /* 0xB4 */
	case TD_SMART_ATTR_ID_PROG_FAIL2: /* 0xB5 */
	case TD_SMART_ATTR_ID_ERASE_FAIL2: /* 0xB6 */
	case TD_SMART_ATTR_ID_PFAIL_PROTECT: /* 0xAF */
	case TD_SMART_ATTR_ID_PERC_LIFE_USED: /*0xF5 */
		if (a1->raw_val >= a2->raw_val)
			td_smart_cp_val(a1, set);
		else
			td_smart_cp_val(a2, set);
		break;

	case TD_SMART_ATTR_ID_TEMP_WARN: /* 0xBE */
	case TD_SMART_ATTR_ID_TEMP: /* 0xC2 */
		/* raw is b[5:6] */
		/* Higher current temp */
		if (a1->raw_16 >= a2->raw_16)
			set->raw_16 = a1->raw_16;
		else
			set->raw_16 = a2->raw_16;

		/* Lowest low temp */
		if (a1->low >= a2->low)
			set->low = a2->low;
		else
			set->low = a1->low;

		/* Higher high temp */
		if (a1->high >= a2->high)
			set->high = a1->high;
		else
			set->high = a2->high;

		/* Lower of the normalized */
		if (a1->norm_val >= a2->norm_val)
			set->norm_val = a2->norm_val;
		else
			set->norm_val = a1->norm_val;

		/* Higher of the worst */
		if (a1->worst_val >= a2->worst_val)
			set->worst_val = a1->worst_val;
		else
			set->worst_val = a2->worst_val;

		break;

	case TD_SMART_ATTR_ID_UNCORRECT_ERR: /* 0xC3 */
		if (a1->raw_val >= a2->raw_val)
			set->raw_val = a1->raw_val;
		else
			set->raw_val = a2->raw_val;
		if (a1->worst_val >= a2->worst_val)
			set->worst_val = a1->worst_val;
		else
			set->worst_val = a2->worst_val;
		set->norm_val = 0x78;
		break;

	case TD_SMART_ATTR_ID_RETIRED_BLK_CNT:     /* 0x05 */
	case TD_SMART_ATTR_ID_REALLOCATION_CNT:    /* 0xC4 */
	case TD_SMART_ATTR_ID_OL_UNCORRECT_ERR:    /* 0xC6 */
	case TD_SMART_ATTR_ID_UDMA_CRC:            /* 0xC7 */
	case TD_SMART_ATTR_ID_LIFE_BYTE_WR_MB:     /* 0xE9 */
	case TD_SMART_ATTR_ID_LIFE_BYTE_WR_GB:     /* 0xF1 */
	case TD_SMART_ATTR_ID_LIFE_BYTE_RD_GB:     /* 0xF2 */
		set->raw_val = a1->raw_val + a2->raw_val;
		set->worst_val = a1->worst_val;
		set->norm_val = a1->norm_val;
		break;

	default:
		rc = -EINVAL;
		break;

	}
	return rc;
}

static int td_smart_rd_val(struct td_dev_ata_state *state)
{
	int rc, i;
	struct td_ucmd *ucmd[2];
	struct td_smart_resp *uresp[2];
	struct td_smart_attribute *uattr[2];
	struct td_smart_resp *resp = state->hdr->dxferp;
	struct td_smart_attribute attr;
	uint8_t chksum;

	uint64_t start, end;
	start = td_get_cycles();

	pr_err("td: td_smart_rd_val\n");
	rc = td_dev_ata_send_cmds(state, ucmd);
	if (unlikely(-ENOMSG == rc))
		goto nomem;

	if (unlikely(rc))
		goto send_fail;

	uresp[0] = (struct td_smart_resp*)ucmd[0]->data_virt;
	uresp[1] = (struct td_smart_resp*)ucmd[1]->data_virt;

	resp->ver[0] = uresp[0]->ver[0];
	resp->ver[1] = uresp[0]->ver[1];

	for(i=0; i < 30; i++) {
		uattr[0] = &uresp[0]->attr[i];
		uattr[1] = &uresp[1]->attr[i];
		/* FIXME: Should we always assume the attributes
		 * are in the same order?
		 */
		if ((uattr[0]->id != 0) &&
		    (!td_smart_set_val(uattr[0], uattr[1], &attr))) {
			memcpy(&resp->attr[i], &attr,
				sizeof(struct td_smart_attribute));
		}

	}

	chksum = td_ata16_chksum((char*)resp);
	resp->chksum = chksum;

	end = td_get_cycles();
	ucmd[0]->ioctl.cycles.ioctl.start = start;
	ucmd[0]->ioctl.cycles.ioctl.end = end;
	ucmd[1]->ioctl.cycles.ioctl.start = start;
	ucmd[1]->ioctl.cycles.ioctl.end = end;

send_fail:
	td_ucmd_put(ucmd[0]);
	td_ucmd_put(ucmd[1]);
nomem:
	return rc;
}

static int td_dev_ata16_smart(struct td_dev_ata_state *state)
{
	int ret = -EINVAL;
	struct pt_16 *cmd = &state->ata_pt_cmd->p16;

	uint16_t features = cmd->feature[0] << 8 | cmd->feature[1];
	uint16_t valid = cmd->lba_high[1] << 8 | cmd->lba_mid[1];

	if (SMART_VALID != valid) {
		printk("invalid command due to valid = %04X\n", valid);
		goto cmd_invalid;
	}

	state->pg_cnt = cmd->sector_cnt[0] << 8 | cmd->sector_cnt[1];
	state->log_addr = cmd->lba_low[1];

	/* All smart commands are 512. */
	switch((state->ata_pt_cmd->ata_cmd[1] & 0x1E) >> 1) {

	case 4: /*PIO IN */
	case 10:
		state->data_from_device = 512;
		state->data_to_device = 0;
		break;

	case 5: /*PIO OUT */
	case 11:
		state->data_from_device = 0;
		state->data_to_device = 512;
		break;

	case 3:
	default:
		state->data_from_device = 0;
		state->data_to_device = 0;
		break;
	}

	switch(features) {
	case 0xD5: /* SMART read log */
		ret = td_smart_rd_log(state);
		break;
	case ATA_SMART_READ_VALUES: /* D0 */
		ret = td_smart_rd_val(state);
		break;
	case ATA_SMART_READ_THRESHOLDS: /* D1 */
	case ATA_SMART_ENABLE:
	case 0xDA: /* SMART return status */
		ret = td_dev_ata16_generic(state);
		//ret = td_dev_ata16_ok(state->hdr);
		break;
	default:
		ret = td_dev_ata16_generic(state);
		printk("SMART cmd not found: %x\n", features);
		goto cmd_not_found;
		break;

	}



cmd_not_found:
cmd_invalid:
	return ret;
}

static int td_dev_ata16_security(struct td_dev_ata_state *state)
{
	int rc;
	struct td_ucmd *ucmd[2];
	uint64_t start, end;
	start = td_get_cycles();

	switch (state->ata_pt_cmd->ata_cmd[14]) {
	case 0xF4:
		state->data_to_device = 512;
		state->data_from_device = 0;
		break;
	case 0xF3:
	default:
		state->data_to_device = 0;
		state->data_from_device = 0;
		break;
	}

	rc = td_dev_ata_send_cmds(state, ucmd);
	if (-ENOMSG == rc)
		goto nomem;

	if(unlikely(rc))
		goto cmd_fail;

	end = td_get_cycles();
	ucmd[0]->ioctl.cycles.ioctl.start = start;
	ucmd[0]->ioctl.cycles.ioctl.end = end;
	ucmd[1]->ioctl.cycles.ioctl.start = start;
	ucmd[1]->ioctl.cycles.ioctl.end = end;

cmd_fail:
	td_ucmd_put(ucmd[0]);
	td_ucmd_put(ucmd[1]);
nomem:
	return rc;

}


static int td_dev_ata12_pass(struct td_dev_ata_state* state)
{
	int rc = 0;

	switch(state->cmd) {
	case ATA_CMD_ID_ATA:
		rc = td_dev_ata_ident(state);
		break;
	default:
		printk("Error ATA16 pass cmd %02X unknown\n", state->cmd);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int td_dev_ata16_pass(struct td_dev_ata_state* state)
{
	int rc = 0;

	/* Currently not being used.
	uint16_t length;


	switch (cmd->t_length) {
	default:
	case 0x00:
		length = 0;
		break;
	case 0x01:
		length = cmd->feature[0] << 8 | cmd->feature[1];
		break;
	case 0x02:
		length = cmd->sector_cnt[0] << 8 | cmd->sector_cnt[1];
		break;
	case 0x03:
		pr_err("Error! STPSIU contains the size!?\n");
		length = 0;
		break;
	}
	*/

	switch(state->cmd) {
	case ATA_CMD_ID_ATA:
		rc = td_dev_ata_ident(state);
		break;
	case ATA_CMD_SMART:
		rc = td_dev_ata16_smart(state);
		break;
	case ATA_CMD_SEC_ERASE_PREP:
	case ATA_CMD_SEC_ERASE_UNIT:
		rc = td_dev_ata16_security(state);
		break;
	case 0x2F:
		printk("Retrieve log treated as generic\n");
		state->data_from_device = 512;
		state->data_to_device = 0;
		rc = td_dev_ata16_generic(state);
		break;
	default:
		printk("Error ATA16 pass cmd %02X unknown\n", state->cmd);
		rc = -EINVAL;
		break;
	}

	return rc;
}
#if 0
static int td_inq_pg00 (struct td_dev_ata_state *state) {

	struct td_ioctl_device_ssd_pt *cmd = state->pt_cmd;
	td_ata_inq_resp_t *answer = state->answer;
	uint8_t len = state->hdr->dxfer_len;

	int size;
	void *loc;
	struct td_page00 *page00 = &answer->page00;
	struct td_page00r *page00r = &answer->page00r;

	/* Supported vital product data */
	/* 0 = perihperal qualifier | peripheral device type */
	if (cmd->ata_cmd[1] == 0) { /* Return all VPD */

		if (len < 36)
			goto too_small;
		page00r->version = 5;
		page00r->resp_format = 2;
		page00r->cmd_que = 1;
		page00r->sync = 1;
		memcpy(page00r->t10, t10, sizeof(t10) - 1);
		loc = page00r->pid;
		memcpy(loc, &model, sizeof(model) - 1);
		loc = PTR_OFS(loc, sizeof(model) - 1);
		memcpy(loc, &serial, sizeof(serial) - 1);
		memcpy(page00r->rev, version, sizeof(version) - 1);
		if (len >= sizeof(struct td_page00r)) {

			memcpy(page00r->vendor, &vendor, sizeof(vendor) - 1);
			page00r->size = sizeof(struct td_page00r) - 3;
			size = page00r->size + 3;
		}
		else { /* Smaller request needed. */
			page00r->size = 33;
			size = page00r->size + 3;
		}
	}
	else { /* looking for supported pages. */
		if(len < sizeof(struct td_page00))
			goto too_small;
		page00->part[3] = 3; /*We support 3 pages, so 3 more bytes of size.*/
		page00->part[4] = 0x00; /* List of supported pages */
		page00->part[5] = 0x80; /* List of supported pages */
		page00->part[6] = 0x83; /* List of supported pages */
		size = 7;
	}
	return size;

too_small:
	return 0;
}

static int td_inq_pg80(struct td_dev_ata_state *state) {

	struct td_ioctl_device_ssd_pt *cmd = state->pt_cmd;
	td_ata_inq_resp_t *answer = state->answer;
	uint8_t len = state->hdr->dxfer_len;

	int size = 0;
	struct td_page80 *page80 = &answer->page80;

	if(len < sizeof(struct td_page80))
			goto too_small;

	if (cmd->ata_cmd[1] & 1) { /* Unit serial number */
		page80->page = 0x80;
		page80->size = sizeof(struct td_page80);
		memcpy(page80->serial, serial, sizeof(serial));
		size = page80->size;
	}
	else {
	}
	return size;

too_small:
	return 0;
}

static int td_inq_pg83(struct td_dev_ata_state *state) {

	struct td_ioctl_device_ssd_pt *cmd = state->pt_cmd;
	td_ata_inq_resp_t *answer = state->answer;
	uint8_t len = state->hdr->dxfer_len;

	int size = 0;
	struct td_page83 *page83 = &answer->page83;
	struct td_page83_desc *desc;
	if(len < sizeof(struct td_page83))
			goto too_small;

	if (cmd->ata_cmd[1] & 1) { /* Device Identification */
		page83->page = 0x83;
		page83->size = sizeof(struct td_page83) - 3;
		/* Start of identification descriptor list. */
		desc = &page83->desc[0];
		desc->pid = 5;
		desc->code_set = 2;
		desc->id_type = 1;
		desc->size = sizeof(struct td_page83_desc) - 3;
		memcpy(desc->string, t10, sizeof(t10));
		
		desc = &page83->desc[1];
		desc->pid = 5;
		desc->code_set = 2;
		desc->id_type = 0;
		desc->size = sizeof(struct td_page83_desc) - 3;
		memcpy(desc->string, serial, sizeof(serial));
		
		/* Next identification desc->iptor. */
		desc = &page83->desc[2];
		desc->pid = 5;
		desc->code_set = 2;
		desc->id_type = 0;
		desc->size = sizeof(struct td_page83_desc) - 3;
		memcpy(desc->string, vendor, sizeof(vendor));

		/* Next identification desc->iptor. */
		desc = &page83->desc[3];
		desc->pid = 5;
		desc->code_set = 2;
		desc->id_type = 8;
		desc->size = sizeof(struct td_page83_desc) - 3;
		memcpy(desc->string, model, sizeof(model));

		/* Next identification desc->iptor. */
		desc = &page83->desc[4];
		desc->code_set = 2;
		desc->id_type = 0;
		desc->size = sizeof(struct td_page83_desc) - 3;
		memcpy(desc->string, version, sizeof(version));

		size = page83->size + 3;
	}
	else {
	}
	return size;

too_small:
	return 0;
}

static int td_inq_inval( sg_io_hdr_t *hdr) {
	return -EINVAL;
}
#endif

static int td_inquiry( struct td_dev_ata_state* state)
{
	struct td_ucmd *ucmd[2];
	return td_dev_ata_send_cmds(state, ucmd);
}


/* FIXME: Should this just fire off the command and let Smart Storage sort it
 * out?*/
int td_dev_sgio_v3(struct td_engine *eng, sg_io_hdr_t *hdr)
{
	int rc = -ENOTSUPP;
	struct td_dev_ata_state state;

	memset(&state, 0, sizeof(state));
	rc = td_cmd_ata_filter(hdr->cmdp);
	if (rc)
		goto not_sane;

	if (hdr->flags & SG_FLAG_MMAP_IO)
	{
		if (hdr->flags & SG_FLAG_DIRECT_IO) {
			rc = -EINVAL;
			goto mmap_direction_inval;
		}
	}

	if (!access_ok(VERIFY_READ, hdr->cmdp, hdr->cmd_len)) {
		rc = -EPERM;
		goto perm_fail;
	}

	state.hdr = hdr;
	state.eng = eng;
	state.p_resp = PTR_OFS(hdr->sbp, 8);
	memset(state.resp, 0, sizeof(struct td_ata_pt_resp));

	state.answer = kzalloc(sizeof(td_ata_inq_resp_t), GFP_USER);
	if (!state.answer) {
		rc = -ENOMEM;
		goto no_mem;
	}

	if ((!hdr->cmdp) || (hdr->cmd_len < 6 ) ||
			(hdr->cmd_len > sizeof(struct td_ata_pt_cmd))) {
		rc = -EMSGSIZE;
		goto msg_inval;
	}


	state.pt_cmd = (struct td_ioctl_device_ssd_pt*)hdr->cmdp;
	state.ata = state.pt_cmd->ata_cmd;
	if (ATA_16 == state.ata[0]) {
		state.cmd = state.ata_pt_cmd->p16.cmd;
		rc = td_dev_ata16_pass(&state);
		if (rc)
			goto ata16_setup_fail;

		hdr->resid = hdr->dxfer_len - state.data_from_device -
			state.data_to_device;
	}
	else if (ATA_12 == state.ata[0]) {
		state.cmd = state.ata_pt_cmd->p12.cmd;
		rc = td_dev_ata12_pass(&state);
		if (rc)
			goto ata12_setup_fail;

		hdr->resid = hdr->dxfer_len - state.data_from_device -
			state.data_to_device;
	}
	else if ( INQUIRY == state.ata[0] ) {
		rc = td_inquiry(&state);
		if (rc)
			goto inquiry_fail;
	}

inquiry_fail:
ata12_setup_fail:
ata16_setup_fail:
	kfree(state.answer);
no_mem:
msg_inval:
perm_fail:
mmap_direction_inval:
not_sane:

	return rc;
}

int td_device_block_sgio(struct td_engine *eng, sg_io_hdr_t *hdr)
{
	int rc = -EIO;

	if (!td_state_can_accept_requests(eng)) {
		goto denied;
	}

	switch (hdr->interface_id) {
	case 'S': /* V3 */
		rc = td_dev_sgio_v3(eng, hdr);
		break;
	case 'Q': /* V4 not supported yet. */
		td_eng_err(eng, "TSA: SG_IO V4 not supported!\n");
		rc = -EINVAL;
		break;
	default:
		td_eng_err(eng, "TSA: SG_IO %d not supported.\n", hdr->interface_id);
		rc = -ENOTSUPP;
		break;
	}

denied:
	return rc;
}
