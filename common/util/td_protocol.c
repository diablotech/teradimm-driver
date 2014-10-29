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


#ifdef __KERNEL__ 
#include "td_kdefn.h"
#else

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define printk printf


///// Troy: please include any stuff you need to compile this file in Linux user-mode
#include <unistd.h>


#endif  /* USER/KERNEL */

#include "td_ioctl.h"
#include "td_protocol.h"

/*
 * We need to create an ATA command that will fit in Pad0-2, without
 * overwriting the odd/even bits.  We won't use the most significant
 * byte in each pad.
 *
 * Command will look like:
 *      |  b7  |  b6  |  b5  |  b4  |  b3  |  b2  |  b1  |  b0  |
 * pad0:|  o/e |ata[6]|ata[5]|ata[4]|ata[3]|ata[2]|ata[1]|ata[0]|
 * pad1:|  o/e |ata[D]|ata[C]|ata[B]|ata[A]|ata[9]|ata[8]|ata[7]|
 * pad2:| o/e* |      |      |      |      |      |ata[F]|ata[E]|
 *           ^--MSB of checksums.
 */
static void td_cmd_ata_embed_cmd(td_cmd_t *tdcmd, uint8_t *ata_cmd) {
	uint64_t cmd_part[3] = {0, 0, 0};
	int i, j, k;

	for(j=0, k=0; k < 3; k++) {
		for(i=0; i < 7 && j < 16; i++) {
			cmd_part[k] |= (uint64_t)ata_cmd[i + (k*7)] << (i*8);
			j++;
		}
	}
	tdcmd->pad0.u64 = cmd_part[0];
	tdcmd->pad1.u64 = cmd_part[1];
	tdcmd->pad2.u64 = cmd_part[2];
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


int td_cmdgen_finalize(uint64_t bytes[8], int evenodd, uint16_t index,
		uint16_t seq, uint8_t rd_buf_dealloc, uint64_t xsum[])
{
	td_cmd_t *tdcmd = (td_cmd_t *)bytes;

	tdcmd->cmd.index = index;

	if (rd_buf_dealloc != TD_INVALID_RD_BUFID) {
		/* this is a piggy back deallocate */
		tdcmd->cmd.dealloc_buf = rd_buf_dealloc;
		tdcmd->cmd.dealloc_enable = 1;
	} else {
		/* disable deallocate */
		tdcmd->cmd.dealloc_enable = 0;
	}

	tdcmd->cmd.seq = seq;

#ifdef CONFIG_TERADIMM_FORCE_4096_128
	if (tdcmd->cmd.decode.from_host) {
		tdcmd->cmd.decode.data_size = TD_DEC_DATA_4K;
		if (tdcmd->cmd.decode.meta_size)
			tdcmd->cmd.decode.meta_size = TD_DEC_META_128;
	}
#endif

	/* Duplicate the fields, before the extra checksum bits are put
	 * in... */
	if (tdcmd->cmd.decode.dupcheck == 1) {
		tdcmd->word[3] = tdcmd->word[0];
		tdcmd->word[4] = tdcmd->word[1];
		tdcmd->word[5] = tdcmd->word[2];
	}

	td_cmd_set_xsum(tdcmd, xsum);

	td_cmd_set_odd(tdcmd, (uint16_t)evenodd);

	return 0;
}

int td_cmdgen_dealloc (uint64_t bytes[8])
{
	td_cmd_t *tdcmd = (td_cmd_t *)bytes;

	tdcmd->word[0] = 0ULL;
	tdcmd->cmd.decode.hardware = 1;
	tdcmd->cmd.id = TD_CMD_DEALLOC_BUF;

	return 0;
}

int td_cmdgen_null (uint64_t bytes[8])
{
	td_cmd_t *tdcmd = (td_cmd_t *)bytes;

	tdcmd->word[0] = 0ULL;
	tdcmd->cmd.decode.hardware = 1;
	tdcmd->cmd.id = TD_CMD_NULL;

	return 0;
}

int td_cmdgen_reset (uint64_t bytes[8])
{
	td_cmd_t *tdcmd = (td_cmd_t *)bytes;

	tdcmd->word[0] = 0ULL;
	tdcmd->cmd.decode.hardware = 1;
	tdcmd->cmd.id = TD_CMD_RESET_CMD;

	return 0;
}



int td_cmdgen_raw (uint64_t bytes[8], uint64_t cmd[8], int len_to_dev,
		int len_from_dev, uint16_t core_buf, uint8_t wep)
{
	td_cmd_t *tdcmd = (td_cmd_t *)bytes;
	uint16_t data_size = 0;

	memcpy(tdcmd, &cmd[0], sizeof(*tdcmd));

	if (len_to_dev) {
		data_size = (uint16_t)len_to_dev;
		if(likely(core_buf != TD_INVALID_CORE_BUFID))
			tdcmd->src.bufid = (uint8_t) core_buf;

		tdcmd->src.wep = wep;
		if (tdcmd->cmd.decode.from_host == 0) {
			printk("ERROR: decode.from_host\n");
			return -EINVAL;
		}

	} else if (len_from_dev) {
		data_size = (uint16_t)len_from_dev;
		if(likely(core_buf != TD_INVALID_CORE_BUFID))
			tdcmd->dst.bufid = (uint8_t) core_buf;
		if (tdcmd->cmd.decode.to_host == 0) {
			printk("ERROR: decode.to_host\n");
			return -EINVAL;
		}
	}

	if (tdcmd->cmd.decode.to_host && tdcmd->cmd.decode.from_host) {
		printk("ERROR: decode.to_host && decode.from_host\n");
		return -EINVAL;
	}

	switch (data_size) {
	case 4096:
		tdcmd->cmd.decode.data_size = TD_DEC_DATA_4K;
		break;
	case 2048:
		tdcmd->cmd.decode.data_size = TD_DEC_DATA_2K;
		break;
	case 1024:
		tdcmd->cmd.decode.data_size = TD_DEC_DATA_1K;
		break;
	case 512:
		tdcmd->cmd.decode.data_size = TD_DEC_DATA_512;
		break;
	case 0:
		if (tdcmd->cmd.decode.to_host || tdcmd->cmd.decode.from_host) {
			printk("ERROR: decode.to_host || decode.from_host\n");
			return -EINVAL;
		}
		break;
	default:
		printk("ERROR: data_size: %u\n", data_size);
		return -EINVAL;
	}

	/* AIDAN: TODO, Need metadata in UCMD */
	tdcmd->cmd.decode.meta_size = TD_DEC_META_NONE;

	return 0;
}

int td_cmdgen_cancel(uint64_t bytes[8], uint8_t tokid)
{
	td_cmd_t *td_cmd = (td_cmd_t*)bytes;
	memset(td_cmd, 0, sizeof(*td_cmd));

	td_cmd->cmd.id = TD_CMD_CANCEL;
	td_cmd->src.bufid = tokid;
	
	return 0;
}

int td_cmdgen_sata_reinit (uint64_t bytes[8], uint32_t subcommand)
{
	td_cmd_t *td_cmd = (td_cmd_t*)bytes;
	memset(td_cmd, 0, sizeof(*td_cmd));

	td_cmd->cmd.id = TD_CMD_SATA_REINIT;
	td_cmd->dst.bufid = (uint8_t)subcommand;
	
	return 0;
}

int td_cmdgen_shutdown (uint64_t bytes[8])
{
	td_cmd_t *td_cmd = (td_cmd_t*)bytes;
	memset(td_cmd, 0, sizeof(*td_cmd));

	td_cmd->cmd.id = TD_CMD_SHUTDOWN;
	return 0;
}

int td_cmdgen_get_reg (uint64_t bytes[8], uint32_t reg)
{
	td_cmd_t *td_cmd = (td_cmd_t*)bytes;
	memset(td_cmd, 0, sizeof(*td_cmd));

	td_cmd->cmd.id = TD_CMD_GET_REG;
	td_cmd->cmd.decode.to_host = 1;
	td_cmd->cmd.decode.hardware = 1;
	td_cmd->cmd.decode.data_size = TD_DEC_DATA_4K;
	td_cmd->cmd.decode.meta_size = TD_DEC_META_NONE;

	td_cmd->src.reg_addr = (uint16_t)reg;
	return 0;
}

int td_cmdgen_put_reg (uint64_t bytes[8], uint32_t reg, uint32_t data,
		uint32_t mask)
{
	td_cmd_t *td_cmd = (td_cmd_t*)bytes;
	memset(td_cmd, 0, sizeof(*td_cmd));

	/* Set up the command here. */
	td_cmd->cmd.id = TD_CMD_PUT_REG;
	td_cmd->cmd.decode.hardware = 1;
	td_cmd->cmd.decode.from_host = 0; /* No DMA. */
	td_cmd->cmd.decode.data_size = TD_DEC_DATA_4K;
	td_cmd->cmd.decode.meta_size = TD_DEC_META_NONE;

	td_cmd->dst.reg_addr = (uint16_t)reg;
	td_cmd->pad0.reg_data = data;
	td_cmd->pad2.reg_mask = mask;
	return 0;
}

int td_cmdgen_fw_get_reg (uint64_t bytes[8], uint32_t reg)
{
	td_cmd_t *td_cmd = (td_cmd_t*)bytes;
	memset(td_cmd, 0, sizeof(*td_cmd));

	td_cmd->cmd.id = TD_CMD_DIAG;
	td_cmd->cmd.decode.meta_size = TD_DEC_META_NONE;
	td_cmd->cmd.decode.data_size = TD_DEC_DATA_4K;
	td_cmd->cmd.decode.hardware = 0;
	td_cmd->cmd.decode.dupcheck = 0;  /* words are not duplicated */
	td_cmd->cmd.decode.to_host = 0;   /* no data.. */
	td_cmd->cmd.decode.from_host = 0; /* no data.. */

	td_cmd->src.diag_reg_src.sub_cmd = TD_FW_DIAG_GET_REG;
	td_cmd->src.diag_reg_src.reg = reg;

	return 0;
}

int td_cmdgen_fw_put_reg (uint64_t bytes[8], uint32_t reg, uint32_t data,
		uint32_t mask)
{
	td_cmd_t *td_cmd = (td_cmd_t*)bytes;
	memset(td_cmd, 0, sizeof(*td_cmd));

	td_cmd->cmd.id = TD_CMD_DIAG;
	td_cmd->cmd.decode.meta_size = TD_DEC_META_NONE;
	td_cmd->cmd.decode.data_size = TD_DEC_DATA_4K;
	td_cmd->cmd.decode.hardware = 0;
	td_cmd->cmd.decode.dupcheck = 0;  /* words are not duplicated */
	td_cmd->cmd.decode.to_host = 0;   /* no data.. */
	td_cmd->cmd.decode.from_host = 0; /* no data.. */

	td_cmd->src.diag_reg_src.sub_cmd = TD_FW_DIAG_PUT_REG;
	td_cmd->src.diag_reg_src.reg = reg;
	td_cmd->src.diag_reg_src.wr_val = data;
	td_cmd->dst.diag_reg_dst.wr_mask = mask;

	return 0;
}



int td_cmdgen_get_params (uint64_t bytes[8], uint32_t page)
{
	td_cmd_t *td_cmd = (td_cmd_t *)bytes;
	memset(td_cmd, 0, sizeof(*td_cmd));

	td_cmd->cmd.id = TD_CMD_GET_PARAMS;
	td_cmd->cmd.decode.to_host = 1;
	td_cmd->cmd.decode.data_size = TD_DEC_DATA_4K;
	td_cmd->cmd.decode.meta_size = TD_DEC_META_NONE;
	td_cmd->src.bufid = (uint8_t)page;

	return 0;
}

int td_cmdgen_set_params (uint64_t bytes[8], uint32_t page)
{
	td_cmd_t *td_cmd = (td_cmd_t *)bytes;
	memset(td_cmd, 0, sizeof(*td_cmd));

	td_cmd->cmd.id = TD_CMD_SET_PARAMS;
	td_cmd->cmd.decode.dupcheck = 0;
	td_cmd->cmd.decode.from_host = 1;
	td_cmd->cmd.decode.data_size = TD_DEC_DATA_4K;
	td_cmd->cmd.decode.meta_size = TD_DEC_META_NONE;
	td_cmd->dst.bufid = (uint8_t)page;

	return 0;
}

/*
 * The 2 magic values we know here are are the "type" of the ATA
 * operation_code:
 *   ATA_16: 0x85  
 *   ATA_12: 0xA1
 */
int td_cmdgen_ata (uint64_t bytes[8], uint8_t*ata_cmd, int ssd, int data_size)
{
	td_cmd_t *tdcmd = (td_cmd_t *)bytes;
	uint16_t data_sectors = 0;
	uint16_t sector_count;

	if ((ata_cmd[0] != 0x85) && (ata_cmd[0] != 0xA1)) {
		printk("ERROR: Not SAT ATA PT\n");
		return -EINVAL;
	}

	memset(tdcmd, 0, sizeof(*tdcmd));

	tdcmd->cmd.id = TD_CMD_SSD_CMD;
	tdcmd->cmd.port = (uint32_t)ssd;
	tdcmd->cmd.to_ssd = 1;

	// And now check protocol
	switch ( (ata_cmd[1] & 0x1E)>>1) {
		case 0: /* Hard Reset */
		case 1: /* SRST */
		case 3: /* Non-data */
		case 8: /* Device Diagnostic */
		case 9: /* DEVICE RESET */
			tdcmd->cmd.decode.from_host = 0;
			tdcmd->cmd.decode.to_host = 0;
			break;
		case 4: /* PIO Data-In */
		case 10: /* UDMA Data In */
			tdcmd->cmd.decode.to_host = 1;
			break;
		case 5: /* PIO Data-Out */
		case 11: /* UDMA Data Out */
			tdcmd->cmd.decode.from_host = 1;
			break;
		case 2: /* Reserved */
		case 6: /* DMA */
		case 7: /* DMA Queued */
		case 12: /* FPDMA */
		case 13: /* Reserved */
		case 14: /* Reserved */
			printk("ERROR: Bad protocol: %u\n", (ata_cmd[1] & 0x1E)>>1);
			return -EIO;
		case 15: /* Return Reponse Information */
			break;
	}


	switch (data_size) {
	case 4096+64:
		tdcmd->cmd.decode.meta_size = TD_DEC_META_64;
	case 4096:
		tdcmd->cmd.decode.data_size = TD_DEC_DATA_4K;
		data_sectors = 8;
		break;
	case 2048+16:
		tdcmd->cmd.decode.meta_size = TD_DEC_META_64;
	case 2048:
		tdcmd->cmd.decode.data_size = TD_DEC_DATA_2K;
		data_sectors = 4;
		break;
	case 1024+8+8:
		tdcmd->cmd.decode.meta_size = TD_DEC_META_64;
	case 1024:
		tdcmd->cmd.decode.data_size = TD_DEC_DATA_1K;
		data_sectors = 2;
		break;
	case 520:
		tdcmd->cmd.decode.meta_size = TD_DEC_META_64;
	case 512:
		tdcmd->cmd.decode.data_size = TD_DEC_DATA_512;
		data_sectors = 1;
		break;
	case 0:
		if (tdcmd->cmd.decode.to_host || tdcmd->cmd.decode.from_host) {
			printk("ERROR: decode.to_host || decode.from_host\n");
			return -EINVAL;
		}
		break;
		
	default:
		printk("ERROR: data_size: %u\n", data_size);
		return -EINVAL;
	}


	// And now check extend
	if (ata_cmd[0] == 0x85)
		if (ata_cmd[1] & 0x01) {
			sector_count = (ata_cmd[5]<< 8) + ata_cmd[6];
		}
		else {
			sector_count = ata_cmd[6];
		}
	else {
		sector_count = ata_cmd[4];
	}

	if (sector_count > data_sectors) {
		printk("ERROR: %u > %u\n", sector_count, data_sectors);
		// return -EINVAL;
	}

	td_cmd_ata_embed_cmd(tdcmd, ata_cmd);
	
	return 0;
}

int td_cmdgen_trace_dump (uint64_t bytes[8])
{
	td_cmd_t *tdcmd = (td_cmd_t *)bytes;
	memset(tdcmd, 0, sizeof(*tdcmd));

	tdcmd->cmd.id = TD_CMD_TRACE_CTRL;;
	tdcmd->cmd.decode.meta_size = TD_DEC_META_NONE;
	tdcmd->cmd.decode.data_size = TD_DEC_DATA_4K;
	tdcmd->cmd.decode.hardware = 0;
	tdcmd->cmd.decode.dupcheck = 0;  /* words are not duplicated */
	tdcmd->cmd.decode.to_host = 1;   /* getting data */
	tdcmd->cmd.decode.from_host = 0; /* no data.. */

	tdcmd->pad0.trace.get = 1;

	return 0;
}

int td_cmdgen_trace_maskset (uint64_t bytes[8], int save)
{
	td_cmd_t *tdcmd = (td_cmd_t *)bytes;
	memset(tdcmd, 0, sizeof(*tdcmd));

	tdcmd->cmd.id = TD_CMD_TRACE_CTRL;
	tdcmd->cmd.decode.meta_size = TD_DEC_META_NONE;
	tdcmd->cmd.decode.data_size = TD_DEC_DATA_4K;
	tdcmd->cmd.decode.hardware = 0;
	tdcmd->cmd.decode.dupcheck = 0;  /* words are not duplicated */
	tdcmd->cmd.decode.to_host = 0;   /* getting data */
	tdcmd->cmd.decode.from_host = 1; /* no data.. */

	tdcmd->pad0.trace.mask = 1;
	
	if (save)
		tdcmd->pad0.trace.savem = 1;

	return 0;
}

int td_cmdgen_OoO(uint64_t bytes[8])
{
	td_cmd_t *tdcmd = (td_cmd_t *)bytes;
	memset(tdcmd, 0, sizeof(*tdcmd));

	tdcmd->cmd.id = TD_CMD_SEQ;
	tdcmd->cmd.decode.dupcheck = 1;
	tdcmd->cmd.decode.data_size = TD_DEC_DATA_4K;
	tdcmd->cmd.decode.meta_size = TD_DEC_META_NONE;
	/* FIXME: is this all we need to do? */
	return 0;
}

int td_cmdgen_trim(uint64_t bytes[8], uint8_t ssd, uint8_t length)
{
	td_cmd_t *tdcmd = (td_cmd_t *)bytes;
	memset(tdcmd, 0, sizeof(*tdcmd));

	tdcmd->cmd.id = TD_CMD_TRIM;
	tdcmd->cmd.port = ssd;
	tdcmd->cmd.to_ssd = 1;
	tdcmd->cmd.decode.from_host = 1;
	tdcmd->cmd.decode.dupcheck = 1;
	tdcmd->cmd.decode.data_size = TD_DEC_DATA_512;
	tdcmd->cmd.decode.meta_size = TD_DEC_META_NONE;
	tdcmd->dst.length = length;
	
	return 0;
}
