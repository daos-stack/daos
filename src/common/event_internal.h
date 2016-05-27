/*
 * SPECIAL LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of Contract No. B599860,
 * and the terms of the LGPL License.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/*
 * LGPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * (C) Copyright 2012, 2013 Intel Corporation
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 or (at your discretion) any later version.
 * (LGPL) version 2.1 accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * LGPL HEADER END
 */
/*
 * common/daos_eq_internal.h
 *
 * Author: Liang Zhen  <liang.zhen@intel.com>
 */

#ifndef EVENT_INTERNAL_H
#define EVENT_INTERNAL_H

#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/shm.h>
#include <pthread.h>
#include <daos/common.h>
#include <daos_event.h>
#include <daos/event.h>
#include <daos/list.h>
#include <daos/hash.h>

typedef struct daos_eq {
	/* After event is completed, it will be moved to the eq_comp list */
	daos_list_t		eq_comp;
	int			eq_n_comp;

	/** In flight events will be put to the disp list */
	daos_list_t		eq_disp;
	int			eq_n_disp;

	struct {
		uint64_t	space[20];
	}			eq_private;

} daos_eq_t;

struct daos_event_ops {
	daos_event_abort_cb_t	op_abort;
	daos_event_comp_cb_t	op_comp;
};

struct daos_event_private {
	daos_handle_t		evx_eqh;
	daos_list_t		evx_link;
	struct daos_hlink	evx_eq_hlink;
	/** children list */
	daos_list_t		evx_child;
	unsigned int		evx_nchild;
	unsigned int		evx_nchild_if;
	unsigned int		evx_nchild_comp;

	daos_ev_status_t	evx_status;
	struct daos_event_private *evx_parent;

	dtp_context_t		*evx_ctx;
	struct daos_event_ops	 evx_ops;
	struct daos_op_sp	 evx_sp;
};

static inline struct daos_event_private *
daos_ev2evx(struct daos_event *ev)
{
	return (struct daos_event_private *)&ev->ev_private;
}

static inline struct daos_event *
daos_evx2ev(struct daos_event_private *evx)
{
	return container_of(evx, struct daos_event, ev_private);
}

struct daos_eq_private {
	/* link chain in the global hash list */
	struct daos_hlink	eqx_hlink;
	pthread_mutex_t		eqx_lock;
	unsigned int		eqx_lock_init:1,
				eqx_finalizing:1;

	/* All of its events are linked here */
	struct daos_hhash	*eqx_events_hash;

	/* DTP context associated with this eq */
	dtp_context_t		 eqx_ctx;
};

static inline struct daos_eq_private *
daos_eq2eqx(struct daos_eq *eq)
{
	return (struct daos_eq_private *)&eq->eq_private;
}

static inline struct daos_eq *
daos_eqx2eq(struct daos_eq_private *eqx)
{
	return container_of(eqx, struct daos_eq, eq_private);
}
#endif /* __EVENT_INTERNAL_H__ */
