//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package engine

import (
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func Test_ValidateLogMasks(t *testing.T) {
	for name, tc := range map[string]struct {
		masks  string
		expErr error
	}{
		"empty": {},
		"single level; no prefix": {
			masks: "DEBUG",
		},
		"single level; no prefix; unknown level": {
			masks:  "WARNING",
			expErr: errors.New("unknown log level"),
		},
		"single assignment": {
			masks: "mgmt=DEBUG",
		},
		"single level; single assignment": {
			masks: "ERR,mgmt=DEBUG",
		},
		"single level; single assignment; mixed caae": {
			masks: "err,mgmt=debuG",
		},
		"single level; single assignment; with space": {
			masks:  "ERR, mgmt=DEBUG",
			expErr: errors.New("illegal characters"),
		},
		"single level; single assignment; bad level": {
			masks:  "ERR,mgmt=DEG",
			expErr: errors.New("unknown log level"),
		},
		"single assignment; single level": {
			masks:  "mgmt=DEBUG,ERR",
			expErr: errors.New("want PREFIX=LEVEL"),
		},
		"multiple assignments": {
			masks: "mgmt=DEBUG,bio=ERR",
		},
		"multiple assignments; bad format": {
			masks:  "mgmt=DEBUG,bio=ERR=",
			expErr: errors.New("want PREFIX=LEVEL"),
		},
		"multiple assignments; bad chars": {
			masks:  "mgmt=DEBUG,bio!=ERR",
			expErr: errors.New("illegal characters"),
		},
		"too long": {
			masks:  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			expErr: errors.New("exceeds maximum length (1024>1023)"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := ValidateLogMasks(tc.masks)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func Test_ValidateLogStreams(t *testing.T) {
	for name, tc := range map[string]struct {
		streams string
		expErr  error
	}{
		"empty": {},
		"single stream": {
			streams: "NET",
		},
		"multiple streams": {
			streams: "ANY,TRACE",
		},
		"unknown stream": {
			streams: "NET,WARNING",
			expErr:  errors.New("unknown"),
		},
		"mixed caae": {
			streams: "NET,io,rebuild",
		},
		"multiple streams; with space": {
			streams: "MD PL",
			expErr:  errors.New("illegal characters"),
		},
		"multiple streams; bad chars": {
			streams: "mgmt,DF,!=",
			expErr:  errors.New("illegal characters"),
		},
		"too long": {
			streams: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			expErr:  errors.New("exceeds maximum length (1024>1023)"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := ValidateLogStreams(tc.streams)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}
