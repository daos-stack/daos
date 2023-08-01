//
// (C) Copyright 2021-2023 Intel Corporation.
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

func TestServer_prepBdevStorage(t *testing.T) {
	usrCurrent, err := user.Current()
	if err != nil {
		t.Fatal(err)
	}
	username := usrCurrent.Username
	if username == "root" {
		t.Fatal("prepBdevStorage tests cannot be run as root user")
	}

	for name, tc := range map[string]struct {
		iommuDisabled   bool
		srvCfgExtra     func(*config.Server) *config.Server
		hugepagesFree   int
		bmbc            *bdev.MockBackendConfig
		overrideUser    string
		expPrepErr      error
		expPrepCall     *storage.BdevPrepareRequest
		expMemChkErr    error
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
			overrideUser:  "root",
			hugepagesFree: 8192,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 8194,
				HugeNodes:     "0",
				TargetUser:    "root",
				DisableVFIO:   true,
				PCIAllowList:  test.MockPCIAddr(0),
			},
			expMemSize:      16384,
			expHugepageSize: 2,
		},
		"non-nvme bdevs; vfio disabled": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithDisableVFIO(true).
					WithEngines(pmemFakeNvmeEngine(0))
			},
			hugepagesFree: 8192,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 8194,
				HugeNodes:     "0",
				TargetUser:    username,
				DisableVFIO:   true,
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
			overrideUser:  "root",
			hugepagesFree: 8192,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 8194,
				HugeNodes:     "0",
				TargetUser:    "root",
				PCIAllowList:  test.MockPCIAddr(0),
			},
			expMemSize:      16384,
			expHugepageSize: 2,
		},
		"non-nvme bdevs; iommu disabled": {
			iommuDisabled: true,
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(pmemFakeNvmeEngine(0))
			},
			hugepagesFree: 8192,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 8194,
				HugeNodes:     "0",
				TargetUser:    username,
			},
			expMemSize:      16384,
			expHugepageSize: 2,
		},
		"no bdevs configured; hugepages disabled": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithDisableHugepages(true).
					WithEngines(pmemOnlyEngine(0), pmemOnlyEngine(1))
			},
		},
		"no bdevs configured; nr_hugepages unset": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(0).
					WithEngines(pmemOnlyEngine(0), pmemOnlyEngine(1))
			},
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: scanMinHugepageCount,
				TargetUser:    username,
				EnableVMD:     true,
			},
		},
		"no bdevs configured; nr_hugepages set": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(1024).
					WithEngines(pmemOnlyEngine(0), pmemOnlyEngine(1))
			},
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 1024,
				TargetUser:    username,
				EnableVMD:     true,
			},
		},
		"2 engines both numa 0; hugepage alloc only on numa 0": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(16384).
					WithEngines(pmemEngine(0), pmemEngine(1).WithPinnedNumaNode(0)).
					WithBdevExclude(test.MockPCIAddr(1))
			},
			hugepagesFree: 16384,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 16388, // 2 extra huge pages requested per engine
				HugeNodes:     "0",
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
					storage.BdevPciAddrSep, test.MockPCIAddr(1)),
				PCIBlockList: test.MockPCIAddr(1),
				EnableVMD:    true,
			},
			expMemSize:      16384, // (16384 hugepages / 2 engines) * 2mib size
			expHugepageSize: 2,
			// Not balanced across NUMA nodes so notice logged
			expNotice: true,
		},
		"2 engines both numa 1; hugepage alloc only on numa 1": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(16384).
					WithEngines(pmemEngine(0).WithPinnedNumaNode(1), pmemEngine(1))
			},
			hugepagesFree: 16384,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 16388, // 2 extra huge pages requested per engine
				HugeNodes:     "1",
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
					storage.BdevPciAddrSep, test.MockPCIAddr(1)),
				EnableVMD: true,
			},
			expMemSize:      16384, // (16384 hugepages / 2 engines) * 2mib size
			expHugepageSize: 2,
			// Not balanced across NUMA nodes so notice logged
			expNotice: true,
		},
		"2 engines; hugepage alloc across numa 0,1": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(16384).
					WithEngines(pmemEngine(0), pmemEngine(1))
			},
			hugepagesFree: 16384,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 8194, // 2 extra huge pages requested per engine
				HugeNodes:     "0,1",
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
					storage.BdevPciAddrSep, test.MockPCIAddr(1)),
				EnableVMD: true,
			},
			expMemSize:      16384, // (16384 hugepages / 2 engines) * 2mib size
			expHugepageSize: 2,
		},
		"2 engines; hugepage alloc across numa 0,1; insufficient free": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(16384).
					WithEngines(pmemEngine(0), pmemEngine(1))
			},
			hugepagesFree: 8191,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 8194, // 2 extra huge pages requested per engine
				HugeNodes:     "0,1",
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
					storage.BdevPciAddrSep, test.MockPCIAddr(1)),
				EnableVMD: true,
			},
			// mem_size engine parameter reflects lower "free" value
			expMemSize:      16382, // (16382 hugepages free / 2 engines) * 2mib size
			expHugepageSize: 2,
			// No error returned, notice logged only, engine-side mem threshold
			// validation instead.
			expNotice: true,
		},
		"2 engines; scm only; nr_hugepages unset": {
			hugepagesFree: 128,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 128,
				TargetUser:    username,
				EnableVMD:     true,
			},
		},
		"2 engines; scm only; nr_hugepages unset; insufficient free": {
			hugepagesFree: 0,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 128,
				TargetUser:    username,
				EnableVMD:     true,
			},
			expMemChkErr: errors.New("requested 128 hugepages; got 0"),
		},
		"0 engines; nr_hugepages unset": {
			hugepagesFree: 128,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 128,
				TargetUser:    username,
				EnableVMD:     true,
			},
		},
		"0 engines; nr_hugepages unset; insufficient free": {
			hugepagesFree: 0,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 128,
				TargetUser:    username,
				EnableVMD:     true,
			},
			expMemChkErr: errors.New("requested 128 hugepages; got 0"),
		},
		// prepare will continue even if reset fails
		"reset fails": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugepages(16384).
					WithEngines(pmemEngine(0), pmemEngine(1)).
					WithBdevExclude(test.MockPCIAddr(1))
			},
			hugepagesFree: 16384,
			bmbc: &bdev.MockBackendConfig{
				ResetErr: errors.New("backed prep reset failed"),
			},
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 8194, // hugepages per engine plus 2 extra
				HugeNodes:     "0,1",
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
					storage.BdevPciAddrSep, test.MockPCIAddr(1)),
				PCIBlockList: test.MockPCIAddr(1),
				EnableVMD:    true,
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
			hugepagesFree: 16384,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 8194, // hugepages per engine plus 2 extra
				HugeNodes:     "0,1",
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(0),
					storage.BdevPciAddrSep, test.MockPCIAddr(1)),
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
			hugepagesFree: 8194,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 8194, // hugepages per engine plus 2 extra
				HugeNodes:     "0",
				TargetUser:    username,
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
			hugepagesFree: 16384,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 8196, // hugepages plus 2 extra per engine
				HugeNodes:     "0,1",
				TargetUser:    username,
				PCIAllowList: strings.Join(test.MockPCIAddrs(0, 1, 2, 3),
					storage.BdevPciAddrSep),
				EnableVMD: true,
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
			expPrepErr: errors.New("uneven distribution"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer().
				WithFabricProvider("ofi+verbs").
				WithAccessPoints("foo", "bar", "baz") // Suppress redundancy NOTICE log msg
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

			// test with typical meminfo values
			mi := &common.MemInfo{
				HugepageSizeKiB: 2048,
				MemTotalKiB:     (humanize.GiByte * 50) / humanize.KiByte,
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

			if err = processConfig(log, cfg, mockFabIfSet, mi, mockIfLookup,
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

			gotPrepErr := prepBdevStorage(srv, !tc.iommuDisabled)

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

			test.CmpErr(t, tc.expPrepErr, gotPrepErr)
			if tc.expPrepErr != nil {
				return
			}

			if len(srv.cfg.Engines) == 0 {
				return
			}

			runner := engine.NewRunner(log, srv.cfg.Engines[0])
			ei := NewEngineInstance(log, srv.ctlSvc.storage, nil, runner)

			mi.HugepagesFree = tc.hugepagesFree

			gotMemChkErr := updateHugeMemValues(srv, ei, mi)
			test.CmpErr(t, tc.expMemChkErr, gotMemChkErr)
			if tc.expMemChkErr != nil {
				return
			}

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

func TestServer_checkEngineTmpfsMem(t *testing.T) {
	for name, tc := range map[string]struct {
		srvCfgExtra func(*config.Server) *config.Server
		memAvailGiB int
		expErr      error
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
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer().WithFabricProvider("ofi+verbs")
			cfg = tc.srvCfgExtra(cfg)

			mi := &common.MemInfo{
				HugepageSizeKiB: 2048,
				MemAvailableKiB: (humanize.GiByte * tc.memAvailGiB) / humanize.KiByte,
			}

			if len(cfg.Engines) == 0 {
				t.Fatal("test expects at least one engine in config")
			}

			ec := cfg.Engines[0]
			runner := engine.NewRunner(log, ec)
			provider := storage.MockProvider(log, 0, &ec.Storage, nil, nil, nil, nil)
			instance := NewEngineInstance(log, provider, nil, runner)

			srv, err := newServer(log, cfg, &system.FaultDomain{})
			if err != nil {
				t.Fatal(err)
			}

			gotErr := checkEngineTmpfsMem(srv, instance, mi)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

// TestServer_scanBdevStorage validates that an error is returned in the case that a SSD is not
// found and doesn't return an error if SPDK fails to init.
func TestServer_scanBdevStorage(t *testing.T) {
	for name, tc := range map[string]struct {
		disableHugepages bool
		bmbc             *bdev.MockBackendConfig
		expErr           error
	}{
		"spdk fails init": {
			bmbc: &bdev.MockBackendConfig{
				ScanErr: errors.New("spdk failed"),
			},
			expErr: errors.New("spdk failed"),
		},
		"bdev in config not found by spdk": {
			bmbc: &bdev.MockBackendConfig{
				ScanErr: storage.FaultBdevNotFound(test.MockPCIAddr()),
			},
			expErr: storage.FaultBdevNotFound(test.MockPCIAddr()),
		},
		"successful scan": {
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.MockNvmeControllers(1),
				},
			},
		},
		"hugepages disabled": {
			disableHugepages: true,
			bmbc: &bdev.MockBackendConfig{
				ScanErr: errors.New("spdk failed"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer().WithFabricProvider("ofi+verbs").
				WithDisableHugepages(tc.disableHugepages)

			if err := cfg.Validate(log); err != nil {
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
					sp,
					scm.NewProvider(log, scm.NewMockBackend(nil), sp, nil),
					mbp, nil),
				srvCfg: cfg,
			}

			_, gotErr := scanBdevStorage(srv)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestServer_setEngineBdevs(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg              engine.Config
		engineIdx        uint32
		scanResp         *storage.BdevScanResponse
		lastEngineIdx    int
		lastBdevCount    int
		expErr           error
		expLastEngineIdx int
		expLastBdevCount int
	}{
		"nil input": {
			expErr: errors.New("nil input param: scanResp"),
		},
		"empty cache": {
			scanResp:      &storage.BdevScanResponse{},
			lastEngineIdx: -1,
			lastBdevCount: -1,
		},
		"index unset; bdev count set": {
			scanResp:      &storage.BdevScanResponse{},
			lastEngineIdx: -1,
			lastBdevCount: 0,
			expErr:        errors.New("to be unset"),
		},
		"index set; bdev count unset": {
			scanResp:      &storage.BdevScanResponse{},
			lastEngineIdx: 0,
			lastBdevCount: -1,
			expErr:        errors.New("to be set"),
		},
		"empty cache; counts match": {
			engineIdx:        1,
			scanResp:         &storage.BdevScanResponse{},
			lastEngineIdx:    0,
			lastBdevCount:    0,
			expLastEngineIdx: 1,
		},
		"empty cache; count mismatch": {
			engineIdx:     1,
			scanResp:      &storage.BdevScanResponse{},
			lastEngineIdx: 0,
			lastBdevCount: 1,
			expErr:        errors.New("engine 1 has 0 but engine 0 has 1"),
		},
		"populated cache; cache miss": {
			engineIdx:     1,
			scanResp:      &storage.BdevScanResponse{Controllers: storage.MockNvmeControllers(1)},
			lastEngineIdx: 0,
			lastBdevCount: 1,
			expErr:        errors.New("engine 1 has 0 but engine 0 has 1"),
		},
		"populated cache; cache hit": {
			cfg: *engine.MockConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("nvme").
						WithBdevDeviceList("0000:00:00.0"),
				),
			engineIdx:        1,
			scanResp:         &storage.BdevScanResponse{Controllers: storage.MockNvmeControllers(1)},
			lastEngineIdx:    0,
			lastBdevCount:    1,
			expLastEngineIdx: 1,
			expLastBdevCount: 1,
		},
		"populated cache; multiple vmd backing devices": {
			cfg: *engine.MockConfig().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("nvme").
						WithBdevDeviceList("0000:05:05.5", "0000:5d:05.5"),
				),
			engineIdx: 1,
			scanResp: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{
					&storage.NvmeController{PciAddr: "5d0505:01:00.0"},
					&storage.NvmeController{PciAddr: "5d0505:03:00.0"},
					&storage.NvmeController{PciAddr: "050505:01:00.0"},
					&storage.NvmeController{PciAddr: "050505:02:00.0"},
				},
			},
			lastEngineIdx:    0,
			lastBdevCount:    4,
			expLastEngineIdx: 1,
			expLastBdevCount: 4,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			engine := NewEngineInstance(log,
				storage.DefaultProvider(log, int(tc.engineIdx), &tc.cfg.Storage),
				nil, engine.NewRunner(log, &tc.cfg))
			engine.setIndex(tc.engineIdx)

			gotErr := setEngineBdevs(engine, tc.scanResp, &tc.lastEngineIdx, &tc.lastBdevCount)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expLastEngineIdx, tc.lastEngineIdx, "unexpected last engine index")
			test.AssertEqual(t, tc.expLastBdevCount, tc.lastBdevCount, "unexpected last bdev count")
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
		expNetDevCls hardware.NetDevClass
		expErr       error
	}{
		"successful validation with matching Infiniband": {
			configA: configA().
				WithFabricInterface("ib1"),
			configB: configB().
				WithFabricInterface("ib0"),
			expNetDevCls: hardware.Infiniband,
		},
		"successful validation with matching Ethernet": {
			configA: configA().
				WithFabricInterface("eth0"),
			configB: configB().
				WithFabricInterface("eth1"),
			expNetDevCls: hardware.Ether,
		},
		"mismatching net dev class with primary server as ib0 / Infiniband": {
			configA: configA().
				WithFabricInterface("ib0"),
			configB: configB().
				WithFabricInterface("eth0"),
			expErr: config.FaultConfigInvalidNetDevClass(1, hardware.Infiniband, hardware.Ether, "eth0"),
		},
		"mismatching net dev class with primary server as eth0 / Ethernet": {
			configA: configA().
				WithFabricInterface("eth0"),
			configB: configB().
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
