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

enum {
	TX_OPEN,
	TX_COMMITTING,
	TX_COMMITTED,
	TX_ABORTED,
	TX_FAILED,
};

struct dc_tx_sub_req {
	d_list_t		 dtsr_link;
	struct dc_object	*dtsr_obj;
	uint64_t		 dtsr_dkey_hash;
	uint32_t		 dtsr_opc;
	uint32_t		 dtsr_flags;
	uint32_t		 dtsr_nr;
	uint32_t		 dtsr_zero_copy:1;
	daos_key_t		 dtsr_dkey_inline;
	daos_key_t		*dtsr_dkey;
	union {
		daos_key_t	*dtsr_akeys;
		daos_iod_t	*dtsr_iods;
	};
	d_sg_list_t		*dtsr_sgls;
};

/* Client transaction handle */
struct dc_tx {
	/** link chain in the global handle hash table */
	struct d_hlink		 tx_hlink;
	/** The transation identifier, that contains the timestamp. */
	struct dtx_id		 tx_id;
	/** container open handle */
	daos_handle_t		 tx_coh;
	/** The list of dc_tx_sub_req */
	d_list_t		 tx_sub_reqs;
	/** The sub requests count */
	uint32_t		 tx_sub_count;
	/** Transaction flags (RDONLY, etc.) */
	uint32_t		 tx_flags;
	/** Transaction status (OPEN, COMMITTED, etc.) */
	uint32_t		 tx_status;
	/** Pool map version when trigger first read. */
	uint32_t		 tx_pm_ver;
	/** The rank for the server on which the coordinator resides. */
	uint32_t		 tx_coordinator_rank;
	/** The target index for the coordinator. */
	uint32_t		 tx_coordinator_tag;
	/** Reference the pool. */
	struct dc_pool		*tx_pool;
};

static void
dc_tx_free(struct d_hlink *hlink)
{
	struct dc_tx	*tx;

	tx = container_of(hlink, struct dc_tx, tx_hlink);
	D_ASSERT(daos_hhash_link_empty(&tx->tx_hlink));

	dc_pool_put(tx->tx_pool);
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
dc_tx_alloc(daos_handle_t coh, daos_epoch_t epoch, uint32_t flags,
	    struct dc_tx **ptx)
{
	daos_handle_t	 ph;
	struct dc_tx	*tx;

	if (daos_handle_is_inval(coh))
		return -DER_NO_HDL;

	ph = dc_cont_hdl2pool_hdl(coh);
	D_ASSERT(!daos_handle_is_inval(ph));

	D_ALLOC_PTR(tx);
	if (tx == NULL)
		return -DER_NOMEM;

	tx->tx_pool = dc_hdl2pool(ph);
	D_ASSERT(tx->tx_pool != NULL);

	D_INIT_LIST_HEAD(&tx->tx_sub_reqs);
	daos_dti_gen(&tx->tx_id, epoch);
	tx->tx_coh = coh;
	tx->tx_flags = flags;
	tx->tx_status = TX_OPEN;
	daos_hhash_hlink_init(&tx->tx_hlink, &tx_h_ops);
	dc_tx_hdl_link(tx);

	*ptx = tx;

	return 0;
}

static int
dc_tx_check(daos_handle_t th, bool check_write, struct dc_tx **ptx)
{
	struct dc_tx	*tx = NULL;
	int		 rc = 0;

	if (daos_handle_is_inval(th))
		return -DER_INVAL;

	tx = dc_tx_hdl2ptr(th);
	if (tx == NULL)
		return -DER_NO_HDL;

	if (tx->tx_status != TX_OPEN) {
		D_ERROR("TX is not valid for update.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	if (check_write && tx->tx_flags & DTF_RDONLY) {
		D_ERROR("TX is READ ONLY\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

out:
	if (rc != 0)
		dc_tx_decref(tx);
	else
		*ptx = tx;

	return rc;
}

static void
dc_tx_cleanup(struct dc_tx *tx)
{
	struct dc_tx_sub_req	*dtsr;
	int			 i;

	while ((dtsr = d_list_pop_entry(&tx->tx_sub_reqs,
					struct dc_tx_sub_req,
					dtsr_link)) != NULL) {
		switch (dtsr->dtsr_opc) {
		case DAOS_OBJ_RPC_UPDATE:
			if (dtsr->dtsr_zero_copy)
				break;

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
			if (!dtsr->dtsr_zero_copy)
				daos_iov_free(dtsr->dtsr_dkey);
			break;
		case DAOS_OBJ_RPC_PUNCH_AKEYS:
			if (!dtsr->dtsr_zero_copy) {
				for (i = 0; i < dtsr->dtsr_nr; i++)
					daos_iov_free(&dtsr->dtsr_akeys[i]);

				D_FREE(dtsr->dtsr_akeys);
				daos_iov_free(dtsr->dtsr_dkey);
			}
			break;
		default:
			D_ASSERT(0);
		}

		obj_decref(dtsr->dtsr_obj);
		D_FREE(dtsr);
	}

	tx->tx_sub_count = 0;
}

int
dc_tx_hdl2epoch_with_check(daos_handle_t th, bool check_write,
			   daos_epoch_t *epoch, uint32_t *pm_ver)
{
	struct dc_tx	*tx = NULL;
	int		 rc;

	rc = dc_tx_check(th, check_write, &tx);
	if (rc != 0)
		return rc;

	*pm_ver = dc_pool_get_version_lock(tx->tx_pool);

	if (tx->tx_pm_ver == 0) {
		tx->tx_pm_ver = *pm_ver;
	} else if (tx->tx_pm_ver != *pm_ver) {
		D_ASSERTF(tx->tx_pm_ver < *pm_ver,
			  "Pool map version is reverted from %u to %u\n",
			  tx->tx_pm_ver, *pm_ver);

		tx->tx_pm_ver = *pm_ver;

		/* If the pool map is refreshed, then it is possible that
		 * the data from former fetch/list/query become stale. On
		 * the other hand, even if related data is still valid,
		 * but related read timestamp (used for MVCC) may be left
		 * on the server that has been evicted from the cluster.
		 * So we have to restart the transaction under such case.
		 */
		D_GOTO(out, rc = -DER_AGAIN);
	}

	*epoch = daos_dti2epoch(&tx->tx_id);

out:
	dc_tx_decref(tx);

	return rc;
}

int
dc_tx_hdl2epoch(daos_handle_t th, daos_epoch_t *epoch)
{
	struct dc_tx	*tx;

	if (daos_handle_is_inval(th))
		return -DER_INVAL;

	tx = dc_tx_hdl2ptr(th);
	if (tx == NULL)
		return -DER_NO_HDL;

	*epoch = daos_dti2epoch(&tx->tx_id);
	dc_tx_decref(tx);

	return 0;
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

	rc = dc_tx_alloc(args->coh, crt_hlc_get(), 0, &tx);
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

	if (rc == 0) {
		tx->tx_status = TX_COMMITTED;
		goto out;
	}

	/* Need to restart the transaction. */
	if (rc == -DER_AGAIN) {
		tx->tx_status = TX_FAILED;
		goto out;
	}

	/* TBD:
	 * 1. Check pool map, if pool map is refreshed, restart TX.
	 * 2. Retry the RPC if it is retriable error.
	 * 3. Failout for other failures.
	 */

	tx->tx_status = TX_FAILED;

out:
	crt_req_decref(req);
	dc_tx_decref(tx);

	return rc;
}

static int
dc_tx_elect_coordinator(struct dc_tx *tx)
{
	/* TBD: elect coordinator. */

	return 0;
}

static int
dc_tx_commit_trigger(tse_task_t *task, struct dc_tx *tx)
{
	crt_rpc_t			*req = NULL;
	struct tx_commit_cb_args	*tcca;
	crt_endpoint_t			 tgt_ep;
	int				 rc;

	rc = dc_tx_elect_coordinator(tx);
	if (rc != 0)
		goto out;

	tgt_ep.ep_grp = tx->tx_pool->dp_sys->sy_group;
	tgt_ep.ep_tag = tx->tx_coordinator_tag;
	tgt_ep.ep_rank = tx->tx_coordinator_rank;

	rc = obj_req_create(daos_task2ctx(task), &tgt_ep,
			    DAOS_OPC_DTX_CPD, &req);
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

	/* TBD:
	 * 1. Prepare bulk.
	 * 2. Rack sub requests into the compounded DTX RPC.
	 */

	tx->tx_status = TX_COMMITTING;
	rc = daos_rpc_send(req, task);
	if (rc != 0)
		D_ERROR("CPD RPC failed rc "DF_RC"\n", DP_RC(rc));

	return rc;

out:
	if (req != NULL)
		crt_req_decref(req);

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

	if (tx->tx_status == TX_COMMITTED)
		D_GOTO(out_tx, rc = 0);

	if (tx->tx_status == TX_COMMITTING)
		D_GOTO(out_tx, rc = -DER_BUSY);

	if (tx->tx_status != TX_OPEN) {
		D_ERROR("Can't commit non-open state transaction (%d)\n",
			tx->tx_status);
		D_GOTO(out_tx, rc = -DER_INVAL);
	}

	if (tx->tx_flags & DTF_RDONLY) {
		D_ERROR("Can't commit a RDONLY TX\n");
		D_GOTO(out_tx, rc = -DER_NO_PERM);
	}

	if (d_list_empty(&tx->tx_sub_reqs)) {
		tx->tx_status = TX_COMMITTED;

		D_GOTO(out_tx, rc = 0);
	}

	rc = dc_tx_commit_trigger(task, tx);

	return rc;

out_tx:
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

	if (tx->tx_status == TX_ABORTED)
		D_GOTO(out_tx, rc = 0);

	if (tx->tx_status != TX_OPEN) {
		D_ERROR("Can't commit non-open state transaction (%d)\n",
			tx->tx_status);
		D_GOTO(out_tx, rc = -DER_INVAL);
	}

	if (tx->tx_flags & DTF_RDONLY) {
		D_ERROR("Can't abort a RDONLY TX\n");
		D_GOTO(out_tx, rc = -DER_NO_PERM);
	}

	tx->tx_status = TX_ABORTED;

out_tx:
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

	rc = dc_tx_alloc(args->coh, args->epoch, DTF_RDONLY, &tx);
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

	if (tx->tx_status == TX_COMMITTING) {
		D_ERROR("Can't close a transaction in committing\n");
		D_GOTO(out_tx, rc = -DER_BUSY);
	}

	dc_tx_cleanup(tx);
	dc_tx_hdl_unlink(tx);
	/* -1 for create */
	dc_tx_decref(tx);

out_tx:
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

	if (tx->tx_status == TX_COMMITTING) {
		D_ERROR("Can't close a transaction in committing\n");
		D_GOTO(out_tx, rc = -DER_BUSY);
	}

	dc_tx_cleanup(tx);
	dc_tx_hdl_unlink(tx);
	/* -1 for create */
	dc_tx_decref(tx);

out_tx:
	/* -1 for hdl2ptr */
	dc_tx_decref(tx);

	return rc;
}

int
dc_tx_restart(daos_handle_t th)
{
	struct dc_tx	*tx;
	int		 rc = 0;

	tx = dc_tx_hdl2ptr(th);
	if (tx == NULL)
		return -DER_NO_HDL;

	if (tx->tx_status != TX_OPEN && tx->tx_status != TX_FAILED) {
		D_ERROR("Can't restart non-open/non-failed state "
			"transaction (%d)\n", tx->tx_status);
		rc = -DER_INVAL;
	} else {
		dc_tx_cleanup(tx);
		daos_dti_gen(&tx->tx_id, crt_hlc_get());
		tx->tx_status = TX_OPEN;
	}

	/* -1 for hdl2ptr */
	dc_tx_decref(tx);

	return rc;
}

int
dc_obj_update_attach(daos_handle_t oh, daos_handle_t th, uint64_t flags,
		     daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
		     d_sg_list_t *sgls)
{
	struct dc_tx		*tx = NULL;
	struct dc_tx_sub_req	*dtsr = NULL;
	int			 rc;
	int			 i;

	rc = dc_tx_check(th, true, &tx);
	if (rc != 0)
		return rc;

	D_ALLOC_PTR(dtsr);
	if (dtsr == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dtsr->dtsr_obj = obj_hdl2ptr(oh);
	if (dtsr->dtsr_obj == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	dtsr->dtsr_opc = DAOS_OBJ_RPC_UPDATE;
	dtsr->dtsr_dkey_hash = obj_dkey2hash(dkey);
	dtsr->dtsr_flags = flags & ~DAOS_ZERO_COPY;
	dtsr->dtsr_nr = nr;

	if (flags & DAOS_ZERO_COPY) {
		dtsr->dtsr_zero_copy = 1;
		dtsr->dtsr_dkey = dkey;
		dtsr->dtsr_iods = iods;
		dtsr->dtsr_sgls = sgls;
	} else {
		dtsr->dtsr_dkey = &dtsr->dtsr_dkey_inline;
		rc = daos_iov_copy(dtsr->dtsr_dkey, &dkey[0]);
		if (rc != 0)
			D_GOTO(out, rc);

		if (nr == 0)
			goto link;

		D_ALLOC_ARRAY(dtsr->dtsr_iods, nr);
		if (dtsr->dtsr_iods == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		D_ALLOC_ARRAY(dtsr->dtsr_sgls, nr);
		if (dtsr->dtsr_sgls == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		for (i = 0; i < nr; i++) {
			rc = daos_iov_copy(&dtsr->dtsr_iods[i].iod_name,
					   &iods[i].iod_name);
			if (rc != 0)
				D_GOTO(out, rc);

			dtsr->dtsr_iods[i].iod_type = iods[i].iod_type;
			dtsr->dtsr_iods[i].iod_size = iods[i].iod_size;
			dtsr->dtsr_iods[i].iod_nr = iods[i].iod_nr;

			D_ALLOC_ARRAY(dtsr->dtsr_iods[i].iod_recxs,
				      iods[i].iod_nr);
			if (dtsr->dtsr_iods[i].iod_recxs == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			memcpy(dtsr->dtsr_iods[i].iod_recxs, iods[i].iod_recxs,
			       sizeof(daos_recx_t) * iods[i].iod_nr);
		}

		rc = daos_sgls_copy_data_out(dtsr->dtsr_sgls, nr, sgls, nr);
		if (rc != 0)
			D_GOTO(out, rc);
	}

link:
	d_list_add_tail(&dtsr->dtsr_link, &tx->tx_sub_reqs);
	tx->tx_sub_count++;

out:
	dc_tx_decref(tx);
	if (rc != 0 && dtsr != NULL) {
		if (!(flags & DAOS_ZERO_COPY)) {
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
					daos_sgl_fini(&dtsr->dtsr_sgls[i],
						      true);

				D_FREE(dtsr->dtsr_sgls);
			}

			daos_iov_free(dtsr->dtsr_dkey);
		}

		D_FREE(dtsr);
	}

	return rc;
}

int
dc_obj_punch_attach(daos_handle_t oh, daos_handle_t th, uint64_t flags)
{
	struct dc_tx		*tx = NULL;
	struct dc_tx_sub_req	*dtsr = NULL;
	int			 rc;

	rc = dc_tx_check(th, true, &tx);
	if (rc != 0)
		return rc;

	D_ALLOC_PTR(dtsr);
	if (dtsr == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dtsr->dtsr_obj = obj_hdl2ptr(oh);
	if (dtsr->dtsr_obj == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	dtsr->dtsr_opc = DAOS_OBJ_RPC_PUNCH;
	dtsr->dtsr_dkey_hash = 0;
	dtsr->dtsr_flags = flags & ~DAOS_ZERO_COPY;

	if (flags & DAOS_ZERO_COPY)
		dtsr->dtsr_zero_copy = 1;

	d_list_add_tail(&dtsr->dtsr_link, &tx->tx_sub_reqs);
	tx->tx_sub_count++;

out:
	dc_tx_decref(tx);
	if (rc != 0 && dtsr != NULL)
		D_FREE(dtsr);

	return rc;
}

int
dc_obj_punch_dkeys_attach(daos_handle_t oh, daos_handle_t th, uint64_t flags,
			  unsigned int nr, daos_key_t *dkeys)
{
	struct dc_tx		*tx = NULL;
	struct dc_tx_sub_req	*dtsr = NULL;
	int			 rc;

	D_ASSERTF(nr == 1, "Invalid dkey count %d\n", nr);

	rc = dc_tx_check(th, true, &tx);
	if (rc != 0)
		return rc;

	D_ALLOC_PTR(dtsr);
	if (dtsr == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dtsr->dtsr_obj = obj_hdl2ptr(oh);
	if (dtsr->dtsr_obj == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	dtsr->dtsr_opc = DAOS_OBJ_RPC_PUNCH_DKEYS;
	dtsr->dtsr_dkey_hash = obj_dkey2hash(&dkeys[0]);
	dtsr->dtsr_flags = flags & ~DAOS_ZERO_COPY;

	if (flags & DAOS_ZERO_COPY) {
		dtsr->dtsr_zero_copy = 1;
		dtsr->dtsr_dkey = &dkeys[0];
	} else {
		dtsr->dtsr_dkey = &dtsr->dtsr_dkey_inline;
		rc = daos_iov_copy(dtsr->dtsr_dkey, &dkeys[0]);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	d_list_add_tail(&dtsr->dtsr_link, &tx->tx_sub_reqs);
	tx->tx_sub_count++;

out:
	dc_tx_decref(tx);
	if (rc != 0 && dtsr != NULL) {
		if (dtsr->dtsr_obj != NULL)
			obj_decref(dtsr->dtsr_obj);

		D_FREE(dtsr);
	}

	return rc;
}

int
dc_obj_punch_akeys_attach(daos_handle_t oh, daos_handle_t th, uint64_t flags,
			  daos_key_t *dkey, unsigned int nr, daos_key_t *akeys)
{
	struct dc_tx		*tx = NULL;
	struct dc_tx_sub_req	*dtsr = NULL;
	int			 rc;
	int			 i;

	rc = dc_tx_check(th, true, &tx);
	if (rc != 0)
		return rc;

	D_ALLOC_PTR(dtsr);
	if (dtsr == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dtsr->dtsr_obj = obj_hdl2ptr(oh);
	if (dtsr->dtsr_obj == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	dtsr->dtsr_opc = DAOS_OBJ_RPC_PUNCH_AKEYS;
	dtsr->dtsr_dkey_hash = obj_dkey2hash(dkey);
	dtsr->dtsr_flags = flags & ~DAOS_ZERO_COPY;
	dtsr->dtsr_nr = nr;

	if (flags & DAOS_ZERO_COPY) {
		dtsr->dtsr_zero_copy = 1;
		dtsr->dtsr_dkey = dkey;
		dtsr->dtsr_akeys = akeys;
	} else {
		dtsr->dtsr_dkey = &dtsr->dtsr_dkey_inline;
		rc = daos_iov_copy(dtsr->dtsr_dkey, &dkey[0]);
		if (rc != 0)
			D_GOTO(out, rc);

		if (nr == 0)
			goto link;

		D_ALLOC_ARRAY(dtsr->dtsr_akeys, nr);
		if (dtsr->dtsr_akeys == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		for (i = 0; i < nr; i++) {
			rc = daos_iov_copy(&dtsr->dtsr_akeys[i], &akeys[i]);
			if (rc != 0)
				D_GOTO(out, rc);
		}
	}

link:
	d_list_add_tail(&dtsr->dtsr_link, &tx->tx_sub_reqs);
	tx->tx_sub_count++;

out:
	dc_tx_decref(tx);
	if (rc != 0 && dtsr != NULL) {
		if (dtsr->dtsr_obj != NULL)
			obj_decref(dtsr->dtsr_obj);

		if (!(flags & DAOS_ZERO_COPY)) {
			if (dtsr->dtsr_akeys != NULL) {
				for (i = 0; i < nr; i++)
					daos_iov_free(&dtsr->dtsr_akeys[i]);

				D_FREE(dtsr->dtsr_akeys);
			}

			daos_iov_free(dtsr->dtsr_dkey);
		}

		D_FREE(dtsr);
	}

	return rc;
}
