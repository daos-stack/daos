/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(misc)

#include <signal.h>
#include <execinfo.h>

#include <gurt/common.h>

static struct sigaction old_handlers[_NSIG];

static void
daos_register_sighand(int signo, void (*handler)(int, siginfo_t *, void *))
{
	struct sigaction act = {0};

	D_ASSERT((signo > 0) && (signo < _NSIG));

	act.sa_flags     = SA_SIGINFO;
	act.sa_sigaction = handler;

	/* register new and save old handler */
	if (sigaction(signo, &act, &old_handlers[signo]) != 0)
		DS_ERROR(errno, "sigaction() failure registering new signal handler for %d", signo);
}

#define PRINT_ERROR(fmt, ...)                                                                      \
	do {                                                                                       \
		fprintf(stderr, fmt "\n", ##__VA_ARGS__);                                          \
		D_ERROR(fmt "\n", ##__VA_ARGS__);                                                  \
	} while (0)

/** This should be safe on Linux since tls is allocated on thread creation */
#define MAX_BT_ENTRIES 256
static __thread void *bt[MAX_BT_ENTRIES];

static void
print_backtrace(int signo, siginfo_t *info, void *p)
{
	int bt_size, rc;

	PRINT_ERROR("*** Process %d (%s) received signal %d (%s) ***", getpid(),
		    program_invocation_short_name, signo, strsignal(signo));

	if (info != NULL) {
		PRINT_ERROR("Associated errno: %s (%d)", strerror(info->si_errno), info->si_errno);

		/* XXX we could get more signal/fault specific details from
		 * info->si_code decode
		 */

		switch (signo) {
		case SIGILL:
		case SIGFPE:
			PRINT_ERROR("Failing at address: %p", info->si_addr);
			break;
		case SIGSEGV:
		case SIGBUS:
			PRINT_ERROR("Failing for address: %p", info->si_addr);
			break;
		}
	} else {
		PRINT_ERROR("siginfo is NULL, additional information unavailable");
	}

	/* since we mainly handle fatal signals here, flush the log to not
	 * risk losing any debug traces
	 */
	d_log_sync();

	bt_size = backtrace(bt, MAX_BT_ENTRIES);
	if (bt_size == MAX_BT_ENTRIES)
		PRINT_ERROR("backtrace may have been truncated");
	if (bt_size > 1) {
		/* start at 1 to ignore this frame */
		char **symbols;

		symbols = backtrace_symbols(&bt[1], bt_size - 1);
		if (symbols) {
			int i;

			for (i = 0; i < bt_size - 1; i++)
				PRINT_ERROR("Frame %s", symbols[i]);
		}
		free(symbols);
	} else {
		PRINT_ERROR("No useful backtrace available");
	}

	/* re-register old handler */
	rc = sigaction(signo, &old_handlers[signo], NULL);
	if (rc != 0) {
		D_ERROR("sigaction() failure registering new and reading old %d signal handler",
			signo);
		/* XXX it is weird, we may end-up in a loop handling same
		 * signal with this handler if we return
		 */
		exit(EXIT_FAILURE);
	}

	/* XXX we may choose to forget about old handler and simply register
	 * signal again as SIG_DFL and raise it for corefile creation
	 */
	if (old_handlers[signo].sa_sigaction != NULL || old_handlers[signo].sa_handler != SIG_IGN) {
		/* XXX will old handler get accurate siginfo_t/ucontext_t ?
		 * we may prefer to call it with the same params we got ?
		 */
		raise(signo);
	}

	memset(&old_handlers[signo], 0, sizeof(struct sigaction));
}

static bool register_handler = false;
static bool registered       = false;

void
d_signal_stack_enable(bool enabled)
{
	register_handler = enabled;
}

void
d_signal_register()
{
	bool enabled = register_handler;

	if (registered)
		return;

	d_getenv_bool("DAOS_SIGNAL_REGISTER", &enabled);

	if (!enabled)
		return;

	daos_register_sighand(SIGILL, print_backtrace);
	daos_register_sighand(SIGFPE, print_backtrace);
	daos_register_sighand(SIGBUS, print_backtrace);
	daos_register_sighand(SIGSEGV, print_backtrace);
	daos_register_sighand(SIGABRT, print_backtrace);

	registered = true;
}
