//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"fmt"
	"net"
	"os/user"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
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

			common.CmpErr(t, tc.expErr, err)
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
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			common.AssertEqual(t, tc.expSetting, gotSetting, "unexpected SRX setting")
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
		t.Log("Skip prepBdevStorage tests when run with root user")
		return
	}

	// basic engine configs populated enough to complete validation
	basicEngineCfg := func(i int) *engine.Config {
		return engine.MockConfig().WithFabricInterfacePort(20000).
			WithPinnedNumaNode(uint(i)).WithFabricInterface(fmt.Sprintf("ib%d", i))
	}
	scmTier := func(i int) *storage.TierConfig {
		return storage.NewTierConfig().WithStorageClass(storage.ClassDcpm.String()).
			WithScmMountPoint(fmt.Sprintf("/mnt/daos%d", i)).
			WithScmDeviceList(fmt.Sprintf("/dev/pmem%d", i))
	}
	nvmeTier := func(i int) *storage.TierConfig {
		return storage.NewTierConfig().WithStorageClass(storage.ClassNvme.String()).
			WithBdevDeviceList(common.MockPCIAddr(int32(i)))
	}
	scmEngine := func(i int) *engine.Config {
		return basicEngineCfg(i).WithStorage(scmTier(i)).WithTargetCount(8)
	}
	nvmeEngine := func(i int) *engine.Config {
		return basicEngineCfg(i).WithStorage(scmTier(i), nvmeTier(i)).WithTargetCount(16)
	}

	for name, tc := range map[string]struct {
		iommuDisabled   bool
		srvCfgExtra     func(*config.Server) *config.Server
		getHpiErr       error
		hugePagesFree   int
		bmbc            *bdev.MockBackendConfig
		expPrepErr      error
		expPrepCalls    []storage.BdevPrepareRequest
		expResetCalls   []storage.BdevPrepareRequest
		expMemChkErr    error
		expMemSize      int
		expHugePageSize int
	}{
		"vfio disabled": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithDisableVFIO(true).
					WithEngines(nvmeEngine(0), nvmeEngine(1))
			},
			expPrepErr: FaultVfioDisabled,
		},
		"iommu disabled": {
			iommuDisabled: true,
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(nvmeEngine(0), nvmeEngine(1))
			},
			expPrepErr: FaultIommuDisabled,
		},
		"no bdevs configured; -1 hugepages requested": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(-1).
					WithEngines(scmEngine(0), scmEngine(1))
			},
		},
		"no bdevs configured; nr_hugepages unset": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(0).
					WithEngines(scmEngine(0), scmEngine(1))
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:     true,
					TargetUser: username,
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: scanMinHugePageCount,
					TargetUser:    username,
				},
			},
		},
		"no bdevs configured; nr_hugepages set": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(1024).
					WithEngines(scmEngine(0), scmEngine(1))
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:     true,
					TargetUser: username,
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: 1024,
					TargetUser:    username,
				},
			},
		},
		"2 engines both numa 0; hugepage alloc only on numa 0": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(16384).
					WithEngines(nvmeEngine(0), nvmeEngine(1).WithPinnedNumaNode(0)).
					WithBdevInclude(common.MockPCIAddr(1), common.MockPCIAddr(2)).
					WithBdevExclude(common.MockPCIAddr(1))
			},
			hugePagesFree: 16384,
			// with reset set to false in request, two calls will be made to
			// bdev backend prepare, one to reset device bindings and hugepage
			// allocations and another to set them as requested
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:     true,
					TargetUser: username,
					PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PCIBlockList: common.MockPCIAddr(1),
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: 16386, // 2 extra huge pages requested
					HugeNodes:     "0",
					TargetUser:    username,
					PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PCIBlockList: common.MockPCIAddr(1),
				},
			},
			expMemSize:      16384,
			expHugePageSize: 2,
		},
		"2 engines both numa 1; hugepage alloc only on numa 1": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(16384).
					WithEngines(nvmeEngine(0).WithPinnedNumaNode(1), nvmeEngine(1))
			},
			hugePagesFree: 16384,
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:     true,
					TargetUser: username,
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: 16386,
					HugeNodes:     "1",
					TargetUser:    username,
				},
			},
			expMemSize:      16384,
			expHugePageSize: 2,
		},
		"2 engines; hugepage alloc across numa 0,1": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(16384).
					WithEngines(nvmeEngine(0), nvmeEngine(1))
			},
			hugePagesFree: 16384,
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:     true,
					TargetUser: username,
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: 8194, // 2 extra huge pages requested per-engine
					HugeNodes:     "0,1",
					TargetUser:    username,
				},
			},
			expMemSize:      16384,
			expHugePageSize: 2,
		},
		"2 engines; hugepage alloc across numa 0,1; insufficient free": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(16384).
					WithEngines(nvmeEngine(0), nvmeEngine(1))
			},
			hugePagesFree: 8191,
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:     true,
					TargetUser: username,
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: 8194,
					HugeNodes:     "0,1",
					TargetUser:    username,
				},
			},
			expMemChkErr: errors.New("0: want 16 GiB (8192 hugepages), got 16 GiB (8191"),
		},
		"2 engines; scm only; nr_hugepages unset": {
			hugePagesFree: 128,
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:     true,
					TargetUser: username,
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: 128,
					TargetUser:    username,
				},
			},
		},
		"2 engines; scm only; nr_hugepages unset; insufficient free": {
			hugePagesFree: 0,
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:     true,
					TargetUser: username,
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: 128,
					TargetUser:    username,
				},
			},
			expMemChkErr: errors.New("requested 128 hugepages; got 0"),
		},
		"0 engines; nr_hugepages unset": {
			hugePagesFree: 128,
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:     true,
					TargetUser: username,
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: 128,
					TargetUser:    username,
				},
			},
		},
		"0 engines; nr_hugepages unset; insufficient free": {
			hugePagesFree: 0,
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:     true,
					TargetUser: username,
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: 128,
					TargetUser:    username,
				},
			},
			expMemChkErr: errors.New("requested 128 hugepages; got 0"),
		},
		"0 engines; nr_hugepages unset; hpi fetch fails": {
			getHpiErr: errors.New("could not find hugepage info"),
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:     true,
					TargetUser: username,
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: 128,
					TargetUser:    username,
				},
			},
		},
		"1 engine; nr_hugepages unset; hpi fetch fails": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(8192).
					WithEngines(nvmeEngine(0))
			},
			getHpiErr: errors.New("could not find hugepage info"),
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:     true,
					TargetUser: username,
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: 8194,
					HugeNodes:     "0",
					TargetUser:    username,
				},
			},
			expMemChkErr: errors.New("could not find hugepage info"),
		},
		// prepare will continue even if reset fails
		"reset fails; 2 engines": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(16384).
					WithEngines(nvmeEngine(0), nvmeEngine(1)).
					WithBdevInclude(common.MockPCIAddr(1), common.MockPCIAddr(2)).
					WithBdevExclude(common.MockPCIAddr(1))
			},
			hugePagesFree: 16384,
			bmbc: &bdev.MockBackendConfig{
				ResetErr: errors.New("backed prep reset failed"),
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:     true,
					TargetUser: username,
					PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PCIBlockList: common.MockPCIAddr(1),
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: 8194,
					HugeNodes:     "0,1",
					TargetUser:    username,
					PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PCIBlockList: common.MockPCIAddr(1),
				},
			},
			expMemSize:      16384,
			expHugePageSize: 2,
		},
		"reset fails; 2 engines; vmd enabled": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(16384).
					WithEngines(nvmeEngine(0), nvmeEngine(1)).
					WithBdevInclude(common.MockPCIAddr(1), common.MockPCIAddr(2)).
					WithBdevExclude(common.MockPCIAddr(1)).
					WithEnableVMD(true)
			},
			hugePagesFree: 16384,
			bmbc: &bdev.MockBackendConfig{
				ResetErr: errors.New("backed prep reset failed"),
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:     true,
					TargetUser: username,
					PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PCIBlockList: common.MockPCIAddr(1),
					EnableVMD:    true,
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: 8194,
					HugeNodes:     "0,1",
					TargetUser:    username,
					PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PCIBlockList: common.MockPCIAddr(1),
					EnableVMD:    true,
				},
			},
			expMemSize:      16384,
			expHugePageSize: 2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer().WithFabricProvider("ofi+verbs")
			if tc.srvCfgExtra != nil {
				cfg = tc.srvCfgExtra(cfg)
			}

			// test only with 2M hugepage size
			if err := cfg.Validate(log, 2048, nil); err != nil {
				t.Fatal(err)
			}

			srv, err := newServer(log, cfg, &system.FaultDomain{})
			if err != nil {
				t.Fatal(err)
			}

			mbb := bdev.NewMockBackend(tc.bmbc)
			mbp := bdev.NewProvider(log, mbb)
			sp := scm.NewMockSysProvider(log, nil)

			srv.ctlSvc = &ControlService{
				StorageControlService: *NewMockStorageControlService(log, cfg.Engines,
					sp,
					scm.NewProvider(log, scm.NewMockBackend(nil), sp),
					mbp),
				srvCfg: cfg,
			}

			gotErr := prepBdevStorage(srv, !tc.iommuDisabled)

			mbb.RLock()
			if diff := cmp.Diff(tc.expPrepCalls, mbb.PrepareCalls); diff != "" {
				t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
			}
			mbb.RUnlock()

			mbb.RLock()
			if diff := cmp.Diff(tc.expResetCalls, mbb.ResetCalls); diff != "" {
				t.Fatalf("unexpected reset calls (-want, +got):\n%s\n", diff)
			}
			mbb.RUnlock()

			common.CmpErr(t, tc.expPrepErr, gotErr)
			if tc.expPrepErr != nil {
				return
			}

			if len(srv.cfg.Engines) == 0 {
				return
			}

			mockGetHugePageInfo := func() (*common.HugePageInfo, error) {
				t.Logf("returning %d free hugepages from mock", tc.hugePagesFree)
				return &common.HugePageInfo{
					PageSizeKb: 2048,
					Free:       tc.hugePagesFree,
				}, tc.getHpiErr
			}

			runner := engine.NewRunner(log, srv.cfg.Engines[0])
			ei := NewEngineInstance(log, srv.ctlSvc.storage, nil, runner)

			gotErr = updateMemValues(srv, ei, mockGetHugePageInfo)
			common.CmpErr(t, tc.expMemChkErr, gotErr)
			if tc.expMemChkErr != nil {
				return
			}

			common.AssertEqual(t, tc.expMemSize, ei.runner.GetConfig().MemSize,
				"unexpected memory size")
			common.AssertEqual(t, tc.expHugePageSize, ei.runner.GetConfig().HugePageSz,
				"unexpected huge page size")
		})
	}
}

// TestServer_scanBdevStorage validates that an error it returned in the case that a SSD is not
// found and doesn't return an error if SPDK fails to init. Emulated NVMe (SPDK AIO mode) should
// also be covered.
func TestServer_scanBdevStorage(t *testing.T) {
	for name, tc := range map[string]struct {
		bmbc   *bdev.MockBackendConfig
		expErr error
	}{
		"spdk fails init": {
			bmbc: &bdev.MockBackendConfig{
				ScanErr: errors.New("spdk failed"),
			},
		},
		"bdev in config not found by spdk": {
			bmbc: &bdev.MockBackendConfig{
				ScanErr: storage.FaultBdevNotFound(common.MockPCIAddr()),
			},
			expErr: storage.FaultBdevNotFound(common.MockPCIAddr()),
		},
		"successful scan": {
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.MockNvmeControllers(1),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer().WithFabricProvider("ofi+verbs")

			// test only with 2M hugepage size
			if err := cfg.Validate(log, 2048, nil); err != nil {
				t.Fatal(err)
			}

			srv, err := newServer(log, cfg, &system.FaultDomain{})
			if err != nil {
				t.Fatal(err)
			}

			mbb := bdev.NewMockBackend(tc.bmbc)
			mbp := bdev.NewProvider(log, mbb)
			sp := scm.NewMockSysProvider(log, nil)

			srv.ctlSvc = &ControlService{
				StorageControlService: *NewMockStorageControlService(log, cfg.Engines,
					sp,
					scm.NewProvider(log, scm.NewMockBackend(nil), sp),
					mbp),
				srvCfg: cfg,
			}

			_, gotErr := scanBdevStorage(srv)
			common.CmpErr(t, tc.expErr, gotErr)
		})
	}
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
			fis := hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Name:        "eth0",
					OSDevice:    "eth0",
					DeviceClass: hardware.Ether,
					Providers:   common.NewStringSet("test"),
				},
				&hardware.FabricInterface{
					Name:        "eth1",
					OSDevice:    "eth1",
					DeviceClass: hardware.Ether,
					NUMANode:    1,
					Providers:   common.NewStringSet("test"),
				},
				&hardware.FabricInterface{
					Name:        "ib0",
					OSDevice:    "ib0",
					DeviceClass: hardware.Infiniband,
					Providers:   common.NewStringSet("test"),
				},
				&hardware.FabricInterface{
					Name:        "ib1",
					OSDevice:    "ib1",
					DeviceClass: hardware.Infiniband,
					NUMANode:    1,
					Providers:   common.NewStringSet("test"),
				},
			)

			cfg := config.DefaultServer().
				WithFabricProvider("test").
				WithEngines(tc.configA, tc.configB)

			gotNetDevCls, gotErr := getFabricNetDevClass(cfg, fis)

			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			common.AssertEqual(t, tc.expNetDevCls, gotNetDevCls,
				"unexpected config network device class")
		})
	}
}
