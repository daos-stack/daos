/**
 * (C) Copyright 2017-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <getopt.h>
#include <time.h>
#include <abt.h>

#include <daos/common.h>
#include <gurt/common.h>
#include <daos_srv/daos_engine.h>

static unsigned long   abt_cntr;
static int             abt_ults;
static bool            abt_waiting;
static bool            abt_exiting;

static ABT_pool        abt_pool;
static ABT_cond        abt_cond;
static ABT_mutex       abt_lock;
static ABT_xstream     abt_xstream;
static ABT_thread_attr abt_attr = ABT_THREAD_ATTR_NULL;
static char           *abt_name;

static int             opt_concur = 1;
static int             opt_secs   = 0;
static ssize_t         opt_stack  = -1;
static int             opt_cr_type;

static void
usage(char *name, FILE *out)
{
	fprintf(out,
		"Usage:\n"
		"\t%s -t test_id -s sec [-n num_ult] [-S stack_size]\n"
		"\t%s -h\n"
		"\n"
		"Options:\n"
		"\t--test=<test id>, -t <test id>\n"
		"\t\tIdentifier of the test to run:\n"
		"\t\t\tc: ULT creation test\n"
		"\t\t\ts: ULT scheduling test\n"
		"\t\t\tm: mutex creation test\n"
		"\t\t\tw: rwlock creation test\n"
		"\t\t\te: eventual creation test\n"
		"\t\t\td: condition creation test\n"
		"\t--sec=<sec>, sn <sec>\n"
		"\t\tDuration in seconds of the test\n"
		"\t--num=<number of ult>, -n <number of ult>\n"
		"\t\tNumber of concurrent creation for ULT creation test\n"
		"\t\tNumber of ULT to schedule for ULT scheduling test\n"
		"\t--stack=<stack size>, -S <stack size>\n"
		"\t\tULT stack size\n"
		"\t--help, -h\n"
		"\t\tPrint this description\n",
		name, name);
}

static inline uint64_t
abt_current_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void
abt_thread_1(void *arg)
{
	ABT_mutex_lock(abt_lock);
	if (!abt_exiting && abt_ults < opt_concur) {
		/* less than the threshold, try to create more at here */
		abt_ults++;
		abt_cntr++;
		ABT_mutex_unlock(abt_lock);

		ABT_thread_create(abt_pool, abt_thread_1, NULL, abt_attr, NULL);

		ABT_mutex_lock(abt_lock);
	} /* else: do nothing and exit */

	abt_ults--;
	if (abt_waiting) {
		ABT_cond_broadcast(abt_cond);
		abt_waiting = false;
	}
	ABT_mutex_unlock(abt_lock);
}

/**
 * Create UTLs for a few seconds, concurrent creation is always lower
 * than @opt_concur.
 */
static void
abt_ult_create_rate(void)
{
	uint64_t then;
	uint64_t now;
	uint64_t prt;
	int      rc;
	int      nsec = 0;

	prt = now = then = abt_current_ms();
	while (1) {
		if (!abt_exiting) {
			now = abt_current_ms();
			if (now - then >= (uint64_t)opt_secs * 1000)
				abt_exiting = true;
		}

		ABT_mutex_lock(abt_lock);
		if (abt_exiting) {
			if (abt_ults == 0) { /* complete */
				ABT_mutex_unlock(abt_lock);
				break;
			}

			abt_waiting = true;
			ABT_cond_wait(abt_cond, abt_lock);
			ABT_mutex_unlock(abt_lock);
			continue;
		}

		if (abt_ults >= opt_concur) {
			abt_waiting = true;
			ABT_cond_wait(abt_cond, abt_lock);
			ABT_mutex_unlock(abt_lock);
			continue;
		}
		abt_ults++;
		abt_cntr++;
		ABT_mutex_unlock(abt_lock);

		rc = ABT_thread_create(abt_pool, abt_thread_1, NULL, abt_attr, NULL);
		if (rc != ABT_SUCCESS) {
			fprintf(stderr, "ABT thread create failed: " AF_RC "\n", AP_RC(rc));
			return;
		}

		if (now - prt >= 1000) {
			nsec++;
			printf("Created %lu threads in %d seconds\n", abt_cntr, nsec);
			prt = now;
		}
		ABT_thread_yield();
	}
	printf("ABT creation rate = %lu/sec.\n", abt_cntr / opt_secs);
}

static void
abt_thread_2(void *arg)
{
	ABT_mutex_lock(abt_lock);
	while (!abt_exiting) {
		abt_cntr++;
		ABT_mutex_unlock(abt_lock);

		ABT_thread_yield();

		ABT_mutex_lock(abt_lock);
	}
	abt_ults--;
	ABT_mutex_unlock(abt_lock);
}

/**
 * Create @opt_concur ULTs, and scheduling all of them for @opt_secs seconds.
 */
static void
abt_sched_rate(void)
{
	uint64_t then = 0;
	uint64_t now;
	int      rc;

	while (1) {
		if (then && !abt_exiting) {
			now = abt_current_ms();
			if (now - then >= (uint64_t)opt_secs * 1000)
				abt_exiting = true;
		}

		ABT_mutex_lock(abt_lock);
		if (abt_exiting) { /* run out of time */
			if (abt_ults == 0) {
				ABT_mutex_unlock(abt_lock);
				break;
			}
			abt_cntr++;
			ABT_mutex_unlock(abt_lock);
			ABT_thread_yield();
			continue;
		}

		if (abt_ults >= opt_concur) { /* no more ULT creation */
			if (then == 0) {
				then = abt_current_ms();
				printf("started all %d ULTs\n", abt_ults);
			}
			abt_cntr++;
			ABT_mutex_unlock(abt_lock);
			ABT_thread_yield();
			continue;
		}

		abt_ults++;
		ABT_mutex_unlock(abt_lock);

		rc = ABT_thread_create(abt_pool, abt_thread_2, NULL, ABT_THREAD_ATTR_NULL, NULL);
		if (rc != ABT_SUCCESS) {
			printf("ABT thread create failed: %d\n", rc);
			ABT_mutex_lock(abt_lock);
			abt_ults--;
			abt_exiting = true;
			ABT_mutex_unlock(abt_lock);
		}
	}
	printf("ABT scheduling rate = %lu/sec.\n", abt_cntr / opt_secs);
}

enum {
	CR_MUTEX,
	CR_RWLOCK,
	CR_COND,
	CR_EVENTUAL,
};

static void
abt_lock_create_rate(void *arg)
{
	ABT_mutex    mutex;
	ABT_cond     cond;
	ABT_rwlock   rwlock;
	ABT_eventual eventual;
	uint64_t     then;
	uint64_t     now;
	int          rc;

	then = abt_current_ms();
	while (1) {
		if (!abt_exiting) {
			now = abt_current_ms();
			if (now - then >= (uint64_t)opt_secs * 1000)
				abt_exiting = true;
		}

		if (abt_exiting)
			break;

		switch (opt_cr_type) {
		case CR_MUTEX:
			rc = ABT_mutex_create(&mutex);
			assert(rc == ABT_SUCCESS);
			ABT_mutex_free(&mutex);
			break;

		case CR_RWLOCK:
			rc = ABT_rwlock_create(&rwlock);
			assert(rc == ABT_SUCCESS);
			ABT_rwlock_free(&rwlock);
			break;

		case CR_COND:
			rc = ABT_cond_create(&cond);
			assert(rc == ABT_SUCCESS);
			ABT_cond_free(&cond);
			break;

		case CR_EVENTUAL:
			rc = ABT_eventual_create(sizeof(int), &eventual);
			assert(rc == ABT_SUCCESS);
			ABT_eventual_free(&eventual);
			break;
		}
		abt_cntr++;
	}
	printf("ABT %s creation rate = %lu/sec.\n", abt_name, abt_cntr / opt_secs);

	ABT_mutex_lock(abt_lock);
	if (abt_waiting) {
		ABT_cond_broadcast(abt_cond);
		abt_waiting = false;
	}
	ABT_mutex_unlock(abt_lock);
}

static void
abt_reset(void)
{
	abt_cntr    = 0;
	abt_ults    = 0;
	abt_exiting = false;
	abt_waiting = false;
}

static struct option abt_ops[] = {
    {"test", required_argument, NULL, 't'}, {"num", required_argument, NULL, 'n'},
    {"sec", required_argument, NULL, 's'},  {"stack", required_argument, NULL, 'S'},
    {"help", no_argument, NULL, 'h'},       {0, 0, 0, 0}};

int
main(int argc, char **argv)
{
	char        test_id = 0;
	const char *optstr  = "t:n:s:S:m:h";
	int         rc;

	while ((rc = getopt_long(argc, argv, optstr, abt_ops, NULL)) != -1) {
		switch (rc) {
		case 't':
			test_id = *optarg;
			break;
		case 'n':
			opt_concur = atoi(optarg);
			break;
		case 's':
			opt_secs = atoi(optarg);
			break;
		case 'S':
			opt_stack = atoi(optarg);
			opt_stack <<= 10; /* kilo-byte */
			break;
		case 'h':
			usage(argv[0], stdout);
			exit(EXIT_SUCCESS);
			break;
		default:
			usage(argv[0], stderr);
			exit(EXIT_FAILURE);
			break;
		}
	}

	if ((test_id == 'c' || test_id == 's') && opt_secs <= 0) {
		fprintf(stderr, "Missing test duration or invalid value.\n");
		usage(argv[0], stderr);
		exit(EXIT_FAILURE);
	}

	if (opt_concur <= 0) {
		fprintf(stderr, "Missing number of ULTs or invalid value.\n");
		usage(argv[0], stderr);
		exit(EXIT_FAILURE);
	}

	rc = daos_debug_init_ex("/dev/stdout", DLOG_INFO);
	if (rc != 0) {
		fprintf(stderr, "unable to create DAOS debug facities: " DF_RC "\n", DP_RC(rc));
		exit(EXIT_FAILURE);
	}

	rc = ABT_init(0, NULL);
	if (rc != ABT_SUCCESS) {
		fprintf(stderr, "Failed to init ABT: " AF_RC "\n", AP_RC(rc));
		exit(EXIT_FAILURE);
	}

	rc = ABT_xstream_self(&abt_xstream);
	if (rc != ABT_SUCCESS) {
		fprintf(stderr, "ABT get self xstream failed: " AF_RC "\n", AP_RC(rc));
		exit(EXIT_FAILURE);
	}

	rc = ABT_xstream_get_main_pools(abt_xstream, 1, &abt_pool);
	if (rc != ABT_SUCCESS) {
		fprintf(stderr, "ABT pool get failed: " AF_RC "\n", AP_RC(rc));
		exit(EXIT_FAILURE);
	}

	rc = ABT_cond_create(&abt_cond);
	if (rc != ABT_SUCCESS) {
		fprintf(stderr, "ABT cond create failed: " AF_RC "\n", AP_RC(rc));
		exit(EXIT_FAILURE);
	}

	rc = ABT_mutex_create(&abt_lock);
	if (rc != ABT_SUCCESS) {
		fprintf(stderr, "ABT mutex create failed: " AF_RC "\n", AP_RC(rc));
		exit(EXIT_FAILURE);
	}

	if (opt_stack > 0) {
		rc = ABT_thread_attr_create(&abt_attr);
		if (rc != ABT_SUCCESS) {
			fprintf(stderr, "ABT thread attr create failed: " AF_RC "\n", AP_RC(rc));
			exit(EXIT_FAILURE);
		}

		rc = ABT_thread_attr_set_stacksize(abt_attr, opt_stack);
		if (rc != ABT_SUCCESS) {
			fprintf(stderr, "Setting ABT thread stack size to %zd failed: " AF_RC "\n",
				opt_stack, AP_RC(rc));
			exit(EXIT_FAILURE);
		}
		printf("ULT stack size = %zd\n", opt_stack);
	} else {
		printf("ULT stack size = default ABT ULT stack size\n");
	}

	switch (test_id) {
	default:
		break;
	case 'c':
		printf("ULT create rate test (concur=%d, secs=%d)\n", opt_concur, opt_secs);
		abt_ult_create_rate();
		goto out;
	case 's':
		printf("ULT scheduling rate test (ULTs=%d, secs=%d)\n", opt_concur, opt_secs);
		abt_sched_rate();
		goto out;
	case 'm':
		printf("mutex creation rate test (secs=%d)\n", opt_secs);
		opt_cr_type = CR_MUTEX;
		abt_name    = "mutex";
		break;
	case 'w':
		printf("rwlock creation rate test (secs=%d)\n", opt_secs);
		opt_cr_type = CR_RWLOCK;
		abt_name    = "rwlock";
		break;
	case 'e':
		printf("eventual creation rate test within ULT (secs=%d)\n", opt_secs);
		opt_cr_type = CR_EVENTUAL;
		abt_name    = "eventual";
		break;
	case 'd':
		printf("condition creation rate test within ULT (secs=%d)\n", opt_secs);
		opt_cr_type = CR_COND;
		abt_name    = "cond";
		break;
	}

	abt_waiting = true;
	rc = ABT_thread_create(abt_pool, abt_lock_create_rate, NULL, ABT_THREAD_ATTR_NULL, NULL);
	if (rc != ABT_SUCCESS) {
		fprintf(stderr, "ABT thread create failed: " AF_RC "\n", AP_RC(rc));
		exit(EXIT_FAILURE);
	}

	ABT_mutex_lock(abt_lock);
	if (abt_waiting)
		ABT_cond_wait(abt_cond, abt_lock);
	ABT_mutex_unlock(abt_lock);
out:
	abt_reset();
	if (abt_attr != ABT_THREAD_ATTR_NULL)
		ABT_thread_attr_free(&abt_attr);

	ABT_mutex_free(&abt_lock);
	ABT_cond_free(&abt_cond);
	ABT_finalize();
	daos_debug_fini();

	return 0;
}
