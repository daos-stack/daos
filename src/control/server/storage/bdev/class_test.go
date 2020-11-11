//
// (C) Copyright 2018-2020 Intel Corporation.
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

package bdev

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// TestParseBdev verifies config parameters for bdev get converted into nvme
// config files that can be consumed by spdk.
func TestParseBdev(t *testing.T) {
	tests := map[string]struct {
		bdevClass       storage.BdevClass
		bdevList        []string
		bdevVmdDisabled bool
		bdevSize        int // relevant for MALLOC/FILE
		bdevNumber      int // relevant for MALLOC
		vosEnv          string
		wantBuf         []string
		errMsg          string
	}{
		"defaults from example config": {
			bdevVmdDisabled: true,
			wantBuf: []string{
				`[Nvme]`,
				`    TransportID "trtype:PCIe traddr:0000:81:00.0" Nvme__0`,
				`    RetryCount 4`,
				`    TimeoutUsec 0`,
				`    ActionOnTimeout None`,
				`    AdminPollRate 100000`,
				`    HotplugEnable No`,
				`    HotplugPollRate 0`,
				``,
			},
		},
		"multiple controllers": {
			bdevClass:       storage.BdevClassNvme,
			bdevVmdDisabled: true,
			bdevList:        []string{"0000:81:00.0", "0000:81:00.1"},
			wantBuf: []string{
				`[Nvme]`,
				`    TransportID "trtype:PCIe traddr:0000:81:00.0" Nvme__0`,
				`    TransportID "trtype:PCIe traddr:0000:81:00.1" Nvme__1`,
				`    RetryCount 4`,
				`    TimeoutUsec 0`,
				`    ActionOnTimeout None`,
				`    AdminPollRate 100000`,
				`    HotplugEnable No`,
				`    HotplugPollRate 0`,
				``,
			},
			vosEnv: "NVME",
		},
		"VMD devices": {
			bdevClass: storage.BdevClassNvme,
			bdevList:  []string{"5d0505:01:00.0", "5d0505:03:00.0"},
			wantBuf: []string{
				`[Vmd]`,
				`    Enable True`,
				``,
				`[Nvme]`,
				`    TransportID "trtype:PCIe traddr:5d0505:01:00.0" Nvme__0`,
				`    TransportID "trtype:PCIe traddr:5d0505:03:00.0" Nvme__1`,
				`    RetryCount 4`,
				`    TimeoutUsec 0`,
				`    ActionOnTimeout None`,
				`    AdminPollRate 100000`,
				`    HotplugEnable No`,
				`    HotplugPollRate 0`,
				``,
			},
			vosEnv: "NVME",
		},
		"multiple VMD and NVMe controllers": {
			bdevClass: storage.BdevClassNvme,
			bdevList:  []string{"0000:81:00.0", "5d0505:01:00.0", "5d0505:03:00.0"},
			wantBuf: []string{
				`[Vmd]`,
				`    Enable True`,
				``,
				`[Nvme]`,
				`    TransportID "trtype:PCIe traddr:0000:81:00.0" Nvme__0`,
				`    TransportID "trtype:PCIe traddr:5d0505:01:00.0" Nvme__1`,
				`    TransportID "trtype:PCIe traddr:5d0505:03:00.0" Nvme__2`,
				`    RetryCount 4`,
				`    TimeoutUsec 0`,
				`    ActionOnTimeout None`,
				`    AdminPollRate 100000`,
				`    HotplugEnable No`,
				`    HotplugPollRate 0`,
				``,
			},
			vosEnv: "NVME",
		},
		"AIO file": {
			bdevClass:       storage.BdevClassFile,
			bdevVmdDisabled: true,
			bdevList:        []string{"myfile", "myotherfile"},
			bdevSize:        5, // GB/file
			wantBuf: []string{
				`[AIO]`,
				`    AIO myfile AIO__0 4096`,
				`    AIO myotherfile AIO__1 4096`,
				``,
			},
			vosEnv: "AIO",
		},
		"AIO kdev": {
			bdevClass:       storage.BdevClassKdev,
			bdevVmdDisabled: true,
			bdevList:        []string{"sdb", "sdc"},
			wantBuf: []string{
				`[AIO]`,
				`    AIO sdb AIO__0`,
				`    AIO sdc AIO__1`,
				``,
			},
			vosEnv: "AIO",
		},
		"MALLOC": {
			bdevClass:       storage.BdevClassMalloc,
			bdevVmdDisabled: true,
			bdevSize:        5, // GB/file
			bdevNumber:      2, // number of LUNs
			wantBuf: []string{
				`[Malloc]`,
				`    NumberOfLuns 2`,
				`    LunSizeInMB 5000`,
				``,
			},
			vosEnv: "MALLOC",
		},
	}

	for name, tt := range tests {
		t.Run(name, func(t *testing.T) {
			testDir, err := ioutil.TempDir("", strings.Replace(t.Name(), "/", "-", -1))
			if err != nil {
				t.Fatal(err)
			}
			defer os.RemoveAll(testDir)

			config := storage.BdevConfig{}
			if tt.bdevClass != "" {
				config.Class = tt.bdevClass
			}
			config.VmdDisabled = tt.bdevVmdDisabled

			if len(tt.bdevList) != 0 {
				switch tt.bdevClass {
				case storage.BdevClassFile, storage.BdevClassKdev:
					for _, devFile := range tt.bdevList {
						absPath := filepath.Join(testDir, devFile)
						config.DeviceList = append(config.DeviceList, absPath)
						// clunky...
						for idx, line := range tt.wantBuf {
							if strings.Contains(line, devFile) {
								tt.wantBuf[idx] = strings.Replace(line, devFile, absPath, -1)
							}
						}
					}
				default:
					config.DeviceList = tt.bdevList
				}
			}

			if tt.bdevSize != 0 {
				config.FileSize = tt.bdevSize
			}
			if tt.bdevNumber != 0 {
				config.DeviceCount = tt.bdevNumber
			}

			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			provider, err := NewClassProvider(log, testDir, &config)
			if err != nil {
				t.Fatal(err)
			}

			if err := provider.GenConfigFile(); err != nil {
				t.Fatal(err)
			}

			if provider.cfgPath == "" {
				if len(config.DeviceList) == 0 {
					return
				}
				t.Fatal("provider cfgPath empty but device list isn't")
			}

			gotBuf, err := ioutil.ReadFile(provider.cfgPath)
			if err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.Join(tt.wantBuf, "\n"), string(gotBuf)); diff != "" {
				t.Fatalf("(-want, +got):\n%s", diff)
			}

			if config.VosEnv != tt.vosEnv {
				t.Fatalf("expected VosEnv to be %q, but it was %q", tt.vosEnv, config.VosEnv)
			}

			// The remainder only applies to loopback file devices.
			if tt.bdevClass != storage.BdevClassFile {
				return
			}
			for _, testFile := range config.DeviceList {
				st, err := os.Stat(testFile)
				if err != nil {
					t.Fatal(err)
				}
				expectedSize := (int64(tt.bdevSize*gbyte) / int64(blkSize)) * int64(blkSize)
				gotSize := st.Size()
				if gotSize != expectedSize {
					t.Fatalf("expected %s size to be %d, but got %d", testFile, expectedSize, gotSize)
				}
			}
		})
	}
}
