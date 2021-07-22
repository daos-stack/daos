//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
)

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
		netDevClass uint32
		expErr      error
		expResult   *FabricInterface
	}{
		"nil": {
			expErr: errors.New("nil NUMAFabric"),
		},
		"empty": {
			nf:        newNUMAFabric(nil),
			expResult: DefaultFabricInterface,
		},
		"type not found": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: netdetect.Ether,
						},
						{
							Name:        "t2",
							NetDevClass: netdetect.Ether,
						},
						{
							Name:        "t3",
							NetDevClass: netdetect.Ether,
						},
					},
				},
			},
			node:        0,
			netDevClass: netdetect.Infiniband,
			expErr:      errors.New("no suitable fabric interface"),
		},
		"choose first device": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: netdetect.Infiniband,
						},
					},
				},
			},
			node:        0,
			netDevClass: netdetect.Infiniband,
			expResult: &FabricInterface{
				Name:        "t1",
				NetDevClass: netdetect.Infiniband,
			},
		},
		"choose later device": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: netdetect.Ether,
						},
						{
							Name:        "t2",
							NetDevClass: netdetect.Infiniband,
						},
					},
				},
			},
			node:        0,
			netDevClass: netdetect.Infiniband,
			expResult: &FabricInterface{
				Name:        "t2",
				NetDevClass: netdetect.Infiniband,
			},
		},
		"nothing on NUMA node": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: netdetect.Infiniband,
						},
					},
					1: {},
				},
			},
			node:        1,
			netDevClass: netdetect.Infiniband,
			expResult: &FabricInterface{
				Name:        "t1",
				NetDevClass: netdetect.Infiniband,
			},
		},
		"type not found on NUMA node": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: netdetect.Infiniband,
						},
					},
					1: {
						{
							Name:        "t2",
							NetDevClass: netdetect.Ether,
						},
					},
				},
			},
			node:        1,
			netDevClass: netdetect.Infiniband,
			expResult: &FabricInterface{
				Name:        "t1",
				NetDevClass: netdetect.Infiniband,
			},
		},
		"load balancing": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: netdetect.Ether,
						},
						{
							Name:        "t2",
							NetDevClass: netdetect.Ether,
						},
						{
							Name:        "t3",
							NetDevClass: netdetect.Ether,
						},
					},
				},
				currentNumaDevIdx: map[int]int{
					0: 1,
				},
			},
			node:        0,
			netDevClass: netdetect.Ether,
			expResult: &FabricInterface{
				Name:        "t2",
				NetDevClass: netdetect.Ether,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)
			if tc.nf != nil {
				tc.nf.log = log
			}

			result, err := tc.nf.GetDevice(tc.node, tc.netDevClass)

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestAgent_NUMAFabric_getNextDevice(t *testing.T) {
	for name, tc := range map[string]struct {
		nf         *NUMAFabric
		node       int
		expErr     error
		expResult  *FabricInterface
		expNodeIdx int
	}{
		"empty node": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {},
				},
			},
			node:   0,
			expErr: errors.New("no fabric interfaces"),
		},
		"single item": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: netdetect.Ether,
						},
					},
				},
				currentNumaDevIdx: map[int]int{
					0: 0,
				},
			},
			expResult: &FabricInterface{
				Name:        "t1",
				NetDevClass: netdetect.Ether,
			},
		},
		"multi item": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: netdetect.Ether,
						},
						{
							Name:        "t2",
							NetDevClass: netdetect.Ether,
						},
						{
							Name:        "t3",
							NetDevClass: netdetect.Ether,
						},
					},
				},
				currentNumaDevIdx: map[int]int{
					0: 0,
				},
			},
			expResult: &FabricInterface{
				Name:        "t1",
				NetDevClass: netdetect.Ether,
			},
			expNodeIdx: 1,
		},
		"round robin": {
			nf: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "t1",
							NetDevClass: netdetect.Ether,
						},
						{
							Name:        "t2",
							NetDevClass: netdetect.Ether,
						},
						{
							Name:        "t3",
							NetDevClass: netdetect.Ether,
						},
					},
				},
				currentNumaDevIdx: map[int]int{
					0: 2,
				},
			},
			expResult: &FabricInterface{
				Name:        "t3",
				NetDevClass: netdetect.Ether,
			},
			expNodeIdx: 0,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := tc.nf.getNextDevice(tc.node)

			common.AssertEqual(t, tc.expNodeIdx, tc.nf.currentNumaDevIdx[tc.node], "")
			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestAgent_NUMAFabricFromScan(t *testing.T) {
	for name, tc := range map[string]struct {
		input               []*netdetect.FabricScan
		getDevAlias         func(ctx context.Context, devName string) (string, error)
		expResult           map[int][]*FabricInterface
		possibleDefaultNUMA []int
	}{
		"no devices in scan": {
			expResult:           map[int][]*FabricInterface{},
			possibleDefaultNUMA: []int{0},
		},
		"skip lo": {
			input: []*netdetect.FabricScan{
				{
					Provider:    "ofi+sockets",
					DeviceName:  "test0",
					NUMANode:    1,
					NetDevClass: netdetect.Ether,
				},
				{
					Provider:   "ofi+sockets",
					DeviceName: "lo",
					NUMANode:   1,
				},
			},
			expResult: map[int][]*FabricInterface{
				1: {
					{
						Name:        "test0",
						NetDevClass: netdetect.Ether,
					},
				},
			},
			possibleDefaultNUMA: []int{1},
		},
		"multiple devices": {
			input: []*netdetect.FabricScan{
				{
					Provider:    "ofi+sockets",
					DeviceName:  "test0",
					NUMANode:    1,
					NetDevClass: netdetect.Ether,
				},
				{
					Provider:    "ofi+verbs",
					DeviceName:  "test1",
					NUMANode:    0,
					NetDevClass: netdetect.Infiniband,
				},
				{
					Provider:    "ofi+sockets",
					DeviceName:  "test2",
					NUMANode:    0,
					NetDevClass: netdetect.Ether,
				},
			},
			expResult: map[int][]*FabricInterface{
				0: {
					{
						Name:        "test1",
						NetDevClass: netdetect.Infiniband,
					},
					{
						Name:        "test2",
						NetDevClass: netdetect.Ether,
					},
				},
				1: {
					{
						Name:        "test0",
						NetDevClass: netdetect.Ether,
					},
				},
			},
			possibleDefaultNUMA: []int{0, 1},
		},
		"with device alias": {
			getDevAlias: func(_ context.Context, dev string) (string, error) {
				return dev + "_alias", nil
			},
			input: []*netdetect.FabricScan{
				{
					Provider:    "ofi+sockets",
					DeviceName:  "test0",
					NUMANode:    2,
					NetDevClass: netdetect.Ether,
				},
				{
					Provider:    "ofi+verbs",
					DeviceName:  "test1",
					NUMANode:    1,
					NetDevClass: netdetect.Infiniband,
				},
				{
					Provider:    "ofi+sockets",
					DeviceName:  "test2",
					NUMANode:    1,
					NetDevClass: netdetect.Ether,
				},
			},
			expResult: map[int][]*FabricInterface{
				1: {
					{
						Name:        "test1",
						NetDevClass: netdetect.Infiniband,
						Domain:      "test1_alias",
					},
					{
						Name:        "test2",
						NetDevClass: netdetect.Ether,
						Domain:      "test2_alias",
					},
				},
				2: {
					{
						Name:        "test0",
						NetDevClass: netdetect.Ether,
						Domain:      "test0_alias",
					},
				},
			},
			possibleDefaultNUMA: []int{1, 2},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			result := NUMAFabricFromScan(context.TODO(), log, tc.input, tc.getDevAlias)

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
