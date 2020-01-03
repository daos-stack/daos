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
	"fmt"
	"testing"
)

func TestSystemCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"system query with no arguments",
			"system query",
			"ConnectClients SystemQuery",
			nil,
		},
		{
			"system stop with no arguments",
			"system stop",
			"ConnectClients SystemStop",
			nil,
		},
		{
			"system stop with kill",
			"system stop --kill",
			"ConnectClients SystemStop",
			nil,
		},
		{
			"system stop with prep",
			"system stop --prep",
			"ConnectClients SystemStop",
			nil,
		},
		{
			"leader query",
			"system leader-query",
			"ConnectClients LeaderQuery-daos_server",
			nil,
		},
		{
			"system list-pools with default config",
			"system list-pools",
			"ConnectClients ListPools-{daos_server}",
			nil,
		},
		{
			"Nonexistent subcommand",
			"system quack",
			"",
			fmt.Errorf("Unknown command"),
		},
	})
}
