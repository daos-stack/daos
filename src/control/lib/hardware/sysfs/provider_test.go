//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package sysfs

import (
	"context"
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestSysfs_NewProvider(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	p := NewProvider(log)

	if p == nil {
		t.Fatal("nil provider returned")
	}

	common.AssertEqual(t, "/sys", p.root, "")
}

func TestSysfs_Provider_GetNetDevClass(t *testing.T) {
	testDir, cleanupTestDir := common.CreateTestDir(t)
	defer cleanupTestDir()

	devs := map[string]uint32{
		"lo":   uint32(hardware.Loopback),
		"eth1": uint32(hardware.Ether),
	}

	for dev, class := range devs {
		path := filepath.Join(testDir, "class", "net", dev)
		os.MkdirAll(path, 0755)

		f, err := os.Create(filepath.Join(path, "type"))
		if err != nil {
			t.Fatal(err.Error())
		}

		_, err = f.WriteString(fmt.Sprintf("%d\n", class))
		f.Close()
		if err != nil {
			t.Fatal(err.Error())
		}
	}

	for name, tc := range map[string]struct {
		in        string
		expResult hardware.NetDevClass
		expErr    error
	}{
		"empty": {
			expErr: errors.New("device name required"),
		},
		"no such device": {
			in:     "fakedevice",
			expErr: errors.New("no such file"),
		},
		"loopback": {
			in:        "lo",
			expResult: hardware.NetDevClass(devs["lo"]),
		},
		"ether": {
			in:        "eth1",
			expResult: hardware.NetDevClass(devs["eth1"]),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			p := NewProvider(log)
			p.root = testDir

			result, err := p.GetNetDevClass(tc.in)

			common.CmpErr(t, tc.expErr, err)
			common.AssertEqual(t, tc.expResult, result, "")
		})
	}
}

func writeTestFile(t *testing.T, path, contents string) {
	t.Helper()

	if err := ioutil.WriteFile(path, []byte(contents), 0644); err != nil {
		t.Fatal(err)
	}
}

func getPCIPath(root, pciAddr string) string {
	return filepath.Join(root, "devices", "pci0000:00", "0000:00:01.0", pciAddr)
}

func setupPCIDev(t *testing.T, root, pciAddr, class, dev string) string {
	t.Helper()

	pciPath := getPCIPath(root, pciAddr)
	path := filepath.Join(pciPath, class, dev)
	if err := os.MkdirAll(path, 0755); err != nil {
		t.Fatal(err)
	}

	if err := os.Symlink(pciPath, filepath.Join(path, "device")); err != nil {
		t.Fatal(err)
	}

	return path
}

func setupClassLink(t *testing.T, root, class, devPath string) {
	classPath := filepath.Join(root, "class", class)
	if err := os.MkdirAll(classPath, 0755); err != nil {
		t.Fatal(err)
	}

	if devPath == "" {
		return
	}

	if err := os.Symlink(devPath, filepath.Join(classPath, filepath.Base(devPath))); err != nil {
		t.Fatal(err)
	}

	if err := os.Symlink(classPath, filepath.Join(devPath, "subsystem")); err != nil {
		t.Fatal(err)
	}
}

func setupNUMANode(t *testing.T, devPath, numaStr string) {
	writeTestFile(t, filepath.Join(devPath, "device", "numa_node"), numaStr)
}

func TestProvider_GetTopology(t *testing.T) {
	validPCIAddr := "0000:02:00.0"

	for name, tc := range map[string]struct {
		setup     func(*testing.T, string)
		p         *Provider
		expResult *hardware.Topology
		expErr    error
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"empty": {
			p:         &Provider{},
			expResult: &hardware.Topology{},
		},
		"no net devices": {
			setup: func(t *testing.T, root string) {
				for _, class := range []string{"net", "infiniband", "cxi"} {
					setupClassLink(t, root, class, "")
				}
			},
			p:         &Provider{},
			expResult: &hardware.Topology{},
		},
		"net device only": {
			setup: func(t *testing.T, root string) {
				path := setupPCIDev(t, root, validPCIAddr, "net", "net0")
				setupNUMANode(t, path, "2\n")
				setupClassLink(t, root, "net", path)
			},
			p: &Provider{},
			expResult: &hardware.Topology{
				NUMANodes: hardware.NodeMap{
					2: hardware.MockNUMANode(2, 0).
						WithDevices([]*hardware.PCIDevice{
							{
								Name:    "net0",
								Type:    hardware.DeviceTypeNetInterface,
								PCIAddr: *hardware.MustNewPCIAddress(validPCIAddr),
							},
						}),
				},
			},
		},
		"fabric devices": {
			setup: func(t *testing.T, root string) {
				for _, dev := range []struct {
					class string
					name  string
				}{
					{class: "net", name: "net0"},
					{class: "infiniband", name: "ib0"},
					{class: "cxi", name: "cxi0"},
				} {
					path := setupPCIDev(t, root, validPCIAddr, dev.class, dev.name)
					setupClassLink(t, root, dev.class, path)
					setupNUMANode(t, path, "2\n")
				}
			},
			p: &Provider{},
			expResult: &hardware.Topology{
				NUMANodes: hardware.NodeMap{
					2: hardware.MockNUMANode(2, 0).
						WithDevices([]*hardware.PCIDevice{
							{
								Name:    "cxi0",
								Type:    hardware.DeviceTypeOFIDomain,
								PCIAddr: *hardware.MustNewPCIAddress(validPCIAddr),
							},
							{
								Name:    "ib0",
								Type:    hardware.DeviceTypeOFIDomain,
								PCIAddr: *hardware.MustNewPCIAddress(validPCIAddr),
							},
							{
								Name:    "net0",
								Type:    hardware.DeviceTypeNetInterface,
								PCIAddr: *hardware.MustNewPCIAddress(validPCIAddr),
							},
						}),
				},
			},
		},
		"exclude non-specified classes": {
			setup: func(t *testing.T, root string) {
				for _, dev := range []struct {
					class string
					name  string
				}{
					{class: "net", name: "net0"},
					{class: "hwmon", name: "hwmon0"},
					{class: "cxi", name: "cxi0"},
					{class: "cxi_user", name: "cxi0"},
					{class: "ptp", name: "ptp0"},
					{class: "infiniband_verbs", name: "uverbs0"},
					{class: "infiniband", name: "mlx0"},
				} {
					path := setupPCIDev(t, root, validPCIAddr, dev.class, dev.name)
					setupClassLink(t, root, dev.class, path)
					setupNUMANode(t, path, "2\n")
				}
			},
			p: &Provider{},
			expResult: &hardware.Topology{
				NUMANodes: hardware.NodeMap{
					2: hardware.MockNUMANode(2, 0).
						WithDevices([]*hardware.PCIDevice{
							{
								Name:    "cxi0",
								Type:    hardware.DeviceTypeOFIDomain,
								PCIAddr: *hardware.MustNewPCIAddress(validPCIAddr),
							},
							{
								Name:    "mlx0",
								Type:    hardware.DeviceTypeOFIDomain,
								PCIAddr: *hardware.MustNewPCIAddress(validPCIAddr),
							},
							{
								Name:    "net0",
								Type:    hardware.DeviceTypeNetInterface,
								PCIAddr: *hardware.MustNewPCIAddress(validPCIAddr),
							},
						}),
				},
			},
		},
		"virtual device": {
			setup: func(t *testing.T, root string) {
				for _, dev := range []struct {
					class string
					name  string
				}{
					{class: "net", name: "net0"},
					{class: "cxi", name: "cxi0"},
					{class: "infiniband", name: "mlx0"},
				} {
					path := setupPCIDev(t, root, validPCIAddr, dev.class, dev.name)
					setupClassLink(t, root, dev.class, path)
					setupNUMANode(t, path, "2\n")
				}

				virtPath := filepath.Join(root, "devices", "virtual", "net", "virt_net0")
				if err := os.MkdirAll(virtPath, 0755); err != nil {
					t.Fatal(err)
				}

				if err := os.Symlink(getPCIPath(root, validPCIAddr), filepath.Join(virtPath, "device")); err != nil {
					t.Fatal(err)
				}

				setupClassLink(t, root, "net", virtPath)
			},
			p: &Provider{},
			expResult: &hardware.Topology{
				NUMANodes: hardware.NodeMap{
					2: hardware.MockNUMANode(2, 0).
						WithDevices([]*hardware.PCIDevice{
							{
								Name:    "cxi0",
								Type:    hardware.DeviceTypeOFIDomain,
								PCIAddr: *hardware.MustNewPCIAddress(validPCIAddr),
							},
							{
								Name:    "mlx0",
								Type:    hardware.DeviceTypeOFIDomain,
								PCIAddr: *hardware.MustNewPCIAddress(validPCIAddr),
							},
							{
								Name:    "net0",
								Type:    hardware.DeviceTypeNetInterface,
								PCIAddr: *hardware.MustNewPCIAddress(validPCIAddr),
							},
							{
								Name:    "virt_net0",
								Type:    hardware.DeviceTypeNetInterface,
								PCIAddr: *hardware.MustNewPCIAddress(validPCIAddr),
							},
						}),
				},
			},
		},
		"no NUMA node": {
			setup: func(t *testing.T, root string) {
				path := setupPCIDev(t, root, validPCIAddr, "net", "net0")
				setupNUMANode(t, path, "-1\n")
				setupClassLink(t, root, "net", path)
			},
			p: &Provider{},
			expResult: &hardware.Topology{
				NUMANodes: hardware.NodeMap{
					0: hardware.MockNUMANode(0, 0).
						WithDevices([]*hardware.PCIDevice{
							{
								Name:    "net0",
								Type:    hardware.DeviceTypeNetInterface,
								PCIAddr: *hardware.MustNewPCIAddress(validPCIAddr),
							},
						}),
				},
			},
		},
		"garbage NUMA file": {
			setup: func(t *testing.T, root string) {
				path := setupPCIDev(t, root, validPCIAddr, "net", "net0")
				setupNUMANode(t, path, "abcdef\n")
				setupClassLink(t, root, "net", path)
			},
			p: &Provider{},
			expResult: &hardware.Topology{
				NUMANodes: hardware.NodeMap{
					0: hardware.MockNUMANode(0, 0).
						WithDevices([]*hardware.PCIDevice{
							{
								Name:    "net0",
								Type:    hardware.DeviceTypeNetInterface,
								PCIAddr: *hardware.MustNewPCIAddress(validPCIAddr),
							},
						}),
				},
			},
		},
		"no NUMA file": {
			setup: func(t *testing.T, root string) {
				path := setupPCIDev(t, root, validPCIAddr, "net", "net0")
				setupClassLink(t, root, "net", path)
			},
			p: &Provider{},
			expResult: &hardware.Topology{
				NUMANodes: hardware.NodeMap{
					0: hardware.MockNUMANode(0, 0).
						WithDevices([]*hardware.PCIDevice{
							{
								Name:    "net0",
								Type:    hardware.DeviceTypeNetInterface,
								PCIAddr: *hardware.MustNewPCIAddress(validPCIAddr),
							},
						}),
				},
			},
		},
		"no PCI device link": {
			setup: func(t *testing.T, root string) {
				path := setupPCIDev(t, root, validPCIAddr, "net", "net0")
				setupNUMANode(t, path, "2\n")
				setupClassLink(t, root, "net", path)

				if err := os.Remove(filepath.Join(path, "device")); err != nil {
					t.Fatal(err)
				}
			},
			p:         &Provider{},
			expResult: &hardware.Topology{},
		},
		"no PCI addr": {
			setup: func(t *testing.T, root string) {
				class := "net"
				dev := "net0"
				pciPath := filepath.Join(root, "devices", "pci0000:00", "junk")
				path := filepath.Join(pciPath, class, dev)
				if err := os.MkdirAll(path, 0755); err != nil {
					t.Fatal(err)
				}

				if err := os.Symlink(pciPath, filepath.Join(path, "device")); err != nil {
					t.Fatal(err)
				}
				setupNUMANode(t, path, "2\n")
				setupClassLink(t, root, "net", path)
			},
			p:         &Provider{},
			expResult: &hardware.Topology{},
		},
		"device is virtio below PCI": {
			setup: func(t *testing.T, root string) {
				class := "net"
				dev := "net0"
				pciPath := getPCIPath(root, validPCIAddr)
				virtioPath := filepath.Join(pciPath, "virtio0")
				path := filepath.Join(virtioPath, class, dev)
				if err := os.MkdirAll(path, 0755); err != nil {
					t.Fatal(err)
				}

				if err := os.Symlink(virtioPath, filepath.Join(path, "device")); err != nil {
					t.Fatal(err)
				}
				setupClassLink(t, root, "net", path)
			},
			p: &Provider{},
			expResult: &hardware.Topology{
				NUMANodes: hardware.NodeMap{
					0: hardware.MockNUMANode(0, 0).
						WithDevices([]*hardware.PCIDevice{
							{
								Name:    "net0",
								Type:    hardware.DeviceTypeNetInterface,
								PCIAddr: *hardware.MustNewPCIAddress(validPCIAddr),
							},
						}),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			testDir, cleanupTestDir := common.CreateTestDir(t)
			defer cleanupTestDir()

			if tc.setup == nil {
				tc.setup = func(t *testing.T, root string) {}
			}

			tc.setup(t, testDir)

			if tc.p != nil {
				tc.p.log = log

				// Mock out a fake sysfs in the testDir
				tc.p.root = testDir
			}

			result, err := tc.p.GetTopology(context.Background())

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Errorf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestSysfs_Provider_GetFabricInterfaces(t *testing.T) {
	setupDefault := func(t *testing.T, root string) {
		path0 := setupPCIDev(t, root, "0000:01:01.1", "cxi", "cxi0")
		setupClassLink(t, root, "cxi", path0)

		path1 := setupPCIDev(t, root, "0000:02:02.1", "cxi", "cxi1")
		setupClassLink(t, root, "cxi", path1)
	}

	for name, tc := range map[string]struct {
		p         *Provider
		setup     func(*testing.T, string)
		expErr    error
		expResult *hardware.FabricInterfaceSet
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"no devices": {
			p:         &Provider{},
			setup:     func(*testing.T, string) {},
			expResult: hardware.NewFabricInterfaceSet(),
		},
		"CXI devices": {
			p: &Provider{},
			expResult: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Name:      "cxi0",
					OSName:    "cxi0",
					Providers: common.NewStringSet("ofi+cxi"),
				},
				&hardware.FabricInterface{
					Name:      "cxi1",
					OSName:    "cxi1",
					Providers: common.NewStringSet("ofi+cxi"),
				},
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			testDir, cleanupTestDir := common.CreateTestDir(t)
			defer cleanupTestDir()

			if tc.setup == nil {
				tc.setup = setupDefault
			}
			tc.setup(t, testDir)

			if tc.p != nil {
				tc.p.log = log

				// Mock out a fake sysfs in the testDir
				tc.p.root = testDir
			}

			result, err := tc.p.GetFabricInterfaces(context.Background())

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResult, result,
				cmp.AllowUnexported(hardware.FabricInterfaceSet{}),
			); diff != "" {
				t.Errorf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}
