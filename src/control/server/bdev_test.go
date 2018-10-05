//
// (C) Copyright 2018 Intel Corporation.
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
	"strings"
	"testing"

	. "github.com/daos-stack/daos/src/control/utils/test"
)

// TestParseBdev verifies config parameters for bdev get converted into nvme
// config files that can be consumed by spdk.
func TestParseBdev(t *testing.T) {
	tests := []struct {
		bdevClass  BdClass
		bdevList   []string
		bdevSize   int // relevant for MALLOC/FILE
		bdevNumber int // relevant for MALLOC
		expected   []string
		errMsg     string
	}{
		{
			"",
			[]string{"0000:81:00.1"},
			0,
			0,
			[]string{`&{checkMountRet:<nil> getenvRet: files:map[]}`},
			"",
		},
		{
			"",
			[]string{},
			0,
			0,
			[]string{`&{checkMountRet:<nil> getenvRet: files:map[]}`},
			"",
		},
		{
			NVME,
			[]string{},
			0,
			0,
			[]string{`&{checkMountRet:<nil> getenvRet: files:map[]}`},
			"",
		},
		{
			NVME,
			[]string{"0000:81:00.0", "0000:81:00.1"},
			0,
			0,
			[]string{
				`&{checkMountRet:<nil> getenvRet: files:map[/mnt/daos/daos_nvme.conf:[Nvme]`,
				`TransportID "trtype:PCIe traddr:0000:81:00.0" Nvme0`,
				`TransportID "trtype:PCIe traddr:0000:81:00.1" Nvme1`,
				`RetryCount 4`,
				`TimeoutUsec 0`,
				`ActionOnTimeout None`,
				`AdminPollRate 100000`,
				`HotplugEnable No`,
				`HotplugPollRate 0`,
				`]}`},
			"",
		},
		{
			FILE,
			[]string{"/tmp/myfile", "/tmp/myotherfile"},
			4,
			0,
			[]string{
				`&{checkMountRet:<nil> getenvRet: files:map[/mnt/daos/daos_nvme.conf:[AIO]`,
				`AIO /tmp/myfile AIO0 4096`,
				`AIO /tmp/myotherfile AIO1 4096`,
				``,
				`]}`},
			"",
		},
		{
			KDEV,
			[]string{"/dev/sdb", "/dev/sdc"},
			0,
			0,
			[]string{
				`&{checkMountRet:<nil> getenvRet: files:map[/mnt/daos/daos_nvme.conf:[AIO]`,
				`AIO /dev/sdb AIO0`,
				`AIO /dev/sdc AIO1`,
				``,
				`]}`},
			"",
		},
		{
			MALLOC,
			[]string{},
			4,
			1,
			[]string{
				`&{checkMountRet:<nil> getenvRet: files:map[/mnt/daos/daos_nvme.conf:[Malloc]`,
				`NumberOfLuns 1`,
				`LunSizeInMB 4096`,
				``,
				`]}`},
			"",
		},
	}

	for _, tt := range tests {
		// create default config and add server populated with test values
		server := NewDefaultServer()
		server.ScmMount = "/mnt/daos"
		server.BdevClass = tt.bdevClass
		server.BdevList = tt.bdevList
		server.BdevSize = tt.bdevSize
		server.BdevNumber = tt.bdevNumber
		config := NewDefaultMockConfig()
		config.Servers = append(config.Servers, server)
		err := config.parseNvme()
		if tt.errMsg != "" {
			ExpectError(t, err, tt.errMsg, "")
			continue
		}
		if err != nil {
			t.Fatal(err.Error())
		}
		actLines := strings.Split(fmt.Sprintf("%+v\n", config.ext), "\n")
		for i, line := range tt.expected {
			AssertEqual(
				t,
				strings.TrimSpace(actLines[i]),
				line,
				fmt.Sprintf("line %d", i))
		}
	}
}
