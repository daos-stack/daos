// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2018-2020, Intel Corporation */

/*
 * util_ravl.c -- unit test for ravl tree
 */
#include <stdint.h>
#include <stdlib.h>

#include "ravl.h"
#include "util.h"
#include "unittest.h"
#include "fault_injection.h"

static int
cmpkey(const void *lhs, const void *rhs)
{
	intptr_t l = (intptr_t)lhs;
	intptr_t r = (intptr_t)rhs;

	return (int)(l - r);
}

static void
test_misc(void)
{
	struct ravl *r = ravl_new(cmpkey);
	struct ravl_node *n = NULL;
	ravl_insert(r, (void *)3);
	ravl_insert(r, (void *)6);
	ravl_insert(r, (void *)1);
	ravl_insert(r, (void *)7);
	ravl_insert(r, (void *)9);
	ravl_insert(r, (void *)5);
	ravl_insert(r, (void *)8);
	ravl_insert(r, (void *)2);
	ravl_insert(r, (void *)4);
	ravl_insert(r, (void *)10);

	n = ravl_find(r, (void *)11, RAVL_PREDICATE_EQUAL);
	UT_ASSERTeq(n, NULL);

	n = ravl_find(r, (void *)10, RAVL_PREDICATE_GREATER);
	UT_ASSERTeq(n, NULL);

	n = ravl_find(r, (void *)11, RAVL_PREDICATE_GREATER);
	UT_ASSERTeq(n, NULL);

	n = ravl_find(r, (void *)11,
		RAVL_PREDICATE_GREATER | RAVL_PREDICATE_EQUAL);
	UT_ASSERTeq(n, NULL);

	n = ravl_find(r, (void *)1, RAVL_PREDICATE_LESS);
	UT_ASSERTeq(n, NULL);

	n = ravl_find(r, (void *)0, RAVL_PREDICATE_LESS_EQUAL);
	UT_ASSERTeq(n, NULL);

	n = ravl_find(r, (void *)9, RAVL_PREDICATE_GREATER);
	UT_ASSERTne(n, NULL);
	UT_ASSERTeq(ravl_data(n), (void *)10);

	n = ravl_find(r, (void *)9, RAVL_PREDICATE_LESS);
	UT_ASSERTne(n, NULL);
	UT_ASSERTeq(ravl_data(n), (void *)8);

	n = ravl_find(r, (void *)9,
		RAVL_PREDICATE_GREATER | RAVL_PREDICATE_EQUAL);
	UT_ASSERTne(n, NULL);
	UT_ASSERTeq(ravl_data(n), (void *)9);

	n = ravl_find(r, (void *)9,
		RAVL_PREDICATE_LESS | RAVL_PREDICATE_EQUAL);
	UT_ASSERTne(n, NULL);
	UT_ASSERTeq(ravl_data(n), (void *)9);

	n = ravl_find(r, (void *)100, RAVL_PREDICATE_LESS);
	UT_ASSERTne(n, NULL);
	UT_ASSERTeq(ravl_data(n), (void *)10);

	n = ravl_find(r, (void *)0, RAVL_PREDICATE_GREATER);
	UT_ASSERTne(n, NULL);
	UT_ASSERTeq(ravl_data(n), (void *)1);

	n = ravl_find(r, (void *)3, RAVL_PREDICATE_EQUAL);
	UT_ASSERTne(n, NULL);
	ravl_remove(r, n);

	n = ravl_find(r, (void *)10, RAVL_PREDICATE_EQUAL);
	UT_ASSERTne(n, NULL);
	ravl_remove(r, n);

	n = ravl_find(r, (void *)6, RAVL_PREDICATE_EQUAL);
	UT_ASSERTne(n, NULL);
	ravl_remove(r, n);

	n = ravl_find(r, (void *)9, RAVL_PREDICATE_EQUAL);
	UT_ASSERTne(n, NULL);
	ravl_remove(r, n);

	n = ravl_find(r, (void *)7, RAVL_PREDICATE_EQUAL);
	UT_ASSERTne(n, NULL);
	ravl_remove(r, n);

	n = ravl_find(r, (void *)1, RAVL_PREDICATE_EQUAL);
	UT_ASSERTne(n, NULL);
	ravl_remove(r, n);

	n = ravl_find(r, (void *)5, RAVL_PREDICATE_EQUAL);
	UT_ASSERTne(n, NULL);
	ravl_remove(r, n);

	n = ravl_find(r, (void *)8, RAVL_PREDICATE_EQUAL);
	UT_ASSERTne(n, NULL);
	ravl_remove(r, n);

	n = ravl_find(r, (void *)2, RAVL_PREDICATE_EQUAL);
	UT_ASSERTne(n, NULL);
	ravl_remove(r, n);

	n = ravl_find(r, (void *)4, RAVL_PREDICATE_EQUAL);
	UT_ASSERTne(n, NULL);
	ravl_remove(r, n);

	ravl_delete(r);
}

static void
test_predicate(void)
{
	struct ravl *r = ravl_new(cmpkey);
	struct ravl_node *n = NULL;
	ravl_insert(r, (void *)10);
	ravl_insert(r, (void *)5);
	ravl_insert(r, (void *)7);

	n = ravl_find(r, (void *)6, RAVL_PREDICATE_GREATER);
	UT_ASSERTne(n, NULL);
	UT_ASSERTeq(ravl_data(n), (void *)7);

	n = ravl_find(r, (void *)6, RAVL_PREDICATE_LESS);
	UT_ASSERTne(n, NULL);
	UT_ASSERTeq(ravl_data(n), (void *)5);

	ravl_delete(r);
}

static void
test_stress(void)
{
	struct ravl *r = ravl_new(cmpkey);

	for (int i = 0; i < 1000000; ++i) {
		ravl_insert(r, (void *)(uintptr_t)rand());
	}

	ravl_delete(r);
}

struct foo {
	int a;
	int b;
	int c;
};

static int
cmpfoo(const void *lhs, const void *rhs)
{
	const struct foo *l = lhs;
	const struct foo *r = rhs;

	return ((l->a + l->b + l->c) - (r->a + r->b + r->c));
}

static void
test_emplace(void)
{
	struct ravl *r = ravl_new_sized(cmpfoo, sizeof(struct foo));

	struct foo a = {1, 2, 3};
	struct foo b = {2, 3, 4};
	struct foo z = {0, 0, 0};

	ravl_emplace_copy(r, &a);
	ravl_emplace_copy(r, &b);

	struct ravl_node *n = ravl_find(r, &z, RAVL_PREDICATE_GREATER);
	struct foo *fn = ravl_data(n);
	UT_ASSERTeq(fn->a, a.a);
	UT_ASSERTeq(fn->b, a.b);
	UT_ASSERTeq(fn->c, a.c);
	ravl_remove(r, n);

	n = ravl_find(r, &z, RAVL_PREDICATE_GREATER);
	fn = ravl_data(n);
	UT_ASSERTeq(fn->a, b.a);
	UT_ASSERTeq(fn->b, b.b);
	UT_ASSERTeq(fn->c, b.c);
	ravl_remove(r, n);

	ravl_delete(r);
}

static void
test_fault_injection_ravl_sized()
{
	if (!core_fault_injection_enabled())
		return;

	core_inject_fault_at(PMEM_MALLOC, 1, "ravl_new_sized");
	struct ravl *r = ravl_new_sized(NULL, 0);
	UT_ASSERTeq(r, NULL);
	UT_ASSERTeq(errno, ENOMEM);
}

static void
test_fault_injection_ravl_node()
{
	if (!core_fault_injection_enabled())
		return;

	struct foo a = {1, 2, 3};
	struct ravl *r = ravl_new_sized(cmpfoo, sizeof(struct foo));
	UT_ASSERTne(r, NULL);

	core_inject_fault_at(PMEM_MALLOC, 1, "ravl_new_node");
	int ret = ravl_emplace_copy(r, &a);
	UT_ASSERTne(ret, 0);
	UT_ASSERTeq(errno, ENOMEM);
}

int
main(int argc, char *argv[])
{
	START(argc, argv, "util_ravl");

	test_predicate();
	test_misc();
	test_stress();
	test_emplace();

	test_fault_injection_ravl_sized();
	test_fault_injection_ravl_node();

	DONE(NULL);
}
