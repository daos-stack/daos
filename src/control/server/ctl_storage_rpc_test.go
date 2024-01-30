//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"os"
	"os/user"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/testing/protocmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/common/proto/ctl"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/mount"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

const defaultRdbSize uint64 = uint64(daos.DefaultDaosMdCapSize)

var (
	defStorageScanCmpOpts = append(test.DefaultCmpOpts(),
		protocmp.IgnoreFields(&ctlpb.NvmeController{}, "serial"))
	defProviderScanRes = &storage.BdevScanResponse{
		Controllers: storage.NvmeControllers{
			storage.MockNvmeController(1),
		},
	}
	defEngineScanRes = &ctlpb.ScanNvmeResp{
		Ctrlrs: proto.NvmeControllers{
			proto.MockNvmeController(2),
		},
		State: new(ctlpb.ResponseState),
	}
)

func TestServer_bdevScan(t *testing.T) {
	for name, tc := range map[string]struct {
		req                 *ctlpb.ScanNvmeReq
		provRes             *storage.BdevScanResponse
		provErr             error
		engTierCfgs         []storage.TierConfigs // one per-engine
		engStopped          []bool                // one per-engine (all false if unset)
		engRes              []ctlpb.ScanNvmeResp  // one per-engine
		engErr              []error               // one per-engine
		expResp             *ctlpb.ScanNvmeResp
		expErr              error
		expBackendScanCalls []storage.BdevScanRequest
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"scan local; no bdevs in config; scan fails": {
			req:         &ctlpb.ScanNvmeReq{Health: true},
			engTierCfgs: []storage.TierConfigs{{}},
			provErr:     errors.New("fail"),
			engStopped:  []bool{false},
			expErr:      errors.New("fail"),
		},
		"scan local; no bdevs in config; devlist passed to backend": {
			req:         &ctlpb.ScanNvmeReq{Health: true},
			engTierCfgs: []storage.TierConfigs{{}},
			engStopped:  []bool{false},
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					proto.MockNvmeController(1),
				},
				State: new(ctlpb.ResponseState),
			},
			expBackendScanCalls: []storage.BdevScanRequest{
				{DeviceList: new(storage.BdevDeviceList)},
			},
		},
		// This should succeed so nil NVMe stats can be returned in SCM-only scenarios.
		"scan local; no bdevs in config; meta requested": {
			req:         &ctlpb.ScanNvmeReq{Health: true, Meta: true},
			engTierCfgs: []storage.TierConfigs{{}},
			engStopped:  []bool{false},
			provRes:     &storage.BdevScanResponse{},
			expResp: &ctlpb.ScanNvmeResp{
				State: new(ctlpb.ResponseState),
			},
			expBackendScanCalls: []storage.BdevScanRequest{
				{DeviceList: new(storage.BdevDeviceList)},
			},
		},
		"scan local; bdevs in config; meta requested": {
			req: &ctlpb.ScanNvmeReq{Health: true, Meta: true},
			engTierCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(1),
							test.MockPCIAddr(2)),
				},
			},
			engStopped: []bool{true},
			expErr:     errors.New("info unavailable"),
		},
		"scan local; bdevs in config; devlist passed to backend; no roles": {
			req: &ctlpb.ScanNvmeReq{Health: true},
			engTierCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(1),
							test.MockPCIAddr(2)),
				},
			},
			provRes: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
				},
			},
			engStopped: []bool{true},
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(1)
						c.SmdDevices = []*ctlpb.SmdDevice{
							{
								Rank:     uint32(ranklist.NilRank),
								RoleBits: 0, // No roles.
							},
						}
						return c
					}(),
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.SmdDevices = []*ctlpb.SmdDevice{
							{
								Rank:     uint32(ranklist.NilRank),
								RoleBits: 0, // No roles.
							},
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
		"scan local; bdevs in config; devlist passed to backend; roles from cfg": {
			req: &ctlpb.ScanNvmeReq{Health: true},
			engTierCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(1)).
						WithBdevDeviceRoles(storage.BdevRoleWAL),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(2)).
						WithBdevDeviceRoles(storage.BdevRoleMeta | storage.BdevRoleData),
				},
			},
			provRes: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
				},
			},
			engStopped: []bool{true},
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(1)
						c.SmdDevices = []*ctlpb.SmdDevice{
							{
								Rank:     uint32(ranklist.NilRank),
								RoleBits: uint32(storage.BdevRoleWAL),
							},
						}
						return c
					}(),
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.SmdDevices = []*ctlpb.SmdDevice{
							{
								Rank:     uint32(ranklist.NilRank),
								RoleBits: uint32(storage.BdevRoleMeta | storage.BdevRoleData),
							},
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
		"scan local; bdevs in config; devlist passed to backend; retry on empty response": {
			req: &ctlpb.ScanNvmeReq{Health: true},
			engTierCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(1),
							test.MockPCIAddr(2)),
				},
			},
			provRes: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{},
			},
			engStopped: []bool{true},
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{},
				State:  new(ctlpb.ResponseState),
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
		"scan remote; bdevs in config": {
			req: &ctlpb.ScanNvmeReq{Health: true, Meta: true},
			engTierCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(1),
							test.MockPCIAddr(2)),
				},
			},
			engStopped: []bool{false},
			engErr:     []error{nil},
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					proto.MockNvmeController(2),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan remote; collate results from multiple engines": {
			req: &ctlpb.ScanNvmeReq{Health: true, Meta: true},
			engTierCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(1),
							test.MockPCIAddr(2)),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(3),
							test.MockPCIAddr(4)),
				},
			},
			engRes: []ctlpb.ScanNvmeResp{
				{
					Ctrlrs: proto.NvmeControllers{
						proto.MockNvmeController(1),
						proto.MockNvmeController(2),
					},
					State: new(ctlpb.ResponseState),
				},
				{
					Ctrlrs: proto.NvmeControllers{
						proto.MockNvmeController(3),
						proto.MockNvmeController(4),
					},
					State: new(ctlpb.ResponseState),
				},
			},
			engErr:     []error{nil, nil},
			engStopped: []bool{false, false},
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					proto.MockNvmeController(1),
					proto.MockNvmeController(2),
					proto.MockNvmeController(3),
					proto.MockNvmeController(4),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan remote; both engine scans fail": {
			req: &ctlpb.ScanNvmeReq{Health: true, Meta: true},
			engTierCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(1),
							test.MockPCIAddr(2)),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(3),
							test.MockPCIAddr(4)),
				},
			},
			engRes:     []ctlpb.ScanNvmeResp{{}, {}},
			engErr:     []error{errors.New("fail1"), errors.New("fail2")},
			engStopped: []bool{false, false},
			expErr:     errors.New("fail2"),
		},
		"scan remote; partial results with one failed engine scan": {
			req: &ctlpb.ScanNvmeReq{Health: true, Meta: true},
			engTierCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(1),
							test.MockPCIAddr(2)),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(3),
							test.MockPCIAddr(4)),
				},
			},
			engRes: []ctlpb.ScanNvmeResp{
				{},
				{
					Ctrlrs: proto.NvmeControllers{
						proto.MockNvmeController(3),
						proto.MockNvmeController(4),
					},
					State: new(ctlpb.ResponseState),
				},
			},
			engErr:     []error{errors.New("fail"), nil},
			engStopped: []bool{false, false},
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					proto.MockNvmeController(3),
					proto.MockNvmeController(4),
				},
				State: &ctlpb.ResponseState{
					Error:  "instance 0: fail",
					Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
				},
			},
		},
		"scan remote; filter results based on request basic flag": {
			req: &ctlpb.ScanNvmeReq{Basic: true},
			engTierCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(1),
							test.MockPCIAddr(2)),
				},
			},
			engRes: []ctlpb.ScanNvmeResp{
				{
					Ctrlrs: proto.NvmeControllers{
						proto.MockNvmeController(1),
						proto.MockNvmeController(2),
					},
					State: new(ctlpb.ResponseState),
				},
			},
			engErr:     []error{nil},
			engStopped: []bool{false},
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						nc := proto.MockNvmeController(1)
						nc.HealthStats = nil
						nc.SmdDevices = nil
						nc.FwRev = ""
						nc.Model = ""
						return nc
					}(),
					func() *ctlpb.NvmeController {
						nc := proto.MockNvmeController(2)
						nc.HealthStats = nil
						nc.SmdDevices = nil
						nc.FwRev = ""
						nc.Model = ""
						return nc
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan local; filter results based on request basic flag": {
			req: &ctlpb.ScanNvmeReq{Basic: true},
			engTierCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(1),
							test.MockPCIAddr(2)),
				},
			},
			provRes: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
				},
			},
			engStopped: []bool{true},
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						nc := proto.MockNvmeController(1)
						nc.HealthStats = nil
						nc.SmdDevices = nil
						nc.FwRev = ""
						nc.Model = ""
						return nc
					}(),
					func() *ctlpb.NvmeController {
						nc := proto.MockNvmeController(2)
						nc.HealthStats = nil
						nc.SmdDevices = nil
						nc.FwRev = ""
						nc.Model = ""
						return nc
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
		"scan local; bdevs in config; vmd enabled": {
			req: &ctlpb.ScanNvmeReq{},
			engTierCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList("0000:05:05.5"),
				},
			},
			provRes: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{
					&storage.NvmeController{PciAddr: "050505:01:00.0"},
					&storage.NvmeController{PciAddr: "050505:03:00.0"},
				},
			},
			engStopped: []bool{true},
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
		"scan remote; bdevs in config; vmd enabled": {
			req: &ctlpb.ScanNvmeReq{Meta: true},
			engTierCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList("0000:05:05.5"),
				},
			},
			engRes: []ctlpb.ScanNvmeResp{
				{
					Ctrlrs: proto.NvmeControllers{
						&ctlpb.NvmeController{PciAddr: "050505:01:00.0"},
					},
					State: new(ctlpb.ResponseState),
				},
			},
			engErr:     []error{nil},
			engStopped: []bool{false},
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					&ctlpb.NvmeController{PciAddr: "050505:01:00.0"},
				},
				State: new(ctlpb.ResponseState),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.provRes == nil {
				tc.provRes = defProviderScanRes
			}
			if tc.engRes == nil {
				tc.engRes = []ctlpb.ScanNvmeResp{*defEngineScanRes}
			}

			if len(tc.engStopped) != len(tc.engTierCfgs) {
				t.Fatal("len tc.engStopped != len tc.tierCfgs")
			}

			idx := 0
			// Mock per-engine-scan function to focus on unit testing bdevScan().
			scanEngineBdevs = func(_ context.Context, _ Engine, _ *ctlpb.ScanNvmeReq) (*ctlpb.ScanNvmeResp, error) {
				if len(tc.engRes) <= idx {
					t.Fatal("engine scan called but response not specified")
				}
				if len(tc.engErr) <= idx {
					t.Fatal("engine scan called but error not specified")
				}
				engRes := tc.engRes[idx]
				engErr := tc.engErr[idx]
				idx++
				return &engRes, engErr
			}
			defer func() {
				scanEngineBdevs = bdevScanEngine
			}()

			engCfgs := []*engine.Config{}
			for _, tcs := range tc.engTierCfgs {
				engCfg := engine.MockConfig().WithStorage(tcs...)
				engCfgs = append(engCfgs, engCfg)
			}
			sCfg := config.DefaultServer().WithEngines(engCfgs...)

			bmbc := &bdev.MockBackendConfig{
				ScanRes: tc.provRes,
				ScanErr: tc.provErr,
			}
			bmb := bdev.NewMockBackend(bmbc)
			smb := scm.NewMockBackend(nil)

			cs := newMockControlServiceFromBackends(t, log, sCfg, bmb, smb, nil,
				tc.engStopped...)

			resp, err := bdevScan(test.Context(t), cs, tc.req, nil)
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

func TestServer_CtlSvc_StorageScan(t *testing.T) {
	ctrlr := storage.MockNvmeController()
	ctrlr.SmdDevices = nil
	ctrlrPB := proto.MockNvmeController()
	ctrlrPB.HealthStats = nil
	ctrlrPB.SmdDevices = nil
	ctrlrPB2 := proto.MockNvmeController(2)
	ctrlrPB2.HealthStats = nil
	ctrlrPB2.SmdDevices = nil
	ctrlrPBwHealth := proto.MockNvmeController()
	ctrlrPBwHealth.SmdDevices = nil
	ctrlrPBBasic := proto.MockNvmeController()
	ctrlrPBBasic.HealthStats = nil
	ctrlrPBBasic.SmdDevices = nil
	ctrlrPBBasic.FwRev = ""
	ctrlrPBBasic.Model = ""

	for name, tc := range map[string]struct {
		req             *ctlpb.StorageScanReq
		bdevScanRes     *ctlpb.ScanNvmeResp
		bdevScanErr     error
		smbc            *scm.MockBackendConfig
		tierCfgs        storage.TierConfigs
		enginesNotReady bool
		expResp         *ctlpb.StorageScanResp
		expErr          error
	}{
		"successful scan; scm namespaces": {
			bdevScanRes: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					ctrlrPB,
					ctrlrPB2,
				},
				State: new(ctlpb.ResponseState),
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes:    storage.ScmModules{storage.MockScmModule()},
				GetNamespacesRes: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
			tierCfgs: storage.TierConfigs{
				storage.NewTierConfig().
					WithStorageClass(storage.ClassNvme.String()).
					WithBdevDeviceList(ctrlr.PciAddr, test.MockPCIAddr(2)),
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{
						ctrlrPB,
						ctrlrPB2,
					},
					State: new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					Namespaces: proto.ScmNamespaces{proto.MockScmNamespace()},
					State:      new(ctlpb.ResponseState),
				},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"successful scan; no scm namespaces": {
			bdevScanRes: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					ctrlrPB,
				},
				State: new(ctlpb.ResponseState),
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes: storage.ScmModules{storage.MockScmModule()},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPB},
					State:  new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					Modules: proto.ScmModules{proto.MockScmModule()},
					State:   new(ctlpb.ResponseState),
				},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"successful scan; multiple bdev tiers in config": {
			bdevScanRes: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					ctrlrPB,
					ctrlrPB2,
				},
				State: new(ctlpb.ResponseState),
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes: storage.ScmModules{storage.MockScmModule()},
			},
			tierCfgs: storage.TierConfigs{
				storage.NewTierConfig().
					WithStorageClass(storage.ClassNvme.String()).
					WithBdevDeviceList(test.MockPCIAddr(1)),
				storage.NewTierConfig().
					WithStorageClass(storage.ClassNvme.String()).
					WithBdevDeviceList(test.MockPCIAddr(2)),
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{
						ctrlrPB,
						ctrlrPB2,
					},
					State: new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					Modules: proto.ScmModules{proto.MockScmModule()},
					State:   new(ctlpb.ResponseState),
				},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"spdk scan failure": {
			bdevScanRes: &ctlpb.ScanNvmeResp{
				State: &ctlpb.ResponseState{
					Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
					Error:  "spdk scan failed",
				},
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes:    storage.ScmModules{storage.MockScmModule()},
				GetNamespacesRes: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					State: &ctlpb.ResponseState{
						Error:  "spdk scan failed",
						Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
					},
				},
				Scm: &ctlpb.ScanScmResp{
					Namespaces: proto.ScmNamespaces{proto.MockScmNamespace()},
					State:      new(ctlpb.ResponseState),
				},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"scm module discovery failure": {
			bdevScanRes: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					ctrlrPB,
				},
				State: new(ctlpb.ResponseState),
			},
			smbc: &scm.MockBackendConfig{
				GetModulesErr: errors.New("scm discover failed"),
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPB},
					State:  new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					State: &ctlpb.ResponseState{
						Error:  "scm discover failed",
						Status: ctlpb.ResponseStatus_CTL_ERR_SCM,
					},
				},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"all discover fail": {
			bdevScanRes: &ctlpb.ScanNvmeResp{
				State: &ctlpb.ResponseState{
					Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
					Error:  "spdk scan failed",
				},
			},
			smbc: &scm.MockBackendConfig{
				GetModulesErr: errors.New("scm discover failed"),
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					State: &ctlpb.ResponseState{
						Error:  "spdk scan failed",
						Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
					},
				},
				Scm: &ctlpb.ScanScmResp{
					State: &ctlpb.ResponseState{
						Error:  "scm discover failed",
						Status: ctlpb.ResponseStatus_CTL_ERR_SCM,
					},
				},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"scan bdev; vmd enabled": {
			bdevScanRes: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					&ctlpb.NvmeController{PciAddr: "050505:01:00.0"},
				},
				State: new(ctlpb.ResponseState),
			},
			tierCfgs: storage.TierConfigs{
				storage.NewTierConfig().
					WithStorageClass(storage.ClassNvme.String()).
					WithBdevDeviceList("0000:05:05.5"),
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{
						&ctlpb.NvmeController{PciAddr: "050505:01:00.0"},
					},
					State: new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					State: new(ctlpb.ResponseState),
				},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"scan usage; engines not ready": {
			req: &ctlpb.StorageScanReq{
				Scm: &ctlpb.ScanScmReq{
					Usage: true,
				},
				Nvme: &ctlpb.ScanNvmeReq{
					Meta: true,
				},
			},
			enginesNotReady: true,
			expErr:          errors.New("no scm details found"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			engineCfg := engine.MockConfig().WithStorage(tc.tierCfgs...)
			engineCfgs := []*engine.Config{engineCfg}
			sCfg := config.DefaultServer().WithEngines(engineCfgs...)

			var cs *ControlService
			if tc.enginesNotReady {
				cs = mockControlService(t, log, sCfg, nil, tc.smbc, nil, true)
			} else {
				cs = mockControlService(t, log, sCfg, nil, tc.smbc, nil)
			}

			scanBdevs = func(_ context.Context, c *ControlService, _ *ctlpb.ScanNvmeReq, _ []*ctlpb.ScmNamespace) (*ctlpb.ScanNvmeResp, error) {
				return tc.bdevScanRes, tc.bdevScanErr
			}
			defer func() {
				scanBdevs = bdevScan
			}()

			if tc.req == nil {
				tc.req = &ctlpb.StorageScanReq{
					Scm:  new(ctlpb.ScanScmReq),
					Nvme: new(ctlpb.ScanNvmeReq),
				}
			}

			resp, err := cs.StorageScan(test.Context(t), tc.req)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, resp, defStorageScanCmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestServer_checkTmpfsMem(t *testing.T) {
	for name, tc := range map[string]struct {
		scmCfgs     map[int]*storage.TierConfig
		memInfoErr  error
		memAvailGiB int
		expErr      error
	}{
		"pmem tier; skip check": {
			scmCfgs: map[int]*storage.TierConfig{
				0: {
					Class: storage.ClassDcpm,
				},
			},
		},
		"meminfo fetch fails": {
			scmCfgs: map[int]*storage.TierConfig{
				0: {
					Class: storage.ClassRam,
					Scm: storage.ScmConfig{
						RamdiskSize: 5,
					},
				},
			},
			memInfoErr: errors.New("fail"),
			expErr:     errors.New("fail"),
		},
		"single engine; ram tier; perform check; low mem": {
			scmCfgs: map[int]*storage.TierConfig{
				0: {
					Class: storage.ClassRam,
					Scm: storage.ScmConfig{
						RamdiskSize: 5,
					},
				},
			},
			memAvailGiB: 4,
			expErr: storage.FaultRamdiskLowMem("Available", 5*humanize.GiByte,
				4.5*humanize.GiByte, 4*humanize.GiByte),
		},
		"single engine; ram tier; perform check": {
			scmCfgs: map[int]*storage.TierConfig{
				0: {
					Class: storage.ClassRam,
					Scm: storage.ScmConfig{
						RamdiskSize: 5,
					},
				},
			},
			memAvailGiB: 5,
		},
		"dual engine; ram tier; perform check; low mem": {
			scmCfgs: map[int]*storage.TierConfig{
				0: {
					Class: storage.ClassRam,
					Scm: storage.ScmConfig{
						RamdiskSize: 80,
					},
				},
				1: {
					Class: storage.ClassRam,
					Scm: storage.ScmConfig{
						RamdiskSize: 80,
					},
				},
			},
			memAvailGiB: 140,
			expErr: storage.FaultRamdiskLowMem("Available", 160*humanize.GiByte,
				144*humanize.GiByte, 140*humanize.GiByte),
		},
		"dual engine; ram tier; perform check": {
			scmCfgs: map[int]*storage.TierConfig{
				1: {
					Class: storage.ClassRam,
					Scm: storage.ScmConfig{
						RamdiskSize: 80,
					},
				},
				0: {
					Class: storage.ClassRam,
					Scm: storage.ScmConfig{
						RamdiskSize: 80,
					},
				},
			},
			memAvailGiB: 145,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			getMemInfo := func() (*common.MemInfo, error) {
				return &common.MemInfo{
					HugepageSizeKiB: 2048,
					MemAvailableKiB: (humanize.GiByte * tc.memAvailGiB) / humanize.KiByte,
				}, tc.memInfoErr
			}

			gotErr := checkTmpfsMem(log, tc.scmCfgs, getMemInfo)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestServer_CtlSvc_StorageFormat(t *testing.T) {
	mockNvmeController0 := storage.MockNvmeController(0)
	mockNvmeController1 := storage.MockNvmeController(1)

	for name, tc := range map[string]struct {
		scmMounted       bool // if scmMounted we emulate ext4 fs is mounted
		tmpfsEmpty       bool // if false, an already-mounted ramdisk is not empty
		superblockExists bool
		instancesStarted bool // engine already started
		sMounts          []string
		sClass           storage.Class
		sDevs            []string
		sSize            int
		bClass           storage.Class
		bDevs            [][]string
		bSize            int
		bmbcs            []*bdev.MockBackendConfig
		awaitTimeout     time.Duration
		getMemInfo       func() (*common.MemInfo, error)
		expAwaitExit     bool
		expAwaitErr      error
		expResp          *ctlpb.StorageFormatResp
		reformat         bool // indicates setting of reformat parameter
	}{
		"ram no nvme": {
			sMounts: []string{"/mnt/daos"},
			sClass:  storage.ClassRam,
			sSize:   6,
			bmbcs:   []*bdev.MockBackendConfig{{}},
			expResp: &ctlpb.StorageFormatResp{
				Crets: []*ctlpb.NvmeControllerResult{},
				Mrets: []*ctlpb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State:    new(ctlpb.ResponseState),
					},
				},
			},
		},
		"dcpm no nvme": {
			sMounts: []string{"/mnt/daos"},
			sClass:  storage.ClassDcpm,
			sDevs:   []string{"/dev/pmem1"},
			bmbcs:   []*bdev.MockBackendConfig{{}},
			expResp: &ctlpb.StorageFormatResp{
				Crets: []*ctlpb.NvmeControllerResult{},
				Mrets: []*ctlpb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State:    new(ctlpb.ResponseState),
					},
				},
			},
		},
		"nvme and ram": {
			sMounts: []string{"/mnt/daos"},
			sClass:  storage.ClassRam,
			sDevs:   []string{"/dev/pmem1"}, // ignored if SCM class is ram
			sSize:   6,
			bClass:  storage.ClassNvme,
			bDevs:   [][]string{{mockNvmeController0.PciAddr}},
			bmbcs: []*bdev.MockBackendConfig{
				{
					ScanRes: &storage.BdevScanResponse{
						Controllers: storage.NvmeControllers{mockNvmeController0},
					},
					FormatRes: &storage.BdevFormatResponse{
						DeviceResponses: storage.BdevDeviceFormatResponses{
							mockNvmeController0.PciAddr: &storage.BdevDeviceFormatResponse{
								Formatted: true,
							},
						},
					},
				},
			},
			expResp: &ctlpb.StorageFormatResp{
				Crets: []*ctlpb.NvmeControllerResult{
					{
						PciAddr: mockNvmeController0.PciAddr,
						State:   new(ctlpb.ResponseState),
					},
				},
				Mrets: []*ctlpb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State:    new(ctlpb.ResponseState),
					},
				},
			},
		},
		"aio file no size and ram": {
			sMounts: []string{"/mnt/daos"},
			sClass:  storage.ClassRam,
			sDevs:   []string{"/dev/pmem1"}, // ignored if SCM class is ram
			sSize:   6,
			bClass:  storage.ClassFile,
			bDevs:   [][]string{{"/tmp/daos-bdev"}},
			bSize:   6,
			bmbcs: []*bdev.MockBackendConfig{
				{
					ScanRes: &storage.BdevScanResponse{
						Controllers: storage.NvmeControllers{},
					},
					FormatRes: &storage.BdevFormatResponse{
						DeviceResponses: storage.BdevDeviceFormatResponses{
							"/tmp/daos-bdev": new(storage.BdevDeviceFormatResponse),
						},
					},
				},
			},
			expResp: &ctlpb.StorageFormatResp{
				Crets: []*ctlpb.NvmeControllerResult{
					{
						PciAddr: "/tmp/daos-bdev",
						State:   new(ctlpb.ResponseState),
					},
				},
				Mrets: []*ctlpb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State:    new(ctlpb.ResponseState),
					},
				},
			},
		},
		"nvme and dcpm": {
			sMounts: []string{"/mnt/daos"},
			sClass:  storage.ClassDcpm,
			sDevs:   []string{"dev/pmem0"},
			bClass:  storage.ClassNvme,
			bDevs:   [][]string{{mockNvmeController0.PciAddr}},
			bmbcs: []*bdev.MockBackendConfig{
				{
					ScanRes: &storage.BdevScanResponse{
						Controllers: storage.NvmeControllers{mockNvmeController0},
					},
					FormatRes: &storage.BdevFormatResponse{
						DeviceResponses: storage.BdevDeviceFormatResponses{
							mockNvmeController0.PciAddr: &storage.BdevDeviceFormatResponse{
								Formatted: true,
							},
						},
					},
				},
			},
			expResp: &ctlpb.StorageFormatResp{
				Crets: []*ctlpb.NvmeControllerResult{
					{
						PciAddr: mockNvmeController0.PciAddr,
						State:   new(ctlpb.ResponseState),
					},
				},
				Mrets: []*ctlpb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State:    new(ctlpb.ResponseState),
					},
				},
			},
		},
		"io instance already running": { // await should exit immediately
			instancesStarted: true,
			scmMounted:       true,
			sMounts:          []string{"/mnt/daos"},
			sClass:           storage.ClassRam,
			sSize:            6,
			bClass:           storage.ClassNvme,
			bDevs:            [][]string{{mockNvmeController0.PciAddr}},
			bmbcs: []*bdev.MockBackendConfig{
				{
					ScanRes: &storage.BdevScanResponse{
						Controllers: storage.NvmeControllers{mockNvmeController0},
					},
				},
			},
			expAwaitExit: true,
			expAwaitErr:  errors.New("can't wait for storage: instance 0 already started"),
			awaitTimeout: time.Second,
			expResp: &ctlpb.StorageFormatResp{
				Crets: []*ctlpb.NvmeControllerResult{
					{
						PciAddr: storage.NilBdevAddress,
						State: &ctlpb.ResponseState{
							Status: ctlpb.ResponseStatus_CTL_SUCCESS,
							Info:   fmt.Sprintf(msgNvmeFormatSkip, 0),
						},
					},
				},
				Mrets: []*ctlpb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State: &ctlpb.ResponseState{
							Status: ctlpb.ResponseStatus_CTL_SUCCESS,
							Info:   "SCM is already formatted",
						},
					},
				},
			},
		},
		// if emulated scm (ram) is already formatted and mounted (with
		// superblock) then awaitStorageReady() will not wait and format
		// attempt should fail with no reformat option set
		"ram already mounted no reformat": {
			scmMounted: true,
			sMounts:    []string{"/mnt/daos"},
			sClass:     storage.ClassRam,
			sSize:      6,
			bClass:     storage.ClassNvme,
			bDevs:      [][]string{{mockNvmeController0.PciAddr}},
			bmbcs: []*bdev.MockBackendConfig{
				{
					ScanRes: &storage.BdevScanResponse{
						Controllers: storage.NvmeControllers{mockNvmeController0},
					},
				},
			},
			expResp: &ctlpb.StorageFormatResp{
				Crets: []*ctlpb.NvmeControllerResult{
					{
						PciAddr: storage.NilBdevAddress,
						State: &ctlpb.ResponseState{
							Status: ctlpb.ResponseStatus_CTL_SUCCESS,
							Info:   fmt.Sprintf(msgNvmeFormatSkip, 0),
						},
					},
				},
				Mrets: []*ctlpb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State: &ctlpb.ResponseState{
							Status: ctlpb.ResponseStatus_CTL_SUCCESS,
							Info:   "SCM is already formatted",
						},
					},
				},
			},
		},
		"ram already mounted but empty": {
			scmMounted: true,
			tmpfsEmpty: true,
			sMounts:    []string{"/mnt/daos"},
			sClass:     storage.ClassRam,
			sSize:      6,
			bClass:     storage.ClassNvme,
			bDevs:      [][]string{{mockNvmeController0.PciAddr}},
			bmbcs: []*bdev.MockBackendConfig{
				{
					ScanRes: &storage.BdevScanResponse{
						Controllers: storage.NvmeControllers{mockNvmeController0},
					},
					FormatRes: &storage.BdevFormatResponse{
						DeviceResponses: storage.BdevDeviceFormatResponses{
							mockNvmeController0.PciAddr: &storage.BdevDeviceFormatResponse{
								Formatted: true,
							},
						},
					},
				},
			},
			expResp: &ctlpb.StorageFormatResp{
				Crets: []*ctlpb.NvmeControllerResult{
					{
						PciAddr: mockNvmeController0.PciAddr,
						State:   new(ctlpb.ResponseState),
					},
				},
				Mrets: []*ctlpb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State: &ctlpb.ResponseState{
							Status: ctlpb.ResponseStatus_CTL_SUCCESS,
							Info:   "SCM is already formatted",
						},
					},
				},
			},
		},
		"ram already mounted and reformat set": {
			scmMounted: true,
			reformat:   true,
			sMounts:    []string{"/mnt/daos"},
			sClass:     storage.ClassRam,
			sSize:      6,
			bClass:     storage.ClassNvme,
			bDevs:      [][]string{{mockNvmeController0.PciAddr}},
			bmbcs: []*bdev.MockBackendConfig{
				{
					ScanRes: &storage.BdevScanResponse{
						Controllers: storage.NvmeControllers{mockNvmeController0},
					},
					FormatRes: &storage.BdevFormatResponse{
						DeviceResponses: storage.BdevDeviceFormatResponses{
							mockNvmeController0.PciAddr: &storage.BdevDeviceFormatResponse{
								Formatted: true,
							},
						},
					},
				},
			},
			expResp: &ctlpb.StorageFormatResp{
				Crets: []*ctlpb.NvmeControllerResult{
					{
						PciAddr: mockNvmeController0.PciAddr,
						State:   new(ctlpb.ResponseState),
					},
				},
				Mrets: []*ctlpb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State:    new(ctlpb.ResponseState),
					},
				},
			},
		},
		"dcpm already mounted no reformat": {
			scmMounted: true,
			sMounts:    []string{"/mnt/daos"},
			sClass:     storage.ClassDcpm,
			sDevs:      []string{"/dev/pmem1"},
			bClass:     storage.ClassNvme,
			bDevs:      [][]string{{mockNvmeController0.PciAddr}},
			bmbcs: []*bdev.MockBackendConfig{
				{
					ScanRes: &storage.BdevScanResponse{
						Controllers: storage.NvmeControllers{mockNvmeController0},
					},
				},
			},
			expResp: &ctlpb.StorageFormatResp{
				Crets: []*ctlpb.NvmeControllerResult{
					{
						PciAddr: storage.NilBdevAddress,
						State: &ctlpb.ResponseState{
							Status: ctlpb.ResponseStatus_CTL_SUCCESS,
							Info:   fmt.Sprintf(msgNvmeFormatSkip, 0),
						},
					},
				},
				Mrets: []*ctlpb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State: &ctlpb.ResponseState{
							Status: ctlpb.ResponseStatus_CTL_SUCCESS,
							Info:   "SCM is already formatted",
						},
					},
				},
			},
		},
		"dcpm already mounted and reformat set": {
			scmMounted: true,
			reformat:   true,
			sMounts:    []string{"/mnt/daos"},
			sClass:     storage.ClassDcpm,
			sDevs:      []string{"/dev/pmem1"},
			bClass:     storage.ClassNvme,
			bDevs:      [][]string{{mockNvmeController0.PciAddr}},
			bmbcs: []*bdev.MockBackendConfig{
				{
					ScanRes: &storage.BdevScanResponse{
						Controllers: storage.NvmeControllers{mockNvmeController0},
					},
					FormatRes: &storage.BdevFormatResponse{
						DeviceResponses: storage.BdevDeviceFormatResponses{
							mockNvmeController0.PciAddr: &storage.BdevDeviceFormatResponse{
								Formatted: true,
							},
						},
					},
				},
			},
			expResp: &ctlpb.StorageFormatResp{
				Crets: []*ctlpb.NvmeControllerResult{
					{
						PciAddr: mockNvmeController0.PciAddr,
						State:   new(ctlpb.ResponseState),
					},
				},
				Mrets: []*ctlpb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State:    new(ctlpb.ResponseState),
					},
				},
			},
		},
		// if superblock exists, awaitStorageReady() won't wait
		"superblock exists and reformat set": {
			scmMounted:       true,
			superblockExists: true,
			reformat:         true,
			sMounts:          []string{"/mnt/daos"},
			sClass:           storage.ClassDcpm,
			sDevs:            []string{"/dev/pmem1"},
			bClass:           storage.ClassNvme,
			bDevs:            [][]string{{mockNvmeController0.PciAddr}},
			bmbcs: []*bdev.MockBackendConfig{
				{
					ScanRes: &storage.BdevScanResponse{
						Controllers: storage.NvmeControllers{mockNvmeController0},
					},
				},
			},
			expAwaitExit: true,
			awaitTimeout: time.Second,
			expResp: &ctlpb.StorageFormatResp{
				Mrets: []*ctlpb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State:    new(ctlpb.ResponseState),
					},
				},
			},
		},
		"nvme and dcpm success multi-io": {
			sMounts: []string{"/mnt/daos0", "/mnt/daos1"},
			sClass:  storage.ClassDcpm,
			sDevs:   []string{"/dev/pmem0", "/dev/pmem1"},
			bClass:  storage.ClassNvme,
			bDevs: [][]string{
				{mockNvmeController0.PciAddr},
				{mockNvmeController1.PciAddr},
			},
			// One for each engine.
			bmbcs: []*bdev.MockBackendConfig{
				{
					ScanRes: &storage.BdevScanResponse{
						Controllers: storage.NvmeControllers{mockNvmeController0},
					},
					FormatRes: &storage.BdevFormatResponse{
						DeviceResponses: storage.BdevDeviceFormatResponses{
							mockNvmeController0.PciAddr: &storage.BdevDeviceFormatResponse{
								Formatted: true,
							},
						},
					},
				},
				{
					ScanRes: &storage.BdevScanResponse{
						Controllers: storage.NvmeControllers{mockNvmeController1},
					},
					FormatRes: &storage.BdevFormatResponse{
						DeviceResponses: storage.BdevDeviceFormatResponses{
							mockNvmeController1.PciAddr: &storage.BdevDeviceFormatResponse{
								Formatted: true,
							},
						},
					},
				},
			},
			expResp: &ctlpb.StorageFormatResp{
				Crets: []*ctlpb.NvmeControllerResult{
					{
						PciAddr: mockNvmeController0.PciAddr,
						State:   new(ctlpb.ResponseState),
					},
					{
						// this should be id 1 but mock
						// backend spits same output for
						// both I/O Engine instances
						PciAddr: mockNvmeController0.PciAddr,
						State:   new(ctlpb.ResponseState),
					},
				},
				Mrets: []*ctlpb.ScmMountResult{
					{
						Mntpoint:    "/mnt/daos0",
						State:       new(ctlpb.ResponseState),
						Instanceidx: 0,
					},
					{
						Mntpoint:    "/mnt/daos1",
						State:       new(ctlpb.ResponseState),
						Instanceidx: 1,
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			testDir, cleanup := test.CreateTestDir(t)
			defer cleanup()

			if tc.expResp == nil {
				t.Fatal("expResp test case parameter required")
			}
			test.AssertEqual(t, len(tc.sMounts), len(tc.expResp.Mrets), name)
			test.AssertEqual(t, len(tc.sMounts), len(tc.bmbcs), name)
			for i := range tc.sMounts {
				// Hack to deal with creating the mountpoint in test.
				// FIXME (DAOS-3471): The tests in this layer really shouldn't be
				// reaching down far enough to actually interact with the filesystem.
				tc.sMounts[i] = filepath.Join(testDir, tc.sMounts[i])
				if len(tc.expResp.Mrets) > 0 {
					mp := &(tc.expResp.Mrets[i].Mntpoint)
					if *mp != "" {
						if strings.HasSuffix(tc.sMounts[i], *mp) {
							*mp = tc.sMounts[i]
						}
					}
				}
			}

			config := config.DefaultServer()

			// validate test parameters
			if len(tc.sDevs) > 0 {
				test.AssertEqual(t, len(tc.sMounts), len(tc.sDevs), name)
			} else {
				tc.sDevs = []string{"/dev/pmem0", "/dev/pmem1"}
			}
			if len(tc.bDevs) > 0 {
				test.AssertEqual(t, len(tc.sMounts), len(tc.bDevs), name)
			} else {
				tc.bDevs = [][]string{{}, {}}
			}

			// map SCM mount targets to source devices
			devToMount := make(map[string]string)

			// add all I/O Engine configurations
			for idx, scmMount := range tc.sMounts {
				if tc.sClass == storage.ClassDcpm {
					devToMount[tc.sDevs[idx]] = scmMount
					t.Logf("sDevs[%d]= %v, value= %v", idx, tc.sDevs[idx], scmMount)
				}
				engine := engine.MockConfig().
					WithStorage(
						storage.NewTierConfig().
							WithScmMountPoint(scmMount).
							WithStorageClass(tc.sClass.String()).
							WithScmRamdiskSize(uint(tc.sSize)).
							WithScmDeviceList(tc.sDevs[idx]),
						storage.NewTierConfig().
							WithStorageClass(tc.bClass.String()).
							WithBdevFileSize(tc.bSize).
							WithBdevDeviceList(tc.bDevs[idx]...),
					)
				config.Engines = append(config.Engines, engine)
			}

			getFsRetStr := "none"
			if tc.scmMounted {
				getFsRetStr = "ext4"
			}
			smsc := &system.MockSysConfig{
				IsMountedBool:  tc.scmMounted,
				GetfsStr:       getFsRetStr,
				SourceToTarget: devToMount,
			}
			if tc.sClass == storage.ClassRam {
				total := uint64(1234)
				avail := total
				if !tc.tmpfsEmpty {
					avail--
				}
				smsc.GetfsUsageResps = []system.GetfsUsageRetval{
					{
						Total: total,
						Avail: avail,
					},
				}
			}
			sysProv := system.NewMockSysProvider(log, smsc)
			mounter := mount.NewProvider(log, sysProv)
			scmProv := scm.NewProvider(log, nil, sysProv, mounter)
			bdevProv := bdev.NewMockProvider(log, nil)
			if tc.getMemInfo == nil {
				tc.getMemInfo = func() (*common.MemInfo, error) {
					return &common.MemInfo{
						MemAvailableKiB: (6 * humanize.GiByte) / humanize.KiByte,
					}, nil
				}
			}

			mscs := NewMockStorageControlService(log, config.Engines, sysProv, scmProv,
				bdevProv, tc.getMemInfo)

			ctxEvt, cancelEvtCtx := context.WithCancel(context.Background())
			t.Cleanup(cancelEvtCtx)

			cs := &ControlService{
				StorageControlService: *mscs,
				harness:               &EngineHarness{log: log},
				events:                events.NewPubSub(ctxEvt, log),
				srvCfg:                config,
			}

			for i, ec := range config.Engines {
				root := filepath.Dir(tc.sMounts[i])
				if tc.scmMounted {
					root = tc.sMounts[i]
				}
				if err := os.MkdirAll(root, 0777); err != nil {
					t.Fatal(err)
				}

				trc := &engine.TestRunnerConfig{}
				trc.Running.Store(tc.instancesStarted)
				runner := engine.NewTestRunner(trc, ec)

				// Engine specific bdev provider.
				ebp := bdev.NewMockProvider(log, tc.bmbcs[i])
				esp := storage.MockProvider(log, 0, &ec.Storage, sysProv,
					scmProv, ebp, nil)

				ei := NewEngineInstance(log, esp, nil, runner)
				ei.ready.Store(tc.instancesStarted)

				// if the instance is expected to have a valid superblock, create one
				if tc.superblockExists {
					if err := ei.createSuperblock(); err != nil {
						t.Fatal(err)
					}
				} else {
					ei.setSuperblock(nil)
				}

				if err := cs.harness.AddInstance(ei); err != nil {
					t.Fatal(err)
				}
			}

			instances := cs.harness.Instances()
			test.AssertEqual(t, len(tc.sMounts), len(instances), "nr mounts != nr instances")

			ctx, cancel := context.WithCancel(test.Context(t))
			if tc.awaitTimeout != 0 {
				ctx, cancel = context.WithTimeout(ctx, tc.awaitTimeout)
			}
			t.Cleanup(cancel)

			// Trigger await storage ready on each instance and send results to
			// awaitCh. awaitStorageReady() will set "waitFormat" flag, fire off
			// "onAwaitFormat" callbacks, select on "storageReady" channel then
			// finally unset "waitFormat" flag.
			awaitCh := make(chan error)
			for _, ei := range instances {
				t.Logf("call awaitStorageReady() (%d)", ei.Index())
				go func(ctx context.Context, e *EngineInstance) {
					select {
					case <-ctx.Done():
					case awaitCh <- e.awaitStorageReady(ctx):
					}
				}(ctx, ei.(*EngineInstance))
			}

			// When all instances are in awaiting format state ("waitFormat" set),
			// close awaitingFormat channel to signal ready state.
			awaitingFormat := make(chan struct{})
			t.Log("polling on 'waitFormat' state(s)")
			go func(ctxIn context.Context) {
				for {
					ready := true
					for _, ei := range instances {
						if !ei.(*EngineInstance).isAwaitingFormat() {
							ready = false
						}
					}
					if ready {
						close(awaitingFormat)
						return
					}
					select {
					case <-time.After(testShortTimeout):
					case <-ctxIn.Done():
						return
					}
				}
			}(ctx)

			select {
			case <-awaitingFormat:
				t.Log("storage is ready and waiting for format")
			case err := <-awaitCh:
				t.Log("rx on awaitCh from unusual awaitStorageReady() returns")
				test.CmpErr(t, tc.expAwaitErr, err)
				if !tc.expAwaitExit {
					t.Fatal("unexpected exit from awaitStorageReady()")
				}
			case <-ctx.Done():
				t.Logf("context done (%s)", ctx.Err())
				test.CmpErr(t, tc.expAwaitErr, ctx.Err())
				if tc.expAwaitErr == nil {
					t.Fatal(ctx.Err())
				}
				if !tc.scmMounted {
					t.Fatalf("unexpected behavior of awaitStorageReady")
				}
			}

			resp, fmtErr := cs.StorageFormat(test.Context(t), &ctlpb.StorageFormatReq{
				Reformat: tc.reformat,
			})
			if fmtErr != nil {
				t.Fatal(fmtErr)
			}

			test.AssertEqual(t, len(tc.expResp.Crets), len(resp.Crets),
				"number of controller results")
			test.AssertEqual(t, len(tc.expResp.Mrets), len(resp.Mrets),
				"number of mount results")
			for _, exp := range tc.expResp.Crets {
				match := false
				for _, got := range resp.Crets {
					if diff := cmp.Diff(exp, got, test.DefaultCmpOpts()...); diff == "" {
						match = true
					}
				}
				if !match {
					t.Fatalf("unexpected results: (\nwant: %+v\ngot: %+v)",
						tc.expResp.Crets, resp.Crets)
				}
			}
			for _, exp := range tc.expResp.Mrets {
				match := false
				for _, got := range resp.Mrets {
					if diff := cmp.Diff(exp, got, test.DefaultCmpOpts()...); diff == "" {
						match = true
					}
				}
				if !match {
					t.Fatalf("unexpected results: (\nwant: %+v\ngot: %+v)",
						tc.expResp.Mrets, resp.Mrets)
				}
			}
		})
	}
}

func TestServer_CtlSvc_StorageNvmeRebind(t *testing.T) {
	usrCurrent, _ := user.Current()
	username := usrCurrent.Username

	for name, tc := range map[string]struct {
		req         *ctlpb.NvmeRebindReq
		bmbc        *bdev.MockBackendConfig
		expErr      error
		expResp     *ctlpb.NvmeRebindResp
		expPrepCall *storage.BdevPrepareRequest
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"failure": {
			req: &ctlpb.NvmeRebindReq{
				PciAddr: test.MockPCIAddr(1),
			},
			bmbc: &bdev.MockBackendConfig{
				PrepareErr: errors.New("failure"),
			},
			expPrepCall: &storage.BdevPrepareRequest{
				TargetUser:   username,
				PCIAllowList: test.MockPCIAddr(1),
			},
			expResp: &ctlpb.NvmeRebindResp{
				State: &ctlpb.ResponseState{
					Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
					Error:  "nvme rebind: failure",
				},
			},
		},
		"success": {
			req: &ctlpb.NvmeRebindReq{
				PciAddr: test.MockPCIAddr(1),
			},
			bmbc: &bdev.MockBackendConfig{},
			expPrepCall: &storage.BdevPrepareRequest{
				TargetUser:   username,
				PCIAllowList: test.MockPCIAddr(1),
			},
			expResp: &ctlpb.NvmeRebindResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mbb := bdev.NewMockBackend(tc.bmbc)
			mbp := bdev.NewProvider(log, mbb)
			scs := NewMockStorageControlService(log, nil, nil,
				scm.NewMockProvider(log, nil, nil), mbp, nil)
			cs := &ControlService{StorageControlService: *scs}

			resp, err := cs.StorageNvmeRebind(test.Context(t), tc.req)

			mbb.RLock()
			if tc.expPrepCall == nil {
				if len(mbb.PrepareCalls) != 0 {
					t.Fatal("unexpected number of prepared calls")
				}
			} else {
				if len(mbb.PrepareCalls) != 1 {
					t.Fatal("unexpected number of prepared calls")
				}
				if diff := cmp.Diff(*tc.expPrepCall, mbb.PrepareCalls[0]); diff != "" {
					t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
				}
			}
			mbb.RUnlock()

			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, resp, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestServer_CtlSvc_StorageNvmeAddDevice(t *testing.T) {
	for name, tc := range map[string]struct {
		req         *ctlpb.NvmeAddDeviceReq
		bmbc        *bdev.MockBackendConfig
		storageCfgs []storage.TierConfigs
		expErr      error
		expDevList  []string
		expResp     *ctlpb.NvmeAddDeviceResp
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"missing engine index 0": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          test.MockPCIAddr(1),
				StorageTierIndex: -1,
			},
			expErr: errors.New("engine with index 0"),
		},
		"missing engine index 1": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          test.MockPCIAddr(1),
				EngineIndex:      1,
				StorageTierIndex: -1,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(0)),
				},
			},
			expErr: errors.New("engine with index 1"),
		},
		"zero bdev configs": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          test.MockPCIAddr(1),
				StorageTierIndex: -1,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
				},
			},
			expErr: errors.New("no bdev storage tiers"),
		},
		"missing bdev config index 0": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          test.MockPCIAddr(1),
				StorageTierIndex: 0,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(0)),
				},
			},
			expErr: errors.New("storage tier with index 0"),
		},
		"missing bdev config index 2": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          test.MockPCIAddr(1),
				StorageTierIndex: 2,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(0)),
				},
			},
			expErr: errors.New("storage tier with index 2"),
		},
		"success; bdev config index unspecified": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          test.MockPCIAddr(1),
				StorageTierIndex: -1,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(0)),
				},
			},
			expResp: &ctlpb.NvmeAddDeviceResp{},
			expDevList: []string{
				test.MockPCIAddr(0), test.MockPCIAddr(1),
			},
		},
		"success; bdev config index specified": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          test.MockPCIAddr(1),
				StorageTierIndex: 1,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(0)),
				},
			},
			expResp: &ctlpb.NvmeAddDeviceResp{},
			expDevList: []string{
				test.MockPCIAddr(0), test.MockPCIAddr(1),
			},
		},
		"failure; write config failed": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          test.MockPCIAddr(1),
				StorageTierIndex: -1,
			},
			bmbc: &bdev.MockBackendConfig{
				WriteConfErr: errors.New("failure"),
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(0)),
				},
			},
			expResp: &ctlpb.NvmeAddDeviceResp{
				State: &ctlpb.ResponseState{
					Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
					Error:  "write nvme config for engine 0: failure",
				},
			},
			expDevList: []string{
				test.MockPCIAddr(0), test.MockPCIAddr(1),
			},
		},
		"zero bdev configs; engine index 1": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          test.MockPCIAddr(1),
				EngineIndex:      1,
				StorageTierIndex: -1,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(0)),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
				},
			},
			expErr: errors.New("no bdev storage tiers"),
		},
		"missing bdev config index 0; engine index 1": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          test.MockPCIAddr(1),
				EngineIndex:      1,
				StorageTierIndex: 0,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(0)),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(0)),
				},
			},
			expErr: errors.New("storage tier with index 0"),
		},
		"missing bdev config index 2; engine index 1": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          test.MockPCIAddr(1),
				EngineIndex:      1,
				StorageTierIndex: 2,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(0)),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(0)),
				},
			},
			expErr: errors.New("storage tier with index 2"),
		},
		"success; bdev config index specified; engine index 1": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          test.MockPCIAddr(1),
				EngineIndex:      1,
				StorageTierIndex: 1,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(0)),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(0)),
				},
			},
			expResp: &ctlpb.NvmeAddDeviceResp{},
			expDevList: []string{
				test.MockPCIAddr(0), test.MockPCIAddr(1),
			},
		},
		"failure; write config failed; engine index 1": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          test.MockPCIAddr(1),
				EngineIndex:      1,
				StorageTierIndex: -1,
			},
			bmbc: &bdev.MockBackendConfig{
				WriteConfErr: errors.New("failure"),
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(0)),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(0)),
				},
			},
			expResp: &ctlpb.NvmeAddDeviceResp{
				State: &ctlpb.ResponseState{
					Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
					Error:  "write nvme config for engine 1: failure",
				},
			},
			expDevList: []string{
				test.MockPCIAddr(0), test.MockPCIAddr(1),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			engineCfgs := []*engine.Config{}
			for idx, tierCfgs := range tc.storageCfgs {
				ec := engine.MockConfig().WithStorage(tierCfgs...)
				ec.Index = uint32(idx)
				engineCfgs = append(engineCfgs, ec)
			}
			serverCfg := config.DefaultServer().WithEngines(engineCfgs...)

			cs := mockControlService(t, log, serverCfg, tc.bmbc, nil, nil)

			resp, err := cs.StorageNvmeAddDevice(test.Context(t), tc.req)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, resp, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}

			// Verify device list has been updated
			es := cs.harness.Instances()[tc.req.EngineIndex].GetStorage()
			// Assumption made that all test cases have 1 SCM tier and by this point
			// at least one bdev tier
			if tc.req.StorageTierIndex == -1 {
				tc.req.StorageTierIndex = 1
			} else if tc.req.StorageTierIndex == 0 {
				t.Fatal("tier index expected to be > 0")
			}
			gotDevs := es.GetBdevConfigs()[tc.req.StorageTierIndex-1].Bdev.DeviceList.Strings()
			if diff := cmp.Diff(tc.expDevList, gotDevs, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestServer_CtlSvc_adjustNvmeSize(t *testing.T) {
	const (
		clusterSize     uint64 = 32 * humanize.MiByte
		hugeClusterSize uint64 = humanize.GiByte
		metaSize        uint64 = 64 * humanize.MiByte
		metaWalSize     uint64 = 128 * humanize.MiByte
		rdbSize         uint64 = 256 * humanize.MiByte
		rdbWalSize      uint64 = 512 * humanize.MiByte
	)

	type StorageCfg struct {
		targetCount int
		tierCfgs    storage.TierConfigs
	}
	type DataInput struct {
		storageCfgs  []*StorageCfg
		scanNvmeResp *ctlpb.ScanNvmeResp
	}
	type ExpectedOutput struct {
		totalBytes     []uint64
		availableBytes []uint64
		usableBytes    []uint64
		message        string
	}

	newTierCfg := func(pciIdx int32) *storage.TierConfig {
		return storage.NewTierConfig().
			WithStorageClass(storage.ClassNvme.String()).
			WithBdevDeviceList(test.MockPCIAddr(pciIdx))
	}

	newNvmeCtlr := func(nvmeCtlr *ctlpb.NvmeController) *ctlpb.NvmeController {
		for _, smdDev := range nvmeCtlr.SmdDevices {
			smdDev.ClusterSize = clusterSize
			smdDev.MetaSize = metaSize
			smdDev.MetaWalSize = metaWalSize
			smdDev.RdbSize = rdbSize
			smdDev.RdbWalSize = rdbWalSize
		}

		return nvmeCtlr
	}

	for name, tc := range map[string]struct {
		input  DataInput
		output ExpectedOutput
	}{
		"homogeneous": {
			input: DataInput{
				storageCfgs: []*StorageCfg{
					{
						targetCount: 12,
						tierCfgs: storage.TierConfigs{
							newTierCfg(1),
							newTierCfg(2),
							newTierCfg(3),
						},
					},
					{
						targetCount: 6,
						tierCfgs: storage.TierConfigs{
							newTierCfg(4),
							newTierCfg(5),
						},
					},
				},
				scanNvmeResp: &ctlpb.ScanNvmeResp{
					Ctrlrs: []*ctlpb.NvmeController{
						{
							PciAddr: test.MockPCIAddr(1),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme0",
									TgtIds:      []int32{0, 1, 2, 3},
									TotalBytes:  10 * hugeClusterSize,
									AvailBytes:  10 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
							DevState: devStateNormal,
						},
						{
							PciAddr: test.MockPCIAddr(2),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme1",
									TgtIds:      []int32{4, 5, 6, 7},
									TotalBytes:  10 * hugeClusterSize,
									AvailBytes:  10 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        0,
								},
							},
							DevState: devStateNormal,
						},
						{
							PciAddr: test.MockPCIAddr(3),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme2",
									TgtIds:      []int32{8, 9, 10, 11},
									TotalBytes:  20 * hugeClusterSize,
									AvailBytes:  20 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
							DevState: devStateNormal,
						},
						{
							PciAddr: test.MockPCIAddr(4),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme3",
									TgtIds:      []int32{0, 1, 2},
									TotalBytes:  20 * hugeClusterSize,
									AvailBytes:  20 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        1,
								},
							},
							DevState: devStateNormal,
						},
						{
							PciAddr: test.MockPCIAddr(5),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme4",
									TgtIds:      []int32{3, 4, 5},
									TotalBytes:  20 * hugeClusterSize,
									AvailBytes:  20 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        1,
									RoleBits:    storage.BdevRoleData,
								},
							},
							DevState: devStateNormal,
						},
					},
				},
			},
			output: ExpectedOutput{
				totalBytes: []uint64{
					10 * hugeClusterSize,
					10 * hugeClusterSize,
					20 * hugeClusterSize,
					20 * hugeClusterSize,
					20 * hugeClusterSize,
				},
				availableBytes: []uint64{
					10 * hugeClusterSize,
					10 * hugeClusterSize,
					20 * hugeClusterSize,
					20 * hugeClusterSize,
					20 * hugeClusterSize,
				},
				usableBytes: []uint64{
					8 * hugeClusterSize,
					8 * hugeClusterSize,
					8 * hugeClusterSize,
					18 * hugeClusterSize,
					18 * hugeClusterSize,
				},
			},
		},
		"heterogeneous": {
			input: DataInput{
				storageCfgs: []*StorageCfg{
					{
						targetCount: 11,
						tierCfgs: storage.TierConfigs{
							newTierCfg(1),
							newTierCfg(2),
							newTierCfg(3),
						},
					},
					{
						targetCount: 5,
						tierCfgs: storage.TierConfigs{
							newTierCfg(4),
							newTierCfg(5),
						},
					},
				},
				scanNvmeResp: &ctlpb.ScanNvmeResp{
					Ctrlrs: []*ctlpb.NvmeController{
						{
							PciAddr: test.MockPCIAddr(1),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme0",
									TgtIds:      []int32{0, 1, 2, 3},
									TotalBytes:  10 * hugeClusterSize,
									AvailBytes:  10 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
							DevState: devStateNormal,
						},
						{
							PciAddr: test.MockPCIAddr(2),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme1",
									TgtIds:      []int32{4, 5, 6},
									TotalBytes:  10 * hugeClusterSize,
									AvailBytes:  10 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
							DevState: devStateNormal,
						},
						{
							PciAddr: test.MockPCIAddr(3),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme2",
									TgtIds:      []int32{7, 8, 9, 10},
									TotalBytes:  20 * hugeClusterSize,
									AvailBytes:  20 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
							DevState: devStateNormal,
						},
						{
							PciAddr: test.MockPCIAddr(4),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme3",
									TgtIds:      []int32{0, 1, 2},
									TotalBytes:  20 * hugeClusterSize,
									AvailBytes:  20 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        1,
									RoleBits:    storage.BdevRoleData,
								},
							},
							DevState: devStateNormal,
						},
						{
							PciAddr: test.MockPCIAddr(5),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme4",
									TgtIds:      []int32{3, 4},
									TotalBytes:  20 * hugeClusterSize,
									AvailBytes:  20 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        1,
									RoleBits:    storage.BdevRoleData,
								},
							},
							DevState: devStateNormal,
						},
					},
				},
			},
			output: ExpectedOutput{
				totalBytes: []uint64{
					10 * hugeClusterSize,
					10 * hugeClusterSize,
					20 * hugeClusterSize,
					20 * hugeClusterSize,
					20 * hugeClusterSize,
				},
				availableBytes: []uint64{
					10 * hugeClusterSize,
					10 * hugeClusterSize,
					20 * hugeClusterSize,
					20 * hugeClusterSize,
					20 * hugeClusterSize,
				},
				usableBytes: []uint64{
					8 * hugeClusterSize,
					6 * hugeClusterSize,
					8 * hugeClusterSize,
					18 * hugeClusterSize,
					12 * hugeClusterSize,
				},
			},
		},
		"new": {
			input: DataInput{
				storageCfgs: []*StorageCfg{
					{
						targetCount: 7,
						tierCfgs: storage.TierConfigs{
							newTierCfg(1),
							newTierCfg(2),
						},
					},
				},
				scanNvmeResp: &ctlpb.ScanNvmeResp{
					Ctrlrs: []*ctlpb.NvmeController{
						{
							PciAddr: test.MockPCIAddr(1),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme0",
									TgtIds:      []int32{0, 1, 2, 3},
									TotalBytes:  10 * hugeClusterSize,
									AvailBytes:  10 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
							DevState: devStateNormal,
						},
						{
							PciAddr: test.MockPCIAddr(2),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme1",
									TgtIds:      []int32{0, 1, 2},
									TotalBytes:  10 * hugeClusterSize,
									AvailBytes:  10 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
							DevState: devStateNew,
						},
					},
				},
			},
			output: ExpectedOutput{
				totalBytes: []uint64{
					10 * hugeClusterSize,
					10 * hugeClusterSize,
				},
				availableBytes: []uint64{
					10 * hugeClusterSize,
					0,
				},
				usableBytes: []uint64{
					8 * hugeClusterSize,
					0,
				},
				message: "not usable: device state \"NEW\"",
			},
		},
		"evicted": {
			input: DataInput{
				storageCfgs: []*StorageCfg{
					{
						targetCount: 7,
						tierCfgs: storage.TierConfigs{
							newTierCfg(1),
							newTierCfg(2),
						},
					},
				},
				scanNvmeResp: &ctlpb.ScanNvmeResp{
					Ctrlrs: []*ctlpb.NvmeController{
						{
							PciAddr: test.MockPCIAddr(1),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme0",
									TgtIds:      []int32{0, 1, 2, 3},
									TotalBytes:  10 * hugeClusterSize,
									AvailBytes:  10 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
							DevState: devStateNormal,
						},
						{
							PciAddr: test.MockPCIAddr(2),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme1",
									TgtIds:      []int32{0, 1, 2},
									TotalBytes:  10 * hugeClusterSize,
									AvailBytes:  10 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
							DevState: devStateFaulty,
						},
					},
				},
			},
			output: ExpectedOutput{
				totalBytes: []uint64{
					10 * hugeClusterSize,
					10 * hugeClusterSize,
				},
				availableBytes: []uint64{
					10 * hugeClusterSize,
					0,
				},
				usableBytes: []uint64{
					8 * hugeClusterSize,
					0,
				},
				message: "not usable: device state \"EVICTED\"",
			},
		},
		"missing targets": {
			input: DataInput{
				storageCfgs: []*StorageCfg{
					{
						targetCount: 4,
						tierCfgs: storage.TierConfigs{
							newTierCfg(1),
							newTierCfg(2),
						},
					},
				},
				scanNvmeResp: &ctlpb.ScanNvmeResp{
					Ctrlrs: []*ctlpb.NvmeController{
						{
							PciAddr: test.MockPCIAddr(1),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme0",
									TgtIds:      []int32{0, 1, 2, 3},
									TotalBytes:  10 * hugeClusterSize,
									AvailBytes:  10 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
							DevState: devStateNormal,
						},
						{
							PciAddr: test.MockPCIAddr(2),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme1",
									TgtIds:      []int32{},
									TotalBytes:  10 * hugeClusterSize,
									AvailBytes:  10 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
							DevState: devStateNormal,
						},
					},
				},
			},
			output: ExpectedOutput{
				totalBytes: []uint64{
					10 * hugeClusterSize,
					10 * hugeClusterSize,
				},
				availableBytes: []uint64{
					10 * hugeClusterSize,
					0,
				},
				usableBytes: []uint64{
					8 * hugeClusterSize,
					0,
				},
				message: "not usable: missing storage info",
			},
		},
		"missing cluster size": {
			input: DataInput{
				storageCfgs: []*StorageCfg{
					{
						targetCount: 7,
						tierCfgs: storage.TierConfigs{
							newTierCfg(1),
							newTierCfg(2),
						},
					},
				},
				scanNvmeResp: &ctlpb.ScanNvmeResp{
					Ctrlrs: []*ctlpb.NvmeController{
						{
							PciAddr: test.MockPCIAddr(1),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme0",
									TgtIds:      []int32{0, 1, 2, 3},
									TotalBytes:  10 * hugeClusterSize,
									AvailBytes:  10 * hugeClusterSize,
									ClusterSize: hugeClusterSize,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
							DevState: devStateNormal,
						},
						{
							PciAddr: test.MockPCIAddr(2),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:       "nvme1",
									TgtIds:     []int32{0, 1, 2},
									TotalBytes: 10 * hugeClusterSize,
									AvailBytes: 10 * hugeClusterSize,
									Rank:       0,
									RoleBits:   storage.BdevRoleData,
								},
							},
							DevState: devStateNormal,
						},
					},
				},
			},
			output: ExpectedOutput{
				totalBytes: []uint64{
					10 * hugeClusterSize,
					10 * hugeClusterSize,
				},
				availableBytes: []uint64{
					10 * hugeClusterSize,
					0,
				},
				usableBytes: []uint64{
					8 * hugeClusterSize,
					0,
				},
				message: "not usable: missing storage info",
			},
		},
		"multi bdev tier": {
			input: DataInput{
				storageCfgs: []*StorageCfg{
					{
						targetCount: 5,
						tierCfgs:    storage.TierConfigs{newTierCfg(1)},
					},
					{
						targetCount: 4,
						tierCfgs:    storage.TierConfigs{newTierCfg(2)},
					},
					{
						targetCount: 6,
						tierCfgs:    storage.TierConfigs{newTierCfg(3)},
					},
					{
						targetCount: 4,
						tierCfgs:    storage.TierConfigs{newTierCfg(4)},
					},
					{
						targetCount: 5,
						tierCfgs:    storage.TierConfigs{newTierCfg(5)},
					},
					{
						targetCount: 6,
						tierCfgs:    storage.TierConfigs{newTierCfg(6)},
					},
				},
				scanNvmeResp: &ctlpb.ScanNvmeResp{
					Ctrlrs: []*ctlpb.NvmeController{
						newNvmeCtlr(&ctlpb.NvmeController{
							PciAddr: test.MockPCIAddr(1),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme0",
									TgtIds:      []int32{0, 1, 2, 3},
									TotalBytes:  10 * humanize.GiByte,
									AvailBytes:  10 * humanize.GiByte,
									ClusterSize: clusterSize,
									Rank:        0,
									RoleBits:    storage.BdevRoleData | storage.BdevRoleMeta,
								},
							},
							DevState: devStateNormal,
						}),
						newNvmeCtlr(&ctlpb.NvmeController{
							PciAddr: test.MockPCIAddr(2),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme1",
									TgtIds:      []int32{0, 1, 2, 3},
									TotalBytes:  10 * humanize.GiByte,
									AvailBytes:  10 * humanize.GiByte,
									ClusterSize: clusterSize,
									Rank:        1,
									RoleBits:    storage.BdevRoleData | storage.BdevRoleWAL,
								},
							},
							DevState: devStateNormal,
						}),
						newNvmeCtlr(&ctlpb.NvmeController{
							PciAddr: test.MockPCIAddr(3),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme2",
									TgtIds:      []int32{0, 1, 2, 3},
									TotalBytes:  10 * humanize.GiByte,
									AvailBytes:  10 * humanize.GiByte,
									ClusterSize: clusterSize,
									Rank:        2,
									RoleBits:    storage.BdevRoleAll,
								},
							},
							DevState: devStateNormal,
						}),
						newNvmeCtlr(&ctlpb.NvmeController{
							PciAddr: test.MockPCIAddr(4),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme3",
									TgtIds:      []int32{0, 1, 2, 3},
									TotalBytes:  10 * humanize.GiByte,
									AvailBytes:  10 * humanize.GiByte,
									ClusterSize: clusterSize,
									Rank:        3,
									RoleBits:    storage.BdevRoleWAL,
								},
							},
							DevState: devStateNormal,
						}),
						newNvmeCtlr(&ctlpb.NvmeController{
							PciAddr: test.MockPCIAddr(5),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme4",
									TgtIds:      []int32{0, 1, 2, 3},
									TotalBytes:  10 * humanize.GiByte,
									AvailBytes:  10 * humanize.GiByte,
									ClusterSize: clusterSize,
									Rank:        4,
									RoleBits:    storage.BdevRoleMeta,
								},
							},
							DevState: devStateNormal,
						}),
						newNvmeCtlr(&ctlpb.NvmeController{
							PciAddr: test.MockPCIAddr(6),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:        "nvme5",
									TgtIds:      []int32{0, 1, 2, 3},
									TotalBytes:  10 * humanize.GiByte,
									AvailBytes:  10 * humanize.GiByte,
									ClusterSize: clusterSize,
									Rank:        5,
									RoleBits:    storage.BdevRoleMeta | storage.BdevRoleMeta,
								},
							},
							DevState: devStateNormal,
						}),
					},
				},
			},
			output: ExpectedOutput{
				totalBytes: []uint64{
					320 * clusterSize,
					320 * clusterSize,
					320 * clusterSize,
					0 * humanize.GiByte,
					0 * humanize.GiByte,
					0 * humanize.GiByte,
				},
				availableBytes: []uint64{
					320 * clusterSize,
					320 * clusterSize,
					320 * clusterSize,
					0 * humanize.GiByte,
					0 * humanize.GiByte,
					0 * humanize.GiByte,
				},
				usableBytes: []uint64{
					300 * clusterSize,
					288 * clusterSize,
					260 * clusterSize,
					0 * humanize.GiByte,
					0 * humanize.GiByte,
					0 * humanize.GiByte,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			engineCfgs := []*engine.Config{}
			for idx, sc := range tc.input.storageCfgs {
				ec := engine.MockConfig().WithStorage(sc.tierCfgs...)
				ec.TargetCount = sc.targetCount
				ec.Index = uint32(idx)
				engineCfgs = append(engineCfgs, ec)
			}
			serverCfg := config.DefaultServer().WithEngines(engineCfgs...)
			cs := mockControlService(t, log, serverCfg, nil, nil, nil)

			cs.adjustNvmeSize(tc.input.scanNvmeResp)

			for idx, ctlr := range tc.input.scanNvmeResp.GetCtrlrs() {
				dev := ctlr.GetSmdDevices()[0]
				test.AssertEqual(t, tc.output.totalBytes[idx], dev.GetTotalBytes(),
					fmt.Sprintf("Invalid total bytes with ctlr %s (index=%d): wait=%d, got=%d",
						ctlr.GetPciAddr(), idx, tc.output.totalBytes[idx], dev.GetTotalBytes()))
				test.AssertEqual(t, tc.output.availableBytes[idx], dev.GetAvailBytes(),
					fmt.Sprintf("Invalid available bytes with ctlr %s (index=%d): wait=%d, got=%d",
						ctlr.GetPciAddr(), idx, tc.output.availableBytes[idx], dev.GetAvailBytes()))
				test.AssertEqual(t, tc.output.usableBytes[idx], dev.GetUsableBytes(),
					fmt.Sprintf("Invalid usable bytes with ctlr %s (index=%d), "+
						"wait=%d (%d clusters) got=%d (%d clusters)",
						ctlr.GetPciAddr(), idx,
						tc.output.usableBytes[idx], tc.output.usableBytes[idx]/clusterSize,
						dev.GetUsableBytes(), dev.GetUsableBytes()/clusterSize))
			}
			if tc.output.message != "" {
				test.AssertTrue(t,
					strings.Contains(buf.String(), tc.output.message),
					"missing message: "+tc.output.message)
			}
		})
	}
}

func TestServer_getRdbSize(t *testing.T) {
	type ExpectedOutput struct {
		size    uint64
		message string
		err     error
	}

	for name, tc := range map[string]struct {
		rdbSize string
		output  ExpectedOutput
	}{
		"simple env var": {
			rdbSize: "DAOS_MD_CAP=1024",
			output: ExpectedOutput{
				size: 1024 * humanize.MiByte,
			},
		},
		"simple default": {
			output: ExpectedOutput{
				size:    defaultRdbSize,
				message: "using default RDB file size",
			},
		},
		"invalid mdcap": {
			rdbSize: "DAOS_MD_CAP=foo",
			output: ExpectedOutput{
				err: errors.New("invalid RDB file size"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			engineCfg := engine.MockConfig()
			if tc.rdbSize != "" {
				engineCfg.WithEnvVars(tc.rdbSize)
			}
			enginesCfg := []*engine.Config{
				engineCfg,
			}
			serverCfg := config.DefaultServer().WithEngines(enginesCfg...)
			cs := mockControlService(t, log, serverCfg, nil, nil, nil)

			size, err := cs.getRdbSize(engineCfg)

			if err != nil {
				test.AssertTrue(t, tc.output.err != nil,
					fmt.Sprintf("Unexpected error %q", err))
				test.CmpErr(t, tc.output.err, err)
				return
			}

			test.AssertTrue(t, err == nil, "Expected error")
			test.AssertEqual(t, tc.output.size, size, "invalid meta data capacity size")
			if tc.output.message != "" {
				test.AssertTrue(t,
					strings.Contains(buf.String(), tc.output.message),
					"missing message: "+tc.output.message)
			}
		})
	}
}

func TestServer_CtlSvc_adjustScmSize(t *testing.T) {
	type EngineConfig struct {
		mdCap       string
		ctrlMdPath  string
		mountPoints []string
	}

	type DataInput struct {
		configs  []*EngineConfig
		response *ctlpb.ScanScmResp
	}

	type ExpectedOutput struct {
		availableBytes []uint64
		usableBytes    []uint64
		message        string
	}

	for name, tc := range map[string]struct {
		input  DataInput
		output ExpectedOutput
	}{
		"single mountPoint": {
			input: DataInput{
				configs: []*EngineConfig{
					{
						mountPoints: []string{"/mnt/daos0"},
					},
				},
				response: &ctlpb.ScanScmResp{
					Namespaces: []*ctlpb.ScmNamespace{
						{
							Mount: &ctlpb.ScmNamespace_Mount{
								Path:       "/mnt/daos0",
								AvailBytes: uint64(64) * humanize.GiByte,
								Class:      storage.ClassFile.String(),
							},
						},
					},
				},
			},
			output: ExpectedOutput{
				availableBytes: []uint64{uint64(64) * humanize.GiByte},
				usableBytes:    []uint64{uint64(64)*humanize.GiByte - defaultRdbSize - mdDaosScmBytes - mdFsScmBytes},
			},
		},
		"three mountPoints": {
			input: DataInput{
				configs: []*EngineConfig{
					{
						mdCap:       "DAOS_MD_CAP=1024",
						mountPoints: []string{"/mnt/daos0", "/mnt/daos1", "/mnt/daos2"},
					},
				},
				response: &ctlpb.ScanScmResp{
					Namespaces: []*ctlpb.ScmNamespace{
						{
							Mount: &ctlpb.ScmNamespace_Mount{
								Path:       "/mnt/daos0",
								AvailBytes: uint64(64) * humanize.GiByte,
								Class:      storage.ClassFile.String(),
							},
						},
						{
							Mount: &ctlpb.ScmNamespace_Mount{
								Path:       "/mnt/daos1",
								AvailBytes: uint64(32) * humanize.GiByte,
								Class:      storage.ClassFile.String(),
							},
						},
						{
							Mount: &ctlpb.ScmNamespace_Mount{
								Path:       "/mnt/daos2",
								AvailBytes: uint64(128) * humanize.GiByte,
								Class:      storage.ClassFile.String(),
							},
						},
					},
				},
			},
			output: ExpectedOutput{
				availableBytes: []uint64{
					uint64(64) * humanize.GiByte,
					uint64(32) * humanize.GiByte,
					uint64(128) * humanize.GiByte,
				},
				usableBytes: []uint64{
					uint64(64)*humanize.GiByte - 1024*humanize.MiByte - mdDaosScmBytes - mdFsScmBytes,
					uint64(32)*humanize.GiByte - 1024*humanize.MiByte - mdDaosScmBytes - mdFsScmBytes,
					uint64(128)*humanize.GiByte - 1024*humanize.MiByte - mdDaosScmBytes - mdFsScmBytes,
				},
			},
		},
		"Missing SCM": {
			input: DataInput{
				configs: []*EngineConfig{
					{
						mdCap:       "DAOS_MD_CAP=1024",
						mountPoints: []string{"/mnt/daos0", "/mnt/daos2"},
					},
				},
				response: &ctlpb.ScanScmResp{
					Namespaces: []*ctlpb.ScmNamespace{
						{
							Mount: &ctlpb.ScmNamespace_Mount{
								Path:       "/mnt/daos0",
								AvailBytes: uint64(64) * humanize.GiByte,
								Class:      storage.ClassFile.String(),
							},
						},
						{
							Mount: &ctlpb.ScmNamespace_Mount{
								Path:       "/mnt/daos1",
								AvailBytes: uint64(32) * humanize.GiByte,
								Class:      storage.ClassFile.String(),
							},
						},
						{
							Mount: &ctlpb.ScmNamespace_Mount{
								Path:       "/mnt/daos2",
								AvailBytes: uint64(128) * humanize.GiByte,
								Class:      storage.ClassFile.String(),
							},
						},
					},
				},
			},
			output: ExpectedOutput{
				availableBytes: []uint64{
					uint64(64) * humanize.GiByte,
					uint64(32) * humanize.GiByte,
					uint64(128) * humanize.GiByte,
				},
				usableBytes: []uint64{
					uint64(64)*humanize.GiByte - 1024*humanize.MiByte - mdDaosScmBytes - mdFsScmBytes,
					0,
					uint64(128)*humanize.GiByte - 1024*humanize.MiByte - mdDaosScmBytes - mdFsScmBytes,
				},
				message: " unknown SCM mount point /mnt/daos1",
			},
		},
		"No more space": {
			input: DataInput{
				configs: []*EngineConfig{
					{
						mountPoints: []string{"/mnt/daos0"},
					},
				},
				response: &ctlpb.ScanScmResp{
					Namespaces: []*ctlpb.ScmNamespace{
						{
							Mount: &ctlpb.ScmNamespace_Mount{
								Path:       "/mnt/daos0",
								AvailBytes: uint64(64) * humanize.KiByte,
								Class:      storage.ClassFile.String(),
							},
						},
					},
				},
			},
			output: ExpectedOutput{
				availableBytes: []uint64{uint64(64) * humanize.KiByte},
				usableBytes:    []uint64{0},
				message:        "No more usable space in SCM device",
			},
		},
		"Multi bdev Tiers": {
			input: DataInput{
				configs: []*EngineConfig{
					{
						mdCap:       "DAOS_MD_CAP=1024",
						ctrlMdPath:  "/mnt",
						mountPoints: []string{"/mnt", "/opt"},
					},
				},
				response: &ctlpb.ScanScmResp{
					Namespaces: []*ctlpb.ScmNamespace{
						{
							Mount: &ctlpb.ScmNamespace_Mount{
								Path:       "/mnt",
								AvailBytes: uint64(64) * humanize.GiByte,
								Class:      storage.ClassFile.String(),
							},
						},
						{
							Mount: &ctlpb.ScmNamespace_Mount{
								Path:       "/opt",
								AvailBytes: uint64(32) * humanize.GiByte,
								Class:      storage.ClassFile.String(),
							},
						},
					},
				},
			},
			output: ExpectedOutput{
				availableBytes: []uint64{
					uint64(64) * humanize.GiByte,
					uint64(32) * humanize.GiByte,
				},
				usableBytes: []uint64{
					uint64(64)*humanize.GiByte - 1024*humanize.MiByte - mdDaosScmBytes - mdFsScmBytes,
					uint64(32)*humanize.GiByte - 1024*humanize.MiByte - mdFsScmBytes,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var enginesCfg []*engine.Config
			for _, cfg := range tc.input.configs {
				engineCfg := engine.MockConfig()
				engineCfg.WithEnvVars(cfg.mdCap)

				var storagesCfg []*storage.TierConfig
				for _, mountPoint := range cfg.mountPoints {
					storageCfg := storage.NewTierConfig()
					storageCfg.WithStorageClass(storage.ClassDcpm.String())
					storageCfg.WithScmMountPoint(mountPoint)
					storagesCfg = append(storagesCfg, storageCfg)
				}

				if cfg.ctrlMdPath != "" {
					engineCfg.WithStorageControlMetadataPath(cfg.ctrlMdPath)

					storageCfg := storage.NewTierConfig()
					storageCfg.WithStorageClass(storage.ClassNvme.String())
					storageCfg.WithBdevDeviceRoles(storage.BdevRoleMeta)
					storagesCfg = append(storagesCfg, storageCfg)
				}

				engineCfg.WithStorage(storagesCfg...)
				enginesCfg = append(enginesCfg, engineCfg)
			}
			serverCfg := config.DefaultServer().WithEngines(enginesCfg...)
			cs := mockControlService(t, log, serverCfg, nil, nil, nil)

			cs.adjustScmSize(tc.input.response)

			for index, namespace := range tc.input.response.Namespaces {
				test.AssertEqual(t, tc.output.availableBytes[index], namespace.GetMount().GetAvailBytes(),
					fmt.Sprintf("Invalid SCM available bytes: nsp=%s, wait=%s (%d bytes), got=%s (%d bytes)",
						namespace.GetMount().GetPath(),
						humanize.Bytes(tc.output.availableBytes[index]), tc.output.availableBytes[index],
						humanize.Bytes(namespace.GetMount().GetAvailBytes()), namespace.GetMount().GetAvailBytes()))
				test.AssertEqual(t, tc.output.usableBytes[index], namespace.GetMount().GetUsableBytes(),
					fmt.Sprintf("Invalid SCM usable bytes: nsp=%s, wait=%s (%d bytes), got=%s (%d bytes)",
						namespace.GetMount().GetPath(),
						humanize.Bytes(tc.output.usableBytes[index]), tc.output.usableBytes[index],
						humanize.Bytes(namespace.GetMount().GetUsableBytes()), namespace.GetMount().GetUsableBytes()))
			}
			if tc.output.message != "" {
				test.AssertTrue(t,
					strings.Contains(buf.String(), tc.output.message),
					"missing message: "+tc.output.message)
			}
		})
	}
}

func TestServer_CtlSvc_getEngineCfgFromNvmeCtl(t *testing.T) {
	type DataInput struct {
		tierCfgs storage.TierConfigs
		nvmeCtlr *ctl.NvmeController
	}
	type ExpectedOutput struct {
		res bool
		msg string
	}

	newTierCfgs := func(tierCfgsSize int32) storage.TierConfigs {
		tierCfgs := make(storage.TierConfigs, tierCfgsSize)
		for idx := range tierCfgs {
			tierCfgs[idx] = storage.NewTierConfig().
				WithStorageClass(storage.ClassNvme.String()).
				WithBdevDeviceList(test.MockPCIAddr(int32(idx + 1)))
		}

		return tierCfgs
	}

	for name, tc := range map[string]struct {
		input  DataInput
		output ExpectedOutput
	}{
		"find NVME Ctlr": {
			input: DataInput{
				tierCfgs: newTierCfgs(5),
				nvmeCtlr: &ctl.NvmeController{
					PciAddr: test.MockPCIAddr(3),
				},
			},
			output: ExpectedOutput{res: true},
		},
		"not find NVME Ctlr": {
			input: DataInput{
				tierCfgs: newTierCfgs(5),
				nvmeCtlr: &ctl.NvmeController{
					PciAddr: test.MockPCIAddr(13),
				},
			},
			output: ExpectedOutput{
				res: false,
				msg: "unknown PCI device",
			},
		},
		"find VMD device": {
			input: DataInput{
				tierCfgs: storage.TierConfigs{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList("0000:04:06.3"),
				},
				nvmeCtlr: &ctl.NvmeController{
					PciAddr: "040603:02:00.0",
				},
			},
			output: ExpectedOutput{res: true},
		},
		"Invalid address": {
			input: DataInput{
				tierCfgs: storage.TierConfigs{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList("0000:04:06.3"),
				},
				nvmeCtlr: &ctl.NvmeController{
					PciAddr: "666",
				},
			},
			output: ExpectedOutput{
				res: false,
				msg: "Invalid PCI address",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			engineCfg := engine.MockConfig().WithStorage(tc.input.tierCfgs...)
			serverCfg := config.DefaultServer().WithEngines(engineCfg)
			cs := mockControlService(t, log, serverCfg, nil, nil, nil)

			ec, err := cs.getEngineCfgFromNvmeCtl(tc.input.nvmeCtlr)

			if tc.output.res {
				test.AssertEqual(t, engineCfg, ec,
					fmt.Sprintf("Invalid engine config: wait=%v got=%v", engineCfg, ec))
				return
			}

			test.AssertEqual(t, (*engine.Config)(nil), ec,
				fmt.Sprintf("Invalid engine config: wait nil"))
			test.AssertTrue(t,
				strings.Contains(err.Error(), tc.output.msg),
				fmt.Sprintf("Invalid error message: %q not contains %q", err, tc.output.msg))
		})
	}
}

func TestServer_CtlSvc_getEngineCfgFromScmNsp(t *testing.T) {
	type DataInput struct {
		tierCfgs storage.TierConfigs
		scmNsp   *ctl.ScmNamespace
	}
	type ExpectedOutput struct {
		res bool
		msg string
	}

	newTierCfgs := func(tierCfgsSize int32) storage.TierConfigs {
		tierCfgs := make(storage.TierConfigs, tierCfgsSize)
		for idx := range tierCfgs {
			tierCfgs[idx] = storage.NewTierConfig().
				WithStorageClass(storage.ClassDcpm.String()).
				WithScmMountPoint(fmt.Sprintf("/mnt/daos%d", idx))
		}

		return tierCfgs
	}

	for name, tc := range map[string]struct {
		input  DataInput
		output ExpectedOutput
	}{
		"find SCM Nsp": {
			input: DataInput{
				tierCfgs: newTierCfgs(5),
				scmNsp: &ctl.ScmNamespace{
					Mount: &ctl.ScmNamespace_Mount{
						Path: "/mnt/daos3",
					},
				},
			},
			output: ExpectedOutput{res: true},
		},
		"not find SCM Nsp": {
			input: DataInput{
				tierCfgs: newTierCfgs(5),
				scmNsp: &ctl.ScmNamespace{
					Mount: &ctl.ScmNamespace_Mount{
						Path: "/mnt/daos666",
					},
				},
			},
			output: ExpectedOutput{
				res: false,
				msg: "unknown SCM mount point"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			engineCfg := engine.MockConfig().WithStorage(tc.input.tierCfgs...)
			serverCfg := config.DefaultServer().WithEngines(engineCfg)
			cs := mockControlService(t, log, serverCfg, nil, nil, nil)

			ec, err := cs.getEngineCfgFromScmNsp(tc.input.scmNsp)

			if tc.output.res {
				test.AssertEqual(t, engineCfg, ec,
					fmt.Sprintf("Invalid engine config: wait=%v got=%v", engineCfg, ec))
				return
			}

			test.AssertEqual(t, (*engine.Config)(nil), ec,
				fmt.Sprintf("Invalid engine config: wait nil"))
			test.AssertTrue(t,
				strings.Contains(err.Error(), tc.output.msg),
				fmt.Sprintf("missing message: %q", tc.output.msg))
		})
	}
}
