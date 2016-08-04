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
	D_FREE_PTR(ref);
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

	rc = daos_lru_ref_hold(cache, key, size, NULL, link);
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


	if (argc < 3) {
		D_ERROR("<exec><size bits(^2)><num_keys>\n");
		exit(-1);
	}

	rc = daos_lru_cache_create(DHASH_FT_RWLOCK,
				   atoi(argv[1]), &uint_ref_llink_ops,
				   &tcache);
	if (rc)
		D_FATAL(rc = EINVAL, "Error in creating lru cache\n");

	num_keys = atoi(argv[2]);
	D_ALLOC(keys, ((num_keys + 2) * sizeof(uint64_t)));
	if (keys == NULL)
		D_FATAL(rc = ENOMEM, "Error in allocating keys_array\n");
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
	if (keys)
		D_FREE(keys, ((num_keys+2) * sizeof(int)));

	return rc;
}
