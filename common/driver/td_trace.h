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

#ifndef _TD_TRACE_H_
#define _TD_TRACE_H_

#include "td_compat.h"

struct td_trace_iterator;

#ifdef CONFIG_TERADIMM_TRACE

enum td_print_index {
	TP_LEGACY          =       1UL<<0,
	TP_1X64            =       1UL<<1,
	TP_2X32            =       1UL<<2,
	TP_4X16            =       1UL<<3,

	TP_MAX             =       1UL<<4,
};

struct td_trace_entry {
	cycles_t ts;
	uint64_t data;
	const char *label;
	unsigned cpu:8;
	enum td_print_index print_index;
};

struct td_trace {
	uint64_t                tt_mask;
	struct td_trace_entry   *tt_entries;
	unsigned int            tt_max;
	unsigned int            tt_next;
	unsigned int            tt_wrap:1;    /**< enable wrap around of trace data */
};

enum td_trace_type {
	TR_ERR          =       -1,       // always show
	TR_BIO          =       1UL<<0,   // 1
	TR_CMD          =       1UL<<1,   // 2
	TR_SIM          =       1UL<<2,   // 4
	TR_RDBUF        =       1UL<<4,   // 16
	TR_TOKEN        =       1UL<<5,   // 32
	TR_STATE        =       1UL<<6,   // 64
	TR_TRIM         =       1UL<<7,   // 128
	TR_FW           =       1UL<<8,   // 256
	TR_LATENCY      =       1UL<<9,   // 512
	TR_RMW          =       1UL<<10,  // 1024
	TR_CPU          =       1UL<<11,  // 2048
	TR_VMK_QUEUE    =       1UL<<12,  // 4096
	TR_VMK_SPLIT    =       1UL<<13,  // 8192

	/* super verbose traces start above bit 16 */
	TR_STATUS       =       1UL<<16,
	TR_COPYOPS      =       1UL<<17,
	TR_LOOP         =       1UL<<18,
	TR_SAMPLE       =       1UL<<19,
	TR_MCEFREE      =       1UL<<20,
	TR_ENGWORK      =       1UL<<21,
	TR_SEQ          =       1UL<<22,
	TR_SCOUT	=	1UL<<23,

	TR_VMK_AIDAN	=	1UL<<24,

	TR_MAX          =       1UL<<31,
};

/* disabled by default */
#define TD_TRACE_DEFAULT_MASK   0x0
#define TD_TRACE_DEFAULT_SIZE   10000

static inline int td_trace_enabled(struct td_trace *trc, enum td_trace_type _t_)
{
	return ((trc) && ((trc)->tt_mask&(_t_)) && (trc)->tt_max);
}

#ifdef CONFIG_TERADIMM_TRACE_TO_PRINTK
/* very very verbose output to dmesg/syslog/console */
#define td_trace_to_printk(trc,_ts_,_t_,_l_,_x_) \
	printk("%16llu %s:%u %s %016llx (%lld)\n", \
		_ts_, __FUNCTION__, __LINE__, _l_, \
		(uint64_t)_x_, (uint64_t)_x_)
#else
#define td_trace_to_printk(trc,_ts_,_t_,_l_,_x_) do { /* nothing */ } while(0)
#endif

#define td_trace_ts(trc,_ts_,_t_,_l_,_i_,_x_) do {                            \
	td_trace_to_printk((trc),_ts_,_t_,_l_,_x_);                           \
	if (td_trace_enabled((trc),_t_)) {                                    \
		if ((trc)->tt_wrap || ((trc)->tt_next <= (trc)->tt_max)) {    \
			int __i = ((trc)->tt_next++) % (trc)->tt_max;         \
			struct td_trace_entry *__t = (trc)->tt_entries + __i; \
			__t->cpu = smp_processor_id();                        \
			__t->ts = (_ts_);                                     \
			__t->label = (_l_);                                   \
			__t->print_index = (_i_);                              \
			__t->data = (_x_);                                     \
		}                                                             \
	}                                                                     \
}while(0)

extern int td_trace_init(struct td_trace *trc, const char *name, int node);
extern void td_trace_cleanup(struct td_trace *trc);
extern void td_trace_reset(struct td_trace *trc);
extern void td_trace_dump(struct td_trace *trc);

static inline void td_trace_set_mask (struct td_trace *trc, uint64_t mask)
{
	trc->tt_mask = mask;
}
static inline uint64_t td_trace_get_mask (struct td_trace *trc)
{
	return trc->tt_mask;
}

static inline void td_trace_set_wrap (struct td_trace *trc, int wrap)
{
	trc->tt_wrap = !!wrap;
}
static inline int td_trace_get_wrap (struct td_trace *trc)
{
	return !!trc->tt_wrap;
}

extern ssize_t td_trace_iterator_read (struct td_trace *trc,
		struct td_trace_iterator*, char __user *,
		size_t);

#else
struct td_trace { };
struct td_trace_entry { };
#define td_trace_init(trc,name,node)          (0)
#define td_trace_cleanup(trc)                 {}
#define td_trace_reset(trc)                   {}
#define td_trace_enabled(trc,_t_)             (0)
#define td_trace_ts(trc,_ts_,_t_,_l_,_i_,_x_) {}
#define td_trace_dump(trc)                    {}
#define td_trace_set_mask(trc,m)              {}
#define td_trace_get_mask(trc)                (0)
#define td_trace_set_wrap(trc,w)              {}
#define td_trace_get_wrap(trc)                (0)
#define td_trace_read(trc,ptr,len,ofs)        (-ENOENT)
#define td_trace_iterator_open(trc)           (NULL)
#define td_trace_iterator_read(i,p,l,s)       (-ENOENT)
#define td_trace_iterator_release(i)          {}
#endif

#define td_trace(trc,_t_,_l_,_x_) \
	td_trace_ts(trc, td_get_cycles(), _t_, _l_, TP_LEGACY, _x_)

#define td_tracef_1x64(trc,_t_,_l_,_u64_) \
	td_trace_ts(trc, td_get_cycles(), _t_, _l_, TP_1X64, _u64_)

#define td_tracef_2x32(trc,_t_,_l_,_u32_1_,_u32_2_) \
	td_trace_ts(trc, td_get_cycles(), _t_, _l_, TP_2X32, (((uint64_t)_u32_1_ << 32) + (_u32_2_)))

#define td_tracef_4x16(trc,_t_,_l_,_u16_1_,_u16_2_,_u16_3_,_u16_4_) \
	td_trace_ts(trc, td_get_cycles(), _t_, _l_, TP_4X16,        \
		((uint64_t)_u16_1_ << 48) +                        \
		((uint64_t)_u16_2_ << 32) +                        \
		((uint64_t)_u16_3_ << 16) +                        \
		(_u16_4_))

enum td_errors_type {
	TE_CORRUPT_CMD	=	0xFFUL << 0,    // Corrupted bit in CMD (any)
	TE_CORRUPT_DUP	=	1UL << 0,       // Corrupted bit in CMD dup fields
	TE_CORRUPT_IDX	=	1UL << 1,       // Corrupted bit in CMD index field (wrong address)
	TE_CORRUPT_ODD	=	1UL << 2,       // Corrupted bit in CMD even/odd
	TE_CORRUPT_XSUM	=	1UL << 3,       // Corrupted bit in CMD XSUM field
	TE_CORRUPT_FID	=	1UL << 4,       // Corrupted bit in CMD ID field
	TE_CORRUPT_HID	=	1UL << 5,       // Corrupted bit in CMD ID field

	TE_CORRUPT_WEP	=	1UL << 6,       // Corrupted bit in WEP data write

	TE_SKIP_CMD     =       0xFUL << 8,     // Skip command (any)
	TE_SKIP_RD_CMD  =       1UL << 8,       // Skip read command
	TE_SKIP_WR_CMD  =       1UL << 9,       // Skip write command
	TE_SKIP_CT_CMD  =       1UL << 10,      // Skip CTL command
	TE_SKIP_HW_CMD  =       1UL << 11,      // Skip HW command

	TE_SKIP_CMD_ROW =       1UL << 12,      // Missing data for from_host CMD
	TE_SKIP_CMD_CLF =       1UL << 13,      // Missing clflush for CMD
	TE_SKIP_WEP_ROW =       1UL << 14,      // Missing data for from_host WEP
	TE_SKIP_WEP_CLF =       1UL << 15,      // Missing clflush for WEP
	
	TE_CMD_REORDER  =       1UL << 16,
	TE_CMD_OFFSET   =       1UL << 17,
	TE_SKIP_FWSTATUS=       1UL << 18,

	TE_LBA          =       1UL << 24,
	TE_STARVE_CPU   =       1UL << 25,
	TE_DELAY_SEMA   =       1UL << 26,

	TE_EXPERIMENT   =       1UL << 32,

	TE_MAX          =       1UL << 40,
};

#ifdef CONFIG_TERADIMM_ERROR_INJECTION
#define TE_INJECT(_eng, _what)  (unlikely(_eng->td_errors_count) && (_eng->td_errors_mask & (_what)))
#else
#define TE_INJECT(_eng, _what) (0)
#endif

#define TE_IS_SET(_eng, _what) (_eng->td_errors_mask & (_what))

#endif
