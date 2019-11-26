/**
 * (C) Copyright 2018 Intel Corporation.
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
 * dc_tx: Transaction Client
 *
 * This module is part of libdaos. It implements the transaction DAOS API.
 */
#define D_LOGFAC	DD_FAC(container)

#include <daos/container.h>
#include <daos/event.h>
#include <daos_types.h>
#include <daos_task.h>
#include "cli_internal.h"
#include "rpc.h"

/*
 * MSC - this is a temporary implementation on top of Epoch API with wall clock
 * timestamps until we have the epoch model available at server side.
 */

enum {
	TX_OPEN,
	TX_COMMITTING,
	TX_COMMITTED,
	TX_ABORTING,
	TX_ABORTED,
	TX_FAILED,
};

enum {
	TX_RW,
	TX_RDONLY,
};

/* Client transaction handle */
struct dc_tx {
	/** link chain in the global handle hash table */
	struct d_hlink		tx_hlink;
	/** unique uuid for this transaction */
	uuid_t			tx_uuid;
	/** timestamp/epoch associated with transaction handle */
	daos_epoch_t		tx_epoch;
	/** container open handle */
	daos_handle_t		tx_coh;
	/** lock to protect status */
	pthread_spinlock_t	tx_spin;
	/** Transaction status (OPEN, COMMITTED, etc.) */
	uint8_t			tx_status;
	/** Transaction mode */
	uint8_t			tx_mode;
};

static void
tx_free(struct d_hlink *hlink)
{
	struct dc_tx *tx;

	tx = container_of(hlink, struct dc_tx, tx_hlink);
	D_ASSERT(daos_hhash_link_empty(&tx->tx_hlink));
	D_SPIN_DESTROY(&tx->tx_spin);
	D_FREE_PTR(tx);
}

static struct d_hlink_ops tx_h_ops = {
	.hop_free	= tx_free,
};

static struct dc_tx *
tx_alloc(void)
{
	struct dc_tx	*tx;
	int		rc;

	D_ALLOC_PTR(tx);
	if (tx == NULL)
		return NULL;

	rc = D_SPIN_INIT(&tx->tx_spin, PTHREAD_PROCESS_PRIVATE);
	if (rc) {
		D_FREE(tx);
		return NULL;
	}

	uuid_generate(tx->tx_uuid);

	daos_hhash_hlink_init(&tx->tx_hlink, &tx_h_ops);
	return tx;
}

void
tx_decref(struct dc_tx *tx)
{
	daos_hhash_link_putref(&tx->tx_hlink);
}

static daos_handle_t
tx_ptr2hdl(struct dc_tx *tx)
{
	daos_handle_t th;

	daos_hhash_link_key(&tx->tx_hlink, &th.cookie);
	return th;
}

struct dc_tx *
tx_hdl2ptr(daos_handle_t th)
{
	struct d_hlink *hlink;

	hlink = daos_hhash_link_lookup(th.cookie);
	if (hlink == NULL)
		return NULL;

	return container_of(hlink, struct dc_tx, tx_hlink);
}

static void
tx_hdl_link(struct dc_tx *tx)
{
	daos_hhash_link_insert(&tx->tx_hlink, DAOS_HTYPE_TX);
}

static void
tx_hdl_unlink(struct dc_tx *tx)
{
	daos_hhash_link_delete(&tx->tx_hlink);
}

int
dc_tx_check(daos_handle_t th, bool check_write, daos_epoch_t *epoch)
{
	struct dc_tx *tx = NULL;

	if (daos_handle_is_inval(th))
		return -DER_INVAL;

	tx = tx_hdl2ptr(th);
	if (tx == NULL)
		return -DER_NO_HDL;

	if (tx->tx_status == TX_FAILED) {
		D_ERROR("Can't use a failed transaction\n");
		return -DER_NO_PERM;
	}

	if (check_write) {
		if (tx->tx_status != TX_OPEN) {
			D_ERROR("TX is not valid for update.\n");
			return -DER_NO_PERM;
		}

		if (tx->tx_mode != TX_RW) {
			D_ERROR("TX is READ ONLY\n");
			return -DER_NO_PERM;
		}
	}

	*epoch = tx->tx_epoch;
	tx_decref(tx);
	return 0;
}

int
daos_tx_hdl2epoch(daos_handle_t th, daos_epoch_t *epoch)
{
	struct dc_tx *tx = NULL;

	if (daos_handle_is_inval(th)) {
		*epoch = crt_hlc_get();
		return 0;
	}

	tx = tx_hdl2ptr(th);
	if (tx == NULL)
		return -DER_NO_HDL;

	*epoch = tx->tx_epoch;
	tx_decref(tx);
	return 0;
}

int
dc_tx_open(tse_task_t *task)
{
	daos_tx_open_t	*args;
	struct dc_tx	*tx;
	int		rc = 0;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	/** Create a transaction handle */
	tx = tx_alloc();
	if (tx == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	tx->tx_coh	= args->coh;
	tx->tx_epoch	= crt_hlc_get();
	tx->tx_status	= TX_OPEN;
	tx->tx_mode	= TX_RW;

	tx_hdl_link(tx);
	*args->th = tx_ptr2hdl(tx);

out:
	tse_task_complete(task, rc);
	return rc;
}

static int
tx_commit_cb(tse_task_t *task, void *data)
{
	struct dc_tx		*tx = *((struct dc_tx **)data);
	int			rc = task->dt_result;

	D_SPIN_LOCK(&tx->tx_spin);

	if (rc != 0)
		tx->tx_status = TX_FAILED;
	else
		tx->tx_status = TX_COMMITTED;

	D_SPIN_UNLOCK(&tx->tx_spin);

	tx_decref(tx);
	return rc;
}

int
dc_tx_commit(tse_task_t *task)
{
	daos_tx_commit_t	*args;
	struct dc_tx		*tx;
	int			rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	tx = tx_hdl2ptr(args->th);
	if (tx == NULL) {
		D_ERROR("Invalid TX handle\n");
		D_GOTO(err_task, rc = -DER_NO_HDL);
	}

	if (tx->tx_mode != TX_RW) {
		D_ERROR("Can't commit a RDONLY TX\n");
		D_GOTO(err_task, rc = -DER_NO_PERM);
	}

	D_SPIN_LOCK(&tx->tx_spin);
	if (tx->tx_status != TX_OPEN) {
		D_ERROR("Can't commit a transaction that is not open\n");
		D_SPIN_UNLOCK(&tx->tx_spin);
		D_GOTO(err_tx, rc = -DER_INVAL);
	}
	tx->tx_status = TX_COMMITTING;
	D_SPIN_UNLOCK(&tx->tx_spin);

	rc = dc_epoch_op(tx->tx_coh, CONT_EPOCH_COMMIT, &tx->tx_epoch, task);
	if (rc != 0) {
		D_ERROR("Failed to register completion cb\n");
		D_GOTO(err_tx, rc);
	}

	/** CB to update TX status */
	rc = tse_task_register_cbs(task, NULL, NULL, 0, tx_commit_cb, &tx,
				   sizeof(tx));
	if (rc != 0) {
		D_ERROR("Failed to register completion cb\n");
		D_GOTO(err_tx, rc);
	}

	/** tx_decref done in tx_commit_cb() */
	return rc;
err_tx:
	tx_decref(tx);
err_task:
	tse_task_complete(task, rc);
	return rc;
}


static int
tx_abort_cb(tse_task_t *task, void *data)
{
	struct dc_tx		*tx = *((struct dc_tx **)data);
	int			rc = task->dt_result;

	D_SPIN_LOCK(&tx->tx_spin);

	if (rc != 0)
		tx->tx_status = TX_FAILED;
	else
		tx->tx_status = TX_ABORTED;

	D_SPIN_UNLOCK(&tx->tx_spin);

	tx_decref(tx);
	return rc;
}

int
dc_tx_abort(tse_task_t *task)
{
	daos_tx_abort_t		*args;
	struct dc_tx		*tx;
	int			rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	tx = tx_hdl2ptr(args->th);
	if (tx == NULL) {
		D_ERROR("Invalid TX handle\n");
		D_GOTO(err_task, rc = -DER_NO_HDL);
	}

	if (tx->tx_mode != TX_RW) {
		D_ERROR("Can't abort a RDONLY TX\n");
		D_GOTO(err_task, rc = -DER_NO_PERM);
	}

	D_SPIN_LOCK(&tx->tx_spin);
	if (tx->tx_status != TX_OPEN) {
		D_ERROR("Can't discard a transaction that is not open\n");
		D_SPIN_UNLOCK(&tx->tx_spin);
		D_GOTO(err_tx, rc = -DER_INVAL);
	}
	tx->tx_status = TX_ABORTING;
	D_SPIN_UNLOCK(&tx->tx_spin);

	rc = dc_epoch_op(tx->tx_coh, CONT_EPOCH_DISCARD, &tx->tx_epoch, task);
	if (rc != 0) {
		D_ERROR("Failed to register completion cb\n");
		D_GOTO(err_tx, rc);
	}

	/** CB to update TX status */
	rc = tse_task_register_cbs(task, NULL, NULL, 0, tx_abort_cb, &tx,
				   sizeof(tx));
	if (rc != 0) {
		D_ERROR("Failed to register completion cb\n");
		D_GOTO(err_tx, rc);
	}

	/** tx_decref done in tx_abort_cb() */
	return rc;
err_tx:
	tx_decref(tx);
err_task:
	tse_task_complete(task, rc);
	return rc;
}

int
dc_tx_open_snap(tse_task_t *task)
{
	daos_tx_open_snap_t	*args;
	struct dc_tx		*tx;
	int			rc = 0;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	/** Create a transaction handle */
	tx = tx_alloc();
	if (tx == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	uuid_generate(tx->tx_uuid);
	tx->tx_coh	= args->coh;
	tx->tx_epoch	= args->epoch;
	tx->tx_status	= TX_OPEN;
	tx->tx_mode	= TX_RDONLY;

	tx_hdl_link(tx);
	*args->th = tx_ptr2hdl(tx);

out:
	tse_task_complete(task, rc);
	return rc;
}

int
dc_tx_close(tse_task_t *task)
{
	daos_tx_close_t		*args;
	struct dc_tx		*tx;
	int			rc = 0;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	tx = tx_hdl2ptr(args->th);
	if (tx == NULL)
		D_GOTO(err_task, rc = -DER_NO_HDL);

	D_SPIN_LOCK(&tx->tx_spin);
	if (tx->tx_status == TX_COMMITTING || tx->tx_status == TX_ABORTING) {
		D_ERROR("Can't close a transaction committing or aborting\n");
		D_GOTO(err_tx, rc = -DER_BUSY);
	}
	D_SPIN_UNLOCK(&tx->tx_spin);

	tx_hdl_unlink(tx);
	/** -1 for hdl2ptr */
	tx_decref(tx);
	/** -1 for create */
	tx_decref(tx);

	tse_task_complete(task, rc);
	return rc;
err_tx:
	D_SPIN_UNLOCK(&tx->tx_spin);
	tx_decref(tx);
err_task:
	tse_task_complete(task, rc);
	return rc;
}

/*
 * MSC - this is a temporary special TX for rebuild that needs to use the client
 * stack with a specific epoch.
 */
int
dc_tx_local_open(daos_handle_t coh, daos_epoch_t epoch, daos_handle_t *th)
{
	struct dc_tx *tx;

	/** Create a transaction handle */
	tx = tx_alloc();
	if (tx == NULL)
		return -DER_NOMEM;

	tx->tx_coh	= coh;
	tx->tx_epoch	= epoch;
	tx->tx_status	= TX_OPEN;
	tx->tx_mode	= TX_RDONLY;

	tx_hdl_link(tx);
	*th = tx_ptr2hdl(tx);

	return 0;
}

int
dc_tx_local_close(daos_handle_t th)
{
	struct dc_tx *tx;

	tx = tx_hdl2ptr(th);
	if (tx == NULL)
		return -DER_NO_HDL;

	tx_hdl_unlink(tx);
	/** -1 for hdl2ptr */
	tx_decref(tx);
	/** -1 for create */
	tx_decref(tx);

	return 0;
}
