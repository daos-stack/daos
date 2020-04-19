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
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/pkg/errors"
)

func TestSystemCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"system query with no arguments",
			"system query",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.SystemQueryReq{
					Ranks: []system.Rank{},
				}),
			}, " "),
			nil,
		},
		{
			"system query with single rank",
			"system query --ranks 0",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.SystemQueryReq{
					Ranks: []system.Rank{0},
				}),
			}, " "),
			nil,
		},
		{
			"system query with multiple ranks",
			"system query --ranks 0,1,4",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.SystemQueryReq{
					Ranks: []system.Rank{0, 1, 4},
				}),
			}, " "),
			nil,
		},
		{
			"system query verbose",
			"system query --verbose",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.SystemQueryReq{
					Ranks: []system.Rank{},
				}),
			}, " "),
			nil,
		},
		{
			"system stop with no arguments",
			"system stop",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.SystemStopReq{
					Prep:  true,
					Kill:  true,
					Ranks: []system.Rank{},
				}),
			}, " "),
			nil,
		},
		{
			"system stop with force",
			"system stop --force",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.SystemStopReq{
					Prep:  true,
					Kill:  true,
					Force: true,
					Ranks: []system.Rank{},
				}),
			}, " "),
			nil,
		},
		{
			"system stop with single rank",
			"system stop --ranks 0",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.SystemStopReq{
					Prep:  true,
					Kill:  true,
					Ranks: []system.Rank{0},
				}),
			}, " "),
			nil,
		},
		{
			"system stop with multiple ranks",
			"system stop --ranks 0,1,4",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.SystemStopReq{
					Prep:  true,
					Kill:  true,
					Ranks: []system.Rank{0, 1, 4},
				}),
			}, " "),
			nil,
		},
		{
			"system start with no arguments",
			"system start",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.SystemStartReq{
					Ranks: []system.Rank{},
				}),
			}, " "),
			nil,
		},
		{
			"system start with single rank",
			"system start --ranks 0",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.SystemStartReq{
					Ranks: []system.Rank{0},
				}),
			}, " "),
			nil,
		},
		{
			"system start with multiple ranks",
			"system start --ranks 0,1,4",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.SystemStartReq{
					Ranks: []system.Rank{0, 1, 4},
				}),
			}, " "),
			nil,
		},
		{
			"leader query",
			"system leader-query",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.LeaderQueryReq{
					System: build.DefaultSystemName,
				}),
			}, " "),
			nil,
		},
		{
			"system list-pools with default config",
			"system list-pools",
			strings.Join([]string{
				"ConnectClients",
				printRequest(t, &control.ListPoolsReq{
					System: build.DefaultSystemName,
				}),
			}, " "),
			nil,
		},
		{
			"Non-existent subcommand",
			"system quack",
			"",
			fmt.Errorf("Unknown command"),
		},
		{
			"Non-existent option",
			"system start --rank 0",
			"",
			errors.New("unknown flag `rank'"),
		},
	})
}
