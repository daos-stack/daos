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

/* Server side minor epoch is 16 bits, and starts from 1, that allows at most
 * '2 ^ 16 - 1' sub modifications.
 */
#define DTX_SUB_WRITE_MAX	((1 << 16) - 1)
#define DTX_SUB_REQ_MAX		((1ULL << 32) - 1)
#define DTX_SUB_REQ_DEF		16

enum dc_tx_status {
	TX_OPEN,
	TX_COMMITTING,
	TX_COMMITTED,
	TX_ABORTED,
	TX_FAILED,
};

/*
 * XXX: In the CPD RPC on-wire data, the read sub requests and write ones are
 *	classified and stored separatedly (but adjacent each other). The Read
 *	ones are in front of the write ones. Such layout will simplify server
 *	side CPD RPC processing.
 *
 *	So when client caches the sub requests, we will keep the same layout,
 *	that can avoid additional memory movement when pack sub requests into
 *	CPD RPC. For such purpose, we will allocate large buffer to cache all
 *	related sub-requests for both read and write consecutively.
 *
 *	LOW								 HIGH
 *	|      <-- read reqs direction -- | -- write reqs directoion -->    |
 *	|---------------------------------|---------------------------------|
 *
 *	The order for read sub requests is not important, but write ones must
 *	be sorted as their sponsored order.
 */

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
	/** Transaction flags (DAOS_TF_RDONLY, DAOS_TF_ZERO_COPY, etc.) */
	uint64_t		 tx_flags;
	uint32_t		 tx_local:1; /* Local TX. */
	/** Transaction status (OPEN, COMMITTED, etc.), see dc_tx_status. */
	enum dc_tx_status	 tx_status;
	/** The rank for the server on which the TX leader resides. */
	uint32_t		 tx_leader_rank;
	/** The target index for the TX leader. */
	uint32_t		 tx_leader_tag;

	/* Pointer to the big buffer to cache all sub requests. */
	struct daos_cpd_sub_req	*tx_req_cache;
	/* How many sub requests can be held in the cache. */
	uint32_t		 tx_total_slots;
	/** The write requests count */
	uint32_t		 tx_write_cnt;
	/** The read requests count */
	uint32_t		 tx_read_cnt;

	/** Pool map version when trigger first IO. */
	uint32_t		 tx_pm_ver;
	/** Reference the pool. */
	struct dc_pool		*tx_pool;
};

static int
dc_tx_get_next_slot(struct dc_tx *tx, bool for_read,
		    struct daos_cpd_sub_req **slot)
{
	struct daos_cpd_sub_req		*buf;
	uint32_t			 start;
	uint32_t			 idx;
	uint32_t			 count;
	uint32_t			 from;
	uint32_t			 to;

	if (for_read) {
		if (tx->tx_flags & DAOS_TF_RDONLY)
			start = tx->tx_total_slots - 1;
		else if (tx->tx_total_slots > DTX_SUB_WRITE_MAX)
			start = tx->tx_total_slots - DTX_SUB_WRITE_MAX - 1;
		else
			start = (tx->tx_total_slots >> 1) - 1;

		/* All read slots are used. */
		if (tx->tx_read_cnt > start)
			goto full;

		idx = start - tx->tx_read_cnt;
	} else {
		D_ASSERT(!(tx->tx_flags & DAOS_TF_RDONLY));

		if (tx->tx_total_slots > DTX_SUB_WRITE_MAX)
			start = tx->tx_total_slots - DTX_SUB_WRITE_MAX;
		else
			start = tx->tx_total_slots >> 1;

		/* All write slots are used. */
		if (tx->tx_write_cnt >= start)
			goto full;

		idx = start + tx->tx_write_cnt;
	}

	*slot = &tx->tx_req_cache[idx];

	return 0;

full:
	if (!for_read && tx->tx_write_cnt >= DTX_SUB_WRITE_MAX)
		return -DER_OVERFLOW;

	if (((tx->tx_read_cnt + tx->tx_write_cnt) >= DTX_SUB_REQ_MAX) ||
	    (tx->tx_total_slots >= DTX_SUB_REQ_MAX))
		return -DER_OVERFLOW;

	if (tx->tx_flags & DAOS_TF_RDONLY ||
	    tx->tx_total_slots <= DTX_SUB_WRITE_MAX)
		count = tx->tx_total_slots << 1;
	else
		count = (tx->tx_total_slots << 1) - DTX_SUB_WRITE_MAX;

	D_ALLOC_ARRAY(buf, count);
	if (buf == NULL)
		return -DER_NOMEM;

	if (for_read) {
		from = 0;
		if (tx->tx_flags & DAOS_TF_RDONLY)
			start = count - 1;
		else if (count > DTX_SUB_WRITE_MAX)
			start = count - DTX_SUB_WRITE_MAX - 1;
		else
			start = (count >> 1) - 1;
		to = start - tx->tx_read_cnt + 1;
		idx = start - tx->tx_read_cnt;
	} else {
		from = start - tx->tx_read_cnt;
		if (count > DTX_SUB_WRITE_MAX)
			start = count - DTX_SUB_WRITE_MAX;
		else
			start = count >> 1;
		to = start - tx->tx_read_cnt;
		idx = start + tx->tx_write_cnt;
	}

	memcpy(buf + to, tx->tx_req_cache + from,
	       (tx->tx_read_cnt + tx->tx_write_cnt) * sizeof(*buf));
	D_FREE(tx->tx_req_cache);
	tx->tx_req_cache = buf;
	tx->tx_total_slots = count;

	*slot = &tx->tx_req_cache[idx];

	return 0;
}

static void
dc_tx_free(struct d_hlink *hlink)
{
	struct dc_tx	*tx;

	tx = container_of(hlink, struct dc_tx, tx_hlink);
	D_ASSERT(daos_hhash_link_empty(&tx->tx_hlink));
	D_ASSERT(tx->tx_read_cnt == 0);
	D_ASSERT(tx->tx_write_cnt == 0);

	if (tx->tx_epoch_task != NULL)
		tse_task_decref(tx->tx_epoch_task);

	D_FREE(tx->tx_req_cache);
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

	D_ALLOC_ARRAY(tx->tx_req_cache, DTX_SUB_REQ_DEF);
	if (tx->tx_req_cache == NULL) {
		D_FREE(tx);
		return -DER_NOMEM;
	}

	tx->tx_total_slots = DTX_SUB_REQ_DEF;

	rc = D_MUTEX_INIT(&tx->tx_lock, NULL);
	if (rc != 0) {
		D_FREE(tx->tx_req_cache);
		D_FREE_PTR(tx);
		return rc;
	}

	tx->tx_pool = dc_hdl2pool(ph);
	D_ASSERT(tx->tx_pool != NULL);

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
dc_tx_cleanup_one(struct dc_tx *tx, struct daos_cpd_sub_req *dcsr)
{
	int	i;

	switch (dcsr->dcsr_opc) {
	case DCSO_UPDATE: {
		struct daos_cpd_update	*dcu = &dcsr->dcsr_update;
		struct obj_iod_array	*iod_array = dcu->dcu_iod_array;
		struct daos_csummer	*csummer;

		csummer = dc_cont_hdl2csummer(tx->tx_coh);

		if (dcu->dcu_iod_array != NULL) {
			for (i = 0; i < dcsr->dcsr_nr; i++) {
				daos_iov_free(&iod_array->oia_iods[i].iod_name);
				D_FREE(iod_array->oia_iods[i].iod_recxs);
			}

			obj_io_desc_fini(iod_array->oia_oiods);
			daos_csummer_free_ic(csummer,
					     &iod_array->oia_iod_csums);
			D_FREE(iod_array->oia_offs);
			D_FREE(dcu->dcu_iod_array);
		}

		if (dcu->dcu_flags & DRF_CPD_BULK) {
			for (i = 0; i < dcsr->dcsr_nr; i++) {
				if (dcu->dcu_bulks[i] != CRT_BULK_NULL)
					crt_bulk_free(dcu->dcu_bulks[i]);
			}
		} else if (dcu->dcu_sgls != NULL) {
			for (i = 0; i < dcsr->dcsr_nr; i++)
				daos_sgl_fini(&dcu->dcu_sgls[i],
					      !(tx->tx_flags &
						DAOS_TF_ZERO_COPY));

			D_FREE(dcu->dcu_sgls);
		}

		daos_csummer_free_ci(csummer, &dcu->dcu_dkey_csum);
		D_FREE(dcu->dcu_ec_tgts);

		daos_iov_free(&dcsr->dcsr_dkey);
		break;
	}
	case DCSO_PUNCH_OBJ:
		break;
	case DCSO_PUNCH_DKEY:
		daos_iov_free(&dcsr->dcsr_dkey);
		break;
	case DCSO_PUNCH_AKEY: {
		struct daos_cpd_punch	*dcp = &dcsr->dcsr_punch;

		for (i = 0; i < dcsr->dcsr_nr; i++)
			daos_iov_free(&dcp->dcp_akeys[i]);

		D_FREE(dcp->dcp_akeys);
		daos_iov_free(&dcsr->dcsr_dkey);
		break;
	}
	case DCSO_READ: {
		struct daos_cpd_read	*dcr = &dcsr->dcsr_read;

		for (i = 0; i < dcsr->dcsr_nr; i++)
			daos_iov_free(&dcr->dcr_iods[i].iod_name);

		D_FREE(dcr->dcr_iods);
		daos_iov_free(&dcsr->dcsr_dkey);
		break;
	}
	default:
		D_ASSERT(0);
	}

	obj_decref(dcsr->dcsr_obj);
}

static void
dc_tx_cleanup(struct dc_tx *tx)
{
	uint32_t	from;
	uint32_t	to;
	uint32_t	i;

	if (tx->tx_flags & DAOS_TF_RDONLY)
		from = tx->tx_total_slots - tx->tx_read_cnt;
	else if (tx->tx_total_slots > DTX_SUB_WRITE_MAX)
		from = tx->tx_total_slots - DTX_SUB_WRITE_MAX - tx->tx_read_cnt;
	else
		from = (tx->tx_total_slots >> 1) - tx->tx_read_cnt;

	to = from + tx->tx_read_cnt + tx->tx_write_cnt;
	for (i = from; i < to; i++)
		dc_tx_cleanup_one(tx, &tx->tx_req_cache[i]);

	tx->tx_read_cnt = 0;
	tx->tx_write_cnt = 0;
}

/**
 * End a operation associated with transaction \a th.
 *
 * \param[in]	task		current task
 * \param[in]	th		transaction handle
 * \param[in]	req_epoch	request epoch
 * \param[in]	rep_rc		reply rc
 * \param[in]	rep_epoch	reply epoch
 */
int
dc_tx_op_end(tse_task_t *task, daos_handle_t th, struct dtx_epoch *req_epoch,
	     int rep_rc, daos_epoch_t rep_epoch)
{
	struct dc_tx	*tx;
	int		 rc = 0;

	D_ASSERT(task != NULL);
	D_ASSERT(daos_handle_is_valid(th));

	if (rep_rc == 0 && (dtx_epoch_chosen(req_epoch) || rep_epoch == 0))
		return 0;

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
		goto out;
	}

	/* TODO: Change the TX status according to rep_rc. */

	if (rep_epoch == DAOS_EPOCH_MAX) {
		D_ERROR("invalid reply epoch: DAOS_EPOCH_MAX\n");
		rc = -DER_PROTO;
		goto out;
	}

	if (tx->tx_epoch_task == task) {
		D_ASSERT(!dtx_epoch_chosen(&tx->tx_epoch));
		tx->tx_epoch.oe_value = rep_epoch;
		if (tx->tx_epoch.oe_first == 0)
			tx->tx_epoch.oe_first = tx->tx_epoch.oe_value;
		D_DEBUG(DB_IO, DF_X64"/%p: set: value="DF_U64" first="DF_U64
			" flags=%x, rpc flags %x\n", th.cookie, task,
			tx->tx_epoch.oe_value, tx->tx_epoch.oe_first,
			tx->tx_epoch.oe_flags, tx->tx_epoch.oe_rpc_flags);
	}

out:
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

	pm_ver = dc_pool_get_version(tx->tx_pool);

	if (tx->tx_pm_ver != pm_ver) {
		D_ASSERTF(tx->tx_pm_ver < pm_ver,
			  "Pool map version is reverted from %u to %u\n",
			  tx->tx_pm_ver, pm_ver);

		/* For external or RW TX, if pool map is stale, restart it. */
		if (tx->tx_pm_ver != 0 &&
		    (!tx->tx_local || !(tx->tx_flags & DAOS_TF_RDONLY))) {
			tx->tx_status = TX_FAILED;
			rc = -DER_TX_RESTART;
		}

		tx->tx_pm_ver = pm_ver;
	}

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

	if (tx->tx_status == TX_FAILED) {
		D_DEBUG(DB_IO, DF_X64"/%p: already failed\n", th.cookie, task);
		rc = -DER_OP_CANCELED;
		goto out;
	}

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
			tse_task_decref(tx->tx_epoch_task);
			tx->tx_epoch_task = NULL;
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
	struct daos_cpd_sub_req	*dcsr;
	struct dtx_epoch	 epoch = tx->tx_epoch;
	uint32_t		 pm_ver = tx->tx_pm_ver;
	int			 rc;

	tx->tx_status = TX_COMMITTING;
	dcsr = &tx->tx_req_cache[tx->tx_total_slots >> 1];

	D_MUTEX_UNLOCK(&tx->tx_lock);

	if (dcsr->dcsr_opc == DCSO_UPDATE) {
		daos_obj_update_t	*args = dc_task_get_args(task);

		args->th = dc_tx_ptr2hdl(tx);
		args->oh = obj_ptr2hdl(dcsr->dcsr_obj);
		args->flags = dcsr->dcsr_api_flags;
		args->dkey = &dcsr->dcsr_dkey;
		args->nr = dcsr->dcsr_nr;
		args->iods = dcsr->dcsr_update.dcu_iod_array->oia_iods;
		args->sgls = dcsr->dcsr_update.dcu_sgls;
		args->ioms = NULL;

		rc = dc_obj_update(task, &epoch, pm_ver, args);
	} else {
		daos_obj_punch_t	*args = dc_task_get_args(task);
		uint32_t		 opc;

		args->th = dc_tx_ptr2hdl(tx);
		args->oh = obj_ptr2hdl(dcsr->dcsr_obj);
		args->dkey = &dcsr->dcsr_dkey;
		args->akeys = dcsr->dcsr_punch.dcp_akeys;
		args->flags = dcsr->dcsr_api_flags;
		args->akey_nr = dcsr->dcsr_nr;

		switch (dcsr->dcsr_opc) {
		case DCSO_PUNCH_OBJ:
			opc = DAOS_OBJ_RPC_PUNCH;
			break;
		case DCSO_PUNCH_DKEY:
			opc = DAOS_OBJ_RPC_PUNCH_DKEYS;
			break;
		case DCSO_PUNCH_AKEY:
			opc = DAOS_OBJ_RPC_PUNCH_AKEYS;
			break;
		default:
			D_ASSERT(0);
		}

		rc = dc_obj_punch(task, &epoch, pm_ver, opc, args);
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
	if (tx->tx_write_cnt == 1)
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

	if (tx->tx_status == TX_COMMITTED)
		D_GOTO(out_tx, rc = 0);

	if (tx->tx_status == TX_COMMITTING) {
		/* FIXME: Before support compounded RPC, the retry update/punch
		 *	  RPC will hit TX_COMMITTING status TX, that is normal.
		 */
		D_ASSERT(tx->tx_write_cnt != 0);
	} else if (tx->tx_status != TX_OPEN) {
		D_ERROR("Can't commit non-open state TX (%d)\n",
			tx->tx_status);
		D_GOTO(out_tx, rc = -DER_NO_PERM);
	} else if (tx->tx_write_cnt == 0) {
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
		tx->tx_epoch.oe_value = 0;
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
	if (rc == 0) {
		*th = dc_tx_ptr2hdl(tx);
		tx->tx_local = 1;
	}

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
	struct daos_cpd_sub_req	*dcsr;
	struct daos_cpd_update	*dcu = NULL;
	struct obj_iod_array	*iod_array = NULL;
	int			 rc;
	int			 i;

	D_ASSERT(nr != 0);

	rc = dc_tx_get_next_slot(tx, false, &dcsr);
	if (rc != 0)
		return rc;

	dcsr->dcsr_obj = obj_hdl2ptr(oh);
	if (dcsr->dcsr_obj == NULL)
		return -DER_NO_HDL;

	rc = daos_iov_copy(&dcsr->dcsr_dkey, dkey);
	if (rc != 0)
		D_GOTO(fail, rc);

	dcsr->dcsr_opc = DCSO_UPDATE;
	dcsr->dcsr_nr = nr;
	dcsr->dcsr_dkey_hash = obj_dkey2hash(dkey);
	dcsr->dcsr_api_flags = flags;

	dcu = &dcsr->dcsr_update;
	D_ALLOC_PTR(dcu->dcu_iod_array);
	if (dcu->dcu_iod_array == NULL)
		D_GOTO(fail, rc = -DER_NOMEM);

	iod_array = dcu->dcu_iod_array;
	iod_array->oia_iod_nr = nr;

	D_ALLOC_ARRAY(iod_array->oia_iods, nr);
	if (iod_array->oia_iods == NULL)
		D_GOTO(fail, rc = -DER_NOMEM);

	for (i = 0; i < nr; i++) {
		rc = daos_iov_copy(&iod_array->oia_iods[i].iod_name,
				   &iods[i].iod_name);
		if (rc != 0)
			D_GOTO(fail, rc);

		iod_array->oia_iods[i].iod_size = iods[i].iod_size;
		iod_array->oia_iods[i].iod_type = iods[i].iod_type;
		iod_array->oia_iods[i].iod_nr = iods[i].iod_nr;

		if (iods[i].iod_recxs == NULL)
			continue;

		D_ALLOC_ARRAY(iod_array->oia_iods[i].iod_recxs,
			      iods[i].iod_nr);
		if (iod_array->oia_iods[i].iod_recxs == NULL)
			D_GOTO(fail, rc = -DER_NOMEM);

		memcpy(iod_array->oia_iods[i].iod_recxs, iods[i].iod_recxs,
		       sizeof(daos_recx_t) * iods[i].iod_nr);
	}

	D_ALLOC_ARRAY(dcu->dcu_sgls, nr);
	if (dcu->dcu_sgls == NULL)
		D_GOTO(fail, rc = -DER_NOMEM);

	if (tx->tx_flags & DAOS_TF_ZERO_COPY)
		rc = daos_sgls_copy_ptr(dcu->dcu_sgls, nr, sgls, nr);
	else
		rc = daos_sgls_copy_all(dcu->dcu_sgls, nr, sgls, nr);
	if (rc != 0)
		D_GOTO(fail, rc);

	tx->tx_write_cnt++;

	return 0;

fail:
	if (dcu != NULL) {
		if (dcu->dcu_iod_array != NULL) {
			for (i = 0; i < nr; i++) {
				daos_iov_free(&iod_array->oia_iods[i].iod_name);
				D_FREE(iod_array->oia_iods[i].iod_recxs);
			}

			D_FREE(dcu->dcu_iod_array);
		}

		if (dcu->dcu_sgls != NULL) {
			for (i = 0; i < nr; i++)
				daos_sgl_fini(&dcu->dcu_sgls[i],
					      !(tx->tx_flags &
						DAOS_TF_ZERO_COPY));

			D_FREE(dcu->dcu_sgls);
		}
	}

	daos_iov_free(&dcsr->dcsr_dkey);
	obj_decref(dcsr->dcsr_obj);

	return rc;
}

static int
dc_tx_add_punch_obj(struct dc_tx *tx, daos_handle_t oh, uint64_t flags)
{
	struct daos_cpd_sub_req	*dcsr;
	int			 rc;

	rc = dc_tx_get_next_slot(tx, false, &dcsr);
	if (rc != 0)
		return rc;

	dcsr->dcsr_obj = obj_hdl2ptr(oh);
	if (dcsr->dcsr_obj == NULL)
		return -DER_NO_HDL;

	dcsr->dcsr_opc = DCSO_PUNCH_OBJ;
	dcsr->dcsr_api_flags = flags;

	tx->tx_write_cnt++;

	return 0;
}

static int
dc_tx_add_punch_dkey(struct dc_tx *tx, daos_handle_t oh, uint64_t flags,
		     daos_key_t *dkey)
{
	struct daos_cpd_sub_req	*dcsr;
	int			 rc;

	rc = dc_tx_get_next_slot(tx, false, &dcsr);
	if (rc != 0)
		return rc;

	dcsr->dcsr_obj = obj_hdl2ptr(oh);
	if (dcsr->dcsr_obj == NULL)
		return -DER_NO_HDL;

	rc = daos_iov_copy(&dcsr->dcsr_dkey, dkey);
	if (rc != 0) {
		obj_decref(dcsr->dcsr_obj);
		return rc;
	}

	dcsr->dcsr_opc = DCSO_PUNCH_DKEY;
	dcsr->dcsr_dkey_hash = obj_dkey2hash(dkey);
	dcsr->dcsr_api_flags = flags;

	tx->tx_write_cnt++;

	return 0;
}

static int
dc_tx_add_punch_akeys(struct dc_tx *tx, daos_handle_t oh, uint64_t flags,
		      daos_key_t *dkey, uint32_t nr, daos_key_t *akeys)
{
	struct daos_cpd_sub_req	*dcsr = NULL;
	struct daos_cpd_punch	*dcp = NULL;
	int			 rc;
	int			 i;

	D_ASSERT(nr != 0);

	rc = dc_tx_get_next_slot(tx, false, &dcsr);
	if (rc != 0)
		return rc;

	dcsr->dcsr_obj = obj_hdl2ptr(oh);
	if (dcsr->dcsr_obj == NULL)
		return -DER_NO_HDL;

	rc = daos_iov_copy(&dcsr->dcsr_dkey, dkey);
	if (rc != 0)
		goto fail;

	dcp = &dcsr->dcsr_punch;
	D_ALLOC_ARRAY(dcp->dcp_akeys, nr);
	if (dcp->dcp_akeys == NULL)
		D_GOTO(fail, rc = -DER_NOMEM);

	for (i = 0; i < nr; i++) {
		rc = daos_iov_copy(&dcp->dcp_akeys[i], &akeys[i]);
		if (rc != 0)
			D_GOTO(fail, rc);
	}

	dcsr->dcsr_opc = DCSO_PUNCH_AKEY;
	dcsr->dcsr_nr = nr;
	dcsr->dcsr_dkey_hash = obj_dkey2hash(dkey);
	dcsr->dcsr_api_flags = flags;

	tx->tx_write_cnt++;

	return 0;

fail:
	if (dcp != NULL && dcp->dcp_akeys != NULL) {
		for (i = 0; i < nr; i++)
			daos_iov_free(&dcp->dcp_akeys[i]);

		D_FREE(dcp->dcp_akeys);
	}

	daos_iov_free(&dcsr->dcsr_dkey);
	obj_decref(dcsr->dcsr_obj);

	return rc;
}

static int
dc_tx_add_read(struct dc_tx *tx, daos_handle_t oh, uint64_t flags,
	       daos_key_t *dkey, uint32_t nr, void *iods_or_akey)
{
	struct daos_cpd_sub_req	*dcsr = NULL;
	struct daos_cpd_read	*dcr = NULL;
	int			 rc;
	int			 i;

	if (tx->tx_status != TX_OPEN)
		return 0;

	if (tx->tx_local && tx->tx_flags & DAOS_TF_RDONLY)
		return 0;

	rc = dc_tx_get_next_slot(tx, true, &dcsr);
	if (rc != 0)
		return rc;

	dcsr->dcsr_obj = obj_hdl2ptr(oh);
	if (dcsr->dcsr_obj == NULL)
		return -DER_NO_HDL;

	/* Set read TS on object shard. */
	if (dkey == NULL)
		goto done;

	rc = daos_iov_copy(&dcsr->dcsr_dkey, dkey);
	if (rc != 0)
		goto fail;

	/* Set read TS on dkey. */
	if (nr == 0)
		goto done;

	dcr = &dcsr->dcsr_read;
	D_ALLOC_ARRAY(dcr->dcr_iods, nr);
	if (dcr->dcr_iods == NULL)
		D_GOTO(fail, rc = -DER_NOMEM);

	/* If nr is 1, then the input @iods_or_akey is an akey.
	 * Otherwise, it is iods array for fetch case.
	 */
	if (nr == 1) {
		rc = daos_iov_copy(&dcr->dcr_iods[0].iod_name,
				   (daos_key_t *)iods_or_akey);
		if (rc != 0)
			D_GOTO(fail, rc);

		goto done;
	}

	for (i = 0; i < nr; i++) {
		rc = daos_iov_copy(&dcr->dcr_iods[i].iod_name,
				   &((daos_iod_t *)iods_or_akey)[i].iod_name);
		if (rc != 0)
			D_GOTO(fail, rc);
	}

done:
	dcsr->dcsr_opc = DCSO_READ;
	dcsr->dcsr_nr = nr;
	dcsr->dcsr_dkey_hash = obj_dkey2hash(dkey);
	dcsr->dcsr_api_flags = flags;

	tx->tx_read_cnt++;

	return 0;

fail:
	if (dcr != NULL && dcr->dcr_iods != NULL) {
		for (i = 0; i < nr; i++)
			daos_iov_free(&dcr->dcr_iods[i].iod_name);

		D_FREE(dcr->dcr_iods);
	}

	daos_iov_free(&dcsr->dcsr_dkey);
	obj_decref(dcsr->dcsr_obj);

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

	/* XXX: Use conditional fetch (with empty sgls) to check the target
	 *	existence on related server.
	 */
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
				      nr, DIOF_CHECK_EXISTENCE | DIOF_TO_LEADER,
				      iods, NULL, NULL, cb_args, NULL,
				      tse_task2sched(parent), &task);
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
dc_tx_attach(daos_handle_t th, enum obj_rpc_opc opc, tse_task_t *task)
{
	struct dc_tx	*tx;
	int		 rc;
	bool		 locked = true;

	rc = dc_tx_check(th, obj_is_modification_opc(opc) ? true : false, &tx);
	if (rc != 0)
		return rc;

	switch (opc) {
	case DAOS_OBJ_RPC_UPDATE: {
		daos_obj_update_t	*up = dc_task_get_args(task);

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
		daos_obj_punch_t	*pu = dc_task_get_args(task);

		D_ASSERTF(!(pu->flags & DAOS_COND_MASK),
			  "Unexpected cond flag %lx for punch obj\n",
			  pu->flags);

		rc = dc_tx_add_punch_obj(tx, pu->oh, pu->flags);
		break;
	}
	case DAOS_OBJ_RPC_PUNCH_DKEYS: {
		daos_obj_punch_t	*pu = dc_task_get_args(task);

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
		daos_obj_punch_t	*pu = dc_task_get_args(task);

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
	case DAOS_OBJ_RPC_FETCH: {
		daos_obj_fetch_t	*fe = dc_task_get_args(task);

		rc = dc_tx_add_read(tx, fe->oh, fe->flags, fe->dkey, fe->nr,
				    fe->nr != 1 ? fe->iods :
				    (void *)&fe->iods[0].iod_name);
		break;
	}
	case DAOS_OBJ_RPC_QUERY_KEY: {
		daos_obj_query_key_t	*qu = dc_task_get_args(task);
		daos_key_t		*dkey;
		uint32_t		 nr;

		if (qu->flags & DAOS_GET_DKEY) {
			dkey = NULL;
			nr = 0;
		} else if (qu->flags & DAOS_GET_AKEY) {
			dkey = qu->dkey;
			nr = 0;
		} else {
			dkey = qu->dkey;
			nr = 1;
		}

		rc = dc_tx_add_read(tx, qu->oh, 0, dkey, nr, qu->akey);
		break;
	}
	case DAOS_OBJ_RECX_RPC_ENUMERATE: {
		daos_obj_list_recx_t	*lr = dc_task_get_args(task);

		rc = dc_tx_add_read(tx, lr->oh, 0, lr->dkey, 1, lr->akey);
		break;
	}
	case DAOS_OBJ_AKEY_RPC_ENUMERATE: {
		daos_obj_list_akey_t	*la = dc_task_get_args(task);

		rc = dc_tx_add_read(tx, la->oh, 0, la->dkey, 0, NULL);
		break;
	}
	case DAOS_OBJ_DKEY_RPC_ENUMERATE: {
		daos_obj_list_dkey_t	*ld = dc_task_get_args(task);

		rc = dc_tx_add_read(tx, ld->oh, 0, NULL, 0, NULL);
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
