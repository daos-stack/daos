//
// (C) Copyright 2020 Intel Corporation.
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

package pretty

import (
	"fmt"
	"strings"
	"testing"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type mockHostStorage struct {
	hostAddr string
	storage  *control.HostStorage
}

func mockHostStorageMap(t *testing.T, hosts ...*mockHostStorage) control.HostStorageMap {
	hsm := make(control.HostStorageMap)

	for _, mhs := range hosts {
		if err := hsm.Add(mhs.hostAddr, mhs.storage); err != nil {
			t.Fatal(err)
		}
	}

	return hsm
}

func TestPretty_PrintNVMeHealthMap(t *testing.T) {
	var (
		controllerA = storage.MockNvmeController(1)
		controllerB = storage.MockNvmeController(2)
	)
	for name, tc := range map[string]struct {
		hsm         control.HostStorageMap
		expPrintStr string
	}{
		"no devices": {
			hsm: mockHostStorageMap(t, &mockHostStorage{"host1", &control.HostStorage{}}),
			expPrintStr: `
-----
host1
-----
  No NVMe devices detected
`,
		},
		"1 host; 2 devices": {
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						NvmeDevices: storage.NvmeControllers{
							controllerA,
							controllerB,
						},
					},
				},
			),
			expPrintStr: fmt.Sprintf(`
-----
host1
-----
PCI:%s Model:%s FW:%s Socket:%d Capacity:%s
  Health Stats:
    Temperature:%dK(%dC)
    Controller Busy Time:0s
    Power Cycles:%d
    Power On Duration:%s
    Unsafe Shutdowns:0
    Media Errors:0
    Error Log Entries:0
  Critical Warnings:
    Temperature: WARNING
    Available Spare: OK
    Device Reliability: OK
    Read Only: OK
    Volatile Memory Backup: OK

PCI:%s Model:%s FW:%s Socket:%d Capacity:%s
  Health Stats:
    Temperature:%dK(%dC)
    Controller Busy Time:0s
    Power Cycles:%d
    Power On Duration:%s
    Unsafe Shutdowns:0
    Media Errors:0
    Error Log Entries:0
  Critical Warnings:
    Temperature: WARNING
    Available Spare: OK
    Device Reliability: OK
    Read Only: OK
    Volatile Memory Backup: OK

`,
				controllerA.PciAddr, controllerA.Model, controllerA.FwRev, controllerA.SocketID,
				humanize.Bytes(controllerA.Capacity()), controllerA.HealthStats.Temp, controllerA.HealthStats.Temp-273,
				controllerA.HealthStats.PowerCycles, time.Duration(controllerA.HealthStats.PowerOnHours)*time.Hour,
				controllerB.PciAddr, controllerB.Model, controllerB.FwRev, controllerB.SocketID,
				humanize.Bytes(controllerB.Capacity()), controllerB.HealthStats.Temp, controllerB.HealthStats.Temp-273,
				controllerB.HealthStats.PowerCycles, time.Duration(controllerB.HealthStats.PowerOnHours)*time.Hour,
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintNvmeHealthMap(tc.hsm, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}
