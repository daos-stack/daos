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
 * This file is part of common DAOS library.
 *
 * common/daos_eq.c
 *
 * Author: Liang Zhen  <liang.zhen@intel.com>
 * Author: Di Wang  <di.wang@intel.com>
 */
#include <daos/transport.h>
#include "daos_eq_internal.h"

static struct daos_hhash *daos_eq_hhash;

/*
 * For the moment, we use a global dtp_context_t to create all the RPC requests
 * this module uses.
 */
static dtp_context_t daos_eq_ctx;
static pthread_mutex_t daos_eq_lock = PTHREAD_MUTEX_INITIALIZER;

int
daos_eq_lib_init(dtp_context_t ctx)
{
	int rc;

	pthread_mutex_lock(&daos_eq_lock);
	if (daos_eq_hhash != NULL) {
		pthread_mutex_unlock(&daos_eq_lock);
		return -DER_ALREADY;
	}

	daos_eq_ctx = ctx;
	rc = daos_hhash_create(DAOS_HHASH_BITS, &daos_eq_hhash);
	pthread_mutex_unlock(&daos_eq_lock);
	return rc;
}

void
daos_eq_lib_fini()
{
	pthread_mutex_lock(&daos_eq_lock);
	daos_eq_ctx = NULL;
	if (daos_eq_hhash != NULL) {
		daos_hhash_destroy(daos_eq_hhash);
		daos_eq_hhash = NULL;
	}
	pthread_mutex_unlock(&daos_eq_lock);
}

static void
daos_eq_free(struct daos_hlink *hlink)
{
	struct daos_eq_private	*eqx;
	struct daos_eq		*eq;

	eqx = container_of(hlink, struct daos_eq_private, eqx_hlink);
	eq = daos_eqx2eq(eqx);
	D_ASSERT(daos_list_empty(&eq->eq_disp));
	D_ASSERT(daos_list_empty(&eq->eq_comp));
	D_ASSERTF(eq->eq_n_comp == 0 && eq->eq_n_disp == 0,
		  "comp %d disp %d\n", eq->eq_n_comp, eq->eq_n_disp);
	D_ASSERT(daos_hhash_link_empty(&eqx->eqx_hlink));

	if (eqx->eqx_events_hash != NULL) {
		daos_hhash_destroy(eqx->eqx_events_hash);
		eqx->eqx_events_hash = NULL;
	}

	if (eqx->eqx_lock_init)
		pthread_mutex_destroy(&eqx->eqx_lock);

	free(eq);
}

struct daos_hlink_ops	eq_h_ops = {
	.hop_free	= daos_eq_free,
};

static struct daos_eq *
daos_eq_alloc(void)
{
	struct daos_eq		*eq;
	struct daos_eq_private	*eqx;
	int			rc;

	eq = malloc(sizeof(*eq));
	if (eq == NULL)
		return NULL;

	DAOS_INIT_LIST_HEAD(&eq->eq_disp);
	DAOS_INIT_LIST_HEAD(&eq->eq_comp);
	eq->eq_n_disp = 0;
	eq->eq_n_comp = 0;

	eqx = daos_eq2eqx(eq);

	rc = pthread_mutex_init(&eqx->eqx_lock, NULL);
	if (rc != 0)
		goto out;
	eqx->eqx_lock_init = 1;

	daos_hhash_hlink_init(&eqx->eqx_hlink, &eq_h_ops);

	rc = daos_hhash_create(DAOS_HHASH_BITS, &eqx->eqx_events_hash);
	if (rc != 0)
		goto out;

	return eq;
out:
	daos_eq_free(&eqx->eqx_hlink);
	return NULL;
}

static struct daos_eq_private *
daos_eq_lookup(daos_handle_t eqh)
{
	struct daos_hlink *hlink;

	hlink = daos_hhash_link_lookup(daos_eq_hhash, eqh.cookie);
	if (hlink == NULL)
		return NULL;

	return container_of(hlink, struct daos_eq_private, eqx_hlink);
}

static void
daos_eq_putref(struct daos_eq_private *eqx)
{
	D_ASSERT(daos_eq_hhash != NULL);
	daos_hhash_link_putref(daos_eq_hhash, &eqx->eqx_hlink);
}

static void
daos_eq_delete(struct daos_eq_private *eqx)
{
	D_ASSERT(daos_eq_hhash != NULL);
	daos_hhash_link_delete(daos_eq_hhash, &eqx->eqx_hlink);
}

static void
daos_eq_insert(struct daos_eq_private *eqx)
{
	D_ASSERT(daos_eq_hhash != NULL);
	daos_hhash_link_insert(daos_eq_hhash, &eqx->eqx_hlink, 0);
}

static void
daos_eq_handle(struct daos_eq_private *eqx, daos_handle_t *h)
{
	daos_hhash_link_key(&eqx->eqx_hlink, &h->cookie);
}

static void
daos_event_launch_locked(struct daos_eq_private *eqx,
			 struct daos_event_private *evx)
{
	struct daos_event_private *parent = evx->evx_parent;
	struct daos_eq *eq = daos_eqx2eq(eqx);

	evx->evx_status = DAOS_EVS_DISPATCH;

	if (parent != NULL) {
		parent->evx_nchild_if++;
		if (!daos_list_empty(&parent->evx_link))
			return;

		D_ASSERT(parent->evx_nchild_if == 1);
		parent->evx_status = DAOS_EVS_DISPATCH;
		evx = parent;
	}

	daos_list_add_tail(&evx->evx_link, &eq->eq_disp);
	eq->eq_n_disp++;
}

int
daos_event_launch(struct daos_event *ev)
{
	struct daos_event_private *evx = daos_ev2evx(ev);
	struct daos_eq_private	  *eqx;
	int			  rc = 0;

	if (evx->evx_status != DAOS_EVS_INIT ||
	    !daos_list_empty(&evx->evx_child)) {
		D_ERROR("Event status %d is wrong, or it's a parent event\n",
			evx->evx_status);
		return DER_NO_PERM;
	}

	if (evx->evx_eqh.cookie == 0) {
		D_ERROR("Invalid EQ handle\n");
		return DER_INVAL;
	}

	eqx = daos_eq_lookup(evx->evx_eqh);
	if (eqx == NULL) {
		D_ERROR("Can't find event queue from handle %"PRIu64"\n",
			evx->evx_eqh.cookie);
		return DER_NONEXIST;
	}

	pthread_mutex_lock(&eqx->eqx_lock);
	if (eqx->eqx_finalizing) {
		D_ERROR("Event queue is in progress of finalizing\n");
		rc = DER_NONEXIST;
		goto out;
	}

	daos_event_launch_locked(eqx, evx);
 out:
	pthread_mutex_unlock(&eqx->eqx_lock);
	daos_eq_putref(eqx);
	return rc;
}

static void
daos_event_complete_locked(struct daos_eq_private *eqx,
			   struct daos_event_private *evx)
{
	struct daos_event_private *parent = evx->evx_parent;
	struct daos_eq		  *eq = daos_eqx2eq(eqx);

	evx->evx_status = DAOS_EVS_COMPLETED;
	if (parent != NULL) {
		D_ASSERT(parent->evx_nchild_if > 0);
		parent->evx_nchild_if--;

		D_ASSERT(parent->evx_nchild_comp < parent->evx_nchild);
		parent->evx_nchild_comp++;

		if (parent->evx_nchild_comp < parent->evx_nchild)
			return;

		parent->evx_status = DAOS_EVS_COMPLETED;
		evx = parent;
	}

	D_ASSERT(!daos_list_empty(&evx->evx_link));
	daos_list_move_tail(&evx->evx_link, &eq->eq_comp);

	D_ASSERT(eq->eq_n_disp > 0);
	eq->eq_n_disp--;
	eq->eq_n_comp++;
}

void
daos_event_complete(struct daos_event *ev)
{
	struct daos_event_private *evx = daos_ev2evx(ev);
	struct daos_eq_private	  *eqx;

	eqx = daos_eq_lookup(evx->evx_eqh);
	D_ASSERT(eqx != NULL);

	pthread_mutex_lock(&eqx->eqx_lock);
	D_ASSERT(!daos_list_empty(&daos_eqx2eq(eqx)->eq_disp));
	D_ASSERT(evx->evx_status == DAOS_EVS_DISPATCH);

	daos_event_complete_locked(eqx, evx);

	pthread_mutex_unlock(&eqx->eqx_lock);

	daos_eq_putref(eqx);
}

int
daos_eq_create(daos_handle_t *eqh)
{
	struct daos_eq_private	*eqx;
	struct daos_eq		*eq;
	int			rc = 0;

	eq = daos_eq_alloc();
	if (eq == NULL)
		return -DER_NOMEM;

	eqx = daos_eq2eqx(eq);
	daos_eq_insert(eqx);
	daos_eq_handle(eqx, eqh);

	daos_eq_putref(eqx);
	return rc;
}

int
daos_eq_poll(daos_handle_t eqh, int wait_inf, int64_t timeout,
	     int n_events, struct daos_event **events)
{
	struct daos_eq_private		*eqx;
	struct daos_event_private	*evx;
	struct daos_event_private	*tmp;
	struct daos_event	*ev;
	struct daos_eq		*eq;
	struct timeval		tv;
	int			count;
	int			rc = 0;
	uint64_t		end = 0;
	uint64_t		now = 0;

	if (n_events == 0)
		return -DER_INVAL;

	if (daos_eq_ctx == NULL)
		return -DER_INVAL;

	eqx = daos_eq_lookup(eqh);
	if (eqx == NULL)
		return -DER_NONEXIST;

	if (timeout >= 0) {
		gettimeofday(&tv, NULL);
		now = tv.tv_sec * 1000 * 1000 + tv.tv_usec;
		end = now + timeout;
	}

	eq = daos_eqx2eq(eqx);
	while (now <= end || timeout < 0) {
		uint64_t interval = 1000 * 1000; /* Milliseconds */

		count = 0;
		pthread_mutex_lock(&eqx->eqx_lock);
		daos_list_for_each_entry_safe(evx, tmp, &eq->eq_comp,
					      evx_link) {
			D_ASSERT(eq->eq_n_comp > 0);
			eq->eq_n_comp--;

			daos_list_del_init(&evx->evx_link);
			D_ASSERT(evx->evx_status == DAOS_EVS_COMPLETED ||
				 evx->evx_status == DAOS_EVS_ABORT);
			evx->evx_status = DAOS_EVS_INIT;

			if (events != NULL) {
				ev = daos_evx2ev(evx);
				events[count++] = ev;
			}

			if (count == n_events)
				break;
		}

		/* Exist once there are completion events XXX */
		if (count > 0) {
			pthread_mutex_unlock(&eqx->eqx_lock);
			rc = count;
			break;
		}

		/* no completion event, eq::eq_comp is empty */
		if (eqx->eqx_finalizing) { /* no new event is coming */
			D_ASSERT(daos_list_empty(&eq->eq_disp));
			pthread_mutex_unlock(&eqx->eqx_lock);
			rc = -DER_NONEXIST;
			break;
		}

		/* wait only if there' inflight event? */
		if (wait_inf && daos_list_empty(&eq->eq_disp)) {
			pthread_mutex_unlock(&eqx->eqx_lock);
			break;
		}

		pthread_mutex_unlock(&eqx->eqx_lock);

		rc = dtp_progress(daos_eq_ctx, interval, NULL, NULL,
				  NULL);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("dtp progress fails: rc = %d\n", rc);
			break;
		}
		rc = 0;

		if (timeout > 0) {
			/* wait until timeout */
			struct timeval	tv;

			gettimeofday(&tv, NULL);
			now = tv.tv_sec * 1000 * 1000 + tv.tv_usec;
		} else if (timeout == 0) {
			/* Do not wait */
			break;
		}
	}

	daos_eq_putref(eqx);
	return rc;
}

int
daos_eq_query(daos_handle_t eqh, daos_eq_query_t query,
	      unsigned int n_events, struct daos_event **events)
{
	struct daos_eq_private		*eqx;
	struct daos_eq			*eq;
	struct daos_event_private	*evx;
	struct daos_event		*ev;
	int				count;

	eqx = daos_eq_lookup(eqh);
	if (eqx == NULL)
		return -DER_NONEXIST;

	eq = daos_eqx2eq(eqx);

	count = 0;
	pthread_mutex_lock(&eqx->eqx_lock);

	if (n_events == 0 || events == NULL) {
		if ((query & DAOS_EQR_COMPLETED) != 0)
			count += eq->eq_n_comp;

		if ((query & DAOS_EQR_DISPATCH) != 0)
			count += eq->eq_n_disp;
		goto out;
	}

	if ((query & DAOS_EQR_COMPLETED) != 0) {
		daos_list_for_each_entry(evx, &eq->eq_comp, evx_link) {
			ev = daos_evx2ev(evx);
			events[count++] = ev;
			if (count == n_events)
				goto out;
		}
	}

	if ((query & DAOS_EQR_DISPATCH) != 0) {
		daos_list_for_each_entry(evx, &eq->eq_disp, evx_link) {
			ev = daos_evx2ev(evx);
			events[count++] = ev;
			if (count == n_events)
				goto out;
		}
	}
out:
	pthread_mutex_unlock(&eqx->eqx_lock);
	daos_eq_putref(eqx);
	return count;
}

/**
 * remove an event from hash of EQ so nobody can find it via ID,
 * user can also indicate to unlink all his children.
 * A unlinked event is still attached on EQ if it's inflight, it will
 * be detached from EQ after completion.
 */
static void
daos_event_unlink_locked(struct daos_eq_private *eqx,
			 struct daos_event_private *evx,
			 int unlink_children)
{
	struct daos_event_private *tmp;

	if (daos_hhash_link_empty(&evx->evx_eq_hlink))
		return;

	daos_hhash_link_delete(eqx->eqx_events_hash, &evx->evx_eq_hlink);
	if (!unlink_children)
		return;

	daos_list_for_each_entry(tmp, &evx->evx_child, evx_link) {
		daos_hhash_link_delete(eqx->eqx_events_hash,
				       &tmp->evx_eq_hlink);
	}
}

static void
daos_event_abort_one(struct daos_event_private *evx, int unlinked)
{
	if (evx->evx_status != DAOS_EVS_DISPATCH)
		return;

	/* NB: ev::ev_error will be set by daos_event_complete(),
	 * so user can decide to not set error if operation has already
	 * finished while trying to abort */
	/* NB: always set ev_status to DAOS_EVS_ABORT even w/o callback,
	 * so aborted parent event can be marked as COMPLETE right after
	 * completion all dispatched events other than completion of all
	 * children. See daos_parent_event_can_complete for details. */
	evx->evx_status = DAOS_EVS_ABORT;
}

static void
daos_event_abort_locked(struct daos_eq_private *eqx,
			struct daos_event_private *evx,
			int unlink)
{
	struct daos_event_private *child;

	D_ASSERT(evx->evx_status == DAOS_EVS_DISPATCH);

	if (unlink)
		daos_event_unlink_locked(eqx, evx, 0);

	daos_event_abort_one(evx, unlink);

	/* abort all children if he has */
	daos_list_for_each_entry(child, &evx->evx_child, evx_link) {
		if (unlink)
			daos_event_unlink_locked(eqx, evx, 0);
		daos_event_abort_one(child, unlink);
	}

	/* if aborted event is not a child event, move it to the
	 * head of dispatched list */
	if (evx->evx_parent == NULL) {
		struct daos_eq *eq = daos_eqx2eq(eqx);

		daos_list_del(&evx->evx_link);
		daos_list_add(&evx->evx_link, &eq->eq_comp);
		eq->eq_n_disp--;
		eq->eq_n_comp++;
	}
}

int
daos_eq_destroy(daos_handle_t eqh, int flags)
{
	struct daos_eq_private	  *eqx;
	struct daos_eq		  *eq;
	struct daos_event_private *evx;
	struct daos_event_private *tmp;
	int	rc = 0;

	eqx = daos_eq_lookup(eqh);
	if (eqx == NULL)
		return -DER_NONEXIST;

	pthread_mutex_lock(&eqx->eqx_lock);
	if (eqx->eqx_finalizing) {
		rc = -DER_NONEXIST;
		goto out;
	}

	eq = daos_eqx2eq(eqx);

	/* If it is not force destroyed, then we need check if
	 * there are still events linked here */
	if (flags & DAOS_EQ_DESTROY_FORCE &&
	    (!daos_list_empty(&eq->eq_disp) ||
	     !daos_list_empty(&eq->eq_comp))) {
		rc = -EBUSY;
		goto out;
	}

	/* prevent other threads to launch new event */
	eqx->eqx_finalizing = 1;

	/* abort all inflight events */
	daos_list_for_each_entry_safe(evx, tmp, &eq->eq_disp, evx_link) {
		D_ASSERT(evx->evx_parent == NULL);
		/* abort and unlink */
		daos_event_abort_locked(eqx, evx, 1);
	}

	D_ASSERT(daos_list_empty(&eq->eq_disp));

	/* unlink all completed */
	daos_list_for_each_entry_safe(evx, tmp, &eq->eq_comp, evx_link) {
		daos_list_del(&evx->evx_link);
		D_ASSERT(eq->eq_n_comp > 0);
		eq->eq_n_comp--;
		daos_event_unlink_locked(eqx, evx, 1);
	}
out:
	pthread_mutex_unlock(&eqx->eqx_lock);
	if (rc == 0)
		daos_eq_delete(eqx);
	daos_eq_putref(eqx);
	return rc;
}

/**
 * Add the event to the event queue, and if there is parent, add
 * it to its child list as well.
 **/
int
daos_event_init(struct daos_event *ev, daos_handle_t eqh,
		struct daos_event *parent)
{
	struct daos_event_private	*evx = daos_ev2evx(ev);
	struct daos_event_private	*parent_evx;
	struct daos_eq_private		*eqx;
	int				rc = 0;

	/* Init the event first */
	memset(ev, 0, sizeof(*ev));
	evx->evx_status	= DAOS_EVS_INIT;
	evx->evx_eqh	= eqh;
	DAOS_INIT_LIST_HEAD(&evx->evx_child);

	/* Do not add the child event to the event queue
	 * hash list,only add to the parent list.
	 */
	if (parent != NULL) {
		/* Insert it to the parent event list */
		parent_evx = daos_ev2evx(parent);
		if (parent_evx->evx_status != DAOS_EVS_INIT) {
			D_ERROR("Parent event is not initialized: %d\n",
				parent_evx->evx_status);
			return -DER_INVAL;
		}

		if (parent_evx->evx_parent != NULL) {
			D_ERROR("Can't nest event\n");
			return -DER_NO_PERM;
		}

		/* it's user's responsibility to protect this list */
		daos_list_add_tail(&evx->evx_link, &parent_evx->evx_child);
		evx->evx_parent = parent_evx;
		parent_evx->evx_nchild++;
		return 0;
	}

	eqx = daos_eq_lookup(eqh);
	if (eqx == NULL) {
		D_ERROR("Invalid EQ handle %"PRIx64"\n", eqh.cookie);
		return -DER_NONEXIST;
	}

	/* Insert event to eq hash link */
	daos_hhash_hlink_init(&evx->evx_eq_hlink, NULL);

	daos_hhash_link_insert(eqx->eqx_events_hash, &evx->evx_eq_hlink, 0);
	DAOS_INIT_LIST_HEAD(&evx->evx_link);

	daos_eq_putref(eqx);
	return rc;
}

/**
 * Unlink events from various list, parent_list, child list, and event queue
 * hash list */
int
daos_event_fini(struct daos_event *ev)
{
	struct daos_event_private *evx = daos_ev2evx(ev);
	struct daos_eq_private	  *eqx;
	struct daos_eq		  *eq;
	int			  rc = 0;

	eqx = daos_eq_lookup(evx->evx_eqh);
	if (eqx == NULL)
		return -DER_NONEXIST;

	eq = daos_eqx2eq(eqx);

	/* If it is a child event, delete it from parent list */
	if (evx->evx_parent != NULL) {
		if (daos_list_empty(&evx->evx_link)) {
			D_ERROR("Event not linked to its parent\n");
			return -DER_INVAL;
		}

		if (evx->evx_parent->evx_status != DAOS_EVS_INIT) {
			D_ERROR("Parent event is not initialized or inflight: "
			       "%d\n", evx->evx_parent->evx_status);
			return -DER_INVAL;
		}

		daos_list_del_init(&evx->evx_link);
		evx->evx_status = DAOS_EVS_INIT;
		evx->evx_parent = NULL;

		return 0;
	}

	/* If there are child events */
	while (!daos_list_empty(&evx->evx_child)) {
		struct daos_event_private *tmp;

		tmp = daos_list_entry(evx->evx_child.next,
				     struct daos_event_private, evx_link);
		D_ASSERTF(tmp->evx_status == DAOS_EVS_INIT ||
			 tmp->evx_status == DAOS_EVS_COMPLETED ||
			 tmp->evx_status == DAOS_EVS_ABORT,
			 "EV %p status: %d\n", tmp, tmp->evx_status);

		if (tmp->evx_status != DAOS_EVS_INIT &&
		    tmp->evx_status != DAOS_EVS_COMPLETED &&
		    tmp->evx_status != DAOS_EVS_ABORT) {
			D_ERROR("Child event %p inflight: %d\n",
				daos_evx2ev(tmp), tmp->evx_status);
			rc = -DER_INVAL;
			goto out;
		}

		daos_list_del_init(&tmp->evx_link);
		tmp->evx_status = DAOS_EVS_INIT;
		tmp->evx_parent = NULL;
	}

	/* Remove from the evx_link */
	if (!daos_list_empty(&evx->evx_link)) {
		daos_list_del(&evx->evx_link);
		if (evx->evx_status == DAOS_EVS_DISPATCH) {
			eq->eq_n_disp--;
		} else if (evx->evx_status == DAOS_EVS_COMPLETED) {
			D_ASSERT(eq->eq_n_comp > 0);
			eq->eq_n_comp--;
		}
	}

	daos_hhash_link_delete(eqx->eqx_events_hash, &evx->evx_eq_hlink);
out:
	daos_eq_putref(eqx);
	return rc;
}

struct daos_event *
daos_event_next(struct daos_event *parent,
		struct daos_event *child)
{
	struct daos_event_private	*evx = daos_ev2evx(parent);
	struct daos_event_private	*tmp;

	if (child == NULL) {
		if (daos_list_empty(&evx->evx_child))
			return NULL;

		tmp = daos_list_entry(evx->evx_child.next,
				     struct daos_event_private, evx_link);
		return daos_evx2ev(tmp);
	}

	tmp = daos_ev2evx(child);
	if (tmp->evx_link.next == &evx->evx_child)
		return NULL;

	tmp = daos_list_entry(tmp->evx_link.next, struct daos_event_private,
			     evx_link);
	return daos_evx2ev(tmp);
}

int
daos_event_abort(struct daos_event *ev)
{
	struct daos_event_private *evx = daos_ev2evx(ev);
	struct daos_eq_private	  *eqx;

	eqx = daos_eq_lookup(evx->evx_eqh);
	if (eqx == NULL) {
		D_ERROR("Invalid EQ handle %"PRIu64"\n", evx->evx_eqh.cookie);
		return -DER_NONEXIST;
	}

	pthread_mutex_lock(&eqx->eqx_lock);
	daos_event_abort_locked(eqx, evx, 0);
	pthread_mutex_unlock(&eqx->eqx_lock);

	daos_eq_putref(eqx);
	return 0;
}
