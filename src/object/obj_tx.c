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
#include "obj_ec.h"
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
	TX_ABORTED,	/**< no more new TX generations */
	TX_FAILED,	/**< may restart a new TX generation */
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
	uint32_t		 tx_local:1, /* Local TX. */
				 tx_retry:1, /** Retry the commit RPC. */
				 tx_set_resend:1; /** Set 'resend' flag. */
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

	struct daos_cpd_sg	 tx_head;
	struct daos_cpd_sg	 tx_reqs;
	struct daos_cpd_sg	 tx_disp;
	struct daos_cpd_sg	 tx_tgts;
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
	    bool local, struct dc_tx **ptx)
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

	if (local)
		tx->tx_local = 1;
	daos_dti_gen(&tx->tx_id, local);

	if (epoch == 0) {
		/* The epoch will be generated by the first accessed server. */
		tx->tx_epoch.oe_first = 0;
		if (DAOS_FAIL_CHECK(DAOS_DTX_SPEC_EPOCH)) {
			tx->tx_epoch.oe_value = daos_fail_value_get();
			tx->tx_epoch.oe_flags = 0;
		} else {
			tx->tx_epoch.oe_value = 0;
			tx->tx_epoch.oe_flags = DTX_EPOCH_UNCERTAIN;
		}
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
		struct obj_reasb_req	*reasb_req = dcsr->dcsr_reasb;
		struct daos_cpd_update	*dcu = &dcsr->dcsr_update;
		struct obj_iod_array	*iod_array = &dcu->dcu_iod_array;
		struct daos_csummer	*csummer;

		csummer = dc_cont_hdl2csummer(tx->tx_coh);

		if (dcu->dcu_flags & DRF_CPD_BULK) {
			for (i = 0; i < dcsr->dcsr_nr; i++) {
				if (dcu->dcu_bulks[i] != CRT_BULK_NULL)
					crt_bulk_free(dcu->dcu_bulks[i]);
			}

			D_FREE(dcu->dcu_bulks);
		}

		daos_csummer_free_ci(csummer, &dcu->dcu_dkey_csum);
		D_FREE(dcu->dcu_ec_tgts);

		if (reasb_req != NULL) {
			if (reasb_req->orr_uiods != NULL) {
				dcu->dcu_iod_array.oia_iods =
							reasb_req->orr_uiods;
				dcsr->dcsr_sgls = reasb_req->orr_usgls;
			}

			obj_reasb_req_fini(reasb_req, dcsr->dcsr_nr);
			D_FREE(dcsr->dcsr_reasb);
		}

		if (iod_array->oia_iods != NULL) {
			for (i = 0; i < dcsr->dcsr_nr; i++) {
				daos_iov_free(&iod_array->oia_iods[i].iod_name);
				D_FREE(iod_array->oia_iods[i].iod_recxs);
			}

			D_FREE(iod_array->oia_iods);
		}

		daos_csummer_free_ic(csummer, &iod_array->oia_iod_csums);
		D_ASSERT(iod_array->oia_offs == NULL);

		if (dcsr->dcsr_sgls != NULL) {
			for (i = 0; i < dcsr->dcsr_nr; i++)
				daos_sgl_fini(&dcsr->dcsr_sgls[i],
					      !(tx->tx_flags &
						DAOS_TF_ZERO_COPY));

			D_FREE(dcsr->dcsr_sgls);
		}

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

static uint32_t
dc_tx_first_req(struct dc_tx *tx)
{
	uint32_t	idx;

	if (tx->tx_flags & DAOS_TF_RDONLY)
		idx = tx->tx_total_slots - tx->tx_read_cnt;
	else if (tx->tx_total_slots > DTX_SUB_WRITE_MAX)
		idx = tx->tx_total_slots - DTX_SUB_WRITE_MAX - tx->tx_read_cnt;
	else
		idx = (tx->tx_total_slots >> 1) - tx->tx_read_cnt;

	return idx;
}

static void
dc_tx_cleanup(struct dc_tx *tx)
{
	struct daos_cpd_sub_head	*dcsh = tx->tx_head.dcs_buf;
	struct daos_cpd_disp_ent	*dcde = tx->tx_disp.dcs_buf;
	uint32_t			 from;
	uint32_t			 to;
	uint32_t			 i;

	from = dc_tx_first_req(tx);
	to = from + tx->tx_read_cnt + tx->tx_write_cnt;
	for (i = from; i < to; i++)
		dc_tx_cleanup_one(tx, &tx->tx_req_cache[i]);

	tx->tx_read_cnt = 0;
	tx->tx_write_cnt = 0;
	tx->tx_retry = 0;

	/* Keep 'tx_set_resend'. */

	if (dcsh != NULL) {
		D_FREE(dcsh->dcsh_mbs);
		D_FREE(tx->tx_head.dcs_buf);
	}

	if (dcde != NULL) {
		for (i = 0; i < tx->tx_disp.dcs_nr; i++)
			D_FREE(dcde[i].dcde_reqs);
		D_FREE(tx->tx_disp.dcs_buf);
	}

	D_FREE(tx->tx_tgts.dcs_buf);
}

/**
 * End a TX operation associated with \a th.
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

	if (rep_rc != -DER_TX_RESTART &&
	    (dtx_epoch_chosen(req_epoch) || rep_epoch == 0))
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

	if (rep_rc == -DER_TX_RESTART)
		tx->tx_status = TX_FAILED;

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

	if (ptx == NULL && DAOS_FAIL_CHECK(DAOS_DTX_STALE_PM)) {
		tx->tx_status = TX_FAILED;
		rc = -DER_TX_RESTART;
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

	rc = dc_tx_alloc(args->coh, 0, args->flags, false, &tx);
	if (rc == 0)
		*args->th = dc_tx_ptr2hdl(tx);

	tse_task_complete(task, rc);

	return rc;
}

struct tx_commit_cb_args {
	struct dc_tx		*tcca_tx;
	crt_rpc_t		*tcca_req;
	daos_tx_commit_t	*tcca_args;
};

static int
dc_tx_commit_cb(tse_task_t *task, void *data)
{
	struct tx_commit_cb_args *tcca = data;
	struct dc_tx		 *tx = tcca->tcca_tx;
	crt_rpc_t		 *req = tcca->tcca_req;
	struct obj_cpd_out	 *oco = crt_reply_get(req);
	tse_task_t		 *pool_task = NULL;
	int			  rc = task->dt_result;
	int			  rc1;
	bool			  locked = true;

	D_MUTEX_LOCK(&tx->tx_lock);

	if (rc == 0) {
		rc = oco->oco_ret;
		if (rc == 0) {
			int	*sub_rets = oco->oco_sub_rets.ca_arrays;

			/* FIXME: currently, we pack one DTX per CPD RPC. */
			rc = sub_rets[0];
		}
	}

	if (rc == 0) {
		uint64_t	*sub_epochs = oco->oco_sub_epochs.ca_arrays;

		tx->tx_status = TX_COMMITTED;
		dc_tx_cleanup(tx);

		/* FIXME: currently, we pack one DTX per CPD RPC. */

		if (tx->tx_epoch.oe_value == 0) {
			if (sub_epochs[0] == 0) {
				D_WARN("Server forgot to reply epoch for TX "
				       DF_DTI"\n", DP_DTI(&tx->tx_id));
			} else {
				tx->tx_epoch.oe_value = sub_epochs[0];
				tx->tx_epoch.oe_flags &= ~DTX_EPOCH_UNCERTAIN;
			}
		} else if (tx->tx_epoch.oe_value != sub_epochs[0]) {
			D_WARN("Server replied different epoch for TX "DF_DTI
			       ": c "DF_U64", s "DF_U64"\n", DP_DTI(&tx->tx_id),
			       tx->tx_epoch.oe_value, sub_epochs[0]);
		} else {
			tx->tx_epoch.oe_flags &= ~DTX_EPOCH_UNCERTAIN;
		}

		goto out;
	}

	if (rc != -DER_TX_RESTART && !obj_retry_error(rc)) {
		tx->tx_retry = 0;
		tx->tx_status = TX_ABORTED;

		goto out;
	}

	/* Need to refresh the local pool map. */
	if (tx->tx_pm_ver < oco->oco_map_version) {
		struct daos_cpd_sub_req		*dcsr;

		dcsr = &tx->tx_req_cache[dc_tx_first_req(tx)];
		tx->tx_pm_ver = oco->oco_map_version;
		rc1 = obj_pool_query_task(tse_task2sched(task), dcsr->dcsr_obj,
					  &pool_task);
		if (rc1 != 0) {
			D_ERROR("Failed to refresh the pool map: "
				DF_RC", original error: "DF_RC"\n",
				DP_RC(rc1), DP_RC(rc));
			tx->tx_status = TX_ABORTED;
			D_GOTO(out, rc = rc1);
		}
	}

	/* Need to restart the TX with newer epoch. */
	if (rc == -DER_TX_RESTART || rc == -DER_STALE) {
		tx->tx_set_resend = 1;
		tx->tx_status = TX_FAILED;

		if (pool_task != NULL) {
			D_MUTEX_UNLOCK(&tx->tx_lock);
			locked = false;

			dc_task_schedule(pool_task, true);
		}

		D_GOTO(out, rc = -DER_TX_RESTART);
	}

	tx->tx_retry = 1;
	tx->tx_set_resend = 1;
	tcca->tcca_args->flags |= DTF_RETRY_COMMIT;

	D_MUTEX_UNLOCK(&tx->tx_lock);
	locked = false;

	if (pool_task != NULL) {
		rc1 = dc_task_depend(task, 1, &pool_task);
		if (rc1 != 0) {
			D_ERROR("Failed to add dependency on pool query: "
				DF_RC", original error: "DF_RC"\n",
				DP_RC(rc1), DP_RC(rc));
			dc_task_decref(pool_task);
			tx->tx_status = TX_ABORTED;

			D_GOTO(out, rc = rc1);
		}
	} else {
		rc1 = dc_task_resched(task);
		if (rc1 != 0) {
			D_ERROR("Failed to re-init task (%p): "
				DF_RC", original error: "DF_RC"\n",
				task, DP_RC(rc1), DP_RC(rc));
			tx->tx_status = TX_ABORTED;

			D_GOTO(out, rc = rc1);
		}
	}

	D_GOTO(out, rc = 0);

out:
	if (locked)
		D_MUTEX_UNLOCK(&tx->tx_lock);

	if (rc != 0)
		task->dt_result = rc;

	crt_req_decref(req);
	/* -1 for dc_tx_commit() held */
	dc_tx_decref(tx);

	return 0;
}

struct dc_tx_req_group {
	uint32_t			 dtrg_rank;
	uint32_t			 dtrg_tgt_idx;
	uint32_t			 dtrg_read_cnt;
	uint32_t			 dtrg_write_cnt;
	uint32_t			 dtrg_slot_cnt;
	struct daos_cpd_req_idx		*dtrg_req_idx;
};

struct dc_tx_rdg {
	d_list_t			 dtr_link;
	struct dtx_redundancy_group	 dtr_group;
};

static int
tx_bulk_prepare(struct daos_cpd_sub_req *dcsr, tse_task_t *task)
{
	struct daos_cpd_update		*dcu = &dcsr->dcsr_update;
	int				 rc;

	/* For most of cases, the leader will dispatch the sub
	 * request to other servers, then always use bind mode
	 * for bulk data transfer. It is not optimized, but it
	 * simplifies the logic.
	 */
	rc = obj_bulk_prep(dcsr->dcsr_sgls, dcsr->dcsr_nr, true,
			   CRT_BULK_RO, task, &dcu->dcu_bulks);
	if (rc == 0)
		dcu->dcu_flags |= ORF_BULK_BIND | DRF_CPD_BULK;

	return rc;
}

/* Classify the update sub request. It is unnecessary to cleanup when
 * hit failure. That will be done via dc_tx_cleanup() sometime later.
 *
 * Return sgl size or negative value on error.
 */
static int
dc_tx_classify_update(struct dc_tx *tx, struct daos_cpd_sub_req *dcsr,
		      struct daos_csummer *csummer)
{
	struct daos_cpd_update		*dcu = &dcsr->dcsr_update;
	struct dc_object		*obj = dcsr->dcsr_obj;
	struct dcs_layout		*singv_los = NULL;
	struct daos_oclass_attr		*oca = NULL;
	int				 rc = 0;

	if (daos_oclass_is_ec(obj->cob_md.omd_id, &oca)) {
		struct obj_reasb_req	*reasb_req;

		D_ALLOC_PTR(reasb_req);
		if (reasb_req == NULL)
			return rc = -DER_NOMEM;

		dcu->dcu_flags |= ORF_EC;
		/* dcsr->dcsr_reasb will be released via
		 * dc_tx_cleanup().
		 */
		dcsr->dcsr_reasb = reasb_req;
		rc = obj_reasb_req_init(dcsr->dcsr_reasb,
					dcu->dcu_iod_array.oia_iods,
					dcsr->dcsr_nr, oca);
		if (rc != 0)
			return rc;

		rc = obj_ec_req_reasb(dcu->dcu_iod_array.oia_iods,
				      dcsr->dcsr_sgls, obj->cob_md.omd_id, oca,
				      dcsr->dcsr_reasb, dcsr->dcsr_nr, true);
		if (rc != 0)
			return rc;

		singv_los = reasb_req->orr_singv_los;

		D_ASSERT(dcu->dcu_iod_array.oia_iods == reasb_req->orr_uiods);
		D_ASSERT(dcsr->dcsr_sgls == reasb_req->orr_usgls);

		/* Overwrite the dcu->dcu_iod_array.oia_iods */
		if (reasb_req->orr_iods != NULL)
			dcu->dcu_iod_array.oia_iods = reasb_req->orr_iods;

		/* Overwrite the dcsr->dcsr_sgls */
		if (reasb_req->orr_sgls != NULL)
			dcsr->dcsr_sgls = reasb_req->orr_sgls;

		dcu->dcu_iod_array.oia_oiods = reasb_req->orr_oiods;
	} else {
		dcu->dcu_iod_array.oia_oiods = NULL;
	}

	dcu->dcu_iod_array.oia_offs = NULL;

	if (dcu->dcu_iod_array.oia_oiods != NULL)
		dcu->dcu_iod_array.oia_oiod_nr = dcsr->dcsr_nr;
	else
		dcu->dcu_iod_array.oia_oiod_nr = 0;

	if (daos_csummer_initialized(csummer)) {
		rc = daos_csummer_calc_key(csummer, &dcsr->dcsr_dkey,
					   &dcu->dcu_dkey_csum);
		if (rc != 0)
			return rc;

		rc = daos_csummer_calc_iods(csummer, dcsr->dcsr_sgls,
					    dcu->dcu_iod_array.oia_iods, NULL,
					    dcsr->dcsr_nr, false, singv_los, -1,
					    &dcu->dcu_iod_array.oia_iod_csums);
	}

	if (rc == 0)
		rc = daos_sgls_packed_size(dcsr->dcsr_sgls,
					   dcsr->dcsr_nr, NULL);

	return rc;
}

static int
dc_tx_classify_common(struct dc_tx *tx, struct daos_cpd_sub_req *dcsr,
		      struct dc_tx_req_group *dtrgs, int dtrg_nr,
		      uint32_t grp_idx, uint32_t req_idx, bool read, bool all,
		      uint32_t *leader_dtrg_idx, uint32_t *act_tgt_cnt,
		      d_list_t *dtr_list, daos_unit_oid_t *leader_oid)
{
	struct dc_object	*obj = dcsr->dcsr_obj;
	struct dc_obj_shard	*shard;
	struct dc_tx_req_group	*dtrg;
	struct dc_tx_req_group	*tmp;
	struct daos_cpd_req_idx	*dcri;
	struct dc_tx_rdg	*dtr;
	struct dc_tx_rdg	*leader_dtr;
	struct daos_oclass_attr	*oca;
	struct daos_cpd_update	*dcu = NULL;
	struct obj_reasb_req	*reasb_req = NULL;
	uint32_t		 size;
	int			 skipped_parity = 0;
	int			 handled = 0;
	int			 start;
	int			 rc = 0;
	int			 idx;

	if (d_list_empty(dtr_list))
		leader_dtr = NULL;
	else
		leader_dtr = d_list_entry(dtr_list->next, struct dc_tx_rdg,
					  dtr_link);

	oca = daos_oclass_attr_find(obj->cob_md.omd_id);
	size = sizeof(*dtr) + sizeof(uint32_t) * obj->cob_grp_size;
	D_ALLOC(dtr, size);
	if (dtr == NULL)
		return -DER_NOMEM;

	start = grp_idx * obj->cob_grp_size;
	dcsr->dcsr_ec_tgt_nr = 0;

	if (dcsr->dcsr_opc == DCSO_UPDATE) {
		dcu = &dcsr->dcsr_update;
		reasb_req = dcsr->dcsr_reasb;
		if (dcu->dcu_flags & ORF_EC && reasb_req->tgt_bitmap != NULL) {
			D_ALLOC_ARRAY(dcu->dcu_ec_tgts, obj->cob_grp_size);
			if (dcu->dcu_ec_tgts == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			dcu->dcu_start_shard = start;
		}
	}

	/* Descending order to guarantee that EC parity is handled firstly. */
	for (idx = start + obj->cob_grp_size - 1; idx >= start; idx--) {
		if (reasb_req != NULL &&
		    !isset(reasb_req->tgt_bitmap, idx - start))
			continue;

		rc = obj_shard_open(obj, idx, tx->tx_pm_ver, &shard);
		if (rc == -DER_NONEXIST) {
			if (oca->ca_resil == DAOS_RES_EC && !all) {
				if (idx >= start + obj->cob_grp_size -
							oca->u.ec.e_p)
					skipped_parity++;

				if (skipped_parity == oca->u.ec.e_p) {
					D_ERROR("Two many (%d) shards in the "
						"redundancy group for opc %u "
						"against the obj "DF_OID
						" for DTX "DF_DTI" are lost\n",
						skipped_parity + 1,
						dcsr->dcsr_opc,
						DP_OID(obj->cob_md.omd_id),
						DP_DTI(&tx->tx_id));
					D_GOTO(out, rc = -DER_IO);
				}
			}

			continue;
		}

		if (rc != 0)
			goto out;

		D_ASSERTF(idx == shard->do_shard,
			  "Invalid shard: idx %u, shard %u\n",
			  idx, shard->do_shard);

		D_ASSERTF(shard->do_target_id < dtrg_nr,
			  "Invalid target index: idx %u, targets %u\n",
			  shard->do_target_id, dtrg_nr);

		dtrg = &dtrgs[shard->do_target_id];
		if (dtrg->dtrg_req_idx == NULL) {
			/* dtrg->dtrg_req_idx will be released by caller. */
			D_ALLOC_ARRAY(dtrg->dtrg_req_idx, DTX_SUB_REQ_DEF);
			if (dtrg->dtrg_req_idx == NULL) {
				obj_shard_close(shard);
				D_GOTO(out, rc = -DER_NOMEM);
			}

			dtrg->dtrg_rank = shard->do_target_rank;
			dtrg->dtrg_tgt_idx = shard->do_target_idx;
			dtrg->dtrg_slot_cnt = DTX_SUB_REQ_DEF;
			(*act_tgt_cnt)++;
		} else {
			D_ASSERTF(dtrg->dtrg_rank == shard->do_target_rank,
				  "Invalid target rank for shard ID %u: "
				  "rank1 %u, rank2 %u\n", shard->do_target_id,
				  shard->do_target_rank, dtrg->dtrg_rank);

			D_ASSERTF(dtrg->dtrg_tgt_idx == shard->do_target_idx,
				  "Invalid target index for shard ID %u: "
				  "idx1 %u, idx2 %u\n", shard->do_target_id,
				  shard->do_target_idx, dtrg->dtrg_tgt_idx);
		}

		if ((dtrg->dtrg_read_cnt + dtrg->dtrg_write_cnt) >=
		    dtrg->dtrg_slot_cnt) {
			D_ALLOC_ARRAY(dcri, dtrg->dtrg_slot_cnt << 1);
			if (dcri == NULL) {
				obj_shard_close(shard);
				D_GOTO(out, rc = -DER_NOMEM);
			}

			memcpy(dcri, dtrg->dtrg_req_idx,
			       sizeof(*dcri) * dtrg->dtrg_slot_cnt);
			D_FREE(dtrg->dtrg_req_idx);
			dtrg->dtrg_req_idx = dcri;
			dtrg->dtrg_slot_cnt <<= 1;
		}

		dcri = &dtrg->dtrg_req_idx[dtrg->dtrg_read_cnt +
					   dtrg->dtrg_write_cnt];
		dcri->dcri_shard_idx = idx;
		dcri->dcri_req_idx = req_idx;

		if (read)
			dtrg->dtrg_read_cnt++;
		else
			dtrg->dtrg_write_cnt++;

		/* XXX: Rules for electing leader:
		 *
		 * R1: For non read-only TX, the DAOS target that only contains
		 *     read sub requests will not be as the leader.
		 *
		 * R2: The DAOS target that holds the most sub requests will be
		 *     elected as the leader.
		 *
		 * R3: If more than one DAOS targets have the some count of sub
		 *     requests, then the 1st parsed one will be elected as the
		 *     leader. That depends on the sub request operation object
		 *     and dkey. It is random from the whole system perspective.
		 *     So it is helpful for the server load balance. But it may
		 *     affect the degree of leader async DTX batched commit.
		 *
		 * R4: The replicas count and redundancy groups count have very
		 *     limited influence on DTX recovery. Because as long as if
		 *     the DTX participants belong to the same redundancy group
		 *     are all unavailable, in spite of which redundancy group,
		 *     and in spite of where the leader is, such DTX cannot be
		 *     recovered.
		 */
		tmp = &dtrgs[*leader_dtrg_idx];
		if (dtrg->dtrg_write_cnt != 0) {
			if ((dtrg->dtrg_read_cnt + dtrg->dtrg_write_cnt) >
			    (tmp->dtrg_read_cnt + tmp->dtrg_write_cnt)) {
				*leader_dtrg_idx = shard->do_target_id;
				leader_dtr = dtr;
				leader_oid->id_pub = obj->cob_md.omd_id;
				leader_oid->id_shard = idx;
			}
		} else if (tmp->dtrg_write_cnt == 0) {
			if (dtrg->dtrg_read_cnt > tmp->dtrg_read_cnt) {
				*leader_dtrg_idx = shard->do_target_id;
				leader_dtr = dtr;
				leader_oid->id_pub = obj->cob_md.omd_id;
				leader_oid->id_shard = idx;
			}
		}

		if (leader_dtr == NULL) {
			*leader_dtrg_idx = shard->do_target_id;
			leader_dtr = dtr;
			leader_oid->id_pub = obj->cob_md.omd_id;
			leader_oid->id_shard = idx;
		}

		if (dcu != NULL && dcu->dcu_ec_tgts != NULL) {
			dcu->dcu_ec_tgts[dcsr->dcsr_ec_tgt_nr].dcet_shard_idx =
							idx;
			dcu->dcu_ec_tgts[dcsr->dcsr_ec_tgt_nr++].dcet_tgt_id =
							shard->do_target_id;
		}

		dtr->dtr_group.drg_ids[dtr->dtr_group.drg_tgt_cnt++] =
							shard->do_target_id;
		obj_shard_close(shard);
		handled++;
	}

	if (handled == 0) {
		D_ERROR("All shards in the redundancy group for the opc %u "
			"against the obj "DF_OID" for DTX "DF_DTI" are lost\n",
			dcsr->dcsr_opc, DP_OID(obj->cob_md.omd_id),
			DP_DTI(&tx->tx_id));
		D_GOTO(out, rc = -DER_IO);
	}

	if (oca->ca_resil == DAOS_RES_EC && !all) {
		dtr->dtr_group.drg_redundancy = oca->u.ec.e_p + 1;
		D_ASSERT(dtr->dtr_group.drg_redundancy <= obj->cob_grp_size);
	} else {
		dtr->dtr_group.drg_redundancy = dtr->dtr_group.drg_tgt_cnt;
	}

	if (leader_dtr == dtr)
		d_list_add(&dtr->dtr_link, dtr_list);
	else
		d_list_add_tail(&dtr->dtr_link, dtr_list);

out:
	if (rc == -DER_NONEXIST)
		rc = 0;

	if (rc != 0)
		D_FREE(dtr);

	return rc;
}

static bool
dc_tx_same_rdg(struct dtx_redundancy_group *grp1,
	       struct dtx_redundancy_group *grp2)
{
	int	i;

	if (grp1->drg_tgt_cnt != grp2->drg_tgt_cnt)
		return false;

	if (grp1->drg_redundancy != grp2->drg_redundancy)
		return false;

	/* FIXME: The comparison between two ID arrays are unsorted.
	 *	  So for the case of ID1 = {1,2,3} and ID2 = {3,1,2}
	 *	  will get 'false' result. It will cause some space
	 *	  overhead, but not fatal.
	 */
	for (i = 0; i < grp1->drg_tgt_cnt; i++) {
		if (grp1->drg_ids[i] != grp2->drg_ids[i])
			return false;
	}

	return true;
}

static size_t
dc_tx_reduce_rdgs(d_list_t *dtr_list, uint32_t *grp_cnt)
{
	struct dc_tx_rdg	*dtr;
	struct dc_tx_rdg	*tmp;
	struct dc_tx_rdg	*next;
	struct dc_tx_rdg	*leader;
	size_t			 size = 0;

	*grp_cnt = 0;
	leader = d_list_entry(dtr_list->next, struct dc_tx_rdg, dtr_link);
	d_list_del(&leader->dtr_link);

	/* Filter the dtrs that are the same as @leader. */
	d_list_for_each_entry_safe(dtr, next, dtr_list, dtr_link) {
		if (dc_tx_same_rdg(&leader->dtr_group, &dtr->dtr_group)) {
			d_list_del(&dtr->dtr_link);
			D_FREE(dtr);
		}
	}

	if (d_list_empty(dtr_list))
		goto out;

	tmp = d_list_entry(dtr_list->next, struct dc_tx_rdg, dtr_link);
	d_list_del(&tmp->dtr_link);

	/* XXX: Try to merge the other non-leaders if possible.
	 *	Consider efficiency, just one cycle scan. We do
	 *	NOT guarantee all mergeable ones will be merged.
	 */
	d_list_for_each_entry_safe(dtr, next, dtr_list, dtr_link) {
		if (dc_tx_same_rdg(&tmp->dtr_group, &dtr->dtr_group)) {
			d_list_del(&dtr->dtr_link);
			D_FREE(dtr);
		} else {
			size += sizeof(struct dtx_redundancy_group) +
				sizeof(uint32_t) * dtr->dtr_group.drg_tgt_cnt;
			(*grp_cnt)++;
		}
	}

	d_list_add(&tmp->dtr_link, dtr_list);
	size += sizeof(struct dtx_redundancy_group) +
		sizeof(uint32_t) * tmp->dtr_group.drg_tgt_cnt;
	(*grp_cnt)++;

out:
	d_list_add(&leader->dtr_link, dtr_list);
	size += sizeof(struct dtx_redundancy_group) +
		sizeof(uint32_t) * leader->dtr_group.drg_tgt_cnt;
	(*grp_cnt)++;

	return size;
}

static void
dc_tx_dump(struct dc_tx *tx)
{
	D_DEBUG(DB_TRACE,
		"Dump TX %p:\n"
		"ID: "DF_DTI"\n"
		"epoch: "DF_U64"\n"
		"flags: "DF_U64"\n"
		"pm_ver: %u\n"
		"leader: %u/%u\n"
		"read_cnt: %u\n"
		"write_cnt: %u\n"
		"head: %p/%u\n"
		"reqs: %p/%u\n"
		"disp: %p/%u\n"
		"tgts: %p/%u\n",
		tx, DP_DTI(&tx->tx_id), tx->tx_epoch.oe_value, tx->tx_flags,
		tx->tx_pm_ver, tx->tx_leader_rank, tx->tx_leader_tag,
		tx->tx_read_cnt, tx->tx_write_cnt,
		tx->tx_head.dcs_buf, tx->tx_head.dcs_nr,
		tx->tx_reqs.dcs_buf, tx->tx_reqs.dcs_nr,
		tx->tx_disp.dcs_buf, tx->tx_disp.dcs_nr,
		tx->tx_tgts.dcs_buf, tx->tx_tgts.dcs_nr);
}

static int
dc_tx_commit_prepare(struct dc_tx *tx, tse_task_t *task)
{
	daos_unit_oid_t			 leader_oid = { 0 };
	struct daos_csummer		*csummer;
	struct dc_tx_req_group		*dtrgs = NULL;
	struct daos_cpd_sub_head	*dcsh = NULL;
	struct daos_cpd_disp_ent	*dcdes = NULL;
	struct daos_shard_tgt		*shard_tgts = NULL;
	struct dtx_memberships		*mbs;
	struct dtx_daos_target		*ddt;
	struct dc_tx_rdg		*dtr;
	void				*ptr;
	d_list_t			 dtr_list;
	size_t				 size;
	uint32_t			 leader_dtrg_idx = 0;
	uint32_t			 act_tgt_cnt = 0;
	uint32_t			 act_grp_cnt = 0;
	uint32_t			 start;
	uint32_t			 tgt_cnt;
	uint32_t			 req_cnt;
	int				 rc = 0;
	int				 i;
	int				 j;

	D_INIT_LIST_HEAD(&dtr_list);
	csummer = dc_cont_hdl2csummer(tx->tx_coh);
	req_cnt = tx->tx_read_cnt + tx->tx_write_cnt;
	tgt_cnt = pool_map_target_nr(tx->tx_pool->dp_map);
	D_ASSERT(tgt_cnt != 0);

	start = dc_tx_first_req(tx);
	D_ALLOC_ARRAY(dtrgs, tgt_cnt);
	if (dtrgs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < req_cnt; i++) {
		struct daos_cpd_sub_req	*dcsr = &tx->tx_req_cache[i + start];
		struct dc_object	*obj = dcsr->dcsr_obj;

		if (dcsr->dcsr_opc == DCSO_UPDATE) {
			rc = dc_tx_classify_update(tx, dcsr, csummer);
			if (rc < 0)
				goto out;

			if (rc > (OBJ_BULK_LIMIT >> 2)) {
				rc = tx_bulk_prepare(dcsr, task);
				if (rc != 0)
					goto out;
			} else {
				dcsr->dcsr_update.dcu_sgls = dcsr->dcsr_sgls;
			}
		}

		if (dcsr->dcsr_opc == DCSO_PUNCH_OBJ) {
			for (j = 0; j < obj->cob_grp_nr; j++) {
				rc = dc_tx_classify_common(tx, dcsr, dtrgs,
						tgt_cnt, j, i, false, true,
						&leader_dtrg_idx, &act_tgt_cnt,
						&dtr_list, &leader_oid);
				if (rc != 0)
					goto out;
			}
		} else {
			rc = obj_dkey2grpidx(obj, dcsr->dcsr_dkey_hash,
					     tx->tx_pm_ver);
			if (rc < 0)
				goto out;

			rc = dc_tx_classify_common(tx, dcsr, dtrgs, tgt_cnt, rc,
					i, dcsr->dcsr_opc == DCSO_READ, false,
					&leader_dtrg_idx, &act_tgt_cnt,
					&dtr_list, &leader_oid);
			if (rc != 0)
				goto out;
		}
	}

	size = dc_tx_reduce_rdgs(&dtr_list, &act_grp_cnt);
	size += sizeof(*ddt) * act_tgt_cnt;

	D_ALLOC_PTR(dcsh);
	if (dcsh == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC(dcsh->dcsh_mbs, size + sizeof(*dcsh->dcsh_mbs));
	if (dcsh->dcsh_mbs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC_ARRAY(dcdes, act_tgt_cnt);
	if (dcdes == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC_ARRAY(shard_tgts, act_tgt_cnt);
	if (shard_tgts == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dcsh->dcsh_xid = tx->tx_id;
	dcsh->dcsh_leader_oid = leader_oid;
	dcsh->dcsh_epoch = tx->tx_epoch;
	if (tx->tx_epoch.oe_flags & DTX_EPOCH_UNCERTAIN)
		dcsh->dcsh_epoch.oe_rpc_flags |= ORF_EPOCH_UNCERTAIN;
	else
		dcsh->dcsh_epoch.oe_rpc_flags &= ~ORF_EPOCH_UNCERTAIN;

	mbs = dcsh->dcsh_mbs;
	mbs->dm_tgt_cnt = act_tgt_cnt;
	mbs->dm_grp_cnt = act_grp_cnt;
	mbs->dm_data_size = size;

	ddt = &mbs->dm_tgts[0];
	ddt->ddt_id = leader_dtrg_idx;
	if (dtrgs[leader_dtrg_idx].dtrg_write_cnt == 0)
		ddt->ddt_flags = DTF_RDONLY;
	ddt++;

	dcdes[0].dcde_read_cnt = dtrgs[leader_dtrg_idx].dtrg_read_cnt;
	dcdes[0].dcde_write_cnt = dtrgs[leader_dtrg_idx].dtrg_write_cnt;
	dcdes[0].dcde_reqs = dtrgs[leader_dtrg_idx].dtrg_req_idx;
	dtrgs[leader_dtrg_idx].dtrg_req_idx = NULL;

	shard_tgts[0].st_rank = dtrgs[leader_dtrg_idx].dtrg_rank;
	shard_tgts[0].st_tgt_id = leader_dtrg_idx;
	shard_tgts[0].st_tgt_idx = dtrgs[leader_dtrg_idx].dtrg_tgt_idx;

	for (i = 0, j = 1; i < tgt_cnt; i++) {
		if (dtrgs[i].dtrg_req_idx == NULL || i == leader_dtrg_idx)
			continue;

		ddt->ddt_id = i;
		if (dtrgs[i].dtrg_write_cnt == 0)
			ddt->ddt_flags = DTF_RDONLY;
		ddt++;

		dcdes[j].dcde_read_cnt = dtrgs[i].dtrg_read_cnt;
		dcdes[j].dcde_write_cnt = dtrgs[i].dtrg_write_cnt;
		dcdes[j].dcde_reqs = dtrgs[i].dtrg_req_idx;
		dtrgs[i].dtrg_req_idx = NULL;

		shard_tgts[j].st_rank = dtrgs[i].dtrg_rank;
		shard_tgts[j].st_tgt_id = i;
		shard_tgts[j].st_tgt_idx = dtrgs[i].dtrg_tgt_idx;
		j++;
	}

	ptr = ddt;
	while ((dtr = d_list_pop_entry(&dtr_list, struct dc_tx_rdg,
				       dtr_link)) != NULL) {
		size = sizeof(dtr->dtr_group) +
		       sizeof(uint32_t) * dtr->dtr_group.drg_tgt_cnt;
		memcpy(ptr, &dtr->dtr_group, size);
		ptr += size;
		D_FREE(dtr);
	}

	tx->tx_reqs.dcs_type = DCST_REQ_CLI;
	tx->tx_reqs.dcs_nr = req_cnt;
	tx->tx_reqs.dcs_buf = tx->tx_req_cache + start;

	tx->tx_disp.dcs_type = DCST_DISP;
	tx->tx_disp.dcs_nr = act_tgt_cnt;
	tx->tx_disp.dcs_buf = dcdes;

	tx->tx_tgts.dcs_type = DCST_TGT;
	tx->tx_tgts.dcs_nr = act_tgt_cnt;
	tx->tx_tgts.dcs_buf = shard_tgts;

	tx->tx_head.dcs_type = DCST_HEAD;
	tx->tx_head.dcs_nr = 1;
	tx->tx_head.dcs_buf = dcsh;


	/* XXX: Currently, we only pack single DTX per CPD RPC, then elect
	 *	the first targets in the dispatch list as the leader.
	 */

	tx->tx_leader_rank = shard_tgts[0].st_rank;
	tx->tx_leader_tag = shard_tgts[0].st_tgt_idx;

	dc_tx_dump(tx);

out:
	if (rc < 0) {
		for (i = 0; i < tgt_cnt; i++)
			D_FREE(dtrgs[i].dtrg_req_idx);

		for (i = 0; i < act_tgt_cnt; i++)
			D_FREE(dcdes[i].dcde_reqs);

		D_FREE(dcdes);
		D_FREE(shard_tgts);
		D_FREE(dcsh->dcsh_mbs);
		D_FREE(dcsh);
	}

	while ((dtr = d_list_pop_entry(&dtr_list, struct dc_tx_rdg,
				       dtr_link)) != NULL)
		D_FREE(dtr);

	D_FREE(dtrgs);

	return rc < 0 ? rc : 0;
}

static int
dc_tx_commit_trigger(tse_task_t *task, struct dc_tx *tx, daos_tx_commit_t *args)
{
	crt_rpc_t			*req = NULL;
	struct obj_cpd_in		*oci;
	struct tx_commit_cb_args	 tcca;
	crt_endpoint_t			 tgt_ep;
	int				 rc;

	if (!tx->tx_retry) {
		rc = dc_tx_commit_prepare(tx, task);
		if (rc != 0) {
			if (rc == -DER_STALE)
				rc = -DER_TX_RESTART;

			goto out;
		}
	}

	tgt_ep.ep_grp = tx->tx_pool->dp_sys->sy_group;
	tgt_ep.ep_tag = tx->tx_leader_tag;
	tgt_ep.ep_rank = tx->tx_leader_rank;

	rc = obj_req_create(daos_task2ctx(task), &tgt_ep,
			    DAOS_OBJ_RPC_CPD, &req);
	if (rc != 0)
		D_GOTO(out, rc);

	crt_req_addref(req);
	tcca.tcca_req = req;
	tcca.tcca_tx = tx;
	tcca.tcca_args = args;

	rc = tse_task_register_comp_cb(task, dc_tx_commit_cb,
				       &tcca, sizeof(tcca));
	if (rc != 0) {
		/* drop ref from crt_req_addref. */
		crt_req_decref(req);
		D_ERROR("Failed to register completion cb: "DF_RC"\n",
			DP_RC(rc));

		D_GOTO(out, rc);
	}

	oci = crt_req_get(req);
	D_ASSERT(oci != NULL);

	rc = dc_cont_hdl2uuid(tx->tx_coh, &oci->oci_co_hdl, &oci->oci_co_uuid);
	D_ASSERT(rc == 0);

	uuid_copy(oci->oci_pool_uuid, tx->tx_pool->dp_pool);
	oci->oci_map_ver = tx->tx_pm_ver;
	oci->oci_flags = DRF_CPD_LEADER | (tx->tx_set_resend ? ORF_RESEND : 0);

	oci->oci_sub_heads.ca_arrays = &tx->tx_head;
	oci->oci_sub_heads.ca_count = 1;
	oci->oci_sub_reqs.ca_arrays = &tx->tx_reqs;
	oci->oci_sub_reqs.ca_count = 1;
	oci->oci_disp_ents.ca_arrays = &tx->tx_disp;
	oci->oci_disp_ents.ca_count = 1;
	oci->oci_disp_tgts.ca_arrays = &tx->tx_tgts;
	oci->oci_disp_tgts.ca_count = 1;

	tx->tx_status = TX_COMMITTING;
	D_MUTEX_UNLOCK(&tx->tx_lock);

	rc = daos_rpc_send(req, task);
	if (rc != 0)
		D_ERROR("CPD RPC failed rc "DF_RC"\n", DP_RC(rc));

	return rc;

out:
	if (req != NULL)
		crt_req_decref(req);

	if (rc == -DER_TX_RESTART)
		tx->tx_status = TX_FAILED;
	else if (rc != 0)
		tx->tx_status = TX_ABORTED;

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
		D_GOTO(out_tx, rc = -DER_ALREADY);

	if (tx->tx_status == TX_COMMITTING &&
	    !(tx->tx_retry && args->flags & DTF_RETRY_COMMIT))
		D_GOTO(out_tx, rc = -DER_INPROGRESS);

	if (tx->tx_status != TX_OPEN &&
	    !(tx->tx_status == TX_COMMITTING &&
	      tx->tx_retry && args->flags & DTF_RETRY_COMMIT)) {
		D_ERROR("Can't commit non-open state TX (%d)\n",
			tx->tx_status);
		D_GOTO(out_tx, rc = -DER_NO_PERM);
	}

	if (tx->tx_write_cnt == 0 && tx->tx_read_cnt == 0) {
		tx->tx_status = TX_COMMITTED;
		D_GOTO(out_tx, rc = 0);
	}

	return dc_tx_commit_trigger(task, tx, args);

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
		D_GOTO(out_tx, rc = -DER_ALREADY);

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

	rc = dc_tx_alloc(args->coh, args->epoch, DAOS_TF_RDONLY, false, &tx);
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
	if (tx->tx_status != TX_FAILED) {
		D_ERROR("Can't restart non-failed state TX (%d)\n",
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

	rc = dc_tx_alloc(coh, epoch, flags, true, &tx);
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

static inline daos_obj_id_t
dc_tx_dcsr2oid(struct daos_cpd_sub_req *dcsr)
{
	return ((struct dc_object *)(dcsr->dcsr_obj))->cob_md.omd_id;
}

static int
dc_tx_add_update(struct dc_tx *tx, daos_handle_t oh, uint64_t flags,
		 daos_key_t *dkey, uint32_t nr, daos_iod_t *iods,
		 d_sg_list_t *sgls)
{
	struct daos_cpd_sub_req	*dcsr;
	struct daos_cpd_update	*dcu = NULL;
	struct obj_iod_array	*iod_array;
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

	dcsr->dcsr_reasb = NULL;
	dcsr->dcsr_sgls = NULL;

	dcsr->dcsr_opc = DCSO_UPDATE;
	dcsr->dcsr_nr = nr;
	dcsr->dcsr_dkey_hash = obj_dkey2hash(dkey);
	dcsr->dcsr_api_flags = flags;

	dcu = &dcsr->dcsr_update;
	iod_array = &dcu->dcu_iod_array;
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

	D_ALLOC_ARRAY(dcsr->dcsr_sgls, nr);
	if (dcsr->dcsr_sgls == NULL)
		D_GOTO(fail, rc = -DER_NOMEM);

	if (tx->tx_flags & DAOS_TF_ZERO_COPY)
		rc = daos_sgls_copy_ptr(dcsr->dcsr_sgls, nr, sgls, nr);
	else
		rc = daos_sgls_copy_all(dcsr->dcsr_sgls, nr, sgls, nr);
	if (rc != 0)
		D_GOTO(fail, rc);

	tx->tx_write_cnt++;

	D_DEBUG(DB_TRACE, "Cache update: DTI "DF_DTI", obj "DF_OID", dkey "
		DF_KEY", flags %lx, nr = %d, write cnt %d\n",
		DP_DTI(&tx->tx_id), DP_OID(dc_tx_dcsr2oid(dcsr)),
		DP_KEY(dkey), flags, nr, tx->tx_write_cnt);

	return 0;

fail:
	if (dcu != NULL) {
		if (iod_array->oia_iods != NULL) {
			for (i = 0; i < nr; i++) {
				daos_iov_free(&iod_array->oia_iods[i].iod_name);
				D_FREE(iod_array->oia_iods[i].iod_recxs);
			}

			D_FREE(iod_array->oia_iods);
		}

		if (dcsr->dcsr_sgls != NULL) {
			for (i = 0; i < nr; i++)
				daos_sgl_fini(&dcsr->dcsr_sgls[i],
					      !(tx->tx_flags &
						DAOS_TF_ZERO_COPY));

			D_FREE(dcsr->dcsr_sgls);
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

	D_DEBUG(DB_TRACE, "Cache punch obj: DTI "DF_DTI", obj "DF_OID
		", flags %lx, write cnt %d\n",
		DP_DTI(&tx->tx_id), DP_OID(dc_tx_dcsr2oid(dcsr)),
		flags, tx->tx_write_cnt);

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

	D_DEBUG(DB_TRACE, "Cache punch dkey: DTI "DF_DTI", obj "DF_OID", dkey "
		DF_KEY", flags %lx, write cnt %d\n",
		DP_DTI(&tx->tx_id), DP_OID(dc_tx_dcsr2oid(dcsr)),
		DP_KEY(dkey), flags, tx->tx_write_cnt);

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

	D_DEBUG(DB_TRACE, "Cache punch akey: DTI "DF_DTI", obj "DF_OID", dkey "
		DF_KEY", flags %lx, nr %d, write cnt %d\n",
		DP_DTI(&tx->tx_id), DP_OID(dc_tx_dcsr2oid(dcsr)),
		DP_KEY(dkey), flags, nr, tx->tx_write_cnt);

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
dc_tx_add_read(struct dc_tx *tx, int opc, daos_handle_t oh, uint64_t flags,
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

	if (dkey != NULL)
		D_DEBUG(DB_TRACE, "Cache read opc %d: DTI "DF_DTI", obj "DF_OID
			", dkey "DF_KEY", flags %lx, nr %d, read cnt %d\n",
			opc, DP_DTI(&tx->tx_id), DP_OID(dc_tx_dcsr2oid(dcsr)),
			DP_KEY(dkey), flags, nr, tx->tx_read_cnt);
	else
		D_DEBUG(DB_TRACE, "Cache enum obj: DTI "DF_DTI", obj "DF_OID
			", flags %lx, nr %d, read cnt %d\n",
			DP_DTI(&tx->tx_id), DP_OID(dc_tx_dcsr2oid(dcsr)),
			flags, nr, tx->tx_read_cnt);

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

static int
dc_tx_check_existence_cb(tse_task_t *task, void *data)
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

	if (args->tmp_iods != NULL) {
		int		i;

		for (i = 0; i < args->nr; i++)
			daos_iov_free(&args->tmp_iods[i].iod_name);

		D_FREE(args->tmp_iods);
	}

	/* The errno will be auto propagated to the dependent task. */
	task->dt_result = rc;

	/* Drop the reference that is held via dc_tx_attach(). */
	dc_tx_decref(tx);

	return 0;
}

static int
dc_tx_check_existence_task(enum obj_rpc_opc opc, daos_handle_t oh,
			   struct dc_tx *tx, uint64_t flags, daos_key_t *dkey,
			   uint32_t nr, void *iods_or_akeys, d_sg_list_t *sgls,
			   tse_task_t *parent)
{
	struct dc_tx_check_existence_cb_args	 cb_args = { 0 };
	daos_iod_t				*iods = NULL;
	tse_task_t				*task = NULL;
	uint64_t				 api_flags;
	int					 rc;
	int					 i;

	cb_args.opc		= opc;
	cb_args.tx		= tx;
	cb_args.oh		= oh;
	cb_args.flags		= flags;
	cb_args.dkey		= dkey;
	cb_args.nr		= nr;
	cb_args.iods_or_akeys	= iods_or_akeys;
	cb_args.sgls		= sgls;

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
			cb_args.tmp_iods = iods;
		} else if (flags & (DAOS_COND_AKEY_INSERT |
				    DAOS_COND_AKEY_UPDATE)) {
			iods = iods_or_akeys;
			api_flags = DAOS_COND_AKEY_FETCH;
		} else {
			/* Only check dkey existence. */
			api_flags = DAOS_COND_DKEY_FETCH;
			nr = 0;
		}
	} else {
		api_flags = DAOS_COND_DKEY_FETCH;
	}

	rc = dc_obj_fetch_task_create(oh, dc_tx_ptr2hdl(tx), api_flags, dkey,
				      nr, DIOF_CHECK_EXISTENCE | DIOF_TO_LEADER,
				      iods, NULL, NULL, NULL, NULL,
				      tse_task2sched(parent), &task);
	if (rc != 0)
		goto out;

	rc = dc_task_depend(parent, 1, &task);
	if (rc != 0) {
		D_ERROR("Fail to add dep on check existence task: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	rc = tse_task_register_comp_cb(task, dc_tx_check_existence_cb,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		D_ERROR("Fail to add CB for check existence task: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	rc = dc_task_schedule(task, true);

	/* Return positive value to notify the sponsor to not call
	 * complete() the task until the checking existence callback.
	 */
	return rc == 0 ? 1 : rc;

out:
	if (task != NULL)
		dc_task_decref(task);

	if (iods != NULL && iods != iods_or_akeys) {
		for (i = 0; i < nr; i++)
			daos_iov_free(&iods[i].iod_name);

		D_FREE(iods);
	}

	/* Drop the reference that is held via dc_tx_attach(). */
	dc_tx_decref(tx);

	return rc;
}

int
dc_tx_attach(daos_handle_t th, enum obj_rpc_opc opc, tse_task_t *task)
{
	struct dc_tx	*tx;
	int		 rc;

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

			return dc_tx_check_existence_task(opc, up->oh, tx,
						up->flags, up->dkey, up->nr,
						up->iods, up->sgls, task);
		}

		rc = dc_tx_add_update(tx, up->oh, up->flags, up->dkey,
				      up->nr, up->iods, up->sgls);
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

			return dc_tx_check_existence_task(opc, pu->oh, tx,
							  pu->flags, pu->dkey,
							  0, NULL, NULL, task);
		}

		rc = dc_tx_add_punch_dkey(tx, pu->oh, pu->flags, pu->dkey);
		break;
	}
	case DAOS_OBJ_RPC_PUNCH_AKEYS: {
		daos_obj_punch_t	*pu = dc_task_get_args(task);

		if (pu->flags & DAOS_COND_PUNCH) {
			D_MUTEX_UNLOCK(&tx->tx_lock);

			return dc_tx_check_existence_task(opc, pu->oh, tx,
					pu->flags, pu->dkey, pu->akey_nr,
					pu->akeys, NULL, task);
		}

		rc = dc_tx_add_punch_akeys(tx, pu->oh, pu->flags, pu->dkey,
					   pu->akey_nr, pu->akeys);
		break;
	}
	case DAOS_OBJ_RPC_FETCH: {
		daos_obj_fetch_t	*fe = dc_task_get_args(task);

		rc = dc_tx_add_read(tx, opc, fe->oh, fe->flags, fe->dkey,
				    fe->nr, fe->nr != 1 ? fe->iods :
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

		rc = dc_tx_add_read(tx, opc, qu->oh, 0, dkey, nr, qu->akey);
		break;
	}
	case DAOS_OBJ_RECX_RPC_ENUMERATE: {
		daos_obj_list_recx_t	*lr = dc_task_get_args(task);

		rc = dc_tx_add_read(tx, opc, lr->oh, 0, lr->dkey, 1, lr->akey);
		break;
	}
	case DAOS_OBJ_AKEY_RPC_ENUMERATE: {
		daos_obj_list_akey_t	*la = dc_task_get_args(task);

		rc = dc_tx_add_read(tx, opc, la->oh, 0, la->dkey, 0, NULL);
		break;
	}
	case DAOS_OBJ_DKEY_RPC_ENUMERATE: {
		daos_obj_list_dkey_t	*ld = dc_task_get_args(task);

		rc = dc_tx_add_read(tx, opc, ld->oh, 0, NULL, 0, NULL);
		break;
	}
	default:
		D_ERROR("Unsupportted TX attach opc %d\n", opc);
		rc = -DER_INVAL;
		break;
	}

	D_MUTEX_UNLOCK(&tx->tx_lock);
	dc_tx_decref(tx);

	return rc;
}
