/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2014 Diablo Technologies Inc. (Diablo).              *
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

#ifndef _TD_MAPPER_H_
#define _TD_MAPPER_H_

#include "td_kdefn.h"

#include "td_compat.h"
#include "td_limits.h"
#include "td_memspace.h"
#include "td_protocol.h"
#include "td_util.h"

#ifdef CONFIG_TERADIMM_FORCE_CMD_UC_MAP
extern int td_fw_cmd_uc;
extern int td_hw_cmd_uc;
#endif

struct td_mapper {
	uint64_t        phys_base;
	uint64_t        phys_size;

	struct {
#define TERADIMM_CACHED_ALIASES_READ_DATA 2
#define TERADIMM_CACHED_ALIASES_READ_META_DATA 2
		void *map_READ_DATA[TERADIMM_READ_DATA_MAX][TERADIMM_CACHED_ALIASES_READ_DATA];
		void *map_READ_META_DATA[TERADIMM_READ_META_DATA_MAX][TERADIMM_CACHED_ALIASES_READ_META_DATA];

#define TERADIMM_CACHED_ALIASES_WRITE_DATA 2
#define TERADIMM_CACHED_ALIASES_WRITE_META_DATA 2
		void *map_WRITE_DATA[TERADIMM_WRITE_DATA_MAX][TERADIMM_CACHED_ALIASES_WRITE_DATA];
		void *map_WRITE_META_DATA[TERADIMM_WRITE_META_DATA_MAX][TERADIMM_CACHED_ALIASES_WRITE_META_DATA];

#define TERADIMM_CACHED_ALIASES_COMMAND 1
		void *map_COMMAND[TERADIMM_COMMAND_MAX][TERADIMM_CACHED_ALIASES_COMMAND];

#define TERADIMM_CACHED_ALIASES_STATUS 1
		void *map_STATUS[TERADIMM_STATUS_MAX][TERADIMM_CACHED_ALIASES_STATUS];

#define TERADIMM_CACHED_ALIASES_EXT_STATUS 1
		void *map_EXT_STATUS[TERADIMM_EXT_STATUS_MAX][TERADIMM_CACHED_ALIASES_EXT_STATUS];

	} td_virt_cache;
	
};

extern int td_mapper_check_memory_mapping(const char *dev_name, uint64_t base, uint64_t size);

extern int td_mapper_init(struct td_mapper *m, const char *dev_name, uint64_t base, uint64_t size);
extern void td_mapper_cleanup(struct td_mapper *m);

extern int td_mapper_verify_ready(struct td_mapper *m);

static inline uint64_t td_mapper_max_offset(struct td_mapper *m)
{
	return min_t(uint64_t, m->phys_size, TD_IOREMAP_SIZE);
}

/* these are defined per platform in td_mapper_{platform}.c */
extern int    td_force_memory_mapping(struct td_mapper *m);
extern void * td_mapper_map_wb(struct td_mapper *m, uint64_t off, uint64_t size);
extern void * td_mapper_map_wc(struct td_mapper *m, uint64_t off, uint64_t size);
extern void * td_mapper_map_uc(struct td_mapper *m, uint64_t off, uint64_t size);
extern void   td_mapper_unmap(struct td_mapper *m, void *virt);

#define TD_MAP_TYPE_WB 0
#define TD_MAP_TYPE_WC 1
#define TD_MAP_TYPE_UC 2

static inline void * td_mapper_map(struct td_mapper *m, int type, uint64_t off, uint64_t size)
{
	switch (type) {
	default:
	case TD_MAP_TYPE_WB: return td_mapper_map_wb(m, off, size);
	case TD_MAP_TYPE_WC: return td_mapper_map_wc(m, off, size);
	case TD_MAP_TYPE_UC: return td_mapper_map_uc(m, off, size);
	}
}

#define TD_MAPPER_TYPE_TEST(_buf_type,_map_type) \
	( CONFIG_TERADIMM_MAP_##_buf_type == TD_MAP_TYPE_##_map_type )

/* ------------------------------------------------------------------------ */

#define CACHED_MAP_PTR(_m,_type,_alias,_id) \
	(_m)->td_virt_cache.map_##_type[(_id)][(_alias)]

#define CACHED_MAP_ALIAS_OK(_type,_alias,_id) \
	((_alias) < TERADIMM_CACHED_ALIASES_##_type \
	 && (_id) < TERADIMM_##_type##_MAX)

#define RETURN_CACHED_MAP_PTR(_m,_type,_alias,_id) \
	if (likely(CACHED_MAP_ALIAS_OK(_type,_alias,_id) && CACHED_MAP_PTR(_m,_type,_alias,_id))) { \
		return CACHED_MAP_PTR(_m,_type,_alias,_id); \
	} else { \
		pr_err("cached pointer for %s(%u,%u) is NULL\n", \
				__stringify(_type), _alias, _id); \
		return NULL; \
	}


static inline void * teradimm_map_status(struct td_mapper *m,
		int alias)
{
	RETURN_CACHED_MAP_PTR(m, STATUS, alias, 0);
}

static inline void * teradimm_map_ext_status(struct td_mapper *m,
		int alias)
{
	RETURN_CACHED_MAP_PTR(m, EXT_STATUS, alias, 0);
}

static inline void * teradimm_map_cmd_buf(struct td_mapper *m,
		int alias, int id)
{
	RETURN_CACHED_MAP_PTR(m, COMMAND, alias, id);
}

static inline void * teradimm_map_read_data_buf(struct td_mapper *m,
		int alias, int id)
{
	RETURN_CACHED_MAP_PTR(m, READ_DATA, alias, id);
}

static inline void * teradimm_map_read_meta_buf(struct td_mapper *m,
		int alias, int id)
{
	RETURN_CACHED_MAP_PTR(m, READ_META_DATA, alias, id);
}

static inline void * teradimm_map_write_data_buf(struct td_mapper *m,
		int alias, int id)
{
	RETURN_CACHED_MAP_PTR(m, WRITE_DATA, alias, id);
}

static inline void * teradimm_map_write_meta_buf(struct td_mapper *m,
		int alias, int id)
{
	RETURN_CACHED_MAP_PTR(m, WRITE_META_DATA, alias, id);
}

#endif
