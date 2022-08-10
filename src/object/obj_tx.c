/**
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
	TX_RESTARTING,
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
	uint32_t		 tx_fixed_epoch:1, /** epoch is specified. */
				 tx_retry:1, /** Retry the commit RPC. */
				 tx_set_resend:1, /** Set 'resend' flag. */
				 tx_reintegrating:1;
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

	struct d_backoff_seq	 tx_backoff_seq;
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

	d_backoff_seq_fini(&tx->tx_backoff_seq);

	if (tx->tx_epoch_task != NULL)
		tse_task_decref(tx->tx_epoch_task);

	D_FREE(tx->tx_req_cache);
	dc_pool_put(tx->tx_pool);
	D_MUTEX_DESTROY(&tx->tx_lock);
	D_FREE(tx);
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
	D_ASSERT(daos_handle_is_valid(ph));

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
		D_FREE(tx);
		return rc;
	}

	tx->tx_pool = dc_hdl2pool(ph);
	D_ASSERT(tx->tx_pool != NULL);

	if (epoch == 0) {
		daos_dti_gen(&tx->tx_id, false);
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
		daos_dti_gen(&tx->tx_id, true);
		/* The epoch is dictated by the caller. */
		tx->tx_fixed_epoch = 1;
		tx->tx_epoch.oe_value = epoch;
		tx->tx_epoch.oe_first = tx->tx_epoch.oe_value;
		tx->tx_epoch.oe_flags = 0;
	}

	tx->tx_coh = coh;
	tx->tx_flags = flags;
	tx->tx_status = TX_OPEN;
	daos_hhash_hlink_init(&tx->tx_hlink, &tx_h_ops);
	dc_tx_hdl_link(tx);

	/*
	 * Initialize the restart backoff sequence to produce:
	 *
	 *   Restart	Range
	 *         1	[0,   0 us]
	 *         2	[0,  16 us]
	 *         3	[0,  64 us]
	 *         4	[0, 256 us]
	 *       ...	...
	 *        10	[0,  ~1  s]
	 *        11	[0,  ~1  s]
	 *       ...	...
	 */
	rc = d_backoff_seq_init(&tx->tx_backoff_seq, 1 /* nzeros */,
				4 /* factor */, 16 /* next (us) */,
				1 << 20 /* max (us) */);
	D_ASSERTF(rc == 0, "d_backoff_seq_init: "DF_RC"\n", DP_RC(rc));

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

		if (dcu->dcu_flags & ORF_CPD_BULK) {
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
				d_sgl_fini(&dcsr->dcsr_sgls[i],
					   !(tx->tx_flags & DAOS_TF_ZERO_COPY));

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

/* Return the index of the leftmost request in the cache. For write case,
 * it is the first write request. Otherwise, it is the last read request.
 */
static uint32_t
dc_tx_leftmost_req(struct dc_tx *tx, bool write)
{
	if (tx->tx_flags & DAOS_TF_RDONLY)
		return tx->tx_total_slots - tx->tx_read_cnt;

	if (tx->tx_total_slots > DTX_SUB_WRITE_MAX)
		return tx->tx_total_slots - DTX_SUB_WRITE_MAX -
			(write ? 0 : tx->tx_read_cnt);

	return (tx->tx_total_slots >> 1) - (write ? 0 : tx->tx_read_cnt);
}

static void
dc_tx_cleanup(struct dc_tx *tx)
{
	struct daos_cpd_sub_head	*dcsh = tx->tx_head.dcs_buf;
	struct daos_cpd_disp_ent	*dcde = tx->tx_disp.dcs_buf;
	uint32_t			 from;
	uint32_t			 to;
	uint32_t			 i;

	from = dc_tx_leftmost_req(tx, false);
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

	if (tx->tx_pm_ver != pm_ver ||
	    DAOS_FAIL_CHECK(DAOS_DTX_STALE_PM)) {
		/* For external non-snap TX, restart it if pool map is stale. */
		if (tx->tx_pm_ver != 0 && !tx->tx_fixed_epoch) {
			tx->tx_status = TX_FAILED;
			rc = -DER_TX_RESTART;
		} else {
			tx->tx_pm_ver = pm_ver;
		}
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
 * \retval DC_TX_GE_REINITED	\a task has been reinitialized
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
		tse_disable_propagate(task);
		rc = tse_task_register_deps(task, 1, &tx->tx_epoch_task);
		if (rc != 0) {
			D_ERROR("cannot depend on task %p: "DF_RC"\n",
				tx->tx_epoch_task, DP_RC(rc));
			goto out;
		}
		rc = tse_task_reinit(task);
		if (rc != 0) {
			D_ERROR("cannot reinitialize task %p: "DF_RC"\n", task, DP_RC(rc));
			goto out;
		}
		rc = DC_TX_GE_REINITED;
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

	if (unlikely(rc == -DER_TX_ID_REUSED)) {
		if (tx->tx_retry)
			/* XXX: it is must because miss to set "RESEND" flag, that is bug. */
			D_ASSERTF(0,
				  "We miss to set 'RESEND' flag (%d) when resend RPC for TX "
				  DF_DTI"\n", tx->tx_set_resend ? 1 : 0, DP_DTI(&tx->tx_id));

		D_INFO("TX ID "DF_DTI" for CPD RPC is reused, re-generate\n", DP_DTI(&tx->tx_id));
		/* For non-retry case, restart TX with new TX ID. */
		daos_dti_gen(&tx->tx_id, false);
		tx->tx_status = TX_FAILED;
		D_GOTO(out, rc = -DER_TX_RESTART);
	}

	if (rc != -DER_TX_RESTART && !obj_retry_error(rc)) {
		tx->tx_retry = 0;
		tx->tx_status = TX_ABORTED;

		goto out;
	}

	/* Need to refresh the local pool map. */
	if (tx->tx_pm_ver < oco->oco_map_version || daos_crt_network_error(rc) ||
	    rc == -DER_TIMEDOUT || rc == -DER_EXCLUDED || rc == -DER_STALE) {
		struct daos_cpd_sub_req		*dcsr;

		dcsr = &tx->tx_req_cache[dc_tx_leftmost_req(tx, false)];
		rc1 = obj_pool_query_task(tse_task2sched(task), dcsr->dcsr_obj,
					  oco->oco_map_version, &pool_task);
		if (rc1 != 0) {
			D_ERROR("Failed to refresh the pool map: "
				DF_RC", original error: "DF_RC"\n",
				DP_RC(rc1), DP_RC(rc));
			tx->tx_status = TX_ABORTED;
			D_GOTO(out, rc = rc1);
		}
	}

	/* Need to restart the TX with newer epoch. */
	if (rc == -DER_TX_RESTART || rc == -DER_STALE || rc == -DER_UPDATE_AGAIN) {
		tx->tx_set_resend = 1;
		tx->tx_status = TX_FAILED;

		if (pool_task != NULL) {
			D_MUTEX_UNLOCK(&tx->tx_lock);
			locked = false;

			tse_task_schedule(pool_task, true);
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
			dc_pool_abandon_map_refresh_task(pool_task);
			tx->tx_status = TX_ABORTED;

			D_GOTO(out, rc = rc1);
		}
	}

	rc1 = dc_task_resched(task);
	if (rc1 != 0) {
		D_ERROR("Failed to re-init task (%p): "DF_RC", original error: "
			DF_RC"\n", task, DP_RC(rc1), DP_RC(rc));
		if (pool_task != NULL)
			dc_pool_abandon_map_refresh_task(pool_task);
		tx->tx_status = TX_ABORTED;

		D_GOTO(out, rc = rc1);
	}

	if (pool_task != NULL)
		tse_task_schedule(pool_task, true);

	rc = 0;

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
	uint8_t				 dtrg_flags; /* see daos_tgt_flags */
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
		dcu->dcu_flags |= ORF_BULK_BIND | ORF_CPD_BULK;

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
	struct cont_props		 props;
	int				 rc = 0;

	oca = obj_get_oca(obj);
	if (daos_oclass_is_ec(oca)) {
		struct obj_reasb_req	*reasb_req;

		D_ALLOC_PTR(reasb_req);
		if (reasb_req == NULL)
			return rc = -DER_NOMEM;

		dcu->dcu_flags |= ORF_EC;
		/* dcsr->dcsr_reasb will be released via
		 * dc_tx_cleanup().
		 */
		dcsr->dcsr_reasb = reasb_req;
		rc = obj_reasb_req_init(dcsr->dcsr_reasb, obj,
					dcu->dcu_iod_array.oia_iods,
					dcsr->dcsr_nr, oca);
		if (rc != 0)
			return rc;

		rc = obj_ec_req_reasb(dcu->dcu_iod_array.oia_iods,
				      obj_ec_dkey_hash_get(obj, dcsr->dcsr_dkey_hash),
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

	if (!daos_csummer_initialized(csummer))
		goto pack;

	props = dc_cont_hdl2props(obj->cob_coh);
	if (!obj_csum_dedup_candidate(&props, dcu->dcu_iod_array.oia_iods,
				      dcsr->dcsr_nr))
		goto pack;

	rc = daos_csummer_calc_key(csummer, &dcsr->dcsr_dkey,
				   &dcu->dcu_dkey_csum);
	if (rc != 0)
		return rc;

	rc = daos_csummer_calc_iods(csummer, dcsr->dcsr_sgls,
				    dcu->dcu_iod_array.oia_iods, NULL,
				    dcsr->dcsr_nr, false, singv_los, -1,
				    &dcu->dcu_iod_array.oia_iod_csums);

pack:
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
	uint8_t			 tgt_flags = 0;

	if (d_list_empty(dtr_list))
		leader_dtr = NULL;
	else
		leader_dtr = d_list_entry(dtr_list->next, struct dc_tx_rdg,
					  dtr_link);

	oca = obj_get_oca(obj);
	size = sizeof(*dtr) + sizeof(uint32_t) * obj->cob_grp_size;
	D_ALLOC(dtr, size);
	if (dtr == NULL)
		return -DER_NOMEM;

	start = grp_idx * obj->cob_grp_size;
	dcsr->dcsr_ec_tgt_nr = 0;

	if (dcsr->dcsr_opc == DCSO_UPDATE) {
		dcu = &dcsr->dcsr_update;
		reasb_req = dcsr->dcsr_reasb;
		if (dcu->dcu_flags & ORF_EC && reasb_req->tgt_bitmap != NIL_BITMAP) {
			D_ALLOC_ARRAY(dcu->dcu_ec_tgts, obj->cob_grp_size);
			if (dcu->dcu_ec_tgts == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			dcu->dcu_start_shard = start;
			if (dcu->dcu_iod_array.oia_oiods != NULL)
				tgt_flags = DTF_REASSEMBLE_REQ;
		}
	}

	/* Descending order to guarantee that EC parity is handled firstly. */
	for (idx = start + obj->cob_grp_size - 1; idx >= start; idx--) {
		if (reasb_req != NULL && reasb_req->tgt_bitmap != NIL_BITMAP &&
		    isclr(reasb_req->tgt_bitmap, idx - start))
			continue;

		rc = obj_shard_open(obj, idx, tx->tx_pm_ver, &shard);
		if (rc == -DER_NONEXIST) {
			rc = 0;
			if (daos_oclass_is_ec(oca) && !all) {
				if (idx >= start + obj->cob_grp_size -
							oca->u.ec.e_p)
					skipped_parity++;

				if (skipped_parity > oca->u.ec.e_p) {
					D_ERROR("Too many (%d) shards in the "
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

		if (shard->do_reintegrating)
			tx->tx_reintegrating = 1;
		/* XXX: It is possible that more than one shards locate on the
		 *	same DAOS target under OSA mode, then the "idx" may be
		 *	not equal to "shard->do_shard".
		 */

		D_ASSERTF(shard->do_target_id < dtrg_nr,
			  "Invalid target ID: ID %u, targets %u\n",
			  shard->do_target_id, dtrg_nr);

		dtrg = &dtrgs[shard->do_target_id];

		dtrg->dtrg_flags |= tgt_flags;
		if (unlikely(shard->do_shard != idx))
			dtrg->dtrg_flags |= DTF_REASSEMBLE_REQ;

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
		dcri->dcri_shard_off = idx;
		dcri->dcri_shard_id = shard->do_shard;
		dcri->dcri_req_idx = req_idx;
		dcri->dcri_padding = 0;

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
				leader_oid->id_shard = shard->do_shard;
			}
		} else if (tmp->dtrg_write_cnt == 0) {
			if (dtrg->dtrg_read_cnt > tmp->dtrg_read_cnt) {
				*leader_dtrg_idx = shard->do_target_id;
				leader_dtr = dtr;
				leader_oid->id_pub = obj->cob_md.omd_id;
				leader_oid->id_shard = shard->do_shard;
			}
		}

		if (leader_dtr == NULL) {
			*leader_dtrg_idx = shard->do_target_id;
			leader_dtr = dtr;
			leader_oid->id_pub = obj->cob_md.omd_id;
			leader_oid->id_shard = shard->do_shard;
		}

		if (dcu != NULL && dcu->dcu_ec_tgts != NULL) {
			dcu->dcu_ec_tgts[dcsr->dcsr_ec_tgt_nr].dcet_shard_idx =
							shard->do_shard;
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

	if (read)
		dtr->dtr_group.drg_flags = DGF_RDONLY;

	if (daos_oclass_is_ec(oca) && !all) {
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
dc_tx_reduce_rdgs(d_list_t *dtr_list, uint32_t *grp_cnt, uint32_t *mod_cnt)
{
	struct dc_tx_rdg	*dtr;
	struct dc_tx_rdg	*tmp;
	struct dc_tx_rdg	*next;
	struct dc_tx_rdg	*leader;
	size_t			 size = 0;

	*grp_cnt = 0;
	leader = d_list_pop_entry(dtr_list, struct dc_tx_rdg, dtr_link);

	/* Filter the dtrs that are the same as @leader. */
	d_list_for_each_entry_safe(dtr, next, dtr_list, dtr_link) {
		if (dc_tx_same_rdg(&leader->dtr_group, &dtr->dtr_group)) {
			d_list_del(&dtr->dtr_link);
			if (leader->dtr_group.drg_flags & DGF_RDONLY) {
				D_FREE(leader);
				leader = dtr;
			} else {
				D_FREE(dtr);
			}
		}
	}

	if (d_list_empty(dtr_list))
		goto out;

	tmp = d_list_pop_entry(dtr_list, struct dc_tx_rdg, dtr_link);

	/* XXX: Try to merge the other non-leaders if possible.
	 *	Consider efficiency, just one cycle scan. We do
	 *	NOT guarantee all mergeable ones will be merged.
	 */
	d_list_for_each_entry_safe(dtr, next, dtr_list, dtr_link) {
		if (dc_tx_same_rdg(&tmp->dtr_group, &dtr->dtr_group)) {
			d_list_del(&dtr->dtr_link);
			if (tmp->dtr_group.drg_flags & DGF_RDONLY) {
				D_FREE(tmp);
				tmp = dtr;
			} else {
				D_FREE(dtr);
			}
		} else {
			size += sizeof(struct dtx_redundancy_group) +
				sizeof(uint32_t) * dtr->dtr_group.drg_tgt_cnt;
			(*grp_cnt)++;
			if (!(dtr->dtr_group.drg_flags & DGF_RDONLY))
				(*mod_cnt)++;
		}
	}

	d_list_add(&tmp->dtr_link, dtr_list);
	size += sizeof(struct dtx_redundancy_group) +
		sizeof(uint32_t) * tmp->dtr_group.drg_tgt_cnt;
	(*grp_cnt)++;
	if (!(tmp->dtr_group.drg_flags & DGF_RDONLY))
		(*mod_cnt)++;

out:
	/* Insert the leader dtr at the head position. */
	d_list_add(&leader->dtr_link, dtr_list);
	size += sizeof(struct dtx_redundancy_group) +
		sizeof(uint32_t) * leader->dtr_group.drg_tgt_cnt;
	(*grp_cnt)++;
	if (!(leader->dtr_group.drg_flags & DGF_RDONLY))
		(*mod_cnt)++;

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
	struct daos_csummer		*csummer_cop = NULL;
	struct dc_tx_req_group		*dtrgs = NULL;
	struct daos_cpd_sub_head	*dcsh = NULL;
	struct daos_cpd_disp_ent	*dcdes = NULL;
	struct daos_shard_tgt		*shard_tgts = NULL;
	struct daos_cpd_sub_req		*dcsr;
	struct dc_object		*obj;
	struct dtx_memberships		*mbs;
	struct dtx_daos_target		*ddt;
	struct dc_tx_rdg		*dtr;
	void				*ptr;
	d_list_t			 dtr_list;
	size_t				 size;
	uint32_t			 leader_dtrg_idx = 0;
	uint32_t			 act_tgt_cnt = 0;
	uint32_t			 act_grp_cnt = 0;
	uint32_t			 mod_grp_cnt = 0;
	uint32_t			 start;
	uint32_t			 tgt_cnt;
	uint32_t			 req_cnt;
	int				 grp_idx;
	int				 rc = 0;
	int				 i;
	int				 j;

	D_INIT_LIST_HEAD(&dtr_list);
	csummer = dc_cont_hdl2csummer(tx->tx_coh);
	if (daos_csummer_initialized(csummer)) {
		csummer_cop = daos_csummer_copy(csummer);
		if (csummer_cop == NULL)
			return -DER_NOMEM;
	}

	req_cnt = tx->tx_read_cnt + tx->tx_write_cnt;
	tgt_cnt = pool_map_target_nr(tx->tx_pool->dp_map);
	D_ASSERT(tgt_cnt != 0);

	D_ALLOC_ARRAY(dtrgs, tgt_cnt);
	if (dtrgs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	start = dc_tx_leftmost_req(tx, false);
	for (i = 0; i < req_cnt; i++) {
		dcsr = &tx->tx_req_cache[i + start];
		obj = dcsr->dcsr_obj;

		if (dcsr->dcsr_opc == DCSO_UPDATE) {
			rc = dc_tx_classify_update(tx, dcsr, csummer_cop);
			if (rc < 0)
				goto out;

			if (rc > (DAOS_BULK_LIMIT >> 2)) {
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
			grp_idx = obj_dkey2grpidx(obj, dcsr->dcsr_dkey_hash,
						  tx->tx_pm_ver);
			if (grp_idx < 0)
				D_GOTO(out, rc = grp_idx);

			rc = dc_tx_classify_common(tx, dcsr, dtrgs, tgt_cnt,
						   grp_idx, i,
						   dcsr->dcsr_opc == DCSO_READ,
						   false, &leader_dtrg_idx,
						   &act_tgt_cnt, &dtr_list,
						   &leader_oid);
			if (rc != 0)
				goto out;
		}
	}

	size = dc_tx_reduce_rdgs(&dtr_list, &act_grp_cnt, &mod_grp_cnt);

	/* For the distributed transaction that all the touched targets
	 * are in the same redundancy group, be as optimization, we will
	 * not store modification group information inside 'dm_data'.
	 */
	if (act_grp_cnt == 1)
		size = 0;

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

	mbs = dcsh->dcsh_mbs;
	mbs->dm_flags = DMF_CONTAIN_LEADER | DMF_SORTED_TGT_ID;

	/* For the case of modification(s) within single RDG,
	 * elect leader as standalone modification case does.
	 */
	if (mod_grp_cnt == 1) {
		uint8_t	*bit_map;

		i = dc_tx_leftmost_req(tx, true);
		dcsr = &tx->tx_req_cache[i];
		obj = dcsr->dcsr_obj;

		grp_idx = obj_dkey2grpidx(obj, dcsr->dcsr_dkey_hash,
					  tx->tx_pm_ver);
		if (grp_idx < 0)
			D_GOTO(out, rc = grp_idx);

		if (obj_is_ec(obj) && dcsr->dcsr_reasb != NULL)
			bit_map = ((struct obj_reasb_req *)(dcsr->dcsr_reasb))->tgt_bitmap;
		else
			bit_map = NIL_BITMAP;

		i = obj_grp_leader_get(obj, grp_idx,
				       obj_ec_dkey_hash_get(obj, dcsr->dcsr_dkey_hash),
				       false, tx->tx_pm_ver, bit_map);
		if (i < 0)
			D_GOTO(out, rc = i);

		leader_oid.id_pub = obj->cob_md.omd_id;
		leader_oid.id_shard = i;
		leader_dtrg_idx = obj_get_shard(obj, i)->po_target;
		if (!obj_is_ec(obj) && act_grp_cnt == 1)
			mbs->dm_flags |= DMF_SRDG_REP;

		/* If there is only one redundancy group to be modified,
		 * then such redundancy group information should already
		 * has been at the head position in the dtr_list.
		 */
	}

	if (DAOS_FAIL_CHECK(DAOS_DTX_SPEC_LEADER)) {
		i = dc_tx_leftmost_req(tx, true);
		dcsr = &tx->tx_req_cache[i];
		obj = dcsr->dcsr_obj;

		leader_oid.id_pub = obj->cob_md.omd_id;
		/* Use the shard 0 as the leader for test. The test program
		 * will guarantee that at least one sub-modification happen
		 * on the object shard 0.
		 */
		leader_oid.id_shard = 0;
		leader_dtrg_idx = obj_get_shard(obj, 0)->po_target;
	}

	dcsh->dcsh_xid = tx->tx_id;
	dcsh->dcsh_leader_oid = leader_oid;
	dcsh->dcsh_epoch = tx->tx_epoch;
	if (tx->tx_epoch.oe_flags & DTX_EPOCH_UNCERTAIN)
		dcsh->dcsh_epoch.oe_rpc_flags |= ORF_EPOCH_UNCERTAIN;
	else
		dcsh->dcsh_epoch.oe_rpc_flags &= ~ORF_EPOCH_UNCERTAIN;

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
	shard_tgts[0].st_flags = dtrgs[leader_dtrg_idx].dtrg_flags;

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
		shard_tgts[j].st_flags = dtrgs[i].dtrg_flags;
		j++;
	}

	if (act_grp_cnt == 1) {
		/* We do not need the group information if all the targets are
		 * in the same redundancy group.
		 */
		dtr = d_list_pop_entry(&dtr_list, struct dc_tx_rdg, dtr_link);
		D_FREE(dtr);
	} else {
		ptr = ddt;
		while ((dtr = d_list_pop_entry(&dtr_list, struct dc_tx_rdg,
					       dtr_link)) != NULL) {
			size = sizeof(dtr->dtr_group) +
			       sizeof(uint32_t) * dtr->dtr_group.drg_tgt_cnt;
			memcpy(ptr, &dtr->dtr_group, size);
			ptr += size;
			D_FREE(dtr);
		}
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
		if (dtrgs != NULL)
			for (i = 0; i < tgt_cnt; i++)
				D_FREE(dtrgs[i].dtrg_req_idx);
		if (dcdes != NULL)
			for (i = 0; i < act_tgt_cnt; i++)
				D_FREE(dcdes[i].dcde_reqs);

		D_FREE(dcdes);
		D_FREE(shard_tgts);
		if (dcsh != NULL) {
			D_FREE(dcsh->dcsh_mbs);
			D_FREE(dcsh);
		}
	}

	while ((dtr = d_list_pop_entry(&dtr_list, struct dc_tx_rdg,
				       dtr_link)) != NULL)
		D_FREE(dtr);

	D_FREE(dtrgs);
	daos_csummer_destroy(&csummer_cop);

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

	if (tx->tx_pm_ver != 0 && tx->tx_pm_ver != dc_pool_get_version(tx->tx_pool) &&
	    (tx->tx_retry || tx->tx_read_cnt > 0))
		D_GOTO(out, rc = -DER_TX_RESTART);

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
		D_ERROR("Failed to register completion cb: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out_req, rc);
	}

	oci = crt_req_get(req);
	D_ASSERT(oci != NULL);

	rc = dc_cont_hdl2uuid(tx->tx_coh, &oci->oci_co_hdl, &oci->oci_co_uuid);
	D_ASSERT(rc == 0);

	uuid_copy(oci->oci_pool_uuid, tx->tx_pool->dp_pool);
	oci->oci_map_ver = tx->tx_pm_ver;
	oci->oci_flags = ORF_CPD_LEADER | (tx->tx_set_resend ? ORF_RESEND : 0);
	if (tx->tx_reintegrating)
		oci->oci_flags |= ORF_REINTEGRATING_IO;

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

	return daos_rpc_send(req, task);

out_req:
	crt_req_decref(req);
	crt_req_decref(req);
out:
	if (rc == -DER_TX_RESTART)
		tx->tx_status = TX_FAILED;
	else if (rc != 0)
		tx->tx_status = TX_ABORTED;
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
	    !(tx->tx_retry && (args->flags & DTF_RETRY_COMMIT)))
		D_GOTO(out_tx, rc = -DER_INPROGRESS);

	if (tx->tx_status != TX_OPEN &&
	    !(tx->tx_status == TX_COMMITTING &&
	      tx->tx_retry && (args->flags & DTF_RETRY_COMMIT))) {
		D_ERROR("Can't commit non-open state TX (%d)\n",
			tx->tx_status);
		D_GOTO(out_tx, rc = -DER_NO_PERM);
	}

	if (tx->tx_write_cnt == 0 && tx->tx_read_cnt == 0) {
		tx->tx_status = TX_COMMITTED;
		D_GOTO(out_tx, rc = 0);
	}

	rc = dc_tx_commit_trigger(task, tx, args);
	if (rc)
		D_GOTO(out_tx, rc);
	return rc;

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

	if (args->epoch == 0 || args->epoch == DAOS_EPOCH_MAX) {
		D_ERROR("Invalid epoch for snapshot\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* XXX: more check snapshot validity before open TX. */

	/* TX against snapshot must be read-only. */
	rc = dc_tx_alloc(args->coh, args->epoch, DAOS_TF_RDONLY, &tx);
	if (rc == 0)
		*args->th = dc_tx_ptr2hdl(tx);

out:
	tse_task_complete(task, rc);

	return rc;
}

static void
dc_tx_close_internal(struct dc_tx *tx)
{
	dc_tx_cleanup(tx);
	dc_tx_hdl_unlink(tx);
	dc_tx_decref(tx);
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
		dc_tx_close_internal(tx);
	}
	D_MUTEX_UNLOCK(&tx->tx_lock);

	/* -1 for hdl2ptr */
	dc_tx_decref(tx);

out_task:
	tse_task_complete(task, rc);

	return rc;
}

/*
 * Begin restarting locked tx. If there is an error, *backoff is unchanged.
 * After a successful dc_tx_restart_begin call, the caller shall first
 * implement the backoff returned by *backoff, and then call dc_tx_restart_end.
 */
static int
dc_tx_restart_begin(struct dc_tx *tx, uint32_t *backoff)
{
	int	rc = 0;

	if (tx->tx_status != TX_FAILED) {
		D_ERROR("Can't restart non-failed state TX (%d)\n",
			tx->tx_status);
		rc = -DER_NO_PERM;
	} else {
		dc_tx_cleanup(tx);

		if (tx->tx_epoch_task != NULL) {
			tse_task_decref(tx->tx_epoch_task);
			tx->tx_epoch_task = NULL;
		}

		/*
		 * Prevent others from restarting the same TX while
		 * tx_lock is temporarily released during the backoff.
		 */
		tx->tx_status = TX_RESTARTING;

		*backoff = d_backoff_seq_next(&tx->tx_backoff_seq);
	}

	return rc;
}

/* End restarting locked tx. See dc_tx_restart_begin. */
static void
dc_tx_restart_end(struct dc_tx *tx)
{
	D_ASSERTF(tx->tx_status == TX_RESTARTING, "%d\n", tx->tx_status);
	tx->tx_status = TX_OPEN;
	tx->tx_pm_ver = 0;
	tx->tx_epoch.oe_value = 0;
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
	uint32_t		 backoff = 0;
	int			 rc = 0;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL,
		  "Task Argument OPC does not match DC OPC (restart)\n");

	tx = tse_task_get_priv_internal(task);
	if (tx == NULL) {
		/* Executing task for the first time. */

		tx = dc_tx_hdl2ptr(args->th);
		if (tx == NULL) {
			rc = -DER_NO_HDL;
			goto out;
		}

		D_MUTEX_LOCK(&tx->tx_lock);

		D_ASSERT(!tx->tx_fixed_epoch);

		rc = dc_tx_restart_begin(tx, &backoff);
		if (rc != 0)
			goto out_tx_lock;

		if (backoff == 0) {
			dc_tx_restart_end(tx);
		} else {
			/*
			 * Reinitialize task with a delay to implement the
			 * backoff and call dc_tx_restart_end below.
			 */
			rc = tse_task_reinit_with_delay(task, backoff);
			if (rc != 0) {
				/* Skip the backoff. */
				backoff = 0;
				dc_tx_restart_end(tx);
				goto out_tx_lock;
			}
			D_MUTEX_UNLOCK(&tx->tx_lock);
			/* Pass our tx reference to task. */
			tse_task_set_priv_internal(task, tx);
			return 0;
		}

out_tx_lock:
		D_MUTEX_UNLOCK(&tx->tx_lock);
		dc_tx_decref(tx);
	} else {
		/* Re-executing task after the reinitialization above. */
		D_MUTEX_LOCK(&tx->tx_lock);
		dc_tx_restart_end(tx);
		D_MUTEX_UNLOCK(&tx->tx_lock);
		dc_tx_decref(tx);
	}

out:
	if (backoff == 0)
		tse_task_complete(task, rc);

	return rc;
}

int
dc_tx_local_open(daos_handle_t coh, daos_epoch_t epoch, uint32_t flags,
		 daos_handle_t *th)
{
	struct dc_tx	*tx = NULL;
	int		 rc;

	if (epoch == 0 || epoch == DAOS_EPOCH_MAX) {
		D_ERROR("Invalid epoch for tx_local_open\n");
		return -DER_INVAL;
	}

	/* local TX must be read-only. */
	rc = dc_tx_alloc(coh, epoch, flags | DAOS_TF_RDONLY, &tx);
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

	dc_tx_close_internal(tx);

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
dc_tx_add_update(struct dc_tx *tx, struct dc_object **obj, uint64_t flags,
		 daos_key_t *dkey, uint32_t nr, daos_iod_t *iods,
		 d_sg_list_t *sgls)
{
	struct daos_cpd_sub_req	*dcsr;
	struct daos_cpd_update	*dcu = NULL;
	struct obj_iod_array	*iod_array;
	int			 rc;
	int			 i;

	D_ASSERT(nr != 0);
	D_ASSERT(obj != NULL);

	if (*obj == NULL)
		return -DER_NO_HDL;

	rc = dc_tx_get_next_slot(tx, false, &dcsr);
	if (rc != 0)
		return rc;

	dcsr->dcsr_obj = *obj;
	*obj = NULL;

	rc = daos_iov_copy(&dcsr->dcsr_dkey, dkey);
	if (rc != 0)
		D_GOTO(fail, rc);

	dcsr->dcsr_reasb = NULL;
	dcsr->dcsr_sgls = NULL;

	dcsr->dcsr_opc = DCSO_UPDATE;
	dcsr->dcsr_nr = nr;
	dcsr->dcsr_dkey_hash = obj_dkey2hash(dc_tx_dcsr2oid(dcsr), dkey);
	dcsr->dcsr_api_flags = flags & ~DAOS_COND_MASK;

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
				d_sgl_fini(&dcsr->dcsr_sgls[i],
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
dc_tx_add_punch_obj(struct dc_tx *tx, struct dc_object **obj, uint64_t flags)
{
	struct daos_cpd_sub_req	*dcsr;
	int			 rc;

	D_ASSERT(obj != NULL);

	if (*obj == NULL)
		return -DER_NO_HDL;

	rc = dc_tx_get_next_slot(tx, false, &dcsr);
	if (rc != 0)
		return rc;

	dcsr->dcsr_obj = *obj;
	*obj = NULL;

	dcsr->dcsr_opc = DCSO_PUNCH_OBJ;
	dcsr->dcsr_api_flags = flags & ~DAOS_COND_MASK;

	tx->tx_write_cnt++;

	D_DEBUG(DB_TRACE, "Cache punch obj: DTI "DF_DTI", obj "DF_OID
		", flags %lx, write cnt %d\n",
		DP_DTI(&tx->tx_id), DP_OID(dc_tx_dcsr2oid(dcsr)),
		flags, tx->tx_write_cnt);

	return 0;
}

static int
dc_tx_add_punch_dkey(struct dc_tx *tx, struct dc_object **obj, uint64_t flags,
		     daos_key_t *dkey)
{
	struct daos_cpd_sub_req	*dcsr;
	int			 rc;

	D_ASSERT(obj != NULL);

	if (*obj == NULL)
		return -DER_NO_HDL;

	rc = dc_tx_get_next_slot(tx, false, &dcsr);
	if (rc != 0)
		return rc;

	dcsr->dcsr_obj = *obj;
	*obj = NULL;

	rc = daos_iov_copy(&dcsr->dcsr_dkey, dkey);
	if (rc != 0) {
		obj_decref(dcsr->dcsr_obj);
		return rc;
	}

	dcsr->dcsr_opc = DCSO_PUNCH_DKEY;
	dcsr->dcsr_dkey_hash = obj_dkey2hash(dc_tx_dcsr2oid(dcsr), dkey);
	dcsr->dcsr_api_flags = flags & ~DAOS_COND_MASK;

	tx->tx_write_cnt++;

	D_DEBUG(DB_TRACE, "Cache punch dkey: DTI "DF_DTI", obj "DF_OID", dkey "
		DF_KEY", flags %lx, write cnt %d\n",
		DP_DTI(&tx->tx_id), DP_OID(dc_tx_dcsr2oid(dcsr)),
		DP_KEY(dkey), flags, tx->tx_write_cnt);

	return 0;
}

static int
dc_tx_add_punch_akeys(struct dc_tx *tx, struct dc_object **obj, uint64_t flags,
		      daos_key_t *dkey, uint32_t nr, daos_key_t *akeys)
{
	struct daos_cpd_sub_req	*dcsr = NULL;
	struct daos_cpd_punch	*dcp = NULL;
	int			 rc;
	int			 i;

	D_ASSERT(nr != 0);
	D_ASSERT(obj != NULL);

	if (*obj == NULL)
		return -DER_NO_HDL;

	rc = dc_tx_get_next_slot(tx, false, &dcsr);
	if (rc != 0)
		return rc;

	dcsr->dcsr_obj = *obj;
	*obj = NULL;

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
	dcsr->dcsr_dkey_hash = obj_dkey2hash(dc_tx_dcsr2oid(dcsr), dkey);
	dcsr->dcsr_api_flags = flags & ~DAOS_COND_MASK;

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
dc_tx_add_read(struct dc_tx *tx, struct dc_object **obj, int opc,
	       uint64_t flags, daos_key_t *dkey, uint32_t nr,
	       void *iods_or_akey)
{
	struct daos_cpd_sub_req	*dcsr = NULL;
	struct daos_cpd_read	*dcr = NULL;
	int			 rc;
	int			 i;

	if (tx->tx_status != TX_OPEN)
		return 0;

	if (tx->tx_fixed_epoch)
		return 0;

	D_ASSERT(obj != NULL);

	if (*obj == NULL)
		return -DER_NO_HDL;

	rc = dc_tx_get_next_slot(tx, true, &dcsr);
	if (rc != 0)
		return rc;

	dcsr->dcsr_obj = *obj;
	*obj = NULL;

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
	dcsr->dcsr_dkey_hash = obj_dkey2hash(dc_tx_dcsr2oid(dcsr), dkey);
	dcsr->dcsr_api_flags = flags & ~DAOS_COND_MASK;

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
	uint64_t		tmp_iod_nr;
	daos_iod_t		*tmp_iods;
};

static int
dc_tx_check_update(uint64_t flags, int result)
{
	if (flags & (DAOS_COND_AKEY_INSERT | DAOS_COND_DKEY_INSERT)) {
		if (result == 0)
			return -DER_EXIST;

		if (result != -DER_NONEXIST)
			return result;

		return 0;
	}

	if (flags & (DAOS_COND_AKEY_UPDATE | DAOS_COND_DKEY_UPDATE) && result != 0)
		return result;

	return 0;
}

static int
dc_tx_per_akey_existence_sub_cb(tse_task_t *task, void *data)
{
	struct dc_tx_check_existence_cb_args	*args = data;

	D_ASSERT(args->opc == DAOS_OBJ_RPC_UPDATE);
	D_ASSERT(args->flags & DAOS_COND_PER_AKEY);
	D_ASSERT(args->tmp_iods != NULL);
	D_ASSERT(args->tmp_iod_nr == 1);

	task->dt_result = dc_tx_check_update(args->tmp_iods->iod_flags, task->dt_result);

	daos_iov_free(&args->tmp_iods->iod_name);
	D_FREE(args->tmp_iods);

	return 0;
}

static int
dc_tx_per_akey_existence_parent_cb(tse_task_t *task, void *data)
{
	struct dc_tx_check_existence_cb_args	*args = data;
	struct dc_object			*obj = NULL;
	struct dc_tx				*tx = args->tx;
	int					 rc = task->dt_result;

	D_ASSERT(args->opc == DAOS_OBJ_RPC_UPDATE);
	D_ASSERT(args->flags & DAOS_COND_PER_AKEY);

	if (rc == 0) {
		obj = obj_hdl2ptr(args->oh);
		D_MUTEX_LOCK(&tx->tx_lock);
		rc = dc_tx_add_update(tx, &obj, args->flags, args->dkey, args->nr,
				      args->iods_or_akeys, args->sgls);
		D_MUTEX_UNLOCK(&tx->tx_lock);
		obj_decref(obj);
	}

	/* Drop the reference that is held via dc_tx_attach(). */
	dc_tx_decref(tx);

	return rc;
}

static int
dc_tx_check_existence_cb(tse_task_t *task, void *data)
{
	struct dc_tx_check_existence_cb_args	*args = data;
	struct dc_object			*obj = NULL;
	struct dc_tx				*tx = args->tx;
	int					 rc = 0;
	int					 i;

	obj = obj_hdl2ptr(args->oh);
	D_MUTEX_LOCK(&tx->tx_lock);

	switch (args->opc) {
	case DAOS_OBJ_RPC_UPDATE:
		rc = dc_tx_check_update(args->flags, task->dt_result);
		if (rc != 0)
			D_GOTO(out, rc);

		rc = dc_tx_add_update(tx, &obj, args->flags,
				      args->dkey, args->nr,
				      args->iods_or_akeys, args->sgls);
		break;
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
		D_ASSERT(args->flags & DAOS_COND_PUNCH);

		if (task->dt_result != 0)
			D_GOTO(out, rc = task->dt_result);

		rc = dc_tx_add_punch_dkey(tx, &obj, args->flags,
					  args->dkey);
		break;
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		D_ASSERT(args->flags & DAOS_COND_PUNCH);

		if (task->dt_result != 0)
			D_GOTO(out, rc = task->dt_result);

		rc = dc_tx_add_punch_akeys(tx, &obj, args->flags,
					   args->dkey, args->nr,
					   args->iods_or_akeys);
		break;
	default:
		D_ASSERT(0);
	}

out:
	D_MUTEX_UNLOCK(&tx->tx_lock);

	if (args->tmp_iods != NULL) {
		for (i = 0; i < args->nr; i++)
			daos_iov_free(&args->tmp_iods[i].iod_name);

		D_FREE(args->tmp_iods);
	}

	/* The errno will be auto propagated to the dependent task. */
	task->dt_result = rc;

	/* Drop the reference that is held via dc_tx_attach(). */
	dc_tx_decref(tx);

	if (obj != NULL)
		obj_decref(obj);

	return 0;
}

static int
dc_tx_per_akey_existence_task(enum obj_rpc_opc opc, daos_handle_t oh, struct dc_tx *tx,
			      uint64_t flags, daos_key_t *dkey, uint32_t nr, void *iods_or_akeys,
			      d_sg_list_t *sgls, tse_task_t *parent)
{
	struct dc_tx_check_existence_cb_args	 cb_args = { 0 };
	daos_iod_t				*in_iods = iods_or_akeys;
	daos_iod_t				*iods = NULL;
	tse_task_t				*task = NULL;
	d_list_t				 task_list;
	int					 rc;
	int					 i;

	D_INIT_LIST_HEAD(&task_list);

	cb_args.opc		= opc;
	cb_args.tx		= tx;
	cb_args.oh		= oh;
	cb_args.flags		= flags;
	cb_args.dkey		= dkey;
	cb_args.nr		= nr;
	cb_args.iods_or_akeys	= iods_or_akeys;
	cb_args.sgls		= sgls;

	/* XXX: individual sub-task for checking each akey's existence independently. */

	for (i = 0; i < nr; i++) {
		if (!(in_iods[i].iod_flags & (DAOS_COND_AKEY_INSERT | DAOS_COND_AKEY_UPDATE)))
			continue;

		D_ALLOC_ARRAY(iods, 1);
		if (iods == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		rc = daos_iov_copy(&iods->iod_name, &in_iods[i].iod_name);
		if (rc != 0)
			goto out;

		iods->iod_flags = in_iods[i].iod_flags;
		cb_args.tmp_iod_nr = 1;
		cb_args.tmp_iods = iods;

		rc = dc_obj_fetch_task_create(oh, dc_tx_ptr2hdl(tx), DAOS_COND_AKEY_FETCH, dkey, 1,
					      DIOF_CHECK_EXISTENCE, iods, NULL, NULL, NULL, NULL,
					      NULL, tse_task2sched(parent), &task);
		if (rc != 0)
			goto out;

		rc = tse_task_register_comp_cb(task, dc_tx_per_akey_existence_sub_cb,
					       &cb_args, sizeof(cb_args));
		if (rc != 0)
			goto out;

		/* decref and delete from head at shard_task_remove */
		tse_task_addref(task);
		tse_task_list_add(task, &task_list);

		iods = NULL;

		rc = dc_task_depend(parent, 1, &task);
		if (rc != 0)
			goto out;
	}

out:
	if (rc == 0) {
		if (unlikely(d_list_empty(&task_list))) {
			struct dc_object	*obj;

			obj_hdl2ptr(oh);
			D_MUTEX_LOCK(&tx->tx_lock);
			rc = dc_tx_add_update(tx, &obj, flags, dkey, nr, iods_or_akeys, sgls);
			D_MUTEX_UNLOCK(&tx->tx_lock);
			obj_decref(obj);

			/* Drop the reference that is held via dc_tx_attach(). */
			dc_tx_decref(tx);
		} else {
			rc = tse_task_register_comp_cb(parent, dc_tx_per_akey_existence_parent_cb,
						       &cb_args, sizeof(cb_args));
			if (rc != 0)
				goto fail;

			tse_task_list_sched(&task_list, true);

			/*
			 * Return positive value to notify the sponsor to not call
			 * complete() the task until the checking existence callback.
			 */
			rc = 1;
		}
	} else {
		if (iods != NULL) {
			if (task != NULL)
				dc_task_decref(task);

			daos_iov_free(&iods->iod_name);
			D_FREE(iods);
		}

fail:
		tse_task_list_traverse(&task_list, shard_task_abort, &rc);
		parent->dt_result = rc;

		/* Drop the reference that is held via dc_tx_attach(). */
		dc_tx_decref(tx);
	}

	return rc;
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
	 *	existence on related target.
	 */
	if (nr != 0) {
		D_ASSERT(iods_or_akeys != NULL);

		if (opc != DAOS_OBJ_RPC_UPDATE) {
			/* For punch akey. */
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
			cb_args.tmp_iod_nr = nr;
			cb_args.tmp_iods = iods;
		} else {
			if (flags & (DAOS_COND_AKEY_INSERT | DAOS_COND_AKEY_UPDATE)) {
				iods = iods_or_akeys;
				api_flags = DAOS_COND_AKEY_FETCH;
			} else {
				/* Only check dkey existence. */
				api_flags = DAOS_COND_DKEY_FETCH;
				nr = 0;
			}
		}
	} else {
		/* For punch dkey */
		api_flags = DAOS_COND_DKEY_FETCH;
	}

	rc = dc_obj_fetch_task_create(oh, dc_tx_ptr2hdl(tx), api_flags, dkey,
				      nr, DIOF_CHECK_EXISTENCE,
				      iods, NULL, NULL, NULL, NULL, NULL,
				      tse_task2sched(parent), &task);
	if (rc != 0)
		goto out;

	rc = tse_task_register_comp_cb(task, dc_tx_check_existence_cb,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		D_ERROR("Fail to add CB for check existence task: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	rc = dc_task_depend(parent, 1, &task);
	if (rc != 0) {
		D_ERROR("Fail to add dep on check existence task: "DF_RC"\n",
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
dc_tx_attach(daos_handle_t th, struct dc_object *obj, enum obj_rpc_opc opc,
	     tse_task_t *task)
{
	struct dc_tx	*tx;
	int		 rc;

	rc = dc_tx_check(th, obj_is_modification_opc(opc) ? true : false, &tx);
	if (rc != 0)
		goto out;

	switch (opc) {
	case DAOS_OBJ_RPC_UPDATE: {
		daos_obj_update_t	*up = dc_task_get_args(task);

		if (up->flags & (DAOS_COND_DKEY_INSERT |
				 DAOS_COND_DKEY_UPDATE |
				 DAOS_COND_AKEY_INSERT |
				 DAOS_COND_AKEY_UPDATE)) {
			D_MUTEX_UNLOCK(&tx->tx_lock);

			if (obj != NULL)
				obj_decref(obj);

			return dc_tx_check_existence_task(opc, up->oh, tx,
						up->flags, up->dkey, up->nr,
						up->iods, up->sgls, task);
		}

		if (up->flags & DAOS_COND_PER_AKEY) {
			D_MUTEX_UNLOCK(&tx->tx_lock);

			if (up->nr == 0 || up->iods == NULL)
				D_GOTO(out, rc = -DER_INVAL);

			if (obj != NULL)
				obj_decref(obj);

			return dc_tx_per_akey_existence_task(opc, up->oh, tx, up->flags, up->dkey,
							     up->nr, up->iods, up->sgls, task);
		}

		rc = dc_tx_add_update(tx, &obj, up->flags, up->dkey,
				      up->nr, up->iods, up->sgls);
		break;
	}
	case DAOS_OBJ_RPC_PUNCH: {
		daos_obj_punch_t	*pu = dc_task_get_args(task);

		D_ASSERTF(!(pu->flags & DAOS_COND_MASK),
			  "Unexpected cond flag %lx for punch obj\n",
			  pu->flags);

		rc = dc_tx_add_punch_obj(tx, &obj, pu->flags);
		break;
	}
	case DAOS_OBJ_RPC_PUNCH_DKEYS: {
		daos_obj_punch_t	*pu = dc_task_get_args(task);

		if (pu->flags & DAOS_COND_PUNCH) {
			D_MUTEX_UNLOCK(&tx->tx_lock);

			if (obj != NULL)
				obj_decref(obj);

			return dc_tx_check_existence_task(opc, pu->oh, tx,
							  pu->flags, pu->dkey,
							  0, NULL, NULL, task);
		}

		rc = dc_tx_add_punch_dkey(tx, &obj, pu->flags, pu->dkey);
		break;
	}
	case DAOS_OBJ_RPC_PUNCH_AKEYS: {
		daos_obj_punch_t	*pu = dc_task_get_args(task);

		if (pu->flags & DAOS_COND_PUNCH) {
			D_MUTEX_UNLOCK(&tx->tx_lock);

			if (obj != NULL)
				obj_decref(obj);

			return dc_tx_check_existence_task(opc, pu->oh, tx,
					pu->flags, pu->dkey, pu->akey_nr,
					pu->akeys, NULL, task);
		}

		rc = dc_tx_add_punch_akeys(tx, &obj, pu->flags, pu->dkey,
					   pu->akey_nr, pu->akeys);
		break;
	}
	case DAOS_OBJ_RPC_FETCH: {
		daos_obj_fetch_t	*fe = dc_task_get_args(task);

		rc = dc_tx_add_read(tx, &obj, opc, fe->flags, fe->dkey,
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

		rc = dc_tx_add_read(tx, &obj, opc, 0, dkey, nr, qu->akey);
		break;
	}
	case DAOS_OBJ_RECX_RPC_ENUMERATE: {
		daos_obj_list_recx_t	*lr = dc_task_get_args(task);

		rc = dc_tx_add_read(tx, &obj, opc, 0, lr->dkey, 1, lr->akey);
		break;
	}
	case DAOS_OBJ_AKEY_RPC_ENUMERATE: {
		daos_obj_list_akey_t	*la = dc_task_get_args(task);

		rc = dc_tx_add_read(tx, &obj, opc, 0, la->dkey, 0, NULL);
		break;
	}
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
		rc = dc_tx_add_read(tx, &obj, opc, 0, NULL, 0, NULL);
		break;
	default:
		D_ERROR("Unsupportted TX attach opc %d\n", opc);
		rc = -DER_INVAL;
		break;
	}

	D_MUTEX_UNLOCK(&tx->tx_lock);
	dc_tx_decref(tx);

out:
	if (obj != NULL)
		obj_decref(obj);

	return rc;
}

struct tx_convert_cb_args {
	struct dc_tx		*conv_tx;
	tse_task_t		*conv_task;
	enum obj_rpc_opc	 conv_opc;
};

static int
dc_tx_convert_cb(tse_task_t *task, void *data)
{
	struct tx_convert_cb_args	*conv = data;
	struct dc_object		*obj = NULL;
	struct dc_tx			*tx = conv->conv_tx;
	tse_task_t			*parent = conv->conv_task;
	int				 rc = task->dt_result;

	if (rc == -DER_TX_RESTART) {
		struct tx_convert_cb_args	new_conv;
		uint32_t			backoff;

		D_MUTEX_LOCK(&tx->tx_lock);
		rc = dc_tx_restart_begin(tx, &backoff);
		if (rc != 0) {
			D_ERROR("Fail to restart TX for convert task "DF_RC"\n",
				DP_RC(rc));
			D_MUTEX_UNLOCK(&tx->tx_lock);
			goto out;
		}
		/*
		 * Since tx is internal, it is okay to end the restart before
		 * the backoff.
		 */
		dc_tx_restart_end(tx);
		D_MUTEX_UNLOCK(&tx->tx_lock);

		tx->tx_pm_ver = dc_pool_get_version(tx->tx_pool);

		switch (conv->conv_opc) {
		case DAOS_OBJ_RPC_UPDATE: {
			daos_obj_update_t	*up = dc_task_get_args(parent);

			obj = obj_hdl2ptr(up->oh);
			rc = dc_tx_add_update(tx, &obj, up->flags, up->dkey,
					      up->nr, up->iods, up->sgls);
			break;
		}
		case DAOS_OBJ_RPC_PUNCH: {
			daos_obj_punch_t	*pu = dc_task_get_args(parent);

			obj = obj_hdl2ptr(pu->oh);
			rc = dc_tx_add_punch_obj(tx, &obj, pu->flags);
			break;
		}
		case DAOS_OBJ_RPC_PUNCH_DKEYS: {
			daos_obj_punch_t	*pu = dc_task_get_args(parent);

			obj = obj_hdl2ptr(pu->oh);
			rc = dc_tx_add_punch_dkey(tx, &obj, pu->flags,
						  pu->dkey);
			break;
		}
		case DAOS_OBJ_RPC_PUNCH_AKEYS: {
			daos_obj_punch_t	*pu = dc_task_get_args(parent);

			obj = obj_hdl2ptr(pu->oh);
			rc = dc_tx_add_punch_akeys(tx, &obj, pu->flags,
						   pu->dkey, pu->akey_nr,
						   pu->akeys);
			break;
		}
		default:
			D_ASSERT(0);
		}

		if (rc != 0) {
			D_ERROR("Fail to re-attach TX for convert task "
				DF_RC"\n", DP_RC(rc));
			goto out;
		}

		new_conv = *conv;
		rc = tse_task_register_comp_cb(task, dc_tx_convert_cb,
					       &new_conv, sizeof(new_conv));
		if (rc != 0) {
			D_ERROR("Fail to re-add CB for TX convert task: "
				DF_RC"\n", DP_RC(rc));
			goto out;
		}

		return tse_task_reinit_with_delay(task, backoff);
	}

out:
	dc_tx_close_internal(tx);

	if (obj != NULL)
		obj_decref(obj);

	return rc;
}

int
dc_tx_convert(struct dc_object *obj, enum obj_rpc_opc opc, tse_task_t *task)
{
	struct tx_convert_cb_args	 conv = { 0 };
	daos_tx_commit_t		*args;
	tse_task_t			*tx_task = NULL;
	struct dc_tx			*tx = NULL;
	int				 rc = 0;

	D_ASSERT(obj != NULL);

	rc = dc_tx_alloc(obj->cob_coh, 0, DAOS_TF_ZERO_COPY, &tx);
	if (rc != 0) {
		D_ERROR("Fail to open TX for opc %u: "DF_RC"\n",
			opc, DP_RC(rc));
		goto out;
	}

	rc = dc_tx_attach(dc_tx_ptr2hdl(tx), obj, opc, task);
	obj = NULL;
	if (rc < 0) {
		D_ERROR("Fail to attach TX for opc %u: "DF_RC"\n",
			opc, DP_RC(rc));
		goto out;
	}

	rc = dc_task_create(dc_tx_commit, tse_task2sched(task), NULL, &tx_task);
	if (rc != 0) {
		D_ERROR("Fail to create tx convert task for opc %u: "DF_RC"\n",
			opc, DP_RC(rc));
		goto out;
	}

	args = dc_task_get_args(tx_task);
	args->th = dc_tx_ptr2hdl(tx);
	args->flags = 0;

	rc = dc_task_depend(task, 1, &tx_task);
	if (rc != 0) {
		D_ERROR("Fail to add dep on TX convert task: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	conv.conv_tx = tx;
	conv.conv_task = task;
	conv.conv_opc = opc;
	task = NULL;

	rc = tse_task_register_comp_cb(tx_task, dc_tx_convert_cb, &conv,
				       sizeof(conv));
	if (rc != 0) {
		D_ERROR("Fail to add CB for TX convert task: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	return dc_task_schedule(tx_task, true);

out:
	if (tx_task != NULL)
		tse_task_complete(tx_task, rc);

	if (task != NULL)
		tse_task_complete(task, rc);

	if (tx != NULL)
		dc_tx_close_internal(tx);

	if (obj != NULL)
		obj_decref(obj);

	return rc;
}
