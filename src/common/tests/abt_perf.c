/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#include <abt.h>
#include <daos/common.h>
#include <getopt.h>
#include <time.h>

static unsigned long	abt_cntr;
static int		abt_ults;
static bool		abt_waiting;
static bool		abt_exiting;

static ABT_pool		abt_pool;
static ABT_cond		abt_cond;
static ABT_mutex	abt_lock;
static ABT_xstream	abt_xstream;
static ABT_thread_attr	abt_attr = ABT_THREAD_ATTR_NULL;
static char		*abt_name;

static int		opt_concur = 1;
static int		opt_secs;
static int		opt_stack;
static int		opt_cr_type;

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
	uint64_t	then;
	uint64_t	now;
	uint64_t	prt;
	int		rc;
	int		nsec = 0;

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

		rc = ABT_thread_create(abt_pool, abt_thread_1, NULL,
				       abt_attr, NULL);
		if (rc != ABT_SUCCESS) {
			printf("ABT thread create failed: %d\n", rc);
			return;
		}

		if (now - prt >= 1000) {
			nsec++;
			printf("Created %lu threads in %d seconds\n",
			       abt_cntr, nsec);
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
	uint64_t	then = 0;
	uint64_t	now;
	int		rc;

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

		rc = ABT_thread_create(abt_pool, abt_thread_2, NULL,
				       ABT_THREAD_ATTR_NULL, NULL);
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
	ABT_mutex	mutex;
	ABT_cond	cond;
	ABT_rwlock	rwlock;
	ABT_eventual	eventual;
	uint64_t	then;
	uint64_t	now;
	int		rc;

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
	printf("ABT %s creation rate = %lu/sec.\n",
		abt_name, abt_cntr / opt_secs);

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
	abt_cntr	= 0;
	abt_ults	= 0;
	abt_exiting	= false;
	abt_waiting	= false;
}

static struct option abt_ops[] = {
	/**
	 * test-id:
	 * m = mutext creation
	 * e = eventual creation
	 * d = condition creation
	 */
	{ "test",	required_argument,	NULL,	't'	},
	/**
	 * if test-id is 'c', it is the number of concurrent creation
	 * if test-id is 's', it is the total number of running ULTs
	 */
	{ "num",	required_argument,	NULL,	'n'	},
	/** test duration in seconds.  */
	{ "sec",	required_argument,	NULL,	's'	},
	/** stack size (kilo-bytes) */
	{ "stack",	required_argument,	NULL,	'S'	},
};

int
main(int argc, char **argv)
{
	char	test_id = 0;
	int	rc;

	while ((rc = getopt_long(argc, argv, "t:n:s:S:",
				 abt_ops, NULL)) != -1) {
		switch (rc) {
		default:
			fprintf(stderr, "unknown opc=%c\n", rc);
			exit(-1);
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
		}
	}

	if (opt_secs == 0) {
		printf("invalid sec=%s\n", argv[1]);
		return -1;
	}

	if (opt_concur == 0) {
		printf("invalid ABT threads=%s\n", argv[2]);
		return -1;
	}

	printf("Create ABT threads for %d seconds, concur=%d\n",
	       opt_secs, opt_concur);

	rc = ABT_init(0, NULL);
	if (rc != ABT_SUCCESS) {
		printf("ABT init failed: %d\n", rc);
		return -1;
	}

	rc = ABT_xstream_self(&abt_xstream);
	if (rc != ABT_SUCCESS) {
		printf("ABT get self xstream failed: %d\n", rc);
		return -1;
	}

	rc = ABT_xstream_get_main_pools(abt_xstream, 1, &abt_pool);
	if (rc != ABT_SUCCESS) {
		printf("ABT pool get failed: %d\n", rc);
		return -1;
	}

	rc = ABT_cond_create(&abt_cond);
	if (rc != ABT_SUCCESS) {
		printf("ABT cond create failed: %d\n", rc);
		return -1;
	}

	rc = ABT_mutex_create(&abt_lock);
	if (rc != ABT_SUCCESS) {
		printf("ABT mutex create failed: %d\n", rc);
		return -1;
	}

	if (opt_stack > 0) {
		rc = ABT_thread_attr_create(&abt_attr);
		if (rc != ABT_SUCCESS) {
			printf("ABT thread attr create failed: %d\n", rc);
			return -1;
		}

		rc = ABT_thread_attr_set_stacksize(abt_attr, opt_stack);
		D_ASSERT(rc == ABT_SUCCESS);
		printf("ULT stack size = %d\n", opt_stack);
	}

	switch (test_id) {
	default:
		break;
	case 'c':
		printf("ULT create rate test (concur=%d, secs=%d)\n",
		       opt_concur, opt_secs);
		abt_ult_create_rate();
		goto out;
	case 's':
		printf("ULT scheduling rate test (ULTs=%d, secs=%d)\n",
		       opt_concur, opt_secs);
		abt_sched_rate();
		goto out;
	case 'm':
		printf("mutex creation rate test (secs=%d)\n", opt_secs);
		opt_cr_type = CR_MUTEX;
		abt_name = "mutex";
		break;
	case 'w':
		printf("rwlock creation rate test (secs=%d)\n", opt_secs);
		opt_cr_type = CR_RWLOCK;
		abt_name = "rwlock";
		break;
	case 'e':
		printf("eventual creation rate test within ULT (secs=%d)\n",
		       opt_secs);
		opt_cr_type = CR_EVENTUAL;
		abt_name = "eventual";
		break;
	case 'd':
		printf("condition creation rate test within ULT (secs=%d)\n",
		       opt_secs);
		opt_cr_type = CR_COND;
		abt_name = "cond";
		break;
	}

	abt_waiting = true;
	rc = ABT_thread_create(abt_pool, abt_lock_create_rate, NULL,
			       ABT_THREAD_ATTR_NULL, NULL);

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
	return 0;
}
