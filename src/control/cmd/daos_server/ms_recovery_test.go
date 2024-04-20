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

// TestDaosServer_MS_Commands_JSON verifies that the JSON-output flag is disabled for
// management-service command syntax.
func TestDaosServer_MS_Commands_JSON(t *testing.T) {
	log, buf := logging.NewTestCommandLineLogger()

	runJSONCmdTests(t, log, buf, []jsonCmdTest{
		{
			"MS status; JSON",
			"ms status -j",
			nil,
			nil,
			errJSONOutputNotSupported,
		},
		{
			"MS restore; JSON",
			"ms restore -p foo -j",
			nil,
			nil,
			errJSONOutputNotSupported,
		},
		{
			"MS recover; JSON",
			"ms recover -j",
			nil,
			nil,
			errJSONOutputNotSupported,
		},
	})
}
