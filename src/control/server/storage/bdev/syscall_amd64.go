//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build linux && amd64
// +build linux,amd64

package bdev

import (
	"syscall"
)

func Dup2(oldfd, newfd int) error {
	return syscall.Dup2(oldfd, newfd)
}
