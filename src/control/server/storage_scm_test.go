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
	. "github.com/daos-stack/go-ipmctl/ipmctl"
)

// MockModule returns a mock SCM module of type exported from go-ipmctl.
func MockModule() DeviceDiscovery {
	m := MockModulePB()
	dd := DeviceDiscovery{}
	dd.Physical_id = uint16(m.Physicalid)
	dd.Channel_id = uint16(m.Channel)
	dd.Channel_pos = uint16(m.Channelpos)
	dd.Memory_controller_id = uint16(m.Memctrlr)
	dd.Socket_id = uint16(m.Socket)
	dd.Capacity = m.Capacity
	return dd
}

type mockIpmCtl struct {
	modules []DeviceDiscovery
}

func (m *mockIpmCtl) Discover() ([]DeviceDiscovery, error) {
	return m.modules, nil
}

// build config with mock external method behaviour relevant to this file
func mockScmConfig(
	mountRet error, unmountRet error, mkdirRet error, removeRet error) configuration {

	config := newDefaultConfiguration(
		&mockExt{
			nil, "", false, mountRet, unmountRet, mkdirRet, removeRet})

	return config
}

// return config reference with specified external method behaviour and scm config params
func newMockScmConfig(
	mountRet error, unmountRet error, mkdirRet error, removeRet error,
	mount string, class ScmClass, devs []string, size int) *configuration {

	c := mockScmConfig(mountRet, unmountRet, mkdirRet, removeRet)
	c.Servers = append(c.Servers, newDefaultServer())
	c.Servers[0].ScmMount = mount
	c.Servers[0].ScmClass = class
	c.Servers[0].ScmList = devs
	c.Servers[0].ScmSize = size

	return &c
}

// mockScmStorage factory
func newMockScmStorage(
	mms []DeviceDiscovery, inited bool, c *configuration) *scmStorage {

	return &scmStorage{
		ipmCtl:      &mockIpmCtl{modules: mms},
		initialized: inited,
		config:      c,
	}
}

func defaultMockScmStorage(config *configuration) *scmStorage {
	m := MockModule()

	return newMockScmStorage([]DeviceDiscovery{m}, false, config)
}

func TestDiscoveryScm(t *testing.T) {
	tests := []struct {
		inited bool
		errMsg string
	}{
		{
			true,
			"",
		},
		{
			false,
			"scm storage not initialized",
		},
	}

	mPB := MockModulePB()
	m := MockModule()
	config := defaultMockConfig(t)

	for _, tt := range tests {
		ss := newMockScmStorage([]DeviceDiscovery{m}, tt.inited, &config)

		if err := ss.Discover(); err != nil {
			if tt.errMsg != "" {
				ExpectError(t, err, tt.errMsg, "")
				continue
			}
			t.Fatal(err)
		}

		AssertEqual(t, len(ss.modules), 1, "unexpected number of modules")
		AssertEqual(t, ss.modules[int32(mPB.Physicalid)], mPB, "unexpected module values")
	}
}

func TestFormatScm(t *testing.T) {
	tests := []struct {
		mountRet    error
		unmountRet  error
		mkdirRet    error
		removeRet   error
		mount       string
		class       ScmClass
		devs        []string
		size        int
		expSyscalls []string // expected arguments in syscall methods
		desc        string
		errMsg      string
	}{
		{
			desc:   "zero values",
			errMsg: "scm mount must be specified in config",
		},
		{
			desc:   "no class",
			mount:  "/mnt/daos",
			errMsg: "unsupported ScmClass",
		},
		{
			desc:  "ram success",
			mount: "/mnt/daos",
			class: scmRAM,
			size:  6,
			expSyscalls: []string{
				"umount /mnt/daos",
				"remove /mnt/daos",
				"mkdir /mnt/daos",
				"mount tmpfs /mnt/daos tmpfs size=6g",
			},
		},
		{
			desc:  "dcpm success",
			mount: "/mnt/daos",
			class: scmDCPM,
			devs:  []string{"/dev/pmem0"},
			expSyscalls: []string{
				"umount /mnt/daos",
				"remove /mnt/daos",
				"wipefs -a /dev/pmem0",
				"mkfs.ext4 /dev/pmem0",
				"mkdir /mnt/daos",
				"mount /dev/pmem0 /mnt/daos ext4 dax",
			},
		},
		{
			desc:   "dcpm missing dev",
			mount:  "/mnt/daos",
			class:  scmDCPM,
			devs:   []string{},
			errMsg: "expecting one scm dcpm pmem device per-server in config",
		},
		{
			desc:   "dcpm nil devs",
			mount:  "/mnt/daos",
			class:  scmDCPM,
			devs:   []string(nil),
			errMsg: "expecting one scm dcpm pmem device per-server in config",
		},
		{
			desc:   "dcpm empty dev",
			mount:  "/mnt/daos",
			class:  scmDCPM,
			devs:   []string{""},
			errMsg: "scm dcpm device list must contain path",
		},
	}

	serverIdx := 0

	for _, tt := range tests {
		commands = []string{}

		config := newMockScmConfig(
			tt.mountRet, tt.unmountRet, tt.mkdirRet, tt.removeRet,
			tt.mount, tt.class, tt.devs, tt.size)

		ss := newMockScmStorage([]DeviceDiscovery{}, true, config)

		if err := ss.Format(serverIdx); err != nil {
			if tt.errMsg != "" {
				ExpectError(t, err, tt.errMsg, "")
				continue
			}
			t.Fatal(err)
		}

		for i, s := range commands {
			AssertEqual(
				t, s, tt.expSyscalls[i],
				fmt.Sprintf("commands don't match (%s)", tt.desc))
		}

		// in case extra values were expected
		AssertEqual(
			t, len(commands), len(tt.expSyscalls),
			fmt.Sprintf("unexpected number of commands (%s)", tt.desc))
	}
}
