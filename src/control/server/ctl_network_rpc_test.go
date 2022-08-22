//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
)

func TestServer_ControlService_fabricInterfaceSetToNetworkScanResp(t *testing.T) {
	for name, tc := range map[string]struct {
		fis       *hardware.FabricInterfaceSet
		provider  string
		expResult *ctlpb.NetworkScanResp
	}{
		"empty": {
			expResult: &ctlpb.NetworkScanResp{},
		},
		"single interface": {
			fis: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Name:          "fi0",
					NetInterfaces: common.NewStringSet("net0"),
					Providers:     common.NewStringSet("p1"),
					NUMANode:      1,
					DeviceClass:   hardware.Infiniband,
				},
			),
			expResult: &ctlpb.NetworkScanResp{
				Interfaces: []*ctlpb.FabricInterface{
					{
						Provider:    "p1",
						Device:      "net0",
						Numanode:    1,
						Netdevclass: uint32(hardware.Infiniband),
					},
				},
			},
		},
		"multi provider": {
			fis: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Name:          "fi0",
					NetInterfaces: common.NewStringSet("net0"),
					Providers:     common.NewStringSet("p1", "p2"),
					NUMANode:      1,
					DeviceClass:   hardware.Infiniband,
				},
			),
			expResult: &ctlpb.NetworkScanResp{
				Interfaces: []*ctlpb.FabricInterface{
					{
						Provider:    "p1",
						Device:      "net0",
						Numanode:    1,
						Netdevclass: uint32(hardware.Infiniband),
					},
					{
						Provider:    "p2",
						Device:      "net0",
						Numanode:    1,
						Netdevclass: uint32(hardware.Infiniband),
					},
				},
			},
		},
		"multi interface": {
			fis: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Name:          "fi0",
					NetInterfaces: common.NewStringSet("net0"),
					Providers:     common.NewStringSet("p1"),
					NUMANode:      0,
					DeviceClass:   hardware.Infiniband,
				},
				&hardware.FabricInterface{
					Name:          "fi1",
					NetInterfaces: common.NewStringSet("net1"),
					Providers:     common.NewStringSet("p1", "p2"),
					NUMANode:      1,
					DeviceClass:   hardware.Infiniband,
				},
			),
			expResult: &ctlpb.NetworkScanResp{
				Interfaces: []*ctlpb.FabricInterface{
					{
						Provider:    "p1",
						Device:      "net0",
						Numanode:    0,
						Netdevclass: uint32(hardware.Infiniband),
					},
					{
						Provider:    "p1",
						Device:      "net1",
						Numanode:    1,
						Netdevclass: uint32(hardware.Infiniband),
					},
					{
						Provider:    "p2",
						Device:      "net1",
						Numanode:    1,
						Netdevclass: uint32(hardware.Infiniband),
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			cs := mockControlService(t, log, config.DefaultServer(), nil, nil, nil)

			result := cs.fabricInterfaceSetToNetworkScanResp(tc.fis, tc.provider)

			if diff := cmp.Diff(tc.expResult, result, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}
