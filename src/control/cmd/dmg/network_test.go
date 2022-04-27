//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/lib/control"
)

func TestNetworkCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"Perform network scan no provider",
			"network scan",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			nil,
		},
		{
			"Perform network scan with provider ofi+tcp (short)",
			"network scan -p 'ofi+tcp'",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{
					Provider: "'ofi+tcp'",
				}),
			}, " "),
			nil,
		},
		{
			"Perform network scan with provider ofi+tcp (long)",
			"network scan --provider 'ofi+tcp'",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{
					Provider: "'ofi+tcp'",
				}),
			}, " "),
			nil,
		},
	})
}
