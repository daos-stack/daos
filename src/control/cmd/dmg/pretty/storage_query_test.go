//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"errors"
	"fmt"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestPretty_PrintHostStorageUsageMap(t *testing.T) {
	var (
		withSpaceUsage = control.MockServerScanResp(t, "withSpaceUsage")
		noStorage      = control.MockServerScanResp(t, "noStorage")
		bothFailed     = control.MockServerScanResp(t, "bothFailed")
	)

	for name, tc := range map[string]struct {
		mic         *control.MockInvokerConfig
		expPrintStr string
	}{
		"empty response": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{},
			},
		},
		"server error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:  "host1",
							Error: errors.New("failed"),
						},
					},
				},
			},
			expPrintStr: `
Errors:
  Hosts Error  
  ----- -----  
  host1 failed 

`,
		},
		"scm and nvme scan error": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1:1",
							Message: bothFailed,
						},
						{
							Addr:    "host2:1",
							Message: bothFailed,
						},
					},
				},
			},
			expPrintStr: `
Errors:
  Hosts     Error            
  -----     -----            
  host[1-2] nvme scan failed 
  host[1-2] scm scan failed  

Hosts     SCM-Total SCM-Free SCM-Used NVMe-Total NVMe-Free NVMe-Used 
-----     --------- -------- -------- ---------- --------- --------- 
host[1-2] 0 B       0 B      N/A      0 B        0 B       N/A       
`,
		},
		"no storage": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: noStorage,
						},
					},
				},
			},
			expPrintStr: `
Hosts SCM-Total SCM-Free SCM-Used NVMe-Total NVMe-Free NVMe-Used 
----- --------- -------- -------- ---------- --------- --------- 
host1 0 B       0 B      N/A      0 B        0 B       N/A       
`,
		},
		"single host with space usage": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr:    "host1",
							Message: withSpaceUsage,
						},
					},
				},
			},
			expPrintStr: `
Hosts SCM-Total SCM-Free SCM-Used NVMe-Total NVMe-Free NVMe-Used 
----- --------- -------- -------- ---------- --------- --------- 
host1 3.0 TB    750 GB   75 %     36 TB      27 TB     25 %      
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ctx := test.Context(t)
			mi := control.NewMockInvoker(log, tc.mic)

			resp, err := control.StorageScan(ctx, mi, &control.StorageScanReq{})
			if err != nil {
				t.Fatal(err)
			}

			var bld strings.Builder
			if err := PrintResponseErrors(resp, &bld); err != nil {
				t.Fatal(err)
			}
			if err := PrintHostStorageUsageMap(resp.HostStorage, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintSmdInfoMap(t *testing.T) {
	mockController := storage.MockNvmeController(1)
	newCtrlr := storage.NvmeController{
		PciAddr:   "0000:8a:00.0",
		NvmeState: storage.NvmeStateNew,
		LedState:  storage.LedStateNormal,
	}
	identCtrlr := storage.NvmeController{
		PciAddr:   "0000:db:00.0",
		NvmeState: storage.NvmeStateNormal,
		LedState:  storage.LedStateIdentify,
	}
	faultCtrlr := storage.NvmeController{
		PciAddr:   "0000:8b:00.0",
		NvmeState: storage.NvmeStateFaulty,
		LedState:  storage.LedStateFaulty,
	}
	unknoCtrlr := storage.NvmeController{
		PciAddr:  "0000:da:00.0",
		LedState: storage.LedStateUnknown,
	}

	for name, tc := range map[string]struct {
		noDevs      bool
		noPools     bool
		hsm         control.HostStorageMap
		opts        []PrintConfigOption
		expPrintStr string
	}{
		"list-pools (standard)": {
			noDevs: true,
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						SmdInfo: &control.SmdInfo{
							Pools: control.SmdPoolMap{
								test.MockUUID(0): {
									{
										UUID:      test.MockUUID(0),
										Rank:      0,
										TargetIDs: []int32{0, 1, 2, 3},
										Blobs:     []uint64{11, 12, 13, 14},
									},
									{
										UUID:      test.MockUUID(0),
										Rank:      1,
										TargetIDs: []int32{0, 1, 2, 3},
										Blobs:     []uint64{11, 12, 13, 14},
									},
								},
							},
						},
					},
				},
			),
			expPrintStr: `
-----
host1
-----
  Pools
    UUID:00000000-0000-0000-0000-000000000000
      Rank:0 Targets:[0 1 2 3]
      Rank:1 Targets:[0 1 2 3]

`,
		},
		"list-pools (verbose)": {
			noDevs: true,
			opts:   []PrintConfigOption{PrintWithVerboseOutput(true)},
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						SmdInfo: &control.SmdInfo{
							Pools: control.SmdPoolMap{
								test.MockUUID(0): {
									{
										UUID:      test.MockUUID(0),
										Rank:      0,
										TargetIDs: []int32{0, 1, 2, 3},
										Blobs:     []uint64{11, 12, 13, 14},
									},
									{
										UUID:      test.MockUUID(0),
										Rank:      1,
										TargetIDs: []int32{0, 1, 2, 3},
										Blobs:     []uint64{11, 12, 13, 14},
									},
								},
							},
						},
					},
				},
			),
			expPrintStr: `
-----
host1
-----
  Pools
    UUID:00000000-0000-0000-0000-000000000000
      Rank:0 Targets:[0 1 2 3] Blobs:[11 12 13 14]
      Rank:1 Targets:[0 1 2 3] Blobs:[11 12 13 14]

`,
		},
		"list-pools (none found)": {
			noDevs: true,
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						SmdInfo: &control.SmdInfo{},
					},
				},
			),
			expPrintStr: `
-----
host1
-----
  No pools with NVMe found
`,
		},
		"list-devices": {
			noPools: true,
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						SmdInfo: &control.SmdInfo{
							Devices: []*storage.SmdDevice{
								{
									UUID:             test.MockUUID(0),
									TargetIDs:        []int32{0, 1, 2},
									HasSysXS:         true,
									Roles:            storage.BdevRoles{storage.BdevRoleWAL},
									Ctrlr:            newCtrlr,
									CtrlrNamespaceID: 1,
								},
								{
									UUID:             test.MockUUID(1),
									TargetIDs:        []int32{3, 4, 5},
									Roles:            storage.BdevRoles{storage.BdevRoleMeta | storage.BdevRoleData},
									Ctrlr:            faultCtrlr,
									CtrlrNamespaceID: 1,
								},
								{
									UUID:             test.MockUUID(2),
									TargetIDs:        []int32{0, 1, 2},
									Rank:             1,
									HasSysXS:         true,
									Roles:            storage.BdevRoles{storage.BdevRoleWAL},
									Ctrlr:            unknoCtrlr,
									CtrlrNamespaceID: 1,
								},
								{
									UUID:      test.MockUUID(3),
									TargetIDs: []int32{3, 4, 5},
									Rank:      1,
									Roles:     storage.BdevRoles{storage.BdevRoleMeta | storage.BdevRoleData},
									Ctrlr:     identCtrlr,
								},
							},
						},
					},
				},
			),
			expPrintStr: `
-----
host1
-----
  Devices
    UUID:00000000-0000-0000-0000-000000000000 [TrAddr:0000:8a:00.0 NSID:1]
      Roles:wal SysXS Targets:[0 1 2] Rank:0 State:NEW LED:OFF
    UUID:00000001-0001-0001-0001-000000000001 [TrAddr:0000:8b:00.0 NSID:1]
      Roles:data,meta Targets:[3 4 5] Rank:0 State:EVICTED LED:ON
    UUID:00000002-0002-0002-0002-000000000002 [TrAddr:0000:da:00.0 NSID:1]
      Roles:wal SysXS Targets:[0 1 2] Rank:1 State:UNKNOWN LED:NA
    UUID:00000003-0003-0003-0003-000000000003 [TrAddr:0000:db:00.0]
      Roles:data,meta Targets:[3 4 5] Rank:1 State:NORMAL LED:QUICK_BLINK
`,
		},
		"list-devices (none found)": {
			noPools: true,
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						SmdInfo: &control.SmdInfo{},
					},
				},
			),
			expPrintStr: `
-----
host1
-----
  No devices found
`,
		},
		"list-devices; with health": {
			noPools: true,
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						SmdInfo: &control.SmdInfo{
							Devices: []*storage.SmdDevice{
								{
									UUID:             test.MockUUID(0),
									TargetIDs:        []int32{0, 1, 2},
									Rank:             0,
									Ctrlr:            *mockController,
									CtrlrNamespaceID: 1,
									Roles:            storage.BdevRoles{storage.BdevRoleAll},
								},
							},
						},
					},
				},
			),
			expPrintStr: fmt.Sprintf(`
-----
host1
-----
  Devices
    UUID:00000000-0000-0000-0000-000000000000 [TrAddr:0000:01:00.0 NSID:1]
      Roles:data,meta,wal Targets:[0 1 2] Rank:0 State:NORMAL LED:OFF
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
        Port: #1
        Max Speed: 1 GT/s
        Negotiated Speed: 1 GT/s
        Max Width: x4
        Negotiated Width: x4

`,
				mockController.HealthStats.TempK(), mockController.HealthStats.TempC(),
				mockController.HealthStats.TempWarnTime, mockController.HealthStats.TempCritTime,
				mockController.HealthStats.CtrlBusyTime, mockController.HealthStats.PowerCycles,
				time.Duration(mockController.HealthStats.PowerOnHours)*time.Hour,
				mockController.HealthStats.UnsafeShutdowns, mockController.HealthStats.MediaErrors,
				mockController.HealthStats.ErrorLogEntries,
				mockController.HealthStats.ProgFailCntNorm, "%", mockController.HealthStats.ProgFailCntRaw,
				mockController.HealthStats.EraseFailCntNorm, "%", mockController.HealthStats.EraseFailCntRaw,
				mockController.HealthStats.WearLevelingCntNorm, "%", mockController.HealthStats.WearLevelingCntMin,
				mockController.HealthStats.WearLevelingCntMax, mockController.HealthStats.WearLevelingCntAvg,
				mockController.HealthStats.EndtoendErrCntRaw, mockController.HealthStats.CrcErrCntRaw,
				mockController.HealthStats.MediaWearRaw, mockController.HealthStats.HostReadsRaw,
				mockController.HealthStats.WorkloadTimerRaw,
				mockController.HealthStats.ThermalThrottleStatus, "%", mockController.HealthStats.ThermalThrottleEventCnt,
				mockController.HealthStats.RetryBufferOverflowCnt,
				mockController.HealthStats.PllLockLossCnt,
				mockController.HealthStats.NandBytesWritten, mockController.HealthStats.HostBytesWritten,
			),
		},
		"identify led": {
			noPools: true,
			opts:    []PrintConfigOption{PrintOnlyLEDInfo()},
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						SmdInfo: &control.SmdInfo{
							Devices: []*storage.SmdDevice{
								{
									UUID:  "842c739b-86b5-462f-a7ba-b4a91b674f3d",
									Ctrlr: identCtrlr,
								},
							},
						},
					},
				},
			),
			expPrintStr: `
-----
host1
-----
  Devices
    TrAddr:0000:db:00.0 [UUID:842c739b-86b5-462f-a7ba-b4a91b674f3d] LED:QUICK_BLINK
`,
		},
		"identify led; no uuid specified": {
			noPools: true,
			opts:    []PrintConfigOption{PrintOnlyLEDInfo()},
			hsm: mockHostStorageMap(t,
				&mockHostStorage{
					"host1",
					&control.HostStorage{
						SmdInfo: &control.SmdInfo{
							Devices: []*storage.SmdDevice{
								{
									Ctrlr:            identCtrlr,
									CtrlrNamespaceID: 1,
								},
							},
						},
					},
				},
			),
			expPrintStr: `
-----
host1
-----
  Devices
    TrAddr:0000:db:00.0 NSID:1 LED:QUICK_BLINK
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintSmdInfoMap(tc.noDevs, tc.noPools, tc.hsm, &bld, tc.opts...); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected print output (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintSmdManageResp(t *testing.T) {
	for name, tc := range map[string]struct {
		op        control.SmdManageOpcode
		printOpts PrintConfigOption
		resp      *control.SmdResp
		expStdout string
		expStderr string
		expErr    error
	}{
		"bad opcode": {
			resp:   new(control.SmdResp),
			expErr: errors.New("unsupported opcode"),
		},
		"empty response": {
			op:        control.SetFaultyOp,
			resp:      new(control.SmdResp),
			expStdout: ``,
		},
		"server error": {
			op: control.DevReplaceOp,
			resp: &control.SmdResp{
				HostErrorsResp: control.MockHostErrorsResp(t,
					&control.MockHostError{
						Hosts: "host1",
						Error: "failed",
					}),
			},
			expStderr: "dev-replace operation failed on host1: failed\n",
		},
		"one success; one fail": {
			op: control.SetFaultyOp,
			resp: &control.SmdResp{
				HostErrorsResp: control.MockHostErrorsResp(t,
					&control.MockHostError{
						Hosts: "host1",
						Error: "engine-0: drpc fails, engine-1: updated",
					}),
				HostStorage: control.MockHostStorageMap(t,
					&control.MockStorageScan{
						Hosts: "host2",
					}),
			},
			expErr: errors.New("unexpected number of results"),
		},
		"two successes": {
			op: control.DevReplaceOp,
			resp: &control.SmdResp{
				HostStorage: control.MockHostStorageMap(t,
					&control.MockStorageScan{
						Hosts: "host[1-2]",
					}),
			},
			expErr: errors.New("unexpected number of results"),
		},
		"two failures": {
			op: control.SetFaultyOp,
			resp: &control.SmdResp{
				HostErrorsResp: control.MockHostErrorsResp(t,
					&control.MockHostError{
						Hosts: "host[1-2]",
						Error: "engine-0: drpc fails, engine-1: not ready",
					}),
			},
			expErr: errors.New("unexpected number of results"),
		},
		"multiple scan entries in map": {
			op: control.DevReplaceOp,
			resp: &control.SmdResp{
				HostStorage: control.MockHostStorageMap(t,
					&control.MockStorageScan{
						Hosts:    "host[1-2]",
						HostScan: control.MockServerScanResp(t, "standard"),
					},
					&control.MockStorageScan{
						Hosts:    "host[3-4]",
						HostScan: control.MockServerScanResp(t, "noStorage"),
					}),
			},
			expErr: errors.New("unexpected number of results"),
		},
		"single success": {
			op: control.SetFaultyOp,
			resp: &control.SmdResp{
				HostStorage: control.MockHostStorageMap(t,
					&control.MockStorageScan{
						Hosts: "host1",
					}),
			},
			expStdout: "set-faulty operation performed successfully on the following " +
				"host: host1\n",
		},
		"two successes; led-check": {
			op:        control.LedCheckOp,
			printOpts: PrintOnlyLEDInfo(),
			resp: &control.SmdResp{
				HostStorage: func() control.HostStorageMap {
					hsm := make(control.HostStorageMap)
					sd := &storage.SmdDevice{
						UUID: test.MockUUID(1),
						Ctrlr: storage.NvmeController{
							PciAddr:  test.MockPCIAddr(1),
							LedState: storage.LedStateNormal,
						},
					}
					hss := &control.HostStorageSet{
						HostSet: control.MockHostSet(t, "host[1-2]"),
						HostStorage: &control.HostStorage{
							SmdInfo: &control.SmdInfo{
								Devices: []*storage.SmdDevice{sd},
							},
						},
					}
					hk, err := hss.HostStorage.HashKey()
					if err != nil {
						t.Fatal(err)
					}
					hsm[hk] = hss
					return hsm
				}(),
			},
			expStdout: `
---------
host[1-2]
---------
  Devices
    TrAddr:0000:01:00.0 [UUID:00000001-0001-0001-0001-000000000001] LED:OFF
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var out, outErr strings.Builder

			gotErr := PrintSmdManageResp(tc.op, tc.resp, &out, &outErr, tc.printOpts)
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expStdout, "\n"), out.String()); diff != "" {
				t.Fatalf("unexpected print output (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(strings.TrimLeft(tc.expStderr, "\n"), outErr.String()); diff != "" {
				t.Fatalf("unexpected print output (-want, +got):\n%s\n", diff)
			}
		})
	}
}
