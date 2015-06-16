/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2013 Diablo Technologies Inc. ("DIABLO").  All       *
 *    rights reserved.                                                   *
 *                                                                       *
 *    This software is being licensed under a dual license, at Diablo's  *
 *    sole discretion.                                                   *
 *                                                                       *
 *    GPL License                                                        *
 *                                                                       *
 *    If you do not have explicit permission from Diablo, then you may   *
 *    only redistribute it and/or modify it under the terms of the GNU   *
 *    General Public License as published by the Free Software           *
 *    Foundation; either version 2 of the License, or (at your option)   *
 *    any later version located at <http://www.gnu.org/licenses/>.  See  *
 *    the GNU General Public License for more details.                   *
 *                                                                       *
 *    BSD License                                                        *
 *                                                                       *
 *    If you have explicit permission from Diablo, then redistribution   *
 *    and use in source and binary forms, with or without modification,  *
 *    are permitted provided that the following conditions are met:      *
 *                                                                       *
 *        * Redistributions of source code must retain the above         *
 *        copyright notice, this list of conditions and the following    *
 *        disclaimer.                                                    *
 *                                                                       *
 *        * Redistributions in binary form must reproduce the above      *
 *        copyright notice, this list of conditions and the following    *
 *        disclaimer in the documentation and/or other materials         *
 *        provided with the distribution.                                *
 *                                                                       *
 *        * Neither the name of the DIABLO nor the names of its          *
 *        contributors may be used to endorse or promote products        *
 *        derived from this software without specific prior written      *
 *        permission.                                                    *
 *                                                                       *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND             *
 *    CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,        *
 *    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF           *
 *    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE           *
 *    DISCLAIMED. IN NO EVENT SHALL DIABLO BE LIABLE FOR ANY DIRECT,     *
 *    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL         *
 *    DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE   *
 *    GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS      *
 *    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,       *
 *    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT(INCLUDING           *
 *    NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS *
 *    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.       *
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
#include "td_dev_scsi.h"

/* the below functions are typically used via the ATA12 / ATA16 pass through
 * SCSI command, and as such they operate directly on the structures.  for a
 * clearer separation it would be useful to implement the SATL (SCSI to ATA
 * Translation Layer). */

/* Get the status buffer from the get_params call and return the result in the
 * sg_io_hdr structure */
static int td_dev_ata_check_sb (struct td_scsi_cmd* cmd)
{
	int rc;
	struct td_ucmd *g_params;
	struct page *p;
	struct td_param_page0_monet_map *monet[2];
	unsigned char *sb = cmd->sense;
	struct td_ata_pt_resp *resp;
	int length = sizeof(struct td_ata_pt_resp) + 8;
	int rm = 0; /* return monet */
	struct td_ata_pt_cmd *ata_pt_cmd = (struct td_ata_pt_cmd *)cmd->request;
	int chk_cond = ata_pt_cmd->ata_cmd[2] & 0x20;
	struct td_device *dev = td_device_from_os(cmd->odev);
	struct td_engine *eng = td_device_engine(dev);

	rc = -ENOMEM;
	g_params = kzalloc (sizeof(struct td_ucmd), GFP_KERNEL);
	if (!g_params) {
		/* Assume ata command failed? */
		cmd->status = SAM_STAT_TASK_ABORTED; /* Task aborted */
		rc = 0x04;
		cmd->sense_len = 0; /* no sense buffer */

		goto no_mem;
	}

	td_ucmd_init(g_params);
	g_params->ioctl.data_len_to_device = 0;
	g_params->ioctl.data_len_from_device = 4096;

	p = alloc_page(GFP_KERNEL);
	g_params->ioctl.data = p;
	rc = td_eng_cmdgen_chk(eng, get_params, &g_params->ioctl.cmd[0], 1);
	if (rc) {
		/* Assume ata command failed? */
		cmd->status = SAM_STAT_TASK_ABORTED; /* Task aborted */
		rc = 0x04;
		cmd->sense_len = 0; /* no sense buffer */

		goto paramsgen_fail;
	}

	/* this looks strange as we did get params on page 1.  try to capture
	 * a trace */
	WARN_ON(1);
	monet[0] = &((struct td_param_page0_map_161*)p)->mMonetParams[0];
	monet[1] = &((struct td_param_page0_map_161*)p)->mMonetParams[1];

	/* Not catching error condition on the command because the Diablo
	 * command is okay, but the ATA command return status is in the sense
	 * data. */
	if ((monet[0]->d2h_reg_3.u8[3] & 0x20) >> 5 ^
			(monet[0]->d2h_reg_3.u8[3] & 0x01)) {
		cmd->status = monet[0]->d2h_reg_3.u8[3];
	}
	else if ((monet[1]->d2h_reg_3.u8[3] & 0x20) >> 5 ^
			(monet[1]->d2h_reg_3.u8[3] & 0x01)) {
		cmd->status = monet[1]->d2h_reg_3.u8[3];
		rm = 1;
	}
	else if(!chk_cond) {
		cmd->status = SAM_STAT_GOOD;
		rc = 0;

		goto no_err;
	}

	/* No errors, but check condition means return sb. */
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

	cmd->status = SAM_STAT_CHECK_CONDITION;
	cmd->sense_len = length;
	rc = DRIVER_SENSE;

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

static int td_dev_ata_send_cmds(struct td_scsi_cmd *cmd,
		struct td_ucmd *ucmd[2], int data_from_device,
		int data_to_device)
{
	int rc = -ENOMEM;
	struct page *p[2];
	int size;
	struct td_device *dev = td_device_from_os(cmd->odev);
	struct td_engine *eng = td_device_engine(dev);

	if (!td_state_can_accept_requests(eng))
		goto denied;

	ucmd[0] = kzalloc (sizeof(*ucmd[0]), GFP_KERNEL);
	if (!ucmd[0])
		goto no_mem0;

	ucmd[0]->ioctl.data_len_from_device = data_from_device;
	ucmd[0]->ioctl.data_len_to_device = data_to_device;

	p[0] = alloc_page(GFP_KERNEL);
	ucmd[0]->ioctl.data = p[0];

	ucmd[1] = kzalloc (sizeof(*ucmd[1]), GFP_KERNEL);
	if (!ucmd[1])
		goto no_mem1;

	ucmd[1]->ioctl.data_len_from_device = data_from_device;
	ucmd[1]->ioctl.data_len_to_device = data_to_device;

	p[1] = alloc_page(GFP_KERNEL);
	ucmd[1]->ioctl.data = p[1];

	size = data_from_device;

	if(data_to_device)
		size = data_to_device;

	rc = td_eng_cmdgen_chk(eng, ata, (&ucmd[0]->ioctl.cmd[0]),
			cmd->request, 0, size);
	rc |= td_eng_cmdgen_chk(eng, ata, (&ucmd[1]->ioctl.cmd[0]),
			cmd->request, 1, size);

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

	if(data_to_device) {
		memcpy(ucmd[0]->data_virt, cmd->dxferp, cmd->dxfer_len);
		memcpy(ucmd[1]->data_virt, cmd->dxferp, cmd->dxfer_len);
	}
	/*  Ready!? */
	td_ucmd_ready(ucmd[0]);
	td_ucmd_ready(ucmd[1]);

	td_enqueue_ucmd(eng, ucmd[0]);
	td_enqueue_ucmd(eng, ucmd[1]);

	/* Poke the beast! */
	td_device_poke(dev);

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
	rc = td_dev_ata_check_sb(cmd);

	return rc;

cmd1_fail:
cmd0_fail:
bail_running1:
bail_running0:
bail_setup1:
bail_setup0:
	cmd->status = SAM_STAT_TASK_ABORTED; /* Task aborted */
	rc = 0x08;

	return rc;

ucmdgen_fail:
	__free_page(p[1]);
	kfree(ucmd[1]);
no_mem1:
	__free_page(p[0]);
	kfree(ucmd[0]);
no_mem0:
	return -ENOMSG;
denied:
	return -EIO;
}

int td_dev_ata16_generic(struct td_scsi_cmd *cmd,
		int data_from_device, int data_to_device) {
	struct td_ucmd *ucmd[2];

	void *dx = NULL;
	union td_ata_identify *id[2];
	int rc = 0;

	dx = cmd->dxferp;

	rc = td_dev_ata_send_cmds(cmd, ucmd, data_from_device, data_to_device);
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

	memcpy(dx, id[0], data_from_device);
	cmd->resid = cmd->dxfer_len - data_from_device - data_to_device;

	rc = 0;

	return rc;

send_fail:
	td_ucmd_put(ucmd[0]);
	td_ucmd_put(ucmd[1]);
nomem:

	return rc;
}

void dump_spd(struct td_engine *eng, uint8_t *spd);

int td_dev_ata_ident(struct td_scsi_cmd *cmd)
{
	struct td_device *dev = td_device_from_os(cmd->odev);
	struct td_engine *eng = td_device_engine(dev);
	union td_ata_identify *response = (union td_ata_identify *)cmd->dxferp;
	int rc = 0;
	struct td_ucmd *ucmd;
	struct page *p;
	struct td_param_page7_map *page_map;
	struct {
		uint8_t hdr[8];
		struct td_ata_pt_resp ata;
	} *pt_status = cmd->sense;

	if (!td_state_can_accept_requests(eng))
		goto denied;

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

	page_map = ucmd->data_virt;

	if (td_eng_conf_hw_var_get(eng, SPD)) {
		uint8_t *spd = PTR_OFS(ucmd->data_virt, 256);
		uint32_t serial = *(uint32_t*) &(spd[122]);
		int i;

		// SPD has 18 bytes for "product type", starting at 128
		for (i = 0; i < min(sizeof(response->model), 18UL); i++)
				response->model[i^1] = spd[128+i];
		for (i = 0; i < 10; i++) {
			response->serial[(9-i)^1] = (serial % 10) + '0';
			serial /= 10;
		}
	} else {
		strncpy(response->model, "DT L                      ", sizeof(response->model));
		strncpy(response->serial, "0000000000", sizeof(response->serial));
	}

	rc = 0;
	response->fw[rc++ ^ 1] = (td_eng_conf_hw_var_get(eng, VER_MAJOR) >> 8) + '@';
	response->fw[rc++ ^ 1] = (td_eng_conf_hw_var_get(eng, VER_MAJOR) && 0x00ff) + '0';
	response->fw[rc++ ^ 1] = '.';
	response->fw[rc++ ^ 1] = (td_eng_conf_hw_var_get(eng, VER_MINOR) / 10) % 10 + '0';
	response->fw[rc++ ^ 1] = (td_eng_conf_hw_var_get(eng, VER_MINOR) / 10) % 10 + '0';
	response->fw[rc++ ^ 1] = '.';
	response->fw[rc++ ^ 1] = (td_eng_conf_hw_var_get(eng, VER_PATCH) / 10) % 10 + '0';
	response->fw[rc++ ^ 1] = (td_eng_conf_hw_var_get(eng, VER_PATCH)       % 10) + '0';
	rc = 0;

	response->size = td_engine_device(eng)->os.block_params.capacity >> SECTOR_SHIFT;

	/* w2: 0x8C37 for ATA-8*/
	response->w[2] = 0x8C37;

	/* w47: 0x80 is a magic number, 0x01 is the max number of logical sectors */
	response->w[47+0] = 0x8001;


	/* w49 sata requires bit 11 & 10 to be set. Bit 12 = ident */
	response->w[49] = 0x1C00;

	/* w50 b14 shall be 1 */
	response->w[50] = 0x4000;

	/* w53: word 88 (bit2) and w70 - w64 (bit1) are valresponse->*/
	response->w[53] = 0x0006;

	/* w63 Bit 0-2 are set for SATA */
	response->w[63] = 0; //0x0007;

	/* w64 Bit 0-1 are set for SATA */
	response->w[64] = 0; //0x0003;

	/* w65 - w68 are set to 0x0078 for SATA */
	response->w[65] = 0x0078; /* 120ms ("Word" 65) */
	response->w[66] = 0x0078;
	response->w[67] = 0x0078;
	response->w[68] = 0x0078;

	/* w76 Bit2 = sata2, Bit1 = sata1 */
	response->w[76] = 0; //0x0006;

	/* w78 in order*/
	response->w[78] = 0; //0x1000;

	/* w79 in order enabled.*/
	response->w[79] = 0x0001; // 0x1000;

	/* w80 is major version: ATA8-ACS */
	response->w[80] = 0x01F0;
	/* w81 is minor version: ACS-2 T13/2015-D revision 3 */
	response->w[81] = 0x0110;

	/* w82  bit14=nop, bit13=rd, bit12=wr, bit0=smart */
	response->w[82] = 0x0001; // 0x7001;

	/* w83 bit14 must be 1. */
	response->w[83] = 0x4000;

	/* Word 84:
	 *  b15=0
	 *  b14=1
	 *  b8=wwn
	 *  b5=log
	 *  b2=serial
	 *  b1=smart self-test
	 *  b0=smart logs
	 */
	response->w[84] = BIT(14) | BIT(8) | BIT(5) |BIT(2) | BIT(0);

	/* w85 copy of 82, make sure bit 15 isn't set.*/
	response->w[85] = response->w[82];
	/* w86 */
	response->w[86] = response->w[83];
	/* w87 */
	response->w[87] = response->w[84];

	/* udma0-6 support enabled for SATA */
	response->w[88] = 0x007F;

	response->chksum = td_ata16_chksum(response);

	cmd->status = SAM_STAT_CHECK_CONDITION;

	pt_status->hdr[0] = 0x72;
	pt_status->hdr[7] = 0x0E;

	/* These are ATA specified */
	pt_status->ata.desc = 0x09;
	pt_status->ata.len = 0x0C;

	cmd->resid = cmd->dxfer_len - sizeof(*response);
	cmd->sense_len = sizeof(*pt_status);

	rc = DRIVER_SENSE;

ucmd_fail:
	td_ucmd_put(ucmd);
nomem_fail:
	return rc;
denied:
	return -EIO;
}

static int td_smart_log_err_sum(struct td_scsi_cmd *cmd,
		int data_from_device, int data_to_device)
{

	int rc = -ENOMEM;
	struct td_ucmd *ucmd[2];
	struct td_smart_log_err_sum *ret_sum = NULL;
	struct td_smart_log_err_sum *sum[2];
	struct td_smart_err_log_data *log[2];
	uint16_t ts[2];
	uint8_t idx[2];
	uint8_t ret_idx;
	uint64_t start, end;
	start = td_get_cycles();

	ret_sum = cmd->dxferp;
	
	rc = td_dev_ata_send_cmds(cmd, ucmd, data_from_device, data_to_device);
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

static int td_smart_log_comp_err(struct td_scsi_cmd *cmd,
		int data_from_device, int data_to_device){
	int rc;
	rc = td_dev_ata16_generic(cmd, data_from_device, data_to_device);
	if (rc)
		goto notok;
notok:
	return rc;
}

static int td_smart_log_stat(struct td_scsi_cmd *cmd, int data_from_device, int data_to_device)
{
	int rc;
	/* do stuff.. */
	rc = td_dev_ata16_generic(cmd, data_from_device, data_to_device);
	if (rc)
		goto notok;
notok:
	return rc;
}

static int td_smart_rd_log(struct td_scsi_cmd *cmd, uint8_t log_addr,
		int data_from_device, int data_to_device)
{

	int rc = -EINVAL;

	switch (log_addr) {
	case TD_SMART_LOG_ERR:
		rc = td_smart_log_err_sum(cmd, data_from_device, data_to_device);
		break;
	case TD_SMART_LOG_CERR:
		rc = td_smart_log_comp_err(cmd, data_from_device, data_to_device);
		break;
	case TD_SMART_LOG_STATS:
		rc = td_smart_log_stat(cmd, data_from_device, data_to_device);
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
				log_addr);
		rc = td_dev_ata16_generic(cmd, data_from_device, data_to_device);
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

static int td_smart_rd_val(struct td_scsi_cmd *cmd, int data_from_device, int data_to_device)
{
	int rc = -EINVAL, i;
	struct td_ucmd *ucmd[2];
	struct td_smart_resp *uresp[2];
	struct td_smart_attribute *uattr[2];
	struct td_smart_resp *resp = cmd->dxferp;
	struct td_smart_attribute attr;
	uint8_t chksum;

	uint64_t start, end;
	start = td_get_cycles();

	pr_err("td: td_smart_rd_val\n");
	rc = td_dev_ata_send_cmds(cmd, ucmd, data_from_device, data_to_device);
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

int td_dev_ata16_smart(struct td_scsi_cmd *cmd)
{
	int ret = -EINVAL;
	int data_to_device;
	int data_from_device;
	uint8_t log_addr;
	struct td_ata_pt_cmd *ata_pt_cmd = (struct td_ata_pt_cmd *)cmd->request;
	struct pt_16 *pt_cmd = &ata_pt_cmd->p16;

	uint16_t features = pt_cmd->feature[0] << 8 | pt_cmd->feature[1];
	uint16_t valid = pt_cmd->lba_high[1] << 8 | pt_cmd->lba_mid[1];

	if (SMART_VALID != valid) {
		printk("invalid command due to valid = %04X\n", valid);
		goto cmd_invalid;
	}

	log_addr = pt_cmd->lba_low[1];

	/* All smart commands are 512. */
	switch((ata_pt_cmd->ata_cmd[1] & 0x1E) >> 1) {

	case 4: /*PIO IN */
	case 10:
		data_from_device = 512;
		data_to_device = 0;
		break;

	case 5: /*PIO OUT */
	case 11:
		data_from_device = 0;
		data_to_device = 512;
		break;

	case 3:
	default:
		data_from_device = 0;
		data_to_device = 0;
		break;
	}

	switch(features) {
	case 0xD5: /* SMART read log */
		ret = td_smart_rd_log(cmd, log_addr, data_from_device, data_to_device);
		break;
	case ATA_SMART_READ_VALUES: /* D0 */
		ret = td_smart_rd_val(cmd, data_from_device, data_to_device);
		break;
	case ATA_SMART_READ_THRESHOLDS: /* D1 */
	case ATA_SMART_ENABLE:
	case 0xDA: /* SMART return status */
		ret = td_dev_ata16_generic(cmd, data_from_device, data_to_device);
		break;
	default:
		ret = td_dev_ata16_generic(cmd, data_from_device, data_to_device);
		printk("SMART cmd not found: %x\n", features);
		goto cmd_not_found;
		break;

	}



cmd_not_found:
cmd_invalid:
	return ret;
}

int td_dev_ata16_security(struct td_scsi_cmd *cmd)
{
	int rc;
	int data_from_device;
	int data_to_device;
	struct td_ucmd *ucmd[2];
	uint64_t start, end;
	struct td_ata_pt_cmd *ata_pt_cmd = (struct td_ata_pt_cmd *)cmd->request;
	start = td_get_cycles();

	switch (ata_pt_cmd->ata_cmd[14]) {
	case 0xF4:
		data_to_device = 512;
		data_from_device = 0;
		break;
	case 0xF3:
	default:
		data_to_device = 0;
		data_from_device = 0;
		break;
	}

	rc = td_dev_ata_send_cmds(cmd, ucmd, data_from_device, data_to_device);
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

