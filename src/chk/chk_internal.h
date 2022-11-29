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
#include <daos_srv/iv.h>
#include <daos_srv/rsvc.h>
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
#define DAOS_CHK_VERSION	1

#define CHK_PROTO_SRV_RPC_LIST									\
	X(CHK_START,										\
		0,	&CQF_chk_start,		ds_chk_start_hdlr,	&chk_start_co_ops),	\
	X(CHK_STOP,										\
		0,	&CQF_chk_stop,		ds_chk_stop_hdlr,	&chk_stop_co_ops),	\
	X(CHK_QUERY,										\
		0,	&CQF_chk_query,		ds_chk_query_hdlr,	&chk_query_co_ops),	\
	X(CHK_MARK,										\
		0,	&CQF_chk_mark,		ds_chk_mark_hdlr,	&chk_mark_co_ops),	\
	X(CHK_ACT,										\
		0,	&CQF_chk_act,		ds_chk_act_hdlr,	&chk_act_co_ops),	\
	X(CHK_CONT_LIST,									\
		0,	&CQF_chk_cont_list,	ds_chk_cont_list_hdlr,	&chk_cont_list_co_ops),	\
	X(CHK_POOL_START,									\
		0,	&CQF_chk_pool_start,	ds_chk_pool_start_hdlr,	&chk_pool_start_co_ops),\
	X(CHK_POOL_MBS,										\
		0,	&CQF_chk_pool_mbs,	ds_chk_pool_mbs_hdlr,	NULL),			\
	X(CHK_REPORT,										\
		0,	&CQF_chk_report,	ds_chk_report_hdlr,	NULL),			\
	X(CHK_REJOIN,										\
		0,	&CQF_chk_rejoin,	ds_chk_rejoin_hdlr,	NULL)

/* Define for RPC enum population below */
#define X(a, b, c, d, e) a

enum chk_rpc_opc {
	CHK_PROTO_SRV_RPC_LIST,
	CHK_PROTO_SRV_RPC_COUNT,
};

#undef X

struct chk_pool_mbs {
	d_rank_t	 cpm_rank;
	uint32_t	 cpm_tgt_nr;
	uint32_t	*cpm_tgt_status;
};

/*
 * CHK_START:
 * From check leader to check engine to start the check instance on specified pool(s) or all pools.
 */
#define DAOS_ISEQ_CHK_START							\
	((uint64_t)		(csi_gen)		CRT_VAR)		\
	((uint32_t)		(csi_flags)		CRT_VAR)		\
	((int32_t)		(csi_phase)		CRT_VAR)		\
	((d_rank_t)		(csi_leader_rank)	CRT_VAR)		\
	((uint32_t)		(csi_api_flags)		CRT_VAR)		\
	((d_rank_t)		(csi_ranks)		CRT_ARRAY)		\
	((struct chk_policy)	(csi_policies)		CRT_ARRAY)		\
	((uuid_t)		(csi_uuids)		CRT_ARRAY)

#define DAOS_OSEQ_CHK_START							\
	((int32_t)		(cso_status)		CRT_VAR)		\
	((d_rank_t)		(cso_rank)		CRT_VAR)		\
	((int32_t)		(cso_child_status)	CRT_VAR)		\
	((uint32_t)		(cso_padding)		CRT_VAR)		\
	((struct ds_pool_clue)	(cso_clues)		CRT_ARRAY)

CRT_RPC_DECLARE(chk_start, DAOS_ISEQ_CHK_START, DAOS_OSEQ_CHK_START);

/*
 * CHK_STOP:
 * From check leader to check engine to stop the check instance on specified pools(s) or all pools.
 */
#define DAOS_ISEQ_CHK_STOP							\
	((uint64_t)		(csi_gen)		CRT_VAR)		\
	((uuid_t)		(csi_uuids)		CRT_ARRAY)

#define DAOS_OSEQ_CHK_STOP							\
	((int32_t)		(cso_status)		CRT_VAR)		\
	((int32_t)		(cso_child_status)	CRT_VAR)		\
	((d_rank_t)		(cso_rank)		CRT_VAR)

CRT_RPC_DECLARE(chk_stop, DAOS_ISEQ_CHK_STOP, DAOS_OSEQ_CHK_STOP);

/*
 * CHK_QUERY:
 * From check leader to check engine to query the check process for specified pools(s) or all pools.
 */
#define DAOS_ISEQ_CHK_QUERY							\
	((uint64_t)		(cqi_gen)		CRT_VAR)		\
	((uuid_t)		(cqi_uuids)		CRT_ARRAY)

#define DAOS_OSEQ_CHK_QUERY							\
	((int32_t)			(cqo_status)		CRT_VAR)	\
	((int32_t)			(cqo_child_status)	CRT_VAR)	\
	((struct chk_query_pool_shard)	(cqo_shards)		CRT_ARRAY)

CRT_RPC_DECLARE(chk_query, DAOS_ISEQ_CHK_QUERY, DAOS_OSEQ_CHK_QUERY);

/*
 * CHK_MARK:
 * From check leader to check engine to mark some rank as "dead". Under check mode, if some rank
 * is dead (and failed to rejoin), it will not be exlcuded from related pool map to avoid further
 * damaging the system, instead, it will be mark as "dead" by the check instance and the check
 * status on related pool(s) will be marked as "failed".
 */
#define DAOS_ISEQ_CHK_MARK							\
	((uint64_t)		(cmi_gen)		CRT_VAR)		\
	((d_rank_t)		(cmi_rank)		CRT_VAR)		\
	((uint32_t)		(cmi_version)		CRT_VAR)

#define DAOS_OSEQ_CHK_MARK							\
	((int32_t)		(cmo_status)		CRT_VAR)		\
	((uint32_t)		(cmo_padding)		CRT_VAR)

CRT_RPC_DECLARE(chk_mark, DAOS_ISEQ_CHK_MARK, DAOS_OSEQ_CHK_MARK);

/*
 * CHK_ACT:
 * From check leader to check engine to execute the admin specified repair action for former
 * reported inconsistency under interaction mode.
 */
#define DAOS_ISEQ_CHK_ACT							\
	((uint64_t)		(cai_gen)		CRT_VAR)		\
	((uint64_t)		(cai_seq)		CRT_VAR)		\
	((uint32_t)		(cai_cla)		CRT_VAR)		\
	((uint32_t)		(cai_act)		CRT_VAR)		\
	((uint32_t)		(cai_flags)		CRT_VAR)		\
	((uint32_t)		(cai_padding)		CRT_VAR)

#define DAOS_OSEQ_CHK_ACT							\
	((int32_t)		(cao_status)		CRT_VAR)		\
	((uint32_t)		(cao_padding)		CRT_VAR)

CRT_RPC_DECLARE(chk_act, DAOS_ISEQ_CHK_ACT, DAOS_OSEQ_CHK_ACT);

/*
 * CHK_CONT_LIST:
 * From PS leader to check engine to get containers list.
 */
#define DAOS_ISEQ_CHK_CONT_LIST							\
	((uint64_t)		(ccli_gen)		CRT_VAR)		\
	((d_rank_t)		(ccli_rank)		CRT_VAR)		\
	((uint32_t)		(ccli_padding)		CRT_VAR)		\
	((uuid_t)		(ccli_pool)		CRT_VAR)

#define DAOS_OSEQ_CHK_CONT_LIST							\
	((int32_t)		(cclo_status)		CRT_VAR)		\
	((int32_t)		(cclo_child_status)	CRT_VAR)		\
	((d_rank_t)		(cclo_rank)		CRT_VAR)		\
	((uint32_t)		(cclo_padding)		CRT_VAR)		\
	((uuid_t)		(cclo_conts)		CRT_ARRAY)

CRT_RPC_DECLARE(chk_cont_list, DAOS_ISEQ_CHK_CONT_LIST, DAOS_OSEQ_CHK_CONT_LIST);

/*
 * CHK_POOL_START:
 * From check leader to check engine to start the pool shard.
 */
#define DAOS_ISEQ_CHK_POOL_START						\
	((uint64_t)		(cpsi_gen)		CRT_VAR)		\
	((uuid_t)		(cpsi_pool)		CRT_VAR)		\
	((uint32_t)		(cpsi_phase)		CRT_VAR)		\
	((uint32_t)		(cpsi_padding)		CRT_VAR)

#define DAOS_OSEQ_CHK_POOL_START						\
	((int32_t)		(cpso_status)		CRT_VAR)		\
	((uint32_t)		(cpso_rank)		CRT_VAR)

CRT_RPC_DECLARE(chk_pool_start, DAOS_ISEQ_CHK_POOL_START, DAOS_OSEQ_CHK_POOL_START);

/*
 * CHK_POOL_MBS:
 * From check leader to check engine to notify the pool members.
 */
#define DAOS_ISEQ_CHK_POOL_MBS							\
	((uint64_t)		(cpmi_gen)		CRT_VAR)		\
	((uuid_t)		(cpmi_pool)		CRT_VAR)		\
	((uint32_t)		(cpmi_flags)		CRT_VAR)		\
	((uint32_t)		(cpmi_phase)		CRT_VAR)		\
	((d_string_t)		(cpmi_label)		CRT_VAR)		\
	((struct chk_pool_mbs)	(cpmi_targets)		CRT_ARRAY)		\

#define DAOS_OSEQ_CHK_POOL_MBS							\
	((int32_t)		(cpmo_status)		CRT_VAR)		\
	((uint32_t)		(cpmo_padding)		CRT_VAR)		\
	((struct rsvc_hint)	(cpmo_hint)		CRT_VAR)

CRT_RPC_DECLARE(chk_pool_mbs, DAOS_ISEQ_CHK_POOL_MBS, DAOS_OSEQ_CHK_POOL_MBS);

/*
 * CHK_REPORT:
 * From check engine to check leader to report the inconsistency and related repair action
 * and result. It can require to interact with the admin to make decision for how to handle
 * the inconsistency.
 */
#define DAOS_ISEQ_CHK_REPORT							\
	((uint64_t)		(cri_gen)		CRT_VAR)		\
	((uint32_t)		(cri_ics_class)		CRT_VAR)		\
	((uint32_t)		(cri_ics_action)	CRT_VAR)		\
	((int32_t)		(cri_ics_result)	CRT_VAR)		\
	((d_rank_t)		(cri_rank)		CRT_VAR)		\
	((uint32_t)		(cri_target)		CRT_VAR)		\
	((uint32_t)		(cri_padding)		CRT_VAR)		\
	((uint64_t)		(cri_seq)		CRT_VAR)		\
	((uuid_t)		(cri_pool)		CRT_VAR)		\
	((d_string_t)		(cri_pool_label)	CRT_VAR)		\
	((uuid_t)		(cri_cont)		CRT_VAR)		\
	((d_string_t)		(cri_cont_label)	CRT_VAR)		\
	((daos_unit_oid_t)	(cri_obj)		CRT_RAW)		\
	((daos_key_t)		(cri_dkey)		CRT_VAR)		\
	((daos_key_t)		(cri_akey)		CRT_VAR)		\
	((d_string_t)		(cri_msg)		CRT_VAR)		\
	((uint32_t)		(cri_options)		CRT_ARRAY)		\
	((d_sg_list_t)		(cri_details)		CRT_ARRAY)

#define DAOS_OSEQ_CHK_REPORT							\
	((int32_t)		(cro_status)		CRT_VAR)		\
	((uint32_t)		(cro_padding)		CRT_VAR)		\
	((uint64_t)		(cro_seq)		CRT_VAR)

CRT_RPC_DECLARE(chk_report, DAOS_ISEQ_CHK_REPORT, DAOS_OSEQ_CHK_REPORT);

/*
 * CHK_REJOIN:
 * From check engine to check leader to require rejoin former check instance after the engine
 * restart under check mode.
 */
#define DAOS_ISEQ_CHK_REJOIN							\
	((uint64_t)		(cri_gen)		CRT_VAR)		\
	((d_rank_t)		(cri_rank)		CRT_VAR)		\
	((uint32_t)		(cri_padding)		CRT_VAR)

#define DAOS_OSEQ_CHK_REJOIN							\
	((int32_t)		(cro_status)		CRT_VAR)		\
	((uint32_t)		(cro_padding)		CRT_VAR)		\
	((uuid_t)		(cro_pools)		CRT_ARRAY)

CRT_RPC_DECLARE(chk_rejoin, DAOS_ISEQ_CHK_REJOIN, DAOS_OSEQ_CHK_REJOIN);

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

#define CHK_DUMMY_POOL		"00000000-0000-0000-0000-000020220531"
#define CHK_INVAL_PHASE		(uint32_t)(-1)
#define CHK_INVAL_STATUS	(uint32_t)(-1)

#define CHK_BTREE_ORDER		16

#define CHK_MSG_BUFLEN		320

/*
 * NOTE: Please be careful when change CHK__CHECK_INCONSIST_CLASS__CIC_UNKNOWN
 *	 to avoid hole is the struct chk_property.
 */
#define CHK_POLICY_MAX		(CHK__CHECK_INCONSIST_CLASS__CIC_UNKNOWN + 1)

typedef int (*chk_co_rpc_cb_t)(void *args, uint32_t rank, int result, void *data, uint32_t nr);

typedef void (*chk_pool_free_data_t)(void *data);

enum chk_start_flags {
	/* Reset all check bookmarks, for leader, engines and all pools. */
	CSF_RESET_ALL		= 1,
	/* Reset the pool which check is not completed. */
	CSF_RESET_NONCOMP	= 2,
	/* Handle orphan pools. */
	CSF_ORPHAN_POOL		= 4,
};

enum chk_act_flags {
	/* The action is applicable to the same kind of inconssitency. */
	CAF_FOR_ALL		= 1,
};

enum chk_mbs_flags {
	CMF_REPAIR_LABEL	= 1,
};

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
	 * reparation during these phases may be on MS leader, not related with any engine.
	 *
	 * For pool bookmark, it is the inconsistency statistics during the phases range
	 * [CSP_POOL_MBS, CSP_CONT_CLEANUP] for the pool. The inconsistency and related
	 * reparation during these phases is applied to the PS leader.
	 */
	struct chk_statistics		cb_statistics;
	struct chk_time			cb_time;
};

/*
 * On each engine (including the leader), there is a key "chk/property" under its local sys_db.
 * That is shared by all the pools for current check instance.
 *
 * DAOS check property is persistent. Unless you specify new property to overwrite the old one
 * when check start, otherwise, it will reuse former property for current check instance.
 *
 *
 * About the leader:
 *
 * The leader bookmark and global pools' traces are only stored on current check leader. So if
 * we switch to new check leader for current check instance, we will lose those former traces.
 * Then we will have to rescan the whole system from scratch when switch to new check leader.
 *
 *
 * About some flags:
 *
 * - CHK__CHECK_FLAG__CF_RESET
 *
 *   If 'reset' flag is specified together with pool list when check start, then it only makes
 *   the check against the specified pools to rescan from the beginning.
 *
 *   If 'reset' flag is specified without pool list when check start, then all pools in system
 *   will be affected with rescanning from scratch.
 *
 *   The 'reset' flag is not stored in the check property persistently. It is per instance, and
 *   only affects current check start. When you restart DAOS check next time without explicitly
 *   specify 'reset' flag, you will reuse former check property and resume the scan from former
 *   pause/stop phase.
 *
 *   The 'reset' flag does not affect check property. If want to change check property, need to
 *   overwrite related property explicitly when check start.
 *
 *   NOTE: If a pool has been 'checked' (as CHK__CHECK_SCAN_PHASE__DSP_DONE) in former instance,
 *	   then current check instance will skip it directly unless explicitly set 'reset' flag
 *	   or reset is triggered for other reason, such as check ranks changes.
 *
 * - CHK__CHECK_FLAG__CF_DRYRUN
 *
 *   To simplify the logic, dryrun mode is per system, not per pool. Means that if dryrun flag is
 *   specified when check start, then all non-completed pools' check will be dryrun mode in spite
 *   of whether a pool is in current instance check list or not.
 *
 *   Under dryrun mode, we do not really repair the found inconsistency, then we will lose former
 *   stable base if we want to resume DAOS check from former pause/stop point. So if former check
 *   instance ran under dryrun mode, then current check start will be handled as 'reset' for all
 *   pools in spite of current instance is dryrun mode or not.
 *
 *   NOTE: Consider above behavior, although the 'dryrun' flag is stored persistently, it is per
 *	   instance, and only affects current check instance.
 *
 * - CHK__CHECK_FLAG__CF_ORPHAN_POOL
 *
 *   Handle orphan pool requires all check engines to report their known pools (shards), then
 *   compare the list with the MS known ones. But for most of time, the check instance may only
 *   drive the check against some specified pool(s). So we offer two ways to trigger the handle
 *   of orphan pools:
 *
 *   1. Anytime when the check is (re)start from the scratch for all pools, in spite of whether
 *      it is for 'reset' flag without pool list or other reason, such as check ranks changes.
 *
 *   2. Explicitly specify 'orphan' flag when check start, in spite of it is for all pools or
 *      just against the specified pool list.
 *
 *   NOTE: Similar as 'reset' flag, the 'orphan' flag is also not stored persistently, instead,
 *	   it only affects current check instance.
 *
 *
 * About the policies:
 *
 * The repair policies are shared among all pools. For some specified inconsistency, its repair
 * policy may be changed during the check scan via CHECK_ACT dRPC downcall with 'for_all' flag.
 *
 * When check start, if do not specify policies, the former policies will be reused. Currently,
 * we do not support to set policy just for special inconsistency class, means that either all
 * are specified (to overwrite) or none. That can be improved in the future.
 *
 *
 * About the ranks:
 *
 * The changes for the ranks that take part in the check means the potential pools' membership
 * changes. It will affect former non-completed pools' check. Currently, to simplify the logic,
 * if current check ranks do not match former ones, then current check start will be handled as
 * 'reset' for all pools.
 */
struct chk_property {
	d_rank_t			cp_leader;
	Chk__CheckFlag			cp_flags;
	Chk__CheckInconsistAction	cp_policies[CHK_POLICY_MAX];
	/*
	 * NOTE: Preserve for supporting to continue the check until the specified phase in the
	 *	 future. -1 means to check all phases.
	 */
	int32_t				cp_phase;
	/* How many ranks (ever or should) take part in the check instance. */
	uint32_t			cp_rank_nr;
};

/*
 * For each check instance, there are one leader instance and 1 ~ N engine instances.
 * For each rank, there can be at most one leader instance and one engine instance.
 *
 * Currently, we do not support to run multiple check instances in the system (even
 * if they are on different ranks sets) at the same time. If multiple pools need to
 * be checked, then please either specify their uuids together (or not specify pool
 * option, then check all pools by default) via single "dmg check" command, or wait
 * one check instance done and then start next.
 */
struct chk_instance {
	struct chk_bookmark	 ci_bk;
	struct chk_property	 ci_prop;

	struct btr_root		 ci_rank_btr;
	daos_handle_t		 ci_rank_hdl;
	d_list_t		 ci_rank_list;

	struct btr_root		 ci_pool_btr;
	daos_handle_t		 ci_pool_hdl;
	d_list_t		 ci_pool_list;

	struct btr_root		 ci_pending_btr;
	daos_handle_t		 ci_pending_hdl;
	d_list_t		 ci_pending_list;

	/* The slowest phase for the failed pool or rank. */
	uint32_t		 ci_slowest_fail_phase;

	uint32_t		 ci_iv_id;
	struct ds_iv_ns		*ci_iv_ns;
	crt_group_t		*ci_iv_group;

	d_rank_list_t		*ci_ranks;

	ABT_thread		 ci_sched;
	ABT_rwlock		 ci_abt_lock;
	ABT_mutex		 ci_abt_mutex;
	ABT_cond		 ci_abt_cond;

	/* Generator for report event, pending repair actions, and so on. Only for leader. */
	uint64_t		 ci_seq;

	uint32_t		 ci_is_leader:1,
				 ci_sched_running:1,
				 ci_starting:1,
				 ci_stopping:1,
				 ci_started:1,
				 ci_implicated:1;
	uint32_t		 ci_start_flags;
};

struct chk_iv {
	uint64_t		 ci_gen;
	uint64_t		 ci_seq;
	uuid_t			 ci_uuid;
	d_rank_t		 ci_rank;
	uint32_t		 ci_phase;
	uint32_t		 ci_ins_status;
	uint32_t		 ci_pool_status;
	uint32_t		 ci_to_leader:1, /* To check leader. */
				 ci_from_psl:1; /* From pool service leader. */
};

/* Check engine uses it to trace pools. Query logic uses it to organize the result. */
struct chk_pool_shard {
	/* Link into chk_pool_rec::cpr_shard_list. */
	d_list_t		 cps_link;
	d_rank_t		 cps_rank;
	void			*cps_data;
	chk_pool_free_data_t	 cps_free_cb;
};

/* Check engine uses it to trace pools. Query logic uses it to organize the result. */
struct chk_pool_rec {
	/* Link into chk_instance::ci_pool_list. */
	d_list_t		 cpr_link;
	/* The list of chk_pool_shard. */
	d_list_t		 cpr_shard_list;
	uint32_t		 cpr_shard_nr;
	uint32_t		 cpr_started:1,
				 cpr_start_post:1,
				 cpr_stop:1,
				 cpr_done:1,
				 cpr_skip:1,
				 cpr_for_orphan:1,
				 cpr_notified_exit:1,
				 cpr_destroyed:1,
				 cpr_healthy:1,
				 cpr_delay_label:1,
				 cpr_exist_on_ms:1,
				 cpr_map_refreshed:1;
	int			 cpr_advice;
	uint32_t		 cpr_phase;
	uuid_t			 cpr_uuid;
	ABT_thread		 cpr_thread;
	struct ds_pool_clues	 cpr_clues;
	struct ds_pool_clue	*cpr_clue;
	struct chk_bookmark	 cpr_bk;
	struct chk_instance	*cpr_ins;
	struct chk_pool_mbs	*cpr_mbs;
	char			*cpr_label;
	ABT_mutex		 cpr_mutex;
	ABT_cond		 cpr_cond;
	int			 cpr_refs;
};

struct chk_pending_rec {
	/* Link into chk_instance::ci_pending_list. */
	d_list_t		 cpr_ins_link;
	/* Link into chk_rank_rec::crr_pending_list. */
	d_list_t		 cpr_rank_link;
	uint64_t		 cpr_seq;
	d_rank_t		 cpr_rank;
	uint32_t		 cpr_class;
	uint32_t		 cpr_action;
	uint32_t		 cpr_busy:1,
				 cpr_exiting:1,
				 cpr_on_leader:1;
	ABT_mutex		 cpr_mutex;
	ABT_cond		 cpr_cond;
};

struct chk_report_unit {
	uint64_t		 cru_gen;
	uint32_t		 cru_cla;
	uint32_t		 cru_act;
	uint32_t		 cru_target;
	d_rank_t		 cru_rank;
	uint32_t		 cru_option_nr;
	uint32_t		 cru_detail_nr;
	uuid_t			*cru_pool;
	char			*cru_pool_label;
	uuid_t			*cru_cont;
	char			*cru_cont_label;
	daos_unit_oid_t		*cru_obj;
	daos_key_t		*cru_dkey;
	daos_key_t		*cru_akey;
	char			*cru_msg;
	uint32_t		*cru_options;
	d_sg_list_t		*cru_details;
	uint32_t		 cru_sugg;
	uint32_t		 cru_result;
};

struct chk_traverse_pools_args {
	uint64_t		 ctpa_gen;
	struct chk_instance	*ctpa_ins;
	uint32_t		 ctpa_status;
};

extern struct crt_proto_format	chk_proto_fmt;

extern struct crt_corpc_ops	chk_start_co_ops;
extern struct crt_corpc_ops	chk_stop_co_ops;
extern struct crt_corpc_ops	chk_query_co_ops;
extern struct crt_corpc_ops	chk_mark_co_ops;
extern struct crt_corpc_ops	chk_act_co_ops;
extern struct crt_corpc_ops	chk_cont_list_co_ops;
extern struct crt_corpc_ops	chk_pool_start_co_ops;

extern btr_ops_t		chk_pool_ops;
extern btr_ops_t		chk_pending_ops;
extern btr_ops_t		chk_rank_ops;
extern btr_ops_t		chk_cont_ops;

/* chk_common.c */

void chk_ranks_dump(uint32_t rank_nr, d_rank_t *ranks);

void chk_pools_dump(d_list_t *head, int pool_nr, uuid_t pools[]);

void  chk_pool_remove_nowait(struct chk_pool_rec *cpr, bool destroy);

void chk_pool_start_svc(struct chk_pool_rec *cpr, int *ret);

void chk_pool_stop_one(struct chk_instance *ins, uuid_t uuid, int status, uint32_t phase, int *ret);

int chk_pools_cleanup_cb(struct sys_db *db, char *table, d_iov_t *key, void *args);

int chk_pool_start_one(struct chk_instance *ins, uuid_t uuid, uint64_t gen);

int chk_pools_load_list(struct chk_instance *ins, uint64_t gen, uint32_t flags,
			int pool_nr, uuid_t pools[]);

int chk_pools_load_from_db(struct sys_db *db, char *table, d_iov_t *key, void *args);

int chk_pools_update_bk(struct chk_instance *ins, uint32_t phase);

int chk_pool_handle_notify(struct chk_instance *ins, struct chk_iv *iv);

int chk_pool_add_shard(daos_handle_t hdl, d_list_t *head, uuid_t uuid, d_rank_t rank,
		       struct chk_bookmark *bk, struct chk_instance *ins,
		       uint32_t *shard_nr, void *data, chk_pool_free_data_t free_cb);

int chk_pending_add(struct chk_instance *ins, d_list_t *rank_head, uint64_t seq,
		    uint32_t rank, uint32_t cla, struct chk_pending_rec **cpr);

int chk_pending_del(struct chk_instance *ins, uint64_t seq, struct chk_pending_rec **cpr);

void chk_pending_destroy(struct chk_pending_rec *cpr);

int chk_prop_prepare(d_rank_t leader, uint32_t flags, int phase,
		     uint32_t policy_nr, struct chk_policy *policies,
		     d_rank_list_t *ranks, struct chk_property *prop);

int chk_ins_init(struct chk_instance **p_ins);

void chk_ins_fini(struct chk_instance **p_ins);

/* chk_engine.c */

int chk_engine_start(uint64_t gen, uint32_t rank_nr, d_rank_t *ranks,
		     uint32_t policy_nr, struct chk_policy *policies, int pool_nr,
		     uuid_t pools[], uint32_t api_flags, int phase, d_rank_t leader,
		     uint32_t flags, struct ds_pool_clues *clues);

int chk_engine_stop(uint64_t gen, int pool_nr, uuid_t pools[]);

int chk_engine_query(uint64_t gen, int pool_nr, uuid_t pools[],
		     uint32_t *shard_nr, struct chk_query_pool_shard **shards);

int chk_engine_mark_rank_dead(uint64_t gen, d_rank_t rank, uint32_t version);

int chk_engine_act(uint64_t gen, uint64_t seq, uint32_t cla, uint32_t act, uint32_t flags);

int chk_engine_cont_list(uint64_t gen, uuid_t pool_uuid, uuid_t **conts, uint32_t *count);

int chk_engine_pool_start(uint64_t gen, uuid_t uuid, uint32_t phase);

int chk_engine_pool_mbs(uint64_t gen, uuid_t uuid, uint32_t phase, const char *label,
			uint32_t flags, uint32_t mbs_nr, struct chk_pool_mbs *mbs_array,
			struct rsvc_hint *hint);

int chk_engine_notify(struct chk_iv *iv);

void chk_engine_rejoin(void *args);

void chk_engine_pause(void);

int chk_engine_init(void);

void chk_engine_fini(void);

/* chk_iv.c */

int chk_iv_update(void *ns, struct chk_iv *iv, uint32_t shortcut, uint32_t sync_mode, bool retry);

int chk_iv_init(void);

int chk_iv_fini(void);

/* chk_leader.c */

bool chk_is_on_leader(uint64_t gen, d_rank_t leader, bool known_leader);

struct ds_iv_ns *chk_leader_get_iv_ns(void);

int chk_leader_report(struct chk_report_unit *cru, uint64_t *seq, int *decision);

int chk_leader_notify(struct chk_iv *iv);

int chk_leader_rejoin(uint64_t gen, d_rank_t rank, int *pool_nr, uuid_t **pools);

void chk_leader_pause(void);

int chk_leader_init(void);

void chk_leader_fini(void);

/* chk_rpc.c */

int chk_start_remote(d_rank_list_t *rank_list, uint64_t gen, uint32_t rank_nr, d_rank_t *ranks,
		     uint32_t policy_nr, struct chk_policy *policies, int pool_nr,
		     uuid_t pools[], uint32_t api_flags, int phase, d_rank_t leader, uint32_t flags,
		     chk_co_rpc_cb_t start_cb, void *args);

int chk_stop_remote(d_rank_list_t *rank_list, uint64_t gen, int pool_nr, uuid_t pools[],
		    chk_co_rpc_cb_t stop_cb, void *args);

int chk_query_remote(d_rank_list_t *rank_list, uint64_t gen, int pool_nr, uuid_t pools[],
		     chk_co_rpc_cb_t query_cb, void *args);

int chk_mark_remote(d_rank_list_t *rank_list, uint64_t gen, d_rank_t rank, uint32_t version);

int chk_act_remote(d_rank_list_t *rank_list, uint64_t gen, uint64_t seq, uint32_t cla,
		   uint32_t act, d_rank_t rank, bool for_all);

int chk_cont_list_remote(struct ds_pool *pool, uint64_t gen, chk_co_rpc_cb_t list_cb, void *args);

int chk_pool_start_remote(d_rank_list_t *rank_list, uint64_t gen, uuid_t uuid, uint32_t phase);

int chk_pool_mbs_remote(d_rank_t rank, uint32_t phase, uint64_t gen, uuid_t uuid, char *label,
			uint32_t flags, uint32_t mbs_nr, struct chk_pool_mbs *mbs_array,
			struct rsvc_hint *hint);

int chk_report_remote(d_rank_t leader, uint64_t gen, uint32_t cla, uint32_t act, int result,
		      d_rank_t rank, uint32_t target, uuid_t *pool, char *pool_label,
		      uuid_t *cont, char *cont_label, daos_unit_oid_t *obj, daos_key_t *dkey,
		      daos_key_t *akey, char *msg, uint32_t option_nr, uint32_t *options,
		      uint32_t detail_nr, d_sg_list_t *details, uint64_t *seq);

int chk_rejoin_remote(d_rank_t leader, uint64_t gen, d_rank_t rank,
		      uint32_t *pool_nr, uuid_t **pools);

/* chk_updcall.c */

int chk_report_upcall(uint64_t gen, uint64_t seq, uint32_t cla, uint32_t act, int result,
		      d_rank_t rank, uint32_t target, uuid_t *pool, char *pool_label,
		      uuid_t *cont, char *cont_label, daos_unit_oid_t *obj, daos_key_t *dkey,
		      daos_key_t *akey, char *msg, uint32_t option_nr, uint32_t *options,
		      uint32_t detail_nr, d_sg_list_t *details);

/* chk_vos.c */

int chk_bk_fetch_leader(struct chk_bookmark *cbk);

int chk_bk_update_leader(struct chk_bookmark *cbk);

int chk_bk_delete_leader(void);

int chk_bk_fetch_engine(struct chk_bookmark *cbk);

int chk_bk_update_engine(struct chk_bookmark *cbk);

int chk_bk_delete_engine(void);

int chk_bk_fetch_pool(struct chk_bookmark *cbk, char *uuid_str);

int chk_bk_update_pool(struct chk_bookmark *cbk, char *uuid_str);

int chk_bk_delete_pool(char *uuid_str);

int chk_prop_fetch(struct chk_property *cpp, d_rank_list_t **rank_list);

int chk_prop_update(struct chk_property *cpp, d_rank_list_t *rank_list);

int chk_traverse_pools(sys_db_trav_cb_t cb, void *args);

void chk_vos_init(void);

void chk_vos_fini(void);

static inline void
chk_ins_set_fail(struct chk_instance *ins, uint32_t phase)
{
	if (ins->ci_slowest_fail_phase == CHK_INVAL_PHASE || ins->ci_slowest_fail_phase > phase)
		ins->ci_slowest_fail_phase = phase;
}

static inline bool
chk_rank_in_list(d_rank_list_t *rlist, d_rank_t rank)
{
	int	i;
	bool	found = false;

	/* TBD: more efficiently search for the sorted ranks list. */

	for (i = 0; i < rlist->rl_nr; i++) {
		if (rlist->rl_ranks[i] == rank) {
			found = true;
			break;
		}
	}

	return found;
}

static inline bool
chk_remove_rank_from_list(d_rank_list_t *rlist, d_rank_t rank)
{
	int	i;
	bool	found = false;

	/* TBD: more efficiently search for the sorted ranks list. */

	for (i = 0; i < rlist->rl_nr; i++) {
		if (rlist->rl_ranks[i] == rank) {
			found = true;
			rlist->rl_nr--;
			/* The leader rank will always be in the rank list. */
			D_ASSERT(rlist->rl_nr > 0);

			if (i < rlist->rl_nr)
				memmove(&rlist->rl_ranks[i], &rlist->rl_ranks[i + 1],
					rlist->rl_nr - i);
			break;
		}
	}

	return found;
}

static inline void
chk_destroy_tree(daos_handle_t *toh, struct btr_root *root)
{
	int	rc;

	if (daos_handle_is_valid(*toh)) {
		rc = dbtree_destroy(*toh, NULL);
		if (rc != 0)
			D_ERROR("Failed to destroy the tree: "DF_RC"\n", DP_RC(rc));

		/*
		 * Reset the tree even if failed to destroy, that may cause DRAM leak,
		 * but it will not prevent next check instance running.
		 */
		*toh = DAOS_HDL_INVAL;
		memset(root, 0, sizeof(*root));
	}
}

static inline void
chk_destroy_pending_tree(struct chk_instance *ins)
{
	ABT_rwlock_wrlock(ins->ci_abt_lock);
	chk_destroy_tree(&ins->ci_pending_hdl, &ins->ci_pending_btr);
	ABT_rwlock_unlock(ins->ci_abt_lock);
}

static inline void
chk_destroy_pool_tree(struct chk_instance *ins)
{
	chk_destroy_tree(&ins->ci_pool_hdl, &ins->ci_pool_btr);
}

static inline void
chk_query_free(struct chk_query_pool_shard *shards, uint32_t shard_nr)
{
	int	i;

	for (i = 0; i < shard_nr; i++)
		D_FREE(shards[i].cqps_targets);

	D_FREE(shards);
}

static inline void
chk_iv_ns_cleanup(struct ds_iv_ns **ns)
{
	if (*ns != NULL) {
		ds_iv_ns_cleanup(*ns);
		ds_iv_ns_put(*ns);
		*ns = NULL;
	}
}

static inline void
chk_fini_clues(struct ds_pool_clue *clue_array, int nr, d_rank_t rank)
{
	struct ds_pool_clues	clues;

	if (rank == dss_self_rank()) {
		clues.pcs_array = clue_array;
		clues.pcs_len = nr;
		ds_pool_clues_fini(&clues);
	}
}

static inline void
chk_fini_shards(struct chk_query_pool_shard *shards, int nr)
{
	if (nr != 0 && shards[0].cqps_rank == dss_self_rank())
		chk_query_free(shards, nr);
}

static inline void
chk_fini_conts(uuid_t *conts, d_rank_t rank)
{
	if (rank == dss_self_rank())
		D_FREE(conts);
}

static inline void
chk_pool_get(struct chk_pool_rec *cpr)
{
	cpr->cpr_refs++;
}

static inline void
chk_pool_put(struct chk_pool_rec *cpr)
{
	struct chk_pool_shard	*cps;
	int			 i;

	/* NOTE: Before being destroyed, keep it in the list. */
	D_ASSERT(!d_list_empty(&cpr->cpr_link));

	if (--(cpr->cpr_refs) == 0) {
		d_list_del(&cpr->cpr_link);
		D_ASSERT(cpr->cpr_thread == ABT_THREAD_NULL);

		while ((cps = d_list_pop_entry(&cpr->cpr_shard_list, struct chk_pool_shard,
					       cps_link)) != NULL) {
			if (cps->cps_free_cb != NULL)
				cps->cps_free_cb(cps->cps_data);
			else
				D_FREE(cps->cps_data);
			D_FREE(cps);
		}
		D_FREE(cpr->cpr_clues.pcs_array);

		if (cpr->cpr_mutex != ABT_MUTEX_NULL)
			ABT_mutex_free(&cpr->cpr_mutex);
		if (cpr->cpr_cond != ABT_COND_NULL)
			ABT_cond_free(&cpr->cpr_cond);

		if (!cpr->cpr_ins->ci_is_leader && cpr->cpr_mbs != NULL) {
			for (i = 0; i < cpr->cpr_shard_nr; i++)
				D_FREE(cpr->cpr_mbs[i].cpm_tgt_status);
		}

		D_FREE(cpr->cpr_mbs);
		D_FREE(cpr->cpr_label);
		D_FREE(cpr);
	}
}

static inline void
chk_pool_shutdown(struct chk_pool_rec *cpr)
{
	d_iov_t		psid;
	int		rc;

	D_ASSERT(cpr->cpr_refs > 0);

	d_iov_set(&psid, cpr->cpr_uuid, sizeof(uuid_t));
	rc = ds_rsvc_stop(DS_RSVC_CLASS_POOL, &psid, RDB_NIL_TERM, false);
	D_DEBUG(DB_MD, "Stop PS for "DF_UUIDF": "DF_RC"\n",
		DP_UUID(cpr->cpr_uuid), DP_RC(rc));
	ds_pool_stop(cpr->cpr_uuid);
	cpr->cpr_started = 0;
	cpr->cpr_start_post = 0;
}

static inline void
chk_pool_wait(struct chk_pool_rec *cpr)
{
	D_ASSERT(cpr->cpr_refs > 0);

	ABT_mutex_lock(cpr->cpr_mutex);
	if (cpr->cpr_thread != ABT_THREAD_NULL && !cpr->cpr_stop) {
		cpr->cpr_stop = 1;
		ABT_cond_broadcast(cpr->cpr_cond);
		ABT_mutex_unlock(cpr->cpr_mutex);
		ABT_thread_free(&cpr->cpr_thread);
	} else {
		ABT_mutex_unlock(cpr->cpr_mutex);
	}
}

static inline bool
chk_pool_in_zombie(struct chk_pool_rec *cpr)
{
	struct chk_pool_shard	*cps;
	struct ds_pool_clue	*clue;
	bool			 found = false;

	d_list_for_each_entry(cps, &cpr->cpr_shard_list, cps_link) {
		clue = cps->cps_data;
		if (clue->pc_dir == DS_POOL_DIR_ZOMBIE) {
			found = true;
			break;
		}
	}

	return found;
}

static inline int
chk_pool_has_err(struct chk_pool_rec *cpr)
{
	struct chk_pool_shard	*cps;
	struct ds_pool_clue	*clue;
	int			 rc = 0;

	d_list_for_each_entry(cps, &cpr->cpr_shard_list, cps_link) {
		clue = cps->cps_data;
		if (clue->pc_rc < 0) {
			rc = clue->pc_rc;
			break;
		}
	}

	return rc >= 0 ? 0 : rc;
}

static inline int
chk_pools_add_from_dir(uuid_t uuid, void *args)
{
	struct chk_traverse_pools_args	*ctpa = args;

	return chk_pool_start_one(ctpa->ctpa_ins, uuid, ctpa->ctpa_gen);
}

static inline uint32_t
chk_pools_find_slowest(struct chk_instance *ins, bool *done)
{
	struct chk_pool_rec	*cpr;
	uint32_t		 phase = CHK__CHECK_SCAN_PHASE__DSP_DONE;

	*done = true;

	d_list_for_each_entry(cpr, &ins->ci_pool_list, cpr_link) {
		if (cpr->cpr_skip || cpr->cpr_done)
			continue;

		*done = false;

		if (cpr->cpr_bk.cb_phase < phase)
			phase = cpr->cpr_bk.cb_phase;
	}

	return phase;
}

static inline int
chk_dup_string(char **tgt, const char *src, size_t len)
{
	int	rc = 0;

	if (src == NULL) {
		*tgt = NULL;
	} else {
		D_STRNDUP(*tgt, src, len);
		if (*tgt == NULL)
			rc = -DER_NOMEM;
	}

	return rc;
}

static inline void
chk_stop_sched(struct chk_instance *ins)
{
	ABT_mutex_lock(ins->ci_abt_mutex);
	if (ins->ci_sched != ABT_THREAD_NULL && ins->ci_sched_running) {
		ins->ci_sched_running = 0;
		ABT_cond_broadcast(ins->ci_abt_cond);
		ABT_mutex_unlock(ins->ci_abt_mutex);
		ABT_thread_free(&ins->ci_sched);
	} else {
		ABT_mutex_unlock(ins->ci_abt_mutex);
	}
}

static inline int
chk_ins_can_start(struct chk_instance *ins)
{
	struct chk_bookmark	*cbk = &ins->ci_bk;

	if (ins->ci_starting)
		return -DER_INPROGRESS;

	if (ins->ci_stopping)
		return -DER_BUSY;

	if (ins->ci_sched_running)
		return -DER_ALREADY;

	/*
	 * If ci_sched_running is zero but check instance is still running,
	 * then someone is trying to stop it.
	 */
	if (((ins->ci_is_leader && cbk->cb_magic == CHK_BK_MAGIC_LEADER) ||
	     (!ins->ci_is_leader && cbk->cb_magic == CHK_BK_MAGIC_ENGINE)) &&
	    cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_RUNNING)
		return -DER_BUSY;

	return 0;
}

#endif /* __CHK_INTERNAL_H__ */
