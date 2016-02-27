/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * Multithreaded Hash-Table based on jump consistent hashing
 *
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */

#include <math.h>
#include <smmintrin.h>
#include <daos/daos_errno.h>
#include <daos/daos_common.h>
#include "vos_chash_table.h"

/*
 * Simple CRC64 hash with intrinsics
 * Should be eventually replaced with hash_function from ISA-L
*/
static uint64_t
vos_chash_generate_crc64(TOID(struct vos_chash_table) chtable, void *key,
			 uint64_t size)
{
	uint64_t	*data = NULL, *new_key = NULL;
	uint64_t	 hash = 0xffffffffffffffff;
	int		 i, counter;

	if (size < 64) {
		new_key = (uint64_t *)malloc(64);
		/*Pad the rest of 64-bytes to 0*/
		memset(new_key, 0, 64);
		counter = 1;
	} else {
		new_key = (uint64_t *)malloc(size);
		memset(new_key, 0, size);
		counter = (int)(size/8);
	}
	memcpy(new_key, key, size);
	data = new_key;
	for (i = 0; i < counter ; i++) {
		hash = _mm_crc32_u64(hash, *data);
		data++;
	}

	D_DEBUG(DF_VOS3, "%"PRIu64"%"PRIu64" %"PRIu64"\n",
			*(uint64_t *)new_key, size, hash);
	free(new_key);

	return hash;
}

/*
 * Jump Consistent Hash from
 * A Fast, Minimal Memory, Consistent Hash Algorithm
 * http://arxiv.org/abs/1406.2294
 * takes as input a 64-bit unsigned int key and returns a
 * bucket number
*/
static int32_t
vos_chash_generate_jch(uint64_t key, uint32_t num_buckets)
{
	int64_t j = 0;
	int64_t b = 1;

	while (j < num_buckets) {
		b  = j;
		key = key * 2862933555777941757ULL + 1;
		j = (b + 1) * ((double)(1LL << 31)/(double)((key >> 33) + 1));
	}

	return b;
}

/*
 *
 * Resize the chash table with resize_buckets
 * resize_buckets can be lesser than or greater than
 * the actual number of buckets.
 *
 */
static int
vos_chash_resize_locked(PMEMobjpool *ph,
			TOID(struct vos_chash_table) chtable,
			daos_size_t resize_buckets)
{

	int				 i, rc = 0;
	uint64_t			 hash;
	daos_size_t			 num_items = 0, current_entries = 0;
	int32_t				 new_bucket_id;
	TOID(struct vos_chash_buckets)	 orig_buckets;
	TOID(struct vos_chash_buckets)	 new_buckets;
	TOID(struct vos_chash_item)	 item_current, item_entry;
	struct vos_chash_buckets	 *ob_iter = NULL;
	struct vos_chash_buckets	 *nb_iter = NULL;
	struct vos_chash_item		 *c_iter = NULL;
	void				 *ckey;
	daos_size_t			 bucket_range, buckets_size;

	orig_buckets = D_RO(chtable)->buckets;
	ob_iter = (struct vos_chash_buckets *)D_RW(orig_buckets);

	for (i = 0; i < D_RO(chtable)->num_buckets; i++)
		current_entries += ob_iter[i].items_in_bucket;
	bucket_range = (D_RO(chtable)->num_buckets *
			sizeof(struct vos_chash_buckets) +
			current_entries * sizeof(struct vos_chash_item));
	D_DEBUG(DF_VOS3, "resize to %"PRIu64" from %"PRIu64"\n",
			D_RO(chtable)->num_buckets, resize_buckets);

	TX_BEGIN(ph) {
		buckets_size = resize_buckets *
			sizeof(struct vos_chash_buckets);
		new_buckets = TX_ZALLOC(struct vos_chash_buckets,
					buckets_size);
		nb_iter = (struct vos_chash_buckets *)D_RW(new_buckets);

		pmemobj_tx_add_range(D_RO(chtable)->buckets.oid, 0,
				     bucket_range);
		TX_ADD_FIELD(chtable, num_buckets);

		for (i = 0; i < D_RO(chtable)->num_buckets; i++) {
			if (TOID_IS_NULL(ob_iter[i].item))
				continue;

			pmemobj_rwlock_wrlock(ph,
					      &ob_iter[i].rw_lock);

			num_items = ob_iter[i].items_in_bucket;
			item_current = ob_iter[i].item;

			while (!TOID_IS_NULL(item_current)) {
				item_entry = item_current;
				ckey = pmemobj_direct(D_RO(item_current)->key);
				c_iter = (struct vos_chash_item *)
					D_RW(item_current);
				hash = vos_chash_generate_crc64(chtable,
							ckey,
							c_iter->key_size);
				new_bucket_id = vos_chash_generate_jch(hash,
							resize_buckets);
				item_current = D_RW(item_current)->next;
				D_RW(item_entry)->next =
					nb_iter[new_bucket_id].item;
				nb_iter[new_bucket_id].item = item_entry;
				nb_iter[new_bucket_id].items_in_bucket += 1;
				num_items--;
			}

			if (num_items) {
				D_ERROR("Not all items were moved\n");
				pmemobj_tx_abort(0);
			}
			pmemobj_rwlock_unlock(ph,
					      &ob_iter[i].rw_lock);

		}
		D_RW(chtable)->buckets = new_buckets;
		D_RW(chtable)->num_buckets = resize_buckets;
		TX_FREE(orig_buckets);
	} TX_ONABORT {
		D_ERROR("resize ht transaction aborted: %s\n",
			pmemobj_errormsg());
		rc = -DER_NOMEM;
	} TX_END;

	return rc;
}

int
vos_chash_create(PMEMobjpool *ph, uint32_t buckets,
		 uint64_t max_buckets,
		 vos_chashing_method_t hashing_method,
		 bool resize,
		 TOID(struct vos_chash_table) *chtable,
		 int (*compare_func)(const void *key1, const void *key2),
		 void (*print_key)(const void *key),
		 void (*print_value)(void *value))
{


	int			      num_buckets, i;
	TOID(struct vos_chash_table)  hash_table;
	daos_size_t		      buckets_size;
	struct vos_chash_buckets      *tbuckets;

	num_buckets = (buckets > 0) ? buckets : VCH_MIN_BUCKET_SIZE;
	buckets_size = num_buckets * sizeof(struct vos_chash_buckets);

	TX_BEGIN(ph) {
		hash_table = TX_ZALLOC(struct vos_chash_table,
				sizeof(struct vos_chash_table));
		D_RW(hash_table)->buckets =
			TX_ZALLOC(struct vos_chash_buckets,
				buckets_size);
		tbuckets = D_RW(D_RW(hash_table)->buckets);
		for (i = 0; i < num_buckets; i++)
			pmemobj_rwlock_zero(ph, &(tbuckets[i].rw_lock));
		D_RW(hash_table)->num_buckets = num_buckets;
		D_RW(hash_table)->max_buckets = max_buckets;
		pmemobj_rwlock_zero(ph, &(D_RW(hash_table)->b_rw_lock));
		D_RW(hash_table)->resize = resize;
		D_RW(hash_table)->compare_keys = compare_func;
		D_RW(hash_table)->print_key = print_key;
		D_RW(hash_table)->print_value = print_value;
		*chtable = hash_table;
	} TX_ONABORT {
		D_ERROR("create hashtable transaction aborted: %s\n",
			pmemobj_errormsg());
		return -DER_NOMEM;
	} TX_END

	return 0;
}

int
vos_chash_insert(PMEMobjpool *ph,
		 TOID(struct vos_chash_table) chtable,
		 void *key, daos_size_t key_size,
		 void *value, daos_size_t value_size)
{

	int				bucket_id = 0;
	int				rc = 0;
	bool				resize_req = false;
	uint64_t			hash_value;
	TOID(struct vos_chash_item)	iter, newpair;
	struct vos_chash_buckets	*buckets;
	void				*ckey, *cvalue;
	daos_size_t			bucket_range, resize_cnt;

	if (TOID_IS_NULL(chtable)) {
		D_ERROR("Table Does not exist\n");
		return DER_NONEXIST;
	}

	pmemobj_rwlock_rdlock(ph, &D_RW(chtable)->b_rw_lock);
	hash_value  = vos_chash_generate_crc64(chtable, key, key_size);
	bucket_id = vos_chash_generate_jch(hash_value,
					D_RO(chtable)->num_buckets);
	buckets = (struct vos_chash_buckets *)D_RW(D_RW(chtable)->buckets);
	bucket_range = sizeof(struct vos_chash_buckets) +
		       buckets[bucket_id].items_in_bucket *
		       sizeof(struct vos_chash_item);
	iter = buckets[bucket_id].item;

	while (!TOID_IS_NULL(iter)) {
		ckey = pmemobj_direct(D_RO(iter)->key);
		if (!(D_RO(chtable)->compare_keys(ckey, key)))
			return DER_EXIST;
		iter = D_RO(iter)->next;
	}

	TX_BEGIN(ph) {
		pmemobj_rwlock_wrlock(ph, &buckets[bucket_id].rw_lock);
		newpair = TX_NEW(struct vos_chash_item);
		D_RW(newpair)->key =  pmemobj_tx_zalloc(key_size, 0);
		D_RW(newpair)->value = pmemobj_tx_zalloc(value_size, 0);
		D_RW(newpair)->key_size = key_size;
		D_RW(newpair)->value_size = value_size;
		ckey = pmemobj_direct(D_RW(newpair)->key);
		TX_MEMCPY(ckey, key, key_size);
		cvalue = pmemobj_direct(D_RW(newpair)->value);
		TX_MEMCPY(cvalue, value, value_size);
		TX_ADD_FIELD(chtable, buckets);
		pmemobj_tx_add_range_direct((void *)(buckets + bucket_id),
					     bucket_range);
		D_RW(newpair)->next = buckets[bucket_id].item;
		buckets[bucket_id].item = newpair;
		buckets[bucket_id].items_in_bucket++;
		/* Determine if Resize Needed */
		if (D_RO(chtable)->resize &&
		   (D_RO(chtable)->num_buckets < D_RO(chtable)->max_buckets)) {
			resize_cnt =  2 * D_RO(chtable)->num_buckets;
			if ((buckets[bucket_id].items_in_bucket >=
			     CHASH_RESIZE_COUNT)
			    && (resize_cnt <= D_RO(chtable)->max_buckets))
				resize_req = true;
		}
		pmemobj_rwlock_unlock(ph, &buckets[bucket_id].rw_lock);
	} TX_ONABORT{
		D_ERROR("insert hashtable transaction aborted: %s\n",
			pmemobj_errormsg());
		rc = -DER_NOMEM;

	} TX_END;

	pmemobj_rwlock_unlock(ph, &D_RW(chtable)->b_rw_lock);

	/* Trigger resize here*/
	if (resize_req) {
		pmemobj_rwlock_wrlock(ph, &D_RW(chtable)->b_rw_lock);
		resize_cnt = 2 * D_RO(chtable)->num_buckets;
		if ((buckets[bucket_id].items_in_bucket >= CHASH_RESIZE_COUNT)
		    && (resize_cnt <= D_RO(chtable)->max_buckets)) {
			D_DEBUG(DF_VOS3, "buckets:%"PRIu64" itb: %d\n",
				D_RO(chtable)->num_buckets,
				buckets[bucket_id].items_in_bucket);
			vos_chash_resize_locked(ph, chtable, resize_cnt);
		}
		pmemobj_rwlock_unlock(ph, &D_RW(chtable)->b_rw_lock);
	}
	return rc;
}

int
vos_chash_lookup(PMEMobjpool *ph, TOID(struct vos_chash_table) chtable,
		 void *key, uint64_t key_size, void **value)
{

	uint64_t		      hash_value;
	TOID(struct vos_chash_item)   iter;
	struct vos_chash_buckets      *buckets;
	int32_t			      bucket_id, ret;
	void			      *ckey;

	hash_value = vos_chash_generate_crc64(chtable, key, key_size);
	bucket_id = vos_chash_generate_jch(hash_value,
					D_RO(chtable)->num_buckets);
	buckets = (struct vos_chash_buckets *)D_RW(D_RW(chtable)->buckets);
	pmemobj_rwlock_rdlock(ph, &D_RW(chtable)->b_rw_lock);
	pmemobj_rwlock_rdlock(ph, &buckets[bucket_id].rw_lock);
	iter = buckets[bucket_id].item;
	while (!TOID_IS_NULL(iter)) {
		ckey = pmemobj_direct(D_RO(iter)->key);
		if (!(D_RO(chtable)->compare_keys(ckey, key))) {
			*value = pmemobj_direct(D_RO(iter)->value);
			ret = 0;
			goto exit;
		}
		iter = D_RO(iter)->next;
	}
	ret = -1; /*Key not found*/
	*value = NULL;
exit:
	pmemobj_rwlock_unlock(ph, &buckets[bucket_id].rw_lock);
	pmemobj_rwlock_unlock(ph, &D_RW(chtable)->b_rw_lock);
	return ret;
}

int
vos_chash_remove(PMEMobjpool *ph, TOID(struct vos_chash_table) chtable,
		 void *key, uint64_t key_size)
{

	int					rc = 0;
	struct vos_chash_buckets		*buckets;
	TOID(struct vos_chash_item)		item_current;
	TOID(struct vos_chash_item)		item_prev;
	TOID(struct vos_chash_item)		item_next;
	daos_size_t				bucket_range;
	uint64_t				hash_value;
	int32_t					bucket_id;
	void					*ckey = NULL;
	bool					resize_req = false;

	hash_value = vos_chash_generate_crc64(chtable, key, key_size);
	pmemobj_rwlock_rdlock(ph, &D_RW(chtable)->b_rw_lock);
	bucket_id = vos_chash_generate_jch(hash_value,
					   D_RO(chtable)->num_buckets);
	buckets = (struct vos_chash_buckets *)D_RO(D_RO(chtable)->buckets);
	bucket_range = sizeof(struct vos_chash_buckets) +
		       buckets[bucket_id].items_in_bucket *
		       sizeof(struct vos_chash_item);

	item_prev = TOID_NULL(struct vos_chash_item);
	item_current = TOID_NULL(struct vos_chash_item);
	item_current = buckets[bucket_id].item;
	while (!TOID_IS_NULL(item_current)) {
		ckey = pmemobj_direct(D_RO(item_current)->key);
		if (!(D_RO(chtable)->compare_keys(ckey, key)))
			break;
		item_prev = item_current;
		item_current = D_RO(item_current)->next;
	}

	if (TOID_IS_NULL(item_current))
		return DER_NONEXIST;

	TX_BEGIN(ph) {
		pmemobj_rwlock_wrlock(ph, &buckets[bucket_id].rw_lock);
		item_next = D_RW(item_current)->next;
		if (TOID_IS_NULL(item_prev)) {
			TX_ADD_FIELD(chtable, buckets);
			pmemobj_tx_add_range_direct((void *)
						(buckets + bucket_id),
						bucket_range);
			buckets[bucket_id].item = item_next;

		} else {
			TX_ADD_FIELD(item_prev, next);
			D_RW(item_prev)->next = item_next;
		}
		buckets[bucket_id].items_in_bucket--;
		pmemobj_tx_free(D_RW(item_current)->key);
		pmemobj_tx_free(D_RW(item_current)->value);
		TX_FREE(item_current);

		/* Determine if we need resize*/
		if (D_RO(chtable)->resize &&
		   (D_RO(chtable)->num_buckets < D_RO(chtable)->max_buckets)) {
			if (!buckets[bucket_id].items_in_bucket)
				resize_req = true;
		}
		pmemobj_rwlock_unlock(ph, &buckets[bucket_id].rw_lock);
	} TX_ONABORT{
		D_ERROR("remove transaction aborted: %s\n",
			pmemobj_errormsg());
		rc = -DER_FREE_MEM;
	} TX_END;
	pmemobj_rwlock_unlock(ph, &D_RW(chtable)->b_rw_lock);

	/* Trigger Resize */
	if (resize_req) {
		pmemobj_rwlock_wrlock(ph, &D_RW(chtable)->b_rw_lock);
		/* Reduce number of buckets by 1 */
		if (!buckets[bucket_id].items_in_bucket &&
		    (D_RW(chtable)->num_buckets - 1))
			vos_chash_resize_locked(ph, chtable,
						D_RW(chtable)->num_buckets - 1);
		pmemobj_rwlock_unlock(ph, &D_RW(chtable)->b_rw_lock);
	}
	return rc;
}

int
vos_chash_print(PMEMobjpool *ph, TOID(struct vos_chash_table) chtable)
{

	int					i;
	TOID(struct vos_chash_item)		item_current;
	struct vos_chash_buckets		*buckets;
	struct vos_chash_item			*l_iter;
	void					*pkey, *pvalue;

	if (TOID_IS_NULL(chtable) || TOID_IS_NULL(D_RO(chtable)->buckets)) {
			D_ERROR("Empty Table\n");
			return DER_NONEXIST;
	}

	buckets = (struct vos_chash_buckets *)D_RO(D_RO(chtable)->buckets);
	D_DEBUG(DF_VOS3, "num_buckets: %"PRIu64"\n",
			D_RO(chtable)->num_buckets);
	for (i = 0; i < D_RO(chtable)->num_buckets; i++) {
		if (!TOID_IS_NULL(buckets[i].item)) {
			item_current = buckets[i].item;
			fprintf(stdout, "Bucket: %d\n", i);
			while (!TOID_IS_NULL(item_current)) {
				l_iter = (struct vos_chash_item *)
					 D_RO(item_current);
				pkey = pmemobj_direct(l_iter->key);
				pvalue = pmemobj_direct(l_iter->value);
				D_RO(chtable)->print_key(pkey);
				D_RO(chtable)->print_value(pvalue);
				item_current = D_RO(item_current)->next;
			}
			printf("\n");
		}
	}
	return 0;
}

int
vos_chash_destroy(PMEMobjpool *ph, TOID(struct vos_chash_table) chtable)
{

	int				i, rc = 0;
	struct vos_chash_buckets	*buckets;
	TOID(struct vos_chash_item)	c_item, f_item;

	if (TOID_IS_NULL(chtable) || TOID_IS_NULL(D_RO(chtable)->buckets)) {
			D_ERROR("Empty Table, nothing to destroy\n");
			return -DER_NONEXIST;
	}

	buckets = (struct vos_chash_buckets *)D_RO(D_RO(chtable)->buckets);

	TX_BEGIN(ph) {
		i = 0;
		while ((i < D_RO(chtable)->num_buckets) &&
			(!TOID_IS_NULL(buckets[i].item))) {
			c_item = buckets[i].item;
			while (!TOID_IS_NULL(c_item)) {
				pmemobj_tx_free(D_RW(c_item)->key);
				pmemobj_tx_free(D_RW(c_item)->value);
				f_item = c_item;
				c_item = D_RW(c_item)->next;
				TX_FREE(f_item);
			}
			i++;
		}
		TX_FREE(D_RO(chtable)->buckets);
		TX_FREE(chtable);
	} TX_ONABORT{
		D_ERROR("insert hashtable transaction aborted: %s\n",
			pmemobj_errormsg());
		rc = -DER_FREE_MEM;
	} TX_END;
	return rc;
}
