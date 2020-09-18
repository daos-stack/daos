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

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/system"
)

func TestStorageQueryCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"per-server metadata target health query",
			"storage query target-health -r 0 -t 1",
			printRequest(t, &control.SmdQueryReq{
				Rank:             system.Rank(0),
				OmitPools:        true,
				IncludeBioHealth: true,
				Target:           "1",
			}),
			nil,
		},
		{
			"per-server metadata target health query (missing flags)",
			"storage query target-health",
			printRequest(t, &control.SmdQueryReq{}),
			errors.New("required flags"),
		},
		{
			"per-server metadata device health query",
			"storage query device-health --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, &control.SmdQueryReq{
				Rank:             system.NilRank,
				OmitPools:        true,
				IncludeBioHealth: true,
				UUID:             "842c739b-86b5-462f-a7ba-b4a91b674f3d",
			}),
			nil,
		},
		{
			"per-server metadata device health query (missing uuid)",
			"storage query device-health",
			printRequest(t, &control.SmdQueryReq{}),
			errors.New("required flag"),
		},
		{
			"per-server metadata query pools",
			"storage query list-pools",
			printRequest(t, &control.SmdQueryReq{
				Rank:        system.NilRank,
				OmitDevices: true,
			}),
			nil,
		},
		{
			"per-server metadata query pools (by rank)",
			"storage query list-pools --rank 42",
			printRequest(t, &control.SmdQueryReq{
				Rank:        system.Rank(42),
				OmitDevices: true,
			}),
			nil,
		},
		{
			"per-server metadata query pools (by uuid)",
			"storage query list-pools --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, &control.SmdQueryReq{
				Rank:        system.NilRank,
				OmitDevices: true,
				UUID:        "842c739b-86b5-462f-a7ba-b4a91b674f3d",
			}),
			nil,
		},
		{
			"per-server metadata query devices",
			"storage query list-devices",
			printRequest(t, &control.SmdQueryReq{
				Rank:      system.NilRank,
				OmitPools: true,
			}),
			nil,
		},
		{
			"per-server metadata query devices (include health)",
			"storage query list-devices --health",
			printRequest(t, &control.SmdQueryReq{
				Rank:             system.NilRank,
				OmitPools:        true,
				IncludeBioHealth: true,
			}),
			nil,
		},
		{
			"per-server metadata query devices (by rank)",
			"storage query list-devices --rank 42",
			printRequest(t, &control.SmdQueryReq{
				Rank:      system.Rank(42),
				OmitPools: true,
			}),
			nil,
		},
		{
			"per-server metadata query devices (by uuid)",
			"storage query list-devices --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, &control.SmdQueryReq{
				Rank:      system.NilRank,
				UUID:      "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				OmitPools: true,
			}),
			nil,
		},
		{
			"Nonexistent subcommand",
			"storage query quack",
			"",
			fmt.Errorf("Unknown command"),
		},
	})
}
