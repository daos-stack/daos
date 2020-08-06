/*
 * (C) Copyright 2015-2020 Intel Corporation.
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
 * DAOS API methods
 */

#ifndef __DAOS_API_H__
#define __DAOS_API_H__

#if defined(__cplusplus)
extern "C" {
#endif

enum {
	/** The transaction is read only. */
	DAOS_TF_RDONLY		= (1 << 0),
	/**
	 * Not copy application data buffer when cache modification on client
	 * for the distributed transaction.
	 *
	 * Please note that the key buffer will always be copied when caching.
	 * Then the TX sponsor can reuse or release related key' buffer after
	 * the operation returning to avoid more programming restriction under
	 * DAOS transaction model.
	 */
	DAOS_TF_ZERO_COPY	= (1 << 1),
};

/**
 * Generate a rank list from a string with a separator argument. This is a
 * convenience function to generate the rank list required by
 * daos_pool_connect().
 *
 * \param[in]	str	string with the rank list
 * \param[in]	sep	separator of the ranks in \a str.
 *			dmg uses ":" as the separator.
 *
 * \return		allocated rank list that user is responsible to free
 *			with d_rank_list_free().
 */
d_rank_list_t *daos_rank_list_parse(const char *str, const char *sep);

/*
 * Transaction API
 */

/**
 * Open a transaction on a container handle. This returns a transaction handle
 * that is tagged with the current epoch. The transaction handle can be used
 * for IOs that need to be committed transactionally.
 *
 * \param[in]	coh	Container handle.
 * \param[out]	th	Returned transaction handle.
 * \param[in]	flags	Transaction flags (DAOS_TF_RDONLY, etc.).
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 if Success, negative if failed.
 */
int
daos_tx_open(daos_handle_t coh, daos_handle_t *th, uint64_t flags,
	     daos_event_t *ev);

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
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 if Success, negative if failed.
 */
int
daos_tx_close(daos_handle_t th, daos_event_t *ev);

/**
 * Restart the transaction handle. It drops all the modifications that have
 * been issued via the handle. This is a local operation, no RPC involved.
 *
 * \param[in]	th	Transaction handle to be restarted.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 if Success, negative if failed.
 */
int
daos_tx_restart(daos_handle_t th, daos_event_t *ev);

/**
 * Return the epoch associated with the transaction handle. An epoch may not be
 * available at the beginning of the transaction, but one shall be available
 * after the transaction successfully commits.
 *
 * This function is specific to the current implementation. It should only be
 * used for testing and debugging purposes.
 *
 * \param[in]	th	Transaction handle.
 * \param[out]	epoch	Returned epoch value.
 *
 * \return		0 if Success, negative if failed.
 * \retval -DER_UNINIT	An epoch is not available yet.
 */
int
daos_tx_hdl2epoch(daos_handle_t th, daos_epoch_t *epoch);

#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_API_H__ */
