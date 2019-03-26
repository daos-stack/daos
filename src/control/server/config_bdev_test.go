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
	"strings"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
)

// TestParseBdev verifies config parameters for bdev get converted into nvme
// config files that can be consumed by spdk.
func TestParseBdev(t *testing.T) {
	tests := []struct {
		bdevClass  BdClass
		bdevList   []string
		bdevSize   int  // relevant for MALLOC/FILE
		bdevNumber int  // relevant for MALLOC
		fileExists bool // mock return value for file exists check
		expEnvs    []string
		expFiles   [][]string
		errMsg     string
	}{
		{
			bdevClass: "",
			bdevList:  []string{"0000:81:00.1"},
		},
		{},
		{
			bdevClass: bdNVMe,
		},
		{
			bdevClass: bdNVMe,
			bdevList:  []string{"0000:81:00.0", "0000:81:00.1"},
			expFiles: [][]string{
				{
					`/mnt/daos/daos_nvme.conf:[Nvme]`,
					`TransportID "trtype:PCIe traddr:0000:81:00.0" Nvme0`,
					`TransportID "trtype:PCIe traddr:0000:81:00.1" Nvme1`,
					`RetryCount 4`,
					`TimeoutUsec 0`,
					`ActionOnTimeout None`,
					`AdminPollRate 100000`,
					`HotplugEnable No`,
					`HotplugPollRate 0`,
					``,
				},
			},
		},
		{
			bdevClass: bdFile,
			bdevList:  []string{"/tmp/myfile", "/tmp/myotherfile"},
			bdevSize:  5, // GB/file
			expFiles: [][]string{
				{`/tmp/myfile:empty size 4999999488`},
				{`/tmp/myotherfile:empty size 4999999488`},
				{
					"/mnt/daos/daos_nvme.conf:[AIO]",
					"AIO /tmp/myfile AIO0 4096",
					"AIO /tmp/myotherfile AIO1 4096",
					"",
				},
			},
			expEnvs: []string{"VOS_BDEV_CLASS=AIO"},
		},
		{
			bdevClass: bdFile,
			bdevList:  []string{"/tmp/myfile", "/tmp/myotherfile"},
			bdevSize:  5, // GB/file
			expFiles: [][]string{
				{
					"/mnt/daos/daos_nvme.conf:[AIO]",
					"AIO /tmp/myfile AIO0 4096",
					"AIO /tmp/myotherfile AIO1 4096",
					"",
				},
			},
			expEnvs:    []string{"VOS_BDEV_CLASS=AIO"},
			fileExists: true,
		},
		{
			bdevClass: bdKdev,
			bdevList:  []string{"/dev/sdb", "/dev/sdc"},
			expFiles: [][]string{
				{
					"/mnt/daos/daos_nvme.conf:[AIO]",
					`AIO /dev/sdb AIO0`,
					`AIO /dev/sdc AIO1`,
					"",
				},
			},
			expEnvs: []string{"VOS_BDEV_CLASS=AIO"},
		},
		{
			bdevClass:  bdMalloc,
			bdevSize:   5, // GB/file
			bdevNumber: 2, // number of LUNs
			expFiles: [][]string{
				{
					"/mnt/daos/daos_nvme.conf:[Malloc]",
					"NumberOfLuns 2",
					"LunSizeInMB 5000",
					"",
				},
			},
			expEnvs: []string{"VOS_BDEV_CLASS=MALLOC"},
		},
	}

	for _, tt := range tests {
		setupTest(t)
		// create default config and add server populated with test values
		server := newDefaultServer()
		server.ScmMount = "/mnt/daos"
		server.BdevClass = tt.bdevClass
		server.BdevList = tt.bdevList
		server.BdevSize = tt.bdevSize
		server.BdevNumber = tt.bdevNumber
		config := newMockConfig(nil, "", tt.fileExists, nil, nil, nil)
		config.Servers = append(config.Servers, server)
		err := config.parseNvme()
		if tt.errMsg != "" {
			ExpectError(t, err, tt.errMsg, "")
			continue
		}
		if err != nil {
			t.Fatal(err)
		}
		AssertEqual(
			t, len(files), len(tt.expFiles),
			fmt.Sprintf("files returned, got %#v want %#v", files, tt.expFiles))
		// iterate through entries representing files created
		for i, file := range files {
			actLines := strings.Split(file, "\n")
			AssertEqual(
				t, len(actLines), len(tt.expFiles[i]),
				fmt.Sprintf("lines returned, got %#v want %#v", files, tt.expFiles))
			// iterate over file contents, trim spaces before comparing
			for j, line := range actLines {
				line = strings.TrimSpace(line)
				AssertEqual(
					t, line, tt.expFiles[i][j],
					fmt.Sprintf("line %d, got %s want %s", i, line, tt.expFiles[i][j]))
			}
		}
		// verify VOS_BDEV_CLASS env gets set as expected
		AssertEqual(t, config.Servers[0].EnvVars, tt.expEnvs, string(tt.bdevClass))
	}
}
