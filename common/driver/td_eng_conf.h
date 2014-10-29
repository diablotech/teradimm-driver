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

#ifndef _TD_ENG_CONF_H_
#define _TD_ENG_CONF_H_

#include "td_kdefn.h"

#include "td_compat.h"
#include "td_defs.h"

#define TD_CLFLUSH_OFF           0x0000

#define TD_FLUSH_RDBUF_MASK      0x000F
#define TD_FLUSH_RDBUF_CLF_PRE   0x0001 /**< clflush_cache_range() before read buffer read */
#define TD_FLUSH_RDBUF_CLF_POST  0x0002 /**< clflush_cache_range() after read buffer read */
#define TD_FLUSH_RDBUF_NTF_PRE   0x0003 /**< td_fill_8x8_movnti() before read buffer read */
#define TD_FLUSH_RDBUF_NTF_POST  0x0004 /**< td_fill_8x8_movnti() after read buffer read */

#define TD_FLUSH_STATUS_MASK     0x00F0
#define TD_FLUSH_STATUS_CLF_PRE  0x0010 /**< clflush_cache_range() before status buffer read */
#define TD_FLUSH_STATUS_CLF_POST 0x0020 /**< clflush_cache_range() after status buffer read */
#define TD_FLUSH_STATUS_NTF_PRE  0x0030 /**< td_fill_8x8_movnti() before status buffer read */
#define TD_FLUSH_STATUS_NTF_POST 0x0040 /**< td_fill_8x8_movnti() after status buffer read */

/* TODO: these are for debugging, they can be removed */
#define TD_FLUSH_CMD_MASK        0x0F00
#define TD_FLUSH_CMD_CLF_POST    0x0200 /**< clflush_cache_range() after command buffer write */
#define TD_FLUSH_CMD_NTF_POST    0x0400 /**< td_fill_8x8_movnti() after command buffer write */
#define TD_FLUSH_WRBUF_MASK      0xF000
#define TD_FLUSH_WRBUF_CLF_POST  0x2000 /**< clflush_cache_range() after write buffer write */
#define TD_FLUSH_WRBUF_NTF_POST  0x4000 /**< td_fill_8x8_movnti() after write buffer write */


#define td_eng_conf_var_get(eng, which)                                 \
	((eng)->conf.regs[TD_CONF_##which])
#define td_eng_conf_var_set(eng, which, val)                            \
	do { (eng)->conf.regs[TD_CONF_##which] = val;                   \
	td_eng_debug(eng, "CONF %s set to %llu\n", __stringify(which), (eng)->conf.regs[TD_CONF_##which]); \
	} while (0)
#define td_eng_conf_var_dec(eng, which)                                 \
	do { (eng)->conf.regs[TD_CONF_##which] -= 1; } while (0)

#define td_eng_conf_hw_var_get(eng, which)                              \
	((eng)->conf.hardware[TD_CONF_HW_##which])
#define td_eng_conf_hw_var_set(eng, which, val)                         \
	do { (eng)->conf.hardware[TD_CONF_HW_##which] = val; \
	     td_eng_debug(eng, "HW CONF %s set to %llu\n", __stringify(which), (eng)->conf.hardware[TD_CONF_HW_##which]); \
	   } while (0)

#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
#define td_eng_conf_mcefree_var_get(eng, which)                              \
	((eng)->conf.mcefree[TD_CONF_MCEFREE_##which])
#define td_eng_conf_mcefree_var_set(eng, which, val)                         \
	do { (eng)->conf.mcefree[TD_CONF_MCEFREE_##which] = val; \
	     td_eng_debug(eng, "MCEFREE CONF %s set to %llu\n", __stringify(which), (eng)->conf.mcefree[TD_CONF_MCEFREE_##which]); \
	   } while (0)
#endif

#define TD_WBINVD_OFF                   0
#define TD_WBINVD_STATUS_ALIAS          2   /**< wbinvd() after status aliases exhausted */
#define TD_WBINVD_READ_ALIAS            4   /**< wbinvd() after read aliases exhausted */

#define TD_E2E_MODE_OFF  0                  /** Make sure OFF is always 0 */
#define TD_E2E_MODE_512B 1
#define TD_E2E_MODE_4kB  2



struct td_engine;
struct td_conf_var_desc {
	int (*check)(struct td_engine *eng, uint32_t conf_num, uint64_t val);
	uint64_t min, max;
};

struct td_eng_conf {
	uint64_t regs[TD_CONF_REGS_MAX];
	uint64_t hardware[TD_CONF_HW_REGS_MAX];
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
	uint64_t mcefree[TD_CONF_MCEFREE_REGS_MAX];
#endif
};

extern const struct td_conf_var_desc td_eng_conf_var_desc[TD_CONF_REGS_MAX];
extern const struct td_conf_var_desc td_eng_conf_hw_var_desc[TD_CONF_HW_REGS_MAX];
#ifdef CONFIG_TERADIMM_MCEFREE_FWSTATUS
extern const struct td_conf_var_desc td_eng_conf_mcefree_var_desc[TD_CONF_MCEFREE_REGS_MAX];
#endif

extern const struct td_timeout_desc td_timeout_lookup[TD_TIMEOUT_MAX];

extern int td_conf_validate_cache (struct td_engine *eng, uint64_t cache);


struct td_eng_conf_sim {
	/*  operational constraints */
	uint    channels;		/**< number of SSDs/channels */
	uint    luns;			/**< number of LUNs per channel */
	uint    lunbuses;               /**< number of SSD<=>LUN buses per channel */


	uint    host_core_xfer_nsec;	/**< 4k host<->core xfer time in nanoseconds */
	uint    core_ssd_xfer_nsec;	/**< 4k core<->ssd xfer time in nanoseconds */
	uint    ssd_lun_xfer_nsec;	/**< 4k ssd<->lun xfer time in nanoseconds */

	uint    proc_cmd_nsec;	        /**< cmd overhead in microprocessor in nanoseconds */
	uint    proc_stat_nsec;	        /**< status overhead in microprocessor in nanoseconds */

	uint64_t    ssd_read_bufs;		/**< number of SSD read buffers */
	uint64_t    ssd_write_bufs;		/**< number of SSD write buffers */

	uint    horizon_usec;           /**< when to drop interval list data in simulator */

	uint    flash_read_nsec;	/**< read flash latency in microseconds */
	uint    flash_write_nsec;	/**< write flash latency in microseconds */
};

extern void td_eng_conf_init(struct td_engine *eng, struct td_eng_conf *conf);
extern void td_eng_conf_sim_init(struct td_eng_conf_sim *conf);

#endif
