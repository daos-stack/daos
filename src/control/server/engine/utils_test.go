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
			masks: "ERROR,mgmt=DEBUG",
		},
		"single level; single assignment; mixed case": {
			masks: "err,mgmt=debuG",
		},
		"single level; single assignment; with space": {
			masks:  "ERROR, mgmt=DEBUG",
			expErr: errors.New("illegal characters"),
		},
		"single level; single assignment; bad subsystem": {
			masks:  "ERROR,zzz=DEBUG",
			expErr: errors.New("unknown name"),
		},
		"single level; single assignment; illegal use of all": {
			masks:  "ERROR,all=DEBUG",
			expErr: errors.New("identifier can not be used"),
		},
		"single level; single assignment; bad level": {
			masks:  "ERROR,mgmt=DEG",
			expErr: errors.New("unknown log level"),
		},
		"single assignment; single level": {
			masks:  "mgmt=DEBUG,ERROR",
			expErr: errors.New("of the form PREFIX=LEVEL"),
		},
		"multiple assignments": {
			masks: "mgmt=DEBUG,bio=ERROR",
		},
		"multiple assignments; bad format": {
			masks:  "mgmt=DEBUG,bio=ERROR=",
			expErr: errors.New("of the form PREFIX=LEVEL"),
		},
		"multiple assignments; bad chars": {
			masks:  "mgmt=DEBUG,bio!=ERROR",
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
			streams: "ANY,TRACE,GROUP_METADATA",
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
			masks:      "ERROR,misc=CRIT,mem=DEBUG",
			subsystems: "misc",
			expMasks:   "ERROR,misc=CRIT",
		},
		"don't skip subsystem if level above error": {
			masks:      "error,common=crit,vos=debug",
			subsystems: "vos",
			expMasks:   "ERROR,common=CRIT,vos=DEBUG",
		},
		"keep assignment for subsystem in list": {
			masks:      "err,container=debug,object=debug",
			subsystems: "object",
			expMasks:   "ERROR,object=DEBUG",
		},
		"add assignment for subsystem in list; remove ineffective assignments": {
			masks:      "debug,rdb=crit,pool=err",
			subsystems: "pool,mgmt",
			expMasks:   "ERROR,rdb=CRIT,mgmt=DEBUG",
		},
		"default base level applied": {
			masks:      "corpc=crit,iv=debug,grp=debug",
			subsystems: "iv,st",
			expMasks:   "ERROR,corpc=CRIT,iv=DEBUG",
		},
		"all passthrough": {
			masks:      "debug",
			subsystems: "all",
			expMasks:   "DEBUG",
		},
		"long mask string": {
			masks:    "info,dtx=debug,vos=debug,object=debug",
			expMasks: "INFO,dtx=DEBUG,vos=DEBUG,object=DEBUG",
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
