/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <daos/common.h>
#include <daos/lru.h>

/** integer key reference */
struct uint_ref {
	struct daos_llink	ur_llink;
	uint64_t		ur_key;
};

void
uint_ref_lru_free(struct daos_llink *llink)
{
	struct uint_ref	*ref;

	D_ASSERT(llink);
	D_PRINT("Freeing LRU ref from uint_ref cb\n");
	ref = container_of(llink, struct uint_ref, ur_llink);
	D_FREE(ref);
}

int
uint_ref_lru_alloc(void *key, unsigned int ksize,
		   void *args, struct daos_llink **link)
{
	struct uint_ref *ref;

	D_ALLOC_PTR(ref);
	if (ref == NULL) {
		D_ERROR("Error in allocating lru_refs");
		return -DER_NOMEM;
	}
	ref->ur_key = *(uint64_t *)key;
	*link = &ref->ur_llink;

	return 0;
}

bool
uint_ref_lru_cmp(const void *key, unsigned int ksize,
		 struct daos_llink *llink)
{
	struct uint_ref	*ref = container_of(llink, struct uint_ref, ur_llink);

	D_ASSERT(ksize == sizeof(uint64_t));

	return (ref->ur_key == *(uint64_t *)key);
}

uint32_t
uint_ref_lru_hash(struct daos_llink *llink)
{
	struct uint_ref	*ref = container_of(llink, struct uint_ref, ur_llink);

	return d_hash_string_u32((const char *)&ref->ur_key,
				 sizeof(ref->ur_key));
}

struct daos_llink_ops uint_ref_llink_ops = {
	.lop_free_ref	= uint_ref_lru_free,
	.lop_alloc_ref	= uint_ref_lru_alloc,
	.lop_cmp_keys	= uint_ref_lru_cmp,
	.lop_rec_hash	= uint_ref_lru_hash,
};

static inline int
test_ref_hold(struct daos_lru_cache *cache,
	      struct daos_llink **link, void *key,
	      unsigned int size)
{
	int rc = 0;
	struct uint_ref *refs;

	rc = daos_lru_ref_hold(cache, key, size, (void *)1, link);
	if (rc)
		D_ERROR("Error in holding reference\n");

	refs = container_of(*link, struct uint_ref, ur_llink);

	D_ASSERT(refs->ur_key == *(uint64_t *)key);

	D_PRINT("Completed ref hold for key: %"PRIu64"\n",
	       *(uint64_t *)key);

	return rc;
}


int
main(int argc, char **argv)
{
	int			rc, num_keys, i, j;
	uint64_t		*keys;
	struct daos_llink	*link_ret[3] = {NULL};
	struct daos_lru_cache	*tcache = NULL;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		return rc;

	if (argc < 3) {
		D_ERROR("<exec><size bits(^2)><num_keys>\n");
		exit(-1);
	}

	rc = daos_lru_cache_create(atoi(argv[1]), D_HASH_FT_RWLOCK,
				   &uint_ref_llink_ops,
				   &tcache);
	if (rc)
		D_ASSERTF(0, "Error in creating lru cache\n");

	num_keys = atoi(argv[2]);
	D_ALLOC_ARRAY(keys, (num_keys + 2));
	if (keys == NULL)
		D_ASSERTF(0, "Error in allocating keys_array\n");

	keys[0] = 0; keys[1] = 1;

	/** Just for testing
	 *  Lets make first two to be busy!
	 */
	rc = test_ref_hold(tcache, &link_ret[0], &keys[0],
			   sizeof(uint64_t));
	if (rc)
		D_GOTO(exit, rc);

	rc = test_ref_hold(tcache, &link_ret[1], &keys[1],
			   sizeof(uint64_t));
	if (rc)
		D_GOTO(exit, rc);

	for (j = 0, i = 2; i < num_keys+2; i++, j++) {

		keys[i] =  j;
		D_PRINT("Hold and release for %d\n", j);
		link_ret[2] = NULL;
		rc = test_ref_hold(tcache, &link_ret[2], &keys[i],
				   sizeof(uint64_t));
		if (rc)
			D_GOTO(exit, rc);

		daos_lru_ref_release(tcache, link_ret[2]);
		D_PRINT("Completed ref release for key: %d\n", j);
	}

	daos_lru_ref_release(tcache, link_ret[0]);
	D_PRINT("Completed ref release for key: %"PRIu64"\n",
		keys[0]);
	daos_lru_ref_release(tcache, link_ret[1]);
	D_PRINT("Completed ref release for key: %"PRIu64"\n",
		keys[1]);
exit:
	daos_lru_cache_destroy(tcache);
	D_FREE(keys);

	daos_debug_fini();
	return rc;
}
