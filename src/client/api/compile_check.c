/*
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <gurt/debug.h>
#include <daos.h>

#if defined(__cplusplus)
#define check_uuid(var, expect) (void)0
#else
#define check_uuid(var, expect)			\
	D_CASSERT(d_is_uuid(var) == expect)
#endif

#define FOREACH_TYPE(ACTION, poh, coh)					\
	ACTION(uuid_t,       uuid,   noop, {0},  poh, coh, true)	\
	ACTION(const uuid_t, cuuid,  noop, {0},  poh, coh, true)	\
	ACTION(char *,       charp,  noop, NULL, poh, coh, false)	\
	ACTION(const char *, ccharp, noop, NULL, poh, coh, false)	\
	ACTION(char,         arr,    [10], {0},  poh, coh, false)	\
	ACTION(const char,   carr,   [10], {0},  poh, coh, false)

#define FOREACH_ALL(ACTION, poh, coh)					\
	FOREACH_TYPE(ACTION, poh, coh)					\
	ACTION(noop,         "STR",  noop, noop, poh, coh, false)

#define DECLARE_ACTION(type, name, mod, init, poh, coh, expect)	\
	type	name mod = init;

#define RUN_ACTION(type, name, mod, init, poh, coh, expect)			\
	check_uuid(name, expect);						\
	D_ASSERT(daos_pool_connect(name, NULL, 0, &poh, NULL, NULL) == 0);	\
	D_ASSERT(daos_cont_open(poh, name, 0, &coh, NULL, NULL) == 0);		\
	D_ASSERT(daos_cont_destroy(poh, name, 0, NULL) == 0);

int main(int argc, char **argv)
{
	daos_handle_t	coh;
	daos_handle_t	poh;

	FOREACH_TYPE(DECLARE_ACTION, poh, coh);

	FOREACH_TYPE(RUN_ACTION, poh, coh);

	return 0;
}
