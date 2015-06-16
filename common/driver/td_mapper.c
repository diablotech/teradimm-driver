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

#include "td_kdefn.h"
#include "td_compat.h"

#include "td_eng_teradimm.h"
#include "td_histogram.h"
#include "td_util.h"
#include "td_engine.h"
#include "td_eng_completion.h"
#include "td_checksum.h"
#include "td_command.h"
#include "td_crypto.h"
#include "td_protocol.h"
#include "td_device.h"
#include "td_devgroup.h"
#include "td_compat.h"
#include "td_memcpy.h"
#include "td_cache.h"
#include "td_ucmd.h"
#include "td_memspace.h"
#include "td_params.h"
#include "td_eng_mcefree.h"

#ifdef CONFIG_TERADIMM_SGIO
#include "td_ata_cmd.h"
#endif

static uint td_alias_shift = 2;

#ifdef CONFIG_TERADIMM_USER_ALIAS_SHIFT
module_param_named(alias_shift, td_alias_shift, uint, 0444);
#endif

static int td_mapper_build_pointer_cache(struct td_mapper *m);
static void td_mapper_release_pointer_cache(struct td_mapper *m);

int td_mapper_check_memory_mapping(const char *dev_name, uint64_t base, uint64_t size)
{
	/* fail if base is under 4G or not page aligned */
	if (base < (1UL << 32)
			|| (base & ~PAGE_MASK)) {
		pr_err("Device %s has invalid base of 0x%llx\n",
				dev_name, base);
		return -EINVAL;
	}

	/* fail if size is less than 1G or not page aligned */
	if (size < (1UL << 20)
			|| (size & ~PAGE_MASK)) {
		pr_err("Device %s has invalid base of 0x%llx\n",
				dev_name, base);
		return -EINVAL;
	}

	return 0;
}


int td_mapper_init(struct td_mapper *m, const char *dev_name,
		uint64_t base, uint64_t size, uint64_t avoid_mask)
{
	int rc;

	rc = td_mapper_check_memory_mapping(dev_name, base, size);
	if (rc)
		goto error_check;

	m->phys_base = base;
	m->phys_size = size;
	m->phys_avoid_mask = avoid_mask;

	rc = td_force_memory_mapping(m);
	if (rc)
		goto error_force_mapping;

	rc = td_mapper_build_pointer_cache(m);
	if (rc)
		goto error_cache;

	return 0;

error_cache:
	td_mapper_release_pointer_cache(m);
error_force_mapping:
error_check:
	return rc;
}


void td_mapper_cleanup(struct td_mapper *m)
{
	td_mapper_release_pointer_cache(m);
}

/* ------------------------------------------------------------------------ */
/* utility stuff */

#ifdef CONFIG_TERADIMM_FORCE_CMD_UC_MAP
int td_fw_cmd_uc = 0;
int td_hw_cmd_uc = 0;

module_param_named(fw_cmd_uc, td_fw_cmd_uc, uint, 0444);
module_param_named(hw_cmd_uc, td_hw_cmd_uc, uint, 0444);
MODULE_PARM_DESC(fw_cmd_uc, "Map firmware commands uncached.");
MODULE_PARM_DESC(hw_cmd_uc, "Map hardware commands uncached.");

static inline int td_mapper_cmd_force_uc(struct td_mapper *m, int id)
{
	/* TODO: the 128 should be based on td_tok_type_from_tokid(),
	 * but at this point we don't have an engine to use */

	int half = TD_TOKENS_PER_DEV/2;
	int resv = TD_TOKENS_PER_DEV - CONFIG_TERADIMM_RESERVE_TOKENS;

	/* FW commands are below half */
	if (td_fw_cmd_uc && id < half)
		return 1;

	/* the upper most IDs are reserved for FWSTATUS push */
	(void)resv;
#if 0
	if (id >= resv)
		return 0;
#endif

	/* HW deallocates */
	if (td_hw_cmd_uc && id >= half)
		return 1;

	return 0;
}
#endif


/* ------------------------------------------------------------------------ */
/* build up all the virtual pointers for all accessible buffers */

#define VALIDATE_TYPE_ALIAS_ID(_m,_type,_alias,_id) \
	if (!CACHED_MAP_ALIAS_OK(_type, alias, id)) { \
		pr_err("invalid mapping attempted %s(%u,%u)\n", \
				__stringify(_type), _alias, _id); \
		return -EINVAL; \
	} else if (CACHED_MAP_PTR(_m,_type,_alias,_id)) { \
		pr_err("%s(%u,%u) is already set\n", \
				__stringify(_type), _alias, _id); \
		return 0; \
	}

#define VALIDATE_OFFSET(_m,_type,_alias,_id,_off) \
	if ((_off) >= td_mapper_max_offset(_m)) { \
		pr_err("%s(%u,%u) generated offset %llu, which is out or range\n", \
				__stringify(_type), _alias, _id, (long long unsigned)_off); \
		return -EINVAL; \
	}

#define REMEMBER_CACHED_MAP_PTR(_m,_type,_alias,_id,_ofs,_size) do {\
	void *_ptr; \
	WARN_ON(! CACHED_MAP_ALIAS_OK(_type,_alias,_id)); \
	_ptr = td_mapper_map(_m, CONFIG_TERADIMM_MAP_##_type, _ofs, _size); \
	if (!_ptr) { \
		pr_err("%s(%u,%u) failed mapping 0x%llx size %llu\n", \
				__stringify(_type), _alias, _id, \
				(long long unsigned)_ofs, \
				(long long unsigned)_size); \
		return -ENOMEM; \
	} \
	CACHED_MAP_PTR(_m,_type,_alias,_id) = _ptr; \
} while(0)


static inline int teradimm_cache_STATUS(struct td_mapper *m,
		int alias, int id)
{
	off_t off;

	VALIDATE_TYPE_ALIAS_ID(m, STATUS, alias, id);

	off = TERADIMM_OFFSET(STATUS, alias, id, m->phys_avoid_mask);

#ifdef CONFIG_TERADIMM_STATUS_FROM_LAST_CACHELINE
	/* status buffers are replicated every 512B */
	off += 4096 - 512;
#endif

	VALIDATE_OFFSET(m, STATUS, alias, id, off);

	REMEMBER_CACHED_MAP_PTR(m, STATUS, alias, id, off, PAGE_SIZE);

	return 0;
}

static inline int teradimm_cache_EXT_STATUS(struct td_mapper *m,
		int alias, int id)
{
	off_t off;

	VALIDATE_TYPE_ALIAS_ID(m, EXT_STATUS, alias, id);

	off = TERADIMM_OFFSET(EXT_STATUS, alias, id, m->phys_avoid_mask);

	VALIDATE_OFFSET(m, EXT_STATUS, alias, id, off);

	REMEMBER_CACHED_MAP_PTR(m, EXT_STATUS, alias, id, off, PAGE_SIZE);

	return 0;
}

static inline int teradimm_cache_COMMAND(struct td_mapper *m,
		int alias, int id)
{
	off_t off;

	VALIDATE_TYPE_ALIAS_ID(m, COMMAND, alias, id);

	off = TERADIMM_OFFSET(COMMAND,alias,id, m->phys_avoid_mask);

	VALIDATE_OFFSET(m, COMMAND, alias, id, off);

	REMEMBER_CACHED_MAP_PTR(m, COMMAND, alias, id, off, PAGE_SIZE);

#ifdef CONFIG_TERADIMM_FORCE_CMD_UC_MAP
	if (td_mapper_cmd_force_uc(m, id)) {
		void *ptr = CACHED_MAP_PTR(m,COMMAND,alias,id);
		unsigned long addr = ((unsigned long)ptr) & PAGE_MASK;
		int rc = set_memory_uc(addr, 1);
		if ( WARN_ON_ONCE(rc) ) {
			pr_err("UC REMAP FAILED %p -> %016lx, rc=%d\n",
				ptr, addr, rc);

		}
	}
#endif

	return 0;
}

static inline int teradimm_cache_READ_DATA(struct td_mapper *m,
		int alias, int id)
{
	off_t off;
	int real_alias = 0;
	if (alias)
		real_alias = 1 << td_alias_shift;

	VALIDATE_TYPE_ALIAS_ID(m, READ_DATA, alias, id);

	off = TERADIMM_OFFSET(READ_DATA,real_alias,id,m->phys_avoid_mask);

	VALIDATE_OFFSET(m, READ_DATA, alias, id, off);

	REMEMBER_CACHED_MAP_PTR(m, READ_DATA, alias, id, off, 2*PAGE_SIZE);

	return 0;
}

static inline int teradimm_cache_READ_META_DATA(struct td_mapper *m,
		int alias, int id)
{
	off_t off;
	int real_alias = 0;
	if (alias)
		real_alias = 1 << td_alias_shift;

	VALIDATE_TYPE_ALIAS_ID(m, READ_META_DATA, alias, id);

	off = TERADIMM_OFFSET(READ_META_DATA,real_alias,id,m->phys_avoid_mask);

#ifdef CONFIG_TERADIMM_RDMETA_FROM_LAST_CACHELINE
	/* read metadata buffers are replicated every 128B */
	off += 4096 - 128;
#endif

	VALIDATE_OFFSET(m, READ_META_DATA, alias, id, off);

	REMEMBER_CACHED_MAP_PTR(m, READ_META_DATA, alias, id, off, PAGE_SIZE);

	return 0;
}

static inline int teradimm_cache_WRITE_DATA(struct td_mapper *m,
		int alias, int id)
{
	off_t off;

	VALIDATE_TYPE_ALIAS_ID(m, WRITE_DATA, alias, id);

	off = TERADIMM_OFFSET(WRITE_DATA,alias,id,m->phys_avoid_mask);

	VALIDATE_OFFSET(m, WRITE_DATA, alias, id, off);

	REMEMBER_CACHED_MAP_PTR(m, WRITE_DATA, alias, id, off, PAGE_SIZE);

	return 0;
}

static inline int teradimm_cache_WRITE_META_DATA(struct td_mapper *m,
		int alias, int id)
{
	off_t off;

	VALIDATE_TYPE_ALIAS_ID(m, WRITE_META_DATA, alias, id);

	off = TERADIMM_OFFSET(WRITE_META_DATA,alias,id,m->phys_avoid_mask);

	VALIDATE_OFFSET(m, WRITE_META_DATA, alias, id, off);

	REMEMBER_CACHED_MAP_PTR(m, WRITE_META_DATA, alias, id, off, PAGE_SIZE);

	return 0;
}

#define BUILD_ONE_TYPE(_m,_type) do {                                     \
	int id, id_max = TERADIMM_##_type##_MAX;                          \
	int alias, alias_max = TERADIMM_CACHED_ALIASES_##_type;           \
	for (id=0; id<id_max; id++) {                                     \
		for (alias=0; alias<alias_max; alias++) {                 \
			teradimm_cache_##_type(_m,alias,id);              \
		}                                                         \
	}                                                                 \
} while(0)

static int td_mapper_build_pointer_cache(struct td_mapper *m)
{
	memset(&m->td_virt_cache, 0, sizeof(m->td_virt_cache));

	BUILD_ONE_TYPE(m, STATUS);
	BUILD_ONE_TYPE(m, EXT_STATUS);
	BUILD_ONE_TYPE(m, COMMAND);
	BUILD_ONE_TYPE(m, READ_DATA);
	BUILD_ONE_TYPE(m, READ_META_DATA);
	BUILD_ONE_TYPE(m, WRITE_DATA);
	BUILD_ONE_TYPE(m, WRITE_META_DATA);

	return 0;
}

#define RELEASE_ONE_TYPE(_m,_type) do {                                   \
	int id, id_max = TERADIMM_##_type##_MAX;                          \
	int alias, alias_max = TERADIMM_CACHED_ALIASES_##_type;           \
	for (id=0; id<id_max; id++) {                                     \
		for (alias=0; alias<alias_max; alias++) {                 \
			void *_ptr = CACHED_MAP_PTR(_m,_type,alias,id);   \
			if (_ptr)                                         \
				td_mapper_unmap(m,_ptr);                  \
			CACHED_MAP_PTR(_m,_type,alias,id) = NULL;         \
		}                                                         \
	}                                                                 \
} while(0)

static void td_mapper_release_pointer_cache(struct td_mapper *m)
{
	RELEASE_ONE_TYPE(m, STATUS);
	RELEASE_ONE_TYPE(m, EXT_STATUS);
	RELEASE_ONE_TYPE(m, COMMAND);
	RELEASE_ONE_TYPE(m, READ_DATA);
	RELEASE_ONE_TYPE(m, READ_META_DATA);
	RELEASE_ONE_TYPE(m, WRITE_DATA);
	RELEASE_ONE_TYPE(m, WRITE_META_DATA);
}

#define SUM_NULL_FOR_ONE_TYPE(_m,_sum,_type) do {                         \
	int id, id_max = TERADIMM_##_type##_MAX;                          \
	int alias, alias_max = TERADIMM_CACHED_ALIASES_##_type;           \
	for (id=0; id<id_max; id++) {                                     \
		for (alias=0; alias<alias_max; alias++) {                 \
			void *_ptr = CACHED_MAP_PTR(_m,_type,alias,id);   \
			if (!_ptr)                                        \
			(_sum)++;                                         \
		}                                                         \
	}                                                                 \
} while(0)


int td_mapper_verify_ready(struct td_mapper *m)
{
	unsigned sum = 0;
	SUM_NULL_FOR_ONE_TYPE(m, sum, STATUS);
	SUM_NULL_FOR_ONE_TYPE(m, sum, EXT_STATUS);
	SUM_NULL_FOR_ONE_TYPE(m, sum, COMMAND);
	SUM_NULL_FOR_ONE_TYPE(m, sum, READ_DATA);
	SUM_NULL_FOR_ONE_TYPE(m, sum, READ_META_DATA);
	SUM_NULL_FOR_ONE_TYPE(m, sum, WRITE_DATA);
	SUM_NULL_FOR_ONE_TYPE(m, sum, WRITE_META_DATA);

	return sum == 0 ? 0 : -EFAULT;
}

