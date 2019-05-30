//
// (C) Copyright 2018-2019 Intel Corporation.
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

package main

import (
	"sync"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	ipmctl "github.com/daos-stack/go-ipmctl/ipmctl"
	spdk "github.com/daos-stack/go-spdk/spdk"
	"github.com/pkg/errors"
	"google.golang.org/grpc"
)

// mockFormatStorageServer provides mocking for server side streaming,
// implement send method and record sent format responses.
type mockFormatStorageServer struct {
	grpc.ServerStream
	Results []*pb.FormatStorageResp
}

func (m *mockFormatStorageServer) Send(resp *pb.FormatStorageResp) error {
	m.Results = append(m.Results, resp)
	return nil
}

// mockUpdateStorageServer provides mocking for server side streaming,
// implement send method and record sent update responses.
type mockUpdateStorageServer struct {
	grpc.ServerStream
	Results []*pb.UpdateStorageResp
}

func (m *mockUpdateStorageServer) Send(resp *pb.UpdateStorageResp) error {
	m.Results = append(m.Results, resp)
	return nil
}

// return config reference with customised storage config behaviour and params
func newMockStorageConfig(
	mountRet error, unmountRet error, mkdirRet error, removeRet error,
	scmMount string, scmClass ScmClass, scmDevs []string, scmSize int,
	bdevClass BdevClass, bdevDevs []string, existsRet bool) *configuration {

	c := newDefaultConfiguration(
		newMockExt(
			nil, existsRet, mountRet, unmountRet, mkdirRet,
			removeRet))
	c.Servers = append(c.Servers, newDefaultServer())
	c.Servers[0].ScmMount = scmMount
	c.Servers[0].ScmClass = scmClass
	c.Servers[0].ScmList = scmDevs
	c.Servers[0].ScmSize = scmSize

	c.Servers[0].BdevClass = bdevClass
	c.Servers[0].BdevList = bdevDevs

	return &c
}

func TestScanStorage(t *testing.T) {
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
		expResp           pb.ScanStorageResp
		errMsg            string
	}{
		{
			"success", nil, nil, nil, true, true,
			pb.ScanStorageResp{
				Ctrlrs:    []*pb.NvmeController{pbCtrlr},
				Nvmestate: new(pb.ResponseState),
				Modules:   []*pb.ScmModule{pbModule},
				Scmstate:  new(pb.ResponseState),
			}, "",
		},
		{
			"spdk init fail", errExample, nil, nil, false, true,
			pb.ScanStorageResp{
				Nvmestate: &pb.ResponseState{
					Error: msgSpdkInitFail +
						": example failure",
					Status: pb.ResponseStatus_CTRL_ERR_NVME,
				},
				Modules:  []*pb.ScmModule{pbModule},
				Scmstate: new(pb.ResponseState),
			}, "",
		},
		{
			"spdk discover fail", nil, errExample, nil, false, true,
			pb.ScanStorageResp{
				Nvmestate: &pb.ResponseState{
					Error: msgSpdkDiscoverFail +
						": example failure",
					Status: pb.ResponseStatus_CTRL_ERR_NVME,
				},
				Modules:  []*pb.ScmModule{pbModule},
				Scmstate: new(pb.ResponseState),
			}, "",
		},
		{
			"ipmctl discover fail", nil, nil, errExample, true, false,
			pb.ScanStorageResp{
				Ctrlrs:    []*pb.NvmeController{pbCtrlr},
				Nvmestate: new(pb.ResponseState),
				Scmstate: &pb.ResponseState{
					Error: msgIpmctlDiscoverFail +
						": example failure",
					Status: pb.ResponseStatus_CTRL_ERR_SCM,
				},
			}, "",
		},
		{
			"all discover fail", nil, errExample, errExample, false, false,
			pb.ScanStorageResp{
				Nvmestate: &pb.ResponseState{
					Error: msgSpdkDiscoverFail +
						": example failure",
					Status: pb.ResponseStatus_CTRL_ERR_NVME,
				},
				Scmstate: &pb.ResponseState{
					Error: msgIpmctlDiscoverFail +
						": example failure",
					Status: pb.ResponseStatus_CTRL_ERR_SCM,
				},
			}, "",
		},
	}

	for _, tt := range tests {
		cs := defaultMockControlService(t)
		cs.scm = newMockScmStorage(
			tt.ipmctlDiscoverRet,
			[]ipmctl.DeviceDiscovery{module},
			false, cs.config)
		cs.nvme = newMockNvmeStorage(
			newMockSpdkEnv(tt.spdkInitEnvRet),
			newMockSpdkNvme(
				"1.0.0", "1.0.1",
				[]spdk.Controller{ctrlr},
				[]spdk.Namespace{MockNamespace(&ctrlr)},
				tt.spdkDiscoverRet, nil, nil),
			false, cs.config)
		resp := new(pb.ScanStorageResp)

		cs.Setup() // runs discovery for nvme & scm
		resp, err := cs.ScanStorage(nil, &pb.ScanStorageReq{})
		if err != nil {
			AssertEqual(t, err.Error(), tt.errMsg, tt.desc)
		}
		AssertEqual(t, "", tt.errMsg, tt.desc)

		AssertEqual(
			t, len(cs.nvme.controllers), len(resp.Ctrlrs),
			"unexpected number of controllers")
		AssertEqual(
			t, len(cs.scm.modules), len(resp.Modules),
			"unexpected number of modules")

		AssertEqual(
			t, resp.Nvmestate, tt.expResp.Nvmestate,
			"unexpected Nvmestate, "+tt.desc)
		AssertEqual(
			t, resp.Nvmestate, tt.expResp.Nvmestate,
			"unexpected Nvmestate, "+tt.desc)
		AssertEqual(
			t, resp.Scmstate, tt.expResp.Scmstate,
			"unexpected Scmstate, "+tt.desc)
		AssertEqual(
			t, resp.Ctrlrs, tt.expResp.Ctrlrs,
			"unexpected controllers, "+tt.desc)
		AssertEqual(
			t, resp.Modules, tt.expResp.Modules,
			"unexpected modules, "+tt.desc)

		AssertEqual(t, cs.nvme.initialized, tt.expNvmeInited, tt.desc)
		AssertEqual(t, cs.scm.initialized, tt.expScmInited, tt.desc)
	}
}

func TestFormatStorage(t *testing.T) {
	tests := []struct {
		superblockExists bool
		mountRet         error
		unmountRet       error
		mkdirRet         error
		removeRet        error
		sMount           string
		sClass           ScmClass
		sDevs            []string
		sSize            int
		bClass           BdevClass
		bDevs            []string
		expNvmeFormatted bool
		expScmFormatted  bool
		mountRets        []*pb.ScmMountResult
		ctrlrRets        []*pb.NvmeControllerResult
		desc             string
		errMsg           string
	}{
		{
			desc:            "ram success",
			sMount:          "/mnt/daos",
			sClass:          scmRAM,
			sSize:           6,
			expScmFormatted: true,
			ctrlrRets: []*pb.NvmeControllerResult{
				{
					Pciaddr: "",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_CONF,
						Error:  ": " + msgBdevClassNotSupported,
					},
				},
			},
			mountRets: []*pb.ScmMountResult{
				{
					Mntpoint: "/mnt/daos",
					State:    new(pb.ResponseState),
				},
			},
		},
		{
			desc:            "dcpm success",
			sMount:          "/mnt/daos",
			sClass:          scmDCPM,
			sDevs:           []string{"/dev/pmem1"},
			expScmFormatted: true,
			ctrlrRets: []*pb.NvmeControllerResult{
				{
					Pciaddr: "",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_CONF,
						Error:  ": " + msgBdevClassNotSupported,
					},
				},
			},
			mountRets: []*pb.ScmMountResult{
				{
					Mntpoint: "/mnt/daos",
					State:    new(pb.ResponseState),
				},
			},
		},
		{
			desc:             "nvme and dcpm success",
			sMount:           "/mnt/daos",
			sClass:           scmDCPM,
			sDevs:            []string{"/dev/pmem1"},
			bClass:           bdNVMe,
			bDevs:            []string{"0000:81:00.0"},
			expScmFormatted:  true,
			expNvmeFormatted: true,
			ctrlrRets: []*pb.NvmeControllerResult{
				{
					Pciaddr: "0000:81:00.0",
					State:   new(pb.ResponseState),
				},
			},
			mountRets: []*pb.ScmMountResult{
				{
					Mntpoint: "/mnt/daos",
					State:    new(pb.ResponseState),
				},
			},
		},
		{
			desc:             "nvme and ram success",
			sMount:           "/mnt/daos",
			sClass:           scmRAM,
			sDevs:            []string{"/dev/pmem1"}, // ignored if SCM class is ram
			sSize:            6,
			bClass:           bdNVMe,
			bDevs:            []string{"0000:81:00.0"},
			expScmFormatted:  true,
			expNvmeFormatted: true,
			ctrlrRets: []*pb.NvmeControllerResult{
				{
					Pciaddr: "0000:81:00.0",
					State:   new(pb.ResponseState),
				},
			},
			mountRets: []*pb.ScmMountResult{
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
			sClass:           scmRAM,
			sSize:            6,
			bClass:           bdNVMe,
			bDevs:            []string{"0000:81:00.0"},
			expScmFormatted:  true,
			expNvmeFormatted: true,
			ctrlrRets: []*pb.NvmeControllerResult{
				{
					Pciaddr: "",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_APP,
						Error:  msgBdevAlreadyFormatted,
					},
				},
			},
			mountRets: []*pb.ScmMountResult{
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

	srvIdx := 0

	for _, tt := range tests {
		config := newMockStorageConfig(
			tt.mountRet, tt.unmountRet, tt.mkdirRet, tt.removeRet,
			tt.sMount, tt.sClass, tt.sDevs, tt.sSize,
			tt.bClass, tt.bDevs, tt.superblockExists)

		cs := mockControlService(config)
		cs.Setup() // init channel used for sync

		mock := &mockFormatStorageServer{}
		mockWg := new(sync.WaitGroup)
		mockWg.Add(1)

		AssertEqual(t, cs.nvme.formatted, false, tt.desc)
		AssertEqual(t, cs.scm.formatted, false, tt.desc)

		go func() {
			// should signal wait group in srv to unlock if
			// successful once format completed
			cs.FormatStorage(nil, mock)
			mockWg.Done()
		}()

		if !tt.superblockExists && tt.expNvmeFormatted && tt.expScmFormatted {
			// conditions met for storage format to succeed, wait
			<-cs.config.Servers[srvIdx].formatted
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
	}
}

func TestUpdateStorage(t *testing.T) {
	pciAddr := "0000:81:00.0" // default pciaddr for tests

	tests := []struct {
		updateRet  error
		bDevs      []string
		nvmeParams *pb.UpdateNvmeReq // provided in client gRPC call
		scmParams  *pb.UpdateScmReq
		moduleRets []*pb.ScmModuleResult
		ctrlrRets  []*pb.NvmeControllerResult
		desc       string
		errMsg     string
	}{
		{
			desc:  "nvme update success",
			bDevs: []string{pciAddr},
			nvmeParams: &pb.UpdateNvmeReq{
				Startrev: "1.0.0",
				Model:    "ABC",
			},
			ctrlrRets: []*pb.NvmeControllerResult{
				{
					Pciaddr: pciAddr,
					State:   new(pb.ResponseState),
				},
			},
			moduleRets: []*pb.ScmModuleResult{
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
			ctrlrRets: []*pb.NvmeControllerResult{
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
			moduleRets: []*pb.ScmModuleResult{
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
			ctrlrRets: []*pb.NvmeControllerResult{
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
			moduleRets: []*pb.ScmModuleResult{
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
		config := defaultMockConfig(t)
		cs := mockControlService(&config)
		cs.Setup() // init channel used for sync
		mock := &mockUpdateStorageServer{}

		req := &pb.UpdateStorageReq{
			Nvme: tt.nvmeParams,
			Scm:  tt.scmParams,
		}

		cs.UpdateStorage(req, mock)

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
	}
}
