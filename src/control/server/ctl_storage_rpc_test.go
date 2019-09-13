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

	"github.com/pkg/errors"
	"google.golang.org/grpc"

	. "github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	. "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/lib/spdk"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// mockStorageFormatServer provides mocking for server side streaming,
// implement send method and record sent format responses.
type mockStorageFormatServer struct {
	grpc.ServerStream
	Results []*pb.StorageFormatResp
}

func (m *mockStorageFormatServer) Send(resp *pb.StorageFormatResp) error {
	m.Results = append(m.Results, resp)
	return nil
}

// mockStorageUpdateServer provides mocking for server side streaming,
// implement send method and record sent update responses.
type mockStorageUpdateServer struct {
	grpc.ServerStream
	Results []*pb.StorageUpdateResp
}

func (m *mockStorageUpdateServer) Send(resp *pb.StorageUpdateResp) error {
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
	pbModule := MockModulePB()
	errExample := errors.New("example failure")

	tests := []struct {
		desc              string
		spdkInitEnvRet    error
		spdkDiscoverRet   error
		ipmctlDiscoverRet error
		expNvmeInited     bool
		expScmInited      bool
		expResp           pb.StorageScanResp
		errMsg            string
	}{
		{
			"success", nil, nil, nil, true, true,
			pb.StorageScanResp{
				Nvme: &pb.ScanNvmeResp{
					Ctrlrs: NvmeControllers{pbCtrlr},
					State:  new(pb.ResponseState),
				},
				Scm: &pb.ScanScmResp{
					Modules: ScmModules{pbModule},
					State:   new(pb.ResponseState),
				},
			}, "",
		},
		{
			"spdk init fail", errExample, nil, nil, false, true,
			pb.StorageScanResp{
				Nvme: &pb.ScanNvmeResp{
					State: &pb.ResponseState{
						Error: "NVMe storage scan: " + msgSpdkInitFail +
							": example failure",
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
					},
				},
				Scm: &pb.ScanScmResp{
					Modules: ScmModules{pbModule},
					State:   new(pb.ResponseState),
				},
			}, "",
		},
		{
			"spdk discover fail", nil, errExample, nil, false, true,
			pb.StorageScanResp{
				Nvme: &pb.ScanNvmeResp{
					State: &pb.ResponseState{
						Error: "NVMe storage scan: " + msgSpdkDiscoverFail +
							": example failure",
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
					},
				},
				Scm: &pb.ScanScmResp{
					Modules: ScmModules{pbModule},
					State:   new(pb.ResponseState),
				},
			}, "",
		},
		{
			"ipmctl discover fail", nil, nil, errExample, true, false,
			pb.StorageScanResp{
				Nvme: &pb.ScanNvmeResp{
					Ctrlrs: NvmeControllers{pbCtrlr},
					State:  new(pb.ResponseState),
				},
				Scm: &pb.ScanScmResp{
					State: &pb.ResponseState{
						Error: "SCM storage scan: " + msgIpmctlDiscoverFail +
							": example failure",
						Status: pb.ResponseStatus_CTRL_ERR_SCM,
					},
				},
			}, "",
		},
		{
			"all discover fail", nil, errExample, errExample, false, false,
			pb.StorageScanResp{
				Scm: &pb.ScanScmResp{
					State: &pb.ResponseState{
						Error: "SCM storage scan: " + msgIpmctlDiscoverFail +
							": example failure",
						Status: pb.ResponseStatus_CTRL_ERR_SCM,
					},
				},
				Nvme: &pb.ScanNvmeResp{
					State: &pb.ResponseState{
						Error: "NVMe storage scan: " + msgSpdkDiscoverFail +
							": example failure",
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
					},
				},
			}, "",
		},
	}

	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)()

			config := defaultMockConfig(t)
			cs := defaultMockControlService(t, log)
			cs.scm = newMockScmStorage(log, config.ext, tt.ipmctlDiscoverRet,
				[]ipmctl.DeviceDiscovery{module}, false, newMockPrepScm())
			cs.nvme = newMockNvmeStorage(
				log, config.ext,
				newMockSpdkEnv(tt.spdkInitEnvRet),
				newMockSpdkNvme(
					log,
					"1.0.0", "1.0.1",
					[]spdk.Controller{ctrlr},
					[]spdk.Namespace{MockNamespace(&ctrlr)},
					tt.spdkDiscoverRet, nil, nil),
				false)
			_ = new(pb.StorageScanResp)

			cs.Setup() // runs discovery for nvme & scm
			resp, err := cs.StorageScan(context.TODO(), &pb.StorageScanReq{})
			if err != nil {
				AssertEqual(t, err.Error(), tt.errMsg, tt.desc)
			}
			AssertEqual(t, "", tt.errMsg, tt.desc)

			AssertEqual(t, len(cs.nvme.controllers), len(resp.Nvme.Ctrlrs), "unexpected number of controllers")
			AssertEqual(t, len(cs.scm.modules), len(resp.Scm.Modules), "unexpected number of modules")

			AssertEqual(t, resp.Nvme.State, tt.expResp.Nvme.State, "unexpected Nvmestate, "+tt.desc)
			AssertEqual(t, resp.Scm.State, tt.expResp.Scm.State, "unexpected Scmstate, "+tt.desc)
			AssertEqual(t, resp.Nvme.Ctrlrs, tt.expResp.Nvme.Ctrlrs, "unexpected controllers, "+tt.desc)
			AssertEqual(t, resp.Scm.Modules, tt.expResp.Scm.Modules, "unexpected modules, "+tt.desc)

			AssertEqual(t, cs.nvme.initialized, tt.expNvmeInited, tt.desc)
			AssertEqual(t, cs.scm.initialized, tt.expScmInited, tt.desc)
		})
	}
}

func TestStoragePrepare(t *testing.T) {
	ctrlr := MockController("")
	module := MockModule()

	tests := []struct {
		desc     string
		inReq    pb.StoragePrepareReq
		outPmems []pmemDev
		expResp  pb.StoragePrepareResp
		isRoot   bool
		errMsg   string
	}{
		{
			"success",
			pb.StoragePrepareReq{
				Nvme: &pb.PrepareNvmeReq{},
				Scm:  &pb.PrepareScmReq{},
			},
			[]pmemDev{},
			pb.StoragePrepareResp{
				Nvme: &pb.PrepareNvmeResp{State: new(pb.ResponseState)},
				Scm:  &pb.PrepareScmResp{State: new(pb.ResponseState)},
			},
			true,
			"",
		},
		{
			"not run as root",
			pb.StoragePrepareReq{
				Nvme: &pb.PrepareNvmeReq{},
				Scm:  &pb.PrepareScmReq{},
			},
			[]pmemDev{},
			pb.StoragePrepareResp{
				Nvme: &pb.PrepareNvmeResp{
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error:  os.Args[0] + " must be run as root or sudo",
					},
				},
				Scm: &pb.PrepareScmResp{
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_SCM,
						Error:  os.Args[0] + " must be run as root or sudo",
					},
				},
			},
			false,
			"",
		},
		{
			"scm only",
			pb.StoragePrepareReq{
				Nvme: nil,
				Scm:  &pb.PrepareScmReq{},
			},
			[]pmemDev{},
			pb.StoragePrepareResp{
				Nvme: nil,
				Scm:  &pb.PrepareScmResp{State: new(pb.ResponseState)},
			},
			true,
			"",
		},
		{
			"nvme only",
			pb.StoragePrepareReq{
				Nvme: &pb.PrepareNvmeReq{},
				Scm:  nil,
			},
			[]pmemDev{},
			pb.StoragePrepareResp{
				Nvme: &pb.PrepareNvmeResp{State: new(pb.ResponseState)},
				Scm:  nil,
			},
			true,
			"",
		},
		{
			"success with pmem devices",
			pb.StoragePrepareReq{
				Nvme: &pb.PrepareNvmeReq{},
				Scm:  &pb.PrepareScmReq{},
			},
			[]pmemDev{MockPmemDevice()},
			pb.StoragePrepareResp{
				Nvme: &pb.PrepareNvmeResp{State: new(pb.ResponseState)},
				Scm: &pb.PrepareScmResp{
					State: new(pb.ResponseState),
					Pmems: []*pb.PmemDevice{MockPmemDevicePB()},
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
			cs.scm = newMockScmStorage(log, config.ext, nil,
				[]ipmctl.DeviceDiscovery{module}, false,
				&mockPrepScm{pmemDevs: tt.outPmems})
			cs.nvme = newMockNvmeStorage(log, config.ext, newMockSpdkEnv(nil),
				newMockSpdkNvme(log, "", "", []spdk.Controller{ctrlr},
					[]spdk.Namespace{MockNamespace(&ctrlr)},
					nil, nil, nil), false)
			_ = new(pb.StoragePrepareResp)

			cs.Setup() // runs discovery for nvme & scm
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
		mountRets        ScmMountResults
		ctrlrRets        NvmeControllerResults
		isRoot           bool
		desc             string
	}{
		{
			desc:            "ram success",
			sMount:          "/mnt/daos",
			sClass:          storage.ScmClassRAM,
			sSize:           6,
			expScmFormatted: true,
			ctrlrRets: NvmeControllerResults{
				{
					Pciaddr: "",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_CONF,
						Error:  ": " + msgBdevClassNotSupported,
					},
				},
			},
			mountRets: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State:    new(pb.ResponseState),
				},
			},
		},
		{
			desc:            "dcpm success",
			sMount:          "/mnt/daos",
			sClass:          storage.ScmClassDCPM,
			sDevs:           []string{"/dev/pmem1"},
			expScmFormatted: true,
			ctrlrRets: NvmeControllerResults{
				{
					Pciaddr: "",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_CONF,
						Error:  ": " + msgBdevClassNotSupported,
					},
				},
			},
			mountRets: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State:    new(pb.ResponseState),
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
			ctrlrRets: NvmeControllerResults{
				{
					Pciaddr: "0000:81:00.0",
					State:   new(pb.ResponseState),
				},
			},
			mountRets: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State:    new(pb.ResponseState),
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
			ctrlrRets: NvmeControllerResults{
				{
					Pciaddr: "0000:81:00.0",
					State:   new(pb.ResponseState),
				},
			},
			mountRets: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State:    new(pb.ResponseState),
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
			ctrlrRets: NvmeControllerResults{
				{
					Pciaddr: "",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_APP,
						Error:  msgBdevAlreadyFormatted,
					},
				},
			},
			mountRets: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_APP,
						Error:  msgScmAlreadyFormatted,
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

			config := newMockStorageConfig(tt.mountRet, tt.unmountRet, tt.mkdirRet,
				tt.removeRet, tt.sMount, tt.sClass, tt.sDevs, tt.sSize,
				tt.bClass, tt.bDevs, tt.superblockExists, tt.isRoot)

			cs := mockControlService(t, log, config)
			cs.Setup() // init channel used for sync

			mock := &mockStorageFormatServer{}
			mockWg := new(sync.WaitGroup)
			mockWg.Add(1)

			AssertEqual(t, cs.nvme.formatted, false, tt.desc)
			AssertEqual(t, cs.scm.formatted, false, tt.desc)

			for _, i := range cs.harness.Instances() {
				i.fsRoot = testDir
				if err := os.MkdirAll(filepath.Join(testDir, tt.sMount), 0777); err != nil {
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
				expected := tt.mountRets[i]
				AssertEqual(
					t, result.State.Error,
					expected.State.Error,
					"unexpected result error message, "+tt.desc)
				AssertEqual(
					t, result.State.Status,
					expected.State.Status,
					"unexpected response status, "+tt.desc)
				AssertEqual(
					t, result.Mntpoint,
					expected.Mntpoint,
					"unexpected mntpoint, "+tt.desc)
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
		nvmeParams *pb.UpdateNvmeReq // provided in client gRPC call
		scmParams  *pb.UpdateScmReq
		moduleRets ScmModuleResults
		ctrlrRets  NvmeControllerResults
		desc       string
	}{
		{
			desc:  "nvme update success",
			bDevs: []string{pciAddr},
			nvmeParams: &pb.UpdateNvmeReq{
				Startrev: "1.0.0",
				Model:    "ABC",
			},
			ctrlrRets: NvmeControllerResults{
				{
					Pciaddr: pciAddr,
					State:   new(pb.ResponseState),
				},
			},
			moduleRets: ScmModuleResults{
				{
					Loc: &pb.ScmModule_Location{},
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_NO_IMPL,
						Error:  msgScmUpdateNotImpl,
					},
				},
			},
		},
		{
			desc:  "nvme update wrong model",
			bDevs: []string{pciAddr},
			nvmeParams: &pb.UpdateNvmeReq{
				Startrev: "1.0.0",
				Model:    "AB",
			},
			ctrlrRets: NvmeControllerResults{
				{
					Pciaddr: pciAddr,
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error: pciAddr + ": " +
							msgBdevModelMismatch +
							" want AB, have ABC",
					},
				},
			},
			moduleRets: ScmModuleResults{
				{
					Loc: &pb.ScmModule_Location{},
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_NO_IMPL,
						Error:  msgScmUpdateNotImpl,
					},
				},
			},
		},
		{
			desc:  "nvme update wrong starting revision",
			bDevs: []string{pciAddr},
			nvmeParams: &pb.UpdateNvmeReq{
				Startrev: "2.0.0",
				Model:    "ABC",
			},
			ctrlrRets: NvmeControllerResults{
				{
					Pciaddr: pciAddr,
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_NVME,
						Error: pciAddr + ": " +
							msgBdevFwrevStartMismatch +
							" want 2.0.0, have 1.0.0",
					},
				},
			},
			moduleRets: ScmModuleResults{
				{
					Loc: &pb.ScmModule_Location{},
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_NO_IMPL,
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

			cs := mockControlService(t, log, defaultMockConfig(t))
			cs.Setup() // init channel used for sync
			mock := &mockStorageUpdateServer{}

			req := &pb.StorageUpdateReq{
				Nvme: tt.nvmeParams,
				Scm:  tt.scmParams,
			}

			if err := cs.StorageUpdate(req, mock); err != nil {
				t.Fatal(err)
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
