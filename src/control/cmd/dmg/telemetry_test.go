//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/pkg/errors"
)

func TestDmg_MetricsCmds(t *testing.T) {
	for name, tc := range map[string]struct {
		cmd    string
		expErr error
	}{
		"metrics list without port": {
			cmd:    "telemetry metrics list -l host",
			expErr: errors.New("--port"),
		},
		"metrics query without port": {
			cmd:    "telemetry metrics query -l host",
			expErr: errors.New("--port"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ctlClient := control.DefaultMockInvoker(log)
			conn := newTestConn(t)
			bridge := &bridgeConnInvoker{
				MockInvoker: *ctlClient,
				t:           t,
				conn:        conn,
			}

			err := runCmd(t, tc.cmd, log, bridge)

			common.CmpErr(t, tc.expErr, err)
		})
	}
}
