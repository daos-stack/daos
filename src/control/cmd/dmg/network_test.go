//
// (C) Copyright 2019-2021 Intel Corporation.
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
			"Perform network scan with provider ofi+sockets (short)",
			"network scan -p 'ofi+sockets'",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{
					Provider: "'ofi+sockets'",
				}),
			}, " "),
			nil,
		},
		{
			"Perform network scan with provider ofi+sockets (long)",
			"network scan --provider 'ofi+sockets'",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{
					Provider: "'ofi+sockets'",
				}),
			}, " "),
			nil,
		},
	})
}
