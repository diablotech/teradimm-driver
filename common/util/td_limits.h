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

#ifndef _TD_LIMITS_H_
#define _TD_LIMITS_H_

#include <linux/types.h>
#include "td_compat.h"

#define TD_PAGE_SIZE 4096                 /* natural size of a data buffer */

#define TD_IOREMAP_SIZE (1ULL << 26)      /* 64M */

/* configuration */

#define TD_MAX_DEVICES       32

#define TD_CHAN_SHIFT        0  /*  1 channel / device (channels disabled) */

#define TD_TOKEN_SHIFT       8 /*  256 tokens / device */
#define TD_LUN_SHIFT         6  /*  64 luns / channel */
#define TD_LUNBUSES_SHIFT    TD_LUN_SHIFT /*  default, as many buses as luns */

#define TD_HOST_RD_BUF_SHIFT 5  /*  32 host read bufs / device */
#define TD_HOST_WR_BUF_SHIFT 3  /*  8 host write bufs / device */

#define TD_CORE_BUF_SHIFT    7  /*  128 core buffers / device */

#define TD_SSD_RD_BUF_SHIFT  5  /*  32 SSD read buffers / channel */
#define TD_SSD_WR_BUF_SHIFT  5  /*  32 SSD write buffers / channel */

#define TD_WR_BURST_SHIFT    2  /*  4 writes at a time */

#define TD_MAX_ALLOWED_ALIASES 131072 /*  arbitrary */

/*  derived limits */

#define TD_CHANS_PER_DEV        (1 << TD_CHAN_SHIFT)        /**< channels per device */
#define TD_TOKENS_PER_DEV       (1 << TD_TOKEN_SHIFT)       /**< tokens per device */

#define TD_LUNS_PER_CHAN        (1 << TD_LUN_SHIFT)         /**< luns per channel */
#define TD_LUNBUSES_PER_CHAN    (1 << TD_LUNBUSES_SHIFT)    /**< lun<=>ssd buses per channel */

#define TD_HOST_RD_BUFS_PER_DEV (1 << TD_HOST_RD_BUF_SHIFT) /**< host read buffers per device */
#define TD_HOST_WR_BUFS_PER_DEV (1 << TD_HOST_WR_BUF_SHIFT) /**< host write buffers per device */

#define TD_CORE_BUFS_PER_DEV    (1 << TD_CORE_BUF_SHIFT)    /**< core buffers per device */

#define TD_SSD_RD_BUFS_PER_CHAN (1 << TD_SSD_RD_BUF_SHIFT)  /**< SSD read buffers per chan */
#define TD_SSD_WR_BUFS_PER_CHAN (1 << TD_SSD_WR_BUF_SHIFT)  /**< SSD write buffers per chan */

#define TD_LUNS_PER_DEV (TD_CHANS_PER_DEV*TD_LUNS_PER_CHAN)   /**< luns per device */
#define TD_LUNBUSES_PER_DEV \
	(TD_CHANS_PER_DEV*TD_LUNBUSES_PER_CHAN)             /**< lun<=>ssd buses / dev */

#define TD_SSD_RD_BUFS_PER_DEV \
	(TD_CHANS_PER_DEV*TD_SSD_RD_BUFS_PER_CHAN)          /**< ssd read bufs / dev */
#define TD_SSD_WR_BUFS_PER_DEV \
	(TD_CHANS_PER_DEV*TD_SSD_WR_BUFS_PER_CHAN)          /**< ssd write bufs / dev */

#define TD_WR_BURST             (1 << TD_WR_BURST_SHIFT)    /**< writes to combine */

/*  absolute maximum for buffers (used for allocating generic bitmaps) */

#define TD_MAX_BUFS_PER_DEV 1024 /*  maximum buffers of any kind per device */

/*  validate maximums */

#if TD_HOST_RD_BUFS_PER_DEV > TD_MAX_BUFS_PER_DEV
#error TD_HOST_RD_BUFS_PER_DEV is too big, limit to TD_MAX_BUFS_PER_DEV
#endif
#if TD_HOST_WR_BUFS_PER_DEV > TD_MAX_BUFS_PER_DEV
#error TD_HOST_WR_BUFS_PER_DEV is too big, limit to TD_MAX_BUFS_PER_DEV
#endif
#if TD_CORE_BUFS_PER_DEV > TD_MAX_BUFS_PER_DEV
#error TD_CORE_BUFS_PER_DEV is too big, limit to TD_MAX_BUFS_PER_DEV
#endif
#if TD_SSD_WR_BUFS_PER_DEV > TD_MAX_BUFS_PER_DEV
#error TD_SSD_WR_BUFS_PER_DEV is too big, limit to TD_MAX_BUFS_PER_DEV
#endif
#if TD_SSD_RD_BUFS_PER_DEV > TD_MAX_BUFS_PER_DEV
#error TD_SSD_RD_BUFS_PER_DEV is too big, limit to TD_MAX_BUFS_PER_DEV
#endif

/**< type large enough to hold TD_CHANS_PER_DEV bits */
#if TD_CHANS_PER_DEV <= (__SIZEOF_LONG__ * 8)
typedef unsigned long td_chan_mask_t;
#else
#error TD_CHANS_PER_DEV is too big, reduce TD_CHAN_SHIFT
#endif

#endif
