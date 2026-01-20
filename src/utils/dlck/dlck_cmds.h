/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_CMDS__
#define __DLCK_CMDS__

#include "dlck_args.h"

/**
 * \brief Validate the integrity of the pool(s) metadata.
 *
 * The \p ctrl argument specifies which pool(s) to check and how the output will be printed.
 *
 * \param[in] ctrl	Control bundle.
 *
 * \retval DER_SUCCESS		All checked pools are ok.
 * \retval -DER_DF_INVAL	Durable format error.
 * \retval -DER_DF_INCOMPT	Incompatible durable format.
 * \retval -DER_ID_MISMATCH	Pool UUID mismatch.
 * \retval -DER_NOTYPE		Unexpected contents.
 * \retval -DER_*		Other errors.
 */
int
dlck_cmd_check(struct dlck_control *ctrl);

#endif /** __DLCK_CMDS__ */
