//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"net"
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

func TestAgent_mgmtModule_getAttachInfo(t *testing.T) {
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

	testFI := fabricInterfacesFromHardware(&hardware.FabricInterface{
		Name:          "test0",
		NetInterfaces: common.NewStringSet("test0"),
		DeviceClass:   hardware.Ether,
		Providers:     common.NewStringSet("ofi+tcp"),
	})

	hintResp := func(resp *mgmtpb.GetAttachInfoResp) *mgmtpb.GetAttachInfoResp {
		withHint := new(mgmtpb.GetAttachInfoResp)
		*withHint = *testResps[0]
		withHint.ClientNetHint.Interface = testFI[0].Name
		withHint.ClientNetHint.Domain = testFI[0].Name

		return withHint
	}

	for name, tc := range map[string]struct {
		cacheDisabled bool
		rpcResps      []*control.HostResponse
		expResps      []*mgmtpb.GetAttachInfoResp
	}{
		"cache disabled": {
			cacheDisabled: true,
			rpcResps:      hostResps(testResps),
			expResps: []*mgmtpb.GetAttachInfoResp{
				hintResp(testResps[0]),
				hintResp(testResps[1]),
				hintResp(testResps[2]),
			},
		},
		"cached": {
			rpcResps: hostResps(testResps),
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
			mod := &mgmtModule{
				log: log,
				sys: sysName,
				fabricInfo: newTestFabricCache(t, log, &NUMAFabric{
					log: log,
					numaMap: map[int][]*FabricInterface{
						0: testFI,
					},
				}),
				attachInfo: newAttachInfoCache(log, !tc.cacheDisabled),
				ctlInvoker: control.NewMockInvoker(log, &control.MockInvokerConfig{
					Sys: sysName,
					UnaryResponse: &control.UnaryResponse{
						Responses: tc.rpcResps,
					},
				}),
			}

			for _, expResp := range tc.expResps {
				resp, err := mod.getAttachInfo(context.Background(), 0, sysName)

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
				0: fabricInterfacesFromHardware(&hardware.FabricInterface{
					Name:          "test0",
					NetInterfaces: common.NewStringSet("test0"),
					DeviceClass:   hardware.Ether,
					Providers:     common.NewStringSet("ofi+tcp"),
				}),
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

			_, err := mod.getAttachInfo(context.Background(), 0, sysName)
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

func TestAgent_mgmtModule_waitFabricReady(t *testing.T) {
	defaultNetIfaceFn := func() ([]net.Interface, error) {
		return []net.Interface{
			{Name: "t0"},
			{Name: "t1"},
			{Name: "t2"},
		}, nil
	}

	defaultDevClassProv := &hardware.MockNetDevClassProvider{
		GetNetDevClassReturn: []hardware.MockGetNetDevClassResult{
			{
				ExpInput: "t0",
				NDC:      hardware.Infiniband,
			},
			{
				ExpInput: "t1",
				NDC:      hardware.Infiniband,
			},
			{
				ExpInput: "t2",
				NDC:      hardware.Ether,
			},
		},
	}

	for name, tc := range map[string]struct {
		netIfacesFn  func() ([]net.Interface, error)
		devClassProv *hardware.MockNetDevClassProvider
		devStateProv *hardware.MockNetDevStateProvider
		netDevClass  hardware.NetDevClass
		expErr       error
		expChecked   []string
	}{
		"netIfaces fails": {
			netIfacesFn: func() ([]net.Interface, error) {
				return nil, errors.New("mock netIfaces")
			},
			netDevClass: hardware.Infiniband,
			expErr:      errors.New("mock netIfaces"),
		},
		"GetNetDevClass fails": {
			devClassProv: &hardware.MockNetDevClassProvider{
				GetNetDevClassReturn: []hardware.MockGetNetDevClassResult{
					{
						ExpInput: "t0",
						Err:      errors.New("mock GetNetDevClass"),
					},
				},
			},
			netDevClass: hardware.Infiniband,
			expErr:      errors.New("mock GetNetDevClass"),
		},
		"GetNetDevState fails": {
			devStateProv: &hardware.MockNetDevStateProvider{
				GetStateReturn: []hardware.MockNetDevStateResult{
					{Err: errors.New("mock NetDevStateProvider")},
				},
			},
			netDevClass: hardware.Infiniband,
			expErr:      errors.New("mock NetDevStateProvider"),
			expChecked:  []string{"t0"},
		},
		"down devices are ignored": {
			devStateProv: &hardware.MockNetDevStateProvider{
				GetStateReturn: []hardware.MockNetDevStateResult{
					{State: hardware.NetDevStateDown},
					{State: hardware.NetDevStateReady},
				},
			},
			netDevClass: hardware.Infiniband,
			expChecked:  []string{"t0", "t1"},
		},
		"success": {
			netDevClass: hardware.Infiniband,
			expChecked:  []string{"t0", "t1"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.netIfacesFn == nil {
				tc.netIfacesFn = defaultNetIfaceFn
			}

			if tc.devClassProv == nil {
				tc.devClassProv = defaultDevClassProv
			}

			if tc.devStateProv == nil {
				tc.devStateProv = &hardware.MockNetDevStateProvider{}
			}

			mod := &mgmtModule{
				log:            log,
				netIfaces:      tc.netIfacesFn,
				devClassGetter: tc.devClassProv,
				devStateGetter: tc.devStateProv,
			}

			err := mod.waitFabricReady(context.Background(), tc.netDevClass)

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expChecked, tc.devStateProv.GetStateCalled); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}
