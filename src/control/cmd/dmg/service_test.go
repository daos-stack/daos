//
// (C) Copyright 2019 Intel Corporation.
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

func TestServiceCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		// FIXME: kill-rank should probably require these arguments
		/*{
			"Kill rank with missing arguments",
			"service kill-rank",
			"ConnectClients KillRank",
			nil,
			errMissingFlag,
		},*/
		{
			"Kill rank",
			"service kill-rank --pool-uuid 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --rank 2",
			"ConnectClients KillRank-uuid 031bcaf8-f0f5-42ef-b3c5-ee048676dceb, rank 2",
			nil,
			cmdSuccess,
		},
		{
			"Nonexistent subcommand",
			"service quack",
			"",
			nil,
			fmt.Errorf("Unknown command"),
		},
	})
}
