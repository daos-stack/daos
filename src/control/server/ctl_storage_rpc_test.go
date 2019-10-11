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

	. "github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common/proto/ctl"
	. "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/spdk"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
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

// mockStorageUpdateServer provides mocking for server side streaming,
// implement send method and record sent update responses.
type mockStorageUpdateServer struct {
	grpc.ServerStream
	Results []*StorageUpdateResp
}

func (m *mockStorageUpdateServer) Send(resp *StorageUpdateResp) error {
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
	ctrlr := MockController("")
	pbCtrlr := MockControllerPB("")
	module := MockModule()
	errExample := errors.New("example failure")

	tests := []struct {
		desc              string
		spdkInitEnvRet    error
		spdkDiscoverRet   error
		ipmctlDiscoverRet error
		expNvmeInited     bool
		expScmInited      bool
		config            *Configuration
		expResp           StorageScanResp
		setupErrMsg       string
		scanErrMsg        string
	}{
		{
			"success", nil, nil, nil, true, true,
			defaultMockConfig(t),
			StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: NvmeControllers{pbCtrlr},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{
					Pmems: PmemDevices{MockPmemDevicePB()},
					State: new(ResponseState),
				},
			}, "", "",
		},
		{
			"spdk init fail", errExample, nil, nil, false, true,
			defaultMockConfig(t),
			StorageScanResp{},
			msgBdevNotFound + ": missing [0000:81:00.0]",
			"",
		},
		{
			"spdk discover fail", nil, errExample, nil, false, true,
			defaultMockConfig(t),
			StorageScanResp{},
			msgBdevNotFound + ": missing [0000:81:00.0]",
			"",
		},
		{
			"ipmctl discover fail", nil, nil, errExample, true, false,
			defaultMockConfig(t),
			StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: NvmeControllers{pbCtrlr},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{
					State: &ResponseState{
						Error:  msgIpmctlDiscoverFail + ": example failure",
						Status: ResponseStatus_CTRL_ERR_SCM,
					},
				},
			}, "", "",
		},
		{
			"all discover fail", nil, errExample, errExample, false, false,
			defaultMockConfig(t),
			StorageScanResp{},
			msgBdevNotFound + ": missing [0000:81:00.0]",
			"",
		},
		{
			"success empty config", nil, nil, nil, true, true,
			emptyMockConfig(t),
			StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: NvmeControllers{pbCtrlr},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{
					Pmems: PmemDevices{MockPmemDevicePB()},
					State: new(ResponseState),
				},
			}, "", "",
		},
		{
			"spdk init fail empty config", errExample, nil, nil, false, true,
			emptyMockConfig(t),
			StorageScanResp{
				Nvme: &ScanNvmeResp{
					State: &ResponseState{
						Error: "NVMe storage scan: " + msgSpdkInitFail +
							": example failure",
						Status: ResponseStatus_CTRL_ERR_NVME,
					},
				},
				Scm: &ScanScmResp{
					Pmems: PmemDevices{MockPmemDevicePB()},
					State: new(ResponseState),
				},
			}, "", "",
		},
		{
			"spdk discover fail empty config", nil, errExample, nil, false, true,
			emptyMockConfig(t),
			StorageScanResp{
				Nvme: &ScanNvmeResp{
					State: &ResponseState{
						Error: "NVMe storage scan: " + msgSpdkDiscoverFail +
							": example failure",
						Status: ResponseStatus_CTRL_ERR_NVME,
					},
				},
				Scm: &ScanScmResp{
					Pmems: PmemDevices{MockPmemDevicePB()},
					State: new(ResponseState),
				},
			}, "", "",
		},
		{
			"ipmctl discover fail empty config", nil, nil, errExample, true, false,
			emptyMockConfig(t),
			StorageScanResp{
				Nvme: &ScanNvmeResp{
					Ctrlrs: NvmeControllers{pbCtrlr},
					State:  new(ResponseState),
				},
				Scm: &ScanScmResp{
					State: &ResponseState{
						Error:  msgIpmctlDiscoverFail + ": example failure",
						Status: ResponseStatus_CTRL_ERR_SCM,
					},
				},
			}, "", "",
		},
		{
			"all discover fail empty config", nil, errExample, errExample, false, false,
			emptyMockConfig(t),
			StorageScanResp{
				Nvme: &ScanNvmeResp{
					State: &ResponseState{
						Error: "NVMe storage scan: " + msgSpdkDiscoverFail +
							": example failure",
						Status: ResponseStatus_CTRL_ERR_NVME,
					},
				},
				Scm: &ScanScmResp{
					State: &ResponseState{
						Error:  msgIpmctlDiscoverFail + ": example failure",
						Status: ResponseStatus_CTRL_ERR_SCM,
					},
				},
			}, "", "",
		},
	}

	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)()

			// test for both empty and default config cases
			config := tt.config
			cs := mockControlService(t, log, config, nil)
			cs.scm = newMockScmStorage(log, config.ext, tt.ipmctlDiscoverRet,
				[]scm.Module{module}, defaultMockPrepScm(), nil)
			cs.nvme = newMockNvmeStorage(
				log, config.ext,
				newMockSpdkEnv(tt.spdkInitEnvRet),
				newMockSpdkNvme(
					log,
					"1.0.0", "1.0.1",
					[]spdk.Controller{ctrlr},
					[]spdk.Namespace{MockNamespace(&ctrlr)},
					[]spdk.DeviceHealth{MockDeviceHealth(&ctrlr)},
					tt.spdkDiscoverRet, nil, nil),
				false)
			_ = new(StorageScanResp)

			// runs discovery for nvme & scm
			err := cs.Setup()
			if err != nil {
				AssertEqual(t, err.Error(), tt.setupErrMsg, tt.desc)
				return
			}
			AssertEqual(t, "", tt.setupErrMsg, tt.desc)

			resp, err := cs.StorageScan(context.TODO(), &StorageScanReq{})
			if err != nil {
				AssertEqual(t, err.Error(), tt.scanErrMsg, tt.desc)
				return
			}
			AssertEqual(t, "", tt.scanErrMsg, tt.desc)

			if diff := cmp.Diff(tt.expResp, *resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}

			AssertEqual(t, cs.nvme.initialized, tt.expNvmeInited, tt.desc)
			AssertEqual(t, cs.scm.scanResults != nil, tt.expScmInited, tt.desc)
		})
	}
}

func TestStoragePrepare(t *testing.T) {
	ctrlr := MockController("")
	module := MockModule()

	tests := []struct {
		desc     string
		inReq    StoragePrepareReq
		outPmems []scm.Namespace
		expResp  StoragePrepareResp
		isRoot   bool
		errMsg   string
	}{
		{
			"success",
			StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  &PrepareScmReq{},
			},
			[]scm.Namespace{},
			StoragePrepareResp{
				Nvme: &PrepareNvmeResp{State: new(ResponseState)},
				Scm:  &PrepareScmResp{State: new(ResponseState)},
			},
			true,
			"",
		},
		{
			"not run as root",
			StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  &PrepareScmReq{},
			},
			[]scm.Namespace{},
			StoragePrepareResp{
				Nvme: &PrepareNvmeResp{
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error:  os.Args[0] + " must be run as root or sudo",
					},
				},
				Scm: &PrepareScmResp{
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_SCM,
						Error:  os.Args[0] + " must be run as root or sudo",
					},
				},
			},
			false,
			"",
		},
		{
			"scm only",
			StoragePrepareReq{
				Nvme: nil,
				Scm:  &PrepareScmReq{},
			},
			[]scm.Namespace{},
			StoragePrepareResp{
				Nvme: nil,
				Scm:  &PrepareScmResp{State: new(ResponseState)},
			},
			true,
			"",
		},
		{
			"nvme only",
			StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  nil,
			},
			[]scm.Namespace{},
			StoragePrepareResp{
				Nvme: &PrepareNvmeResp{State: new(ResponseState)},
				Scm:  nil,
			},
			true,
			"",
		},
		{
			"success with pmem devices",
			StoragePrepareReq{
				Nvme: &PrepareNvmeReq{},
				Scm:  &PrepareScmReq{},
			},
			[]scm.Namespace{MockPmemDevice()},
			StoragePrepareResp{
				Nvme: &PrepareNvmeResp{State: new(ResponseState)},
				Scm: &PrepareScmResp{
					State: new(ResponseState),
					Pmems: []*PmemDevice{MockPmemDevicePB()},
				},
			},
			true,
			"",
		},
	}

	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)()

			config := newDefaultConfiguration(newMockExt(nil, true, nil,
				true, nil, nil, nil, tt.isRoot))

			cs := defaultMockControlService(t, log)
			cs.scm = newMockScmStorage(log, config.ext, nil, []scm.Module{module},
				&mockPrepScm{namespaces: tt.outPmems}, nil)
			cs.nvme = newMockNvmeStorage(log, config.ext, newMockSpdkEnv(nil),
				newMockSpdkNvme(log, "", "", []spdk.Controller{ctrlr},
					[]spdk.Namespace{MockNamespace(&ctrlr)},
					[]spdk.DeviceHealth{MockDeviceHealth(&ctrlr)},
					nil, nil, nil), false)
			_ = new(StoragePrepareResp)

			// runs discovery for nvme & scm
			if err := cs.Setup(); err != nil {
				t.Fatal(err.Error() + tt.desc)
			}
			resp, err := cs.StoragePrepare(context.TODO(), &tt.inReq)
			if err != nil {
				AssertEqual(t, err.Error(), tt.errMsg, tt.desc)
			}
			AssertEqual(t, "", tt.errMsg, tt.desc)

			if resp.Nvme == nil {
				AssertEqual(t, resp.Nvme, tt.expResp.Nvme, "unexpected nvme response, "+tt.desc)
			} else {
				AssertEqual(t, resp.Nvme.State, tt.expResp.Nvme.State, "unexpected nvme state in response, "+tt.desc)
			}
			if resp.Scm == nil {
				AssertEqual(t, resp.Scm, tt.expResp.Scm, "unexpected scm response, "+tt.desc)
			} else {
				AssertEqual(t, resp.Scm.State, tt.expResp.Scm.State, "unexpected scm state in response, "+tt.desc)
				AssertEqual(t, resp.Scm.Pmems, tt.expResp.Scm.Pmems, "unexpected pmem devices in response, "+tt.desc)
			}
		})
	}
}

func TestStorageFormat(t *testing.T) {
	tests := []struct {
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
		expScmFormatted  bool
		expResults       []*StorageFormatResp
		isRoot           bool
		desc             string
	}{
		{
			desc:            "ram success",
			sMount:          "/mnt/daos",
			sClass:          storage.ScmClassRAM,
			sSize:           6,
			expScmFormatted: true,
			expResults: []*StorageFormatResp{
				{
					Mrets: []*ScmMountResult{
						{
							Mntpoint: "/mnt/daos",
							State:    new(ResponseState),
						},
					},
				},
			},
		},
		{
			desc:            "dcpm success",
			sMount:          "/mnt/daos",
			sClass:          storage.ScmClassDCPM,
			sDevs:           []string{"/dev/pmem1"},
			expScmFormatted: true,
			expResults: []*StorageFormatResp{
				{
					Mrets: []*ScmMountResult{
						{
							Mntpoint: "/mnt/daos",
							State:    new(ResponseState),
						},
					},
				},
			},
		},
		{
			desc:             "nvme and dcpm success",
			sMount:           "/mnt/daos",
			sClass:           storage.ScmClassDCPM,
			sDevs:            []string{"/dev/pmem1"},
			bClass:           storage.BdevClassNvme,
			bDevs:            []string{"0000:81:00.0"},
			expScmFormatted:  true,
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
		{
			desc:             "nvme and ram success",
			sMount:           "/mnt/daos",
			sClass:           storage.ScmClassRAM,
			sDevs:            []string{"/dev/pmem1"}, // ignored if SCM class is ram
			sSize:            6,
			bClass:           storage.BdevClassNvme,
			bDevs:            []string{"0000:81:00.0"},
			expScmFormatted:  true,
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
		{
			desc: "already formatted",
			// if superblock exists should set storage formatted
			superblockExists: true,
			sMount:           "/mnt/daos",
			sClass:           storage.ScmClassRAM,
			sSize:            6,
			bClass:           storage.BdevClassNvme,
			bDevs:            []string{"0000:81:00.0"},
			expScmFormatted:  true,
			expNvmeFormatted: true,
			expResults: []*StorageFormatResp{
				{
					Crets: []*NvmeControllerResult{
						{
							Pciaddr: "",
							State: &ResponseState{
								Status: ResponseStatus_CTRL_ERR_APP,
								Error:  msgBdevAlreadyFormatted,
							},
						},
					},
					Mrets: []*ScmMountResult{
						{
							Mntpoint: "/mnt/daos",
							State: &ResponseState{
								Status: ResponseStatus_CTRL_ERR_APP,
								Error:  msgScmAlreadyFormatted,
							},
						},
					},
				},
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)()

			testDir, err := ioutil.TempDir("", strings.Replace(t.Name(), "/", "-", -1))
			defer os.RemoveAll(testDir)
			if err != nil {
				t.Fatal(err)
			}

			// Hack to deal with creating the mountpoint in test.
			// FIXME (DAOS-3471): The tests in this layer really shouldn't be
			// reaching down far enough to actually interact with the filesystem.
			tt.sMount = filepath.Join(testDir, tt.sMount)
			if len(tt.expResults) == 1 && len(tt.expResults[0].Mrets) == 1 {
				if strings.HasSuffix(tt.sMount, tt.expResults[0].Mrets[0].Mntpoint) {
					tt.expResults[0].Mrets[0].Mntpoint = tt.sMount
				}
			}

			config := newMockStorageConfig(tt.mountRet, tt.unmountRet, tt.mkdirRet,
				tt.removeRet, tt.sMount, tt.sClass, tt.sDevs, tt.sSize,
				tt.bClass, tt.bDevs, tt.superblockExists, tt.isRoot)

			getFsRetStr := "none"
			if tt.superblockExists {
				getFsRetStr = "ext4"
			}
			msc := &scm.MockSysConfig{
				IsMountedBool: tt.superblockExists,
				MountErr:      tt.mountRet,
				UnmountErr:    tt.unmountRet,
				GetfsStr:      getFsRetStr,
			}
			cs := mockControlService(t, log, config, msc)

			// runs discovery for nvme & scm
			if err := cs.Setup(); err != nil {
				t.Fatal(err.Error() + tt.desc)
			}

			mock := &mockStorageFormatServer{}
			mockWg := new(sync.WaitGroup)
			mockWg.Add(1)

			AssertEqual(t, cs.nvme.formatted, false, tt.desc)
			AssertEqual(t, cs.scm.formatted, false, tt.desc)

			for _, i := range cs.harness.Instances() {
				root := filepath.Dir(tt.sMount)
				if tt.superblockExists {
					root = tt.sMount
				}
				if err := os.MkdirAll(root, 0777); err != nil {
					t.Fatal(err)
				}

				// if the instance is expected to have a valid superblock, create one
				if tt.superblockExists {
					if err := i.CreateSuperblock(&mgmtInfo{}); err != nil {
						t.Fatal(err)
					}
				}
			}

			go func() {
				// should signal wait group in srv to unlock if
				// successful once format completed
				_ = cs.StorageFormat(nil, mock)
				mockWg.Done()
			}()

			if !tt.superblockExists && tt.expNvmeFormatted && tt.expScmFormatted {
				if err := cs.harness.AwaitStorageReady(context.Background()); err != nil {
					t.Fatal(err)
				}
			}
			mockWg.Wait() // wait for test goroutines to complete

			if diff := cmp.Diff(tt.expResults, mock.Results); diff != "" {
				t.Fatalf("unexpected results: (-want, +got):\n%s\n", diff)
			}

			AssertEqual(t, cs.nvme.formatted, tt.expNvmeFormatted, tt.desc)
			AssertEqual(t, cs.scm.formatted, tt.expScmFormatted, tt.desc)
		})
	}
}

func TestStorageUpdate(t *testing.T) {
	pciAddr := "0000:81:00.0" // default pciaddr for tests

	tests := []struct {
		bDevs      []string
		nvmeParams *UpdateNvmeReq // provided in client gRPC call
		scmParams  *UpdateScmReq
		moduleRets ScmModuleResults
		ctrlrRets  NvmeControllerResults
		desc       string
	}{
		{
			desc:  "nvme update success",
			bDevs: []string{pciAddr},
			nvmeParams: &UpdateNvmeReq{
				Startrev: "1.0.0",
				Model:    "ABC",
			},
			ctrlrRets: NvmeControllerResults{
				{
					Pciaddr: pciAddr,
					State:   new(ResponseState),
				},
			},
			moduleRets: ScmModuleResults{
				{
					Loc: &ScmModule_Location{},
					State: &ResponseState{
						Status: ResponseStatus_CTRL_NO_IMPL,
						Error:  msgScmUpdateNotImpl,
					},
				},
			},
		},
		{
			desc:  "nvme update wrong model",
			bDevs: []string{pciAddr},
			nvmeParams: &UpdateNvmeReq{
				Startrev: "1.0.0",
				Model:    "AB",
			},
			ctrlrRets: NvmeControllerResults{
				{
					Pciaddr: pciAddr,
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error: pciAddr + ": " +
							msgBdevModelMismatch +
							" want AB, have ABC",
					},
				},
			},
			moduleRets: ScmModuleResults{
				{
					Loc: &ScmModule_Location{},
					State: &ResponseState{
						Status: ResponseStatus_CTRL_NO_IMPL,
						Error:  msgScmUpdateNotImpl,
					},
				},
			},
		},
		{
			desc:  "nvme update wrong starting revision",
			bDevs: []string{pciAddr},
			nvmeParams: &UpdateNvmeReq{
				Startrev: "2.0.0",
				Model:    "ABC",
			},
			ctrlrRets: NvmeControllerResults{
				{
					Pciaddr: pciAddr,
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_NVME,
						Error: pciAddr + ": " +
							msgBdevFwrevStartMismatch +
							" want 2.0.0, have 1.0.0",
					},
				},
			},
			moduleRets: ScmModuleResults{
				{
					Loc: &ScmModule_Location{},
					State: &ResponseState{
						Status: ResponseStatus_CTRL_NO_IMPL,
						Error:  msgScmUpdateNotImpl,
					},
				},
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)()

			config := defaultMockConfig(t)
			cs := mockControlService(t, log, config, nil)

			// runs discovery for nvme & scm
			if err := cs.Setup(); err != nil {
				t.Fatal(err)
			}

			mock := &mockStorageUpdateServer{}

			req := &StorageUpdateReq{
				Nvme: tt.nvmeParams,
				Scm:  tt.scmParams,
			}

			if err := cs.StorageUpdate(req, mock); err != nil {
				t.Fatal(err.Error() + tt.desc)
			}

			AssertEqual(
				t, len(mock.Results), 1,
				"unexpected number of responses sent, "+tt.desc)

			for i, result := range mock.Results[0].Crets {
				expected := tt.ctrlrRets[i]
				AssertEqual(
					t, result.State.Error,
					expected.State.Error,
					"unexpected result error message, "+tt.desc)
				AssertEqual(
					t, result.State.Status,
					expected.State.Status,
					"unexpected response status, "+tt.desc)
				AssertEqual(
					t, result.Pciaddr,
					expected.Pciaddr,
					"unexpected pciaddr, "+tt.desc)
			}

			for i, result := range mock.Results[0].Mrets {
				expected := tt.moduleRets[i]
				AssertEqual(
					t, result.State.Error,
					expected.State.Error,
					"unexpected result error message, "+tt.desc)
				AssertEqual(
					t, result.State.Status,
					expected.State.Status,
					"unexpected response status, "+tt.desc)
				AssertEqual(
					t, result.Loc,
					expected.Loc,
					"unexpected mntpoint, "+tt.desc)
			}
		})
	}
}
