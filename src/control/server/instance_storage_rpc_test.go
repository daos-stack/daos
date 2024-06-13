//
// (C) Copyright 2023-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/pciutils"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

type mockAccessProvider struct {
	pciDev    *hardware.PCIDevice
	pciDevErr error
}

func (ap *mockAccessProvider) Cleanup() {}

func (ap *mockAccessProvider) PCIeCapsFromConfig(cfgBytes []byte, dev *hardware.PCIDevice) error {
	if ap.pciDevErr != nil {
		return ap.pciDevErr
	}
	*dev = *ap.pciDev
	return nil
}

func TestIOEngineInstance_populateCtrlrHealth(t *testing.T) {
	healthWithoutLinkStats := func() *ctlpb.BioHealthResp {
		bhr := proto.MockNvmeHealth()
		bhr.LinkPortId = 0
		bhr.LinkMaxSpeed = 0
		bhr.LinkNegSpeed = 0
		bhr.LinkMaxWidth = 0
		bhr.LinkNegWidth = 0

		return bhr
	}

	for name, tc := range map[string]struct {
		devState   ctlpb.NvmeDevState
		pciCfgSpc  string
		pciDev     *hardware.PCIDevice
		pciDevErr  error
		healthRes  *ctlpb.BioHealthResp
		healthErr  error
		expCtrlr   *ctlpb.NvmeController
		expUpdated bool
		expErr     error
	}{
		"bad state; skip health": {
			healthRes: healthWithoutLinkStats(),
			expCtrlr:  &ctlpb.NvmeController{},
		},
		"update health; add link stats skipped as empty pci config space": {
			devState: ctlpb.NvmeDevState_NORMAL,
			pciDev: &hardware.PCIDevice{
				LinkNegSpeed: 8e+9,
			},
			healthRes: healthWithoutLinkStats(),
			expCtrlr: &ctlpb.NvmeController{
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: healthWithoutLinkStats(),
			},
			expUpdated: true,
		},
		"update health; add link stats; pciutils lib error": {
			devState:  ctlpb.NvmeDevState_NORMAL,
			pciCfgSpc: "ABCD",
			pciDev: &hardware.PCIDevice{
				LinkNegSpeed: 8e+9,
			},
			pciDevErr: errors.New("fail"),
			expErr:    errors.New("fail"),
		},
		"update health; add link stats": {
			devState:  ctlpb.NvmeDevState_NORMAL,
			pciCfgSpc: "ABCD",
			pciDev: &hardware.PCIDevice{
				LinkPortID:   1,
				LinkNegSpeed: 1e+9,
				LinkMaxSpeed: 1e+9,
				LinkNegWidth: 4,
				LinkMaxWidth: 4,
			},
			healthRes: healthWithoutLinkStats(),
			expCtrlr: &ctlpb.NvmeController{
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: proto.MockNvmeHealth(),
			},
			expUpdated: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			getCtrlrHealth = func(_ context.Context, _ Engine, _ *ctlpb.BioHealthReq) (*ctlpb.BioHealthResp, error) {
				return tc.healthRes, tc.healthErr
			}
			defer func() {
				getCtrlrHealth = getBioHealth
			}()

			ctx := context.WithValue(test.Context(t), pciutils.AccessKey,
				&mockAccessProvider{
					pciDev:    tc.pciDev,
					pciDevErr: tc.pciDevErr,
				})

			ctrlr := &ctlpb.NvmeController{
				PciCfg:   tc.pciCfgSpc,
				DevState: tc.devState,
			}

			upd, err := populateCtrlrHealth(ctx, NewMockInstance(nil),
				&ctlpb.BioHealthReq{}, ctrlr)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expCtrlr, ctrlr,
				defStorageScanCmpOpts...); diff != "" {
				t.Fatalf("unexpected controller output (-want, +got):\n%s\n", diff)
			}
			test.AssertEqual(t, tc.expUpdated, upd, "")
		})
	}
}

func TestIOEngineInstance_bdevScanEngine(t *testing.T) {
	c := storage.MockNvmeController(2)
	withState := func(ctrlr *ctlpb.NvmeController, state ctlpb.NvmeDevState) *ctlpb.NvmeController {
		ctrlr.DevState = state
		ctrlr.HealthStats = nil
		// scanEngineBdevsOverDrpc will always populate RoleBits in ctrlr.SmdDevices
		ctrlr.SmdDevices = []*ctlpb.SmdDevice{{RoleBits: 7}}
		return ctrlr
	}
	withDevState := func(smd *ctlpb.SmdDevice, state ctlpb.NvmeDevState) *ctlpb.SmdDevice {
		smd.Ctrlr.DevState = state
		return smd
	}
	defSmdScanRes := func() *ctlpb.SmdDevResp {
		sd := proto.MockSmdDevice(c, 2)
		return &ctlpb.SmdDevResp{Devices: []*ctlpb.SmdDevice{sd}}
	}
	healthRespWithUsage := func() *ctlpb.BioHealthResp {
		mh := proto.MockNvmeHealth(2)
		mh.TotalBytes, mh.AvailBytes, mh.ClusterSize = 1, 2, 3
		mh.MetaWalSize, mh.RdbWalSize = 4, 5
		return mh
	}

	for name, tc := range map[string]struct {
		req                 ctlpb.ScanNvmeReq
		bdevAddrs           []string
		rank                int
		provRes             *storage.BdevScanResponse
		provErr             error
		engStopped          bool
		smdRes              *ctlpb.SmdDevResp
		smdErr              error
		healthRes           *ctlpb.BioHealthResp
		healthErr           error
		expResp             *ctlpb.ScanNvmeResp
		expErr              error
		expBackendScanCalls []storage.BdevScanRequest
	}{
		"no bdevs in cfg": {
			bdevAddrs: []string{},
			expErr:    errors.New("empty device list"),
		},
		"engines stopped; scan over engine provider": {
			bdevAddrs:  []string{test.MockPCIAddr(1), test.MockPCIAddr(2)},
			engStopped: true,
			provRes: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
				},
			},
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(1)
						c.SmdDevices = []*ctlpb.SmdDevice{
							{Rank: uint32(ranklist.NilRank)},
						}
						return c
					}(),
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.SmdDevices = []*ctlpb.SmdDevice{
							{Rank: uint32(ranklist.NilRank)},
						}
						return c
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
			expBackendScanCalls: []storage.BdevScanRequest{
				{
					DeviceList: storage.MustNewBdevDeviceList(
						test.MockPCIAddr(1), test.MockPCIAddr(2)),
				},
			},
		},
		"engines stopped; scan over engine provider; retry on empty response": {
			bdevAddrs:  []string{test.MockPCIAddr(1), test.MockPCIAddr(2)},
			engStopped: true,
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(1)
						c.SmdDevices = []*ctlpb.SmdDevice{
							{Rank: uint32(ranklist.NilRank)},
						}
						return c
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
			expBackendScanCalls: []storage.BdevScanRequest{
				{
					DeviceList: storage.MustNewBdevDeviceList(
						test.MockPCIAddr(1), test.MockPCIAddr(2)),
				},
				{
					DeviceList: storage.MustNewBdevDeviceList(
						test.MockPCIAddr(1), test.MockPCIAddr(2)),
				},
			},
		},
		"engines stopped; scan fails over engine provider": {
			engStopped: true,
			provErr:    errors.New("provider scan fail"),
			expErr:     errors.New("provider scan fail"),
		},
		"engines stopped; scan over engine provider; vmd enabled": {
			bdevAddrs:  []string{"0000:05:05.5"},
			engStopped: true,
			provRes: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{
					&storage.NvmeController{
						PciAddr:   "050505:01:00.0",
						NvmeState: storage.NvmeStateNormal,
					},
					&storage.NvmeController{
						PciAddr:   "050505:03:00.0",
						NvmeState: storage.NvmeStateNormal,
					},
				},
			},
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						nc := &ctlpb.NvmeController{
							PciAddr:  "050505:01:00.0",
							DevState: ctlpb.NvmeDevState_NORMAL,
						}
						nc.SmdDevices = []*ctlpb.SmdDevice{
							{Rank: uint32(ranklist.NilRank)},
						}
						return nc
					}(),
					func() *ctlpb.NvmeController {
						nc := &ctlpb.NvmeController{
							PciAddr:  "050505:03:00.0",
							DevState: ctlpb.NvmeDevState_NORMAL,
						}
						nc.SmdDevices = []*ctlpb.SmdDevice{
							{Rank: uint32(ranklist.NilRank)},
						}
						return nc
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
			expBackendScanCalls: []storage.BdevScanRequest{
				{DeviceList: storage.MustNewBdevDeviceList("0000:05:05.5")},
			},
		},
		"scan fails over drpc": {
			smdErr: errors.New("drpc fail"),
			expErr: errors.New("drpc fail"),
		},
		"scan over drpc; no req flags; rank and roles populated": {
			req:    ctlpb.ScanNvmeReq{},
			rank:   1,
			smdRes: defSmdScanRes(),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.HealthStats = nil
						c.SmdDevices = []*ctlpb.SmdDevice{
							{Rank: 1, RoleBits: storage.BdevRoleAll},
						}
						return c
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan over drpc; no req flags; invalid rank": {
			req:    ctlpb.ScanNvmeReq{},
			rank:   -1,
			smdRes: defSmdScanRes(),
			expErr: errors.New("nil superblock"),
		},
		"scan over drpc; with health": {
			req:       ctlpb.ScanNvmeReq{Health: true},
			rank:      1,
			smdRes:    defSmdScanRes(),
			healthRes: healthRespWithUsage(),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.HealthStats = healthRespWithUsage()
						c.SmdDevices = []*ctlpb.SmdDevice{
							{Rank: 1, RoleBits: storage.BdevRoleAll},
						}
						return c
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan over drpc; with meta": {
			req:       ctlpb.ScanNvmeReq{Meta: true},
			rank:      1,
			smdRes:    defSmdScanRes(),
			healthRes: healthRespWithUsage(),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.HealthStats = nil
						sd := proto.MockSmdDevice(nil, 2)
						sd.Rank = 1
						c.SmdDevices = []*ctlpb.SmdDevice{sd}
						return c
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan over drpc; with smd and health; usage and wal size reported": {
			req:       ctlpb.ScanNvmeReq{Meta: true, Health: true},
			rank:      1,
			smdRes:    defSmdScanRes(),
			healthRes: healthRespWithUsage(),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.HealthStats = healthRespWithUsage()
						sd := proto.MockSmdDevice(nil, 2)
						sd.Rank = 1
						sd.TotalBytes = c.HealthStats.TotalBytes
						sd.AvailBytes = c.HealthStats.AvailBytes
						sd.ClusterSize = c.HealthStats.ClusterSize
						sd.MetaWalSize = c.HealthStats.MetaWalSize
						sd.RdbWalSize = c.HealthStats.RdbWalSize
						c.SmdDevices = []*ctlpb.SmdDevice{sd}
						return c
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan over drpc; only ctrlrs with valid states shown": {
			req: ctlpb.ScanNvmeReq{},
			bdevAddrs: []string{
				test.MockPCIAddr(1), test.MockPCIAddr(2),
				test.MockPCIAddr(1), test.MockPCIAddr(2),
				test.MockPCIAddr(5),
			},
			smdRes: &ctlpb.SmdDevResp{
				Devices: proto.SmdDevices{
					withDevState(proto.MockSmdDevice(
						storage.MockNvmeController(1), 1),
						ctlpb.NvmeDevState_UNPLUGGED),
					withDevState(proto.MockSmdDevice(
						storage.MockNvmeController(2), 2),
						ctlpb.NvmeDevState_UNKNOWN),
					withDevState(proto.MockSmdDevice(
						storage.MockNvmeController(3), 3),
						ctlpb.NvmeDevState_NORMAL),
					withDevState(proto.MockSmdDevice(
						storage.MockNvmeController(4), 4),
						ctlpb.NvmeDevState_NEW),
					withDevState(proto.MockSmdDevice(
						storage.MockNvmeController(5), 5),
						ctlpb.NvmeDevState_EVICTED),
				},
			},
			healthRes: healthRespWithUsage(),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					withState(proto.MockNvmeController(3),
						ctlpb.NvmeDevState_NORMAL),
					withState(proto.MockNvmeController(4),
						ctlpb.NvmeDevState_NEW),
					withState(proto.MockNvmeController(5),
						ctlpb.NvmeDevState_EVICTED),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan over drpc; with smd and health; missing ctrlr in smd": {
			req: ctlpb.ScanNvmeReq{Meta: true, Health: true},
			smdRes: func() *ctlpb.SmdDevResp {
				ssr := defSmdScanRes()
				ssr.Devices[0].Ctrlr = nil
				return ssr
			}(),
			healthRes: healthRespWithUsage(),
			expErr:    errors.New("no ctrlr ref"),
		},
		"scan over drpc; with smd and health; health scan fails": {
			req:       ctlpb.ScanNvmeReq{Meta: true, Health: true},
			smdRes:    defSmdScanRes(),
			healthErr: errors.New("health scan failed"),
			expErr:    errors.New("health scan failed"),
		},
		"scan over drpc; with smd and health; smd list fails": {
			req:       ctlpb.ScanNvmeReq{Meta: true, Health: true},
			smdErr:    errors.New("smd scan failed"),
			healthRes: healthRespWithUsage(),
			expErr:    errors.New("smd scan failed"),
		},
		"scan over drpc; with smd and health; nil smd list returned": {
			req:       ctlpb.ScanNvmeReq{Meta: true, Health: true},
			healthRes: healthRespWithUsage(),
			expErr:    errors.New("nil smd scan resp"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			scanSmd = func(_ context.Context, _ Engine, _ *ctlpb.SmdDevReq) (*ctlpb.SmdDevResp, error) {
				return tc.smdRes, tc.smdErr
			}
			defer func() {
				scanSmd = listSmdDevices
			}()
			getCtrlrHealth = func(_ context.Context, _ Engine, _ *ctlpb.BioHealthReq) (*ctlpb.BioHealthResp, error) {
				return tc.healthRes, tc.healthErr
			}
			defer func() {
				getCtrlrHealth = getBioHealth
			}()

			if tc.provRes == nil {
				tc.provRes = defProviderScanRes
			}

			ec := engine.MockConfig()
			if tc.bdevAddrs == nil {
				tc.bdevAddrs = []string{test.MockPCIAddr(1)}
			}
			ec.WithStorage(storage.NewTierConfig().
				WithStorageClass(storage.ClassNvme.String()).
				WithBdevDeviceList(tc.bdevAddrs...))

			sCfg := config.DefaultServer().WithEngines(ec)

			bmbc := &bdev.MockBackendConfig{
				ScanRes: tc.provRes,
				ScanErr: tc.provErr,
			}
			bmb := bdev.NewMockBackend(bmbc)
			smb := scm.NewMockBackend(nil)

			cs := newMockControlServiceFromBackends(t, log, sCfg, bmb, smb, nil,
				tc.engStopped)
			ei := cs.harness.Instances()[0].(*EngineInstance)
			if tc.rank < 0 {
				ei.setSuperblock(nil)
			} else {
				ei.setSuperblock(&Superblock{
					Rank: ranklist.NewRankPtr(uint32(tc.rank)), ValidRank: true,
				})
			}

			resp, err := bdevScanEngine(test.Context(t), ei, &tc.req)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, resp,
				defStorageScanCmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}

			cmpopt := cmp.Comparer(func(x, y *storage.BdevDeviceList) bool {
				if x == nil && y == nil {
					return true
				}
				return x.Equals(y)
			})

			bmb.RLock()
			if len(tc.expBackendScanCalls) != len(bmb.ScanCalls) {
				t.Fatalf("unexpected number of backend scan calls, want %d got %d",
					len(tc.expBackendScanCalls), len(bmb.ScanCalls))
			}
			if len(tc.expBackendScanCalls) == 0 {
				return
			}
			if diff := cmp.Diff(tc.expBackendScanCalls, bmb.ScanCalls,
				append(defStorageScanCmpOpts, cmpopt)...); diff != "" {
				t.Fatalf("unexpected backend scan calls (-want, +got):\n%s\n", diff)
			}
			bmb.RUnlock()
		})
	}
}
