//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/daos-stack/daos/src/control/logging"
)

// TestDaosServer_Support_Commands_JSON verifies that the JSON-output flag is disabled for support
// command syntax.
func TestDaosServer_Support_Commands_JSON(t *testing.T) {
	log := logging.NewCommandLineLogger()

	runJSONCmdTests(t, log, []jsonCmdTest{
		{
			"Collect-log; JSON",
			"support collect-log -j",
			nil,
			nil,
			errJSONOutputNotSupported,
		},
	})
}
