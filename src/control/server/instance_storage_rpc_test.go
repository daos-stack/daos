//
// (C) Copyright 2023-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func TestIOEngineInstance_populateCtrlrHealth(t *testing.T) {
	mockLspciOut := `01:00.0 Non-Volatile memory controller: Intel Corporation PCIe Data Center SSD (rev 01) (prog-if 02 [NVM Express])
        Subsystem: Hewlett Packard Enterprise Device 00a8
        Control: I/O- Mem+ BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx+
        Status: Cap+ 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-
        Latency: 0
        Interrupt: pin A routed to IRQ 0
        Region 0: Memory at bc000000 (64-bit, non-prefetchable)
        Capabilities: [40] Power Management version 3
                Flags: PMEClk- DSI- D1- D2- AuxCurrent=0mA PME(D0-,D1-,D2-,D3hot-,D3cold-)
                Status: D0 NoSoftRst+ PME-Enable- DSel=0 DScale=0 PME-
        Capabilities: [50] MSI-X: Enable- Count=32 Masked-
                Vector table: BAR=0 offset=00002000
                PBA: BAR=0 offset=00003000
        Capabilities: [60] Express (v2) Endpoint, MSI 00
                DevCap: MaxPayload 256 bytes, PhantFunc 0, Latency L0s <4us, L1 <4us
                        ExtTag+ AttnBtn- AttnInd- PwrInd- RBE+ FLReset+ SlotPowerLimit 0.000W
                DevCtl: CorrErr- NonFatalErr- FatalErr- UnsupReq-
                        RlxdOrd+ ExtTag+ PhantFunc- AuxPwr- NoSnoop+ FLReset-
                        MaxPayload 128 bytes, MaxReadReq 512 bytes
                DevSta: CorrErr+ NonFatalErr- FatalErr- UnsupReq+ AuxPwr- TransPend-
                LnkCap: Port #0, Speed 8GT/s, Width x4, ASPM L0s L1, Exit Latency L0s <4us, L1 <4us
                        ClockPM- Surprise- LLActRep- BwNot- ASPMOptComp+
                LnkCtl: ASPM Disabled; RCB 64 bytes, Disabled- CommClk-
                        ExtSynch- ClockPM- AutWidDis- BWInt- AutBWInt-
                LnkSta: Speed 8GT/s (ok), Width x4 (ok)
                        TrErr- Train- SlotClk- DLActive- BWMgmt- ABWMgmt-
                DevCap2: Completion Timeout: Range ABCD, TimeoutDis+ NROPrPrP- LTR-
                         10BitTagComp- 10BitTagReq- OBFF Not Supported, ExtFmt- EETLPPrefix-
                         EmergencyPowerReduction Not Supported, EmergencyPowerReductionInit-
                         FRS- TPHComp- ExtTPHComp-
                DevCtl2: Completion Timeout: 50us to 50ms, TimeoutDis- LTR- OBFF Disabled,
                         AtomicOpsCtl: ReqEn-
                LnkCap2: Supported Link Speeds: 2.5-8GT/s, Crosslink- Retimer- 2Retimers- DRS-
                LnkCtl2: Target Link Speed: 8GT/s, EnterCompliance- SpeedDis-
                         Transmit Margin: Normal Operating Range, EnterModifiedCompliance- ComplianceSOS-
                         Compliance De-emphasis: -6dB
                LnkSta2: Current De-emphasis Level: -3.5dB, EqualizationComplete+ EqualizationPhase1+
                         EqualizationPhase2+ EqualizationPhase3+ LinkEqualizationRequest-
                         Retimer- 2Retimers- CrosslinkRes: unsupported
`
	healthWithoutLinkStats := func() *ctlpb.BioHealthResp {
		bhr := proto.MockNvmeHealth()
		bhr.LnkSta = ""
		bhr.LnkCap = ""
		bhr.LnkCtl = ""

		return bhr
	}

	for name, tc := range map[string]struct {
		inDevState ctlpb.NvmeDevState
		lspciOut   string
		lspciErr   error
		healthRes  *ctlpb.BioHealthResp
		healthErr  error
		expCtrlr   *ctlpb.NvmeController
		expUpdated bool
		expErr     error
	}{
		"bad state; skip health": {
			healthRes: healthWithoutLinkStats(),
			expCtrlr:  &ctlpb.NvmeController{},
		},
		"good state; update health": {
			inDevState: ctlpb.NvmeDevState_NORMAL,
			healthRes:  healthWithoutLinkStats(),
			expCtrlr: &ctlpb.NvmeController{
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: healthWithoutLinkStats(),
			},
			expUpdated: true,
		},
		"update health; add link stats; invalid lspci output": {
			inDevState: ctlpb.NvmeDevState_NORMAL,
			lspciOut:   "nothing to see here",
			healthRes:  healthWithoutLinkStats(),
			expCtrlr: &ctlpb.NvmeController{
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: healthWithoutLinkStats(),
			},
			expUpdated: true,
		},
		"update health; add link stats": {
			inDevState: ctlpb.NvmeDevState_NORMAL,
			lspciOut:   mockLspciOut,
			healthRes:  healthWithoutLinkStats(),
			expCtrlr: &ctlpb.NvmeController{
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: proto.MockNvmeHealth(),
			},
			expUpdated: true,
		},
		"update health; add link stats; lspci fails": {
			inDevState: ctlpb.NvmeDevState_NORMAL,
			lspciErr:   errors.New("fail"),
			healthRes:  healthWithoutLinkStats(),
			expErr:     errors.New("fail"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			getCtrlrHealth = func(_ context.Context, _ Engine, _ *ctlpb.BioHealthReq) (*ctlpb.BioHealthResp, error) {
				return tc.healthRes, tc.healthErr
			}
			defer func() {
				getCtrlrHealth = getBioHealth
			}()

			sysCfg := system.MockSysConfig{
				RunLspciWithInputOut: tc.lspciOut,
				RunLspciWithInputErr: tc.lspciErr,
			}
			cs := newMockControlServiceFromBackends(t, log,
				config.DefaultServer().WithEngines(engine.MockConfig()),
				nil, nil, &sysCfg)
			ei := cs.harness.Instances()[0].(*EngineInstance)
			ctrlr := &ctlpb.NvmeController{
				PciCfg:   tc.lspciOut,
				DevState: tc.inDevState,
			}

			upd, err := populateCtrlrHealth(test.Context(t), ei, &ctlpb.BioHealthReq{},
				ctrlr)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expCtrlr, ctrlr,
				defStorageScanCmpOpts...); diff != "" {
				t.Fatalf("unexpected controller output (-want, +got):\n%s\n", diff)
			}
			test.AssertEqual(t, tc.expUpdated, upd, "")
		})
	}
}

func TestIOEngineInstance_bdevScanEngine(t *testing.T) {
	c := storage.MockNvmeController(2)
	withState := func(ctrlr *ctlpb.NvmeController, state ctlpb.NvmeDevState) *ctlpb.NvmeController {
		ctrlr.DevState = state
		ctrlr.HealthStats = nil
		// scanEngineBdevsOverDrpc will always populate RoleBits in ctrlr.SmdDevices
		ctrlr.SmdDevices = []*ctlpb.SmdDevice{{RoleBits: 7}}
		return ctrlr
	}
	withDevState := func(smd *ctlpb.SmdDevice, state ctlpb.NvmeDevState) *ctlpb.SmdDevice {
		smd.Ctrlr.DevState = state
		return smd
	}
	defSmdScanRes := func() *ctlpb.SmdDevResp {
		sd := proto.MockSmdDevice(c, 2)
		return &ctlpb.SmdDevResp{Devices: []*ctlpb.SmdDevice{sd}}
	}
	healthRespWithUsage := func() *ctlpb.BioHealthResp {
		mh := proto.MockNvmeHealth(2)
		mh.TotalBytes, mh.AvailBytes, mh.ClusterSize = 1, 2, 3
		mh.MetaWalSize, mh.RdbWalSize = 4, 5
		return mh
	}

	for name, tc := range map[string]struct {
		req                 ctlpb.ScanNvmeReq
		bdevAddrs           []string
		rank                int
		provRes             *storage.BdevScanResponse
		provErr             error
		engStopped          bool
		smdRes              *ctlpb.SmdDevResp
		smdErr              error
		healthRes           *ctlpb.BioHealthResp
		healthErr           error
		expResp             *ctlpb.ScanNvmeResp
		expErr              error
		expBackendScanCalls []storage.BdevScanRequest
	}{
		"no bdevs in cfg": {
			bdevAddrs: []string{},
			expErr:    errors.New("empty device list"),
		},
		"engines stopped; scan over engine provider": {
			bdevAddrs:  []string{test.MockPCIAddr(1), test.MockPCIAddr(2)},
			engStopped: true,
			provRes: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
				},
			},
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(1)
						c.SmdDevices = []*ctlpb.SmdDevice{
							{Rank: uint32(ranklist.NilRank)},
						}
						return c
					}(),
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.SmdDevices = []*ctlpb.SmdDevice{
							{Rank: uint32(ranklist.NilRank)},
						}
						return c
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
			expBackendScanCalls: []storage.BdevScanRequest{
				{
					DeviceList: storage.MustNewBdevDeviceList(
						test.MockPCIAddr(1), test.MockPCIAddr(2)),
				},
			},
		},
		"engines stopped; scan over engine provider; retry on empty response": {
			bdevAddrs:  []string{test.MockPCIAddr(1), test.MockPCIAddr(2)},
			engStopped: true,
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(1)
						c.SmdDevices = []*ctlpb.SmdDevice{
							{Rank: uint32(ranklist.NilRank)},
						}
						return c
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
			expBackendScanCalls: []storage.BdevScanRequest{
				{
					DeviceList: storage.MustNewBdevDeviceList(
						test.MockPCIAddr(1), test.MockPCIAddr(2)),
				},
				{
					DeviceList: storage.MustNewBdevDeviceList(
						test.MockPCIAddr(1), test.MockPCIAddr(2)),
				},
			},
		},
		"engines stopped; scan fails over engine provider": {
			engStopped: true,
			provErr:    errors.New("provider scan fail"),
			expErr:     errors.New("provider scan fail"),
		},
		"engines stopped; scan over engine provider; vmd enabled": {
			bdevAddrs:  []string{"0000:05:05.5"},
			engStopped: true,
			provRes: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{
					&storage.NvmeController{
						PciAddr:   "050505:01:00.0",
						NvmeState: storage.NvmeStateNormal,
					},
					&storage.NvmeController{
						PciAddr:   "050505:03:00.0",
						NvmeState: storage.NvmeStateNormal,
					},
				},
			},
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						nc := &ctlpb.NvmeController{
							PciAddr:  "050505:01:00.0",
							DevState: ctlpb.NvmeDevState_NORMAL,
						}
						nc.SmdDevices = []*ctlpb.SmdDevice{
							{Rank: uint32(ranklist.NilRank)},
						}
						return nc
					}(),
					func() *ctlpb.NvmeController {
						nc := &ctlpb.NvmeController{
							PciAddr:  "050505:03:00.0",
							DevState: ctlpb.NvmeDevState_NORMAL,
						}
						nc.SmdDevices = []*ctlpb.SmdDevice{
							{Rank: uint32(ranklist.NilRank)},
						}
						return nc
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
			expBackendScanCalls: []storage.BdevScanRequest{
				{DeviceList: storage.MustNewBdevDeviceList("0000:05:05.5")},
			},
		},
		"scan fails over drpc": {
			smdErr: errors.New("drpc fail"),
			expErr: errors.New("drpc fail"),
		},
		"scan over drpc; no req flags; rank and roles populated": {
			req:    ctlpb.ScanNvmeReq{},
			rank:   1,
			smdRes: defSmdScanRes(),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.HealthStats = nil
						c.SmdDevices = []*ctlpb.SmdDevice{
							{Rank: 1, RoleBits: storage.BdevRoleAll},
						}
						return c
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan over drpc; no req flags; invalid rank": {
			req:    ctlpb.ScanNvmeReq{},
			rank:   -1,
			smdRes: defSmdScanRes(),
			expErr: errors.New("nil superblock"),
		},
		"scan over drpc; with health": {
			req:       ctlpb.ScanNvmeReq{Health: true},
			rank:      1,
			smdRes:    defSmdScanRes(),
			healthRes: healthRespWithUsage(),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.HealthStats = healthRespWithUsage()
						c.SmdDevices = []*ctlpb.SmdDevice{
							{Rank: 1, RoleBits: storage.BdevRoleAll},
						}
						return c
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan over drpc; with meta": {
			req:       ctlpb.ScanNvmeReq{Meta: true},
			rank:      1,
			smdRes:    defSmdScanRes(),
			healthRes: healthRespWithUsage(),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.HealthStats = nil
						sd := proto.MockSmdDevice(nil, 2)
						sd.Rank = 1
						c.SmdDevices = []*ctlpb.SmdDevice{sd}
						return c
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan over drpc; with smd and health; usage and wal size reported": {
			req:       ctlpb.ScanNvmeReq{Meta: true, Health: true},
			rank:      1,
			smdRes:    defSmdScanRes(),
			healthRes: healthRespWithUsage(),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					func() *ctlpb.NvmeController {
						c := proto.MockNvmeController(2)
						c.HealthStats = healthRespWithUsage()
						sd := proto.MockSmdDevice(nil, 2)
						sd.Rank = 1
						sd.TotalBytes = c.HealthStats.TotalBytes
						sd.AvailBytes = c.HealthStats.AvailBytes
						sd.ClusterSize = c.HealthStats.ClusterSize
						sd.MetaWalSize = c.HealthStats.MetaWalSize
						sd.RdbWalSize = c.HealthStats.RdbWalSize
						c.SmdDevices = []*ctlpb.SmdDevice{sd}
						return c
					}(),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan over drpc; only ctrlrs with valid states shown": {
			req: ctlpb.ScanNvmeReq{},
			bdevAddrs: []string{
				test.MockPCIAddr(1), test.MockPCIAddr(2),
				test.MockPCIAddr(1), test.MockPCIAddr(2),
				test.MockPCIAddr(5),
			},
			smdRes: &ctlpb.SmdDevResp{
				Devices: proto.SmdDevices{
					withDevState(proto.MockSmdDevice(
						storage.MockNvmeController(1), 1),
						ctlpb.NvmeDevState_UNPLUGGED),
					withDevState(proto.MockSmdDevice(
						storage.MockNvmeController(2), 2),
						ctlpb.NvmeDevState_UNKNOWN),
					withDevState(proto.MockSmdDevice(
						storage.MockNvmeController(3), 3),
						ctlpb.NvmeDevState_NORMAL),
					withDevState(proto.MockSmdDevice(
						storage.MockNvmeController(4), 4),
						ctlpb.NvmeDevState_NEW),
					withDevState(proto.MockSmdDevice(
						storage.MockNvmeController(5), 5),
						ctlpb.NvmeDevState_EVICTED),
				},
			},
			healthRes: healthRespWithUsage(),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{
					withState(proto.MockNvmeController(3),
						ctlpb.NvmeDevState_NORMAL),
					withState(proto.MockNvmeController(4),
						ctlpb.NvmeDevState_NEW),
					withState(proto.MockNvmeController(5),
						ctlpb.NvmeDevState_EVICTED),
				},
				State: new(ctlpb.ResponseState),
			},
		},
		"scan over drpc; with smd and health; missing ctrlr in smd": {
			req: ctlpb.ScanNvmeReq{Meta: true, Health: true},
			smdRes: func() *ctlpb.SmdDevResp {
				ssr := defSmdScanRes()
				ssr.Devices[0].Ctrlr = nil
				return ssr
			}(),
			healthRes: healthRespWithUsage(),
			expErr:    errors.New("no ctrlr ref"),
		},
		"scan over drpc; with smd and health; health scan fails": {
			req:       ctlpb.ScanNvmeReq{Meta: true, Health: true},
			smdRes:    defSmdScanRes(),
			healthErr: errors.New("health scan failed"),
			expErr:    errors.New("health scan failed"),
		},
		"scan over drpc; with smd and health; smd list fails": {
			req:       ctlpb.ScanNvmeReq{Meta: true, Health: true},
			smdErr:    errors.New("smd scan failed"),
			healthRes: healthRespWithUsage(),
			expErr:    errors.New("smd scan failed"),
		},
		"scan over drpc; with smd and health; nil smd list returned": {
			req:       ctlpb.ScanNvmeReq{Meta: true, Health: true},
			healthRes: healthRespWithUsage(),
			expErr:    errors.New("nil smd scan resp"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			scanSmd = func(_ context.Context, _ Engine, _ *ctlpb.SmdDevReq) (*ctlpb.SmdDevResp, error) {
				return tc.smdRes, tc.smdErr
			}
			defer func() {
				scanSmd = listSmdDevices
			}()
			getCtrlrHealth = func(_ context.Context, _ Engine, _ *ctlpb.BioHealthReq) (*ctlpb.BioHealthResp, error) {
				return tc.healthRes, tc.healthErr
			}
			defer func() {
				getCtrlrHealth = getBioHealth
			}()

			if tc.provRes == nil {
				tc.provRes = defProviderScanRes
			}

			ec := engine.MockConfig()
			if tc.bdevAddrs == nil {
				tc.bdevAddrs = []string{test.MockPCIAddr(1)}
			}
			ec.WithStorage(storage.NewTierConfig().
				WithStorageClass(storage.ClassNvme.String()).
				WithBdevDeviceList(tc.bdevAddrs...))

			sCfg := config.DefaultServer().WithEngines(ec)

			bmbc := &bdev.MockBackendConfig{
				ScanRes: tc.provRes,
				ScanErr: tc.provErr,
			}
			bmb := bdev.NewMockBackend(bmbc)
			smb := scm.NewMockBackend(nil)

			cs := newMockControlServiceFromBackends(t, log, sCfg, bmb, smb, nil,
				tc.engStopped)
			ei := cs.harness.Instances()[0].(*EngineInstance)
			if tc.rank < 0 {
				ei.setSuperblock(nil)
			} else {
				ei.setSuperblock(&Superblock{
					Rank: ranklist.NewRankPtr(uint32(tc.rank)), ValidRank: true,
				})
			}

			resp, err := bdevScanEngine(test.Context(t), ei, &tc.req)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, resp,
				defStorageScanCmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}

			cmpopt := cmp.Comparer(func(x, y *storage.BdevDeviceList) bool {
				if x == nil && y == nil {
					return true
				}
				return x.Equals(y)
			})

			bmb.RLock()
			if len(tc.expBackendScanCalls) != len(bmb.ScanCalls) {
				t.Fatalf("unexpected number of backend scan calls, want %d got %d",
					len(tc.expBackendScanCalls), len(bmb.ScanCalls))
			}
			if len(tc.expBackendScanCalls) == 0 {
				return
			}
			if diff := cmp.Diff(tc.expBackendScanCalls, bmb.ScanCalls,
				append(defStorageScanCmpOpts, cmpopt)...); diff != "" {
				t.Fatalf("unexpected backend scan calls (-want, +got):\n%s\n", diff)
			}
			bmb.RUnlock()
		})
	}
}
