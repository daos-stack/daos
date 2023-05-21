//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package engine

import (
	"testing"

	"github.com/google/go-cmp/cmp"
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
		"single level; single assignment; mixed case": {
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
			expErr: errors.New("of the form PREFIX=LEVEL"),
		},
		"multiple assignments": {
			masks: "mgmt=DEBUG,bio=ERR",
		},
		"multiple assignments; bad format": {
			masks:  "mgmt=DEBUG,bio=ERR=",
			expErr: errors.New("of the form PREFIX=LEVEL"),
		},
		"multiple assignments; bad chars": {
			masks:  "mgmt=DEBUG,bio!=ERR",
			expErr: errors.New("illegal characters"),
		},
		"multiple base levels specified": {
			masks:  "debug,err,bio=Err",
			expErr: errors.New("of the form PREFIX=LEVEL"),
		},
		"base level not specified first": {
			masks:  "bio=Err,debug",
			expErr: errors.New("of the form PREFIX=LEVEL"),
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

func Test_MergeLogEnvVars(t *testing.T) {
	for name, tc := range map[string]struct {
		masks      string
		subsystems string
		expMasks   string
		expErr     error
	}{
		"empty subsystems": {},
		"empty masks": {
			subsystems: "misc",
		},
		"debug base level": {
			masks:      "debug",
			subsystems: "vos",
			expMasks:   "ERROR,vos=DEBUG",
		},
		"skip subsystem": {
			masks:      "ERR,misc=CRIT,mem=DEBUG",
			subsystems: "misc",
			expMasks:   "ERROR,misc=CRIT",
		},
		"don't skip subsystem if level above error": {
			masks:      "err,common=crit,vos=debug",
			subsystems: "vos",
			expMasks:   "ERROR,common=CRIT,vos=DEBUG",
		},
		"keep assignment for subsystem in list": {
			masks:      "err,container=debug,object=debug",
			subsystems: "object",
			expMasks:   "ERROR,object=DEBUG",
		},
		"add assignment for subsystem in list; remove ineffective assignments": {
			masks:      "dbug,rdb=crit,pool=err",
			subsystems: "pool,mgmt",
			expMasks:   "ERROR,rdb=CRIT,mgmt=DEBUG",
		},
		"default base level applied": {
			masks:      "corpc=crit,iv=dbug,grp=dbug",
			subsystems: "iv,st",
			expMasks:   "ERROR,corpc=CRIT,iv=DEBUG",
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotMasks, gotErr := MergeLogEnvVars(tc.masks, tc.subsystems)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expMasks, gotMasks); diff != "" {
				t.Fatalf("unexpected resultant log masks:\n%s\n", diff)
			}
		})
	}
}
