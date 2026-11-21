//
// Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build asan

package main

/*
// From sanitizer/lsan_interface.h; included by hand as the scons Go scanner maps <> includes to src/.
void __lsan_do_leak_check(void);
*/
import "C"

// checkLeaks runs LeakSanitizer before os.Exit, which skips the check Go does when main returns.
func checkLeaks() {
	C.__lsan_do_leak_check()
}
