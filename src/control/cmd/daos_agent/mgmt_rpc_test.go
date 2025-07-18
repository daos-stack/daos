//
// (C) Copyright 2021-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
// (C) Copyright 2025 Google LLC
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
	"github.com/pkg/errors"
	"golang.org/x/sys/unix"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/telemetry"
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
	testSys := "test_sys"
	testResp := &control.GetAttachInfoResp{
		System:       "dontcare",
		ServiceRanks: []*control.PrimaryServiceRank{{Rank: 1, Uri: "my uri"}},
		MSRanks:      []uint32{0, 1, 2, 3},
		ClientNetHint: control.ClientNetworkHint{
			Provider:    "ofi+tcp",
			NetDevClass: uint32(hardware.Ether),
		},
	}

	testFIS := hardware.NewFabricInterfaceSet(
		&hardware.FabricInterface{
			Name:          "test0",
			NetInterfaces: common.NewStringSet("test0"),
			DeviceClass:   hardware.Ether,
			Providers:     hardware.NewFabricProviderSet(&hardware.FabricProvider{Name: "ofi+tcp"}),
			NUMANode:      0,
		},
		&hardware.FabricInterface{
			Name:          "dev1",
			NetInterfaces: common.NewStringSet("test1"),
			DeviceClass:   hardware.Ether,
			NUMANode:      2,
			Providers:     hardware.NewFabricProviderSet(&hardware.FabricProvider{Name: "ofi+tcp"}, &hardware.FabricProvider{Name: "ucx+tcp"}),
		},
		&hardware.FabricInterface{
			Name:          "dev2",
			NetInterfaces: common.NewStringSet("test2"),
			DeviceClass:   hardware.Infiniband,
			NUMANode:      2,
			Providers:     hardware.NewFabricProviderSet(&hardware.FabricProvider{Name: "ofi+verbs"}),
		})

	testFabric := NUMAFabricFromScan(test.Context(t), logging.NewCommandLineLogger(), testFIS)
	testFabric.getAddrInterface = mockGetAddrInterface

	reqBytes := func(req *mgmtpb.GetAttachInfoReq) []byte {
		t.Helper()
		bytes, err := proto.Marshal(req)
		if err != nil {
			t.Fatal(err)
		}
		return bytes
	}

	respWith := func(in *control.GetAttachInfoResp, iface, domain string, numaMap []*mgmtpb.FabricInterfaces) *mgmtpb.GetAttachInfoResp {
		t.Helper()
		out := new(mgmtpb.GetAttachInfoResp)
		if err := convert.Types(in, out); err != nil {
			t.Fatal(err)
		}
		out.ClientNetHint.Interface = iface
		out.ClientNetHint.Domain = domain
		out.NumaFabricInterfaces = numaMap
		return out
	}

	for name, tc := range map[string]struct {
		sysName           string
		mockGetAttachInfo getAttachInfoFn
		mockFabricScan    fabricScanFn
		mockGetNetIfaces  func() ([]net.Interface, error)
		numaGetter        *mockNUMAProvider
		fabricCfg         []*NUMAFabricConfig
		reqBytes          []byte
		expResp           *mgmtpb.GetAttachInfoResp
		expErr            error
	}{
		"junk req": {
			reqBytes: []byte("garbage"),
			expErr:   errors.New("unmarshal"),
		},
		"non-matching system name": {
			reqBytes: reqBytes(&mgmtpb.GetAttachInfoReq{Sys: "bad"}),
			expResp:  &mgmtpb.GetAttachInfoResp{Status: int32(daos.InvalidInput)},
		},
		"get NUMA fails": {
			reqBytes:   reqBytes(&mgmtpb.GetAttachInfoReq{Sys: testSys}),
			numaGetter: &mockNUMAProvider{GetNUMANodeIDForPIDErr: errors.New("mock get NUMA")},
			expErr:     errors.New("mock get NUMA"),
		},
		"getAttachInfo fails": {
			reqBytes: reqBytes(&mgmtpb.GetAttachInfoReq{Sys: testSys}),
			mockGetAttachInfo: func(_ context.Context, _ control.UnaryInvoker, _ *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error) {
				return nil, errors.New("mock GetAttachInfo")
			},
			expErr: errors.New("mock GetAttachInfo"),
		},
		"waitFabricReady fails": {
			reqBytes: reqBytes(&mgmtpb.GetAttachInfoReq{Sys: testSys}),
			mockGetNetIfaces: func() ([]net.Interface, error) {
				return nil, errors.New("mock get net ifaces")
			},
			expErr: errors.New("mock get net ifaces"),
		},
		"scan fabric fails": {
			reqBytes: reqBytes(&mgmtpb.GetAttachInfoReq{Sys: testSys}),
			mockFabricScan: func(_ context.Context, _ ...string) (*NUMAFabric, error) {
				return nil, errors.New("mock fabric scan")
			},
			expErr: errors.New("mock fabric scan"),
		},
		"success": {
			reqBytes: reqBytes(&mgmtpb.GetAttachInfoReq{Sys: testSys}),
			expResp: respWith(testResp, "test1", "dev1", []*mgmtpb.FabricInterfaces{
				{
					Ifaces: []*mgmtpb.FabricInterface{
						{
							Interface: "test0",
							Domain:    "test0",
							Provider:  "ofi+tcp",
						},
					},
				},
				{
					NumaNode: 1,
				},
				{
					NumaNode: 2,
					Ifaces: []*mgmtpb.FabricInterface{
						{
							NumaNode:  2,
							Interface: "test1",
							Domain:    "dev1",
							Provider:  "ofi+tcp",
						},
					},
				},
			}),
		},
		"no sys succeeds": {
			reqBytes: reqBytes(&mgmtpb.GetAttachInfoReq{}),
			expResp: respWith(testResp, "test1", "dev1", []*mgmtpb.FabricInterfaces{
				{
					Ifaces: []*mgmtpb.FabricInterface{
						{
							Interface: "test0",
							Domain:    "test0",
							Provider:  "ofi+tcp",
						},
					},
				},
				{
					NumaNode: 1,
				},
				{
					NumaNode: 2,
					Ifaces: []*mgmtpb.FabricInterface{
						{
							NumaNode:  2,
							Interface: "test1",
							Domain:    "dev1",
							Provider:  "ofi+tcp",
						},
					},
				},
			}),
		},
		"req interface/domain": {
			reqBytes: reqBytes(&mgmtpb.GetAttachInfoReq{
				Sys:       testSys,
				Interface: "test1",
				Domain:    "dev1",
			}),
			expResp: respWith(testResp, "test1", "dev1", []*mgmtpb.FabricInterfaces{
				{
					Ifaces: []*mgmtpb.FabricInterface{
						{
							Interface: "test0",
							Domain:    "test0",
							Provider:  "ofi+tcp",
						},
					},
				},
				{
					NumaNode: 1,
				},
				{
					NumaNode: 2,
					Ifaces: []*mgmtpb.FabricInterface{
						{
							NumaNode:  2,
							Interface: "test1",
							Domain:    "dev1",
							Provider:  "ofi+tcp",
						},
					},
				},
			}),
		},
		"req interface only": {
			reqBytes: reqBytes(&mgmtpb.GetAttachInfoReq{
				Sys:       testSys,
				Interface: "test0",
			}),
			expResp: respWith(testResp, "test0", "test0", []*mgmtpb.FabricInterfaces{
				{
					Ifaces: []*mgmtpb.FabricInterface{
						{
							Interface: "test0",
							Domain:    "test0",
							Provider:  "ofi+tcp",
						},
					},
				},
				{
					NumaNode: 1,
				},
				{
					NumaNode: 2,
					Ifaces: []*mgmtpb.FabricInterface{
						{
							NumaNode:  2,
							Interface: "test1",
							Domain:    "dev1",
							Provider:  "ofi+tcp",
						},
					},
				},
			}),
		},
		"req interface with cfg ifaces": {
			reqBytes: reqBytes(&mgmtpb.GetAttachInfoReq{
				Sys:       testSys,
				Interface: "test0",
			}),
			fabricCfg: []*NUMAFabricConfig{
				{
					NUMANode: 0,
					Interfaces: []*FabricInterfaceConfig{
						{
							Interface: "test0",
							Domain:    "test0",
						},
					},
				},
			},
			expResp: respWith(testResp, "test0", "test0", []*mgmtpb.FabricInterfaces{
				{
					Ifaces: []*mgmtpb.FabricInterface{
						{
							Interface: "test0",
							Domain:    "test0",
							Provider:  "ofi+tcp", // automatically set to the same as server requested
						},
					},
				},
			}),
		},
		"incompatible error": {
			reqBytes: reqBytes(&mgmtpb.GetAttachInfoReq{}),
			mockGetAttachInfo: func(_ context.Context, _ control.UnaryInvoker, _ *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error) {
				return nil, &fault.Fault{Code: code.ServerWrongSystem}
			},
			expResp: &mgmtpb.GetAttachInfoResp{Status: int32(daos.ControlIncompatible)},
		},
		"bad cert error": {
			reqBytes: reqBytes(&mgmtpb.GetAttachInfoReq{}),
			mockGetAttachInfo: func(_ context.Context, _ control.UnaryInvoker, _ *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error) {
				return nil, &fault.Fault{Code: code.SecurityInvalidCert}
			},
			expResp: &mgmtpb.GetAttachInfoResp{Status: int32(daos.BadCert)},
		},
		"MS connection error": {
			reqBytes: reqBytes(&mgmtpb.GetAttachInfoReq{}),
			mockGetAttachInfo: func(_ context.Context, _ control.UnaryInvoker, _ *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error) {
				return nil, errors.Errorf("unable to contact the %s", build.ManagementServiceName)
			},
			expResp: &mgmtpb.GetAttachInfoResp{Status: int32(daos.Unreachable)},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.numaGetter == nil {
				tc.numaGetter = &mockNUMAProvider{
					GetNUMANodeIDForPIDResult: 2,
				}
			}

			if tc.mockGetAttachInfo == nil {
				tc.mockGetAttachInfo = func(_ context.Context, _ control.UnaryInvoker, _ *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error) {
					return testResp, nil
				}
			}

			if tc.mockFabricScan == nil {
				tc.mockFabricScan = func(_ context.Context, _ ...string) (*NUMAFabric, error) {
					return testFabric, nil
				}
			}

			if tc.mockGetNetIfaces == nil {
				tc.mockGetNetIfaces = func() ([]net.Interface, error) {
					ifaces := []net.Interface{}
					for _, dev := range testFIS.NetDevices() {
						ifaces = append(ifaces, net.Interface{Name: dev})
					}
					return ifaces, nil
				}
			}

			ic := newTestInfoCache(t, log, testInfoCacheParams{
				mockGetAttachInfo: tc.mockGetAttachInfo,
				mockScanFabric:    tc.mockFabricScan,
				mockNetIfaces:     tc.mockGetNetIfaces,
				mockNetDevClassGetter: &hardware.MockNetDevClassProvider{
					GetNetDevClassReturn: []hardware.MockGetNetDevClassResult{
						{
							NDC: hardware.Ether,
						},
					},
				},
			})
			if tc.fabricCfg != nil {
				nf := NUMAFabricFromConfig(log, tc.fabricCfg)
				ic.EnableStaticFabricCache(test.Context(t), nf)
			}
			mod := &mgmtModule{
				log:        log,
				sys:        testSys,
				cache:      ic,
				numaGetter: tc.numaGetter,
			}

			respBytes, err := mod.handleGetAttachInfo(test.Context(t), tc.reqBytes, 123)

			test.CmpErr(t, tc.expErr, err)

			if tc.expResp == nil {
				if respBytes == nil {
					return
				}
				t.Fatalf("expected nil response, got bytes: %+v", respBytes)
			}

			resp := new(mgmtpb.GetAttachInfoResp)
			err = proto.Unmarshal(respBytes, resp)
			if err != nil {
				t.Fatal(err)
			}
			cmpOpts := cmp.Options{
				protocmp.Transform(),
			}
			if diff := cmp.Diff(tc.expResp, resp, cmpOpts...); diff != "" {
				t.Fatalf("want-, got+:\n%s", diff)
			}
		})
	}
}

func TestAgent_mgmtModule_getAttachInfo_Parallel(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	sysName := "dontcare"
	ctlInvoker := control.NewMockInvoker(log, &control.MockInvokerConfig{
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
	})
	ic := newTestInfoCache(t, log, testInfoCacheParams{
		ctlInvoker: ctlInvoker,
		mockScanFabric: func(_ context.Context, _ ...string) (*NUMAFabric, error) {
			fis := hardware.NewFabricInterfaceSet(&hardware.FabricInterface{
				Name:          "test0",
				NetInterfaces: common.NewStringSet("test0"),
				Providers:     testFabricProviderSet("ofi+tcp"),
				DeviceClass:   hardware.Ether,
			})
			nf := NUMAFabricFromScan(test.Context(t), log, fis)
			nf.getAddrInterface = mockGetAddrInterface
			return nf, nil
		},
		mockGetAttachInfo: control.GetAttachInfo,
	})

	mod := &mgmtModule{
		log:        log,
		sys:        sysName,
		cache:      ic,
		ctlInvoker: ctlInvoker,
	}

	var wg sync.WaitGroup

	numThreads := 20
	for i := 0; i < numThreads; i++ {
		wg.Add(1)
		go func(n int) {
			defer wg.Done()

			_, err := mod.getAttachInfo(test.Context(t), 0,
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
			defer test.ShowBufferOnFailure(t, buf)

			mod := &mgmtModule{
				log:            log,
				useDefaultNUMA: atm.NewBool(tc.useDefaultNUMA),
				numaGetter:     tc.numaGetter,
			}

			result, err := mod.getNUMANode(test.Context(t), 123)

			test.AssertEqual(t, tc.expResult, result, "")
			test.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestAgent_mgmtModule_RefreshCache(t *testing.T) {
	for name, tc := range map[string]struct {
		getInfoCache func(logging.Logger) *InfoCache
		expErr       error
	}{
		"nil cache": {
			getInfoCache: func(_ logging.Logger) *InfoCache { return nil },
			expErr:       errors.New("nil"),
		},
		"caches disabled": {
			getInfoCache: func(log logging.Logger) *InfoCache {
				return newTestInfoCache(t, log, testInfoCacheParams{
					disableFabricCache:     true,
					disableAttachInfoCache: true,
				})
			},
			expErr: errors.New("disabled"),
		},
		"nothing cached": {
			getInfoCache: func(log logging.Logger) *InfoCache {
				return newTestInfoCache(t, log, testInfoCacheParams{
					mockGetAttachInfo: func(_ context.Context, _ control.UnaryInvoker, _ *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error) {
						return nil, errors.New("shouldn't call getAttachInfo")
					},
					mockScanFabric: func(_ context.Context, _ ...string) (*NUMAFabric, error) {
						return nil, errors.New("shouldn't call fabric scan")
					},
				})
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mod := &mgmtModule{
				log:   log,
				cache: tc.getInfoCache(log),
			}

			err := mod.RefreshCache(test.Context(t))

			test.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestAgent_handleSetupClientTelemetry(t *testing.T) {
	testCreds := &unix.Ucred{
		Uid: 123,
		Gid: 456,
	}
	testSysName := "test-sys"
	testJobID := "test-job"
	testShmKey := int32(42)

	for name, tc := range map[string]struct {
		clientBytes []byte
		clientReq   *mgmtpb.ClientTelemetryReq
		clientCred  *unix.Ucred
		expResp     *mgmtpb.ClientTelemetryResp
		expErr      error
	}{
		"nil client request": {
			clientReq:  nil,
			clientCred: testCreds,
			expErr:     errors.New("empty request"),
		},
		"garbage client request": {
			clientBytes: []byte("invalid"),
			clientCred:  testCreds,
			expErr:      drpc.UnmarshalingPayloadFailure(),
		},
		"unset jobid": {
			clientReq: &mgmtpb.ClientTelemetryReq{
				Sys:    testSysName,
				Jobid:  "",
				ShmKey: testShmKey,
			},
			clientCred: testCreds,
			expErr:     errors.New("empty jobid"),
		},
		"unset shm key": {
			clientReq: &mgmtpb.ClientTelemetryReq{
				Sys:    testSysName,
				Jobid:  testJobID,
				ShmKey: 0,
			},
			clientCred: testCreds,
			expErr:     errors.New("unset shm key"),
		},
		"nil user creds": {
			clientReq: &mgmtpb.ClientTelemetryReq{
				Sys:    testSysName,
				Jobid:  testJobID,
				ShmKey: testShmKey,
			},
			clientCred: nil,
			expErr:     errors.New("nil user credentials"),
		},
		"success": {
			clientReq: &mgmtpb.ClientTelemetryReq{
				Sys:    testSysName,
				Jobid:  testJobID,
				ShmKey: testShmKey,
			},
			clientCred: testCreds,
			expResp: &mgmtpb.ClientTelemetryResp{
				AgentUid: int32(unix.Getuid()),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			parent := test.MustLogContext(t)
			log := logging.FromContext(parent)

			mod := &mgmtModule{
				log: log,
			}

			var reqBytes []byte
			if len(tc.clientBytes) > 0 {
				reqBytes = tc.clientBytes
			} else {
				var err error
				reqBytes, err = proto.Marshal(tc.clientReq)
				if err != nil {
					t.Fatal(err)
				}
			}

			testID := uint32(telemetry.NextTestID(telemetry.AgentIDBase))
			telemetry.InitTestMetricsProducer(t, int(testID), 2048)
			defer telemetry.CleanupTestMetricsProducer(t)

			ctx, err := telemetry.Init(parent, testID)
			if err != nil {
				t.Fatal(err)
			}
			defer telemetry.Fini()

			gotResp, gotErr := mod.handleSetupClientTelemetry(ctx, reqBytes, tc.clientCred)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			expRespBytes, err := proto.Marshal(tc.expResp)
			if err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(expRespBytes, gotResp, protocmp.Transform()); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestAgent_mgmtModule_GetMethod(t *testing.T) {
	for name, tc := range map[string]struct {
		methodID  int32
		expMethod drpc.Method
		expErr    error
	}{
		"get-attach-info": {
			methodID:  daos.MethodGetAttachInfo.ID(),
			expMethod: daos.MethodGetAttachInfo,
		},
		"notify-pool-connect": {
			methodID:  daos.MethodNotifyPoolConnect.ID(),
			expMethod: daos.MethodNotifyPoolConnect,
		},
		"notify-pool-disconnect": {
			methodID:  daos.MethodNotifyPoolDisconnect.ID(),
			expMethod: daos.MethodNotifyPoolDisconnect,
		},
		"notify-exit": {
			methodID:  daos.MethodNotifyExit.ID(),
			expMethod: daos.MethodNotifyExit,
		},
		"setup-client-telemetry": {
			methodID:  daos.MethodSetupClientTelemetry.ID(),
			expMethod: daos.MethodSetupClientTelemetry,
		},
		"unknown": {
			methodID: -1,
			expErr:   errors.New("method ID -1"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			parent := test.MustLogContext(t)
			log := logging.FromContext(parent)

			mod := &mgmtModule{
				log: log,
			}

			method, err := mod.GetMethod(tc.methodID)

			test.CmpErr(t, tc.expErr, err)
			test.CmpAny(t, "", tc.expMethod, method)
		})
	}
}
