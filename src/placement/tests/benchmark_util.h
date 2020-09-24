/**
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#ifndef __TIME_PROFILING_H__
#define __TIME_PROFILING_H__

#ifdef USE_TIME_PROFILING

#include <stdio.h>
#include <time.h>
#include <float.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#define COMPILER_BARRIER() asm volatile("" ::: "memory")

/**
 * Number of nanoseconds in one second.
 */
#define NANOSECONDS_PER_SECOND 1000000000LL

struct benchmark_handle {
	struct timespec wallclock_start_time;
	struct timespec thread_start_time;
	long long wallclock_delta_ns;
	long long thread_delta_ns;
};

/*
 * Allocates a benchmark data structure
 */
static inline struct benchmark_handle *
benchmark_alloc(void)
{
	struct benchmark_handle *hdl;

	hdl = (struct benchmark_handle *)calloc(sizeof(struct benchmark_handle),
						1);
	if (hdl == NULL)
		return NULL;

	hdl->wallclock_delta_ns = -1;
	hdl->thread_delta_ns = -1;

	return hdl;
}

static inline void
benchmark_start(struct benchmark_handle *hdl)
{
	COMPILER_BARRIER();
	clock_gettime(CLOCK_MONOTONIC, &hdl->wallclock_start_time);
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &hdl->thread_start_time);
	COMPILER_BARRIER();
}

/**
 * Stops a benchmark and stores the result in the benchmark handle
 */
static inline void
benchmark_stop(struct benchmark_handle *hdl)
{
	struct timespec wallclock_stop_time;
	struct timespec thread_stop_time;

	COMPILER_BARRIER();
	clock_gettime(CLOCK_MONOTONIC, &wallclock_stop_time);
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &thread_stop_time);
	hdl->wallclock_delta_ns = (((long long)(wallclock_stop_time.tv_sec -
					hdl->wallclock_start_time.tv_sec) *
				    NANOSECONDS_PER_SECOND) +
				   (long long)(wallclock_stop_time.tv_nsec -
					hdl->wallclock_start_time.tv_nsec));
	hdl->thread_delta_ns = (((long long)(thread_stop_time.tv_sec -
					hdl->thread_start_time.tv_sec) *
				 NANOSECONDS_PER_SECOND) +
				(long long)(thread_stop_time.tv_nsec -
					hdl->thread_start_time.tv_nsec));
}

static inline void
benchmark_free(struct benchmark_handle *hdl)
{
	if (hdl != NULL)
		free(hdl);
}

static inline void
benchmark_graph(double *ydata, const char **const keys,
		int64_t series_count, int64_t data_count,
		const char *xlabel, const char *ylabel,
		double y_user_max, const char *title,
		const char *fifo_path, const bool use_x11)
{
	FILE  *gp_w;
	FILE  *gp_r;
	int64_t idx = 0;
	int64_t series = 0;
	struct winsize w;
	double ydata_max = 0, ydata_min = DBL_MAX;

	/* Create a FIFO to communicate with gnuplot */
	if (mkfifo(fifo_path, 0600)) {
		if (errno != EEXIST) {
			perror(fifo_path);
			unlink(fifo_path);
			return;
		}
	}

	gp_w = popen("gnuplot", "w");
	if (gp_w == NULL) {
		perror("popen(gnuplot)");
		return;
	}

	/* Tell gnuplot to print to the FIFO */
	fprintf(gp_w, "set print \"%s\"\n", fifo_path);
	fflush(gp_w);

	/* Open the FIFO for reading */
	gp_r = fopen(fifo_path, "r");
	if (gp_r == NULL) {
		perror(fifo_path);
		pclose(gp_w);
		return;
	}

	for (series = 0; series < series_count; series++) {
		for (idx = 0; idx < data_count; idx++) {
			if (ydata[series * data_count + idx] > ydata_max)
				ydata_max = ydata[series * data_count + idx];
			if (ydata[series * data_count + idx] < ydata_min)
				ydata_min = ydata[series * data_count + idx];
		}
	}

	if ((y_user_max) > 0)
		ydata_max = (y_user_max);
	else
		ydata_max *= 1.2F;

	/* Get the terminal width */
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

	/* Print the data to gnuplot, which will display the graph */
	if (!use_x11)
		fprintf(gp_w, "set terminal dumb feed %d %d\n",
			w.ws_col - 5, w.ws_row - 5);
	fprintf(gp_w, "set key below vertical\n");
	fprintf(gp_w, "set title \"%s\"\n", title);
	fprintf(gp_w, "set xlabel \"%s\"\n", (xlabel));
	fprintf(gp_w, "set ylabel \"%s\"\n", (ylabel));
	fprintf(gp_w, "set xrange [%d:%ld]\n", 0, data_count);
	fprintf(gp_w, "set yrange [%0f:%0f]\n", 0.0F, ydata_max);
	fprintf(gp_w, "plot \"-\" title \"%s\"", keys[0]);
	for (series = 1; series < series_count; series++)
		fprintf(gp_w, ", \"\" title \"%s\"", keys[series]);
	fprintf(gp_w, "\n");
	for (series = 0; series < series_count; series++) {
		if (series > 0)
			fprintf(gp_w, "e\n");
		for (idx = 0; idx < data_count; idx++) {
			fprintf(gp_w, "%f\n",
				ydata[series * data_count + idx]);
			fprintf(stdout, "%f\n",
				ydata[series * data_count + idx]);
		}
	}
	fprintf(gp_w, "end\n");

	fflush(gp_w);
	fclose(gp_r);
	pclose(gp_w);
	unlink(fifo_path);
}

#else /* USE_TIME_PROFILING */

#define benchmark_alloc()
#define benchmark_start(hdl)
#define benchmark_stop(hdl)
#define benchmark_free(hdl)
#define benchmark_graph(ydata, keys, series_count, data_count, \
			xlabel, ylabel, y_user_max, title, fifo_path, use_x11)

#endif /* USE_TIME_PROFILING */

#endif /* __TIME_PROFILING_H__ */


