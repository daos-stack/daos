/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(tests)

#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_errno.h>
#include <daos/debug.h>
#include <daos_srv/d_vector.h>

#define SRAND_SEED 0x1234
#define ARRAY_MAX  10

struct element {
	/**
	 * - about 3-4 elements per page
	 * - odd size
	 */
	char content[D_VECTOR_SEGMENT_SIZE / 4 - 1];
};

struct state {
	struct element *array;
	d_vector_t      vec;
};

static int
setup(void **state_ptr)
{
	static struct state state;

	srand(SRAND_SEED);

	D_ALLOC_ARRAY(state.array, ARRAY_MAX);

	for (int i = 0; i < ARRAY_MAX; ++i) {
		char pattern = rand() % CHAR_MAX;
		memset(&state.array[i], pattern, sizeof(struct element));
	}

	d_vector_init(sizeof(struct element), &state.vec);

	*state_ptr = &state;

	return 0;
}

static int
teardown(void **state_ptr)
{
	struct state *state = *state_ptr;

	D_FREE(state->array);

	return 0;
}

static void
empty_vector(void **state_ptr)
{
	struct state       *state = *state_ptr;
	struct element     *entry;
	d_vector_segment_t *segment;
	uint32_t            idx;

	d_vector_for_each_entry(entry, segment, idx, &state->vec.dv_list) {
		(void)entry;
		assert_false(true);
	}
}

static const struct CMUnitTest tests_all[] = {
    {"DVEC100: empty", empty_vector, setup, teardown},
};

int
main(int argc, char **argv)
{
	const char *test_name = "d_vector_t tests";

	return cmocka_run_group_tests_name(test_name, tests_all, NULL, NULL);
}
