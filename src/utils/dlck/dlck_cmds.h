/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_CMDS__
#define __DLCK_CMDS__

enum dlck_cmd {
	DLCK_CMD_NOT_SET = -2,
	DLCK_CMD_UNKNOWN = -1,
	DLCK_CMD_DTX_ACT_RECOVER,
};

#define DLCK_CMD_DTX_ACT_RECOVER_STR "dtx_act_recs_recover"

struct dlck_control;

typedef int (*dlck_cmd_func)(struct dlck_control *ctrl);

/**
 * \brief Recover DTX records by scanning the VOS tree.
 *
 * 1. List all the records for active DTX entries.
 * 2. Remove records from all active DTX entries. (write mode only)
 * 3. Populate active DTX entries' records. (write mode only)
 *
 * \param[in]	ctrl	Control state.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_dtx_act_recs_recover(struct dlck_control *ctrl);

#define DLCK_CMDS_FUNCS                                                                            \
	{                                                                                          \
		dlck_dtx_act_recs_recover                                                          \
	}

#endif /** __DLCK_CMDS__ */
