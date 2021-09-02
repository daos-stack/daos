//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"testing"

	"github.com/daos-stack/daos/src/control/logging"
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
