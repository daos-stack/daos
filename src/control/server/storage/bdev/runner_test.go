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
package bdev

import (
	"fmt"
	"os"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestBdevRunnerPrepare(t *testing.T) {
	const (
		testNrHugePages  = 42
		testTargetUser   = "amos"
		testPciWhitelist = "a,b,c"
		testPciBlacklist = "x,y,z"
	)

	for name, tc := range map[string]struct {
		req    PrepareRequest
		mbc    *MockBackendConfig
		expEnv []string
		expErr error
	}{
		"prepare reset fails": {
			req: PrepareRequest{},
			mbc: &MockBackendConfig{
				PrepareResetErr: errors.New("reset failed"),
			},
			expErr: errors.New("reset failed"),
		},
		"prepare fails": {
			req: PrepareRequest{},
			mbc: &MockBackendConfig{
				PrepareErr: errors.New("prepare failed"),
			},
			expErr: errors.New("prepare failed"),
		},
		"defaults": {
			req: PrepareRequest{},
			expEnv: []string{
				fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
				fmt.Sprintf("%s=%d", nrHugepagesEnv, defaultNrHugepages),
				fmt.Sprintf("%s=", targetUserEnv),
			},
		},
		"user-specified values": {
			req: PrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    testTargetUser,
				PCIWhitelist:  testPciWhitelist,
				DisableVFIO:   true,
			},
			expEnv: []string{
				fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
				fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
				fmt.Sprintf("%s=%s", targetUserEnv, testTargetUser),
				fmt.Sprintf("%s=%s", pciWhiteListEnv, testPciWhitelist),
				fmt.Sprintf("%s=%s", driverOverrideEnv, vfioDisabledDriver),
			},
		},
		"blacklist": {
			req: PrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    testTargetUser,
				PCIBlacklist:  testPciBlacklist,
				DisableVFIO:   true,
			},
			expEnv: []string{
				fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
				fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
				fmt.Sprintf("%s=%s", targetUserEnv, testTargetUser),
				fmt.Sprintf("%s=%s", pciBlackListEnv, testPciBlacklist),
				fmt.Sprintf("%s=%s", driverOverrideEnv, vfioDisabledDriver),
			},
		},
		"blacklist whitelist fails": {
			req: PrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    testTargetUser,
				PCIBlacklist:  testPciBlacklist,
				PCIWhitelist:  testPciWhitelist,
				DisableVFIO:   true,
			},
			expErr: errors.New(
				"bdev_include and bdev_exclude can't be used together"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			s := &spdkSetupScript{
				log: log,
				runCmd: func(log logging.Logger, env []string, cmdStr string, args ...string) (string, error) {
					if len(args) > 0 && args[0] == "reset" {
						if tc.mbc != nil {
							return "", tc.mbc.PrepareResetErr
						}
						return "", nil
					}

					if tc.mbc != nil && tc.mbc.PrepareErr != nil {
						return "", tc.mbc.PrepareErr
					}

					if diff := cmp.Diff(tc.expEnv, env); diff != "" {
						t.Fatalf("\nunexpected cmd env (-want, +got):\n%s\n", diff)
					}

					return "", nil
				},
			}
			b := newBackend(log, s)
			p := NewProvider(log, b).WithForwardingDisabled()

			_, gotErr := p.Prepare(tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}
		})
	}
}
