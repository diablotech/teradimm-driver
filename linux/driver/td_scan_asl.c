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
#include "td_discovery.h"

#include "td_scan.h"

/* defines */
/* This is defined in later kernels. */
#define DMI_ENTRY_MEM_DEVICE		17

#define DMI_MEGADIMM			"DTI_MegaDIMM_rev1"
#define DMI_TERADIMM_LITE               "TDIMMLite"

#define TD_ACPI_HID			"IBM8000"

static const char td_acpu_dsm_uuid[] = {
	/* 0000 */    0x5E, 0xB6, 0x07, 0xF5, 0x4B, 0xA6, 0x29, 0x46,
	/* 0008 */    0x89, 0xD1, 0xFD, 0x3C, 0x05, 0xC7, 0x7A, 0xA5
};

/* acpi structs */
/* dmi structs */

struct td_scan_mem_dmi_entry {
	/* The dmi_header */
	uint8_t type;
	uint8_t length;
	uint16_t handle;
	/* memory device info */
	uint16_t phys_mem_array;
	uint16_t mem_err_info_handle;
	uint16_t total_width;
	uint16_t data_width;
	uint16_t size;
	uint8_t form;
	uint8_t device_set; /* 0x0F */
	uint8_t device_locator;
	uint8_t bank_locator;
	uint8_t memory_type;
	uint16_t type_detail;
	uint16_t speed;
	uint8_t manufacturer;
	uint8_t serial_number;
	uint8_t asset_tag;
	uint8_t part_number;
	uint8_t attributes;
	uint32_t extended_size;
	uint16_t conf_mem_clk_speed;
} __attribute__((__packed__));

static LIST_HEAD(td_device_info_list);
static uint16_t td_min_conf_mem_clk_speed = (~((u_int16_t)0));

/* prototypes */

static int td_scan_check_entry(struct td_device_info *dev_info,
		const struct td_dev_callback_opts *opts);

/* acpi */
static acpi_status td_scan_acpi_callback(acpi_handle handle, u32 level,
					     void *ctxt, void **retv);
static acpi_status td_scan_extract_irq(struct acpi_resource *resource,
		struct td_device_info* td_info);
static acpi_status td_scan_extract_checksum(struct acpi_resource *resource,
		struct td_device_info* td_info);
static acpi_status td_scan_extract_address64(struct acpi_resource *resource,
		struct td_device_info* td_info);
static acpi_status td_acpi_memory_get_resource(struct acpi_resource *resource,
		void *handle);

/* dmi */
static void td_scan_dmi_walk_callback(const struct dmi_header *dm, void *dummy);
#ifdef KABI__dmi_walk
#if KABI__dmi_walk == 1
static void td_scan_dmi_walk_cb_1(const struct dmi_header *dm);
#endif
#endif
static char *td_scan_dmi_string (const struct dmi_header *dm, uint8_t nth);


/* Functions */

void td_asl_scan(const struct td_dev_callback_opts* opts)
{
	int ret;
	acpi_status status;
	struct td_device_info *info;
	char dev_char = 'a';

	if (!list_empty(&td_device_info_list)) {
		pr_err("td_scan: skipping second scan for devices.");
		return;
	}

	pr_debug("td_scan: ACPI checks\n");
	status = acpi_get_devices(TD_ACPI_HID, td_scan_acpi_callback,
			NULL, NULL);

	pr_debug("td_scan: dmi walk\n");
	compat_dmi_walk(&td_scan_dmi_walk_callback, &td_scan_dmi_walk_cb_1);

	if (td_min_conf_mem_clk_speed == (~((u_int16_t)0)))
		td_min_conf_mem_clk_speed = 0;

	list_for_each_entry(info, &td_device_info_list, link) {

		pr_debug("td_scan: found uid=0x%X\n", info->uid.unique_id);

		/* look for missing info, info->valid if it's OK */

		ret = td_scan_check_entry(info, opts);
		if (ret) {
			pr_err("td_scan: partial information for device, "
				"skipping device '%s', error=%d\n",
				info->dev_name, ret);
		}

		/* assign the name even if the device is no good, this way
		 * UEFI SMBIOS hiccups don't renumber the devices as often */

		sprintf(info->dev_name, "%s%c", info->device_type, dev_char);
		pr_debug("dev name = %s\n", info->dev_name);

		/* figure out the next character */
		if (dev_char == 'z') {
			dev_char = 'A';
		} else if (dev_char == 'Z'+1) {
			pr_err("td_scan: too many devices");
			break;
		} else {
			dev_char++;
		}
	}
}

void td_asl_print_table(void)
{
	struct td_device_info *info;

	list_for_each_entry(info, &td_device_info_list, link) {

		if (!info->valid)
			continue;

		pr_info("ACPI PNP slot = %d channel = %d home_agent = %d "
			"cpu = %d, irq = %d locator = (%s) type = %s\n",
			info->uid.slot_id, info->uid.channel,
			info->uid.home_agent, info->uid.cpu_socket,
			info->irq, info->bank_locator, info->device_type);

		pr_info("  - size = 0x%llX, offset = 0x%llX\n",
			info->size, info->offset);

	}
}

void td_asl_enumerate(td_create_callback callback_func, void *opaque)
{
	struct td_device_info *info;

pr_err("CPU_%u: enumerating\n", (int)smp_processor_id());

	list_for_each_entry(info, &td_device_info_list, link) {

		if (!info->valid)
			continue;

pr_err("CPU_%u: calling callback %p(%p)\n", (int)smp_processor_id(), callback_func, info);
		callback_func(info, opaque);
	}
}

void td_asl_cleanup(void)
{
	struct td_device_info *info;

	while( !list_empty(&td_device_info_list) ) {

		info = list_entry(td_device_info_list.next,
				struct td_device_info, link);
		list_del(&info->link);

		kfree(info);
	}
}

/* check if dev_info has anything missing.
 * return 0 if valid, 1 if not.
 */
static int td_scan_check_entry(struct td_device_info *dev_info,
		const struct td_dev_callback_opts* opts){

	int ret = 0;

	dev_info->valid = 0;

	if (!dev_info->irq) {
		pr_err("ERROR: irq isn't set!\n");
		ret |= 1;
	}

	if (!dev_info->bank_locator) {
		pr_err("ERROR: bank_locator isn't set!\n");
		ret |= 1 << 1;
	}

	if (!strncmp(dev_info->device_type, "   ", 3)) {
		if (opts->td_scan_enable > 1) {
			if (opts->td_scan_enable == 4) {
				snprintf(dev_info->device_type, 3, "td");
			}
			else if (opts->td_scan_enable == 3) {
				snprintf(dev_info->device_type, 3, "md");
			}
		}
		else if (opts->td_magic_type) {
			pr_warn("No device type detected for DIMM in %s"
					", falling back to td\n",
					(dev_info->bank_locator ? : "(null)"));
			snprintf(dev_info->device_type, 3, "td");
		}
		else {
			pr_err("ERROR: device_type isn't set!\n");
			pr_err("bank_locator in ACPI probably does not match SMBIOS !\n");
			ret |= 1 << 2;
		}
	}
	if (!dev_info->memspeed) {
		if (opts->td_magic_speed) {
			pr_warn("No speed detected for %s, using %d\n",
					(dev_info->bank_locator ? : "(null)"),
					td_min_conf_mem_clk_speed);
			dev_info->memspeed = td_min_conf_mem_clk_speed;
		}
		else {
			pr_warn("No speed detected for %s\n",
					(dev_info->bank_locator ? : "(null)"));
		}
	}

	if (!dev_info->size) {
		pr_err("ERROR: size isn't set!\n");
		ret |= 1 << 3;
	}

	if (!dev_info->offset) {
		pr_err("ERROR: offset isn't set!\n");
		ret |= 1 << 4;
	}

	dev_info->valid = (ret == 0);

	return ret;
}

/* ACPI functions */

static acpi_status td_scan_extract_irq(struct acpi_resource *resource,
		struct td_device_info* td_info) {
	struct acpi_resource_extended_irq *e_irq = &resource->data.extended_irq;
	td_info->irq = e_irq->interrupts[0];
	return AE_OK;

}

static acpi_status td_scan_extract_checksum(struct acpi_resource *resource,
					    struct td_device_info* td_info) {
	struct acpi_resource_end_tag *etag = &resource->data.end_tag;
	td_info->checksum = etag->checksum;
	return AE_OK;
}

static acpi_status td_scan_extract_address64(struct acpi_resource *resource,
					     struct td_device_info* td_info) {
	acpi_status ret;
	struct acpi_resource_address64 address64;

	ret = acpi_resource_to_address64(resource, &address64);

	if (ACPI_FAILURE(ret)) {
		goto resource_failed;
	}

	if( (address64.resource_type != ACPI_MEMORY_RANGE) ||
			(address64.address_length == 0)) {
		ret = AE_NOT_FOUND;
		goto resource_notfound;
	}

	td_info->size = address64.address_length;
	td_info->offset = address64.minimum;

resource_notfound:
resource_failed:
	return ret;

}

static acpi_status td_acpi_memory_get_resource(struct acpi_resource *resource,
					       void *handle) {

	acpi_status ret;
	struct list_head *pos;
	struct td_device_info* tmp;
	bool new = false;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer buffer1 = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj_desc;
	char *locator;

	ret = acpi_evaluate_object_typed(handle, METHOD_NAME__UID,  NULL,
					 &buffer, ACPI_TYPE_INTEGER);

	if (ACPI_FAILURE(ret))
	{
		goto no_obj;
	}

	obj_desc = (union acpi_object*)buffer.pointer;
	if (obj_desc->integer.value > ACPI_UINT8_MAX) {
		ret = AE_NOT_IMPLEMENTED;
		goto integer_too_big;
	}

	list_for_each(pos, &td_device_info_list) {
		tmp = list_entry(pos, struct td_device_info, link);
		if (tmp->uid.unique_id == (uint8_t)obj_desc->integer.value)
			break;

		tmp = NULL;
	}

	if (tmp == NULL)
	{
		tmp = (struct td_device_info*)kmalloc(
				sizeof(struct td_device_info), GFP_KERNEL);
		tmp->uid.unique_id = (uint8_t)obj_desc->integer.value;
		tmp->source = "ACPI";
		new = true;
	}

	switch (resource->type) {
	case ACPI_RESOURCE_TYPE_ADDRESS64:
		ret = td_scan_extract_address64(resource, tmp);
		break;
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		ret = td_scan_extract_irq(resource, tmp);
		break;
	case ACPI_RESOURCE_TYPE_END_TAG:
		ret = td_scan_extract_checksum(resource, tmp);
		break;
	default:
		pr_err("td_scan: Unhandled resorce type %d?!\n",
			resource->type);
		ret = AE_NOT_IMPLEMENTED;
		break;
	}

	if (ACPI_FAILURE(ret))
		goto r_type_failure;

	if (new) {
		tmp->memspeed = 0;
		ret = acpi_evaluate_object_typed(handle, "_STR",  NULL,
						 &buffer1, ACPI_TYPE_BUFFER);
		if (ACPI_FAILURE(ret))
			goto no_str;

		obj_desc = (union acpi_object*)buffer1.pointer;

		/*_STR is unicode, so the locator string will only need to be
		 * 1/2 the size.
		 */
		locator = kzalloc(sizeof(char*)*(obj_desc->buffer.length/2),
				GFP_KERNEL);

		utf16s_to_utf8s((wchar_t *)obj_desc->buffer.pointer,
				obj_desc->buffer.length, UTF16_LITTLE_ENDIAN,
				locator, PAGE_SIZE);

		if (!locator)
			goto oom_new_obj;

		/* If the string is too large, we may as well abort adding it.*/
		if (strlen(locator) > TD_LOCATOR_SIZE) {
			pr_err("bank locator string too long for "
			       "bank_locator\n");
			goto str_too_long;
		}

		/* Copy the entire string.. */
		strncpy(tmp->bank_locator, locator, sizeof(tmp->bank_locator));
		strncpy(tmp->device_type, "   ", sizeof(tmp->device_type));

		pr_debug("td_scan: Found bank_locator = \"%s\"\n",
				tmp->bank_locator);
		list_add(&(tmp->link), &td_device_info_list);
	}

	goto success;

str_too_long:
	kfree(locator);
oom_new_obj:
no_str:
r_type_failure:
	kfree(tmp);

success:
integer_too_big:
no_obj:
	kfree(buffer.pointer);
	kfree(buffer1.pointer);
	return ret;

}

static acpi_status td_scan_acpi_callback(acpi_handle handle, u32 level,
		void *ctxt, void **retv) {
	acpi_status ret;

	ret = acpi_walk_resources(handle, METHOD_NAME__CRS,
			td_acpi_memory_get_resource, handle);

	/* Don't return AE_NOT_FOUND, or we will abort further callbacks. */
	if (ret == AE_NOT_FOUND)
		ret = 0;

	return ret;
}

/* DMI functions */
static void td_scan_dmi_walk_callback(const struct dmi_header *dm,
				      void *dummy) {

	struct td_scan_mem_dmi_entry *memdev =
		(struct td_scan_mem_dmi_entry*)dm;

	char *part_num, *locator;
	struct list_head *pos;
	struct td_device_info* tmp;

	if (!memdev->size)
		goto no_size;

	if (memdev->length < 0x13)
		goto too_short;

	/* Keep track of the slowest non-zero memory speed we see. */
	if (memdev->conf_mem_clk_speed &&
	    td_min_conf_mem_clk_speed > memdev->conf_mem_clk_speed) {
		td_min_conf_mem_clk_speed = memdev->conf_mem_clk_speed;
	}

	if (dm->type == DMI_ENTRY_MEM_DEVICE) {
		const char* dev_type = "   ";
		part_num = td_scan_dmi_string(dm, memdev->part_number);
		locator = td_scan_dmi_string(dm, memdev->bank_locator);
		if (!part_num)
			goto no_part_num;

		if (!locator)
			goto no_locator;

		if (strncmp(part_num, DMI_MEGADIMM,
					strlen(DMI_MEGADIMM)) == 0) {
			dev_type = "md";
		} else if (strncmp(part_num, DMI_TERADIMM_LITE,
					strlen(DMI_TERADIMM_LITE)) == 0) {
			dev_type = "td";
		} else {
			goto not_dti_device;
		}

		list_for_each(pos, &td_device_info_list) {
			tmp = list_entry(pos, struct td_device_info, link);
			if (strncmp(tmp->bank_locator,
				    locator, TD_LOCATOR_SIZE) == 0) {
				snprintf(tmp->device_type, 3, dev_type);
				tmp->memspeed = memdev->conf_mem_clk_speed;
			}
		}
	}

no_locator:
no_part_num:
too_short:
no_size:
not_dti_device:
	return;
}
#ifdef KABI__dmi_walk
#if KABI__dmi_walk == 1
/* for older dmi_walk implementations. */
static void td_scan_dmi_walk_cb_1(const struct dmi_header *dm) {
	return td_scan_dmi_walk_callback(dm, NULL);
}
#endif
#endif

/*
 * Extract the nth string from the dmi_header.
 * Returns the char* to the string.
 * Note: string numbering starts at 1.
 * */
static char *td_scan_dmi_string (const struct dmi_header *dm, uint8_t nth) {
	/* set the buffer to the start of the struct.*/
	char *buff = (char*)dm;
	if(nth < 1)
		return NULL;
	/* Skip all non-string data. This points to string 1. */
	buff += dm->length;

	/* we only need it increment if it's 2 or larger. */
	while(nth > 1 && *buff) {
		buff += strlen(buff) + 1;
		nth--;
	}
	/*  Returning null is okay. */
	if (!*buff)
		return NULL;

	return buff;
}


#ifndef KABI__acpi_evaluate_object_typed
#include "lk_compat_acpi.c"
#endif  /* KABI__acpi_evaluate_object_typed */

#ifdef KABI__dmi_walk_needed
#include "lk_compat_dmi.c"
#endif /* KABI__dmi_walk_needed */


