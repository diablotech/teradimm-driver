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

#include "td_util.h"

#ifndef KABI__ratelimit
int td_ratelimit(void)
{
	return 1;
}
#else
#include <linux/ratelimit.h>

static DEFINE_RATELIMIT_STATE(td_ratelimit_state, 5 * HZ, 10);
int td_ratelimit(void)
{
	return __ratelimit(&td_ratelimit_state);
}
#endif

void td_dump_data(const char *prefix, const void *void_data, unsigned len)
{
	char buf[64];
	char *e=buf+sizeof(buf);
	int row, col, rc;
	const char *data = (const char *)void_data;
	const int width = 16;

	for (row=0; row<16; row++) {
		char *p = buf;

		p += rc = snprintf(p, e-p,
				"%04x:", row*width);
		if (rc<0)
			return;

		for (col=0; col<width; col++) {
			unsigned off;
			unsigned char x;
			off = (row*width) + col;

			if (off >= len) {
				if (col) goto last_one;
				return;
			}

			x = data[off];

			p += rc = snprintf(p, e-p,
					" %02x", x);
			if (rc<0)
				return;
		}
last_one:
		printk("%s%s\n", prefix, buf);
	}
}


