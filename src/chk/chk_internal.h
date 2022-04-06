/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * DAOS global consistency checker RPC Protocol Definitions
 */

#ifndef __CHK_INTERNAL_H__
#define __CHK_INTERNAL_H__

#include <daos/rpc.h>
#include <daos_srv/daos_chk.h>

#include "chk.pb-c.h"

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See daos/rpc.h.
 */
#define DAOS_CHK_VERSION 1

#define CHK_PROTO_SRV_RPC_LIST									\
	X(CHK_START,	0,	&CQF_chk_start,	ds_chk_start_hdlr,	NULL,	"chk_start")	\
	X(CHK_STOP,	0,	&CQF_chk_stop,	ds_chk_stop_hdlr,	NULL,	"chk_stop")	\
	X(CHK_QUERY,	0,	&CQF_chk_query,	ds_chk_query_hdlr,	NULL,	"chk_query")	\
	X(CHK_ACT,	0,	&CQF_chk_act,	ds_chk_act_hdlr,	NULL,	"chk_act")

/* Define for RPC enum population below */
#define X(a, b, c, d, e, f) a,

enum chk_rpc_opc {
	CHK_PROTO_SRV_RPC_LIST
	CHK_PROTO_SRV_RPC_COUNT,
};

#undef X

int chk_rejoin(void);

int chk_pause(void);

#endif /* __CHK_INTERNAL_H__ */
