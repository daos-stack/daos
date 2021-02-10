//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
// +build linux

package pbin

import "syscall"

// Hack alert! https://github.com/golang/go/issues/1435
//
// The setuid(2) implementation provided by Linux only affects the
// current thread, not the whole process. The implementation provided
// in glibc correctly sets it for all threads, but the Go maintainers
// didn't want to deal with cross-platform compatibility stuff, so
// they punted and made syscall.Setuid() return ENOTSUPP on Linux.
//
// This simple cgo wrapper around glibc's setuid should do the trick.

/*
#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>

static int
glibc_setuid(uid_t uid) {
	int rc = setuid(uid);
	return (rc < 0) ? errno : 0;
}
*/
import "C"

func setuid(uid int) error {
	rc := C.glibc_setuid(C.uid_t(uid))
	if rc != 0 {
		return syscall.Errno(rc)
	}
	return nil
}
