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
					Providers: hardware.NewFabricProviderSet(
						&hardware.FabricProvider{
							Name:     "p1",
							Priority: 2,
						}),
					NUMANode:    1,
					DeviceClass: hardware.Infiniband,
				},
			),
			expResult: &ctlpb.NetworkScanResp{
				Interfaces: []*ctlpb.FabricInterface{
					{
						Provider:    "p1",
						Device:      "net0",
						Numanode:    1,
						Netdevclass: uint32(hardware.Infiniband),
						Priority:    2,
					},
				},
			},
		},
		"multi provider": {
			fis: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Name:          "fi0",
					NetInterfaces: common.NewStringSet("net0"),
					Providers: hardware.NewFabricProviderSet(
						&hardware.FabricProvider{
							Name:     "p1",
							Priority: 1,
						},
						&hardware.FabricProvider{
							Name:     "p2",
							Priority: 2,
						},
					),
					NUMANode:    1,
					DeviceClass: hardware.Infiniband,
				},
			),
			expResult: &ctlpb.NetworkScanResp{
				Interfaces: []*ctlpb.FabricInterface{
					{
						Provider:    "p1",
						Device:      "net0",
						Numanode:    1,
						Netdevclass: uint32(hardware.Infiniband),
						Priority:    1,
					},
					{
						Provider:    "p2",
						Device:      "net0",
						Numanode:    1,
						Netdevclass: uint32(hardware.Infiniband),
						Priority:    2,
					},
				},
			},
		},
		"multi interface": {
			fis: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Name:          "fi0",
					NetInterfaces: common.NewStringSet("net0"),
					Providers: hardware.NewFabricProviderSet(
						&hardware.FabricProvider{
							Name:     "p1",
							Priority: 0,
						},
					),
					NUMANode:    0,
					DeviceClass: hardware.Infiniband,
				},
				&hardware.FabricInterface{
					Name:          "fi1",
					NetInterfaces: common.NewStringSet("net1"),
					Providers: hardware.NewFabricProviderSet(
						&hardware.FabricProvider{
							Name:     "p1",
							Priority: 1,
						},
						&hardware.FabricProvider{
							Name:     "p2",
							Priority: 2,
						},
					),
					NUMANode:    1,
					DeviceClass: hardware.Infiniband,
				},
			),
			expResult: &ctlpb.NetworkScanResp{
				Interfaces: []*ctlpb.FabricInterface{
					{
						Provider:    "p1",
						Device:      "net0",
						Numanode:    0,
						Netdevclass: uint32(hardware.Infiniband),
						Priority:    0,
					},
					{
						Provider:    "p1",
						Device:      "net1",
						Numanode:    1,
						Netdevclass: uint32(hardware.Infiniband),
						Priority:    1,
					},
					{
						Provider:    "p2",
						Device:      "net1",
						Numanode:    1,
						Netdevclass: uint32(hardware.Infiniband),
						Priority:    2,
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			cs := mockControlService(t, log, config.DefaultServer(), nil, nil, nil)

			result := cs.fabricInterfaceSetToNetworkScanResp(tc.fis)

			if diff := cmp.Diff(tc.expResult, result, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}
