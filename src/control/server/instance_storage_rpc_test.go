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
	defSmdScanRes := &ctlpb.SmdDevResp{
		Devices: []*ctlpb.SmdDevice{
			proto.MockSmdDevice(test.MockPCIAddr(2), 2),
		},
	}

	for name, tc := range map[string]struct {
		req                ctlpb.ScanNvmeReq
		bdevAddrs          []string
		provRes            *storage.BdevScanResponse
		provErr            error
		engStopped         bool
		smdRes             *ctlpb.SmdDevResp
		smdErr             error
		healthRes          *ctlpb.BioHealthResp
		healthErr          error
		expResp            *ctlpb.ScanNvmeResp
		expErr             error
		expBackendScanCall *storage.BdevScanRequest
	}{
		"scan over drpc; no health or meta": {
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
		"scan over engine provider; no bdevs in config": {
			engStopped: true,
			expErr:     errors.New("empty device list"),
		},
		"scan over engine provider; bdevs in config": {
			bdevAddrs:  []string{test.MockPCIAddr(1), test.MockPCIAddr(2)},
			engStopped: true,
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					proto.MockNvmeController(1),
				},
				State: new(ctlpb.ResponseState),
			},
			expBackendScanCall: &storage.BdevScanRequest{
				DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddr(1),
					test.MockPCIAddr(2)),
			},
		},
		"scan fails over engine provider": {
			bdevAddrs:  []string{test.MockPCIAddr(1), test.MockPCIAddr(2)},
			engStopped: true,
			provErr:    errors.New("provider scan fail"),
			expErr:     errors.New("provider scan fail"),
		},
		// "scan over drpc; with health": {},
		// "scan over drpc; with smd": {},
		// "scan over drpc; with smd and health": {},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			scanSmd = func(_ context.Context, _ *EngineInstance, _ *ctlpb.SmdDevReq) (*ctlpb.SmdDevResp, error) {
				return tc.smdRes, tc.smdErr
			}
			defer func() {
				scanSmd = listSmdDevices
			}()
			getCtrlrHealth = func(_ context.Context, _ *EngineInstance, _ *ctlpb.BioHealthReq) (*ctlpb.BioHealthResp, error) {
				return tc.healthRes, tc.healthErr
			}
			defer func() {
				getCtrlrHealth = getBioHealth
			}()

			if tc.provRes == nil {
				tc.provRes = defProviderScanRes
			}
			if tc.smdRes == nil {
				tc.smdRes = defSmdScanRes
			}

			ec := engine.MockConfig()
			if tc.bdevAddrs != nil {
				ec.WithStorage(storage.NewTierConfig().
					WithStorageClass(storage.ClassNvme.String()).
					WithBdevDeviceList(tc.bdevAddrs...))
			}

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
			switch len(bmb.ScanCalls) {
			case 0:
				if tc.expBackendScanCall == nil {
					return
				}
				t.Fatalf("unexpected number of backend scan calls, want 1 got 0")
			case 1:
				if tc.expBackendScanCall != nil {
					break
				}
				t.Fatalf("unexpected number of backend scan calls, want 0 got 1")
			default:
				t.Fatalf("unexpected number of backend scan calls, want 0-1 got %d",
					len(bmb.ScanCalls))
			}
			if diff := cmp.Diff(*tc.expBackendScanCall, bmb.ScanCalls[0],
				append(defStorageScanCmpOpts, cmpopt)...); diff != "" {
				t.Fatalf("unexpected backend scan calls (-want, +got):\n%s\n", diff)
			}
			bmb.RUnlock()
		})
	}
}
