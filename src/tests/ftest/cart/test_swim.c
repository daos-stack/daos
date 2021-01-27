/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This is a simple example of SWIM implementation on top of CaRT APIs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <getopt.h>
#include <sys/queue.h>

#include <gurt/common.h>
#include <cart/swim.h>

#define USE_CART_FOR_DEBUG_LOG 1

#define MEMBERS_MAX  1000
#define FAILURES_MAX 1000

static int failures;
static size_t members_count;
static swim_id_t failed_member = SWIM_ID_INVALID;

static size_t pkt_total;
static size_t pkt_failed;

struct network_pkt {
	TAILQ_ENTRY(network_pkt)	 np_link;
	swim_id_t			 np_from;
	swim_id_t			 np_to;
	struct swim_member_update	*np_upds;
	size_t				 np_nupds;
};

struct swim_target {
	CIRCLEQ_ENTRY(swim_target)	 st_link;
	swim_id_t			 st_id;
};

static struct global {
	pthread_mutex_t mutex;
	pthread_t progress_tid;
	pthread_t network_tid;
	TAILQ_HEAD(, network_pkt) pkts;
	CIRCLEQ_HEAD(, swim_target) target_list[MEMBERS_MAX];
	struct swim_target *target[MEMBERS_MAX];
	struct swim_member_state swim_ms[MEMBERS_MAX][MEMBERS_MAX];
	struct swim_context *swim_ctx[MEMBERS_MAX];
	uint64_t detect_timestamp[MEMBERS_MAX];
	uint64_t fail_timestamp;
	uint64_t detect_min;
	uint64_t detect_max;
	int shutdown;
} g;

static int test_send_message(struct swim_context *ctx, swim_id_t to,
			     struct swim_member_update *upds,
			     size_t nupds)
{
	struct network_pkt *item;
	int rc = 0;

	item = malloc(sizeof(*item));
	if (item != NULL) {
		item->np_from  = swim_self_get(ctx);
		item->np_to    = to;
		item->np_upds  = upds;
		item->np_nupds = nupds;

		rc = pthread_mutex_lock(&g.mutex);
		D_ASSERT(rc == 0);
		TAILQ_INSERT_TAIL(&g.pkts, item, np_link);
		rc = pthread_mutex_unlock(&g.mutex);
		D_ASSERT(rc == 0);
	}

	return rc;
}

static swim_id_t test_get_dping_target(struct swim_context *ctx)
{
	swim_id_t self = swim_self_get(ctx);
	static swim_id_t id;
	int count = 0;

	do {
		if (count++ > members_count)
			return SWIM_ID_INVALID;
		g.target[self] = CIRCLEQ_LOOP_NEXT(&g.target_list[self],
						   g.target[self], st_link);
		id = g.target[self]->st_id;
	} while (id == self ||
		 g.swim_ms[self][id].sms_status == SWIM_MEMBER_DEAD);
	return id;
}

static swim_id_t test_get_iping_target(struct swim_context *ctx)
{
	swim_id_t self = swim_self_get(ctx);
	static swim_id_t id;
	int count = 0;

	do {
		if (count++ > members_count)
			return SWIM_ID_INVALID;
		g.target[self] = CIRCLEQ_LOOP_NEXT(&g.target_list[self],
						   g.target[self], st_link);
		id = g.target[self]->st_id;
	} while (id == self ||
		 g.swim_ms[self][id].sms_status != SWIM_MEMBER_ALIVE);
	return id;
}

static int test_get_member_state(struct swim_context *ctx,
				 swim_id_t id, struct swim_member_state *state)
{
	swim_id_t self = swim_self_get(ctx);
	int rc = 0;

	*state = g.swim_ms[self][id];
	return rc;
}

static int test_set_member_state(struct swim_context *ctx,
				 swim_id_t id, struct swim_member_state *state)
{
	swim_id_t self = swim_self_get(ctx);
	enum swim_member_status s;
	struct timespec now;
	uint64_t t;
	int i, cnt, rc = 0;

	switch (state->sms_status) {
	case SWIM_MEMBER_INACTIVE:
		break;
	case SWIM_MEMBER_ALIVE:
		break;
	case SWIM_MEMBER_SUSPECT:
		break;
	case SWIM_MEMBER_DEAD:
		if (id == failed_member) {
			rc = clock_gettime(CLOCK_MONOTONIC, &now);
			if (!rc) {
				g.detect_timestamp[self] = now.tv_sec;
				t = g.detect_timestamp[self]
				  - g.fail_timestamp;
				if (t < g.detect_min)
					g.detect_min = t;
				if (t > g.detect_max)
					g.detect_max = t;
			} else {
				fprintf(stderr, "%lu: clock_gettime() "
					"error %d\n", self, rc);
			}
		} else if (self != failed_member) {
			fprintf(stdout, "%lu: false DEAD %lu\n", self, id);
			abort();
		}
		break;
	default:
		fprintf(stderr, "%lu: notify %lu unknown\n", self, id);
		break;
	}

	g.swim_ms[self][id] = *state;

	cnt = 0;
	for (i = 0; i < members_count; i++) {
		if (i != failed_member) {
			s = g.swim_ms[i][failed_member].sms_status;
			if (s == SWIM_MEMBER_DEAD)
				cnt++;
		}
	}
	if (cnt == members_count - 1)
		g.shutdown = 1;

	return rc;
}

int test_run(void)
{
	struct timespec now;
	int cnt, t, rc = 0;

	sleep(1);

	/* print the state of all members from all targets */
	for (cnt = 0; !g.shutdown; cnt++) {
		t = rand() % 10;

		if (failed_member == SWIM_ID_INVALID) {
			fprintf(stdout, ".");
			fflush(stdout);
		}

		if (cnt > t && failed_member == SWIM_ID_INVALID) {
			rc = clock_gettime(CLOCK_MONOTONIC, &now);
			if (!rc) {
				g.fail_timestamp = now.tv_sec;
				failed_member = rand() % members_count;
				fprintf(stdout, "\n*** FAIL member %lu ***\n",
					failed_member);
				fflush(stdout);
			} else {
				fprintf(stderr, "clock_gettime() error %d\n",
					rc);
			}
		}
		sleep(1);
	}

	fprintf(stderr, "\nWith %zu members failure was detected after:\n"
		"min %lu sec (%lu ticks), max %lu sec (%lu ticks)\n",
		members_count, g.detect_min, (g.detect_min+1)/2,
		g.detect_max, (g.detect_max+1)/2);

	return rc;
}

static int cur_core;

static void *network_thread(void *arg)
{
	static size_t pkt_last;
	struct network_pkt *item;
	cpu_set_t	cpuset;
	int		num_cores = sysconf(_SC_NPROCESSORS_ONLN);
	int		rc = 0;

	CPU_ZERO(&cpuset);
	CPU_SET(++cur_core % num_cores, &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	fprintf(stderr, "network  thread running on core %d\n", sched_getcpu());

	do {
		rc = pthread_mutex_lock(&g.mutex);
		D_ASSERT(rc == 0);
		item = TAILQ_FIRST(&g.pkts);
		if (item != NULL) {
			TAILQ_REMOVE(&g.pkts, item, np_link);
			rc = pthread_mutex_unlock(&g.mutex);
			D_ASSERT(rc == 0);

			pkt_total++;
			if (!(rand() % failures)) {
				pkt_failed++;
				fprintf(stdout, "DROP RPC %lu ==> %lu\n",
					item->np_from, item->np_to);
				fflush(stdout);
			} else if (failed_member != item->np_from &&
				   failed_member != item->np_to) {
				/* emulate RPC receive by target */
				rc = swim_parse_message(g.swim_ctx[item->np_to],
						   item->np_from, item->np_upds,
						   item->np_nupds);
				if (rc) {
					fprintf(stderr, "swim_parse_message() "
						" error %d\n", rc);
				}
			}

			D_FREE(item->np_upds);
			free(item);
		} else {
			rc = pthread_mutex_unlock(&g.mutex);
			D_ASSERT(rc == 0);
		}

		if (pkt_last != pkt_total && !(pkt_total % members_count)) {
			pkt_last = pkt_total;
			fprintf(stderr, "packets: %6zu, net drops: %3zu\n",
				pkt_total, pkt_failed);
		}
	} while (!g.shutdown);

	fprintf(stderr, "network  thread exit rc=%d\n", rc);

	pthread_exit(NULL);
}

static void *progress_thread(void *arg)
{
	int		num_cores = sysconf(_SC_NPROCESSORS_ONLN);
	cpu_set_t	cpuset;
	int64_t		timeout = 0;
	int		i, rc = 0;

	CPU_ZERO(&cpuset);
	CPU_SET(++cur_core % num_cores, &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	fprintf(stderr, "progress thread running on core %d\n", sched_getcpu());

	do {
		for (i = 0; i < members_count; i++) {
			rc = swim_progress(g.swim_ctx[i], timeout);
			if (rc == -ESHUTDOWN)
				g.shutdown = 1;
		}
		usleep(2000);
	} while (!g.shutdown);

	fprintf(stderr, "progress thread exit rc=%d\n", rc);

	pthread_exit(NULL);
}

static struct swim_ops swim_ops = {
	.send_message     = &test_send_message,
	.get_dping_target = &test_get_dping_target,
	.get_iping_target = &test_get_iping_target,
	.get_member_state = &test_get_member_state,
	.set_member_state = &test_set_member_state,
};

int test_fini(void)
{
	struct swim_target *st;
	int i, rc = 0;

	g.shutdown = 1;

	if (g.network_tid) {
		rc = pthread_join(g.network_tid, NULL);
		if (rc)
			fprintf(stderr, "pthread_join() failed rc=%d\n", rc);
	}

	if (g.progress_tid) {
		rc = pthread_join(g.progress_tid, NULL);
		if (rc)
			fprintf(stderr, "pthread_join() failed rc=%d\n", rc);
	}

	rc = pthread_mutex_destroy(&g.mutex);
	D_ASSERT(rc == 0);

	for (i = 0; i < members_count; i++) {
		swim_fini(g.swim_ctx[i]);

		while (!CIRCLEQ_EMPTY(&g.target_list[i])) {
			st = CIRCLEQ_FIRST(&g.target_list[i]);
			CIRCLEQ_REMOVE(&g.target_list[i], st, st_link);
			free(st);
		}
	}

	return rc;
}

#ifdef USE_CART_FOR_DEBUG_LOG
extern int crt_init_opt(char *grpid, uint32_t flags, void *opt);
#endif

int test_init(void)
{
	struct swim_target *st;
	int i, j, n, rc = -EFAULT;

#ifdef USE_CART_FOR_DEBUG_LOG
	rc = crt_init_opt("test_swim", 2, NULL);
	if (rc) /* need to logging only, therefore ignore all errors */
		fprintf(stderr, " crt_init failed %d\n", rc);
#endif
	memset(&g, 0, sizeof(g));

	TAILQ_INIT(&g.pkts);
	g.shutdown   = 0;
	g.detect_min = UINT64_MAX;
	g.detect_max = 0;

	for (i = 0; i < members_count; i++) {
		CIRCLEQ_INIT(&g.target_list[i]);

		st = malloc(sizeof(struct swim_target));
		if (st == NULL) {
			fprintf(stderr, "malloc() for swim_target failed\n");
			goto out;
		}
		st->st_id = i;
		CIRCLEQ_INSERT_HEAD(&g.target_list[i], st, st_link);
		g.target[i] = st;

		for (j = 0; j < members_count; j++) {
			if (i != j) {
				st = malloc(sizeof(struct swim_target));
				if (st == NULL) {
					fprintf(stderr, "malloc() for "
							"swim_target failed\n");
					goto out;
				}
				st->st_id = j;
				CIRCLEQ_INSERT_AFTER(&g.target_list[i],
						     g.target[i], st, st_link);

				for (n = 1 + rand() % (j + 1); n > 0; n--)
					g.target[i] = CIRCLEQ_LOOP_NEXT(
							&g.target_list[i],
							g.target[i], st_link);
			}

			g.swim_ms[i][j].sms_incarnation = 0;
			g.swim_ms[i][j].sms_status = SWIM_MEMBER_ALIVE;
		}

		g.swim_ctx[i] = swim_init(i, &swim_ops, &g);
		if (g.swim_ctx[i] == NULL) {
			fprintf(stderr, "swim_init() failed\n");
			goto out;
		}
	}

	rc = pthread_mutex_init(&g.mutex, NULL);
	if (rc) {
		fprintf(stderr, "pthread_mutex_init() failed rc=%d\n", rc);
		goto out;
	}

	rc = pthread_create(&g.network_tid, NULL, network_thread, NULL);
	if (rc) {
		fprintf(stderr, "pthread_create() failed rc=%d\n", rc);
		goto out;
	}

	rc = pthread_create(&g.progress_tid, NULL, progress_thread, NULL);
	if (rc) {
		fprintf(stderr, "pthread_create() failed rc=%d\n", rc);
		goto out;
	}
out:
	return rc;
}

int test_parse_args(int argc, char **argv)
{
	unsigned int nr;
	int option_index = 0;
	int rc = 0;
	char *end;
	struct option long_options[] = {
		{"size", required_argument, 0, 's'},
		{"failures", no_argument, 0, 'f'},
		{0, 0, 0, 0}
	};

	while (1) {
		rc = getopt_long(argc, argv, "s:f:", long_options,
				 &option_index);
		if (rc == -1)
			break;
		switch (rc) {
		case 0:
			if (long_options[option_index].flag != 0)
				break;
		case 's':
			nr = strtoul(optarg, &end, 10);
			if (end == optarg || nr < 2 || nr > MEMBERS_MAX) {
				fprintf(stderr, "size %d not in range "
					"[%d, %d], using %d for test.\n", nr,
					2, MEMBERS_MAX, MEMBERS_MAX);
			} else {
				members_count = nr;
				fprintf(stderr, "will use %d members.\n", nr);
			}
			break;
		case 'f':
			nr = strtoul(optarg, &end, 10);
			if (end == optarg || nr < 10 || nr > FAILURES_MAX) {
				fprintf(stderr, "failures 1/%d not in range "
					"[1/%d, 1/%d], using 1/%d for test.\n",
					nr, 10, FAILURES_MAX, FAILURES_MAX);
			} else {
				failures = nr;
				fprintf(stderr, "will introduce 1/%d "
					"failures.\n", nr);
			}
			break;
		case '?':
			return 1;
		default:
			return 1;
		}
	}
	if (optind < argc) {
		fprintf(stderr, "non-option argv elements encountered");
		return 1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct timespec now;
	int rc;

	failures = FAILURES_MAX;
	members_count = MEMBERS_MAX;
	rc = test_parse_args(argc, argv);
	if (rc != 0) {
		fprintf(stderr, "test_parse_args() failed rc=%d\n", rc);
		return rc;
	}

	rc = clock_gettime(CLOCK_MONOTONIC, &now);
	if (!rc)
		srand(now.tv_nsec + getpid());
	rc = test_init();
	if (!rc)
		rc = test_run();
	test_fini();

	return rc;
}
