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

#ifndef _TD_COMPAT_H_
#define _TD_COMPAT_H_

#if    defined (VMWARE)
#include "td_compat_vmk.h"
#else
#include "td_compat_linux.h"
#endif

#if !defined(BITS_PER_LONG)
#  if defined(__x86_64) || defined(__x86_64__) || (__WORDSIZE == 64)
#    define BITS_PER_LONG 64
#  elif defined(__i386) || defined(__i386__) || (__WORDSIZE == 32)
#    define BITS_PER_LONG 32
#  endif
#endif

#ifdef __GNUC__
#define MACRO_BLOCK_OPEN ({
#define MACRO_BLOCK_CLOSE })
#else
#define MACRO_BLOCK_OPEN do {
#define MACRO_BLOCK_CLOSE } while(0)
#endif

#ifdef __GNUC__
/** this macro defines a block of structure members that does not share
 * a cacheline with any other members that precede it or follow it */
#define TD_DECLARE_IN_PRIVATE_CACHE_LINE(name,stuff) \
	struct { \
		unsigned __priv_cacheline_start_##name[0] __aligned64; \
		stuff \
		unsigned __priv_cacheline_end_##name[0] __aligned64; \
	}
#else
/* fix this for your compiler */
#define TD_DECLARE_IN_PRIVATE_CACHE_LINE(name,stuff) \
	struct { \
		unsigned __priv_cacheline_start_##name[0] __aligned64; \
		stuff \
		unsigned __priv_cacheline_end_##name[0] __aligned64; \
	}
#endif


#ifndef min_t
#define min_t(_t,_x,_y) MACRO_BLOCK_OPEN \
	_t __x = (_x), __y = (_y); \
	__x < __y ? __x : __y; \
MACRO_BLOCK_CLOSE
#endif

#ifndef max_t
#define max_t(_t,_x,_y) MACRO_BLOCK_OPEN \
	_t __x = (_x), __y = (_y); \
	__x > __y ? __x : __y; \
MACRO_BLOCK_CLOSE
#endif

#if !defined(pr_warn) && defined(pr_warning)
#define pr_warn pr_warning
#endif

#if defined(__GNUC__) && !defined(__KERNEL__) && !defined(HZ)
// using gcc, not in kernel, and no HZ defined...
// guess it's 1000
#define HZ 1000
#endif

#ifndef simple_strtoul
// simple_strtoul() is like strtoul()
#define simple_strtoul(str,end,base) strtoul(str,end,base)
#endif

/* Used for VOID* arithmetic, which MSC_VER doesn't do */
#define PTR_OFS(_ptr, _off)				\
	((void*)(((char*)(_ptr)) + (_off)))

#define NUM_ELEMENTS(_array) (sizeof(_array)/sizeof(_array[0]))

/* Some SMART exports that aren't in older kernels */
#ifdef __KERNEL__
#ifndef ATA_CMD_SMART
#define ATA_CMD_SMART 0xB0
#endif
#ifndef ATA_SMART_ENABLE
#define ATA_SMART_ENABLE 0xD8
#endif
#ifndef ATA_SMART_READ_VALUES
#define ATA_SMART_READ_VALUES 0xD0
#endif
#ifndef ATA_SMART_READ_THRESHOLDS
#define ATA_SMART_READ_THRESHOLDS 0xD1
#endif
#ifndef ATA_CMD_SEC_ERASE_PREP
#define ATA_CMD_SEC_ERASE_PREP 0xF3
#endif
#ifndef ATA_CMD_SEC_ERASE_UNIT
#define ATA_CMD_SEC_ERASE_UNIT 0xF4
#endif
#endif


#ifdef __KERNEL__
extern void *td_compat_ioremap(unsigned long base, unsigned long size);
extern void td_compat_iounmap(void *vptr);
#endif

#ifdef __GNUC__

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#ifndef __may_alias
#define __may_alias __attribute__((may_alias))
#endif

#ifndef __aligned8
#define __aligned8 __attribute__((aligned(8)))
#endif

#ifndef __packed_aligned8
#define __packed_aligned8 __attribute__((packed,aligned(8)))
#endif

#ifndef __aligned16
#define __aligned16 __attribute__((aligned(16)))
#endif

#ifndef __packed_aligned16
#define __packed_aligned16 __attribute__((packed,aligned(16)))
#endif

#ifndef __aligned64
#define __aligned64 __attribute__((aligned(64)))
#endif

#ifndef likely                                                                 
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely                                                               
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#endif /* __GNUC__ */

#ifndef __unused
#  define __unused(x) UNUSED_ ## x __attribute__((__unused__))
#endif

#ifndef __SIZEOF_LONG__
#define __SIZEOF_LONG__	4
#endif

#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT		9
#endif

#ifndef SECTOR_SIZE
#define SECTOR_SIZE             (1<<SECTOR_SHIFT)
#endif


#if defined(__KERNEL__) && defined(CLFLUSH_SIZE)

#ifndef KABI__clflush
static inline void clflush(volatile void *__p)
{
	asm volatile("clflush %0" : "+m" (*(volatile char __force *)__p));
}
#endif /* KABI__clflush */

#ifndef KABI__clflush_cache_range
/**
 * clflush_cache_range - flush a cache range with clflush
 * @vaddr:      virtual start address
 * @size:       number of bytes to flush
 *
 * clflush is an unordered instruction which needs fencing with mfence
 * to avoid ordering issues.
 *
 * We actually try and avoid the name clfush_cache_range in this case
 * becuase the kernel headers define it, but not usably.
 */
#ifdef clflush_cache_range
#undef clflush_cache_range
#endif
#define clflush_cache_range(_v,_s) _clflush_cache_range(_v,_s)
static inline void _clflush_cache_range(const void *vstart, unsigned int size)
{
	void *vaddr = (void*)vstart;
	void *vend = PTR_OFS(vaddr, size - 1);

	mb();

	for (; vaddr < vend; vaddr += CLFLUSH_SIZE)
		clflush(vaddr);
	/*
	 * Flush any possible final partial cacheline:
	 */
	clflush(vend);

	mb();
}
#endif /* KABI__clflush_cache_range */

#ifndef pr_err
#define pr_err(fmt, ...) \
        printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#endif
#endif

#ifndef pr_warn
#define pr_warning(fmt, ...) \
        printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn pr_warning
#endif


#ifndef list_first_entry
/**
 * list_first_entry - get the first element from a list
 * @ptr:        the list head to take the element from.
 * @type:       the type of the struct this is embedded in.
 * @member:     the name of the list_struct within the struct.
 *
 * Note, that list is expected to be not empty.
 */
#define list_first_entry(ptr, type, member) \
        list_entry((ptr)->next, type, member)
#endif

#ifdef __KERNEL__
uint64_t td_compat_check_sector_size(uint64_t bio_sector_size);
#endif


#ifndef KABI__time_after64
#define time_after64(a,b)	((__s64)(b) - (__s64)(a) < 0)
#endif

#ifdef __KERNEL__
#ifndef PMSG_HIBERNATE
#define PM_EVENT_HIBERNATE PM_SUSPEND_DISK
#define PMSG_HIBERNATE  ((struct pm_message){ .event = PM_EVENT_HIBERNATE, })
#endif
#endif

#ifndef BIT
#define BIT(_b) (1<<_b)
#endif

#ifndef KABI__kzalloc_node
#define kzalloc_node(_size, _flags, _node)  kzalloc(_size, _flags)
#endif

#ifndef KABI__vmalloc_node
#define vmalloc_node(_size, _node)  vmalloc(_size)
#endif

#ifdef KABI__atomic

/* Our pointer API is based on atomic64, which is uint64_t based */
#define td_atomic_ptr_t atomic64_t
#define td_atomic_ptr_read(_ptr) ((void*)atomic64_read(_ptr))
#define td_atomic_ptr_set(_ptr, _data)  atomic64_set(_ptr, (uint64_t)_data)
#define td_atomic_ptr_xchg(_ptr, _data)  atomic64_xchg(_ptr, (uint64_t)_data)

#else
/* Let's roll our own, thankfully we only care about x86_64 */

typedef struct {
	volatile void* data;
} td_atomic_ptr_t;

static inline void td_atomic_ptr_set(td_atomic_ptr_t *ptr, void* d)
{
	ptr->data = d;
}

static inline void* td_atomic_ptr_read (td_atomic_ptr_t *ptr)
{
	return (void*)ptr->data;
}

static inline void* td_atomic_ptr_xchg (td_atomic_ptr_t *ptr, void* d)
{
	asm volatile("xchgq %0,%1"
		: "=r" (d)
		: "m" (*((volatile long *)(ptr))), "0" (d)
		: "memory");
	return d;
}
#endif

/*
 * __FILENAME__ is a local macro, the "name" part of __FILE__
 * Kbiuld (and vmware) use long absolute paths, so this helps reduce
 * the lenght of the filename is used in logs, etc
 * 
 * If we don't hav a nice way, just take __FILE__
 */
#ifdef KBUILD_BASENAME
#define __FILENAME__    KBUILD_BASENAME  ".c"
#else
#define __FILENAME__    __FILE__
#endif

/* Taken from linux/types.h 3.15-rc1 */
#ifndef KABI__uintptr
typedef unsigned long             uintptr_t;
#endif

#ifdef KABI__bio_bi_size
/* Linux git commit 4f024f37 changed the bio struct*/
#define bio_size        bi_size
#define bio_sector      bi_sector
#define bio_idx         bi_idx
#define td_bvec_iter    int
#define td_bv_idx(x)    x

/* Create our own for_each to use bvec and not *bvec. */
#define td_bio_for_each_segment(bvec, bio, iter)                       \
        for (iter = (bio)->bi_idx;                                     \
             bvec = (bio)->bi_io_vec[iter], iter < (bio)->bi_vcnt;     \
             iter++)

#else

#define bio_size        bi_iter.bi_size
#define bio_sector      bi_iter.bi_sector
#define bio_idx         bi_iter.bi_idx
#define td_bvec_iter    struct bvec_iter
#define td_bv_idx(x)    x.bi_idx

#define td_bio_for_each_segment(bvec, bio, iter) \
	bio_for_each_segment(bvec, bio, iter)
#endif

#ifndef bio_iovec_idx
/* Removed in 3.13 */
#define bio_iovec_idx(bio, idx) (&((bio)->bi_io_vec[(idx)]))
#endif

#endif
