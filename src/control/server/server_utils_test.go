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
	"github.com/daos-stack/daos/src/control/common/test"
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
			WithBdevDeviceList(test.MockPCIAddr(int32(i)))
	}
	nonNvmeTier := func() *storage.TierConfig {
		return storage.NewTierConfig().WithStorageClass(storage.ClassFile.String()).
			WithBdevFileSize(10).WithBdevDeviceList("bdev1", "bdev2")
	}
	scmEngine := func(i int) *engine.Config {
		return basicEngineCfg(i).WithStorage(scmTier(i)).WithTargetCount(8)
	}
	nvmeEngine := func(i int) *engine.Config {
		return basicEngineCfg(i).WithStorage(scmTier(i), nvmeTier(i)).WithTargetCount(16)
	}
	nonNvmeEngine := func(i int) *engine.Config {
		return basicEngineCfg(i).WithStorage(scmTier(i), nonNvmeTier()).WithTargetCount(16)
	}

	for name, tc := range map[string]struct {
		iommuDisabled   bool
		srvCfgExtra     func(*config.Server) *config.Server
		getHpiErr       error
		hugePagesFree   int
		bmbc            *bdev.MockBackendConfig
		overrideUser    string
		expPrepErr      error
		expPrepCall     *storage.BdevPrepareRequest
		expMemChkErr    error
		expMemSize      int
		expHugePageSize int
	}{
		"vfio disabled; non-root user": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithDisableVFIO(true).
					WithEngines(nvmeEngine(0))
			},
			expPrepErr: FaultVfioDisabled,
		},
		"vfio disabled; root user": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithDisableVFIO(true).
					WithEngines(nvmeEngine(0))
			},
			overrideUser:  "root",
			hugePagesFree: 8192,
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 8194,
				HugeNodes:     "0",
				TargetUser:    "root",
				DisableVFIO:   true,
			},
			expMemSize:      16384,
			expHugePageSize: 2,
		},
		"non-nvme bdevs; vfio disabled": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithDisableVFIO(true).
					WithEngines(nonNvmeEngine(0))
			},
			hugePagesFree: 8192,
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 8194,
				HugeNodes:     "0",
				TargetUser:    username,
				DisableVFIO:   true,
			},
			expMemSize:      16384,
			expHugePageSize: 2,
		},
		"iommu disabled": {
			iommuDisabled: true,
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(nvmeEngine(0), nvmeEngine(1))
			},
			expPrepErr: FaultIommuDisabled,
		},
		"iommu disabled; root user": {
			iommuDisabled: true,
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(nvmeEngine(0))
			},
			overrideUser:  "root",
			hugePagesFree: 8192,
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 8194,
				HugeNodes:     "0",
				TargetUser:    "root",
			},
			expMemSize:      16384,
			expHugePageSize: 2,
		},
		"non-nvme bdevs; iommu disabled": {
			iommuDisabled: true,
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithEngines(nonNvmeEngine(0))
			},
			hugePagesFree: 8192,
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 8194,
				HugeNodes:     "0",
				TargetUser:    username,
			},
			expMemSize:      16384,
			expHugePageSize: 2,
		},
		"no bdevs configured; hugepages disabled": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithDisableHugePages(true).
					WithEngines(scmEngine(0), scmEngine(1))
			},
		},
		"no bdevs configured; nr_hugepages unset": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(0).
					WithEngines(scmEngine(0), scmEngine(1))
			},
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: scanMinHugePageCount,
				TargetUser:    username,
				EnableVMD:     true,
			},
		},
		"no bdevs configured; nr_hugepages set": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(1024).
					WithEngines(scmEngine(0), scmEngine(1))
			},
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 1024,
				TargetUser:    username,
				EnableVMD:     true,
			},
		},
		"2 engines both numa 0; hugepage alloc only on numa 0": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(16384).
					WithEngines(nvmeEngine(0), nvmeEngine(1).WithPinnedNumaNode(0)).
					WithBdevInclude(test.MockPCIAddr(1), test.MockPCIAddr(2)).
					WithBdevExclude(test.MockPCIAddr(1))
			},
			hugePagesFree: 16384,
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 16386, // 2 extra huge pages requested
				HugeNodes:     "0",
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
				PCIBlockList: test.MockPCIAddr(1),
				EnableVMD:    true,
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
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 16386,
				HugeNodes:     "1",
				TargetUser:    username,
				EnableVMD:     true,
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
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 8194, // 2 extra huge pages requested per-engine
				HugeNodes:     "0,1",
				TargetUser:    username,
				EnableVMD:     true,
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
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 8194,
				HugeNodes:     "0,1",
				TargetUser:    username,
				EnableVMD:     true,
			},
			expMemChkErr: errors.New("0: want 16 GiB (8192 hugepages), got 16 GiB (8191"),
		},
		"2 engines; scm only; nr_hugepages unset": {
			hugePagesFree: 128,
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 128,
				TargetUser:    username,
				EnableVMD:     true,
			},
		},
		"2 engines; scm only; nr_hugepages unset; insufficient free": {
			hugePagesFree: 0,
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 128,
				TargetUser:    username,
				EnableVMD:     true,
			},
			expMemChkErr: errors.New("requested 128 hugepages; got 0"),
		},
		"0 engines; nr_hugepages unset": {
			hugePagesFree: 128,
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 128,
				TargetUser:    username,
				EnableVMD:     true,
			},
		},
		"0 engines; nr_hugepages unset; insufficient free": {
			hugePagesFree: 0,
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 128,
				TargetUser:    username,
				EnableVMD:     true,
			},
			expMemChkErr: errors.New("requested 128 hugepages; got 0"),
		},
		"0 engines; nr_hugepages unset; hpi fetch fails": {
			getHpiErr: errors.New("could not find hugepage info"),
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 128,
				TargetUser:    username,
				EnableVMD:     true,
			},
		},
		"1 engine; nr_hugepages unset; hpi fetch fails": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(8192).
					WithEngines(nvmeEngine(0))
			},
			getHpiErr: errors.New("could not find hugepage info"),
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 8194,
				HugeNodes:     "0",
				TargetUser:    username,
				EnableVMD:     true,
			},
			expMemChkErr: errors.New("could not find hugepage info"),
		},
		// prepare will continue even if reset fails
		"reset fails": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(16384).
					WithEngines(nvmeEngine(0), nvmeEngine(1)).
					WithBdevInclude(test.MockPCIAddr(1), test.MockPCIAddr(2)).
					WithBdevExclude(test.MockPCIAddr(1))
			},
			hugePagesFree: 16384,
			bmbc: &bdev.MockBackendConfig{
				ResetErr: errors.New("backed prep reset failed"),
			},
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 8194,
				HugeNodes:     "0,1",
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
				PCIBlockList: test.MockPCIAddr(1),
				EnableVMD:    true,
			},
			expMemSize:      16384,
			expHugePageSize: 2,
		},
		// VMD not enabled in prepare request.
		"vmd disabled": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(16384).
					WithEngines(nvmeEngine(0), nvmeEngine(1)).
					WithDisableVMD(true)
			},
			hugePagesFree: 16384,
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 8194,
				HugeNodes:     "0,1",
				TargetUser:    username,
			},
			expMemSize:      16384,
			expHugePageSize: 2,
		},
		// VMD not enabled in prepare request.
		"non-nvme bdevs; vmd enabled": {
			srvCfgExtra: func(sc *config.Server) *config.Server {
				return sc.WithNrHugePages(8192).
					WithEngines(nonNvmeEngine(0))
			},
			hugePagesFree: 8194,
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 8194,
				HugeNodes:     "0",
				TargetUser:    username,
			},
			expMemSize:      16384,
			expHugePageSize: 2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer().WithFabricProvider("ofi+verbs")
			if tc.srvCfgExtra != nil {
				cfg = tc.srvCfgExtra(cfg)
			}

			// ensure that the engine affinities are set.
			if err := cfg.SetEngineAffinities(log); err != nil {
				t.Fatal(err)
			}

			// test only with 2M hugepage size
			if err := cfg.Validate(log, 2048); err != nil {
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

			if tc.overrideUser != "" {
				srv.runningUser = &user.User{Username: tc.overrideUser}
			}

			gotErr := prepBdevStorage(srv, !tc.iommuDisabled)

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

			test.CmpErr(t, tc.expPrepErr, gotErr)
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
			test.CmpErr(t, tc.expMemChkErr, gotErr)
			if tc.expMemChkErr != nil {
				return
			}

			test.AssertEqual(t, tc.expMemSize, ei.runner.GetConfig().MemSize,
				"unexpected memory size")
			test.AssertEqual(t, tc.expHugePageSize, ei.runner.GetConfig().HugePageSz,
				"unexpected huge page size")
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
				WithDisableHugePages(tc.disableHugepages)

			// test only with 2M hugepage size
			if err := cfg.Validate(log, 2048); err != nil {
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
			test.CmpErr(t, tc.expErr, gotErr)
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
					Name:          "eth0",
					NetInterfaces: common.NewStringSet("eth0"),
					DeviceClass:   hardware.Ether,
					Providers:     common.NewStringSet("test"),
				},
				&hardware.FabricInterface{
					Name:          "eth1",
					NetInterfaces: common.NewStringSet("eth1"),
					DeviceClass:   hardware.Ether,
					NUMANode:      1,
					Providers:     common.NewStringSet("test"),
				},
				&hardware.FabricInterface{
					Name:          "ib0",
					NetInterfaces: common.NewStringSet("ib0"),
					DeviceClass:   hardware.Infiniband,
					Providers:     common.NewStringSet("test"),
				},
				&hardware.FabricInterface{
					Name:          "ib1",
					NetInterfaces: common.NewStringSet("ib1"),
					DeviceClass:   hardware.Infiniband,
					NUMANode:      1,
					Providers:     common.NewStringSet("test"),
				},
			)

			cfg := config.DefaultServer().
				WithFabricProvider("test").
				WithEngines(tc.configA, tc.configB)

			gotNetDevCls, gotErr := getFabricNetDevClass(cfg, fis)

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
