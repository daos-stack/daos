//
// (C) Copyright 2019-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

func TestStorageQueryCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"per-server metadata query pools",
			"storage query list-pools",
			printRequest(t, &control.SmdQueryReq{
				Rank:        ranklist.NilRank,
				OmitDevices: true,
			}),
			nil,
		},
		{
			"per-server metadata query pools (by rank)",
			"storage query list-pools --rank 42",
			printRequest(t, &control.SmdQueryReq{
				Rank:        ranklist.Rank(42),
				OmitDevices: true,
			}),
			nil,
		},
		{
			"per-server metadata query pools (by uuid)",
			"storage query list-pools --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, &control.SmdQueryReq{
				Rank:        ranklist.NilRank,
				OmitDevices: true,
				UUID:        "842c739b-86b5-462f-a7ba-b4a91b674f3d",
			}),
			nil,
		},
		{
			"per-server metadata query devices",
			"storage query list-devices",
			printRequest(t, &control.SmdQueryReq{
				Rank:      ranklist.NilRank,
				OmitPools: true,
			}),
			nil,
		},
		{
			"per-server metadata device query health",
			"storage query list-devices --health",
			printRequest(t, &control.SmdQueryReq{
				Rank:             ranklist.NilRank,
				OmitPools:        true,
				IncludeBioHealth: true,
			}),
			nil,
		},
		{
			"per-server metadata device query health (by uuid)",
			"storage query list-devices --health --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, &control.SmdQueryReq{
				Rank:             ranklist.NilRank,
				OmitPools:        true,
				IncludeBioHealth: true,
				UUID:             "842c739b-86b5-462f-a7ba-b4a91b674f3d",
			}),
			nil,
		},
		{
			"per-server metadata query devices (show only evicted)",
			"storage query list-devices --show-evicted",
			printRequest(t, &control.SmdQueryReq{
				Rank:           ranklist.NilRank,
				OmitPools:      true,
				FaultyDevsOnly: true,
			}),
			nil,
		},
		{
			"per-server metadata query devices (show only evicted short)",
			"storage query list-devices -e",
			printRequest(t, &control.SmdQueryReq{
				Rank:           ranklist.NilRank,
				OmitPools:      true,
				FaultyDevsOnly: true,
			}),
			nil,
		},
		{
			"per-server metadata query devices (by rank)",
			"storage query list-devices --rank 42",
			printRequest(t, &control.SmdQueryReq{
				Rank:      ranklist.Rank(42),
				OmitPools: true,
			}),
			nil,
		},
		{
			"per-server metadata query devices (by uuid)",
			"storage query list-devices --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, &control.SmdQueryReq{
				Rank:      ranklist.NilRank,
				UUID:      "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				OmitPools: true,
			}),
			nil,
		},
		{
			"per-server storage space query",
			"storage query usage",
			printRequest(t, &control.StorageScanReq{Usage: true}),
			nil,
		},
		{
			"per-server storage space query (with custom mem-ratio but no show-usable)",
			"storage query usage --mem-ratio 25.5",
			"",
			errors.New("only supported with --show-usable"),
		},
		{
			"per-server storage space query (with custom mem-ratio)",
			"storage query usage --show-usable --mem-ratio 25.5",
			printRequest(t, &control.StorageScanReq{Usage: true, MemRatio: 0.255}),
			nil,
		},
		{
			"per-server storage space query (with two-tier mem-ratio)",
			"storage query usage -u --mem-ratio 20,80",
			printRequest(t, &control.StorageScanReq{Usage: true, MemRatio: 0.2}),
			nil,
		},
		{
			"per-server storage space query (with 100% mem-ratio)",
			"storage query usage -u --mem-ratio 100%",
			printRequest(t, &control.StorageScanReq{Usage: true, MemRatio: 1}),
			nil,
		},
		{
			"per-server storage space query (with three-tier mem-ratio)",
			"storage query usage -u --mem-ratio 10,20,70",
			"",
			errors.New("want 2 ratio values got 3"),
		},
		{
			"per-server storage space query (with --show-usable flag)",
			"storage query usage --show-usable",
			printRequest(t, &control.StorageScanReq{Usage: true}),
			nil,
		},
		{
			"Set FAULTY device status (missing host)",
			"storage set nvme-faulty --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d -f",
			"",
			errors.New("not specified"),
		},
		{
			"Set FAULTY device status (with > 1 host)",
			"storage set nvme-faulty -l host-[1-2] -f --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			"",
			errors.New("must specify a single host"),
		},
		{
			"Set FAULTY device status (force)",
			"storage set nvme-faulty --host foo --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d -f",
			printRequest(t, func() *control.SmdManageReq {
				req := &control.SmdManageReq{
					Operation: control.SetFaultyOp,
					IDs:       "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				}
				req.SetHostList([]string{"foo"})
				return req
			}()),
			nil,
		},
		{
			"Set FAULTY device status (without force)",
			"storage set nvme-faulty --host foo --uuid abcd",
			"",
			errors.New("consent not given"),
		},
		{
			"Set FAULTY device status without device specified",
			"storage set nvme-faulty --host foo",
			"",
			errors.New("the required flag `-u, --uuid' was not specified"),
		},
		{
			"Reuse a FAULTY device (missing host)",
			"storage replace nvme --old-uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d --new-uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			"",
			errors.New("not specified"),
		},
		{
			"Reuse a FAULTY device",
			"storage replace nvme --host foo --old-uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d --new-uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, func() *control.SmdManageReq {
				req := &control.SmdManageReq{
					Operation:   control.DevReplaceOp,
					IDs:         "842c739b-86b5-462f-a7ba-b4a91b674f3d",
					ReplaceUUID: "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				}
				req.SetHostList([]string{"foo"})
				return req
			}()),
			nil,
		},
		{
			"Replace an evicted device with a new device",
			"storage replace nvme --host foo --old-uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d --new-uuid 2ccb8afb-5d32-454e-86e3-762ec5dca7be",
			printRequest(t, func() *control.SmdManageReq {
				req := &control.SmdManageReq{
					Operation:   control.DevReplaceOp,
					IDs:         "842c739b-86b5-462f-a7ba-b4a91b674f3d",
					ReplaceUUID: "2ccb8afb-5d32-454e-86e3-762ec5dca7be",
				}
				req.SetHostList([]string{"foo"})
				return req
			}()),
			nil,
		},
		{
			"Try to replace a device without a new device UUID specified",
			"storage replace nvme -l foo --old-uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			"",
			errors.New("the required flag `--new-uuid' was not specified"),
		},
		{
			"Identify device without device UUID or PCI address specified",
			"storage led identify",
			printRequest(t, &control.SmdManageReq{
				Operation: control.LedBlinkOp,
			}),
			nil,
		},
		{
			"Identify a device",
			"storage led identify 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, &control.SmdManageReq{
				Operation: control.LedBlinkOp,
				IDs:       "842c739b-86b5-462f-a7ba-b4a91b674f3d",
			}),
			nil,
		},
		{
			"Identify a device with multiple device IDs with 60min timeout",
			"storage led identify --timeout 60 842c739b-86b5-462f-a7ba-b4a91b674f3d,d50505:01:00.0",
			printRequest(t, &control.SmdManageReq{
				Operation:       control.LedBlinkOp,
				IDs:             "842c739b-86b5-462f-a7ba-b4a91b674f3d,d50505:01:00.0",
				IdentifyTimeout: 60,
			}),
			nil,
		},
		{
			"Reset LED on device",
			"storage led identify --reset 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, &control.SmdManageReq{
				Operation: control.LedResetOp,
				IDs:       "842c739b-86b5-462f-a7ba-b4a91b674f3d",
			}),
			nil,
		},
		{
			"Reset LED on device; timeout set",
			"storage led identify --reset --timeout 1 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			"",
			errors.New("timeout option can not be set"),
		},
		{
			"Reset LED on multiple devices",
			"storage led identify --reset 842c739b-86b5-462f-a7ba-b4a91b674f3d,d50505:01:00.0",
			printRequest(t, &control.SmdManageReq{
				Operation: control.LedResetOp,
				IDs:       "842c739b-86b5-462f-a7ba-b4a91b674f3d,d50505:01:00.0",
			}),
			nil,
		},
		{
			"Check LED state of a VMD device",
			"storage led check 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, &control.SmdManageReq{
				Operation: control.LedCheckOp,
				IDs:       "842c739b-86b5-462f-a7ba-b4a91b674f3d",
			}),
			nil,
		},
		{
			"Check LED state of a VMD device with multiple device IDs",
			"storage led check 842c739b-86b5-462f-a7ba-b4a91b674f3d,d50505:01:00.0",
			printRequest(t, &control.SmdManageReq{
				Operation: control.LedCheckOp,
				IDs:       "842c739b-86b5-462f-a7ba-b4a91b674f3d,d50505:01:00.0",
			}),
			nil,
		},
		{
			"check LED state without device UUID or PCI address specified",
			"storage led check",
			printRequest(t, &control.SmdManageReq{
				Operation: control.LedCheckOp,
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
