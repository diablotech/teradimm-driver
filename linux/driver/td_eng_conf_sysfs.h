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

#ifndef _TD_SYSFS_H_
#define _TD_SYSFS_H_

#include <linux/types.h>
#include <linux/sysfs.h>

struct td_device;

#if defined CONFIG_TERADIMM_SYSFS
extern int td_eng_conf_sysfs_register(struct td_device *dev);
extern void td_eng_conf_sysfs_unregister(struct td_device *dev);
#else
#define td_eng_conf_sysfs_register(dev)   (0)
#define td_eng_conf_sysfs_unregister(dev) ({})
#endif

#endif

