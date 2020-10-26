//
// (C) Copyright 2019-2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package server

import (
	"context"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	. "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

func TestServer_CtlSvc_StorageScan_PreIOStart(t *testing.T) {
	ctrlr := storage.MockNvmeController()
	ctrlr.SmdDevices = nil
	ctrlrPB := proto.MockNvmeController()
	ctrlrPB.Healthstats = nil
	ctrlrPB.Smddevices = nil
	ctrlrPBwHealth := proto.MockNvmeController()
	ctrlrPBwHealth.Smddevices = nil

	for name, tc := range map[string]struct {
		multiIO     bool
		req         *StorageScanReq
		bmbc        *bdev.MockBackendConfig
		smbc        *scm.MockBackendConfig
		expSetupErr error
		expErr      error
		expResp     StorageScanResp
	}{
		"successful scan with bdev and scm namespaces": {
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{ctrlr},
				},
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:     storage.ScmModules{storage.MockScmModule()},
				GetNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPB},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{
					Namespaces: proto.ScmNamespaces{proto.MockScmNamespace()},
					State:      new(ResponseState),
				},
			},
		},
		"successful scan no scm namespaces": {
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{ctrlr},
				},
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{storage.MockScmModule()},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPB},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{
					Modules: proto.ScmModules{proto.MockScmModule()},
					State:   new(ResponseState),
				},
			},
		},
		"spdk scan failure": {
			bmbc: &bdev.MockBackendConfig{
				ScanErr: errors.New("spdk scan failed"),
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:     storage.ScmModules{storage.MockScmModule()},
				GetNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					State: &ResponseState{
						Error:  "spdk scan failed",
						Status: ResponseStatus_CTL_ERR_NVME,
					},
				},
				Scm: &ScanScmResp{
					Namespaces: proto.ScmNamespaces{proto.MockScmNamespace()},
					State:      new(ResponseState),
				},
			},
		},
		"scm module discovery failure": {
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{ctrlr},
				},
			},
			smbc: &scm.MockBackendConfig{
				DiscoverErr: errors.New("scm discover failed"),
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPB},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{
					State: &ResponseState{
						Error:  "scm discover failed",
						Status: ResponseStatus_CTL_ERR_SCM,
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
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					State: &ResponseState{
						Error:  "spdk scan failed",
						Status: ResponseStatus_CTL_ERR_NVME,
					},
				},
				Scm: &ScanScmResp{
					State: &ResponseState{
						Error:  "scm discover failed",
						Status: ResponseStatus_CTL_ERR_SCM,
					},
				},
			},
		},
		"scan bdev health with single io server down": {
			req: &StorageScanReq{
				Nvme: &ScanNvmeReq{
					Health: true,
				},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{ctrlr},
				},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPBwHealth},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{
					State: new(ResponseState),
				},
			},
		},
		"scan bdev health with multiple io servers down": {
			multiIO: true,
			req: &StorageScanReq{
				Nvme: &ScanNvmeReq{
					Health: true,
				},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{ctrlr},
				},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					// response should not contain duplicates
					Ctrlrs: proto.NvmeControllers{ctrlrPBwHealth},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{
					State: new(ResponseState),
				},
			},
		},
		"scan bdev meta with io servers down": {
			req: &StorageScanReq{
				Nvme: &ScanNvmeReq{
					Meta: true,
				},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{ctrlr},
				},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{ctrlrPB},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{
					State: new(ResponseState),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			emptyCfg := emptyMockConfig(t)
			ioCfg := ioserver.NewConfig().
				WithBdevClass("nvme").
				WithBdevDeviceList(storage.MockNvmeController().PciAddr)
			ioCfgs := []*ioserver.Config{ioCfg}
			if tc.multiIO {
				ioCfgs = append(ioCfgs, ioCfg)
			}
			defaultWithNvme := newDefaultConfiguration(nil).WithServers(ioCfgs...)

			// test for both empty and default config cases
			for _, config := range []*Configuration{defaultWithNvme, emptyCfg} {
				cs := mockControlService(t, log, config, tc.bmbc, tc.smbc, nil)
				for _, srv := range cs.harness.instances {
					srv.ready.SetFalse()
				}

				// runs discovery for nvme & scm
				err := cs.Setup()
				common.CmpErr(t, tc.expSetupErr, err)
				if err != nil {
					return
				}

				if tc.req == nil {
					tc.req = &StorageScanReq{
						Nvme: new(ScanNvmeReq),
					}
				}

				// cs.StorageScan will never return err
				resp, err := cs.StorageScan(context.TODO(), tc.req)
				if err != nil {
					t.Fatal(err)
				}

				if tc.req.Nvme.Health || tc.req.Nvme.Meta {
					if len(cs.harness.instances) == 0 {
						tc.expResp.Nvme.Ctrlrs = nil
					}
				}

				cmpOpts := []cmp.Option{
					cmpopts.IgnoreFields(NvmeController{}, "Serial"),
				}
				if diff := cmp.Diff(tc.expResp, *resp, cmpOpts...); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
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
	newCtrlrMultiNs := func(idx int32) *storage.NvmeController {
		ctrlr := storage.MockNvmeController(idx)
		ctrlr.Serial = common.MockUUID(idx)
		ctrlr.SmdDevices = nil
		ctrlr.Namespaces = append(ctrlr.Namespaces,
			storage.MockNvmeNamespace(int32(ctrlr.Namespaces[0].ID+1)))

		return ctrlr
	}

	// expected protobuf output to be returned svc.StorageScan when health
	// updated over drpc. Override serial uuid with variable argument
	newCtrlrHealth := func(idx int32, serialIdx ...int32) (*NvmeController, *mgmtpb.BioHealthResp) {
		ctrlr := proto.MockNvmeController(idx)
		sIdx := idx
		if len(serialIdx) > 0 {
			sIdx = serialIdx[0]
		}
		ctrlr.Model = fmt.Sprintf("model-%d", sIdx)
		ctrlr.Serial = common.MockUUID(sIdx)
		ctrlr.Healthstats = proto.MockNvmeHealth(idx + 1)
		ctrlr.Smddevices = nil

		bioHealthResp := new(mgmtpb.BioHealthResp)
		if err := convert.Types(ctrlr.Healthstats, bioHealthResp); err != nil {
			t.Fatal(err)
		}
		bioHealthResp.Model = ctrlr.Model
		bioHealthResp.Serial = ctrlr.Serial
		bioHealthResp.TotalBytes = uint64(idx) * uint64(humanize.TByte)
		bioHealthResp.AvailBytes = uint64(idx) * uint64(humanize.TByte/2)

		return ctrlr, bioHealthResp
	}
	newCtrlrPBwHealth := func(idx int32, serialIdx ...int32) *NvmeController {
		c, _ := newCtrlrHealth(idx, serialIdx...)
		return c
	}
	newBioHealthResp := func(idx int32, serialIdx ...int32) *mgmtpb.BioHealthResp {
		_, b := newCtrlrHealth(idx, serialIdx...)
		return b
	}

	// expected protobuf output to be returned svc.StorageScan when smd
	// updated over drpc
	newCtrlrMeta := func(idx int32) (*NvmeController, *mgmtpb.SmdDevResp_Device) {
		ctrlr := proto.MockNvmeController(idx)
		ctrlr.Serial = common.MockUUID(idx)
		ctrlr.Healthstats = nil
		sd := proto.MockSmdDevice(idx + 1)
		sd.Rank = uint32(idx)
		ctrlr.Smddevices = []*NvmeController_SmdDevice{sd}

		smdDevRespDevice := new(mgmtpb.SmdDevResp_Device)
		if err := convert.Types(ctrlr.Smddevices[0], smdDevRespDevice); err != nil {
			t.Fatal(err)
		}

		// expect resultant controller to have updated utilisation values
		ctrlr.Smddevices[0].TotalBytes = uint64(idx) * uint64(humanize.TByte)
		ctrlr.Smddevices[0].AvailBytes = uint64(idx) * uint64(humanize.TByte/2)

		return ctrlr, smdDevRespDevice
	}
	newCtrlrPBwMeta := func(idx int32) *NvmeController {
		c, _ := newCtrlrMeta(idx)
		return c
	}
	newSmdDevRespDevice := func(idx int32) *mgmtpb.SmdDevResp_Device {
		_, b := newCtrlrMeta(idx)
		return b
	}

	mockPbScmMount := proto.MockScmMountPoint()
	mockPbScmNamespace := proto.MockScmNamespace()
	mockPbScmNamespace.Mount = mockPbScmMount

	for name, tc := range map[string]struct {
		req       *StorageScanReq
		bmbc      *bdev.MockBackendConfig
		smbc      *scm.MockBackendConfig
		smsc      *scm.MockSysConfig
		cfg       *Configuration
		scanTwice bool
		junkResp  bool
		drpcResps map[int][]*mockDrpcResponse
		expErr    error
		expResp   StorageScanResp
	}{
		"scan bdev health with io servers up": {
			req: &StorageScanReq{Nvme: &ScanNvmeReq{Health: true}},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{newCtrlr(1)},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								newSmdDevRespDevice(1),
							},
						},
					},
					{Message: newBioHealthResp(1)},
				},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{newCtrlrPBwHealth(1)},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{State: new(ResponseState)},
			},
		},
		"scan bdev meta with io servers up": {
			req: &StorageScanReq{Nvme: &ScanNvmeReq{Meta: true}},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{newCtrlr(1)},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								newSmdDevRespDevice(1),
							},
						},
					},
					{Message: newBioHealthResp(1)},
				},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{newCtrlrPBwMeta(1)},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{State: new(ResponseState)},
			},
		},
		"scan bdev health with multiple io servers up": {
			req: &StorageScanReq{Nvme: &ScanNvmeReq{Health: true}},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{
						newCtrlr(1), newCtrlr(2),
					},
				},
			},
			cfg: newDefaultConfiguration(nil).WithServers(
				ioserver.NewConfig().
					WithBdevClass("nvme").
					WithBdevDeviceList(storage.MockNvmeController(1).PciAddr),
				ioserver.NewConfig().
					WithBdevClass("nvme").
					WithBdevDeviceList(storage.MockNvmeController(2).PciAddr),
			),
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								newSmdDevRespDevice(1),
							},
						},
					},
					{Message: newBioHealthResp(1)},
				},
				1: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								newSmdDevRespDevice(2),
							},
						},
					},
					{Message: newBioHealthResp(2)},
				},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{
						newCtrlrPBwHealth(1),
						newCtrlrPBwHealth(2),
					},
					State: new(ResponseState),
				},
				Scm: &ScanScmResp{State: new(ResponseState)},
			},
		},
		"scan bdev meta with multiple io servers up": {
			req: &StorageScanReq{Nvme: &ScanNvmeReq{Meta: true}},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{
						newCtrlr(1), newCtrlr(2),
					},
				},
			},
			cfg: newDefaultConfiguration(nil).WithServers(
				ioserver.NewConfig().
					WithBdevClass("nvme").
					WithBdevDeviceList(storage.MockNvmeController(1).PciAddr),
				ioserver.NewConfig().
					WithBdevClass("nvme").
					WithBdevDeviceList(storage.MockNvmeController(2).PciAddr),
			),
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								newSmdDevRespDevice(1),
							},
						},
					},
					{Message: newBioHealthResp(1)},
				},
				1: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								newSmdDevRespDevice(2),
							},
						},
					},
					{Message: newBioHealthResp(2)},
				},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{
						newCtrlrPBwMeta(1),
						newCtrlrPBwMeta(2),
					},
					State: new(ResponseState),
				},
				Scm: &ScanScmResp{State: new(ResponseState)},
			},
		},
		// make sure information is not duplicated in cache
		"verify cache integrity over multiple storage scan calls": {
			req: &StorageScanReq{Nvme: &ScanNvmeReq{Meta: true}},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{
						newCtrlr(1), newCtrlr(2),
					},
				},
			},
			cfg: newDefaultConfiguration(nil).WithServers(
				ioserver.NewConfig().
					WithBdevClass("nvme").
					WithBdevDeviceList(storage.MockNvmeController(1).PciAddr),
				ioserver.NewConfig().
					WithBdevClass("nvme").
					WithBdevDeviceList(storage.MockNvmeController(2).PciAddr),
			),
			scanTwice: true,
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								newSmdDevRespDevice(1),
							},
						},
					},
					{Message: newBioHealthResp(1)},
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								newSmdDevRespDevice(1),
							},
						},
					},
					{Message: newBioHealthResp(1)},
				},
				1: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								newSmdDevRespDevice(2),
							},
						},
					},
					{Message: newBioHealthResp(2)},
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								newSmdDevRespDevice(2),
							},
						},
					},
					{Message: newBioHealthResp(2)},
				},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{
						newCtrlrPBwMeta(1),
						newCtrlrPBwMeta(2),
					},
					State: new(ResponseState),
				},
				Scm: &ScanScmResp{State: new(ResponseState)},
			},
		},
		"scan bdev meta with multiple io servers up with multiple nvme namespaces": {
			req: &StorageScanReq{Nvme: &ScanNvmeReq{Meta: true}},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{
						newCtrlrMultiNs(1), newCtrlrMultiNs(2),
					},
				},
			},
			cfg: newDefaultConfiguration(nil).WithServers(
				ioserver.NewConfig().
					WithBdevClass("nvme").
					WithBdevDeviceList(storage.MockNvmeController(1).PciAddr),
				ioserver.NewConfig().
					WithBdevClass("nvme").
					WithBdevDeviceList(storage.MockNvmeController(2).PciAddr),
			),
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								newSmdDevRespDevice(1),
								newSmdDevRespDevice(2),
							},
						},
					},
					{Message: newBioHealthResp(1, 1)},
					{Message: newBioHealthResp(2, 1)},
				},
				1: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								newSmdDevRespDevice(3),
								newSmdDevRespDevice(4),
							},
						},
					},
					{Message: newBioHealthResp(3, 2)},
					{Message: newBioHealthResp(4, 2)},
				},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{
						&NvmeController{
							Pciaddr:  newCtrlr(1).PciAddr,
							Model:    newCtrlr(1).Model,
							Serial:   newCtrlr(1).Serial,
							Fwrev:    newCtrlr(1).FwRev,
							Socketid: newCtrlr(1).SocketID,
							Namespaces: []*NvmeController_Namespace{
								{
									Id:   newCtrlrMultiNs(1).Namespaces[0].ID,
									Size: newCtrlrMultiNs(1).Namespaces[0].Size,
								},
								{
									Id:   newCtrlrMultiNs(1).Namespaces[1].ID,
									Size: newCtrlrMultiNs(1).Namespaces[1].Size,
								},
							},
							Smddevices: []*NvmeController_SmdDevice{
								{
									Uuid:       newSmdDevRespDevice(1).Uuid,
									TgtIds:     newSmdDevRespDevice(1).TgtIds,
									State:      newSmdDevRespDevice(1).State,
									Rank:       1,
									TotalBytes: newBioHealthResp(1, 1).TotalBytes,
									AvailBytes: newBioHealthResp(1, 1).AvailBytes,
								},
								{
									Uuid:       newSmdDevRespDevice(2).Uuid,
									TgtIds:     newSmdDevRespDevice(2).TgtIds,
									State:      newSmdDevRespDevice(2).State,
									Rank:       1,
									TotalBytes: newBioHealthResp(2, 1).TotalBytes,
									AvailBytes: newBioHealthResp(2, 1).AvailBytes,
								},
							},
						},
						&NvmeController{
							Pciaddr:  newCtrlr(2).PciAddr,
							Model:    newCtrlr(2).Model,
							Serial:   newCtrlr(2).Serial,
							Fwrev:    newCtrlr(2).FwRev,
							Socketid: newCtrlr(2).SocketID,
							Namespaces: []*NvmeController_Namespace{
								{
									Id:   newCtrlrMultiNs(2).Namespaces[0].ID,
									Size: newCtrlrMultiNs(2).Namespaces[0].Size,
								},
								{
									Id:   newCtrlrMultiNs(2).Namespaces[1].ID,
									Size: newCtrlrMultiNs(2).Namespaces[1].Size,
								},
							},
							Smddevices: []*NvmeController_SmdDevice{
								{
									Uuid:       newSmdDevRespDevice(3).Uuid,
									TgtIds:     newSmdDevRespDevice(3).TgtIds,
									State:      newSmdDevRespDevice(3).State,
									Rank:       2,
									TotalBytes: newBioHealthResp(3, 2).TotalBytes,
									AvailBytes: newBioHealthResp(3, 2).AvailBytes,
								},
								{
									Uuid:       newSmdDevRespDevice(4).Uuid,
									TgtIds:     newSmdDevRespDevice(4).TgtIds,
									State:      newSmdDevRespDevice(4).State,
									Rank:       2,
									TotalBytes: newBioHealthResp(4, 2).TotalBytes,
									AvailBytes: newBioHealthResp(4, 2).AvailBytes,
								},
							},
						},
					},
					State: new(ResponseState),
				},
				Scm: &ScanScmResp{State: new(ResponseState)},
			},
		},
		"scan scm with space utilization": {
			smbc: &scm.MockBackendConfig{
				DiscoverRes:     storage.ScmModules{storage.MockScmModule()},
				GetNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
			smsc: &scm.MockSysConfig{
				GetfsUsageTotal: mockPbScmMount.TotalBytes,
				GetfsUsageAvail: mockPbScmMount.AvailBytes,
			},
			cfg: newDefaultConfiguration(nil).WithServers(
				ioserver.NewConfig().
					WithScmMountPoint(mockPbScmMount.Path).
					WithScmClass(storage.ScmClassDCPM.String()).
					WithScmDeviceList(mockPbScmNamespace.Blockdev)),
			drpcResps: map[int][]*mockDrpcResponse{
				0: {},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					State: new(ResponseState),
				},
				Scm: &ScanScmResp{
					Namespaces: proto.ScmNamespaces{mockPbScmNamespace},
					State:      new(ResponseState),
				},
			},
		},
		"scan scm with pmem not in instance device list": {
			smbc: &scm.MockBackendConfig{
				DiscoverRes:     storage.ScmModules{storage.MockScmModule()},
				GetNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
			smsc: &scm.MockSysConfig{
				GetfsUsageTotal: mockPbScmMount.TotalBytes,
				GetfsUsageAvail: mockPbScmMount.AvailBytes,
			},
			cfg: newDefaultConfiguration(nil).WithServers(
				ioserver.NewConfig().
					WithScmMountPoint(mockPbScmMount.Path).
					WithScmClass(storage.ScmClassDCPM.String()).
					WithScmDeviceList("/dev/foo", "/dev/bar")),
			drpcResps: map[int][]*mockDrpcResponse{
				0: {},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					State: new(ResponseState),
				},
				Scm: &ScanScmResp{
					Namespaces: proto.ScmNamespaces{proto.MockScmNamespace()},
					State:      new(ResponseState),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.cfg == nil {
				tc.cfg = newDefaultConfiguration(nil).WithServers(
					ioserver.NewConfig().
						WithBdevClass("nvme").
						WithBdevDeviceList(storage.MockNvmeController().PciAddr),
				)
			}
			if len(tc.cfg.Servers) != len(tc.drpcResps) {
				t.Fatalf("num servers in tc.cfg doesn't match num drpc msgs")
			}

			cs := mockControlService(t, log, tc.cfg, tc.bmbc, tc.smbc, tc.smsc)
			cs.harness.started.SetTrue()
			for i := range cs.harness.instances {
				// replace harness instance with mock IO server
				// to enable mocking of harness instance drpc channel
				newSrv := newTestIOServer(log, false, tc.cfg.Servers[i])
				newSrv.scmProvider = cs.scm
				cs.harness.instances[i] = newSrv

				cfg := new(mockDrpcClientConfig)
				if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					for _, mock := range tc.drpcResps[i] {
						cfg.setSendMsgResponseList(t, mock)
					}
				}
				newSrv.setDrpcClient(newMockDrpcClient(cfg))
				newSrv._superblock.Rank = system.NewRankPtr(uint32(i + 1))
			}

			// runs discovery for nvme & scm
			if err := cs.Setup(); err != nil {
				t.Fatal(err)
			}

			if tc.req == nil {
				tc.req = &StorageScanReq{
					Nvme: new(ScanNvmeReq),
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

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreFields(NvmeController{}, "Serial"),
			}
			if diff := cmp.Diff(tc.expResp, *resp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestServer_CtlSvc_StoragePrepare(t *testing.T) {
	for name, tc := range map[string]struct {
		bmbc    *bdev.MockBackendConfig
		smbc    *scm.MockBackendConfig
		req     StoragePrepareReq
		expResp *StoragePrepareResp
	}{
		"success": {
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{storage.MockScmModule()},
			},
			req: StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  &PrepareScmReq{},
			},
			expResp: &StoragePrepareResp{
				Nvme: &PrepareNvmeResp{State: new(ResponseState)},
				Scm:  &PrepareScmResp{State: new(ResponseState)},
			},
		},
		"scm only": {
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{storage.MockScmModule()},
			},
			req: StoragePrepareReq{
				Nvme: nil,
				Scm:  &PrepareScmReq{},
			},
			expResp: &StoragePrepareResp{
				Nvme: nil,
				Scm:  &PrepareScmResp{State: new(ResponseState)},
			},
		},
		"nvme only": {
			req: StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  nil,
			},
			expResp: &StoragePrepareResp{
				Nvme: &PrepareNvmeResp{State: new(ResponseState)},
				Scm:  nil,
			},
		},
		"success with pmem devices": {
			smbc: &scm.MockBackendConfig{
				DiscoverRes:      storage.ScmModules{storage.MockScmModule()},
				PrepNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
				PrepNeedsReboot:  true,
			},
			req: StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  &PrepareScmReq{},
			},
			expResp: &StoragePrepareResp{
				Nvme: &PrepareNvmeResp{State: new(ResponseState)},
				Scm: &PrepareScmResp{
					State: &ResponseState{
						Info: scm.MsgRebootRequired,
					},
					Namespaces:     []*ScmNamespace{proto.MockScmNamespace()},
					Rebootrequired: true,
				},
			},
		},
		"fail scm prep": {
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{storage.MockScmModule()},
				PrepErr:     errors.New("scm prep error"),
			},
			req: StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  &PrepareScmReq{},
			},
			expResp: &StoragePrepareResp{
				Nvme: &PrepareNvmeResp{State: new(ResponseState)},
				Scm: &PrepareScmResp{
					State: &ResponseState{
						Status: ResponseStatus_CTL_ERR_SCM,
						Error:  "scm prep error",
					},
				},
			},
		},
		"fail nvme prep": {
			bmbc: &bdev.MockBackendConfig{
				PrepareErr: errors.New("nvme prep error"),
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{storage.MockScmModule()},
			},
			req: StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  &PrepareScmReq{},
			},
			expResp: &StoragePrepareResp{
				Nvme: &PrepareNvmeResp{
					State: &ResponseState{
						Status: ResponseStatus_CTL_ERR_NVME,
						Error:  "nvme prep error",
					},
				},
				Scm: &PrepareScmResp{
					State: new(ResponseState),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			config := newDefaultConfiguration(nil)
			cs := mockControlService(t, log, config, tc.bmbc, tc.smbc, nil)
			_ = new(StoragePrepareResp)

			// runs discovery for nvme & scm
			if err := cs.Setup(); err != nil {
				t.Fatal(err.Error() + name)
			}

			// StoragePrepare should never return an error
			resp, err := cs.StoragePrepare(context.TODO(), &tc.req)
			if err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(tc.expResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestServer_CtlSvc_StorageFormat(t *testing.T) {
	mockNvmeController0 := storage.MockNvmeController(0)
	mockNvmeController1 := storage.MockNvmeController(1)
	defaultAddrStr := "127.0.0.1:10001"
	defaultAddr, err := net.ResolveTCPAddr("tcp", defaultAddrStr)
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		scmMounted       bool // if scmMounted we emulate ext4 fs is mounted
		superblockExists bool
		instancesStarted bool // io_server already started
		recreateSBs      bool
		mountRet         error
		unmountRet       error
		mkdirRet         error
		removeRet        error
		sMounts          []string
		sClass           storage.ScmClass
		sDevs            []string
		sSize            int
		bClass           storage.BdevClass
		bDevs            [][]string
		bmbc             *bdev.MockBackendConfig
		awaitTimeout     time.Duration
		expAwaitExit     bool
		expAwaitErr      error
		expResp          *StorageFormatResp
		isRoot           bool
		reformat         bool // indicates setting of reformat parameter
	}{
		"ram no nvme": {
			sMounts: []string{"/mnt/daos"},
			sClass:  storage.ScmClassRAM,
			sSize:   6,
			expResp: &StorageFormatResp{
				Crets: []*NvmeControllerResult{},
				Mrets: []*ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State:    new(ResponseState),
					},
				},
			},
		},
		"dcpm no nvme": {
			sMounts: []string{"/mnt/daos"},
			sClass:  storage.ScmClassDCPM,
			sDevs:   []string{"/dev/pmem1"},
			expResp: &StorageFormatResp{
				Crets: []*NvmeControllerResult{},
				Mrets: []*ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State:    new(ResponseState),
					},
				},
			},
		},
		"nvme and ram": {
			sMounts: []string{"/mnt/daos"},
			sClass:  storage.ScmClassRAM,
			sDevs:   []string{"/dev/pmem1"}, // ignored if SCM class is ram
			sSize:   6,
			bClass:  storage.BdevClassNvme,
			bDevs:   [][]string{{mockNvmeController0.PciAddr}},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{mockNvmeController0},
				},
				FormatRes: &bdev.FormatResponse{
					DeviceResponses: bdev.DeviceFormatResponses{
						mockNvmeController0.PciAddr: &bdev.DeviceFormatResponse{
							Formatted: true,
						},
					},
				},
			},
			expResp: &StorageFormatResp{
				Crets: []*NvmeControllerResult{
					{
						Pciaddr: mockNvmeController0.PciAddr,
						State:   new(ResponseState),
					},
				},
				Mrets: []*ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State:    new(ResponseState),
					},
				},
			},
		},
		"nvme and dcpm": {
			sMounts: []string{"/mnt/daos"},
			sClass:  storage.ScmClassDCPM,
			sDevs:   []string{"dev/pmem0"},
			bClass:  storage.BdevClassNvme,
			bDevs:   [][]string{{mockNvmeController0.PciAddr}},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{mockNvmeController0},
				},
				FormatRes: &bdev.FormatResponse{
					DeviceResponses: bdev.DeviceFormatResponses{
						mockNvmeController0.PciAddr: &bdev.DeviceFormatResponse{
							Formatted: true,
						},
					},
				},
			},
			expResp: &StorageFormatResp{
				Crets: []*NvmeControllerResult{
					{
						Pciaddr: mockNvmeController0.PciAddr,
						State:   new(ResponseState),
					},
				},
				Mrets: []*ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State:    new(ResponseState),
					},
				},
			},
		},
		"io instances already running": { // await should exit immediately
			instancesStarted: true,
			scmMounted:       true,
			sMounts:          []string{"/mnt/daos"},
			sClass:           storage.ScmClassRAM,
			sSize:            6,
			bClass:           storage.BdevClassNvme,
			bDevs:            [][]string{{mockNvmeController0.PciAddr}},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{mockNvmeController0},
				},
			},
			expAwaitExit: true,
			expAwaitErr:  errors.New("can't wait for storage: instance 0 already started"),
			awaitTimeout: time.Second,
			expResp: &StorageFormatResp{
				Crets: []*NvmeControllerResult{
					{
						Pciaddr: "<nil>",
						State: &ResponseState{
							Status: ResponseStatus_CTL_SUCCESS,
							Info:   fmt.Sprintf(msgNvmeFormatSkip, 0),
						},
					},
				},
				Mrets: []*ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State: &ResponseState{
							Status: ResponseStatus_CTL_ERR_SCM,
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
			sClass:     storage.ScmClassRAM,
			sSize:      6,
			bClass:     storage.BdevClassNvme,
			bDevs:      [][]string{{mockNvmeController0.PciAddr}},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{mockNvmeController0},
				},
			},
			expResp: &StorageFormatResp{
				Crets: []*NvmeControllerResult{
					{
						Pciaddr: "<nil>",
						State: &ResponseState{
							Status: ResponseStatus_CTL_SUCCESS,
							Info:   fmt.Sprintf(msgNvmeFormatSkip, 0),
						},
					},
				},
				Mrets: []*ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State: &ResponseState{
							Status: ResponseStatus_CTL_ERR_SCM,
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
			sClass:     storage.ScmClassRAM,
			sSize:      6,
			bClass:     storage.BdevClassNvme,
			bDevs:      [][]string{{mockNvmeController0.PciAddr}},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{mockNvmeController0},
				},
				FormatRes: &bdev.FormatResponse{
					DeviceResponses: bdev.DeviceFormatResponses{
						mockNvmeController0.PciAddr: &bdev.DeviceFormatResponse{
							Formatted: true,
						},
					},
				},
			},
			expResp: &StorageFormatResp{
				Crets: []*NvmeControllerResult{
					{
						Pciaddr: mockNvmeController0.PciAddr,
						State:   new(ResponseState),
					},
				},
				Mrets: []*ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State:    new(ResponseState),
					},
				},
			},
		},
		"dcpm already mounted no reformat": {
			scmMounted: true,
			sMounts:    []string{"/mnt/daos"},
			sClass:     storage.ScmClassDCPM,
			sDevs:      []string{"/dev/pmem1"},
			bClass:     storage.BdevClassNvme,
			bDevs:      [][]string{{mockNvmeController0.PciAddr}},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{mockNvmeController0},
				},
			},
			expResp: &StorageFormatResp{
				Crets: []*NvmeControllerResult{
					{
						Pciaddr: "<nil>",
						State: &ResponseState{
							Status: ResponseStatus_CTL_SUCCESS,
							Info:   fmt.Sprintf(msgNvmeFormatSkip, 0),
						},
					},
				},
				Mrets: []*ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State: &ResponseState{
							Status: ResponseStatus_CTL_ERR_SCM,
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
			sClass:     storage.ScmClassDCPM,
			sDevs:      []string{"/dev/pmem1"},
			bClass:     storage.BdevClassNvme,
			bDevs:      [][]string{{mockNvmeController0.PciAddr}},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{mockNvmeController0},
				},
				FormatRes: &bdev.FormatResponse{
					DeviceResponses: bdev.DeviceFormatResponses{
						mockNvmeController0.PciAddr: &bdev.DeviceFormatResponse{
							Formatted: true,
						},
					},
				},
			},
			expResp: &StorageFormatResp{
				Crets: []*NvmeControllerResult{
					{
						Pciaddr: mockNvmeController0.PciAddr,
						State:   new(ResponseState),
					},
				},
				Mrets: []*ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State:    new(ResponseState),
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
			sClass:           storage.ScmClassDCPM,
			sDevs:            []string{"/dev/pmem1"},
			bClass:           storage.BdevClassNvme,
			bDevs:            [][]string{{mockNvmeController0.PciAddr}},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{mockNvmeController0},
				},
			},
			expAwaitExit: true,
			awaitTimeout: time.Second,
			expResp: &StorageFormatResp{
				Mrets: []*ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State:    new(ResponseState),
					},
				},
			},
		},
		"nvme and dcpm success multi-io": {
			sMounts: []string{"/mnt/daos0", "/mnt/daos1"},
			sClass:  storage.ScmClassDCPM,
			sDevs:   []string{"/dev/pmem0", "/dev/pmem1"},
			bClass:  storage.BdevClassNvme,
			bDevs: [][]string{
				{mockNvmeController0.PciAddr},
				{mockNvmeController1.PciAddr},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{
					Controllers: storage.NvmeControllers{mockNvmeController0, mockNvmeController1},
				},
				FormatRes: &bdev.FormatResponse{
					DeviceResponses: bdev.DeviceFormatResponses{
						mockNvmeController0.PciAddr: &bdev.DeviceFormatResponse{
							Formatted: true,
						},
					},
				},
			},
			expResp: &StorageFormatResp{
				Crets: []*NvmeControllerResult{
					{
						Pciaddr: mockNvmeController0.PciAddr,
						State:   new(ResponseState),
					},
					{
						// this should be id 1 but mock
						// backend spits same output for
						// both IO server instances
						Pciaddr: mockNvmeController0.PciAddr,
						State:   new(ResponseState),
					},
				},
				Mrets: []*ScmMountResult{
					{
						Mntpoint:    "/mnt/daos0",
						State:       new(ResponseState),
						Instanceidx: 0,
					},
					{
						Mntpoint:    "/mnt/daos1",
						State:       new(ResponseState),
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

			config := newDefaultConfiguration(newMockExt(tc.isRoot))

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

			// add all IO server configurations
			for idx, scmMount := range tc.sMounts {
				if tc.sClass == storage.ScmClassDCPM {
					devToMount[tc.sDevs[idx]] = scmMount
					t.Logf("sDevs[%d]= %v, value= %v", idx, tc.sDevs[idx], scmMount)
				}
				iosrv := ioserver.NewConfig().
					WithScmMountPoint(scmMount).
					WithScmClass(tc.sClass.String()).
					WithBdevClass(tc.bClass.String()).
					WithScmRamdiskSize(tc.sSize).
					WithBdevDeviceList(tc.bDevs[idx]...).
					WithScmDeviceList(tc.sDevs[idx])
				config.Servers = append(config.Servers, iosrv)
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
			cs := mockControlService(t, log, config, tc.bmbc, nil, msc)

			instances := cs.harness.Instances()
			common.AssertEqual(t, len(tc.sMounts), len(instances), name)

			// runs discovery for nvme & scm
			if err := cs.Setup(); err != nil {
				t.Fatal(err.Error() + name)
			}

			for i, srv := range instances {
				root := filepath.Dir(tc.sMounts[i])
				if tc.scmMounted {
					root = tc.sMounts[i]
				}
				if err := os.MkdirAll(root, 0777); err != nil {
					t.Fatal(err)
				}

				msClientCfg := mgmtSvcClientCfg{
					ControlAddr:  defaultAddr,
					AccessPoints: []string{defaultAddrStr},
				}
				srv.msClient = newMgmtSvcClient(context.TODO(), log, msClientCfg)

				// if the instance is expected to have a valid superblock, create one
				if tc.superblockExists {
					if err := srv.createSuperblock(false); err != nil {
						t.Fatal(err)
					}
				}

				trc := &ioserver.TestRunnerConfig{}
				if tc.instancesStarted {
					trc.Running.SetTrue()
					srv.ready.SetTrue()
				}
				srv.runner = ioserver.NewTestRunner(trc, config.Servers[i])
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
				go func(s *IOServerInstance) {
					awaitCh <- s.awaitStorageReady(ctx, tc.recreateSBs)
				}(srv)
			}

			awaitingFormat := make(chan struct{})
			t.Log("waiting for awaiting format state")
			go func(ctxIn context.Context) {
				for {
					ready := true
					for _, srv := range instances {
						if !srv.isAwaitingFormat() {
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

			resp, fmtErr := cs.StorageFormat(context.TODO(), &StorageFormatReq{Reformat: tc.reformat})
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
					if diff := cmp.Diff(exp, got); diff == "" {
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
					if diff := cmp.Diff(exp, got); diff == "" {
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
