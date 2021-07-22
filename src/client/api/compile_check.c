/*
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <gurt/debug.h>
#include <daos.h>

#define check_string_non_literal(var, expect)						\
	D_CASSERT(d_is_string_non_literal(var) == expect)

#define check_string_literal(var, expect)						\
	D_CASSERT(d_is_string_literal(var) == expect)

int main(int argc, char **argv)
{
	uuid_t	uuid = {0};
	daos_handle_t	coh;
	daos_handle_t	poh = {0};
	char *charp = NULL;
	const char *ccharp = "hello";
	char	arr[10] = {0};
	const char	carr[10] = {0};

	/** Check the macros */
	check_string_non_literal(charp, true);
	check_string_non_literal(ccharp, true);
	check_string_non_literal(uuid, false);
	check_string_non_literal(arr, true);
	check_string_non_literal(carr, true);
	check_string_literal("Hello", true);
	check_string_literal(charp, false);
	check_string_literal(ccharp, false);
	check_string_literal(uuid, false);
	check_string_literal(arr, false);
	check_string_literal(carr, false);

	/* Test compilation with real APIs */
	D_ASSERT(daos_pool_connect(uuid, NULL, 0, &poh, NULL, NULL) == 0);
	D_ASSERT(daos_pool_connect(arr, NULL, 0, &poh, NULL, NULL) == 0);
	D_ASSERT(daos_pool_connect(ccharp, NULL, 0, &poh, NULL, NULL) == 0);
	D_ASSERT(daos_pool_connect(charp, NULL, 0, &poh, NULL, NULL) == 0);
	D_ASSERT(daos_cont_open(poh, uuid, 0, &coh, NULL, NULL) == 0);
	D_ASSERT(daos_cont_open(poh, arr, 0, &coh, NULL, NULL) == 0);
	D_ASSERT(daos_cont_open(poh, ccharp, 0, &coh, NULL, NULL) == 0);
	D_ASSERT(daos_cont_open(poh, charp, 0, &coh, NULL, NULL) == 0);
	D_ASSERT(daos_cont_destroy(poh, uuid, 0, NULL) == 0);
	D_ASSERT(daos_cont_destroy(poh, arr, 0, NULL) == 0);
	D_ASSERT(daos_cont_destroy(poh, ccharp, 0, NULL) == 0);
	D_ASSERT(daos_cont_destroy(poh, charp, 0, NULL) == 0);

	return 0;
}
