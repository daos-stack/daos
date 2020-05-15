//
// (C) Copyright 2019-2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
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
			"Get network provider list",
			"network list",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			nil,
		},
		{
			"Perform network scan no provider",
			"network scan",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			nil,
		},
		{
			"Perform network scan all providers long",
			"network scan --all",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			nil,
		},
		{
			"Perform network scan all providers short",
			"network scan -a",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			nil,
		},
		{
			"Perform network scan with provider ofi+sockets (short)",
			"network scan -p 'ofi+sockets'",
			strings.Join([]string{
				"ConnectClients",
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
				"ConnectClients",
				printRequest(t, &control.NetworkScanReq{
					Provider: "'ofi+sockets'",
				}),
			}, " "),
			nil,
		},
	})
}
