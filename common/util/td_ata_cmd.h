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

#ifndef _TD_ATA_CMD_H
#define _TD_ATA_CMD_H

#include "td_compat.h"
// all structures in this files will be 1 byte packed

#ifdef CONFIG_TERADIMM_SGIO
#include <scsi/sg.h> /* IOCTL #defines.. */
#endif

#define SMART_VALID 0xC24F
#define SMART_EXCEED 0x2CF4

/* T13/2015-D Table A.2  Page 433 */
enum __packed td_smart_log_type {
	TD_SMART_LOG_DIR = 0x00,
	TD_SMART_LOG_ERR = 0x01,
	TD_SMART_LOG_CERR = 0x02,
	TD_SMART_LOG_ECERR = 0x03,
	TD_SMART_LOG_STATS = 0x04,
	TD_SMART_LOG_CF = 0x05,
	TD_SMART_LOG_STEST = 0x06,
	TD_SMART_LOG_ESTEST = 0x07,
	TD_SMART_LOG_PCOND = 0x08,
	TD_SMART_LOG_SSTEST = 0x09,
	TD_SMART_LOG_LPS = 0x0D,
	TD_SMART_LOG_NCQ_ERR = 0x10,
	TD_SMART_LOG_SATA_PHY_ERR = 0x011,
	/*0x12 - 0x17 Reserved for SATA*/
	TD_SMART_LOG_WR_STR_ERR = 0x21,
	TD_SMART_LOG_RD_STR_ERR = 0x22
};

enum __packed td_smart_attr_id_val {
	TD_SMART_ATTR_ID_FLASH_ROM_CK = 0x02,
	TD_SMART_ATTR_ID_RETIRED_BLK_CNT = 0x05,
	TD_SMART_ATTR_ID_POWER_ON_HR = 0x09,
	TD_SMART_ATTR_ID_POWER_CYCLE = 0x0C,
	TD_SMART_ATTR_ID_ECC_SOFT_ERR = 0x0D,
	TD_SMART_ATTR_ID_WR_AMP = 0x20,
	TD_SMART_ATTR_RESERVE_BLK_CNT = 0xAA,
	TD_SMART_ATTR_ID_PROG_FAIL = 0xAB,
	TD_SMART_ATTR_ID_ERASE_FAIL = 0xAC,
	TD_SMART_ATTR_ID_UNKNOWN = 0xAD,
	TD_SMART_ATTR_ID_PFAIL = 0xAE,
	TD_SMART_ATTR_ID_PFAIL_PROTECT = 0xAF,
	TD_SMART_ATTR_ID_PERC_LIFE_LEFT = 0xB1,
	TD_SMART_ATTR_ID_E2E_DETECT = 0xB4,
	TD_SMART_ATTR_ID_PROG_FAIL2 = 0xB5,
	TD_SMART_ATTR_ID_ERASE_FAIL2 = 0xB6,
	TD_SMART_ATTR_ID_TEMP_WARN = 0xBE,
	TD_SMART_ATTR_ID_TEMP = 0xC2,
	TD_SMART_ATTR_ID_UNCORRECT_ERR = 0xC3,
	TD_SMART_ATTR_ID_REALLOCATION_CNT = 0xC4,
	TD_SMART_ATTR_ID_OL_UNCORRECT_ERR = 0xC6,
	TD_SMART_ATTR_ID_UDMA_CRC = 0xC7,
	TD_SMART_ATTR_ID_LIFE_BYTE_WR_MB = 0xE9,
	TD_SMART_ATTR_ID_LIFE_BYTE_WR_GB = 0xF1,
	TD_SMART_ATTR_ID_LIFE_BYTE_RD_GB = 0xF2,
	TD_SMART_ATTR_ID_PERC_LIFE_USED = 0xF5,

};
/* Table A.6 Page 436 */
//enum __packed td_smart_log_stat_type {
//};
/* Inquiry */
struct __packed td_inq_cmd {
	unsigned char op_code;    /* should always be 0x12*/
	unsigned char _b1_7_02:6; /* reserved */
	unsigned char _b1_1:1;    /* obsolete */
	unsigned char evpd:1;     /* extended vpd. */
	unsigned char page;       /* The requested page code. */
	uint16_t length;          /* allocation length */
	unsigned char control;

};
struct __packed td_page00 {
	unsigned char part[10];
};

struct __packed td_page00r {
	unsigned char qualifier:3;   /* peripheral qualifier */
	unsigned char dev_type:5;    /* peripheral device type */
	unsigned char rmb:1;         /* removable medium */
	unsigned char _b1_6_0:7;     /* reserved */
	unsigned char version;       /* Protocol version */
	unsigned char _b3_7_6:2;     /* obsolete */
	unsigned char norm_aca:1;    /* normal aca supported */
	unsigned char hi_sup:1;      /* hierachical support */
	unsigned char resp_format:4; /* response format */
	uint8_t size;                /* Remaining length of the struct */
	unsigned char sccs:1;        /* embedded SCSI storage array controller */
	unsigned char acc:1;         /* access control coordinator */
	unsigned char tpgs:2;        /* asymettric logical unit access*/
	unsigned char _3pc:1;        /* 3rd party copy */
	unsigned char _b5_2_1:2;     /* reserved */
	unsigned char protect:1;     /* support for protection information */
	unsigned char bque:1;        /* with cmd_que, full task management support */
	unsigned char enc_serv:1;    /* enclosuyre services */
	unsigned char vs1:1;
	unsigned char multi_p:1;     /* multi-port */
	unsigned char mchngr:1;      /* medium changer */
	unsigned char _b6_2_1:2;     /* obsolete */
	unsigned char addr16:1;      /* 16-bit wide SCSI addresses */
	unsigned char _b6_7_6:2;     /* obsolete */
	unsigned char wbus16:1;      /* 16-bit wide SCSI transfers */
	unsigned char sync:1;        /* synchronous transfer */
	unsigned char linked:1;      /* linked command support */
	unsigned char _b7_2:1;       /* obsolete */
	unsigned char cmd_que:1;     /* with bque, fuull task management support */
	unsigned char vs2:1;
	char t10[8];                 /* t10 vendor */
	char pid[16];                /* product id */
	char rev[4];                 /* product revision */
	char vendor[20];             /* vendor specific */
	unsigned char _b56_7_4:4;    /* reserved */
	unsigned char clk:2;         /* clocking */
	unsigned char qas:1;         /*  */
	unsigned char ius:1;         /* */
	uint16_t ver1;               /* Version descriptor 1 */
	uint16_t ver2;               /* Version descriptor 2 */
	uint16_t ver3;               /* Version descriptor 3 */
	uint16_t ver4;               /* Version descriptor 4 */
	uint16_t ver5;               /* Version descriptor 5 */
	uint16_t ver6;               /* Version descriptor 6 */
	uint16_t ver7;               /* Version descriptor 7 */
	uint16_t ver8;               /* Version descriptor 8 */
	uint8_t _res[21];            /* reserved */
        /* The rest are vendor specific.. */

};
/* Unit serial number */
struct __packed td_page80 {
	unsigned char qualifier:3;   /* peripheral qualifier */
	unsigned char dev_type:5;    /* peripheral device type */
	unsigned char page;          /* page code */
	unsigned char _b2;           /* reserved */
	uint8_t size;                /* sizeof td_page80 - 3 */
	unsigned char serial[10];    /* serial number, can be as big as necessary*/
};

/* Device ID */

struct __packed td_page83_desc {
	unsigned char pid:4;         /* protocol identifier */
	unsigned char code_set:4;    /* code set: 1 = bin, 2 = ascii, 3 = utf-8 */
	unsigned char piv:1;         /* protocol identifier valid*/
	unsigned char _b1_6:1;       /* reserved */
	unsigned char assoc:2;       /* association */
	unsigned char id_type:4;     /* identifier type */
	unsigned char _b2;           /* reserved */
	uint8_t size;                /*sizeof td_page83_desc */
	union {
		char string[10];
		uint32_t uint32;
	};

};

struct __packed td_page83 {
	unsigned char qualifier:3;   /* peripheral qualifier */
	unsigned char dev_type:5;    /* peripheral device type */
	unsigned char page;          /* page code */
	uint16_t size;               /* sizeof td_page83 */
	struct td_page83_desc desc[5];
};

struct __packed pt_16 {
	uint8_t op_code;

	uint8_t mult_cnt:3;
	uint8_t proto:4;
	uint8_t ext:1;

	uint8_t off_line:2;
	uint8_t ck_cond:1;
	uint8_t _b2_4:1;
	uint8_t t_dir:1;
	uint8_t byt_blok:1;
	uint8_t t_length:2;

	uint8_t feature[2];
	uint8_t sector_cnt[2];
	uint8_t lba_low[2];
	uint8_t lba_mid[2];
	uint8_t lba_high[2];
	uint8_t dev;
	uint8_t cmd;
	uint8_t ctrl;
};
struct __packed pt_12 {
	uint8_t op_code;

	uint8_t mult_cnt:3;
	uint8_t proto:4;
	uint8_t _b1_0:1;

	uint8_t off_line:2;
	uint8_t ck_cond:1;
	uint8_t _b2_4:1;
	uint8_t t_dir:1;
	uint8_t byt_blok:1;
	uint8_t t_length:2;

	uint8_t feature[1];
	uint8_t sector_cnt[1];
	uint8_t lba_low[1];
	uint8_t lba_mid[1];
	uint8_t lba_high[1];
	uint8_t dev;
	uint8_t cmd;
	uint8_t _b10;
	uint8_t ctrl;
};
struct __packed td_ata_pt_cmd {
	union {
		uint8_t ata_cmd[16];
		struct pt_16 p16;
		struct pt_12 p12;
	};
};

struct __packed td_ata_pt_resp {
	union {
		uint8_t ata_resp[14];
		struct {
			uint8_t desc;
			uint8_t len;
			uint8_t _b2_7_1:7;
			uint8_t ext:1;
			uint8_t error;
			union {
				uint16_t sector_cnt;
				struct {
					uint8_t sect_res;
					uint8_t sect_cnt;
				};
			};
			union {
				uint16_t lba_low;
				struct {
					uint8_t lba_lres;
					uint8_t lba_l;
				};
			};
			union {
				uint16_t lba_mid;
				struct {
					uint8_t lba_mres;
					uint8_t lba_m;
				};
			};
			union {
				uint16_t lba_high;
				struct {
					uint8_t lba_hres;
					uint8_t lba_h;
				};
			};
			uint8_t dev;
			uint8_t status;

		};
	};
};

/* spec AT Attachement ATA/ATAPI Command Set - 2 (ACS-2)
 * T13/2015-D
 * Page 133 (Table 54)
 */
union __packed td_ata_identify {
	struct __packed {
		uint16_t w0_w9[10];

		unsigned char serial[20];
		uint16_t w20_w22[3];

		unsigned char fw[8];
		unsigned char model[40];

#if 1
		uint16_t w47_w99[53];
#else
		uint16_t w47_w81[35];
		uint16_t w82;
		uint16_t w83;
		uint16_t w84_w99[16];
#endif
		uint64_t size;          /* 100-103 is size */

		uint16_t w104_w107[4];
		unsigned char wwn[8];    /* world-wide name */

#if 1
		uint16_t w112_w254[143];
		uint8_t w255_upper;
		uint8_t chksum;
#else
		uint16_t _w112;
		uint16_t _w113;
		uint16_t _w114;
		uint16_t _w115;
		uint16_t _w116_tls;

		uint16_t lss0; /*logical sector size (dword) */
		uint16_t lss1;

		uint8_t _w119_15:1;
		uint8_t _w119_14:1;
		uint8_t _w119_13_8:6;
		uint8_t ext_power_0:1;
		uint8_t sense_data_0:1;
		uint8_t ff_ctrl_0:1;
		uint8_t dwld_micro_0:1;
		uint8_t rd_log_dma_0:1;
		uint8_t wr_uncorrect_0:1;
		uint8_t wr_rd_verify_0:1;
		uint8_t _w119_ddt_0:1;

		uint8_t _w120_15:1;
		uint8_t _w120_14:1;
		uint8_t _w120_13_8:6;
		uint8_t ext_power_1:1;
		uint8_t sense_data_1:1;
		uint8_t ff_ctrl_1:1;
		uint8_t dwld_micro_1:1;
		uint8_t rd_log_dma_1:1;
		uint8_t wr_uncorrect_1:1;
		uint8_t wr_rd_verify_1:1;
		uint8_t _w119_ddt_1:1;

		uint16_t _w121_w126[6];
		uint16_t _w127;

		uint16_t _w128;
		uint16_t _w129_e159[31];
		uint16_t _w160;
		uint16_t _w161_w167[7];

		uint16_t _w168:12;
		uint8_t dev_ff:4;

		uint16_t _w169;
		uint16_t extra_ident[4];
		uint16_t _w174_w175[2];
		uint16_t serial2[30];
		uint16_t _w206;
		uint16_t _w207_w208[2];

		uint8_t _w209_15:1;
		uint8_t _w209_14:1;
		uint16_t sec_offset:14;

		uint16_t _w210_w213[4];
		uint16_t _w214;
		uint16_t _w215_w216[2];
		uint16_t _w217;
		uint16_t _w218;
		uint16_t _w219; /*nv cache*/
		uint16_t _w220;
		uint16_t _w221;

#if 0
		uint8_t ttype:4; /* transport typy, 1h=serial */
		uint8_t _w222_11_6:6;
		uint8_t sata30:1;
		uint8_t sata26:1;
		uint8_t sata25:1;
		uint8_t sata10a:1;
		uint8_t ata8_ast:1;
#else
		uint16_t _w222;
#endif

		uint16_t tport_minor; /* transport minor version number */
		uint16_t _w224_229[6];
		uint16_t ext_sect[4]; /*extended number of user addressable sec*/
		uint16_t min_micro_sz; /*512-byte block min microcode size*/
		uint16_t max_micro_sz;

		uint16_t _w236_w254[19];

		uint8_t chksum_val;
		uint8_t chksum;
#endif
	};
	uint16_t w[256];
};

/* spec AT Attachement ATA/ATAPI Command Set - 2 (ACS-2)
 * T13/2015-D
 * Page 302 (Table 155)
 */
struct __packed td_smart_attribute {
	uint8_t id;
	uint16_t flags;
	uint8_t norm_val;
	uint8_t worst_val;
	union {
		struct {
			/* for temp */
			uint16_t raw_16;
			uint8_t low;
			uint8_t high;
		};
		uint32_t raw_val;
	};
	uint8_t vend[3];
};

struct __packed td_smart_resp {
	union __packed {
		uint8_t vendor[362];
		struct __packed {
			uint8_t ver[2];
			struct td_smart_attribute attr[30];
		};
	};
	uint8_t ol_coll;      /* offline collection status */
	uint8_t stest;        /* self-test execution status*/
	uint8_t time_ol[2];   /* time (s) to complete offline data collection*/
	uint8_t vendor1;
	uint8_t ol_coll_cap;  /* off-line data collection capability */
	uint8_t cap[2];       /* smart capability */
	uint8_t err_cap;      /* offset 307, bit0 = err log supp*/
	uint8_t vendor2;
	uint8_t sh_poll;      /* polling time for short self-test */
	uint8_t ext_poll;     /* polling time for extended self-test */
	uint8_t conv_poll;    /* polling time for convenyance self-test */
	uint8_t ext_poll2[2]; /* polling time for extended self-test */
	uint8_t _b377_385[9];
	uint8_t vendor3[125];
	uint8_t chksum;       /* checksum of response. */
};

union td_ata_inq_resp {
	struct td_page00 page00;
	struct td_page00r page00r;
	struct td_page80 page80;
	struct td_page83 page83;
};
typedef union td_ata_inq_resp td_ata_inq_resp_t;

/* SMART Error Logs */
/* page 469 */
struct __packed td_smart_cmd_data {
	uint8_t trans;
	uint8_t feature;
	uint8_t count;
	uint8_t lba_low;
	uint8_t lba_mid;
	uint8_t lib_high;
	uint8_t dev;
	uint8_t cmd;
	uint32_t ts;
};
/* page 485 */
struct __packed td_smart_err_data {
	uint8_t _b1;
	uint8_t err;
	uint8_t count;
	uint8_t lba_low;
	uint8_t lba_mid;
	uint8_t lba_high;
	uint8_t dev;
	uint8_t status;
	uint8_t ext_err[19];
	uint8_t state;
	uint16_t l_ts;
};

/* page 446 */
struct __packed td_ecomp_cmd_data {
	uint8_t dev_ctrl;
	uint8_t feat_low;
	uint8_t feat_high;
	uint8_t cnt_low;
	uint8_t cnt_high;
	uint8_t lba_0_7;
	uint8_t lba_24_31;
	uint8_t lba_8_15;
	uint8_t lba_32_39;
	uint8_t lba_16_23;
	uint8_t lba_40_47;
	uint8_t dev;
	uint8_t cmd;
	uint8_t _b13;
	uint32_t ts;
};

/* page 447 */
struct __packed td_ecomp_err_data {
	uint8_t tr;
	uint8_t err;
	uint8_t cnt_low;
	uint8_t cnt_high;
	uint8_t lba_0_7;
	uint8_t lba_24_31;
	uint8_t lba_8_15;
	uint8_t lba_32_39;
	uint8_t lba_16_23;
	uint8_t lba_40_47;
	uint8_t dev;
	uint8_t status;
	uint8_t ext_info[19];
	uint8_t state;
	uint16_t ts;
};

/* page 447 */
enum __packed td_ext_err_state_e {
	TD_EXT_ERR_STATE_UNKNOWN         = 0x00,
	TD_EXT_ERR_STATE_SLEEP           = 0x01,
	TD_EXT_ERR_STATE_STANDBY         = 0x02,
	TD_EXT_ERR_STATE_ACTIVE_IDLE     = 0x03,
	TD_EXT_ERR_STATE_EXE_SMART_TEST  = 0x04,
	/* reserved - 0x05 - 0x0A */
	TD_EXT_ERR_STATE_VEND_0          = 0x0B,
	TD_EXT_ERR_STATE_VEND_1          = 0x0C,
	TD_EXT_ERR_STATE_VEND_2          = 0x0D,
	TD_EXT_ERR_STATE_VEND_3          = 0x0E,
	TD_EXT_ERR_STATE_VEND_4          = 0x0F,

};

/* page 468 */
struct __packed td_smart_err_log_data {
	struct td_smart_cmd_data data[5];
	struct td_smart_err_data err;
};

/* Page 446 */
struct __packed td_ecomp_log_err_entry {
	struct td_ecomp_cmd_data data[5];
	struct td_ecomp_err_data err;
};

/* SMART Summary Error Log -  page 483
 * Log address 01h
 */
struct __packed td_smart_log_err_sum {
	uint8_t ver;
	uint8_t index;
	struct td_smart_err_log_data log[5];
	uint16_t count;
	uint8_t _b454_510[56];
	uint8_t chksum;

};
/* page 484 */
struct __packed td_smart_err_log_entry {
	struct td_smart_cmd_data err_cmd[8];
	uint8_t ts[4];
};

/* page 435 */
struct __packed td_smart_log_comp_err {
	uint8_t ver;
	uint8_t index;
	struct td_smart_err_log_entry err[5];
	uint16_t count;
	uint8_t _b454_b510[57];
	uint8_t chksum;
};
/* page 445 */
struct __packed td_smart_log_ecomp_err {
	uint8_t ver;
	uint8_t _b1;
	uint16_t index;
	struct td_ecomp_log_err_entry err[4];
	uint16_t count;
	uint8_t _b502_b510[9];
	uint8_t chksum;
};
/* page 434 */
struct __packed td_log_gpl {
	uint16_t ver;
	uint16_t num[254];
};

/* page 434 */
struct __packed td_smart_log_0 {
	uint8_t ver[2];
	uint8_t num_p1;
	uint8_t _b3;
	uint8_t num_pg2;
	uint8_t _b5;
	uint8_t data[504];
	uint8_t num_255;
	uint8_t _b511;
};

/* SMART statistics. */
/* page 436 */
struct td_smart_log_stat{
	uint8_t dummy;
};

union __packed td_smart_log {
	struct td_log_gpl gpl;
	struct td_smart_log_0 log0;
	struct td_smart_log_err_sum err_pg1;
	struct td_smart_log_comp_err err_comp;
	struct td_smart_log_ecomp_err err_ecomp;
	struct td_smart_log_stat stat;
};
typedef union td_smart_log td_smart_log_t;


union __packed td_smart_page {
	struct td_smart_resp resp;
	td_smart_log_t log;
};
typedef union td_smart_page td_smart_page_t;

#ifdef CONFIG_TERADIMM_SGIO
struct __packed td_dev_ata_state {
	struct td_engine *eng;
	sg_io_hdr_t *hdr;
	struct td_ioctl_device_ssd_pt *pt_cmd;
	union {
		uint8_t *ata;
		struct td_ata_pt_cmd *ata_pt_cmd;
	};
	uint8_t cmd;
	union {
		void *p_resp;
		struct td_ata_pt_resp *resp;
	};
	td_ata_inq_resp_t *answer;
	uint16_t pg_cnt;
	int data_to_device;
	int data_from_device;
	uint8_t log_addr;

};
#endif

static inline uint8_t td_ata16_chksum(void *ata) {
	uint8_t ret;
	int i;
	ret = 0;
	for(i = 0; i < 511; i++) {
		ret += *(uint8_t*)PTR_OFS(ata,i);
	}
	return ~ret + 1;
}


#endif /* _TD_ATA_CMD_H */
