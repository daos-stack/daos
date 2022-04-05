//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"sync"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

func hostResps(resps ...*mgmtpb.GetAttachInfoResp) []*control.HostResponse {
	result := []*control.HostResponse{}
	for _, r := range resps {
		result = append(result, &control.HostResponse{
			Message: r,
		})
	}
	return result
}

func TestAgent_mgmtModule_getAttachInfo(t *testing.T) {
	testSrvResp := func() *mgmtpb.GetAttachInfoResp {
		return &mgmtpb.GetAttachInfoResp{
			RankUris: []*mgmtpb.GetAttachInfoResp_RankUri{
				{
					Rank:     0,
					Uri:      "uri0",
					Provider: "ofi+verbs",
				},
				{
					Rank:     1,
					Uri:      "uri1",
					Provider: "ofi+verbs",
				},
				{
					Rank:     3,
					Uri:      "uri3",
					Provider: "ofi+verbs",
				},
			},
			SecondaryRankUris: []*mgmtpb.GetAttachInfoResp_RankUri{
				{
					Rank:     0,
					Uri:      "uri4-sec",
					Provider: "ofi+sockets",
				},
				{
					Rank:     1,
					Uri:      "uri5-sec",
					Provider: "ofi+sockets",
				},
				{
					Rank:     3,
					Uri:      "uri6-sec",
					Provider: "ofi+sockets",
				},
				{
					Rank:     0,
					Uri:      "uri0-sec",
					Provider: "ofi+tcp",
				},
				{
					Rank:     1,
					Uri:      "uri1-sec",
					Provider: "ofi+tcp",
				},
				{
					Rank:     3,
					Uri:      "uri3-sec",
					Provider: "ofi+tcp",
				},
			},
			MsRanks: []uint32{0, 1, 3},
			ClientNetHint: &mgmtpb.ClientNetHint{
				Provider:    "ofi+verbs",
				NetDevClass: uint32(hardware.Infiniband),
			},
			SecondaryClientNetHints: []*mgmtpb.ClientNetHint{
				{
					Provider:    "ofi+tcp",
					NetDevClass: uint32(hardware.Infiniband),
				},
			},
		}
	}

	priResp := func(fi, domain string) *mgmtpb.GetAttachInfoResp {
		withHint := testSrvResp()
		withHint.ClientNetHint.Interface = fi
		withHint.ClientNetHint.Domain = domain

		return withHint
	}

	secResp := func(fi, domain string) *mgmtpb.GetAttachInfoResp {
		return &mgmtpb.GetAttachInfoResp{
			RankUris: []*mgmtpb.GetAttachInfoResp_RankUri{
				{
					Rank:     0,
					Uri:      "uri0-sec",
					Provider: "ofi+tcp",
				},
				{
					Rank:     1,
					Uri:      "uri1-sec",
					Provider: "ofi+tcp",
				},
				{
					Rank:     3,
					Uri:      "uri3-sec",
					Provider: "ofi+tcp",
				},
			},
			MsRanks: []uint32{0, 1, 3},
			ClientNetHint: &mgmtpb.ClientNetHint{
				Provider:    "ofi+tcp",
				NetDevClass: uint32(hardware.Infiniband),
				Interface:   fi,
				Domain:      domain,
			},
		}
	}

	for name, tc := range map[string]struct {
		reqIface  string
		reqDomain string
		provider  string
		numaNode  int
		rpcResp   *control.HostResponse
		expResp   *mgmtpb.GetAttachInfoResp
		expErr    error
	}{
		"RPC error": {
			rpcResp: &control.HostResponse{
				Error: errors.New("mock RPC"),
			},
			expErr: errors.New("mock RPC"),
		},
		"no provider hint": {
			rpcResp: &control.HostResponse{
				Message: &mgmtpb.GetAttachInfoResp{
					RankUris: []*mgmtpb.GetAttachInfoResp_RankUri{
						{
							Rank:     0,
							Uri:      "uri0",
							Provider: "ofi+verbs",
						},
					},
					MsRanks: []uint32{0},
					ClientNetHint: &mgmtpb.ClientNetHint{
						NetDevClass: uint32(hardware.Infiniband),
					},
				},
			},
			expErr: errors.New("no provider"),
		},
		"no provider match": {
			rpcResp: &control.HostResponse{
				Message: &mgmtpb.GetAttachInfoResp{
					RankUris: []*mgmtpb.GetAttachInfoResp_RankUri{
						{
							Rank:     0,
							Uri:      "uri0",
							Provider: "notreal",
						},
					},
					MsRanks: []uint32{0},
					ClientNetHint: &mgmtpb.ClientNetHint{
						Provider:    "notreal",
						NetDevClass: uint32(hardware.Infiniband),
					},
				},
			},
			expErr: errors.New("no suitable fabric interface"),
		},
		"basic success": {

			rpcResp: &control.HostResponse{
				Message: testSrvResp(),
			},
			expResp: priResp("fi0", "d0"),
		},
		"primary provider": {
			provider: "ofi+verbs",
			rpcResp: &control.HostResponse{
				Message: testSrvResp(),
			},
			expResp: priResp("fi0", "d0"),
		},
		"secondary provider": {
			provider: "ofi+tcp",
			rpcResp: &control.HostResponse{
				Message: testSrvResp(),
			},
			expResp: secResp("fi0", "fi0"),
		},
		"client req iface and domain": {
			reqIface:  "fi1",
			reqDomain: "d1",
			provider:  "ofi+verbs",
			rpcResp: &control.HostResponse{
				Message: testSrvResp(),
			},
			expResp: priResp("fi1", "d1"),
		},
		"client req secondary provider": {
			reqIface:  "fi1",
			reqDomain: "fi1",
			provider:  "ofi+tcp",
			rpcResp: &control.HostResponse{
				Message: testSrvResp(),
			},
			expResp: secResp("fi1", "fi1"),
		},
		"client req iface for secondary provider": {
			reqIface:  "fi1",
			reqDomain: "fi1",
			rpcResp: &control.HostResponse{
				Message: testSrvResp(),
			},
			expResp: secResp("fi1", "fi1"),
		},
		"client req iface only": {
			reqIface: "fi1",
			rpcResp: &control.HostResponse{
				Message: testSrvResp(),
			},
			expResp: secResp("fi1", "fi1"),
		},
		"client req domain-only ignored": {
			reqDomain: "d2",
			provider:  "ofi+verbs",
			rpcResp: &control.HostResponse{
				Message: testSrvResp(),
			},
			expResp: priResp("fi0", "d0"),
		},
		"client req provider mismatch ignored": {
			reqIface:  "fi1",
			reqDomain: "d1",
			provider:  "ofi+tcp",
			rpcResp: &control.HostResponse{
				Message: testSrvResp(),
			},
			expResp: secResp("fi1", "d1"),
		},
		"client req iface/domain mismatch ignored": {
			reqIface:  "fi0",
			reqDomain: "d2",
			provider:  "ofi+verbs",
			rpcResp: &control.HostResponse{
				Message: testSrvResp(),
			},
			expResp: priResp("fi0", "d2"),
		},
		"client req iface not found ignored": {
			reqIface: "notreal",
			provider: "ofi+verbs",
			rpcResp: &control.HostResponse{
				Message: testSrvResp(),
			},
			expResp: priResp("notreal", "notreal"),
		},
		"config provider not found": {
			provider: "notreal",
			rpcResp: &control.HostResponse{
				Message: testSrvResp(),
			},
			expErr: errors.New("no valid connection information"),
		},
		"config provider hint missing": {
			provider: "ofi+sockets",
			rpcResp: &control.HostResponse{
				Message: testSrvResp(),
			},
			expErr: errors.New("no valid connection information"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			testFabric := &NUMAFabric{
				log: log,
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "fi0",
							Domain:      "d0",
							NetDevClass: hardware.Infiniband,
							hw: &hardware.FabricInterface{
								Providers: common.NewStringSet("ofi+verbs"),
							},
						},
						{
							Name:        "fi0",
							NetDevClass: hardware.Infiniband,
							hw: &hardware.FabricInterface{
								Providers: common.NewStringSet("ofi+tcp"),
							},
						},
					},
					1: {
						{
							Name:        "fi1",
							Domain:      "d1",
							NetDevClass: hardware.Infiniband,
							hw: &hardware.FabricInterface{
								Providers: common.NewStringSet("ofi+verbs"),
							},
						},
						{
							Name:        "fi1",
							NetDevClass: hardware.Infiniband,
							hw: &hardware.FabricInterface{
								Providers: common.NewStringSet("ofi+tcp"),
							},
						},
					},
				},
			}

			sysName := "dontcare"
			mod := &mgmtModule{
				log:        log,
				sys:        sysName,
				fabricInfo: newTestFabricCache(t, log, testFabric),
				attachInfo: newAttachInfoCache(log, true),
				ctlInvoker: control.NewMockInvoker(log, &control.MockInvokerConfig{
					Sys: sysName,
					UnaryResponse: &control.UnaryResponse{
						Responses: []*control.HostResponse{tc.rpcResp},
					},
				}),
				provider: tc.provider,
			}

			resp, err := mod.getAttachInfo(context.Background(), tc.numaNode,
				&mgmtpb.GetAttachInfoReq{
					Sys:       sysName,
					Interface: tc.reqIface,
					Domain:    tc.reqDomain,
				})

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResp, resp, cmpopts.IgnoreUnexported(
				mgmtpb.GetAttachInfoResp{},
				mgmtpb.GetAttachInfoResp_RankUri{},
				mgmtpb.ClientNetHint{},
			)); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestAgent_mgmtModule_getAttachInfo_cacheResp(t *testing.T) {
	testResps := []*mgmtpb.GetAttachInfoResp{
		{
			MsRanks: []uint32{0, 1, 3},
			ClientNetHint: &mgmtpb.ClientNetHint{
				Provider:    "ofi+tcp",
				NetDevClass: uint32(hardware.Ether),
			},
		},
		{
			MsRanks: []uint32{0},
			ClientNetHint: &mgmtpb.ClientNetHint{
				Provider:    "ofi+tcp",
				NetDevClass: uint32(hardware.Ether),
			},
		},
		{
			MsRanks: []uint32{2, 3},
			ClientNetHint: &mgmtpb.ClientNetHint{
				Provider:    "ofi+tcp",
				NetDevClass: uint32(hardware.Ether),
			},
		},
	}

	hostResps := func(resps []*mgmtpb.GetAttachInfoResp) []*control.HostResponse {
		result := []*control.HostResponse{}

		for _, r := range resps {
			result = append(result, &control.HostResponse{
				Message: r,
			})
		}

		return result
	}

	testFI := fabricInterfaceFromHardware(&hardware.FabricInterface{
		Name:         "test0",
		NetInterface: "test0",
		DeviceClass:  hardware.Ether,
		Providers:    common.NewStringSet("ofi+tcp"),
	})

	hintResp := func(resp *mgmtpb.GetAttachInfoResp) *mgmtpb.GetAttachInfoResp {
		withHint := new(mgmtpb.GetAttachInfoResp)
		*withHint = *resp
		withHint.ClientNetHint.Interface = testFI.Name
		withHint.ClientNetHint.Domain = testFI.Name

		return withHint
	}

	for name, tc := range map[string]struct {
		cacheDisabled bool
		rpcResps      []*mgmtpb.GetAttachInfoResp
		expResps      []*mgmtpb.GetAttachInfoResp
	}{
		"cache disabled": {
			cacheDisabled: true,
			rpcResps:      testResps,
			expResps: []*mgmtpb.GetAttachInfoResp{
				hintResp(testResps[0]),
				hintResp(testResps[1]),
				hintResp(testResps[2]),
			},
		},
		"cached": {
			rpcResps: testResps,
			expResps: []*mgmtpb.GetAttachInfoResp{
				hintResp(testResps[0]),
				hintResp(testResps[0]),
				hintResp(testResps[0]),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			sysName := "dontcare"
			mockInvokerCfg := &control.MockInvokerConfig{
				Sys:              sysName,
				UnaryResponseSet: []*control.UnaryResponse{},
			}

			for _, rpcResp := range tc.rpcResps {
				mockInvokerCfg.UnaryResponseSet = append(mockInvokerCfg.UnaryResponseSet,
					&control.UnaryResponse{
						Responses: hostResps([]*mgmtpb.GetAttachInfoResp{rpcResp}),
					},
				)
			}

			mod := &mgmtModule{
				log: log,
				sys: sysName,
				fabricInfo: newTestFabricCache(t, log, &NUMAFabric{
					log: log,
					numaMap: map[int][]*FabricInterface{
						0: {
							testFI,
						},
					},
				}),
				attachInfo: newAttachInfoCache(log, !tc.cacheDisabled),
				ctlInvoker: control.NewMockInvoker(log, mockInvokerCfg),
			}

			for _, expResp := range tc.expResps {
				resp, err := mod.getAttachInfo(context.Background(), 0,
					&mgmtpb.GetAttachInfoReq{
						Sys: sysName,
					})

				common.CmpErr(t, nil, err)

				if diff := cmp.Diff(expResp, resp, cmpopts.IgnoreUnexported(mgmtpb.GetAttachInfoResp{}, mgmtpb.ClientNetHint{})); diff != "" {
					t.Fatalf("-want, +got:\n%s", diff)
				}
			}
		})
	}

}

func TestAgent_mgmtModule_getAttachInfo_Parallel(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	sysName := "dontcare"

	mod := &mgmtModule{
		log: log,
		sys: sysName,
		fabricInfo: newTestFabricCache(t, log, &NUMAFabric{
			log: log,
			numaMap: map[int][]*FabricInterface{
				0: {
					fabricInterfaceFromHardware(&hardware.FabricInterface{
						Name:         "test0",
						NetInterface: "test0",
						DeviceClass:  hardware.Ether,
						Providers:    common.NewStringSet("ofi+tcp"),
					}),
				},
			},
		}),
		attachInfo: newAttachInfoCache(log, true),
		ctlInvoker: control.NewMockInvoker(log, &control.MockInvokerConfig{
			Sys: sysName,
			UnaryResponse: &control.UnaryResponse{
				Responses: []*control.HostResponse{
					{
						Message: &mgmtpb.GetAttachInfoResp{
							MsRanks: []uint32{0, 1, 3},
							ClientNetHint: &mgmtpb.ClientNetHint{
								Provider:    "ofi+tcp",
								NetDevClass: uint32(hardware.Ether),
							},
						},
					},
				},
			},
		}),
	}

	var wg sync.WaitGroup

	numThreads := 20
	for i := 0; i < numThreads; i++ {
		wg.Add(1)
		go func(n int) {
			defer wg.Done()

			_, err := mod.getAttachInfo(context.Background(), 0,
				&mgmtpb.GetAttachInfoReq{
					Sys: sysName,
				})
			if err != nil {
				panic(errors.Wrapf(err, "thread %d", n))
			}
		}(i)
	}

	wg.Wait()
}

type mockNUMAProvider struct {
	GetNUMANodeIDForPIDResult uint
	GetNUMANodeIDForPIDErr    error
}

func (m *mockNUMAProvider) GetNUMANodeIDForPID(_ context.Context, _ int32) (uint, error) {
	return m.GetNUMANodeIDForPIDResult, m.GetNUMANodeIDForPIDErr
}

func TestAgent_mgmtModule_getNUMANode(t *testing.T) {
	for name, tc := range map[string]struct {
		useDefaultNUMA bool
		numaGetter     hardware.ProcessNUMAProvider
		expResult      uint
		expErr         error
	}{
		"default": {
			useDefaultNUMA: true,
			numaGetter:     &mockNUMAProvider{GetNUMANodeIDForPIDResult: 2},
			expResult:      0,
		},
		"got NUMA": {
			numaGetter: &mockNUMAProvider{GetNUMANodeIDForPIDResult: 2},
			expResult:  2,
		},
		"error": {
			numaGetter: &mockNUMAProvider{
				GetNUMANodeIDForPIDErr: errors.New("mock GetNUMANodeIDForPID"),
			},
			expErr: errors.New("mock GetNUMANodeIDForPID"),
		},
		"non-NUMA-aware": {
			numaGetter: &mockNUMAProvider{
				GetNUMANodeIDForPIDErr: hardware.ErrNoNUMANodes,
			},
			expResult: 0,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mod := &mgmtModule{
				log:            log,
				useDefaultNUMA: tc.useDefaultNUMA,
				numaGetter:     tc.numaGetter,
			}

			result, err := mod.getNUMANode(context.Background(), 123)

			common.AssertEqual(t, tc.expResult, result, "")
			common.CmpErr(t, tc.expErr, err)
		})
	}
}
