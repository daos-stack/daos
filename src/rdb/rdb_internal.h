/*
 * (C) Copyright 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * rdb: Internal Declarations
 */

#ifndef RDB_INTERNAL_H
#define RDB_INTERNAL_H

#include <abt.h>
#include <raft.h>
#include <gurt/hash.h>
#include <daos/lru.h>
#include <daos/rpc.h>
#include <daos/object.h>
#include "rdb_layout.h"

/* rdb_raft.c (parts required by struct rdb) **********************************/

enum rdb_raft_event_type {
	RDB_RAFT_STEP_UP,
	RDB_RAFT_STEP_DOWN
};

struct rdb_raft_event {
	enum rdb_raft_event_type	dre_type;
	uint64_t			dre_term;
};

#define RDB_NOSPC_ERR_INTVL_USEC (1000000)	/* 1 second */

/* rdb.c **********************************************************************/

static inline struct rdb *
rdb_from_storage(struct rdb_storage *storage)
{
	return (struct rdb *)storage;
}

static inline struct rdb_storage *
rdb_to_storage(struct rdb *db)
{
	return (struct rdb_storage *)db;
}

enum {
	CHKPT_NONE = 0,
	CHKPT_MUTEX,
	CHKPT_MAIN_CV,
	CHKPT_COMMIT_CV,
	CHKPT_ULT,
};

struct rdb_chkpt_record {
	uint32_t dcr_state : 4, dcr_init : 1, dcr_enabled : 1, dcr_waiting : 1, dcr_needed : 1,
	    dcr_idle : 1, dcr_stop : 1;
	uint32_t dcr_thresh;
	uint64_t dcr_commit_id;
	uint64_t dcr_wait_id;
	struct umem_store *dcr_store;
};

/* multi-ULT locking in struct rdb:
 *  d_mutex: for RPC mgmt and ref count:
 *    d_requests, d_replies/cv, d_ref/cv
 *  d_raft_mutex: for raft state
 *    d_lc_record, d_applied/cv, d_events[]/cv, d_nevents, d_compact_cv
 *
 * TODO: locking for d_stop
 */
struct rdb {
	/* General fields */
	d_list_t		d_entry;	/* in rdb_hash */
	uuid_t			d_uuid;		/* of database */
	ABT_mutex		d_mutex;	/* d_replies, d_replies_cv */
	int			d_ref;		/* of callers and RPCs */
	ABT_cond		d_ref_cv;	/* for d_ref decrements */
	struct rdb_cbs	       *d_cbs;		/* callers' callbacks */
	void		       *d_arg;		/* for d_cbs callbacks */
	struct daos_lru_cache  *d_kvss;		/* rdb_kvs cache */
	daos_handle_t		d_pool;		/* VOS pool */
	struct rdb_chkpt_record d_chkpt_record; /* pool checkpoint information */
	ABT_thread              d_chkptd;       /* thread handle for pool checkpoint daemon */
	ABT_mutex               d_chkpt_mutex;  /* mutex for checkpoint synchronization */
	ABT_cond                d_chkpt_cv;     /* for triggering pool checkpointing */
	ABT_cond                d_commit_cv;    /* for waking active pool checkpoint */
	daos_handle_t		d_mc;		/* metadata container */
	uint64_t		d_nospc_ts;	/* last time commit observed low/no space (usec) */
	bool			d_new;		/* for skipping lease recovery */
	bool			d_use_leases;	/* when verifying leadership */

	/* rdb_raft fields */
	raft_server_t	       *d_raft;
	bool			d_raft_loaded;	/* from storage (see rdb_raft_load) */
	ABT_mutex		d_raft_mutex;	/* for raft state machine */
	daos_handle_t		d_lc;		/* log container */
	struct rdb_lc_record	d_lc_record;	/* of d_lc */
	daos_handle_t		d_slc;		/* staging log container */
	struct rdb_lc_record    d_slc_record;   /* of d_slc */
	uint64_t		d_applied;	/* last applied index */
	uint64_t		d_debut;	/* first entry in a term */
	ABT_cond		d_applied_cv;	/* for d_applied updates */
	struct d_hash_table	d_results;	/* rdb_raft_result hash */
	d_list_t		d_requests;	/* RPCs waiting for replies */
	d_list_t		d_replies;	/* RPCs received replies */
	ABT_cond		d_replies_cv;	/* for d_replies enqueues */
	struct rdb_raft_event	d_events[2];	/* rdb_raft_events queue */
	int			d_nevents;	/* d_events queue len from 0 */
	ABT_cond		d_events_cv;	/* for d_events enqueues */
	uint64_t		d_compact_thres;/* of compactable entries */
	ABT_cond		d_compact_cv;	/* for triggering base updates */
	ABT_cond                d_compacted_cv; /* for d_lc_record.dlr_aggregated updates */
	bool			d_stop;		/* for rdb_stop() */
	ABT_thread		d_timerd;
	ABT_thread              d_callbackd;
	ABT_thread		d_recvd;
	ABT_thread		d_compactd;
	size_t			d_ae_max_size;
	unsigned int		d_ae_max_entries;
};

/* thresholds of free space for a leader to avoid appending new log entries (512 KiB)
 * and follower to warn if the situation is really dire (16KiB)
 */
#define RDB_NOAPPEND_FREE_SPACE (1ULL << 19)
#define RDB_CRITICAL_FREE_SPACE (1ULL << 14)

/* Current rank */
#define DF_RANK "%u"
static inline d_rank_t
DP_RANK(void)
{
	d_rank_t	rank;
	int		rc;

	rc = crt_group_rank(NULL, &rank);
	D_ASSERTF(rc == 0, "%d\n", rc);
	return rank;
}

#define DF_DB		DF_UUID"["DF_RANK"]"
#define DP_DB(db)	DP_UUID((db)->d_uuid), DP_RANK()

/* Number of "base" references that the rdb_stop() path expects to remain */
#define RDB_BASE_REFS 1

int rdb_hash_init(void);
void rdb_hash_fini(void);
void rdb_get(struct rdb *db);
void rdb_put(struct rdb *db);
struct rdb *rdb_lookup(const uuid_t uuid);

/* rdb_raft.c *****************************************************************/

/*
 * Per-raft_node_t INSTALLSNAPSHOT state
 *
 * dis_seq and dis_anchor track the last chunk successfully received by the
 * follower.
 */
struct rdb_raft_is {
	uint64_t		dis_index;	/* snapshot index */
	uint64_t		dis_seq;	/* last sequence number */
	struct rdb_anchor	dis_anchor;	/* last anchor */
};

/* Per-raft_node_t data */
struct rdb_raft_node {
	d_rank_t		dn_rank;

	/* Leader fields */
	uint64_t		dn_term;	/* of leader */
	struct rdb_raft_is	dn_is;
};

int rdb_raft_init(daos_handle_t pool, daos_handle_t mc, const d_rank_list_t *replicas);
int rdb_raft_open(struct rdb *db, uint64_t caller_term);
int rdb_raft_start(struct rdb *db);
void rdb_raft_stop(struct rdb *db);
void rdb_raft_close(struct rdb *db);
void rdb_raft_resign(struct rdb *db, uint64_t term);
int rdb_raft_campaign(struct rdb *db);
int rdb_raft_ping(struct rdb *db, uint64_t caller_term);
int rdb_raft_verify_leadership(struct rdb *db);
int rdb_raft_add_replica(struct rdb *db, d_rank_t rank);
int rdb_raft_remove_replica(struct rdb *db, d_rank_t rank);
int rdb_raft_append_apply(struct rdb *db, void *entry, size_t size,
			  void *result);
int rdb_raft_wait_applied(struct rdb *db, uint64_t index, uint64_t term);
int rdb_raft_get_ranks(struct rdb *db, d_rank_list_t **ranksp);
void rdb_requestvote_handler(crt_rpc_t *rpc);
void rdb_appendentries_handler(crt_rpc_t *rpc);
void rdb_installsnapshot_handler(crt_rpc_t *rpc);
void rdb_raft_process_reply(struct rdb *db, crt_rpc_t *rpc);
void rdb_raft_free_request(struct rdb *db, crt_rpc_t *rpc);
int rdb_raft_trigger_compaction(struct rdb *db, bool compact_all, uint64_t *idx);

/* rdb_rpc.c ******************************************************************/

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See src/include/daos/rpc.h.
 */
#define DAOS_RDB_VERSION 4
/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 */
#define RDB_PROTO_SRV_RPC_LIST						\
	X(RDB_REQUESTVOTE,						\
		0, &CQF_rdb_requestvote,				\
		rdb_requestvote_handler, NULL),				\
	X(RDB_APPENDENTRIES,						\
		0, &CQF_rdb_appendentries,				\
		rdb_appendentries_handler, NULL),			\
	X(RDB_INSTALLSNAPSHOT,						\
		0, &CQF_rdb_installsnapshot,				\
		rdb_installsnapshot_handler, NULL)

/* Define for RPC enum population below */
#define X(a, b, c, d, e) a

enum rdb_operation {
	RDB_PROTO_SRV_RPC_LIST,
};

#undef X

extern struct crt_proto_format rdb_proto_fmt;

#define DAOS_ISEQ_RDB_OP	/* input fields */		 \
	((uuid_t)		(ri_uuid)		CRT_VAR)

#define DAOS_OSEQ_RDB_OP	/* output fields */		 \
	((int32_t)		(ro_rc)			CRT_VAR) \
	((uint32_t)		(ro_padding)		CRT_VAR)

CRT_RPC_DECLARE(rdb_op, DAOS_ISEQ_RDB_OP, DAOS_OSEQ_RDB_OP)

#define DAOS_ISEQ_RDB_REQUESTVOTE /* input fields */		 \
	((struct rdb_op_in)	(rvi_op)		CRT_VAR) \
	((msg_requestvote_t)	(rvi_msg)		CRT_RAW)

#define DAOS_OSEQ_RDB_REQUESTVOTE /* output fields */		 \
	((struct rdb_op_out)	(rvo_op)		CRT_VAR) \
	((msg_requestvote_response_t) (rvo_msg)		CRT_VAR)

CRT_RPC_DECLARE(rdb_requestvote, DAOS_ISEQ_RDB_REQUESTVOTE,
		DAOS_OSEQ_RDB_REQUESTVOTE)

#define DAOS_ISEQ_RDB_APPENDENTRIES /* input fields */		 \
	((struct rdb_op_in)	(aei_op)		CRT_VAR) \
	((msg_appendentries_t)	(aei_msg)		CRT_VAR)

#define DAOS_OSEQ_RDB_APPENDENTRIES /* output fields */		 \
	((struct rdb_op_out)	(aeo_op)		CRT_VAR) \
	((msg_appendentries_response_t) (aeo_msg)	CRT_RAW)

CRT_RPC_DECLARE(rdb_appendentries, DAOS_ISEQ_RDB_APPENDENTRIES,
		DAOS_OSEQ_RDB_APPENDENTRIES)

struct rdb_local {
	d_iov_t	rl_kds_iov;	/* isi_kds buffer */
	d_iov_t	rl_data_iov;	/* isi_data buffer */
};

#define DAOS_ISEQ_RDB_INSTALLSNAPSHOT /* input fields */	 \
	((struct rdb_op_in)	(isi_op)		CRT_VAR) \
	((msg_installsnapshot_t) (isi_msg)		CRT_VAR) \
	/* chunk sequence number */				 \
	((uint64_t)		(isi_seq)		CRT_VAR) \
	/* chunk anchor */					 \
	((struct rdb_anchor)	(isi_anchor)		CRT_RAW) \
	/* daos_key_desc_t[] */					 \
	((crt_bulk_t)		(isi_kds)		CRT_VAR) \
	/* described by isi_kds */				 \
	((crt_bulk_t)		(isi_data)		CRT_VAR) \
	/* Local fields (not sent over the network) */		 \
	((struct rdb_local)	(isi_local)		CRT_VAR)

#define DAOS_OSEQ_RDB_INSTALLSNAPSHOT /* output fields */	 \
	((struct rdb_op_out)	(iso_op)		CRT_VAR) \
	((msg_installsnapshot_response_t) (iso_msg)	CRT_VAR) \
	/* chunk saved? */					 \
	((uint64_t)		(iso_success)		CRT_VAR) \
	/* last seq number */					 \
	((uint64_t)		(iso_seq)		CRT_VAR) \
	/* last anchor */					 \
	((struct rdb_anchor)	(iso_anchor)		CRT_RAW)

CRT_RPC_DECLARE(rdb_installsnapshot, DAOS_ISEQ_RDB_INSTALLSNAPSHOT,
		DAOS_OSEQ_RDB_INSTALLSNAPSHOT)

int rdb_create_raft_rpc(crt_opcode_t opc, raft_node_t *node, crt_rpc_t **rpc);
int rdb_send_raft_rpc(crt_rpc_t *rpc, struct rdb *db);
int rdb_abort_raft_rpcs(struct rdb *db);
void rdb_recvd(void *arg);

/* rdb_tx.c *******************************************************************/

int rdb_tx_apply(struct rdb *db, uint64_t index, const void *buf, size_t len,
		 void *result, bool *critp);

/* rdb_kvs.c ******************************************************************/

/* KVS cache entry */
struct rdb_kvs {
	struct daos_llink	de_entry;	/* in LRU */
	rdb_path_t		de_path;
	rdb_oid_t		de_object;
	uint8_t			de_buf[];	/* for de_path */
};

int rdb_kvs_cache_create(struct daos_lru_cache **cache);
void rdb_kvs_cache_destroy(struct daos_lru_cache *cache);
void rdb_kvs_cache_evict(struct daos_lru_cache *cache);
int rdb_kvs_lookup(struct rdb *db, const rdb_path_t *path, uint64_t index,
		   bool alloc, struct rdb_kvs **kvs);
void rdb_kvs_put(struct rdb *db, struct rdb_kvs *kvs);
void rdb_kvs_evict(struct rdb *db, struct rdb_kvs *kvs);

/* rdb_path.c *****************************************************************/

int rdb_path_clone(const rdb_path_t *path, rdb_path_t *new_path);
typedef int (*rdb_path_iterate_cb_t)(d_iov_t *key, void *arg);
int rdb_path_iterate(const rdb_path_t *path, rdb_path_iterate_cb_t cb,
		     void *arg);
int rdb_path_pop(rdb_path_t *path);

/* rdb_util.c *****************************************************************/

extern const daos_size_t rdb_iov_max;
size_t rdb_encode_iov(const d_iov_t *iov, void *buf);
ssize_t rdb_decode_iov(const void *buf, size_t len, d_iov_t *iov);
ssize_t rdb_decode_iov_backward(const void *buf_end, size_t len,
				d_iov_t *iov);

void rdb_oid_to_uoid(rdb_oid_t oid, daos_unit_oid_t *uoid);

void rdb_anchor_set_zero(struct rdb_anchor *anchor);
void rdb_anchor_set_eof(struct rdb_anchor *anchor);
bool rdb_anchor_is_eof(const struct rdb_anchor *anchor);
void rdb_anchor_to_hashes(const struct rdb_anchor *anchor,
			  daos_anchor_t *obj_anchor, daos_anchor_t *dkey_anchor,
			  daos_anchor_t *akey_anchor, daos_anchor_t *ev_anchor,
			  daos_anchor_t *sv_anchor);
void rdb_anchor_from_hashes(struct rdb_anchor *anchor,
			    daos_anchor_t *obj_anchor,
			    daos_anchor_t *dkey_anchor,
			    daos_anchor_t *akey_anchor,
			    daos_anchor_t *ev_anchor, daos_anchor_t *sv_anchor);

int rdb_vos_fetch(daos_handle_t cont, daos_epoch_t epoch, rdb_oid_t oid,
		  daos_key_t *akey, d_iov_t *value);
int rdb_vos_fetch_addr(daos_handle_t cont, daos_epoch_t epoch, rdb_oid_t oid,
		       daos_key_t *akey, d_iov_t *value);
int rdb_vos_query_key_max(daos_handle_t cont, daos_epoch_t epoch, rdb_oid_t oid, daos_key_t *akey);
int rdb_vos_iter_fetch(daos_handle_t cont, daos_epoch_t epoch, rdb_oid_t oid,
		       enum rdb_probe_opc opc, daos_key_t *akey_in,
		       daos_key_t *akey_out, d_iov_t *value);
int rdb_vos_iterate(daos_handle_t cont, daos_epoch_t epoch, rdb_oid_t oid,
		    bool backward, rdb_iterate_cb_t cb, void *arg);
int rdb_vos_update(daos_handle_t cont, daos_epoch_t epoch, rdb_oid_t oid,
		   bool crit, int n, d_iov_t akeys[], d_iov_t values[]);
int rdb_vos_punch(daos_handle_t cont, daos_epoch_t epoch, rdb_oid_t oid, int n,
		  d_iov_t akeys[]);
int rdb_vos_discard(daos_handle_t cont, daos_epoch_t low, daos_epoch_t high);
int rdb_vos_aggregate(daos_handle_t cont, daos_epoch_t high);

/*
 * Maximal number of a-keys (i.e., the n parameter) passed to an
 * rdb_mc_update() call. Bumping this number increases the stack usage of
 * rdb_vos_update().
 */
#define RDB_VOS_BATCH_MAX 2

/* Update n (<= RDB_VOS_BATCH_MAX) a-keys atomically. */
static inline int
rdb_mc_update(daos_handle_t mc, rdb_oid_t oid, int n, d_iov_t akeys[],
	      d_iov_t values[])
{
	D_DEBUG(DB_TRACE, "mc="DF_X64" oid="DF_X64" n=%d akeys[0]=<%p, %zd> "
		"values[0]=<%p, %zd>\n", mc.cookie, oid, n, akeys[0].iov_buf,
		akeys[0].iov_len, values[0].iov_buf, values[0].iov_len);
	return rdb_vos_update(mc, RDB_MC_EPOCH, oid, true /* crit */, n,
			      akeys, values);
}

static inline int
rdb_mc_lookup(daos_handle_t mc, rdb_oid_t oid, d_iov_t *akey,
	      d_iov_t *value)
{
	D_DEBUG(DB_TRACE, "mc="DF_X64" oid="DF_X64" akey=<%p, %zd> "
		"value=<%p, %zd, %zd>\n", mc.cookie, oid, akey->iov_buf,
		akey->iov_len, value->iov_buf, value->iov_buf_len,
		value->iov_len);
	return rdb_vos_fetch(mc, RDB_MC_EPOCH, oid, akey, value);
}

static inline int
rdb_lc_update(daos_handle_t lc, uint64_t index, rdb_oid_t oid, bool crit,
	      int n, d_iov_t akeys[], d_iov_t values[])
{
	D_DEBUG(DB_TRACE, "lc="DF_X64" index="DF_U64" oid="DF_X64
		" n=%d akeys[0]=<%p, %zd> values[0]=<%p, %zd>\n", lc.cookie,
		index, oid, n, akeys[0].iov_buf, akeys[0].iov_len,
		values[0].iov_buf, values[0].iov_len);
	return rdb_vos_update(lc, index, oid, crit, n, akeys, values);
}

static inline int
rdb_lc_punch(daos_handle_t lc, uint64_t index, rdb_oid_t oid, int n,
	     d_iov_t akeys[])
{
	if (n > 0)
		D_DEBUG(DB_TRACE, "lc="DF_X64" index="DF_U64" oid="DF_X64
			" n=%d akeys[0]=<%p, %zd>\n", lc.cookie, index, oid, n,
			akeys[0].iov_buf, akeys[0].iov_len);
	else
		D_DEBUG(DB_TRACE, "lc="DF_X64" index="DF_U64" oid="DF_X64
			" n=%d\n", lc.cookie, index, oid, n);
	return rdb_vos_punch(lc, index, oid, n, akeys);
}

/* Discard index range [low, high]. */
static inline int
rdb_lc_discard(daos_handle_t lc, uint64_t low, uint64_t high)
{
	D_DEBUG(DB_TRACE, "lc="DF_X64" low="DF_U64" high="DF_U64"\n", lc.cookie,
		low, high);
	return rdb_vos_discard(lc, low, high);
}

/* Aggregate index range [0, high] and yield from time to time. */
static inline int
rdb_lc_aggregate(daos_handle_t lc, uint64_t high)
{
	D_DEBUG(DB_TRACE, "lc="DF_X64" high="DF_U64"\n", lc.cookie, high);
	return rdb_vos_aggregate(lc, high);
}

static inline int
rdb_lc_lookup(daos_handle_t lc, uint64_t index, rdb_oid_t oid,
	      d_iov_t *akey, d_iov_t *value)
{
	D_DEBUG(DB_TRACE, "lc="DF_X64" index="DF_U64" oid="DF_X64
		" akey=<%p, %zd> value=<%p, %zd, %zd>\n", lc.cookie, index, oid,
		akey->iov_buf, akey->iov_len, value->iov_buf,
		value->iov_buf_len, value->iov_len);
	if (value->iov_buf == NULL)
		return rdb_vos_fetch_addr(lc, index, oid, akey, value);
	else
		return rdb_vos_fetch(lc, index, oid, akey, value);
}

static inline int
rdb_lc_iter_fetch(daos_handle_t lc, uint64_t index, rdb_oid_t oid,
		  enum rdb_probe_opc opc, d_iov_t *akey_in,
		  d_iov_t *akey_out, d_iov_t *value)
{
	D_DEBUG(DB_TRACE, "lc="DF_X64" index="DF_U64" oid="DF_X64" opc=%d"
		" akey_in=<%p, %zd> akey_out=<%p, %zd> value=<%p, %zd, %zd>\n",
		lc.cookie, index, oid, opc,
		akey_in == NULL ? NULL : akey_in->iov_buf,
		akey_in == NULL ? 0 : akey_in->iov_len,
		akey_out == NULL ? NULL : akey_out->iov_buf,
		akey_out == NULL ? 0 : akey_out->iov_len,
		value == NULL ? NULL : value->iov_buf,
		value == NULL ? 0 : value->iov_buf_len,
		value == NULL ? 0 : value->iov_len);
	return rdb_vos_iter_fetch(lc, index, oid, opc, akey_in, akey_out,
				  value);
}

static inline int
rdb_lc_query_key_max(daos_handle_t lc, uint64_t index, rdb_oid_t oid, d_iov_t *akey)
{
	return rdb_vos_query_key_max(lc, index, oid, akey);
}

static inline int
rdb_lc_iterate(daos_handle_t lc, uint64_t index, rdb_oid_t oid, bool backward,
	       rdb_iterate_cb_t cb, void *arg)
{
	D_DEBUG(DB_TRACE, "lc="DF_X64" index="DF_U64" oid="DF_X64
		" backward=%d\n", lc.cookie, index, oid, backward);
	return rdb_vos_iterate(lc, index, oid, backward, cb, arg);
}

int
rdb_scm_left(struct rdb *db, daos_size_t *scm_left_outp);

#endif /* RDB_INTERNAL_H */
