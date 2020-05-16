//
// (C) Copyright 2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//
// +build linux

package helper

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
