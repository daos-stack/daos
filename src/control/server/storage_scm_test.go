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

package server

import (
	"fmt"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common/proto/ctl"
	. "github.com/daos-stack/daos/src/control/common/storage"
	. "github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// MockModule returns a mock SCM module of type exported from ipmctl.
func MockModule() DeviceDiscovery {
	m := common.MockModulePB()
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

// ScmStorage factory with mocked interfaces for testing
func newMockScmStorage(log logging.Logger, ext External, discoverModulesRet error,
	mms []DeviceDiscovery, inited bool, prep PrepScm) *scmStorage {

	return &scmStorage{
		ext:         ext,
		log:         log,
		ipmctl:      &mockIpmctl{discoverModulesRet, mms},
		prep:        prep,
		initialized: inited,
	}
}

func defaultMockScmStorage(log logging.Logger, ext External) *scmStorage {
	m := MockModule()

	return newMockScmStorage(log, ext, nil, []DeviceDiscovery{m}, false, newMockPrepScm())
}

func TestDiscoverScm(t *testing.T) {
	mPB := common.MockModulePB()
	m := MockModule()

	tests := map[string]struct {
		inited            bool
		ipmctlDiscoverRet error
		errMsg            string
		expModules        ScmModules
	}{
		"already initialized": {
			true,
			nil,
			"",
			ScmModules(nil),
		},
		"normal run": {
			false,
			nil,
			"",
			ScmModules{mPB},
		},
		"results in error": {
			false,
			errors.New("ipmctl example failure"),
			msgIpmctlDiscoverFail + ": ipmctl example failure",
			ScmModules{mPB},
		},
		"discover succeeds but get pmem fails": {
			false,
			nil,
			msgIpmctlDiscoverFail,
			ScmModules{mPB},
		},
	}

	for name, tt := range tests {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)()

			ss := newMockScmStorage(log, nil, tt.ipmctlDiscoverRet,
				[]DeviceDiscovery{m}, tt.inited, newMockPrepScm())

			if err := ss.Discover(); err != nil {
				if tt.errMsg != "" {
					common.AssertEqual(t, err.Error(), tt.errMsg, "")
					return
				}
				t.Fatal(err)
			}

			common.AssertEqual(t, ss.modules, tt.expModules, "unexpected list of modules")
		})
	}
}

func TestFormatScm(t *testing.T) {
	err := errors.New(msgNdctlNotFound)
	noNdctlPrep := &mockPrepScm{
		prepRet:          err,
		resetRet:         err,
		getStateRet:      err,
		getNamespacesRet: err,
	}

	tests := []struct {
		inited    bool
		formatted bool
		mountRet  error
		// log context should be stack layer registering result
		unmountRet error
		mkdirRet   error
		removeRet  error
		mount      string
		prep       PrepScm
		expErrMsg  string
		class      storage.ScmClass
		devs       []string
		size       int
		expCmds    []string // expected arguments in syscall methods
		expResults ScmMountResults
		desc       string
	}{
		{
			inited: false,
			mount:  "/mnt/daos",
			expResults: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_APP,
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
			expResults: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_APP,
						Error:  msgScmAlreadyFormatted,
					},
				},
			},
			desc: "already formatted",
		},
		{
			inited: true,
			expResults: ScmMountResults{
				{
					Mntpoint: "",
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_CONF,
						Error:  msgScmMountEmpty,
					},
				},
			},
			desc: "missing mount point",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			expResults: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_CONF,
						Error:  ": " + msgScmClassNotSupported,
					},
				},
			},
			desc: "no class",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			class:  storage.ScmClassRAM,
			size:   6,
			expResults: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State:    &ResponseState{},
				},
			},
			expCmds: []string{
				"syscall: calling unmount with /mnt/daos, MNT_DETACH",
				"os: removeall /mnt/daos",
				"os: mkdirall /mnt/daos, 0777",
				"syscall: mount tmpfs, /mnt/daos, tmpfs, 0, size=6g",
			},
			desc: "ram success",
		},
		{
			inited:    true,
			mount:     "/mnt/daos",
			class:     storage.ScmClassRAM,
			size:      6,
			prep:      noNdctlPrep, // format should succeed without ndctl being installed
			expErrMsg: msgIpmctlDiscoverFail + ": " + msgNdctlNotFound,
			expResults: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State:    &ResponseState{},
				},
			},
			expCmds: []string{
				"syscall: calling unmount with /mnt/daos, MNT_DETACH",
				"os: removeall /mnt/daos",
				"os: mkdirall /mnt/daos, 0777",
				"syscall: mount tmpfs, /mnt/daos, tmpfs, 0, size=6g",
			},
			desc: "ram no ndctl installed",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			class:  storage.ScmClassDCPM,
			devs:   []string{"/dev/pmem0"},
			expResults: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State:    &ResponseState{},
				},
			},
			expCmds: []string{
				"syscall: calling unmount with /mnt/daos, MNT_DETACH",
				"os: removeall /mnt/daos",
				"cmd: wipefs -a /dev/pmem0",
				"cmd: mkfs.ext4 /dev/pmem0",
				"os: mkdirall /mnt/daos, 0777",
				"syscall: mount /dev/pmem0, /mnt/daos, ext4, 0, dax",
			},
			desc: "dcpm success",
		},
		{
			inited:    true,
			mount:     "/mnt/daos",
			class:     storage.ScmClassDCPM,
			devs:      []string{"/dev/pmem0"},
			prep:      noNdctlPrep, // format should succeed without ndctl being installed
			expErrMsg: msgIpmctlDiscoverFail + ": " + msgNdctlNotFound,
			expResults: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State:    &ResponseState{},
				},
			},
			expCmds: []string{
				"syscall: calling unmount with /mnt/daos, MNT_DETACH",
				"os: removeall /mnt/daos",
				"cmd: wipefs -a /dev/pmem0",
				"cmd: mkfs.ext4 /dev/pmem0",
				"os: mkdirall /mnt/daos, 0777",
				"syscall: mount /dev/pmem0, /mnt/daos, ext4, 0, dax",
			},
			desc: "dcpm no ndctl installed",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			class:  storage.ScmClassDCPM,
			devs:   []string{},
			expResults: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_CONF,
						Error:  msgScmBadDevList,
					},
				},
			},
			desc: "dcpm missing dev",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			class:  storage.ScmClassDCPM,
			devs:   []string(nil),
			expResults: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_CONF,
						Error:  msgScmBadDevList,
					},
				},
			},
			desc: "dcpm nil devs",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			class:  storage.ScmClassDCPM,
			devs:   []string{""},
			expResults: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_CONF,
						Error:  msgScmDevEmpty,
					},
				},
			},
			desc: "dcpm empty dev",
		},
	}

	srvIdx := 0

	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)()

			config := newMockStorageConfig(tt.mountRet, tt.unmountRet,
				tt.mkdirRet, tt.removeRet, tt.mount, tt.class, tt.devs,
				tt.size, storage.BdevClassNvme, []string{}, false, false)

			mockPrep := tt.prep
			if mockPrep == nil {
				mockPrep = newMockPrepScm()
			}

			ss := newMockScmStorage(log, config.ext, nil, []DeviceDiscovery{},
				false, mockPrep)
			ss.formatted = tt.formatted

			results := ScmMountResults{}

			if tt.inited {
				// Discovery is run in SCS.Setup() and is not
				// fatal, continue with expected errors to
				// format as in normal program execution.
				if err := ss.Discover(); err != nil {
					if tt.expErrMsg != "" {
						common.ExpectError(t, err, tt.expErrMsg, tt.desc)
					} else {
						// unexpected failure
						t.Fatal(tt.desc + ": " + err.Error())
					}
				}
			}

			scmCfg := config.Servers[srvIdx].Storage.SCM
			ss.Format(scmCfg, &results)

			// only ocm result in response for the moment
			common.AssertEqual(
				t, len(results), 1,
				"unexpected number of response results, "+tt.desc)

			result := results[0]

			common.AssertEqual(
				t, result.State.Error, tt.expResults[0].State.Error,
				"unexpected result error message, "+tt.desc)
			common.AssertEqual(
				t, result.State.Status, tt.expResults[0].State.Status,
				"unexpected response status, "+tt.desc)
			common.AssertEqual(
				t, result.Mntpoint, tt.expResults[0].Mntpoint,
				"unexpected mntpoint, "+tt.desc)

			if result.State.Status == ResponseStatus_CTRL_SUCCESS {
				common.AssertEqual(
					t, ss.formatted,
					true, "expect formatted state, "+tt.desc)
			}

			cmds := config.ext.getHistory()
			common.AssertEqual(
				t, len(cmds), len(tt.expCmds), "number of cmds, "+tt.desc)
			for i, s := range cmds {
				common.AssertEqual(
					t, s, tt.expCmds[i],
					fmt.Sprintf("commands don't match (%s)", tt.desc))
			}
		})
	}
}

// TestUpdateScm currently just verifies that response is populated with not
// implemented state in result.
func TestUpdateScm(t *testing.T) {
	tests := []struct {
		expResults ScmModuleResults
		desc       string
	}{
		{
			expResults: ScmModuleResults{
				{
					Loc: &ScmModule_Location{},
					State: &ResponseState{
						Status: ResponseStatus_CTRL_NO_IMPL,
						Error:  msgScmUpdateNotImpl,
					},
				},
			},
			desc: "not implemented",
		},
	}

	srvIdx := 0

	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)()

			config := defaultMockConfig(t)
			ss := newMockScmStorage(log, config.ext, nil, []DeviceDiscovery{},
				false, newMockPrepScm())

			results := ScmModuleResults{}

			req := &UpdateScmReq{}
			scmCfg := config.Servers[srvIdx].Storage.SCM
			ss.Update(scmCfg, req, &results)

			// only ocm result in response for the moment
			common.AssertEqual(
				t, len(results), 1,
				"unexpected number of response results, "+tt.desc)

			result := results[0]

			common.AssertEqual(
				t, result.State.Error, tt.expResults[0].State.Error,
				"unexpected result error message, "+tt.desc)
			common.AssertEqual(
				t, result.State.Status, tt.expResults[0].State.Status,
				"unexpected response status, "+tt.desc)
			common.AssertEqual(
				t, result.Loc, tt.expResults[0].Loc,
				"unexpected module location, "+tt.desc)
		})
	}
}
