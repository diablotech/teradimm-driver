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

#ifndef __included_kabi_h__
#warning KABI header file not included before conifg file
#endif

#define CONFIG_TERADIMM_TRACE
#define CONFIG_TERADIMM_COMPLETE_WRITE_FIRST
#undef CONFIG_TERADIMM_DIRECT_STATUS_ACCESS
#define CONFIG_TERADIMM_SYSFS
#undef CONFIG_TERADIMM_SIMULATOR
#define CONFIG_TERADIMM_RMW_EARLY_WRBUF
#define CONFIG_TERADIMM_DEBUG
#define CONFIG_TERADIMM_DEBUG_DEVICE_LOCK
#undef CONFIG_TERADIMM_RDBUF_TRACKING
#undef CONFIG_TERADIMM_RDBUF_TRACKING_IOCTL
#undef CONFIG_TERADIMM_MEGADIMM
#define CONFIG_TERADIMM_ACPI_ASL
#define CONFIG_TERADIMM_SGIO
#define CONFIG_MEGADIMM_TRACE_TOKENS 66
#undef CONFIG_TERADIMM_USES_CORE_BUFFS_ONLY
#undef CONFIG_TD_HISTOGRAM
#undef CONFIG_TERADIMM_CHECKSUM128_C
#define CONFIG_TERADIMM_INIT_MEMORY_MAPPING
#undef CONFIG_TERADIMM_INIT_MEMORY_MAPPING_ALWAYS
#define CONFIG_TERADIMM_MEMCPY_XSUM
#undef CONFIG_TERADIMM_STM
#undef CONFIG_TERADIMM_DEBUG_STATUS_CHANGES
#define CONFIG_TERADIMM_RESERVE_TOKENS 32
#undef CONFIG_TERADIMM_ABSOLUTELY_NO_READS
#define CONFIG_TERADIMM_FORCE_4096_128
#define CONFIG_TERADIMM_SKIP_E2E
#define CONFIG_TERADIMM_ENABLE_FORCE_RESET
#define CONFIG_TERADIMM_SPY_UCMD
#define CONFIG_TERADIMM_ARM_RESET_ON_START
#undef CONFIG_TERADIMM_HALT_ON_WRITE_ERROR
#define CONFIG_TERADIMM_ERROR_INJECTION
#define CONFIG_TERADIMM_PRIVATE_SPLIT_STASH
#undef CONFIG_TERADIMM_FORCE_SSD_HACK
#define CONFIG_TERADIMM_LOCK_LESS_DEVICE_TRAVERSAL
#define CONFIG_TERADIMM_TRIM
#undef CONFIG_TERADIMM_TRACK_CPU_USAGE
#define CONFIG_TERADIMM_OFFLOAD_COMPLETION_THREAD
#define CONFIG_TERADIMM_BIO_SLEEP 1
#define CONFIG_TERADIMM_MCEFREE_STATUS_POLLING
#define CONFIG_TERADIMM_MCEFREE_FWSTATUS
#define CONFIG_TERADIMM_MCEFREE_TOKEN_TYPES
#define CONFIG_TERADIMM_SILENCE_ZERO_E2E_ERRORS
#undef CONFIG_TERADIMM_USE_NATIVE_GET_CYCLES
#undef CONFIG_TERADIMM_STATUS_FROM_LAST_CACHELINE
#undef CONFIG_TERADIMM_RDMETA_FROM_LAST_CACHELINE
#undef CONFIG_TERADIMM_MCEFREE_RDBUF_HOLD_STATS
#undef CONFIG_TERADIMM_DYNAMIC_READ_ALIASING
#undef CONFIG_TERADIMM_DYNAMIC_STATUS_ALIASING
#define CONFIG_TERADIMM_MAPPER_CACHING
#define CONFIG_TERADIMM_DONT_TRACE_IN_DEAD_STATE
#define CONFIG_TERADIMM_RUSH_INGRESS_PIPE

#define CONFIG_TERADIMM_INCOMING_BACKPRESSURE TD_BACKPRESSURE_EVENT

#ifdef KABI__ioremap_wc /* RHEL5 cannot kzalloc over 128k*/
#define CONFIG_TERADIMM_TOKEN_HISTORY 16
#else
#undef CONFIG_TERADIMM_TOKEN_HISTORY
#endif

#define CONFIG_TERADIMM_FWSTATUS_HISTORY 256

#define CONFIG_TERADIMM_DEPREICATED_RAID_CREATE_V0

/* valid settings for MAP are TD_MAP_TYPE_{UC,WC,WB} see td_mapper.h */
#ifdef KABI__ioremap_wc
#define CONFIG_TERADIMM_MAP_STATUS          TD_MAP_TYPE_WC
#define CONFIG_TERADIMM_MAP_EXT_STATUS      TD_MAP_TYPE_WC
#define CONFIG_TERADIMM_MAP_COMMAND         TD_MAP_TYPE_WC
#define CONFIG_TERADIMM_MAP_READ_DATA       TD_MAP_TYPE_WC
#define CONFIG_TERADIMM_MAP_READ_META_DATA  TD_MAP_TYPE_WC
#define CONFIG_TERADIMM_MAP_WRITE_DATA      TD_MAP_TYPE_WC
#define CONFIG_TERADIMM_MAP_WRITE_META_DATA TD_MAP_TYPE_WC

#define CONFIG_TERADIMM_FORCE_CMD_UC_MAP

#else
/* bug 6177 - cannot use WC mapping on REHL5 */
#define CONFIG_TERADIMM_MAP_STATUS          TD_MAP_TYPE_WB
#define CONFIG_TERADIMM_MAP_EXT_STATUS      TD_MAP_TYPE_WB
#define CONFIG_TERADIMM_MAP_COMMAND         TD_MAP_TYPE_WB
#define CONFIG_TERADIMM_MAP_READ_DATA       TD_MAP_TYPE_WB
#define CONFIG_TERADIMM_MAP_READ_META_DATA  TD_MAP_TYPE_WB
#define CONFIG_TERADIMM_MAP_WRITE_DATA      TD_MAP_TYPE_WB
#define CONFIG_TERADIMM_MAP_WRITE_META_DATA TD_MAP_TYPE_WB

#endif

