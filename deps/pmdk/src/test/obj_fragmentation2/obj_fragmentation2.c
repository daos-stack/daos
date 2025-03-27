// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2017-2020, Intel Corporation */

/*
 * obj_fragmentation.c -- measures average heap external fragmentation
 *
 * This test is based on the workloads proposed in:
 *	Log-structured Memory for DRAM-based Storage
 *	by Stephen M. Rumble, Ankita Kejriwal, and John Ousterhout
 *
 * https://www.usenix.org/system/files/conference/fast14/fast14-paper_rumble.pdf
 */

#include <stdlib.h>
#include <math.h>
#include "rand.h"
#include "unittest.h"

#define LAYOUT_NAME "obj_fragmentation"

#define MEGABYTE (1ULL << 20)
#define GIGABYTE (1ULL << 30)

#define RRAND(max, min)\
((min) == (max) ? (min) : (rnd64() % ((max) - (min)) + (min)))

static PMEMoid *objects;
static size_t nobjects;
static size_t allocated_current;
#define MAX_OBJECTS (200ULL * 1000000)

#define ALLOC_TOTAL (5000ULL * MEGABYTE)
#define ALLOC_CURR (1000 * MEGABYTE)
#define FREES_P 200

#define DEFAULT_FILE_SIZE (3 * GIGABYTE)

static void
shuffle_objects(size_t start, size_t end)
{
	PMEMoid tmp;
	size_t dest;
	for (size_t n = start; n < end; ++n) {
		dest = RRAND(nobjects - 1, 0);
		tmp = objects[n];
		objects[n] = objects[dest];
		objects[dest] = tmp;
	}
}

static PMEMoid
remove_last()
{
	UT_ASSERT(nobjects > 0);

	PMEMoid obj = objects[--nobjects];

	return obj;
}

static void
delete_objects(PMEMobjpool *pop, float pct)
{
	size_t nfree = (size_t)(nobjects * pct);

	PMEMoid oid = pmemobj_root(pop, 1);

	shuffle_objects(0, nobjects);
	while (nfree--) {
		oid = remove_last();

		allocated_current -= pmemobj_alloc_usable_size(oid);

		pmemobj_free(&oid);
	}
}

/*
 * object_next_size -- generates random sizes in range with
 *	exponential distribution
 */
static size_t
object_next_size(size_t max, size_t min)
{
	float fmax = (float)max;
	float fmin = (float)min;

	float n = (float)rnd64() / ((float)UINT64_MAX / 1.0f);
	return (size_t)(fmin + (fmax - fmin) * (float)exp(n * - 4.0));
}

/*
 * allocate_exponential -- allocates objects from a large range of sizes.
 *
 * This is designed to stress the recycler subsystem that will have to
 * constantly look for freed/empty runs and reuse them.
 *
 * For small pools (single digit gigabytes), this test will show large
 * fragmentation because it can use a large number of runs - which is fine.
 */
static void
allocate_exponential(PMEMobjpool *pop, size_t size_min, size_t size_max)
{
	size_t allocated_total = 0;

	PMEMoid oid;

	while (allocated_total < ALLOC_TOTAL) {
		size_t s = object_next_size(size_max, size_min);
		int ret = pmemobj_alloc(pop, &oid, s, 0, NULL, NULL);
		if (ret != 0) {
			/* delete a random percentage of allocated objects */
			float delete_pct = (float)RRAND(90, 10) / 100.0f;
			delete_objects(pop, delete_pct);
			continue;
		}

		s = pmemobj_alloc_usable_size(oid);

		objects[nobjects++] = oid;
		UT_ASSERT(nobjects < MAX_OBJECTS);
		allocated_total += s;
		allocated_current += s;
	}
}

static void
allocate_objects(PMEMobjpool *pop, size_t size_min, size_t size_max)
{
	size_t allocated_total = 0;

	size_t sstart = 0;

	PMEMoid oid;

	while (allocated_total < ALLOC_TOTAL) {
		size_t s = RRAND(size_max, size_min);
		pmemobj_alloc(pop, &oid, s, 0, NULL, NULL);

		UT_ASSERTeq(OID_IS_NULL(oid), 0);
		s = pmemobj_alloc_usable_size(oid);

		objects[nobjects++] = oid;
		UT_ASSERT(nobjects < MAX_OBJECTS);
		allocated_total += s;
		allocated_current += s;

		if (allocated_current > ALLOC_CURR) {
			shuffle_objects(sstart, nobjects);
			for (int i = 0; i < FREES_P; ++i) {
				oid = remove_last();
				allocated_current -=
					pmemobj_alloc_usable_size(oid);
				pmemobj_free(&oid);
			}
			sstart = nobjects;
		}
	}
}

typedef void workload(PMEMobjpool *pop);

static void w0(PMEMobjpool *pop) {
	allocate_objects(pop, 100, 100);
}

static void w1(PMEMobjpool *pop) {
	allocate_objects(pop, 100, 100);
	allocate_objects(pop, 130, 130);
}

static void w2(PMEMobjpool *pop) {
	allocate_objects(pop, 100, 100);
	delete_objects(pop, 0.9F);
	allocate_objects(pop, 130, 130);
}

static void w3(PMEMobjpool *pop) {
	allocate_objects(pop, 100, 150);
	allocate_objects(pop, 200, 250);
}

static void w4(PMEMobjpool *pop) {
	allocate_objects(pop, 100, 150);
	delete_objects(pop, 0.9F);
	allocate_objects(pop, 200, 250);
}

static void w5(PMEMobjpool *pop) {
	allocate_objects(pop, 100, 200);
	delete_objects(pop, 0.5);
	allocate_objects(pop, 1000, 2000);
}

static void w6(PMEMobjpool *pop) {
	allocate_objects(pop, 1000, 2000);
	delete_objects(pop, 0.9F);
	allocate_objects(pop, 1500, 2500);
}

static void w7(PMEMobjpool *pop) {
	allocate_objects(pop, 50, 150);
	delete_objects(pop, 0.9F);
	allocate_objects(pop, 5000, 15000);
}

static void w8(PMEMobjpool *pop) {
	allocate_objects(pop, 2 * MEGABYTE, 2 * MEGABYTE);
}

static void w9(PMEMobjpool *pop) {
	allocate_exponential(pop, 1, 5 * MEGABYTE);
}

static workload *workloads[] = {
	w0, w1, w2, w3, w4, w5, w6, w7, w8, w9
};

static float workloads_target[] = {
	0.01f, 0.01f, 0.01f, 0.9f, 0.8f, 0.7f, 0.3f, 0.8f, 0.73f, 3.0f
};

static float workloads_defrag_target[] = {
	0.01f, 0.01f, 0.01f, 0.01f, 0.01f, 0.05f, 0.09f, 0.13f, 0.01f, 0.16f
};

/*
 * Last two workloads operates mostly on huge chunks, so run
 * stats are useless.
 */
static float workloads_stat_target[] = {
	0.01f, 1.1f, 1.1f, 0.86f, 0.76f, 1.01f, 0.23f, 1.24f, 2100.f, 2100.f
};

static float workloads_defrag_stat_target[] = {
	0.01f, 0.01f, 0.01f, 0.02f, 0.02f, 0.04f, 0.08f, 0.12f, 2100.f, 2100.f
};

int
main(int argc, char *argv[])
{
	START(argc, argv, "obj_fragmentation2");

	if (argc < 3)
		UT_FATAL("usage: %s filename workload [seed] [defrag]",
			argv[0]);

	const char *path = argv[1];

	PMEMobjpool *pop = pmemobj_create(path, LAYOUT_NAME, DEFAULT_FILE_SIZE,
				S_IWUSR | S_IRUSR);
	if (pop == NULL)
		UT_FATAL("!pmemobj_create: %s", path);

	int w = atoi(argv[2]);

	if (argc > 3)
		randomize((unsigned)atoi(argv[3]));
	else
		randomize(0);

	int defrag = argc > 4 ? atoi(argv[4]) != 0 : 0;

	objects = ZALLOC(sizeof(PMEMoid) * MAX_OBJECTS);
	UT_ASSERTne(objects, NULL);

	workloads[w](pop);

	/* this is to trigger global recycling */
	pmemobj_defrag(pop, NULL, 0, NULL);

	size_t active = 0;
	size_t allocated = 0;
	pmemobj_ctl_get(pop, "stats.heap.run_active", &active);
	pmemobj_ctl_get(pop, "stats.heap.run_allocated", &allocated);
	float stat_frag = 0;
	if (active != 0 && allocated != 0) {
		stat_frag = ((float)active / allocated) - 1.f;
		UT_ASSERT(stat_frag <= workloads_stat_target[w]);
	}

	if (defrag) {
		PMEMoid **objectsf = ZALLOC(sizeof(PMEMoid) * nobjects);
		for (size_t i = 0; i < nobjects; ++i)
			objectsf[i] = &objects[i];

		pmemobj_defrag(pop, objectsf, nobjects, NULL);

		FREE(objectsf);

		active = 0;
		allocated = 0;

		/* this is to trigger global recycling */
		pmemobj_defrag(pop, NULL, 0, NULL);

		pmemobj_ctl_get(pop, "stats.heap.run_active", &active);
		pmemobj_ctl_get(pop, "stats.heap.run_allocated", &allocated);
		if (active != 0 && allocated != 0) {
			stat_frag = ((float)active / allocated) - 1.f;
			UT_ASSERT(stat_frag <= workloads_defrag_stat_target[w]);
		}
	}

	PMEMoid oid;
	size_t remaining = 0;
	size_t chunk = (100); /* calc at chunk level */
	while (pmemobj_alloc(pop, &oid, chunk, 0, NULL, NULL) == 0)
		remaining += pmemobj_alloc_usable_size(oid) + 16;

	size_t allocated_sum = 0;
	oid = pmemobj_root(pop, 1);
	for (size_t n = 0; n < nobjects; ++n) {
		if (OID_IS_NULL(objects[n]))
			continue;
		oid = objects[n];
		allocated_sum += pmemobj_alloc_usable_size(oid) + 16;
	}
	size_t used = DEFAULT_FILE_SIZE - remaining;
	float frag = ((float)used / allocated_sum) - 1.f;

	UT_OUT("FRAG: %f\n", frag);
	UT_ASSERT(frag <= (defrag ?
		workloads_defrag_target[w] : workloads_target[w]));

	pmemobj_close(pop);

	FREE(objects);

	DONE(NULL);
}
