/*
 * (C) Copyright 2015-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

/** Flags for daos_tx_open */
enum {
	/** The transaction is read only. */
	DAOS_TF_RDONLY = (1 << 0),
	/**
	 * Do not copy caller data buffers during modifications associated with
	 * the transaction. The buffers must remain unchanged until the
	 * daos_tx_commit operation for the transaction completes.
	 *
	 * Key buffers are always copied, regardless of this flag. They can be
	 * released or repurposed after corresponding operations complete.
	 */
	DAOS_TF_ZERO_COPY = (1 << 1),
};

/**
 * Generate a rank list from a string with a separator argument. This is a
 * convenience function to generate the rank list.
 *
 * \param[in]	str	string with the rank list
 * \param[in]	sep	separator of the ranks in \a str.
 *			dmg uses ":" as the separator.
 *
 * \return		allocated rank list that user is responsible to free
 *			with d_rank_list_free().
 */
d_rank_list_t *
daos_rank_list_parse(const char *str, const char *sep);

/*
 * Transaction API
 */

/**
 * Open a transaction on a container handle. The resulting transaction handle
 * can be used for IOs in this container that need to be committed
 * transactionally.
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
daos_tx_open(daos_handle_t coh, daos_handle_t *th, uint64_t flags, daos_event_t *ev);

/**
 * Commit the transaction. If the operation succeeds, the transaction handle
 * cannot be used for any new IO. If -DER_TX_RESTART is returned, the caller
 * needs to restart the transaction with the same transaction handle, by
 * calling daos_tx_restart, re-executing the caller code for this transaction,
 * and calling daos_tx_commit again.
 *
 * \param[in]	th	Transaction handle to commit.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 if Success, negative if failed.
 *			Possible error values include:
 *			-DER_NO_HDL     invalid transaction handle.
 *			-DER_INVAL      Invalid parameter
 *			-DER_TX_RESTART	transaction needs to restart (e.g.,
 *					due to conflicts).
 */
int
daos_tx_commit(daos_handle_t th, daos_event_t *ev);

/**
 * Create a read-only transaction from a snapshot. This does not create the
 * snapshot, but only a read transaction to be able to read from a snapshot
 * created with daos_cont_create_snap. If the user passes an epoch that is not
 * snapshoted, or the snapshot was deleted, reads using that transaction may
 * get undefined results.
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
daos_tx_open_snap(daos_handle_t coh, daos_epoch_t epoch, daos_handle_t *th, daos_event_t *ev);

/**
 * Abort all modifications on the transaction. The transaction handle cannot be
 * used for any new IO.
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
 * Close the transaction handle. This is a local operation, no RPC involved.
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
 * Restart the transaction after encountering a -DER_TX_RESTART error. This
 * drops all the IOs that have been issued via the transaction handle. Whether
 * the restarted transaction observes any conflicting modifications committed
 * after this transaction was originally opened is undefined. If callers would
 * like to retry transactions for their own purposes, they shall open new
 * transactions instead. This is a local operation, no RPC involved.
 *
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

/**
 * Initialize an iteratror anchor.
 *
 * \param[in]	anchor	Anchor to be initialized
 * \param[in]	opts	(reserved) Initialization options
 */
static inline int
daos_anchor_init(daos_anchor_t *anchor, __attribute__((unused)) unsigned int opts)
{
	daos_anchor_t _anchor = DAOS_ANCHOR_INIT;

	*anchor = _anchor;
	return 0;
}

/**
 * Finalizie an iteratror anchor, free resources allocated
 * during the iteration.
 *
 * \param[in]	anchor	Anchor to be finialized
 */
static inline void
daos_anchor_fini(__attribute__((unused)) daos_anchor_t *anchor)
{
	/* NOOP for now, might need to free memory */
}

/**
 * End of the iteration
 */
static inline bool
daos_anchor_is_eof(daos_anchor_t *anchor)
{
	return anchor->da_type == DAOS_ANCHOR_TYPE_EOF;
}

#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_API_H__ */
