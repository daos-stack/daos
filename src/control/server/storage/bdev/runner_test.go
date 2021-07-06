//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package bdev

import (
	"fmt"
	"os"
	"os/user"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

func mockRun(log logging.Logger, env []string, cmdStr string, args ...string) (string, error) {
	log.Debugf("running: %s", cmdStr+" "+strings.Join(args, " "))
	return "", nil
}

func mockScriptRunner(log logging.Logger) *spdkSetupScript {
	return &spdkSetupScript{
		log:    log,
		runCmd: mockRun,
	}
}

func defaultBackendWithMockRunner(log logging.Logger) *spdkBackend {
	return newBackend(log, mockScriptRunner(log))
}

func TestRunner_Prepare(t *testing.T) {
	const (
		testNrHugePages       = 42
		nonexistentTargetUser = "nonexistentTargetUser"
		testPciAllowlist      = "a,b,c"
		testPciBlocklist      = "x,y,z"
	)
	usrCurrent, _ := user.Current()
	username := usrCurrent.Username

	for name, tc := range map[string]struct {
		req    PrepareRequest
		mbc    *MockBackendConfig
		expEnv []string
		expErr error
	}{
		"prepare reset fails": {
			req: PrepareRequest{
				TargetUser: username,
			},
			mbc: &MockBackendConfig{
				PrepareResetErr: errors.New("reset failed"),
			},
			expErr: errors.New("reset failed"),
		},
		"prepare fails": {
			req: PrepareRequest{
				TargetUser: username,
			},
			mbc: &MockBackendConfig{
				PrepareErr: errors.New("prepare failed"),
			},
			expErr: errors.New("prepare failed"),
		},
		"defaults": {
			req: PrepareRequest{
				TargetUser: username,
			},
			expEnv: []string{
				fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
				fmt.Sprintf("%s=%d", nrHugepagesEnv, defaultNrHugepages),
				fmt.Sprintf("%s=%s", targetUserEnv, username),
			},
		},
		"user-specified values": {
			req: PrepareRequest{
				HugePageCount:         testNrHugePages,
				DisableCleanHugePages: true,
				TargetUser:            username,
				PCIAllowlist:          testPciAllowlist,
				DisableVFIO:           true,
			},
			expEnv: []string{
				fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
				fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
				fmt.Sprintf("%s=%s", targetUserEnv, username),
				fmt.Sprintf("%s=%s", pciAllowListEnv, testPciAllowlist),
				fmt.Sprintf("%s=%s", driverOverrideEnv, vfioDisabledDriver),
			},
		},
		"blocklist": {
			req: PrepareRequest{
				HugePageCount:         testNrHugePages,
				DisableCleanHugePages: true,
				TargetUser:            username,
				PCIBlocklist:          testPciBlocklist,
				DisableVFIO:           true,
			},
			expEnv: []string{
				fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
				fmt.Sprintf("%s=%d", nrHugepagesEnv, testNrHugePages),
				fmt.Sprintf("%s=%s", targetUserEnv, username),
				fmt.Sprintf("%s=%s", pciBlockListEnv, testPciBlocklist),
				fmt.Sprintf("%s=%s", driverOverrideEnv, vfioDisabledDriver),
			},
		},
		"blocklist allowlist fails": {
			req: PrepareRequest{
				HugePageCount:         testNrHugePages,
				DisableCleanHugePages: true,
				TargetUser:            username,
				PCIBlocklist:          testPciBlocklist,
				PCIAllowlist:          testPciAllowlist,
				DisableVFIO:           true,
			},
			expErr: errors.New(
				"bdev_include and bdev_exclude can not be used together"),
		},
		"unknown target user fails": {
			req: PrepareRequest{
				DisableCleanHugePages: true,
				TargetUser:            nonexistentTargetUser,
				DisableVFIO:           true,
			},
			expErr: errors.New(
				"lookup on local host: user: unknown user nonexistentTargetUser"),
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
