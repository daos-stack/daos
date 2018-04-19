/**
 * (C) Copyright 2017 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * rdb: Internal Declarations
 */

#ifndef RDB_INTERNAL_H
#define RDB_INTERNAL_H

#include <abt.h>
#include <raft.h>
#include <gurt/hash.h>
#include <daos/lru.h>
#include <daos/rpc.h>

/* rdb_raft.c (parts required by struct rdb) **********************************/

enum rdb_raft_event_type {
	RDB_RAFT_STEP_UP,
	RDB_RAFT_STEP_DOWN
};

struct rdb_raft_event {
	enum rdb_raft_event_type	dre_type;
	uint64_t			dre_term;
};

/* rdb.c **********************************************************************/

struct rdb {
	d_list_t		d_entry;	/* in rdb_hash */
	uuid_t			d_uuid;		/* of database */
	ABT_mutex		d_mutex;	/* mainly for using CVs */
	int			d_ref;		/* of callers and RPCs */
	ABT_cond		d_ref_cv;	/* for d_ref decrements */
	struct rdb_cbs	       *d_cbs;		/* callers' callbacks */
	void		       *d_arg;		/* for d_cbs callbacks */
	struct daos_lru_cache  *d_trees;	/* rdb_tree cache */
	PMEMobjpool	       *d_pmem;
	daos_handle_t		d_attr;		/* rdb attribute tree */
	d_rank_list_t       *d_replicas;

	raft_server_t	       *d_raft;
	daos_handle_t		d_log;		/* rdb log tree */
	uint64_t		d_applied;	/* last applied index */
	uint64_t		d_debut;	/* first entry in a term */
	ABT_cond		d_applied_cv;	/* for d_applied updates */
	ABT_cond		d_committed_cv;	/* for last committed updates */
	struct d_hash_table	d_results;	/* rdb_raft_result hash */
	d_list_t		d_requests;	/* RPCs waiting for replies */
	d_list_t		d_replies;	/* RPCs received replies */
	ABT_cond		d_replies_cv;	/* for d_replies enqueues */
	struct rdb_raft_event	d_events[2];	/* rdb_raft_events queue */
	int			d_nevents;	/* d_events queue len from 0 */
	ABT_cond		d_events_cv;	/* for d_events enqueues */
	bool			d_stop;		/* for rdb_stop() */
	ABT_thread		d_timerd;
	ABT_thread		d_applyd;
	ABT_thread		d_callbackd;
	ABT_thread		d_recvd;
};

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
#define DP_DB(db)	DP_UUID(db->d_uuid), DP_RANK()

/* Number of "base" references that the rdb_stop() path expects to remain */
#define RDB_BASE_REFS 1

int rdb_hash_init(void);
void rdb_hash_fini(void);
void rdb_get(struct rdb *db);
void rdb_put(struct rdb *db);
struct rdb *rdb_lookup(const uuid_t uuid);

void rdb_start_handler(crt_rpc_t *rpc);
int rdb_start_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv);
void rdb_stop_handler(crt_rpc_t *rpc);
int rdb_stop_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv);

/* rdb_raft.c *****************************************************************/

/* Per-raft_node_t data */
struct rdb_raft_node {
	d_rank_t	dn_rank;
};

int rdb_raft_init(daos_handle_t rdb_attr);
int rdb_raft_start(struct rdb *db);
void rdb_raft_stop(struct rdb *db);
void rdb_raft_resign(struct rdb *db, uint64_t term);
int rdb_raft_verify_leadership(struct rdb *db);
int rdb_raft_append_apply(struct rdb *db, void *entry, size_t size,
			  void *result);
int rdb_raft_wait_applied(struct rdb *db, uint64_t index, uint64_t term);
void rdb_requestvote_handler(crt_rpc_t *rpc);
void rdb_appendentries_handler(crt_rpc_t *rpc);
void rdb_raft_process_reply(struct rdb *db, raft_node_t *node, crt_rpc_t *rpc);
void rdb_raft_free_request(struct rdb *db, crt_rpc_t *rpc);

/* rdb_rpc.c ******************************************************************/

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See src/include/daos/rpc.h.
 */
enum rdb_operation {
	RDB_REQUESTVOTE		= 1,
	RDB_APPENDENTRIES	= 2,
	RDB_START		= 3,
	RDB_STOP		= 4
};

struct rdb_op_in {
	uuid_t	ri_uuid;
};

struct rdb_op_out {
	int32_t		ro_rc;
	uint32_t	ro_padding;
};

struct rdb_requestvote_in {
	struct rdb_op_in	rvi_op;
	msg_requestvote_t	rvi_msg;
};

struct rdb_requestvote_out {
	struct rdb_op_out		rvo_op;
	msg_requestvote_response_t	rvo_msg;
};

struct rdb_appendentries_in {
	struct rdb_op_in	aei_op;
	msg_appendentries_t	aei_msg;
};

struct rdb_appendentries_out {
	struct rdb_op_out		aeo_op;
	msg_appendentries_response_t	aeo_msg;
};

enum rdb_start_flag {
	RDB_AF_CREATE	= 1
};

struct rdb_start_in {
	uuid_t			dai_uuid;
	uuid_t			dai_pool;	/* for ds_mgmt_tgt_file() */
	uint32_t		dai_flags;	/* rdb_start_flag */
	uint32_t		dai_padding;
	uint64_t		dai_size;
	d_rank_list_t       *dai_ranks;
};

struct rdb_start_out {
	int	dao_rc;
};

enum rdb_stop_flag {
	RDB_OF_DESTROY	= 1
};

struct rdb_stop_in {
	uuid_t		doi_uuid;
	uuid_t		doi_pool;	/* for ds_mgmt_tgt_file() */
	uint32_t	doi_flags;	/* rdb_stop_flag */
};

struct rdb_stop_out {
	int	doo_rc;
};

extern struct daos_rpc rdb_srv_rpcs[];

int rdb_create_raft_rpc(crt_opcode_t opc, raft_node_t *node, crt_rpc_t **rpc);
int rdb_send_raft_rpc(crt_rpc_t *rpc, struct rdb *db, raft_node_t *node);
int rdb_abort_raft_rpcs(struct rdb *db);
int rdb_create_bcast(crt_opcode_t opc, crt_group_t *group, crt_rpc_t **rpc);
void rdb_recvd(void *arg);

/* rdb_tx.c *******************************************************************/

int rdb_tx_apply(struct rdb *db, uint64_t index, const void *buf, size_t len,
		 void *result, d_list_t *destroyed);

/* rdb_kvs.c ******************************************************************/

/* Tree handle cache entry */
struct rdb_tree {
	struct daos_llink	de_entry;	/* in LRU */
	rdb_path_t		de_path;
	daos_handle_t		de_hdl;		/* of dbtree */
	d_list_t		de_list;	/* for rdb_tx_apply_op() */
};

int rdb_tree_cache_create(struct daos_lru_cache **cache);
void rdb_tree_cache_destroy(struct daos_lru_cache *cache);
int rdb_tree_lookup(struct rdb *db, const daos_iov_t *path,
		    struct rdb_tree **tree);
void rdb_tree_put(struct rdb *db, struct rdb_tree *tree);
void rdb_tree_evict(struct rdb *db, struct rdb_tree *tree);

/* rdb_path.c *****************************************************************/

int rdb_path_clone(const rdb_path_t *path, rdb_path_t *new_path);
typedef int (*rdb_path_iterate_cb_t)(daos_iov_t *key, void *arg);
int rdb_path_iterate(const rdb_path_t *path, rdb_path_iterate_cb_t cb,
		     void *arg);
int rdb_path_pop(rdb_path_t *path);

/* rdb_util.c *****************************************************************/

#define DF_IOV		"<%p,"DF_U64">"
#define DP_IOV(iov)	(iov)->iov_buf, (iov)->iov_len

extern const daos_size_t rdb_iov_max;
size_t rdb_encode_iov(const daos_iov_t *iov, void *buf);
ssize_t rdb_decode_iov(const void *buf, size_t len, daos_iov_t *iov);
ssize_t rdb_decode_iov_backward(const void *buf_end, size_t len,
				daos_iov_t *iov);

int rdb_create_tree(daos_handle_t parent, daos_iov_t *key,
		    enum rdb_kvs_class class, uint64_t feats,
		    unsigned int order, daos_handle_t *child);
int rdb_open_tree(daos_handle_t tree, daos_iov_t *key, daos_handle_t *child);
int rdb_destroy_tree(daos_handle_t parent, daos_iov_t *key);

#endif /* RDB_INTERNAL_H */
