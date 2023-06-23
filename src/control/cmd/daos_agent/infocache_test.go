//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"net"
	"testing"
	"time"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/cache"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
)

type testInfoCacheParams struct {
	mockGetAttachInfo      getAttachInfoFn
	mockScanFabric         fabricScanFn
	mockNetIfaces          func() ([]net.Interface, error)
	mockNetDevClassGetter  hardware.NetDevClassProvider
	mockNetDevStateGetter  hardware.NetDevStateProvider
	disableFabricCache     bool
	disableAttachInfoCache bool
	ctlInvoker             control.Invoker
	cachedItems            []cache.Item
}

func newTestInfoCache(t *testing.T, log logging.Logger, params testInfoCacheParams) *InfoCache {
	c := cache.NewItemCache(log)
	for _, item := range params.cachedItems {
		c.Set(item)
	}

	ic := &InfoCache{
		log:            log,
		getAttachInfo:  params.mockGetAttachInfo,
		fabricScan:     params.mockScanFabric,
		devClassGetter: params.mockNetDevClassGetter,
		devStateGetter: params.mockNetDevStateGetter,
		netIfaces:      params.mockNetIfaces,
		client:         params.ctlInvoker,
		cache:          c,
	}

	if ic.netIfaces == nil {
		ic.netIfaces = func() ([]net.Interface, error) {
			return []net.Interface{
				{Name: "test0"},
				{Name: "test1"},
			}, nil
		}
	}

	if ic.devClassGetter == nil {
		ic.devClassGetter = &hardware.MockNetDevClassProvider{
			GetNetDevClassReturn: []hardware.MockGetNetDevClassResult{
				{
					NDC: hardware.Ether,
				},
			},
		}
	}

	if ic.devStateGetter == nil {
		ic.devStateGetter = &hardware.MockNetDevStateProvider{
			GetStateReturn: []hardware.MockNetDevStateResult{
				{
					State: hardware.NetDevStateReady,
				},
			},
		}
	}

	if params.disableAttachInfoCache {
		ic.DisableAttachInfoCache()
	} else {
		ic.EnableAttachInfoCache(0)
	}
	if params.disableFabricCache {
		ic.DisableFabricCache()
	} else {
		ic.EnableFabricCache()
	}
	return ic
}

func TestAgent_newCachedAttachInfo(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	expSys := "my_system"
	expRefreshInterval := time.Second
	expClient := control.NewMockInvoker(log, &control.MockInvokerConfig{})

	ai := newCachedAttachInfo(expRefreshInterval, expSys, expClient,
		func(ctx context.Context, rpcClient control.UnaryInvoker, req *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error) {
			return nil, nil
		})

	test.AssertEqual(t, expSys, ai.system, "")
	test.AssertEqual(t, expRefreshInterval, ai.refreshInterval, "")
	test.AssertEqual(t, expClient, ai.rpcClient, "")
	test.AssertEqual(t, time.Time{}, ai.lastCached, "")
	if ai.lastResponse != nil {
		t.Fatalf("expected nothing cached, found:\n%+v", ai.lastResponse)
	}
	if ai.fetch == nil {
		t.Fatalf("expected refresh function to be non-nil")
	}
}

func TestAgent_cachedAttachInfo_Key(t *testing.T) {
	for name, tc := range map[string]struct {
		ai        *cachedAttachInfo
		expResult string
	}{
		"nil": {},
		"no system name": {
			ai:        newCachedAttachInfo(0, "", nil, nil),
			expResult: "GetAttachInfo",
		},
		"system name": {
			ai:        newCachedAttachInfo(0, "my_system", nil, nil),
			expResult: "GetAttachInfo-my_system",
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.ai.Key(), "")
		})
	}
}

func TestAgent_cachedAttachInfo_NeedsRefresh(t *testing.T) {
	for name, tc := range map[string]struct {
		ai        *cachedAttachInfo
		expResult bool
	}{
		"nil": {},
		"never cached": {
			ai:        newCachedAttachInfo(0, "test", nil, nil),
			expResult: true,
		},
		"no refresh": {
			ai: &cachedAttachInfo{
				cacheItem: cacheItem{
					lastCached: time.Now().Add(-time.Minute),
				},
				lastResponse: &control.GetAttachInfoResp{},
			},
		},
		"expired": {
			ai: &cachedAttachInfo{
				cacheItem: cacheItem{
					lastCached:      time.Now().Add(-time.Minute),
					refreshInterval: time.Second,
				},
				lastResponse: &control.GetAttachInfoResp{},
			},
			expResult: true,
		},
		"not expired": {
			ai: &cachedAttachInfo{
				cacheItem: cacheItem{
					lastCached:      time.Now().Add(-time.Second),
					refreshInterval: time.Minute,
				},
				lastResponse: &control.GetAttachInfoResp{},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.ai.NeedsRefresh(), "")
		})
	}
}

func TestAgent_cachedAttachInfo_Refresh(t *testing.T) {
	resp1 := &control.GetAttachInfoResp{
		System: "resp1",
		ServiceRanks: []*control.PrimaryServiceRank{
			{
				Rank: 1,
				Uri:  "rank one",
			},
			{
				Rank: 2,
				Uri:  "rank two",
			},
		},
		MSRanks: []uint32{0, 1, 2},
		ClientNetHint: control.ClientNetworkHint{
			Provider:    "prov",
			NetDevClass: uint32(hardware.Ether),
		},
	}

	resp2 := &control.GetAttachInfoResp{
		System: "resp2",
		ServiceRanks: []*control.PrimaryServiceRank{
			{
				Rank: 3,
				Uri:  "rank three",
			},
			{
				Rank: 4,
				Uri:  "rank four",
			},
		},
		MSRanks: []uint32{1, 3},
		ClientNetHint: control.ClientNetworkHint{
			Provider:    "other",
			NetDevClass: uint32(hardware.Infiniband),
		},
	}

	for name, tc := range map[string]struct {
		nilCache      bool
		ctlResult     *control.GetAttachInfoResp
		ctlErr        error
		alreadyCached *control.GetAttachInfoResp
		expErr        error
		expCached     *control.GetAttachInfoResp
	}{
		"nil": {
			nilCache: true,
			expErr:   errors.New("nil"),
		},
		"GetAttachInfo fails": {
			ctlErr: errors.New("mock GetAttachInfo"),
			expErr: errors.New("mock GetAttachInfo"),
		},
		"not initialized": {
			ctlResult: resp1,
			expCached: resp1,
		},
		"previously cached": {
			ctlResult:     resp2,
			alreadyCached: resp1,
			expCached:     resp2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var ai *cachedAttachInfo
			if !tc.nilCache {
				ai = newCachedAttachInfo(0, "test", control.DefaultClient(),
					func(_ context.Context, _ control.UnaryInvoker, _ *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error) {
						return tc.ctlResult, tc.ctlErr
					})
				ai.lastResponse = tc.alreadyCached
				if ai.lastResponse != nil {
					ai.lastCached = time.Now()
				}
			}

			err := ai.Refresh(test.Context(t))

			test.CmpErr(t, tc.expErr, err)

			if ai == nil {
				return
			}

			if diff := cmp.Diff(tc.expCached, ai.lastResponse); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestAgent_newCachedFabricInfo(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	cfi := newCachedFabricInfo(log, func(ctx context.Context, providers ...string) (*NUMAFabric, error) {
		return nil, nil
	})

	test.AssertEqual(t, time.Duration(0), cfi.refreshInterval, "")
	test.AssertEqual(t, time.Time{}, cfi.lastCached, "")
	if cfi.lastResults != nil {
		t.Fatalf("expected nothing cached, found:\n%+v", cfi.lastResults)
	}
	if cfi.fetch == nil {
		t.Fatalf("expected refresh function to be non-nil")
	}
}

func TestAgent_cachedFabricInfo_Key(t *testing.T) {
	for name, tc := range map[string]struct {
		cfi *cachedFabricInfo
	}{
		"nil": {},
		"normal": {
			cfi: newCachedFabricInfo(nil, nil),
		},
	} {
		t.Run(name, func(t *testing.T) {
			// should always be the same
			test.AssertEqual(t, fabricKey, tc.cfi.Key(), "")
		})
	}
}

func TestAgent_cachedFabricInfo_NeedsRefresh(t *testing.T) {
	for name, tc := range map[string]struct {
		nilCache  bool
		cacheTime time.Time
		expResult bool
	}{
		"nil": {
			nilCache: true,
		},
		"not initialized": {
			expResult: true,
		},
		"initialized": {
			cacheTime: time.Now().Add(-time.Minute),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var cfi *cachedFabricInfo
			if !tc.nilCache {
				cfi = newCachedFabricInfo(log, nil)
				cfi.cacheItem.lastCached = tc.cacheTime
			}

			test.AssertEqual(t, tc.expResult, cfi.NeedsRefresh(), "")
		})
	}
}

func TestAgent_cachedFabricInfo_Refresh(t *testing.T) {
	scan1 := map[int][]*FabricInterface{
		2: {
			{Name: "two"},
		},
	}
	scan2 := map[int][]*FabricInterface{
		1: {
			{Name: "one"},
		},
		3: {
			{Name: "three"},
		},
	}

	for name, tc := range map[string]struct {
		nilCache      bool
		disabled      bool
		fabricResult  map[int][]*FabricInterface
		fabricErr     error
		alreadyCached map[int][]*FabricInterface
		expErr        error
		expCached     map[int][]*FabricInterface
	}{
		"nil": {
			nilCache: true,
			expErr:   errors.New("nil"),
		},
		"fabric scan fails": {
			fabricErr: errors.New("mock fabric scan"),
			expErr:    errors.New("mock fabric scan"),
		},
		"not initialized": {
			fabricResult: scan1,
			expCached:    scan1,
		},
		"previously cached": {
			fabricResult:  scan2,
			alreadyCached: scan1,
			expCached:     scan2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var cfi *cachedFabricInfo
			if !tc.nilCache {
				cfi = newCachedFabricInfo(log, nil)
				cfi.fetch = func(_ context.Context, _ ...string) (*NUMAFabric, error) {
					if tc.fabricResult != nil {
						return &NUMAFabric{
							numaMap: tc.fabricResult,
						}, nil
					}
					return nil, tc.fabricErr
				}
				if tc.alreadyCached != nil {
					cfi.lastResults = &NUMAFabric{
						numaMap: tc.alreadyCached,
					}
					cfi.lastCached = time.Now()
				}
			}

			err := cfi.Refresh(test.Context(t))

			test.CmpErr(t, tc.expErr, err)

			if cfi == nil {
				return
			}

			if tc.expCached == nil {
				if cfi.lastResults != nil {
					t.Fatalf("expected empty cache, got %+v", cfi.lastResults)
				}
				return
			}

			if diff := cmp.Diff(tc.expCached, cfi.lastResults.numaMap, cmpopts.IgnoreUnexported(FabricInterface{})); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestAgent_NewInfoCache(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg                *Config
		expEnabled         bool
		expIgnoredIfaces   common.StringSet
		expRefreshInterval time.Duration
	}{
		"default": {
			cfg:        &Config{},
			expEnabled: true,
		},
		"caches disabled": {
			cfg: &Config{
				DisableCache: true,
			},
		},
		"ignored interfaces": {
			cfg: &Config{
				ExcludeFabricIfaces: common.NewStringSet("eth0", "eth1"),
			},
			expEnabled:       true,
			expIgnoredIfaces: common.NewStringSet("eth0", "eth1"),
		},
		"refresh interval": {
			cfg: &Config{
				CacheExpiration: refreshMinutes(5 * time.Minute),
			},
			expEnabled:         true,
			expRefreshInterval: 5 * time.Minute,
		},
		"fabric interfaces": {
			cfg: &Config{
				FabricInterfaces: []*NUMAFabricConfig{
					{
						NUMANode: 1,
						Interfaces: []*FabricInterfaceConfig{
							{
								Interface: "if0",
								Domain:    "d0",
							},
						},
					},
				},
			},
			expEnabled: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ic := NewInfoCache(test.Context(t), log, nil, tc.cfg)

			test.AssertEqual(t, tc.expEnabled, ic.IsAttachInfoCacheEnabled(), "")
			test.AssertEqual(t, tc.expEnabled, ic.IsFabricCacheEnabled(), "")

			test.AssertEqual(t, tc.expIgnoredIfaces, ic.ignoreIfaces, "")
			test.AssertEqual(t, tc.expRefreshInterval, ic.attachInfoRefresh, "")
		})
	}
}

func TestAgent_InfoCache_EnableAttachInfoCache(t *testing.T) {
	for name, tc := range map[string]struct {
		ic              *InfoCache
		refreshInterval time.Duration
		expEnabled      bool
	}{
		"nil": {},
		"disabled": {
			ic:         newTestInfoCache(t, nil, testInfoCacheParams{disableAttachInfoCache: true}),
			expEnabled: true,
		},
		"already enabled": {
			ic:         newTestInfoCache(t, nil, testInfoCacheParams{}),
			expEnabled: true,
		},
		"refresh interval": {
			ic:              newTestInfoCache(t, nil, testInfoCacheParams{disableAttachInfoCache: true}),
			refreshInterval: time.Minute,
			expEnabled:      true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.ic.EnableAttachInfoCache(tc.refreshInterval)

			test.AssertEqual(t, tc.expEnabled, tc.ic.IsAttachInfoCacheEnabled(), "")
		})
	}
}

func TestAgent_InfoCache_DisableAttachInfoCache(t *testing.T) {
	for name, tc := range map[string]struct {
		ic *InfoCache
	}{
		"nil": {},
		"already disabled": {
			ic: newTestInfoCache(t, nil, testInfoCacheParams{disableAttachInfoCache: true}),
		},
		"enabled": {
			ic: newTestInfoCache(t, nil, testInfoCacheParams{}),
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.ic.DisableAttachInfoCache()

			test.AssertFalse(t, tc.ic.IsAttachInfoCacheEnabled(), "")
		})
	}
}

func TestAgent_InfoCache_EnableFabricCache(t *testing.T) {
	for name, tc := range map[string]struct {
		ic           *InfoCache
		startEnabled bool
		expEnabled   bool
	}{
		"nil": {},
		"disabled": {
			ic:         newTestInfoCache(t, nil, testInfoCacheParams{disableFabricCache: true}),
			expEnabled: true,
		},
		"already enabled": {
			ic:           newTestInfoCache(t, nil, testInfoCacheParams{}),
			startEnabled: true,
			expEnabled:   true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.startEnabled, tc.ic.IsFabricCacheEnabled(), "")

			tc.ic.EnableFabricCache()

			test.AssertEqual(t, tc.expEnabled, tc.ic.IsFabricCacheEnabled(), "")
		})
	}
}

func TestAgent_InfoCache_EnableStaticFabricCache(t *testing.T) {
	cfg := []*NUMAFabricConfig{
		{
			NUMANode: 1,
			Interfaces: []*FabricInterfaceConfig{
				{
					Interface: "if0",
					Domain:    "if0",
				},
				{
					Interface: "if0",
					Domain:    "d0",
				},
			},
		},
	}

	for name, tc := range map[string]struct {
		ic           *InfoCache
		startEnabled bool
		expEnabled   bool
	}{
		"nil": {},
		"disabled": {
			ic:         newTestInfoCache(t, nil, testInfoCacheParams{disableFabricCache: true}),
			expEnabled: true,
		},
		"already enabled": {
			ic:           newTestInfoCache(t, nil, testInfoCacheParams{}),
			startEnabled: true,
			expEnabled:   true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			test.AssertEqual(t, tc.startEnabled, tc.ic.IsFabricCacheEnabled(), "")

			nf := NUMAFabricFromConfig(log, cfg)
			tc.ic.EnableStaticFabricCache(test.Context(t), nf)

			test.AssertEqual(t, tc.expEnabled, tc.ic.IsFabricCacheEnabled(), "")
			if tc.ic == nil {
				return
			}

			if tc.expEnabled {
				item, cleanup, err := tc.ic.cache.Get(test.Context(t), fabricKey)
				test.CmpErr(t, nil, err)
				defer cleanup()

				fabricCache, ok := item.(*cachedFabricInfo)
				if !ok {
					t.Fatalf("bad item type %T", item)
				}

				test.AssertEqual(t, time.Duration(0), fabricCache.refreshInterval, "expected no refresh")
				if diff := cmp.Diff(nf.numaMap, fabricCache.lastResults.numaMap, cmpopts.IgnoreUnexported(FabricInterface{})); diff != "" {
					t.Fatalf("-want, +got:\n%s", diff)
				}
			}
		})
	}
}

func TestAgent_InfoCache_DisableFabricCache(t *testing.T) {
	for name, tc := range map[string]struct {
		ic           *InfoCache
		startEnabled bool
	}{
		"nil": {},
		"already disabled": {
			ic: newTestInfoCache(t, nil, testInfoCacheParams{disableFabricCache: true}),
		},
		"enabled": {
			ic:           newTestInfoCache(t, nil, testInfoCacheParams{}),
			startEnabled: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.startEnabled, tc.ic.IsFabricCacheEnabled(), "")

			tc.ic.DisableFabricCache()

			test.AssertFalse(t, tc.ic.IsFabricCacheEnabled(), "")
		})
	}
}

func TestAgent_InfoCache_AddProvider(t *testing.T) {
	for name, tc := range map[string]struct {
		ic           *InfoCache
		input        string
		expProviders common.StringSet
	}{
		"nil": {
			input: "something",
		},
		"empty": {
			ic:           &InfoCache{},
			input:        "something",
			expProviders: common.NewStringSet("something"),
		},
		"add": {
			ic: &InfoCache{
				providers: common.NewStringSet("something"),
			},
			input:        "something else",
			expProviders: common.NewStringSet("something", "something else"),
		},
		"ignore empty string": {
			ic:    &InfoCache{},
			input: "",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.ic != nil {
				tc.ic.log = log
			}

			tc.ic.AddProvider(tc.input)

			if tc.ic == nil {
				return
			}
			if diff := cmp.Diff(tc.expProviders, tc.ic.providers); diff != "" {
				t.Fatalf("want-, got+:\n%s", diff)
			}
		})
	}
}

func TestAgent_InfoCache_GetAttachInfo(t *testing.T) {
	ctlResp := &control.GetAttachInfoResp{
		System:       "dontcare",
		ServiceRanks: []*control.PrimaryServiceRank{{Rank: 1, Uri: "my uri"}},
		MSRanks:      []uint32{0, 1, 2, 3},
		ClientNetHint: control.ClientNetworkHint{
			Provider:    "ofi+tcp",
			NetDevClass: uint32(hardware.Ether),
		},
	}

	for name, tc := range map[string]struct {
		getInfoCache func(logging.Logger) *InfoCache
		system       string
		remoteResp   *control.GetAttachInfoResp
		remoteErr    error
		expErr       error
		expResp      *control.GetAttachInfoResp
		expRemote    bool
		expCached    bool
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"disabled": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				return newTestInfoCache(t, l, testInfoCacheParams{
					disableAttachInfoCache: true,
				})
			},
			remoteResp: ctlResp,
			expResp:    ctlResp,
			expRemote:  true,
		},
		"disabled fails fetch": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				return newTestInfoCache(t, l, testInfoCacheParams{
					disableAttachInfoCache: true,
				})
			},
			remoteErr: errors.New("mock remote"),
			expErr:    errors.New("mock remote"),
			expRemote: true,
		},
		"enabled but empty": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				return newTestInfoCache(t, l, testInfoCacheParams{})
			},
			remoteResp: ctlResp,
			expResp:    ctlResp,
			expRemote:  true,
			expCached:  true,
		},
		"enabled but empty fails fetch": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				return newTestInfoCache(t, l, testInfoCacheParams{})
			},
			remoteErr: errors.New("mock remote"),
			expErr:    errors.New("mock remote"),
			expRemote: true,
		},
		"enabled and cached": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				ic := newTestInfoCache(t, l, testInfoCacheParams{})
				ic.cache.Set(&cachedAttachInfo{
					fetch: func(_ context.Context, _ control.UnaryInvoker, _ *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error) {
						return nil, errors.New("shouldn't call cached remote")
					},
					lastResponse: ctlResp,
					cacheItem:    cacheItem{lastCached: time.Now()},
					system:       "test",
				})
				return ic
			},
			system:    "test",
			remoteErr: errors.New("shouldn't call remote"),
			expResp:   ctlResp,
			expCached: true,
		},
		"default system": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				ic := newTestInfoCache(t, l, testInfoCacheParams{})
				ic.cache.Set(&cachedAttachInfo{
					fetch: func(_ context.Context, _ control.UnaryInvoker, _ *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error) {
						return nil, errors.New("shouldn't call cached remote")
					},
					lastResponse: ctlResp,
					cacheItem:    cacheItem{lastCached: time.Now()},
					system:       build.DefaultSystemName,
				})
				return ic
			},
			remoteErr: errors.New("shouldn't call remote"),
			expResp:   ctlResp,
			expCached: true,
		},
		"cache miss": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				ic := newTestInfoCache(t, l, testInfoCacheParams{})
				ic.cache.Set(&cachedAttachInfo{
					fetch: func(_ context.Context, _ control.UnaryInvoker, _ *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error) {
						return nil, errors.New("shouldn't call cached remote")
					},
					lastResponse: &control.GetAttachInfoResp{},
					cacheItem:    cacheItem{lastCached: time.Now()},
					system:       "test",
				})
				return ic
			},
			system:     "somethingelse",
			remoteResp: ctlResp,
			expResp:    ctlResp,
			expCached:  true,
			expRemote:  true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var ic *InfoCache
			if tc.getInfoCache != nil {
				ic = tc.getInfoCache(log)
			}

			calledRemote := false
			if ic != nil {
				ic.getAttachInfo = func(_ context.Context, _ control.UnaryInvoker, _ *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error) {
					calledRemote = true
					return tc.remoteResp, tc.remoteErr
				}
			}

			if tc.system == "" {
				tc.system = build.DefaultSystemName
			}
			resp, err := ic.GetAttachInfo(test.Context(t), tc.system)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResp, resp); diff != "" {
				t.Fatalf("want-, got+:\n%s", diff)
			}

			test.AssertEqual(t, tc.expRemote, calledRemote, "")

			if ic == nil {
				return
			}

			if tc.expCached && tc.expResp != nil {
				cachedItem, unlockItem, err := ic.cache.Get(test.Context(t), sysAttachInfoKey(tc.system))
				if err != nil {
					t.Fatal(err)
				}
				defer unlockItem()
				cached, ok := cachedItem.(*cachedAttachInfo)
				test.AssertTrue(t, ok, "wrong type cached")
				if diff := cmp.Diff(tc.expResp, cached.lastResponse); diff != "" {
					t.Fatalf("want-, got+:\n%s", diff)
				}
			}
		})
	}
}

func mockGetAddrInterface(name string) (addrFI, error) {
	return &mockNetInterface{
		addrs: []net.Addr{
			&net.IPNet{IP: net.IP{127, 0, 0, 1}},
		},
	}, nil
}

func TestAgent_InfoCache_GetFabricDevice(t *testing.T) {
	testSet := hardware.NewFabricInterfaceSet(&hardware.FabricInterface{
		Name:          "dev0",
		NetInterfaces: common.NewStringSet("test0"),
		DeviceClass:   hardware.Ether,
		Providers:     hardware.NewFabricProviderSet(&hardware.FabricProvider{Name: "testprov"}),
	})

	for name, tc := range map[string]struct {
		getInfoCache    func(logging.Logger) *InfoCache
		devClass        hardware.NetDevClass
		provider        string
		fabricResp      *hardware.FabricInterfaceSet
		fabricErr       error
		expResult       *FabricInterface
		expErr          error
		expScan         bool
		expCachedFabric *hardware.FabricInterfaceSet
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"disabled": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				return newTestInfoCache(t, l, testInfoCacheParams{
					disableFabricCache: true,
				})
			},
			devClass:   hardware.Ether,
			provider:   "testprov",
			fabricResp: testSet,
			expScan:    true,
			expResult: &FabricInterface{
				Name:        "test0",
				Domain:      "dev0",
				NetDevClass: hardware.Ether,
			},
		},
		"disabled fails fabric ready check": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				return newTestInfoCache(t, l, testInfoCacheParams{
					disableFabricCache: true,
					mockNetIfaces: func() ([]net.Interface, error) {
						return nil, errors.New("mock net ifaces")
					},
				})
			},
			devClass:  hardware.Ether,
			provider:  "testprov",
			fabricErr: errors.New("shouldn't call scan"),
			expErr:    errors.New("mock net ifaces"),
		},
		"disabled fails fetch": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				return newTestInfoCache(t, l, testInfoCacheParams{
					disableFabricCache: true,
				})
			},
			devClass:  hardware.Ether,
			provider:  "testprov",
			fabricErr: errors.New("mock fabric scan"),
			expScan:   true,
			expErr:    errors.New("mock fabric scan"),
		},
		"enabled but empty": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				return newTestInfoCache(t, l, testInfoCacheParams{})
			},
			devClass:   hardware.Ether,
			provider:   "testprov",
			fabricResp: testSet,
			expScan:    true,
			expResult: &FabricInterface{
				Name:        "test0",
				Domain:      "dev0",
				NetDevClass: hardware.Ether,
			},
			expCachedFabric: testSet,
		},
		"enabled but empty fails ready wait": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				return newTestInfoCache(t, l, testInfoCacheParams{
					mockNetIfaces: func() ([]net.Interface, error) {
						return nil, errors.New("mock net ifaces")
					},
				})
			},
			devClass:  hardware.Ether,
			provider:  "testprov",
			fabricErr: errors.New("shouldn't call scan"),
			expErr:    errors.New("mock net ifaces"),
		},
		"enabled but empty fails fetch": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				return newTestInfoCache(t, l, testInfoCacheParams{})
			},
			devClass:  hardware.Ether,
			provider:  "testprov",
			fabricErr: errors.New("mock fabric scan"),
			expScan:   true,
			expErr:    errors.New("mock fabric scan"),
		},
		"enabled and cached": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				ic := newTestInfoCache(t, l, testInfoCacheParams{})
				nf := NUMAFabricFromScan(test.Context(t), l, testSet)
				nf.getAddrInterface = mockGetAddrInterface
				ic.cache.Set(&cachedFabricInfo{
					fetch: func(ctx context.Context, providers ...string) (*NUMAFabric, error) {
						return nil, errors.New("shouldn't call cached fetch")
					},
					lastResults: nf,
					cacheItem:   cacheItem{lastCached: time.Now()},
				})
				return ic
			},
			devClass:  hardware.Ether,
			provider:  "testprov",
			fabricErr: errors.New("shouldn't call scan"),
			expResult: &FabricInterface{
				Name:        "test0",
				Domain:      "dev0",
				NetDevClass: hardware.Ether,
			},
			expCachedFabric: hardware.NewFabricInterfaceSet(&hardware.FabricInterface{
				Name:          "dev0",
				NetInterfaces: common.NewStringSet("test0"),
				DeviceClass:   hardware.Ether,
				Providers:     hardware.NewFabricProviderSet(&hardware.FabricProvider{Name: "testprov"}),
			}),
		},
		"requested not found": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				ic := newTestInfoCache(t, l, testInfoCacheParams{})
				nf := NUMAFabricFromScan(test.Context(t), l, testSet)
				nf.getAddrInterface = mockGetAddrInterface
				ic.cache.Set(&cachedFabricInfo{
					fetch: func(ctx context.Context, providers ...string) (*NUMAFabric, error) {
						return nil, errors.New("shouldn't call cached fetch")
					},
					lastResults: nf,
					cacheItem:   cacheItem{lastCached: time.Now()},
				})
				return ic
			},
			devClass:  hardware.Ether,
			provider:  "bad",
			fabricErr: errors.New("shouldn't call scan"),
			expErr:    errors.New("no suitable fabric interface"),
			expCachedFabric: hardware.NewFabricInterfaceSet(&hardware.FabricInterface{
				Name:          "dev0",
				NetInterfaces: common.NewStringSet("test0"),
				DeviceClass:   hardware.Ether,
				Providers:     hardware.NewFabricProviderSet(&hardware.FabricProvider{Name: "testprov"}),
			}),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var ic *InfoCache
			if tc.getInfoCache != nil {
				ic = tc.getInfoCache(log)
			}

			calledScan := false
			if ic != nil {
				ic.fabricScan = func(_ context.Context, _ ...string) (*NUMAFabric, error) {
					calledScan = true
					if tc.fabricResp != nil {
						nf := NUMAFabricFromScan(test.Context(t), log, tc.fabricResp)
						nf.getAddrInterface = mockGetAddrInterface
						return nf, nil
					}
					return nil, tc.fabricErr
				}
			}

			result, err := ic.GetFabricDevice(test.Context(t), 0, tc.devClass, tc.provider)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result, cmpopts.IgnoreUnexported(FabricInterface{})); diff != "" {
				t.Fatalf("want-, got+:\n%s", diff)
			}

			test.AssertEqual(t, tc.expScan, calledScan, "")

			if ic == nil {
				return
			}

			if tc.expCachedFabric != nil {
				data, unlock, err := ic.cache.Get(test.Context(t), fabricKey)
				if err != nil {
					t.Fatal(err)
				}
				defer unlock()

				cached, ok := data.(*cachedFabricInfo)
				test.AssertTrue(t, ok, "bad cached data type")

				expNF := NUMAFabricFromScan(test.Context(t), log, tc.expCachedFabric)
				if diff := cmp.Diff(expNF.numaMap, cached.lastResults.numaMap, cmpopts.IgnoreUnexported(FabricInterface{})); diff != "" {
					t.Fatalf("want-, got+:\n%s", diff)
				}
			}
		})
	}
}

func TestAgent_InfoCache_Refresh(t *testing.T) {
	ctlResp := &control.GetAttachInfoResp{
		System:       "dontcare",
		ServiceRanks: []*control.PrimaryServiceRank{{Rank: 1, Uri: "my uri"}},
		MSRanks:      []uint32{0, 1, 2, 3},
		ClientNetHint: control.ClientNetworkHint{
			Provider:    "ofi+tcp",
			NetDevClass: uint32(hardware.Ether),
		},
	}

	testSet := hardware.NewFabricInterfaceSet(&hardware.FabricInterface{
		Name:          "dev0",
		NetInterfaces: common.NewStringSet("test0"),
		DeviceClass:   hardware.Ether,
		Providers:     hardware.NewFabricProviderSet(&hardware.FabricProvider{Name: "testprov"}),
	})

	testSys := "test_sys"

	for name, tc := range map[string]struct {
		getInfoCache        func(logging.Logger) *InfoCache
		expErr              error
		expCachedFabric     *hardware.FabricInterfaceSet
		expCachedAttachInfo *control.GetAttachInfoResp
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"both disabled": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				return newTestInfoCache(t, l, testInfoCacheParams{
					disableFabricCache:     true,
					disableAttachInfoCache: true,
				})
			},
			expErr: errors.New("disabled"),
		},
		"both enabled, cache items not created": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				return newTestInfoCache(t, l, testInfoCacheParams{})
			},
		},
		"both enabled, cache items exist": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				return newTestInfoCache(t, l, testInfoCacheParams{
					cachedItems: []cache.Item{
						newCachedAttachInfo(0, testSys, nil,
							func(_ context.Context, _ control.UnaryInvoker, _ *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error) {
								return ctlResp, nil
							}),
						newCachedFabricInfo(l,
							func(_ context.Context, _ ...string) (*NUMAFabric, error) {
								return NUMAFabricFromScan(test.Context(t), l, testSet), nil
							}),
					},
				})
			},
			expCachedFabric:     testSet,
			expCachedAttachInfo: ctlResp,
		},
		"fabric disabled": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				return newTestInfoCache(t, l, testInfoCacheParams{
					disableFabricCache: true,
					cachedItems: []cache.Item{
						newCachedAttachInfo(0, testSys, nil,
							func(_ context.Context, _ control.UnaryInvoker, _ *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error) {
								return ctlResp, nil
							}),
						newCachedFabricInfo(l,
							func(_ context.Context, _ ...string) (*NUMAFabric, error) {
								return nil, errors.New("shouldn't call fabric")
							}),
					},
				})
			},
			expCachedAttachInfo: ctlResp,
		},
		"attach info disabled": {
			getInfoCache: func(l logging.Logger) *InfoCache {
				return newTestInfoCache(t, l, testInfoCacheParams{
					disableAttachInfoCache: true,
					cachedItems: []cache.Item{
						newCachedAttachInfo(0, testSys, nil,
							func(_ context.Context, _ control.UnaryInvoker, _ *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error) {
								return nil, errors.New("shouldn't call GetAttachInfo")
							}),
						newCachedFabricInfo(l,
							func(_ context.Context, _ ...string) (*NUMAFabric, error) {
								return NUMAFabricFromScan(test.Context(t), l, testSet), nil
							}),
					},
				})
			},
			expCachedFabric: testSet,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var ic *InfoCache
			if tc.getInfoCache != nil {
				ic = tc.getInfoCache(log)
			}

			err := ic.Refresh(test.Context(t))

			test.CmpErr(t, tc.expErr, err)

			if tc.expCachedFabric != nil {
				data, unlock, err := ic.cache.Get(test.Context(t), fabricKey)
				if err != nil {
					t.Fatal(err)
				}
				defer unlock()

				cached, ok := data.(*cachedFabricInfo)
				test.AssertTrue(t, ok, "bad cached data type")

				expNF := NUMAFabricFromScan(test.Context(t), log, tc.expCachedFabric)
				if diff := cmp.Diff(expNF.numaMap, cached.lastResults.numaMap, cmpopts.IgnoreUnexported(FabricInterface{})); diff != "" {
					t.Fatalf("want-, got+:\n%s", diff)
				}
			}

			if tc.expCachedAttachInfo != nil {
				data, unlock, err := ic.cache.Get(test.Context(t), sysAttachInfoKey(testSys))
				if err != nil {
					t.Fatal(err)
				}
				defer unlock()

				cached, ok := data.(*cachedAttachInfo)
				test.AssertTrue(t, ok, "bad cached data type")

				if diff := cmp.Diff(tc.expCachedAttachInfo, cached.lastResponse); diff != "" {
					t.Fatalf("want-, got+:\n%s", diff)
				}
			}
		})
	}
}

func TestAgent_InfoCache_waitFabricReady(t *testing.T) {
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
			defer test.ShowBufferOnFailure(t, buf)

			if tc.netIfacesFn == nil {
				tc.netIfacesFn = defaultNetIfaceFn
			}

			if tc.devClassProv == nil {
				tc.devClassProv = defaultDevClassProv
			}

			if tc.devStateProv == nil {
				tc.devStateProv = &hardware.MockNetDevStateProvider{}
			}

			ic := &InfoCache{
				log:            log,
				netIfaces:      tc.netIfacesFn,
				devClassGetter: tc.devClassProv,
				devStateGetter: tc.devStateProv,
			}

			err := ic.waitFabricReady(test.Context(t), tc.netDevClass)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expChecked, tc.devStateProv.GetStateCalled); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}
