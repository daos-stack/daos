//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"net"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestAgent_FabricInterface_AddProvider(t *testing.T) {
	for name, tc := range map[string]struct {
		fi       *FabricInterface
		provider string
		expFI    *FabricInterface
	}{
		"nil": {
			provider: "ofi+sockets",
		},
		"empty": {
			fi: &FabricInterface{
				Name:        "test",
				NetDevClass: hardware.Ether,
			},
			provider: "p1",
			expFI: &FabricInterface{
				Name:        "test",
				NetDevClass: hardware.Ether,
				Providers:   []string{"p1"},
			},
		},
		"add": {
			fi: &FabricInterface{
				Name:        "test",
				NetDevClass: hardware.Ether,
				Providers:   []string{"p1", "p2"},
			},
			provider: "p3",
			expFI: &FabricInterface{
				Name:        "test",
				NetDevClass: hardware.Ether,
				Providers:   []string{"p1", "p2", "p3"},
			},
		},
		"empty provider string": {
			fi: &FabricInterface{
				Name:        "test",
				NetDevClass: hardware.Ether,
				Providers:   []string{"p1", "p2"},
			},
			expFI: &FabricInterface{
				Name:        "test",
				NetDevClass: hardware.Ether,
				Providers:   []string{"p1", "p2"},
			},
		},
		"duplicate provider string": {
			fi: &FabricInterface{
				Name:        "test",
				NetDevClass: hardware.Ether,
				Providers:   []string{"p1", "p2"},
			},
			provider: "p1",
			expFI: &FabricInterface{
				Name:        "test",
				NetDevClass: hardware.Ether,
				Providers:   []string{"p1", "p2"},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.fi.AddProvider(tc.provider)

			if diff := cmp.Diff(tc.expFI, tc.fi); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestAgent_NewNUMAFabric(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	result := newNUMAFabric(log)

	if result == nil {
		t.Fatal("result was nil")
	}

	if result.numaMap == nil {
		t.Fatal("map was nil")
	}
}

func TestAgent_NUMAFabric_NumNUMANodes(t *testing.T) {
	for name, tc := range map[string]struct {
		nf        *NUMAFabric
		expResult int
	}{
		"nil": {},
		"empty struct": {
			nf: &NUMAFabric{},
		},
		"multiple nodes": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0:  {},
					1:  {},
					3:  {},
					10: {},
				},
			},
			expResult: 4,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)
			if tc.nf != nil {
				tc.nf.log = log
			}

			common.AssertEqual(t, tc.expResult, tc.nf.NumNUMANodes(), "")
		})
	}
}

func TestAgent_NUMAFabric_NumDevices(t *testing.T) {
	for name, tc := range map[string]struct {
		nf        *NUMAFabric
		node      int
		expResult int
	}{
		"nil": {},
		"empty": {
			nf:   &NUMAFabric{},
			node: 5,
		},
		"multiple devices on node": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					3: {&FabricInterface{}, &FabricInterface{}},
				},
			},
			node:      3,
			expResult: 2,
		},
		"multiple nodes": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0:  {&FabricInterface{}, &FabricInterface{}, &FabricInterface{}},
					1:  {&FabricInterface{}},
					3:  {&FabricInterface{}},
					10: {&FabricInterface{}},
				},
			},
			node:      0,
			expResult: 3,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)
			if tc.nf != nil {
				tc.nf.log = log
			}

			common.AssertEqual(t, tc.expResult, tc.nf.NumDevices(tc.node), "")
		})
	}
}

func TestAgent_NUMAFabric_Add(t *testing.T) {
	for name, tc := range map[string]struct {
		nf        *NUMAFabric
		input     *FabricInterface
		node      int
		expErr    error
		expResult map[int][]*FabricInterface
	}{
		"nil": {
			expErr: errors.New("nil NUMAFabric"),
		},
		"empty": {
			nf:    newNUMAFabric(nil),
			input: &FabricInterface{Name: "test1"},
			node:  2,
			expResult: map[int][]*FabricInterface{
				2: {{Name: "test1"}},
			},
		},
		"non-empty": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					2: {{Name: "t1"}},
					3: {{Name: "t2"}, {Name: "t3"}},
				},
			},
			input: &FabricInterface{Name: "test1"},
			node:  2,
			expResult: map[int][]*FabricInterface{
				2: {{Name: "t1"}, {Name: "test1"}},
				3: {{Name: "t2"}, {Name: "t3"}},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)
			if tc.nf != nil {
				tc.nf.log = log
			}

			err := tc.nf.Add(tc.node, tc.input)

			common.CmpErr(t, tc.expErr, err)

			if tc.nf == nil {
				return
			}
			if diff := cmp.Diff(tc.expResult, tc.nf.numaMap); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestAgent_NUMAFabric_GetDevice(t *testing.T) {
	for name, tc := range map[string]struct {
		nf          *NUMAFabric
		node        int
		provider    string
		netDevClass hardware.NetDevClass
		expErr      error
		expResults  []*FabricInterface
	}{
		"nil": {
			expErr: errors.New("nil NUMAFabric"),
		},
		"empty": {
			provider:    "ofi+sockets",
			nf:          newNUMAFabric(nil),
			netDevClass: hardware.Loopback,
			expErr:      errors.New("no suitable fabric interface"),
		},
		"no provider": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+sockets"},
						},
						{
							Name:        "t2",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+sockets"},
						},
						{
							Name:        "t3",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+sockets"},
						},
					},
				},
			},
			node:        0,
			netDevClass: hardware.Ether,
			expErr:      errors.New("provider is required"),
		},
		"type not found": {
			provider: "ofi+sockets",
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+sockets"},
						},
						{
							Name:        "t2",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+sockets"},
						},
						{
							Name:        "t3",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+sockets"},
						},
					},
				},
			},
			node:        0,
			netDevClass: hardware.Infiniband,
			expErr:      errors.New("no suitable fabric interface"),
		},
		"provider not found": {
			provider: "ofi+verbs",
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: hardware.Infiniband,
							Providers:   []string{"ofi+sockets"},
						},
						{
							Name:        "t2",
							NetDevClass: hardware.Infiniband,
							Providers:   []string{"ofi+sockets"},
						},
						{
							Name:        "t3",
							NetDevClass: hardware.Infiniband,
							Providers:   []string{"ofi+sockets"},
						},
					},
				},
			},
			node:        0,
			netDevClass: hardware.Infiniband,
			expErr:      errors.New("no suitable fabric interface"),
		},
		"choose first device": {
			provider: "ofi+sockets",
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: hardware.Infiniband,
							Providers:   []string{"ofi+sockets"},
						},
					},
				},
			},
			node:        0,
			netDevClass: hardware.Infiniband,
			expResults: []*FabricInterface{
				{
					Name:        "t1",
					NetDevClass: hardware.Infiniband,
					Providers:   []string{"ofi+sockets"},
				},
				{
					Name:        "t1",
					NetDevClass: hardware.Infiniband,
					Providers:   []string{"ofi+sockets"},
				},
			},
		},
		"choose later device": {
			provider: "ofi+sockets",
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+sockets"},
						},
						{
							Name:        "t2",
							NetDevClass: hardware.Infiniband,
							Providers:   []string{"ofi+sockets"},
						},
					},
				},
			},
			node:        0,
			netDevClass: hardware.Infiniband,
			expResults: []*FabricInterface{
				{
					Name:        "t2",
					NetDevClass: hardware.Infiniband,
					Providers:   []string{"ofi+sockets"},
				},
				{
					Name:        "t2",
					NetDevClass: hardware.Infiniband,
					Providers:   []string{"ofi+sockets"},
				},
				{
					Name:        "t2",
					NetDevClass: hardware.Infiniband,
					Providers:   []string{"ofi+sockets"},
				},
			},
		},
		"nothing on NUMA node": {
			provider: "ofi+sockets",
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: hardware.Infiniband,
							Providers:   []string{"ofi+sockets"},
						},
					},
					1: {},
				},
			},
			node:        1,
			netDevClass: hardware.Infiniband,
			expResults: []*FabricInterface{
				{
					Name:        "t1",
					NetDevClass: hardware.Infiniband,
					Providers:   []string{"ofi+sockets"},
				},
			},
		},
		"type not found on NUMA node": {
			provider: "ofi+sockets",
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: hardware.Infiniband,
							Providers:   []string{"ofi+sockets"},
						},
					},
					1: {
						{
							Name:        "t2",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+sockets"},
						},
					},
				},
			},
			node:        1,
			netDevClass: hardware.Infiniband,
			expResults: []*FabricInterface{
				{
					Name:        "t1",
					NetDevClass: hardware.Infiniband,
					Providers:   []string{"ofi+sockets"},
				},
				{
					Name:        "t1",
					NetDevClass: hardware.Infiniband,
					Providers:   []string{"ofi+sockets"},
				},
			},
		},
		"manual FI matches any": {
			provider: "ofi+sockets",
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: FabricDevClassManual,
							Providers:   []string{"ofi+sockets"},
						},
					},
					1: {
						{
							Name:        "t2",
							NetDevClass: FabricDevClassManual,
							Providers:   []string{"ofi+sockets"},
						},
					},
				},
			},
			node:        1,
			netDevClass: hardware.Infiniband,
			expResults: []*FabricInterface{
				{
					Name:        "t2",
					NetDevClass: FabricDevClassManual,
					Providers:   []string{"ofi+sockets"},
				},
				{
					Name:        "t2",
					NetDevClass: FabricDevClassManual,
					Providers:   []string{"ofi+sockets"},
				},
			},
		},
		"load balancing": {
			provider: "ofi+sockets",
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+sockets"},
						},
						{
							Name:        "t2",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+sockets"},
						},
						{
							Name:        "t3",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+sockets"},
						},
					},
				},
				currentNumaDevIdx: map[int]int{
					0: 1,
				},
			},
			node:        0,
			netDevClass: hardware.Ether,
			expResults: []*FabricInterface{
				{
					Name:        "t2",
					NetDevClass: hardware.Ether,
					Providers:   []string{"ofi+sockets"},
				},
				{
					Name:        "t3",
					NetDevClass: hardware.Ether,
					Providers:   []string{"ofi+sockets"},
				},
				{
					Name:        "t1",
					NetDevClass: hardware.Ether,
					Providers:   []string{"ofi+sockets"},
				},
				{
					Name:        "t2",
					NetDevClass: hardware.Ether,
					Providers:   []string{"ofi+sockets"},
				},
			},
		},
		"validating IPs fails": {
			provider: "ofi+sockets",
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: hardware.Infiniband,
							Providers:   []string{"ofi+sockets"},
						},
					},
				},
				getAddrInterface: func(_ string) (addrFI, error) {
					return nil, errors.New("mock getAddrInterface")
				},
			},
			node:        0,
			netDevClass: hardware.Infiniband,
			expErr:      FabricNotFoundErr(hardware.Infiniband),
		},
		"specific provider": {
			provider: "ofi+verbs",
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+sockets"},
						},
						{
							Name:        "t2",
							Domain:      "t2_dom",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+sockets", "ofi+verbs"},
						},
					},
					1: {
						{
							Name:        "t3",
							Domain:      "t3_dom",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+sockets"},
						},
					},
				},
			},
			node:        0,
			netDevClass: hardware.Ether,
			expResults: []*FabricInterface{
				{
					Name:        "t2",
					Domain:      "t2_dom",
					NetDevClass: hardware.Ether,
					Providers:   []string{"ofi+sockets", "ofi+verbs"},
				},
				{
					Name:        "t2",
					Domain:      "t2_dom",
					NetDevClass: hardware.Ether,
					Providers:   []string{"ofi+sockets", "ofi+verbs"},
				},
				{
					Name:        "t2",
					Domain:      "t2_dom",
					NetDevClass: hardware.Ether,
					Providers:   []string{"ofi+sockets", "ofi+verbs"},
				},
			},
		},
		"specific provider from other numa": {
			provider: "ofi+verbs",
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+sockets"},
						},
					},
					1: {
						{
							Name:        "t2",
							Domain:      "t2_dom",
							NetDevClass: hardware.Ether,
							Providers:   []string{"ofi+verbs"},
						},
					},
				},
			},
			node:        0,
			netDevClass: hardware.Ether,
			expResults: []*FabricInterface{
				{
					Name:        "t2",
					Domain:      "t2_dom",
					NetDevClass: hardware.Ether,
					Providers:   []string{"ofi+verbs"},
				},
				{
					Name:        "t2",
					Domain:      "t2_dom",
					NetDevClass: hardware.Ether,
					Providers:   []string{"ofi+verbs"},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)
			if tc.nf != nil {
				tc.nf.log = log
				if tc.nf.getAddrInterface == nil {
					tc.nf.getAddrInterface = getMockNetInterfaceSuccess
				}
			}

			var results []*FabricInterface
			for i := 0; i < tc.nf.NumDevices(tc.node)+1; i++ {
				result, err := tc.nf.GetDevice(tc.node, tc.netDevClass, tc.provider)
				common.CmpErr(t, tc.expErr, err)
				if tc.expErr != nil {
					return
				}
				results = append(results, result)
			}

			if diff := cmp.Diff(tc.expResults, results); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestAgent_NUMAFabric_Find(t *testing.T) {
	for name, tc := range map[string]struct {
		nf        *NUMAFabric
		name      string
		expResult *FabricInterface
		expErr    error
	}{
		"nil": {
			name:   "eth0",
			expErr: errors.New("nil"),
		},
		"not found": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: hardware.Ether,
						},
						{
							Name:        "t2",
							NetDevClass: hardware.Ether,
						},
						{
							Name:        "t3",
							NetDevClass: hardware.Ether,
						},
					},
				},
			},
			name:   "t4",
			expErr: errors.New("not found"),
		},
		"found": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: hardware.Ether,
						},
						{
							Name:        "t2",
							NetDevClass: hardware.Ether,
						},
						{
							Name:        "t3",
							NetDevClass: hardware.Ether,
						},
					},
				},
			},
			name: "t2",
			expResult: &FabricInterface{
				Name:        "t2",
				NetDevClass: hardware.Ether,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := tc.nf.Find(tc.name)

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestAgent_NUMAFabricFromScan(t *testing.T) {
	for name, tc := range map[string]struct {
		input               *hardware.FabricInterfaceSet
		expResult           map[int][]*FabricInterface
		possibleDefaultNUMA []int
	}{
		"no devices in scan": {
			expResult:           map[int][]*FabricInterface{},
			possibleDefaultNUMA: []int{0},
		},
		"include lo": {
			input: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Providers:   common.NewStringSet("ofi+sockets"),
					Name:        "test0",
					OSDevice:    "os_test0",
					NUMANode:    1,
					DeviceClass: hardware.Ether,
				},
				&hardware.FabricInterface{
					Providers:   common.NewStringSet("ofi+sockets"),
					Name:        "lo",
					OSDevice:    "lo",
					NUMANode:    1,
					DeviceClass: hardware.Loopback,
				},
			),
			expResult: map[int][]*FabricInterface{
				1: {

					{
						Name:        "lo",
						Domain:      "lo",
						NetDevClass: hardware.Loopback,
						Providers:   []string{"ofi+sockets"},
					},
					{
						Name:        "os_test0",
						Domain:      "test0",
						NetDevClass: hardware.Ether,
						Providers:   []string{"ofi+sockets"},
					},
				},
			},
			possibleDefaultNUMA: []int{1},
		},
		"multiple devices": {
			input: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Providers:   common.NewStringSet("ofi+sockets"),
					Name:        "test0",
					OSDevice:    "os_test0",
					NUMANode:    1,
					DeviceClass: hardware.Ether,
				},
				&hardware.FabricInterface{
					Providers:   common.NewStringSet("ofi+verbs"),
					Name:        "test1",
					OSDevice:    "os_test1",
					NUMANode:    0,
					DeviceClass: hardware.Infiniband,
				},
				&hardware.FabricInterface{
					Providers:   common.NewStringSet("ofi+sockets"),
					Name:        "test2",
					OSDevice:    "os_test2",
					NUMANode:    0,
					DeviceClass: hardware.Ether,
				},
			),
			expResult: map[int][]*FabricInterface{
				0: {
					{
						Name:        "os_test1",
						Domain:      "test1",
						NetDevClass: hardware.Infiniband,
						Providers:   []string{"ofi+verbs"},
					},
					{
						Name:        "os_test2",
						Domain:      "test2",
						NetDevClass: hardware.Ether,
						Providers:   []string{"ofi+sockets"},
					},
				},
				1: {
					{
						Name:        "os_test0",
						Domain:      "test0",
						NetDevClass: hardware.Ether,
						Providers:   []string{"ofi+sockets"},
					},
				},
			},
			possibleDefaultNUMA: []int{0, 1},
		},
		"multiple providers per device": {
			input: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Providers:   common.NewStringSet("ofi+sockets", "ofi+tcp"),
					Name:        "test0",
					OSDevice:    "os_test0",
					NUMANode:    1,
					DeviceClass: hardware.Ether,
				},
				&hardware.FabricInterface{
					Providers:   common.NewStringSet("ofi+verbs"),
					Name:        "verbs_test1",
					OSDevice:    "os_test1",
					NUMANode:    0,
					DeviceClass: hardware.Infiniband,
				},
				&hardware.FabricInterface{
					Providers:   common.NewStringSet("ofi+sockets", "ofi+tcp"),
					Name:        "test1",
					OSDevice:    "os_test1",
					NUMANode:    0,
					DeviceClass: hardware.Infiniband,
				},
			),
			expResult: map[int][]*FabricInterface{
				0: {

					{
						Name:        "os_test1",
						Domain:      "test1",
						NetDevClass: hardware.Infiniband,
						Providers:   []string{"ofi+sockets", "ofi+tcp"},
					},
					{
						Name:        "os_test1",
						Domain:      "verbs_test1",
						NetDevClass: hardware.Infiniband,
						Providers:   []string{"ofi+verbs"},
					},
				},
				1: {
					{
						Name:        "os_test0",
						Domain:      "test0",
						NetDevClass: hardware.Ether,
						Providers:   []string{"ofi+sockets", "ofi+tcp"},
					},
				},
			},
			possibleDefaultNUMA: []int{0, 1},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			result := NUMAFabricFromScan(context.TODO(), log, tc.input)

			if diff := cmp.Diff(tc.expResult, result.numaMap); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}

			defaultNumaOK := false
			for _, numa := range tc.possibleDefaultNUMA {
				if numa == result.defaultNumaNode {
					defaultNumaOK = true
				}
			}

			if !defaultNumaOK {
				t.Fatalf("default NUMA node %d (expected in list: %+v)", result.defaultNumaNode, tc.possibleDefaultNUMA)
			}
		})
	}
}

func TestAgent_NUMAFabricFromConfig(t *testing.T) {
	for name, tc := range map[string]struct {
		input               []*NUMAFabricConfig
		expResult           map[int][]*FabricInterface
		possibleDefaultNUMA []int
	}{
		"empty input": {
			expResult:           map[int][]*FabricInterface{},
			possibleDefaultNUMA: []int{0},
		},
		"no devices on NUMA node": {
			input: []*NUMAFabricConfig{
				{
					NUMANode:   1,
					Interfaces: []*FabricInterfaceConfig{},
				},
			},
			expResult:           map[int][]*FabricInterface{},
			possibleDefaultNUMA: []int{0},
		},
		"single NUMA node": {
			input: []*NUMAFabricConfig{
				{
					NUMANode: 1,
					Interfaces: []*FabricInterfaceConfig{
						{
							Interface: "test0",
							Domain:    "test0_domain",
						},
					},
				},
			},
			expResult: map[int][]*FabricInterface{
				1: {
					{
						Name:        "test0",
						Domain:      "test0_domain",
						NetDevClass: FabricDevClassManual,
					},
				},
			},
			possibleDefaultNUMA: []int{1},
		},
		"multiple devices": {
			input: []*NUMAFabricConfig{
				{
					NUMANode: 0,
					Interfaces: []*FabricInterfaceConfig{
						{
							Interface: "test1",
						},
						{
							Interface: "test2",
							Domain:    "test2_domain",
						},
					},
				},
				{
					NUMANode: 1,
					Interfaces: []*FabricInterfaceConfig{
						{
							Interface: "test0",
							Domain:    "test0_domain",
						},
					},
				},
			},
			expResult: map[int][]*FabricInterface{
				0: {
					{
						Name:        "test1",
						NetDevClass: FabricDevClassManual,
					},
					{
						Name:        "test2",
						Domain:      "test2_domain",
						NetDevClass: FabricDevClassManual,
					},
				},
				1: {
					{
						Name:        "test0",
						Domain:      "test0_domain",
						NetDevClass: FabricDevClassManual,
					},
				},
			},
			possibleDefaultNUMA: []int{0, 1},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			result := NUMAFabricFromConfig(log, tc.input)

			if diff := cmp.Diff(tc.expResult, result.numaMap); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}

			defaultNumaOK := false
			for _, numa := range tc.possibleDefaultNUMA {
				if numa == result.defaultNumaNode {
					defaultNumaOK = true
				}
			}

			if !defaultNumaOK {
				t.Fatalf("default NUMA node %d (expected in list: %+v)", result.defaultNumaNode, tc.possibleDefaultNUMA)
			}
		})
	}
}

type mockNetInterface struct {
	addrs    []net.Addr
	addrsErr error
}

func (m *mockNetInterface) Addrs() ([]net.Addr, error) {
	return m.addrs, m.addrsErr
}

func getMockNetInterfaceSuccess(_ string) (addrFI, error) {
	return &mockNetInterface{
		addrs: []net.Addr{
			&net.IPNet{
				IP: net.IP("127.0.0.1"),
			},
		},
	}, nil
}

func TestAgent_NUMAFabric_validateDevice(t *testing.T) {
	getMockNetInterfaceFunc := func(addrs []net.Addr, err error) func(string) (addrFI, error) {
		return func(_ string) (addrFI, error) {
			return &mockNetInterface{
				addrs:    addrs,
				addrsErr: err,
			}, nil
		}
	}

	for name, tc := range map[string]struct {
		getAddrInterface func(name string) (addrFI, error)
		expErr           error
	}{
		"getAddrInterface fails": {
			getAddrInterface: func(name string) (addrFI, error) {
				return nil, errors.New("mock getAddrInterface")
			},
			expErr: errors.New("mock getAddrInterface"),
		},
		"interface Addrs() fails": {
			getAddrInterface: getMockNetInterfaceFunc(nil, errors.New("mock Addrs()")),
			expErr:           errors.New("mock Addrs()"),
		},
		"empty Addrs()": {
			getAddrInterface: getMockNetInterfaceFunc([]net.Addr{}, nil),
			expErr:           errors.New("no IP addresses"),
		},
		"no IP addrs": {
			getAddrInterface: getMockNetInterfaceFunc([]net.Addr{
				&net.TCPAddr{},
			}, nil),
			expErr: errors.New("no IP addresses"),
		},
		"IP addr is empty": {
			getAddrInterface: getMockNetInterfaceFunc([]net.Addr{
				&net.IPNet{},
			}, nil),
			expErr: errors.New("no IP addresses"),
		},
		"IP addr is unspecified": {
			getAddrInterface: getMockNetInterfaceFunc([]net.Addr{
				&net.IPNet{
					IP: net.IPv4zero,
				},
			}, nil),
			expErr: errors.New("no IP addresses"),
		},
		"success": {
			getAddrInterface: getMockNetInterfaceSuccess,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			nf := newNUMAFabric(log)
			nf.getAddrInterface = tc.getAddrInterface

			err := nf.validateDevice(&FabricInterface{
				Name: "not_real",
			})

			common.CmpErr(t, tc.expErr, err)
		})
	}
}
