//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestPretty_PrintNVMeController(t *testing.T) {
	ctrlrWithSmd := func(idx int32, roleBits int) *storage.NvmeController {
		c := storage.MockNvmeController(idx)
		sd := storage.MockSmdDevice(nil, idx)
		sd.Roles = storage.BdevRoles{storage.OptionBits(roleBits)}
		sd.Rank = ranklist.Rank(idx)
		c.SmdDevices = []*storage.SmdDevice{sd}
		return c
	}
	ctrlrWithNilRank := func(idx int32) *storage.NvmeController {
		c := ctrlrWithSmd(idx, 0)
		c.SmdDevices[0].Rank = ranklist.NilRank
		return c
	}
	for name, tc := range map[string]struct {
		devices     storage.NvmeControllers
		expPrintStr string
	}{
		"multiple controllers": {
			devices: storage.NvmeControllers{
				storage.MockNvmeController(1),
				storage.MockNvmeController(2),
			},
			expPrintStr: `
NVMe PCI     Model   FW Revision Socket Capacity Role(s) Rank 
--------     -----   ----------- ------ -------- ------- ---- 
0000:01:00.0 model-1 fwRev-1     1      2.0 TB   NA      None 
0000:02:00.0 model-2 fwRev-2     0      2.0 TB   NA      None 
`,
		},
		"vmd backing devices": {
			devices: storage.NvmeControllers{
				&storage.NvmeController{PciAddr: "050505:01:00.0"},
				&storage.NvmeController{PciAddr: "050505:03:00.0"},
			},
			expPrintStr: `
NVMe PCI       Model FW Revision Socket Capacity Role(s) Rank 
--------       ----- ----------- ------ -------- ------- ---- 
050505:01:00.0                   0      0 B      NA      None 
050505:03:00.0                   0      0 B      NA      None 
`,
		},
		"controllers with roles": {
			devices: storage.NvmeControllers{
				ctrlrWithSmd(1, 1),
				ctrlrWithSmd(2, 6),
			},
			expPrintStr: `
NVMe PCI     Model   FW Revision Socket Capacity Role(s)  Rank 
--------     -----   ----------- ------ -------- -------  ---- 
0000:01:00.0 model-1 fwRev-1     1      2.0 TB   data     1    
0000:02:00.0 model-2 fwRev-2     0      2.0 TB   meta,wal 2    
`,
		},
		"controllers with no roles": {
			devices: storage.NvmeControllers{
				ctrlrWithSmd(1, 0),
				ctrlrWithSmd(2, 0),
			},
			expPrintStr: `
NVMe PCI     Model   FW Revision Socket Capacity Role(s) Rank 
--------     -----   ----------- ------ -------- ------- ---- 
0000:01:00.0 model-1 fwRev-1     1      2.0 TB   NA      1    
0000:02:00.0 model-2 fwRev-2     0      2.0 TB   NA      2    
`,
		},
		"controllers with no rank": {
			devices: storage.NvmeControllers{
				ctrlrWithNilRank(1),
				ctrlrWithNilRank(2),
			},
			expPrintStr: `
NVMe PCI     Model   FW Revision Socket Capacity Role(s) Rank 
--------     -----   ----------- ------ -------- ------- ---- 
0000:01:00.0 model-1 fwRev-1     1      2.0 TB   NA      None 
0000:02:00.0 model-2 fwRev-2     0      2.0 TB   NA      None 
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintNvmeControllers(tc.devices, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected print output (-want, +got):\n%s\n", diff)
			}
		})
	}
}

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
	ttStr := getTimestampString(tt)

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
  Intel Vendor SMART Attributes:
    Program Fail Count:
       Normalized:%d%s
       Raw:%d
    Erase Fail Count:
       Normalized:%d%s
       Raw:%d
    Wear Leveling Count:
       Normalized:%d%s
       Min:%d
       Max:%d
       Avg:%d
    End-to-End Error Detection Count:%d
    CRC Error Count:%d
    Timed Workload, Media Wear:%d
    Timed Workload, Host Read/Write Ratio:%d
    Timed Workload, Timer:%d
    Thermal Throttle Status:%d%s
    Thermal Throttle Event Count:%d
    Retry Buffer Overflow Counter:%d
    PLL Lock Loss Count:%d
    NAND Bytes Written:%d
    Host Bytes Written:%d
  PCIe Link Info:
    Capabilities: %s
    Control: %s
    Status: %s

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
  Intel Vendor SMART Attributes:
    Program Fail Count:
       Normalized:%d%s
       Raw:%d
    Erase Fail Count:
       Normalized:%d%s
       Raw:%d
    Wear Leveling Count:
       Normalized:%d%s
       Min:%d
       Max:%d
       Avg:%d
    End-to-End Error Detection Count:%d
    CRC Error Count:%d
    Timed Workload, Media Wear:%d
    Timed Workload, Host Read/Write Ratio:%d
    Timed Workload, Timer:%d
    Thermal Throttle Status:%d%s
    Thermal Throttle Event Count:%d
    Retry Buffer Overflow Counter:%d
    PLL Lock Loss Count:%d
    NAND Bytes Written:%d
    Host Bytes Written:%d
  PCIe Link Info:
    Capabilities: %s
    Control: %s
    Status: %s

`,
				controllerA.PciAddr, controllerA.Model, controllerA.FwRev,
				controllerA.SocketID, humanize.Bytes(controllerA.Capacity()),
				controllerA.HealthStats.TempK(), controllerA.HealthStats.TempC(),
				controllerA.HealthStats.TempWarnTime, controllerA.HealthStats.TempCritTime,
				controllerA.HealthStats.CtrlBusyTime, controllerA.HealthStats.PowerCycles,
				time.Duration(controllerA.HealthStats.PowerOnHours)*time.Hour,
				controllerA.HealthStats.UnsafeShutdowns, controllerA.HealthStats.MediaErrors,
				controllerA.HealthStats.ErrorLogEntries,
				controllerA.HealthStats.ProgFailCntNorm, "%", controllerA.HealthStats.ProgFailCntRaw,
				controllerA.HealthStats.EraseFailCntNorm, "%", controllerA.HealthStats.EraseFailCntRaw,
				controllerA.HealthStats.WearLevelingCntNorm, "%", controllerA.HealthStats.WearLevelingCntMin,
				controllerA.HealthStats.WearLevelingCntMax, controllerA.HealthStats.WearLevelingCntAvg,
				controllerA.HealthStats.EndtoendErrCntRaw, controllerA.HealthStats.CrcErrCntRaw,
				controllerA.HealthStats.MediaWearRaw, controllerA.HealthStats.HostReadsRaw,
				controllerA.HealthStats.WorkloadTimerRaw,
				controllerA.HealthStats.ThermalThrottleStatus, "%", controllerA.HealthStats.ThermalThrottleEventCnt,
				controllerA.HealthStats.RetryBufferOverflowCnt,
				controllerA.HealthStats.PllLockLossCnt,
				controllerA.HealthStats.NandBytesWritten, controllerA.HealthStats.HostBytesWritten,
				controllerA.HealthStats.LnkCap, controllerA.HealthStats.LnkCtl,
				controllerA.HealthStats.LnkSta,

				controllerB.PciAddr, controllerB.Model, controllerB.FwRev, controllerB.SocketID,
				humanize.Bytes(controllerB.Capacity()),
				controllerB.HealthStats.TempK(), controllerB.HealthStats.TempC(),
				controllerB.HealthStats.TempWarnTime, controllerB.HealthStats.TempCritTime,
				controllerB.HealthStats.CtrlBusyTime, controllerB.HealthStats.PowerCycles,
				time.Duration(controllerB.HealthStats.PowerOnHours)*time.Hour,
				controllerB.HealthStats.UnsafeShutdowns, controllerB.HealthStats.MediaErrors,
				controllerB.HealthStats.ErrorLogEntries,
				controllerB.HealthStats.ProgFailCntNorm, "%", controllerB.HealthStats.ProgFailCntRaw,
				controllerB.HealthStats.EraseFailCntNorm, "%", controllerB.HealthStats.EraseFailCntRaw,
				controllerB.HealthStats.WearLevelingCntNorm, "%", controllerB.HealthStats.WearLevelingCntMin,
				controllerB.HealthStats.WearLevelingCntMax, controllerB.HealthStats.WearLevelingCntAvg,
				controllerB.HealthStats.EndtoendErrCntRaw, controllerB.HealthStats.CrcErrCntRaw,
				controllerB.HealthStats.MediaWearRaw, controllerB.HealthStats.HostReadsRaw,
				controllerB.HealthStats.WorkloadTimerRaw,
				controllerB.HealthStats.ThermalThrottleStatus, "%", controllerB.HealthStats.ThermalThrottleEventCnt,
				controllerB.HealthStats.RetryBufferOverflowCnt,
				controllerB.HealthStats.PllLockLossCnt,
				controllerB.HealthStats.NandBytesWritten, controllerB.HealthStats.HostBytesWritten,
				controllerA.HealthStats.LnkCap, controllerA.HealthStats.LnkCtl,
				controllerA.HealthStats.LnkSta,
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
  Intel Vendor SMART Attributes:
    Program Fail Count:
       Normalized:%d%s
       Raw:%d
    Erase Fail Count:
       Normalized:%d%s
       Raw:%d
    Wear Leveling Count:
       Normalized:%d%s
       Min:%d
       Max:%d
       Avg:%d
    End-to-End Error Detection Count:%d
    CRC Error Count:%d
    Timed Workload, Media Wear:%d
    Timed Workload, Host Read/Write Ratio:%d
    Timed Workload, Timer:%d
    Thermal Throttle Status:%d%s
    Thermal Throttle Event Count:%d
    Retry Buffer Overflow Counter:%d
    PLL Lock Loss Count:%d
    NAND Bytes Written:%d
    Host Bytes Written:%d
  PCIe Link Info:
    Capabilities: %s
    Control: %s
    Status: %s

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
				controllerAwTS.HealthStats.ProgFailCntNorm, "%", controllerAwTS.HealthStats.ProgFailCntRaw,
				controllerAwTS.HealthStats.EraseFailCntNorm, "%", controllerAwTS.HealthStats.EraseFailCntRaw,
				controllerAwTS.HealthStats.WearLevelingCntNorm, "%", controllerAwTS.HealthStats.WearLevelingCntMin,
				controllerAwTS.HealthStats.WearLevelingCntMax, controllerAwTS.HealthStats.WearLevelingCntAvg,
				controllerAwTS.HealthStats.EndtoendErrCntRaw, controllerAwTS.HealthStats.CrcErrCntRaw,
				controllerAwTS.HealthStats.MediaWearRaw, controllerAwTS.HealthStats.HostReadsRaw,
				controllerAwTS.HealthStats.WorkloadTimerRaw,
				controllerAwTS.HealthStats.ThermalThrottleStatus, "%", controllerAwTS.HealthStats.ThermalThrottleEventCnt,
				controllerAwTS.HealthStats.RetryBufferOverflowCnt,
				controllerAwTS.HealthStats.PllLockLossCnt,
				controllerAwTS.HealthStats.NandBytesWritten, controllerAwTS.HealthStats.HostBytesWritten,
				controllerA.HealthStats.LnkCap, controllerA.HealthStats.LnkCtl,
				controllerA.HealthStats.LnkSta,
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
