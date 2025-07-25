//
// (C) Copyright 2021-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"fmt"
	"net"
	"os"
	"os/user"
	"strings"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	sysprov "github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

// basic engine configs populated enough to complete validation
func basicEngineCfg(i int) *engine.Config {
	return engine.MockConfig().WithFabricInterfacePort(20000).
		WithFabricInterface(fmt.Sprintf("ib%d", i))
}
func pmemTier(i int) *storage.TierConfig {
	return storage.NewTierConfig().WithStorageClass(storage.ClassDcpm.String()).
		WithScmMountPoint(fmt.Sprintf("/mnt/daos%d", i)).
		WithScmDeviceList(fmt.Sprintf("/dev/pmem%d", i))
}
func ramTier(i, ramdiskSize int) *storage.TierConfig {
	return storage.NewTierConfig().WithStorageClass(storage.ClassRam.String()).
		WithScmMountPoint(fmt.Sprintf("/mnt/daos%d", i)).
		WithScmRamdiskSize(uint(ramdiskSize))
}
func nvmeTier(i int) *storage.TierConfig {
	return storage.NewTierConfig().WithStorageClass(storage.ClassNvme.String()).
		WithBdevDeviceList(test.MockPCIAddr(int32(i)))
}
func fakeNvmeTier() *storage.TierConfig {
	return storage.NewTierConfig().WithStorageClass(storage.ClassFile.String()).
		WithBdevFileSize(10).WithBdevDeviceList("bdev1", "bdev2")
}
func pmemOnlyEngine(i int) *engine.Config {
	return basicEngineCfg(i).WithStorage(pmemTier(i)).WithTargetCount(8)
}
func pmemEngine(i int) *engine.Config {
	return basicEngineCfg(i).WithStorage(pmemTier(i), nvmeTier(i)).WithTargetCount(16)
}
func pmemFakeNvmeEngine(i int) *engine.Config {
	return basicEngineCfg(i).WithStorage(pmemTier(i), fakeNvmeTier()).WithTargetCount(16)
}
func ramEngine(i, ramdiskSize int) *engine.Config {
	return basicEngineCfg(i).WithStorage(ramTier(i, ramdiskSize), nvmeTier(i)).WithTargetCount(16)
}

var mockFabIfSet = hardware.NewFabricInterfaceSet(
	&hardware.FabricInterface{
		Name:          "eth0",
		NetInterfaces: common.NewStringSet("eth0"),
		DeviceClass:   hardware.Ether,
		Providers:     testFabricProviderSet("test", "ofi+tcp"),
	},
	&hardware.FabricInterface{
		Name:          "eth1",
		NetInterfaces: common.NewStringSet("eth1"),
		DeviceClass:   hardware.Ether,
		NUMANode:      1,
		Providers:     testFabricProviderSet("test", "ofi+tcp"),
	},
	&hardware.FabricInterface{
		Name:          "ib0",
		NetInterfaces: common.NewStringSet("ib0"),
		DeviceClass:   hardware.Infiniband,
		Providers:     testFabricProviderSet("test", "ofi+tcp", "ofi+verbs;ofi_rxm"),
	},
	&hardware.FabricInterface{
		Name:          "ib1",
		NetInterfaces: common.NewStringSet("ib1"),
		DeviceClass:   hardware.Infiniband,
		NUMANode:      1,
		Providers:     testFabricProviderSet("test", "ofi+tcp", "ofi+verbs;ofi_rxm"),
	},
)

type mockInterface struct {
	addrs []net.Addr
	err   error
}

func (m mockInterface) Addrs() ([]net.Addr, error) {
	return m.addrs, m.err
}

type mockAddr struct{}

func (a mockAddr) Network() string {
	return "mock network"
}

func (a mockAddr) String() string {
	return "mock string"
}

func TestServer_checkFabricInterface(t *testing.T) {
	for name, tc := range map[string]struct {
		name   string
		lookup func(string) (netInterface, error)
		expErr error
	}{
		"no name": {
			expErr: errors.New("no name"),
		},
		"no lookup fn": {
			name:   "dontcare",
			expErr: errors.New("no lookup"),
		},
		"lookup failed": {
			name: "dontcare",
			lookup: func(_ string) (netInterface, error) {
				return nil, errors.New("mock lookup")
			},
			expErr: errors.New("mock lookup"),
		},
		"interface Addrs failed": {
			name: "dontcare",
			lookup: func(_ string) (netInterface, error) {
				return &mockInterface{
					err: errors.New("mock Addrs"),
				}, nil
			},
			expErr: errors.New("mock Addrs"),
		},
		"interface has no addrs": {
			name: "dontcare",
			lookup: func(_ string) (netInterface, error) {
				return &mockInterface{
					addrs: make([]net.Addr, 0),
				}, nil
			},
			expErr: errors.New("no network addresses"),
		},
		"success": {
			name: "dontcare",
			lookup: func(_ string) (netInterface, error) {
				return &mockInterface{
					addrs: []net.Addr{
						&mockAddr{},
					},
				}, nil
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			err := checkFabricInterface(tc.name, tc.lookup)

			test.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestServer_getSrxSetting(t *testing.T) {
	defCfg := config.DefaultServer()

	for name, tc := range map[string]struct {
		cfg        *config.Server
		expSetting int32
		expErr     error
	}{
		"no engines": {
			cfg:        config.DefaultServer(),
			expSetting: -1,
		},
		"not set defaults to cfg value": {
			cfg: config.DefaultServer().WithEngines(
				engine.MockConfig(),
				engine.MockConfig(),
			),
			expSetting: int32(common.BoolAsInt(!defCfg.Fabric.DisableSRX)),
		},
		"set to 0 in both (single)": {
			cfg: config.DefaultServer().WithEngines(
				engine.MockConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0"),
				engine.MockConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0"),
			),
			expSetting: 0,
		},
		"set to 1 in both (single)": {
			cfg: config.DefaultServer().WithEngines(
				engine.MockConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=1"),
				engine.MockConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=1"),
			),
			expSetting: 1,
		},
		"set to 0 in both (multi)": {
			cfg: config.DefaultServer().WithEngines(
				engine.MockConfig().WithEnvVars("FOO=BAR", "FI_OFI_RXM_USE_SRX=0"),
				engine.MockConfig().WithEnvVars("FOO=BAR", "FI_OFI_RXM_USE_SRX=0"),
			),
			expSetting: 0,
		},
		"set to 1 in both (multi)": {
			cfg: config.DefaultServer().WithEngines(
				engine.MockConfig().WithEnvVars("FOO=BAR", "FI_OFI_RXM_USE_SRX=1"),
				engine.MockConfig().WithEnvVars("FOO=BAR", "FI_OFI_RXM_USE_SRX=1"),
			),
			expSetting: 1,
		},
		"set twice; first value used": {
			cfg: config.DefaultServer().WithEngines(
				engine.MockConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0", "FI_OFI_RXM_USE_SRX=1"),
				engine.MockConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=1"),
			),
			expErr: errors.New("FI_OFI_RXM_USE_SRX"),
		},
		"set in both; different values": {
			cfg: config.DefaultServer().WithEngines(
				engine.MockConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0"),
				engine.MockConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=1"),
			),
			expErr: errors.New("FI_OFI_RXM_USE_SRX"),
		},
		"set in first; no vars in second": {
			cfg: config.DefaultServer().WithEngines(
				engine.MockConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0"),
				engine.MockConfig(),
			),
			expErr: errors.New("FI_OFI_RXM_USE_SRX"),
		},
		"no vars in first; set in second": {
			cfg: config.DefaultServer().WithEngines(
				engine.MockConfig(),
				engine.MockConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0"),
			),
			expErr: errors.New("FI_OFI_RXM_USE_SRX"),
		},
		"set in first; unset in second": {
			cfg: config.DefaultServer().WithEngines(
				engine.MockConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0"),
				engine.MockConfig().WithEnvVars("FOO=bar"),
			),
			expErr: errors.New("FI_OFI_RXM_USE_SRX"),
		},
		"unset in first; set in second": {
			cfg: config.DefaultServer().WithEngines(
				engine.MockConfig().WithEnvVars("FOO=bar"),
				engine.MockConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0"),
			),
			expErr: errors.New("FI_OFI_RXM_USE_SRX"),
		},
		"wonky value": {
			cfg: config.DefaultServer().WithEngines(
				engine.MockConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=on"),
			),
			expErr: errors.New("on"),
		},
		"set in env_pass_through": {
			cfg: config.DefaultServer().WithEngines(
				engine.MockConfig().WithEnvPassThrough("FI_OFI_RXM_USE_SRX"),
			),
			expErr: errors.New("FI_OFI_RXM_USE_SRX"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotSetting, gotErr := getSrxSetting(tc.cfg)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expSetting, gotSetting, "unexpected SRX setting")
		})
	}
}

func TestServer_prepBdevStorage_setEngineMemSize(t *testing.T) {
	usrCurrent, err := user.Current()
	if err != nil {
		t.Fatal(err)
	}
	username := usrCurrent.Username
	if username == "root" {
		t.Fatal("prepBdevStorage tests cannot be run as root user")
	}
	prepCmpOpt := cmp.Comparer(func(x, y *storage.BdevDeviceList) bool {
		if x == nil && y == nil {
			return true
		}
		return x.Equals(y)
	})
	defClean := storage.BdevPrepareRequest{
		CleanSpdkHugepages: true,
		CleanSpdkLockfiles: true,
	}
	defCleanSingleEngine := storage.BdevPrepareRequest{
		CleanSpdkHugepages: true,
		CleanSpdkLockfiles: true,
		PCIAllowList:       test.MockPCIAddr(0),
	}
	defCleanDualEngine := storage.BdevPrepareRequest{
		CleanSpdkHugepages: true,
		CleanSpdkLockfiles: true,
		PCIAllowList: strings.Join([]string{
			test.MockPCIAddr(0), test.MockPCIAddr(1),
		}, storage.BdevPciAddrSep),
	}

	for name, tc := range map[string]struct {
		iommuDisabled   bool
		srvCfgExtra     func(*config.Server) *config.Server
		memInfo1        *common.SysMemInfo // Before prepBdevStorage()
		memInfo2        *common.SysMemInfo // After prepBdevStorage()
		hugepagesFree   int                // Values for all NUMA nodes, will be split per-node.
		hugepagesTotal  int                // Values for all NUMA nodes, will be split per-node.
		bmbc            *bdev.MockBackendConfig
		overrideUser    string
		expPrepErr      error
		expPrepCalls    []storage.BdevPrepareRequest
		expMemSize      int
		expHugepageSize int
		expNotice       bool
	}{
		"vfio disabled; non-root user": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithDisableVFIO(true).
					WithEngines(pmemEngine(0))
			},
			expPrepErr: FaultVfioDisabled,
		},
		"vfio disabled; root user": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithDisableVFIO(true).
					WithEngines(pmemEngine(0))
			},
			overrideUser:   "root",
			hugepagesFree:  8192,
			hugepagesTotal: 8192,
			expPrepCalls: []storage.BdevPrepareRequest{
				defCleanSingleEngine,
				{
					HugeNodes:    "nodes_hp[0]=8192",
					TargetUser:   "root",
					DisableVFIO:  true,
					PCIAllowList: test.MockPCIAddr(0),
				},
			},
			expMemSize:      16384,
			expHugepageSize: 2,
		},
		"non-nvme bdevs; vfio disabled": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithDisableVFIO(true).
					WithEngines(pmemFakeNvmeEngine(0))
			},
			hugepagesFree:  8192,
			hugepagesTotal: 8192,
			expPrepCalls: []storage.BdevPrepareRequest{
				defClean,
				{
					HugeNodes:   "nodes_hp[0]=8192",
					TargetUser:  username,
					DisableVFIO: true,
				},
			},
			expMemSize:      16384,
			expHugepageSize: 2,
		},
		"iommu disabled": {
			iommuDisabled: true,
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(pmemEngine(0), pmemEngine(1))
			},
			expPrepErr: FaultIommuDisabled,
		},
		"iommu disabled; root user": {
			iommuDisabled: true,
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(pmemEngine(0))
			},
			overrideUser:   "root",
			hugepagesFree:  8192,
			hugepagesTotal: 8192,
			expPrepCalls: []storage.BdevPrepareRequest{
				defCleanSingleEngine,
				{
					HugeNodes:    "nodes_hp[0]=8192",
					TargetUser:   "root",
					PCIAllowList: test.MockPCIAddr(0),
				},
			},
			expMemSize:      16384,
			expHugepageSize: 2,
		},
		"non-nvme bdevs; iommu disabled": {
			iommuDisabled: true,
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(pmemFakeNvmeEngine(0))
			},
			hugepagesFree:  8192,
			hugepagesTotal: 8192,
			expPrepCalls: []storage.BdevPrepareRequest{
				defClean,
				{
					HugeNodes:  "nodes_hp[0]=8192",
					TargetUser: username,
				},
			},
			expMemSize:      16384,
			expHugepageSize: 2,
		},
		"no bdevs configured; hugepages disabled": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithDisableHugepages(true).
					WithEngines(pmemOnlyEngine(0), pmemOnlyEngine(1))
			},
			expNotice: true,
		},
		"no engines configured; nr_hugepages unset": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(0).WithEngines()
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				defClean,
				{
					HugeNodes:  "nodes_hp[0]=128",
					TargetUser: username,
					EnableVMD:  true,
				},
			},
		},
		"no bdevs configured; nr_hugepages unset": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(0).
					WithEngines(pmemOnlyEngine(0), pmemOnlyEngine(1))
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				defClean,
				{
					HugeNodes:  "nodes_hp[0]=128",
					TargetUser: username,
					EnableVMD:  true,
				},
			},
		},
		"no bdevs configured; nr_hugepages set": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(1024).
					WithEngines(pmemOnlyEngine(0), pmemOnlyEngine(1))
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				defClean,
				{
					HugeNodes:  "nodes_hp[0]=1024",
					TargetUser: username,
					EnableVMD:  true,
				},
			},
		},
		"2 engines both numa 0; hugepage alloc only on numa 0; insufficient existing pages": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(16384).
					WithEngines(pmemEngine(0), pmemEngine(1).WithPinnedNumaNode(0)).
					WithBdevExclude(test.MockPCIAddr(3))
			},
			memInfo1: &common.SysMemInfo{
				MemInfo: common.MemInfo{
					MemTotalKiB:     (50 * humanize.GiByte) / humanize.KiByte,
					HugepageSizeKiB: 2048,
					HugepagesFree:   16386,
					HugepagesTotal:  16386,
				},
				NumaNodes: []common.MemInfo{
					{
						NumaNodeIndex:  0,
						HugepagesTotal: 8193, // Not enough to satisfy 16384.
						HugepagesFree:  8193,
					},
					{
						NumaNodeIndex:  1,
						HugepagesTotal: 8193,
						HugepagesFree:  8193,
					},
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				defCleanDualEngine,
				{
					HugeNodes:  "nodes_hp[0]=16384", // Grow allocation.
					TargetUser: username,
					PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
						storage.BdevPciAddrSep, test.MockPCIAddr(1)),
					PCIBlockList: test.MockPCIAddr(3),
					EnableVMD:    true,
				},
			},
			memInfo2: &common.SysMemInfo{
				MemInfo: common.MemInfo{
					MemTotalKiB:     (50 * humanize.GiByte) / humanize.KiByte,
					HugepageSizeKiB: 2048,
					HugepagesFree:   16386,
					HugepagesTotal:  16386,
				},
				NumaNodes: []common.MemInfo{
					{
						NumaNodeIndex:  0,
						HugepagesTotal: 16384, // New allocation.
						HugepagesFree:  16384,
					},
					{
						NumaNodeIndex:  1,
						HugepagesTotal: 8193,
						HugepagesFree:  8193,
					},
				},
			},
			// (16384 hugepages / 2 engines both on NUMA-0) * 2mib size
			expMemSize:      16384,
			expHugepageSize: 2,
			// Not balanced across NUMA nodes so notice logged
			expNotice: true,
		},
		"2 engines both numa 0; hugepage alloc only on numa 0; sufficient existing pages": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(16384).
					WithEngines(pmemEngine(0), pmemEngine(1).WithPinnedNumaNode(0)).
					WithBdevExclude(test.MockPCIAddr(3))
			},
			memInfo1: &common.SysMemInfo{
				MemInfo: common.MemInfo{
					MemTotalKiB:     (50 * humanize.GiByte) / humanize.KiByte,
					HugepageSizeKiB: 2048,
					HugepagesFree:   16386,
					HugepagesTotal:  16386,
				},
				NumaNodes: []common.MemInfo{
					{
						NumaNodeIndex:  0,
						HugepagesTotal: 16386, // Enough to satisfy 16384.
						HugepagesFree:  16386,
					},
					{
						NumaNodeIndex:  1,
						HugepagesTotal: 0,
						HugepagesFree:  0,
					},
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				defCleanDualEngine,
				{
					HugeNodes:  "nodes_hp[0]=16386", // Keep existing allocation.
					TargetUser: username,
					PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
						storage.BdevPciAddrSep, test.MockPCIAddr(1)),
					PCIBlockList: test.MockPCIAddr(3),
					EnableVMD:    true,
				},
			},
			// (16384 hugepages requested / 2 engines both on NUMA-0) * 2mib size
			expMemSize:      16384,
			expHugepageSize: 2,
			// Not balanced across NUMA nodes so notice logged
			expNotice: true,
		},
		"2 engines both numa 1; hugepage alloc only on numa 1; insufficient existing pages": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(16384).
					WithEngines(pmemEngine(0).WithPinnedNumaNode(1), pmemEngine(1))
			},
			memInfo1: &common.SysMemInfo{
				MemInfo: common.MemInfo{
					MemTotalKiB:     (50 * humanize.GiByte) / humanize.KiByte,
					HugepageSizeKiB: 2048,
					HugepagesFree:   16386,
					HugepagesTotal:  16386,
				},
				NumaNodes: []common.MemInfo{
					{
						NumaNodeIndex:  0,
						HugepagesTotal: 8193, // Not enough to satisfy 16384.
						HugepagesFree:  8193,
					},
					{
						NumaNodeIndex:  1,
						HugepagesTotal: 8193,
						HugepagesFree:  8193,
					},
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				defCleanDualEngine,
				{
					HugeNodes:  "nodes_hp[1]=16384", // Grow allocation.
					TargetUser: username,
					PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
						storage.BdevPciAddrSep, test.MockPCIAddr(1)),
					EnableVMD: true,
				},
			},
			memInfo2: &common.SysMemInfo{
				MemInfo: common.MemInfo{
					MemTotalKiB:     (50 * humanize.GiByte) / humanize.KiByte,
					HugepageSizeKiB: 2048,
					HugepagesFree:   16386,
					HugepagesTotal:  16386,
				},
				NumaNodes: []common.MemInfo{
					{
						NumaNodeIndex:  0,
						HugepagesTotal: 8193,
						HugepagesFree:  8193,
					},
					{
						NumaNodeIndex:  1,
						HugepagesTotal: 16384, // New allocation.
						HugepagesFree:  16384,
					},
				},
			},
			// (16384 hugepages requested / 2 engines both on NUMA-0) * 2mib size
			expMemSize:      16384,
			expHugepageSize: 2,
			// Not balanced across NUMA nodes so notice logged
			expNotice: true,
		},
		"2 engines both numa 1; hugepage alloc only on numa 1; sufficient existing pages": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(16384).
					WithEngines(pmemEngine(0).WithPinnedNumaNode(1), pmemEngine(1))
			},
			memInfo1: &common.SysMemInfo{
				MemInfo: common.MemInfo{
					MemTotalKiB:     (50 * humanize.GiByte) / humanize.KiByte,
					HugepageSizeKiB: 2048,
					HugepagesFree:   16386,
					HugepagesTotal:  16386,
				},
				NumaNodes: []common.MemInfo{
					{
						NumaNodeIndex:  0,
						HugepagesTotal: 0,
						HugepagesFree:  0,
					},
					{
						NumaNodeIndex:  1,
						HugepagesTotal: 16386, // Enough to satisfy 16384.
						HugepagesFree:  16386,
					},
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				defCleanDualEngine,
				{
					HugeNodes:  "nodes_hp[1]=16386", // Keep existing allocation.
					TargetUser: username,
					PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
						storage.BdevPciAddrSep, test.MockPCIAddr(1)),
					EnableVMD: true,
				},
			},
			// (16384 hugepages requested / 2 engines both on NUMA-0) * 2mib size
			expMemSize:      16384,
			expHugepageSize: 2,
			// Not balanced across NUMA nodes so notice logged
			expNotice: true,
		},
		"2 engines; hugepage alloc across numa 0,1": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(pmemEngine(0), pmemEngine(1))
			},
			memInfo1: &common.SysMemInfo{
				MemInfo: common.MemInfo{
					MemTotalKiB:     (50 * humanize.GiByte) / humanize.KiByte,
					HugepageSizeKiB: 2048,
				},
				NumaNodes: []common.MemInfo{
					{NumaNodeIndex: 0},
					{NumaNodeIndex: 1},
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				defCleanDualEngine,
				{
					HugeNodes:  "nodes_hp[0]=8192,nodes_hp[1]=8192",
					TargetUser: username,
					PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
						storage.BdevPciAddrSep, test.MockPCIAddr(1)),
					EnableVMD: true,
				},
			},
			memInfo2: &common.SysMemInfo{
				MemInfo: common.MemInfo{
					MemTotalKiB:     (50 * humanize.GiByte) / humanize.KiByte,
					HugepageSizeKiB: 2048,
					HugepagesTotal:  16386,
					HugepagesFree:   16384,
				},
				NumaNodes: []common.MemInfo{
					{
						NumaNodeIndex:  0,
						HugepagesTotal: 8192,
						HugepagesFree:  8192,
					},
					{
						NumaNodeIndex:  1,
						HugepagesTotal: 8192,
						HugepagesFree:  8192,
					},
				},
			},
			// (8192 hugepages-per-engine calculated) * 2mib size
			expMemSize:      16384,
			expHugepageSize: 2,
		},
		"2 engines; hugepage alloc across numa 0,1; sufficient existing pages": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(pmemEngine(0), pmemEngine(1))
			},
			memInfo1: &common.SysMemInfo{
				MemInfo: common.MemInfo{
					MemTotalKiB:     (50 * humanize.GiByte) / humanize.KiByte,
					HugepageSizeKiB: 2048,
					HugepagesFree:   16386,
					HugepagesTotal:  16386,
				},
				NumaNodes: []common.MemInfo{
					{
						NumaNodeIndex:  0,
						HugepagesTotal: 8193,
						HugepagesFree:  8193,
					},
					{
						NumaNodeIndex:  1,
						HugepagesTotal: 8193,
						HugepagesFree:  8193,
					},
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				defCleanDualEngine,
				{
					HugeNodes:  "nodes_hp[0]=8193,nodes_hp[1]=8193",
					TargetUser: username,
					PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
						storage.BdevPciAddrSep, test.MockPCIAddr(1)),
					EnableVMD: true,
				},
			},
			// (8192 hugepages-per-engine calculated) * 2mib size
			expMemSize:      16384,
			expHugepageSize: 2,
		},
		"2 engines; hugepage alloc across numa 0,1; existing pages on numa-0": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(pmemEngine(0), pmemEngine(1))
			},
			memInfo1: &common.SysMemInfo{
				MemInfo: common.MemInfo{
					MemTotalKiB:     (50 * humanize.GiByte) / humanize.KiByte,
					HugepageSizeKiB: 2048,
					HugepagesFree:   16386,
					HugepagesTotal:  16386,
				},
				NumaNodes: []common.MemInfo{
					{
						NumaNodeIndex:  0,
						HugepagesTotal: 8393, // Sufficient.
						HugepagesFree:  8393,
					},
					{
						NumaNodeIndex:  1,
						HugepagesTotal: 7993, // Insufficient.
						HugepagesFree:  7993,
					},
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				defCleanDualEngine,
				{
					HugeNodes:  "nodes_hp[0]=8393,nodes_hp[1]=8192",
					TargetUser: username,
					PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
						storage.BdevPciAddrSep, test.MockPCIAddr(1)),
					EnableVMD: true,
				},
			},
			memInfo2: &common.SysMemInfo{
				MemInfo: common.MemInfo{
					MemTotalKiB:     (50 * humanize.GiByte) / humanize.KiByte,
					HugepageSizeKiB: 2048,
					HugepagesFree:   16386,
					HugepagesTotal:  16386,
				},
				NumaNodes: []common.MemInfo{
					{
						NumaNodeIndex:  0,
						HugepagesTotal: 8393,
						HugepagesFree:  8393,
					},
					{
						NumaNodeIndex:  1,
						HugepagesTotal: 8192,
						HugepagesFree:  8192,
					},
				},
			},
			// (8192 hugepages-per-engine calculated) * 2mib size
			expMemSize:      16384,
			expHugepageSize: 2,
		},
		"2 engines; hugepage alloc across numa 0,1; missing per-numa info": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(16384).
					WithEngines(pmemEngine(0), pmemEngine(1))
			},
			memInfo1: &common.SysMemInfo{
				// Missing per-numa meminfo triggers legacy behavior which shrinks
				// allocation.
				MemInfo: common.MemInfo{
					MemTotalKiB:     (50 * humanize.GiByte) / humanize.KiByte,
					HugepageSizeKiB: 2048,
					HugepagesFree:   16386,
					HugepagesTotal:  16386,
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				defCleanDualEngine,
				{
					HugeNodes:  "nodes_hp[0]=8192,nodes_hp[1]=8192",
					TargetUser: username,
					PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
						storage.BdevPciAddrSep, test.MockPCIAddr(1)),
					EnableVMD: true,
				},
			},
			memInfo2: &common.SysMemInfo{
				MemInfo: common.MemInfo{
					MemTotalKiB:     (50 * humanize.GiByte) / humanize.KiByte,
					HugepageSizeKiB: 2048,
					HugepagesFree:   16384, // Allocation shrunk.
					HugepagesTotal:  16384,
				},
			},
			// (8192 hugepages-per-engine calculated) * 2mib size
			expMemSize:      16384,
			expHugepageSize: 2,
		},
		"1 engine; hugepage alloc on numa 0; insufficient free": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(pmemEngine(0))
			},
			hugepagesTotal: 8192,
			hugepagesFree:  8180,
			expPrepCalls: []storage.BdevPrepareRequest{
				defCleanSingleEngine,
				{
					HugeNodes:    "nodes_hp[0]=8192",
					TargetUser:   username,
					PCIAllowList: test.MockPCIAddr(0),
					EnableVMD:    true,
				},
			},
			// mem_size engine parameter is hower "hugepagesFree" value
			expMemSize:      16360,
			expHugepageSize: 2,
			// No error returned, notice logged only, engine-side mem threshold
			// validation instead.
			expNotice: true,
		},
		"2 engines; scm only; nr_hugepages unset": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(pmemOnlyEngine(0), pmemOnlyEngine(1))
			},
			hugepagesTotal: 128,
			hugepagesFree:  128,
			expPrepCalls: []storage.BdevPrepareRequest{
				defClean,
				{
					TargetUser: username,
					EnableVMD:  true,
					HugeNodes:  "nodes_hp[0]=128",
				},
			},
		},
		"0 engines; nr_hugepages unset": {
			hugepagesTotal: 128,
			hugepagesFree:  128,
			expPrepCalls: []storage.BdevPrepareRequest{
				defClean,
				{
					TargetUser: username,
					EnableVMD:  true,
					HugeNodes:  "nodes_hp[0]=128",
				},
			},
		},
		// prepare will continue even if reset fails
		"reset fails": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(pmemEngine(0), pmemEngine(1)).
					WithBdevExclude(test.MockPCIAddr(3))
			},
			hugepagesFree:  16384,
			hugepagesTotal: 16384,
			bmbc: &bdev.MockBackendConfig{
				ResetErr: errors.New("backed prep reset failed"),
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				defCleanDualEngine,
				{
					TargetUser: username,
					PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
						storage.BdevPciAddrSep, test.MockPCIAddr(1)),
					PCIBlockList: test.MockPCIAddr(3),
					EnableVMD:    true,
					HugeNodes:    "nodes_hp[0]=8192,nodes_hp[1]=8192",
				},
			},
			expMemSize:      16384, // (16384 hugepages / 2 engines) * 2mib size
			expHugepageSize: 2,
		},
		// VMD not enabled in prepare request.
		"vmd disabled": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(16384).
					WithEngines(pmemEngine(0), pmemEngine(1)).
					WithDisableVMD(true)
			},
			hugepagesFree:  16384,
			hugepagesTotal: 16384,
			expPrepCalls: []storage.BdevPrepareRequest{
				defCleanDualEngine,
				{
					TargetUser: username,
					PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
						storage.BdevPciAddrSep, test.MockPCIAddr(1)),
					HugeNodes: "nodes_hp[0]=8192,nodes_hp[1]=8192",
				},
			},
			expMemSize:      16384, // (16384 hugepages / 2 engines) * 2mib size
			expHugepageSize: 2,
		},
		// VMD not enabled in prepare request.
		"non-nvme bdevs; vmd enabled": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(8192).
					WithEngines(pmemFakeNvmeEngine(0))
			},
			hugepagesFree:  8192,
			hugepagesTotal: 8192,
			expPrepCalls: []storage.BdevPrepareRequest{
				defClean,
				{
					TargetUser: username,
					HugeNodes:  "nodes_hp[0]=8192",
				},
			},
			expMemSize:      16384, // 8192 hugepages * 2mib size
			expHugepageSize: 2,
		},
		"4 engines; hugepage alloc across numa 0,1": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(16384).WithEngines(
					engine.MockConfig().WithFabricInterfacePort(20000).
						WithPinnedNumaNode(0).WithFabricInterface("ib0").
						WithTargetCount(8).WithStorage(pmemTier(0), nvmeTier(0)),
					engine.MockConfig().WithFabricInterfacePort(21000).
						WithPinnedNumaNode(0).WithFabricInterface("ib0").
						WithTargetCount(8).WithStorage(pmemTier(1), nvmeTier(1)),
					engine.MockConfig().WithFabricInterfacePort(20000).
						WithPinnedNumaNode(1).WithFabricInterface("ib1").
						WithTargetCount(8).WithStorage(pmemTier(2), nvmeTier(2)),
					engine.MockConfig().WithFabricInterfacePort(21000).
						WithPinnedNumaNode(1).WithFabricInterface("ib1").
						WithTargetCount(8).WithStorage(pmemTier(3), nvmeTier(3)),
				)
			},
			hugepagesFree:  16384,
			hugepagesTotal: 16384,
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages: true,
					CleanSpdkLockfiles: true,
					PCIAllowList: strings.Join([]string{
						test.MockPCIAddr(0), test.MockPCIAddr(1), test.MockPCIAddr(2),
						test.MockPCIAddr(3),
					}, storage.BdevPciAddrSep),
				},
				{
					TargetUser: username,
					PCIAllowList: strings.Join(test.MockPCIAddrs(0, 1, 2, 3),
						storage.BdevPciAddrSep),
					EnableVMD: true,
					HugeNodes: "nodes_hp[0]=8192,nodes_hp[1]=8192",
				},
			},
			expMemSize:      8192, // 16384 pages * 2mib divided by 4 engines
			expHugepageSize: 2,
		},
		"4 engines; uneven numa distribution": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(16384).WithEngines(
					engine.MockConfig().WithFabricInterfacePort(20000).
						WithPinnedNumaNode(0).WithFabricInterface("ib0").
						WithTargetCount(8).WithStorage(pmemTier(0), nvmeTier(0)),
					engine.MockConfig().WithFabricInterfacePort(21000).
						WithPinnedNumaNode(0).WithFabricInterface("ib0").
						WithTargetCount(8).WithStorage(pmemTier(1), nvmeTier(1)),
					engine.MockConfig().WithFabricInterfacePort(22000).
						WithPinnedNumaNode(0).WithFabricInterface("ib0").
						WithTargetCount(8).WithStorage(pmemTier(2), nvmeTier(2)),
					engine.MockConfig().WithFabricInterfacePort(20000).
						WithPinnedNumaNode(1).WithFabricInterface("ib1").
						WithTargetCount(8).WithStorage(pmemTier(3), nvmeTier(3)),
				)
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages: true,
					CleanSpdkLockfiles: true,
					PCIAllowList: strings.Join([]string{
						test.MockPCIAddr(0), test.MockPCIAddr(1), test.MockPCIAddr(2),
						test.MockPCIAddr(3),
					}, storage.BdevPciAddrSep),
				},
			},
			expPrepErr: errors.New("uneven distribution"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer().
				WithFabricProvider("ofi+verbs").
				WithMgmtSvcReplicas("foo", "bar", "baz") // Suppress redundancy NOTICE log msg
			if tc.srvCfgExtra != nil {
				cfg = tc.srvCfgExtra(cfg)
			}

			mockAffSrc := func(l logging.Logger, e *engine.Config) (uint, error) {
				iface := e.Fabric.Interface
				l.Debugf("eval affinity of iface %q", iface)
				switch iface {
				case "ib0":
					return 0, nil
				case "ib1":
					return 1, nil
				}
				return 0, errors.Errorf("unrecognized fabric interface: %s", iface)
			}

			// Populate typical meminfo values.
			if tc.memInfo1 == nil {
				tc.memInfo1 = &common.SysMemInfo{}
				tc.memInfo1.MemTotalKiB = (50 * humanize.GiByte) / humanize.KiByte
				tc.memInfo1.HugepageSizeKiB = 2048
				tc.memInfo1.HugepagesFree = tc.hugepagesFree
				tc.memInfo1.HugepagesTotal = tc.hugepagesTotal
				if len(cfg.Engines) > 0 {
					tc.memInfo1.NumaNodes = []common.MemInfo{
						{
							NumaNodeIndex:  0,
							HugepagesTotal: tc.hugepagesTotal / len(cfg.Engines),
							HugepagesFree:  tc.hugepagesFree / len(cfg.Engines),
						},
						{
							NumaNodeIndex:  1,
							HugepagesTotal: tc.hugepagesTotal / len(cfg.Engines),
							HugepagesFree:  tc.hugepagesFree / len(cfg.Engines),
						},
					}
				}
			} else if tc.hugepagesFree != 0 || tc.hugepagesTotal != 0 {
				t.Fatal("incorrect test parameters")
			}
			if tc.memInfo2 == nil {
				tc.memInfo2 = tc.memInfo1
			}

			osSetenv = func(string, string) error {
				return nil
			}
			// return function variable to default after test
			defer func() {
				osSetenv = os.Setenv
			}()

			mockIfLookup := func(string) (netInterface, error) {
				return &mockInterface{
					addrs: []net.Addr{
						&mockAddr{},
					},
				}, nil
			}

			if err = processConfig(log, cfg, mockFabIfSet, tc.memInfo1, mockIfLookup,
				mockAffSrc); err != nil {
				t.Fatal(err)
			}

			srv, err := newServer(log, cfg, &system.FaultDomain{})
			if err != nil {
				t.Fatal(err)
			}

			mbb := bdev.NewMockBackend(tc.bmbc)
			mbp := bdev.NewProvider(log, mbb)
			sp := sysprov.NewMockSysProvider(log, nil)

			srv.ctlSvc = &ControlService{
				StorageControlService: *NewMockStorageControlService(log, cfg.Engines,
					sp, scm.NewProvider(log, scm.NewMockBackend(nil), sp, nil),
					mbp, nil),
				srvCfg: cfg,
			}

			if tc.overrideUser != "" {
				srv.runningUser = &user.User{Username: tc.overrideUser}
			}

			gotPrepErr := prepBdevStorage(srv, !tc.iommuDisabled, tc.memInfo1)

			mbb.RLock()
			if diff := cmp.Diff(tc.expPrepCalls, mbb.PrepareCalls, prepCmpOpt); diff != "" {
				t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
			}
			if len(mbb.ResetCalls) != 0 {
				t.Fatalf("unexpected number of reset calls, want 0 got %d",
					len(mbb.ResetCalls))
			}
			mbb.RUnlock()

			test.CmpErr(t, tc.expPrepErr, gotPrepErr)
			if tc.expPrepErr != nil {
				return
			}

			if len(srv.cfg.Engines) == 0 {
				return
			}

			runner := engine.NewRunner(log, srv.cfg.Engines[0])
			ei := NewEngineInstance(log, srv.ctlSvc.storage, nil, runner, nil)

			setEngineMemSize(srv, ei, tc.memInfo2)

			test.AssertEqual(t, tc.expMemSize, ei.runner.GetConfig().MemSize,
				"unexpected memory size")
			test.AssertEqual(t, tc.expHugepageSize, ei.runner.GetConfig().HugepageSz,
				"unexpected huge page size")
			txtMod := ""
			if !tc.expNotice {
				txtMod = "not "
			}
			msg := fmt.Sprintf("expected NOTICE level message to %shave been logged",
				txtMod)
			test.AssertEqual(t, tc.expNotice, strings.Contains(buf.String(), "NOTICE"),
				msg)
		})
	}
}

func TestServer_cleanEngineSpdkResources(t *testing.T) {
	for name, tc := range map[string]struct {
		srvCfgExtra func(*config.Server) *config.Server
		bmbc        *bdev.MockBackendConfig
		expErr      error
		expPrepCall *storage.BdevPrepareRequest
	}{
		"bdevs configured; hugepages disabled": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithDisableHugepages(true).
					WithEngines(pmemEngine(1))
			},
			// Returns early so no prep call.
		},
		"no bdevs configured": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(pmemOnlyEngine(1))
			},
			bmbc: &bdev.MockBackendConfig{},
			expPrepCall: &storage.BdevPrepareRequest{
				CleanSpdkHugepages: true,
				CleanSpdkLockfiles: true,
			},
		},
		"bdev resources cleaned": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(pmemEngine(1))
			},
			bmbc: &bdev.MockBackendConfig{},
			expPrepCall: &storage.BdevPrepareRequest{
				CleanSpdkHugepages: true,
				CleanSpdkLockfiles: true,
				PCIAllowList:       test.MockPCIAddr(1),
			},
		},
		"bdev resources cleaned; multiple ssds": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(
					basicEngineCfg(1).WithTargetCount(16).
						WithStorage(pmemTier(1),
							storage.NewTierConfig().
								WithStorageClass(storage.ClassNvme.String()).
								WithBdevDeviceList(
									test.MockPCIAddr(int32(1)),
									test.MockPCIAddr(int32(2)))))
			},
			bmbc: &bdev.MockBackendConfig{},
			expPrepCall: &storage.BdevPrepareRequest{
				CleanSpdkHugepages: true,
				CleanSpdkLockfiles: true,
				PCIAllowList: strings.Join([]string{
					test.MockPCIAddr(1), test.MockPCIAddr(2),
				}, storage.BdevPciAddrSep),
			},
		},
		"bdev resources clean fails": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(pmemEngine(1))
			},
			bmbc: &bdev.MockBackendConfig{
				ResetErr: errors.New("backed prep reset failed"),
			},
			expPrepCall: &storage.BdevPrepareRequest{
				CleanSpdkHugepages: true,
				CleanSpdkLockfiles: true,
				PCIAllowList:       test.MockPCIAddr(1),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer().
				WithFabricProvider("ofi+verbs")
			if tc.srvCfgExtra != nil {
				cfg = tc.srvCfgExtra(cfg)
			}

			srv, err := newServer(log, cfg, &system.FaultDomain{})
			if err != nil {
				t.Fatal(err)
			}

			mbb := bdev.NewMockBackend(tc.bmbc)
			mbp := bdev.NewProvider(log, mbb)
			sp := sysprov.NewMockSysProvider(log, nil)

			srv.ctlSvc = &ControlService{
				StorageControlService: *NewMockStorageControlService(log, nil,
					sp, scm.NewProvider(log, scm.NewMockBackend(nil), sp, nil),
					mbp, nil),
				srvCfg: cfg,
			}

			if len(srv.cfg.Engines) == 0 {
				t.Fatal("zero engines configured")
			}

			runner := engine.NewRunner(log, srv.cfg.Engines[0])
			ei := NewEngineInstance(log, srv.ctlSvc.storage, nil, runner, nil)
			storageCfg := ei.runner.GetConfig().Storage
			pciAddrs := storageCfg.Tiers.NVMeBdevs().Devices()

			test.CmpErr(t, tc.expErr, cleanSpdkResources(srv, pciAddrs))

			mbb.RLock()
			if tc.expPrepCall != nil {
				if len(mbb.PrepareCalls) != 1 {
					t.Fatalf("expected prepare to be called once")
				}
				if diff := cmp.Diff(*tc.expPrepCall, mbb.PrepareCalls[0]); diff != "" {
					t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
				}
			} else if len(mbb.PrepareCalls) != 0 {
				t.Fatalf("expected prepare not to be called")
			}
			mbb.RUnlock()
		})
	}
}

func TestServer_checkEngineTmpfsMem(t *testing.T) {
	for name, tc := range map[string]struct {
		srvCfgExtra  func(*config.Server) *config.Server
		memAvailGiB  int
		tmpfsMounted bool
		tmpfsSize    uint64
		expErr       error
	}{
		"pmem tier; skip check": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(pmemEngine(0)).
					WithBdevExclude(test.MockPCIAddr(1))
			},
		},
		"single engine; ram tier; perform check": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(ramEngine(0, 10)).
					WithBdevExclude(test.MockPCIAddr(1))
			},
			memAvailGiB: 9,
		},
		"dual engine; ram tier; perform check; low mem": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(ramEngine(0, 10), ramEngine(1, 10)).
					WithBdevExclude(test.MockPCIAddr(1))
			},
			// Only accounts for single engine
			memAvailGiB: 8,
			expErr: storage.FaultRamdiskLowMem("Available", 10*humanize.GiByte,
				9*humanize.GiByte, 8*humanize.GiByte),
		},
		"tmpfs already mounted; more than calculated": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(ramEngine(0, 10))
			},
			tmpfsMounted: true,
			tmpfsSize:    11,
			expErr:       errors.New("ramdisk size"),
		},
		"tmpfs already mounted; less than calculated": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(ramEngine(0, 10))
			},
			tmpfsMounted: true,
			tmpfsSize:    9,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer().WithFabricProvider("ofi+verbs")
			cfg = tc.srvCfgExtra(cfg)

			smi := &common.SysMemInfo{}
			smi.HugepageSizeKiB = 2048
			smi.MemAvailableKiB = (humanize.GiByte * tc.memAvailGiB) / humanize.KiByte

			if len(cfg.Engines) == 0 {
				t.Fatal("test expects at least one engine in config")
			}

			ec := cfg.Engines[0]
			runner := engine.NewRunner(log, ec)
			sysMockCfg := &sysprov.MockSysConfig{
				IsMountedBool: tc.tmpfsMounted,
			}
			if tc.tmpfsMounted {
				sysMockCfg.GetfsUsageResps = []sysprov.GetfsUsageRetval{
					{
						Total: tc.tmpfsSize * humanize.GiByte,
					},
				}
			}
			sysMock := sysprov.NewMockSysProvider(log, sysMockCfg)
			scmMock := &storage.MockScmProvider{}
			provider := storage.MockProvider(log, 0, &ec.Storage, sysMock, scmMock, nil, nil)
			instance := NewEngineInstance(log, provider, nil, runner, nil)

			srv, err := newServer(log, cfg, &system.FaultDomain{})
			if err != nil {
				t.Fatal(err)
			}

			gotErr := checkEngineTmpfsMem(srv, instance, smi)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func testFabricProviderSet(prov ...string) *hardware.FabricProviderSet {
	providers := []*hardware.FabricProvider{}
	for _, p := range prov {
		providers = append(providers, &hardware.FabricProvider{
			Name: p,
		})
	}
	return hardware.NewFabricProviderSet(providers...)
}

func TestServer_getNetDevClass(t *testing.T) {
	configA := func() *engine.Config {
		return engine.MockConfig().
			WithLogFile("a").
			WithStorage(
				storage.NewTierConfig().
					WithStorageClass("ram").
					WithScmRamdiskSize(1).
					WithScmMountPoint("a"),
			).
			WithFabricInterfacePort(42)
	}
	configB := func() *engine.Config {
		return engine.MockConfig().
			WithLogFile("b").
			WithStorage(
				storage.NewTierConfig().
					WithStorageClass("ram").
					WithScmRamdiskSize(1).
					WithScmMountPoint("b"),
			).
			WithFabricInterfacePort(43)
	}

	for name, tc := range map[string]struct {
		configA      *engine.Config
		configB      *engine.Config
		expNetDevCls []hardware.NetDevClass
		expErr       error
	}{
		"provider doesn't match": {
			configA: configA().
				WithFabricProvider("wrong").
				WithFabricInterface("ib1"),
			configB: configB().
				WithFabricProvider("wrong").
				WithFabricInterface("ib0"),
			expErr: errors.New("not supported on network device"),
		},
		"successful validation with matching Infiniband": {
			configA: configA().
				WithFabricProvider("ofi+verbs;ofi_rxm").
				WithFabricInterface("ib1"),
			configB: configB().
				WithFabricProvider("ofi+verbs;ofi_rxm").
				WithFabricInterface("ib0"),
			expNetDevCls: []hardware.NetDevClass{hardware.Infiniband},
		},
		"successful validation with matching Ethernet": {
			configA: configA().
				WithFabricProvider("ofi+tcp").
				WithFabricInterface("eth0"),
			configB: configB().
				WithFabricProvider("ofi+tcp").
				WithFabricInterface("eth1"),
			expNetDevCls: []hardware.NetDevClass{hardware.Ether},
		},
		"multi interface": {
			configA: configA().
				WithFabricProvider("ofi+tcp,ofi+verbs;ofi_rxm").
				WithFabricInterface(strings.Join([]string{"eth0", "ib0"}, engine.MultiProviderSeparator)),
			configB: configB().
				WithFabricProvider("ofi+tcp,ofi+verbs;ofi_rxm").
				WithFabricInterface(strings.Join([]string{"eth1", "ib1"}, engine.MultiProviderSeparator)),
			expNetDevCls: []hardware.NetDevClass{hardware.Ether, hardware.Infiniband},
		},
		"mismatching net dev class with primary server as ib0 / Infiniband": {
			configA: configA().
				WithFabricProvider("ofi+tcp").
				WithFabricInterface("ib0"),
			configB: configB().
				WithFabricProvider("ofi+tcp").
				WithFabricInterface("eth0"),
			expErr: config.FaultConfigInvalidNetDevClass(1, hardware.Infiniband, hardware.Ether, "eth0"),
		},
		"mismatching net dev class with primary server as eth0 / Ethernet": {
			configA: configA().
				WithFabricProvider("ofi+tcp").
				WithFabricInterface("eth0"),
			configB: configB().
				WithFabricProvider("ofi+tcp").
				WithFabricInterface("ib0"),
			expErr: config.FaultConfigInvalidNetDevClass(1, hardware.Ether, hardware.Infiniband, "ib0"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			cfg := config.DefaultServer().
				WithFabricProvider("test").
				WithEngines(tc.configA, tc.configB)

			gotNetDevCls, gotErr := getFabricNetDevClass(cfg, mockFabIfSet)

			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			test.AssertEqual(t, tc.expNetDevCls, gotNetDevCls,
				"unexpected config network device class")
		})
	}
}

type mockReplicaAddrSrc struct {
	replicaAddrResult *net.TCPAddr
	replicaAddrErr    error
}

func (m *mockReplicaAddrSrc) ReplicaAddr() (*net.TCPAddr, error) {
	return m.replicaAddrResult, m.replicaAddrErr
}

func TestServerUtils_resolveFirstAddr(t *testing.T) {
	for name, tc := range map[string]struct {
		host    string
		lookup  ipLookupFn
		expAddr *net.TCPAddr
		expErr  error
	}{
		"host without port": {
			host:   "localhost",
			expErr: errors.New("missing port"),
		},
		"invalid port": {
			host:   "localhost:daos",
			expErr: errors.New("strconv.Atoi"),
		},
		"lookup failure": {
			host:   "localhost:42",
			lookup: func(string) ([]net.IP, error) { return nil, errors.New("lookup") },
			expErr: errors.New("lookup"),
		},
		"no addresses": {
			host:   "localhost:42",
			lookup: func(string) ([]net.IP, error) { return nil, nil },
			expErr: errors.New("no addresses"),
		},
		"localhost resolves to ipv6 & ipv4; prefer ipv4": {
			host: "localhost:10001",
			lookup: func(host string) ([]net.IP, error) {
				return []net.IP{
					net.ParseIP("::1"),
					net.ParseIP("127.0.0.1"),
				}, nil
			},
			expAddr: &net.TCPAddr{IP: net.ParseIP("127.0.0.1"), Port: 10001},
		},
		"localhost resolves to ipv4 & ipv6; prefer ipv4": {
			host: "localhost:10001",
			lookup: func(host string) ([]net.IP, error) {
				return []net.IP{
					net.ParseIP("127.0.0.1"),
					net.ParseIP("::1"),
				}, nil
			},
			expAddr: &net.TCPAddr{IP: net.ParseIP("127.0.0.1"), Port: 10001},
		},
		"host resolves to 2 ipv4; prefer lower": {
			host: "host:10001",
			lookup: func(host string) ([]net.IP, error) {
				return []net.IP{
					net.ParseIP("127.0.0.2"),
					net.ParseIP("127.0.0.1"),
				}, nil
			},
			expAddr: &net.TCPAddr{IP: net.ParseIP("127.0.0.1"), Port: 10001},
		},
		"host resolves to 2 ipv6; prefer lower": {
			host: "host:10001",
			lookup: func(host string) ([]net.IP, error) {
				return []net.IP{
					net.ParseIP("::2"),
					net.ParseIP("::1"),
				}, nil
			},
			expAddr: &net.TCPAddr{IP: net.ParseIP("::1"), Port: 10001},
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotAddr, gotErr := resolveFirstAddr(tc.host, tc.lookup)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if !common.CmpTCPAddr(tc.expAddr, gotAddr) {
				t.Fatalf("unexpected address: want %s, got %s", tc.expAddr, gotAddr)
			}
		})
	}
}

func TestServerUtils_getControlAddr(t *testing.T) {
	testTCPAddr := &net.TCPAddr{
		IP:   net.ParseIP("127.0.0.1"),
		Port: 1234,
	}

	for name, tc := range map[string]struct {
		params  ctlAddrParams
		expAddr *net.TCPAddr
		expErr  error
	}{
		"success (not a replica)": {
			params: ctlAddrParams{
				port: testTCPAddr.Port,
				replicaAddrSrc: &mockReplicaAddrSrc{
					replicaAddrErr: errors.New("not a replica"),
				},
				lookupHost: func(addr string) ([]net.IP, error) {
					test.AssertEqual(t, "0.0.0.0", addr, "")
					return []net.IP{testTCPAddr.IP}, nil
				},
			},
			expAddr: testTCPAddr,
		},
		"success (replica)": {
			params: ctlAddrParams{
				port: testTCPAddr.Port,
				replicaAddrSrc: &mockReplicaAddrSrc{
					replicaAddrResult: testTCPAddr,
				},
				lookupHost: func(addr string) ([]net.IP, error) {
					test.AssertEqual(t, "127.0.0.1", addr, "")
					return []net.IP{testTCPAddr.IP}, nil
				},
			},
			expAddr: testTCPAddr,
		},
		"hostname with multiple IPs resolves to single replica address": {
			params: ctlAddrParams{
				port: testTCPAddr.Port,
				replicaAddrSrc: &mockReplicaAddrSrc{
					replicaAddrResult: testTCPAddr,
				},
				lookupHost: func(addr string) ([]net.IP, error) {
					return []net.IP{
						{testTCPAddr.IP[0], testTCPAddr.IP[1], testTCPAddr.IP[2], testTCPAddr.IP[3] + 1},
						testTCPAddr.IP,
					}, nil
				},
			},
			expAddr: testTCPAddr,
		},
		"resolve fails": {
			params: ctlAddrParams{
				lookupHost: func(addr string) ([]net.IP, error) {
					return nil, errors.New("mock resolve")
				},
			},
			expErr: errors.New("mock resolve"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.params.lookupHost == nil {
				tc.params.lookupHost = func(_ string) ([]net.IP, error) {
					return []net.IP{testTCPAddr.IP}, nil
				}
			}

			if tc.params.replicaAddrSrc == nil {
				tc.params.replicaAddrSrc = &mockReplicaAddrSrc{
					replicaAddrErr: errors.New("not a replica"),
				}
			}

			addr, err := getControlAddr(tc.params)

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expAddr.String(), addr.String(), "")
		})
	}
}

func TestServer_processFabricProvider(t *testing.T) {
	for name, tc := range map[string]struct {
		cfgFabric string
		expFabric string
	}{
		"ofi+verbs": {
			cfgFabric: "ofi+verbs",
			expFabric: "ofi+verbs;ofi_rxm",
		},
		"ofi+verbs;ofi_rxm": {
			cfgFabric: "ofi+verbs;ofi_rxm",
			expFabric: "ofi+verbs;ofi_rxm",
		},
		"ofi+tcp": {
			cfgFabric: "ofi+tcp",
			expFabric: "ofi+tcp",
		},
		"ofi+tcp;ofi_rxm": {
			cfgFabric: "ofi+tcp;ofi_rxm",
			expFabric: "ofi+tcp;ofi_rxm",
		},
		"ucx": {
			cfgFabric: "ucx+ud",
			expFabric: "ucx+ud",
		},
	} {
		t.Run(name, func(t *testing.T) {
			cfg := &config.Server{
				Fabric: engine.FabricConfig{
					Provider: tc.cfgFabric,
				},
			}

			processFabricProvider(cfg)

			test.AssertEqual(t, tc.expFabric, cfg.Fabric.Provider, "")
		})
	}
}

func TestServer_formatBytestring(t *testing.T) {
	bytesIn := "86805309060410000102080100000000040000bc0000000000000000" +
		"000000000000000000000000000000009015a8000000000040000000" +
		"00000000000100000150030008000000000000000000000011601f00" +
		"00200000003000000000000010000200a185001010290900436c4100" +
		"00004300000000000000000000000000000000001f00000000000000" +
		"0e00000003001f000000000000000000000000000000000000000000" +
		"00000000000000000000000000000000000000000000000000000000" +
		"00000000000000000000000000000000000000000000000000000000" +
		"00000000000000000000000000000000000000000000000000000000" +
		"0000000001000115000000000000000030200600"
	expOut := `00: 86 80 53 09 06 04 10 00 01 02 08 01 00 00 00 00
10: 04 00 00 bc 00 00 00 00 00 00 00 00 00 00 00 00
20: 00 00 00 00 00 00 00 00 00 00 00 00 90 15 a8 00
30: 00 00 00 00 40 00 00 00 00 00 00 00 00 01 00 00
40: 01 50 03 00 08 00 00 00 00 00 00 00 00 00 00 00
50: 11 60 1f 00 00 20 00 00 00 30 00 00 00 00 00 00
60: 10 00 02 00 a1 85 00 10 10 29 09 00 43 6c 41 00
70: 00 00 43 00 00 00 00 00 00 00 00 00 00 00 00 00
80: 00 00 00 00 1f 00 00 00 00 00 00 00 0e 00 00 00
90: 03 00 1f 00 00 00 00 00 00 00 00 00 00 00 00 00
a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
100: 01 00 01 15 00 00 00 00 00 00 00 00 30 20 06 00
`

	sb := new(strings.Builder)

	formatBytestring(bytesIn, sb)

	if diff := cmp.Diff(expOut, sb.String()); diff != "" {
		t.Fatalf("unexpected output format (-want, +got):\n%s\n", diff)
	}
}
