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
	"strings"
	"testing"

	"github.com/pkg/errors"

	. "github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	. "github.com/daos-stack/daos/src/control/common/storage"
	. "github.com/daos-stack/daos/src/control/lib/ipmctl"
)

// MockModule returns a mock SCM module of type exported from ipmctl.
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
	c *Configuration) *scmStorage {

	return &scmStorage{
		ipmctl:      &mockIpmctl{discoverModulesRet, mms},
		initialized: inited,
		config:      c,
	}
}

func defaultMockScmStorage(config *Configuration) *scmStorage {
	m := MockModule()

	return newMockScmStorage(
		nil, []DeviceDiscovery{m}, false, config)
}

func TestGetState(t *testing.T) {
	defer ShowLogOnFailure(t)()

	var regionsOut string  // variable cmd output
	commands := []string{} // external commands issued
	// ndctl create-namespace command return json format
	pmemOut := `{
   "dev":"namespace%d.0",
   "mode":"fsdax",
   "map":"dev",
   "size":"2964.94 GiB (3183.58 GB)",
   "uuid":"842fc847-28e0-4bb6-8dfc-d24afdba1528",
   "raw_uuid":"dedb4b28-dc4b-4ccd-b7d1-9bd475c91264",
   "sector_size":512,
   "blockdev":"pmem%d",
   "numa_node":%d
}
`
	onePmem, _ := parsePmemDevs(fmt.Sprintf(pmemOut, 1, 1, 0))
	twoPmemsJson := "[" + fmt.Sprintf(pmemOut, 1, 1, 0) + "," + fmt.Sprintf(pmemOut, 2, 2, 1) + "]"
	twoPmems, _ := parsePmemDevs(twoPmemsJson)
	createRegionsOut := MsgScmRebootRequired + "\n"
	pmemId := 1

	mockRun := func(in string) (string, error) {
		retString := in

		switch in {
		case cmdScmCreateRegions:
			retString = createRegionsOut // example successful output
		case cmdScmShowRegions:
			retString = regionsOut
		case cmdScmCreateNamespace:
			// stimulate free capacity of region being used
			regionsOut = strings.Replace(regionsOut, "3012.0", "0.0", 1)
			retString = fmt.Sprintf(pmemOut, pmemId, pmemId, pmemId-1)
			pmemId += 1
		case cmdScmListNamespaces:
			retString = twoPmemsJson
		}

		commands = append(commands, in)
		return retString, nil
	}

	tests := []struct {
		desc              string
		errMsg            string
		showRegionOut     string
		expRebootRequired bool
		expPmemDevs       []pmemDev
		expCommands       []string
	}{
		{
			desc:              "modules but no regions",
			showRegionOut:     outScmNoRegions,
			expRebootRequired: true,
			expCommands:       []string{cmdScmShowRegions, cmdScmCreateRegions},
		},
		{
			desc: "single region with free capacity",
			showRegionOut: "\n" +
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=0.0 GiB\n" +
				"---ISetID=0x81187f4881f02ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=3012.0 GiB\n" +
				"\n",
			expCommands: []string{cmdScmShowRegions, cmdScmCreateNamespace, cmdScmShowRegions},
			expPmemDevs: onePmem,
		},
		{
			desc: "regions with free capacity",
			showRegionOut: "\n" +
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=3012.0 GiB\n" +
				"---ISetID=0x81187f4881f02ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=3012.0 GiB\n" +
				"\n",
			expCommands: []string{
				cmdScmShowRegions, cmdScmCreateNamespace, cmdScmShowRegions,
				cmdScmCreateNamespace, cmdScmShowRegions,
			},
			expPmemDevs: twoPmems,
		},
		{
			desc: "regions with no capacity",
			showRegionOut: "\n" +
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=0.0 GiB\n" +
				"---ISetID=0x81187f4881f02ccb---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=0.0 GiB\n" +
				"\n",
			expCommands: []string{cmdScmShowRegions, cmdScmListNamespaces},
			expPmemDevs: twoPmems,
		},
	}

	for _, tt := range tests {
		config := defaultMockConfig(t)
		ss := defaultMockScmStorage(config).withRunCmd(mockRun)

		if err := ss.Discover(); err != nil {
			t.Fatal(err)
		}

		// reset to initial values between tests
		regionsOut = tt.showRegionOut
		pmemId = 1
		commands = nil

		needsReboot, pmemDevs, err := ss.Prep()
		if tt.errMsg != "" {
			ExpectError(t, err, tt.errMsg, tt.desc)
			continue
		}
		if err != nil {
			t.Fatal(tt.desc + ": " + err.Error())
		}

		AssertEqual(t, commands, tt.expCommands, tt.desc+": unexpected list of commands run")
		AssertEqual(t, needsReboot, tt.expRebootRequired, tt.desc+": unexpected value for is reboot required")
		AssertEqual(t, pmemDevs, tt.expPmemDevs, tt.desc+": unexpected list of pmem kernel device names")
	}
}

func TestDiscoverScm(t *testing.T) {
	mPB := MockModulePB()
	m := MockModule()
	config := defaultMockConfig(t)

	tests := []struct {
		inited            bool
		ipmctlDiscoverRet error
		errMsg            string
		expModules        ScmModules
	}{
		{
			true,
			nil,
			"",
			ScmModules(nil),
		},
		{
			false,
			nil,
			"",
			ScmModules{mPB},
		},
		{
			false,
			errors.New("ipmctl example failure"),
			msgIpmctlDiscoverFail + ": ipmctl example failure",
			ScmModules{mPB},
		},
	}

	for _, tt := range tests {
		ss := newMockScmStorage(
			tt.ipmctlDiscoverRet, []DeviceDiscovery{m}, tt.inited,
			config)

		if err := ss.Discover(); err != nil {
			if tt.errMsg != "" {
				AssertEqual(t, err.Error(), tt.errMsg, "")
				continue
			}
			t.Fatal(err)
		}

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
		expResults ScmMountResults
		desc       string
	}{
		{
			inited: false,
			mount:  "/mnt/daos",
			expResults: ScmMountResults{
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
			expResults: ScmMountResults{
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
			expResults: ScmMountResults{
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
			expResults: ScmMountResults{
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
			expResults: ScmMountResults{
				{
					Mntpoint: "/mnt/daos",
					State:    &pb.ResponseState{},
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
			class:  scmDCPM,
			devs:   []string{"/dev/pmem0"},
			expResults: ScmMountResults{
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
				"syscall: mount /dev/pmem0, /mnt/daos, ext4, 0, dax",
			},
			desc: "dcpm success",
		},
		{
			inited: true,
			mount:  "/mnt/daos",
			class:  scmDCPM,
			devs:   []string{},
			expResults: ScmMountResults{
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
			expResults: ScmMountResults{
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
			expResults: ScmMountResults{
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

		results := ScmMountResults{}

		if tt.inited {
			if err := ss.Discover(); err != nil {
				t.Fatal(err)
			}
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
		expResults ScmModuleResults
		desc       string
	}{
		{
			expResults: ScmModuleResults{
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
			nil, []DeviceDiscovery{}, false, config)

		results := ScmModuleResults{}

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
