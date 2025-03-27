/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2022-2023 Hewlett Packard Enterprise Development LP
 *
 * Generic ad-hoc CPU performance tests.
 */

#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>

int main(int argc, char **argv)
{
	struct timespec ts1, ts2;
	uint8_t arr[16];
	uint64_t *a;
	double *d;
	uint64_t count;
	int i;

	/* Test alignment consequences on integer sum */
	for (i = 0; i < 8; i++) {
		count = 1000000000;
		a = (uint64_t *)&arr[i];
		clock_gettime(CLOCK_MONOTONIC, &ts1);
		while (count--)
			(*a) += 1;
		clock_gettime(CLOCK_MONOTONIC, &ts2);
		if (ts2.tv_nsec < ts1.tv_nsec) {
			ts2.tv_nsec += 1000000000;
			ts2.tv_sec -= 1;
		}
		ts2.tv_nsec -= ts1.tv_nsec;
		ts2.tv_sec -= ts1.tv_sec;
		printf("a[%d] = %3ld.%09ld\n", i, ts2.tv_sec, ts2.tv_nsec);
	}

	/* Test alignment consequences on double sum */
	for (i = 0; i < 8; i++) {
		count = 1000000000;
		d = (double *)&arr[i];
		clock_gettime(CLOCK_MONOTONIC, &ts1);
		while (count--)
			(*d) += 1.0;
		clock_gettime(CLOCK_MONOTONIC, &ts2);
		if (ts2.tv_nsec < ts1.tv_nsec) {
			ts2.tv_nsec += 1000000000;
			ts2.tv_sec -= 1;
		}
		ts2.tv_nsec -= ts1.tv_nsec;
		ts2.tv_sec -= ts1.tv_sec;
		printf("d[%d] = %3ld.%09ld\n", i, ts2.tv_sec, ts2.tv_nsec);
	}

	return 0;
}
