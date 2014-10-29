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

#ifndef _TD_ERRNO_H_
#define _TD_ERRNO_H_

#ifdef __KERNEL__
#include <asm/errno.h>
#else
#include <errno.h>
#endif

#define TD_ERR_RANGE_START 10000
#define TD_ERR_RANGE_END   10999

#define TD_ERR(x)       (TD_ERR_RANGE_START + (x))
#define TD_IS_TD_ERR(x) ((x) >= TD_ERR_RANGE_START && (x) <= TD_ERR_RANGE_END)

#define TD_ERR_E2E_BAD_XSUM TD_ERR(0)
#define TD_ERR_E2E_BAD_LBA  TD_ERR(1)


#endif
