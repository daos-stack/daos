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
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common/proto/ctl"
	. "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/spdk"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// MockScmModule returns a mock SCM module of type storage.ScmModule.
func MockScmModule() storage.ScmModule {
	m := common.MockModulePB()

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
	m := common.MockPmemDevicePB()

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
	var (
		ctrlr      = MockController()
		pbCtrlr    = common.MockControllerPB()
		errExample = errors.New("example failure")
	)

	for name, tc := range map[string]struct {
		spdkInitEnvRet      error
		spdkDiscoverRet     error
		scmDiscoverRes      []storage.ScmModule
		scmDiscoverErr      error
		scmGetNamespacesRes []storage.ScmNamespace
		scmGetNamespacesErr error
		expSetupErr         error
		expResp             StorageScanResp
		expNvmeFailedInit   bool
	}{
		"successful scan with scm namespaces": {
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: NvmeControllers{pbCtrlr},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{
					Pmems: ScmNamespaces{common.MockPmemDevicePB()},
					State: new(ResponseState),
				},
			},
		},
		"successful scan no scm namespaces": {
			scmGetNamespacesRes: []storage.ScmNamespace{},
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: NvmeControllers{pbCtrlr},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{
					Modules: ScmModules{common.MockModulePB()},
					State:   new(ResponseState),
				},
			},
		},
		"spdk init fail": {
			spdkInitEnvRet: errExample,
			expSetupErr:    errors.New(msgBdevNotFound + ": missing [0000:81:00.0]"),
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					State: &ResponseState{
						Error: "NVMe storage scan: " + msgSpdkInitFail +
							": example failure",
						Status: ResponseStatus_CTL_ERR_NVME,
					},
				},
				Scm: &ScanScmResp{
					Pmems: ScmNamespaces{common.MockPmemDevicePB()},
					State: new(ResponseState),
				},
			},
			expNvmeFailedInit: true,
		},
		"spdk discover fail": {
			spdkDiscoverRet: errExample,
			expSetupErr:     errors.New(msgBdevNotFound + ": missing [0000:81:00.0]"),
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					State: &ResponseState{
						Error: "NVMe storage scan: " + msgSpdkDiscoverFail +
							": example failure",
						Status: ResponseStatus_CTL_ERR_NVME,
					},
				},
				Scm: &ScanScmResp{
					Pmems: ScmNamespaces{common.MockPmemDevicePB()},
					State: new(ResponseState),
				},
			},
			expNvmeFailedInit: true,
		},
		"scm module discovery fail": {
			scmDiscoverErr: errExample,
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: NvmeControllers{pbCtrlr},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{
					State: &ResponseState{
						Error:  "example failure",
						Status: ResponseStatus_CTL_ERR_SCM,
					},
				},
			},
		},
		"all discover fail": {
			scmDiscoverErr:  errExample,
			spdkDiscoverRet: errExample,
			expSetupErr:     errors.New(msgBdevNotFound + ": missing [0000:81:00.0]"),
			expResp: StorageScanResp{
				Nvme: &ScanNvmeResp{
					State: &ResponseState{
						Error: "NVMe storage scan: " + msgSpdkDiscoverFail +
							": example failure",
						Status: ResponseStatus_CTL_ERR_NVME,
					},
				},
				Scm: &ScanScmResp{
					State: &ResponseState{
						Error:  "example failure",
						Status: ResponseStatus_CTL_ERR_SCM,
					},
				},
			},
			expNvmeFailedInit: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mbc := scm.MockBackendConfig{
				DiscoverRes:     tc.scmDiscoverRes,
				DiscoverErr:     tc.scmDiscoverErr,
				GetNamespaceRes: tc.scmGetNamespacesRes,
				GetNamespaceErr: tc.scmGetNamespacesErr,
			}
			if mbc.DiscoverRes == nil {
				mbc.DiscoverRes = []storage.ScmModule{MockScmModule()}
			}
			if mbc.GetNamespaceRes == nil {
				mbc.GetNamespaceRes = []storage.ScmNamespace{MockScmNamespace()}
			}

			// test for both empty and default config cases
			for configIdx, config := range []*Configuration{defaultMockConfig(t), emptyMockConfig(t)} {
				cs := mockControlService(t, log, config, &mbc, nil)

				// overwrite default nvme storage behaviour
				cs.nvme = newMockNvmeStorage(
					log, config.ext,
					newMockSpdkEnv(tc.spdkInitEnvRet),
					newMockSpdkNvme(log, []spdk.Controller{ctrlr},
						[]spdk.Namespace{MockNamespace(&ctrlr)},
						[]spdk.DeviceHealth{MockDeviceHealth(&ctrlr)},
						tc.spdkDiscoverRet, nil),
					false)
				_ = new(StorageScanResp)

				// runs discovery for nvme & scm
				err := cs.Setup()
				if err != nil {
					common.ExpectError(t, err, tc.expSetupErr.Error(), name)
				} else {
					// if emptyMockConfig (configIdx == 1), no err raised in setup.
					if configIdx == 0 {
						common.AssertEqual(t, tc.expSetupErr, nil, name)
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

				common.AssertEqual(t, cs.nvme.initialized, !tc.expNvmeFailedInit, name)
			}
		})
	}
}

func TestStoragePrepare(t *testing.T) {
	var (
		ctrlr      = MockController()
		errExample = errors.New("example failure")
	)

	for name, tc := range map[string]struct {
		inReq               StoragePrepareReq
		prepScmNamespaceRes []storage.ScmNamespace
		prepScmErr          error
		expResp             *StoragePrepareResp
		isRoot              bool
	}{
		"success": {
			StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  &PrepareScmReq{},
			},
			[]storage.ScmNamespace{},
			nil,
			&StoragePrepareResp{
				Nvme: &PrepareNvmeResp{State: new(ResponseState)},
				Scm:  &PrepareScmResp{State: new(ResponseState)},
			},
			true,
		},
		"not run as root": {
			StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  &PrepareScmReq{},
			},
			[]storage.ScmNamespace{},
			nil,
			&StoragePrepareResp{
				Nvme: &PrepareNvmeResp{
					State: &ResponseState{
						Status: ResponseStatus_CTL_ERR_NVME,
						Error:  os.Args[0] + " must be run as root or sudo in order to prepare NVMe in this release",
					},
				},
				Scm: &PrepareScmResp{
					State: new(ResponseState),
				},
			},
			false,
		},
		"scm only": {
			StoragePrepareReq{
				Nvme: nil,
				Scm:  &PrepareScmReq{},
			},
			[]storage.ScmNamespace{},
			nil,
			&StoragePrepareResp{
				Nvme: nil,
				Scm:  &PrepareScmResp{State: new(ResponseState)},
			},
			true,
		},
		"nvme only": {
			StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  nil,
			},
			[]storage.ScmNamespace{},
			nil,
			&StoragePrepareResp{
				Nvme: &PrepareNvmeResp{State: new(ResponseState)},
				Scm:  nil,
			},
			true,
		},
		"success with pmem devices": {
			StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  &PrepareScmReq{},
			},
			[]storage.ScmNamespace{MockScmNamespace()},
			nil,
			&StoragePrepareResp{
				Nvme: &PrepareNvmeResp{State: new(ResponseState)},
				Scm: &PrepareScmResp{
					State: new(ResponseState),
					Pmems: []*PmemDevice{common.MockPmemDevicePB()},
				},
			},
			true,
		},
		"fail scm prep": {
			StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  &PrepareScmReq{},
			},
			[]storage.ScmNamespace{MockScmNamespace()},
			errExample,
			&StoragePrepareResp{
				Nvme: &PrepareNvmeResp{State: new(ResponseState)},
				Scm: &PrepareScmResp{
					State: &ResponseState{
						Status: ResponseStatus_CTL_ERR_SCM,
						Error:  errExample.Error(),
					},
				},
			},
			true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			config := newDefaultConfiguration(newMockExt(nil, true, nil,
				true, nil, nil, nil, tc.isRoot))
			mbc := scm.MockBackendConfig{
				DiscoverRes:      []storage.ScmModule{MockScmModule()},
				PrepNamespaceRes: tc.prepScmNamespaceRes,
				PrepErr:          tc.prepScmErr,
			}
			cs := mockControlService(t, log, config, &mbc, nil)
			cs.nvme = newMockNvmeStorage(log, config.ext, newMockSpdkEnv(nil),
				newMockSpdkNvme(log, []spdk.Controller{ctrlr},
					[]spdk.Namespace{MockNamespace(&ctrlr)},
					[]spdk.DeviceHealth{MockDeviceHealth(&ctrlr)},
					nil, nil), false)
			_ = new(StoragePrepareResp)

			// runs discovery for nvme & scm
			if err := cs.Setup(); err != nil {
				t.Fatal(err.Error() + name)
			}

			// StoragePrepare should never return an error
			resp, err := cs.StoragePrepare(context.TODO(), &tc.inReq)
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
			bDevs:            []string{"0000:81:00.0"},
			expNvmeFormatted: true,
			expResults: []*StorageFormatResp{
				{
					Crets: []*NvmeControllerResult{
						{
							Pciaddr: "0000:81:00.0",
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
			bDevs:            []string{"0000:81:00.0"},
			expNvmeFormatted: true,
			expResults: []*StorageFormatResp{
				{
					Crets: []*NvmeControllerResult{
						{
							Pciaddr: "0000:81:00.0",
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
			bDevs:            []string{"0000:81:00.0"},
			expResults: []*StorageFormatResp{
				{
					Crets: []*NvmeControllerResult{
						{
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
			bDevs:            []string{"0000:81:00.0"},
			expNvmeFormatted: true,
			expResults: []*StorageFormatResp{
				{
					Crets: []*NvmeControllerResult{
						{
							Pciaddr: "0000:81:00.0",
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
			bDevs:            []string{"0000:81:00.0"},
			expResults: []*StorageFormatResp{
				{
					Crets: []*NvmeControllerResult{
						{
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
			bDevs:            []string{"0000:81:00.0"},
			expNvmeFormatted: true,
			expResults: []*StorageFormatResp{
				{
					Crets: []*NvmeControllerResult{
						{
							Pciaddr: "0000:81:00.0",
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

			testDir, err := ioutil.TempDir("", strings.Replace(t.Name(), "/", "-", -1))
			defer os.RemoveAll(testDir)
			if err != nil {
				t.Fatal(err)
			}

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
			cs := mockControlService(t, log, config, nil, msc)

			// runs discovery for nvme & scm
			if err := cs.Setup(); err != nil {
				t.Fatal(err.Error() + name)
			}

			mock := &mockStorageFormatServer{}
			mockWg := new(sync.WaitGroup)
			mockWg.Add(1)

			common.AssertEqual(t, cs.nvme.formatted, false, name)

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

			common.AssertEqual(t, cs.nvme.formatted, tc.expNvmeFormatted, name)
		})
	}
}
