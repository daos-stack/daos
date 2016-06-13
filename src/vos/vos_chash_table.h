/**
 * (C) Copyright 2016 Intel Corporation.
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
 * Hash-Table based on jump consistent hashing
 * vos/vos_chash_table.h
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */

#ifndef _VOS_CHASH_TABLE_H
#define _VOS_CHASH_TABLE_H

#include <stdint.h>
#include <inttypes.h>
#include <daos_types.h>
#include <libpmemobj.h>


/* Can change this based on requirement*/
#define VCH_MIN_BUCKET_SIZE 1000

/* Can change this based on requirement*/
#define VCH_MAX_BUCKET_SIZE 10000


/* Allowed number of collisions/bucket */
#define CHASH_RESIZE_COUNT 3

/* Random value to identify the type*/
#define VOS_CHASH_OFFSET 1000

typedef enum { MD5, CRC64
/* Add more hashing options here */
} vos_chashing_method_t;

struct vos_chash_table;
struct vos_chash_buckets;
struct vos_chash_table;

TOID_DECLARE(struct vos_chash_table, VOS_CHASH_OFFSET);
TOID_DECLARE(struct vos_chash_buckets, VOS_CHASH_OFFSET + 1);
TOID_DECLARE(struct vos_chash_item, VOS_CHASH_OFFSET + 2);


struct vos_chash_item {
	PMEMoid			     key;
	daos_size_t		     key_size;
	PMEMoid			     value;
	daos_size_t		     value_size;
	TOID(struct vos_chash_item)  next;
};

struct vos_chash_buckets {
	TOID(struct vos_chash_item) item;
	int			    items_in_bucket;
	PMEMrwlock		    rw_lock;
};

/** customized hash table functions */
typedef struct {
	int32_t				(*hop_key_cmp)(const void *key1,
						       const void *key2);
	void				(*hop_key_print)(const void *key);
	void				(*hop_val_print)(const void *value);
} vos_chash_ops_t;

struct vos_chash_table {
	daos_size_t			num_buckets;
	daos_size_t			max_buckets;
	vos_chashing_method_t		hashing_method;
	bool				resize;
	TOID(struct vos_chash_buckets)	buckets;
	PMEMrwlock			b_rw_lock;
	vos_chash_ops_t			*vh_ops;
};

int
vos_chash_create(PMEMobjpool *pool, uint32_t buckets,
		 uint64_t max_buckets,
		 vos_chashing_method_t hashing_method,
		 bool resize,
		 TOID(struct vos_chash_table) *chtable,
		 vos_chash_ops_t *hops);

int vos_chash_set_ops(PMEMobjpool *ph,
		      TOID(struct vos_chash_table) chtable,
		      vos_chash_ops_t *hops);

int
vos_chash_destroy(PMEMobjpool *pool, TOID(struct vos_chash_table) chtable);

int
vos_chash_insert(PMEMobjpool *pool,
		 TOID(struct vos_chash_table) chtable,
		 void *key, daos_size_t key_size,
		 void *value, daos_size_t value_size);
int
vos_chash_remove(PMEMobjpool *pool,
		 TOID(struct vos_chash_table) chtable,
		 void *key, uint64_t key_size);
int
vos_chash_lookup(PMEMobjpool *pool, TOID(struct vos_chash_table) chtable,
		 void *key, uint64_t key_size, void **value);
int
vos_chash_print(PMEMobjpool *pool, TOID(struct vos_chash_table) chtable);

#endif
