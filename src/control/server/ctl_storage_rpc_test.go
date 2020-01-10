//
// (C) Copyright 2019 Intel Corporation.
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
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto"
	. "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// MockScmModule returns a mock SCM module of type storage.ScmModule.
func MockScmModule() storage.ScmModule {
	m := proto.MockScmModule()

	return storage.ScmModule{
		PhysicalID:      uint32(m.Physicalid),
		ChannelID:       uint32(m.Loc.Channel),
		ChannelPosition: uint32(m.Loc.Channelpos),
		ControllerID:    uint32(m.Loc.Memctrlr),
		SocketID:        uint32(m.Loc.Socket),
		Capacity:        m.Capacity,
	}
}

// MockScmNamespace returns a mock SCM namespace (PMEM device file),
// which would normally be parsed from the output of ndctl cmdline tool.
func MockScmNamespace() storage.ScmNamespace {
	m := proto.MockPmemDevice()

	return storage.ScmNamespace{
		UUID:        m.Uuid,
		BlockDevice: m.Blockdev,
		Name:        m.Dev,
		NumaNode:    m.Numanode,
		Size:        m.Size,
	}
}

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

	c := newDefaultConfiguration(newMockExt(nil, existsRet, mountRet,
		true, unmountRet, mkdirRet, removeRet, isRoot))

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
				DiscoverRes:     storage.ScmModules{MockScmModule()},
				GetNamespaceRes: storage.ScmNamespaces{MockScmNamespace()},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: proto.NvmeControllers{proto.MockNvmeController()},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{
					Pmems: proto.ScmNamespaces{proto.MockPmemDevice()},
					State: new(ResponseState),
				},
			},
		},
		"successful scan no scm namespaces": {
			bmbc: &bdev.MockBackendConfig{
				ScanRes: storage.NvmeControllers{storage.MockNvmeController()},
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{MockScmModule()},
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
				DiscoverRes:     storage.ScmModules{MockScmModule()},
				GetNamespaceRes: storage.ScmNamespaces{MockScmNamespace()},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					State: &ResponseState{
						Error:  "spdk init failed",
						Status: ResponseStatus_CTL_ERR_NVME,
					},
				},
				Scm: &ScanScmResp{
					Pmems: proto.ScmNamespaces{proto.MockPmemDevice()},
					State: new(ResponseState),
				},
			},
		},
		"spdk scan failure": {
			bmbc: &bdev.MockBackendConfig{
				ScanErr: errors.New("spdk scan failed"),
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:     storage.ScmModules{MockScmModule()},
				GetNamespaceRes: storage.ScmNamespaces{MockScmNamespace()},
			},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					State: &ResponseState{
						Error:  "spdk scan failed",
						Status: ResponseStatus_CTL_ERR_NVME,
					},
				},
				Scm: &ScanScmResp{
					Pmems: proto.ScmNamespaces{proto.MockPmemDevice()},
					State: new(ResponseState),
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

				if diff := cmp.Diff(tc.expResp, *resp); diff != "" {
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
				DiscoverRes: storage.ScmModules{MockScmModule()},
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
				DiscoverRes: storage.ScmModules{MockScmModule()},
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
				DiscoverRes:      storage.ScmModules{MockScmModule()},
				PrepNamespaceRes: storage.ScmNamespaces{MockScmNamespace()},
			},
			req: StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  &PrepareScmReq{},
			},
			expResp: &StoragePrepareResp{
				Nvme: &PrepareNvmeResp{State: new(ResponseState)},
				Scm: &PrepareScmResp{
					State: new(ResponseState),
					Pmems: []*PmemDevice{proto.MockPmemDevice()},
				},
			},
		},
		"fail scm prep": {
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{MockScmModule()},
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
				DiscoverRes: storage.ScmModules{MockScmModule()},
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

func TestStorageFormat(t *testing.T) {
	var (
		mockNvmeController = storage.MockNvmeController()
	)

	for name, tc := range map[string]struct {
		superblockExists bool
		mountRet         error
		unmountRet       error
		mkdirRet         error
		removeRet        error
		sMount           string
		sClass           storage.ScmClass
		sDevs            []string
		sSize            int
		bClass           storage.BdevClass
		bDevs            []string
		expNvmeFormatted bool
		bmbc             *bdev.MockBackendConfig
		expResults       []*StorageFormatResp
		isRoot           bool
		reformat         bool
	}{
		"ram success": {
			sMount:           "/mnt/daos",
			sClass:           storage.ScmClassRAM,
			sSize:            6,
			expNvmeFormatted: true,
			expResults: []*StorageFormatResp{
				{
					Crets: []*NvmeControllerResult{},
					Mrets: []*ScmMountResult{
						{
							Mntpoint: "/mnt/daos",
							State:    new(ResponseState),
						},
					},
				},
			},
		},
		"dcpm success": {
			sMount:           "/mnt/daos",
			sClass:           storage.ScmClassDCPM,
			sDevs:            []string{"/dev/pmem1"},
			expNvmeFormatted: true,
			expResults: []*StorageFormatResp{
				{
					Crets: []*NvmeControllerResult{},
					Mrets: []*ScmMountResult{
						{
							Mntpoint: "/mnt/daos",
							State:    new(ResponseState),
						},
					},
				},
			},
		},
		"nvme and dcpm success": {
			sMount:           "/mnt/daos",
			sClass:           storage.ScmClassDCPM,
			sDevs:            []string{"/dev/pmem1"},
			bClass:           storage.BdevClassNvme,
			bDevs:            []string{mockNvmeController.PciAddr},
			expNvmeFormatted: true,
			bmbc: &bdev.MockBackendConfig{
				ScanRes: storage.NvmeControllers{mockNvmeController},
			},
			expResults: []*StorageFormatResp{
				{
					Crets: []*NvmeControllerResult{
						{
							Pciaddr: mockNvmeController.PciAddr,
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
		},
		"nvme and ram success": {
			sMount:           "/mnt/daos",
			sClass:           storage.ScmClassRAM,
			sDevs:            []string{"/dev/pmem1"}, // ignored if SCM class is ram
			sSize:            6,
			bClass:           storage.BdevClassNvme,
			bDevs:            []string{mockNvmeController.PciAddr},
			expNvmeFormatted: true,
			bmbc: &bdev.MockBackendConfig{
				ScanRes: storage.NvmeControllers{mockNvmeController},
			},
			expResults: []*StorageFormatResp{
				{
					Crets: []*NvmeControllerResult{
						{
							Pciaddr: mockNvmeController.PciAddr,
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
		},
		"ram already mounted no reformat": {
			superblockExists: true, // if superblockExists we emulate ext4 fs is mounted
			sMount:           "/mnt/daos",
			sClass:           storage.ScmClassRAM,
			sSize:            6,
			bClass:           storage.BdevClassNvme,
			bDevs:            []string{mockNvmeController.PciAddr},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: storage.NvmeControllers{mockNvmeController},
			},
			expResults: []*StorageFormatResp{
				{
					Crets: []*NvmeControllerResult{
						{
							Pciaddr: "<nil>",
							State: &ResponseState{
								Status: ResponseStatus_CTL_ERR_NVME,
								Error:  msgBdevScmNotReady,
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
		},
		"ram already mounted and reformat set": {
			superblockExists: true, // if superblockExists we emulate ext4 fs is mounted
			reformat:         true,
			sMount:           "/mnt/daos",
			sClass:           storage.ScmClassRAM,
			sSize:            6,
			bClass:           storage.BdevClassNvme,
			bDevs:            []string{mockNvmeController.PciAddr},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: storage.NvmeControllers{mockNvmeController},
			},
			expNvmeFormatted: true,
			expResults: []*StorageFormatResp{
				{
					Crets: []*NvmeControllerResult{
						{
							Pciaddr: mockNvmeController.PciAddr,
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
		},
		"dcpm already mounted no reformat": {
			superblockExists: true, // if superblockExists we emulate ext4 fs is mounted
			sMount:           "/mnt/daos",
			sClass:           storage.ScmClassDCPM,
			sDevs:            []string{"/dev/pmem1"},
			bClass:           storage.BdevClassNvme,
			bDevs:            []string{mockNvmeController.PciAddr},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: storage.NvmeControllers{mockNvmeController},
			},
			expResults: []*StorageFormatResp{
				{
					Crets: []*NvmeControllerResult{
						{
							Pciaddr: "<nil>",
							State: &ResponseState{
								Status: ResponseStatus_CTL_ERR_NVME,
								Error:  msgBdevScmNotReady,
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
		},
		"dcpm already mounted and reformat set": {
			superblockExists: true, // if superblockExists we emulate ext4 fs is mounted
			reformat:         true,
			sMount:           "/mnt/daos",
			sClass:           storage.ScmClassDCPM,
			sDevs:            []string{"/dev/pmem1"},
			bClass:           storage.BdevClassNvme,
			bDevs:            []string{mockNvmeController.PciAddr},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: storage.NvmeControllers{mockNvmeController},
			},
			expNvmeFormatted: true,
			expResults: []*StorageFormatResp{
				{
					Crets: []*NvmeControllerResult{
						{
							Pciaddr: mockNvmeController.PciAddr,
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
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			testDir, cleanup := common.CreateTestDir(t)
			defer cleanup()

			// Hack to deal with creating the mountpoint in test.
			// FIXME (DAOS-3471): The tests in this layer really shouldn't be
			// reaching down far enough to actually interact with the filesystem.
			tc.sMount = filepath.Join(testDir, tc.sMount)
			if len(tc.expResults) == 1 && len(tc.expResults[0].Mrets) == 1 {
				mp := &(tc.expResults[0].Mrets[0].Mntpoint)
				if *mp != "" {
					if strings.HasSuffix(tc.sMount, *mp) {
						*mp = tc.sMount
					}
				}
			}

			config := newMockStorageConfig(tc.mountRet, tc.unmountRet, tc.mkdirRet,
				tc.removeRet, tc.sMount, tc.sClass, tc.sDevs, tc.sSize,
				tc.bClass, tc.bDevs, tc.superblockExists, tc.isRoot)

			getFsRetStr := "none"
			if tc.superblockExists {
				getFsRetStr = "ext4"
			}
			msc := &scm.MockSysConfig{
				IsMountedBool: tc.superblockExists,
				MountErr:      tc.mountRet,
				UnmountErr:    tc.unmountRet,
				GetfsStr:      getFsRetStr,
			}
			cs := mockControlService(t, log, config, tc.bmbc, nil, msc)

			// runs discovery for nvme & scm
			if err := cs.Setup(); err != nil {
				t.Fatal(err.Error() + name)
			}

			mock := &mockStorageFormatServer{}
			mockWg := new(sync.WaitGroup)
			mockWg.Add(1)

			for _, i := range cs.harness.Instances() {
				root := filepath.Dir(tc.sMount)
				if tc.superblockExists {
					root = tc.sMount
				}
				if err := os.MkdirAll(root, 0777); err != nil {
					t.Fatal(err)
				}

				// if the instance is expected to have a valid superblock, create one
				if tc.superblockExists {
					if err := i.CreateSuperblock(&mgmtInfo{}); err != nil {
						t.Fatal(err)
					}
				}
			}

			go func() {
				// should signal wait group in srv to unlock if
				// successful once format completed
				_ = cs.StorageFormat(&StorageFormatReq{Reformat: tc.reformat}, mock)
				mockWg.Done()
			}()

			if !tc.superblockExists && tc.expNvmeFormatted {
				if err := cs.harness.AwaitStorageReady(context.Background(), false); err != nil {
					t.Fatal(err)
				}
			}
			mockWg.Wait() // wait for test goroutines to complete

			if diff := cmp.Diff(tc.expResults, mock.Results); diff != "" {
				t.Fatalf("unexpected results: (-want, +got):\n%s\n", diff)
			}
		})
	}
}
