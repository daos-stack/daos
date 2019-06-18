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
	"fmt"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	. "github.com/daos-stack/go-ipmctl/ipmctl"
	"github.com/pkg/errors"
)

// MockModule returns a mock SCM module of type exported from go-ipmctl.
func MockModule() DeviceDiscovery {
	m := MockModulePB()
	dd := DeviceDiscovery{}
	dd.Physical_id = uint16(m.Physicalid)
	dd.Channel_id = uint16(m.Loc.Channel)
	dd.Channel_pos = uint16(m.Loc.Channelpos)
	dd.Memory_controller_id = uint16(m.Loc.Memctrlr)
	dd.Socket_id = uint16(m.Loc.Socket)
	dd.Capacity = m.Capacity

	return dd
}

type mockIpmctl struct {
	discoverModulesRet error
	modules            []DeviceDiscovery
}

func (m *mockIpmctl) Discover() ([]DeviceDiscovery, error) {
	return m.modules, m.discoverModulesRet
}

// mockScmStorage factory
func newMockScmStorage(
	discoverModulesRet error, mms []DeviceDiscovery, inited bool,
	c *configuration) *scmStorage {

	return &scmStorage{
		ipmctl:      &mockIpmctl{discoverModulesRet, mms},
		initialized: inited,
		config:      c,
	}
}

func defaultMockScmStorage(config *configuration) *scmStorage {
	m := MockModule()

	return newMockScmStorage(
		nil, []DeviceDiscovery{m}, false, config)
}

func TestDiscoverScm(t *testing.T) {
	mPB := MockModulePB()
	m := MockModule()
	config := defaultMockConfig(t)

	tests := []struct {
		inited            bool
		ipmctlDiscoverRet error
		errMsg            string
		expModules        []*pb.ScmModule
	}{
		{
			true,
			nil,
			"",
			[]*pb.ScmModule(nil),
		},
		{
			false,
			nil,
			"",
			[]*pb.ScmModule{mPB},
		},
		{
			false,
			errors.New("ipmctl example failure"),
			msgIpmctlDiscoverFail + ": ipmctl example failure",
			[]*pb.ScmModule{mPB},
		},
	}

	for _, tt := range tests {
		ss := newMockScmStorage(
			tt.ipmctlDiscoverRet, []DeviceDiscovery{m}, tt.inited,
			&config)

		resp := new(pb.ScanStorageResp)
		ss.Discover(resp)
		if tt.errMsg != "" {
			AssertEqual(t, resp.Scmstate.Error, tt.errMsg, "")
			AssertTrue(
				t,
				resp.Scmstate.Status != pb.ResponseStatus_CTRL_SUCCESS,
				"")
			continue
		}
		AssertEqual(t, resp.Scmstate.Error, "", "")
		AssertEqual(t, resp.Scmstate.Status, pb.ResponseStatus_CTRL_SUCCESS, "")

		AssertEqual(t, ss.modules, tt.expModules, "unexpected list of modules")
	}
}

func TestFormatScm(t *testing.T) {
	tests := []struct {
		inited    bool
		formatted bool
		mountRet  error
		// log context should be stack layer registering result
		unmountRet error
		mkdirRet   error
		removeRet  error
		mount      string
		class      ScmClass
		devs       []string
		size       int
		expCmds    []string // expected arguments in syscall methods
		expResults []*pb.ScmMountResult
		desc       string
	}{
		{
			inited: false,
			mount:  "/mnt/daos",
			expResults: []*pb.ScmMountResult{
				{
					Mntpoint: "/mnt/daos",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_APP,
						Error:  msgScmNotInited,
					},
				},
			},
			desc: "not initialised",
		},
		{
			inited:    true,
			mount:     "/mnt/daos",
			formatted: true,
			expResults: []*pb.ScmMountResult{
				{
					Mntpoint: "/mnt/daos",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_APP,
						Error:  msgScmAlreadyFormatted,
					},
				},
			},
			desc: "already formatted",
		},
		{
			inited: true,
			expResults: []*pb.ScmMountResult{
				{
					Mntpoint: "",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_CONF,
						Error:  msgScmMountEmpty,
					},
				},
			},
			desc: "missing mount point",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			expResults: []*pb.ScmMountResult{
				{
					Mntpoint: "/mnt/daos",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_CONF,
						Error:  ": " + msgScmClassNotSupported,
					},
				},
			},
			desc: "no class",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			class:  scmRAM,
			size:   6,
			expResults: []*pb.ScmMountResult{
				{
					Mntpoint: "/mnt/daos",
					State:    &pb.ResponseState{},
				},
			},
			expCmds: []string{
				"syscall: calling unmount with /mnt/daos, MNT_DETACH",
				"os: removeall /mnt/daos",
				"os: mkdirall /mnt/daos, 0777",
				// 33806 is the combination of the following
				// syscall flags: MS_NOATIME|MS_SILENT|MS_NODEV
				// |MS_NOEXEC|MS_NOSUID
				"syscall: mount tmpfs, /mnt/daos, tmpfs, 33806, size=6g",
			},
			desc: "ram success",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			class:  scmDCPM,
			devs:   []string{"/dev/pmem0"},
			expResults: []*pb.ScmMountResult{
				{
					Mntpoint: "/mnt/daos",
					State:    &pb.ResponseState{},
				},
			},
			expCmds: []string{
				"syscall: calling unmount with /mnt/daos, MNT_DETACH",
				"os: removeall /mnt/daos",
				"cmd: wipefs -a /dev/pmem0",
				"cmd: mkfs.ext4 /dev/pmem0",
				"os: mkdirall /mnt/daos, 0777",
				// 33806 is the combination of the following
				// syscall flags: MS_NOATIME|MS_SILENT|MS_NODEV
				// |MS_NOEXEC|MS_NOSUID
				"syscall: mount /dev/pmem0, /mnt/daos, ext4, 33806, dax",
			},
			desc: "dcpm success",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			class:  scmDCPM,
			devs:   []string{},
			expResults: []*pb.ScmMountResult{
				{
					Mntpoint: "/mnt/daos",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_CONF,
						Error:  msgScmBadDevList,
					},
				},
			},
			desc: "dcpm missing dev",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			class:  scmDCPM,
			devs:   []string(nil),
			expResults: []*pb.ScmMountResult{
				{
					Mntpoint: "/mnt/daos",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_CONF,
						Error:  msgScmBadDevList,
					},
				},
			},
			desc: "dcpm nil devs",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			class:  scmDCPM,
			devs:   []string{""},
			expResults: []*pb.ScmMountResult{
				{
					Mntpoint: "/mnt/daos",
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_ERR_CONF,
						Error:  msgScmDevEmpty,
					},
				},
			},
			desc: "dcpm empty dev",
		},
	}

	srvIdx := 0

	for _, tt := range tests {
		config := newMockStorageConfig(
			tt.mountRet, tt.unmountRet, tt.mkdirRet, tt.removeRet,
			tt.mount, tt.class, tt.devs, tt.size,
			bdNVMe, []string{}, false)
		ss := newMockScmStorage(
			nil, []DeviceDiscovery{}, false, config)
		ss.formatted = tt.formatted

		results := []*pb.ScmMountResult{}

		if tt.inited {
			// not concerned with response
			ss.Discover(new(pb.ScanStorageResp))
		}

		ss.Format(srvIdx, &results)

		// only ocm result in response for the moment
		AssertEqual(
			t, len(results), 1,
			"unexpected number of response results, "+tt.desc)

		result := results[0]

		AssertEqual(
			t, result.State.Error, tt.expResults[0].State.Error,
			"unexpected result error message, "+tt.desc)
		AssertEqual(
			t, result.State.Status, tt.expResults[0].State.Status,
			"unexpected response status, "+tt.desc)
		AssertEqual(
			t, result.Mntpoint, tt.expResults[0].Mntpoint,
			"unexpected mntpoint, "+tt.desc)

		if result.State.Status == pb.ResponseStatus_CTRL_SUCCESS {
			AssertEqual(
				t, ss.formatted,
				true, "expect formatted state, "+tt.desc)
		}

		cmds := ss.config.ext.getHistory()
		AssertEqual(
			t, len(cmds), len(tt.expCmds), "number of cmds, "+tt.desc)
		for i, s := range cmds {
			AssertEqual(
				t, s, tt.expCmds[i],
				fmt.Sprintf("commands don't match (%s)", tt.desc))
		}
	}
}

// TestUpdateScm currently just verifies that response is populated with not
// implemented state in result.
func TestUpdateScm(t *testing.T) {
	tests := []struct {
		expResults []*pb.ScmModuleResult
		desc       string
	}{
		{
			expResults: []*pb.ScmModuleResult{
				{
					Loc: &pb.ScmModule_Location{},
					State: &pb.ResponseState{
						Status: pb.ResponseStatus_CTRL_NO_IMPL,
						Error:  msgScmUpdateNotImpl,
					},
				},
			},
			desc: "not implemented",
		},
	}

	srvIdx := 0

	for _, tt := range tests {
		config := defaultMockConfig(t)
		ss := newMockScmStorage(
			nil, []DeviceDiscovery{}, false, &config)

		results := []*pb.ScmModuleResult{}

		req := &pb.UpdateScmReq{}
		ss.Update(srvIdx, req, &results)

		// only ocm result in response for the moment
		AssertEqual(
			t, len(results), 1,
			"unexpected number of response results, "+tt.desc)

		result := results[0]

		AssertEqual(
			t, result.State.Error, tt.expResults[0].State.Error,
			"unexpected result error message, "+tt.desc)
		AssertEqual(
			t, result.State.Status, tt.expResults[0].State.Status,
			"unexpected response status, "+tt.desc)
		AssertEqual(
			t, result.Loc, tt.expResults[0].Loc,
			"unexpected module location, "+tt.desc)
	}
}
