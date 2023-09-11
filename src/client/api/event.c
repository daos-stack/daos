/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * This file is part of client DAOS library.
 *
 * client/event.c
 *
 * Author: Liang Zhen  <liang.zhen@intel.com>
 * Author: Di Wang  <di.wang@intel.com>
 */
#define D_LOGFAC	DD_FAC(client)

#include "client_internal.h"
#include <daos/rpc.h>

/** thread-private event */
static __thread daos_event_t	ev_thpriv;
static __thread bool		ev_thpriv_is_init;

/**
 * Global progress timeout for synchronous operation
 * busy-polling by default (0), timeout in us otherwise
 */
static uint32_t ev_prog_timeout;

#define EQ_WITH_CRT

#if !defined(EQ_WITH_CRT)

#define crt_init(a,b,c)			({0;})
#define crt_finalize()			({0;})
#define crt_context_create(a, b)	({0;})
#define crt_context_destroy(a, b)	({0;})
#define crt_progress_cond(ctx, timeout, cb, args)	\
({							\
	int __rc = cb(args);				\
							\
	while ((timeout) != 0 && __rc == 0) {		\
		sleep(1);				\
		__rc = cb(args);			\
		if ((timeout) < 0)			\
			continue;			\
		if ((timeout) < 1000000)		\
			break;				\
		(timeout) -= 1000000;			\
	}						\
	0;						\
})
#endif

/*
 * For the moment, we use a global crt_context_t to create all the RPC requests
 * this module uses.
 */
static crt_context_t daos_eq_ctx;
static pthread_mutex_t daos_eq_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int eq_ref;

/*
 * Pointer to global scheduler for events not part of an EQ. Events initialized
 * as part of an EQ will be tracked in that EQ scheduler.
 */
static tse_sched_t daos_sched_g;

int
daos_eq_lib_init()
{
	int rc;

	D_MUTEX_LOCK(&daos_eq_lock);
	if (eq_ref > 0) {
		eq_ref++;
		D_GOTO(unlock, rc = 0);
	}

	rc = crt_init_opt(NULL, 0, daos_crt_init_opt_get(false, 1));
	if (rc != 0) {
		D_ERROR("failed to initialize crt: "DF_RC"\n", DP_RC(rc));
		D_GOTO(unlock, rc);
	}

	/* use a global shared context for all eq for now */
	rc = crt_context_create(&daos_eq_ctx);
	if (rc != 0) {
		D_ERROR("failed to create client context: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(crt, rc);
	}

	/** set up scheduler for non-eq events */
	rc = tse_sched_init(&daos_sched_g, NULL, daos_eq_ctx);
	if (rc != 0)
		D_GOTO(crt, rc);

	eq_ref = 1;

	d_getenv_int("D_POLL_TIMEOUT", &ev_prog_timeout);

unlock:
	D_MUTEX_UNLOCK(&daos_eq_lock);
	return rc;
crt:
	crt_finalize();
	D_GOTO(unlock, rc);
}

int
daos_eq_lib_fini()
{
	int rc;

	if (daos_eq_ctx != NULL) {
		rc = crt_context_destroy(daos_eq_ctx, 1 /* force */);
		if (rc != 0) {
			D_ERROR("failed to destroy client context: "DF_RC"\n",
				DP_RC(rc));
			return rc;
		}
		daos_eq_ctx = NULL;
	}

	D_MUTEX_LOCK(&daos_eq_lock);
	if (eq_ref == 0)
		D_GOTO(unlock, rc = -DER_UNINIT);
	if (eq_ref > 1) {
		eq_ref--;
		D_GOTO(unlock, rc = 0);
	}
	ev_thpriv_is_init = false;

	tse_sched_complete(&daos_sched_g, 0, true);

	rc = crt_finalize();
	if (rc != 0) {
		D_ERROR("failed to shutdown crt: "DF_RC"\n", DP_RC(rc));
		D_GOTO(unlock, rc);
	}

	eq_ref = 0;
unlock:
	D_MUTEX_UNLOCK(&daos_eq_lock);
	return rc;
}

static void
daos_eq_free(struct d_hlink *hlink)
{
	struct daos_eq_private	*eqx;
	struct daos_eq		*eq;

	eqx = container_of(hlink, struct daos_eq_private, eqx_hlink);
	eq = daos_eqx2eq(eqx);
	D_ASSERT(d_list_empty(&eq->eq_running));
	D_ASSERT(d_list_empty(&eq->eq_comp));
	D_ASSERTF(eq->eq_n_comp == 0 && eq->eq_n_running == 0,
		  "comp %d running %d\n", eq->eq_n_comp, eq->eq_n_running);
	D_ASSERT(daos_hhash_link_empty(&eqx->eqx_hlink));

	if (eqx->eqx_lock_init)
		D_MUTEX_DESTROY(&eqx->eqx_lock);

	D_FREE(eq);
}

crt_context_t
daos_get_crt_ctx()
{
	return daos_eq_ctx;
}

struct d_hlink_ops	eq_h_ops = {
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

	D_INIT_LIST_HEAD(&eq->eq_running);
	D_INIT_LIST_HEAD(&eq->eq_comp);
	eq->eq_n_running = 0;
	eq->eq_n_comp = 0;

	eqx = daos_eq2eqx(eq);

	rc = D_MUTEX_INIT(&eqx->eqx_lock, NULL);
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
	struct d_hlink *hlink;

	hlink = daos_hhash_link_lookup(eqh.cookie);
	if (hlink == NULL)
		return NULL;

	return container_of(hlink, struct daos_eq_private, eqx_hlink);
}

static void
daos_eq_putref(struct daos_eq_private *eqx)
{
	daos_hhash_link_putref(&eqx->eqx_hlink);
}

static void
daos_eq_delete(struct daos_eq_private *eqx)
{
	daos_hhash_link_delete(&eqx->eqx_hlink);
}

static void
daos_eq_insert(struct daos_eq_private *eqx)
{
	daos_hhash_link_insert(&eqx->eqx_hlink, DAOS_HTYPE_EQ);
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

	evx->evx_status = DAOS_EVS_RUNNING;
	if (evx->evx_parent != NULL) {
		evx->evx_parent->evx_nchild_running++;
		return;
	}

	if (eq != NULL) {
		d_list_add_tail(&evx->evx_link, &eq->eq_running);
		eq->eq_n_running++;
	}
}

crt_context_t
daos_ev2ctx(struct daos_event *ev)
{
	return daos_ev2evx(ev)->evx_ctx;
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

	D_ALLOC_PTR(ecl);
	if (ecl == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&ecl->op_comp_list);
	ecl->op_comp_arg = arg;
	ecl->op_comp_cb = cb;

	d_list_add_tail(&ecl->op_comp_list, &evx->evx_callback.evx_comp_list);

	return 0;
}

static int
daos_event_complete_cb(struct daos_event_private *evx, int rc)
{
	struct daos_event_comp_list	*ecl;
	struct daos_event_comp_list	*tmp;
	int				 ret = rc;
	int				 err;

	d_list_for_each_entry_safe(ecl, tmp, &evx->evx_callback.evx_comp_list,
				   op_comp_list) {
		d_list_del_init(&ecl->op_comp_list);
		err = ecl->op_comp_cb(ecl->op_comp_arg, daos_evx2ev(evx), rc);
		D_FREE(ecl);
		if (ret == 0)
			ret = err;
	}

	return ret;
}

void
daos_event_errno_rc(struct daos_event *ev)
{
	struct daos_event_private *evx = daos_ev2evx(ev);

	evx->is_errno = 1;
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

	rc = daos_event_complete_cb(evx, rc);
	if (evx->is_errno)
		ev->ev_error = daos_der2errno(rc);
	else
		ev->ev_error = rc;

	atomic_store(&evx->evx_status, DAOS_EVS_COMPLETED);

	if (parent_evx != NULL) {
		daos_event_t *parent_ev = daos_evx2ev(parent_evx);

		D_ASSERT(parent_evx->evx_nchild_running > 0);
		parent_evx->evx_nchild_running--;

		D_ASSERT(parent_evx->evx_nchild_comp <
			 parent_evx->evx_nchild);
		parent_evx->evx_nchild_comp++;

		if (parent_evx->evx_nchild_comp < parent_evx->evx_nchild) {
			/* Not all children have completed yet */
			parent_ev->ev_error = parent_ev->ev_error ?: rc;
			goto out;
		}

		/* If the parent is not launched yet, let's return */
		if (parent_evx->evx_status == DAOS_EVS_READY)
			goto out;

		/* If the parent was completed or aborted, we can return */
		if (parent_evx->evx_status == DAOS_EVS_COMPLETED ||
		    parent_evx->evx_status == DAOS_EVS_ABORTED)
			goto out;

		/* If the parent is not a barrier it will complete on its own */
		if (!parent_evx->is_barrier)
			goto out;

		/* Complete the barrier parent */
		D_ASSERT(parent_evx->evx_status == DAOS_EVS_RUNNING);
		rc = daos_event_complete_cb(parent_evx, rc);
		atomic_store(&parent_evx->evx_status, DAOS_EVS_COMPLETED);
		parent_ev->ev_error = parent_ev->ev_error ?: rc;
		evx = parent_evx;
	}

	if (eq != NULL) {
		D_ASSERT(!d_list_empty(&evx->evx_link));
		d_list_move_tail(&evx->evx_link, &eq->eq_comp);
		eq->eq_n_comp++;
		D_ASSERT(eq->eq_n_running > 0);
		eq->eq_n_running--;
	}

out:
	return 0;
}

int
daos_event_launch(struct daos_event *ev)
{
	struct daos_event_private	*evx = daos_ev2evx(ev);
	struct daos_eq_private		*eqx = NULL;
	int				  rc = 0;

	if (atomic_load(&evx->evx_status) != DAOS_EVS_READY) {
		D_ERROR("Event status should be INIT: %d\n", evx->evx_status);
		return -DER_NO_PERM;
	}

	if (evx->evx_nchild > evx->evx_nchild_running + evx->evx_nchild_comp) {
		D_ERROR("Launch all children before launch the parent.\n");
		rc = -DER_NO_PERM;
		goto out;
	}

	if (daos_handle_is_valid(evx->evx_eqh)) {
		eqx = daos_eq_lookup(evx->evx_eqh);
		if (eqx == NULL) {
			D_ERROR("Can't find eq from handle %"PRIu64"\n", evx->evx_eqh.cookie);
			return -DER_NONEXIST;
		}

		D_MUTEX_LOCK(&eqx->eqx_lock);
		if (eqx->eqx_finalizing) {
			D_ERROR("Event queue is in progress of finalizing\n");
			rc = -DER_NONEXIST;
			goto out;
		}
	} else {
		D_MUTEX_LOCK(&evx->evx_lock);
	}

	daos_event_launch_locked(eqx, evx);

	/*
	 * If all child events completed before a barrier parent was launched,
	 * complete the parent.
	 */
	if (evx->is_barrier && evx->evx_nchild > 0 &&
	    evx->evx_nchild == evx->evx_nchild_comp) {
		D_ASSERT(evx->evx_nchild_running == 0);
		daos_event_complete_locked(eqx, evx, rc);
	}
out:
	if (eqx != NULL) {
		D_MUTEX_UNLOCK(&eqx->eqx_lock);
		daos_eq_putref(eqx);
	} else {
		D_MUTEX_UNLOCK(&evx->evx_lock);
	}

	return rc;
}

int
daos_event_parent_barrier(struct daos_event *ev)
{
	struct daos_event_private	*evx = daos_ev2evx(ev);

	if (evx->evx_nchild == 0) {
		D_ERROR("Can't start a parent event with no children\n");
		return -DER_INVAL;
	}

	/*
	 * Mark this event as a barrier event to be completed by the last
	 * completing child.
	 */
	evx->is_barrier = 1;

	return daos_event_launch(ev);
}

void
daos_event_complete(struct daos_event *ev, int rc)
{
	struct daos_event_private	*evx = daos_ev2evx(ev);
	struct daos_eq_private		*eqx = NULL;

	if (daos_handle_is_valid(evx->evx_eqh)) {
		eqx = daos_eq_lookup(evx->evx_eqh);
		D_ASSERT(eqx != NULL);

		D_MUTEX_LOCK(&eqx->eqx_lock);
	} else {
		D_MUTEX_LOCK(&evx->evx_lock);
	}

	if (evx->evx_status == DAOS_EVS_READY || evx->evx_status == DAOS_EVS_COMPLETED ||
	    evx->evx_status == DAOS_EVS_ABORTED)
		goto out;

	D_ASSERT(evx->evx_status == DAOS_EVS_RUNNING);

	daos_event_complete_locked(eqx, evx, rc);

out:
	if (eqx != NULL) {
		D_MUTEX_UNLOCK(&eqx->eqx_lock);
		daos_eq_putref(eqx);
	} else {
		D_MUTEX_UNLOCK(&evx->evx_lock);
	}
}

struct ev_progress_arg {
	struct daos_eq_private		*eqx;
	struct daos_event_private	*evx;
};

static int
ev_progress_cb(void *arg)
{
	struct ev_progress_arg		*epa = (struct ev_progress_arg  *)arg;
	struct daos_event_private       *evx = epa->evx;
	struct daos_eq_private		*eqx = epa->eqx;
	int				rc;

	tse_sched_progress(evx->evx_sched);

	if (daos_handle_is_inval(evx->evx_eqh))
		D_MUTEX_LOCK(&evx->evx_lock);
	else
		D_MUTEX_LOCK(&eqx->eqx_lock);

	/** If another thread progressed this, get out now. */
	if (evx->evx_status == DAOS_EVS_READY)
		D_GOTO(unlock, rc = 1);

	/** Event is still in-flight */
	if (evx->evx_status != DAOS_EVS_COMPLETED && evx->evx_status != DAOS_EVS_ABORTED)
		D_GOTO(unlock, rc = 0);

	/** If there are children in flight, then return in-flight */
	if (evx->evx_nchild_running > 0)
		D_GOTO(unlock, rc = 0);

	/** Change status of event to INIT only if event is not in EQ and get out. */
	if (daos_handle_is_inval(evx->evx_eqh)) {
		if (evx->evx_status == DAOS_EVS_COMPLETED || evx->evx_status == DAOS_EVS_ABORTED)
			evx->evx_status = DAOS_EVS_READY;
		D_GOTO(unlock, rc = 1);
	}

	/** if the EQ was finalized from under us, just update the event status and return. */
	if (eqx->eqx_finalizing) {
		evx->evx_status = DAOS_EVS_READY;
		D_ASSERT(d_list_empty(&evx->evx_link));
		D_GOTO(unlock, rc = 1);
	}

	/*
	 * Check again if the event is still in completed state, then remove it from the event
	 * queue.
	 */
	if (evx->evx_status == DAOS_EVS_COMPLETED || evx->evx_status == DAOS_EVS_ABORTED) {
		struct daos_eq *eq = daos_eqx2eq(eqx);

		evx->evx_status = DAOS_EVS_READY;
		D_ASSERT(eq->eq_n_comp > 0);
		eq->eq_n_comp--;
		d_list_del_init(&evx->evx_link);
	}
	rc = 1;
	D_ASSERT(evx->evx_status == DAOS_EVS_READY);

unlock:
	if (daos_handle_is_inval(evx->evx_eqh))
		D_MUTEX_UNLOCK(&evx->evx_lock);
	else
		D_MUTEX_UNLOCK(&eqx->eqx_lock);
	return rc;
}

int
daos_event_test(struct daos_event *ev, int64_t timeout, bool *flag)
{
	struct ev_progress_arg		epa;
	struct daos_event_private	*evx = daos_ev2evx(ev);
	int				rc;

	/** Can't call test on a Child event */
	if (evx->evx_parent != NULL)
		return -DER_NO_PERM;

	epa.evx = evx;
	epa.eqx = NULL;

	if (daos_handle_is_valid(evx->evx_eqh)) {
		epa.eqx = daos_eq_lookup(evx->evx_eqh);
		if (epa.eqx == NULL) {
			D_ERROR("Can't find eq from handle %"PRIu64"\n", evx->evx_eqh.cookie);
			return -DER_NONEXIST;
		}
	}

	/* pass the timeout to crt_progress() with a conditional callback */
	rc = crt_progress_cond(evx->evx_ctx, timeout, ev_progress_cb, &epa);

	/** drop ref grabbed in daos_eq_lookup() */
	if (epa.eqx)
		daos_eq_putref(epa.eqx);

	if (rc != 0 && rc != -DER_TIMEDOUT) {
		D_ERROR("crt progress failed with "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (atomic_load(&evx->evx_status) == DAOS_EVS_READY)
		*flag = true;
	else
		*flag = false;

	return 0;
}

int
daos_eq_create(daos_handle_t *eqh)
{
	struct daos_eq_private	*eqx;
	struct daos_eq		*eq;
	int			rc = 0;

	/** not thread-safe, but best effort */
	D_MUTEX_LOCK(&daos_eq_lock);
	if (eq_ref == 0) {
		D_MUTEX_UNLOCK(&daos_eq_lock);
		return -DER_UNINIT;
	}
	D_MUTEX_UNLOCK(&daos_eq_lock);

	eq = daos_eq_alloc();
	if (eq == NULL)
		return -DER_NOMEM;

	eqx = daos_eq2eqx(eq);

	rc = crt_context_create(&eqx->eqx_ctx);
	if (rc) {
		D_WARN("Failed to create CART context; using the global one, "DF_RC"\n", DP_RC(rc));
		eqx->eqx_ctx = daos_eq_ctx;
	}

	daos_eq_insert(eqx);
	daos_eq_handle(eqx, eqh);

	rc = tse_sched_init(&eqx->eqx_sched, NULL, eqx->eqx_ctx);

	daos_eq_putref(eqx);
	return rc;
}

struct eq_progress_arg {
	struct daos_eq_private	 *eqx;
	unsigned int		  n_events;
	struct daos_event	**events;
	int			  wait_running;
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

	tse_sched_progress(&epa->eqx->eqx_sched);

	D_MUTEX_LOCK(&epa->eqx->eqx_lock);
	d_list_for_each_entry_safe(evx, tmp, &eq->eq_comp, evx_link) {
		D_ASSERT(eq->eq_n_comp > 0);

		/** don't poll out a parent if it has in-flight events */
		if (evx->evx_nchild_running > 0)
			continue;

		eq->eq_n_comp--;

		d_list_del_init(&evx->evx_link);
		D_ASSERT(evx->evx_status == DAOS_EVS_COMPLETED ||
			 evx->evx_status == DAOS_EVS_ABORTED);
		evx->evx_status = DAOS_EVS_READY;

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
		D_MUTEX_UNLOCK(&epa->eqx->eqx_lock);
		return 1;
	}

	/* no completion event, eq::eq_comp is empty */
	if (epa->eqx->eqx_finalizing) { /* no new event is coming */
		D_ASSERT(d_list_empty(&eq->eq_running));
		D_MUTEX_UNLOCK(&epa->eqx->eqx_lock);
		D_ERROR("EQ Progress called while EQ is finalizing\n");
		return -DER_NONEXIST;
	}

	/* wait only if there are running events? */
	if (epa->wait_running && d_list_empty(&eq->eq_running)) {
		D_MUTEX_UNLOCK(&epa->eqx->eqx_lock);
		return 1;
	}

	D_MUTEX_UNLOCK(&epa->eqx->eqx_lock);

	/** continue waiting */
	return 0;
}

int
daos_eq_poll(daos_handle_t eqh, int wait_running, int64_t timeout,
	     unsigned int n_events, struct daos_event **events)
{
	struct eq_progress_arg	epa;
	int			rc;

	if (n_events == 0 || events == NULL)
		return -DER_INVAL;

	/** look up private eq */
	epa.eqx = daos_eq_lookup(eqh);
	if (epa.eqx == NULL) {
		D_ERROR("Invalid EQ handle %"PRIu64"\n", eqh.cookie);
		return -DER_NONEXIST;
	}

	epa.n_events	= n_events;
	epa.events	= events;
	epa.wait_running = wait_running;
	epa.count	= 0;

	/* pass the timeout to crt_progress() with a conditional callback */
	rc = crt_progress_cond(epa.eqx->eqx_ctx, timeout, eq_progress_cb, &epa);

	/* drop ref grabbed in daos_eq_lookup() */
	daos_eq_putref(epa.eqx);

	if (rc != 0 && rc != -DER_TIMEDOUT) {
		D_ERROR("crt progress failed with "DF_RC"\n", DP_RC(rc));
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
	if (eqx == NULL) {
		D_ERROR("Invalid EQ handle %"PRIu64"\n", eqh.cookie);
		return -DER_NONEXIST;
	}

	eq = daos_eqx2eq(eqx);

	count = 0;
	D_MUTEX_LOCK(&eqx->eqx_lock);

	if (n_events == 0 || events == NULL) {
		if ((query & DAOS_EQR_COMPLETED) != 0)
			count += eq->eq_n_comp;

		if ((query & DAOS_EQR_WAITING) != 0)
			count += eq->eq_n_running;
		goto out;
	}

	if ((query & DAOS_EQR_COMPLETED) != 0) {
		d_list_for_each_entry(evx, &eq->eq_comp, evx_link) {
			ev = daos_evx2ev(evx);
			events[count++] = ev;
			if (count == n_events)
				goto out;
		}
	}

	if ((query & DAOS_EQR_WAITING) != 0) {
		d_list_for_each_entry(evx, &eq->eq_running, evx_link) {
			ev = daos_evx2ev(evx);
			events[count++] = ev;
			if (count == n_events)
				goto out;
		}
	}
out:
	D_MUTEX_UNLOCK(&eqx->eqx_lock);
	daos_eq_putref(eqx);
	return count;
}

static int
daos_event_abort_locked(struct daos_eq_private *eqx,
			struct daos_event_private *evx)
{
	if (evx->evx_status != DAOS_EVS_RUNNING)
		return -DER_NO_PERM;

	/** Since we don't support task and RPC abort, this is a no-op for now */
	return 0;
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
	if (eqx == NULL) {
		D_ERROR("eqh nonexist.\n");
		return -DER_NONEXIST;
	}

	D_MUTEX_LOCK(&eqx->eqx_lock);

	if (eqx->eqx_finalizing) {
		D_ERROR("eqx_finalizing.\n");
		rc = -DER_NONEXIST;
		goto out;
	}

	eq = daos_eqx2eq(eqx);

	/* If it is not force destroyed, then we need check if
	 * there are still events linked here */
	if (((flags & DAOS_EQ_DESTROY_FORCE) == 0) &&
	    (!d_list_empty(&eq->eq_running) ||
	     !d_list_empty(&eq->eq_comp))) {
		rc = -DER_BUSY;
		goto out;
	}

	/* prevent other threads to launch new event */
	eqx->eqx_finalizing = 1;

	D_MUTEX_UNLOCK(&eqx->eqx_lock);

	/** Flush the tasks for this EQ */
	if (eqx->eqx_ctx != NULL) {
		rc = crt_context_flush(eqx->eqx_ctx, 0);
		if (rc != 0) {
			D_ERROR("failed to flush client context: "DF_RC"\n",
				DP_RC(rc));
			return rc;
		}
	}

	D_MUTEX_LOCK(&eqx->eqx_lock);

	/* abort all launched events */
	d_list_for_each_entry_safe(evx, tmp, &eq->eq_running, evx_link) {
		D_ASSERT(evx->evx_parent == NULL);
		rc = daos_event_abort_locked(eqx, evx);
		if (rc) {
			D_ERROR("Failed to abort event\n");
			goto out;
		}
	}

	D_ASSERT(d_list_empty(&eq->eq_running));

	d_list_for_each_entry_safe(evx, tmp, &eq->eq_comp, evx_link) {
		d_list_del(&evx->evx_link);
		D_ASSERT(eq->eq_n_comp > 0);
		eq->eq_n_comp--;
	}

	tse_sched_complete(&eqx->eqx_sched, rc, true);

	/** destroy the EQ cart context only if it's not the global one */
	if (eqx->eqx_ctx != daos_eq_ctx) {
		rc = crt_context_destroy(eqx->eqx_ctx, (flags & DAOS_EQ_DESTROY_FORCE));
		if (rc) {
			D_ERROR("Failed to destroy CART context for EQ: " DF_RC "\n", DP_RC(rc));
			goto out;
		}
	}
	eqx->eqx_ctx = NULL;

out:
	D_MUTEX_UNLOCK(&eqx->eqx_lock);
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

	if (!force && atomic_load(&evp->evx_status) == DAOS_EVS_RUNNING)
		return -DER_BUSY;

	if (d_list_empty(&evp->evx_child)) {
		D_ASSERT(d_list_empty(&evp->evx_link));
		D_FREE(ev);
		return rc;
	}

	rc = daos_event_destroy_children(ev, force);
	if (rc == 0)
		D_FREE(ev);

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
	d_list_for_each_entry_safe(sub_evx, tmp, &evp->evx_child,
				   evx_link) {
		struct daos_event *sub_ev = daos_evx2ev(sub_evx);
		daos_ev_status_t ev_status = atomic_load(&sub_evx->evx_status);

		d_list_del_init(&sub_evx->evx_link);
		rc = daos_event_destroy(sub_ev, force);
		if (rc != 0) {
			d_list_add(&sub_evx->evx_link,
				   &evp->evx_child);
			break;
		}
		if (ev_status == DAOS_EVS_COMPLETED)
			evp->evx_nchild_comp--;
		else if (ev_status == DAOS_EVS_RUNNING)
			evp->evx_nchild_running--;
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
	atomic_init(&evx->evx_status, DAOS_EVS_READY);
	D_INIT_LIST_HEAD(&evx->evx_child);
	D_INIT_LIST_HEAD(&evx->evx_link);
	D_INIT_LIST_HEAD(&evx->evx_callback.evx_comp_list);

	if (parent != NULL) {
		/* if there is parent */
		/* Insert it to the parent event list */
		parent_evx = daos_ev2evx(parent);

		if (parent_evx->evx_status != DAOS_EVS_READY) {
			D_ERROR("Parent event is not initialized or is already "
				"running/aborted: %d\n",
				parent_evx->evx_status);
			return -DER_INVAL;
		}

		if (parent_evx->evx_parent != NULL) {
			D_ERROR("Can't nest event\n");
			return -DER_NO_PERM;
		}

		/* it's user's responsibility to protect this list */
		d_list_add_tail(&evx->evx_link, &parent_evx->evx_child);
		evx->evx_eqh	= parent_evx->evx_eqh;
		evx->evx_ctx	= parent_evx->evx_ctx;
		evx->evx_sched	= parent_evx->evx_sched;
		evx->evx_parent	= parent_evx;
		parent_evx->evx_nchild++;
	} else if (daos_handle_is_valid(eqh)) {
		/* if there is event queue */
		evx->evx_eqh = eqh;
		eqx = daos_eq_lookup(eqh);
		if (eqx == NULL) {
			D_ERROR("Invalid EQ handle %"PRIx64"\n", eqh.cookie);
			return -DER_NONEXIST;
		}
		/* inherit transport context from event queue */
		evx->evx_ctx = eqx->eqx_ctx;
		evx->evx_sched = &eqx->eqx_sched;
		daos_eq_putref(eqx);
	} else {
		if (daos_sched_g.ds_udata == NULL) {
			D_ERROR("The DAOS client library is not initialized: "DF_RC"\n",
				DP_RC(-DER_UNINIT));
			return -DER_UNINIT;
		}
		evx->evx_ctx = daos_eq_ctx;
		evx->evx_sched = &daos_sched_g;
	}

	if (daos_handle_is_inval(evx->evx_eqh)) {
		/** since there is no EQ, initialize the evx lock */
		rc = D_MUTEX_INIT(&evx->evx_lock, NULL);
		if (rc)
			return rc;
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

	if (daos_handle_is_valid(evx->evx_eqh)) {
		eqx = daos_eq_lookup(evx->evx_eqh);
		if (eqx == NULL) {
			D_ERROR("Invalid EQ handle %"PRIu64"\n", evx->evx_eqh.cookie);
			return -DER_NONEXIST;
		}
		eq = daos_eqx2eq(eqx);
		D_MUTEX_LOCK(&eqx->eqx_lock);
	}

	if (evx->evx_status == DAOS_EVS_RUNNING) {
		rc = -DER_BUSY;
		goto out;
	}

	/** destroy the event lock if there is not event queue or this is a child event */
	if (daos_handle_is_inval(evx->evx_eqh))
		D_MUTEX_DESTROY(&evx->evx_lock);

	/* If there are child events */
	while (!d_list_empty(&evx->evx_child)) {
		struct daos_event_private *tmp;

		tmp = d_list_entry(evx->evx_child.next,
				   struct daos_event_private, evx_link);
		D_ASSERTF(tmp->evx_status == DAOS_EVS_READY ||
			  tmp->evx_status == DAOS_EVS_COMPLETED ||
			  tmp->evx_status == DAOS_EVS_ABORTED,
			 "EV %p status: %d\n", tmp, tmp->evx_status);

		if (tmp->evx_status != DAOS_EVS_READY &&
		    tmp->evx_status != DAOS_EVS_COMPLETED &&
		    tmp->evx_status != DAOS_EVS_ABORTED) {
			D_ERROR("Child event %p launched: %d\n", daos_evx2ev(tmp), tmp->evx_status);
			rc = -DER_INVAL;
			goto out;
		}

		if (eqx != NULL)
			D_MUTEX_UNLOCK(&eqx->eqx_lock);

		rc = daos_event_fini(daos_evx2ev(tmp));
		if (rc < 0) {
			D_ERROR("Failed to finalize child event "DF_RC"\n", DP_RC(rc));
			goto out_unlocked;
		}

		if (eqx != NULL)
			D_MUTEX_LOCK(&eqx->eqx_lock);

		tmp->evx_status = DAOS_EVS_READY;
		tmp->evx_parent = NULL;
	}

	/* If it is a child event, delete it from parent list */
	if (evx->evx_parent != NULL) {
		if (d_list_empty(&evx->evx_link)) {
			D_ERROR("Event not linked to its parent\n");
			rc = -DER_INVAL;
			goto out;
		}

		if (evx->evx_parent->evx_status != DAOS_EVS_READY) {
			D_ERROR("Parent event not init or launched: %d\n",
				evx->evx_parent->evx_status);
			rc = -DER_INVAL;
			goto out;
		}

		d_list_del_init(&evx->evx_link);
		evx->evx_status = DAOS_EVS_READY;
		evx->evx_parent = NULL;
		evx->evx_ctx = NULL;
	}

	/* Remove from the evx_link */
	if (!d_list_empty(&evx->evx_link)) {
		d_list_del(&evx->evx_link);
		D_ASSERT(evx->evx_status != DAOS_EVS_RUNNING);

		if (evx->evx_status == DAOS_EVS_COMPLETED && eq != NULL) {
			D_ASSERTF(eq->eq_n_comp > 0, "eq %p\n", eq);
			eq->eq_n_comp--;
		}
	}

	evx->evx_ctx = NULL;
out:
	if (eqx != NULL)
		D_MUTEX_UNLOCK(&eqx->eqx_lock);
out_unlocked:
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
		if (d_list_empty(&evx->evx_child))
			return NULL;

		tmp = d_list_entry(evx->evx_child.next,
				   struct daos_event_private, evx_link);
		return daos_evx2ev(tmp);
	}

	tmp = daos_ev2evx(child);
	if (tmp->evx_link.next == &evx->evx_child)
		return NULL;

	tmp = d_list_entry(tmp->evx_link.next, struct daos_event_private,
			   evx_link);
	return daos_evx2ev(tmp);
}

int
daos_event_abort(struct daos_event *ev)
{
	struct daos_event_private	*evx = daos_ev2evx(ev);
	struct daos_eq_private		*eqx = NULL;
	int				rc;

	if (daos_handle_is_valid(evx->evx_eqh)) {
		eqx = daos_eq_lookup(evx->evx_eqh);
		if (eqx == NULL) {
			D_ERROR("Invalid EQ handle %"PRIu64"\n", evx->evx_eqh.cookie);
			return -DER_NONEXIST;
		}
		D_MUTEX_LOCK(&eqx->eqx_lock);
	} else {
		D_MUTEX_LOCK(&evx->evx_lock);
	}

	rc = daos_event_abort_locked(eqx, evx);

	if (eqx != NULL) {
		D_MUTEX_UNLOCK(&eqx->eqx_lock);
		daos_eq_putref(eqx);
	} else {
		D_MUTEX_UNLOCK(&evx->evx_lock);
	}

	return rc;
}

int
daos_event_priv_reset(void)
{
	int rc;

	if (ev_thpriv_is_init) {
		rc = daos_event_fini(&ev_thpriv);
		if (rc) {
			D_ERROR("Failed to finalize thread private event "DF_RC"\n", DP_RC(rc));
			return rc;
		}
	}

	rc = daos_event_init(&ev_thpriv, DAOS_HDL_INVAL, NULL);
	if (rc) {
		D_ERROR("Failed to initialize thread private event "DF_RC"\n", DP_RC(rc));
		return rc;
	}
	return 0;
}

int
daos_event_priv_get(daos_event_t **ev)
{
	struct daos_event_private	*evx = daos_ev2evx(&ev_thpriv);
	daos_ev_status_t		ev_status;
	int				rc;

	D_ASSERT(*ev == NULL);

	if (!ev_thpriv_is_init) {
		rc = daos_event_init(&ev_thpriv, DAOS_HDL_INVAL, NULL);
		if (rc)
			return rc;
		ev_thpriv_is_init = true;
	}

	ev_status = atomic_load(&evx->evx_status);
	if (ev_status != DAOS_EVS_READY) {
		D_CRIT("private event is inuse, status=%d\n", ev_status);
		return -DER_BUSY;
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
	struct ev_progress_arg	epa;
	struct daos_event_private *evx = daos_ev2evx(&ev_thpriv);
	int rc = 0, rc2;

	D_ASSERT(ev_thpriv_is_init);

	epa.evx = evx;
	epa.eqx = NULL;

	/* Wait on the event to complete */
	while (atomic_load(&evx->evx_status) != DAOS_EVS_READY) {
		rc = crt_progress_cond(evx->evx_ctx, ev_prog_timeout, ev_progress_cb, &epa);

		/** progress succeeded, loop can exit if event completed */
		if (rc == 0) {
			rc = ev_thpriv.ev_error;
			if (atomic_load(&evx->evx_status) == DAOS_EVS_READY)
				break;
			continue;
		}

		/** progress timeout, try calling progress again */
		if (rc == -DER_TIMEDOUT)
			continue;

		D_ERROR("crt progress failed with "DF_RC"\n", DP_RC(rc));

		/** other progress failure; op should fail with that err. */
		break;
	}

	/** on success, the event should have been reset to ready stat by the progress cb */
	if (rc == 0)
		D_ASSERT(evx->evx_status == DAOS_EVS_READY);
	rc2 = daos_event_priv_reset();
	if (rc2) {
		if (rc == 0)
			rc = rc2;
		return rc;
	}
	D_ASSERT(ev_thpriv.ev_error == 0);
	return rc;
}

tse_sched_t *
daos_ev2sched(struct daos_event *ev)
{
	return daos_ev2evx(ev)->evx_sched;
}
