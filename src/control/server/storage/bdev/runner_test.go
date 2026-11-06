//
// (C) Copyright 2019-2021 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"fmt"
	"strings"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// scriptCall absolute details of a call into the bdev runner script.
type scriptCall struct {
	Env  []string
	Args []string
}

func mockScriptRunner(t *testing.T, log logging.Logger, mbc *MockBackendConfig) (*spdkSetupScript, *[]scriptCall) {
	if mbc == nil {
		mbc = &MockBackendConfig{}
	}
	calls := make([]scriptCall, 0)

	return &spdkSetupScript{
		log: log,
		runCmd: func(log logging.Logger, env []string, cmdStr string, args ...string) (string, error) {
			log.Debugf("spdk setup script mock call: cmd %s, env %v, args %v", cmdStr, env, args)
			calls = append(calls, scriptCall{Env: env, Args: args})

			if len(args) > 0 && args[0] == "reset" {
				return "", mbc.ResetErr
			}

			if mbc.PrepareErr != nil {
				return "", mbc.PrepareErr
			}

			return "", nil
		},
	}, &calls
}

func TestSpdkSetupScript_validateDevType(t *testing.T) {
	for name, tc := range map[string]struct {
		devType string
		unset   bool
		expErr  error
	}{
		"unset": {
			unset:  true,
			expErr: errors.New("invalid or unset _DEV_TYPE"),
		},
		"empty": {
			devType: "",
			expErr:  errors.New("invalid or unset _DEV_TYPE"),
		},
		"invalid": {
			devType: "IOAT",
			expErr:  errors.New("invalid or unset _DEV_TYPE"),
		},
		"lower case invalid": {
			devType: "nvme",
			expErr:  errors.New("invalid or unset _DEV_TYPE"),
		},
		"vmd": {
			devType: devTypeVMD,
		},
		"nvme": {
			devType: devTypeNVMe,
		},
	} {
		t.Run(name, func(t *testing.T) {
			sss := &spdkSetupScript{
				env: map[string]string{
					"PATH": "/some/path",
				},
			}
			if !tc.unset {
				sss.env[devTypeEnv] = tc.devType
			}

			gotErr := sss.validateDevType()

			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestSpdkSetupScript_setsDevType(t *testing.T) {
	for name, tc := range map[string]struct {
		enableVMD  bool
		expDevType string
	}{
		"vmd disabled": {
			enableVMD:  false,
			expDevType: devTypeNVMe,
		},
		"vmd enabled": {
			enableVMD:  true,
			expDevType: devTypeVMD,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			req := &storage.BdevPrepareRequest{
				TargetUser: "bob",
				EnableVMD:  tc.enableVMD,
			}

			for _, method := range []struct {
				name string
				call func(*spdkSetupScript, *storage.BdevPrepareRequest) error
			}{
				{"Prepare", (*spdkSetupScript).Prepare},
				{"Unbind", (*spdkSetupScript).Unbind},
				{"Reset", (*spdkSetupScript).Reset},
			} {
				t.Run(method.name, func(t *testing.T) {
					sss, calls := mockScriptRunner(t, log, nil)

					if err := method.call(sss, req); err != nil {
						t.Fatalf("unexpected error: %s", err)
					}
					if len(*calls) != 1 {
						t.Fatalf("expected a single script call, got %d", len(*calls))
					}

					var gotDevType string
					for _, kv := range (*calls)[0].Env {
						if strings.HasPrefix(kv, devTypeEnv+"=") {
							gotDevType = strings.TrimPrefix(kv, devTypeEnv+"=")
						}
					}
					test.AssertEqual(t, tc.expDevType, gotDevType,
						fmt.Sprintf("unexpected %s value", devTypeEnv))
				})
			}
		})
	}
}
