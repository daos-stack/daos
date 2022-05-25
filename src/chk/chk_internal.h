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

#include <abt.h>
#include <uuid/uuid.h>
#include <daos/rpc.h>
#include <daos/btree.h>
#include <daos/object.h>
#include <daos_srv/pool.h>
#include <daos_srv/daos_chk.h>
#include <daos_srv/daos_engine.h>

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

/* dkey for check DB under sys_db */
#define CHK_DB_TABLE		"chk"

/* akey for leader bookmark under CHK_DB_TABLE */
#define CHK_BK_LEADER		"leader"

/* akey for engine bookmark under CHK_DB_TABLE */
#define CHK_BK_ENGINE		"engine"

/* akey for check property under CHK_DB_TABLE */
#define CHK_PROPERTY		"property"

/* akey for the list of ranks under CHK_DB_TABLE */
#define CHK_RANKS		"ranks"

#define CHK_BK_MAGIC_LEADER	0xe6f703da
#define CHK_BK_MAGIC_ENGINE	0xe6f703db
#define CHK_BK_MAGIC_POOL	0xe6f703dc

/*
 * XXX: Please be careful when change CHK__CHECK_INCONSIST_CLASS__CIC_UNKNOWN
 *	to avoid hole is the struct chk_property.
 */
#define CHK_POLICY_MAX		(CHK__CHECK_INCONSIST_CLASS__CIC_UNKNOWN + 1)
#define CHK_POOLS_MAX		(1 << 6)

/*
 * Each check instance has a unique leader engine that uses key "chk/leader" under its local
 * sys_db to trace the check instance.
 *
 * For each engine, include the leader engine, there is a system level key "chk/engine" under
 * the engine's local sys_db to trace the check instance on the engine. When server (re)start
 * the check module uses it to determain whether needs to rejoin the check instance.
 *
 * For each pool, there is a key "chk/$pool_uuid" under the engine's local sys_db to trace
 * check process for the pool on related engine.
 */
struct chk_bookmark {
	uint32_t			cb_magic;
	uint32_t			cb_version;
	uint64_t			cb_gen;
	Chk__CheckScanPhase		cb_phase;
	union {
		Chk__CheckInstStatus	cb_ins_status;
		Chk__CheckPoolStatus	cb_pool_status;
	};
	/*
	 * For leader bookmark, it is the inconsistency statistics during the phases range
	 * [CSP_PREPARE, CSP_POOL_LIST] for the whole system. The inconsistency and related
	 * reparation during these phases may be in MS, not related with any engine.
	 *
	 * For pool bookmark, it is the inconsistency statistics during the phases range
	 * [CSP_POOL_MBS, CSP_CONT_CLEANUP] for the pool. The inconsistency and related
	 * reparation during these phases is applied to the pool service leader.
	 */
	struct chk_statistics		cb_statistics;
	struct chk_time			cb_time;
};

/* On each engine (including the leader), there is a key "chk/property" under its local sys_db. */
struct chk_property {
	d_rank_t			cp_leader;
	Chk__CheckFlag			cp_flags;
	Chk__CheckInconsistAction	cp_policies[CHK_POLICY_MAX];
	/*
	 * How many pools will be handled by the check instance. -1 means to handle all pools.
	 * If the specified pools count exceeds CHK_POOLS_MAX, then all pools will be handled.
	 */
	int32_t				cp_pool_nr;
	uuid_t				cp_pools[CHK_POOLS_MAX];
	/*
	 * XXX: Preserve for supporting to continue the check until the specified phase in the
	 *	future. -1 means to check all phases.
	 */
	int32_t				cp_phase;
	/* How many ranks (ever or should) take part in the check instance. */
	uint32_t			cp_rank_nr;
};

/* chk_vos.c */

int chk_bk_fetch_leader(struct chk_bookmark *cbk);

int chk_bk_update_leader(struct chk_bookmark *cbk);

int chk_bk_delete_leader(void);

int chk_bk_fetch_engine(struct chk_bookmark *cbk);

int chk_bk_update_engine(struct chk_bookmark *cbk);

int chk_bk_delete_engine(void);

int chk_bk_fetch_pool(struct chk_bookmark *cbk, uuid_t uuid);

int chk_bk_update_pool(struct chk_bookmark *cbk, uuid_t uuid);

int chk_bk_delete_pool(uuid_t uuid);

int chk_prop_fetch(struct chk_property *cpp, d_rank_list_t **rank_list);

int chk_prop_update(struct chk_property *cpp, d_rank_list_t *rank_list);

int chk_traverse_pools(sys_db_trav_cb_t cb, void *args);

void chk_vos_init(void);

void chk_vos_fini(void);

#endif /* __CHK_INTERNAL_H__ */
