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
#include <daos/hash.h>
#include <daos/lru.h>
#include <daos/rpc.h>

/* rdb.c **********************************************************************/

struct rdb {
	uuid_t			d_uuid;		/* of database */
	int			d_ref;
	struct rdb_cbs	       *d_cbs;
	void		       *d_arg;		/* for d_cbs callbacks */
	struct daos_lru_cache  *d_trees;	/* rdb_tree cache */
	PMEMobjpool	       *d_pmem;
	daos_handle_t		d_attr;		/* rdb attribute tree */
	daos_handle_t		d_log;		/* rdb log tree */

	raft_server_t	       *d_raft;
	ABT_mutex		d_mutex;	/* not useful currently */
	struct dhash_table	d_results;	/* rdb_raft_result objects */
	ABT_thread		d_timerd;
	bool			d_timerd_stop;
	ABT_thread		d_applyd;
	bool			d_applyd_stop;
	uint64_t		d_debut;	/* first entry in a term */
	uint64_t		d_applied;	/* last applied index */
	ABT_cond		d_applied_cv;	/* for d_applied updates */
	ABT_cond		d_committed_cv;	/* for last committed */

	daos_list_t		d_replies;	/* list of Raft RPCs */
	ABT_thread		d_recvd;
	bool			d_recvd_stop;
};

/* Current rank */
#define DF_RANK "%u"
static inline crt_rank_t
DP_RANK(void)
{
	crt_rank_t	rank;
	int		rc;

	rc = crt_group_rank(NULL, &rank);
	D_ASSERTF(rc == 0, "%d\n", rc);
	return rank;
}

#define DF_DB		DF_UUID"["DF_RANK"]"
#define DP_DB(db)	DP_UUID(db->d_uuid), DP_RANK()

extern struct rdb *the_one_rdb_hack;

/* rdb_raft.c *****************************************************************/

/* Per-raft_node_t data */
struct rdb_raft_node {
	crt_rank_t	dn_rank;
};

int rdb_raft_init(daos_handle_t rdb_attr);
int rdb_raft_start(struct rdb *db, const daos_rank_list_t *replicas);
void rdb_raft_stop(struct rdb *db);
int rdb_raft_verify_leadership(struct rdb *db);
int rdb_raft_append_apply(struct rdb *db, void *entry, size_t size,
			  void *result);
int rdb_raft_wait_applied(struct rdb *db, uint64_t index);
int rdb_requestvote_handler(crt_rpc_t *rpc);
int rdb_appendentries_handler(crt_rpc_t *rpc);
void rdb_raft_process_reply(struct rdb *db, raft_node_t *node, crt_rpc_t *rpc);

/* rdb_rpc.c ******************************************************************/

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See src/include/daos/rpc.h.
 */
enum rdb_operation {
	RDB_REQUESTVOTE		= 1,
	RDB_APPENDENTRIES	= 2
};

extern struct daos_rpc rdb_srv_rpcs[];

int rdb_create_raft_rpc(crt_opcode_t opc, raft_node_t *node, crt_rpc_t **rpc);
int rdb_send_raft_rpc(crt_rpc_t *rpc, struct rdb *db, raft_node_t *node);
void rdb_recvd(void *arg);

/* rdb_tx.c *******************************************************************/

int rdb_tx_apply(struct rdb *db, uint64_t index, const void *buf, size_t len,
		 void *result, daos_list_t *destroyed);

/* rdb_tree.c *****************************************************************/

/* Tree handle cache entry */
struct rdb_tree {
	struct daos_llink	de_entry;	/* in LRU */
	rdb_path_t		de_path;
	daos_handle_t		de_hdl;		/* of dbtree */
	daos_list_t		de_list;	/* for rdb_tx_apply_op() */
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
