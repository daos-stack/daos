/*
 * (C) Copyright 2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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

#include "../../../cart/swim/swim_internal.h"

#define USE_CART_FOR_DEBUG_LOG 1

#define MEMBERS_MAX	10000
#define GLITCHES_MIN	1
#define GLITCHES_MAX	1000
#define FAILURES_MIN	1
#define FAILURES_MAX	1000

static int verbose;
static int glitches;
static int failures;
static int net_delay;
static size_t members_count;
static swim_id_t victim = SWIM_ID_INVALID;

static size_t pkt_sent;
static size_t pkt_total;
static size_t pkt_glitch;

struct network_pkt {
	TAILQ_ENTRY(network_pkt)	 np_link;
	swim_id_t			 np_from;
	swim_id_t			 np_to;
	struct swim_member_update	*np_upds;
	size_t				 np_nupds;
	uint64_t			 np_time;
};

struct swim_target {
	CIRCLEQ_ENTRY(swim_target)	 st_link;
	swim_id_t			 st_id;
};

static struct global {
	pthread_spinlock_t		 lock;
	pthread_t			 progress_tid;
	pthread_t			 network_tid;
	TAILQ_HEAD(, network_pkt)	 pkts;
	struct swim_member_state	 swim_state[MEMBERS_MAX][MEMBERS_MAX];
	CIRCLEQ_HEAD(, swim_target)	 target_list[MEMBERS_MAX];
	struct swim_target		*target[MEMBERS_MAX];
	struct swim_context		*swim_ctx[MEMBERS_MAX];
	/* SWIM statistics: */
	uint64_t			 detect_sec[MEMBERS_MAX];
	uint64_t			 victim_sec;
	uint64_t			 detect_min;
	uint64_t			 detect_max;
	/* SWIM control flags: */
	unsigned int			 shutdown:1;
} g;

static int test_send_message(struct swim_context *ctx, swim_id_t to,
			     struct swim_member_update *upds,
			     size_t nupds)
{
	struct network_pkt *item;
	int rc = -DER_NOMEM;

	item = malloc(sizeof(*item));
	if (item != NULL) {
		item->np_from  = swim_self_get(ctx);
		item->np_to    = to;
		item->np_upds  = upds;
		item->np_nupds = nupds;
		item->np_time  = swim_now_ms();

		D_SPIN_LOCK(&g.lock);
		TAILQ_INSERT_TAIL(&g.pkts, item, np_link);
		pkt_sent++;
		D_SPIN_UNLOCK(&g.lock);

		rc = 0;
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
		 g.swim_state[self][id].sms_status == SWIM_MEMBER_DEAD);
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
		 g.swim_state[self][id].sms_status != SWIM_MEMBER_ALIVE);
	return id;
}

static int test_get_member_state(struct swim_context *ctx,
				 swim_id_t id, struct swim_member_state *state)
{
	swim_id_t self = swim_self_get(ctx);
	int rc = 0;

	if (self == SWIM_ID_INVALID)
		return -EINVAL;

	*state = g.swim_state[self][id];
	return rc;
}

static int test_set_member_state(struct swim_context *ctx,
				 swim_id_t id, struct swim_member_state *state)
{
	swim_id_t self_id = swim_self_get(ctx);
	enum swim_member_status s;
	struct timespec now;
	uint64_t sec;
	int i, cnt, rc = 0;

	if (self_id == SWIM_ID_INVALID)
		return -EINVAL;

	switch (state->sms_status) {
	case SWIM_MEMBER_INACTIVE:
		break;
	case SWIM_MEMBER_ALIVE:
		break;
	case SWIM_MEMBER_SUSPECT:
		state->sms_delay += swim_ping_timeout_get();
		break;
	case SWIM_MEMBER_DEAD:
		if (id == victim) {
			rc = clock_gettime(CLOCK_MONOTONIC, &now);
			if (!rc) {
				g.detect_sec[self_id] = now.tv_sec;
				sec = g.detect_sec[self_id] - g.victim_sec;
				if (sec < g.detect_min)
					g.detect_min = sec;
				if (sec > g.detect_max)
					g.detect_max = sec;
			} else {
				fprintf(stderr, "%lu: clock_gettime() "
					"error %d\n", self_id, rc);
			}
		} else if (self_id != victim) {
			fprintf(stdout, "%lu: false DEAD %lu\n", self_id, id);
			abort();
		}
		break;
	default:
		fprintf(stderr, "%lu: notify %lu unknown\n", self_id, id);
		break;
	}

	g.swim_state[self_id][id] = *state;

	cnt = 0;
	for (i = 0; i < members_count; i++) {
		if (i != victim) {
			s = g.swim_state[i][victim].sms_status;
			if (s == SWIM_MEMBER_DEAD)
				cnt++;
		}
	}
	if (cnt == members_count - 1) {
		fprintf(stdout, "DEAD detected by all members. Shutdown...\n");
		g.shutdown = 1;
	}

	return rc;
}

int test_run(void)
{
	enum swim_member_status s;
	struct timespec now;
	uint64_t time = swim_now_ms();
	int i, j, cs, cd, tick, rc = 0;

	sleep(1);
	fprintf(stderr, "-=main=- thread running on core %d\n", sched_getcpu());
	for (i = 0; i < members_count; i++)
		g.swim_ctx[i]->sc_next_tick_time = time;

	tick = 0;
	/* print the state of all members from all targets */
	while (!g.shutdown && g.swim_ctx[0]->sc_self != SWIM_ID_INVALID) {
		if (time != g.swim_ctx[0]->sc_next_tick_time) {
			time = g.swim_ctx[0]->sc_next_tick_time;
			tick++;

			if (verbose) {
				fprintf(stdout, "     ");
				for (i = 0; i < members_count; i++)
					fprintf(stdout, " [%2u]", i);
				fprintf(stdout, "avg:self\n");
			}

			cs = 0;
			cd = 0;
			for (i = 0; i < members_count; i++) {
				uint32_t dmin, dmax, davg, delay;

				dmin = UINT32_MAX;
				dmax = 0;
				davg = 0;
				for (j = 0; j < members_count; j++) {
					if (i == j)
						continue;
					delay = g.swim_state[i][j].sms_delay;
					if (delay < dmin)
						dmin = delay;
					if (delay > dmax)
						dmax = delay;
					davg += delay;
				}
				davg /= members_count - 1;

				s = g.swim_state[i][victim].sms_status;
				if (s == SWIM_MEMBER_SUSPECT)
					cs++;
				if (s == SWIM_MEMBER_DEAD)
					cd++;

				if (verbose) {
					fprintf(stdout, "[%2u]", i);
					for (j = 0; j < members_count; j++) {
						delay = g.swim_state[i][j].sms_delay;
						fprintf(stdout, " %4u", delay);
					}
					delay = g.swim_state[i][i].sms_delay;
					fprintf(stdout, " %3u:%u\n", davg, delay);
				}
			}

			fprintf(stdout, "%3d. ALIVE=%zu\tSUSPECT=%u\tDEAD=%u\n",
				tick, members_count - cs - cd, cs, cd);
			fflush(stdout);
		}

		if (victim == SWIM_ID_INVALID && !g.victim_sec && tick > 0) {
			rc = clock_gettime(CLOCK_MONOTONIC, &now);
			if (!rc) {
				victim = rand() % members_count;
				g.victim_sec = now.tv_sec;

				fprintf(stdout, "%3d. *** VICTIM %lu ***\n",
					tick, victim);
				fflush(stdout);
			} else {
				fprintf(stderr, "clock_gettime() rc=%d\n", rc);
			}
		}
		usleep(1000);
	}
	g.shutdown = 1;

	fprintf(stderr, "\nWith %zu members failure was detected after:\n"
		"min %lu sec max %lu sec\n",
		members_count, g.detect_min, g.detect_max);

	return rc;
}

/*
 * NB: Keep this functionality the same as in crt_swim_srv_cb() !!!
 */
static void deliver_pkt(struct network_pkt *item)
{
	swim_id_t id;
	swim_id_t self_id = item->np_to;
	swim_id_t from_id = item->np_from;
	struct swim_context *ctx = g.swim_ctx[self_id];
	struct swim_member_state *state;
	uint64_t max_delay, rcv_delay, snd_delay;
	int i, rc = 0;

	max_delay = swim_ping_timeout_get() / 2;
	rcv_delay = swim_now_ms() - item->np_time;

	for (i = 0; i < item->np_nupds; i++) {
		id = item->np_upds[i].smu_id;
		state = &item->np_upds[i].smu_state;
		snd_delay = g.swim_state[self_id][id].sms_delay;
		snd_delay = snd_delay ? (snd_delay + state->sms_delay) / 2
				      : state->sms_delay;
		g.swim_state[self_id][id].sms_delay = snd_delay;
	}

	snd_delay = g.swim_state[self_id][from_id].sms_delay;
	snd_delay = snd_delay ? (snd_delay + rcv_delay) / 2 : rcv_delay;
	g.swim_state[self_id][from_id].sms_delay = snd_delay;

	if (rcv_delay > max_delay)
		swim_net_glitch_update(ctx, self_id, rcv_delay - max_delay);
	else if (snd_delay > max_delay)
		swim_net_glitch_update(ctx, from_id, snd_delay - max_delay);

	/* emulate RPC receive by target */
	rc = swim_parse_message(ctx, from_id, item->np_upds, item->np_nupds);
	if (rc == -ESHUTDOWN)
		swim_self_set(ctx, SWIM_ID_INVALID);
	else if (rc)
		fprintf(stderr, "swim_parse_message() rc=%d\n", rc);
}

static int cur_core;

static void *network_thread(void *arg)
{
	struct network_pkt *item;
	cpu_set_t	cpuset;
	int		num_cores = sysconf(_SC_NPROCESSORS_ONLN);
	int		rc = 0;

	CPU_ZERO(&cpuset);
	CPU_SET(++cur_core % num_cores, &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	fprintf(stderr, "network  thread running on core %d\n", sched_getcpu());

	do {
		D_SPIN_LOCK(&g.lock);
		item = TAILQ_FIRST(&g.pkts);
		if (item != NULL) {
			TAILQ_REMOVE(&g.pkts, item, np_link);
			pkt_total++;
			D_SPIN_UNLOCK(&g.lock);

			if (!(rand() % glitches)) {
				usleep(rand() % (6 * net_delay));
				D_SPIN_LOCK(&g.lock);
				TAILQ_INSERT_TAIL(&g.pkts, item, np_link);
				pkt_glitch++;
				D_SPIN_UNLOCK(&g.lock);
			} else {
				if (!(!(rand() % failures) &&
				      (item->np_from == victim ||
				       item->np_to == victim)))
					deliver_pkt(item);

				D_FREE(item->np_upds);
				free(item);
			}
		} else {
			D_SPIN_UNLOCK(&g.lock);
			usleep(1000);
		}

		usleep(rand() % (3 * net_delay));
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
				swim_self_set(g.swim_ctx[i], SWIM_ID_INVALID);
			else if (rc && rc != -ETIMEDOUT)
				fprintf(stderr, "swim_progress() rc=%d\n", rc);
		}
		usleep(100);
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
			fprintf(stderr, "pthread_join() rc=%d\n", rc);
	}

	if (g.progress_tid) {
		rc = pthread_join(g.progress_tid, NULL);
		if (rc)
			fprintf(stderr, "pthread_join() rc=%d\n", rc);
	}

	for (i = 0; i < members_count; i++) {
		swim_fini(g.swim_ctx[i]);

		while (!CIRCLEQ_EMPTY(&g.target_list[i])) {
			st = CIRCLEQ_FIRST(&g.target_list[i]);
			CIRCLEQ_REMOVE(&g.target_list[i], st, st_link);
			free(st);
		}
	}

	D_SPIN_DESTROY(&g.lock);

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
	rc = crt_init_opt("test_swim", 2 /*CRT_FLAG_BIT_AUTO_SWIM_DISABLE*/,
			  NULL);
	if (rc) /* need to logging only, therefore ignore all errors */
		fprintf(stderr, "crt_init() rc=%d\n", rc);
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

			g.swim_state[i][j].sms_incarnation = 0;
			g.swim_state[i][j].sms_status = SWIM_MEMBER_ALIVE;
		}

		g.swim_ctx[i] = swim_init(i, &swim_ops, &g);
		if (g.swim_ctx[i] == NULL) {
			fprintf(stderr, "swim_init() failed\n");
			goto out;
		}
	}

	rc = D_SPIN_INIT(&g.lock, PTHREAD_PROCESS_PRIVATE);
	if (rc) {
		fprintf(stderr, "D_SPIN_INIT() rc=%d\n", rc);
		goto out;
	}

	rc = pthread_create(&g.network_tid, NULL, network_thread, NULL);
	if (rc) {
		fprintf(stderr, "pthread_create() rc=%d\n", rc);
		goto out;
	}

	rc = pthread_create(&g.progress_tid, NULL, progress_thread, NULL);
	if (rc) {
		fprintf(stderr, "pthread_create() rc=%d\n", rc);
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
		{"size",     required_argument, 0, 's'},
		{"glitches", required_argument, 0, 'g'},
		{"failures", required_argument, 0, 'f'},
		{"delay",    required_argument, 0, 'd'},
		{"verbose",  no_argument, &verbose, 1},
		{0, 0, 0, 0}
	};

	while (1) {
		rc = getopt_long(argc, argv, "?s:g:f:d:v", long_options,
				 &option_index);
		if (rc == -1)
			break;
		switch (rc) {
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
		case 'g':
			nr = strtoul(optarg, &end, 10);
			if (end == optarg ||
			    nr < GLITCHES_MIN || nr > GLITCHES_MAX) {
				fprintf(stderr, "glitches 1/%d not in range "
					"[1/%d, 1/%d], using 1/%d for test.\n",
					nr, GLITCHES_MIN, GLITCHES_MAX,
					glitches);
			} else {
				glitches = nr;
				fprintf(stderr, "will introduce 1/%d "
					"glitches.\n", nr);
			}
			break;
		case 'f':
			nr = strtoul(optarg, &end, 10);
			if (end == optarg ||
			    nr < FAILURES_MIN || nr > FAILURES_MAX) {
				fprintf(stderr, "failures 1/%d not in range "
					"[1/%d, 1/%d], using 1/%d for test.\n",
					nr, FAILURES_MIN, FAILURES_MAX,
					failures);
			} else {
				failures = nr;
				fprintf(stderr, "will introduce 1/%d "
					"failures.\n", nr);
			}
			break;
		case 'd':
			nr = strtoul(optarg, &end, 10);
			if (end == optarg ||
			    nr < 1 || nr > 1000) {
				fprintf(stderr, "delay %d not in range "
					"[%d, %d], using %d usec for test.\n",
					nr, 1, 1000,
					net_delay);
			} else {
				net_delay = nr;
				fprintf(stderr, "will use %d usec "
					"net delay.\n", nr);
			}
			break;
		case 'v':
			verbose = 1;
			break;
		case 0:
			if (long_options[option_index].flag != 0)
				break;
		case '?':
		default:
			fprintf(stderr, "Usage: %s [options]\n", argv[0]);
			fprintf(stderr, "Options are:\n"
"-s (--size)     : count of SWIM members (group size)\n"
"-g (--glitches) : how many glitches will be introduced in communication\n"
"-f (--failures) : how many failures will be introduced in communication\n"
"-d (--delay)    : the amount of communication delay for each packet in usec\n"
"-v              : verbose output about internal state during simulation\n");
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

	glitches = GLITCHES_MAX;
	failures = FAILURES_MAX;
	net_delay = 10;
	members_count = 1000;
	rc = test_parse_args(argc, argv);
	if (rc)
		return rc;

	rc = clock_gettime(CLOCK_MONOTONIC, &now);
	if (!rc)
		srand(now.tv_nsec + getpid());
	rc = test_init();
	if (!rc)
		rc = test_run();
	test_fini();

	return rc;
}
