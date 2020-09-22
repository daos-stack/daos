/*
 * (C) Copyright 2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#define D_LOGFAC	DD_FAC(mem)

#include <pthread.h>
#include <gurt/list.h>
#include <gurt/common.h>

#define MM_MAGIC	0xa5a55a5a
#define MM_MIN_SIZE	0x80	/* 128 bytes */
#define MM_MAX_NBUCKETS	10

struct mm_item {
	d_list_t	 mi_link;
	uint32_t	 mi_bucket;
	uint32_t	 mi_magic;
	char		 mi_buff[0];
};

struct mm_bucket {
	d_list_t	   mb_head;
	pthread_spinlock_t mb_lock;
	uint32_t	   mb_count;
};

struct mm_manager {
	struct mm_bucket *mm_buckets;
	uint32_t	  mm_count;
};

static struct mm_manager dmm;

int
d_mm_init(size_t n)
{
	struct mm_bucket *bucket;
	size_t i;
	int rc;

	if (unlikely(n < 2 || n >= MM_MAX_NBUCKETS))
		D_GOTO(out, rc = -DER_INVAL);

	if (unlikely(dmm.mm_count != 0))
		D_GOTO(out, rc = -DER_ALREADY);

	dmm.mm_buckets = malloc(n * sizeof(*dmm.mm_buckets));
	if (unlikely(dmm.mm_buckets == NULL))
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < n; i++) {
		bucket = &dmm.mm_buckets[i];
		bucket->mb_count = 0;
		D_INIT_LIST_HEAD(&bucket->mb_head);
		rc = D_SPIN_INIT(&bucket->mb_lock, PTHREAD_PROCESS_PRIVATE);
		if (unlikely(rc))
			D_GOTO(out_free, rc);
	}
	dmm.mm_count = n;
	D_GOTO(out, rc = 0);

out_free:
	while (i > 0) {
		bucket = &dmm.mm_buckets[--i];
		D_SPIN_DESTROY(&bucket->mb_lock);
	}
	free(dmm.mm_buckets);
out:
	return rc;
}

void
d_mm_fini(void)
{
	struct mm_bucket *bucket;
	size_t i, count = dmm.mm_count;

	if (unlikely(count == 0))
		return;

	d_mm_flush();
	dmm.mm_count = 0;
	for (i = 0; i < count; i++) {
		bucket = &dmm.mm_buckets[i];
		D_ASSERT(bucket->mb_count == 0);
		D_SPIN_DESTROY(&bucket->mb_lock);
	}
	free(dmm.mm_buckets);
}

void *
d_mm_alloc(size_t size)
{
	struct mm_bucket *bucket;
	struct mm_item *item = NULL;
	size_t i, nsize;

	size += sizeof(struct mm_item);
	for (i = 0, nsize = MM_MIN_SIZE; nsize < size; i++)
		nsize <<= 1;

	if (likely(i < dmm.mm_count)) {
		bucket = &dmm.mm_buckets[i];
		D_SPIN_LOCK(&bucket->mb_lock);
		item = d_list_pop_entry(&bucket->mb_head,
					struct mm_item, mi_link);
		if (likely(item != NULL))
			bucket->mb_count--;
		D_SPIN_UNLOCK(&bucket->mb_lock);
	}

	if (unlikely(item == NULL)) {
		item = malloc(nsize);
		if (unlikely(item == NULL))
			return NULL;
		memset(&item->mi_buff[0], 0, size - sizeof(struct mm_item));
	}

	item->mi_magic  = MM_MAGIC;
	item->mi_bucket = i;

	return &item->mi_buff[0];
}

void *
d_mm_realloc(void *ptr, size_t size)
{
	struct mm_item *item = container_of(ptr, struct mm_item, mi_buff);
	void *ptr2;
	size_t osize;


	if (unlikely(ptr == NULL))
		return d_mm_alloc(size);

	D_ASSERT(item->mi_magic == MM_MAGIC);

	osize = (MM_MIN_SIZE << item->mi_bucket) - sizeof(struct mm_item);
	if (size <= osize)
		return ptr;

	ptr2 = d_mm_alloc(size);
	if (unlikely(ptr2 == NULL))
		return realloc(ptr, size);

	memmove(ptr2, ptr, osize);

	d_mm_free(ptr);
	return ptr2;
}

void
d_mm_free(void *ptr)
{
	struct mm_bucket *bucket;
	struct mm_item *item = container_of(ptr, struct mm_item, mi_buff);
	size_t i, nsize;

	if (unlikely(ptr == NULL))
		return;

	D_ASSERT(item->mi_magic == MM_MAGIC);

	if (unlikely(item->mi_bucket >= dmm.mm_count)) {
		free(item);
		return;
	}

	i = item->mi_bucket;
	nsize = (MM_MIN_SIZE << i) - sizeof(struct mm_item);
	memset(&item->mi_buff[0], 0, nsize);

	bucket = &dmm.mm_buckets[i];
	D_SPIN_LOCK(&bucket->mb_lock);
	d_list_add(&item->mi_link, &bucket->mb_head);
	bucket->mb_count++;
	D_SPIN_UNLOCK(&bucket->mb_lock);
}

void
d_mm_flush(void)
{
	struct mm_bucket *bucket;
	struct mm_item *item;
	size_t i, count = dmm.mm_count;

	if (unlikely(count == 0))
		return;

	for (i = 0; i < count; i++) {
		bucket = &dmm.mm_buckets[i];
		D_SPIN_LOCK(&bucket->mb_lock);
		while ((item = d_list_pop_entry(&bucket->mb_head,
						struct mm_item, mi_link))) {
			bucket->mb_count--;
			D_SPIN_UNLOCK(&bucket->mb_lock);
			free(item);
			D_SPIN_LOCK(&bucket->mb_lock);
		}
		D_SPIN_UNLOCK(&bucket->mb_lock);
	}
}
