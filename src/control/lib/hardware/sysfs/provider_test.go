//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package sysfs

import (
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"strconv"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestSysfs_NewProvider(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	p := NewProvider(log)

	if p == nil {
		t.Fatal("nil provider returned")
	}

	test.AssertEqual(t, "/sys", p.root, "")
}

func TestSysfs_isNetvscDevice(t *testing.T) {
	mkUeventFile := func(testDir string, content string) {
		t.Helper()

		dirPath := path.Join(testDir, "device")
		if err := os.MkdirAll(dirPath, 0755); err != nil {
			t.Fatal(err)
		}

		filePath := path.Join(dirPath, "uevent")
		if err := ioutil.WriteFile(filePath, []byte(content), 0644); err != nil {
			t.Fatal(err)
		}
	}

	for name, tc := range map[string]struct {
		subsystem string
		content   string
		expRes    bool
	}{
		"nil": {
			expRes: false,
		},
		"valid NetvscDevice": {
			subsystem: "net",
			content: `42
FOO=BAR
DRIVER=hv_netvsc`,
			expRes: true,
		},
		"invalid subsystem": {
			subsystem: "ib",
			content: `42
FOO=BAR
DRIVER=hv_netvsc`,
			expRes: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			testDir, cleanupTestDir := test.CreateTestDir(t)
			defer cleanupTestDir()

			if tc.content != "" {
				mkUeventFile(testDir, tc.content)
			}

			res := isNetvscDevice(testDir, tc.subsystem)
			test.AssertEqual(t, tc.expRes, res, "")
		})
	}
}

func setupTestNetDevClasses(t *testing.T, root string, devClasses map[string]uint32) {
	t.Helper()

	for dev, class := range devClasses {
		path := filepath.Join(root, "class", "net", dev)
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
}

func setupTestNetDevOperStates(t *testing.T, root string, devStates map[string]string) {
	t.Helper()

	for dev, state := range devStates {
		path := filepath.Join(root, "class", "net", dev)
		os.MkdirAll(path, 0755)

		f, err := os.Create(filepath.Join(path, "operstate"))
		if err != nil {
			t.Fatal(err.Error())
		}

		_, err = f.WriteString(fmt.Sprintf("%s\n", state))
		f.Close()
		if err != nil {
			t.Fatal(err.Error())
		}
	}
}

func TestSysfs_Provider_GetNetDevClass(t *testing.T) {
	testDir, cleanupTestDir := test.CreateTestDir(t)
	defer cleanupTestDir()

	devs := map[string]uint32{
		"lo":   uint32(hardware.Loopback),
		"eth1": uint32(hardware.Ether),
	}

	setupTestNetDevClasses(t, testDir, devs)

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
			defer test.ShowBufferOnFailure(t, buf)

			p := NewProvider(log)
			p.root = testDir

			result, err := p.GetNetDevClass(tc.in)

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expResult, result, "")
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

	if dev != "" {
		if err := os.Symlink(pciPath, filepath.Join(path, "device")); err != nil && !os.IsExist(err) {
			t.Fatal(err)
		}
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

	if err := os.Symlink(devPath, filepath.Join(classPath, filepath.Base(devPath))); err != nil && !os.IsExist(err) {
		t.Fatal(err)
	}

	if err := os.Symlink(classPath, filepath.Join(devPath, "subsystem")); err != nil && !os.IsExist(err) {
		t.Fatal(err)
	}
}

func setupNUMANode(t *testing.T, devPath, numaStr string) {
	writeTestFile(t, filepath.Join(devPath, "device", "numa_node"), numaStr)
}

func setupVirtualIB(t *testing.T, root, virtDev, parent string) {
	virtDevDir := filepath.Join(root, "devices", "virtual", "net", virtDev)
	if err := os.MkdirAll(virtDevDir, 0755); err != nil {
		t.Fatal(err)
	}

	setupClassLink(t, root, "net", virtDevDir)
	setupTestNetDevClasses(t, root, map[string]uint32{
		virtDev: uint32(hardware.Infiniband),
	})

	// Link to the parent device is just the parent's name
	f, err := os.Create(filepath.Join(virtDevDir, "parent"))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	_, err = f.WriteString(fmt.Sprintf("%s\n", parent))
	if err != nil {
		t.Fatal(err)
	}
}

func setupNetvscDev(t *testing.T, root, devName, backingDevName string) {
	devPath := filepath.Join(root, "devices", "netsvc", devName)
	if err := os.MkdirAll(devPath, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(devPath, filepath.Join(root, "class", "net", devName)); err != nil {
		t.Fatal(err)
	}

	if backingDevName != "" {
		backingDevPath := filepath.Join(root, "class", "net", backingDevName)
		if err := os.Symlink(backingDevPath, filepath.Join(devPath, "lower_"+backingDevName)); err != nil {
			t.Fatal(err)
		}
	}

	setupClassLink(t, root, "net", devPath)

	dirPath := path.Join(devPath, "device")
	if err := os.MkdirAll(dirPath, 0755); err != nil {
		t.Fatal(err)
	}

	filePath := path.Join(dirPath, "uevent")
	if err := ioutil.WriteFile(filePath, []byte("DRIVER=hv_netvsc"), 0644); err != nil {
		t.Fatal(err)
	}
}

func TestProvider_GetTopology(t *testing.T) {
	validPCIAddr := "0000:02:00.0"
	testTopo := &hardware.Topology{
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
	}

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
					{class: "infiniband", name: "mlx0"},
					{class: "cxi", name: "cxi0"},
				} {
					path := setupPCIDev(t, root, validPCIAddr, dev.class, dev.name)
					setupClassLink(t, root, dev.class, path)
					setupNUMANode(t, path, "2\n")
				}
			},
			p:         &Provider{},
			expResult: testTopo,
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
			p:         &Provider{},
			expResult: testTopo,
		},
		"virtual devices": {
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

				// Virtual device with a physical device backing it
				virtPath1 := filepath.Join(root, "devices", "virtual", "net", "virt_net0")
				if err := os.MkdirAll(virtPath1, 0755); err != nil {
					t.Fatal(err)
				}

				backingDevPath := filepath.Join(root, "class", "net", "net0")
				if err := os.Symlink(backingDevPath, filepath.Join(virtPath1, "lower_net0")); err != nil {
					t.Fatal(err)
				}

				setupClassLink(t, root, "net", virtPath1)

				// Virtual IB device
				setupVirtualIB(t, root, "virt_ib0", "net0")

				// Virtual device with no physical device
				virtPath := filepath.Join(root, "devices", "virtual", "net", "virt0")
				if err := os.MkdirAll(virtPath, 0755); err != nil {
					t.Fatal(err)
				}

				setupClassLink(t, root, "net", virtPath)
			},
			p: &Provider{},
			expResult: &hardware.Topology{
				NUMANodes: testTopo.NUMANodes,
				VirtualDevices: []*hardware.VirtualDevice{
					{
						Name: "virt0",
						Type: hardware.DeviceTypeNetInterface,
					},
					{
						Name:          "virt_ib0",
						Type:          hardware.DeviceTypeNetInterface,
						BackingDevice: testTopo.AllDevices()["net0"].(*hardware.PCIDevice),
					},
					{
						Name:          "virt_net0",
						Type:          hardware.DeviceTypeNetInterface,
						BackingDevice: testTopo.AllDevices()["net0"].(*hardware.PCIDevice),
					},
				},
			},
		},
		"NetVSC devices": {
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

				// Virtual IB device
				setupVirtualIB(t, root, "virt_ib0", "net0")

				setupNetvscDev(t, root, "eth0", "net0")
				setupNetvscDev(t, root, "eth1", "")
			},
			p: &Provider{},
			expResult: &hardware.Topology{
				NUMANodes: testTopo.NUMANodes,
				VirtualDevices: []*hardware.VirtualDevice{
					{
						Name:          "eth0",
						Type:          hardware.DeviceTypeNetInterface,
						BackingDevice: testTopo.AllDevices()["net0"].(*hardware.PCIDevice),
					},
					{
						Name:          "virt_ib0",
						Type:          hardware.DeviceTypeNetInterface,
						BackingDevice: testTopo.AllDevices()["net0"].(*hardware.PCIDevice),
					},
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
			defer test.ShowBufferOnFailure(t, buf)

			testDir, cleanupTestDir := test.CreateTestDir(t)
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

			result, err := tc.p.GetTopology(test.Context(t))

			test.CmpErr(t, tc.expErr, err)

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
		provider  string
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
					Name:   "cxi0",
					OSName: "cxi0",
					Providers: hardware.NewFabricProviderSet(
						&hardware.FabricProvider{
							Name: "ofi+cxi",
						},
					),
				},
				&hardware.FabricInterface{
					Name:   "cxi1",
					OSName: "cxi1",
					Providers: hardware.NewFabricProviderSet(
						&hardware.FabricProvider{
							Name: "ofi+cxi",
						},
					),
				},
			),
		},
		"CXI specified": {
			p:        &Provider{},
			provider: "ofi+cxi",
			expResult: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Name:   "cxi0",
					OSName: "cxi0",
					Providers: hardware.NewFabricProviderSet(
						&hardware.FabricProvider{
							Name: "ofi+cxi",
						},
					),
				},
				&hardware.FabricInterface{
					Name:   "cxi1",
					OSName: "cxi1",
					Providers: hardware.NewFabricProviderSet(
						&hardware.FabricProvider{
							Name: "ofi+cxi",
						},
					),
				},
			),
		},
		"specified different fabric provider": {
			p:         &Provider{},
			provider:  "ofi+tcp",
			expResult: hardware.NewFabricInterfaceSet(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			testDir, cleanupTestDir := test.CreateTestDir(t)
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

			result, err := tc.p.GetFabricInterfaces(test.Context(t), tc.provider)

			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResult, result,
				cmp.AllowUnexported(hardware.FabricInterfaceSet{}),
				cmp.AllowUnexported(hardware.FabricProviderSet{}),
			); diff != "" {
				t.Errorf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestSysfs_Provider_GetNetDevState(t *testing.T) {
	setupNet := func(t *testing.T, root string) {
		t.Helper()

		path := setupPCIDev(t, root, "0000:02:02.1", "net", "net0")
		setupClassLink(t, root, "net", path)

		setupTestNetDevClasses(t, root, map[string]uint32{
			"net0": uint32(hardware.Ether),
			"lo":   uint32(hardware.Loopback),
		})
	}

	setupIBPorts := func(t *testing.T, ibPath string, portState map[int]string) {
		t.Helper()

		for port, state := range portState {
			portPath := filepath.Join(ibPath, "ports", strconv.Itoa(port))
			if err := os.MkdirAll(portPath, 0755); err != nil {
				t.Fatal(err)
			}

			statePath := filepath.Join(portPath, "state")
			if err := ioutil.WriteFile(statePath, []byte(state), 0644); err != nil {
				t.Fatal(err)
			}
		}
	}

	setupIBDevPath := func(t *testing.T, root, iface, domain string) string {
		t.Helper()

		ibPath := setupPCIDev(t, root, "0000:01:01.1", "infiniband", domain)
		setupClassLink(t, root, "infiniband", ibPath)
		netPath := setupPCIDev(t, root, "0000:01:01.1", "net", iface)
		setupClassLink(t, root, "net", netPath)

		setupTestNetDevClasses(t, root, map[string]uint32{
			iface: uint32(hardware.Infiniband),
		})

		return ibPath
	}

	setupIB := func(t *testing.T, root string) {
		t.Helper()

		ibPath := setupIBDevPath(t, root, "ib0", "mlx0")

		setupIBPorts(t, ibPath, map[int]string{
			1: "4: ACTIVE",
		})
	}

	setupDefault := func(t *testing.T, root string) {
		t.Helper()

		setupIB(t, root)
		setupNet(t, root)
	}

	for name, tc := range map[string]struct {
		setup    func(*testing.T, string)
		p        *Provider
		iface    string
		expState hardware.NetDevState
		expErr   error
	}{
		"nil": {
			iface:  "ib0",
			expErr: errors.New("nil"),
		},
		"no iface": {
			p:      &Provider{},
			expErr: errors.New("interface name is required"),
		},
		"bad interface": {
			p:      &Provider{},
			iface:  "fake",
			expErr: errors.New("can't determine device class"),
		},
		"net no operstate": {
			setup: func(t *testing.T, root string) {
				setupNet(t, root)
			},
			p:      &Provider{},
			iface:  "net0",
			expErr: errors.New("failed to get \"net0\" operational state"),
		},
		"net ready": {
			setup: func(t *testing.T, root string) {
				setupNet(t, root)
				setupTestNetDevOperStates(t, root, map[string]string{
					"net0": "up",
				})
			},
			p:        &Provider{},
			iface:    "net0",
			expState: hardware.NetDevStateReady,
		},
		"net down": {
			setup: func(t *testing.T, root string) {
				setupNet(t, root)
				setupTestNetDevOperStates(t, root, map[string]string{
					"net0": "down",
				})
			},
			p:        &Provider{},
			iface:    "net0",
			expState: hardware.NetDevStateDown,
		},
		"net lowerlayerdown": {
			setup: func(t *testing.T, root string) {
				setupNet(t, root)
				setupTestNetDevOperStates(t, root, map[string]string{
					"net0": "lowerlayerdown",
				})
			},
			p:        &Provider{},
			iface:    "net0",
			expState: hardware.NetDevStateDown,
		},
		"net notpresent": {
			setup: func(t *testing.T, root string) {
				setupNet(t, root)
				setupTestNetDevOperStates(t, root, map[string]string{
					"net0": "notpresent",
				})
			},
			p:        &Provider{},
			iface:    "net0",
			expState: hardware.NetDevStateDown,
		},
		"net testing": {
			setup: func(t *testing.T, root string) {
				setupNet(t, root)
				setupTestNetDevOperStates(t, root, map[string]string{
					"net0": "testing",
				})
			},
			p:        &Provider{},
			iface:    "net0",
			expState: hardware.NetDevStateNotReady,
		},
		"net dormant": {
			setup: func(t *testing.T, root string) {
				setupNet(t, root)
				setupTestNetDevOperStates(t, root, map[string]string{
					"net0": "dormant",
				})
			},
			p:        &Provider{},
			iface:    "net0",
			expState: hardware.NetDevStateNotReady,
		},
		"net unknown": {
			setup: func(t *testing.T, root string) {
				setupNet(t, root)
				setupTestNetDevOperStates(t, root, map[string]string{
					"net0": "unknown",
				})
			},
			p:        &Provider{},
			iface:    "net0",
			expState: hardware.NetDevStateUnknown,
		},
		"net operstate case-insensitive": {
			setup: func(t *testing.T, root string) {
				setupNet(t, root)
				setupTestNetDevOperStates(t, root, map[string]string{
					"net0": "UP",
				})
			},
			p:        &Provider{},
			iface:    "net0",
			expState: hardware.NetDevStateReady,
		},
		"loopback unknown is ready": {
			setup: func(t *testing.T, root string) {
				setupNet(t, root)
				setupTestNetDevOperStates(t, root, map[string]string{
					"lo": "unknown",
				})
			},
			p:        &Provider{},
			iface:    "lo",
			expState: hardware.NetDevStateReady,
		},
		"no IB dir": {
			setup: func(t *testing.T, root string) {
				setupTestNetDevClasses(t, root, map[string]uint32{
					"ib0": uint32(hardware.Infiniband),
				})
			},
			p:      &Provider{},
			iface:  "ib0",
			expErr: errors.New("can't access Infiniband details"),
		},
		"no IB devices": {
			setup: func(t *testing.T, root string) {
				netPath := setupPCIDev(t, root, "0000:01:01.1", "net", "ib0")
				setupClassLink(t, root, "net", netPath)
				_ = setupPCIDev(t, root, "0000:01:01.1", "infiniband", "")

				setupTestNetDevClasses(t, root, map[string]uint32{
					"ib0": uint32(hardware.Infiniband),
				})
			},
			p:        &Provider{},
			iface:    "ib0",
			expState: hardware.NetDevStateUnknown,
		},
		"virtual IB device": {
			setup: func(t *testing.T, root string) {
				setupIB(t, root)
				setupVirtualIB(t, root, "ib0.1", "ib0")
			},
			p:        &Provider{},
			iface:    "ib0.1",
			expState: hardware.NetDevStateReady,
		},
		"no port info": {
			setup: func(t *testing.T, root string) {
				_ = setupIBDevPath(t, root, "ib0", "mlx0")
			},
			p:      &Provider{},
			iface:  "ib0",
			expErr: errors.New("unable to get ports"),
		},
		"no port state file": {
			setup: func(t *testing.T, root string) {
				ibPath := setupIBDevPath(t, root, "ib0", "mlx0")
				portPath := filepath.Join(ibPath, "ports", "1")
				if err := os.MkdirAll(portPath, 0755); err != nil {
					t.Fatal(err)
				}
			},
			p:      &Provider{},
			iface:  "ib0",
			expErr: errors.New("unable to get state"),
		},
		"invalid port state": {
			setup: func(t *testing.T, root string) {
				t.Helper()

				ibPath := setupIBDevPath(t, root, "ib0", "mlx0")

				setupIBPorts(t, ibPath, map[int]string{
					1: "garbage",
				})
			},
			p:        &Provider{},
			iface:    "ib0",
			expState: hardware.NetDevStateUnknown,
		},
		"port down": {
			setup: func(t *testing.T, root string) {
				t.Helper()

				ibPath := setupIBDevPath(t, root, "ib0", "mlx0")

				setupIBPorts(t, ibPath, map[int]string{
					1: "1: DOWN",
				})
			},
			p:        &Provider{},
			iface:    "ib0",
			expState: hardware.NetDevStateDown,
		},
		"port not ready": {
			setup: func(t *testing.T, root string) {
				t.Helper()

				ibPath := setupIBDevPath(t, root, "ib0", "mlx0")

				setupIBPorts(t, ibPath, map[int]string{
					1: "2: INITIALIZING",
				})
			},
			p:        &Provider{},
			iface:    "ib0",
			expState: hardware.NetDevStateNotReady,
		},
		"ready": {
			p:        &Provider{},
			iface:    "ib0",
			expState: hardware.NetDevStateReady,
		},
		"one dev not ready": {
			setup: func(t *testing.T, root string) {
				t.Helper()

				for dev, state := range map[string]string{
					"mlx0_0": "1: DOWN",
					"mlx0_1": "0: UNKNOWN",
					"mlx0_2": "4: ACTIVE",
					"mlx0_3": "3: ARMED",
				} {
					ibPath := setupIBDevPath(t, root, "ib0", dev)

					setupIBPorts(t, ibPath, map[int]string{
						1: state,
					})
				}
			},
			p:        &Provider{},
			iface:    "ib0",
			expState: hardware.NetDevStateNotReady,
		},
		"one IB dev up, others down/unknown": {
			setup: func(t *testing.T, root string) {
				t.Helper()

				for dev, state := range map[string]string{
					"mlx0_0": "1: DOWN",
					"mlx0_1": "0: UNKNOWN",
					"mlx0_2": "4: ACTIVE",
				} {
					ibPath := setupIBDevPath(t, root, "ib0", dev)

					setupIBPorts(t, ibPath, map[int]string{
						1: state,
					})
				}
			},
			p:        &Provider{},
			iface:    "ib0",
			expState: hardware.NetDevStateReady,
		},
		"all IB devs down or unknown": {
			setup: func(t *testing.T, root string) {
				t.Helper()

				for dev, state := range map[string]string{
					"mlx0_0": "1: DOWN",
					"mlx0_1": "0: UNKNOWN",
					"mlx0_2": "0: UNKNOWN",
				} {
					ibPath := setupIBDevPath(t, root, "ib0", dev)

					setupIBPorts(t, ibPath, map[int]string{
						1: state,
					})
				}
			},
			p:        &Provider{},
			iface:    "ib0",
			expState: hardware.NetDevStateDown,
		},
		"all IB devs unknown": {
			setup: func(t *testing.T, root string) {
				t.Helper()

				for dev, state := range map[string]string{
					"mlx0_1": "0: UNKNOWN",
					"mlx0_2": "0: UNKNOWN",
				} {
					ibPath := setupIBDevPath(t, root, "ib0", dev)

					setupIBPorts(t, ibPath, map[int]string{
						1: state,
					})
				}
			},
			p:        &Provider{},
			iface:    "ib0",
			expState: hardware.NetDevStateUnknown,
		},
		"at least one IB port not ready": {
			setup: func(t *testing.T, root string) {
				t.Helper()

				ibPath := setupIBDevPath(t, root, "ib0", "mlx0")

				setupIBPorts(t, ibPath, map[int]string{
					1: "1: DOWN",
					2: "0: UNKNOWN",
					3: "4: ACTIVE",
					4: "2: INITIALIZING",
				})
			},
			p:        &Provider{},
			iface:    "ib0",
			expState: hardware.NetDevStateNotReady,
		},
		"one IB port active, others down/unknown": {
			setup: func(t *testing.T, root string) {
				t.Helper()

				ibPath := setupIBDevPath(t, root, "ib0", "mlx0")

				setupIBPorts(t, ibPath, map[int]string{
					1: "1: DOWN",
					2: "0: UNKNOWN",
					3: "4: ACTIVE",
				})
			},
			p:        &Provider{},
			iface:    "ib0",
			expState: hardware.NetDevStateReady,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			testDir, cleanupTestDir := test.CreateTestDir(t)
			defer cleanupTestDir()

			if tc.p != nil {
				tc.p.log = log

				// Mock out a fake sysfs in the testDir
				tc.p.root = testDir
			}

			if tc.setup == nil {
				tc.setup = setupDefault
			}
			tc.setup(t, testDir)

			state, err := tc.p.GetNetDevState(tc.iface)

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expState, state, "")
		})
	}
}

func TestSysfs_Provider_ibStateToNetDevState(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expResult hardware.NetDevState
	}{
		"empty": {
			expResult: hardware.NetDevStateUnknown,
		},
		"garbage": {
			input:     "trash",
			expResult: hardware.NetDevStateUnknown,
		},
		"unknown": {
			input:     "0: UNKNOWN",
			expResult: hardware.NetDevStateUnknown,
		},
		"down": {
			input:     "1: DOWN",
			expResult: hardware.NetDevStateDown,
		},
		"init": {
			input:     "2: INITIALIZING",
			expResult: hardware.NetDevStateNotReady,
		},
		"armed": {
			input:     "3: ARMED",
			expResult: hardware.NetDevStateNotReady,
		},
		"active": {
			input:     "4: ACTIVE",
			expResult: hardware.NetDevStateReady,
		},
		"bad enum": {
			input:     "1234: something",
			expResult: hardware.NetDevStateUnknown,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			p := &Provider{log: log}
			result := p.ibStateToNetDevState(tc.input)

			test.AssertEqual(t, tc.expResult, result, "")
		})
	}
}

func TestSysfs_condenseNetDevState(t *testing.T) {
	for name, tc := range map[string]struct {
		input     []hardware.NetDevState
		expResult hardware.NetDevState
	}{
		"nil": {
			expResult: hardware.NetDevStateUnknown,
		},
		"empty": {
			input:     []hardware.NetDevState{},
			expResult: hardware.NetDevStateUnknown,
		},
		"unknown": {
			input:     []hardware.NetDevState{hardware.NetDevStateUnknown},
			expResult: hardware.NetDevStateUnknown,
		},
		"down": {
			input:     []hardware.NetDevState{hardware.NetDevStateDown},
			expResult: hardware.NetDevStateDown,
		},
		"not ready": {
			input:     []hardware.NetDevState{hardware.NetDevStateNotReady},
			expResult: hardware.NetDevStateNotReady,
		},
		"ready": {
			input:     []hardware.NetDevState{hardware.NetDevStateReady},
			expResult: hardware.NetDevStateReady,
		},
		"down overrides unknown": {
			input: []hardware.NetDevState{
				hardware.NetDevStateUnknown,
				hardware.NetDevStateDown,
				hardware.NetDevStateUnknown,
			},
			expResult: hardware.NetDevStateDown,
		},
		"ready overrides down/unknown": {
			input: []hardware.NetDevState{
				hardware.NetDevStateUnknown,
				hardware.NetDevStateDown,
				hardware.NetDevStateReady,
				hardware.NetDevStateUnknown,
				hardware.NetDevStateDown,
			},
			expResult: hardware.NetDevStateReady,
		},
		"not ready overrides all": {
			input: []hardware.NetDevState{
				hardware.NetDevStateUnknown,
				hardware.NetDevStateDown,
				hardware.NetDevStateReady,
				hardware.NetDevStateNotReady,
				hardware.NetDevStateUnknown,
				hardware.NetDevStateDown,
				hardware.NetDevStateReady,
			},
			expResult: hardware.NetDevStateNotReady,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := condenseNetDevState(tc.input)

			test.AssertEqual(t, tc.expResult, result, "")
		})
	}
}

func setupTestIsIOMMUEnabled(t *testing.T, root string, extraDirs ...string) {
	t.Helper()

	dirs := append([]string{root}, extraDirs...)

	path := filepath.Join(dirs...)
	os.MkdirAll(path, 0755)
}

func TestSysfs_Provider_IsIOMMUEnabled(t *testing.T) {
	for name, tc := range map[string]struct {
		nilProvider bool
		extraDirs   []string
		expResult   bool
		expErr      error
	}{
		"nil provider": {
			nilProvider: true,
			expErr:      errors.New("provider is nil"),
		},
		"missing iommu dir": {
			extraDirs: []string{"class"},
		},
		"iommu disabled": {
			extraDirs: []string{"class", "iommu"},
		},
		"iommu enabled": {
			extraDirs: []string{"class", "iommu", "dmar0"},
			expResult: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			testDir, cleanupTestDir := test.CreateTestDir(t)
			defer cleanupTestDir()

			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			var p *Provider
			if !tc.nilProvider {
				p = NewProvider(log)
				p.root = testDir
				setupTestIsIOMMUEnabled(t, testDir, tc.extraDirs...)
			}

			result, err := p.IsIOMMUEnabled()

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expResult, result, "")
		})
	}
}
