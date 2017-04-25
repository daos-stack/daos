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

#include <daos_types.h>

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
