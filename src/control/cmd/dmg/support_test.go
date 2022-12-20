//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/pkg/errors"

	// "github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
	// "github.com/daos-stack/daos/src/control/lib/support"
)

func TestSupportCollectlogCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"support collectlog without any args",
			"support collectlog",
			printRequest(t, &control.CollectLogReq{}),
			errors.New("DAOS Management Service is down"),
		},
	})
}
