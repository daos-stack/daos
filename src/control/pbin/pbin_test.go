//
// (C) Copyright 2019-2020 Intel Corporation.
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
package pbin_test

import (
	"fmt"
	"os"
	"path"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
)

const (
	childModeEnvVar    = "GO_TESTING_CHILD_MODE"
	childModeEcho      = "MODE_ECHO"
	childModeReqRes    = "MODE_REQ_RES"
	childVersionEnvVar = "GO_TESTING_CHILD_VERSION"
	testMsg            = "hello world"
)

func childErrExit(err error) {
	if err == nil {
		err = errors.New("unknown error")
	}
	fmt.Fprintf(os.Stderr, "CHILD ERROR: %s\n", err)
	os.Exit(1)
}

func TestMain(m *testing.M) {
	mode := os.Getenv(childModeEnvVar)
	switch mode {
	case "":
		// default; run the test binary
		os.Exit(m.Run())
	case childModeEcho:
		// for stdio_test
		echo()
	case childModeReqRes:
		// for exec_test
		reqRes()
	default:
		childErrExit(errors.Errorf("Unknown child mode: %q", mode))
	}
}

func TestPbin_CheckHelper(t *testing.T) {
	for name, tc := range map[string]struct {
		helperName   string
		childVersion string
		expErr       error
	}{
		"unknown helper": {
			helperName: "bad-helper-name",
			expErr:     pbin.PrivilegedHelperNotAvailable("bad-helper-name"),
		},
		"success": {
			helperName: path.Base(os.Args[0]),
		},
		"bad version": {
			helperName:   path.Base(os.Args[0]),
			childVersion: "0.0.0",
			expErr:       errors.New("version mismatch"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			os.Setenv(childModeEnvVar, childModeReqRes)

			if tc.childVersion == "" {
				tc.childVersion = build.DaosVersion
			}
			os.Setenv(childVersionEnvVar, tc.childVersion)

			gotErr := pbin.CheckHelper(log, tc.helperName)
			common.CmpErr(t, tc.expErr, gotErr)
		})
	}
}
