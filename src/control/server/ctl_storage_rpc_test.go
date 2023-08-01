//
// (C) Copyright 2019-2023 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
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
)

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
		tierCfgs    storage.TierConfigs
		expResp     *ctlpb.StorageScanResp
		expErr      error
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
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{ctrlr},
				},
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
		"successful scan; no bdevs in config": {
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{ctrlr},
				},
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes: storage.ScmModules{storage.MockScmModule()},
			},
			tierCfgs: storage.TierConfigs{},
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
		"successful scan; missing bdev in config": {
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{ctrlr},
				},
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes: storage.ScmModules{storage.MockScmModule()},
			},
			tierCfgs: storage.TierConfigs{
				storage.NewTierConfig().
					WithStorageClass(storage.ClassNvme.String()).
					WithBdevDeviceList(test.MockPCIAddr(2)),
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{},
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
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{
						ctrlr,
						storage.MockNvmeController(2),
					},
				},
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
			bmbc: &bdev.MockBackendConfig{
				ScanErr: errors.New("spdk scan failed"),
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
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{ctrlr},
				},
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
			bmbc: &bdev.MockBackendConfig{
				ScanErr: errors.New("spdk scan failed"),
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
				MemInfo: proto.MockPBMemInfo(),
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
				MemInfo: proto.MockPBMemInfo(),
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
				MemInfo: proto.MockPBMemInfo(),
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
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"scan bdev; vmd enabled": {
			req: &ctlpb.StorageScanReq{
				Scm:  &ctlpb.ScanScmReq{},
				Nvme: &ctlpb.ScanNvmeReq{},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{
						&storage.NvmeController{PciAddr: "050505:01:00.0"},
					},
				},
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
		"scan usage": {
			req: &ctlpb.StorageScanReq{
				Scm: &ctlpb.ScanScmReq{
					Usage: true,
				},
				Nvme: &ctlpb.ScanNvmeReq{
					Meta: true,
				},
			},
			expErr: FaultDataPlaneNotStarted,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.tierCfgs == nil {
				tc.tierCfgs = storage.TierConfigs{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(1)),
				}
			}

			engineCfg := engine.MockConfig().WithStorage(tc.tierCfgs...)
			engineCfgs := []*engine.Config{engineCfg}
			if tc.multiEngine {
				engineCfgs = append(engineCfgs, engineCfg)
			}
			sCfg := config.DefaultServer().WithEngines(engineCfgs...)

			// tests are for pre-engine-start scenario so pass notStarted: true
			cs := mockControlService(t, log, sCfg, tc.bmbc, tc.smbc, nil, true)

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
	const (
		clusterSize uint64 = 32 * humanize.MiByte
		metaWalSize uint64 = 64 * humanize.MiByte
		rdbSize     uint64 = defaultRdbSize
		rdbWalSize  uint64 = 512 * humanize.MiByte
	)

	adjustScmSize := func(sizeBytes uint64, withMdDaosScm bool) uint64 {
		mdBytes := rdbSize + mdFsScmBytes
		if withMdDaosScm {
			mdBytes += mdDaosScmBytes
		}

		if sizeBytes < mdBytes {
			return 0
		}

		return sizeBytes - mdBytes
	}

	adjustNvmeSize := func(nvmeCtlr *ctlpb.NvmeController, mdBytes uint64, engineTargetCount int) *ctlpb.NvmeController {
		getClusterCount := func(sizeBytes uint64) uint64 {
			clusterCount := sizeBytes / clusterSize
			if sizeBytes%clusterSize != 0 {
				clusterCount += 1
			}
			return clusterCount
		}

		type deviceSizeStat struct {
			clusterPerTarget uint64
			smdDevs          []*ctlpb.SmdDevice
		}
		devicesToAdjust := make(map[uint32]*deviceSizeStat, 0)
		for _, dev := range nvmeCtlr.GetSmdDevices() {
			targetCount := uint64(len(dev.GetTgtIds()))
			dev.MetaSize = adjustScmSize(mdBytes, false) / uint64(engineTargetCount)
			dev.AvailBytes = (dev.GetAvailBytes() / clusterSize) * clusterSize

			usableClusterCount := dev.GetAvailBytes() / clusterSize
			usableClusterCount -= getClusterCount(dev.MetaSize) * uint64(engineTargetCount)
			usableClusterCount -= getClusterCount(metaWalSize) * uint64(engineTargetCount)
			usableClusterCount -= getClusterCount(rdbSize)
			usableClusterCount -= getClusterCount(rdbWalSize)

			rank := dev.GetRank()
			if devicesToAdjust[rank] == nil {
				devicesToAdjust[rank] = &deviceSizeStat{
					clusterPerTarget: math.MaxUint64,
				}
			}
			devicesToAdjust[rank].smdDevs = append(devicesToAdjust[rank].smdDevs, dev)
			clusterPerTarget := usableClusterCount / targetCount
			if clusterPerTarget < devicesToAdjust[rank].clusterPerTarget {
				devicesToAdjust[rank].clusterPerTarget = clusterPerTarget
			}
		}

		for _, item := range devicesToAdjust {
			for _, dev := range item.smdDevs {
				targetCount := uint64(len(dev.GetTgtIds()))
				dev.UsableBytes = item.clusterPerTarget * targetCount * clusterSize
			}
		}

		return nvmeCtlr
	}

	// output to be returned from mock bdev backend
	newCtrlr := func(idx int32) *storage.NvmeController {
		ctrlr := storage.MockNvmeController(idx)
		ctrlr.Serial = test.MockUUID(idx)
		ctrlr.SmdDevices = nil

		return ctrlr
	}
	newCtrlrMultiNs := func(idx int32, numNss int) *storage.NvmeController {
		ctrlr := storage.MockNvmeController(idx)
		ctrlr.Serial = test.MockUUID(idx)
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
		ctrlr.Serial = test.MockUUID(sIdx)
		ctrlr.HealthStats = proto.MockNvmeHealth(idx + 1)
		ctrlr.HealthStats.ClusterSize = clusterSize
		ctrlr.HealthStats.MetaWalSize = metaWalSize
		ctrlr.HealthStats.RdbWalSize = rdbWalSize
		ctrlr.SmdDevices = nil

		bioHealthResp := new(ctlpb.BioHealthResp)
		if err := convert.Types(ctrlr.HealthStats, bioHealthResp); err != nil {
			t.Fatal(err)
		}
		bioHealthResp.TotalBytes = uint64(idx) * uint64(humanize.TByte)
		bioHealthResp.AvailBytes = uint64(idx) * uint64(humanize.TByte/2)
		bioHealthResp.ClusterSize = clusterSize
		bioHealthResp.MetaWalSize = metaWalSize
		bioHealthResp.RdbWalSize = rdbWalSize

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
		ctrlr.Serial = test.MockUUID(ctrlrIdx)
		ctrlr.HealthStats = nil

		if len(smdIndexes) == 0 {
			smdIndexes = append(smdIndexes, ctrlrIdx)
		}
		smdDevRespDevices := make([]*ctlpb.SmdDevice, len(smdIndexes))
		ctrlr.SmdDevices = make([]*ctlpb.SmdDevice, len(smdIndexes))
		ctrlr.Namespaces = make([]*ctlpb.NvmeController_Namespace, len(smdIndexes))
		for i, idx := range smdIndexes {
			sd := proto.MockSmdDevice(ctrlr.PciAddr, idx+1)
			sd.DevState = devStateNormal
			sd.Rank = uint32(ctrlrIdx)
			sd.TrAddr = ctrlr.PciAddr
			ctrlr.SmdDevices[i] = sd

			smdPB := new(ctlpb.SmdDevice)
			if err := convert.Types(sd, smdPB); err != nil {
				t.Fatal(err)
			}
			smdDevRespDevices[i] = smdPB

			// expect resultant controller to have updated utilization values
			ctrlr.SmdDevices[i].TotalBytes = uint64(idx) * uint64(humanize.TByte)
			ctrlr.SmdDevices[i].AvailBytes = uint64(idx) * uint64(humanize.TByte/2)
			ctrlr.SmdDevices[i].ClusterSize = clusterSize
			ctrlr.SmdDevices[i].MetaWalSize = metaWalSize
			ctrlr.SmdDevices[i].RdbSize = rdbSize
			ctrlr.SmdDevices[i].RdbWalSize = rdbWalSize
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
		return c
	}
	newSmdDevResp := func(idx int32, smdIndexes ...int32) *ctlpb.SmdDevResp {
		_, s := newCtrlrMeta(idx, smdIndexes...)
		return s
	}

	smdDevRespStateNew := newSmdDevResp(1)
	smdDevRespStateNew.Devices[0].DevState = devStateNew
	smdDevRespStateNew.Devices[0].ClusterSize = 0
	smdDevRespStateNew.Devices[0].MetaWalSize = 0
	smdDevRespStateNew.Devices[0].RdbWalSize = 0

	ctrlrPBwMetaNew := newCtrlrPBwMeta(1)
	ctrlrPBwMetaNew.SmdDevices[0].AvailBytes = 0
	ctrlrPBwMetaNew.SmdDevices[0].TotalBytes = 0
	ctrlrPBwMetaNew.SmdDevices[0].DevState = devStateNew
	ctrlrPBwMetaNew.SmdDevices[0].ClusterSize = 0
	ctrlrPBwMetaNew.SmdDevices[0].UsableBytes = 0
	ctrlrPBwMetaNew.SmdDevices[0].RdbSize = 0
	ctrlrPBwMetaNew.SmdDevices[0].RdbWalSize = 0
	ctrlrPBwMetaNew.SmdDevices[0].MetaSize = 0
	ctrlrPBwMetaNew.SmdDevices[0].MetaWalSize = 0

	ctrlrPBwMetaNormal := newCtrlrPBwMeta(1)
	ctrlrPBwMetaNormal.SmdDevices[0].AvailBytes = 0
	ctrlrPBwMetaNormal.SmdDevices[0].TotalBytes = 0
	ctrlrPBwMetaNormal.SmdDevices[0].DevState = devStateNormal
	ctrlrPBwMetaNormal.SmdDevices[0].ClusterSize = 0
	ctrlrPBwMetaNormal.SmdDevices[0].UsableBytes = 0
	ctrlrPBwMetaNormal.SmdDevices[0].RdbSize = 0
	ctrlrPBwMetaNormal.SmdDevices[0].RdbWalSize = 0
	ctrlrPBwMetaNormal.SmdDevices[0].MetaSize = 0
	ctrlrPBwMetaNormal.SmdDevices[0].MetaWalSize = 0

	mockPbScmMount0 := proto.MockScmMountPoint(0)
	mockPbScmMount0.Rank += 1
	mockPbScmNamespace0 := proto.MockScmNamespace(0)
	mockPbScmNamespace0.Mount = mockPbScmMount0
	mockPbScmMount1 := proto.MockScmMountPoint(1)
	mockPbScmMount1.Rank += 1
	mockPbScmNamespace1 := proto.MockScmNamespace(1)
	mockPbScmNamespace1.Mount = mockPbScmMount1

	for name, tc := range map[string]struct {
		req               *ctlpb.StorageScanReq
		csCtrlrs          *storage.NvmeControllers   // control service storage provider
		eCtrlrs           []*storage.NvmeControllers // engine storage provider
		smbc              *scm.MockBackendConfig
		smsc              *system.MockSysConfig
		storageCfgs       []storage.TierConfigs
		engineTargetCount []int
		enginesNotReady   bool
		scanTwice         bool
		junkResp          bool
		drpcResps         map[int][]*mockDrpcResponse
		expErr            error
		expResp           *ctlpb.StorageScanResp
	}{
		"engine up; scan bdev basic": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Basic: true},
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(1).PciAddr),
				},
			},
			csCtrlrs:          &storage.NvmeControllers{newCtrlr(1)},
			engineTargetCount: []int{4},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{newCtrlrPBwBasic(1)},
					State:  new(ctlpb.ResponseState),
				},
				Scm:     &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"engine up; scan bdev basic; no bdevs in config": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Basic: true},
			},
			storageCfgs: []storage.TierConfigs{},
			csCtrlrs:    &storage.NvmeControllers{newCtrlr(1)},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{newCtrlrPBwBasic(1)},
					State:  new(ctlpb.ResponseState),
				},
				Scm:     &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"engine up; scan bdev basic; missing bdev in config": {
			req: &ctlpb.StorageScanReq{
				Scm:  new(ctlpb.ScanScmReq),
				Nvme: &ctlpb.ScanNvmeReq{Basic: true},
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(1).PciAddr),
				},
			},
			csCtrlrs:          &storage.NvmeControllers{newCtrlr(2)},
			engineTargetCount: []int{4},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{},
					State:  new(ctlpb.ResponseState),
				},
				Scm:     &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
				MemInfo: proto.MockPBMemInfo(),
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
			engineTargetCount: []int{4},
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
				Scm:     &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"engine up; scan bdev meta": {
			req: &ctlpb.StorageScanReq{
				Scm:  &ctlpb.ScanScmReq{Usage: true},
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
			},
			csCtrlrs: &storage.NvmeControllers{newCtrlr(1)},
			smbc: &scm.MockBackendConfig{
				GetModulesRes: storage.ScmModules{
					storage.MockScmModule(0),
				},
				GetNamespacesRes: storage.ScmNamespaces{
					storage.MockScmNamespace(0),
				},
			},
			smsc: &system.MockSysConfig{
				GetfsUsageResps: []system.GetfsUsageRetval{
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
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(newCtrlr(1).PciAddr),
				},
			},
			engineTargetCount: []int{4},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: newSmdDevResp(1)},
					{Message: newBioHealthResp(1)},
				},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{
						adjustNvmeSize(newCtrlrPBwMeta(1), mockPbScmMount0.AvailBytes, 4),
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
								Class:       mockPbScmMount0.Class,
								DeviceList:  mockPbScmMount0.DeviceList,
								Path:        mockPbScmMount0.Path,
								TotalBytes:  mockPbScmMount0.TotalBytes,
								AvailBytes:  mockPbScmMount0.AvailBytes,
								UsableBytes: adjustScmSize(mockPbScmMount0.AvailBytes, false),
								Rank:        mockPbScmMount0.Rank,
							},
						},
					},
					State: new(ctlpb.ResponseState),
				},
				MemInfo: proto.MockPBMemInfo(),
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
			engineTargetCount: []int{4, 4},
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
				Scm:     &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		// make sure stale information is cleared and not used from cache
		"verify cache invalidation over multiple storage scan calls": {
			req: &ctlpb.StorageScanReq{
				Scm:  &ctlpb.ScanScmReq{Usage: true},
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
			},
			csCtrlrs: &storage.NvmeControllers{newCtrlr(1), newCtrlr(2)},
			eCtrlrs:  []*storage.NvmeControllers{{newCtrlr(1)}, {newCtrlr(2)}},
			smbc: &scm.MockBackendConfig{
				GetModulesRes: storage.ScmModules{
					storage.MockScmModule(0),
				},
				GetNamespacesRes: storage.ScmNamespaces{
					storage.MockScmNamespace(0),
					storage.MockScmNamespace(1),
				},
			},
			smsc: &system.MockSysConfig{
				GetfsUsageResps: []system.GetfsUsageRetval{
					{
						Total: mockPbScmMount0.TotalBytes,
						Avail: mockPbScmMount0.AvailBytes,
					},
					{
						Total: mockPbScmMount1.TotalBytes,
						Avail: mockPbScmMount1.AvailBytes,
					},
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
			scanTwice:         true,
			engineTargetCount: []int{4, 4},
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
						adjustNvmeSize(newCtrlrPBwMeta(1), mockPbScmMount0.AvailBytes, 4),
						adjustNvmeSize(newCtrlrPBwMeta(2), mockPbScmMount1.AvailBytes, 4),
					},
					State: new(ctlpb.ResponseState),
				},
				// Scm: &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
				Scm: &ctlpb.ScanScmResp{
					Namespaces: proto.ScmNamespaces{
						&ctlpb.ScmNamespace{
							Blockdev: mockPbScmNamespace0.Blockdev,
							Dev:      mockPbScmNamespace0.Dev,
							Size:     mockPbScmNamespace0.Size,
							Uuid:     mockPbScmNamespace0.Uuid,
							Mount: &ctlpb.ScmNamespace_Mount{
								Class:       mockPbScmMount0.Class,
								DeviceList:  mockPbScmMount0.DeviceList,
								Path:        mockPbScmMount0.Path,
								TotalBytes:  mockPbScmMount0.TotalBytes,
								AvailBytes:  mockPbScmMount0.AvailBytes,
								UsableBytes: adjustScmSize(mockPbScmMount0.AvailBytes, false),
								Rank:        mockPbScmMount0.Rank,
							},
						},
						&ctlpb.ScmNamespace{
							Blockdev: mockPbScmNamespace1.Blockdev,
							Dev:      mockPbScmNamespace1.Dev,
							Size:     mockPbScmNamespace1.Size,
							Uuid:     mockPbScmNamespace1.Uuid,
							NumaNode: mockPbScmNamespace1.NumaNode,
							Mount: &ctlpb.ScmNamespace_Mount{
								Class:       mockPbScmMount1.Class,
								DeviceList:  mockPbScmMount1.DeviceList,
								Path:        mockPbScmMount1.Path,
								TotalBytes:  mockPbScmMount1.TotalBytes,
								AvailBytes:  mockPbScmMount1.AvailBytes,
								UsableBytes: adjustScmSize(mockPbScmMount1.AvailBytes, false),
								Rank:        mockPbScmMount1.Rank,
							},
						},
					},
					State: new(ctlpb.ResponseState),
				},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"engines up; scan bdev meta; multiple nvme namespaces": {
			req: &ctlpb.StorageScanReq{
				Scm:  &ctlpb.ScanScmReq{Usage: true},
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
			},
			csCtrlrs: &storage.NvmeControllers{
				newCtrlrMultiNs(1, 2), newCtrlrMultiNs(2, 2),
			},
			eCtrlrs: []*storage.NvmeControllers{
				{newCtrlrMultiNs(1, 2)}, {newCtrlrMultiNs(2, 2)},
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes: storage.ScmModules{
					storage.MockScmModule(0),
				},
				GetNamespacesRes: storage.ScmNamespaces{
					storage.MockScmNamespace(0),
					storage.MockScmNamespace(1),
				},
			},
			smsc: &system.MockSysConfig{
				GetfsUsageResps: []system.GetfsUsageRetval{
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
			engineTargetCount: []int{8, 8},
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
						adjustNvmeSize(newCtrlrPBwMeta(1, 1, 2), mockPbScmMount0.AvailBytes, 8),
						adjustNvmeSize(newCtrlrPBwMeta(2, 3, 4), mockPbScmMount1.AvailBytes, 8),
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
								Class:       mockPbScmMount0.Class,
								DeviceList:  mockPbScmMount0.DeviceList,
								Path:        mockPbScmMount0.Path,
								TotalBytes:  mockPbScmMount0.TotalBytes,
								AvailBytes:  mockPbScmMount0.AvailBytes,
								UsableBytes: adjustScmSize(mockPbScmMount0.AvailBytes, false),
								Rank:        mockPbScmMount0.Rank,
							},
						},
						&ctlpb.ScmNamespace{
							Blockdev: mockPbScmNamespace1.Blockdev,
							Dev:      mockPbScmNamespace1.Dev,
							Size:     mockPbScmNamespace1.Size,
							Uuid:     mockPbScmNamespace1.Uuid,
							NumaNode: mockPbScmNamespace1.NumaNode,
							Mount: &ctlpb.ScmNamespace_Mount{
								Class:       mockPbScmMount1.Class,
								DeviceList:  mockPbScmMount1.DeviceList,
								Path:        mockPbScmMount1.Path,
								TotalBytes:  mockPbScmMount1.TotalBytes,
								AvailBytes:  mockPbScmMount1.AvailBytes,
								UsableBytes: adjustScmSize(mockPbScmMount1.AvailBytes, false),
								Rank:        mockPbScmMount1.Rank,
							},
						},
					},
					State: new(ctlpb.ResponseState),
				},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"scan scm usage": {
			req: &ctlpb.StorageScanReq{
				Scm:  &ctlpb.ScanScmReq{Usage: true},
				Nvme: new(ctlpb.ScanNvmeReq),
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes:    storage.ScmModules{storage.MockScmModule(0)},
				GetNamespacesRes: storage.ScmNamespaces{storage.MockScmNamespace(0)},
			},
			smsc: &system.MockSysConfig{
				GetfsUsageResps: []system.GetfsUsageRetval{
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
			engineTargetCount: []int{4},
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
								Class:       mockPbScmMount0.Class,
								DeviceList:  mockPbScmMount0.DeviceList,
								Path:        mockPbScmMount0.Path,
								Rank:        mockPbScmMount0.Rank,
								TotalBytes:  mockPbScmMount0.TotalBytes,
								AvailBytes:  mockPbScmMount0.AvailBytes,
								UsableBytes: adjustScmSize(mockPbScmMount0.AvailBytes, true),
							},
						},
					},
					State: new(ctlpb.ResponseState),
				},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"scan scm usage; pmem not in instance device list": {
			req: &ctlpb.StorageScanReq{
				Scm:  &ctlpb.ScanScmReq{Usage: true},
				Nvme: new(ctlpb.ScanNvmeReq),
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes:    storage.ScmModules{storage.MockScmModule(0)},
				GetNamespacesRes: storage.ScmNamespaces{storage.MockScmNamespace(0)},
			},
			smsc: &system.MockSysConfig{
				GetfsUsageResps: []system.GetfsUsageRetval{
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
			engineTargetCount: []int{4},
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
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"scan scm usage; class ram": {
			req: &ctlpb.StorageScanReq{
				Scm:  &ctlpb.ScanScmReq{Usage: true},
				Nvme: new(ctlpb.ScanNvmeReq),
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes:    storage.ScmModules{storage.MockScmModule(0)},
				GetNamespacesRes: storage.ScmNamespaces{storage.MockScmNamespace(0)},
			},
			smsc: &system.MockSysConfig{
				GetfsUsageResps: []system.GetfsUsageRetval{
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
			engineTargetCount: []int{4},
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
								Class:       "ram",
								Path:        mockPbScmMount0.Path,
								TotalBytes:  mockPbScmMount0.TotalBytes,
								AvailBytes:  mockPbScmMount0.AvailBytes,
								UsableBytes: adjustScmSize(mockPbScmMount0.AvailBytes, true),
								Rank:        mockPbScmMount0.Rank,
							},
						},
					},
					State: new(ctlpb.ResponseState),
				},
				MemInfo: proto.MockPBMemInfo(),
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
				GetModulesRes: storage.ScmModules{
					storage.MockScmModule(0),
				},
				GetNamespacesRes: storage.ScmNamespaces{
					storage.MockScmNamespace(0),
					storage.MockScmNamespace(1),
				},
			},
			smsc: &system.MockSysConfig{
				GetfsUsageResps: []system.GetfsUsageRetval{
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
			engineTargetCount: []int{4, 4},
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
						adjustNvmeSize(newCtrlrPBwMeta(1), mockPbScmMount0.AvailBytes, 4),
						adjustNvmeSize(newCtrlrPBwMeta(2), mockPbScmMount1.AvailBytes, 4),
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
								Class:       mockPbScmMount0.Class,
								DeviceList:  mockPbScmMount0.DeviceList,
								Path:        mockPbScmMount0.Path,
								TotalBytes:  mockPbScmMount0.TotalBytes,
								AvailBytes:  mockPbScmMount0.AvailBytes,
								UsableBytes: adjustScmSize(mockPbScmMount0.AvailBytes, false),
								Rank:        mockPbScmMount0.Rank,
							},
						},
						&ctlpb.ScmNamespace{
							Blockdev: mockPbScmNamespace1.Blockdev,
							Dev:      mockPbScmNamespace1.Dev,
							Size:     mockPbScmNamespace1.Size,
							Uuid:     mockPbScmNamespace1.Uuid,
							NumaNode: mockPbScmNamespace1.NumaNode,
							Mount: &ctlpb.ScmNamespace_Mount{
								Class:       mockPbScmMount1.Class,
								DeviceList:  mockPbScmMount1.DeviceList,
								Path:        mockPbScmMount1.Path,
								TotalBytes:  mockPbScmMount1.TotalBytes,
								AvailBytes:  mockPbScmMount1.AvailBytes,
								UsableBytes: adjustScmSize(mockPbScmMount1.AvailBytes, false),
								Rank:        mockPbScmMount1.Rank,
							},
						},
					},
					State: new(ctlpb.ResponseState),
				},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
		"multi-engine; multi-tier; with usage; engines not ready": {
			req: &ctlpb.StorageScanReq{
				Scm:  &ctlpb.ScanScmReq{Usage: true},
				Nvme: &ctlpb.ScanNvmeReq{Meta: true},
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
			engineTargetCount: []int{4, 4},
			enginesNotReady:   true,
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
			expErr: errEngineNotReady,
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
			engineTargetCount: []int{4},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: smdDevRespStateNew},
					{
						Message: &ctlpb.BioHealthResp{
							Status: int32(daos.Nonexistent),
						},
					},
				},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPBwMetaNew},
					State:  new(ctlpb.ResponseState),
				},
				Scm:     &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
				MemInfo: proto.MockPBMemInfo(),
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
			engineTargetCount: []int{4},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: smdDevRespStateNew},
					{
						Message: &ctlpb.BioHealthResp{
							Status: int32(daos.FreeMemError),
						},
					},
				},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPBwMetaNew},
					State:  new(ctlpb.ResponseState),
				},
				Scm:     &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
				MemInfo: proto.MockPBMemInfo(),
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
			engineTargetCount: []int{4},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{Message: newSmdDevResp(1)},
					{
						Message: &ctlpb.BioHealthResp{
							Status: int32(daos.Nonexistent),
						},
					},
				},
			},
			expResp: &ctlpb.StorageScanResp{
				Nvme: &ctlpb.ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPBwMetaNormal},
					State:  new(ctlpb.ResponseState),
				},
				Scm:     &ctlpb.ScanScmResp{State: new(ctlpb.ResponseState)},
				MemInfo: proto.MockPBMemInfo(),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if len(tc.storageCfgs) != len(tc.drpcResps) {
				t.Fatalf("number of tc.storageCfgs doesn't match num drpc msg groups")
			}

			if len(tc.storageCfgs) == 1 && tc.eCtrlrs == nil && tc.csCtrlrs != nil {
				log.Debugf("using control service storage provider for first engine")
				tc.eCtrlrs = []*storage.NvmeControllers{tc.csCtrlrs}
			}

			var csbmbc *bdev.MockBackendConfig
			if tc.csCtrlrs != nil {
				log.Debugf("bdevs %v to be returned for control service scan", *tc.csCtrlrs)
				csbmbc = &bdev.MockBackendConfig{
					ScanRes: &storage.BdevScanResponse{Controllers: *tc.csCtrlrs},
				}
			}

			var engineCfgs []*engine.Config
			for i, sc := range tc.storageCfgs {
				log.Debugf("storage cfg contains bdevs %v for engine %d", sc.Bdevs(), i)
				engineCfgs = append(engineCfgs,
					engine.MockConfig().
						WithStorage(sc...).
						WithTargetCount(tc.engineTargetCount[i]))
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
					log.Debugf("bdevs %v to be returned for engine %d scan",
						*tc.eCtrlrs[idx], idx)
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
					bdev.NewMockProvider(log, ebmbc), nil)
				if tc.eCtrlrs != nil && len(tc.eCtrlrs) > idx {
					sp.SetBdevCache(storage.BdevScanResponse{
						Controllers: *tc.eCtrlrs[idx],
					})
				}
				te := newTestEngine(log, false, sp, ec)

				if tc.enginesNotReady {
					te.ready.SetFalse()
				}

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
				te.setDrpcClient(newMockDrpcClient(dcc))
				te._superblock.Rank = ranklist.NewRankPtr(uint32(idx + 1))
				for _, tc := range te.storage.GetBdevConfigs() {
					tc.Bdev.DeviceRoles.OptionBits = storage.OptionBits(storage.BdevRoleAll)
				}
				md := te.storage.GetControlMetadata()
				md.Path = "/foo"
				md.DevicePath = md.Path

				cs.harness.instances[idx] = te
			}
			cs.harness.started.SetTrue()

			if tc.req == nil {
				tc.req = &ctlpb.StorageScanReq{
					Scm:  new(ctlpb.ScanScmReq),
					Nvme: new(ctlpb.ScanNvmeReq),
				}
			}

			if tc.scanTwice {
				_, err := cs.StorageScan(test.Context(t), tc.req)
				test.CmpErr(t, tc.expErr, err)
				if err != nil {
					return
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
		superblockExists bool
		instancesStarted bool // engine already started
		sMounts          []string
		sClass           storage.Class
		sDevs            []string
		sSize            int
		bClass           storage.Class
		bDevs            [][]string
		bSize            int
		bmbc             *bdev.MockBackendConfig
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
			defer test.ShowBufferOnFailure(t, buf)

			testDir, cleanup := test.CreateTestDir(t)
			defer cleanup()

			if tc.expResp == nil {
				t.Fatal("expResp test case parameter required")
			}
			test.AssertEqual(t, len(tc.sMounts), len(tc.expResp.Mrets), name)
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
			sysProv := system.NewMockSysProvider(log, smsc)
			mounter := mount.NewProvider(log, sysProv)
			scmProv := scm.NewProvider(log, nil, sysProv, mounter)
			bdevProv := bdev.NewMockProvider(log, tc.bmbc)
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

			// Mimic control service start-up and engine creation where cache is shared
			// to the engines from the base control service storage provider.
			nvmeScanResp, err := cs.NvmeScan(storage.BdevScanRequest{})
			if err != nil {
				t.Fatal(err)
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

				storProv := storage.MockProvider(log, 0, &ec.Storage, sysProv,
					scmProv, bdevProv, nil)

				ei := NewEngineInstance(log, storProv, nil, runner)
				ei.ready.Store(tc.instancesStarted)
				ei.storage.SetBdevCache(*nvmeScanResp)

				// if the instance is expected to have a valid superblock, create one
				if tc.superblockExists {
					if err := ei.createSuperblock(false); err != nil {
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
					case awaitCh <- e.awaitStorageReady(ctx, false):
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
									DevState:    devStateNormal,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        0,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        1,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        1,
									RoleBits:    storage.BdevRoleData,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        1,
									RoleBits:    storage.BdevRoleData,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        1,
									RoleBits:    storage.BdevRoleData,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
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
									DevState:    devStateNew,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
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
									DevState:    devStateFaulty,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        0,
									RoleBits:    storage.BdevRoleData,
								},
							},
						},
						{
							PciAddr: test.MockPCIAddr(2),
							SmdDevices: []*ctlpb.SmdDevice{
								{
									Uuid:       "nvme1",
									TgtIds:     []int32{0, 1, 2},
									TotalBytes: 10 * hugeClusterSize,
									AvailBytes: 10 * hugeClusterSize,
									DevState:   devStateNormal,
									Rank:       0,
									RoleBits:   storage.BdevRoleData,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        0,
									RoleBits:    storage.BdevRoleData | storage.BdevRoleMeta,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        1,
									RoleBits:    storage.BdevRoleData | storage.BdevRoleWAL,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        2,
									RoleBits:    storage.BdevRoleAll,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        3,
									RoleBits:    storage.BdevRoleWAL,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        4,
									RoleBits:    storage.BdevRoleMeta,
								},
							},
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
									DevState:    devStateNormal,
									Rank:        5,
									RoleBits:    storage.BdevRoleMeta | storage.BdevRoleMeta,
								},
							},
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
