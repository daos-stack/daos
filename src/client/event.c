/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/*
 * This file is part of common DAOS library.
 *
 * common/event.c
 *
 * Author: Liang Zhen  <liang.zhen@intel.com>
 * Author: Di Wang  <di.wang@intel.com>
 */

#define DD_SUBSYS	DD_FAC(client)
#include "client_internal.h"
#include <daos/rpc.h>

static struct daos_hhash *daos_eq_hhash;

/** thread-private event */
static __thread daos_event_t	ev_thpriv;
static __thread bool		ev_thpriv_is_init;

#define EQ_WITH_CRT

#if !defined(EQ_WITH_CRT)

#define crt_init(a,b,c)			({0;})
#define crt_finalize()			({0;})
#define crt_context_create(a, b)	({0;})
#define crt_context_destroy(a, b)	({0;})
#define crt_progress(ctx, timeout, cb, args)	\
({						\
	int __rc = cb(args);			\
						\
	while ((timeout) != 0 && __rc == 0) {	\
		sleep(1);			\
		__rc = cb(args);		\
		if ((timeout) < 0)		\
			continue;		\
		if ((timeout) < 1000000)	\
			break;			\
		(timeout) -= 1000000;		\
	}					\
	0;					\
})

#endif

/*
 * For the moment, we use a global crt_context_t to create all the RPC requests
 * this module uses.
 */
static crt_context_t daos_eq_ctx;
static pthread_mutex_t daos_eq_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int refcount;

int
daos_eq_lib_init()
{
	int rc;

	pthread_mutex_lock(&daos_eq_lock);
	if (refcount > 0) {
		refcount++;
		D_GOTO(unlock, rc = 0);
	}

	rc = daos_hhash_create(DAOS_HHASH_BITS, &daos_eq_hhash);
	if (rc != 0) {
		D_ERROR("failed to create hash for eq: %d\n", rc);
		D_GOTO(unlock, rc);
	}

	rc = crt_init(NULL, NULL, 0 /* client-only */);
	if (rc != 0) {
		D_ERROR("failed to initialize crt: %d\n", rc);
		D_GOTO(hash, rc);
	}

	/* use a global shared context for all eq for now */
	rc = crt_context_create(NULL /* arg */, &daos_eq_ctx);
	if (rc != 0) {
		D_ERROR("failed to create client context: %d\n", rc);
		D_GOTO(crt, rc);
	}

	refcount = 1;
unlock:
	pthread_mutex_unlock(&daos_eq_lock);
	return rc;
crt:
	crt_finalize();
hash:
	daos_hhash_destroy(daos_eq_hhash);
	D_GOTO(unlock, rc);
}

int
daos_eq_lib_fini()
{
	int rc;

	pthread_mutex_lock(&daos_eq_lock);
	if (refcount == 0)
		D_GOTO(unlock, rc = -DER_UNINIT);
	if (refcount > 1) {
		refcount--;
		D_GOTO(unlock, rc = 0);
	}

	if (daos_eq_ctx != NULL) {
		rc = crt_context_destroy(daos_eq_ctx, 1 /* force */);
		if (rc != 0) {
			D_ERROR("failed to destroy client context: %d\n", rc);
			D_GOTO(unlock, rc);
		}
		daos_eq_ctx = NULL;
	}

	rc = crt_finalize();
	if (rc != 0) {
		D_ERROR("failed to shutdown crt: %d\n", rc);
		D_GOTO(unlock, rc);
	}

	D_ASSERT(daos_eq_hhash != NULL);
	daos_hhash_destroy(daos_eq_hhash);

	refcount = 0;
unlock:
	pthread_mutex_unlock(&daos_eq_lock);
	return rc;
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

	if (eqx->eqx_lock_init)
		pthread_mutex_destroy(&eqx->eqx_lock);

	D_FREE_PTR(eq);
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

	D_ALLOC_PTR(eq);
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
	daos_hhash_link_insert(daos_eq_hhash, &eqx->eqx_hlink, DAOS_HTYPE_EQ);
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
	struct daos_eq *eq = NULL;

	if (eqx != NULL)
		eq = daos_eqx2eq(eqx);

	evx->evx_status = DAOS_EVS_DISPATCH;
	if (evx->evx_parent != NULL) {
		evx->evx_parent->evx_nchild_if++;
		return;
	}

	if (eq != NULL) {
		daos_list_add_tail(&evx->evx_link, &eq->eq_disp);
		eq->eq_n_disp++;
	}
}

crt_context_t
daos_ev2ctx(struct daos_event *ev)
{
	return daos_ev2evx(ev)->evx_ctx;
}

struct daos_op_sp *
daos_ev2sp(struct daos_event *ev)
{
	return &daos_ev2evx(ev)->evx_callback.evx_inline_cb_sp;
}

daos_handle_t
daos_ev2eqh(struct daos_event *ev)
{
	struct daos_event_private *evx = daos_ev2evx(ev);

	return evx->evx_eqh;
}

int
daos_event_register_comp_cb(struct daos_event *ev,
			    daos_event_comp_cb_t cb, void *arg)
{
	struct daos_event_comp_list	*ecl;
	struct daos_event_private	*evx = daos_ev2evx(ev);

	if (arg == daos_ev2sp(ev) &&
	    evx->evx_callback.evx_inline_cb == NULL) {
		evx->evx_callback.evx_inline_cb = cb;
		return 0;
	}

	D_ALLOC_PTR(ecl);
	if (ecl == NULL)
		return -DER_NOMEM;

	DAOS_INIT_LIST_HEAD(&ecl->op_comp_list);
	ecl->op_comp_arg = arg;
	ecl->op_comp_cb = cb;

	daos_list_add_tail(&evx->evx_callback.evx_comp_list,
			   &ecl->op_comp_list);

	return 0;
}

static int
daos_event_complete_cb(struct daos_event_private *evx, int rc)
{
	struct daos_event_comp_list	*ecl;
	struct daos_event_comp_list	*tmp;
	int				 ret = rc;
	int				 err;

	if (evx->evx_callback.evx_inline_cb != NULL) {
		err = evx->evx_callback.evx_inline_cb(
				&evx->evx_callback.evx_inline_cb_sp,
				daos_evx2ev(evx), rc);
		evx->evx_callback.evx_inline_cb = NULL;
		if (ret == 0)
			ret = err;
	}

	daos_list_for_each_entry_safe(ecl, tmp,
				      &evx->evx_callback.evx_comp_list,
				      op_comp_list) {
		daos_list_del_init(&ecl->op_comp_list);
		err = ecl->op_comp_cb(ecl->op_comp_arg, daos_evx2ev(evx), rc);
		D_FREE_PTR(ecl);
		if (ret == 0)
			ret = err;
	}

	return ret;
}

static int
daos_event_complete_locked(struct daos_eq_private *eqx,
			   struct daos_event_private *evx, int rc)
{
	struct daos_event_private	*parent_evx = evx->evx_parent;
	struct daos_eq			*eq = NULL;
	daos_event_t			*ev = daos_evx2ev(evx);

	if (eqx != NULL)
		eq = daos_eqx2eq(eqx);

	evx->evx_status = DAOS_EVS_COMPLETED;
	rc = daos_event_complete_cb(evx, rc);
	ev->ev_error = rc;

	if (parent_evx != NULL) {
		daos_event_t *parent_ev = daos_evx2ev(parent_evx);

		D_ASSERT(parent_evx->evx_nchild_if > 0);
		parent_evx->evx_nchild_if--;

		D_ASSERT(parent_evx->evx_nchild_comp <
			 parent_evx->evx_nchild);
		parent_evx->evx_nchild_comp++;
		if (parent_evx->evx_nchild_comp < parent_evx->evx_nchild) {
			/* Not all children completion yet */
			parent_ev->ev_error = parent_ev->ev_error ?: rc;
			return 0;
		}

		/* If the parent is not launched yet, let's return */
		if (parent_evx->evx_status == DAOS_EVS_INIT)
			return 0;

		if (parent_evx->evx_status == DAOS_EVS_DISPATCH) {
			parent_evx->evx_status = DAOS_EVS_COMPLETED;
			rc = daos_event_complete_cb(parent_evx, 0);
		}

		parent_ev->ev_error = parent_ev->ev_error ?: rc;
		evx = parent_evx;
	}

	if (eq != NULL) {
		D_ASSERT(!daos_list_empty(&evx->evx_link));
		daos_list_move_tail(&evx->evx_link, &eq->eq_comp);
		eq->eq_n_comp++;
		D_ASSERT(eq->eq_n_disp > 0);
		eq->eq_n_disp--;
	}

	return 0;
}

int
daos_event_launch(struct daos_event *ev)
{
	struct daos_event_private	*evx = daos_ev2evx(ev);
	struct daos_eq_private		*eqx = NULL;
	int				  rc = 0;

	if (evx->evx_status != DAOS_EVS_INIT) {
		D_ERROR("Event status should be INIT: %d\n", evx->evx_status);
		return -DER_NO_PERM;
	}

	if (evx->evx_nchild > evx->evx_nchild_if + evx->evx_nchild_comp) {
		D_ERROR("Launch all children before launch the parent.\n");
		rc = -DER_NO_PERM;
		goto out;
	}

	if (!daos_handle_is_inval(evx->evx_eqh)) {
		eqx = daos_eq_lookup(evx->evx_eqh);
		if (eqx == NULL) {
			D_ERROR("Can't find eq from handle %"PRIu64"\n",
				evx->evx_eqh.cookie);
			return -DER_NONEXIST;
		}

		pthread_mutex_lock(&eqx->eqx_lock);
		if (eqx->eqx_finalizing) {
			D_ERROR("Event queue is in progress of finalizing\n");
			rc = -DER_NONEXIST;
			goto out;
		}
	}

	daos_event_launch_locked(eqx, evx);
	if (evx->evx_nchild > 0 &&
	    evx->evx_nchild == evx->evx_nchild_comp) {
		D_ASSERT(evx->evx_nchild_if == 0);
		daos_event_complete_locked(eqx, evx, rc);
	}
 out:
	if (eqx != NULL)
		pthread_mutex_unlock(&eqx->eqx_lock);

	if (eqx != NULL)
		daos_eq_putref(eqx);

	return rc;
}

void
daos_event_complete(struct daos_event *ev, int rc)
{
	struct daos_event_private	*evx = daos_ev2evx(ev);
	struct daos_eq_private		*eqx = NULL;

	if (!daos_handle_is_inval(evx->evx_eqh)) {
		eqx = daos_eq_lookup(evx->evx_eqh);
		D_ASSERT(eqx != NULL);

		pthread_mutex_lock(&eqx->eqx_lock);
	}

	D_ASSERT(evx->evx_status == DAOS_EVS_DISPATCH ||
		 evx->evx_status == DAOS_EVS_ABORT);

	daos_event_complete_locked(eqx, evx, rc);

	if (eqx != NULL)
		pthread_mutex_unlock(&eqx->eqx_lock);

	if (eqx != NULL)
		daos_eq_putref(eqx);
}

static int
ev_progress_cb(void *arg)
{
	struct daos_event *ev = arg;
	struct daos_event_private *evx = daos_ev2evx(ev);

	if (evx->evx_status == DAOS_EVS_COMPLETED ||
	    evx->evx_status == DAOS_EVS_ABORT) {
		evx->evx_status = DAOS_EVS_INIT;
		return 1;
	}

	return 0;
}

int
daos_event_test(struct daos_event *ev, int64_t timeout)
{
	struct daos_event_private *evx = daos_ev2evx(ev);

	/** pass the timeout to crt_progress() with a conditional callback */
	return crt_progress(evx->evx_ctx, timeout, ev_progress_cb, ev);
}

int
daos_eq_create(daos_handle_t *eqh)
{
	struct daos_eq_private	*eqx;
	struct daos_eq		*eq;
	int			 rc = 0;

	/** not thread-safe, but best effort */
	if (refcount == 0)
		return -DER_UNINIT;

	eq = daos_eq_alloc();
	if (eq == NULL)
		return -DER_NOMEM;

	eqx = daos_eq2eqx(eq);
	daos_eq_insert(eqx);
	eqx->eqx_ctx = daos_eq_ctx;
	daos_eq_handle(eqx, eqh);

	daos_eq_putref(eqx);
	return rc;
}

struct eq_progress_arg {
	struct daos_eq_private	 *eqx;
	unsigned int		  n_events;
	struct daos_event	**events;
	int			  wait_inf;
	int			  count;
};

static int
eq_progress_cb(void *arg)
{
	struct eq_progress_arg		*epa = (struct eq_progress_arg  *)arg;
	struct daos_event		*ev;
	struct daos_eq			*eq;
	struct daos_event_private	*evx;
	struct daos_event_private	*tmp;

	eq = daos_eqx2eq(epa->eqx);

	pthread_mutex_lock(&epa->eqx->eqx_lock);
	daos_list_for_each_entry_safe(evx, tmp, &eq->eq_comp, evx_link) {
		D_ASSERT(eq->eq_n_comp > 0);
		eq->eq_n_comp--;

		daos_list_del_init(&evx->evx_link);
		D_ASSERT(evx->evx_status == DAOS_EVS_COMPLETED ||
			 evx->evx_status == DAOS_EVS_ABORT);
		evx->evx_status = DAOS_EVS_INIT;

		if (epa->events != NULL) {
			ev = daos_evx2ev(evx);
			epa->events[epa->count++] = ev;
		}

		D_ASSERT(epa->count <= epa->n_events);
		if (epa->count == epa->n_events)
			break;
	}

	/* exit once there are completion events */
	if (epa->count > 0) {
		pthread_mutex_unlock(&epa->eqx->eqx_lock);
		return 1;
	}

	/* no completion event, eq::eq_comp is empty */
	if (epa->eqx->eqx_finalizing) { /* no new event is coming */
		D_ASSERT(daos_list_empty(&eq->eq_disp));
		pthread_mutex_unlock(&epa->eqx->eqx_lock);
		return -DER_NONEXIST;
	}

	/* wait only if there's inflight event? */
	if (epa->wait_inf && daos_list_empty(&eq->eq_disp)) {
		pthread_mutex_unlock(&epa->eqx->eqx_lock);
		return 1;
	}

	pthread_mutex_unlock(&epa->eqx->eqx_lock);

	/** continue waiting */
	return 0;
}

int
daos_eq_poll(daos_handle_t eqh, int wait_inf, int64_t timeout,
	     unsigned int n_events, struct daos_event **events)
{
	struct eq_progress_arg	epa;
	int			rc;

	if (n_events == 0)
		return -DER_INVAL;

	/** look up private eq */
	epa.eqx = daos_eq_lookup(eqh);
	if (epa.eqx == NULL)
		return -DER_NONEXIST;

	epa.n_events	= n_events;
	epa.events	= events;
	epa.wait_inf	= wait_inf;
	epa.count	= 0;

	/** pass the timeout to crt_progress() with a conditional callback */
	rc = crt_progress(epa.eqx->eqx_ctx, timeout, eq_progress_cb, &epa);

	/** drop ref grabbed in daos_eq_lookup() */
	daos_eq_putref(epa.eqx);

	if (rc != 0 && rc != -DER_TIMEDOUT) {
		D_ERROR("crt progress failed with %d\n", rc);
		return rc;
	}

	return epa.count;
}

int
daos_eq_query(daos_handle_t eqh, daos_eq_query_t query,
	      unsigned int n_events, struct daos_event **events)
{
	struct daos_eq_private		*eqx;
	struct daos_eq			*eq;
	struct daos_event_private	*evx;
	struct daos_event		*ev;
	int				 count;

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

static void
daos_event_abort_one(struct daos_event_private *evx)
{
	if (evx->evx_status != DAOS_EVS_DISPATCH)
		return;

	/* NB: ev::ev_error will be set by daos_event_complete(),
	 * so user can decide to not set error if operation has already
	 * finished while trying to abort */
	/* NB: always set ev_status to DAOS_EVS_ABORT even w/o callback,
	 * so aborted parent event can be marked as COMPLETE right after
	 * completion all launched events other than completion of all
	 * children. See daos_parent_event_can_complete for details. */
	evx->evx_status = DAOS_EVS_ABORT;
	daos_event_complete_cb(evx, -DER_CANCELED);
}

static void
daos_event_abort_locked(struct daos_eq_private *eqx,
			struct daos_event_private *evx)
{
	struct daos_event_private *child;

	D_ASSERT(evx->evx_status == DAOS_EVS_DISPATCH);

	daos_event_abort_one(evx);
	/* abort all children if he has */
	daos_list_for_each_entry(child, &evx->evx_child, evx_link)
		daos_event_abort_one(child);

	/* if aborted event is not a child event, move it to the
	 * head of launched list */
	if (evx->evx_parent == NULL && eqx != NULL) {
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
	struct daos_eq_private		*eqx;
	struct daos_eq			*eq;
	struct daos_event_private	*evx;
	struct daos_event_private	*tmp;
	int				 rc = 0;

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
	if (((flags & DAOS_EQ_DESTROY_FORCE) == 0) &&
	    (!daos_list_empty(&eq->eq_disp) ||
	     !daos_list_empty(&eq->eq_comp))) {
		rc = -DER_BUSY;
		goto out;
	}

	/* prevent other threads to launch new event */
	eqx->eqx_finalizing = 1;

	/* abort all inflight events */
	daos_list_for_each_entry_safe(evx, tmp, &eq->eq_disp, evx_link) {
		D_ASSERT(evx->evx_parent == NULL);
		daos_event_abort_locked(eqx, evx);
	}

	D_ASSERT(daos_list_empty(&eq->eq_disp));

	daos_list_for_each_entry_safe(evx, tmp, &eq->eq_comp, evx_link) {
		daos_list_del(&evx->evx_link);
		D_ASSERT(eq->eq_n_comp > 0);
		eq->eq_n_comp--;
	}
	eqx->eqx_ctx = NULL;
out:
	pthread_mutex_unlock(&eqx->eqx_lock);
	if (rc == 0)
		daos_eq_delete(eqx);
	daos_eq_putref(eqx);
	return rc;
}

int
daos_event_destroy_children(struct daos_event *ev, bool force);

/**
 * Destroy events and all of its sub-events
 **/
int
daos_event_destroy(struct daos_event *ev, bool force)
{
	struct daos_event_private	*evp = daos_ev2evx(ev);
	int				 rc = 0;

	if (!force && evp->evx_status == DAOS_EVS_DISPATCH)
		return -DER_BUSY;

	if (daos_list_empty(&evp->evx_child)) {
		D_ASSERT(daos_list_empty(&evp->evx_link));
		D_FREE_PTR(ev);
		return rc;
	}

	rc = daos_event_destroy_children(ev, force);
	if (rc == 0)
		D_FREE_PTR(ev);

	return rc;
}

int
daos_event_destroy_children(struct daos_event *ev, bool force)
{
	struct daos_event_private	*evp = daos_ev2evx(ev);
	struct daos_event_private	*sub_evx;
	struct daos_event_private	*tmp;
	int				 rc = 0;

	/* Destroy all of sub events */
	daos_list_for_each_entry_safe(sub_evx, tmp, &evp->evx_child,
				      evx_link) {
		struct daos_event *sub_ev = daos_evx2ev(sub_evx);
		daos_ev_status_t ev_status = sub_evx->evx_status;

		daos_list_del_init(&sub_evx->evx_link);
		rc = daos_event_destroy(sub_ev, force);
		if (rc != 0) {
			daos_list_add(&sub_evx->evx_link,
				      &evp->evx_child);
			break;
		}
		if (ev_status == DAOS_EVS_COMPLETED)
			evp->evx_nchild_comp--;
		else if (ev_status == DAOS_EVS_DISPATCH)
			evp->evx_nchild_if--;
		evp->evx_nchild--;
	}

	return rc;
}

/**
 * Add the event to the event queue, and if there is parent, add
 * it to its child list as well.
 */
int
daos_event_init(struct daos_event *ev, daos_handle_t eqh,
		struct daos_event *parent)
{
	struct daos_event_private	*evx = daos_ev2evx(ev);
	struct daos_event_private	*parent_evx;
	struct daos_eq_private		*eqx;
	int				rc = 0;

	D_CASSERT(sizeof(ev->ev_private) >= sizeof(*evx));

	/* Init the event first */
	memset(ev, 0, sizeof(*ev));
	evx->evx_status	= DAOS_EVS_INIT;
	DAOS_INIT_LIST_HEAD(&evx->evx_child);
	DAOS_INIT_LIST_HEAD(&evx->evx_link);
	DAOS_INIT_LIST_HEAD(&evx->evx_callback.evx_comp_list);

	if (parent != NULL) {
		/* if there is parent */
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
		evx->evx_eqh	= parent_evx->evx_eqh;
		evx->evx_ctx	= parent_evx->evx_ctx;
		evx->evx_parent	= parent_evx;
		parent_evx->evx_nchild++;
	} else if (!daos_handle_is_inval(eqh)) {
		/* if there is event queue */
		evx->evx_eqh = eqh;
		eqx = daos_eq_lookup(eqh);
		if (eqx == NULL) {
			D_ERROR("Invalid EQ handle %"PRIx64"\n", eqh.cookie);
			return -DER_NONEXIST;
		}
		/* inherit transport context from event queue */
		evx->evx_ctx = eqx->eqx_ctx;
		daos_eq_putref(eqx);
	} else {
		evx->evx_ctx = daos_eq_ctx;
	}

	return rc;
}

/**
 * Unlink events from various list, parent_list, child list,
 * and event queue hash list, and destroy all of the child
 * events
 **/
int
daos_event_fini(struct daos_event *ev)
{
	struct daos_event_private	*evx = daos_ev2evx(ev);
	struct daos_eq_private		*eqx = NULL;
	struct daos_eq			*eq = NULL;
	int				 rc = 0;

	if (!daos_handle_is_inval(evx->evx_eqh)) {
		eqx = daos_eq_lookup(evx->evx_eqh);
		if (eqx == NULL)
			return -DER_NONEXIST;
		eq = daos_eqx2eq(eqx);
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
		rc = daos_event_fini(daos_evx2ev(tmp));
		if (rc < 0)
			goto out;
		tmp->evx_status = DAOS_EVS_INIT;
		tmp->evx_parent = NULL;
	}

	/* If it is a child event, delete it from parent list */
	if (evx->evx_parent != NULL) {
		if (daos_list_empty(&evx->evx_link)) {
			D_ERROR("Event not linked to its parent\n");
			return -DER_INVAL;
		}

		if (evx->evx_parent->evx_status != DAOS_EVS_INIT) {
			D_ERROR("Parent event not init or inflight: %d\n",
				evx->evx_parent->evx_status);
			return -DER_INVAL;
		}

		daos_list_del_init(&evx->evx_link);
		evx->evx_status = DAOS_EVS_INIT;
		evx->evx_parent = NULL;
		evx->evx_ctx = NULL;
	}

	/* Make sure the scheduler has been finished */
	D_ASSERT(daos_ev2sched(ev)->ds_event == NULL);

	/* Remove from the evx_link */
	if (!daos_list_empty(&evx->evx_link)) {
		daos_list_del(&evx->evx_link);
		if (evx->evx_status == DAOS_EVS_DISPATCH && eq != NULL) {
			eq->eq_n_disp--;
		} else if (evx->evx_status == DAOS_EVS_COMPLETED &&
			   eq != NULL) {
			D_ASSERTF(eq->eq_n_comp > 0, "eq %p\n", eq);
			eq->eq_n_comp--;
		}
	}

	evx->evx_ctx = NULL;
out:
	if (eq != NULL)
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
	struct daos_event_private	*evx = daos_ev2evx(ev);
	struct daos_eq_private		*eqx = NULL;

	if (!daos_handle_is_inval(evx->evx_eqh)) {
		eqx = daos_eq_lookup(evx->evx_eqh);
		if (eqx == NULL) {
			D_ERROR("Invalid EQ handle %"PRIu64"\n",
				evx->evx_eqh.cookie);
			return -DER_NONEXIST;
		}
		pthread_mutex_lock(&eqx->eqx_lock);
	}

	daos_event_abort_locked(eqx, evx);

	if (eqx != NULL) {
		pthread_mutex_unlock(&eqx->eqx_lock);
		daos_eq_putref(eqx);
	}

	return 0;
}

int
daos_event_priv_get(daos_event_t **ev)
{
	int rc;

	D_ASSERT(*ev == NULL);

	if (!ev_thpriv_is_init) {
		rc = daos_event_init(&ev_thpriv, DAOS_HDL_INVAL, NULL);
		if (rc)
			return rc;
		ev_thpriv_is_init = true;
	}

	*ev = &ev_thpriv;
	return 0;
}

bool
daos_event_is_priv(daos_event_t *ev)
{
	return (ev == &ev_thpriv);
}

int
daos_event_priv_wait()
{
	int rc;

	D_ASSERT(ev_thpriv_is_init);

	/* Wait the event to complete */
	rc = daos_event_test(&ev_thpriv, DAOS_EQ_WAIT);
	if (rc == 0)
		rc = ev_thpriv.ev_error;

	return rc;
}
