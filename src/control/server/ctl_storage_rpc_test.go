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
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/common/proto/ctl"
	. "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// mockStorageFormatServer provides mocking for server side streaming,
// implement send method and record sent format responses.
type mockStorageFormatServer struct {
	grpc.ServerStream
	Results []*StorageFormatResp
}

func (m *mockStorageFormatServer) Send(resp *StorageFormatResp) error {
	m.Results = append(m.Results, resp)
	return nil
}

// return config reference with customised storage config behaviour and params
func newMockStorageConfig(
	mountRet error, unmountRet error, mkdirRet error, removeRet error,
	scmMount string, scmClass storage.ScmClass, scmDevs []string, scmSize int,
	bdevClass storage.BdevClass, bdevDevs []string, existsRet bool, isRoot bool,
) *Configuration {

	c := newDefaultConfiguration(newMockExt(isRoot))

	c.Servers = append(c.Servers,
		ioserver.NewConfig().
			WithScmMountPoint(scmMount).
			WithScmClass(scmClass.String()).
			WithScmDeviceList(scmDevs...).
			WithScmRamdiskSize(scmSize).
			WithBdevClass(bdevClass.String()).
			WithBdevDeviceList(bdevDevs...),
	)

	return c
}

func TestStorageScan(t *testing.T) {
	for name, tc := range map[string]struct {
		bmbc        *bdev.MockBackendConfig
		smbc        *scm.MockBackendConfig
		expSetupErr error
		expResp     StorageScanResp
	}{
		"successful scan with bdev and scm namespaces": {
			bmbc: &bdev.MockBackendConfig{
				ScanRes: storage.NvmeControllers{storage.MockNvmeController()},
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:     storage.ScmModules{storage.MockScmModule()},
				GetNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{proto.MockNvmeController()},
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
				ScanRes: storage.NvmeControllers{storage.MockNvmeController()},
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{storage.MockScmModule()},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{proto.MockNvmeController()},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{
					Modules: proto.ScmModules{proto.MockScmModule()},
					State:   new(ResponseState),
				},
			},
		},
		"spdk init failure": {
			bmbc: &bdev.MockBackendConfig{
				InitErr: errors.New("spdk init failed"),
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:     storage.ScmModules{storage.MockScmModule()},
				GetNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					State: &ResponseState{
						Error:  "spdk init failed",
						Status: ResponseStatus_CTL_ERR_NVME,
					},
				},
				Scm: &ScanScmResp{
					Namespaces: proto.ScmNamespaces{proto.MockScmNamespace()},
					State:      new(ResponseState),
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
				ScanRes: storage.NvmeControllers{storage.MockNvmeController()},
			},
			smbc: &scm.MockBackendConfig{
				DiscoverErr: errors.New("scm discover failed"),
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{proto.MockNvmeController()},
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
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			emptyCfg := emptyMockConfig(t)
			defaultWithNvme := newDefaultConfiguration(nil).WithServers(
				ioserver.NewConfig().
					WithBdevClass("nvme").
					WithBdevDeviceList(storage.MockNvmeController().PciAddr),
			)

			// test for both empty and default config cases
			for configIdx, config := range []*Configuration{defaultWithNvme, emptyCfg} {
				cs := mockControlService(t, log, config, tc.bmbc, tc.smbc, nil)

				// runs discovery for nvme & scm
				err := cs.Setup()
				if err != nil {
					common.CmpErr(t, tc.expSetupErr, err)
				} else {
					// if emptyMockConfig (configIdx == 1), no err raised in setup.
					if configIdx == 0 {
						common.CmpErr(t, tc.expSetupErr, nil)
					}
				}

				// cs.StorageScan will never return err
				resp, err := cs.StorageScan(context.TODO(), &StorageScanReq{})
				if err != nil {
					t.Fatal(err)
				}

				cmpOpts := []cmp.Option{
					cmpopts.IgnoreFields(ctl.NvmeController{}, "Healthstats", "Serial"),
				}
				if diff := cmp.Diff(tc.expResp, *resp, cmpOpts...); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}
		})
	}
}

func TestStoragePrepare(t *testing.T) {
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
			},
			req: StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  &PrepareScmReq{},
			},
			expResp: &StoragePrepareResp{
				Nvme: &PrepareNvmeResp{State: new(ResponseState)},
				Scm: &PrepareScmResp{
					State:      new(ResponseState),
					Namespaces: []*ScmNamespace{proto.MockScmNamespace()},
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
						Error:  "SPDK prepare: nvme prep error",
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

// TODO: re-work test after making multi-io format concurrent (DAOS-4627)
//       and decoupling instance flow-control (DAOS-4456)
//func TestStorageFormat(t *testing.T) {
//	var (
//		mockNvmeController0 = storage.MockNvmeController(0)
//		mockNvmeController1 = storage.MockNvmeController(1)
//	)
//
//	for name, tc := range map[string]struct {
//		superblockExists bool
//		mountRet         error
//		unmountRet       error
//		mkdirRet         error
//		removeRet        error
//		sMounts          []string
//		sClass           storage.ScmClass
//		sDevs            []string
//		sSize            int
//		bClass           storage.BdevClass
//		bDevs            [][]string
//		expNvmeFormatted bool
//		bmbc             *bdev.MockBackendConfig
//		expResp          *StorageFormatResp
//		isRoot           bool
//		reformat         bool
//	}{
//		"ram success": {
//			sMounts:          []string{"/mnt/daos"},
//			sClass:           storage.ScmClassRAM,
//			sSize:            6,
//			expNvmeFormatted: true,
//			expResp: &StorageFormatResp{
//				Crets: []*NvmeControllerResult{},
//				Mrets: []*ScmMountResult{
//					{
//						Mntpoint: "/mnt/daos",
//						State:    new(ResponseState),
//					},
//				},
//			},
//		},
//		"dcpm success": {
//			sMounts:          []string{"/mnt/daos"},
//			sClass:           storage.ScmClassDCPM,
//			sDevs:            []string{"/dev/pmem1"},
//			expNvmeFormatted: true,
//			expResp: &StorageFormatResp{
//				Crets: []*NvmeControllerResult{},
//				Mrets: []*ScmMountResult{
//					{
//						Mntpoint: "/mnt/daos",
//						State:    new(ResponseState),
//					},
//				},
//			},
//		},
//		"nvme and ram success": {
//			sMounts:          []string{"/mnt/daos"},
//			sClass:           storage.ScmClassRAM,
//			sDevs:            []string{"/dev/pmem1"}, // ignored if SCM class is ram
//			sSize:            6,
//			bClass:           storage.BdevClassNvme,
//			bDevs:            [][]string{[]string{mockNvmeController0.PciAddr}},
//			expNvmeFormatted: true,
//			bmbc: &bdev.MockBackendConfig{
//				ScanRes: storage.NvmeControllers{mockNvmeController0},
//			},
//			expResp: &StorageFormatResp{
//				Crets: []*NvmeControllerResult{
//					{
//						Pciaddr: mockNvmeController0.PciAddr,
//						State:   new(ResponseState),
//					},
//				},
//				Mrets: []*ScmMountResult{
//					{
//						Mntpoint: "/mnt/daos",
//						State:    new(ResponseState),
//					},
//				},
//			},
//		},
//		"nvme and dcpm success": {
//			sMounts:          []string{"/mnt/daos"},
//			sClass:           storage.ScmClassDCPM,
//			sDevs:            []string{"dev/pmem0"},
//			bClass:           storage.BdevClassNvme,
//			bDevs:            [][]string{[]string{mockNvmeController0.PciAddr}},
//			expNvmeFormatted: true,
//			bmbc: &bdev.MockBackendConfig{
//				ScanRes: storage.NvmeControllers{mockNvmeController0},
//			},
//			expResp: &StorageFormatResp{
//				Crets: []*NvmeControllerResult{
//					{
//						Pciaddr: mockNvmeController0.PciAddr,
//						State:   new(ResponseState),
//					},
//				},
//				Mrets: []*ScmMountResult{
//					{
//						Mntpoint: "/mnt/daos",
//						State:    new(ResponseState),
//					},
//				},
//			},
//		},
//		"ram already mounted no reformat": {
//			superblockExists: true, // if superblockExists we emulate ext4 fs is mounted
//			sMounts:          []string{"/mnt/daos"},
//			sClass:           storage.ScmClassRAM,
//			sSize:            6,
//			bClass:           storage.BdevClassNvme,
//			bDevs:            [][]string{[]string{mockNvmeController0.PciAddr}},
//			bmbc: &bdev.MockBackendConfig{
//				ScanRes: storage.NvmeControllers{mockNvmeController0},
//			},
//			expResp: &StorageFormatResp{
//				Crets: []*NvmeControllerResult{
//					{
//						Pciaddr: "<nil>",
//						State: &ResponseState{
//							Status: ResponseStatus_CTL_SUCCESS,
//							Info:   fmt.Sprintf(msgNvmeFormatSkip, 0),
//						},
//					},
//				},
//				Mrets: []*ScmMountResult{
//					{
//						Mntpoint: "/mnt/daos",
//						State: &ResponseState{
//							Status: ResponseStatus_CTL_ERR_SCM,
//							Error:  scm.FaultFormatNoReformat.Error(),
//							Info:   fault.ShowResolutionFor(scm.FaultFormatNoReformat),
//						},
//					},
//				},
//			},
//		},
//		"ram already mounted and reformat set": {
//			superblockExists: true, // if superblockExists we emulate ext4 fs is mounted
//			reformat:         true,
//			sMounts:          []string{"/mnt/daos"},
//			sClass:           storage.ScmClassRAM,
//			sSize:            6,
//			bClass:           storage.BdevClassNvme,
//			bDevs:            [][]string{[]string{mockNvmeController0.PciAddr}},
//			bmbc: &bdev.MockBackendConfig{
//				ScanRes: storage.NvmeControllers{mockNvmeController0},
//			},
//			expNvmeFormatted: true,
//			expResp: &StorageFormatResp{
//				Crets: []*NvmeControllerResult{
//					{
//						Pciaddr: mockNvmeController0.PciAddr,
//						State:   new(ResponseState),
//					},
//				},
//				Mrets: []*ScmMountResult{
//					{
//						Mntpoint: "/mnt/daos",
//						State:    new(ResponseState),
//					},
//				},
//			},
//		},
//		"dcpm already mounted no reformat": {
//			superblockExists: true, // if superblockExists we emulate ext4 fs is mounted
//			sMounts:          []string{"/mnt/daos"},
//			sClass:           storage.ScmClassDCPM,
//			sDevs:            []string{"/dev/pmem1"},
//			bClass:           storage.BdevClassNvme,
//			bDevs:            [][]string{[]string{mockNvmeController0.PciAddr}},
//			bmbc: &bdev.MockBackendConfig{
//				ScanRes: storage.NvmeControllers{mockNvmeController0},
//			},
//			expResp: &StorageFormatResp{
//				Crets: []*NvmeControllerResult{
//					{
//						Pciaddr: "<nil>",
//						State: &ResponseState{
//							Status: ResponseStatus_CTL_SUCCESS,
//							Info:   fmt.Sprintf(msgNvmeFormatSkip, 0),
//						},
//					},
//				},
//				Mrets: []*ScmMountResult{
//					{
//						Mntpoint: "/mnt/daos",
//						State: &ResponseState{
//							Status: ResponseStatus_CTL_ERR_SCM,
//							Error:  scm.FaultFormatNoReformat.Error(),
//							Info:   fault.ShowResolutionFor(scm.FaultFormatNoReformat),
//						},
//					},
//				},
//			},
//		},
//		"dcpm already mounted and reformat set": {
//			superblockExists: true, // if superblockExists we emulate ext4 fs is mounted
//			reformat:         true,
//			sMounts:          []string{"/mnt/daos"},
//			sClass:           storage.ScmClassDCPM,
//			sDevs:            []string{"/dev/pmem1"},
//			bClass:           storage.BdevClassNvme,
//			bDevs:            [][]string{[]string{mockNvmeController0.PciAddr}},
//			bmbc: &bdev.MockBackendConfig{
//				ScanRes: storage.NvmeControllers{mockNvmeController0},
//			},
//			expNvmeFormatted: true,
//			expResp: &StorageFormatResp{
//				Crets: []*NvmeControllerResult{
//					{
//						Pciaddr: mockNvmeController0.PciAddr,
//						State:   new(ResponseState),
//					},
//				},
//				Mrets: []*ScmMountResult{
//					{
//						Mntpoint: "/mnt/daos",
//						State:    new(ResponseState),
//					},
//				},
//			},
//		},
//		"nvme and dcpm success multi-io": {
//			sMounts: []string{"/mnt/daos0", "/mnt/daos1"},
//			sClass:  storage.ScmClassDCPM,
//			sDevs:   []string{"/dev/pmem0", "/dev/pmem1"},
//			bClass:  storage.BdevClassNvme,
//			bDevs: [][]string{
//				[]string{mockNvmeController0.PciAddr},
//				[]string{mockNvmeController1.PciAddr},
//			},
//			expNvmeFormatted: true,
//			bmbc: &bdev.MockBackendConfig{
//				ScanRes: storage.NvmeControllers{mockNvmeController0, mockNvmeController1},
//			},
//			expResp: &StorageFormatResp{
//				Crets: []*NvmeControllerResult{
//					{
//						Pciaddr: mockNvmeController0.PciAddr,
//						State:   new(ResponseState),
//					},
//					{
//						Pciaddr: mockNvmeController1.PciAddr,
//						State:   new(ResponseState),
//					},
//				},
//				Mrets: []*ScmMountResult{
//					{
//						Mntpoint: "/mnt/daos0",
//						State:    new(ResponseState),
//					},
//					{
//						Mntpoint: "/mnt/daos1",
//						State:    new(ResponseState),
//					},
//				},
//			},
//		},
//	} {
//		t.Run(name, func(t *testing.T) {
//			log, buf := logging.NewTestLogger(t.Name())
//			defer common.ShowBufferOnFailure(t, buf)
//
//			testDir, cleanup := common.CreateTestDir(t)
//			defer cleanup()
//
//			common.AssertEqual(t, len(tc.sMounts), len(tc.expResp.Mrets), name)
//			for i := range tc.sMounts {
//				// Hack to deal with creating the mountpoint in test.
//				// FIXME (DAOS-3471): The tests in this layer really shouldn't be
//				// reaching down far enough to actually interact with the filesystem.
//				tc.sMounts[i] = filepath.Join(testDir, tc.sMounts[i])
//				if len(tc.expResp.Mrets) > 0 {
//					mp := &(tc.expResp.Mrets[i].Mntpoint)
//					if *mp != "" {
//						if strings.HasSuffix(tc.sMounts[i], *mp) {
//							*mp = tc.sMounts[i]
//						}
//					}
//				}
//			}
//
//			config := newDefaultConfiguration(newMockExt(tc.isRoot))
//
//			// validate test parameters
//			if len(tc.sDevs) > 0 {
//				common.AssertEqual(t, len(tc.sMounts), len(tc.sDevs), name)
//			} else {
//				tc.sDevs = []string{"/dev/pmem0", "/dev/pmem1"}
//			}
//			if len(tc.bDevs) > 0 {
//				common.AssertEqual(t, len(tc.sMounts), len(tc.bDevs), name)
//			} else {
//				tc.bDevs = [][]string{{}, {}}
//			}
//
//			// map SCM mount targets to source devices
//			devToMount := make(map[string]string)
//
//			// add all IO server configurations
//			for idx, scmMount := range tc.sMounts {
//				if tc.sClass == storage.ScmClassDCPM {
//					devToMount[tc.sDevs[idx]] = scmMount
//					t.Logf("sDevs[%d]= %v, value= %v", idx, tc.sDevs[idx], scmMount)
//				}
//				iosrv := ioserver.NewConfig().
//					WithScmMountPoint(scmMount).
//					WithScmClass(tc.sClass.String()).
//					WithBdevClass(tc.bClass.String()).
//					WithScmRamdiskSize(tc.sSize).
//					WithBdevDeviceList(tc.bDevs[idx]...).
//					WithScmDeviceList(tc.sDevs[idx])
//				config.Servers = append(config.Servers, iosrv)
//			}
//
//			getFsRetStr := "none"
//			if tc.superblockExists {
//				getFsRetStr = "ext4"
//			}
//			msc := &scm.MockSysConfig{
//				IsMountedBool:  tc.superblockExists,
//				MountErr:       tc.mountRet,
//				UnmountErr:     tc.unmountRet,
//				GetfsStr:       getFsRetStr,
//				SourceToTarget: devToMount,
//			}
//			cs := mockControlService(t, log, config, tc.bmbc, nil, msc)
//
//			common.AssertEqual(t, len(tc.sMounts), len(cs.harness.Instances()), name)
//
//			// runs discovery for nvme & scm
//			if err := cs.Setup(); err != nil {
//				t.Fatal(err.Error() + name)
//			}
//
//			mockWg := new(sync.WaitGroup)
//			mockWg.Add(1)
//
//			for i, instance := range cs.harness.Instances() {
//				root := filepath.Dir(tc.sMounts[i])
//				if tc.superblockExists {
//					root = tc.sMounts[i]
//				}
//				if err := os.MkdirAll(root, 0777); err != nil {
//					t.Fatal(err)
//				}
//
//				// if the instance is expected to have a valid superblock, create one
//				if tc.superblockExists {
//					if err := instance.CreateSuperblock(&mgmtInfo{}); err != nil {
//						t.Fatal(err)
//					}
//				}
//			}
//
//			var resp *StorageFormatResp
//			var fmtErr error
//			go func() {
//				// should signal wait group in srv to unlock if
//				// successful once format completed
//				resp, fmtErr = cs.StorageFormat(context.TODO(), &StorageFormatReq{Reformat: tc.reformat})
//				mockWg.Done()
//			}()
//
//			if !tc.superblockExists && tc.expNvmeFormatted {
//				if err := cs.harness.AwaitStorageReady(context.Background(), false); err != nil {
//					t.Fatal(err)
//				}
//			}
//			mockWg.Wait() // wait for test goroutines to complete
//
//			if fmtErr != nil {
//				t.Fatal(fmtErr)
//			}
//
//			common.AssertEqual(t, len(tc.expResp.Crets), len(resp.Crets),
//				"number of controller results")
//			common.AssertEqual(t, len(tc.expResp.Mrets), len(resp.Mrets),
//				"number of mount results")
//			for _, exp := range tc.expResp.Crets {
//				match := false
//				for _, got := range resp.Crets {
//					if diff := cmp.Diff(exp, got); diff == "" {
//						match = true
//					}
//				}
//				if !match {
//					t.Fatalf("unexpected results: (\nwant: %+v\ngot: %+v)",
//						tc.expResp.Crets, resp.Crets)
//				}
//			}
//			for _, exp := range tc.expResp.Mrets {
//				match := false
//				for _, got := range resp.Mrets {
//					if diff := cmp.Diff(exp, got); diff == "" {
//						match = true
//					}
//				}
//				if !match {
//					t.Fatalf("unexpected results: (\nwant: %+v\ngot: %+v)",
//						tc.expResp.Mrets, resp.Mrets)
//				}
//			}
//		})
//	}
//}
