//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"os"
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
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

var (
	defStorageScanCmpOpts = append(common.DefaultCmpOpts(),
		protocmp.IgnoreFields(&ctlpb.NvmeController{}, "serial"))
)

func TestServer_CtlSvc_StorageScan_PreIOStart(t *testing.T) {
	ctrlr := storage.MockNvmeController()
	ctrlr.SmdDevices = nil
	ctrlrPB := proto.MockNvmeController()
	ctrlrPB.HealthStats = nil
	ctrlrPB.SmdDevices = nil
	ctrlrPBwHealth := proto.MockNvmeController()
	ctrlrPBwHealth.SmdDevices = nil
	ctrlrPBBasic := proto.MockNvmeController()
	ctrlrPBBasic.HealthStats = nil
	ctrlrPBBasic.SmdDevices = nil
	ctrlrPBBasic.FwRev = ""
	ctrlrPBBasic.Model = ""

	for name, tc := range map[string]struct {
		multiIO     bool
		req         *ctlpb.StorageScanReq
		bmbc        *bdev.MockBackendConfig
		smbc        *scm.MockBackendConfig
		expSetupErr error
		expErr      error
		expResp     ctlpb.StorageScanResp
	}{
		//		"successful scan with bdev and scm namespaces": {
		//			bmbc: &bdev.MockBackendConfig{
		//				ScanRes: &storage.BdevScanResponse{
		//					Controllers: storage.NvmeControllers{ctrlr},
		//				},
		//			},
		//			smbc: &scm.MockBackendConfig{
		//				DiscoverRes:         storage.ScmModules{storage.MockScmModule()},
		//				GetPmemNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
		//			},
		//			expResp: ctlpb.StorageScanResp{
		//				Nvme: &ctlpb.ScanNvmeResp{
		//					Ctrlrs: proto.NvmeControllers{ctrlrPB},
		//					State:  new(ctlpb.ResponseState),
		//				},
		//				Scm: &ctlpb.ScanScmResp{
		//					Namespaces: proto.ScmNamespaces{proto.MockScmNamespace()},
		//					State:      new(ctlpb.ResponseState),
		//				},
		//			},
		//		},
		//		"successful scan no scm namespaces": {
		//			bmbc: &bdev.MockBackendConfig{
		//				ScanRes: &storage.BdevScanResponse{
		//					Controllers: storage.NvmeControllers{ctrlr},
		//				},
		//			},
		//			smbc: &scm.MockBackendConfig{
		//				DiscoverRes: storage.ScmModules{storage.MockScmModule()},
		//			},
		//			expResp: ctlpb.StorageScanResp{
		//				Nvme: &ctlpb.ScanNvmeResp{
		//					Ctrlrs: proto.NvmeControllers{ctrlrPB},
		//					State:  new(ctlpb.ResponseState),
		//				},
		//				Scm: &ctlpb.ScanScmResp{
		//					Modules: proto.ScmModules{proto.MockScmModule()},
		//					State:   new(ctlpb.ResponseState),
		//				},
		//			},
		//		},
		//		"spdk scan failure": {
		//			bmbc: &bdev.MockBackendConfig{
		//				ScanErr: errors.New("spdk scan failed"),
		//			},
		//			smbc: &scm.MockBackendConfig{
		//				DiscoverRes:         storage.ScmModules{storage.MockScmModule()},
		//				GetPmemNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
		//			},
		//			expResp: ctlpb.StorageScanResp{
		//				Nvme: &ctlpb.ScanNvmeResp{
		//					State: &ctlpb.ResponseState{
		//						Error:  "spdk scan failed",
		//						Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
		//					},
		//				},
		//				Scm: &ctlpb.ScanScmResp{
		//					Namespaces: proto.ScmNamespaces{proto.MockScmNamespace()},
		//					State:      new(ctlpb.ResponseState),
		//				},
		//			},
		//		},
		//		"scm module discovery failure": {
		//			bmbc: &bdev.MockBackendConfig{
		//				ScanRes: &storage.BdevScanResponse{
		//					Controllers: storage.NvmeControllers{ctrlr},
		//				},
		//			},
		//			smbc: &scm.MockBackendConfig{
		//				DiscoverErr: errors.New("scm discover failed"),
		//			},
		//			expResp: ctlpb.StorageScanResp{
		//				Nvme: &ctlpb.ScanNvmeResp{
		//					Ctrlrs: proto.NvmeControllers{ctrlrPB},
		//					State:  new(ctlpb.ResponseState),
		//				},
		//				Scm: &ctlpb.ScanScmResp{
		//					State: &ctlpb.ResponseState{
		//						Error:  "scm discover failed",
		//						Status: ctlpb.ResponseStatus_CTL_ERR_SCM,
		//					},
		//				},
		//			},
		//		},
		//		"all discover fail": {
		//			bmbc: &bdev.MockBackendConfig{
		//				ScanErr: errors.New("spdk scan failed"),
		//			},
		//			smbc: &scm.MockBackendConfig{
		//				DiscoverErr: errors.New("scm discover failed"),
		//			},
		//			expResp: ctlpb.StorageScanResp{
		//				Nvme: &ctlpb.ScanNvmeResp{
		//					State: &ctlpb.ResponseState{
		//						Error:  "spdk scan failed",
		//						Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
		//					},
		//				},
		//				Scm: &ctlpb.ScanScmResp{
		//					State: &ctlpb.ResponseState{
		//						Error:  "scm discover failed",
		//						Status: ctlpb.ResponseStatus_CTL_ERR_SCM,
		//					},
		//				},
		//			},
		//		},
		"scan bdev health with single io server down": {
			req: &ctlpb.StorageScanReq{
				Scm: &ctlpb.ScanScmReq{},
				Nvme: &ctlpb.ScanNvmeReq{
					Health: true,
				},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{ctrlr},
				},
			},
			expResp: ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPBwHealth},
					State:  new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					State: new(ctlpb.ResponseState),
				},
			},
		},
		//		"scan bdev health with multiple io servers down": {
		//			multiIO: true,
		//			req: &ctlpb.StorageScanReq{
		//				Scm: &ctlpb.ScanScmReq{},
		//				Nvme: &ctlpb.ScanNvmeReq{
		//					Health: true,
		//				},
		//			},
		//			bmbc: &bdev.MockBackendConfig{
		//				ScanRes: &storage.BdevScanResponse{
		//					Controllers: storage.NvmeControllers{ctrlr},
		//				},
		//			},
		//			expResp: ctlpb.StorageScanResp{
		//				Nvme: &ctlpb.ScanNvmeResp{
		//					// response should not contain duplicates
		//					Ctrlrs: proto.NvmeControllers{ctrlrPBwHealth},
		//					State:  new(ctlpb.ResponseState),
		//				},
		//				Scm: &ctlpb.ScanScmResp{
		//					State: new(ctlpb.ResponseState),
		//				},
		//			},
		//		},
		//		"scan bdev meta with io servers down": {
		//			req: &ctlpb.StorageScanReq{
		//				Scm: &ctlpb.ScanScmReq{},
		//				Nvme: &ctlpb.ScanNvmeReq{
		//					Meta: true,
		//				},
		//			},
		//			bmbc: &bdev.MockBackendConfig{
		//				ScanRes: &storage.BdevScanResponse{
		//					Controllers: storage.NvmeControllers{ctrlr},
		//				},
		//			},
		//			expResp: ctlpb.StorageScanResp{
		//				Nvme: &ctlpb.ScanNvmeResp{
		//					Ctrlrs: proto.NvmeControllers{ctrlrPB},
		//					State:  new(ctlpb.ResponseState),
		//				},
		//				Scm: &ctlpb.ScanScmResp{
		//					State: new(ctlpb.ResponseState),
		//				},
		//			},
		//		},
		//		"scan bdev with nvme basic set": {
		//			req: &ctlpb.StorageScanReq{
		//				Scm: &ctlpb.ScanScmReq{},
		//				Nvme: &ctlpb.ScanNvmeReq{
		//					Basic: true,
		//				},
		//			},
		//			bmbc: &bdev.MockBackendConfig{
		//				ScanRes: &storage.BdevScanResponse{
		//					Controllers: storage.NvmeControllers{ctrlr},
		//				},
		//			},
		//			expResp: ctlpb.StorageScanResp{
		//				Nvme: &ctlpb.ScanNvmeResp{
		//					Ctrlrs: proto.NvmeControllers{ctrlrPBBasic},
		//					State:  new(ctlpb.ResponseState),
		//				},
		//				Scm: &ctlpb.ScanScmResp{
		//					State: new(ctlpb.ResponseState),
		//				},
		//			},
		//		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			//emptyCfg := config.DefaultServer()
			engineCfg := engine.NewConfig().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass(storage.ClassNvme.String()).
						WithBdevDeviceList(storage.MockNvmeController().PciAddr),
				)
			engineCfgs := []*engine.Config{engineCfg}
			if tc.multiIO {
				engineCfgs = append(engineCfgs, engineCfg)
			}
			defaultWithNvme := config.DefaultServer().WithEngines(engineCfgs...)

			// test for both empty and default config cases
			// for _, config := range []*config.Server{defaultWithNvme, emptyCfg} {
			cs := mockControlService(t, log, defaultWithNvme, tc.bmbc, tc.smbc, nil)
			for _, srv := range cs.harness.instances {
				srv.(*EngineInstance).ready.SetFalse()
			}

			// TODO DAOS-8040: re-enable VMD
			// t.Logf("VMD disabled: %v", cs.bdev.IsVMDDisabled())

			// runs discovery for nvme & scm
			err := cs.Setup()
			common.CmpErr(t, tc.expSetupErr, err)
			if err != nil {
				return
			}

			if tc.req == nil {
				tc.req = &ctlpb.StorageScanReq{
					Scm:  new(ctlpb.ScanScmReq),
					Nvme: new(ctlpb.ScanNvmeReq),
				}
			}

			// cs.StorageScan should never return err
			resp, err := cs.StorageScan(context.TODO(), tc.req)
			if err != nil {
				t.Fatal(err)
			}

			if tc.req.Nvme.Health || tc.req.Nvme.Meta {
				if len(cs.harness.instances) == 0 {
					tc.expResp.Nvme.Ctrlrs = nil
				}
			}

			er := tc.expResp.GetNvme()
			ec := er.GetCtrlrs()
			fmt.Printf("c: %v\n\n", len(ec))
			rc := resp.Nvme.GetCtrlrs()[0]
			if diff := cmp.Diff(ec[0], rc, defStorageScanCmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}

			log.Infof("&&& %v\n\n", resp.Nvme.GetCtrlrs())
			log.Infof("&&& %v\n\n", tc.expResp.Nvme.GetCtrlrs())

			if diff := cmp.Diff(tc.expResp, resp, defStorageScanCmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}

		})
	}
}

func TestServer_CtlSvc_StorageScan_PostIOStart(t *testing.T) {
	// output to be returned from mock bdev backend
	newCtrlr := func(idx int32) *storage.NvmeController {
		ctrlr := storage.MockNvmeController(idx)
		ctrlr.Serial = common.MockUUID(idx)
		ctrlr.SmdDevices = nil

		return ctrlr
	}
	newCtrlrMultiNs := func(idx int32, numNss int) *storage.NvmeController {
		ctrlr := storage.MockNvmeController(idx)
		ctrlr.Serial = common.MockUUID(idx)
		ctrlr.SmdDevices = nil
		ctrlr.Namespaces = make([]*storage.NvmeNamespace, numNss)
		for i := 0; i < numNss; i++ {
			ctrlr.Namespaces[i] = storage.MockNvmeNamespace(int32(i + 1))
		}

		return ctrlr
	}

	// expected protobuf output to be returned svc.StorageScan when health
	// updated over drpc. Override serial uuid with variable argument
	newCtrlrHealth := func(idx int32, serialIdx ...int32) (*ctlpb.NvmeController, *ctlpb.BioHealthResp) {
		ctrlr := proto.MockNvmeController(idx)
		sIdx := idx
		if len(serialIdx) > 0 {
			sIdx = serialIdx[0]
		}
		ctrlr.Model = fmt.Sprintf("model-%d", sIdx)
		ctrlr.Serial = common.MockUUID(sIdx)
		ctrlr.HealthStats = proto.MockNvmeHealth(idx + 1)
		ctrlr.SmdDevices = nil

		bioHealthResp := new(ctlpb.BioHealthResp)
		if err := convert.Types(ctrlr.HealthStats, bioHealthResp); err != nil {
			t.Fatal(err)
		}
		bioHealthResp.TotalBytes = uint64(idx) * uint64(humanize.TByte)
		bioHealthResp.AvailBytes = uint64(idx) * uint64(humanize.TByte/2)

		return ctrlr, bioHealthResp
	}
	newCtrlrPBwHealth := func(idx int32, serialIdx ...int32) *ctlpb.NvmeController {
		c, _ := newCtrlrHealth(idx, serialIdx...)
		return c
	}
	newBioHealthResp := func(idx int32, serialIdx ...int32) *ctlpb.BioHealthResp {
		_, b := newCtrlrHealth(idx, serialIdx...)
		return b
	}

	// expected protobuf output to be returned svc.StorageScan when smd
	// updated over drpc
	newCtrlrMeta := func(ctrlrIdx int32, smdIndexes ...int32) (*ctlpb.NvmeController, *ctlpb.SmdDevResp) {
		ctrlr := proto.MockNvmeController(ctrlrIdx)
		ctrlr.Serial = common.MockUUID(ctrlrIdx)
		ctrlr.HealthStats = nil

		if len(smdIndexes) == 0 {
			smdIndexes = append(smdIndexes, ctrlrIdx)
		}
		smdDevRespDevices := make([]*ctlpb.SmdDevResp_Device, len(smdIndexes))
		ctrlr.SmdDevices = make([]*ctlpb.NvmeController_SmdDevice, len(smdIndexes))
		ctrlr.Namespaces = make([]*ctlpb.NvmeController_Namespace, len(smdIndexes))
		for i, idx := range smdIndexes {
			sd := proto.MockSmdDevice(ctrlr.PciAddr, idx+1)
			sd.Rank = uint32(ctrlrIdx)
			sd.TrAddr = ctrlr.PciAddr
			ctrlr.SmdDevices[i] = sd

			smdPB := new(ctlpb.SmdDevResp_Device)
			if err := convert.Types(sd, smdPB); err != nil {
				t.Fatal(err)
			}
			smdDevRespDevices[i] = smdPB

			// expect resultant controller to have updated utilization values
			ctrlr.SmdDevices[i].TotalBytes = uint64(idx) * uint64(humanize.TByte)
			ctrlr.SmdDevices[i].AvailBytes = uint64(idx) * uint64(humanize.TByte/2)
			ctrlr.Namespaces[i] = proto.MockNvmeNamespace(int32(i + 1))
		}

		return ctrlr, &ctlpb.SmdDevResp{Devices: smdDevRespDevices}
	}
	newCtrlrPBwMeta := func(idx int32, smdIndexes ...int32) *ctlpb.NvmeController {
		c, _ := newCtrlrMeta(idx, smdIndexes...)
		return c
	}
	newSmdDevResp := func(idx int32, smdIndexes ...int32) *ctlpb.SmdDevResp {
		_, s := newCtrlrMeta(idx, smdIndexes...)
		return s
	}

	mockPbScmMount := proto.MockScmMountPoint()
	mockPbScmNamespace := proto.MockScmNamespace()
	mockPbScmNamespace.Mount = mockPbScmMount

	for name, tc := range map[string]struct {
		req       *ctlpb.StorageScanReq
		bmbc      *bdev.MockBackendConfig
		smbc      *scm.MockBackendConfig
		smsc      *scm.MockSysConfig
		cfg       *config.Server
		scanTwice bool
		junkResp  bool
		drpcResps map[int][]*mockDrpcResponse
		expErr    error
		expResp   ctlpb.StorageScanResp
	}{
		"scan bdev health with io servers up": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Health: true},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{newCtrlr(1)},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: newSmdDevResp(1)},
					{Message: newBioHealthResp(1)},
				},
			},
			expResp: ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{newCtrlrPBwHealth(1)},
					State:  new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
			},
		},
		"scan bdev meta with io servers up": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{newCtrlr(1)},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: newSmdDevResp(1)},
					{Message: newBioHealthResp(1)},
				},
			},
			expResp: ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{newCtrlrPBwMeta(1)},
					State:  new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
			},
		},
		"scan bdev health with multiple io servers up": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Health: true},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{
						newCtrlr(1), newCtrlr(2),
					},
				},
			},
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().
					WithStorage(
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList(storage.MockNvmeController(1).PciAddr),
					),
				engine.NewConfig().
					WithStorage(
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList(storage.MockNvmeController(2).PciAddr),
					),
			),
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: newSmdDevResp(1)},
					{Message: newBioHealthResp(1)},
				},
				1: {
					{Message: newSmdDevResp(2)},
					{Message: newBioHealthResp(2)},
				},
			},
			expResp: ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{
						newCtrlrPBwHealth(1),
						newCtrlrPBwHealth(2),
					},
					State: new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
			},
		},
		"scan bdev meta with multiple io servers up": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{
						newCtrlr(1), newCtrlr(2),
					},
				},
			},
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().
					WithStorage(
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList(storage.MockNvmeController(1).PciAddr),
					),
				engine.NewConfig().
					WithStorage(
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList(storage.MockNvmeController(2).PciAddr),
					),
			),
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: newSmdDevResp(1)},
					{Message: newBioHealthResp(1)},
				},
				1: {
					{Message: newSmdDevResp(2)},
					{Message: newBioHealthResp(2)},
				},
			},
			expResp: ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{
						newCtrlrPBwMeta(1),
						newCtrlrPBwMeta(2),
					},
					State: new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
			},
		},
		// make sure information is not duplicated in cache
		"verify cache integrity over multiple storage scan calls": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{
						newCtrlr(1), newCtrlr(2),
					},
				},
			},
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().
					WithStorage(
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList(storage.MockNvmeController(1).PciAddr),
					),
				engine.NewConfig().
					WithStorage(
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList(storage.MockNvmeController(2).PciAddr),
					),
			),
			scanTwice: true,
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: newSmdDevResp(1)},
					{Message: newBioHealthResp(1)},
					{Message: newSmdDevResp(1)},
					{Message: newBioHealthResp(1)},
				},
				1: {
					{Message: newSmdDevResp(2)},
					{Message: newBioHealthResp(2)},
					{Message: newSmdDevResp(2)},
					{Message: newBioHealthResp(2)},
				},
			},
			expResp: ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{
						newCtrlrPBwMeta(1),
						newCtrlrPBwMeta(2),
					},
					State: new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
			},
		},
		"scan bdev meta with multiple io servers up with multiple nvme namespaces": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{
						newCtrlrMultiNs(1, 2), newCtrlrMultiNs(2, 2),
					},
				},
			},
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().
					WithStorage(
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList(storage.MockNvmeController(1).PciAddr),
					),
				engine.NewConfig().
					WithStorage(
						storage.NewTierConfig().
							WithBdevClass(storage.ClassNvme.String()).
							WithBdevDeviceList(storage.MockNvmeController(2).PciAddr),
					),
			),
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: newSmdDevResp(1, 1, 2)},
					{Message: newBioHealthResp(1, 1)},
					{Message: newBioHealthResp(2, 1)},
				},
				1: {
					{Message: newSmdDevResp(2, 3, 4)},
					{Message: newBioHealthResp(3, 2)},
					{Message: newBioHealthResp(4, 2)},
				},
			},
			expResp: ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{
						newCtrlrPBwMeta(1, 1, 2),
						newCtrlrPBwMeta(2, 3, 4),
					},
					State: new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
			},
		},
		"scan scm usage": {
			req: &ctlpb.StorageScanReq{
				Scm:  &ctlpb.ScanScmReq{Usage: true},
				Nvme: new(ctlpb.ScanNvmeReq),
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:         storage.ScmModules{storage.MockScmModule()},
				GetPmemNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
			smsc: &scm.MockSysConfig{
				GetfsUsageTotal: mockPbScmMount.TotalBytes,
				GetfsUsageAvail: mockPbScmMount.AvailBytes,
			},
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().
					WithStorage(
						storage.NewTierConfig().
							WithScmClass(storage.ClassDcpm.String()).
							WithScmMountPoint(mockPbScmMount.Path).
							WithScmDeviceList(mockPbScmNamespace.Blockdev),
					),
			),
			drpcResps: map[int][]*mockDrpcResponse{
				0: {},
			},
			expResp: ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					State: new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					Namespaces: proto.ScmNamespaces{mockPbScmNamespace},
					State:      new(ctlpb.ResponseState),
				},
			},
		},
		"scan scm usage with pmem not in instance device list": {
			req: &ctlpb.StorageScanReq{
				Scm:  &ctlpb.ScanScmReq{Usage: true},
				Nvme: new(ctlpb.ScanNvmeReq),
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:         storage.ScmModules{storage.MockScmModule()},
				GetPmemNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
			smsc: &scm.MockSysConfig{
				GetfsUsageTotal: mockPbScmMount.TotalBytes,
				GetfsUsageAvail: mockPbScmMount.AvailBytes,
			},
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().
					WithStorage(
						storage.NewTierConfig().
							WithScmClass(storage.ClassDcpm.String()).
							WithScmMountPoint(mockPbScmMount.Path).
							WithScmDeviceList("/dev/foo", "/dev/bar"),
					),
			),
			drpcResps: map[int][]*mockDrpcResponse{
				0: {},
			},
			expResp: ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					State: new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					State: &ctlpb.ResponseState{
						Status: ctlpb.ResponseStatus_CTL_ERR_SCM,
						Error:  "instance 0: no pmem namespace for mount /mnt/daos1",
					},
				},
			},
		},
		"scan scm usage with class ram": {
			req: &ctlpb.StorageScanReq{
				Scm:  &ctlpb.ScanScmReq{Usage: true},
				Nvme: new(ctlpb.ScanNvmeReq),
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:         storage.ScmModules{storage.MockScmModule()},
				GetPmemNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
			smsc: &scm.MockSysConfig{
				GetfsUsageTotal: mockPbScmMount.TotalBytes,
				GetfsUsageAvail: mockPbScmMount.AvailBytes,
			},
			cfg: config.DefaultServer().WithEngines(
				engine.NewConfig().
					WithStorage(
						storage.NewTierConfig().
							WithScmClass(storage.ClassRam.String()).
							WithScmMountPoint(mockPbScmMount.Path).
							WithScmRamdiskSize(16),
					),
			),
			drpcResps: map[int][]*mockDrpcResponse{
				0: {},
			},
			expResp: ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					State: new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					Namespaces: proto.ScmNamespaces{
						&ctlpb.ScmNamespace{
							Blockdev: "ramdisk",
							Size:     uint64(humanize.GiByte * 16),
							Mount: &ctlpb.ScmNamespace_Mount{
								Path:       mockPbScmMount.Path,
								TotalBytes: mockPbScmMount.TotalBytes,
								AvailBytes: mockPbScmMount.AvailBytes,
							},
						},
					},
					State: new(ctlpb.ResponseState),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.cfg == nil {
				tc.cfg = config.DefaultServer().WithEngines(
					engine.NewConfig().
						WithStorage(
							storage.NewTierConfig().
								WithBdevClass(storage.ClassNvme.String()).
								WithBdevDeviceList(storage.MockNvmeController().PciAddr),
						),
				)
			}
			if len(tc.cfg.Engines) != len(tc.drpcResps) {
				t.Fatalf("num servers in tc.cfg doesn't match num drpc msgs")
			}

			bp := bdev.NewMockProvider(log, tc.bmbc)
			cs := &ControlService{
				StorageControlService: *NewMockStorageControlService(log,
					tc.cfg.Engines,
					scm.NewMockSysProvider(tc.smsc),
					scm.NewMockProvider(log, tc.smbc, tc.smsc),
					bp),
				harness: &EngineHarness{
					log: log,
				},
				events: events.NewPubSub(context.TODO(), log),
				srvCfg: tc.cfg,
			}

			cs.harness.started.SetTrue()
			for i := range cs.harness.instances {
				// replace harness instance with mock I/O Engine
				// to enable mocking of harness instance drpc channel
				sp := storage.MockProvider(log, 0, &tc.cfg.Engines[i].Storage, nil, nil, bp)
				ne := newTestEngine(log, false, sp)
				ne.storage.Sys = cs.storage.Sys
				ne.storage.Scm = cs.storage.Scm
				cs.harness.instances[i] = ne

				cfg := new(mockDrpcClientConfig)
				if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					for _, mock := range tc.drpcResps[i] {
						cfg.setSendMsgResponseList(t, mock)
					}
				}
				ne.setDrpcClient(newMockDrpcClient(cfg))
				ne._superblock.Rank = system.NewRankPtr(uint32(i + 1))
			}

			// TODO DAOS-8040: re-enable VMD
			// t.Logf("VMD disabled: %v", cs.bdev.IsVMDDisabled())

			// runs discovery for nvme & scm
			if err := cs.Setup(); err != nil {
				t.Fatal(err)
			}

			if tc.req == nil {
				tc.req = &ctlpb.StorageScanReq{
					Scm:  new(ctlpb.ScanScmReq),
					Nvme: new(ctlpb.ScanNvmeReq),
				}
			}

			if tc.scanTwice {
				_, err := cs.StorageScan(context.TODO(), tc.req)
				common.CmpErr(t, tc.expErr, err)
				if err != nil {
					return
				}
			}

			resp, err := cs.StorageScan(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, *resp, defStorageScanCmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestServer_CtlSvc_StorageFormat(t *testing.T) {
	mockNvmeController0 := storage.MockNvmeController(0)
	mockNvmeController1 := storage.MockNvmeController(1)

	for name, tc := range map[string]struct {
		scmMounted       bool // if scmMounted we emulate ext4 fs is mounted
		superblockExists bool
		instancesStarted bool // engine already started
		recreateSBs      bool
		mountRet         error
		unmountRet       error
		mkdirRet         error
		removeRet        error
		sMounts          []string
		sClass           storage.Class
		sDevs            []string
		sSize            int
		bClass           storage.Class
		bDevs            [][]string
		bSize            int
		bmbc             *bdev.MockBackendConfig
		awaitTimeout     time.Duration
		expAwaitExit     bool
		expAwaitErr      error
		expResp          *ctlpb.StorageFormatResp
		isRoot           bool
		reformat         bool // indicates setting of reformat parameter
	}{
		"ram no nvme": {
			sMounts: []string{"/mnt/daos"},
			sClass:  storage.ClassRam,
			sSize:   6,
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
			bmbc: &bdev.MockBackendConfig{
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
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{mockNvmeController0},
				},
				FormatRes: &storage.BdevFormatResponse{
					DeviceResponses: storage.BdevDeviceFormatResponses{
						"/tmp/daos-bdev": new(storage.BdevDeviceFormatResponse),
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
			bmbc: &bdev.MockBackendConfig{
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
		"io instances already running": { // await should exit immediately
			instancesStarted: true,
			scmMounted:       true,
			sMounts:          []string{"/mnt/daos"},
			sClass:           storage.ClassRam,
			sSize:            6,
			bClass:           storage.ClassNvme,
			bDevs:            [][]string{{mockNvmeController0.PciAddr}},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{mockNvmeController0},
				},
			},
			expAwaitExit: true,
			expAwaitErr:  errors.New("can't wait for storage: instance 0 already started"),
			awaitTimeout: time.Second,
			expResp: &ctlpb.StorageFormatResp{
				Crets: []*ctlpb.NvmeControllerResult{
					{
						PciAddr: "<nil>",
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
							Status: ctlpb.ResponseStatus_CTL_ERR_SCM,
							Error:  "instance 0: can't format storage of running instance",
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
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{mockNvmeController0},
				},
			},
			expResp: &ctlpb.StorageFormatResp{
				Crets: []*ctlpb.NvmeControllerResult{
					{
						PciAddr: "<nil>",
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
							Status: ctlpb.ResponseStatus_CTL_ERR_SCM,
							Error:  scm.FaultFormatNoReformat.Error(),
							Info:   fault.ShowResolutionFor(scm.FaultFormatNoReformat),
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
			bmbc: &bdev.MockBackendConfig{
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
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{mockNvmeController0},
				},
			},
			expResp: &ctlpb.StorageFormatResp{
				Crets: []*ctlpb.NvmeControllerResult{
					{
						PciAddr: "<nil>",
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
							Status: ctlpb.ResponseStatus_CTL_ERR_SCM,
							Error:  scm.FaultFormatNoReformat.Error(),
							Info:   fault.ShowResolutionFor(scm.FaultFormatNoReformat),
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
			bmbc: &bdev.MockBackendConfig{
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
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{mockNvmeController0},
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
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{mockNvmeController0, mockNvmeController1},
				},
				FormatRes: &storage.BdevFormatResponse{
					DeviceResponses: storage.BdevDeviceFormatResponses{
						mockNvmeController0.PciAddr: &storage.BdevDeviceFormatResponse{
							Formatted: true,
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
			defer common.ShowBufferOnFailure(t, buf)

			testDir, cleanup := common.CreateTestDir(t)
			defer cleanup()

			if tc.expResp == nil {
				t.Fatal("expResp test case parameter required")
			}
			common.AssertEqual(t, len(tc.sMounts), len(tc.expResp.Mrets), name)
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
				common.AssertEqual(t, len(tc.sMounts), len(tc.sDevs), name)
			} else {
				tc.sDevs = []string{"/dev/pmem0", "/dev/pmem1"}
			}
			if len(tc.bDevs) > 0 {
				common.AssertEqual(t, len(tc.sMounts), len(tc.bDevs), name)
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
				engine := engine.NewConfig().
					WithStorage(
						storage.NewTierConfig().
							WithScmMountPoint(scmMount).
							WithScmClass(tc.sClass.String()).
							WithScmRamdiskSize(uint(tc.sSize)).
							WithScmDeviceList(tc.sDevs[idx]),
						storage.NewTierConfig().
							WithBdevClass(tc.bClass.String()).
							WithBdevFileSize(tc.bSize).
							WithBdevDeviceList(tc.bDevs[idx]...),
					)
				config.Engines = append(config.Engines, engine)
			}

			getFsRetStr := "none"
			if tc.scmMounted {
				getFsRetStr = "ext4"
			}
			msc := &scm.MockSysConfig{
				IsMountedBool:  tc.scmMounted,
				MountErr:       tc.mountRet,
				UnmountErr:     tc.unmountRet,
				GetfsStr:       getFsRetStr,
				SourceToTarget: devToMount,
			}
			cs := mockControlServiceNoSB(t, log, config, tc.bmbc, nil, msc)

			instances := cs.harness.Instances()
			common.AssertEqual(t, len(tc.sMounts), len(instances), name)

			// TODO DAOS-8040: re-enable VMD
			// t.Logf("VMD disabled: %v", cs.bdev.IsVMDDisabled())

			// runs discovery for nvme & scm
			if err := cs.Setup(); err != nil {
				t.Fatal(err.Error() + name)
			}

			for i, e := range instances {
				srv := e.(*EngineInstance)
				root := filepath.Dir(tc.sMounts[i])
				if tc.scmMounted {
					root = tc.sMounts[i]
				}
				if err := os.MkdirAll(root, 0777); err != nil {
					t.Fatal(err)
				}

				// if the instance is expected to have a valid superblock, create one
				if tc.superblockExists {
					if err := srv.createSuperblock(false); err != nil {
						t.Fatal(err)
					}
				}

				trc := &engine.TestRunnerConfig{}
				if tc.instancesStarted {
					trc.Running.SetTrue()
					srv.ready.SetTrue()
				}
				srv.runner = engine.NewTestRunner(trc, config.Engines[i])
			}

			ctx, cancel := context.WithCancel(context.Background())
			if tc.awaitTimeout != 0 {
				ctx, cancel = context.WithTimeout(ctx, tc.awaitTimeout)
			}
			defer cancel()

			awaitCh := make(chan error)
			inflight := 0
			for _, srv := range instances {
				inflight++
				go func(s *EngineInstance) {
					awaitCh <- s.awaitStorageReady(ctx, tc.recreateSBs)
				}(srv.(*EngineInstance))
			}

			awaitingFormat := make(chan struct{})
			t.Log("waiting for awaiting format state")
			go func(ctxIn context.Context) {
				for {
					ready := true
					for _, srv := range instances {
						if !srv.(*EngineInstance).isAwaitingFormat() {
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
				t.Log("storage is waiting")
			case err := <-awaitCh:
				inflight--
				common.CmpErr(t, tc.expAwaitErr, err)
				if !tc.expAwaitExit {
					t.Fatal("unexpected exit from awaitStorageReady()")
				}
			case <-ctx.Done():
				common.CmpErr(t, tc.expAwaitErr, ctx.Err())
				if tc.expAwaitErr == nil {
					t.Fatal(ctx.Err())
				}
				if !tc.scmMounted || inflight > 0 {
					t.Fatalf("unexpected behavior of awaitStorageReady")
				}
			}

			resp, fmtErr := cs.StorageFormat(context.TODO(), &ctlpb.StorageFormatReq{Reformat: tc.reformat})
			if fmtErr != nil {
				t.Fatal(fmtErr)
			}

			common.AssertEqual(t, len(tc.expResp.Crets), len(resp.Crets),
				"number of controller results")
			common.AssertEqual(t, len(tc.expResp.Mrets), len(resp.Mrets),
				"number of mount results")
			for _, exp := range tc.expResp.Crets {
				match := false
				for _, got := range resp.Crets {
					if diff := cmp.Diff(exp, got, common.DefaultCmpOpts()...); diff == "" {
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
					if diff := cmp.Diff(exp, got, common.DefaultCmpOpts()...); diff == "" {
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
