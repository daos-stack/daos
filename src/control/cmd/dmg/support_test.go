//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"errors"
	"testing"

	"github.com/daos-stack/daos/src/control/lib/control"
)

func TestSupportCollectlogCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"support collect-log without any args",
			"support collect-log",
			printRequest(t, &control.CollectLogReq{
				TargetFolder: "",
			}),
			errors.New("DAOS Management Service is down"),
		},
	})
}
