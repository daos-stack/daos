//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/server/storage"
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
			"per-server metadata query devices (show only evicted)",
			"storage query list-devices --show-evicted",
			printRequest(t, &control.SmdQueryReq{
				Rank:      system.NilRank,
				OmitPools: true,
				StateMask: storage.NvmeFlagFaulty,
			}),
			nil,
		},
		{
			"per-server metadata query devices (show only evicted short)",
			"storage query list-devices -e",
			printRequest(t, &control.SmdQueryReq{
				Rank:      system.NilRank,
				OmitPools: true,
				StateMask: storage.NvmeFlagFaulty,
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
			"Set FAULTY device status (force)",
			"storage set nvme-faulty --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d -f",
			printRequest(t, &control.SmdQueryReq{
				UUID:      "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				SetFaulty: true,
				OmitPools: true,
			}),
			nil,
		},
		{
			"Set FAULTY device status (without force)",
			"storage set nvme-faulty --uuid abcd",
			"StorageSetFaulty",
			errors.New("consent not given"),
		},
		{
			"Set FAULTY device status (with > 1 host)",
			"-l host-[1-2] storage set nvme-faulty -f --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			"StorageSetFaulty",
			errors.New("> 1 host"),
		},
		{
			"Set FAULTY device status without device specified",
			"storage set nvme-faulty",
			"StorageSetFaulty",
			errors.New("the required flag `-u, --uuid' was not specified"),
		},
		{
			"Reuse a FAULTY device",
			"storage replace nvme --old-uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d --new-uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, &control.SmdQueryReq{
				UUID:        "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				ReplaceUUID: "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				NoReint:     false,
				OmitPools:   true,
			}),
			nil,
		},
		{
			"Replace an evicted device with a new device",
			"storage replace nvme --old-uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d --new-uuid 2ccb8afb-5d32-454e-86e3-762ec5dca7be",
			printRequest(t, &control.SmdQueryReq{
				UUID:        "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				ReplaceUUID: "2ccb8afb-5d32-454e-86e3-762ec5dca7be",
				NoReint:     false,
				OmitPools:   true,
			}),
			nil,
		},
		{
			"Try to replace a device without a new device UUID specified",
			"storage replace nvme --old-uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			"StorageReplaceNvme",
			errors.New("the required flag `--new-uuid' was not specified"),
		},
		{
			"Identify device without device specified",
			"storage led identify",
			"",
			errors.New("the required flag `-u, --uuid' was not specified"),
		},
		{
			"Identify a device",
			"storage led identify --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, &control.SmdQueryReq{
				UUID:      "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				Identify:  true,
				OmitPools: true,
			}),
			nil,
		},
		{
			"Clear identifying LED state on a VMD device",
			"storage led clear --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, &control.SmdQueryReq{
				UUID:      "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				ResetLED:  true,
				OmitPools: true,
			}),
			nil,
		},
		{
			"Query LED state of a VMD device",
			"storage led check --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, &control.SmdQueryReq{
				UUID:      "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				GetLED:    true,
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
