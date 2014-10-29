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

#ifndef _TD_SCAN_H_
#define _TD_SCAN_H_


#ifdef CONFIG_TERADIMM_ACPI_ASL


#ifndef KABI__acpi_evaluate_object_typed

/* Byte order for UTF-16 strings */
enum utf16_endian {
	UTF16_HOST_ENDIAN,
	UTF16_LITTLE_ENDIAN,
	UTF16_BIG_ENDIAN
};

extern int utf16s_to_utf8s(const wchar_t *pwcs, int len,
		enum utf16_endian endian, u8 *s, int maxlen);

extern acpi_status
acpi_evaluate_object_typed(acpi_handle handle,
			   acpi_string pathname,
			   struct acpi_object_list * external_params,
			   struct acpi_buffer * return_buffer,
			   acpi_object_type return_type);
#endif

#ifdef KABI__dmi_walk
#if KABI__dmi_walk == 1
#define compat_dmi_walk(cb2, cb1) dmi_walk(cb1)
#endif

#if KABI__dmi_walk == 2
#define compat_dmi_walk(cb2, cb1) dmi_walk(cb2, NULL)
#endif

#else /*no KABI__dmi_walk*/
#define KABI__dmi_walk_needed
#include <linux/dmi.h>
extern int dmi_walk(void (*decode)(const struct dmi_header *, void *),
		void *private_data);
#define compat_dmi_walk(cb2, cb1) dmi_walk(cb2, NULL)
#endif

#endif

#endif // _TD_SCAN_H
