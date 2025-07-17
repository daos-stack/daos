/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_DLCK_H__
#define __DAOS_DLCK_H__

#include "d_vector.h"

/**
 * Trace a single DTX record.
 */
struct dlck_dtx_rec {
	uint32_t   lid;
	umem_off_t umoff;
};

/**
 * Execution statistics.
 */
struct dlck_stats {
	unsigned touched;
};

/**
 * \brief Collect the records for active DTX entries.
 *
 * Scan the entire provided \p coh container for records referencing active DTX entries.
 *
 * \param[in]	coh	The container to process.
 * \param[out]	dv	Vector of active DAE records.
 *
 * \retval 0		Success.
 * \retval -DER_*	Error.
 */
int
dlck_vos_cont_rec_get_active(daos_handle_t coh, d_vector_t *dv, struct dlck_stats *ds);

/**
 * \brief Remove records from all active DTX entries.
 *
 * This process is intended for catastrophic recovery in case the records of active DTX entries have
 * been corrupted.
 *
 * \param[in]	coh	Parent container.
 *
 * \retval 0		Success.
 * \retval -DER_*	The transaction has failed.
 */
int
dlck_dtx_act_recs_remove(daos_handle_t coh);

/**
 * \brief Set active DTX entries' records as provided by \p dda.
 *
 * This process assumes the existing DAE records have been first removed. Please find a relevant API
 * call to do it for you. This process is intended for catastrophic recovery in case the records of
 * DAEs have been corrupted.
 *
 * \param[in]	coh	Parent container.
 * \param[in]	dv	Vector of active DAE records.
 *
 * \retval 0			Success.
 * \retval -DER_NOTSUPPORTED	DAE records are not removed.
 * \retval -DER_NOMEM		Run out of memory.
 * \retval -DER_*		The transaction has failed.
 */
int
dlck_dtx_act_recs_set(daos_handle_t coh, d_vector_t *dv);

#endif /* __DAOS_DLCK_H__ */
