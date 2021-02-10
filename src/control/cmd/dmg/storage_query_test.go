//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
			"per-server storage space utilization query",
			"storage query usage",
			printRequest(t, &control.StorageScanReq{Usage: true}),
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
