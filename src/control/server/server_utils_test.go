//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"net"
	"os/user"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
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
	for name, tc := range map[string]struct {
		cfg        *config.Server
		expSetting int32
		expErr     error
	}{
		"no engines": {
			cfg:        config.DefaultServer(),
			expSetting: -1,
		},
		"not set": {
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig(),
				engine.NewConfig(),
			),
			expSetting: -1,
		},
		"set to 0 in both (single)": {
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0"),
				engine.NewConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0"),
			),
			expSetting: 0,
		},
		"set to 1 in both (single)": {
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=1"),
				engine.NewConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=1"),
			),
			expSetting: 1,
		},
		"set to 0 in both (multi)": {
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().WithEnvVars("FOO=BAR", "FI_OFI_RXM_USE_SRX=0"),
				engine.NewConfig().WithEnvVars("FOO=BAR", "FI_OFI_RXM_USE_SRX=0"),
			),
			expSetting: 0,
		},
		"set to 1 in both (multi)": {
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().WithEnvVars("FOO=BAR", "FI_OFI_RXM_USE_SRX=1"),
				engine.NewConfig().WithEnvVars("FOO=BAR", "FI_OFI_RXM_USE_SRX=1"),
			),
			expSetting: 1,
		},
		"set twice; first value used": {
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0", "FI_OFI_RXM_USE_SRX=1"),
				engine.NewConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=1"),
			),
			expErr: errors.New("FI_OFI_RXM_USE_SRX"),
		},
		"set in both; different values": {
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0"),
				engine.NewConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=1"),
			),
			expErr: errors.New("FI_OFI_RXM_USE_SRX"),
		},
		"set in first; no vars in second": {
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0"),
				engine.NewConfig(),
			),
			expErr: errors.New("FI_OFI_RXM_USE_SRX"),
		},
		"no vars in first; set in second": {
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig(),
				engine.NewConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0"),
			),
			expErr: errors.New("FI_OFI_RXM_USE_SRX"),
		},
		"set in first; unset in second": {
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0"),
				engine.NewConfig().WithEnvVars("FOO=bar"),
			),
			expErr: errors.New("FI_OFI_RXM_USE_SRX"),
		},
		"unset in first; set in second": {
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().WithEnvVars("FOO=bar"),
				engine.NewConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=0"),
			),
			expErr: errors.New("FI_OFI_RXM_USE_SRX"),
		},
		"wonky value": {
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().WithEnvVars("FI_OFI_RXM_USE_SRX=on"),
			),
			expSetting: -1,
		},
		"set in env_pass_through": {
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().WithEnvPassThrough("FI_OFI_RXM_USE_SRX"),
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

	for name, tc := range map[string]struct {
		enableVMD     bool
		bmbc          *bdev.MockBackendConfig
		smbc          *scm.MockBackendConfig
		allowList     []string
		blockList     []string
		expErr        error
		expPrepCalls  []storage.BdevPrepareRequest
		expResetCalls []storage.BdevPrepareRequest
	}{
		"nvme prep succeeds; user params": {
			allowList: []string{common.MockPCIAddr(1), common.MockPCIAddr(2)},
			blockList: []string{common.MockPCIAddr(1)},
			// with reset set to false in request, two calls will be made to
			// bdev backend prepare, one to reset device bindings and hugepage
			// allocations and another to set them as requested
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:        true,
					HugePageCount: minHugePageCount,
					TargetUser:    username,
					PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PCIBlockList: common.MockPCIAddr(1),
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: minHugePageCount,
					TargetUser:    username,
					PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PCIBlockList: common.MockPCIAddr(1),
				},
			},
		},
		"nvme prep fails; user params": {
			allowList: []string{common.MockPCIAddr(1), common.MockPCIAddr(2)},
			blockList: []string{common.MockPCIAddr(1)},
			bmbc: &bdev.MockBackendConfig{
				PrepareErr: errors.New("backed prep setup failed"),
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:        true,
					HugePageCount: minHugePageCount,
					TargetUser:    username,
					PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PCIBlockList: common.MockPCIAddr(1),
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					HugePageCount: minHugePageCount,
					TargetUser:    username,
					PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PCIBlockList: common.MockPCIAddr(1),
				},
			},
		},
		"nvme reset fails; user params": {
			allowList: []string{common.MockPCIAddr(1), common.MockPCIAddr(2)},
			blockList: []string{common.MockPCIAddr(1)},
			bmbc: &bdev.MockBackendConfig{
				ResetErr: errors.New("backed prep setup failed"),
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:        true,
					HugePageCount: minHugePageCount,
					TargetUser:    username,
					PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PCIBlockList: common.MockPCIAddr(1),
				},
			},
		},
		"nvme prep succeeds; user params; vmd enabled": {
			enableVMD: true,
			allowList: []string{common.MockPCIAddr(1), common.MockPCIAddr(2)},
			blockList: []string{common.MockPCIAddr(1)},
			expResetCalls: []storage.BdevPrepareRequest{
				{
					Reset_:        true,
					EnableVMD:     true,
					HugePageCount: minHugePageCount,
					TargetUser:    username,
					PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PCIBlockList: common.MockPCIAddr(1),
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					EnableVMD:     true,
					HugePageCount: minHugePageCount,
					TargetUser:    username,
					PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PCIBlockList: common.MockPCIAddr(1),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			cfg := &config.Server{
				BdevExclude: tc.blockList,
				BdevInclude: tc.allowList,
				EnableVMD:   tc.enableVMD,
			}
			srv, err := newServer(context.TODO(), log, cfg, &system.FaultDomain{})
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

			gotErr := prepBdevStorage(srv, true, common.GetHugePageInfo)

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

			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}
