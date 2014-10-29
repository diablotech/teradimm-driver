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

#ifndef _TD_PARAMS_H_
#define _TD_PARAMS_H_


typedef union {
	uint64_t u64;
	uint8_t u8[8];
	struct {
		uint8_t hw[7];
		uint8_t fw;
	};
} teradimm_global_status_t;

enum td_global_status {
	TD_GLOBAL_STATUS_BLANK                 = 0x00,
	TD_GLOBAL_STATUS_MEMINIT               = 0x10,
	TD_GLOBAL_STATUS_SATA_PHY_UP           = 0x21,
	TD_GLOBAL_STATUS_SATA_VERCHECK         = 0x22,
	TD_GLOBAL_STATUS_SATA_something        = 0x23,
	TD_GLOBAL_STATUS_SECURE_ERASE          = 0x24,
};
#define TD_GLOBAL_STATUS_FINAL(a)  ((a) & 0x80)
#define TD_GLOBAL_STATUS_STATE(a)  ((a) & 0x7F)
#define TD_GLOBAL_STATUS_PERFECT(a)  ((a) == 0x80)

union td_param {
	uint32_t u32;
	uint16_t u16[2];
	uint8_t u8[4];
	struct {
		uint32_t value:31;
		uint32_t set:1;
	};
};

typedef struct picVersionInfo
{
    union
    {
        uint32_t u32;
        struct
        {
            uint8_t   activeVersion;
            uint8_t   inactiveVersion;
            uint8_t   expectedVersion;
            uint8_t   flags;
        };
    };
} picVersionInfo_t;
// Flags indicated in picVersionInfo.flags
#define PIC_VERFLAG_NO_ERROR                0
#define PIC_VERFLAG_INTEGRITY_FAILURE       1
#define PIC_VERFLAG_UPGRADE_RESET_PENDING   2
#define PIC_VERFLAG_MISMATCH_CAUSE_UNKNOWN  4
#define PIC_VERFLAG_OFFLINE_UPGRADE_REQUIRED 8

#define MONET_ID_SIZE 8

typedef struct td_param_page0_monet_map {
	char monet_fw_rev[MONET_ID_SIZE];
	union td_param lba_count_low;
	union td_param lba_count_high;
	union td_param d2h_reg_1;
	union td_param d2h_reg_2;
	union td_param d2h_reg_3;
} monet_fw_params_t;

#define MAX_SATA_PORT 2
#define CAPEX_FLAGS_SIZE 32

#define CAPEX_0_ASYNC_UPGRADE   0
#define CAPEX_0_PFAIL           1
#define CAPEX_0_SHUTDOWN        2
#define CAPEX_0_SPD_AVAILABLE   3
#define CAPEX_0_OVER_PROVISION  4
#define CAPEX_0_SATA_SLOT_LIMIT 5
#define CAPEX_0_SATA_EVENT_LOGS 6
#define CAPEX_0_THROTTLING      7
#define CAPEX_0_VERSION         8
#define CAPEX_0_STREAMING_IO    9
#define CAPEX_0_TEMP_MON        10
#define CAPEX_0_CRYPTO_ERASE    11
#define CAPEX_0_DIAG_REG_CMD    12
#define CAPEX_0_SEC             13
#define CAPEX_0_POWER_LIMIT     14
#define CAPEX_0_HOST_IRQ        15
#define CAPEX_0_RAID_PAGE       16


#define CAPEX_FLAG(_word, _bit)   (1 << CAPEX_ ## _word ## _ ## _bit )

#define CAPEX_CHECK(_param, _word, _bit)                        \
		(_word < (sizeof(_param)/sizeof(_param[0]))) && \
		(_param[_word].u32 & CAPEX_FLAG(_word, _bit))

#define PARAM_IDX_MAGIC		0
#define PARAM_IDX_PROTOCOL	1
#define PARAM_IDX_RUSH          2

#define DRAM_PROT_STATE_NO_SUPPORT  0
#define DRAM_PROT_STATE_SUPPORTED   0x1
#define DRAM_PROT_STATE_DEGRADED    0x2

#define FIRMWARE_BANK_0         28
#define FIRMWARE_BANK_1          9
#define FIRMWARE_BANK_2         11

#define TD_RAID_PARAM 8;


enum td_param_page0_version {
	PARAM_PAGE0_VERSION_160_SHORT = 0x00000160,
	PARAM_PAGE0_VERSION_161_SHORT = 0x00000161,
	PARAM_PAGE0_VERSION_161       = 0x00010601,
	PARAM_PAGE0_VERSION_162       = 0x00010602,
	PARAM_PAGE0_VERSION_163       = 0x00010603,
};

struct td_param_page0_map_160 {
	union td_param magic;
	union td_param protocol;
	union td_param rush_version;
	union td_param fw_version;
	union td_param fw_date;
	union td_param fw_bi_0;
	union td_param fw_bi_1;
	union td_param fw_version_backup;
	union td_param fw_date_backup;
	union td_param fw_bi_0_backup;
	union td_param fw_bi_1_backup;

	union td_param fw_crashdumps;
	union td_param fw_fallback;

	union td_param lba_size;
	union td_param lba_align_mask;

	union td_param RushWriteBufSize;
	union td_param RushWriteMetaSize;
	union td_param RushCmdBufSize;
	union td_param RushCmdStatusSize;
	union td_param RushExStatusSize;

	union td_param RushCoreBufCount;
	union td_param RushWriteCount;
	union td_param RushReadCount;
	union td_param RushCmdCount;

	union td_param ssd_total;
	union td_param ssd_online_mask;

	union td_param _reserved[6];
	union td_param capex[32];

	union td_param ssd_page_offset;
	monet_fw_params_t mMonetParams[MAX_SATA_PORT];
};

struct td_param_page0_map_161 {
	union td_param mMagicNumber;
	union td_param mProtocolVer;
	union td_param mRushVersion;

	union td_param mFWVersion;
	union td_param mFWDate;

	union td_param currRushFwPkgVersHi;
	union td_param currRushFwPkgVersLo;
	union td_param currRushFwPkgDateHi;
	union td_param currRushFwPkgDateLo;
	union td_param currRushFwPkgBuildIdHi;
	union td_param currRushFwPkgBuildIdLo;

	union td_param obsRushFwPkgVersHi;
	union td_param obsRushFwPkgVersLo;
	union td_param obsRushFwPkgDateHi;
	union td_param obsRushFwPkgDateLo;
	union td_param obsRushFwPkgBuildIdHi;
	union td_param obsRushFwPkgBuildIdLo;

	union td_param prod0RushFwPkgVersHi;
	union td_param prod0RushFwPkgVersLo;
	union td_param prod0RushFwPkgDateHi;
	union td_param prod0RushFwPkgDateLo;
	union td_param prod0RushFwPkgBuildIdHi;
	union td_param prod0RushFwPkgBuildIdLo;

	union td_param currRushFwVersHi;
	union td_param currRushFwVersLo;
	union td_param currRushFwDateHi;
	union td_param currRushFwDateLo;
	union td_param currRushFwBuildIdHi;
	union td_param currRushFwBuildIdLo;

	union td_param obsRushFwVersHi;
	union td_param obsRushFwVersLo;
	union td_param obsRushFwDateHi;
	union td_param obsRushFwDateLo;
	union td_param obsRushFwBuildIdHi;
	union td_param obsRushFwBuildIdLo;

	union td_param prod0RushFwVersHi;
	union td_param prod0RushFwVersLo;

	union td_param prod0RushFwDateHi;
	union td_param prod0RushFwDateLo;
	union td_param prod0RushFwBuildIdHi;
	union td_param prod0RushFwBuildIdLo;

	union td_param mFWCrashDump;
	union td_param mFWFallBack;
	union td_param mLbaSize;
	union td_param mLbaAlignMask;
	union td_param mRushDataSize;
	union td_param mRushMetaDataSize;
	union td_param mRushCMDSize;
	union td_param mRushStatusSize;
	union td_param mRushExtStatusSize;
	union td_param mRushCoreBufCount;
	union td_param mRushWriteBufCount;
	union td_param mRushReadBufCount;
	union td_param mRushCMDSlotCount;
	union td_param mSSDTotal;
	union td_param mSSDOnlineMask;
	union td_param Reserved_0[6];
	union td_param mCapexFlags[CAPEX_FLAGS_SIZE];
	union td_param mMonetPageOffset;
	monet_fw_params_t mMonetParams[MAX_SATA_PORT];
	union td_param Reserved_1[19];
};

struct td_param_page0_map_162 {
	union td_param mMagicNumber; // 0
	union td_param mProtocolVer; // 1
	union td_param mRushVersion; // 2

	union td_param Reserved_0[2]; // 3

	union td_param currRushFwPkgVersHi; // 5
	union td_param currRushFwPkgVersLo; // 6
	union td_param currRushFwPkgDateHi; // 7
	union td_param currRushFwPkgDateLo; // 8
	union td_param currRushFwPkgBuildIdHi; // 9
	union td_param currRushFwPkgBuildIdLo; // 10

	union td_param obsRushFwPkgVersHi; // 11
	union td_param obsRushFwPkgVersLo; // 12
	union td_param obsRushFwPkgDateHi; // 13
	union td_param obsRushFwPkgDateLo; // 14
	union td_param obsRushFwPkgBuildIdHi; // 15
	union td_param obsRushFwPkgBuildIdLo; // 16

	union td_param prod0RushFwPkgVersHi; // 17
	union td_param prod0RushFwPkgVersLo; // 18
	union td_param prod0RushFwPkgDateHi; // 19
	union td_param prod0RushFwPkgDateLo; // 20
	union td_param prod0RushFwPkgBuildIdHi; // 21
	union td_param prod0RushFwPkgBuildIdLo; // 22

	union td_param currRushFwVersHi; // 23
	union td_param currRushFwVersLo; // 24
	union td_param currRushFwDateHi; // 25
	union td_param currRushFwDateLo; // 26
	union td_param currRushFwBuildIdHi; // 27
	union td_param currRushFwBuildIdLo; // 28

	union td_param obsRushFwVersHi; // 29
	union td_param obsRushFwVersLo; // 30
	union td_param obsRushFwDateHi; // 31
	union td_param obsRushFwDateLo; // 32
	union td_param obsRushFwBuildIdHi; // 33
	union td_param obsRushFwBuildIdLo; // 34

	union td_param prod0RushFwVersHi; // 35
	union td_param prod0RushFwVersLo; // 36
	union td_param prod0RushFwDateHi; // 37
	union td_param prod0RushFwDateLo; // 38
	union td_param prod0RushFwBuildIdHi; // 39
	union td_param prod0RushFwBuildIdLo; // 40

	union td_param mFWCrashDump; // 41
	union td_param mFWFallBack; // 42
	union td_param mLbaSize; // 43
	union td_param mLbaAlignMask; // 44
	union td_param mRushDataSize; // 45
	union td_param mRushMetaDataSize; // 46
	union td_param mRushCMDSize; // 47
	union td_param mRushStatusSize; // 48
	union td_param mRushExtStatusSize; // 49
	union td_param mRushCoreBufCount; // 50
	union td_param mRushWriteBufCount; // 51
	union td_param mRushReadBufCount; // 52
	union td_param mRushCMDSlotCount; // 53
	union td_param mSSDTotal; // 54
	union td_param mSSDOnlineMask; // 55
	union td_param mDRAMProtectionState; // 56
	union td_param mZQErrFlag; // 57
	union td_param Reserved_1[4]; // 58
	union td_param mCapexFlags[CAPEX_FLAGS_SIZE]; // 62
	union td_param mMonetPageOffset; // 94
	monet_fw_params_t mMonetParams[MAX_SATA_PORT]; // 95
	union td_param mRsvdCoreBuffMask1; // 109
	union td_param mRsvdCoreBuffMask2; // 110
	union td_param mRsvdCoreBuffMask3; // 111
	union td_param mRsvdCoreBuffMask4; // 112
	picVersionInfo_t mPicVerInfo; // 113
	union td_param Reserved_2[14]; // 114
};

struct td_param_page1_map {
	union td_param magic_1;
	union td_param magic_2;
	union td_param fw_interrupt_threshold;
	union td_param fw_interrupt_timeout;
	union td_param fw_temp_poll_rate;
	union td_param fw_ssd_fua_mode;
	union td_param host_blocksize;
	union td_param host_e2e_mode;
	union td_param host_ssd_stride_sectors;
	union td_param host_provision_low;
	union td_param host_provision_high;
	union td_param device_alias[11];
	union td_param host_trim;
	union td_param ecc_alarm;
	union td_param Reserved[104];
};

struct td_fw_ecc_err_info {
	union td_param HOSTECCErrCnt;
	union td_param HISCEccErrCnt;
	union td_param CPMDMAErrCnt;
	union td_param HIMParityErrCnt;
	/* ICBM memory errors */
	union td_param CoreBufErrFlag0;    /* core buffers 0~31 */
	union td_param CoreBufErrFlag1;    /* bit 0~15: core buffers 32~47, bit 16~31: core meta data buffers 0~15  */

	union td_param CoreBufErrSingle;
	union td_param CoreBufErrMulti;

	union td_param CoreMetaBufErrSingle;
	union td_param CoreMetaBufErrMulti;

	union td_param CmdBuffErrSingle;
	union td_param CmdBuffErrMulti;

	union td_param CmdFifoErrSingle;
	union td_param CmdFifoErrMulti;

	union td_param CmdNumFifoErrSingle;
	union td_param CmdNumFifoErrMulti;

	union td_param StatusErrSingle;
	union td_param StatusErrMulti;

	/* SATA errors */
	union td_param AhciErrSingle;
	union td_param AhciErrMulti;

	union td_param P0RxFifoErrSingle;
	union td_param P0RxFifoErrMulti;

	union td_param P0TxFifoErrSingle;
	union td_param P0TxFifoErrMulti;

	union td_param P1RxFifoErrSingle;
	union td_param P1RxFifoErrMulti;

	union td_param P1TxFifoErrSingle;
	union td_param P1TxFifoErrMulti;

	/* HIM CBI FIFO errors */
	union td_param CbiFifoErrSingle;
	union td_param CbiFifoErrMulti;

	/* CPM memory errors */
	union td_param DtcmErrSingle;
	union td_param DtcmErrMulti;

	union td_param ItcmErrSingle;
	union td_param ItcmErrMulti;
};

struct td_param_page4_map {
	union td_param mMagicNumber; // set to 0x00d1ab10
	union td_param mHostCapexFlags[32];

	// corresponds to CAPEX_FLAG_SATA_SLOT_LIMIT
	union td_param mSataSlotLimit;			// 33

	// corresponds to CAPEX_FLAG_THROTTLE
	union td_param mThrottleBandwidth;		// 34

	union td_param Reserved_0;			// 35
	// corresponds to CAPEX_FLAG_STREAMING_IO
	union td_param mStreamingIOProfileRead4K;	// 36
	union td_param mStreamingIOProfileRead512;	// 37
	union td_param mStreamingIOProfileWrite4K;	// 38
	union td_param mStreamingIOProfileWrite512;	// 39
	union td_param mStreamingIOBuckets4096W[8];	// 40-47
	union td_param mStreamingIOBuckets4096R[8];	// 48-55
	union td_param mStreamingIOBuckets512W[8];	// 56-63
	union td_param mStreamingIOBuckets512R[8];	// 64-71

	// corresponds to CAPEX_FLAG_TEMP_MON
	union td_param mLastMonetTemp;			// 72

	// Streaming Tuning
	union td_param mPatchProfile;			// 73
	union td_param mTargetStreamSize;		// 74
	union td_param mMaxStreamSize;			// 75
	union td_param mSlotThresh;			// 76
	union td_param mCheckCoreBufs;			// 77
	union td_param mSeqLbaThresh;			// 78
	union td_param mDelayTime;			// 79

	// ECC error info
	struct td_fw_ecc_err_info mEccErrInfo;		// 80-113

	union td_param mSpiAddr;			// 114
	union td_param mPowerLimit;                     // 115
	union td_param mIntDecimation;			// 116
	union td_param Reserved_1[11];            // 117-127
};

struct td_param_page5_map {
	union td_param mMagicNumber;
	union td_param mSataPort_errCount;
	union td_param mUnknownFIS_errCount;
	union td_param mPortConnectChange_errCount;
	union td_param mPhyRdyChange_errCount;
	union td_param mOverflow_errCount;
	union td_param mInterfaceNonFatal_errCount;
	union td_param mInterfaceFatal_errCount;
	union td_param mHostBusData_errCount;
	union td_param mHostBusFatal_errCount;
	union td_param mTaskFile_errCount;

	// (ERR)
	union td_param mRecoveredDataIntegrityError_I;
	union td_param mRecoveredCOMMError_M;
	union td_param mTranDataIntError_T;
	union td_param mPerCommError_C;
	union td_param mProtError_P;
	union td_param mIntError_E;

	//(DIAG)
	union td_param mPhyRdyChange_N;
	union td_param mPhyIntError_I;
	union td_param mCommWake_W;
	union td_param mDecode10B8BError_B;
	union td_param mDisparityError_D;
	union td_param mCrcError_C;
	union td_param mHandshakeError_H;
	union td_param mLinkSequenceError_S;
	union td_param mTransportStateTransError_T;
	union td_param mUnknownFISType_F;
	union td_param mExchanged_X;

	union td_param mLastSataError;  // snapshot of last PxSERR/PxISR reported value
	union td_param mPxTFESreg;      // snapshot of last TFES register contents reported.
	union td_param Reserved[98];
};

struct td_param_fw_bank_version_info {
	union td_param VersHi;
	union td_param VersLo;
	union td_param DateHi;
	union td_param DateLo;
	union td_param BuildIdHi;
	union td_param BuildIdLo;
};

struct td_param_page7_map {
	union td_param mMagicNumber;
	union td_param mRegisterVendorID;
	union td_param mManufacturingDate;
	union td_param mModuleSerialNumber;
	union td_param mModuleRevisionCode;
	union td_param mModuleVendorID;
	union td_param mMSDType;
	union td_param mMSDConfig;
	union td_param mMSDCapacity;
	union td_param mMSDPFail;
	union td_param mMSDHostInterfaceASIC;
	union td_param mMSDMaxPower;
	union td_param mMSDBootable;
	union td_param mMSDPlatform;       
	union td_param mMSDBrand;          
	union td_param mMSDFeatureBits;    
	union td_param mMSD_RFU[8];
	union td_param mMSD_CRC;           
	union td_param mMSDDiabloVendorID;

	struct td_param_fw_bank_version_info packageBank0;
	struct td_param_fw_bank_version_info packageBank1;
	struct td_param_fw_bank_version_info packageBank2;

	union td_param currentFWBank;
	union td_param alternateFWBank;
	union td_param selectedFWBank;

	union td_param mReserved[17];
	uint8_t mRawSPD[256];
};

enum TD_ENG_E2E_MODE {
	E2E_OFF = 0,            // TD_E2E_MODE_NONE
	E2E_512B = 1,           // TD_E2E_MODE_512B
	E2E_4kB =  2,           // TD_E2E_MODE_4kB
};

struct td_param_raid_member {
	union td_param UUID[TR_UUID_LENGTH/3];
};

struct td_param_raid {
	union td_param UUID[TR_UUID_LENGTH];
	union td_param name[TD_DEVICE_NAME_MAX];
	union td_param version;
	union td_param dev_count;
	union td_param raid_level;
	union td_param write_count;
	struct td_param_raid_member members[2];    /* At least 2 members. */
};

#endif
