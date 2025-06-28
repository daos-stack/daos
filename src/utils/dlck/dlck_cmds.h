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

struct dlck_args;

typedef int (*dlck_cmd_func)(struct dlck_args *args);

int
dlck_dtx_act_recs_recover(struct dlck_args *args);

#define DLCK_CMDS_FUNCS {dlck_dtx_act_recs_recover}

#endif /** __DLCK_CMDS__ */