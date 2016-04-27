/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015 Intel Corporation.
 */
/**
 * Internal event API.
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 *
 * Version 0.1
 */
#ifndef __DAOS_EVENTX_H__
#define __DAOS_EVENTX_H__

#include <daos_types.h>
#include <daos_errno.h>
#include <daos/list.h>
#include <daos/hash.h>
#include <daos/transport.h>

struct daos_event_ops {
	int (*op_abort)(void *param, int unlinked);
	int (*op_complete)(void *param, int error, int unlinked);
};

/**
 * Finish event queue library
 */
void
daos_eq_lib_fini(void);

/**
 * Initialize event queue library
 */
int
daos_eq_lib_init(dtp_context_t ctx);

/**
 * Mark the event completed, i.e. move this event
 * to completion list.
 *
 * \param ev [IN]	event to complete.
 */
void
daos_event_complete(daos_event_t *ev);

/**
 * Mark the event launched, i.e. move this event to launch list.
 *
 * \param ev [IN}	event to launch.
 */
int
daos_event_launch(struct daos_event *ev);
#endif /*  __DAOS_EV_INTERNAL_H__ */
