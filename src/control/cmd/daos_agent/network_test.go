//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
)

func TestAgent_FabricInterfaceSetToHostFabricMap(t *testing.T) {
	for name, tc := range map[string]struct {
		fis       *hardware.FabricInterfaceSet
		expResult *control.HostFabric
		expErr    error
	}{
		"nil": {
			expResult: &control.HostFabric{},
		},
		"empty": {
			fis:       hardware.NewFabricInterfaceSet(),
			expResult: &control.HostFabric{},
		},
		"single": {
			fis: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Name:        "fi0",
					OSDevice:    "os0",
					DeviceClass: hardware.Ether,
					Providers:   common.NewStringSet("p1"),
					NUMANode:    1,
				},
			),
			expResult: &control.HostFabric{
				Interfaces: []*control.HostFabricInterface{
					{
						Provider:    "p1",
						Device:      "os0",
						NumaNode:    1,
						NetDevClass: hardware.Ether,
					},
				},
				Providers: []string{"p1"},
			},
		},
		"missing OS device": {
			fis: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Name:        "fi0",
					DeviceClass: hardware.Ether,
					Providers:   common.NewStringSet("p1"),
					NUMANode:    1,
				},
			),
			expResult: &control.HostFabric{
				Interfaces: []*control.HostFabricInterface{
					{
						Provider:    "p1",
						Device:      "fi0",
						NumaNode:    1,
						NetDevClass: hardware.Ether,
					},
				},
				Providers: []string{"p1"},
			},
		},
		"exclude loopback": {
			fis: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Name:        "fi0",
					OSDevice:    "os0",
					DeviceClass: hardware.Ether,
					Providers:   common.NewStringSet("p1"),
					NUMANode:    1,
				},
				&hardware.FabricInterface{
					Name:        "lo",
					OSDevice:    "lo",
					DeviceClass: hardware.Loopback,
					Providers:   common.NewStringSet("p2"),
				},
			),
			expResult: &control.HostFabric{
				Interfaces: []*control.HostFabricInterface{
					{
						Provider:    "p1",
						Device:      "os0",
						NumaNode:    1,
						NetDevClass: hardware.Ether,
					},
				},
				Providers: []string{"p1"},
			},
		},
		"multiple": {
			fis: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Name:        "fi0",
					OSDevice:    "os0",
					DeviceClass: hardware.Ether,
					Providers:   common.NewStringSet("p1"),
					NUMANode:    1,
				},
				&hardware.FabricInterface{
					Name:        "fi1",
					OSDevice:    "os1",
					DeviceClass: hardware.Ether,
					Providers:   common.NewStringSet("p1", "p2"),
					NUMANode:    1,
				},
				&hardware.FabricInterface{
					Name:        "fi2",
					OSDevice:    "os2",
					DeviceClass: hardware.Infiniband,
					Providers:   common.NewStringSet("p2", "p3"),
					NUMANode:    2,
				},
			),
			expResult: &control.HostFabric{
				Interfaces: []*control.HostFabricInterface{
					{
						Provider:    "p1",
						Device:      "os0",
						NumaNode:    1,
						NetDevClass: hardware.Ether,
					},
					{
						Provider:    "p1",
						Device:      "os1",
						NumaNode:    1,
						NetDevClass: hardware.Ether,
					},
					{
						Provider:    "p2",
						Device:      "os1",
						NumaNode:    1,
						NetDevClass: hardware.Ether,
					},
					{
						Provider:    "p2",
						Device:      "os2",
						NumaNode:    2,
						NetDevClass: hardware.Infiniband,
					},
					{
						Provider:    "p3",
						Device:      "os2",
						NumaNode:    2,
						NetDevClass: hardware.Infiniband,
					},
				},
				Providers: []string{"p1", "p2", "p3"},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := fabricInterfaceSetToHostFabric(tc.fis)

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}
