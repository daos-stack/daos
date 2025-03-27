/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2022 Hewlett Packard Enterprise Development LP
 */

/* Notes:
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <criterion/criterion.h>
#include <criterion/parameterized.h>

#include "cxip.h"
#include "cxip_test_common.h"

bool verbose = false;

typedef void (*GenFunc)(void);
typedef double (*SumFunc)(size_t, double*);

struct sum_dist {
	const char *name;	// distribution name
	GenFunc func;		// distribution generator
};

struct sum_test {
	const char* name;	// test name
	SumFunc func;		// test function
	double min, max;	// cumulative results
	bool contrib;		// contribute to global min/max
};

struct sum_test_suite {
	double gmin, gmax;	// cumulative global bounds
};

/**
 * @brief Data generation models.
 *
 * These functions generate arrays of doubles using different models that create
 * different distributions of numbers.
 */

unsigned int seed = 3;
size_t numvals = 0;
double *values = NULL;

/* Data generators for the dataset */
void init_dataset(size_t size)
{
	free(values);
	numvals = size;
	values = calloc(size, sizeof(double));
}

void gen_const_data(void)
{
	/* constant data */
	int i;

	for (i = 0; i < numvals; i++)
		values[i] = 1.0;
}

void gen_random_data(void)
{
	/* randomized data */
	int i;

	if (seed) {
		srand(seed);
		seed = 0;
	}
	for (i = 0; i < numvals; i++) {
		int rnd, e;

		rnd = random();
		e = -32*(rnd & 0x7);
		rnd >>= 3;
		values[i] = scalbn(((rnd * 2.0)/RAND_MAX) - 1.0, e);
	}
}

void gen_series_data(void)
{
	/* converging series */
	double s = 1.0;
	int i;

	for (i = 0; i < numvals; i++) {
		values[i] = s / (i+1);
		s = -s;
	}
}

void gen_sine_data(void)
{
	/* sine wave, particularly hard on reproducibility */
	double s = 2.0*M_PI/numvals;
	int i;

	for (i = 0; i < numvals; i++) {
		values[i] = sin(s*i);
	}
}

void gen_range_data(void)
{
	int i, e, s, v;

	/* oscillating between -inf and +inf */
	v = 0;
	s = 1;
	for (i = 0; i < numvals; i++) {
		if (!(i % 2048)) {
			v += 1;
			s = -s;
		}
		e = (i % 2048) - 1023;
		values[i] = s*scalbn(1.0*v, e - 1023);
	}
}

/**
 * @brief Data ordering models.
 *
 * These functions reorder generated data to test associativity.
 *
 */

void nosort_data(void)
{
}

int _sortfunc(const void *p1, const void *p2)
{
	double *v1 = (double *)p1;
	double *v2 = (double *)p2;

	if (*v1 == *v2)
		return 0;
	return (*v1 < *v2) ? -1 : 1;
}

void sort_data(void)
{
	qsort(values, numvals, sizeof(double), _sortfunc);
}

void scramble_data(void)
{
	int i, j;
	double t;

	for (i = numvals-1; i > 0; i--) {
		j = random() %(i+1);
		t = values[i];
		values[i] = values[j];
		values[j] = t;
	}
}

void reverse_data(void)
{
	int i, j, half;
	double t;

	half = numvals/2;
	for (i = 0; i < half; i++) {
		j = numvals-1-i;
		t = values[i];
		values[i] = values[j];
		values[j] = t;
	}
}

/**
 * @brief Summation algoritihms.
 *
 * These function perform the double summation using different algorithms.
 */

double simple_sum(size_t n, double *v)
{
	double s = 0.0;
	int i;

	for (i = 0; i < n; i++)
		s += v[i];

	return s;
}

#define	RADIX 32
double tree_sum(size_t n, double *v)
{
	double s = 0.0;
	int i, k;

	if (n > RADIX) {
		k = n/RADIX;
		for (i = 0; i < RADIX - 1; i++, n -= k)
			s += tree_sum(k, &v[k*i]);
		s += tree_sum(n, &v[k*i]);
	} else {
		for (i = 0; i < n; i++)
			s += v[i];
	}

	return s;
}

double Kahans_sum(size_t n, double *v)
{
	double s = 0.0;
	double c = 0.0;
	int i;

	for (i = 0; i < n; i++) {
		double y = v[i] - c;
		double t = s + y;

		c = (t - s) - y;
		s = t;
	}

	return s;
}

void print_repsum(struct cxip_repsum *x)
{
	printf("M=%3d T=[%016lx, %016lx, %016lx, %016lx] oflow=%d inexact=%d\n",
		x->M, x->T[0], x->T[1], x->T[2], x->T[3],
		x->overflow, x->inexact);
}

/**
 * @brief Static structures to make the above models accessible to the test
 * code.
 *
 */

struct sum_dist test_dists[] = {
	{.name="const",  .func=&gen_const_data},
	{.name="random", .func=&gen_random_data},
	{.name="series", .func=&gen_series_data},
	{.name="sin",    .func=&gen_sine_data},
	{.name="range",  .func=&gen_range_data}
};
#define	NUM_DISTS	(sizeof(test_dists)/sizeof(struct sum_dist))

struct sum_dist test_perms[] = {
	{.name="nosort",   .func=&nosort_data},
	{.name="sort",     .func=&sort_data},
	{.name="scramble", .func=&scramble_data},
	{.name="reverse",  .func=&reverse_data},
};
#define	NUM_PERMS	(sizeof(test_perms)/sizeof(struct sum_dist))
#define	PERM_NOSORT	0
#define	PERM_SORT	1
#define	PERM_SCRAMBLE	2
#define	PERM_REVERSE	3

struct sum_test test_cases[] = {
	{.name="simple_sum", .func=&simple_sum,   .contrib=true},
	{.name="tree_sum",   .func=&tree_sum,     .contrib=true},
	{.name="Kahans_sum", .func=&Kahans_sum,   .contrib=true},
	{.name="rep_sum",    .func=&cxip_rep_sum, .contrib=false},
};
#define	NUM_CASES	(sizeof(test_cases)/sizeof(struct sum_test))
#define	TEST_SIMPLE	0
#define	TEST_TREE	1
#define	TEST_KAHAN	2
#define	TEST_REPSUM	3

struct sum_test_suite test_suite;

/**
 * @brief Main test code.
 *
 * The basic model is to take a particular distribution of doubles, then perform
 * multiple summations of that distribution with different orderings of the
 * values, retaining the result as a (min, max) pair.
 *
 * For a perfectly-reproducible summation method, the final result for each
 * distribution will show min == max.
 */

void _show_results(void)
{
	struct sum_test *test;
	double dif, mid, err;
	int n;

	for (n = 0; n < NUM_CASES; n++) {
		test = &test_cases[n];
		dif = (test->max - test->min);
		mid = (test->max + test->min)/2.0;
		err = fabs(mid ? dif/mid : dif);

		if (verbose)
			printf("%12s %29.20g %29.20g %g\n",
				test->name, test->min, test->max, err);
	}
}

void _reset_results(void)
{
	int n;

	test_suite.gmax = -HUGE_VAL;
	test_suite.gmin = HUGE_VAL;
	for (n = 0; n < NUM_CASES; n++) {
		test_cases[n].max = -HUGE_VAL;
		test_cases[n].min = HUGE_VAL;
	}
}

/* Perform a single summation and record min/max */
void _runtest(struct sum_test *test)
{
	double sum;

	sum = test->func(numvals, values);
	if (test->min > sum)
		test->min = sum;
	if (test->max < sum)
		test->max = sum;
	if (test->contrib) {
		if (test_suite.gmin > sum)
			test_suite.gmin = sum;
		if (test_suite.gmax < sum)
			test_suite.gmax = sum;
	}
}

/* Perform a summations */
void _run_tests(uint64_t tstmask)
{
	int n;

	for (n = 0; n < NUM_CASES; n++) {
		if (!(tstmask & (1 << n)))
			continue;
		if (verbose)
			printf("    ... %s\n", test_cases[n].name);
		_runtest(&test_cases[n]);
	}
}

/* reorder the data, and perform summations using different methods */
void run_permutations(uint64_t tstmask)
{
	int sequence[] = {
		PERM_NOSORT,
		PERM_REVERSE,
		PERM_SORT,
		PERM_REVERSE,
		PERM_SCRAMBLE,
		PERM_REVERSE,
	};
	int seqcnt = sizeof(sequence)/sizeof(int);
	int n, p;

	_reset_results();
	for (n = 0; n < seqcnt; n++) {
		p = sequence[n];
		if (verbose)
			printf("  ----- %s\n", test_perms[p].name);
		test_perms[p].func();
		_run_tests(tstmask);
	}
	_show_results();
}

/* generate a distribution of values, and run permutations */
void run_dists(uint64_t dstmask, uint64_t tstmask)
{
	int n;

	for (n = 0; n < NUM_DISTS; n++) {
		if (!(dstmask & (1 << n)))
			continue;
		if (verbose)
			printf("======= %s\n", test_dists[n].name);
		test_dists[n].func();
		run_permutations(tstmask);
	}
}

static inline bool _equal(double a, double b)
{
	return (isnan(a) && isnan(b)) || a == b;
}

TestSuite(repsum, .init = cxit_setup_ep, .fini =cxit_teardown_ep,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

/*
 * Convert double->repsum and repsum->double, and compare for equality.
 */
Test(repsum, convert)
{
	struct cxip_repsum x;
	double s[] = {1.0, -1.0};
	double d1, d2;
	int i, j, k;

	/* note that this exponent spans subnormals and +inf/-inf */
	for (i = -1100; i < 1100; i++) {
		for (j = 0; j < 53; j++) {
			for (k = 0; k < 2; k++) {
				d1 = scalbn(s[k]*((1 << j) - 1), i);
				cxip_dbl_to_rep(&x, d1);
				cxip_rep_to_dbl(&d2, &x);
				cr_assert(_equal(d1, d2),
					"%d, %d: %.13e != %.13e\n",
					i, j, d1, d2);
			}
		}
	}
	/* explicit -inf */
	d1 = -INFINITY;
	cxip_dbl_to_rep(&x, d1);
	cxip_rep_to_dbl(&d2, &x);
	cr_assert(d1 == d2, "%d, %d, %.13e != %.13e\n", i, j, d1, d2);
	/* explicit +inf */
	d1 = +INFINITY;
	cxip_dbl_to_rep(&x, d1);
	cxip_rep_to_dbl(&d2, &x);
	cr_assert(d1 == d2, "%d, %d: %.13e != %.13e\n", i, j, d1, d2);
	/* explicit NaN */
	d1 = NAN;
	cxip_dbl_to_rep(&x, d1);
	cxip_rep_to_dbl(&d2, &x);
	cr_assert(isnan(d2), "%d, %d: %.13e != %.13e %016lx != %016lx\n",
		  i, j, d1, d2, _dbl2bits(d1), _dbl2bits(d2));
}

/*
 * Add two values using double and using repsum, and compare for equality.
 */
Test(repsum, add)
{
	double s1[] = {1.0, 1.0, -1.0, -1.0};
	double s2[] = {1.0, -1.0, 1.0, -1.0};
	double d1, d2, d3, d4;
	int i, j, k;

	/* note that this exponent spans subnormals and +inf/-inf */
	for (i = -1100; i < 1100; i++) {
		for (j = 0; j < 53; j++) {
			for (k = 0; k < 4; k++) {
				d1 = scalbn(s1[k]*((1 << j) - 1), i);
				d2 = scalbn(s2[k]*((1 << j) - 1), i+1);
				d3 = d1 + d2;
				d4 = cxip_rep_add_dbl(d1, d2);
				cr_assert(_equal(d3, d4),
					  "%d, %d, %d: %.13e != %.13e"
					  " %016lx %016lx %016lx %016lx\n",
					  i, j, k, d3, d4,
					  _dbl2bits(d1), _dbl2bits(d2),
					  _dbl2bits(d3), _dbl2bits(d4));
			}
		}
	}
}

/*
 * Add combinations of NAN and INFINITY, compare for correct result.
 */
Test(repsum, inf)
{
	double a[] = {1.0, +INFINITY, -INFINITY, NAN};
	double d1, d2, d3, d4, exp;
	int i, j;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			d1 = a[i];
			d2 = a[j];
			d3 = d1 + d2;
			d4 = cxip_rep_add_dbl(d1, d2);
			if (isnan(d1) || isnan(d2))
				exp = NAN;
			else if (isinf(d1) && isinf(d2))
				exp = (d1 == d2) ? d1 : NAN;
			else if (isinf(d1))
				exp = d1;
			else if (isinf(d2))
				exp = d2;
			else
				exp = d3;
			cr_assert(_equal(d3, exp),
				"dbl %d, %d: (%e + %e) = %e, expected %e\n",
				i, j, d1, d2, d3, exp);
			cr_assert(_equal(d4, exp),
				"rep %d, %d: (%e + %e) = %e, expected %e\n",
				i, j, d1, d2, d4, exp);
		}
	}
}

/*
 * Test for overflow by performing too many sums.
 * 0.5 places MSBit in bit 39 of a bin.
 * 1LL << 24 additions of 0.5 will fill overflow area.
 * One more addition should trigger overflow.
 */
Test(repsum, overflow)
{
	struct cxip_repsum x, y;
	long int i, n;

	cxip_dbl_to_rep(&x, 0.0);
	cxip_dbl_to_rep(&y, 0.5);
	n = 1LL << 24;
	for (i = 0L; i < n-1; i++) {
		cxip_rep_add(&x, &y);
		if (x.overflow)
			break;
	}
	cr_assert(!x.overflow, "overflow at %lx not expected\n", i++);
	cxip_rep_add(&x, &y);
	cr_assert(x.overflow, "overflow at %ld expected\n", i);
	cxip_dbl_to_rep(&y, 0.0);
	cxip_rep_add(&y, &x);
	cr_assert(y.overflow, "overflow not propagated\n");
}

/*
 * Test for expected loss of precision.
 * Adding 1.0*2^i for i=(0,39) will fill a bin.
 * Doing this four times will fill the T[] array.
 * Doing this one more time will drop the LSBin.
 */
Test(repsum, inexact)
{
	struct cxip_repsum x, y;
	int i, n;

	cxip_dbl_to_rep(&x, 0.0);
	n = 4*40;
	for (i = 0; i < n; i++) {
		cxip_dbl_to_rep(&y, scalbn(1.0, i));
		cxip_rep_add(&x, &y);
		if (x.inexact)
			break;
	}
	cr_assert(!x.inexact, "inexact at %x not expected\n", i++);
	cxip_dbl_to_rep(&y, scalbn(1.0, i));
	cxip_rep_add(&x, &y);
	cr_assert(x.inexact, "inexact at %x expected\n", i);
	cxip_dbl_to_rep(&y, 0.0);
	cxip_rep_add(&y, &x);
	cr_assert(y.inexact, "inexact not propagated\n");
}

/*
 * Test comparison of different methods over datasets
 * In all cases, repsum should be reproducible, err = 0.
 */
Test(repsum, comparison)
{
	struct sum_test *test;
	double dif, mid, err;

	init_dataset(100000);
	run_dists(-1L, -1L);

	test = &test_cases[TEST_REPSUM];
	dif = (test->max - test->min);
	mid = (test->max + test->min)/2.0;
	err = fabs(mid ? dif/mid : dif);
	if (err)
		printf("%12s %29.20g %29.20g %g\n",
			test->name, test->min, test->max, err);
	cr_assert(!err, "repsum is not reproducible\n");
}
