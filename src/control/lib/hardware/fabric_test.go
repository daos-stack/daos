//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"context"
	"fmt"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func fabricCmpOpts() []cmp.Option {
	return []cmp.Option{
		cmp.AllowUnexported(FabricInterfaceSet{}, FabricProviderSet{}),
	}
}

func ignoreFabricProviderPriorityCmpOpts() []cmp.Option {
	return []cmp.Option{
		cmp.FilterPath(
			func(p cmp.Path) bool {
				return p.Last().String() == ".Priority"
			},
			cmp.Ignore(),
		),
		cmp.FilterPath(
			func(p cmp.Path) bool {
				return p.Last().String() == ".byPriority"
			},
			cmp.Ignore(),
		),
	}
}

// newTestFabricProviderSet creates a new set of FabricProviders. The priority is derived from the order
// of the provider strings.
func newTestFabricProviderSet(providers ...string) *FabricProviderSet {
	set := new(FabricProviderSet)
	for i, p := range providers {
		set.Add(&FabricProvider{
			Name:     p,
			Priority: i,
		})
	}

	return set
}

func TestHardware_FabricProvider_String(t *testing.T) {
	for name, tc := range map[string]struct {
		p         *FabricProvider
		expResult string
	}{
		"nil": {
			expResult: "<nil>",
		},
		"no name": {
			p:         &FabricProvider{},
			expResult: "<no name>",
		},
		"name": {
			p: &FabricProvider{
				Name: "p1",
			},
			expResult: "p1",
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.p.String(), "")
		})
	}
}

func TestHardware_FabricProviderSet_String(t *testing.T) {
	for name, tc := range map[string]struct {
		ps        *FabricProviderSet
		expResult string
	}{
		"nil": {
			expResult: "<nil>",
		},
		"empty": {
			ps:        newTestFabricProviderSet(),
			expResult: "<empty>",
		},
		"one provider": {
			ps:        newTestFabricProviderSet("p1"),
			expResult: "p1",
		},
		"multiple providers": {
			ps:        newTestFabricProviderSet("p3", "p2", "p1"),
			expResult: "p3, p2, p1",
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.ps.String(), "")
		})
	}
}

func TestHardware_FabricProviderSet_Len(t *testing.T) {
	for name, tc := range map[string]struct {
		ps        *FabricProviderSet
		expResult int
	}{
		"nil": {},
		"empty": {
			ps: newTestFabricProviderSet(),
		},
		"one provider": {
			ps:        newTestFabricProviderSet("p1"),
			expResult: 1,
		},
		"multiple providers": {
			ps:        newTestFabricProviderSet("p3", "p2", "p1"),
			expResult: 3,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.ps.Len(), "")
		})
	}
}

func TestHardware_FabricProviderSet_Has(t *testing.T) {
	for name, tc := range map[string]struct {
		ps        *FabricProviderSet
		prov      string
		expResult bool
	}{
		"nil": {
			prov: "p3",
		},
		"empty": {
			ps:   newTestFabricProviderSet(),
			prov: "p3",
		},
		"only provider": {
			ps:        newTestFabricProviderSet("p1"),
			prov:      "p1",
			expResult: true,
		},
		"single provider doesn't have": {
			ps:   newTestFabricProviderSet("p1"),
			prov: "p2",
		},
		"multiple providers has": {
			ps:        newTestFabricProviderSet("p3", "p2", "p1"),
			prov:      "p1",
			expResult: true,
		},
		"multiple providers doesn't have": {
			ps:   newTestFabricProviderSet("p3", "p2", "p1"),
			prov: "p6",
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.ps.Has(tc.prov), "")
		})
	}
}

func TestHardware_FabricProviderSet_Add(t *testing.T) {
	for name, tc := range map[string]struct {
		ps       *FabricProviderSet
		toAdd    []*FabricProvider
		expOrder []string
	}{
		"nil set": {
			toAdd: []*FabricProvider{
				{
					Name: "p1",
				},
			},
		},
		"no input": {
			ps: newTestFabricProviderSet(),
		},
		"nil in list": {
			ps: newTestFabricProviderSet(),
			toAdd: []*FabricProvider{
				nil,
				{
					Name: "p1",
				},
			},
			expOrder: []string{"p1"},
		},
		"empty in list": {
			ps: newTestFabricProviderSet(),
			toAdd: []*FabricProvider{
				{},
				{
					Name: "p1",
				},
			},
			expOrder: []string{"p1"},
		},
		"add one to empty set": {
			ps: newTestFabricProviderSet(),
			toAdd: []*FabricProvider{
				{
					Name: "p1",
				},
			},
			expOrder: []string{"p1"},
		},
		"add multi to empty set": {
			ps: newTestFabricProviderSet(),
			toAdd: []*FabricProvider{
				{
					Name:     "p1",
					Priority: 1,
				},
				{
					Name:     "p2",
					Priority: 2,
				},
				{
					Name:     "p3",
					Priority: 3,
				},
			},
			expOrder: []string{"p1", "p2", "p3"},
		},
		"add to existing set": {
			ps: NewFabricProviderSet(
				&FabricProvider{
					Name: "p0",
				}, &FabricProvider{
					Name:     "p4",
					Priority: 4,
				},
			),
			toAdd: []*FabricProvider{
				{
					Name:     "p1",
					Priority: 1,
				},
				{
					Name:     "p2",
					Priority: 2,
				},
				{
					Name:     "p3",
					Priority: 3,
				},
			},
			expOrder: []string{"p0", "p1", "p2", "p3", "p4"},
		},
		"add duplicate": {
			ps: newTestFabricProviderSet("p0", "p1"),
			toAdd: []*FabricProvider{
				{
					Name:     "p1",
					Priority: 1,
				},
				{
					Name:     "p2",
					Priority: 2,
				},
				{
					Name:     "p3",
					Priority: 3,
				},
			},
			expOrder: []string{"p0", "p1", "p2", "p3"},
		},
		"duplicates in input": {
			ps: newTestFabricProviderSet("p0"),
			toAdd: []*FabricProvider{
				{
					Name:     "p1",
					Priority: 2,
				},
				{
					Name:     "p2",
					Priority: 3,
				},
				{
					Name:     "p1",
					Priority: 1,
				},
			},
			expOrder: []string{"p0", "p1", "p2"},
		},
		"add duplicate lower priority": {
			ps: newTestFabricProviderSet("p0", "p1", "p2"),
			toAdd: []*FabricProvider{
				{
					Name:     "p1",
					Priority: 4,
				},
			},
			expOrder: []string{"p0", "p1", "p2"},
		},
		"add duplicate higher priority": {
			ps: newTestFabricProviderSet("p0", "p1", "p2"),
			toAdd: []*FabricProvider{
				{
					Name:     "p2",
					Priority: 0,
				},
			},
			expOrder: []string{"p0", "p2", "p1"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.ps.Add(tc.toAdd...)

			orderedSlice := tc.ps.ToSlice()
			test.AssertEqual(t, len(tc.expOrder), len(orderedSlice), "")
			for i, provName := range tc.expOrder {
				test.AssertEqual(t, provName, orderedSlice[i].Name, "")
			}
		})
	}
}

func TestHardware_FabricProviderSet_ToSlice(t *testing.T) {
	longList := []string{}
	expLongSlice := []*FabricProvider{}
	for i := 0; i < 25; i++ {
		name := fmt.Sprintf("prov%d", i)
		longList = append(longList, name)
		expLongSlice = append(expLongSlice, &FabricProvider{
			Name:     name,
			Priority: i,
		})
	}

	for name, tc := range map[string]struct {
		ps        *FabricProviderSet
		expResult []*FabricProvider
	}{
		"nil": {
			expResult: []*FabricProvider{},
		},
		"empty set": {
			ps:        newTestFabricProviderSet(),
			expResult: []*FabricProvider{},
		},
		"single member": {
			ps: newTestFabricProviderSet("p1"),
			expResult: []*FabricProvider{
				{
					Name: "p1",
				},
			},
		},
		"long list in order": {
			ps:        newTestFabricProviderSet(longList...),
			expResult: expLongSlice,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.ps.ToSlice()

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_FabricInterface_String(t *testing.T) {
	for name, tc := range map[string]struct {
		fi        *FabricInterface
		expResult string
	}{
		"nil": {
			expResult: "<nil>",
		},
		"empty": {
			fi:        &FabricInterface{},
			expResult: "<no name> (providers: none)",
		},
		"no OS name": {
			fi: &FabricInterface{
				Name:      "test0",
				Providers: newTestFabricProviderSet("p1", "p2"),
			},
			expResult: "test0 (providers: p1, p2)",
		},
		"with net interface": {
			fi: &FabricInterface{
				Name:          "test0",
				NetInterfaces: common.NewStringSet("os_test0"),
				Providers:     newTestFabricProviderSet("p1", "p2"),
			},
			expResult: "test0 (interface: os_test0) (providers: p1, p2)",
		},
		"different OS name": {
			fi: &FabricInterface{
				Name:          "test0:1",
				OSName:        "test0",
				NetInterfaces: common.NewStringSet("os_test0"),
				Providers:     newTestFabricProviderSet("p1", "p2"),
			},
			expResult: "test0:1 (OS name: test0) (interface: os_test0) (providers: p1, p2)",
		},
		"OS name": {
			fi: &FabricInterface{
				Name:          "test0",
				OSName:        "test0",
				NetInterfaces: common.NewStringSet("os_test0"),
				Providers:     newTestFabricProviderSet("p1", "p2"),
			},
			expResult: "test0 (interface: os_test0) (providers: p1, p2)",
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.fi.String(), "")
		})
	}
}

func TestHardware_FabricInterface_SupportsProvider(t *testing.T) {
	for name, tc := range map[string]struct {
		fi        *FabricInterface
		in        string
		expResult bool
	}{
		"nil": {
			in: "something",
		},
		"single not found": {
			fi: &FabricInterface{
				Providers: newTestFabricProviderSet("lib+p1", "lib+p3"),
			},
			in: "lib+p2",
		},
		"single match": {
			fi: &FabricInterface{
				Providers: newTestFabricProviderSet("lib+p1", "lib+p2", "lib+p3"),
			},
			in:        "lib+p1",
			expResult: true,
		},
		"no prefix": {
			fi: &FabricInterface{
				Providers: newTestFabricProviderSet("p1", "p2", "p3"),
			},
			in:        "p3",
			expResult: true,
		},
		"no prefix not found": {
			fi: &FabricInterface{
				Providers: newTestFabricProviderSet("p1", "p2", "lib+p3"),
			},
			in: "p3",
		},
		"multi match": {
			fi: &FabricInterface{
				Providers: newTestFabricProviderSet("lib+p1", "lib+p2", "lib+p3"),
			},
			in:        "lib+p1,p2",
			expResult: true,
		},
		"partial match": {
			fi: &FabricInterface{
				Providers: newTestFabricProviderSet("lib+p1", "lib+p3"),
			},
			in: "lib+p1,p2",
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.fi.SupportsProvider(tc.in)

			test.AssertEqual(t, tc.expResult, result, "")
		})
	}
}

func TestHardware_FabricInterface_TopologyName(t *testing.T) {
	for name, tc := range map[string]struct {
		fi        *FabricInterface
		expResult string
		expErr    error
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"empty": {
			fi:     &FabricInterface{},
			expErr: errors.New("no name"),
		},
		"name only": {
			fi: &FabricInterface{
				Name: "fi0",
			},
			expResult: "fi0",
		},
		"different OS name": {
			fi: &FabricInterface{
				Name:   "fi0",
				OSName: "fi0_os",
			},
			expResult: "fi0_os",
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := tc.fi.TopologyName()

			test.AssertEqual(t, tc.expResult, result, "")
			test.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestHardware_NewFabricInterfaceSet(t *testing.T) {
	for name, tc := range map[string]struct {
		input     []*FabricInterface
		expResult *FabricInterfaceSet
	}{
		"no input": {
			expResult: &FabricInterfaceSet{
				byName:   fabricInterfaceMap{},
				byNetDev: map[string]fabricInterfaceMap{},
			},
		},
		"input added": {
			input: []*FabricInterface{
				{
					Name:          "test0",
					NetInterfaces: common.NewStringSet("os_test0"),
				},
				{
					Name:          "test1",
					NetInterfaces: common.NewStringSet("os_test1"),
				},
				{
					Name:          "os_test1",
					NetInterfaces: common.NewStringSet("os_test1"),
				},
			},
			expResult: &FabricInterfaceSet{
				byName: fabricInterfaceMap{
					"test0": {
						Name:          "test0",
						NetInterfaces: common.NewStringSet("os_test0"),
					},
					"test1": {
						Name:          "test1",
						NetInterfaces: common.NewStringSet("os_test1"),
					},
					"os_test1": {
						Name:          "os_test1",
						NetInterfaces: common.NewStringSet("os_test1"),
					},
				},
				byNetDev: map[string]fabricInterfaceMap{
					"os_test0": {
						"test0": {
							Name:          "test0",
							NetInterfaces: common.NewStringSet("os_test0"),
						},
					},
					"os_test1": {
						"test1": {
							Name:          "test1",
							NetInterfaces: common.NewStringSet("os_test1"),
						},
						"os_test1": {
							Name:          "os_test1",
							NetInterfaces: common.NewStringSet("os_test1"),
						},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := NewFabricInterfaceSet(tc.input...)

			if diff := cmp.Diff(tc.expResult, result, fabricCmpOpts()...); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_FabricInterfaceSet_Names(t *testing.T) {
	for name, tc := range map[string]struct {
		fis       *FabricInterfaceSet
		expResult []string
	}{
		"nil": {
			expResult: []string{},
		},
		"empty": {
			fis:       NewFabricInterfaceSet(),
			expResult: []string{},
		},
		"one": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "test0",
					NetInterfaces: common.NewStringSet("os_test0"),
				}),
			expResult: []string{"test0"},
		},
		"multiple": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "test0",
					NetInterfaces: common.NewStringSet("os_test0"),
				},
				&FabricInterface{
					Name:          "test1",
					NetInterfaces: common.NewStringSet("os_test1"),
				},
				&FabricInterface{
					Name:          "test2",
					NetInterfaces: common.NewStringSet("os_test2"),
				}),
			expResult: []string{"test0", "test1", "test2"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.fis.Names()

			test.AssertEqual(t, len(tc.expResult), tc.fis.NumFabricInterfaces(), "")
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_FabricInterfaceSet_NetDevices(t *testing.T) {
	for name, tc := range map[string]struct {
		fis       *FabricInterfaceSet
		expResult []string
	}{
		"nil": {
			expResult: []string{},
		},
		"empty": {
			fis:       NewFabricInterfaceSet(),
			expResult: []string{},
		},
		"one": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "test0",
					NetInterfaces: common.NewStringSet("os_test0"),
				}),
			expResult: []string{"os_test0"},
		},
		"multiple": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "test0",
					NetInterfaces: common.NewStringSet("os_test0"),
				},
				&FabricInterface{
					Name:          "test1",
					NetInterfaces: common.NewStringSet("os_test1"),
				},
				&FabricInterface{
					Name:          "test2",
					NetInterfaces: common.NewStringSet("os_test2"),
				}),
			expResult: []string{"os_test0", "os_test1", "os_test2"},
		},
		"same net device": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "test0",
					NetInterfaces: common.NewStringSet("os_test0"),
				},
				&FabricInterface{
					Name:          "test1",
					NetInterfaces: common.NewStringSet("os_test0"),
				},
				&FabricInterface{
					Name:          "test2",
					NetInterfaces: common.NewStringSet("os_test0"),
				}),
			expResult: []string{"os_test0"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.fis.NetDevices()

			test.AssertEqual(t, len(tc.expResult), tc.fis.NumNetDevices(), "")
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_FabricInterfaceSet_Update(t *testing.T) {
	for name, tc := range map[string]struct {
		fis       *FabricInterfaceSet
		input     *FabricInterface
		expResult *FabricInterfaceSet
	}{
		"nil": {
			input: &FabricInterface{},
		},
		"nil input": {
			fis:       NewFabricInterfaceSet(),
			expResult: NewFabricInterfaceSet(),
		},
		"no name": {
			fis:       NewFabricInterfaceSet(),
			input:     &FabricInterface{},
			expResult: NewFabricInterfaceSet(),
		},
		"add to empty set": {
			fis: NewFabricInterfaceSet(),
			input: &FabricInterface{
				Name:          "one",
				NetInterfaces: common.NewStringSet("dev1"),
			},
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "one",
					NetInterfaces: common.NewStringSet("dev1"),
				},
			),
		},
		"add": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "one",
					NetInterfaces: common.NewStringSet("dev1"),
				},
				&FabricInterface{
					Name:          "two",
					NetInterfaces: common.NewStringSet("dev2"),
				},
			),
			input: &FabricInterface{
				Name:          "three",
				NetInterfaces: common.NewStringSet("dev3"),
			},
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "one",
					NetInterfaces: common.NewStringSet("dev1"),
				},
				&FabricInterface{
					Name:          "two",
					NetInterfaces: common.NewStringSet("dev2"),
				},
				&FabricInterface{
					Name:          "three",
					NetInterfaces: common.NewStringSet("dev3"),
				},
			),
		},
		"update": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "one",
					NetInterfaces: common.NewStringSet("dev1"),
				},
				&FabricInterface{
					Name:          "two",
					NetInterfaces: common.NewStringSet("dev2"),
				},
			),
			input: &FabricInterface{
				Name:          "two",
				NetInterfaces: common.NewStringSet("dev2"),
				DeviceClass:   Ether,
				NUMANode:      1,
				Providers:     newTestFabricProviderSet("p1", "p2"),
			},
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "one",
					NetInterfaces: common.NewStringSet("dev1"),
				},
				&FabricInterface{
					Name:          "two",
					NetInterfaces: common.NewStringSet("dev2"),
					DeviceClass:   Ether,
					NUMANode:      1,
					Providers:     newTestFabricProviderSet("p1", "p2"),
				},
			),
		},
		"can't override nonzero": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "one",
					NetInterfaces: common.NewStringSet("dev1"),
					DeviceClass:   Ether,
					NUMANode:      2,
				},
			),
			input: &FabricInterface{
				Name: "one",
			},
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "one",
					NetInterfaces: common.NewStringSet("dev1"),
					DeviceClass:   Ether,
					NUMANode:      2,
				},
			),
		},
		"add to providers": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "one",
					NetInterfaces: common.NewStringSet("dev1"),
					DeviceClass:   Ether,
					NUMANode:      2,
					Providers:     newTestFabricProviderSet("p0", "p1"),
				},
			),
			input: &FabricInterface{
				Name:          "one",
				NetInterfaces: common.NewStringSet("dev1"),
				DeviceClass:   Ether,
				NUMANode:      2,
				Providers:     newTestFabricProviderSet("p2", "p3"),
			},
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "one",
					NetInterfaces: common.NewStringSet("dev1"),
					DeviceClass:   Ether,
					NUMANode:      2,
					Providers:     newTestFabricProviderSet("p0", "p1", "p2", "p3"),
				},
			),
		},
		"add duplicate providers": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "one",
					NetInterfaces: common.NewStringSet("dev1"),
					DeviceClass:   Ether,
					NUMANode:      2,
					Providers:     newTestFabricProviderSet("p0", "p1"),
				},
			),
			input: &FabricInterface{
				Name:          "one",
				NetInterfaces: common.NewStringSet("dev1"),
				DeviceClass:   Ether,
				NUMANode:      2,
				Providers:     newTestFabricProviderSet("p1", "p2"),
			},
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "one",
					NetInterfaces: common.NewStringSet("dev1"),
					DeviceClass:   Ether,
					NUMANode:      2,
					Providers:     newTestFabricProviderSet("p0", "p1", "p2"),
				},
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.fis.Update(tc.input)

			cmpOpts := append(fabricCmpOpts(), ignoreFabricProviderPriorityCmpOpts()...)
			if diff := cmp.Diff(tc.expResult, tc.fis, cmpOpts...); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_FabricInterfaceSet_Remove(t *testing.T) {
	for name, tc := range map[string]struct {
		fis       *FabricInterfaceSet
		input     string
		expResult *FabricInterfaceSet
	}{
		"nil": {
			input: "something",
		},
		"empty": {
			fis:       NewFabricInterfaceSet(),
			input:     "something",
			expResult: NewFabricInterfaceSet(),
		},
		"not found": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "fi0",
					NetInterfaces: common.NewStringSet("net0"),
				},
			),
			input: "fi1",
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "fi0",
					NetInterfaces: common.NewStringSet("net0"),
				},
			),
		},
		"removed": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "fi0",
					NetInterfaces: common.NewStringSet("net0"),
				},
				&FabricInterface{
					Name:          "fi1",
					NetInterfaces: common.NewStringSet("net0"),
				},
			),
			input: "fi1",
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "fi0",
					NetInterfaces: common.NewStringSet("net0"),
				},
			),
		},
		"remove only item": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "fi0",
					NetInterfaces: common.NewStringSet("net0"),
				},
			),
			input:     "fi0",
			expResult: NewFabricInterfaceSet(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.fis.Remove(tc.input)

			if diff := cmp.Diff(tc.expResult, tc.fis, fabricCmpOpts()...); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_FabricInterfaceSet_GetInterface(t *testing.T) {
	for name, tc := range map[string]struct {
		fis       *FabricInterfaceSet
		name      string
		expResult *FabricInterface
		expErr    error
	}{
		"nil": {
			name:   "something",
			expErr: errors.New("nil"),
		},
		"empty": {
			fis:    NewFabricInterfaceSet(),
			name:   "something",
			expErr: errors.New("not found"),
		},
		"empty input": {
			fis:    NewFabricInterfaceSet(),
			expErr: errors.New("name is required"),
		},
		"not found": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "test0",
					NetInterfaces: common.NewStringSet("os_test0"),
				},
				&FabricInterface{
					Name:          "test1",
					NetInterfaces: common.NewStringSet("os_test1"),
				},
				&FabricInterface{
					Name:          "test2",
					NetInterfaces: common.NewStringSet("os_test2"),
				},
			),
			name:   "test10",
			expErr: errors.New("not found"),
		},
		"success": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "test0",
					NetInterfaces: common.NewStringSet("os_test0"),
				},
				&FabricInterface{
					Name:          "test1",
					NetInterfaces: common.NewStringSet("os_test1"),
				},
				&FabricInterface{
					Name:          "test2",
					NetInterfaces: common.NewStringSet("os_test2"),
				},
			),
			name: "test1",
			expResult: &FabricInterface{
				Name:          "test1",
				NetInterfaces: common.NewStringSet("os_test1"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := tc.fis.GetInterface(tc.name)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result, fabricCmpOpts()...); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_FabricInterfaceSet_GetInterfaceOnNetDevice(t *testing.T) {
	for name, tc := range map[string]struct {
		fis       *FabricInterfaceSet
		netDev    string
		provider  string
		expResult *FabricInterface
		expErr    error
	}{
		"nil": {
			netDev:   "something",
			provider: "something else",
			expErr:   errors.New("nil"),
		},
		"no dev input": {
			fis:      NewFabricInterfaceSet(),
			provider: "something",
			expErr:   errors.New("network device name is required"),
		},
		"no provider input": {
			fis:    NewFabricInterfaceSet(),
			netDev: "something",
			expErr: errors.New("provider is required"),
		},
		"net device not found": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "test0",
					NetInterfaces: common.NewStringSet("os_test0"),
				},
				&FabricInterface{
					Name:          "test1",
					NetInterfaces: common.NewStringSet("os_test1"),
				},
			),
			netDev:   "notfound",
			provider: "something",
			expErr:   errors.New("network device \"notfound\" not found"),
		},
		"provider not found on device": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "test0",
					NetInterfaces: common.NewStringSet("os_test0"),
					Providers:     newTestFabricProviderSet("p1", "p2"),
				},
				&FabricInterface{
					Name:          "test1",
					NetInterfaces: common.NewStringSet("os_test0"),
					Providers:     newTestFabricProviderSet("p3"),
				},
				&FabricInterface{
					Name:          "test2",
					NetInterfaces: common.NewStringSet("os_test2"),
					Providers:     newTestFabricProviderSet("p4"),
				},
			),
			netDev:   "os_test0",
			provider: "p4",
			expErr:   errors.New("provider \"p4\" not supported"),
		},
		"success": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "test0",
					NetInterfaces: common.NewStringSet("os_test0"),
					Providers:     newTestFabricProviderSet("p1", "p2"),
				},
				&FabricInterface{
					Name:          "test1",
					NetInterfaces: common.NewStringSet("os_test0"),
					Providers:     newTestFabricProviderSet("p3"),
				},
				&FabricInterface{
					Name:          "test2",
					NetInterfaces: common.NewStringSet("os_test2"),
					Providers:     newTestFabricProviderSet("p4"),
				},
			),
			netDev:   "os_test0",
			provider: "p3",
			expResult: &FabricInterface{
				Name:          "test1",
				NetInterfaces: common.NewStringSet("os_test0"),
				Providers:     newTestFabricProviderSet("p3"),
			},
		},
		"provider helper specified, success": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "test0",
					NetInterfaces: common.NewStringSet("os_test0"),
					Providers:     newTestFabricProviderSet("p1", "p2", "p3"),
				},
				&FabricInterface{
					Name:          "test1",
					NetInterfaces: common.NewStringSet("os_test0"),
					Providers:     newTestFabricProviderSet("p3;h1"),
				},
				&FabricInterface{
					Name:          "test2",
					NetInterfaces: common.NewStringSet("os_test2"),
					Providers:     newTestFabricProviderSet("p4"),
				},
			),
			netDev:   "os_test0",
			provider: "p3;h1",
			expResult: &FabricInterface{
				Name:          "test1",
				NetInterfaces: common.NewStringSet("os_test0"),
				Providers:     newTestFabricProviderSet("p3;h1"),
			},
		},
		"provider helper specified, not found": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "test0",
					NetInterfaces: common.NewStringSet("os_test0"),
					Providers:     newTestFabricProviderSet("p1", "p2"),
				},
				&FabricInterface{
					Name:          "test1",
					NetInterfaces: common.NewStringSet("os_test0"),
					Providers:     newTestFabricProviderSet("p3;h1", "p3"),
				},
				&FabricInterface{
					Name:          "test2",
					NetInterfaces: common.NewStringSet("os_test2"),
					Providers:     newTestFabricProviderSet("p4"),
				},
			),
			netDev:   "os_test0",
			provider: "p3;h2",
			expErr:   errors.New("not supported"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := tc.fis.GetInterfaceOnNetDevice(tc.netDev, tc.provider)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result, fabricCmpOpts()...); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_NetDevClass_String(t *testing.T) {
	for name, tc := range map[string]struct {
		ndc       NetDevClass
		expResult string
	}{
		"netrom": {
			ndc:       Netrom,
			expResult: "NETROM",
		},
		"ether": {
			ndc:       Ether,
			expResult: "ETHER",
		},
		"eether": {
			ndc:       Eether,
			expResult: "EETHER",
		},
		"ax25": {
			ndc:       Ax25,
			expResult: "AX25",
		},
		"pronet": {
			ndc:       Pronet,
			expResult: "PRONET",
		},
		"chaos": {
			ndc:       Chaos,
			expResult: "CHAOS",
		},
		"ieee802": {
			ndc:       IEEE802,
			expResult: "IEEE802",
		},
		"arcnet": {
			ndc:       Arcnet,
			expResult: "ARCNET",
		},
		"appletlk": {
			ndc:       Appletlk,
			expResult: "APPLETLK",
		},
		"dlci": {
			ndc:       Dlci,
			expResult: "DLCI",
		},
		"atm": {
			ndc:       Atm,
			expResult: "ATM",
		},
		"metricom": {
			ndc:       Metricom,
			expResult: "METRICOM",
		},
		"ieee1394": {
			ndc:       IEEE1394,
			expResult: "IEEE1394",
		},
		"eui64": {
			ndc:       Eui64,
			expResult: "EUI64",
		},
		"infiniband": {
			ndc:       Infiniband,
			expResult: "INFINIBAND",
		},
		"loopback": {
			ndc:       Loopback,
			expResult: "LOOPBACK",
		},
		"unknown": {
			ndc:       NetDevClass(0xFFFFFFFE),
			expResult: "UNKNOWN (0xfffffffe)",
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.ndc.String(), "")
		})
	}
}

func TestHardware_FabricScannerConfig_IsValid(t *testing.T) {
	for name, tc := range map[string]struct {
		config *FabricScannerConfig
		expErr error
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"minimal valid": {
			config: &FabricScannerConfig{
				TopologyProvider: &MockTopologyProvider{},
				FabricInterfaceProviders: []FabricInterfaceProvider{
					&MockFabricInterfaceProvider{},
				},
				NetDevClassProvider: &MockNetDevClassProvider{},
			},
		},
		"no TopologyProvider": {
			config: &FabricScannerConfig{
				FabricInterfaceProviders: []FabricInterfaceProvider{
					&MockFabricInterfaceProvider{},
				},
				NetDevClassProvider: &MockNetDevClassProvider{},
			},
			expErr: errors.New("TopologyProvider is required"),
		},
		"no NetDevClassProvider": {
			config: &FabricScannerConfig{
				TopologyProvider: &MockTopologyProvider{},
				FabricInterfaceProviders: []FabricInterfaceProvider{
					&MockFabricInterfaceProvider{},
				},
			},
			expErr: errors.New("NetDevClassProvider is required"),
		},
		"no FabricInterfaceProvider": {
			config: &FabricScannerConfig{
				TopologyProvider:    &MockTopologyProvider{},
				NetDevClassProvider: &MockNetDevClassProvider{},
			},
			expErr: errors.New("FabricInterfaceProvider is required"),
		},
		"multiple FabricInterfaceProviders": {
			config: &FabricScannerConfig{
				TopologyProvider: &MockTopologyProvider{},
				FabricInterfaceProviders: []FabricInterfaceProvider{
					&MockFabricInterfaceProvider{},
					&MockFabricInterfaceProvider{},
					&MockFabricInterfaceProvider{},
					&MockFabricInterfaceProvider{},
				},
				NetDevClassProvider: &MockNetDevClassProvider{},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.CmpErr(t, tc.expErr, tc.config.Validate())
		})
	}
}

func TestHardware_NewFabricScanner(t *testing.T) {
	for name, tc := range map[string]struct {
		config    *FabricScannerConfig
		expResult *FabricScanner
		expErr    error
	}{
		"nil config": {
			expErr: errors.New("nil"),
		},
		"success": {
			config: GetMockFabricScannerConfig(),
			expResult: &FabricScanner{
				config: GetMockFabricScannerConfig(),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			result, err := NewFabricScanner(log, tc.config)

			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResult, result,
				cmp.AllowUnexported(
					FabricScanner{},
					MockTopologyProvider{},
					MockFabricInterfaceProvider{},
					MockNetDevClassProvider{},
				),
				test.CmpOptIgnoreFieldAnyType("log"),
				cmpopts.IgnoreFields(FabricScanner{}, "mutex"),
				cmpopts.IgnoreFields(MockNetDevClassProvider{}, "mutex"),
			); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_FabricScanner_Scan(t *testing.T) {
	testTopo := &Topology{
		NUMANodes: NodeMap{
			0: MockNUMANode(0, 6).
				WithDevices([]*PCIDevice{
					mockPCIDevice("test", 1, 2, 3, 4).withType(DeviceTypeOFIDomain),
					mockPCIDevice("os_test", 1, 2, 3, 4).withType(DeviceTypeNetInterface),
					mockPCIDevice("os_test", 2, 2, 3, 4).withType(DeviceTypeNetInterface),
				}),
		},
	}

	testFis := NewFabricInterfaceSet(
		&FabricInterface{
			Name:          "test01",
			NetInterfaces: common.NewStringSet("os_test01"),
			Providers:     newTestFabricProviderSet("ofi+verbs"),
		},
		&FabricInterface{
			Name:          "os_test01",
			NetInterfaces: common.NewStringSet("os_test01"),
			Providers:     newTestFabricProviderSet("ofi+tcp"),
		},

		&FabricInterface{
			Name:          "os_test02",
			NetInterfaces: common.NewStringSet("os_test02"),
			Providers:     newTestFabricProviderSet("ofi+sockets"),
		},
	)

	for name, tc := range map[string]struct {
		config             *FabricScannerConfig
		providers          []string
		cacheTopology      *Topology
		nilScanner         bool
		builders           []FabricInterfaceSetBuilder
		expBuildersChanged bool
		expResult          *FabricInterfaceSet
		expErr             error
	}{
		"nil": {
			nilScanner: true,
			expErr:     errors.New("nil"),
		},
		"invalid config": {
			config: &FabricScannerConfig{},
			expErr: errors.New("invalid"),
		},
		"nothing found": {
			config: &FabricScannerConfig{
				TopologyProvider: &MockTopologyProvider{
					GetTopoReturn: testTopo,
				},
				FabricInterfaceProviders: []FabricInterfaceProvider{
					&MockFabricInterfaceProvider{
						GetFabricReturn: NewFabricInterfaceSet(),
					},
				},
				NetDevClassProvider: &MockNetDevClassProvider{
					GetNetDevClassReturn: []MockGetNetDevClassResult{
						{
							NDC: Infiniband,
						},
					},
				},
			},
			expErr: errors.New("no fabric interfaces found"),
		},
		"nothing found with provider": {
			config: &FabricScannerConfig{
				TopologyProvider: &MockTopologyProvider{
					GetTopoReturn: testTopo,
				},
				FabricInterfaceProviders: []FabricInterfaceProvider{
					&MockFabricInterfaceProvider{
						GetFabricReturn: NewFabricInterfaceSet(),
					},
				},
				NetDevClassProvider: &MockNetDevClassProvider{
					GetNetDevClassReturn: []MockGetNetDevClassResult{
						{
							NDC: Infiniband,
						},
					},
				},
			},
			providers: []string{"ofi+tcp"},
			expErr:    errors.New("no fabric interfaces found with providers: ofi+tcp"),
		},
		"already initialized": {
			config: GetMockFabricScannerConfig(),
			builders: []FabricInterfaceSetBuilder{
				&MockFabricInterfaceSetBuilder{},
				&MockFabricInterfaceSetBuilder{},
				&MockFabricInterfaceSetBuilder{},
			},
			expErr: errors.New("no fabric interfaces found"),
		},
		"topology fails": {
			config: &FabricScannerConfig{
				TopologyProvider: &MockTopologyProvider{
					GetTopoErr: errors.New("mock GetTopology"),
				},
				FabricInterfaceProviders: []FabricInterfaceProvider{
					&MockFabricInterfaceProvider{
						GetFabricReturn: testFis,
					},
				},
				NetDevClassProvider: &MockNetDevClassProvider{
					GetNetDevClassReturn: []MockGetNetDevClassResult{
						{
							NDC: Ether,
						},
					},
				},
			},
			expErr: errors.New("mock GetTopology"),
		},
		"fabric fails": {
			config: &FabricScannerConfig{
				TopologyProvider: &MockTopologyProvider{
					GetTopoReturn: testTopo,
				},
				FabricInterfaceProviders: []FabricInterfaceProvider{
					&MockFabricInterfaceProvider{
						GetFabricReturn: NewFabricInterfaceSet(),
					},
					&MockFabricInterfaceProvider{
						GetFabricErr: errors.New("mock GetFabricInterfaces"),
					},
				},
				NetDevClassProvider: &MockNetDevClassProvider{
					GetNetDevClassReturn: []MockGetNetDevClassResult{
						{
							NDC: Ether,
						},
					},
				},
			},
			expErr: errors.New("mock GetFabricInterfaces"),
		},
		"netdevclass fails": {
			config: &FabricScannerConfig{
				TopologyProvider: &MockTopologyProvider{
					GetTopoReturn: testTopo,
				},
				FabricInterfaceProviders: []FabricInterfaceProvider{
					&MockFabricInterfaceProvider{
						GetFabricReturn: testFis,
					},
				},
				NetDevClassProvider: &MockNetDevClassProvider{
					GetNetDevClassReturn: []MockGetNetDevClassResult{
						{
							Err:      errors.New("mock GetNetDevClass"),
							ExpInput: "os_test01",
						},
					},
				},
			},
			// we ignore the error in this case
			expResult: testFis,
		},
		"re-init with different provider": {
			config: &FabricScannerConfig{
				TopologyProvider: &MockTopologyProvider{
					GetTopoReturn: testTopo,
				},
				FabricInterfaceProviders: []FabricInterfaceProvider{
					&MockFabricInterfaceProvider{
						GetFabricReturn: NewFabricInterfaceSet(
							&FabricInterface{
								Name:          "os_test01",
								NetInterfaces: common.NewStringSet("os_test01"),
								Providers:     newTestFabricProviderSet("ofi+tcp"),
							},
						),
					},
				},
				NetDevClassProvider: &MockNetDevClassProvider{
					GetNetDevClassReturn: []MockGetNetDevClassResult{
						{
							NDC:      Infiniband,
							ExpInput: "os_test01",
						},
					},
				},
			},
			providers: []string{"ofi+tcp"},
			builders: []FabricInterfaceSetBuilder{
				&MockFabricInterfaceSetBuilder{},
				&MockFabricInterfaceSetBuilder{},
				&MockFabricInterfaceSetBuilder{},
			},
			expBuildersChanged: true,
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "os_test01",
					NetInterfaces: common.NewStringSet("os_test01"),
					DeviceClass:   Infiniband,
					Providers:     newTestFabricProviderSet("ofi+tcp"),
				},
			),
		},
		"success for all providers": {
			config: &FabricScannerConfig{
				TopologyProvider: &MockTopologyProvider{
					GetTopoReturn: testTopo,
				},
				FabricInterfaceProviders: []FabricInterfaceProvider{
					&MockFabricInterfaceProvider{
						GetFabricReturn: testFis,
					},
				},
				NetDevClassProvider: &MockNetDevClassProvider{
					GetNetDevClassReturn: []MockGetNetDevClassResult{
						{
							NDC:      Infiniband,
							ExpInput: "os_test01",
						},
						{
							NDC:      Ether,
							ExpInput: "os_test02",
						},
						{
							NDC:      Infiniband,
							ExpInput: "os_test01",
						},
					},
				},
			},
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "test01",
					NetInterfaces: common.NewStringSet("os_test01"),
					DeviceClass:   Infiniband,
					Providers:     newTestFabricProviderSet("ofi+verbs"),
				},
				&FabricInterface{
					Name:          "os_test01",
					NetInterfaces: common.NewStringSet("os_test01"),
					DeviceClass:   Infiniband,
					Providers:     newTestFabricProviderSet("ofi+tcp"),
				},
				&FabricInterface{
					Name:          "os_test02",
					NetInterfaces: common.NewStringSet("os_test02"),
					DeviceClass:   Ether,
					Providers:     newTestFabricProviderSet("ofi+sockets"),
				},
			),
		},
		"request multiple providers": {
			config: &FabricScannerConfig{
				TopologyProvider: &MockTopologyProvider{
					GetTopoReturn: testTopo,
				},
				FabricInterfaceProviders: []FabricInterfaceProvider{
					&multiCallFabricInterfaceProvider{prefix: "os_test"},
				},
				NetDevClassProvider: &MockNetDevClassProvider{
					GetNetDevClassReturn: []MockGetNetDevClassResult{
						{
							NDC:      Ether,
							ExpInput: "os_test01",
						},
						{
							NDC:      Ether,
							ExpInput: "os_test02",
						},
					},
				},
			},
			providers: []string{"ofi+verbs", "ofi+tcp"},
			expResult: expectedMultiCallFIProviderResult("os_test", Ether, "ofi+verbs", "ofi+tcp"),
		},
		"topology cached": {
			config: &FabricScannerConfig{
				TopologyProvider: &MockTopologyProvider{
					GetTopoErr: errors.New("mock GetTopology"),
				},
				FabricInterfaceProviders: []FabricInterfaceProvider{
					&MockFabricInterfaceProvider{
						GetFabricReturn: testFis,
					},
				},
				NetDevClassProvider: &MockNetDevClassProvider{
					GetNetDevClassReturn: []MockGetNetDevClassResult{
						{
							NDC:      Infiniband,
							ExpInput: "os_test01",
						},
						{
							NDC:      Ether,
							ExpInput: "os_test02",
						},
						{
							NDC:      Infiniband,
							ExpInput: "os_test01",
						},
					},
				},
			},
			cacheTopology: testTopo,
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "test01",
					NetInterfaces: common.NewStringSet("os_test01"),
					DeviceClass:   Infiniband,
					Providers:     newTestFabricProviderSet("ofi+verbs"),
				},
				&FabricInterface{
					Name:          "os_test01",
					NetInterfaces: common.NewStringSet("os_test01"),
					DeviceClass:   Infiniband,
					Providers:     newTestFabricProviderSet("ofi+tcp"),
				},
				&FabricInterface{
					Name:          "os_test02",
					NetInterfaces: common.NewStringSet("os_test02"),
					DeviceClass:   Ether,
					Providers:     newTestFabricProviderSet("ofi+sockets"),
				},
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var scanner *FabricScanner
			if !tc.nilScanner {
				var err error
				scanner, err = NewFabricScanner(log, tc.config)
				if err != nil {
					t.Fatal(err)
				}

				scanner.topo = tc.cacheTopology

				if len(tc.builders) > 0 {
					scanner.builders = tc.builders
				}
			}

			result, err := scanner.Scan(test.Context(t), tc.providers...)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result, fabricCmpOpts()...); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}

			if !tc.expBuildersChanged {
				for _, b := range tc.builders {
					mock, ok := b.(*MockFabricInterfaceSetBuilder)
					if !ok {
						t.Fatalf("bad test setup: test builders aren't mocks")
					}
					test.AssertEqual(t, 1, mock.BuildPartCalled, "")
				}
			}
		})
	}
}

func TestHardware_FabricScanner_CacheTopology(t *testing.T) {
	for name, tc := range map[string]struct {
		fs     *FabricScanner
		topo   *Topology
		expErr error
	}{
		"nil": {
			topo:   &Topology{},
			expErr: errors.New("nil"),
		},
		"nil input": {
			fs:     &FabricScanner{},
			expErr: errors.New("nil"),
		},
		"success": {
			fs:   &FabricScanner{},
			topo: &Topology{},
		},
		"overwrite": {
			fs: &FabricScanner{
				topo: &Topology{
					NUMANodes: NodeMap{
						0: MockNUMANode(0, 8),
						1: MockNUMANode(1, 8),
					},
				},
			},
			topo: &Topology{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.fs != nil {
				tc.fs.log = log
			}

			err := tc.fs.CacheTopology(tc.topo)

			test.CmpErr(t, tc.expErr, err)

			if tc.fs == nil {
				return
			}

			// Verify the new topology was saved
			if diff := cmp.Diff(tc.topo, tc.fs.topo); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_defaultFabricInterfaceSetBuilders(t *testing.T) {
	config := &FabricInterfaceSetBuilderConfig{
		Providers: []string{"testprovider"},
		Topology:  &Topology{},
		FabricInterfaceProviders: []FabricInterfaceProvider{
			&MockFabricInterfaceProvider{},
			&MockFabricInterfaceProvider{},
		},
		NetDevClassProvider: &MockNetDevClassProvider{},
	}
	expResult := []FabricInterfaceSetBuilder{
		newFabricInterfaceBuilder(nil,
			[]string{"testprovider"},
			&MockFabricInterfaceProvider{},
			&MockFabricInterfaceProvider{}),
		newNetworkDeviceBuilder(nil, &Topology{}),
		newNetDevClassBuilder(nil, &MockNetDevClassProvider{}),
		newNUMAAffinityBuilder(nil, &Topology{}),
	}

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	result := defaultFabricInterfaceSetBuilders(log, config)

	if diff := cmp.Diff(expResult, result,
		cmp.AllowUnexported(FabricInterfaceBuilder{}),
		cmp.AllowUnexported(NUMAAffinityBuilder{}),
		cmp.AllowUnexported(NetDevClassBuilder{}),
		cmp.AllowUnexported(NetworkDeviceBuilder{}),
		cmp.AllowUnexported(MockFabricInterfaceProvider{}),
		cmp.AllowUnexported(MockNetDevClassProvider{}),
		test.CmpOptIgnoreFieldAnyType("log"),
		cmpopts.IgnoreFields(MockNetDevClassProvider{}, "mutex"),
	); diff != "" {
		t.Fatalf("(-want, +got)\n%s\n", diff)
	}
}

func TestHardware_FabricInterfaceBuilder_BuildPart(t *testing.T) {
	for name, tc := range map[string]struct {
		builder   *FabricInterfaceBuilder
		set       *FabricInterfaceSet
		expResult *FabricInterfaceSet
		expErr    error
	}{
		"nil builder": {
			set:       NewFabricInterfaceSet(),
			expErr:    errors.New("FabricInterfaceBuilder is nil"),
			expResult: NewFabricInterfaceSet(),
		},
		"nil set": {
			builder: newFabricInterfaceBuilder(nil, []string{}),
			expErr:  errors.New("FabricInterfaceSet is nil"),
		},
		"uninit": {
			builder:   &FabricInterfaceBuilder{},
			set:       NewFabricInterfaceSet(),
			expErr:    errors.New("uninitialized"),
			expResult: NewFabricInterfaceSet(),
		},
		"success": {
			builder: newFabricInterfaceBuilder(nil, []string{},
				&MockFabricInterfaceProvider{
					GetFabricReturn: NewFabricInterfaceSet(
						&FabricInterface{
							Name: "test0",
						},
						&FabricInterface{
							Name: "test1",
						},
					),
				},
			),
			set: NewFabricInterfaceSet(),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name: "test0",
				},
				&FabricInterface{
					Name: "test1",
				},
			),
		},
		"merge success": {
			builder: newFabricInterfaceBuilder(nil, []string{},
				&MockFabricInterfaceProvider{
					GetFabricReturn: NewFabricInterfaceSet(
						&FabricInterface{
							Name:      "test0",
							Providers: newTestFabricProviderSet("p2", "p3"),
						},
						&FabricInterface{
							Name:      "test1",
							Providers: newTestFabricProviderSet("p2"),
						},
					),
				},
				&MockFabricInterfaceProvider{
					GetFabricReturn: NewFabricInterfaceSet(
						&FabricInterface{
							Name:      "test2",
							Providers: newTestFabricProviderSet("p1"),
						},
						&FabricInterface{
							Name:      "test3",
							Providers: newTestFabricProviderSet("p1"),
						},
						&FabricInterface{
							Name:      "test0",
							Providers: newTestFabricProviderSet("p1"),
						},
					),
				},
			),
			set: NewFabricInterfaceSet(),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:      "test0",
					Providers: newTestFabricProviderSet("p1", "p2", "p3"),
				},
				&FabricInterface{
					Name:      "test1",
					Providers: newTestFabricProviderSet("p2"),
				},
				&FabricInterface{
					Name:      "test2",
					Providers: newTestFabricProviderSet("p1"),
				},
				&FabricInterface{
					Name:      "test3",
					Providers: newTestFabricProviderSet("p1"),
				},
			),
		},
		"get FI fails": {
			builder: newFabricInterfaceBuilder(nil, []string{},
				&MockFabricInterfaceProvider{
					GetFabricReturn: NewFabricInterfaceSet(
						&FabricInterface{
							Name: "test0",
						},
						&FabricInterface{
							Name: "test1",
						},
					),
				},
				&MockFabricInterfaceProvider{
					GetFabricErr: errors.New("mock GetFabricInterfaces"),
				},
			),
			set:       NewFabricInterfaceSet(),
			expErr:    errors.New("mock GetFabricInterfaces"),
			expResult: NewFabricInterfaceSet(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.builder != nil {
				tc.builder.log = log
			}

			err := tc.builder.BuildPart(test.Context(t), tc.set)

			test.CmpErr(t, tc.expErr, err)

			cmpOpts := append(fabricCmpOpts(), ignoreFabricProviderPriorityCmpOpts()...)
			if diff := cmp.Diff(tc.expResult, tc.set, cmpOpts...); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_NetDeviceBuilder_BuildPart(t *testing.T) {
	testTopo := &Topology{
		NUMANodes: map[uint]*NUMANode{
			0: MockNUMANode(0, 8).WithDevices([]*PCIDevice{
				{
					Name:    "net0",
					Type:    DeviceTypeNetInterface,
					PCIAddr: *MustNewPCIAddress("0000:00:00.1"),
				},
				{
					Name:    "ofi0",
					Type:    DeviceTypeOFIDomain,
					PCIAddr: *MustNewPCIAddress("0000:00:00.1"),
				},
				{
					Name:    "net1",
					Type:    DeviceTypeNetInterface,
					PCIAddr: *MustNewPCIAddress("0000:00:00.2"),
				},
			}),
			1: MockNUMANode(1, 8).WithDevices([]*PCIDevice{
				{
					Name:    "net2",
					Type:    DeviceTypeNetInterface,
					PCIAddr: *MustNewPCIAddress("0000:00:01.1"),
				},
				{
					Name:    "ofi2",
					Type:    DeviceTypeOFIDomain,
					PCIAddr: *MustNewPCIAddress("0000:00:01.1"),
				},
			}),
		},
	}

	testTopo.VirtualDevices = []*VirtualDevice{
		{
			Name: "virt0",
			Type: DeviceTypeNetInterface,
		},
		{
			Name:          "net2.100",
			Type:          DeviceTypeNetInterface,
			BackingDevice: testTopo.AllDevices()["net2"].(*PCIDevice),
		},
		{
			Name: "lo",
			Type: DeviceTypeNetInterface,
		},
	}

	for name, tc := range map[string]struct {
		builder   *NetworkDeviceBuilder
		set       *FabricInterfaceSet
		expResult *FabricInterfaceSet
		expErr    error
	}{
		"nil builder": {
			set:       NewFabricInterfaceSet(),
			expErr:    errors.New("NetworkDeviceBuilder is nil"),
			expResult: NewFabricInterfaceSet(),
		},
		"nil set": {
			builder: newNetworkDeviceBuilder(nil, testTopo),
			expErr:  errors.New("FabricInterfaceSet is nil"),
		},
		"uninit": {
			builder:   &NetworkDeviceBuilder{},
			set:       NewFabricInterfaceSet(),
			expErr:    errors.New("uninitialized"),
			expResult: NewFabricInterfaceSet(),
		},
		"empty set": {
			builder:   newNetworkDeviceBuilder(nil, testTopo),
			set:       NewFabricInterfaceSet(),
			expResult: NewFabricInterfaceSet(),
		},
		"not in topo": {
			builder: newNetworkDeviceBuilder(nil, testTopo),
			set: NewFabricInterfaceSet(
				&FabricInterface{
					Name: "notfound",
				},
			),
			expResult: NewFabricInterfaceSet(),
		},
		"OSName not in topo": {
			builder: newNetworkDeviceBuilder(nil, testTopo),
			set: NewFabricInterfaceSet(
				&FabricInterface{
					Name:   "dontcare",
					OSName: "notfound",
				},
			),
			expResult: NewFabricInterfaceSet(),
		},
		"not OFI domain": {
			builder: newNetworkDeviceBuilder(nil, testTopo),
			set: NewFabricInterfaceSet(
				&FabricInterface{
					Name: "net1",
				},
			),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "net1",
					NetInterfaces: common.NewStringSet("net1"),
				},
			),
		},
		"virtual net device": {
			builder: newNetworkDeviceBuilder(nil, testTopo),
			set: NewFabricInterfaceSet(
				&FabricInterface{
					Name: "virt0",
				},
			),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "virt0",
					NetInterfaces: common.NewStringSet("virt0"),
				},
			),
		},
		"OFI domain": {
			builder: newNetworkDeviceBuilder(nil, testTopo),
			set: NewFabricInterfaceSet(
				&FabricInterface{
					Name: "ofi0",
				},
			),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "ofi0",
					NetInterfaces: common.NewStringSet("net0"),
				},
			),
		},
		"OFI domain with virtual net device": {
			builder: newNetworkDeviceBuilder(nil, testTopo),
			set: NewFabricInterfaceSet(
				&FabricInterface{
					Name: "ofi2",
				},
			),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "ofi2",
					NetInterfaces: common.NewStringSet("net2", "net2.100"),
				},
			),
		},
		"OFI domain with OS name": {
			builder: newNetworkDeviceBuilder(nil, testTopo),
			set: NewFabricInterfaceSet(
				&FabricInterface{
					Name:   "ofi0:1",
					OSName: "ofi0",
				},
			),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "ofi0:1",
					OSName:        "ofi0",
					NetInterfaces: common.NewStringSet("net0"),
				},
			),
		},
		"loopback class": {
			builder: newNetworkDeviceBuilder(nil, testTopo),
			set: NewFabricInterfaceSet(
				&FabricInterface{
					Name:        "lo",
					DeviceClass: Loopback,
				},
			),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "lo",
					NetInterfaces: common.NewStringSet("lo"),
					DeviceClass:   Loopback,
				},
			),
		},
		"loopback name only": {
			builder: newNetworkDeviceBuilder(nil, testTopo),
			set: NewFabricInterfaceSet(
				&FabricInterface{
					Name: "lo",
				},
			),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "lo",
					NetInterfaces: common.NewStringSet("lo"),
				},
			),
		},
		"multiple FIs": {
			builder: newNetworkDeviceBuilder(nil, testTopo),
			set: NewFabricInterfaceSet(
				&FabricInterface{
					Name: "ofi0",
				},
				&FabricInterface{
					Name: "net0",
				},
				&FabricInterface{
					Name: "net1",
				},
				&FabricInterface{
					Name: "ofi2",
				},
				&FabricInterface{
					Name: "net2",
				},
			),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "ofi0",
					NetInterfaces: common.NewStringSet("net0"),
				},
				&FabricInterface{
					Name:          "net0",
					NetInterfaces: common.NewStringSet("net0"),
				},
				&FabricInterface{
					Name:          "net1",
					NetInterfaces: common.NewStringSet("net1"),
				},
				&FabricInterface{
					Name:          "ofi2",
					NetInterfaces: common.NewStringSet("net2", "net2.100"),
				},
				&FabricInterface{
					Name:          "net2",
					NetInterfaces: common.NewStringSet("net2"),
				},
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.builder != nil {
				tc.builder.log = log
			}

			err := tc.builder.BuildPart(test.Context(t), tc.set)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, tc.set, fabricCmpOpts()...); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_NUMAAffinityBuilder_BuildPart(t *testing.T) {
	testTopo := &Topology{
		NUMANodes: map[uint]*NUMANode{
			1: MockNUMANode(1, 8).WithDevices([]*PCIDevice{
				{
					Name:    "net0",
					Type:    DeviceTypeNetInterface,
					PCIAddr: *MustNewPCIAddress("0000:00:00.1"),
				},
				{
					Name:    "net1",
					Type:    DeviceTypeNetInterface,
					PCIAddr: *MustNewPCIAddress("0000:00:00.2"),
				},
			}),
			2: MockNUMANode(2, 8).WithDevices([]*PCIDevice{
				{
					Name:    "net2",
					Type:    DeviceTypeNetInterface,
					PCIAddr: *MustNewPCIAddress("0000:00:01.1"),
				},
				{
					Name:    "ofi2",
					Type:    DeviceTypeOFIDomain,
					PCIAddr: *MustNewPCIAddress("0000:00:01.1"),
				},
			}),
		},
	}

	for name, tc := range map[string]struct {
		builder   *NUMAAffinityBuilder
		set       *FabricInterfaceSet
		expResult *FabricInterfaceSet
		expErr    error
	}{
		"nil builder": {
			set:       NewFabricInterfaceSet(),
			expErr:    errors.New("NUMAAffinityBuilder is nil"),
			expResult: NewFabricInterfaceSet(),
		},
		"uninit": {
			builder:   &NUMAAffinityBuilder{},
			set:       NewFabricInterfaceSet(),
			expErr:    errors.New("uninitialized"),
			expResult: NewFabricInterfaceSet(),
		},
		"nil set": {
			builder: newNUMAAffinityBuilder(nil, &Topology{}),
			expErr:  errors.New("FabricInterfaceSet is nil"),
		},
		"empty set": {
			builder:   newNUMAAffinityBuilder(nil, testTopo),
			set:       NewFabricInterfaceSet(),
			expResult: NewFabricInterfaceSet(),
		},
		"not in topo": {
			builder: newNUMAAffinityBuilder(nil, testTopo),
			set: NewFabricInterfaceSet(
				&FabricInterface{
					Name: "notfound",
				},
				&FabricInterface{
					Name: "net0",
				},
			),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name: "notfound",
				},
				&FabricInterface{
					Name:     "net0",
					NUMANode: 1,
				},
			),
		},
		"loopback": {
			builder: newNUMAAffinityBuilder(nil, testTopo),
			set: NewFabricInterfaceSet(
				&FabricInterface{
					Name:        "lo",
					DeviceClass: Loopback,
				},
				&FabricInterface{
					Name: "net0",
				},
			),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:        "lo",
					DeviceClass: Loopback,
				},
				&FabricInterface{
					Name:     "net0",
					NUMANode: 1,
				},
			),
		},
		"success": {
			builder: newNUMAAffinityBuilder(nil, testTopo),
			set: NewFabricInterfaceSet(
				&FabricInterface{
					Name: "net0",
				},
				&FabricInterface{
					Name: "net1",
				},
				&FabricInterface{
					Name: "ofi2",
				},
				&FabricInterface{
					Name: "net2",
				},
			),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:     "net0",
					NUMANode: 1,
				},
				&FabricInterface{
					Name:     "net1",
					NUMANode: 1,
				},
				&FabricInterface{
					Name:     "ofi2",
					NUMANode: 2,
				},
				&FabricInterface{
					Name:     "net2",
					NUMANode: 2,
				},
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.builder != nil {
				tc.builder.log = log
			}

			err := tc.builder.BuildPart(test.Context(t), tc.set)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, tc.set, fabricCmpOpts()...); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_NetDevClassBuilder_BuildPart(t *testing.T) {
	for name, tc := range map[string]struct {
		builder   *NetDevClassBuilder
		set       *FabricInterfaceSet
		expResult *FabricInterfaceSet
		expErr    error
	}{
		"nil builder": {
			set:       NewFabricInterfaceSet(),
			expErr:    errors.New("NetDevClassBuilder is nil"),
			expResult: NewFabricInterfaceSet(),
		},
		"nil set": {
			builder: newNetDevClassBuilder(nil, &MockNetDevClassProvider{}),
			expErr:  errors.New("FabricInterfaceSet is nil"),
		},
		"uninit": {
			builder:   &NetDevClassBuilder{},
			set:       NewFabricInterfaceSet(),
			expErr:    errors.New("uninitialized"),
			expResult: NewFabricInterfaceSet(),
		},
		"empty set": {
			builder:   newNetDevClassBuilder(nil, &MockNetDevClassProvider{}),
			set:       NewFabricInterfaceSet(),
			expResult: NewFabricInterfaceSet(),
		},
		"success": {
			builder: newNetDevClassBuilder(nil,
				&MockNetDevClassProvider{
					GetNetDevClassReturn: []MockGetNetDevClassResult{
						{
							ExpInput: "net1",
							NDC:      Ether,
						},
						{
							ExpInput: "net2",
							NDC:      Infiniband,
						},
						{
							ExpInput: "net2",
							NDC:      Infiniband,
						},
					},
				},
			),
			set: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "net1",
					NetInterfaces: common.NewStringSet("net1"),
				},
				&FabricInterface{
					Name:          "ofi2",
					NetInterfaces: common.NewStringSet("net2"),
				},
				&FabricInterface{
					Name:          "net2",
					NetInterfaces: common.NewStringSet("net2"),
				},
			),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "net1",
					NetInterfaces: common.NewStringSet("net1"),
					DeviceClass:   Ether,
				},
				&FabricInterface{
					Name:          "ofi2",
					NetInterfaces: common.NewStringSet("net2"),
					DeviceClass:   Infiniband,
				},
				&FabricInterface{
					Name:          "net2",
					NetInterfaces: common.NewStringSet("net2"),
					DeviceClass:   Infiniband,
				},
			),
		},
		"error": {
			builder: newNetDevClassBuilder(nil,
				&MockNetDevClassProvider{
					GetNetDevClassReturn: []MockGetNetDevClassResult{
						{
							ExpInput: "net1",
							Err:      errors.New("mock GetNetDevClass"),
						},
						{
							ExpInput: "net2",
							NDC:      Infiniband,
						},
						{
							ExpInput: "net2",
							NDC:      Infiniband,
						},
					},
				},
			),
			set: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "net1",
					NetInterfaces: common.NewStringSet("net1"),
				},
				&FabricInterface{
					Name:          "ofi2",
					NetInterfaces: common.NewStringSet("net2"),
				},
				&FabricInterface{
					Name:          "net2",
					NetInterfaces: common.NewStringSet("net2"),
				},
			),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:          "net1",
					NetInterfaces: common.NewStringSet("net1"),
				},
				&FabricInterface{
					Name:          "ofi2",
					NetInterfaces: common.NewStringSet("net2"),
					DeviceClass:   Infiniband,
				},
				&FabricInterface{
					Name:          "net2",
					NetInterfaces: common.NewStringSet("net2"),
					DeviceClass:   Infiniband,
				},
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.builder != nil {
				tc.builder.log = log
			}

			err := tc.builder.BuildPart(test.Context(t), tc.set)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, tc.set, fabricCmpOpts()...); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_WaitFabricReady(t *testing.T) {
	for name, tc := range map[string]struct {
		stateProv      *MockNetDevStateProvider
		ifaces         []string
		ignoreUnusable bool
		timeout        time.Duration
		expErr         error
	}{
		"nil checker": {
			ifaces: []string{"fi0"},
			expErr: errors.New("nil"),
		},
		"no interfaces": {
			stateProv: &MockNetDevStateProvider{},
			expErr:    errors.New("no fabric interfaces"),
		},
		"instant success": {
			stateProv: &MockNetDevStateProvider{},
			ifaces:    []string{"fi0"},
		},
		"instant failure": {
			stateProv: &MockNetDevStateProvider{
				GetStateReturn: []MockNetDevStateResult{
					{Err: errors.New("mock GetNetDevState")},
				},
			},
			ifaces: []string{"fi0"},
			expErr: errors.New("mock GetNetDevState"),
		},
		"success after tries": {
			stateProv: &MockNetDevStateProvider{
				GetStateReturn: []MockNetDevStateResult{
					{State: NetDevStateNotReady},
					{State: NetDevStateNotReady},
					{State: NetDevStateReady},
				},
			},
			ifaces: []string{"fi0"},
		},
		"failure after tries": {
			stateProv: &MockNetDevStateProvider{
				GetStateReturn: []MockNetDevStateResult{
					{State: NetDevStateNotReady},
					{State: NetDevStateNotReady},
					{Err: errors.New("mock GetNetDevState")},
				},
			},
			ifaces: []string{"fi0"},
			expErr: errors.New("mock GetNetDevState"),
		},
		"multi iface with failure": {
			stateProv: &MockNetDevStateProvider{
				GetStateReturn: []MockNetDevStateResult{
					{State: NetDevStateNotReady},
					{State: NetDevStateNotReady},
					{State: NetDevStateReady},
					{Err: errors.New("mock GetNetDevState")},
				},
			},
			ifaces: []string{"fi0", "fi1"},
			expErr: errors.New("mock GetNetDevState"),
		},
		"multi iface success": {
			stateProv: &MockNetDevStateProvider{
				GetStateReturn: []MockNetDevStateResult{
					{State: NetDevStateNotReady},
					{State: NetDevStateNotReady},
					{State: NetDevStateReady},
					{State: NetDevStateNotReady},
					{State: NetDevStateReady},
				},
			},
			ifaces: []string{"fi0", "fi1"},
		},
		"duplicates": {
			stateProv: &MockNetDevStateProvider{},
			ifaces:    []string{"fi0", "fi0"},
		},
		"timeout": {
			stateProv: &MockNetDevStateProvider{
				GetStateReturn: []MockNetDevStateResult{
					{State: NetDevStateNotReady},
				},
			},
			timeout: time.Millisecond,
			ifaces:  []string{"fi0"},
			expErr:  errors.New("context deadline"),
		},
		"requested interface unusable": {
			stateProv: &MockNetDevStateProvider{
				GetStateReturn: []MockNetDevStateResult{
					{State: NetDevStateNotReady},
					{State: NetDevStateDown},
				},
			},
			ifaces: []string{"fi0", "fi1"},
			expErr: errors.New("unusable"),
		},
		"ignore unusable": {
			stateProv: &MockNetDevStateProvider{
				GetStateReturn: []MockNetDevStateResult{
					{State: NetDevStateNotReady},
					{State: NetDevStateDown},
					{State: NetDevStateUnknown},
					{State: NetDevStateReady},
					{State: NetDevStateDown},
					{State: NetDevStateUnknown},
				},
			},
			ignoreUnusable: true,
			ifaces:         []string{"fi0", "fi1", "fi2"},
		},
		"all unusable": {
			stateProv: &MockNetDevStateProvider{
				GetStateReturn: []MockNetDevStateResult{
					{State: NetDevStateDown},
					{State: NetDevStateUnknown},
				},
			},
			ignoreUnusable: true,
			ifaces:         []string{"fi0", "fi1"},
			expErr:         errors.New("no usable fabric interfaces"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var ctx context.Context

			if tc.timeout == 0 {
				ctx = test.Context(t)
			} else {
				var cancel context.CancelFunc
				ctx, cancel = context.WithTimeout(test.Context(t), tc.timeout)
				defer cancel()
			}

			err := WaitFabricReady(ctx, log, WaitFabricReadyParams{
				StateProvider:  tc.stateProv,
				FabricIfaces:   tc.ifaces,
				IgnoreUnusable: tc.ignoreUnusable,
			})

			test.CmpErr(t, tc.expErr, err)
		})
	}
}
