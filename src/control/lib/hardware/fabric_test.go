//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"context"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

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
				Providers: common.NewStringSet("p1", "p2"),
			},
			expResult: "test0 (providers: p1, p2)",
		},
		"with net interface": {
			fi: &FabricInterface{
				Name:         "test0",
				NetInterface: "os_test0",
				Providers:    common.NewStringSet("p1", "p2"),
			},
			expResult: "test0 (interface: os_test0) (providers: p1, p2)",
		},
		"different OS name": {
			fi: &FabricInterface{
				Name:         "test0:1",
				OSName:       "test0",
				NetInterface: "os_test0",
				Providers:    common.NewStringSet("p1", "p2"),
			},
			expResult: "test0:1 (OS name: test0) (interface: os_test0) (providers: p1, p2)",
		},
		"OS name": {
			fi: &FabricInterface{
				Name:         "test0",
				OSName:       "test0",
				NetInterface: "os_test0",
				Providers:    common.NewStringSet("p1", "p2"),
			},
			expResult: "test0 (interface: os_test0) (providers: p1, p2)",
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.expResult, tc.fi.String(), "")
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
				Providers: common.NewStringSet("lib+p1", "lib+p3"),
			},
			in: "lib+p2",
		},
		"single match": {
			fi: &FabricInterface{
				Providers: common.NewStringSet("lib+p1", "lib+p2", "lib+p3"),
			},
			in:        "lib+p1",
			expResult: true,
		},
		"no prefix": {
			fi: &FabricInterface{
				Providers: common.NewStringSet("p1", "p2", "p3"),
			},
			in:        "p3",
			expResult: true,
		},
		"no prefix not found": {
			fi: &FabricInterface{
				Providers: common.NewStringSet("p1", "p2", "lib+p3"),
			},
			in: "p3",
		},
		"multi match": {
			fi: &FabricInterface{
				Providers: common.NewStringSet("lib+p1", "lib+p2", "lib+p3"),
			},
			in:        "lib+p1,p2",
			expResult: true,
		},
		"partial match": {
			fi: &FabricInterface{
				Providers: common.NewStringSet("lib+p1", "lib+p3"),
			},
			in: "lib+p1,p2",
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.fi.SupportsProvider(tc.in)

			common.AssertEqual(t, tc.expResult, result, "")
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

			common.AssertEqual(t, tc.expResult, result, "")
			common.CmpErr(t, tc.expErr, err)
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
					Name:         "test0",
					NetInterface: "os_test0",
				},
				{
					Name:         "test1",
					NetInterface: "os_test1",
				},
				{
					Name:         "os_test1",
					NetInterface: "os_test1",
				},
			},
			expResult: &FabricInterfaceSet{
				byName: fabricInterfaceMap{
					"test0": {
						Name:         "test0",
						NetInterface: "os_test0",
					},
					"test1": {
						Name:         "test1",
						NetInterface: "os_test1",
					},
					"os_test1": {
						Name:         "os_test1",
						NetInterface: "os_test1",
					},
				},
				byNetDev: map[string]fabricInterfaceMap{
					"os_test0": {
						"test0": {
							Name:         "test0",
							NetInterface: "os_test0",
						},
					},
					"os_test1": {
						"test1": {
							Name:         "test1",
							NetInterface: "os_test1",
						},
						"os_test1": {
							Name:         "os_test1",
							NetInterface: "os_test1",
						},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := NewFabricInterfaceSet(tc.input...)

			if diff := cmp.Diff(tc.expResult, result,
				cmp.AllowUnexported(FabricInterfaceSet{}),
			); diff != "" {
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
					Name:         "test0",
					NetInterface: "os_test0",
				}),
			expResult: []string{"test0"},
		},
		"multiple": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "test0",
					NetInterface: "os_test0",
				},
				&FabricInterface{
					Name:         "test1",
					NetInterface: "os_test1",
				},
				&FabricInterface{
					Name:         "test2",
					NetInterface: "os_test2",
				}),
			expResult: []string{"test0", "test1", "test2"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.fis.Names()

			common.AssertEqual(t, len(tc.expResult), tc.fis.NumFabricInterfaces(), "")
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
					Name:         "test0",
					NetInterface: "os_test0",
				}),
			expResult: []string{"os_test0"},
		},
		"multiple": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "test0",
					NetInterface: "os_test0",
				},
				&FabricInterface{
					Name:         "test1",
					NetInterface: "os_test1",
				},
				&FabricInterface{
					Name:         "test2",
					NetInterface: "os_test2",
				}),
			expResult: []string{"os_test0", "os_test1", "os_test2"},
		},
		"same net device": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "test0",
					NetInterface: "os_test0",
				},
				&FabricInterface{
					Name:         "test1",
					NetInterface: "os_test0",
				},
				&FabricInterface{
					Name:         "test2",
					NetInterface: "os_test0",
				}),
			expResult: []string{"os_test0"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.fis.NetDevices()

			common.AssertEqual(t, len(tc.expResult), tc.fis.NumNetDevices(), "")
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
				Name:         "one",
				NetInterface: "dev1",
			},
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "one",
					NetInterface: "dev1",
				},
			),
		},
		"add": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "one",
					NetInterface: "dev1",
				},
				&FabricInterface{
					Name:         "two",
					NetInterface: "dev2",
				},
			),
			input: &FabricInterface{
				Name:         "three",
				NetInterface: "dev3",
			},
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "one",
					NetInterface: "dev1",
				},
				&FabricInterface{
					Name:         "two",
					NetInterface: "dev2",
				},
				&FabricInterface{
					Name:         "three",
					NetInterface: "dev3",
				},
			),
		},
		"update": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "one",
					NetInterface: "dev1",
				},
				&FabricInterface{
					Name:         "two",
					NetInterface: "dev2",
				},
			),
			input: &FabricInterface{
				Name:         "two",
				NetInterface: "dev2",
				DeviceClass:  Ether,
				NUMANode:     1,
				Providers:    common.NewStringSet("p1", "p2"),
			},
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "one",
					NetInterface: "dev1",
				},
				&FabricInterface{
					Name:         "two",
					NetInterface: "dev2",
					DeviceClass:  Ether,
					NUMANode:     1,
					Providers:    common.NewStringSet("p1", "p2"),
				},
			),
		},
		"can't override nonzero": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "one",
					NetInterface: "dev1",
					DeviceClass:  Ether,
					NUMANode:     2,
				},
			),
			input: &FabricInterface{
				Name: "one",
			},
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "one",
					NetInterface: "dev1",
					DeviceClass:  Ether,
					NUMANode:     2,
				},
			),
		},
		"add to providers": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "one",
					NetInterface: "dev1",
					DeviceClass:  Ether,
					NUMANode:     2,
					Providers:    common.NewStringSet("p0", "p1"),
				},
			),
			input: &FabricInterface{
				Name:         "one",
				NetInterface: "dev1",
				DeviceClass:  Ether,
				NUMANode:     2,
				Providers:    common.NewStringSet("p2", "p3"),
			},
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "one",
					NetInterface: "dev1",
					DeviceClass:  Ether,
					NUMANode:     2,
					Providers:    common.NewStringSet("p0", "p1", "p2", "p3"),
				},
			),
		},
		"add duplicate providers": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "one",
					NetInterface: "dev1",
					DeviceClass:  Ether,
					NUMANode:     2,
					Providers:    common.NewStringSet("p0", "p1"),
				},
			),
			input: &FabricInterface{
				Name:         "one",
				NetInterface: "dev1",
				DeviceClass:  Ether,
				NUMANode:     2,
				Providers:    common.NewStringSet("p1", "p2"),
			},
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "one",
					NetInterface: "dev1",
					DeviceClass:  Ether,
					NUMANode:     2,
					Providers:    common.NewStringSet("p0", "p1", "p2"),
				},
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.fis.Update(tc.input)

			if diff := cmp.Diff(tc.expResult, tc.fis,
				cmp.AllowUnexported(FabricInterfaceSet{}),
			); diff != "" {
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
					Name:         "fi0",
					NetInterface: "net0",
				},
			),
			input: "fi1",
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "fi0",
					NetInterface: "net0",
				},
			),
		},
		"removed": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "fi0",
					NetInterface: "net0",
				},
				&FabricInterface{
					Name:         "fi1",
					NetInterface: "net0",
				},
			),
			input: "fi1",
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "fi0",
					NetInterface: "net0",
				},
			),
		},
		"remove only item": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "fi0",
					NetInterface: "net0",
				},
			),
			input:     "fi0",
			expResult: NewFabricInterfaceSet(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.fis.Remove(tc.input)

			if diff := cmp.Diff(tc.expResult, tc.fis,
				cmp.AllowUnexported(FabricInterfaceSet{}),
			); diff != "" {
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
					Name:         "test0",
					NetInterface: "os_test0",
				},
				&FabricInterface{
					Name:         "test1",
					NetInterface: "os_test1",
				},
				&FabricInterface{
					Name:         "test2",
					NetInterface: "os_test2",
				},
			),
			name:   "test10",
			expErr: errors.New("not found"),
		},
		"success": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "test0",
					NetInterface: "os_test0",
				},
				&FabricInterface{
					Name:         "test1",
					NetInterface: "os_test1",
				},
				&FabricInterface{
					Name:         "test2",
					NetInterface: "os_test2",
				},
			),
			name: "test1",
			expResult: &FabricInterface{
				Name:         "test1",
				NetInterface: "os_test1",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := tc.fis.GetInterface(tc.name)

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
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
					Name:         "test0",
					NetInterface: "os_test0",
				},
				&FabricInterface{
					Name:         "test1",
					NetInterface: "os_test1",
				},
			),
			netDev:   "notfound",
			provider: "something",
			expErr:   errors.New("network device \"notfound\" not found"),
		},
		"provider not found on device": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "test0",
					NetInterface: "os_test0",
					Providers:    common.NewStringSet("p1", "p2"),
				},
				&FabricInterface{
					Name:         "test1",
					NetInterface: "os_test0",
					Providers:    common.NewStringSet("p3"),
				},
				&FabricInterface{
					Name:         "test2",
					NetInterface: "os_test2",
					Providers:    common.NewStringSet("p4"),
				},
			),
			netDev:   "os_test0",
			provider: "p4",
			expErr:   errors.New("provider \"p4\" not supported"),
		},
		"success": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "test0",
					NetInterface: "os_test0",
					Providers:    common.NewStringSet("p1", "p2"),
				},
				&FabricInterface{
					Name:         "test1",
					NetInterface: "os_test0",
					Providers:    common.NewStringSet("p3"),
				},
				&FabricInterface{
					Name:         "test2",
					NetInterface: "os_test2",
					Providers:    common.NewStringSet("p4"),
				},
			),
			netDev:   "os_test0",
			provider: "p3",
			expResult: &FabricInterface{
				Name:         "test1",
				NetInterface: "os_test0",
				Providers:    common.NewStringSet("p3"),
			},
		},
		"provider helper specified, success": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "test0",
					NetInterface: "os_test0",
					Providers:    common.NewStringSet("p1", "p2", "p3"),
				},
				&FabricInterface{
					Name:         "test1",
					NetInterface: "os_test0",
					Providers:    common.NewStringSet("p3;h1"),
				},
				&FabricInterface{
					Name:         "test2",
					NetInterface: "os_test2",
					Providers:    common.NewStringSet("p4"),
				},
			),
			netDev:   "os_test0",
			provider: "p3;h1",
			expResult: &FabricInterface{
				Name:         "test1",
				NetInterface: "os_test0",
				Providers:    common.NewStringSet("p3;h1"),
			},
		},
		"provider helper specified, not found": {
			fis: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "test0",
					NetInterface: "os_test0",
					Providers:    common.NewStringSet("p1", "p2"),
				},
				&FabricInterface{
					Name:         "test1",
					NetInterface: "os_test0",
					Providers:    common.NewStringSet("p3;h1", "p3"),
				},
				&FabricInterface{
					Name:         "test2",
					NetInterface: "os_test2",
					Providers:    common.NewStringSet("p4"),
				},
			),
			netDev:   "os_test0",
			provider: "p3;h2",
			expErr:   errors.New("not supported"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := tc.fis.GetInterfaceOnNetDevice(tc.netDev, tc.provider)

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
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
		"any": {
			ndc:       NetDevClass(0xFFFFFFFF),
			expResult: "ANY",
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.expResult, tc.ndc.String(), "")
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
			common.CmpErr(t, tc.expErr, tc.config.Validate())
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
			defer common.ShowBufferOnFailure(t, buf)

			result, err := NewFabricScanner(log, tc.config)

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResult, result,
				cmp.AllowUnexported(
					FabricScanner{},
					MockTopologyProvider{},
					MockFabricInterfaceProvider{},
					MockNetDevClassProvider{},
				),
				common.CmpOptIgnoreFieldAnyType("log"),
				cmpopts.IgnoreFields(FabricScanner{}, "mutex"),
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
				}),
		},
	}

	testFis := NewFabricInterfaceSet(
		&FabricInterface{
			Name:         "test01",
			NetInterface: "os_test01",
		},
	)

	for name, tc := range map[string]struct {
		config        *FabricScannerConfig
		cacheTopology *Topology
		nilScanner    bool
		builders      []FabricInterfaceSetBuilder
		expResult     *FabricInterfaceSet
		expErr        error
	}{
		"nil": {
			nilScanner: true,
			expErr:     errors.New("nil"),
		},
		"invalid config": {
			config: &FabricScannerConfig{},
			expErr: errors.New("invalid"),
		},
		"already initialized": {
			config: GetMockFabricScannerConfig(),
			builders: []FabricInterfaceSetBuilder{
				&mockFabricInterfaceSetBuilder{},
				&mockFabricInterfaceSetBuilder{},
				&mockFabricInterfaceSetBuilder{},
			},
			expResult: NewFabricInterfaceSet(),
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
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "test01",
					NetInterface: "os_test01",
				},
			),
		},
		"success": {
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
							NDC:      Ether,
							ExpInput: "os_test01",
						},
					},
				},
			},
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "test01",
					NetInterface: "os_test01",
					DeviceClass:  Ether,
				},
			),
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
							NDC:      Ether,
							ExpInput: "os_test01",
						},
					},
				},
			},
			cacheTopology: testTopo,
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "test01",
					NetInterface: "os_test01",
					DeviceClass:  Ether,
				},
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

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

			result, err := scanner.Scan(context.TODO())

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result, cmp.AllowUnexported(FabricInterfaceSet{})); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}

			for _, b := range tc.builders {
				mock, ok := b.(*mockFabricInterfaceSetBuilder)
				if !ok {
					t.Fatalf("bad test setup: test builders aren't mocks")
				}
				common.AssertEqual(t, 1, mock.buildPartCalled, "")
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
			defer common.ShowBufferOnFailure(t, buf)

			if tc.fs != nil {
				tc.fs.log = log
			}

			err := tc.fs.CacheTopology(tc.topo)

			common.CmpErr(t, tc.expErr, err)

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
		Topology: &Topology{},
		FabricInterfaceProviders: []FabricInterfaceProvider{
			&MockFabricInterfaceProvider{},
			&MockFabricInterfaceProvider{},
		},
		NetDevClassProvider: &MockNetDevClassProvider{},
	}
	expResult := []FabricInterfaceSetBuilder{
		newFabricInterfaceBuilder(nil,
			&MockFabricInterfaceProvider{},
			&MockFabricInterfaceProvider{}),
		newNetworkDeviceBuilder(nil, &Topology{}),
		newNetDevClassBuilder(nil, &MockNetDevClassProvider{}),
		newNUMAAffinityBuilder(nil, &Topology{}),
	}

	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	result := defaultFabricInterfaceSetBuilders(log, config)

	if diff := cmp.Diff(expResult, result,
		cmp.AllowUnexported(FabricInterfaceBuilder{}),
		cmp.AllowUnexported(NUMAAffinityBuilder{}),
		cmp.AllowUnexported(NetDevClassBuilder{}),
		cmp.AllowUnexported(NetworkDeviceBuilder{}),
		cmp.AllowUnexported(MockFabricInterfaceProvider{}),
		cmp.AllowUnexported(MockNetDevClassProvider{}),
		common.CmpOptIgnoreFieldAnyType("log"),
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
			builder: newFabricInterfaceBuilder(nil),
			expErr:  errors.New("FabricInterfaceSet is nil"),
		},
		"uninit": {
			builder:   &FabricInterfaceBuilder{},
			set:       NewFabricInterfaceSet(),
			expErr:    errors.New("uninitialized"),
			expResult: NewFabricInterfaceSet(),
		},
		"success": {
			builder: newFabricInterfaceBuilder(nil,
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
			builder: newFabricInterfaceBuilder(nil,
				&MockFabricInterfaceProvider{
					GetFabricReturn: NewFabricInterfaceSet(
						&FabricInterface{
							Name:      "test0",
							Providers: common.NewStringSet("p2", "p3"),
						},
						&FabricInterface{
							Name:      "test1",
							Providers: common.NewStringSet("p2"),
						},
					),
				},
				&MockFabricInterfaceProvider{
					GetFabricReturn: NewFabricInterfaceSet(
						&FabricInterface{
							Name:      "test2",
							Providers: common.NewStringSet("p1"),
						},
						&FabricInterface{
							Name:      "test3",
							Providers: common.NewStringSet("p1"),
						},
						&FabricInterface{
							Name:      "test0",
							Providers: common.NewStringSet("p1"),
						},
					),
				},
			),
			set: NewFabricInterfaceSet(),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:      "test0",
					Providers: common.NewStringSet("p1", "p2", "p3"),
				},
				&FabricInterface{
					Name:      "test1",
					Providers: common.NewStringSet("p2"),
				},
				&FabricInterface{
					Name:      "test2",
					Providers: common.NewStringSet("p1"),
				},
				&FabricInterface{
					Name:      "test3",
					Providers: common.NewStringSet("p1"),
				},
			),
		},
		"get FI fails": {
			builder: newFabricInterfaceBuilder(nil,
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
			defer common.ShowBufferOnFailure(t, buf)

			if tc.builder != nil {
				tc.builder.log = log
			}

			err := tc.builder.BuildPart(context.Background(), tc.set)

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, tc.set,
				cmp.AllowUnexported(FabricInterfaceSet{}),
			); diff != "" {
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
					Name:         "net1",
					NetInterface: "net1",
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
					Name:         "ofi0",
					NetInterface: "net0",
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
					Name:         "ofi0:1",
					OSName:       "ofi0",
					NetInterface: "net0",
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
					Name:         "lo",
					NetInterface: "lo",
					DeviceClass:  Loopback,
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
					Name:         "lo",
					NetInterface: "lo",
				},
			),
		},
		"multiple": {
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
					Name:         "ofi0",
					NetInterface: "net0",
				},
				&FabricInterface{
					Name:         "net0",
					NetInterface: "net0",
				},
				&FabricInterface{
					Name:         "net1",
					NetInterface: "net1",
				},
				&FabricInterface{
					Name:         "ofi2",
					NetInterface: "net2",
				},
				&FabricInterface{
					Name:         "net2",
					NetInterface: "net2",
				},
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.builder != nil {
				tc.builder.log = log
			}

			err := tc.builder.BuildPart(context.Background(), tc.set)

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, tc.set,
				cmp.AllowUnexported(FabricInterfaceSet{}),
			); diff != "" {
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
			defer common.ShowBufferOnFailure(t, buf)

			if tc.builder != nil {
				tc.builder.log = log
			}

			err := tc.builder.BuildPart(context.Background(), tc.set)

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, tc.set,
				cmp.AllowUnexported(FabricInterfaceSet{}),
			); diff != "" {
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
					Name:         "net1",
					NetInterface: "net1",
				},
				&FabricInterface{
					Name:         "ofi2",
					NetInterface: "net2",
				},
				&FabricInterface{
					Name:         "net2",
					NetInterface: "net2",
				},
			),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "net1",
					NetInterface: "net1",
					DeviceClass:  Ether,
				},
				&FabricInterface{
					Name:         "ofi2",
					NetInterface: "net2",
					DeviceClass:  Infiniband,
				},
				&FabricInterface{
					Name:         "net2",
					NetInterface: "net2",
					DeviceClass:  Infiniband,
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
					Name:         "net1",
					NetInterface: "net1",
				},
				&FabricInterface{
					Name:         "ofi2",
					NetInterface: "net2",
				},
				&FabricInterface{
					Name:         "net2",
					NetInterface: "net2",
				},
			),
			expResult: NewFabricInterfaceSet(
				&FabricInterface{
					Name:         "net1",
					NetInterface: "net1",
				},
				&FabricInterface{
					Name:         "ofi2",
					NetInterface: "net2",
					DeviceClass:  Infiniband,
				},
				&FabricInterface{
					Name:         "net2",
					NetInterface: "net2",
					DeviceClass:  Infiniband,
				},
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.builder != nil {
				tc.builder.log = log
			}

			err := tc.builder.BuildPart(context.Background(), tc.set)

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, tc.set,
				cmp.AllowUnexported(FabricInterfaceSet{}),
			); diff != "" {
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
			defer common.ShowBufferOnFailure(t, buf)

			var ctx context.Context

			if tc.timeout == 0 {
				ctx = context.Background()
			} else {
				var cancel context.CancelFunc
				ctx, cancel = context.WithTimeout(context.Background(), tc.timeout)
				defer cancel()
			}

			err := WaitFabricReady(ctx, log, WaitFabricReadyParams{
				StateProvider:  tc.stateProv,
				FabricIfaces:   tc.ifaces,
				IgnoreUnusable: tc.ignoreUnusable,
			})

			common.CmpErr(t, tc.expErr, err)
		})
	}
}
