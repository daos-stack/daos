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
#define DDSUBSYS	DDFAC(tests)

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

	D__ASSERT(llink);
	D__PRINT("Freeing LRU ref from uint_ref cb\n");
	ref = container_of(llink, struct uint_ref, ur_llink);
	D__FREE_PTR(ref);
}

int
uint_ref_lru_alloc(void *key, unsigned int ksize,
		   void *args, struct daos_llink **link)
{
	struct uint_ref *ref;

	D__ALLOC_PTR(ref);
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
	struct uint_ref	*ref = NULL;

	ref = container_of(llink, struct uint_ref, ur_llink);
	return (ref->ur_key == *(uint64_t *)key);
}

struct daos_llink_ops uint_ref_llink_ops = {
	.lop_free_ref	= uint_ref_lru_free,
	.lop_alloc_ref	= uint_ref_lru_alloc,
	.lop_cmp_keys	= uint_ref_lru_cmp,
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

	D__ASSERT(refs->ur_key == *(uint64_t *)key);

	D__PRINT("Completed ref hold for key: %"PRIu64"\n",
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

	rc = daos_debug_init(NULL);
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
		D__ASSERTF(0, "Error in creating lru cache\n");

	num_keys = atoi(argv[2]);
	D__ALLOC(keys, ((num_keys + 2) * sizeof(uint64_t)));
	if (keys == NULL)
		D__ASSERTF(0, "Error in allocating keys_array\n");

	keys[0] = 0; keys[1] = 1;

	/** Just for testing
	 *  Lets make first two to be busy!
	 */
	rc = test_ref_hold(tcache, &link_ret[0], &keys[0],
			   sizeof(uint64_t));
	if (rc)
		D__GOTO(exit, rc);

	rc = test_ref_hold(tcache, &link_ret[1], &keys[1],
			   sizeof(uint64_t));
	if (rc)
		D__GOTO(exit, rc);

	for (j = 0, i = 2; i < num_keys+2; i++, j++) {

		keys[i] =  j;
		D__PRINT("Hold and release for %d\n", j);
		link_ret[2] = NULL;
		rc = test_ref_hold(tcache, &link_ret[2], &keys[i],
				   sizeof(uint64_t));
		if (rc)
			D__GOTO(exit, rc);

		daos_lru_ref_release(tcache, link_ret[2]);
		D__PRINT("Completed ref release for key: %d\n", j);
	}

	daos_lru_ref_release(tcache, link_ret[0]);
	D__PRINT("Completed ref release for key: %"PRIu64"\n",
		keys[0]);
	daos_lru_ref_release(tcache, link_ret[1]);
	D__PRINT("Completed ref release for key: %"PRIu64"\n",
		keys[1]);
exit:
	daos_lru_cache_destroy(tcache);
	if (keys)
		D__FREE(keys, ((num_keys+2) * sizeof(int)));

	daos_debug_fini();
	return rc;
}
