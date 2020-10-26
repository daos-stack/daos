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
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestPretty_PrintNVMeHealthMap(t *testing.T) {
	var (
		controllerA    = storage.MockNvmeController(1)
		controllerB    = storage.MockNvmeController(2)
		controllerAwTS = storage.MockNvmeController(1)
	)
	tt, err := strconv.ParseUint("1405544146", 10, 64)
	if err != nil {
		t.Fatal(err)
	}
	controllerAwTS.HealthStats.Timestamp = tt
	ttStr := time.Unix(int64(tt), 0).Format(time.UnixDate)

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
    Temperature:%dK(%.02fC)
    Temperature Warning Duration:%dm0s
    Temperature Critical Duration:%dm0s
    Controller Busy Time:%dm0s
    Power Cycles:%d
    Power On Duration:%s
    Unsafe Shutdowns:%d
    Media Errors:%d
    Error Log Entries:%d
  Critical Warnings:
    Temperature: WARNING
    Available Spare: WARNING
    Device Reliability: WARNING
    Read Only: WARNING
    Volatile Memory Backup: WARNING

PCI:%s Model:%s FW:%s Socket:%d Capacity:%s
  Health Stats:
    Temperature:%dK(%.02fC)
    Temperature Warning Duration:%dm0s
    Temperature Critical Duration:%dm0s
    Controller Busy Time:%dm0s
    Power Cycles:%d
    Power On Duration:%s
    Unsafe Shutdowns:%d
    Media Errors:%d
    Error Log Entries:%d
  Critical Warnings:
    Temperature: WARNING
    Available Spare: WARNING
    Device Reliability: WARNING
    Read Only: WARNING
    Volatile Memory Backup: WARNING

`,
				controllerA.PciAddr, controllerA.Model, controllerA.FwRev,
				controllerA.SocketID, humanize.Bytes(controllerA.Capacity()),
				controllerA.HealthStats.TempK(), controllerA.HealthStats.TempC(),
				controllerA.HealthStats.TempWarnTime, controllerA.HealthStats.TempCritTime,
				controllerA.HealthStats.CtrlBusyTime, controllerA.HealthStats.PowerCycles,
				time.Duration(controllerA.HealthStats.PowerOnHours)*time.Hour,
				controllerA.HealthStats.UnsafeShutdowns, controllerA.HealthStats.MediaErrors,
				controllerA.HealthStats.ErrorLogEntries,

				controllerB.PciAddr, controllerB.Model, controllerB.FwRev, controllerB.SocketID,
				humanize.Bytes(controllerB.Capacity()),
				controllerB.HealthStats.TempK(), controllerB.HealthStats.TempC(),
				controllerB.HealthStats.TempWarnTime, controllerB.HealthStats.TempCritTime,
				controllerB.HealthStats.CtrlBusyTime, controllerB.HealthStats.PowerCycles,
				time.Duration(controllerB.HealthStats.PowerOnHours)*time.Hour,
				controllerB.HealthStats.UnsafeShutdowns, controllerB.HealthStats.MediaErrors,
				controllerB.HealthStats.ErrorLogEntries,
			),
		},
		"1 host; 1 device, fetched over drpc": {
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						NvmeDevices: storage.NvmeControllers{
							controllerAwTS,
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
    Timestamp:%s
    Temperature:%dK(%.02fC)
    Temperature Warning Duration:%dm0s
    Temperature Critical Duration:%dm0s
    Controller Busy Time:%dm0s
    Power Cycles:%d
    Power On Duration:%s
    Unsafe Shutdowns:%d
    Media Errors:%d
    Read Errors:%d
    Write Errors:%d
    Unmap Errors:%d
    Checksum Errors:%d
    Error Log Entries:%d
  Critical Warnings:
    Temperature: WARNING
    Available Spare: WARNING
    Device Reliability: WARNING
    Read Only: WARNING
    Volatile Memory Backup: WARNING

`,
				controllerAwTS.PciAddr, controllerAwTS.Model, controllerAwTS.FwRev,
				controllerAwTS.SocketID, humanize.Bytes(controllerAwTS.Capacity()), ttStr,
				controllerAwTS.HealthStats.TempK(), controllerAwTS.HealthStats.TempC(),
				controllerAwTS.HealthStats.TempWarnTime, controllerAwTS.HealthStats.TempCritTime,
				controllerAwTS.HealthStats.CtrlBusyTime, controllerAwTS.HealthStats.PowerCycles,
				time.Duration(controllerAwTS.HealthStats.PowerOnHours)*time.Hour,
				controllerAwTS.HealthStats.UnsafeShutdowns, controllerAwTS.HealthStats.MediaErrors,
				controllerAwTS.HealthStats.ReadErrors, controllerAwTS.HealthStats.WriteErrors,
				controllerAwTS.HealthStats.UnmapErrors, controllerAwTS.HealthStats.ChecksumErrors,
				controllerAwTS.HealthStats.ErrorLogEntries,
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintNvmeHealthMap(tc.hsm, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected print output (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintNVMetaMap(t *testing.T) {
	var (
		controllerA = storage.MockNvmeController(1)
		controllerB = storage.MockNvmeController(2)
		controllerC = storage.MockNvmeController(1)
		controllerD = storage.MockNvmeController(2)
		controllerE = storage.MockNvmeController(1)
		controllerF = storage.MockNvmeController(2)
	)
	controllerA.SmdDevices = nil
	controllerB.SmdDevices = nil
	controllerE.SmdDevices = []*storage.SmdDevice{
		{
			UUID:      common.MockUUID(0),
			TargetIDs: []int32{0, 1, 2},
			Rank:      0,
			State:     "NORMAL",
		},
		{
			UUID:      common.MockUUID(1),
			TargetIDs: []int32{3, 4, 5},
			Rank:      0,
			State:     "FAULTY",
		},
	}
	controllerF.SmdDevices = []*storage.SmdDevice{
		{
			UUID:      common.MockUUID(2),
			TargetIDs: []int32{6, 7, 8},
			Rank:      1,
			State:     "NORMAL",
		},
		{
			UUID:      common.MockUUID(3),
			TargetIDs: []int32{9, 10, 11},
			Rank:      1,
			State:     "FAULTY",
		},
	}
	for name, tc := range map[string]struct {
		hsm         control.HostStorageMap
		expPrintStr string
	}{
		"no controllers": {
			hsm: mockHostStorageMap(t, &mockHostStorage{"host1", &control.HostStorage{}}),
			expPrintStr: `
-----
host1
-----
  No NVMe devices detected
`,
		},
		"no smd devices on controllers": {
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
  No SMD devices found

PCI:%s Model:%s FW:%s Socket:%d Capacity:%s
  No SMD devices found

`,
				controllerA.PciAddr, controllerA.Model, controllerA.FwRev,
				controllerA.SocketID, humanize.Bytes(controllerA.Capacity()),
				controllerB.PciAddr, controllerB.Model, controllerB.FwRev,
				controllerB.SocketID, humanize.Bytes(controllerB.Capacity())),
		},
		"single smd device on each controller": {
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						NvmeDevices: storage.NvmeControllers{
							controllerC,
							controllerD,
						},
					},
				},
			),
			expPrintStr: fmt.Sprintf(`
-----
host1
-----
PCI:%s Model:%s FW:%s Socket:%d Capacity:%s
  SMD Devices
    UUID:%s Targets:%v Rank:%d State:%s

PCI:%s Model:%s FW:%s Socket:%d Capacity:%s
  SMD Devices
    UUID:%s Targets:%v Rank:%d State:%s

`,
				controllerC.PciAddr, controllerC.Model, controllerC.FwRev,
				controllerC.SocketID, humanize.Bytes(controllerC.Capacity()),
				controllerC.SmdDevices[0].UUID, controllerC.SmdDevices[0].TargetIDs,
				controllerC.SmdDevices[0].Rank, controllerC.SmdDevices[0].State,

				controllerD.PciAddr, controllerD.Model, controllerD.FwRev,
				controllerD.SocketID, humanize.Bytes(controllerD.Capacity()),
				controllerD.SmdDevices[0].UUID, controllerD.SmdDevices[0].TargetIDs,
				controllerD.SmdDevices[0].Rank, controllerD.SmdDevices[0].State),
		},
		"multiple smd devices on each controller": {
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						NvmeDevices: storage.NvmeControllers{
							controllerE,
							controllerF,
						},
					},
				},
			),
			expPrintStr: fmt.Sprintf(`
-----
host1
-----
PCI:%s Model:%s FW:%s Socket:%d Capacity:%s
  SMD Devices
    UUID:%s Targets:%v Rank:%d State:%s
    UUID:%s Targets:%v Rank:%d State:%s

PCI:%s Model:%s FW:%s Socket:%d Capacity:%s
  SMD Devices
    UUID:%s Targets:%v Rank:%d State:%s
    UUID:%s Targets:%v Rank:%d State:%s

`,
				controllerE.PciAddr, controllerE.Model, controllerE.FwRev,
				controllerE.SocketID, humanize.Bytes(controllerE.Capacity()),
				controllerE.SmdDevices[0].UUID, controllerE.SmdDevices[0].TargetIDs,
				controllerE.SmdDevices[0].Rank, controllerE.SmdDevices[0].State,
				controllerE.SmdDevices[1].UUID, controllerE.SmdDevices[1].TargetIDs,
				controllerE.SmdDevices[1].Rank, controllerE.SmdDevices[1].State,

				controllerF.PciAddr, controllerF.Model, controllerF.FwRev,
				controllerF.SocketID, humanize.Bytes(controllerF.Capacity()),
				controllerF.SmdDevices[0].UUID, controllerF.SmdDevices[0].TargetIDs,
				controllerF.SmdDevices[0].Rank, controllerF.SmdDevices[0].State,
				controllerF.SmdDevices[1].UUID, controllerF.SmdDevices[1].TargetIDs,
				controllerF.SmdDevices[1].Rank, controllerF.SmdDevices[1].State),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintNvmeMetaMap(tc.hsm, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected print output (-want, +got):\n%s\n", diff)
			}
		})
	}
}
