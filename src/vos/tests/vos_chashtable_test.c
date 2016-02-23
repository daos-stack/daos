#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <omp.h>
#include <inttypes.h>
#include "vos_chash_table.h"


int
compare_integers(const void *a, const void *b)
{
	if (*(uint64_t *)a == *(uint64_t *)b)
		return 0;
	else
		return -1;
}

void
print_integer_keys(const void *a)
{
	printf("Key: %"PRIu64"\t", *(uint64_t *)a);
}

void
print_integer_values(void *a)
{
	printf("Value: %"PRIu64"\n", *(uint64_t *)a);
}

bool
file_exists(const char *filename)
{
	FILE *fp;
	fp = fopen(filename, "r");
	if (fp) {
		fclose(fp);
		return true;
	}
	return false;
}


static int
test_multithreaded_ops(PMEMobjpool *pop, int bucket_size, int num_keys,
			 int num_threads)
{

	uint64_t i, *keys = NULL, *values = NULL;
	int ret = 0;
	TOID(struct vos_chash_table) hashtable;

	keys = (uint64_t *) malloc(num_keys * sizeof(uint64_t));
	if (NULL == keys) {
		fprintf(stderr, "Error in allocating keys array\n");
		return -1;
	}
	memset(keys, 0, num_keys * sizeof(uint64_t));

	values = (uint64_t *) malloc(num_keys * sizeof(uint64_t));
	if (NULL == values) {
		fprintf(stderr, "Error in allocating values array\n");
		return -1;
	}
	memset(values, 0, num_keys * sizeof(uint64_t));

	vos_chash_create(pop, bucket_size, 100, true, CRC64, &hashtable,
			 compare_integers, print_integer_keys,
			 print_integer_values);

	#pragma omp parallel num_threads(num_threads)
	{
		#pragma omp for
		for (i = 0; i < num_keys; i++) {
			keys[i] = rand() % 100000 + 1;
			values[i] = rand() % 10;
			ret = vos_chash_insert(pop, hashtable,
					       (void *)(keys + i),
					       sizeof(keys + i),
					       (void *)(values + i),
					       sizeof(values + i));
			if (ret)
				printf("Insert failed\n");
		}
	}

	printf("Success: Compeleted Inserts\n");
	vos_chash_print(pop, hashtable);
	printf("Success: Compeleted Printing the hash table\n");
	printf("************************************************\n");

	#pragma omp parallel num_threads(num_threads)
	{

		void *value_ret = NULL;
		#pragma omp for
		for (i = 0; i < num_keys; i++) {
			ret = vos_chash_lookup(pop, hashtable,
					(void *)(keys + i),
					sizeof(keys + i),
					&value_ret);
			if (!ret &&  (value_ret != NULL)) {
				if (values[i] != *(uint64_t *)value_ret) {
					fprintf(stderr,
						"%"PRIu64"->%"PRIu64"\n",
						values[i],
						*(uint64_t *)value_ret);
				}
			} else
				fprintf(stderr, "NULL value with ret: %d\n",
					ret);
		}

		if (omp_get_thread_num() == 0)
			printf("Success: Compeleted Lookups\n");

		if (omp_get_thread_num() == 1) {
			ret = vos_chash_remove(pop, hashtable,
					(void *)(keys + 1),
					sizeof(keys + 1));
			if (ret)
				fprintf(stderr, "Error in Remove %d\n",
					omp_get_thread_num());
		}
		if (omp_get_thread_num() == 4) {
			ret = vos_chash_remove(pop,
					hashtable,
					(void *)(keys + 3),
					sizeof(keys + 3));
			if (ret)
				fprintf(stderr, "Error in Remove %d\n",
					omp_get_thread_num());
		}
	}
	printf("Success: Compeleted Removes\n");
	vos_chash_print(pop, hashtable);
	printf("************************************************\n");

	vos_chash_destroy(pop, hashtable);
	printf("Success: Compeleted detroy\n");
	free(keys);
	free(values);

	return ret;
}

static int
test_single_thread_ops(PMEMobjpool *pop, int bucket_size, int num_keys)
{

	void *value_ret = NULL;
	uint64_t i, *keys = NULL, *values = NULL;
	int ret = 0;
	TOID(struct vos_chash_table) hashtable;


	keys = (uint64_t *) malloc(num_keys * sizeof(uint64_t));
	if (NULL == keys) {
		fprintf(stderr, "Error in allocating keys array\n");
		return -1;
	}
	memset(keys, 0, num_keys * sizeof(uint64_t));
	values = (uint64_t *) malloc(num_keys * sizeof(uint64_t));
	if (NULL == values) {
		fprintf(stderr, "Error in allocating values array\n");
		return -1;
	}
	memset(values, 0, num_keys * sizeof(uint64_t));

	vos_chash_create(pop, bucket_size, 100, true, CRC64, &hashtable,
			 compare_integers, print_integer_keys,
			 print_integer_values);

	for (i = 0; i < num_keys; i++) {
		keys[i] = rand() % 100000 + 1;
		values[i] = rand() % 10;
		ret = vos_chash_insert(pop, hashtable,
				       (void *)(keys + i),
				       sizeof(keys + i),
				       (void *)(values + i),
				       sizeof(values + i));
		if (ret)
			return ret;
	}
	printf("Success: Compeleted Inserts\n");
	vos_chash_print(pop, hashtable);
	printf("Success: Compeleted Printing the hash table\n");
	printf("************************************************\n");

	for (i = 0; i < num_keys; i++) {
		ret = vos_chash_lookup(pop, hashtable,
				       (void *)(keys + i),
				       sizeof(keys + i),
				       &value_ret);
		if (!ret &&  (value_ret != NULL)) {
			if (values[i] != *(uint64_t *)value_ret) {
				fprintf(stderr,
					"Expected %"PRIu64" got %"PRIu64"\n",
					values[i], *(uint64_t *)value_ret);
			}
		} else{
			fprintf(stderr, "NULL value\n");
			return ret;
		}
	}
	printf("Success: Compeleted Lookups\n");
	fflush(stdout);

	ret = vos_chash_remove(pop, hashtable, (void *)(keys + 1),
			       sizeof(keys + 1));
	if (ret)
		return ret;
	ret = vos_chash_remove(pop, hashtable, (void *)(keys + 3),
			       sizeof(keys + 3));
	if (ret)
		return ret;
	printf("Success: Compeleted Removes\n");
	vos_chash_print(pop, hashtable);
	printf("************************************************\n");


	vos_chash_destroy(pop, hashtable);
	printf("Success: Compeleted detroy\n");
	free(keys);
	free(values);

	return ret;
}


int main(int argc, char *argv[])
{
	int bucket_size, num_keys;
	int isthread, num_threads;
	PMEMobjpool *pop;
	int ret = 0;

	if (argc < 4) {
		fprintf(stderr,
			"<exec> <bucket_size> <num_keys> <threads> <count>\n");
		return -1;
	}

	bucket_size = atoi(argv[1]);
	num_keys = atoi(argv[2]);
	isthread = atoi(argv[3]);
	if (file_exists("/mnt/pmem_store/test_hash_table"))
		remove("/mnt/pmem_store/test_hash_table");
	pop = pmemobj_create("/mnt/pmem_store/test_hash_table",
			     "hashtable test", 10737418240, 0666);

	if (argc == 5)
		num_threads = atoi(argv[4]);
	else
		num_threads = 8;

	if (!atoi(argv[3]))
		ret = test_single_thread_ops(pop, bucket_size, num_keys);
	else
		ret = test_multithreaded_ops(pop, bucket_size,
					       num_keys, num_threads);
	return ret;
}
