//
// (C) Copyright 2023-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"sort"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/testing/protocmp"

	"github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/pciutils"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

type mockPCIeLinkStatsProvider struct {
	pciDev    *hardware.PCIDevice
	pciDevErr error
}

func (mp *mockPCIeLinkStatsProvider) PCIeCapsFromConfig(cfgBytes []byte, dev *hardware.PCIDevice) error {
	if mp.pciDevErr != nil {
		return mp.pciDevErr
	}
	*dev = *mp.pciDev
	return nil
}

func TestIOEngineInstance_populateCtrlrHealth(t *testing.T) {
	healthWithLinkStats := func(maxSpd, spd float32, maxWdt, wdt uint32) *ctlpb.BioHealthResp {
		bhr := proto.MockNvmeHealth()
		bhr.LinkMaxSpeed = maxSpd
		bhr.LinkNegSpeed = spd
		bhr.LinkMaxWidth = maxWdt
		bhr.LinkNegWidth = wdt

		return bhr
	}
	pciAddr := test.MockPCIAddr(1)
	lastStatsMap := func(bhr *ctlpb.BioHealthResp) map[string]*ctlpb.BioHealthResp {
		return map[string]*ctlpb.BioHealthResp{pciAddr: bhr}
	}

	for name, tc := range map[string]struct {
		badDevState      bool
		nilLinkStatsProv bool
		noPciCfgSpc      bool
		pciDev           *hardware.PCIDevice
		pciDevErr        error
		healthReq        *ctlpb.BioHealthReq
		healthRes        *ctlpb.BioHealthResp
		nilHealthRes     bool
		healthErr        error
		lastStats        map[string]*ctlpb.BioHealthResp
		expCtrlr         *ctlpb.NvmeController
		expNotUpdated    bool
		expErr           error
		expDispatched    []*events.RASEvent
		expLastStats     map[string]*ctlpb.BioHealthResp
	}{
		"bad state; skip health": {
			badDevState: true,
			expCtrlr: &ctlpb.NvmeController{
				PciAddr: pciAddr,
				PciCfg:  "ABCD",
			},
			expNotUpdated: true,
		},
		"update health; add link stats skipped as empty pci config space": {
			noPciCfgSpc: true,
			expCtrlr: &ctlpb.NvmeController{
				PciAddr:     pciAddr,
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: healthWithLinkStats(0, 0, 0, 0),
			},
		},
		"nil bio health response": {
			nilHealthRes: true,
			expErr:       errors.New("nil BioHealthResp"),
		},
		"empty bio health response; empty link stats": {
			healthRes: new(ctlpb.BioHealthResp),
			pciDev:    new(hardware.PCIDevice),
			expCtrlr: &ctlpb.NvmeController{
				PciAddr:     pciAddr,
				PciCfg:      "ABCD",
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: new(ctlpb.BioHealthResp),
			},
			expLastStats: lastStatsMap(new(ctlpb.BioHealthResp)),
		},
		"error retrieving bio health response": {
			healthErr: errors.New("fail"),
			expErr:    errors.New("fail"),
		},
		"update health; add link stats; pciutils lib error": {
			pciDevErr: errors.New("fail"),
			expErr:    errors.New("fail"),
		},
		"update health; add link stats; pciutils lib error; missing pcie caps": {
			pciDevErr: pciutils.ErrNoPCIeCaps,
			expCtrlr: &ctlpb.NvmeController{
				PciAddr:     pciAddr,
				PciCfg:      "ABCD",
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: healthWithLinkStats(0, 0, 0, 0),
			},
		},
		"update health; add link stats; normal link state; no event published": {
			expCtrlr: &ctlpb.NvmeController{
				PciAddr:     pciAddr,
				PciCfg:      "ABCD",
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: proto.MockNvmeHealth(),
			},
			expLastStats: lastStatsMap(proto.MockNvmeHealth()),
		},
		"update health; add link stats; speed downgraded; no last stats": {
			pciDev: &hardware.PCIDevice{
				LinkPortID:   1,
				LinkMaxSpeed: 2.5e+9,
				LinkNegSpeed: 1e+9,
				LinkMaxWidth: 4,
				LinkNegWidth: 4,
			},
			// Stats only exist for different PCI address.
			lastStats: map[string]*ctlpb.BioHealthResp{
				test.MockPCIAddr(2): proto.MockNvmeHealth(),
			},
			expCtrlr: &ctlpb.NvmeController{
				PciAddr:     test.MockPCIAddr(1),
				PciCfg:      "ABCD",
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: healthWithLinkStats(2.5e+9, 1e+9, 4, 4),
			},
			expDispatched: []*events.RASEvent{
				events.NewGenericEvent(events.RASNVMeLinkSpeedChanged,
					events.RASSeverityWarning, "NVMe PCIe device at "+
						"\"0000:01:00.0\" port-1: link speed changed to "+
						"1 GT/s (max 2.5 GT/s)", ""),
			},
			expLastStats: map[string]*ctlpb.BioHealthResp{
				pciAddr:             healthWithLinkStats(2.5e+9, 1e+9, 4, 4),
				test.MockPCIAddr(2): proto.MockNvmeHealth(),
			},
		},
		"update health; add link stats; width downgraded; no last stats": {
			pciDev: &hardware.PCIDevice{
				LinkPortID:   1,
				LinkMaxSpeed: 1e+9,
				LinkNegSpeed: 1e+9,
				LinkMaxWidth: 8,
				LinkNegWidth: 4,
			},
			expCtrlr: &ctlpb.NvmeController{
				PciAddr:     test.MockPCIAddr(1),
				PciCfg:      "ABCD",
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: healthWithLinkStats(1e+9, 1e+9, 8, 4),
			},
			expDispatched: []*events.RASEvent{
				events.NewGenericEvent(events.RASNVMeLinkWidthChanged,
					events.RASSeverityWarning, "NVMe PCIe device at "+
						"\"0000:01:00.0\" port-1: link width changed to "+
						"x4 (max x8)", ""),
			},
			expLastStats: lastStatsMap(healthWithLinkStats(1e+9, 1e+9, 8, 4)),
		},
		"update health; add link stats; link state normal; identical last stats; no event": {
			lastStats: lastStatsMap(proto.MockNvmeHealth()),
			expCtrlr: &ctlpb.NvmeController{
				PciAddr:     test.MockPCIAddr(1),
				PciCfg:      "ABCD",
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: proto.MockNvmeHealth(),
			},
			expLastStats: lastStatsMap(proto.MockNvmeHealth()),
		},
		"update health; add link stats; link state normal; speed degraded in last stats": {
			lastStats: lastStatsMap(healthWithLinkStats(1e+9, 0.5e+9, 4, 4)),
			expCtrlr: &ctlpb.NvmeController{
				PciAddr:     pciAddr,
				PciCfg:      "ABCD",
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: proto.MockNvmeHealth(),
			},
			expDispatched: []*events.RASEvent{
				events.NewGenericEvent(events.RASNVMeLinkSpeedChanged,
					events.RASSeverityNotice, "NVMe PCIe device at "+
						"\"0000:01:00.0\" port-1: link speed changed to "+
						"1 GT/s (max 1 GT/s)", ""),
			},
			expLastStats: lastStatsMap(proto.MockNvmeHealth()),
		},
		"update health; add link stats; link state normal; width degraded in last stats": {
			lastStats: lastStatsMap(healthWithLinkStats(1e+9, 1e+9, 4, 1)),
			expCtrlr: &ctlpb.NvmeController{
				PciAddr:     pciAddr,
				PciCfg:      "ABCD",
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: proto.MockNvmeHealth(),
			},
			expDispatched: []*events.RASEvent{
				events.NewGenericEvent(events.RASNVMeLinkWidthChanged,
					events.RASSeverityNotice, "NVMe PCIe device at "+
						"\"0000:01:00.0\" port-1: link width changed to "+
						"x4 (max x4)", ""),
			},
			expLastStats: lastStatsMap(proto.MockNvmeHealth()),
		},
		"update health; add link stats; speed degraded; speed degraded in last stats; no event": {
			pciDev: &hardware.PCIDevice{
				LinkPortID:   1,
				LinkMaxSpeed: 2.5e+9,
				LinkNegSpeed: 1e+9,
				LinkMaxWidth: 4,
				LinkNegWidth: 4,
			},
			lastStats: lastStatsMap(healthWithLinkStats(2.5e+9, 1e+9, 4, 4)),
			expCtrlr: &ctlpb.NvmeController{
				PciAddr:     test.MockPCIAddr(1),
				PciCfg:      "ABCD",
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: healthWithLinkStats(2.5e+9, 1e+9, 4, 4),
			},
			expLastStats: lastStatsMap(healthWithLinkStats(2.5e+9, 1e+9, 4, 4)),
		},
		"update health; add link stats; width degraded; width degraded in last stats; no event": {
			pciDev: &hardware.PCIDevice{
				LinkPortID:   1,
				LinkMaxSpeed: 1e+9,
				LinkNegSpeed: 1e+9,
				LinkMaxWidth: 8,
				LinkNegWidth: 4,
			},
			lastStats: lastStatsMap(healthWithLinkStats(1e+9, 1e+9, 8, 4)),
			expCtrlr: &ctlpb.NvmeController{
				PciAddr:     test.MockPCIAddr(1),
				PciCfg:      "ABCD",
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: healthWithLinkStats(1e+9, 1e+9, 8, 4),
			},
			expLastStats: lastStatsMap(healthWithLinkStats(1e+9, 1e+9, 8, 4)),
		},
		"update health; add link stats; speed degraded; width degraded in last stats": {
			pciDev: &hardware.PCIDevice{
				LinkPortID:   1,
				LinkMaxSpeed: 2.5e+9,
				LinkNegSpeed: 1e+9,
				LinkMaxWidth: 8,
				LinkNegWidth: 8,
			},
			lastStats: lastStatsMap(healthWithLinkStats(2.5e+9, 2.5e+9, 8, 4)),
			expCtrlr: &ctlpb.NvmeController{
				PciAddr:     test.MockPCIAddr(1),
				PciCfg:      "ABCD",
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: healthWithLinkStats(2.5e+9, 1e+9, 8, 8),
			},
			expDispatched: []*events.RASEvent{
				events.NewGenericEvent(events.RASNVMeLinkSpeedChanged,
					events.RASSeverityWarning, "NVMe PCIe device at "+
						"\"0000:01:00.0\" port-1: link speed changed to "+
						"1 GT/s (max 2.5 GT/s)", ""),
				events.NewGenericEvent(events.RASNVMeLinkWidthChanged,
					events.RASSeverityNotice, "NVMe PCIe device at "+
						"\"0000:01:00.0\" port-1: link width changed to "+
						"x8 (max x8)", ""),
			},
			expLastStats: lastStatsMap(healthWithLinkStats(2.5e+9, 1e+9, 8, 8)),
		},
		"update health; add link stats; width degraded; speed degraded in last stats": {
			pciDev: &hardware.PCIDevice{
				LinkPortID:   1,
				LinkMaxSpeed: 2.5e+9,
				LinkNegSpeed: 2.5e+9,
				LinkMaxWidth: 8,
				LinkNegWidth: 4,
			},
			lastStats: lastStatsMap(healthWithLinkStats(2.5e+9, 1e+9, 8, 8)),
			expCtrlr: &ctlpb.NvmeController{
				PciAddr:     test.MockPCIAddr(1),
				PciCfg:      "ABCD",
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: healthWithLinkStats(2.5e+9, 2.5e+9, 8, 4),
			},
			expDispatched: []*events.RASEvent{
				events.NewGenericEvent(events.RASNVMeLinkSpeedChanged,
					events.RASSeverityNotice, "NVMe PCIe device at "+
						"\"0000:01:00.0\" port-1: link speed changed to "+
						"2.5 GT/s (max 2.5 GT/s)", ""),
				events.NewGenericEvent(events.RASNVMeLinkWidthChanged,
					events.RASSeverityWarning, "NVMe PCIe device at "+
						"\"0000:01:00.0\" port-1: link width changed to "+
						"x4 (max x8)", ""),
			},
			expLastStats: lastStatsMap(healthWithLinkStats(2.5e+9, 2.5e+9, 8, 4)),
		},
		"update health; add link stats; speed degraded; speed diff degraded in last stats": {
			pciDev: &hardware.PCIDevice{
				LinkPortID:   1,
				LinkMaxSpeed: 8e+9,
				LinkNegSpeed: 1e+9,
				LinkMaxWidth: 4,
				LinkNegWidth: 4,
			},
			lastStats: lastStatsMap(healthWithLinkStats(8e+9, 2.5e+9, 4, 4)),
			expCtrlr: &ctlpb.NvmeController{
				PciAddr:     test.MockPCIAddr(1),
				PciCfg:      "ABCD",
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: healthWithLinkStats(8e+9, 1e+9, 4, 4),
			},
			expDispatched: []*events.RASEvent{
				events.NewGenericEvent(events.RASNVMeLinkSpeedChanged,
					events.RASSeverityWarning, "NVMe PCIe device at "+
						"\"0000:01:00.0\" port-1: link speed changed to "+
						"1 GT/s (max 8 GT/s)", ""),
			},
			expLastStats: lastStatsMap(healthWithLinkStats(8e+9, 1e+9, 4, 4)),
		},
		"update health; add link stats; width degraded; width diff degraded in last stats": {
			pciDev: &hardware.PCIDevice{
				LinkPortID:   1,
				LinkMaxSpeed: 1e+9,
				LinkNegSpeed: 1e+9,
				LinkMaxWidth: 16,
				LinkNegWidth: 4,
			},
			lastStats: lastStatsMap(healthWithLinkStats(1e+9, 1e+9, 16, 8)),
			expCtrlr: &ctlpb.NvmeController{
				PciAddr:     test.MockPCIAddr(1),
				PciCfg:      "ABCD",
				DevState:    ctlpb.NvmeDevState_NORMAL,
				HealthStats: healthWithLinkStats(1e+9, 1e+9, 16, 4),
			},
			expDispatched: []*events.RASEvent{
				events.NewGenericEvent(events.RASNVMeLinkWidthChanged,
					events.RASSeverityWarning, "NVMe PCIe device at "+
						"\"0000:01:00.0\" port-1: link width changed to "+
						"x4 (max x16)", ""),
			},
			expLastStats: lastStatsMap(healthWithLinkStats(1e+9, 1e+9, 16, 4)),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			scanHealth = func(_ context.Context, _ Engine, _ *ctlpb.BioHealthReq) (*ctlpb.BioHealthResp, error) {
				return tc.healthRes, tc.healthErr
			}
			defer func() {
				scanHealth = getBioHealth
			}()

			var devState ctlpb.NvmeDevState
			if !tc.badDevState {
				devState = ctlpb.NvmeDevState_NORMAL
			}
			var pciCfgSpc string
			if !tc.noPciCfgSpc {
				pciCfgSpc = "ABCD"
			}
			if tc.pciDev == nil {
				tc.pciDev = &hardware.PCIDevice{
					LinkPortID:   1,
					LinkNegSpeed: 1e+9,
					LinkMaxSpeed: 1e+9,
					LinkNegWidth: 4,
					LinkMaxWidth: 4,
				}
			}
			if tc.healthRes == nil && !tc.nilHealthRes {
				tc.healthRes = healthWithLinkStats(0, 0, 0, 0)
			}

			var mockProv *mockPCIeLinkStatsProvider
			if !tc.nilLinkStatsProv {
				mockProv = &mockPCIeLinkStatsProvider{
					pciDev:    tc.pciDev,
					pciDevErr: tc.pciDevErr,
				}
			}

			ctrlr := &ctlpb.NvmeController{
				PciAddr:  pciAddr,
				PciCfg:   pciCfgSpc,
				DevState: devState,
			}

			ctx, cancel := context.WithTimeout(test.Context(t), 200*time.Millisecond)
			defer cancel()

			ps := events.NewPubSub(ctx, log)
			defer ps.Close()

			ei := NewEngineInstance(log, nil, nil, nil, ps)
			ei._lastHealthStats = tc.lastStats

			subscriber := newMockSubscriber(2)
			ps.Subscribe(events.RASTypeInfoOnly, subscriber)

			chReq := ctrlrHealthReq{
				engine:        ei,
				bhReq:         tc.healthReq,
				ctrlr:         ctrlr,
				linkStatsProv: mockProv,
			}

			upd, err := populateCtrlrHealth(test.Context(t), chReq)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expCtrlr, ctrlr,
				defStorageScanCmpOpts...); diff != "" {
				t.Fatalf("unexpected controller output (-want, +got):\n%s\n", diff)
			}
			test.AssertEqual(t, !tc.expNotUpdated, upd, "")

			<-ctx.Done()

			if diff := cmp.Diff(tc.expLastStats, ei._lastHealthStats, protocmp.Transform()); diff != "" {
				t.Fatalf("unexpected last health stats (-want, +got)\n%s\n", diff)
			}

			// Compare events received with expected, sort received first.
			dispatched := subscriber.getRx()
			sort.Strings(dispatched)
			var expEvtStrs []string
			for _, e := range tc.expDispatched {
				e.Timestamp = "" // Remove TS before comparing.
				expEvtStrs = append(expEvtStrs, e.String())
			}
			if diff := cmp.Diff(expEvtStrs, dispatched, defEvtCmpOpts...); diff != "" {
				t.Fatalf("unexpected events dispatched (-want, +got)\n%s\n", diff)
			}
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
		sd.Rank = 2
		return &ctlpb.SmdDevResp{Devices: []*ctlpb.SmdDevice{sd}}
	}
	healthRespWithUsage := func() *ctlpb.BioHealthResp {
		mh := proto.MockNvmeHealth(2)
		mh.TotalBytes, mh.AvailBytes, mh.ClusterSize = 1, 2, 3
		mh.MetaWalSize, mh.RdbWalSize = 4, 5
		return mh
	}
	ctrlrWithUsageAndMeta := func() *ctlpb.NvmeController {
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
		"scan over drpc; with meta and health; usage and wal size reported": {
			req:       ctlpb.ScanNvmeReq{Meta: true, Health: true},
			rank:      1,
			smdRes:    defSmdScanRes(),
			healthRes: healthRespWithUsage(),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{ctrlrWithUsageAndMeta()},
				State:  new(ctlpb.ResponseState),
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
		"scan over drpc; with meta and health; missing ctrlr in smd": {
			req: ctlpb.ScanNvmeReq{Meta: true, Health: true},
			smdRes: func() *ctlpb.SmdDevResp {
				ssr := defSmdScanRes()
				ssr.Devices[0].Ctrlr = nil
				return ssr
			}(),
			healthRes: healthRespWithUsage(),
			expErr:    errors.New("no ctrlr ref"),
		},
		"scan over drpc; with meta and health; health scan fails": {
			req:       ctlpb.ScanNvmeReq{Meta: true, Health: true},
			smdRes:    defSmdScanRes(),
			healthErr: errors.New("health scan failed"),
			expErr:    errors.New("health scan failed"),
		},
		"scan over drpc; with meta and health; smd list fails": {
			req:       ctlpb.ScanNvmeReq{Meta: true, Health: true},
			smdErr:    errors.New("smd scan failed"),
			healthRes: healthRespWithUsage(),
			expErr:    errors.New("smd scan failed"),
		},
		"scan over drpc; with meta and health; nil smd list returned": {
			req:       ctlpb.ScanNvmeReq{Meta: true, Health: true},
			healthRes: healthRespWithUsage(),
			expErr:    errors.New("nil smd scan resp"),
		},
		"scan over drpc; with meta and health; link info update skipped": {
			req:  ctlpb.ScanNvmeReq{Meta: true, Health: true},
			rank: 1,
			smdRes: func() *ctlpb.SmdDevResp {
				ssr := defSmdScanRes()
				ssr.Devices[0].Ctrlr.PciCfg = "ABCD"
				return ssr
			}(),
			healthRes: healthRespWithUsage(),
			expResp: &ctlpb.ScanNvmeResp{
				Ctrlrs: proto.NvmeControllers{ctrlrWithUsageAndMeta()},
				State:  new(ctlpb.ResponseState),
			},
		},
		"scan over drpc; with health; link info update run but failed": {
			req: ctlpb.ScanNvmeReq{Health: true, LinkStats: true},
			smdRes: func() *ctlpb.SmdDevResp {
				ssr := defSmdScanRes()
				ssr.Devices[0].Ctrlr.PciCfg = "ABCD"
				return ssr
			}(),
			healthRes: healthRespWithUsage(),
			// Prove link stat provider gets called without Meta flag.
			expErr: errors.New("link stats provider fail"),
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
			scanHealth = func(_ context.Context, _ Engine, _ *ctlpb.BioHealthReq) (*ctlpb.BioHealthResp, error) {
				return tc.healthRes, tc.healthErr
			}
			defer func() {
				scanHealth = getBioHealth
			}()
			linkStatsProv = &mockPCIeLinkStatsProvider{
				pciDevErr: errors.New("link stats provider fail"),
			}
			defer func() {
				linkStatsProv = pciutils.NewPCIeLinkStatsProvider()
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
					Rank:      ranklist.NewRankPtr(uint32(tc.rank)),
					ValidRank: true,
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
