/*
 * (C) Copyright 2015-2019 Intel Corporation.
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
 * \file
 *
 * DAOS Transaction API methods
 */

#ifndef __DAOS_TX_H__
#define __DAOS_TX_H__

#include <daos_types.h>

#include <daos_pool.h>
#include <daos_container.h>
#include <daos_object.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Open a transaction on a container handle. This returns a transaction handle
 * that is tagged with the current epoch. The transaction handle can be used
 * for IOs that need to be committed transactionally.
 *
 * \param[in]	coh	Container handle.
 * \param[out]	th	Returned transaction handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 if Success, negative if failed.
 */
int
daos_tx_open(daos_handle_t coh, daos_handle_t *th, daos_event_t *ev);

/**
 * Commit the transaction on the container it was created with. The transaction
 * can't be used for future updates anymore. If -DER_RESTART was returned, the
 * operations that have been done on this transaction need to be redone with a
 * newer transaction since a conflict was detected with another transaction.
 *
 * \param[in]	th	Transaction handle to commit.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 if Success, negative if failed.
 *			Possible error values include:
 *			-DER_NO_HDL     invalid transaction handle.
 *			-DER_INVAL      Invalid parameter
 *			-DER_RESTART	transaction conflict detected.
 */
int
daos_tx_commit(daos_handle_t th, daos_event_t *ev);

/**
 * Create a read-only transaction from a snapshot. This does not create the
 * snapshot, but only a read transaction to be able to read from a persistent
 * snapshot in the container. If the user passes an epoch that is not
 * snapshoted, or the snapshot was deleted, reads using that transaction might
 * fail if the epoch was aggregated.
 *
 * \param[in]	coh	Container handle.
 * \param[in]	epoch	Epoch of snapshot to read from.
 * \param[out]	th	Returned read only transaction handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 if Success, negative if failed.
 */
int
daos_tx_open_snap(daos_handle_t coh, daos_epoch_t epoch, daos_handle_t *th,
		  daos_event_t *ev);

/**
 * Abort all updates on the transaction. The transaction can't be used for
 * future updates anymore.
 *
 * \param[in]	th	Transaction handle to abort.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 if Success, negative if failed.
 */
int
daos_tx_abort(daos_handle_t th, daos_event_t *ev);

/**
 * Close and free the transaction handle. This is a local operation, no RPC
 * involved.
 *
 * \param[in]	th	Transaction handle to free.
 *
 * \return		0 if Success, negative if failed.
 */
int
daos_tx_close(daos_handle_t th, daos_event_t *ev);

/**
 * Return epoch associated with the transaction handle.
 *
 * \param[in]	th	Transaction handle.
 * \param[out]	th	Returned epoch value.
 *
 * \return		0 if Success, negative if failed.
 */
int
daos_tx_hdl2epoch(daos_handle_t th, daos_epoch_t *epoch);

#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_TX_H__ */
