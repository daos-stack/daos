//
// (C) Copyright 2023 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func TestIOEngineInstance_bdevScanEngine(t *testing.T) {
	c := storage.MockNvmeController(2)
	defSmdScanRes := func() *ctlpb.SmdDevResp {
		return &ctlpb.SmdDevResp{
			Devices: []*ctlpb.SmdDevice{
				proto.MockSmdDevice(c, 2),
			},
		}
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
					proto.MockNvmeController(1),
					proto.MockNvmeController(2),
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
					proto.MockNvmeController(1),
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
		"scan over drpc; no health or meta": {
			smdRes:    defSmdScanRes(),
			healthRes: proto.MockNvmeHealth(2),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.HealthStats = nil
						c.SmdDevices = nil
						return c
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan fails over drpc": {
			smdErr: errors.New("drpc fail"),
			expErr: errors.New("drpc fail"),
		},
		"scan over drpc; with health": {
			req:       ctlpb.ScanNvmeReq{Health: true},
			smdRes:    defSmdScanRes(),
			healthRes: healthRespWithUsage(),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.HealthStats = healthRespWithUsage()
						c.SmdDevices = nil
						return c
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan over drpc; with smd": {
			req:       ctlpb.ScanNvmeReq{Meta: true},
			smdRes:    defSmdScanRes(),
			healthRes: healthRespWithUsage(),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.HealthStats = nil
						c.SmdDevices = []*ctlpb.SmdDevice{
							proto.MockSmdDevice(nil, 2),
						}
						return c
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan over drpc; with smd and health; usage and wal size reported": {
			req:       ctlpb.ScanNvmeReq{Meta: true, Health: true},
			smdRes:    defSmdScanRes(),
			healthRes: healthRespWithUsage(),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.HealthStats = healthRespWithUsage()
						sd := proto.MockSmdDevice(nil, 2)
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
