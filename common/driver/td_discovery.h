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

#include "td_kdefn.h"

#include "td_compat.h"


#define TD_LOCATOR_SIZE                 64

struct td_spd {
	uint8_t idx;
	uint8_t val;
};
		
struct td_discovered_info
{
	/* Users must call this when done with this structure */
	void (*done) (struct td_discovered_info*);

	const char      *source;

	/* Memory information */
	uint64_t        mem_base;
	uint64_t        mem_size;
	unsigned int    mem_speed;
	
	/* Description of memory controller/package */
	uint8_t         socket;
	uint8_t         agent;
	uint8_t         channel;
	uint8_t         slot;
	
	/* Do we have an interrupt */
	int             irq;

	/* Any correlation with a  _SM_ DMI entry? */
	char            *bank_locator;
	
	/* And SPD specific dat */
	uint8_t         spd_type;       // SPD 150
	uint8_t         spd_asic;       // SPD 155

	unsigned        spd_count;
	struct td_spd   spd[];
};

#define TD_DISCOVERED_INFO_SIZE(spd_count)              \
	(   sizeof(struct td_discovered_info) +         \
	    sizeof(struct td_spd)*spd_count)

	

int td_device_create_discovered (struct td_discovered_info* dev_info, void *opaque);


typedef int (*td_scan_callback_t) (struct td_discovered_info* dev_info, void* opaque);


#ifdef CONFIG_TERADIMM_ACPI_ASL

struct td_dev_callback_opts {
	uint td_scan_enable;
	uint td_magic_speed;
	uint td_magic_type;
};

/* _UID from ACPI Tables. */
union td_uid {
	struct {
		uint8_t slot_id:2;      /* [0:1] */
		uint8_t channel:2;      /* [2:3] */
		uint8_t home_agent:2;   /* [4:5] */
		uint8_t cpu_socket:2;   /* [6:7] */
		};
	uint8_t unique_id;
} __attribute__((__packed__));
typedef union td_uid td_uid_t;


/* Structure used to gather information for driver init. */
struct td_device_info {
	/* List stuff */
	struct list_head link;
	const char* source;
	char device_type[3];
	char dev_name[6];
	uint64_t offset;
	uint64_t size;
	uint32_t irq;
	td_uid_t uid;
	int use_cpu;
	uint16_t memspeed;
	uint8_t checksum;
	uint8_t valid;
	char bank_locator[TD_LOCATOR_SIZE];
};


typedef int (*td_create_callback) (struct td_device_info *info, void *opaque);

/* in linux/driver/td_scan_asl.c */
extern void td_asl_scan(const struct td_dev_callback_opts* opts);
extern void td_asl_print_table(void);
extern void td_asl_enumerate(td_create_callback callback_func, void *opaque);
extern void td_asl_cleanup(void);

#endif

extern int td_bios_scan (void);
extern void td_bios_enumerate(td_scan_callback_t func, void* opaque);
extern void td_bios_cleanup(void);

#ifdef CONFIG_TERADIMM_BIOS_FLSHDIMM
extern int td_scan_bios_flshdimm (void);
#endif
