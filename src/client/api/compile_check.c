/*
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <gurt/debug.h>
#include <daos.h>

#if defined(__cplusplus)
#define check_uuid(var, expect, type) (void)0
#define check_string(var, expect, type) (void)0
#define check_uuid_func(...) (void)0
#define check_const_uuid_func(...) (void)0
#else
#define check_uuid(var, expect, type)							\
	do {										\
		printf("uuid check:   "#var" of type " #type " should be a %s, is %s\n",\
		       expect ? "uuid" : "string", d_is_uuid(var) ? "uuid" : "string");	\
		D_CASSERT(d_is_uuid(var) == expect);					\
	} while (0)

#define check_string(var, expect, type)								\
	do {											\
		printf("string check: "#var" of type " #type " should be a %s, is %s\n",	\
		       expect ? "string" : "uuid", d_is_string(var) ? "string" : "uuid");	\
		D_CASSERT(d_is_string(var) == expect);						\
	} while (0)

static void
check_uuid_func(uuid_t uuid)
{
	check_uuid(uuid, true, uuid_t (in function));
	check_string(uuid, false, uuid_t (in function));
}

static void
check_const_uuid_func(const uuid_t uuid)
{
	check_uuid(uuid, true, const uuid_t (in function));
	check_string(uuid, false, const uuid_t (in function));
}
#endif

#define noop

#define FOREACH_UUID_TYPE(ACTION, poh, coh)				\
	ACTION(uuid_t,       uuid,   noop, {0},  poh, coh, true)	\
	ACTION(const uuid_t, cuuid,  noop, {0},  poh, coh, true)

#define FOREACH_TYPE(ACTION, poh, coh)					\
	FOREACH_UUID_TYPE(ACTION, poh, coh)				\
	ACTION(char *,       charp,  noop, NULL, poh, coh, false)	\
	ACTION(const char *, ccharp, noop, NULL, poh, coh, false)	\
	ACTION(char,         arr,    [10], {0},  poh, coh, false)	\
	ACTION(const char,   carr,   [10], {0},  poh, coh, false)	\
	ACTION(char,         arrs,   [16], {0},  poh, coh, false)	\
	ACTION(char,         dyn,    [],   "c",  poh, coh, false)

#define FOREACH_ALL(ACTION, poh, coh)					\
	FOREACH_TYPE(ACTION, poh, coh)					\
	ACTION(literal,      "STR",  noop, noop, poh, coh, false)

#define DECLARE_ACTION(type, name, mod, init, poh, coh, expect)	\
	type	name mod = init;

#define RUN_FUNCTION(cmd)	\
	do {			\
		int	__rc;	\
		__rc = cmd;	\
		(void)__rc;	\
	} while (0)

#define RUN_ACTION(type, name, mod, init, poh, coh, expect)			\
	check_uuid(name, expect, type mod);					\
	RUN_FUNCTION(daos_pool_connect(name, NULL, 0, &poh, NULL, NULL) == 0);	\
	RUN_FUNCTION(daos_cont_open(poh, name, 0, &coh, NULL, NULL) == 0);	\
	RUN_FUNCTION(daos_cont_destroy(poh, name, 0, NULL) == 0);		\
	check_string(name, !(expect), type mod);

int main(int argc, char **argv)
{
	daos_handle_t	coh;
	daos_handle_t	poh;

	FOREACH_TYPE(DECLARE_ACTION, poh, coh);

	FOREACH_ALL(RUN_ACTION, poh, coh);

	check_uuid_func(uuid);
	check_const_uuid_func(uuid);
	check_const_uuid_func(cuuid);

	return 0;
}
