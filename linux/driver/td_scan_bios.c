/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                       *
 *    Copyright (c) 2013 Diablo Technologies Inc. ("Diablo").  All       *
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
 *    Modified BSD License                                               *
 *                                                                       *
 *    If you have explicit permission from Diablo, then (1) use in       *
 *    source and binary forms, with or without modification; as well as  *
 *    (2) redistribution ONLY in binary form, with or without            *
 *    modification; are permitted provided that the following conditions *
 *    are met:                                                           *
 *                                                                       *
 *    * Redistributions in binary form must reproduce the above          *
 *    copyright notice, this list of conditions and the following        *
 *    disclaimer in the documentation and/or other materials provided    *
 *    with the distribution.                                             *
 *                                                                       *
 *    * Neither the name of the DIABLO nor the names of its contributors *
 *    may be used to endorse or promote products derived from this       *
 *    software without specific prior written permission.                *
 *                                                                       *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND             *
 *    CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,        *
 *    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF           *
 *    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE           *
 *    DISCLAIMED. IN NO EVENT SHALL DIABLO BE LIABLE FOR ANY DIRECT,     *
 *    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES *
 *    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR *
 *    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) *
 *    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN          *
 *    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR       *
 *    OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,     *
 *    EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                 *
 *                                                                       *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "td_kdefn.h"

#include "td_discovery.h"
#include "td_util.h"


static int td_scan_debug = 0;
module_param_named(scan_debug, td_scan_debug, uint, 0444);

static int td_scan_x = 1;
static int td_scan_y = 1;

module_param_named(scan_x, td_scan_x, uint, 0444);
module_param_named(scan_y, td_scan_y, uint, 0444);

#ifndef KABI__acpi_tables

#define ACPI_NAME_SIZE		  4
#define ACPI_OEM_ID_SIZE	  6
#define ACPI_OEM_TABLE_ID_SIZE	  8


struct acpi20_table_rsdp {
	char signature[8];      /* ACPI signature, contains "RSD PTR " */
	u8 checksum;	    /* ACPI 1.0 checksum */
	char oem_id[6];			/* OEM identification */
	u8 revision;	    /* Must be (0) for ACPI 1.0 or (2) for ACPI 2.0+ */
	u32 rsdt_address;      /* 32-bit physical address of the RSDT */
	u32 length;	     /* Table length in bytes, including header (ACPI 2.0+) */
	u64 xsdt_address;      /* 64-bit physical address of the XSDT (ACPI 2.0+) */
	u8 extended_checksum;   /* Checksum of entire table (ACPI 2.0+) */
	u8 reserved[3];	 /* Reserved, must be zero */
};

struct __packed acpi_table_header {
	  char signature[ACPI_NAME_SIZE]; /* ASCII table signature */
	  u32 length;	     /* Length of table in bytes, including this header */
	  u8 revision;	    /* ACPI Specification minor version # */
	  u8 checksum;	    /* To make sum of entire table == 0 */
	  char oem_id[ACPI_OEM_ID_SIZE];  /* ASCII OEM identification */
	  char oem_table_id[ACPI_OEM_TABLE_ID_SIZE];      /* ASCII OEM table identification */
	  u32 oem_revision;       /* OEM revision number */
	  char asl_compiler_id[ACPI_NAME_SIZE];   /* ASCII ASL compiler vendor ID */
	  u32 asl_compiler_revision;      /* ASL compiler version */
};

/*******************************************************************************
 *
 * RSDT/XSDT - Root System Description Tables
 *	     Version 1 (both)
 *
 ******************************************************************************/

struct __packed acpi_table_rsdt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 entry[];      /* Array of pointers to ACPI tables */
};

struct __packed acpi_table_xsdt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u64 entry[];      /* Array of pointers to ACPI tables */
};


#endif

static void td_bios_discovered_done(struct td_discovered_info *dev)
{
	pr_info("Completed %s discovery for %s\n", dev->source,
			dev->bank_locator);
	kfree(dev);
}

static char *td_bios_nospace(char *t, char *s, int m)
{
	int l = strnlen(s, m);
	strncpy(t, s, m);
	t[l] = 0;
	while (l > 0 && (t[l - 1] == ' ' || t[l - 1] == '\t')) t[--l] = 0;
	while (t[0] == ' ' || t[0] == '\t') t++;
	return t;
}

static void dump_memory(const char* prefix, void* ptr, int len)
{
	char buffer[256], *buf_ptr; 
	int i;

	buf_ptr = buffer;
	for (i = 0; i < len; i++)
	{
	  //printk(KERN_INFO " [%02d] 0x%02X", i, ((char*)rsdp)[i]);
	  *buf_ptr++ = ' ';
	  *buf_ptr++ = "0123456789abcdef"[(((char*)ptr)[i]>>4)&0x0F];
	  *buf_ptr++ = "0123456789abcdef"[(((char*)ptr)[i])&0x0F];
	}
	*buf_ptr = '\0';
	printk(KERN_INFO "%s %s\n", prefix, buffer);
}

static void td_bios_print_header(struct acpi_table_header * header)
{
	char oem[7], id[9];

	printk(KERN_INFO "ACPI: %.4s (%4u) rev: %d oem: %s id: %s build: (%08X) %d.%d\n", 
		header->signature, header->length, header->revision, td_bios_nospace(oem, header->oem_id, 6), 
		td_bios_nospace(id, header->oem_table_id, 8),
		header->oem_revision, header->oem_revision >> 16, header->oem_revision & 0xffff);
}

#ifdef CONFIG_TERADIMM_ACPI_IBM_FLSHDIMM
struct __packed flshdimm_resource {
	u8      type;
	u8      length;
	u8      revision;
	u8      reserved;
	union {
	    u32 uid;
	    struct {
	      u32   slot:8;
	      u32   channel:8;
	      u32   agent:8;
	      u32   socket:8;
	    };
	};
	u32      base_low;
	u32      base_high;
	u32      length_low;
	u32      length_high;
	u16      irq;
	u8       reserved_2;
	u8       status;
	char     bank_string[32];
	u32      reserved_3;
};

struct acpi_flshdimm_table {
	struct {
	      struct acpi_table_header acpi;
	      u32		      reserved_to_be_1;
	      u64		      reserved0;
	} header;
	struct flshdimm_resource	device[];
};

static int process_flshdimm (struct acpi_table_header *acpi,
		td_scan_callback_t cb_func, void *data)
{
	struct acpi_flshdimm_table *info = (struct acpi_flshdimm_table*)acpi;
	unsigned int length = acpi->length - sizeof(info->header);
	int i;

	
	for (i = 0; i < (length / sizeof(info->device[0])); i++) {
	    struct flshdimm_resource *ibm = &info->device[i];
	    struct td_discovered_info *dev_info;

	    dev_info = kmalloc(TD_DISCOVERED_INFO_SIZE(0), GFP_KERNEL);
	    dev_info->done = td_bios_discovered_done;

	    dev_info->source = "IBM FLSHDIMM";

	    dev_info->mem_base = ibm->base_high;
	    dev_info->mem_base <<= 32;
	    dev_info->mem_base |= ibm->base_low;

	    dev_info->mem_size = ibm->length_high;
	    dev_info->mem_size <<= 32;
	    dev_info->mem_size |= ibm->length_low;

	    dev_info->socket = ibm->socket;
	    dev_info->agent = ibm->agent;
	    dev_info->channel = ibm->channel;
	    dev_info->slot = ibm->slot;

	    dev_info->bank_locator = ibm->bank_string;

	    // IBM FLSHDIMM is always a TDL, no SPD info, and at least 1333
	    dev_info->spd_type = 1;
	    dev_info->spd_asic = 1;
	    dev_info->spd_count = 0;
	    dev_info->mem_speed = 1333;

	    if (cb_func)
		cb_func(dev_info, data);
	    else {
		pr_info("Discovered Diablo FLSHDIMM %d in slot %s at 0x%llx\n", i,
				dev_info->bank_locator, dev_info->mem_base);
		kfree(dev_info);
	    }
	}

	return 0;
}
#endif

#define MCS_DIMM_ACPI_HEADER_SIZE               48
#define MCS_DIMM_RESOURCE_HEADER_SIZE           48

#define MCS_DIMM_STATUS_DNE                     0x00
#define MCS_DIMM_STATUS_DISABLED                0x01
#define MCS_DIMM_STATUS_OK                      0x80
#define MCS_DIMM_STATUS_NOT_TRAINED             0x82

struct __packed mcs_dimm_resource {
	uint8_t         reserved;   // Should be 0
	uint8_t         length;
	uint8_t         spd_type;
	uint8_t         spd_asic;

	uint8_t         slot;
	uint8_t         channel;
	uint8_t         agent;
	uint8_t         socket;

	uint64_t        mem_base;
	uint64_t        mem_size;

	uint32_t        reserved2;

	uint16_t        mem_speed;
	uint8_t         status;
	uint8_t         reserved3;

	uint8_t         spd_158_173[16];
	char            locator_string[0];
};

static int process_mcs_issue(struct mcs_dimm_resource *mcs)
{
	int ret = 1;

	switch(mcs->status) {
	case MCS_DIMM_STATUS_DNE:
		/* Does not exist.
		 * There's no device, but there's an empty entry. */
		break;
	case MCS_DIMM_STATUS_DISABLED:
		pr_err("MCS DIMM in slot %s is disabled!\n",
				mcs->locator_string);
		break;
	case MCS_DIMM_STATUS_NOT_TRAINED:
		pr_warn("MCS DIMM in slot %s is not trained\n",
				mcs->locator_string);
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

static int process_mcs_dimm (struct acpi_table_header *acpi,
		td_scan_callback_t cb_func, void *data)
{
	int cork = 0;
	int i = 0, c;
	unsigned int length = acpi->length;
	unsigned int offset = MCS_DIMM_ACPI_HEADER_SIZE;
	
	while (offset < length) {
	    struct mcs_dimm_resource *mcs = PTR_OFS(acpi, offset);
	    struct td_discovered_info *dev_info;

	    if (mcs->length < MCS_DIMM_RESOURCE_HEADER_SIZE) {
		    pr_err("MCS DIMM Entry invalid: length == 0 at offset 0x%x\n",
				    offset);
		    pr_err(" - acpi = 0x%p (%lu)\n", acpi, sizeof(*acpi));
		    pr_err(" - mcs = 0x%p (%lu)\n", mcs, sizeof(*mcs));
		    pr_err("     length(0x%p) %u\n", &mcs->length, mcs->length);

		    for (i = 0; i < acpi->length/16; i++)
			dump_memory("ACPI: ", PTR_OFS(acpi, i*16), 16);

		    dump_memory("MCS: ", mcs, MCS_DIMM_RESOURCE_HEADER_SIZE);
		    break;
	    }

	    if (td_scan_debug && ! cb_func) {
		    pr_info("MCS-DIMM Entry at offset %u\n", offset);
		    td_dump_data("  ", mcs, mcs->length);
	    }
	    if (cork++ > 256)
		    break;

	    if(mcs->status != MCS_DIMM_STATUS_OK && process_mcs_issue(mcs))
			    goto skip_dev;

	    dev_info = kmalloc(TD_DISCOVERED_INFO_SIZE(16), GFP_KERNEL);
	    dev_info->done = td_bios_discovered_done;

	    dev_info->source = "MCS DIMM";

	    dev_info->spd_type = mcs->spd_type;
	    dev_info->spd_asic = mcs->spd_asic;

	    dev_info->mem_base = mcs->mem_base;
	    dev_info->mem_size = mcs->mem_size;

	    dev_info->socket = mcs->socket;
	    dev_info->agent = mcs->agent;
	    dev_info->channel = mcs->channel;
	    dev_info->slot = mcs->slot;

	    dev_info->mem_speed = mcs->mem_speed;

	    dev_info->bank_locator = mcs->locator_string;

	    dev_info->spd_count = 16;
	    for (c = 0; c < 16; c++) {
		    dev_info->spd[c].idx = 158+c;
		    dev_info->spd[c].val = mcs->spd_158_173[c];
	    }

	    if (cb_func && (i % td_scan_x) < td_scan_y)
		cb_func(dev_info, data);
	    else {
		pr_info("Discovered Diablo MCS Device %d in slot %s at 0x%llx\n", i,
				dev_info->bank_locator, dev_info->mem_base);
		kfree(dev_info);
	    }

	    i++;

skip_dev:
	    offset += mcs->length;
	}

	return 0;
}

struct td_bios_table {
	char *sig_id;
	char *oem_id;
	char *tbl_id;
	int   sig_length;
	int   oem_length;
	int   tbl_length;
	int (*process) (struct acpi_table_header*, td_scan_callback_t, void* opague);
	void *ptr;
} td_bios_tables_list[] =
{
#ifdef CONFIG_TERADIMM_ACPI_IBM_FLSHDIMM
	{ "OEM", "IBM", "FLSHDIMM", 3, 3, 8, process_flshdimm, NULL },
#endif
	{ "OEM", NULL,  "MCS-DIMM", 3, 0, 8, process_mcs_dimm, NULL },
};


void td_bios_enumerate(td_scan_callback_t func, void* opaque)
{
	int j;

	for (j = 0; j < NUM_ELEMENTS(td_bios_tables_list); j++)
	{
		if (td_bios_tables_list[j].ptr) {
			td_bios_tables_list[j].process(td_bios_tables_list[j].ptr,
					func, opaque);
		}
	}
}

int td_bios_table_process (struct acpi_table_header *tbl)
{
	char sig[5];
	char oem[7];
	char table[9];
	int j;

	if (td_scan_debug > 1)
		td_bios_print_header(tbl);

	td_bios_nospace(sig, tbl->signature, 4);
	td_bios_nospace(oem, tbl->oem_id, 6);
	td_bios_nospace(table, tbl->oem_table_id, 8);

	for (j = 0; j < NUM_ELEMENTS(td_bios_tables_list); j++) {
		if (td_bios_tables_list[j].sig_id &&
				strncmp(sig, td_bios_tables_list[j].sig_id,
						td_bios_tables_list[j].sig_length) )
			continue;

		if (td_bios_tables_list[j].oem_id &&
				strncmp(oem, td_bios_tables_list[j].oem_id,
					td_bios_tables_list[j].oem_length) )
			continue;

		if (strncmp(table, td_bios_tables_list[j].tbl_id,
					td_bios_tables_list[j].tbl_length) )
			continue;

		td_bios_tables_list[j].ptr = tbl;

		if (td_scan_debug)
			pr_info("Discovered %s / %s / %s\n", sig, oem, table);

		td_bios_tables_list[j].process(td_bios_tables_list[j].ptr,
				NULL, NULL);
		return 1;
	}
	return 0;
}

#ifdef KABI__acpi_get_table_by_index
int td_bios_scan ()
{
	struct acpi_table_header *tbl;
	acpi_status status;
	int found = 0;
	int i;

	for (i = 0; 1; i++) {
		status = acpi_get_table_by_index(i, &tbl);

		if (status)
			break;

		found += td_bios_table_process(tbl);
	}

	if (found == 0)
		return -ENODEV;

	return 0;
}
#else

/*
 * Checksum an ACPI table.
 */

static unsigned char td_bios_checksum(void *buffer, int length)
{
	unsigned char sum = 0;
	while (length--)
		sum += *(unsigned char *)buffer++;
	return sum;
}


/*
 * Search a block of memory for the RSDP signature
 */
#define CHAR(_c) ( ( (_c >= 'A' && _c <= 'Z') || \
		   (_c >= 'a' && _c <= 'z') ) ? _c : '?')

#define RSDP_SCAN_STEP	      4
#define RSDP_CHECKSUM_LENGTH 20

static void* acpi_tb_scan_memory_for_rsdp(u64 address, int length)
{
	u32 offset = 0;
	char *cur;
	void *base = ioremap(address, length);

	if (base == NULL) {
		printk("Couldn't get ptr for RSDP 0x%llx\n", address);
		return NULL;
	}
	cur = base;
	//printk(" Looking for RDSP  0x%llx-0x%llx [%p], 0x%X bytes\n", address, address+length, base, length);

	while (offset < length) {
		if (0 && cur[0] == 'R') printk(" - %p [0x%016llx]: %c%c%c%c %c%c%c%c\n", cur, address+offset,
		    CHAR(cur[0]), CHAR(cur[1]), CHAR(cur[2]), CHAR(cur[3]),
		    CHAR(cur[4]), CHAR(cur[5]), CHAR(cur[6]), CHAR(cur[7])
		    );
		if (memcmp(cur, "RSD PTR ", 8) == 0 && td_bios_checksum(cur, RSDP_CHECKSUM_LENGTH) == 0) 
		{
			int i, csum;
			csum = 0;
			for (i = 0; i < 20; i++)
			    csum += ((uint8_t*)cur)[i];
			if ((csum & 0xFF) == 0) {
			    printk(KERN_INFO "acpi: RSDP rev %d found at address %p,  0x%llx\n", 
				  cur[15],
				  cur, address + offset);

			    if (cur[15] != 0)
			      return cur;
			} else {
			  printk(KERN_INFO "acpi: RSDP checksum not valid; continuing\n");
			}
		}
		offset += RSDP_SCAN_STEP;
		cur += RSDP_SCAN_STEP;
		cond_resched();
	}
	iounmap(base);
	return NULL;
}

#ifndef HI_RSDP_WINDOW_BASE
#define HI_RSDP_WINDOW_BASE         0xE0000     /* Physical Address */
#define HI_RSDP_WINDOW_SIZE         0x20000
#endif

#ifndef LO_RSDP_WINDOW_BASE
#define LO_RSDP_WINDOW_BASE         0           /* Physical Address */
#define LO_RSDP_WINDOW_SIZE         0x400
#endif

#ifndef GiB
#define GiB        (1024ULL * 1024ULL * 1024ULL)
#endif

#ifndef MiB
#define MiB        (1024ULL * 1024ULL)
#endif

static struct {
	uint64_t base;
	uint64_t size;
} td_scan_locations[] =
{
	{ HI_RSDP_WINDOW_BASE, HI_RSDP_WINDOW_SIZE },
	
	{ (1 * GiB),    (3 * GiB)   },
#if 0
	{ 0x7a7a0000, 0x10000   },
	{ 0xba7a0000, 0x10000   },

	{ 0x60000000, 0x4000000 },
	{ 0x64000000, 0x4000000 },
	{ 0x68000000, 0x4000000 },
	{ 0x6c000000, 0x4000000 },
	{ LO_RSDP_WINDOW_BASE, LO_RSDP_WINDOW_SIZE },
#endif
};

static struct acpi20_table_rsdp* td_find_acpi_root (void)
{
	int i;

	printk("Scanning known BIOS/EFI locations\n");

	for (i = 0; i < NUM_ELEMENTS(td_scan_locations); i++)
	{
		struct acpi20_table_rsdp *rsdp;
		uint64_t base = td_scan_locations[i].base;
		uint64_t end = base + td_scan_locations[i].size;
		//printk("%d: 0x%llx-0x%llx\n", i, base, end-1);
		do {
			uint64_t size = min(64*MiB, td_scan_locations[i].size);
			rsdp = acpi_tb_scan_memory_for_rsdp(base, 64*MiB);
			if (rsdp)
				return rsdp;
			base += size;
		} while (base < end);
	}

	return NULL;
}


int td_bios_scan ()
{
	struct acpi20_table_rsdp *rsdp;
	struct acpi_table_rsdt *rsdt = NULL;
	struct acpi_table_xsdt *xsdt = NULL;

	rsdp = td_find_acpi_root();

	if (rsdp == NULL)
		return -ENODEV;

	switch (rsdp->revision) {
	case 0:
		rsdt = ioremap(rsdp->rsdt_address, 512);
		if (rsdt && strncmp(rsdt->header.signature, "RSDT", 4) == 0) {
			int i;

			if (td_scan_debug > 1)
				td_bios_print_header(&rsdt->header);

			for (i = 0; i < (rsdt->header.length - sizeof(struct acpi_table_header)) / sizeof(u32); i++)
				if (rsdt->entry[i])
					td_bios_table_process(ioremap(rsdt->entry[i], 4096));
			return 0;
		}
		pr_err("Couldn't find RSDT: %p\n", rsdt);
		if (rsdt) iounmap(rsdt);
		break;
	case 1:
	case 2:
		xsdt = ioremap(rsdp->xsdt_address, 512);
		if (xsdt && strncmp(xsdt->header.signature, "XSDT", 4) == 0) {
			int i;

			if (td_scan_debug > 1)
				td_bios_print_header(&xsdt->header);

			for (i = 0; i < (xsdt->header.length - sizeof(struct acpi_table_header)) / sizeof(u64); i++)
				td_bios_table_process(ioremap(xsdt->entry[i], 4096));
			return 0;
		}
		pr_err("Couldn't find XSDT: %p\n", xsdt);
		if (xsdt) iounmap(xsdt);
		break;
	default:
		pr_err("Invalid revision\n");
	}

	return 0;
}

#endif

void td_bios_cleanup()
{
}

