/**
 * (C) Copyright 2020 Intel Corporation.
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
/**
 * obj_tx: DAOS Transaction
 *
 * This module is part of libdaos. It implements the DAOS transaction API.
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos_task.h>
#include <daos_types.h>
#include <daos/common.h>
#include <daos/object.h>
#include <daos/container.h>
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/dtx.h>
#include <daos/task.h>
#include "obj_rpc.h"
#include "obj_internal.h"

enum dc_tx_status {
	TX_OPEN,
	TX_COMMITTING,
	TX_COMMITTED,
	TX_ABORTED,
	TX_FAILED,
};

/* Request cache for each modification in the TX */
struct dc_tx_sub_req {
	/* link into dc_tx::tx_sub_reqs. */
	d_list_t		 dtsr_link;
	/* Pointer to the object to be modified. */
	struct dc_object	*dtsr_obj;
	/* The hashed dkey if applicable. */
	uint64_t		 dtsr_dkey_hash;
	/* The modification type: update/punch_obj/punch_dkey/punch_akey. */
	uint32_t		 dtsr_opc;
	/* Modification flags, see update/punch API. */
	uint32_t		 dtsr_flags;
	/* The count of akeys to be modified if applicable. */
	uint32_t		 dtsr_nr;
	/* Hold the dkey to be modified for non-zero-copy case. */
	daos_key_t		 dtsr_dkey_inline;
	/*
	 * Pointer to the dkey to be modified, either dtsr_dkey_inline
	 * or the TX sponsor given key buffer.
	 */
	daos_key_t		*dtsr_dkey;
	union {
		/* The array of akeys to be punched. */
		daos_key_t	*dtsr_akeys;
		/* The array of iods to be updated. */
		daos_iod_t	*dtsr_iods;
	};
	/* Array of sgls to be updated. */
	d_sg_list_t		*dtsr_sgls;
};

/* Client transaction handle */
struct dc_tx {
	/** Link chain in the global handle hash table */
	struct d_hlink		 tx_hlink;
	/** The TX identifier, that contains the timestamp. */
	struct dtx_id		 tx_id;
	/** Container open handle */
	daos_handle_t		 tx_coh;
	/** Protects all fields below. */
	pthread_mutex_t		 tx_lock;
	/** The TX epoch. */
	struct dtx_epoch	 tx_epoch;
	/** The task choosing the TX epoch. */
	tse_task_t		*tx_epoch_task;
	/** The list of dc_tx_sub_req. */
	d_list_t		 tx_sub_reqs;
	/** Transaction flags (DAOS_TF_RDONLY, DAOS_TF_ZERO_COPY, etc.) */
	uint64_t		 tx_flags;
	/** The sub requests count */
	uint32_t		 tx_sub_count;
	/** Transaction status (OPEN, COMMITTED, etc.), see dc_tx_status. */
	enum dc_tx_status	 tx_status;
	/** Pool map version when trigger first IO. */
	uint32_t		 tx_pm_ver;
	/** The rank for the server on which the TX leader resides. */
	uint32_t		 tx_leader_rank;
	/** The target index for the TX leader. */
	uint32_t		 tx_leader_tag;
	/** Reference the pool. */
	struct dc_pool		*tx_pool;
};

static void
dc_tx_free(struct d_hlink *hlink)
{
	struct dc_tx	*tx;

	tx = container_of(hlink, struct dc_tx, tx_hlink);
	D_ASSERT(daos_hhash_link_empty(&tx->tx_hlink));

	if (tx->tx_epoch_task != NULL)
		tse_task_decref(tx->tx_epoch_task);
	dc_pool_put(tx->tx_pool);
	D_MUTEX_DESTROY(&tx->tx_lock);
	D_FREE_PTR(tx);
}

static struct d_hlink_ops tx_h_ops = {
	.hop_free	= dc_tx_free,
};

static void
dc_tx_decref(struct dc_tx *tx)
{
	daos_hhash_link_putref(&tx->tx_hlink);
}

static struct dc_tx *
dc_tx_hdl2ptr(daos_handle_t th)
{
	struct d_hlink	*hlink;

	hlink = daos_hhash_link_lookup(th.cookie);
	if (hlink == NULL)
		return NULL;

	return container_of(hlink, struct dc_tx, tx_hlink);
}

static daos_handle_t
dc_tx_ptr2hdl(struct dc_tx *tx)
{
	daos_handle_t	th;

	daos_hhash_link_key(&tx->tx_hlink, &th.cookie);

	return th;
}

static void
dc_tx_hdl_link(struct dc_tx *tx)
{
	daos_hhash_link_insert(&tx->tx_hlink, DAOS_HTYPE_TX);
}

static void
dc_tx_hdl_unlink(struct dc_tx *tx)
{
	daos_hhash_link_delete(&tx->tx_hlink);
}

static int
dc_tx_alloc(daos_handle_t coh, daos_epoch_t epoch, uint64_t flags,
	    struct dc_tx **ptx)
{
	daos_handle_t	 ph;
	struct dc_tx	*tx;
	int		 rc;

	if (daos_handle_is_inval(coh))
		return -DER_NO_HDL;

	ph = dc_cont_hdl2pool_hdl(coh);
	D_ASSERT(!daos_handle_is_inval(ph));

	D_ALLOC_PTR(tx);
	if (tx == NULL)
		return -DER_NOMEM;

	rc = D_MUTEX_INIT(&tx->tx_lock, NULL);
	if (rc != 0) {
		D_FREE_PTR(tx);
		return rc;
	}

	tx->tx_pool = dc_hdl2pool(ph);
	D_ASSERT(tx->tx_pool != NULL);

	D_INIT_LIST_HEAD(&tx->tx_sub_reqs);
	daos_dti_gen(&tx->tx_id, false);

	if (epoch == 0) {
		/* The epoch will be generated by the first accessed server. */
		tx->tx_epoch.oe_value = 0;
		tx->tx_epoch.oe_first = 0;
		tx->tx_epoch.oe_flags = DTX_EPOCH_UNCERTAIN;
	} else {
		/* The epoch is dictated by the caller. */
		tx->tx_epoch.oe_value = epoch;
		tx->tx_epoch.oe_first = tx->tx_epoch.oe_value;
		tx->tx_epoch.oe_flags = 0;
	}

	tx->tx_coh = coh;
	tx->tx_flags = flags;
	tx->tx_status = TX_OPEN;
	daos_hhash_hlink_init(&tx->tx_hlink, &tx_h_ops);
	dc_tx_hdl_link(tx);

	*ptx = tx;

	return 0;
}

static void
dc_tx_cleanup_one(struct dc_tx *tx, struct dc_tx_sub_req *dtsr)
{
	int	i;

	if (!(tx->tx_flags & DAOS_TF_ZERO_COPY)) {
		switch (dtsr->dtsr_opc) {
		case DAOS_OBJ_RPC_UPDATE:
			for (i = 0; i < dtsr->dtsr_nr; i++) {
				daos_iov_free(&dtsr->dtsr_iods[i].iod_name);
				D_FREE(dtsr->dtsr_iods[i].iod_recxs);
				daos_sgl_fini(&dtsr->dtsr_sgls[i], true);
			}

			D_FREE(dtsr->dtsr_iods);
			D_FREE(dtsr->dtsr_sgls);
			daos_iov_free(dtsr->dtsr_dkey);
			break;
		case DAOS_OBJ_RPC_PUNCH:
			break;
		case DAOS_OBJ_RPC_PUNCH_DKEYS:
			daos_iov_free(dtsr->dtsr_dkey);
			break;
		case DAOS_OBJ_RPC_PUNCH_AKEYS:
			for (i = 0; i < dtsr->dtsr_nr; i++)
				daos_iov_free(&dtsr->dtsr_akeys[i]);

			D_FREE(dtsr->dtsr_akeys);
			daos_iov_free(dtsr->dtsr_dkey);
			break;
		default:
			D_ASSERT(0);
		}
	}

	obj_decref(dtsr->dtsr_obj);
	D_FREE(dtsr);
}

static void
dc_tx_cleanup(struct dc_tx *tx)
{
	struct dc_tx_sub_req	*dtsr;

	while ((dtsr = d_list_pop_entry(&tx->tx_sub_reqs,
					struct dc_tx_sub_req,
					dtsr_link)) != NULL) {
		dc_tx_cleanup_one(tx, dtsr);
		tx->tx_sub_count--;
	}

	D_ASSERTF(tx->tx_sub_count == 0,
		  "Invalid sub requests count %d when cleanup\n",
		  tx->tx_sub_count);
}

/** Set the epoch of \a th to \a epoch. */
int
dc_tx_set_epoch(tse_task_t *task, daos_handle_t th, daos_epoch_t epoch)
{
	struct dc_tx	*tx;
	int		 rc = 0;

	D_ASSERT(task != NULL);

	if (epoch == 0) {
		return 0;
	} else if (epoch == DAOS_EPOCH_MAX) {
		D_ERROR("invalid epoch: DAOS_EPOCH_MAX\n");
		return -DER_INVAL;
	}

	tx = dc_tx_hdl2ptr(th);
	if (tx == NULL) {
		D_ERROR("failed to find transaction handle "DF_X64"\n",
			th.cookie);
		return -DER_NO_HDL;
	}

	D_MUTEX_LOCK(&tx->tx_lock);
	if (tx->tx_status != TX_OPEN && tx->tx_status != TX_FAILED &&
	    tx->tx_status != TX_COMMITTING) {
		D_ERROR("Can't set epoch on non-open/non-failed/non-committing "
			"TX (%d)\n", tx->tx_status);
		rc = -DER_NO_PERM;
	} else if (tx->tx_epoch_task == task) {
		D_ASSERT(!dtx_epoch_chosen(&tx->tx_epoch));
		tx->tx_epoch.oe_value = epoch;
		if (tx->tx_epoch.oe_first == 0)
			tx->tx_epoch.oe_first = tx->tx_epoch.oe_value;
		tx->tx_epoch.oe_flags &= ~DTX_EPOCH_HINT;
		D_DEBUG(DB_IO, DF_X64"/%p: set: value="DF_U64" first="DF_U64
			" flags="DF_X64"\n", th.cookie, task,
			tx->tx_epoch.oe_value, tx->tx_epoch.oe_first,
			tx->tx_epoch.oe_flags);
	}
	D_MUTEX_UNLOCK(&tx->tx_lock);

	dc_tx_decref(tx);

	return rc;
}

int
dc_tx_get_dti(daos_handle_t th, struct dtx_id *dti)
{
	struct dc_tx	*tx;

	tx = dc_tx_hdl2ptr(th);
	if (tx == NULL)
		return -DER_NO_HDL;

	daos_dti_copy(dti, &tx->tx_id);
	dc_tx_decref(tx);

	return 0;
}

/*
 * Check the Pool Map Version: if the (client known) latest pool map version
 * is newer than the TX known pool map version, then it is possible that the
 * data from TX former fetch/list/query may become stale. On the other hand,
 * even if related data is still valid, but related read timestamp (used for
 * MVCC) may be left on the server that is evicted from the cluster. Then we
 * have to restart the transaction under such case.
 *
 * If the parameter @ptx is not NULL, then the transaction handle pointer to
 * the "dc_tx" will be returned via it, with dc_tx.tx_lock locked, and then the
 * caller can directly use it without dc_tx_hdl2ptr() again.
 */
static int
dc_tx_check_pmv_internal(daos_handle_t th, struct dc_tx **ptx)
{
	struct dc_tx	*tx;
	uint32_t	 pm_ver;
	int		 rc = 0;

	if (daos_handle_is_inval(th))
		return -DER_INVAL;

	tx = dc_tx_hdl2ptr(th);
	if (tx == NULL)
		return -DER_NO_HDL;

	D_MUTEX_LOCK(&tx->tx_lock);

	/* For RDONLY TX, not check pool map version to force restart TX. */
	if (tx->tx_pm_ver == 0 || tx->tx_flags & DAOS_TF_RDONLY)
		D_GOTO(out, rc = 0);

	pm_ver = dc_pool_get_version(tx->tx_pool);

	if (tx->tx_pm_ver != pm_ver) {
		D_ASSERTF(tx->tx_pm_ver < pm_ver,
			  "Pool map version is reverted from %u to %u\n",
			  tx->tx_pm_ver, pm_ver);

		tx->tx_pm_ver = pm_ver;
		tx->tx_status = TX_FAILED;
		rc = -DER_TX_RESTART;
	}

out:
	if (rc != 0 || ptx == NULL) {
		D_MUTEX_UNLOCK(&tx->tx_lock);
		dc_tx_decref(tx);
	} else {
		*ptx = tx;
	}

	return rc;
}

int
dc_tx_check_pmv(daos_handle_t th)
{
	return dc_tx_check_pmv_internal(th, NULL);
}

/* See dc_tx_check_pmv_internal for the semantics of ptx. */
static int
dc_tx_check(daos_handle_t th, bool check_write, struct dc_tx **ptx)
{
	struct dc_tx	*tx = NULL;
	int		 rc;

	rc = dc_tx_check_pmv_internal(th, &tx);
	if (rc != 0)
		return rc;

	if (check_write) {
		if (tx->tx_status != TX_OPEN) {
			D_ERROR("TX is not valid for modification.\n");
			D_GOTO(out, rc = -DER_NO_PERM);
		}

		if (tx->tx_flags & DAOS_TF_RDONLY) {
			D_ERROR("TX is READ ONLY.\n");
			D_GOTO(out, rc = -DER_NO_PERM);
		}

		if (srv_io_mode != DIM_DTX_FULL_ENABLED) {
			D_ERROR("NOT allow modification because "
				"DTX is not full enabled.\n");
			D_GOTO(out, rc = -DER_NO_PERM);
		}
	} else if (tx->tx_status != TX_OPEN) {
		D_ERROR("TX is not valid for fetch.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

out:
	if (rc != 0) {
		D_MUTEX_UNLOCK(&tx->tx_lock);
		/* -1 for dc_tx_check_pmv_internal() held */
		dc_tx_decref(tx);
	} else {
		*ptx = tx;
	}

	return rc;
}

int
dc_tx_hdl2epoch_and_pmv(daos_handle_t th, struct dtx_epoch *epoch,
			uint32_t *pm_ver)
{
	struct dc_tx	*tx = NULL;
	int		 rc;

	rc = dc_tx_check(th, false, &tx);
	if (rc == 0) {
		if (tx->tx_pm_ver == 0)
			tx->tx_pm_ver = dc_pool_get_version(tx->tx_pool);

		*pm_ver = tx->tx_pm_ver;
		*epoch = tx->tx_epoch;
		D_MUTEX_UNLOCK(&tx->tx_lock);
		dc_tx_decref(tx);
	}

	return rc;
}

static int
complete_epoch_task(tse_task_t *task, void *arg)
{
	daos_handle_t	*th = arg;
	struct dc_tx	*tx;

	D_ASSERT(task != NULL);

	tx = dc_tx_hdl2ptr(*th);
	if (tx == NULL) {
		D_ERROR("cannot find transaction handle "DF_X64"\n",
			th->cookie);
		return -DER_NO_HDL;
	}
	D_MUTEX_LOCK(&tx->tx_lock);

	/*
	 * If dc_tx_restart is called on this TX before we reach here,
	 * tx_epoch_task may be NULL or a different task.
	 */
	if (tx->tx_epoch_task == task) {
		tse_task_decref(tx->tx_epoch_task);
		tx->tx_epoch_task = NULL;
		D_DEBUG(DB_IO, DF_X64"/%p: epoch task complete\n", th->cookie,
			task);
	}

	D_MUTEX_UNLOCK(&tx->tx_lock);
	dc_tx_decref(tx);
	return 0;
}

/**
 * Get the TX epoch for TX operations. See the return values.
 *
 * \param[in,out]	task	current task
 * \param[in]		th	TX handle
 * \param[out]		epoch	epoch
 *
 * \retval DC_TX_GE_CHOSEN	\a epoch can be used for I/Os of \a th
 * \retval DC_TX_GE_CHOOSING	\a task shall call dc_tx_set_epoch, if a TX
 *				epoch is chosen, in a completion callback
 *				registered after this function returns
 * \retval DC_TX_GE_REINIT	\a task must reinit itself
 */
int
dc_tx_get_epoch(tse_task_t *task, daos_handle_t th, struct dtx_epoch *epoch)
{
	struct dc_tx	*tx;
	int		 rc;

	tx = dc_tx_hdl2ptr(th);
	if (tx == NULL) {
		D_ERROR("cannot find transaction handle "DF_X64"\n", th.cookie);
		return -DER_NO_HDL;
	}
	D_MUTEX_LOCK(&tx->tx_lock);

	if (dtx_epoch_chosen(&tx->tx_epoch)) {
		/* The TX epoch is chosen before we acquire the lock. */
		*epoch = tx->tx_epoch;
		rc = DC_TX_GE_CHOSEN;
	} else if (tx->tx_epoch_task == NULL) {
		/*
		 * The TX epoch hasn't been chosen yet, and nobody is choosing
		 * it. So this task will be the "epoch task".
		 */
		D_DEBUG(DB_IO, DF_X64"/%p: choosing epoch\n", th.cookie, task);
		tse_task_addref(task);
		tx->tx_epoch_task = task;
		rc = tse_task_register_comp_cb(task, complete_epoch_task, &th,
					       sizeof(th));
		if (rc != 0) {
			D_ERROR("cannot register completion callback: "DF_RC
				"\n", DP_RC(rc));
			goto out;
		}
		*epoch = tx->tx_epoch;
		rc = DC_TX_GE_CHOOSING;
	} else {
		/*
		 * The TX epoch hasn't been chosen yet, but some task is
		 * already choosing it. We'll "wait" for that "epoch task" to
		 * complete.
		 */
		D_DEBUG(DB_IO, DF_X64"/%p: waiting for epoch task %p\n",
			th.cookie, task, tx->tx_epoch_task);
		rc = tse_task_register_deps(task, 1, &tx->tx_epoch_task);
		if (rc != 0) {
			D_ERROR("cannot depend on task %p: "DF_RC"\n",
				tx->tx_epoch_task, DP_RC(rc));
			goto out;
		}
		rc = DC_TX_GE_REINIT;
	}

out:
	D_MUTEX_UNLOCK(&tx->tx_lock);
	dc_tx_decref(tx);
	return rc;
}

int
dc_tx_hdl2epoch(daos_handle_t th, daos_epoch_t *epoch)
{
	struct dc_tx	*tx;
	int		 rc = 0;

	if (daos_handle_is_inval(th))
		return -DER_INVAL;

	tx = dc_tx_hdl2ptr(th);
	if (tx == NULL)
		return -DER_NO_HDL;

	/**
	 * XXX: If the TX has never talked with any server, its epoch is not
	 *	chosen yet. dc_tx_hdl2epoch() returns -DER_UNINIT to indicate
	 *	that. The caller can re-call hdl2epoch after some fetch or
	 *	TX commit.
	 */
	D_MUTEX_LOCK(&tx->tx_lock);
	if (dtx_epoch_chosen(&tx->tx_epoch))
		*epoch = tx->tx_epoch.oe_value;
	else
		rc = -DER_UNINIT;
	D_MUTEX_UNLOCK(&tx->tx_lock);
	dc_tx_decref(tx);

	return rc;
}

int
dc_tx_open(tse_task_t *task)
{
	daos_tx_open_t	*args;
	struct dc_tx	*tx = NULL;
	int		 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL,
		  "Task Argument OPC does not match DC OPC (open)\n");

	rc = dc_tx_alloc(args->coh, 0, args->flags, &tx);
	if (rc == 0)
		*args->th = dc_tx_ptr2hdl(tx);

	tse_task_complete(task, rc);

	return rc;
}

struct tx_commit_cb_args {
	struct dc_tx	*tcca_tx;
	crt_rpc_t	*tcca_req;
};

static int
dc_tx_commit_cb(tse_task_t *task, void *data)
{
	struct tx_commit_cb_args *tcca = (struct tx_commit_cb_args *)data;
	struct dc_tx		 *tx = tcca->tcca_tx;
	crt_rpc_t		 *req = tcca->tcca_req;
	int			  rc = task->dt_result;

	D_MUTEX_LOCK(&tx->tx_lock);

	if (rc == 0) {
		tx->tx_status = TX_COMMITTED;
		goto out;
	}

	tx->tx_status = TX_FAILED;

	/* Required to restart the TX with newer epoch. */
	if (rc == -DER_TX_RESTART)
		goto out;

	/* FIXME:
	 * 1. Check pool map, if pool map is refreshed, restart TX.
	 * 2. Retry the RPC if it is retriable error.
	 * 3. Failout for other failures.
	 */

out:
	crt_req_decref(req);
	D_MUTEX_UNLOCK(&tx->tx_lock);
	/* -1 for dc_tx_commit() held */
	dc_tx_decref(tx);

	return rc;
}

static int
dc_tx_elect_leader(struct dc_tx *tx)
{
	/* FIXME: elect leader. */

	return 0;
}

int
dc_tx_non_cpd_cb(daos_handle_t th, int result)
{
	struct dc_tx	*tx;

	tx = dc_tx_hdl2ptr(th);
	if (tx == NULL)
		return -DER_NO_HDL;

	D_MUTEX_LOCK(&tx->tx_lock);
	if (result != 0)
		tx->tx_status = TX_FAILED;
	else
		tx->tx_status = TX_COMMITTED;
	D_MUTEX_UNLOCK(&tx->tx_lock);

	dc_tx_decref(tx);

	return 0;
}

static int
dc_tx_commit_non_cpd(tse_task_t *task, struct dc_tx *tx)
{
	struct dc_tx_sub_req	*dtsr;
	struct dtx_epoch	 epoch = tx->tx_epoch;
	uint32_t		 pm_ver = tx->tx_pm_ver;
	int			 rc;

	tx->tx_status = TX_COMMITTING;
	dtsr = d_list_entry(tx->tx_sub_reqs.next, struct dc_tx_sub_req,
			    dtsr_link);

	D_MUTEX_UNLOCK(&tx->tx_lock);

	if (dtsr->dtsr_opc == DAOS_OBJ_RPC_UPDATE) {
		daos_obj_update_t	*args = dc_task_get_args(task);

		args->th = dc_tx_ptr2hdl(tx);
		args->oh = obj_ptr2hdl(dtsr->dtsr_obj);
		args->flags = dtsr->dtsr_flags;
		args->dkey = dtsr->dtsr_dkey;
		args->nr = dtsr->dtsr_nr;
		args->iods = dtsr->dtsr_iods;
		args->sgls = dtsr->dtsr_sgls;
		args->ioms = NULL;

		rc = dc_obj_update(task, &epoch, pm_ver, args);
	} else {
		daos_obj_punch_t	*args = dc_task_get_args(task);

		args->th = dc_tx_ptr2hdl(tx);
		args->oh = obj_ptr2hdl(dtsr->dtsr_obj);
		args->dkey = dtsr->dtsr_dkey;
		args->akeys = dtsr->dtsr_akeys;
		args->flags = dtsr->dtsr_flags;
		args->akey_nr = dtsr->dtsr_nr;

		rc = dc_obj_punch(task, &epoch, pm_ver, dtsr->dtsr_opc, args);
	}

	/* -1 for dc_tx_commit() held */
	dc_tx_decref(tx);

	return rc;
}

static int
dc_tx_commit_trigger(tse_task_t *task, struct dc_tx *tx)
{
	crt_rpc_t			*req = NULL;
	struct tx_commit_cb_args	*tcca;
	crt_endpoint_t			 tgt_ep;
	int				 rc;

	/* FIXME: Before support compounded RPC, let's use the existing
	 *	  object update/punch RPC to commit the TX modification.
	 */
	if (tx->tx_sub_count == 1)
		return dc_tx_commit_non_cpd(task, tx);

	D_ASSERT(0);

	rc = dc_tx_elect_leader(tx);
	if (rc != 0)
		goto out;

	tgt_ep.ep_grp = tx->tx_pool->dp_sys->sy_group;
	tgt_ep.ep_tag = tx->tx_leader_tag;
	tgt_ep.ep_rank = tx->tx_leader_rank;

	rc = obj_req_create(daos_task2ctx(task), &tgt_ep,
			    DAOS_OBJ_RPC_CPD, &req);
	if (rc != 0)
		D_GOTO(out, rc);

	crt_req_addref(req);
	tcca = crt_req_get(req);
	D_ASSERT(tcca != NULL);

	tcca->tcca_tx = tx;
	tcca->tcca_req = req;

	rc = tse_task_register_comp_cb(task, dc_tx_commit_cb,
				       tcca, sizeof(*tcca));
	if (rc != 0) {
		/* drop ref from crt_req_addref. */
		crt_req_decref(req);
		D_ERROR("Failed to register completion cb: rc = %d\n", rc);

		D_GOTO(out, rc);
	}

	/* FIXME:
	 * 1. Prepare bulk.
	 * 2. Pack sub requests into the RPC.
	 */

	tx->tx_status = TX_COMMITTING;
	D_MUTEX_UNLOCK(&tx->tx_lock);

	rc = daos_rpc_send(req, task);
	if (rc != 0)
		D_ERROR("CPD RPC failed rc "DF_RC"\n", DP_RC(rc));

	return rc;

out:
	if (req != NULL)
		crt_req_decref(req);

	D_MUTEX_UNLOCK(&tx->tx_lock);
	/* -1 for dc_tx_commit() held */
	dc_tx_decref(tx);
	tse_task_complete(task, rc);

	return rc;
}

int
dc_tx_commit(tse_task_t *task)
{
	daos_tx_commit_t	*args;
	struct dc_tx		*tx;
	int			 rc = 0;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL,
		  "Task Argument OPC does not match DC OPC (commit)\n");

	tx = dc_tx_hdl2ptr(args->th);
	if (tx == NULL) {
		D_ERROR("Invalid TX handle\n");
		D_GOTO(out_task, rc = -DER_NO_HDL);
	}

	D_MUTEX_LOCK(&tx->tx_lock);

	if (tx->tx_flags & DAOS_TF_RDONLY) {
		D_ERROR("Can't commit a RDONLY TX\n");
		D_GOTO(out_tx, rc = -DER_NO_PERM);
	}

	if (tx->tx_status == TX_COMMITTED)
		D_GOTO(out_tx, rc = 0);

	if (tx->tx_status == TX_COMMITTING) {
		/* FIXME: Before support compounded RPC, the retry update/punch
		 *	  RPC will hit TX_COMMITTING status TX, that is normal.
		 */
		D_ASSERT(!d_list_empty(&tx->tx_sub_reqs));
	} else if (tx->tx_status != TX_OPEN) {
		D_ERROR("Can't commit non-open state TX (%d)\n",
			tx->tx_status);
		D_GOTO(out_tx, rc = -DER_NO_PERM);
	} else if (d_list_empty(&tx->tx_sub_reqs)) {
		tx->tx_status = TX_COMMITTED;
		D_GOTO(out_tx, rc = 0);
	}

	return dc_tx_commit_trigger(task, tx);

out_tx:
	D_MUTEX_UNLOCK(&tx->tx_lock);
	dc_tx_decref(tx);

out_task:
	tse_task_complete(task, rc);

	return rc;
}

int
dc_tx_abort(tse_task_t *task)
{
	daos_tx_abort_t		*args;
	struct dc_tx		*tx;
	int			 rc = 0;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL,
		  "Task Argument OPC does not match DC OPC (abort)\n");

	tx = dc_tx_hdl2ptr(args->th);
	if (tx == NULL) {
		D_ERROR("Invalid TX handle\n");
		D_GOTO(out_task, rc = -DER_NO_HDL);
	}

	D_MUTEX_LOCK(&tx->tx_lock);

	if (tx->tx_status == TX_ABORTED)
		D_GOTO(out_tx, rc = 0);

	if (tx->tx_status != TX_OPEN) {
		D_ERROR("Can't commit non-open state TX (%d)\n",
			tx->tx_status);
		D_GOTO(out_tx, rc = -DER_NO_PERM);
	}

	if (tx->tx_flags & DAOS_TF_RDONLY) {
		D_ERROR("Can't abort a RDONLY TX\n");
		D_GOTO(out_tx, rc = -DER_NO_PERM);
	}

	tx->tx_status = TX_ABORTED;

out_tx:
	D_MUTEX_UNLOCK(&tx->tx_lock);
	dc_tx_decref(tx);

out_task:
	tse_task_complete(task, rc);

	return rc;
}

int
dc_tx_open_snap(tse_task_t *task)
{
	daos_tx_open_snap_t	*args;
	struct dc_tx		*tx = NULL;
	int			 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL,
		  "Task Argument OPC does not match DC OPC (open snap)\n");

	rc = dc_tx_alloc(args->coh, args->epoch, DAOS_TF_RDONLY, &tx);
	if (rc == 0)
		*args->th = dc_tx_ptr2hdl(tx);

	tse_task_complete(task, rc);

	return rc;
}

int
dc_tx_close(tse_task_t *task)
{
	daos_tx_close_t		*args;
	struct dc_tx		*tx;
	int			 rc = 0;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL,
		  "Task Argument OPC does not match DC OPC (close)\n");

	tx = dc_tx_hdl2ptr(args->th);
	if (tx == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	D_MUTEX_LOCK(&tx->tx_lock);
	if (tx->tx_status == TX_COMMITTING) {
		D_ERROR("Can't close a TX in committing\n");
		rc = -DER_BUSY;
	} else {
		dc_tx_cleanup(tx);
		dc_tx_hdl_unlink(tx);
		/* -1 for create */
		dc_tx_decref(tx);
	}
	D_MUTEX_UNLOCK(&tx->tx_lock);

	/* -1 for hdl2ptr */
	dc_tx_decref(tx);

out_task:
	tse_task_complete(task, rc);

	return rc;
}

/**
 * Restart a transaction that has encountered a -DER_TX_RESTART. This shall not
 * be used to restart a transaction created by dc_tx_open_snap or
 * dc_tx_local_open, either of which shall not encounter -DER_TX_RESTART.
 */
int
dc_tx_restart(tse_task_t *task)
{
	daos_tx_restart_t	*args;
	struct dc_tx		*tx;
	int			 rc = 0;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL,
		  "Task Argument OPC does not match DC OPC (restart)\n");

	tx = dc_tx_hdl2ptr(args->th);
	if (tx == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	D_MUTEX_LOCK(&tx->tx_lock);
	if (tx->tx_status != TX_OPEN && tx->tx_status != TX_FAILED) {
		D_ERROR("Can't restart non-open/non-failed state TX (%d)\n",
			tx->tx_status);
		rc = -DER_NO_PERM;
	} else {
		dc_tx_cleanup(tx);

		tx->tx_status = TX_OPEN;
		if (dtx_epoch_chosen(&tx->tx_epoch))
			tx->tx_epoch.oe_flags |= DTX_EPOCH_HINT;
		if (tx->tx_epoch_task != NULL) {
			tse_task_decref(tx->tx_epoch_task);
			tx->tx_epoch_task = NULL;
		}
	}
	D_MUTEX_UNLOCK(&tx->tx_lock);

	/* -1 for hdl2ptr */
	dc_tx_decref(tx);

out_task:
	tse_task_complete(task, rc);

	return rc;
}

int
dc_tx_local_open(daos_handle_t coh, daos_epoch_t epoch, uint32_t flags,
		 daos_handle_t *th)
{
	struct dc_tx	*tx = NULL;
	int		 rc;

	rc = dc_tx_alloc(coh, epoch, flags, &tx);
	if (rc == 0)
		*th = dc_tx_ptr2hdl(tx);

	return rc;
}

int
dc_tx_local_close(daos_handle_t th)
{
	struct dc_tx	*tx;
	int		 rc = 0;

	tx = dc_tx_hdl2ptr(th);
	if (tx == NULL)
		return -DER_NO_HDL;

	D_MUTEX_LOCK(&tx->tx_lock);
	if (tx->tx_status == TX_COMMITTING) {
		D_ERROR("Can't close a TX in committing\n");
		D_GOTO(out_tx, rc = -DER_BUSY);
	}

	dc_tx_cleanup(tx);
	dc_tx_hdl_unlink(tx);
	/* -1 for create */
	dc_tx_decref(tx);

out_tx:
	D_MUTEX_UNLOCK(&tx->tx_lock);
	/* -1 for hdl2ptr */
	dc_tx_decref(tx);

	return rc;
}

static int
dc_tx_add_update(struct dc_tx *tx, daos_handle_t oh, uint64_t flags,
		 daos_key_t *dkey, uint32_t nr, daos_iod_t *iods,
		 d_sg_list_t *sgls)
{
	struct dc_tx_sub_req	*dtsr;
	int			 rc = 0;
	int			 i;

	D_ASSERT(nr != 0);

	D_ALLOC_PTR(dtsr);
	if (dtsr == NULL)
		return -DER_NOMEM;

	dtsr->dtsr_obj = obj_hdl2ptr(oh);
	if (dtsr->dtsr_obj == NULL)
		D_GOTO(fail, rc = -DER_NO_HDL);

	dtsr->dtsr_opc = DAOS_OBJ_RPC_UPDATE;
	dtsr->dtsr_flags = flags;
	dtsr->dtsr_dkey_hash = obj_dkey2hash(dkey);
	dtsr->dtsr_nr = nr;

	if (tx->tx_flags & DAOS_TF_ZERO_COPY) {
		dtsr->dtsr_dkey = dkey;
		dtsr->dtsr_iods = iods;
		dtsr->dtsr_sgls = sgls;
	} else {
		dtsr->dtsr_dkey = &dtsr->dtsr_dkey_inline;
		rc = daos_iov_copy(dtsr->dtsr_dkey, dkey);
		if (rc != 0)
			D_GOTO(fail, rc);

		D_ALLOC_ARRAY(dtsr->dtsr_iods, nr);
		if (dtsr->dtsr_iods == NULL)
			D_GOTO(fail, rc = -DER_NOMEM);

		D_ALLOC_ARRAY(dtsr->dtsr_sgls, nr);
		if (dtsr->dtsr_sgls == NULL)
			D_GOTO(fail, rc = -DER_NOMEM);

		for (i = 0; i < nr; i++) {
			rc = daos_iov_copy(&dtsr->dtsr_iods[i].iod_name,
					   &iods[i].iod_name);
			if (rc != 0)
				D_GOTO(fail, rc);

			dtsr->dtsr_iods[i].iod_type = iods[i].iod_type;
			dtsr->dtsr_iods[i].iod_size = iods[i].iod_size;
			dtsr->dtsr_iods[i].iod_nr = iods[i].iod_nr;

			if (iods[i].iod_recxs == NULL)
				continue;

			D_ALLOC_ARRAY(dtsr->dtsr_iods[i].iod_recxs,
				      iods[i].iod_nr);
			if (dtsr->dtsr_iods[i].iod_recxs == NULL)
				D_GOTO(fail, rc = -DER_NOMEM);

			memcpy(dtsr->dtsr_iods[i].iod_recxs, iods[i].iod_recxs,
			       sizeof(daos_recx_t) * iods[i].iod_nr);
		}

		rc = daos_sgls_copy_all(dtsr->dtsr_sgls, nr, sgls, nr);
		if (rc != 0)
			D_GOTO(fail, rc);
	}

	d_list_add_tail(&dtsr->dtsr_link, &tx->tx_sub_reqs);
	tx->tx_sub_count++;

	return 0;

fail:
	if (!(tx->tx_flags & DAOS_TF_ZERO_COPY)) {
		if (dtsr->dtsr_iods != NULL) {
			for (i = 0; i < nr; i++) {
				daos_iov_free(
					&dtsr->dtsr_iods[i].iod_name);
				D_FREE(dtsr->dtsr_iods[i].iod_recxs);
			}

			D_FREE(dtsr->dtsr_iods);
		}

		if (dtsr->dtsr_sgls != NULL) {
			for (i = 0; i < nr; i++)
				daos_sgl_fini(&dtsr->dtsr_sgls[i], true);

			D_FREE(dtsr->dtsr_sgls);
		}

		daos_iov_free(dtsr->dtsr_dkey);
	}

	if (dtsr->dtsr_obj != NULL)
		obj_decref(dtsr->dtsr_obj);

	D_FREE(dtsr);

	return rc;
}

static int
dc_tx_add_punch_obj(struct dc_tx *tx, daos_handle_t oh, uint64_t flags)
{
	struct dc_tx_sub_req	*dtsr;
	int			 rc = 0;

	D_ALLOC_PTR(dtsr);
	if (dtsr == NULL)
		return -DER_NOMEM;

	dtsr->dtsr_obj = obj_hdl2ptr(oh);
	if (dtsr->dtsr_obj == NULL)
		D_GOTO(fail, rc = -DER_NO_HDL);

	dtsr->dtsr_opc = DAOS_OBJ_RPC_PUNCH;
	dtsr->dtsr_flags = flags;
	dtsr->dtsr_dkey_hash = 0;

	d_list_add_tail(&dtsr->dtsr_link, &tx->tx_sub_reqs);
	tx->tx_sub_count++;

	return 0;

fail:
	D_FREE(dtsr);

	return rc;
}

static int
dc_tx_add_punch_dkey(struct dc_tx *tx, daos_handle_t oh, uint64_t flags,
		     daos_key_t *dkey)
{
	struct dc_tx_sub_req	*dtsr;
	int			 rc = 0;

	D_ALLOC_PTR(dtsr);
	if (dtsr == NULL)
		return -DER_NOMEM;

	dtsr->dtsr_obj = obj_hdl2ptr(oh);
	if (dtsr->dtsr_obj == NULL)
		D_GOTO(fail, rc = -DER_NO_HDL);

	dtsr->dtsr_opc = DAOS_OBJ_RPC_PUNCH_DKEYS;
	dtsr->dtsr_flags = flags;
	dtsr->dtsr_dkey_hash = obj_dkey2hash(dkey);

	if (tx->tx_flags & DAOS_TF_ZERO_COPY) {
		dtsr->dtsr_dkey = dkey;
	} else {
		dtsr->dtsr_dkey = &dtsr->dtsr_dkey_inline;
		rc = daos_iov_copy(dtsr->dtsr_dkey, dkey);
		if (rc != 0)
			D_GOTO(fail, rc);
	}

	d_list_add_tail(&dtsr->dtsr_link, &tx->tx_sub_reqs);
	tx->tx_sub_count++;

	return 0;

fail:
	if (dtsr->dtsr_obj != NULL)
		obj_decref(dtsr->dtsr_obj);

	D_FREE(dtsr);

	return rc;
}

static int
dc_tx_add_punch_akeys(struct dc_tx *tx, daos_handle_t oh, uint64_t flags,
		      daos_key_t *dkey, uint32_t nr, daos_key_t *akeys)
{
	struct dc_tx_sub_req	*dtsr;
	int			 rc = 0;
	int			 i;

	D_ASSERT(nr != 0);

	D_ALLOC_PTR(dtsr);
	if (dtsr == NULL)
		return -DER_NOMEM;

	dtsr->dtsr_obj = obj_hdl2ptr(oh);
	if (dtsr->dtsr_obj == NULL)
		D_GOTO(fail, rc = -DER_NO_HDL);

	dtsr->dtsr_opc = DAOS_OBJ_RPC_PUNCH_AKEYS;
	dtsr->dtsr_flags = flags;
	dtsr->dtsr_dkey_hash = obj_dkey2hash(dkey);
	dtsr->dtsr_nr = nr;

	if (tx->tx_flags & DAOS_TF_ZERO_COPY) {
		dtsr->dtsr_dkey = dkey;
		dtsr->dtsr_akeys = akeys;
	} else {
		dtsr->dtsr_dkey = &dtsr->dtsr_dkey_inline;
		rc = daos_iov_copy(dtsr->dtsr_dkey, dkey);
		if (rc != 0)
			D_GOTO(fail, rc);

		D_ALLOC_ARRAY(dtsr->dtsr_akeys, nr);
		if (dtsr->dtsr_akeys == NULL)
			D_GOTO(fail, rc = -DER_NOMEM);

		for (i = 0; i < nr; i++) {
			rc = daos_iov_copy(&dtsr->dtsr_akeys[i], &akeys[i]);
			if (rc != 0)
				D_GOTO(fail, rc);
		}
	}

	d_list_add_tail(&dtsr->dtsr_link, &tx->tx_sub_reqs);
	tx->tx_sub_count++;

	return 0;

fail:
	if (!(tx->tx_flags & DAOS_TF_ZERO_COPY)) {
		if (dtsr->dtsr_akeys != NULL) {
			for (i = 0; i < nr; i++)
				daos_iov_free(&dtsr->dtsr_akeys[i]);

			D_FREE(dtsr->dtsr_akeys);
		}

		daos_iov_free(dtsr->dtsr_dkey);
	}

	if (dtsr->dtsr_obj != NULL)
		obj_decref(dtsr->dtsr_obj);

	D_FREE(dtsr);

	return rc;
}

struct dc_tx_check_existence_cb_args {
	enum obj_rpc_opc	opc;
	struct dc_tx		*tx;
	daos_handle_t		oh;
	uint64_t		flags;
	daos_key_t		*dkey;
	uint64_t		nr;
	void			*iods_or_akeys;
	d_sg_list_t		*sgls;
	daos_iod_t		*tmp_iods;
};

void
dc_tx_check_existence_cb(void *data, tse_task_t *task)
{
	struct dc_tx_check_existence_cb_args	*args = data;
	struct dc_tx				*tx = args->tx;
	int					 rc = 0;

	D_MUTEX_LOCK(&tx->tx_lock);

	switch (args->opc) {
	case DAOS_OBJ_RPC_UPDATE:
		if (args->flags & (DAOS_COND_DKEY_INSERT |
				   DAOS_COND_AKEY_INSERT)) {
			if (task->dt_result == 0)
				D_GOTO(out, rc = -DER_EXIST);

			if (task->dt_result != -DER_NONEXIST)
				D_GOTO(out, rc = task->dt_result);
		} else if (args->flags & (DAOS_COND_DKEY_UPDATE |
					  DAOS_COND_AKEY_UPDATE)) {
			if (task->dt_result != 0)
				D_GOTO(out, rc = task->dt_result);
		}

		rc = dc_tx_add_update(tx, args->oh, args->flags,
				      args->dkey, args->nr,
				      args->iods_or_akeys, args->sgls);
		break;
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
		D_ASSERT(args->flags & DAOS_COND_PUNCH);

		if (task->dt_result != 0)
			D_GOTO(out, rc = task->dt_result);

		rc = dc_tx_add_punch_dkey(tx, args->oh, args->flags,
					  args->dkey);
		break;
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		D_ASSERT(args->flags & DAOS_COND_PUNCH);

		if (task->dt_result != 0)
			D_GOTO(out, rc = task->dt_result);

		rc = dc_tx_add_punch_akeys(tx, args->oh, args->flags,
					   args->dkey, args->nr,
					   args->iods_or_akeys);
		break;
	default:
		D_ASSERT(0);
	}

out:
	D_MUTEX_UNLOCK(&tx->tx_lock);

	/* Drop the reference that is held via dc_tx_attach(). */
	dc_tx_decref(tx);
	if (args->tmp_iods != NULL) {
		int		i;

		for (i = 0; i < args->nr; i++)
			daos_iov_free(&args->tmp_iods[i].iod_name);

		D_FREE(args->tmp_iods);
	}

	D_FREE(args);
	/* The errno will be auto propagated to the dependent task. */
	task->dt_result = rc;
}

static int
dc_tx_check_existence_task(enum obj_rpc_opc opc, daos_handle_t oh,
			   struct dc_tx *tx, uint64_t flags, daos_key_t *dkey,
			   uint32_t nr, void *iods_or_akeys, d_sg_list_t *sgls,
			   tse_task_t *parent)
{
	daos_iod_t				*iods = NULL;
	struct dc_tx_check_existence_cb_args	*cb_args;
	tse_task_t				*task;
	uint64_t				 api_flags;
	int					 rc;
	int					 i;

	D_ALLOC_PTR(cb_args);
	if (cb_args == NULL)
		return -DER_NOMEM;

	cb_args->opc		= opc;
	cb_args->tx		= tx;
	cb_args->oh		= oh;
	cb_args->flags		= flags;
	cb_args->dkey		= dkey;
	cb_args->nr		= nr;
	cb_args->iods_or_akeys	= iods_or_akeys;
	cb_args->sgls		= sgls;

	if (nr != 0) {
		D_ASSERT(iods_or_akeys != NULL);

		if (opc != DAOS_OBJ_RPC_UPDATE) {
			D_ALLOC_ARRAY(iods, nr);
			if (iods == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			for (i = 0; i < nr; i++) {
				rc = daos_iov_copy(&iods[i].iod_name,
					&((daos_key_t *)iods_or_akeys)[i]);
				if (rc != 0)
					goto out;
			}

			api_flags = DAOS_COND_AKEY_FETCH;
			cb_args->tmp_iods = iods;
		} else if (flags & (DAOS_COND_AKEY_INSERT |
				    DAOS_COND_AKEY_UPDATE)) {
			iods = iods_or_akeys;
			api_flags = DAOS_COND_AKEY_FETCH;
		} else {
			/* Only check dkey existence. */
			api_flags = DAOS_COND_DKEY_FETCH;
		}
	} else {
		api_flags = DAOS_COND_DKEY_FETCH;
	}

	rc = dc_obj_fetch_task_create(oh, dc_tx_ptr2hdl(tx), api_flags, dkey,
			nr, DIOF_CHECK_EXISTENCE | DIOF_TO_LEADER, iods, sgls,
			NULL, cb_args, NULL, tse_task2sched(parent), &task);
	if (rc != 0)
		goto out;

	rc = dc_task_depend(parent, 1, &task);
	if (rc != 0) {
		D_ERROR("Fail to add dep on check existence task: "DF_RC"\n",
			DP_RC(rc));
		dc_task_decref(task);
		goto out;
	}

	rc = dc_task_schedule(task, true);

	/* Return positive value to notify the sponsor to not call
	 * complete() the task until the checking existence callback.
	 */
	return rc == 0 ? 1 : rc;

out:
	if (iods != NULL && iods != iods_or_akeys) {
		for (i = 0; i < nr; i++)
			daos_iov_free(&iods[i].iod_name);

		D_FREE(iods);
	}

	D_FREE(cb_args);

	return rc;
}

int
dc_tx_attach(daos_handle_t th, void *args, enum obj_rpc_opc opc,
	     tse_task_t *task)
{
	struct dc_tx	*tx;
	int		 rc;
	bool		 locked = true;

	rc = dc_tx_check(th, true, &tx);
	if (rc != 0)
		return rc;

	switch (opc) {
	case DAOS_OBJ_RPC_UPDATE: {
		daos_obj_update_t	*up = args;

		if (up->flags & (DAOS_COND_DKEY_INSERT |
				 DAOS_COND_DKEY_UPDATE |
				 DAOS_COND_AKEY_INSERT |
				 DAOS_COND_AKEY_UPDATE)) {
			D_MUTEX_UNLOCK(&tx->tx_lock);
			locked = false;
			rc = dc_tx_check_existence_task(opc, up->oh, tx,
							up->flags, up->dkey,
							up->nr, up->iods,
							up->sgls, task);
		} else {
			rc = dc_tx_add_update(tx, up->oh, up->flags, up->dkey,
					      up->nr, up->iods, up->sgls);
		}
		break;
	}
	case DAOS_OBJ_RPC_PUNCH: {
		daos_obj_punch_t	*pu = args;

		D_ASSERTF(!(pu->flags & DAOS_COND_MASK),
			  "Unexpected cond flag %lx for punch obj\n",
			  pu->flags);

		rc = dc_tx_add_punch_obj(tx, pu->oh, pu->flags);
		break;
	}
	case DAOS_OBJ_RPC_PUNCH_DKEYS: {
		daos_obj_punch_t	*pu = args;

		if (pu->flags & DAOS_COND_PUNCH) {
			D_MUTEX_UNLOCK(&tx->tx_lock);
			locked = false;
			rc = dc_tx_check_existence_task(opc, pu->oh, tx,
							pu->flags, pu->dkey, 0,
							NULL, NULL, task);
		} else {
			rc = dc_tx_add_punch_dkey(tx, pu->oh, pu->flags,
						  pu->dkey);
		}
		break;
	}
	case DAOS_OBJ_RPC_PUNCH_AKEYS: {
		daos_obj_punch_t	*pu = args;

		if (pu->flags & DAOS_COND_PUNCH) {
			D_MUTEX_UNLOCK(&tx->tx_lock);
			locked = false;
			rc = dc_tx_check_existence_task(opc, pu->oh, tx,
							pu->flags, pu->dkey,
							pu->akey_nr, pu->akeys,
							NULL, task);
		} else {
			rc = dc_tx_add_punch_akeys(tx, pu->oh, pu->flags,
						   pu->dkey, pu->akey_nr,
						   pu->akeys);
		}
		break;
	}
	default:
		D_ERROR("Unsupportted TX attach opc %d\n", opc);
		rc = -DER_INVAL;
		break;
	}

	if (locked)
		D_MUTEX_UNLOCK(&tx->tx_lock);
	dc_tx_decref(tx);

	return rc;
}
