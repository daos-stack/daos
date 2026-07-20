//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
// Note: intentionally not "#include <stdlib.h>" -- SCons' Go dependency scanner
// (site_scons/site_tools/go_builder.py) treats every "#include <...>" line in a
// CGO preamble as a DAOS public header located under src/include/, which breaks
// the build for genuine system headers.  Declare getenv() directly instead.
extern char *getenv(const char *name);

// Compiled-in default LSAN options, used whenever ASAN_OPTIONS/LSAN_OPTIONS is not
// set in the environment.  daos_server_helper is spawned as a privileged sub-process
// by daos_server (see pbin.ExecReq, which sets child.Env = os.Environ()) and so only
// inherits whatever environment daos_server itself was started with -- routine
// invocations (e.g. "daos_server nvme reset" from the ftest framework) do not set
// ASAN_OPTIONS at all.  Without this hook, LSAN falls back to its own default of
// detect_leaks=1 and performs a stop-the-world ptrace-based scan whenever the
// process exits through any path that runs libc atexit handlers (including from
// within CGO/SPDK C code, which does not bypass atexit() the way Go's own exit path
// does).  That scan aborts with "LeakSanitizer has encountered a fatal error" on
// some CI hosts (likely a ptrace restriction).  Values explicitly set via
// ASAN_OPTIONS/LSAN_OPTIONS in the environment still take precedence over this
// compiled-in default.
const char *__lsan_default_options(void) {
	return "detect_leaks=0";
}

// Compiled-in default TSAN options for daos_server.  This is a control-plane
// management daemon, not the DAOS-18859 reproduction target (the suspected
// race is client-side, inside the daos CLI process during pool connect -- see
// pool.go).  Silence TSan reporting outright: report_bugs is a genuine,
// documented sanitizer_common flag that keeps all instrumentation active but
// suppresses bug reports, avoiding noise unrelated to this investigation
// without needing a separate non-instrumented build of the linked C
// libraries.  Values explicitly set via TSAN_OPTIONS in the environment still
// take precedence over this default.
const char *__tsan_default_options(void) {
	return "report_bugs=0";
}

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
