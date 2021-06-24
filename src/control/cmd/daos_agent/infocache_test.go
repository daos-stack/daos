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
			common.AssertEqual(t, tc.enabled, cache.IsEnabled(), "IsEnabled()")
			common.AssertFalse(t, cache.IsCached(), "default state is uncached")
			if cache.AttachInfo != nil {
				t.Fatalf("initial data should be nil, got %+v", cache.AttachInfo)
			}
		})
	}
}

func TestAgent_attachInfoCache_Cache(t *testing.T) {
	for name, tc := range map[string]struct {
		aic       *attachInfoCache
		input     *mgmtpb.GetAttachInfoResp
		expErr    error
		expCached bool
	}{
		"nil cache": {
			expErr: errors.New("nil attachInfoCache"),
		},
		"not enabled": {
			aic:    &attachInfoCache{},
			expErr: errors.New("not enabled"),
		},
		"nil input": {
			aic:    &attachInfoCache{enabled: atm.NewBool(true)},
			expErr: errors.New("nil input"),
		},
		"success": {
			aic: &attachInfoCache{enabled: atm.NewBool(true)},
			input: &mgmtpb.GetAttachInfoResp{
				Status: -1000,
				RankUris: []*mgmtpb.GetAttachInfoResp_RankUri{
					{Rank: 1, Uri: "firsturi"},
					{Rank: 2, Uri: "nexturi"},
				},
			},
			expCached: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			err := tc.aic.Cache(context.TODO(), tc.input)

			common.CmpErr(t, tc.expErr, err)
			common.AssertEqual(t, tc.expCached, tc.aic.IsCached(), "IsCached()")

			if tc.aic == nil {
				return
			}

			if diff := cmp.Diff(tc.input, tc.aic.AttachInfo, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestAgent_attachInfoCache_IsEnabled(t *testing.T) {
	for name, tc := range map[string]struct {
		aic        *attachInfoCache
		expEnabled bool
	}{
		"nil": {},
		"not enabled": {
			aic: &attachInfoCache{},
		},
		"enabled": {
			aic:        &attachInfoCache{enabled: atm.NewBool(true)},
			expEnabled: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			enabled := tc.aic.IsEnabled()

			common.AssertEqual(t, tc.expEnabled, enabled, "IsEnabled()")
		})
	}
}

func TestAgent_newLocalFabricCache(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	cache := newLocalFabricCache(log)

	if cache == nil {
		t.Fatal("expected non-nil cache")
	}

	common.AssertEqual(t, log, cache.log, "")
	common.AssertFalse(t, cache.IsCached(), "default state is uncached")
}

func newTestFabricCache(t *testing.T, log logging.Logger, cacheMap *NUMAFabric) *localFabricCache {
	t.Helper()

	cache := newLocalFabricCache(log)
	if cache == nil {
		t.Fatalf("nil cache")
	}
	cache.localNUMAFabric = cacheMap
	cache.initialized.SetTrue()

	return cache
}

func TestAgent_localFabricCache_Cache(t *testing.T) {
	for name, tc := range map[string]struct {
		lfc       *localFabricCache
		input     []*netdetect.FabricScan
		expErr    error
		expCached bool
		expResult *NUMAFabric
	}{
		"nil": {
			expErr: errors.New("nil localFabricCache"),
		},
		"no devices in scan": {
			lfc:       &localFabricCache{},
			expCached: true,
			expResult: &NUMAFabric{
				numaMap: map[int][]*FabricInterface{},
			},
		},
		"successfully cached": {
			lfc: &localFabricCache{},
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

			err := tc.lfc.Cache(context.TODO(), tc.input)

			common.CmpErr(t, tc.expErr, err)
			common.AssertEqual(t, tc.expCached, tc.lfc.IsCached(), "IsCached()")

			if tc.lfc == nil {
				return
			}

			if diff := cmp.Diff(tc.expResult.numaMap, tc.lfc.localNUMAFabric.numaMap); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
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
			expErr: errors.New("nil localFabricCache"),
		},
		"nothing cached": {
			lfc:    &localFabricCache{},
			expErr: errors.New("not cached"),
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
