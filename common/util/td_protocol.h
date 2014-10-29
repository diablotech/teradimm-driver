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

#ifndef _TD_PROTOCOL_H_
#define _TD_PROTOCOL_H_
/*
 * Header to define TeraDIMM command enums. 
 *
 *
 */

#include "td_compat.h"
#include "td_errno.h"

#define TD_CMD_SPEC_VER 1.6

#define TD_CMD_WORD_LEN 8
#define TD_CMD_LIMIT 256
#define TD_WEP 8

#define TD_ODD 2
#define TD_EVEN 1

#define ULLMAX        ((unsigned long long)-1LL)
#define TD_LBA_MAX    ULLMAX >> 8
#define TD_CHKSUM_MAX ULLMAX >> 2
#define TD_DEALOC_MAX 0x3F;


/* Command structure */

union __packed td_cmd_addr {
	/* basic access */
	uint64_t u64;

	/* lba type */
	struct {
		uint64_t lba:48;	/* [47:0] LBA */
		uint64_t length:3;	/* [50:48] length */
		uint64_t _61_51:11;	/* [51:61] reserved*/
		uint64_t odd_even:2;	/* [63:62] isOdd/isEven */
	} lba;

	/* reads including sequential */
	/* writes src & copy src & Firmware Download src */
	/* SSD Status dst & SSD External Error dst*/
	/* Trim */
	/* SSD Status src & SSD External Error src & Firmware Download dst*/
	/* getReg & putReg */
	struct {
		union {
			uint16_t log_addr;	/* [15:0] log address */
			uint16_t reg_addr;	/* [15:0] register address */
			struct {
				uint8_t bufid;	/* [7:0] internal buffer id */
				uint8_t bcnt;	/* [15:8] block count for sequential reads */
			};
		};
		union {
			struct {
				uint16_t wep:3;         /* [18:16] WEP ID to DMA to core */
				uint16_t _23_19:5;      /* [23:19] reserved */

				uint16_t sec_pfail_wep:3; /* [26:25] WEP ID of additional redundancy for PFAIL */
				uint16_t _29_27:3;      /* [29:27] reserved */
				uint16_t sec_mode:2;    /* [31:30] SEC mode (enum td_sec_mode_e) */
			};
			uint16_t pg_offset;	/* [31:16] page offset */

		};

		uint32_t _32_47:16;     /* [47:32] reserved */
		uint32_t length:3;      /* [48:50] Length of discard */
		uint32_t _61_51:11;     /* [61:51] reserved */
		uint32_t odd_even:2;	/* [63:62] isOdd/isEven */
	};

	struct {
		uint64_t buf_id:8;	/* [ 7:0] internal buffer id */
		uint64_t buf_cnt:8;	/* [15:8] buffer count */
		uint64_t lbbc:12;       /* [27:16] Last buffer byte count */
		uint64_t auto_commit:4;      /* [31:28] Auto-commit firmware upgrade to backup storage */
		uint64_t step:8;        /* [39:32] The step of the upgrade */
		uint64_t _61_40:22;	/* [61:40] reserved */
		uint64_t odd_even:2;	/* [63:62] isOdd/isEven */

	} upgrade;

	struct {
		uint8_t sub_cmd;	/* [ 7:0] internal buffer id */
		struct {
			struct {
				uint8_t log_type:4;  /* [11:8] log type */
				uint8_t page:4;      /* [15:12] current page */
			} get_logs;
			struct {
				uint8_t val;         /* [23:16] Log type */
			} bldr;
		};
		uint8_t  _31_24;        /* [31:24] reserved  */
		uint32_t _61_32:30;	/* [61:32] reserved */
		uint32_t odd_even:2;	/* [63:62] isOdd/isEven */
	} diagnostics;

	struct {
		uint64_t sub_cmd:8;	/* [ 7:0] subcommand */
		uint64_t reg:16;	/* [23:8] reg to read or write */
		uint64_t wr_val:32;     /* [55:24] value to write */
		uint64_t _61_56:6;	/* [61:56] reserved */
		uint64_t odd_even:2;	/* [63:62] isOdd/isEven */
	} diag_reg_src;

	struct {
		uint64_t buf_id:8;	/* [ 7:0] internal buffer id */
		uint64_t wr_mask:32;    /* [39:8] register write mask */
		uint64_t _61_40:22;	/* [61:40] reserved */
		uint64_t odd_even:2;	/* [63:62] isOdd/isEven */
	} diag_reg_dst;

};

typedef union td_cmd_addr td_cmd_addr_t;

/* Command decode field format */
union __packed td_cmd_decode {
	uint8_t bits;
	struct {
		uint8_t meta_size:2; 	/* decode[1:0] size of metadata */
		uint8_t data_size:2; 	/* decode[3:2] size of main data */
		uint8_t to_host:1;	/* decode[4]   transfer data to host */
		uint8_t from_host:1;	/* decode[5]   transfer data from host */
		uint8_t hardware:1;	/* decode[6]   hardware command */
		uint8_t dupcheck:1;	/* decode[7]   firmware checks dups */
	};
};
typedef union td_cmd_decode td_cmd_decode_t;

union __packed td_cmd {
	uint64_t word[TD_CMD_WORD_LEN];
	struct {
		union td_cmd_cmd {
			uint64_t u64;
			struct {
				uint8_t id;		/* cmd[0:7]   Command/format of remaining words */
				td_cmd_decode_t decode;	/* cmd[15:8]  pre-decoded semnantic information */
				struct {
					uint16_t dealloc_buf:5;	/* cmd[20:16] dealoc buf index */
					uint16_t dealloc_enable:1;	/* cmd[21] dealoc buf index */
					uint16_t _27_22:6;	/* cmd[27:22] reserved */
					uint16_t rate_limit:4;	/* cmd[31:28] rate limiting encoding */
				};
				struct {
					uint32_t seq:16;	/* cmd[47:32] sequence number */
					uint32_t index:9;	/* cmd[56:48] command buffer index value */
					uint32_t to_ssd:1;      /* cmd[57]    indicate that this is going to an SSD. */
					uint32_t port:2;	/* cmd[59:57] SATA port ID */
					uint32_t sync:1;	/* cmd[60]    sync flag */
					uint32_t clr_irq:1;	/* cmd[61]    clear irq request */
					uint32_t odd_even:2;	/* cmd[63:62] isOdd/isEven */
				};
			};
		} cmd;

		td_cmd_addr_t src;
		td_cmd_addr_t dst;

		union {
			uint64_t u64;
			uint64_t cmd_copy;
			struct {
				uint64_t reg_data:32;	/* pad0[31:0] RegData */ 
				uint64_t _32_61:30;	/* pad0[61:32] reserved */
				uint64_t odd_even:2;	/* pad0[63:62] isOdd/isEven */
			};
			struct {
				uint8_t mode:4; /* [0:3] enable/disable trace */
				uint8_t get:1;        /* [4]   get the trace */
				uint8_t mask:1;       /* [5]   trace mask is in core buffer */
				uint8_t savem:1;      /* [6]   save trace mask to flash and restore it on boot.*/
				uint8_t clearm:1;     /* [7]   Clear trace mask on boot */
			} trace;
		} pad0;

		union {
			uint64_t u64;
			uint64_t src_copy;
			struct {
				uint64_t _0_61:62;	/* pad1[61:0] reserved */
				uint64_t odd_even:2;	/* pad1[63:62] isOdd/isEven */
			};
		} pad1;

		union {
			uint64_t u64;
			uint64_t dst_copy;
			struct {
				uint64_t reg_mask:32;	/* pad2[31:0] RegMask[31:0] */
				uint64_t _32_57:26;	/* pad2[57:32] */
				uint64_t chksum1_msb:2;	/* pad2[59:58] MSB[63:62] of chksum1 */
				uint64_t chksum0_msb:2;	/* pad2[61:60] MSB[63:62] of chksum0 */
				uint64_t odd_even:2;	/* pad2[63:62] isOdd/isEven */
			};
		} pad2;

		union {
			uint64_t u64;
			struct {
				uint64_t chksum1:62;	/* Other checksum */
				uint64_t odd_even:2;	/* pad3[63:62] isOdd/isEven */
			};
		} chksum1;

		union {
			uint64_t u64;
			struct {
				uint64_t chksum0:62;	/* checksum */
				uint64_t odd_even:2;	/* chksum[63:62] isOdd/isEven */
			};
		 } chksum0;
	};

};
typedef union td_cmd_cmd td_cmd_cmd_t;
typedef union td_cmd td_cmd_t;

/* Command decode metadata size format */
enum td_decode_meta_e {
	TD_DEC_META_NONE = 0,
	TD_DEC_META_64 = 1,
	TD_DEC_META_128 = 2,
	TD_DEC_META_RES = 3,/* Reserved */
	TD_DEC_META_MAX = 2
};

enum td_sec_mode_e {
	TD_SEC_MODE_DISABLED = 0,
	TD_SEC_MODE_DOUBLE = 1,
	TD_SEC_MODE_TRIPLE = 2
};

static inline unsigned int td_metadata_bytes(enum td_decode_meta_e msz)
{
	switch (msz) {
	default:
		return 0;
	case TD_DEC_META_64:
		return 64;
	case TD_DEC_META_128:
		return 128;
	}
}

/* command decode data size format */
enum td_decode_data_e {
	TD_DEC_DATA_4K = 0,
	TD_DEC_DATA_2K = 1,
	TD_DEC_DATA_1K = 2,
	TD_DEC_DATA_512 = 3,
	TD_DEC_DATA_MAX = 3
};

static inline unsigned int td_data_bytes(enum td_decode_data_e dsz)
{
	switch (dsz) {
	case TD_DEC_DATA_4K:
		return 4096;
	case TD_DEC_DATA_2K:
		return 2048;
	case TD_DEC_DATA_1K:
		return 1024;
	case TD_DEC_DATA_512:
		return 512;
	default:
		return 0;
	}
}

static inline enum td_decode_data_e td_dec_data_size (unsigned int len)
{
	if (len <= 512)
		return TD_DEC_DATA_512;
	if (len <= 1024)
		return TD_DEC_DATA_1K;
	if (len <= 2048)
		return TD_DEC_DATA_2K;
	return TD_DEC_DATA_4K;
}

/* Command decode ssd id format (actually in byte 3) */
enum td_cmd_ssd_id_e {
	TD_CMD_SSD_0 = 0,
	TD_CMD_SSD_1 = 1,
	TD_CMD_SSD_MAX = 1,
	TD_CMD_SSD_RESERVED0 = 2,
	TD_CMD_SSD_RESERVED1 = 3,
};

#define TD_CMD_FREE_RDBUF_INVALID 0xFF

/* Command IDs */
enum td_cmd_e {
	TD_CMD_NULL		= 0x00,
	TD_CMD_GET_REG		= 0x01,
	TD_CMD_PUT_REG		= 0x02,

				/* 0x03 - 0x39 reserved */

	TD_CMD_RESET_CMD	= 0x40,
	TD_CMD_RD_SEQ		= 0x41,
	TD_CMD_RD_PAGE		= 0x42,
	TD_CMD_WR_DLY		= 0x43,
	TD_CMD_WR_FINAL		= 0x44,
	TD_CMD_RD_EXT		= 0x45,
	TD_CMD_WR_EXT		= 0x46,
	TD_CMD_TRIM		= 0x47,
	TD_CMD_SSD_STATS	= 0x48,
	TD_CMD_SSD_CMD		= 0x49,
	TD_CMD_COPY		= 0x4A,
	TD_CMD_FW_NULL		= 0x4B,
	TD_CMD_SHUTDOWN		= 0x4C,
	TD_CMD_STARTUP		= 0x4D,
	TD_CMD_TRACE_CTRL	= 0x4E,
	TD_CMD_TEST		= 0x4F,
	TD_CMD_SEQ		= 0x50,
	TD_CMD_FW_DEBUG_LOG	= 0x51,
	TD_CMD_SSD_EXT_ERR	= 0x52,
	TD_CMD_FW_DOWNLOAD	= 0x53,
	TD_CMD_GET_PARAMS	= 0x54,
	TD_CMD_SET_PARAMS	= 0x55,
	TD_CMD_CRASH_LOG	= 0x56,
	TD_CMD_TEST_READ	= 0x57,
	TD_CMD_TEST_WRITE	= 0x58,
	TD_CMD_SATA_REINIT	= 0x59,
	TD_CMD_SATA_NOREAD      = 0x5A,
	TD_CMD_SATA_NOWRITE     = 0x5B,
	TD_CMD_CANCEL     	= 0x5C,
	TD_CMD_FAULT_INJECTION  = 0x5D,
	TD_CMD_DIAG             = 0x5E,

				/* 0x5F - 0xF6 reserved*/	
	
	/* HW F-codes - when HW detects and error */
	TD_CMD_HWF_CMDOFFSET	= 0xF7,
	TD_CMD_HWF_HWCMD	= 0xF8,
	/* 0xF9 & 0xFA reserved */
	TD_CMD_HWF_DATAECC	= 0xFB,
	TD_CMD_HWF_TIMEOUT	= 0xFC,
	TD_CMD_HWF_XSUM 	= 0xFD,
	/* 0xFE reserved */
	TD_CMD_DEALLOC_BUF	= 0xFF
};

/* Status Buffer */
union __packed td_status {
	uint8_t byte;
	struct {
		uint8_t rdbuf:5;    /* [4:0] read buffer/status/error code */
		uint8_t extend:1;   /* [5] check extended status buffer   */
		uint8_t odd:1;      /* [6] is Odd/Even */
		uint8_t success:1;  /* [7] error */
	} fin;
	struct {
		uint8_t status:5;   /* [4:0] read buffer/status/error code */
		uint8_t extend:1;   /* [5] check extended status buffer   */
		uint8_t odd:1;      /* [6] is Odd/Even */
		uint8_t success:1;  /* [7] error */
	} ext;

};
typedef union td_status td_status_t;

enum td_generic_status {
	TD_STATUS_QUEUED             = 0x00,
	TD_STATUS_RECEIVED           = 0x01,
	TD_STATUS_PENDING            = 0x02,
	TD_STATUS_COLLISION          = 0x03,
	TD_STATUS_EXE_ERR            = 0x04,
	TD_STATUS_HDATA_DMA_XSUM_ERR = 0x05,
	TD_STATUS_HDATA_ECC_ERR      = 0x06,
	TD_STATUS_HCMD_ECC_ERR       = 0x07,
	TD_STATUS_OoO                = 0x08, // _ERR ?
	TD_STATUS_OoO_RANGE_ERR      = 0x09,
	TD_STATUS_OoO_DUP_ERR        = 0x0A,
	TD_STATUS_UNKNOWN_HW_CMD_ERR = 0x0B,
	TD_STATUS_FIELD_ERR          = 0x0C,
	TD_STATUS_EXE_WARNING        = 0x0D,
	TD_STATUS_SEQUENCED          = 0x0E,
	TD_STATUS_ILLEGAL_CMD_ERR    = 0x1F,
};

enum td_status_hw_fcode {
	TD_STATUS_HW_DMA
};

enum td_write_status {
	TD_WR_STATUS_QUEUED             = TD_STATUS_QUEUED,
	TD_WR_STATUS_RECEIVED           = TD_STATUS_RECEIVED,
	TD_WR_STATUS_WAIT_SSD           = TD_STATUS_PENDING,
	TD_WR_STATUS_COLLISION          = TD_STATUS_COLLISION,
	TD_WR_STATUS_EXE_ERROR          = TD_STATUS_EXE_ERR,
	TD_WR_STATUS_HDATA_DMA_XSUM_ERR = TD_STATUS_HDATA_DMA_XSUM_ERR,
	TD_WR_STATUS_HDATA_ECC_ERR      = TD_STATUS_HDATA_ECC_ERR,
	TD_WR_STATUS_HCMD_ECC_ERR       = TD_STATUS_HCMD_ECC_ERR,
	TD_WR_STATUS_OoO                = TD_STATUS_OoO,
	TD_WR_STATUS_OoO_RANGE_ERR      = TD_STATUS_OoO_RANGE_ERR,
	TD_WR_STATUS_OoO_DUP_ERR        = TD_STATUS_OoO_DUP_ERR,
	TD_WR_STATUS_UNKNOWN_HW_CMD_ERR = TD_STATUS_UNKNOWN_HW_CMD_ERR,
	TD_WR_STATUS_FIELD_ERR          = TD_STATUS_FIELD_ERR,
	TD_WR_STATUS_SSD_WARNING        = TD_STATUS_EXE_WARNING,
	TD_WR_STATUS_SEQUENCED          = TD_STATUS_SEQUENCED,
	TD_WR_STATUS_ILLEGAL_CMD_ERR    = TD_STATUS_ILLEGAL_CMD_ERR,
};

enum td_read_status {
	TD_RD_STATUS_QUEUED             = TD_STATUS_QUEUED,
	TD_RD_STATUS_RECEIVED           = TD_STATUS_RECEIVED,
	TD_RD_STATUS_WAIT_SSD           = TD_STATUS_PENDING,
	TD_RD_STATUS_COLLISION          = TD_STATUS_COLLISION,
	TD_RD_STATUS_EXE_ERROR          = TD_STATUS_EXE_ERR,
	TD_RD_STATUS_HDATA_DMA_XSUM_ERR = TD_STATUS_HDATA_DMA_XSUM_ERR,
	/* RESERVED                     = 6 */
	TD_RD_STATUS_HCMD_ECC_ERR       = TD_STATUS_HCMD_ECC_ERR,
	TD_RD_STATUS_OoO                = TD_STATUS_OoO,
	TD_RD_STATUS_OoO_RANGE_ERR      = TD_STATUS_OoO_RANGE_ERR,
	TD_RD_STATUS_OoO_DUP_ERR        = TD_STATUS_OoO_DUP_ERR,
	TD_RD_STATUS_UNKNOWN_HW_CMD_ERR = TD_STATUS_UNKNOWN_HW_CMD_ERR,
	TD_RD_STATUS_FIELD_ERR          = TD_STATUS_FIELD_ERR,
	TD_RD_STATUS_SSD_WARNING        = TD_STATUS_EXE_WARNING,
	TD_RD_STATUS_SEQUENCED          = TD_STATUS_SEQUENCED,
	TD_RD_STATUS_ILLEGAL_CMD_ERR    = TD_STATUS_ILLEGAL_CMD_ERR,
};

enum td_upgrade_fw {
	TD_FW_UP_MANIFEST        = 0,
	TD_FW_UP_PIC             = 1,
	TD_FW_UP_BL2             = 2,
	TD_FW_UP_DIAG            = 3,
	TD_FW_UP_RUSH            = 4,
	TD_FW_UP_MONET           = 5,
	TD_FW_UP_IMAGE_MAX       = TD_FW_UP_MONET,
	TD_FW_UP_INVOKE          = 100,
	TD_FW_UP_COMMIT          = 101,
	TD_FW_UP_SETDIAGMODE     = 102,
	TD_FW_UP_BANKSWITCH      = 103,
	TD_FW_UP_QUERY_STATUS    = 104,
};

enum td_diag_subcmd {
  TD_FW_DIAG_BASIC                                  =  1,
  TD_FW_DIAG_CAL_BOOTPOINTER                        =  2,
  TD_FW_DIAG_READ_SSD_LOGS                          =  3,
  TD_FW_DIAG_TEST_SUPERCAP                          =  4,
  TD_FW_DIAG_TEST_RUSH_FW_BANK0_INTEGRITY           =  5,
  TD_FW_DIAG_TEST_RUSH_FW_BANK1_INTEGRITY           =  6,
  TD_FW_DIAG_TEST_RUSH_FW_BANK2_INTEGRITY           =  7,
  TD_FW_DIAG_TEST_MONET0_FW_ACTIVE_BANK_INTEGRITY   =  8,
  TD_FW_DIAG_TEST_MONET0_FW_INACTIVE_BANK_INTEGRITY =  9,
  TD_FW_DIAG_TEST_MONET1_FW_ACTIVE_BANK_INTEGRITY   = 10,
  TD_FW_DIAG_TEST_MONET1_FW_INACTIVE_BANK_INTEGRITY = 11,
  TD_FW_DIAG_TEST_PIC_FW_ACTIVE_BANK_INTEGRITY      = 12,
  TD_FW_DIAG_TEST_PIC_FW_INACTIVE_BANK_INTEGRITY    = 13,
  TD_FW_DIAG_TEST_RUSH_FW_BANK0_VERSION             = 14,
  TD_FW_DIAG_TEST_RUSH_FW_BANK1_VERSION             = 15,
  TD_FW_DIAG_TEST_RUSH_FW_BANK2_VERSION             = 16,
  TD_FW_DIAG_TEST_MONET0_FW_ACTIVE_BANK_VERSION     = 17,
  TD_FW_DIAG_TEST_MONET0_FW_INACTIVE_BANK_VERSION   = 18,
  TD_FW_DIAG_TEST_MONET1_FW_ACTIVE_BANK_VERSION     = 19,
  TD_FW_DIAG_TEST_MONET1_FW_INACTIVE_BANK_VERSION   = 20,
  TD_FW_DIAG_TEST_PIC_FW_ACTIVE_BANK_VERSION        = 21,
  TD_FW_DIAG_TEST_PIC_FW_INACTIVE_BANK_VERSION      = 22,
  TD_FW_DIAG_GET_REG                                = 23,
  TD_FW_DIAG_PUT_REG                                = 24
};
  
/* Exteneded Status Buffer */
union __packed td_ext_status {
	uint64_t ext_cmd[TD_CMD_LIMIT];
	uint64_t _7F9;
	uint64_t _7FA;
	uint64_t _7FB;
	uint64_t _7FC;
	uint64_t _7FD;
	uint64_t _7FE;
	uint64_t _7FF;
	uint64_t wep_rd[TD_WEP];
	union {
		uint64_t u64;
		struct {
			uint64_t lower_rd:8; /*[7:0]   WEP lower meta-data buffer read */
			uint64_t upper_rd:8; /*[15:8]  WEP upper meta-data buffer read */
			uint64_t lower_wr:8; /*[23:16] WEP lower meta-data buffer write */
			uint64_t upper_wr:8; /*[31:24] WEP upper meta-data buffer write */
			uint64_t _39_32:8;   /*[39:32] reserved */
			uint64_t _55_40:16;  /*[55:40] reserved */
			uint64_t done:8;     /*[56:63] WEP write buffer write_done */
		};
	} wep_complete;
	union {
		uint64_t u64;
		struct {
			uint64_t scr_host:32;    /* [31:0] Scrubber collision on host facing read buffer */
			uint64_t scr_stat:8;     /* [39:32] Scrubber collision on status buffer cache lines */
			uint64_t up_data:1;      /* [40] uP data ecc error */
			uint64_t up_code:1;      /* [41] uP code ecc error */
			uint64_t dp_mem:1;       /* [42] Datapath memory ecc error*/
			uint64_t cmd_fifo:6;     /* [48:43] Command input FIFO fullness count */
			uint64_t hba0_dead:1;    /* [49] hba[0] dead */
			uint64_t hba1_dead:1;    /* [50] hba[1] dead */
			uint64_t _58_51:8;       /*[58:51] reserved*/
			uint64_t pfail_rw_fail:1;/* [59] power fail re-write failed, check CoreBuffer[127]*/
			uint64_t post_fail:1;    /* [60] post fail */
			uint64_t post_ok:1;      /* [61] post ok*/
			uint64_t ssd_rdy:1;      /* [62] ssd ready */
			uint64_t rush_rdy:1;     /* [63] Rush ready */
		};
	} global_ext;

};
typedef union td_ext_status td_ext_status_t;




/*
 * These are exported functions to td_protocol.c for generating TeraDIMM
 * diablo commands
 */

/* Takes a valid, command, and fills in the remaing decode bits */
int td_cmdgen_finalize(uint64_t bytes[8], int evenodd, uint16_t index,
		uint16_t seq, uint8_t rd_buf_dealloc, uint64_t xsum[]);

/* independand dealloc command */
int td_cmdgen_dealloc (uint64_t bytes[8]);

/* NULL command */
int td_cmdgen_null (uint64_t bytes[8]);

/* RESET command */
int td_cmdgen_reset (uint64_t bytes[8]);

/* Generate an diablo SSD_CMD */
int td_cmdgen_ata (uint64_t bytes[8], uint8_t*ata_cmd, int ssd, int data_size);


/* SET/PUT REGISTER command */
int td_cmdgen_get_reg (uint64_t bytes[8], uint32_t reg);
int td_cmdgen_put_reg (uint64_t bytes[8], uint32_t reg, uint32_t data, uint32_t mask);

int td_cmdgen_fw_get_reg (uint64_t bytes[8], uint32_t reg);
int td_cmdgen_fw_put_reg (uint64_t bytes[8], uint32_t reg, uint32_t data, uint32_t mask);

/* GET/SET PARAMS */
int td_cmdgen_get_params (uint64_t bytes[8], uint32_t page);
int td_cmdgen_set_params (uint64_t bytes[8], uint32_t page);

int td_cmdgen_trace_dump (uint64_t bytes[8]);
int td_cmdgen_trace_maskset (uint64_t bytes[8], int save);

int td_cmdgen_OoO (uint64_t bytes[8]);

int td_cmdgen_cancel(uint64_t bytes[8], uint8_t tokid);

int td_cmdgen_sata_reinit(uint64_t bytes[8], uint32_t subcommand);

int td_cmdgen_shutdown(uint64_t bytes[8]);

int td_cmdgen_trim(uint64_t bytes[8], uint8_t ssd, uint8_t length);

/* Creates a dibalo command bsaed on the raw command info */
int td_cmdgen_raw (uint64_t bytes[8], uint64_t cmd[8], int len_to_dev, int len_from_dev, uint16_t core_buf, uint8_t wep);



/* Helper inline functions needed for the TD commands */

static inline void td_cmd_set_odd(td_cmd_t *cmd, int is_odd) {
	uint8_t i = (is_odd ? TD_ODD : TD_EVEN);
	cmd->cmd.odd_even = i;
	cmd->src.odd_even = i;
	cmd->dst.odd_even = i;
	cmd->pad0.odd_even = i;
	cmd->pad1.odd_even = i;
	cmd->pad2.odd_even = i;
	cmd->chksum1.odd_even = i;
	cmd->chksum0.odd_even = i;
}

static inline void td_cmd_get_xsum(td_cmd_t *cmd, uint64_t* xsum)
{
	xsum[0] = ((uint64_t)cmd->pad2.chksum0_msb << 62) | cmd->chksum0.chksum0;
	xsum[1] = ((uint64_t)cmd->pad2.chksum1_msb << 62) | cmd->chksum1.chksum1;
}

static inline void td_cmd_set_xsum(td_cmd_t *cmd, uint64_t *xsum)
{
	cmd->chksum0.chksum0 = xsum[0] & TD_CHKSUM_MAX;
	cmd->pad2.chksum0_msb = xsum[0] >> 62;

	cmd->chksum1.chksum1 = xsum[1] & TD_CHKSUM_MAX;
	cmd->pad2.chksum1_msb = xsum[1] >> 62;
}



/** commands with hardware bit set */
static inline int td_cmd_is_hardware(const void *cmd_bytes)
{
	const td_cmd_t *tdcmd = (td_cmd_t*)cmd_bytes;
	return tdcmd->cmd.decode.hardware;
}

/** commands that never go to firmware */
static inline int td_cmd_is_hardware_only(const void *cmd_bytes)
{
	const td_cmd_t *tdcmd = (td_cmd_t*)cmd_bytes;
	switch (tdcmd->cmd.id) {
	case TD_CMD_NULL:
	case TD_CMD_GET_REG:
	case TD_CMD_PUT_REG:
	case TD_CMD_RESET_CMD:
	case TD_CMD_DEALLOC_BUF:
		return !0;
	default:
		return 0;
	}
}

/** commands that never go to firmware */
static inline int td_cmd_is_reset(const void *cmd_bytes)
{
	const td_cmd_t *tdcmd = (td_cmd_t*)cmd_bytes;
	switch (tdcmd->cmd.id) {
	case TD_CMD_RESET_CMD:
		return !0;
	default:
		return 0;
	}
}

static inline int td_cmd_is_sequenced(const void *cmd_bytes)
{
	const td_cmd_t *tdcmd = (td_cmd_t*)cmd_bytes;
	return tdcmd->cmd.to_ssd;
}



#if 0

This information should be beside whatever "makes" the particular command.

static inline void td_cmd_calc_decode(td_cmd_t *cmd,
		enum td_decode_meta_e msz, /* meta data size */
		enum td_decode_data_e dsz, /* data size */
		enum td_cmd_ssd_id_e ssd) /* ssd ID */
{

/*	BUGON(msz > TD_DEC_META_MAX);
	BUGON(dsz > TD_DEC_DATA_MAX);
	BUGON(ssd > TD_CMD_SSD_MAX);
	*/
	/* 
	if(msz > TD_DEC_META_MAX) {
		die("metadata out of range. %d\n", msz);
	}

	if (dsz > TD_DEC_DATA_MAX) {
		die("Datasize out of range. %d\n", dsz);
	}

	if(ssd > TD_CMD_SSD_MAX) {
		die("SSD out of range. %d\n", ssd);
	}
*/
	switch (cmd->cmd.id) {

	case TD_CMD_GET_REG: /* 4k,0b (default) */
		cmd->cmd.decode.to_host = 1;
	case TD_CMD_NULL:
	case TD_CMD_PUT_REG:
	case TD_CMD_RESET_CMD:
	case TD_CMD_DEALLOC_BUF:
		cmd->cmd.decode.hardware = 1;
		break;

	case TD_CMD_RD_PAGE:
	case TD_CMD_TEST_READ:
	case TD_CMD_RD_SEQ:
	case TD_CMD_RD_EXT:
		cmd->cmd.decode.meta_size = (uint8_t)msz;
	case TD_CMD_FW_DEBUG_LOG:
		cmd->cmd.decode.data_size = (uint8_t)dsz;
		cmd->cmd.decode.to_host = 1;
		cmd->cmd.ssd = ssd;
		break;

	case TD_CMD_COPY:
		cmd->cmd.decode.data_size = (uint8_t)dsz;
	case TD_CMD_SSD_STATS: /* size = 4K (default ) */
	case TD_CMD_GET_PARAMS:
	case TD_CMD_CRASH_LOG:
		cmd->cmd.decode.dupcheck = 1;
		cmd->cmd.decode.to_host = 1;
		break;

	case TD_CMD_FW_DOWNLOAD:
		cmd->cmd.decode.dupcheck = 1;
	case TD_CMD_WR_DLY:
	case TD_CMD_WR_FINAL:
	case TD_CMD_TEST_WRITE:
	case TD_CMD_WR_EXT:
		cmd->cmd.ssd = ssd;
		cmd->cmd.decode.meta_size = (uint8_t)msz;
		cmd->cmd.decode.data_size = (uint8_t)dsz;
		cmd->cmd.decode.from_host = 1;

	case TD_CMD_FW_NULL:
	case TD_CMD_SATA_REINIT:
	case TD_CMD_SHUTDOWN:
	case TD_CMD_STARTUP:
	case TD_CMD_TRACE_CTRL:
	case TD_CMD_TEST:
	case TD_CMD_SEQ:
	case TD_CMD_SSD_EXT_ERR: /* ? */
	case TD_CMD_SET_PARAMS:
		cmd->cmd.decode.dupcheck = 1;
		break;

	case TD_CMD_TRIM:
		cmd->cmd.ssd = ssd;
		cmd->cmd.decode.meta_size = (uint8_t)msz;
		break;

	case TD_CMD_SSD_CMD:
		cmd->cmd.decode.from_host = 1;
		cmd->cmd.decode.to_host = 1;
		cmd->cmd.decode.dupcheck = 0;
		cmd->cmd.decode.hardware = 0;
		cmd->cmd.ssd = ssd;
		cmd->cmd.decode.meta_size = (uint8_t)msz;
		cmd->cmd.decode.data_size = (uint8_t)dsz;
		break;

	/*
	case TD_CMD_CMDOFFSET_ERR:
	case TD_CMD_HW_CMD_ERR:
	case TD_CMD_DATA_ECC_ERR:
	case TD_CMD_TIMEOUT_ERR:
	case TD_CMD_CHKSUM_ERR:
	*/
	default:
		break;

	}
}
#endif

/* 4k end-to-end protection encoding */

struct td_e2e_4k {
	uint64_t fletcher0;
	uint64_t fletcher1;
	uint64_t server_id:32;
	uint64_t lba:32;
	uint64_t timestamp;
	uint64_t unused_0;
	uint64_t unused_1;
	uint64_t unused_2;
	uint64_t unused_3;
};
#define TD_E2E_4k_SIZE (sizeof(struct td_e2e_4k))

/**
 * generate a 4k E2E metadata tag
 * @param e2e   - E2E being built
 * @param xsum  - the fletcher checksum for this block
 * @param lba   - LBA of 512B block
 * @param server_id - server_id
 * @param timestamp - timestamp
 */
static inline void td_e2e_4k_gen(struct td_e2e_4k *e2e, const uint64_t *xsum,
		uint32_t lba, uint32_t server_id, uint64_t timestamp)
{
	e2e->fletcher0 = xsum[0];
	e2e->fletcher1 = xsum[1];
	e2e->lba = lba;
	e2e->server_id = server_id;
	e2e->timestamp = timestamp;
	e2e->unused_0 = 0;
	e2e->unused_1 = 0;
	e2e->unused_2 = 0;
	e2e->unused_3 = 0;
}

/**
 * verify 4k E2E metadata tag against xsum and lba
 * @param e2e   - E2E being checked
 * @param xsum  - the fletcher checksum for this block
 * @param lba   - LBA of 512B block
 * @return zero on success, negative error code otherwise
 */
static inline int td_e2e_4k_check(const struct td_e2e_4k *e2e,
		const uint64_t *xsum, uint32_t lba)
{
	if (unlikely (e2e->fletcher0 != xsum[0]
				|| e2e->fletcher1 != xsum[1]))
		return -TD_ERR_E2E_BAD_XSUM;

	if (unlikely (e2e->lba != lba))
		return -TD_ERR_E2E_BAD_LBA;

	return 0;
}



struct td_e2e_512B {
	uint64_t folded_fletcher:32;
	uint64_t lba:32;
};
#define TD_E2E_512_SIZE (sizeof(struct td_e2e_512B))

static inline uint32_t td_e2e_512B_fold_fletcher(uint64_t fletcher0,
		uint64_t fletcher1)
{
	return (uint32_t)fletcher0 ^ (uint32_t)(fletcher0 >> 32)
	     ^ (uint32_t)fletcher1 ^ (uint32_t)(fletcher1 >> 32);
}

/**
 * generate a 512B E2E metadata tag
 * @param e2e   - E2E being built
 * @param xsum  - the fletcher checksum for this block
 * @param lba   - LBA of 512B block
 */
static inline void td_e2e_512B_gen(struct td_e2e_512B *e2e,
		const uint64_t *xsum, uint32_t lba)
{
	/* generate the next E2E metadata block */
	e2e->folded_fletcher = td_e2e_512B_fold_fletcher(xsum[0], xsum[1]);
	e2e->lba = lba;
}

/**
 * verify 512B E2E metadata tag against xsum and lba
 * @param e2e   - E2E being checked
 * @param xsum  - the fletcher checksum for this block
 * @param lba   - LBA of 512B block
 * @return zero on success, negative error code otherwise
 */
static inline int td_e2e_512B_check(const struct td_e2e_512B *e2e,
		const uint64_t *xsum, uint32_t lba)
{
	uint32_t folded_fletcher;

	folded_fletcher = td_e2e_512B_fold_fletcher(xsum[0], xsum[1]);

	if (unlikely (e2e->folded_fletcher != folded_fletcher))
		return -TD_ERR_E2E_BAD_XSUM;

	if (unlikely (e2e->lba != lba))
		return -TD_ERR_E2E_BAD_LBA;

	return 0;
}


/**
 * update global fletcher checksum for write command
 * @param g_xsum - checksum protecting the whole transaction (upto 4k + 64B)
 * @param l_xsum - checksum of a data block
 * @param l_size - size of the data block for which l_xsum was computed
 */
static inline void td_global_fletcher_update(uint64_t *g_xsum,
		const uint64_t *l_xsum, unsigned l_size)
{
	unsigned l_words = l_size / 8;

	/* update the "all data fletcher checksum" with the
	 * computation of this sector's data checksum */
	g_xsum[1] += (l_words * g_xsum[0])
		     + l_xsum[1];
	g_xsum[0] += l_xsum[0];
}

struct td_bio_timeout_enum_desc {
	//union {
	//	enum td_write_status write;
	//	enum td_read_status read;
	//};
	uint32_t td_status_enum;
	uint32_t td_timeout_enum;
};

#endif
