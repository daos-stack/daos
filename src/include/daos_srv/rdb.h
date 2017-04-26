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
 * rdb: Replicated Database
 */

#ifndef DAOS_SRV_RDB_H
#define DAOS_SRV_RDB_H

#include <daos_types.h>
#include <daos/btree_class.h>

/** Database (opaque) */
struct rdb;

/** Database callbacks */
struct rdb_cbs {
	/**
	 * Called when this replica becomes the leader of \a term. Must not
	 * Argobots-block. A replicated service over rdb may want to take the
	 * chance to start itself on this replica.
	 */
	void (*dc_step_up)(struct rdb *db, uint64_t term, void *arg);

	/**
	 * Called when this replica steps down as the leader of \a term. Must
	 * not Argobots-block. A replicated service over rdb may want to take
	 * the chance to stop itself on this replica.
	 */
	void (*dc_step_down)(struct rdb *db, uint64_t term, void *arg);
};

/**
 * Path (opaque)
 *
 * A path is a list of keys. An absolute path begins with a special key
 * (rdb_path_root_key) representing the root KVS.
 */
typedef daos_iov_t rdb_path_t;

/**
 * Root key (opaque)
 *
 * A special key representing the root KVS in a path.
 */
extern daos_iov_t rdb_path_root_key;

/** Path methods */
int rdb_path_init(rdb_path_t *path);
void rdb_path_fini(rdb_path_t *path);
int rdb_path_clone(const rdb_path_t *path, rdb_path_t *new_path);
int rdb_path_push(rdb_path_t *path, const daos_iov_t *key);

/**
 * Define a daos_iov_t object, named \a prefix + \a name, that represents a
 * constant string key. See rdb_layout.[ch] for an example of the usage of this
 * helper macro.
 */
#define RDB_STRING_KEY(prefix, name)					\
static char	prefix ## name ## _buf[] = #name;			\
daos_iov_t	prefix ## name = {					\
	.iov_buf	= prefix ## name ## _buf,			\
	.iov_buf_len	= sizeof(prefix ## name),			\
	.iov_len	= sizeof(prefix ## name)			\
}

/** KVS classes */
enum rdb_kvs_class {
	RDB_KVS_GENERIC,	/**< hash-ordered byte-stream keys */
	RDB_KVS_INTEGER		/**< ordered fixed-size integer keys */
};

#endif /* DAOS_SRV_RDB_H */
