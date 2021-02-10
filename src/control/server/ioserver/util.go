//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ioserver

import (
	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
)

const (
	// NvmeMinBytesPerTarget is min NVMe pool allocation per target
	NvmeMinBytesPerTarget = 1 * humanize.GiByte
	// ScmMinBytesPerTarget is min SCM pool allocation per target
	ScmMinBytesPerTarget = 16 * humanize.MiByte
)

type (
	cmdLogger struct {
		logFn  func(string)
		prefix string
	}
)

func (cl *cmdLogger) Write(data []byte) (int, error) {
	if cl.logFn == nil {
		return 0, errors.New("no log function set in cmdLogger")
	}

	var msg string
	if cl.prefix != "" {
		msg = cl.prefix + " "
	}
	msg += string(data)
	cl.logFn(msg)
	return len(data), nil
}
