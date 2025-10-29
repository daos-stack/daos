/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_CHECKER__
#define __DLCK_CHECKER__

#include <stdio.h>

#include <daos_srv/checker.h>

#define DLCK_PRINT_INDENT         '-'
#define DLCK_CHECKER_MAIN_MAGIC   0x17A28DC5626110A5
#define DLCK_CHECKER_WORKER_MAGIC 0xEB4F7DD311060A6D

/**
 * \struct dlck_checker_worker
 *
 * Custom payload of the worker checker.
 */
struct dlck_checker_worker {
	uint64_t magic;
	FILE    *stream;
	char     prefix[CHECKER_INDENT_MAX + 2]; /** ' ' and '\0' hence 2 characters */
};

/**
 * \struct dlck_checker_main
 *
 * Custom payload of the main checker.
 */
struct dlck_checker_main {
	struct dlck_checker_worker core;
	ABT_mutex                  stream_mutex;
};

#define DLCK_PRINT_MAIN_LOCK_FAIL_FMT                                                              \
	"Failed to lock the stream's synchronization mutex: " DF_RC "\n"
#define DLCK_PRINT_MAIN_UNLOCK_FAIL_FMT                                                            \
	"Failed to unlock the stream's synchronization mutex: " DF_RC "\n"

/**
 * \brief Init the main checker.
 *
 * Prints to stdout and it is guarded by a mutex.
 *
 * \param[out]	dp	Initialized checker.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_NOMEM	Out of memory.
 */
int
dlck_checker_main_init(struct checker *ck);

/**
 * Finalize the main print utility.
 *
 * \param[in]	ck	Checker to finalize.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	An error.
 */
int
dlck_checker_main_fini(struct checker *ck);

/**
 * Get the custom payload from the main print utility.
 *
 * \note Only for advance use-cases. Please see DLCK_PRINT*() macros first.
 *
 * \param[in]   dp      Print utility (only the main one will work).
 *
 * \return The custom payload.
 */
static inline struct dlck_checker_main *
dlck_checker_main_get_custom(struct checker *ck)
{
	struct dlck_checker_main *dcm = ck->ck_private;
	D_ASSERT(dcm->core.magic == DLCK_CHECKER_MAIN_MAGIC);
	return dcm;
}

/**
 * \brief Init a worker's checker.
 *
 * Creates and opens a logfile. The created checker will direct log into the created file.
 *
 * \param[in]	options	Control options.
 * \param[in]	log_dir	Directory where a logfile will be created.
 * \param[in]	po_uuid	Pool's UUID.
 * \param[in]	tgt_id	Target's ID.
 * \param[in]	main_dp	Main checker. To report errors when they occur.
 * \param[out]	ck	Created checker.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_NOMEM	Out of memory.
 * \retval -DER_*	Other error.
 */
int
dlck_checker_worker_init(struct checker_options *options, const char *log_dir, uuid_t po_uuid,
			 int tgt_id, struct checker *main_ck, struct checker *ck);

/**
 * \brief Finalize the worker's checker.
 *
 * \note The worker output stream will be closed.
 *
 * \param[in] ck	Checker to finalize.
 */
void
dlck_checker_worker_fini(struct checker *ck);

#endif /** __DLCK_CHECKER__ */
