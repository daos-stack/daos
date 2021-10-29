//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestAgent_newAttachInfoCache(t *testing.T) {
	for name, tc := range map[string]struct {
		enabled bool
	}{
		"enabled": {
			enabled: true,
		},
		"disabled": {
			enabled: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cache := newAttachInfoCache(log, tc.enabled)

			if cache == nil {
				t.Fatal("expected non-nil cache")
			}

			common.AssertEqual(t, log, cache.log, "")
			common.AssertEqual(t, tc.enabled, cache.isEnabled(), "isEnabled()")
			common.AssertFalse(t, cache.isCached(), "default state is uncached")
		})
	}
}

func TestAgent_attachInfoCache_Get(t *testing.T) {
	srvResp := &mgmtpb.GetAttachInfoResp{
		Status: -1000,
		RankUris: []*mgmtpb.GetAttachInfoResp_RankUri{
			{Rank: 1, Uri: "firsturi"},
			{Rank: 2, Uri: "nexturi"},
		},
	}

	for name, tc := range map[string]struct {
		aic       *attachInfoCache
		cache     *mgmtpb.GetAttachInfoResp
		expCached bool
		expRemote bool
		remoteErr bool
		expErr    error
	}{
		"not enabled": {
			aic:       &attachInfoCache{},
			expRemote: true,
		},
		"not cached": {
			aic:       &attachInfoCache{enabled: atm.NewBool(true)},
			expRemote: true,
			expCached: true,
		},
		"cached": {
			aic:       &attachInfoCache{enabled: atm.NewBool(true)},
			cache:     srvResp,
			expCached: true,
		},
		"remote fails": {
			aic:       &attachInfoCache{enabled: atm.NewBool(true)},
			expRemote: true,
			remoteErr: true,
			expErr:    errors.New("no soup for you"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.cache != nil {
				tc.aic.attachInfo = tc.cache
				tc.aic.initialized.SetTrue()
			}

			if tc.aic == nil {
				return
			}

			numaNode := 42
			sysName := "snekSezSyss"
			remoteInvoked := atm.NewBool(false)
			getFn := func(_ context.Context, node int, name string) (*mgmtpb.GetAttachInfoResp, error) {
				common.AssertEqual(t, numaNode, node, "node was not supplied")
				common.AssertEqual(t, sysName, name, "name was not supplied")

				remoteInvoked.SetTrue()
				if tc.remoteErr {
					return nil, tc.expErr
				}
				return srvResp, nil
			}

			cachedResp, gotErr := tc.aic.Get(context.Background(), numaNode, sysName, getFn)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(srvResp, cachedResp, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}

			common.AssertEqual(t, tc.expCached, tc.aic.isCached(), "cache state")
			common.AssertEqual(t, tc.expRemote, remoteInvoked.Load(), "remote invoked")
		})
	}
}

func TestAgent_newLocalFabricCache(t *testing.T) {
	for name, tc := range map[string]struct {
		enabled bool
	}{
		"enabled": {
			enabled: true,
		},
		"disabled": {},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cache := newLocalFabricCache(log, tc.enabled)

			if cache == nil {
				t.Fatal("expected non-nil cache")
			}

			common.AssertEqual(t, log, cache.log, "")
			common.AssertFalse(t, cache.IsCached(), "default state is uncached")
			common.AssertEqual(t, tc.enabled, cache.IsEnabled(), "")
		})
	}
}

func newTestFabricCache(t *testing.T, log logging.Logger, cacheMap *NUMAFabric) *localFabricCache {
	t.Helper()

	cache := newLocalFabricCache(log, true)
	if cache == nil {
		t.Fatalf("nil cache")
	}
	cache.localNUMAFabric = cacheMap
	cache.initialized.SetTrue()

	cache.localNUMAFabric.getAddrInterface = getMockNetInterfaceSuccess

	return cache
}

func TestAgent_localFabricCache_IsEnabled(t *testing.T) {
	for name, tc := range map[string]struct {
		fic        *localFabricCache
		expEnabled bool
	}{
		"nil": {},
		"not enabled": {
			fic: &localFabricCache{},
		},
		"enabled": {
			fic:        &localFabricCache{enabled: atm.NewBool(true)},
			expEnabled: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			enabled := tc.fic.IsEnabled()

			common.AssertEqual(t, tc.expEnabled, enabled, "IsEnabled()")
		})
	}
}

func TestAgent_localFabricCache_CacheScan(t *testing.T) {
	for name, tc := range map[string]struct {
		lfc       *localFabricCache
		input     []*netdetect.FabricScan
		expCached bool
		expResult *NUMAFabric
	}{
		"nil": {},
		"disabled": {
			lfc: newLocalFabricCache(nil, false),
		},
		"no devices in scan": {
			lfc:       newLocalFabricCache(nil, true),
			expCached: true,
			expResult: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{},
			},
		},
		"successfully cached": {
			lfc: newLocalFabricCache(nil, true),
			input: []*netdetect.FabricScan{
				{
					Provider:    "ofi+sockets",
					DeviceName:  "test0",
					NUMANode:    1,
					NetDevClass: netdetect.Ether,
				},
				{
					Provider:   "ofi+sockets",
					DeviceName: "lo",
					NUMANode:   1,
				},
				{
					Provider:    "ofi+verbs",
					DeviceName:  "test1",
					NUMANode:    0,
					NetDevClass: netdetect.Infiniband,
				},
				{
					Provider:    "ofi+sockets",
					DeviceName:  "test2",
					NUMANode:    0,
					NetDevClass: netdetect.Ether,
				},
			},
			expCached: true,
			expResult: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "test1",
							NetDevClass: netdetect.Infiniband,
						},
						{
							Name:        "test2",
							NetDevClass: netdetect.Ether,
						},
					},
					1: {
						{
							Name:        "test0",
							NetDevClass: netdetect.Ether,
						},
					},
				},
			},
		},
		"with device alias": {
			lfc: &localFabricCache{
				enabled: atm.NewBool(true),
				getDevAlias: func(_ context.Context, dev string) (string, error) {
					return dev + "_alias", nil
				},
			},
			input: []*netdetect.FabricScan{
				{
					Provider:    "ofi+sockets",
					DeviceName:  "test0",
					NUMANode:    2,
					NetDevClass: netdetect.Ether,
				},
				{
					Provider:    "ofi+verbs",
					DeviceName:  "test1",
					NUMANode:    1,
					NetDevClass: netdetect.Infiniband,
				},
				{
					Provider:    "ofi+sockets",
					DeviceName:  "test2",
					NUMANode:    1,
					NetDevClass: netdetect.Ether,
				},
			},
			expCached: true,
			expResult: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					1: {
						{
							Name:        "test1",
							NetDevClass: netdetect.Infiniband,
							Domain:      "test1_alias",
						},
						{
							Name:        "test2",
							NetDevClass: netdetect.Ether,
							Domain:      "test2_alias",
						},
					},
					2: {
						{
							Name:        "test0",
							NetDevClass: netdetect.Ether,
							Domain:      "test0_alias",
						},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.lfc != nil {
				tc.lfc.log = log
			}

			tc.lfc.CacheScan(context.TODO(), tc.input)

			common.AssertEqual(t, tc.expCached, tc.lfc.IsCached(), "IsCached()")

			if tc.lfc == nil {
				return
			}

			if tc.expCached {
				if diff := cmp.Diff(tc.expResult.numaMap, tc.lfc.localNUMAFabric.numaMap); diff != "" {
					t.Fatalf("-want, +got:\n%s", diff)
				}
			} else if len(tc.lfc.localNUMAFabric.numaMap) > 0 {
				t.Fatalf("expected nothing cached, found: %+v", tc.lfc.localNUMAFabric.numaMap)
			}
		})
	}
}

func TestAgent_localFabricCache_Cache(t *testing.T) {
	for name, tc := range map[string]struct {
		lfc       *localFabricCache
		input     *NUMAFabric
		expCached bool
	}{
		"nil": {},
		"nil NUMAFabric": {
			lfc: newLocalFabricCache(nil, true),
		},
		"no NUMA nodes": {
			lfc: newLocalFabricCache(nil, true),
			input: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{},
			},
			expCached: true,
		},
		"successfully cached": {
			lfc: newLocalFabricCache(nil, true),
			input: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{
					0: {
						{
							Name:        "test1",
							NetDevClass: netdetect.Infiniband,
						},
						{
							Name:        "test2",
							NetDevClass: netdetect.Ether,
						},
					},
					1: {
						{
							Name:        "test0",
							NetDevClass: netdetect.Ether,
						},
					},
				},
			},
			expCached: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.lfc != nil {
				tc.lfc.log = log
			}

			tc.lfc.Cache(context.TODO(), tc.input)

			common.AssertEqual(t, tc.expCached, tc.lfc.IsCached(), "IsCached()")

			if tc.lfc == nil {
				return
			}

			if tc.lfc.localNUMAFabric == nil {
				t.Fatal("NUMAFabric in cache is nil")
			}

			if tc.expCached {
				if diff := cmp.Diff(tc.input.numaMap, tc.lfc.localNUMAFabric.numaMap); diff != "" {
					t.Fatalf("-want, +got:\n%s", diff)
				}
			} else if len(tc.lfc.localNUMAFabric.numaMap) > 0 {
				t.Fatalf("expected nothing cached, got: %+v", tc.lfc.localNUMAFabric.numaMap)
			}
		})
	}
}

func TestAgent_localFabricCache_GetDevice(t *testing.T) {
	populatedCache := &NUMAFabric{
		numaMap: map[int][]*FabricInterface{
			0: {
				{
					Name:        "test1",
					NetDevClass: netdetect.Infiniband,
					Domain:      "test1_alias",
				},
				{
					Name:        "test2",
					NetDevClass: netdetect.Ether,
					Domain:      "test2_alias",
				},
			},
			1: {
				{
					Name:        "test3",
					NetDevClass: netdetect.Infiniband,
					Domain:      "test3_alias",
				},
				{
					Name:        "test4",
					NetDevClass: netdetect.Infiniband,
					Domain:      "test4_alias",
				},
				{
					Name:        "test5",
					NetDevClass: netdetect.Ether,
					Domain:      "test5_alias",
				},
			},
			2: {
				{
					Name:        "test6",
					NetDevClass: netdetect.Ether,
					Domain:      "test6_alias",
				},
				{
					Name:        "test7",
					NetDevClass: netdetect.Ether,
					Domain:      "test7_alias",
				},
			},
		},
	}

	for name, tc := range map[string]struct {
		lfc         *localFabricCache
		numaNode    int
		netDevClass uint32
		expDevice   *FabricInterface
		expErr      error
	}{
		"nil cache": {
			expErr: NotCachedErr,
		},
		"nothing cached": {
			lfc:    &localFabricCache{},
			expErr: NotCachedErr,
		},
		"success": {
			lfc:         newTestFabricCache(t, nil, populatedCache),
			numaNode:    1,
			netDevClass: netdetect.Ether,
			expDevice: &FabricInterface{
				Name:        "test5",
				NetDevClass: netdetect.Ether,
				Domain:      "test5_alias",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.lfc != nil {
				tc.lfc.log = log
				if tc.lfc.localNUMAFabric != nil {
					tc.lfc.localNUMAFabric.log = log
				}
			}

			dev, err := tc.lfc.GetDevice(tc.numaNode, tc.netDevClass)

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expDevice, dev); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}
