//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"math"
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
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	"github.com/daos-stack/daos/src/control/common/proto/ctl"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	clusterSize uint64 = humanize.GiByte
	mdcapSize   uint64 = 128 * humanize.MiByte
)

var (
	defStorageScanCmpOpts = append(common.DefaultCmpOpts(),
		protocmp.IgnoreFields(&ctlpb.NvmeController{}, "serial"))
)

func adjustNvmeSize(smdDevices []*ctl.NvmeController_SmdDevice) {
	const targetNb uint64 = 4

	availBytes := uint64(math.MaxUint64)
	for _, dev := range smdDevices {
		unalignedMemory := dev.AvailBytes % (targetNb * clusterSize)
		usabledMemory := dev.AvailBytes - unalignedMemory
		if usabledMemory < availBytes {
			availBytes = usabledMemory
		}
	}

	for _, dev := range smdDevices {
		dev.AvailBytes = availBytes
	}
}

func adjustScmSize(availBytes uint64) uint64 {
	const mdCapSize uint64 = uint64(128) * humanize.MiByte
	const mdBytes uint64 = mdCapSize + mdDaosScmBytes

	if availBytes < mdBytes {
		return 0
	}

	return availBytes - mdBytes
}

func TestServer_CtlSvc_StorageScan_PreEngineStart(t *testing.T) {
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
		multiEngine bool
		req         *ctlpb.StorageScanReq
		bmbc        *bdev.MockBackendConfig
		smbc        *scm.MockBackendConfig
		expResp     *ctlpb.StorageScanResp
	}{
		"successful scan; scm namespaces": {
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{
						ctrlr,
						storage.MockNvmeController(2),
					},
				},
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:         storage.ScmModules{storage.MockScmModule()},
				GetPmemNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
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
			},
		},
		"successful scan; no scm namespaces": {
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{ctrlr},
				},
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{storage.MockScmModule()},
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
			},
		},
		"spdk scan failure": {
			bmbc: &bdev.MockBackendConfig{
				ScanErr: errors.New("spdk scan failed"),
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:         storage.ScmModules{storage.MockScmModule()},
				GetPmemNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
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
			},
		},
		"scm module discovery failure": {
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{ctrlr},
				},
			},
			smbc: &scm.MockBackendConfig{
				DiscoverErr: errors.New("scm discover failed"),
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
			},
		},
		"all discover fail": {
			bmbc: &bdev.MockBackendConfig{
				ScanErr: errors.New("spdk scan failed"),
			},
			smbc: &scm.MockBackendConfig{
				DiscoverErr: errors.New("scm discover failed"),
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
			},
		},
		"scan bdev health; single engine down": {
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
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPBwHealth},
					State:  new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					State: new(ctlpb.ResponseState),
				},
			},
		},
		"scan bdev health; multiple engines down": {
			multiEngine: true,
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
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					// response should not contain duplicates
					Ctrlrs: proto.NvmeControllers{ctrlrPBwHealth},
					State:  new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					State: new(ctlpb.ResponseState),
				},
			},
		},
		"scan bdev meta; engines down": {
			req: &ctlpb.StorageScanReq{
				Scm: &ctlpb.ScanScmReq{},
				Nvme: &ctlpb.ScanNvmeReq{
					Meta: true,
				},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{ctrlr},
				},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPB},
					State:  new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					State: new(ctlpb.ResponseState),
				},
			},
		},
		"scan bdev; nvme basic set": {
			req: &ctlpb.StorageScanReq{
				Scm: &ctlpb.ScanScmReq{},
				Nvme: &ctlpb.ScanNvmeReq{
					Basic: true,
				},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{ctrlr},
				},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPBBasic},
					State:  new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					State: new(ctlpb.ResponseState),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			tCfg := storage.NewTierConfig().
				WithStorageClass(storage.ClassNvme.String()).
				WithBdevDeviceList(common.MockPCIAddr(0))

			engineCfg := engine.MockConfig().WithStorage(tCfg)
			engineCfgs := []*engine.Config{engineCfg}
			if tc.multiEngine {
				engineCfgs = append(engineCfgs, engineCfg)
			}
			sCfg := config.DefaultServer().WithEngines(engineCfgs...)

			cs := mockControlService(t, log, sCfg, tc.bmbc, tc.smbc, nil)
			for _, ei := range cs.harness.instances {
				// tests are for pre-engine-start scenario
				ei.(*EngineInstance).ready.SetFalse()
			}

			if tc.req == nil {
				tc.req = &ctlpb.StorageScanReq{
					Scm:  new(ctlpb.ScanScmReq),
					Nvme: new(ctlpb.ScanNvmeReq),
				}
			}

			resp, err := cs.StorageScan(context.TODO(), tc.req)
			if err != nil {
				t.Fatal(err)
			}

			if tc.req.Nvme.Health || tc.req.Nvme.Meta {
				if len(cs.harness.instances) == 0 {
					tc.expResp.Nvme.Ctrlrs = nil
				}
			}

			if diff := cmp.Diff(tc.expResp, resp, defStorageScanCmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestServer_CtlSvc_StorageScan_PostEngineStart(t *testing.T) {
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
		bioHealthResp.ClusterSize = clusterSize

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
			sd.DevState = storage.MockNvmeStateNormal.String()
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
			ctrlr.SmdDevices[i].ClusterSize = clusterSize
			ctrlr.Namespaces[i] = proto.MockNvmeNamespace(int32(i + 1))
		}

		return ctrlr, &ctlpb.SmdDevResp{Devices: smdDevRespDevices}
	}
	newCtrlrPB := func(idx int32) *ctlpb.NvmeController {
		c, _ := newCtrlrMeta(idx)
		c.SmdDevices = nil
		return c
	}
	newCtrlrPBwBasic := func(idx int32) *ctlpb.NvmeController {
		c := newCtrlrPB(idx)
		c.FwRev = ""
		c.Model = ""
		return c
	}
	newCtrlrPBwMeta := func(idx int32, smdIndexes ...int32) *ctlpb.NvmeController {
		c, _ := newCtrlrMeta(idx, smdIndexes...)
		adjustNvmeSize(c.GetSmdDevices())
		return c
	}
	newSmdDevResp := func(idx int32, smdIndexes ...int32) *ctlpb.SmdDevResp {
		_, s := newCtrlrMeta(idx, smdIndexes...)
		return s
	}

	smdDevRespStateNew := newSmdDevResp(1)
	smdDevRespStateNew.Devices[0].DevState = storage.MockNvmeStateNew.String()

	ctrlrPBwMetaNew := newCtrlrPBwMeta(1)
	ctrlrPBwMetaNew.SmdDevices[0].AvailBytes = 0
	ctrlrPBwMetaNew.SmdDevices[0].TotalBytes = 0
	ctrlrPBwMetaNew.SmdDevices[0].DevState = storage.MockNvmeStateNew.String()
	ctrlrPBwMetaNew.SmdDevices[0].ClusterSize = 0

	ctrlrPBwMetaNormal := newCtrlrPBwMeta(1)
	ctrlrPBwMetaNormal.SmdDevices[0].AvailBytes = 0
	ctrlrPBwMetaNormal.SmdDevices[0].TotalBytes = 0
	ctrlrPBwMetaNormal.SmdDevices[0].DevState = storage.MockNvmeStateNormal.String()
	ctrlrPBwMetaNormal.SmdDevices[0].ClusterSize = 0

	mockPbScmMount0 := proto.MockScmMountPoint(0)
	mockPbScmNamespace0 := proto.MockScmNamespace(0)
	mockPbScmNamespace0.Mount = mockPbScmMount0
	mockPbScmMount1 := proto.MockScmMountPoint(1)
	mockPbScmNamespace1 := proto.MockScmNamespace(1)
	mockPbScmNamespace1.Mount = mockPbScmMount1

	for name, tc := range map[string]struct {
		req         *ctlpb.StorageScanReq
		csCtrlrs    *storage.NvmeControllers   // control service storage provider
		eCtrlrs     []*storage.NvmeControllers // engine storage provider
		smbc        *scm.MockBackendConfig
		smsc        *scm.MockSysConfig
		storageCfgs []storage.TierConfigs
		scanTwice   bool
		junkResp    bool
		drpcResps   map[int][]*mockDrpcResponse
		expErr      error
		expResp     *ctlpb.StorageScanResp
	}{
		"engine up; scan bdev basic": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Basic: true},
			},
			csCtrlrs: &storage.NvmeControllers{newCtrlr(1)},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(1).PciAddr),
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{newCtrlrPBwBasic(1)},
					State:  new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
			},
		},
		"engine up; scan bdev health": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Health: true},
			},
			csCtrlrs: &storage.NvmeControllers{newCtrlr(1)},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(1).PciAddr),
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: newSmdDevResp(1)},
					{Message: newBioHealthResp(1)},
				},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{newCtrlrPBwHealth(1)},
					State:  new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
			},
		},
		"engine up; scan bdev meta": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
			},
			csCtrlrs: &storage.NvmeControllers{newCtrlr(1)},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(1).PciAddr),
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: newSmdDevResp(1)},
					{Message: newBioHealthResp(1)},
				},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{newCtrlrPBwMeta(1)},
					State:  new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
			},
		},
		"engines up; scan bdev health": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Health: true},
			},
			csCtrlrs: &storage.NvmeControllers{newCtrlr(1), newCtrlr(2)},
			eCtrlrs:  []*storage.NvmeControllers{{newCtrlr(1)}, {newCtrlr(2)}},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(1).PciAddr),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(2).PciAddr),
				},
			},
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
			expResp: &ctlpb.StorageScanResp{
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
		"engines up; scan bdev meta": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
			},
			csCtrlrs: &storage.NvmeControllers{newCtrlr(1), newCtrlr(2)},
			eCtrlrs:  []*storage.NvmeControllers{{newCtrlr(1)}, {newCtrlr(2)}},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(1).PciAddr),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(2).PciAddr),
				},
			},
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
			expResp: &ctlpb.StorageScanResp{
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
		// make sure stale information is cleared and not used from cache
		"verify cache invalidation over multiple storage scan calls": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
			},
			csCtrlrs: &storage.NvmeControllers{newCtrlr(1), newCtrlr(2)},
			eCtrlrs:  []*storage.NvmeControllers{{newCtrlr(1)}, {newCtrlr(2)}},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(1).PciAddr),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(2).PciAddr),
				},
			},
			scanTwice: true,
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: newSmdDevResp(1, 1, 2, 3)},
					{Message: newBioHealthResp(1, 1)},
					{Message: newBioHealthResp(1, 2)},
					{Message: newBioHealthResp(1, 3)},
					{Message: newSmdDevResp(1)},
					{Message: newBioHealthResp(1)},
				},
				1: {
					{Message: newSmdDevResp(2, 1, 2, 3)},
					{Message: newBioHealthResp(1, 1)},
					{Message: newBioHealthResp(1, 2)},
					{Message: newBioHealthResp(1, 3)},
					{Message: newSmdDevResp(2)},
					{Message: newBioHealthResp(2)},
				},
			},
			expResp: &ctlpb.StorageScanResp{
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
		"engines up; scan bdev meta; multiple nvme namespaces": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
			},
			csCtrlrs: &storage.NvmeControllers{
				newCtrlrMultiNs(1, 2), newCtrlrMultiNs(2, 2),
			},
			eCtrlrs: []*storage.NvmeControllers{
				{newCtrlrMultiNs(1, 2)}, {newCtrlrMultiNs(2, 2)},
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(1).PciAddr),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(2).PciAddr),
				},
			},
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
			expResp: &ctlpb.StorageScanResp{
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
				DiscoverRes:         storage.ScmModules{storage.MockScmModule(0)},
				GetPmemNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace(0)},
			},
			smsc: &scm.MockSysConfig{
				GetfsUsageResps: []scm.GetfsUsageRetval{
					{
						Total: mockPbScmMount0.TotalBytes,
						Avail: mockPbScmMount0.AvailBytes,
					},
				},
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockPbScmMount0.Path).
						WithScmDeviceList(mockPbScmNamespace0.Blockdev),
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					State: new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					Namespaces: proto.ScmNamespaces{
						&ctlpb.ScmNamespace{
							Blockdev: mockPbScmNamespace0.Blockdev,
							Dev:      mockPbScmNamespace0.Dev,
							Size:     mockPbScmNamespace0.Size,
							Uuid:     mockPbScmNamespace0.Uuid,
							Mount: &ctlpb.ScmNamespace_Mount{
								Class:      mockPbScmMount0.Class,
								DeviceList: mockPbScmMount0.DeviceList,
								Path:       mockPbScmMount0.Path,
								TotalBytes: mockPbScmMount0.TotalBytes,
								AvailBytes: adjustScmSize(mockPbScmMount0.AvailBytes),
							},
						},
					},
					State: new(ctlpb.ResponseState),
				},
			},
		},
		"scan scm usage; pmem not in instance device list": {
			req: &ctlpb.StorageScanReq{
				Scm:  &ctlpb.ScanScmReq{Usage: true},
				Nvme: new(ctlpb.ScanNvmeReq),
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:         storage.ScmModules{storage.MockScmModule(0)},
				GetPmemNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace(0)},
			},
			smsc: &scm.MockSysConfig{
				GetfsUsageResps: []scm.GetfsUsageRetval{
					{
						Total: mockPbScmMount0.TotalBytes,
						Avail: mockPbScmMount0.AvailBytes,
					},
				},
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockPbScmMount0.Path).
						WithScmDeviceList("/dev/foo", "/dev/bar"),
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					State: new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					State: &ctlpb.ResponseState{
						Status: ctlpb.ResponseStatus_CTL_ERR_SCM,
						Error:  "instance 0: no pmem namespace for mount /mnt/daos0",
					},
				},
			},
		},
		"scan scm usage; class ram": {
			req: &ctlpb.StorageScanReq{
				Scm:  &ctlpb.ScanScmReq{Usage: true},
				Nvme: new(ctlpb.ScanNvmeReq),
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:         storage.ScmModules{storage.MockScmModule(0)},
				GetPmemNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace(0)},
			},
			smsc: &scm.MockSysConfig{
				GetfsUsageResps: []scm.GetfsUsageRetval{
					{
						Total: mockPbScmMount0.TotalBytes,
						Avail: mockPbScmMount0.AvailBytes,
					},
				},
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassRam.String()).
						WithScmMountPoint(mockPbScmMount0.Path).
						WithScmRamdiskSize(16),
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					State: new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					Namespaces: proto.ScmNamespaces{
						&ctlpb.ScmNamespace{
							Blockdev: "ramdisk",
							Size:     uint64(humanize.GiByte * 16),
							Mount: &ctlpb.ScmNamespace_Mount{
								Class:      "ram",
								Path:       mockPbScmMount0.Path,
								TotalBytes: mockPbScmMount0.TotalBytes,
								AvailBytes: adjustScmSize(mockPbScmMount0.AvailBytes),
							},
						},
					},
					State: new(ctlpb.ResponseState),
				},
			},
		},
		"multi-engine; multi-tier; with usage": {
			req: &ctlpb.StorageScanReq{
				Scm:  &ctlpb.ScanScmReq{Usage: true},
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
			},
			csCtrlrs: &storage.NvmeControllers{newCtrlr(1), newCtrlr(2)},
			eCtrlrs:  []*storage.NvmeControllers{{newCtrlr(1)}, {newCtrlr(2)}},
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{
					storage.MockScmModule(0),
				},
				GetPmemNamespaceRes: storage.ScmNamespaces{
					storage.MockScmNamespace(0),
					storage.MockScmNamespace(1),
				},
			},
			smsc: &scm.MockSysConfig{
				GetfsUsageResps: []scm.GetfsUsageRetval{
					{
						Total: mockPbScmMount0.TotalBytes,
						Avail: mockPbScmMount0.AvailBytes,
					},
					{
						Total: mockPbScmMount1.TotalBytes,
						Avail: mockPbScmMount1.AvailBytes,
					},
				},
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockPbScmMount0.Path).
						WithScmDeviceList(mockPbScmNamespace0.Blockdev),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(1).PciAddr),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockPbScmMount1.Path).
						WithScmDeviceList(mockPbScmNamespace1.Blockdev),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(2).PciAddr),
				},
			},
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
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{
						newCtrlrPBwMeta(1),
						newCtrlrPBwMeta(2),
					},
					State: new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{
					Namespaces: proto.ScmNamespaces{
						&ctlpb.ScmNamespace{
							Blockdev: mockPbScmNamespace0.Blockdev,
							Dev:      mockPbScmNamespace0.Dev,
							Size:     mockPbScmNamespace0.Size,
							Uuid:     mockPbScmNamespace0.Uuid,
							Mount: &ctlpb.ScmNamespace_Mount{
								Class:      mockPbScmMount0.Class,
								DeviceList: mockPbScmMount0.DeviceList,
								Path:       mockPbScmMount0.Path,
								TotalBytes: mockPbScmMount0.TotalBytes,
								AvailBytes: adjustScmSize(mockPbScmMount0.AvailBytes),
							},
						},
						&ctlpb.ScmNamespace{
							Blockdev: mockPbScmNamespace1.Blockdev,
							Dev:      mockPbScmNamespace1.Dev,
							Size:     mockPbScmNamespace1.Size,
							Uuid:     mockPbScmNamespace1.Uuid,
							NumaNode: mockPbScmNamespace1.NumaNode,
							Mount: &ctlpb.ScmNamespace_Mount{
								Class:      mockPbScmMount1.Class,
								DeviceList: mockPbScmMount1.DeviceList,
								Path:       mockPbScmMount1.Path,
								TotalBytes: mockPbScmMount1.TotalBytes,
								AvailBytes: adjustScmSize(mockPbScmMount1.AvailBytes),
							},
						},
					},
					State: new(ctlpb.ResponseState),
				},
			},
		},
		// Sometimes when more than a few ssds are assigned to engine without many targets,
		// some of the smd entries for the latter ssds are in state "NEW" rather than
		// "NORMAL", when in this state, health is unavailable and DER_NONEXIST is returned.
		"bdev scan; meta; new state; non-existent smd health": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
			},
			csCtrlrs: &storage.NvmeControllers{newCtrlr(1)},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(1).PciAddr),
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: smdDevRespStateNew},
					{
						Message: &ctlpb.BioHealthResp{
							Status: int32(drpc.DaosNonexistant),
						},
					},
				},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPBwMetaNew},
					State:  new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
			},
		},
		"bdev scan; meta; new state; nomem smd health": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
			},
			csCtrlrs: &storage.NvmeControllers{newCtrlr(1)},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(1).PciAddr),
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: smdDevRespStateNew},
					{
						Message: &ctlpb.BioHealthResp{
							Status: int32(drpc.DaosFreeMemError),
						},
					},
				},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPBwMetaNew},
					State:  new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
			},
		},
		"bdev scan; meta; normal state; non-existent smd health": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
			},
			csCtrlrs: &storage.NvmeControllers{newCtrlr(1)},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(1).PciAddr),
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: newSmdDevResp(1)},
					{
						Message: &ctlpb.BioHealthResp{
							Status: int32(drpc.DaosNonexistant),
						},
					},
				},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPBwMetaNormal},
					State:  new(ctlpb.ResponseState),
				},
				Scm: &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if len(tc.storageCfgs) != len(tc.drpcResps) {
				t.Fatalf("number of tc.storageCfgs doesn't match num drpc msg groups")
			}

			if len(tc.storageCfgs) == 1 && tc.eCtrlrs == nil && tc.csCtrlrs != nil {
				log.Debugf("using control service storage provider for first engine")
				tc.eCtrlrs = []*storage.NvmeControllers{tc.csCtrlrs}
			}

			var csbmbc *bdev.MockBackendConfig
			if tc.csCtrlrs != nil {
				csbmbc = &bdev.MockBackendConfig{
					ScanRes: &storage.BdevScanResponse{Controllers: *tc.csCtrlrs},
				}
			}

			var engineCfgs []*engine.Config
			for _, sc := range tc.storageCfgs {
				engineCfgs = append(engineCfgs, engine.MockConfig().WithStorage(sc...))
			}
			sCfg := config.DefaultServer().WithEngines(engineCfgs...)
			cs := mockControlService(t, log, sCfg, csbmbc, tc.smbc, tc.smsc)

			// In production, during server/server.go:srv.addEngines() and after
			// srv.createEngine(), engine.storage.SetBdevCache() is called to load the
			// results of the start-up bdev scan from the control service storage
			// provider into the engine's storage provider. The control service and
			// each of the engines have distinct storage provider instances so cached
			// cached results have to be explicitly shared so results are available when
			// engines are up.

			for idx, ec := range engineCfgs {
				var ebmbc *bdev.MockBackendConfig
				if tc.eCtrlrs != nil && len(tc.eCtrlrs) > idx {
					log.Debugf("bdevs %v for engine %d", *tc.eCtrlrs[idx], idx)
					ebmbc = &bdev.MockBackendConfig{
						ScanRes: &storage.BdevScanResponse{
							Controllers: *tc.eCtrlrs[idx],
						},
					}
				}

				// replace harness instance with mock I/O Engine
				// to enable mocking of harness instance drpc channel
				sp := storage.MockProvider(log, idx, &ec.Storage,
					cs.storage.Sys, // share system provider cfo
					scm.NewMockProvider(log, tc.smbc, nil),
					bdev.NewMockProvider(log, ebmbc))
				ne := newTestEngine(log, false, sp, ec)

				// mock drpc responses
				dcc := new(mockDrpcClientConfig)
				if tc.junkResp {
					dcc.setSendMsgResponse(drpc.Status_SUCCESS,
						makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > idx {
					t.Logf("setting %d drpc responses for engine %d",
						len(tc.drpcResps[idx]), idx)
					dcc.setSendMsgResponseList(t, tc.drpcResps[idx]...)
				} else {
					t.Fatal("drpc response mocks unpopulated")
				}
				ne.setDrpcClient(newMockDrpcClient(dcc))
				ne._superblock.Rank = system.NewRankPtr(uint32(idx + 1))

				cs.harness.instances[idx] = ne
			}
			cs.harness.started.SetTrue()

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

			if diff := cmp.Diff(tc.expResp, resp, defStorageScanCmpOpts...); diff != "" {
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
		"io instance already running": { // await should exit immediately
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

			// Mimic control service start-up and engine creation where cache is shared
			// to the engines from the base control service storage provider.
			nvmeScanResp, err := cs.NvmeScan(storage.BdevScanRequest{})
			if err != nil {
				t.Fatal(err)
			}

			for i, e := range instances {
				ei := e.(*EngineInstance)
				ei.storage.SetBdevCache(*nvmeScanResp)

				root := filepath.Dir(tc.sMounts[i])
				if tc.scmMounted {
					root = tc.sMounts[i]
				}
				if err := os.MkdirAll(root, 0777); err != nil {
					t.Fatal(err)
				}

				// if the instance is expected to have a valid superblock, create one
				if tc.superblockExists {
					if err := ei.createSuperblock(false); err != nil {
						t.Fatal(err)
					}
				}

				trc := &engine.TestRunnerConfig{}
				if tc.instancesStarted {
					trc.Running.SetTrue()
					ei.ready.SetTrue()
				}
				ei.runner = engine.NewTestRunner(trc, config.Engines[i])
			}

			ctx, cancel := context.WithCancel(context.Background())
			if tc.awaitTimeout != 0 {
				ctx, cancel = context.WithTimeout(ctx, tc.awaitTimeout)
			}
			defer cancel()

			// Trigger await storage ready on each instance and send results to
			// awaitCh. awaitStorageReady() will set "waitFormat" flag, fire off
			// "onAwaitFormat" callbacks, select on "storageReady" channel then
			// finally unset "waitFormat" flag.
			awaitCh := make(chan error)
			for _, ei := range instances {
				t.Logf("call awaitStorageReady() (%d)", ei.Index())
				go func(e *EngineInstance) {
					awaitCh <- e.awaitStorageReady(ctx, tc.recreateSBs)
				}(ei.(*EngineInstance))
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
				common.CmpErr(t, tc.expAwaitErr, err)
				if !tc.expAwaitExit {
					t.Fatal("unexpected exit from awaitStorageReady()")
				}
			case <-ctx.Done():
				t.Logf("context done (%s)", ctx.Err())
				common.CmpErr(t, tc.expAwaitErr, ctx.Err())
				if tc.expAwaitErr == nil {
					t.Fatal(ctx.Err())
				}
				if !tc.scmMounted {
					t.Fatalf("unexpected behavior of awaitStorageReady")
				}
			}

			resp, fmtErr := cs.StorageFormat(context.TODO(), &ctlpb.StorageFormatReq{
				Reformat: tc.reformat,
			})
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
				PciAddr: common.MockPCIAddr(1),
			},
			bmbc: &bdev.MockBackendConfig{
				PrepareErr: errors.New("failure"),
			},
			expPrepCall: &storage.BdevPrepareRequest{
				TargetUser:   username,
				PCIAllowList: common.MockPCIAddr(1),
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
				PciAddr: common.MockPCIAddr(1),
			},
			bmbc: &bdev.MockBackendConfig{},
			expPrepCall: &storage.BdevPrepareRequest{
				TargetUser:   username,
				PCIAllowList: common.MockPCIAddr(1),
			},
			expResp: &ctlpb.NvmeRebindResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mbb := bdev.NewMockBackend(tc.bmbc)
			mbp := bdev.NewProvider(log, mbb)
			scs := NewMockStorageControlService(log, nil, nil,
				scm.NewMockProvider(log, nil, nil), mbp)
			cs := &ControlService{StorageControlService: *scs}

			resp, err := cs.StorageNvmeRebind(context.TODO(), tc.req)

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

			common.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, resp, common.DefaultCmpOpts()...); diff != "" {
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
				PciAddr:          common.MockPCIAddr(1),
				StorageTierIndex: -1,
			},
			expErr: errors.New("engine with index 0"),
		},
		"missing engine index 1": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          common.MockPCIAddr(1),
				EngineIndex:      1,
				StorageTierIndex: -1,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(common.MockPCIAddr(0)),
				},
			},
			expErr: errors.New("engine with index 1"),
		},
		"zero bdev configs": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          common.MockPCIAddr(1),
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
				PciAddr:          common.MockPCIAddr(1),
				StorageTierIndex: 0,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(common.MockPCIAddr(0)),
				},
			},
			expErr: errors.New("storage tier with index 0"),
		},
		"missing bdev config index 2": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          common.MockPCIAddr(1),
				StorageTierIndex: 2,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(common.MockPCIAddr(0)),
				},
			},
			expErr: errors.New("storage tier with index 2"),
		},
		"success; bdev config index unspecified": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          common.MockPCIAddr(1),
				StorageTierIndex: -1,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(common.MockPCIAddr(0)),
				},
			},
			expResp: &ctlpb.NvmeAddDeviceResp{},
			expDevList: []string{
				common.MockPCIAddr(0), common.MockPCIAddr(1),
			},
		},
		"success; bdev config index specified": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          common.MockPCIAddr(1),
				StorageTierIndex: 1,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(common.MockPCIAddr(0)),
				},
			},
			expResp: &ctlpb.NvmeAddDeviceResp{},
			expDevList: []string{
				common.MockPCIAddr(0), common.MockPCIAddr(1),
			},
		},
		"failure; write config failed": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          common.MockPCIAddr(1),
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
						WithBdevDeviceList(common.MockPCIAddr(0)),
				},
			},
			expResp: &ctlpb.NvmeAddDeviceResp{
				State: &ctlpb.ResponseState{
					Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
					Error:  "write nvme config for engine 0: failure",
				},
			},
			expDevList: []string{
				common.MockPCIAddr(0), common.MockPCIAddr(1),
			},
		},
		"zero bdev configs; engine index 1": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          common.MockPCIAddr(1),
				EngineIndex:      1,
				StorageTierIndex: -1,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(common.MockPCIAddr(0)),
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
				PciAddr:          common.MockPCIAddr(1),
				EngineIndex:      1,
				StorageTierIndex: 0,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(common.MockPCIAddr(0)),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(common.MockPCIAddr(0)),
				},
			},
			expErr: errors.New("storage tier with index 0"),
		},
		"missing bdev config index 2; engine index 1": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          common.MockPCIAddr(1),
				EngineIndex:      1,
				StorageTierIndex: 2,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(common.MockPCIAddr(0)),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(common.MockPCIAddr(0)),
				},
			},
			expErr: errors.New("storage tier with index 2"),
		},
		"success; bdev config index specified; engine index 1": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          common.MockPCIAddr(1),
				EngineIndex:      1,
				StorageTierIndex: 1,
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(common.MockPCIAddr(0)),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(common.MockPCIAddr(0)),
				},
			},
			expResp: &ctlpb.NvmeAddDeviceResp{},
			expDevList: []string{
				common.MockPCIAddr(0), common.MockPCIAddr(1),
			},
		},
		"failure; write config failed; engine index 1": {
			req: &ctlpb.NvmeAddDeviceReq{
				PciAddr:          common.MockPCIAddr(1),
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
						WithBdevDeviceList(common.MockPCIAddr(0)),
				},
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(common.MockPCIAddr(0)),
				},
			},
			expResp: &ctlpb.NvmeAddDeviceResp{
				State: &ctlpb.ResponseState{
					Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
					Error:  "write nvme config for engine 1: failure",
				},
			},
			expDevList: []string{
				common.MockPCIAddr(0), common.MockPCIAddr(1),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			engineCfgs := []*engine.Config{}
			for idx, tierCfgs := range tc.storageCfgs {
				ec := engine.MockConfig().WithStorage(tierCfgs...)
				ec.Index = uint32(idx)
				engineCfgs = append(engineCfgs, ec)
			}
			serverCfg := config.DefaultServer().WithEngines(engineCfgs...)

			cs := mockControlService(t, log, serverCfg, tc.bmbc, nil, nil)

			resp, err := cs.StorageNvmeAddDevice(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, resp, common.DefaultCmpOpts()...); diff != "" {
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
			if diff := cmp.Diff(tc.expDevList, gotDevs, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestServer_adjustNvmeSize(t *testing.T) {
	type ExpectedOutput struct {
		availableBytes []uint64
		message        string
	}

	for name, tc := range map[string]struct {
		input  *ctlpb.ScanNvmeResp
		output ExpectedOutput
	}{
		"success": {
			input: &ctlpb.ScanNvmeResp{
				Ctrlrs: []*ctlpb.NvmeController{
					{
						SmdDevices: []*ctlpb.NvmeController_SmdDevice{
							{
								Uuid:        "nvme0",
								TgtIds:      []int32{0, 1, 2, 3},
								AvailBytes:  10 * humanize.GiByte,
								ClusterSize: clusterSize,
								DevState:    "NORMAL",
								Rank:        0,
							},
							{
								Uuid:        "nvme1",
								TgtIds:      []int32{0, 1, 2, 3},
								AvailBytes:  10 * humanize.GiByte,
								ClusterSize: clusterSize,
								DevState:    "NORMAL",
								Rank:        0,
							},
							{
								TgtIds:      []int32{0, 1, 2, 3},
								AvailBytes:  20 * humanize.GiByte,
								ClusterSize: clusterSize,
								DevState:    "NORMAL",
								Rank:        0,
							},
							{
								TgtIds:      []int32{0, 1, 2},
								AvailBytes:  20 * humanize.GiByte,
								ClusterSize: clusterSize,
								DevState:    "NORMAL",
								Rank:        1,
							},
							{
								TgtIds:      []int32{0, 1, 2},
								AvailBytes:  20 * humanize.GiByte,
								ClusterSize: clusterSize,
								DevState:    "NORMAL",
								Rank:        1,
							},
						},
					},
				},
			},
			output: ExpectedOutput{
				availableBytes: []uint64{
					8 * humanize.GiByte,
					8 * humanize.GiByte,
					8 * humanize.GiByte,
					18 * humanize.GiByte,
					18 * humanize.GiByte,
				},
			},
		},
		"new": {
			input: &ctlpb.ScanNvmeResp{
				Ctrlrs: []*ctlpb.NvmeController{
					{
						SmdDevices: []*ctlpb.NvmeController_SmdDevice{
							{
								Uuid:        "nvme0",
								TgtIds:      []int32{0, 1, 2, 3},
								AvailBytes:  10 * humanize.GiByte,
								ClusterSize: clusterSize,
								DevState:    "NORMAL",
							},
							{
								Uuid:        "nvme1",
								TgtIds:      []int32{0, 1, 2},
								AvailBytes:  10 * humanize.GiByte,
								ClusterSize: clusterSize,
								DevState:    "NEW",
							},
						},
					},
				},
			},
			output: ExpectedOutput{
				availableBytes: []uint64{
					8 * humanize.GiByte,
					0,
				},
				message: "Adjusting available size of unusable SMD device",
			},
		},
		"evicted": {
			input: &ctlpb.ScanNvmeResp{
				Ctrlrs: []*ctlpb.NvmeController{
					{
						SmdDevices: []*ctlpb.NvmeController_SmdDevice{
							{
								TgtIds:      []int32{0, 1, 2, 3},
								AvailBytes:  10 * humanize.GiByte,
								ClusterSize: clusterSize,
								DevState:    "NORMAL",
							},
							{
								TgtIds:      []int32{0, 1, 2},
								AvailBytes:  10 * humanize.GiByte,
								ClusterSize: clusterSize,
								DevState:    "EVICTED",
							},
						},
					},
				},
			},
			output: ExpectedOutput{
				availableBytes: []uint64{
					8 * humanize.GiByte,
					0,
				},
				message: "Adjusting available size of unusable SMD device",
			},
		},
		"missing targets": {
			input: &ctlpb.ScanNvmeResp{
				Ctrlrs: []*ctlpb.NvmeController{
					{
						SmdDevices: []*ctlpb.NvmeController_SmdDevice{
							{
								TgtIds:      []int32{0, 1, 2, 3},
								AvailBytes:  10 * humanize.GiByte,
								ClusterSize: clusterSize,
								DevState:    "NORMAL",
							},
							{
								TgtIds:      []int32{},
								AvailBytes:  10 * humanize.GiByte,
								ClusterSize: clusterSize,
								DevState:    "NORMAL",
							},
						},
					},
				},
			},
			output: ExpectedOutput{
				availableBytes: []uint64{
					8 * humanize.GiByte,
					10 * humanize.GiByte,
				},
				message: "missing storage info",
			},
		},
		"missing cluster size": {
			input: &ctlpb.ScanNvmeResp{
				Ctrlrs: []*ctlpb.NvmeController{
					{
						SmdDevices: []*ctlpb.NvmeController_SmdDevice{
							{
								Uuid:        "nvme0",
								TgtIds:      []int32{0, 1, 2, 3},
								AvailBytes:  10 * humanize.GiByte,
								ClusterSize: clusterSize,
								DevState:    "NORMAL",
							},
							{
								Uuid:       "nvme1",
								TgtIds:     []int32{0, 1, 2},
								AvailBytes: 10 * humanize.GiByte,
								DevState:   "NORMAL",
							},
						},
					},
				},
			},
			output: ExpectedOutput{
				availableBytes: []uint64{
					8 * humanize.GiByte,
					10 * humanize.GiByte,
				},
				message: "missing storage info",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cs := mockControlService(t, log, nil, nil, nil, nil)
			cs.adjustNvmeSize(tc.input)

			ctlr := tc.input.GetCtrlrs()[0]
			for index, device := range ctlr.GetSmdDevices() {
				common.AssertEqual(t, device.GetAvailBytes(),
					tc.output.availableBytes[index], "Invalid available bytes")
			}
			if tc.output.message != "" {
				common.AssertTrue(t,
					strings.Contains(buf.String(), tc.output.message),
					"missing message: "+tc.output.message)
			}
		})
	}
}

func TestServer_getMetadataCapacity(t *testing.T) {
	type EngineConfig struct {
		mdCap       string
		mountPoints []string
	}

	type ExpectedOutput struct {
		size    uint64
		message string
		err     error
	}

	for name, tc := range map[string]struct {
		mountPoint string
		configs    []*EngineConfig
		output     ExpectedOutput
	}{
		"simple env var": {
			mountPoint: "/mnt/daos0",
			configs: []*EngineConfig{
				{
					mdCap:       "DAOS_MD_CAP=1024",
					mountPoints: []string{"/mnt/daos0"},
				},
			},
			output: ExpectedOutput{
				size: 1024 * humanize.MiByte,
			},
		},
		"simple default": {
			mountPoint: "/mnt/daos0",
			configs: []*EngineConfig{
				{
					mountPoints: []string{"/mnt/daos0"},
				},
			},
			output: ExpectedOutput{
				size:    mdcapSize,
				message: " using default metadata capacity with SCM",
			},
		},
		"complex env var": {
			mountPoint: "/mnt/daos2",
			configs: []*EngineConfig{
				{
					mdCap:       "DAOS_MD_CAP=1024",
					mountPoints: []string{"/mnt/daos0"},
				},
				{
					mdCap:       "DAOS_MD_CAP=512",
					mountPoints: []string{"/mnt/daos1", "/mnt/daos2", "/mnt/daos3"},
				},
				{
					mountPoints: []string{"/mnt/daos4"},
				},
			},
			output: ExpectedOutput{
				size: 512 * humanize.MiByte,
			},
		},
		"unknown device": {
			mountPoint: "/mnt/daos0",
			output: ExpectedOutput{
				err: errors.New("unknown SCM mount point"),
			},
		},
		"invalid mdcap": {
			mountPoint: "/mnt/daos0",
			configs: []*EngineConfig{
				{
					mdCap:       "DAOS_MD_CAP=foo",
					mountPoints: []string{"/mnt/daos0"},
				},
			},
			output: ExpectedOutput{
				err: errors.New("invalid metadata capacity"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			var enginesCfg []*engine.Config
			for _, cfg := range tc.configs {
				var storagesCfg []*storage.TierConfig
				for _, mountPoint := range cfg.mountPoints {
					storageCfg := storage.NewTierConfig()
					storageCfg.WithStorageClass(storage.ClassDcpm.String())
					storageCfg.WithScmMountPoint(mountPoint)

					storagesCfg = append(storagesCfg, storageCfg)
				}

				engineCfg := engine.MockConfig()
				engineCfg.WithStorage(storagesCfg...)
				engineCfg.WithEnvVars(cfg.mdCap)

				enginesCfg = append(enginesCfg, engineCfg)
			}
			serverCfg := config.DefaultServer().WithEngines(enginesCfg...)
			cs := mockControlService(t, log, serverCfg, nil, nil, nil)

			size, err := cs.getMetadataCapacity(tc.mountPoint)

			if err != nil {
				common.AssertTrue(t, tc.output.err != nil,
					fmt.Sprintf("Unexpected error %q", err))
				common.CmpErr(t, tc.output.err, err)
				return
			}

			common.AssertTrue(t, err == nil, "Expected error")
			common.AssertEqual(t, tc.output.size, size, "invalid meta data capacity size")
			if tc.output.message != "" {
				common.AssertTrue(t,
					strings.Contains(buf.String(), tc.output.message),
					"missing message: "+tc.output.message)
			}
		})
	}
}

func TestServer_adjustScmSize(t *testing.T) {
	type EngineConfig struct {
		mdCap       string
		mountPoints []string
	}

	type DataInput struct {
		configs  []*EngineConfig
		response *ctlpb.ScanScmResp
	}

	type ExpectedOutput struct {
		availableBytes []uint64
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
				availableBytes: []uint64{uint64(64)*humanize.GiByte - mdcapSize - mdDaosScmBytes},
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
					uint64(64)*humanize.GiByte - 1024*humanize.MiByte - mdDaosScmBytes,
					uint64(32)*humanize.GiByte - 1024*humanize.MiByte - mdDaosScmBytes,
					uint64(128)*humanize.GiByte - 1024*humanize.MiByte - mdDaosScmBytes,
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
					uint64(64)*humanize.GiByte - 1024*humanize.MiByte - mdDaosScmBytes,
					uint64(32) * humanize.GiByte,
					uint64(128)*humanize.GiByte - 1024*humanize.MiByte - mdDaosScmBytes,
				},
				message: "Skipping SCM /mnt/daos1",
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
				availableBytes: []uint64{0},
				message:        "WARNING: Adjusting available size to 0 Bytes of SCM device",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			var enginesCfg []*engine.Config
			for _, cfg := range tc.input.configs {
				var storagesCfg []*storage.TierConfig
				for _, mountPoint := range cfg.mountPoints {
					storageCfg := storage.NewTierConfig()
					storageCfg.WithStorageClass(storage.ClassDcpm.String())
					storageCfg.WithScmMountPoint(mountPoint)

					storagesCfg = append(storagesCfg, storageCfg)
				}

				engineCfg := engine.MockConfig()
				engineCfg.WithStorage(storagesCfg...)
				engineCfg.WithEnvVars(cfg.mdCap)

				enginesCfg = append(enginesCfg, engineCfg)
			}
			serverCfg := config.DefaultServer().WithEngines(enginesCfg...)
			cs := mockControlService(t, log, serverCfg, nil, nil, nil)

			cs.adjustScmSize(tc.input.response)

			for index, namespace := range tc.input.response.Namespaces {
				common.AssertEqual(t, namespace.GetMount().GetAvailBytes(),
					tc.output.availableBytes[index], "Invalid available bytes")
			}
			if tc.output.message != "" {
				common.AssertTrue(t,
					strings.Contains(buf.String(), tc.output.message),
					"missing message: "+tc.output.message)
			}
		})
	}
}
