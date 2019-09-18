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

package storage

import (
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	. "github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common/proto/ctl"
	. "github.com/daos-stack/daos/src/control/common/storage"
	. "github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	. "github.com/daos-stack/daos/src/control/server/storage/config"
	. "github.com/daos-stack/daos/src/control/server/storage/messages"
)

func TestDiscoverScm(t *testing.T) {
	mPB := MockModulePB()
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
			MsgIpmctlDiscoverFail + ": ipmctl example failure",
			ScmModules{mPB},
		},
	}

	for name, tt := range tests {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)()

			ss := NewMockScmProvider(log, nil, tt.ipmctlDiscoverRet,
				[]DeviceDiscovery{m}, tt.inited, NewMockPrepScm())

			if err := ss.Discover(); err != nil {
				if tt.errMsg != "" {
					AssertEqual(t, err.Error(), tt.errMsg, "")
					return
				}
				t.Fatal(err)
			}

			AssertEqual(t, ss.modules, tt.expModules, "unexpected list of modules")
		})
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
						Error:  MsgScmNotInited,
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
						Error:  MsgScmAlreadyFormatted,
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
						Error:  MsgScmMountEmpty,
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
						Error:  ": " + MsgScmClassNotSupported,
					},
				},
			},
			desc: "no class",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			class:  ScmClassRAM,
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
			inited: true,
			mount:  "/mnt/daos",
			class:  ScmClassDCPM,
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
			inited: true,
			mount:  "/mnt/daos",
			class:  ScmClassDCPM,
			devs:   []string{},
			expResults: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_CONF,
						Error:  MsgScmBadDevList,
					},
				},
			},
			desc: "dcpm missing dev",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			class:  ScmClassDCPM,
			devs:   []string(nil),
			expResults: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_CONF,
						Error:  MsgScmBadDevList,
					},
				},
			},
			desc: "dcpm nil devs",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			class:  ScmClassDCPM,
			devs:   []string{""},
			expResults: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State: &ResponseState{
						Status: ResponseStatus_CTRL_ERR_CONF,
						Error:  MsgScmDevEmpty,
					},
				},
			},
			desc: "dcpm empty dev",
		},
	}

	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)()

			scmExt := &MockScmExt{
				mountRet:   tt.mountRet,
				unmountRet: tt.unmountRet,
				mkdirRet:   tt.mkdirRet,
				removeRet:  tt.removeRet,
			}
			ss := NewMockScmProvider(log, scmExt, nil, []DeviceDiscovery{},
				false, NewMockPrepScm())
			ss.formatted = tt.formatted

			results := ScmMountResults{}

			if tt.inited {
				if err := ss.Discover(); err != nil {
					t.Fatal(err)
				}
			}

			scmCfg := ScmConfig{
				MountPoint:  tt.mount,
				Class:       tt.class,
				RamdiskSize: tt.size,
				DeviceList:  tt.devs,
			}
			ss.Format(scmCfg, &results)

			// only ocm result in response for the moment
			AssertEqual(
				t, len(results), 1,
				"unexpected number of response results, "+tt.desc)

			result := results[0]

			if diff := cmp.Diff(tt.expResults[0], result); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}

			cmds := scmExt.getHistory()
			AssertEqual(
				t, len(cmds), len(tt.expCmds), "number of cmds, "+tt.desc)
			for i, s := range cmds {
				AssertEqual(
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
						Error:  MsgScmUpdateNotImpl,
					},
				},
			},
			desc: "not implemented",
		},
	}

	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)()

			ss := NewMockScmProvider(log, nil, nil, []DeviceDiscovery{},
				false, NewMockPrepScm())

			results := ScmModuleResults{}

			req := &UpdateScmReq{}
			scmCfg := ScmConfig{}
			ss.Update(scmCfg, req, &results)

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
		})
	}
}
