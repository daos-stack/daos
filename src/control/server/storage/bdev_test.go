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

package storage

import (
	"bytes"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/logging"
	. "github.com/daos-stack/daos/src/control/server/storage/config"
)

// TestParseBdev verifies config parameters for bdev get converted into nvme
// config files that can be consumed by spdk.
func TestParseBdev(t *testing.T) {
	tests := map[string]struct {
		bdevClass  BdevClass
		bdevList   []string
		bdevSize   int // relevant for MALLOC/FILE
		bdevNumber int // relevant for MALLOC
		vosEnv     string
		wantBuf    []string
		errMsg     string
	}{
		"defaults from example config": {
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
			bdevClass: BdevClassNvme,
			bdevList:  []string{"0000:81:00.0", "0000:81:00.1"},
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
		},
		"AIO file": {
			bdevClass: BdevClassFile,
			bdevList:  []string{"myfile", "myotherfile"},
			bdevSize:  5, // GB/file
			wantBuf: []string{
				`[AIO]`,
				`    AIO myfile AIO__0 4096`,
				`    AIO myotherfile AIO__1 4096`,
				``,
			},
			vosEnv: "AIO",
		},
		"AIO kdev": {
			bdevClass: BdevClassKdev,
			bdevList:  []string{"sdb", "sdc"},
			wantBuf: []string{
				`[AIO]`,
				`    AIO sdb AIO__0`,
				`    AIO sdc AIO__1`,
				``,
			},
			vosEnv: "AIO",
		},
		"MALLOC": {
			bdevClass:  BdevClassMalloc,
			bdevSize:   5, // GB/file
			bdevNumber: 2, // number of LUNs
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

			config := BdevConfig{}
			if tt.bdevClass != "" {
				config.Class = tt.bdevClass
			}
			if len(tt.bdevList) != 0 {
				switch tt.bdevClass {
				case BdevClassFile, BdevClassKdev:
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

			var logBuf bytes.Buffer
			testLog := logging.NewCombinedLogger(t.Name(), &logBuf)
			defer func(t *testing.T) {
				if t.Failed() {
					t.Error(logBuf.String())
				}
			}(t)

			provider, err := NewBdevProvider(testLog, testDir, &config)
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

			if err := provider.PrepareDevices(); err != nil {
				t.Fatal(err)
			}

			// The remainder only applies to loopback file devices.
			if tt.bdevClass != BdevClassFile {
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
