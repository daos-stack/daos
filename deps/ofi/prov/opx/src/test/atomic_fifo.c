/*
 * Copyright (C) 2021 by Cornelis Networks.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "rdma/opx/fi_opx_atomic_fifo.h"
#include "rdma/opx/fi_opx_timer.h"

#define TEST_ITERATIONS		(1000000)
#define EXCLUDE_ITERATIONS	(100000)

struct barrier_phase {
	volatile uint64_t	started;
	volatile uint64_t	completed;
	uint64_t		unused[6];
} __attribute__((__aligned__(64)));


struct barrier_shared {
	struct barrier_phase	phase[2];
} __attribute__((__aligned__(64)));

struct barrier_participant {
	uint64_t		iteration;
	uint64_t		count;
	volatile uint64_t *	started[2];
	volatile uint64_t *	completed[2];
};



static inline void barrier_shared_init (struct barrier_shared * shared) {
	shared->phase[0].started = 0;
	shared->phase[0].completed = 0;
	shared->phase[1].started = 0;
	shared->phase[1].completed = 0;
};

static inline void barrier_participant_init (struct barrier_shared * shared, struct barrier_participant * participant, uint64_t participants) {

	participant->iteration = 0;
	participant->count = participants;
	participant->started[0] = &shared->phase[0].started;
	participant->started[1] = &shared->phase[1].started;
	participant->completed[0] = &shared->phase[0].completed;
	participant->completed[1] = &shared->phase[1].completed;
}

static inline void barrier_enter (struct barrier_participant * participant) {

	const uint64_t iteration = participant->iteration;
	const uint64_t index = iteration & 0x01ul;

	fi_opx_compiler_inc_u64(participant->started[index]);
}

static inline void barrier_wait (struct barrier_participant * participant) {

	const uint64_t count = participant->count;
	const uint64_t iteration = participant->iteration;
	const uint64_t index = iteration & 0x01ul;

	volatile uint64_t * const started = participant->started[index];
	volatile uint64_t * const completed = participant->completed[index];
	while (*started != count);

	const uint64_t result = fi_opx_compiler_fetch_and_inc_u64(completed);
	if (result == (count-1)) {
		*started = 0;
		*completed = 0;
	}

	participant->iteration = iteration + 1;
}


struct producer_test {
	struct fi_opx_atomic_fifo_producer	producer;
	double					elapsed_usec;
};

struct producer_info {

	struct barrier_participant	barrier;
	unsigned			exclude_iterations;
	unsigned			test_iterations;
	uint64_t			id;
	pthread_t			thread;
	unsigned			test_count;
	unsigned			do_produce_unsafe;

	struct producer_test		test[3];
};

static inline
void test_producer (struct producer_info * info, unsigned index, const unsigned do_produce_unsafe) {

	struct barrier_participant * barrier = &info->barrier;
	struct fi_opx_atomic_fifo_producer * producer = &info->test[index].producer;

	const unsigned exclude_iterations = info->exclude_iterations;
	const unsigned test_iterations = info->test_iterations;
	const uint64_t id = info->id << 56;

	union fi_opx_timer_state timer;
	union fi_opx_timer_stamp start;
	fi_opx_timer_init(&timer);

	barrier_enter(barrier);
	barrier_wait(barrier);

	if (do_produce_unsafe) {
		unsigned i;
		for (i=1; i<=exclude_iterations; ++i) {
			const uint64_t data_lsh3b = id | (i<<3);
			fi_opx_atomic_fifo_produce_unsafe(producer, data_lsh3b);
		}

		fi_opx_timer_now(&start, &timer);
		for (; i<=(exclude_iterations+test_iterations); ++i) {
			const uint64_t data_lsh3b = id | (i<<3);
			fi_opx_atomic_fifo_produce_unsafe(producer, data_lsh3b);
		}
	} else {
		unsigned i;
		for (i=1; i<=exclude_iterations; ++i) {
			const uint64_t data_lsh3b = id | (i<<3);
			fi_opx_atomic_fifo_produce(producer, data_lsh3b);
		}

		fi_opx_timer_now(&start, &timer);
		for (; i<=(exclude_iterations+test_iterations); ++i) {
			const uint64_t data_lsh3b = id | (i<<3);
			fi_opx_atomic_fifo_produce(producer, data_lsh3b);
		}
	}

	info->test[index].elapsed_usec =
		fi_opx_timer_elapsed_usec(&start, &timer);

	barrier_enter(barrier);
	barrier_wait(barrier);

	return;
}

void * pthread_producer (void * arg) {

	struct producer_info * info = (struct producer_info *)arg;

	const unsigned test_count = info->test_count;
	unsigned i;

	if (info->do_produce_unsafe) {
		for (i=0; i<test_count; ++i) {
			test_producer(info, i, 1);		/* validation */
			test_producer(info, i, 1);		/* performance */
		}
	} else {
		for (i=0; i<test_count; ++i) {
			test_producer(info, i, 0);		/* validation */
			test_producer(info, i, 0);		/* performance */
		}
	}

	return NULL;
}


struct consumer_test {

	struct fi_opx_atomic_fifo	fifo;
	double				elapsed_usec;
};

struct consumer_info {

	struct barrier_participant	barrier;
	unsigned			exclude_iterations;
	unsigned			test_iterations;
	uint64_t			id;
	pthread_t			thread;
	unsigned			test_count;
	unsigned			num_producers;
	unsigned			do_consume_wait;

	struct consumer_test		test[3];
};

static inline
void test_consumer (struct consumer_info * info, const unsigned index, const unsigned do_consume_wait, const unsigned do_validation) {

	struct barrier_participant * barrier = &info->barrier;
	struct fi_opx_atomic_fifo * fifo = &info->test[index].fifo;

	const unsigned num_producers = info->num_producers;
	const unsigned exclude_iterations = info->exclude_iterations * num_producers;
	const unsigned test_iterations = info->test_iterations * num_producers;

	union fi_opx_timer_state timer;
	union fi_opx_timer_stamp start;
	fi_opx_timer_init(&timer);

	unsigned i;
	uint64_t last[64];
	for (i=0; i<64; ++i) last[i] = 0;

	barrier_enter(barrier);
	barrier_wait(barrier);
	
	for (i=0; i<exclude_iterations; ++i) {
		uint64_t data_lsh3b = 0;
		if (do_consume_wait) {
			fi_opx_atomic_fifo_consume_wait(fifo, &data_lsh3b);
		} else {
			int rc = 1;
			do {
				rc = fi_opx_atomic_fifo_consume(fifo, &data_lsh3b);
			} while (rc != 0);
		}

		if (do_validation) {
			const unsigned producer = (unsigned)(data_lsh3b >> 56);
			if (producer >= num_producers) {
				fprintf(stderr, "\nerror. invalid producer id %u, num_producers = %u\n", producer, num_producers);
				abort();
			}
			const uint64_t data = (data_lsh3b & 0x00FFFFFFFFFFFFFFul) >> 3;
			if ((last[producer] + 1) != data) {
				fprintf(stderr, "\nvalidation error. last[%u] = 0x%016lx (%lu), data = 0x%016lx (%lu)\n", producer, last[producer], last[producer], data, data);
				abort();
			}
			last[producer] = data;
		}
	}

	fi_opx_timer_now(&start, &timer);
	for (; i<(exclude_iterations+test_iterations); ++i) {
		uint64_t data_lsh3b = 0;
		if (do_consume_wait) {
			fi_opx_atomic_fifo_consume_wait(fifo, &data_lsh3b);
		} else {
			int rc = 1;
			do {
				rc = fi_opx_atomic_fifo_consume(fifo, &data_lsh3b);
			} while (rc != 0);
		}

		if (do_validation) {
			const unsigned producer = (unsigned)(data_lsh3b >> 56);
			if (producer >= num_producers) {
				fprintf(stderr, "\nerror. invalid producer id %u, num_producers = %u\n", producer, num_producers);
				abort();
			}
			const uint64_t data = (data_lsh3b & 0x00FFFFFFFFFFFFFFul) >> 3;
			if ((last[producer] + 1) != data) {
				fprintf(stderr, "\nvalidation error. last[%u] = 0x%016lx (%lu), data = 0x%016lx (%lu)\n", producer, last[producer], last[producer], data, data);
				abort();
			}
			last[producer] = data;
		}
	}
	const double elapsed_usec = fi_opx_timer_elapsed_usec(&start, &timer);

	barrier_enter(barrier);
	barrier_wait(barrier);

	info->test[index].elapsed_usec = elapsed_usec;

	if (do_validation) {
		for (i=1; i<info->num_producers; ++i) {
			if (last[i] != last[i-1]) {
				fprintf(stderr, "\nvalidation error. final producer values do not match. last[%u] = 0x%016lx (%lu), last[%u] = 0x%016lx (%lu)\n", i-1, last[i-1], last[i-1], i, last[i], last[i]);
				abort();
			}
		}
	}

	return;
}

void * pthread_consumer (void * arg) {

	struct consumer_info * info = (struct consumer_info *)arg;

	const unsigned test_count = info->test_count;
	unsigned i;
	fprintf(stderr, "# testing ");
	if (info->do_consume_wait) {
		for (i=0; i<test_count; ++i) {
			fprintf(stderr, ".");
			test_consumer(info, i, 1, 1);
			fprintf(stderr, ".");
			test_consumer(info, i, 1, 0);
		}
	} else {
		for (i=0; i<test_count; ++i) {
			fprintf(stderr, ".");
			test_consumer(info, i, 0, 1);
			fprintf(stderr, ".");
			test_consumer(info, i, 0, 0);
		}
	}
	fprintf(stderr, " done\n\n");

	return NULL;
}

void print_help() {
	fprintf(stderr, "usage:\n");
	fprintf(stderr, "\tatomic_fifo [-w] [-u] [-p #] [-i #] [-x #]\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "\t-w\tuse the 'consume wait' atomic fifo function [default no]\n");
	fprintf(stderr, "\t-u\tuse the 'thread unsafe' version of atomic fifo produce [default no]\n");
	fprintf(stderr, "\t-p #\tnumber of producers [default 1]\n");
	fprintf(stderr, "\t-i #\tnumber of timed test iterations [default %u]\n", TEST_ITERATIONS);
	fprintf(stderr, "\t-x #\tnumber of test iterations excluded from timing [default %u]\n", EXCLUDE_ITERATIONS);
	fprintf(stderr, "\n");
}

int main (int argc, char * argv[]) {

	long num_producers = 1;
	unsigned do_consume_wait = 0;
	unsigned do_produce_unsafe = 0;
	long test_iterations = TEST_ITERATIONS;
	long exclude_iterations = EXCLUDE_ITERATIONS;

	if (argc > 1) {

		char * end = NULL;
		unsigned opt = 1;
		do {
			if (argv[opt][0] != '-') {
				print_help();
				exit(1);
			}

			switch (argv[opt][1]) {
				case 'p':
					++opt; if (argc <= opt) { print_help(); exit(1); }

					num_producers = strtol(argv[opt], &end, 10);
					if ((num_producers < 1) || (num_producers > 64)) {
						fprintf(stderr, "invalid number of producers: %ld\n", num_producers);
						abort();
					}
					break;

				case 'i':
					++opt; if (argc <= opt) { print_help(); exit(1); }

					test_iterations = strtol(argv[opt], &end, 10);
					if (test_iterations <= 0) {
						fprintf(stderr, "invalid number of test iterations: %ld\n", test_iterations);
						abort();
					}
					break;

				case 'x':
					++opt; if (argc <= opt) { print_help(); exit(1); }

					exclude_iterations = strtol(argv[opt], &end, 10);
					if (exclude_iterations < 0) {
						fprintf(stderr, "invalid number of exclude iterations: %ld\n", exclude_iterations);
						abort();
					}
					break;

				case 'w':
					do_consume_wait = 1;
					break;

				case 'u':
					do_produce_unsafe = 1;
					break;

				default:
					print_help();
					exit(1);
			}
			++opt;
		} while (opt < argc);
	}


	struct barrier_shared barrier;
	barrier_shared_init(&barrier);

	struct consumer_info c;
	fi_opx_atomic_fifo_init(&c.test[0].fifo, 16);
	fi_opx_atomic_fifo_init(&c.test[1].fifo, 256);
	fi_opx_atomic_fifo_init(&c.test[2].fifo, 1024);
	barrier_participant_init(&barrier, &c.barrier, num_producers+1);
	c.exclude_iterations = exclude_iterations;
	c.test_iterations = test_iterations;
	c.id = 0;
	c.test_count = 3;
	c.num_producers = num_producers;
	c.do_consume_wait = do_consume_wait;

	struct producer_info p[64];
	unsigned i, j;
	for (j=0; j<num_producers; ++j) {
		barrier_participant_init(&barrier, &p[j].barrier, num_producers+1);
		p[j].exclude_iterations = exclude_iterations;
		p[j].test_iterations = test_iterations;
		p[j].id = j;
		p[j].do_produce_unsafe = do_produce_unsafe;
		p[j].test_count = 3;
		for (i=0; i<3; ++i) {
			fi_opx_atomic_fifo_producer_init(&p[j].test[i].producer, &c.test[i].fifo);
		}

		int rc = pthread_create(&p[j].thread, NULL, pthread_producer, (void *)&p[j]);
		if (rc != 0) return -1;
	}


	pthread_consumer((void *)&c);


	fprintf(stdout, "# %10s %10s %10s%s\n", "fifo", "consumer", "producer", num_producers > 1 ? "s" : "");
	fprintf(stdout, "# %10s %10s %10s\n", "size", "usec", "usec");
	fprintf(stdout, "#\n");

	for (i=0; i<3; ++i) {

		const unsigned test_iterations = c.test_iterations;

		fprintf(stdout, "  %10lu %10.6f",
			c.test[i].fifo.size, c.test[i].elapsed_usec/((double)test_iterations));

		unsigned j;
		for (j=0; j<num_producers; ++j) {
			fprintf(stdout, " %10.6f", p[j].test[i].elapsed_usec/((double)test_iterations));
		};
		fprintf(stdout, "\n");
		fflush(stdout);
	}


	for (i=0; i<num_producers; ++i) {
		void * retval = NULL;
		int rc = pthread_join(p[i].thread, &retval);
		if (rc != 0) {
			fprintf(stderr, "join error\n"); abort();
		}
	}

	return 0;
}

