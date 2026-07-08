//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
#include <stdlib.h>

// Weak references — resolve to real ASAN/LSAN functions in ASAN builds, NULL otherwise.
extern void __attribute__((weak)) __lsan_do_leak_check(void);

// Go exits via exit_group syscall, bypassing libc exit() and ASAN's atexit handlers.
// Call these explicitly before the process terminates so that ASAN reports are written
// to log_path even when the process exits via Go's runtime.
//
// __lsan_do_leak_check() is LSAN's on-demand API: unlike the implicit atexit-based
// check, it ignores ASAN_OPTIONS=detect_leaks=0 and always performs a stop-the-world
// scan.  On some CI hosts this scan aborts with "LeakSanitizer has encountered a
// fatal error" (observed with daos_server nvme reset on CI hardware), failing the
// command even though the underlying operation succeeded.  Make the call opt-in via
// DAOS_ASAN_LEAK_CHECK=1 so routine CLI invocations are unaffected.
static void run_asan_fini(void) {
	if (__lsan_do_leak_check && getenv("DAOS_ASAN_LEAK_CHECK"))
		__lsan_do_leak_check();
}
*/
import "C"

// runASANFini explicitly invokes ASAN/LSAN finalization.  Must be called at every
// exit point in main() because Go's runtime calls exit_group() directly, bypassing
// libc's exit() and therefore ASAN's registered atexit() handlers.
func runASANFini() {
	C.run_asan_fini()
}
