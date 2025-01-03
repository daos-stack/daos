/**
 * (C) Copyright 2017-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>
#include <abt.h>

#include <daos/common.h>
#include <gurt/common.h>

static size_t g_stack_size     = 0;
static size_t g_total_size     = 0;
static void  *g_stack_start    = NULL;
static void  *g_stack_end      = NULL;
static bool   g_check_overflow = false;

static void
usage(char *name, FILE *out)
{
	fprintf(out,
		"Usage:\n"
		"\t%s [-c] [-p] [-t] [-s stack_size] [-S var_size]\n"
		"\t%s -h\n"
		"\n"
		"Options:\n"
		"\t--check-overflow, -c\n"
		"\t\tCheck if the stack was overflowed\n"
		"\t--on-pool, -p\n"
		"\t\tCreate ULT thread on ABT pool\n"
		"\t--unnamed-thread, -u\n"
		"\t\tCreate an unnamed ULT thread\n"
		"\t--stack-size=<stack size>, -s <stack size>\n"
		"\t\tSize in kilo bytes of the ULT thread stack\n"
		"\t--var-size=<variable size>, -S <variable size>\n"
		"\t\tSize in bytes of the variable to allocate on the stack\n"
		"\t--help, -h\n"
		"\t\tPrint this description\n",
		name, name);
}

static void
stack_fill(void *arg)
{
	ABT_thread thread;
	void      *sp       = NULL;
	size_t     var_size = (size_t)arg;
	int        rc;

	rc = ABT_thread_self(&thread);
	D_ASSERT(rc == ABT_SUCCESS);
	rc = ABT_thread_get_stacksize(thread, &g_stack_size);
	D_ASSERT(rc == ABT_SUCCESS);
	printf("Starting filling stack:\n"
	       "\t- stack size: %zu\n"
	       "\t- var size:   %zu\n",
	       g_stack_size, var_size);

	g_stack_start = &sp;
	for (;;) {
		g_stack_end = alloca(var_size);
		g_total_size += var_size;
	}
}

static void
handler_segv(int sig, siginfo_t *si, void *unused)
{
	printf("\n"
	       "--------------------------------------------------------------------------------\n"
	       "Signal 0x%x was handle:\n"
	       "\tFailed to access memory location %p\n"
	       "\tAllocated %zu bytes on stack: start=%p end=%p size=%zd\n"
	       "--------------------------------------------------------------------------------\n",
	       si->si_signo, si->si_addr, g_total_size, g_stack_start, g_stack_end,
	       g_stack_start - g_stack_end);

	if (g_check_overflow && (g_total_size > g_stack_size))
		exit(EXIT_FAILURE);

	exit(EXIT_SUCCESS);
}

static void
signal_register(void)
{
	struct sigaction act = {0};
	stack_t          ss;
	int              rc;

	ss.ss_sp = malloc(SIGSTKSZ);
	D_ASSERT(ss.ss_sp != NULL);
	ss.ss_size  = SIGSTKSZ;
	ss.ss_flags = 0;
	rc          = sigaltstack(&ss, NULL);
	D_ASSERT(rc == 0);

	act.sa_flags     = SA_SIGINFO | SA_ONSTACK;
	act.sa_sigaction = handler_segv;
	rc               = sigaction(SIGSEGV, &act, NULL);
	D_ASSERT(rc == 0);
}

int
main(int argc, char **argv)
{
	const char         *opt_cfg        = "pucs:S:h";
	const struct option long_opt_cfg[] = {{"on-pool", no_argument, NULL, 'p'},
					      {"unnamed-thread", no_argument, NULL, 'u'},
					      {"check-overflow", no_argument, NULL, 'c'},
					      {"stack-size", required_argument, NULL, 's'},
					      {"var-size", required_argument, NULL, 'S'},
					      {"help", no_argument, NULL, 'h'},
					      {0, 0, 0, 0}};
	int                 opt;
	bool                create_on_pool;
	ABT_thread          named_thread = {0};
	ABT_thread         *thread       = &named_thread;
	ABT_thread_attr     attr         = ABT_THREAD_ATTR_NULL;
	size_t              var_size     = 1 << 6;
	ssize_t             stack_size   = -1;
	int                 rc;

	while ((opt = getopt_long(argc, argv, opt_cfg, long_opt_cfg, NULL)) != -1) {
		switch (opt) {
		case 'c':
			g_check_overflow = true;
			break;
		case 'p':
			create_on_pool = true;
			break;
		case 'u':
			thread = NULL;
			break;
		case 's':
			stack_size = (size_t)atoi(optarg) << 10;
			break;
		case 'S':
			var_size = (size_t)atoi(optarg);
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

	printf("Initializing test...\n");
	rc = daos_debug_init_ex("/dev/stdout", DLOG_INFO);
	D_ASSERT(rc == 0);
	rc = ABT_init(0, NULL);
	D_ASSERT(rc == 0);

	if (stack_size != -1) {
		rc = ABT_thread_attr_create(&attr);
		D_ASSERT(rc == ABT_SUCCESS);
		rc = ABT_thread_attr_set_stacksize(attr, stack_size);
		D_ASSERT(rc == ABT_SUCCESS);
	}

	signal_register();

	if (create_on_pool) {
		ABT_pool pool;

		rc = ABT_self_get_last_pool(&pool);
		D_ASSERT(rc == ABT_SUCCESS);
		ABT_thread_create(pool, stack_fill, (void *)var_size, attr, thread);
	} else {
		ABT_xstream xstream;

		rc = ABT_self_get_xstream(&xstream);
		D_ASSERT(rc == ABT_SUCCESS);
		ABT_thread_create_on_xstream(xstream, stack_fill, (void *)var_size, attr, thread);
	}

	printf("Scheduling ULT test thread...\n");
	ABT_thread_yield();
	D_ASSERT(false);
}
